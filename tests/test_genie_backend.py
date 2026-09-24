"""Native bridge framing and prompt boundaries; no model-accuracy claim."""
import importlib.util
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from urllib.error import HTTPError
from urllib.request import Request, urlopen

spec = importlib.util.spec_from_file_location("genie_backend", Path(__file__).resolve().parents[1] /
                                             "scripts/agent/serve_genie_backend.py")
backend = importlib.util.module_from_spec(spec)
spec.loader.exec_module(backend)


class WorkerProtocolTests(unittest.TestCase):
    def worker(self, code, timeout=3):
        return backend.NativeWorker([sys.executable, "-u", "-c", code], timeout=timeout)

    def assert_stopped(self, worker):
        self.assertFalse(worker.is_running())
        self.assertIsNotNone(worker.process.poll())
        self.assertFalse(worker.reader.is_alive())
        self.assertTrue(worker.process.stdin.closed)
        self.assertTrue(worker.process.stdout.closed)

    def test_mismatched_reply_stops_process_instead_of_reusing_stale_result(self):
        worker = self.worker("import json,sys,time; print(json.dumps({'ready':True})); "
                             "sys.stdin.readline(); print(json.dumps({'request_id':'old'})); time.sleep(5)")
        try:
            with self.assertRaisesRegex(RuntimeError, "identity mismatch"):
                worker.request({"op": "health"})
            self.assertIsNotNone(worker.process.poll())
        finally:
            worker.close()

    def test_timeout_stops_native_inference_before_another_request(self):
        worker = self.worker("import json,sys,time; print(json.dumps({'ready':True})); "
                             "sys.stdin.readline(); time.sleep(5)", timeout=1)
        try:
            with self.assertRaisesRegex(TimeoutError, "stopped"):
                worker.request({"op": "completion"})
            with self.assertRaisesRegex(RuntimeError, "no longer running"):
                worker.request({"op": "completion"})
        finally:
            worker.close()

    def test_unicode_content_and_native_error_leave_framing_intact(self):
        worker = self.worker("import json,sys\nprint(json.dumps({'ready':True}))\n"
                             "for line in sys.stdin:\n r=json.loads(line); "
                             "print(json.dumps({'request_id':r['request_id'], "
                             "'error':'bad input'} if r['op']=='bad' else r))\n")
        try:
            with self.assertRaisesRegex(ValueError, "bad input"):
                worker.request({"op": "bad"})
            self.assertEqual(worker.request({"op": "tokenize", "content": "Ø 30°C"})["content"], "Ø 30°C")
        finally:
            worker.close()

    def test_invalid_native_output_fails_closed_immediately(self):
        for output, expected in (("not json", "protocol failed"), ("[]", "Invalid native worker response")):
            with self.subTest(output=output):
                worker = self.worker("import json,sys,time; print(json.dumps({'ready':True})); "
                                     "sys.stdin.readline(); print(" + repr(output) + "); time.sleep(30)")
                try:
                    with self.assertRaisesRegex(RuntimeError, expected):
                        worker.request({"op": "health"})
                    self.assert_stopped(worker)
                    with self.assertRaisesRegex(RuntimeError, "no longer running"):
                        worker.request({"op": "health"})
                finally:
                    worker.close()

    def test_unexpected_eof_closes_streams_and_reader(self):
        worker = self.worker("import json,sys; print(json.dumps({'ready':True})); sys.stdin.readline()")
        try:
            with self.assertRaisesRegex(RuntimeError, "closed its output"):
                worker.request({"op": "health"})
            self.assert_stopped(worker)
        finally:
            worker.close()

    def test_unsolicited_output_overflow_stops_without_blocking_reader(self):
        worker = self.worker("import json,sys,time; print(json.dumps({'ready':True})); "
                             "sys.stdin.readline(); "
                             "[print(json.dumps({'unexpected':i})) for i in range(6)]; time.sleep(30)")
        try:
            # Trigger output without consuming any response, filling the bounded queue.
            worker.process.stdin.write('{}\n')
            worker.process.stdin.flush()
            self.assertTrue(worker._closed.wait(timeout=3))
            worker.close()
            self.assert_stopped(worker)
            self.assertIn("unsolicited responses", str(worker._fatal))
        finally:
            worker.close()

    def test_concurrent_close_is_idempotent_and_releases_streams(self):
        worker = self.worker("import json,time; print(json.dumps({'ready':True})); time.sleep(30)")
        errors = []
        def close():
            try:
                worker.close()
            except Exception as exc:
                errors.append(exc)
        closers = [threading.Thread(target=close) for _ in range(3)]
        try:
            for closer in closers:
                closer.start()
            for closer in closers:
                closer.join(timeout=5)
                self.assertFalse(closer.is_alive())
            self.assertEqual(errors, [])
            worker.close()
            self.assert_stopped(worker)
        finally:
            worker.close()

    def test_close_releases_a_request_waiting_on_native_inference(self):
        with tempfile.TemporaryDirectory() as directory:
            marker = Path(directory) / "received"
            worker = self.worker("import json,sys,time; from pathlib import Path; "
                "print(json.dumps({'ready':True})); sys.stdin.readline(); "
                "Path(" + repr(str(marker)) + ").touch(); time.sleep(30)", timeout=30)
            errors = []
            def request():
                try:
                    worker.request({"op": "completion"})
                except Exception as exc:
                    errors.append(exc)
            caller = threading.Thread(target=request)
            try:
                caller.start()
                deadline = time.monotonic() + 3
                while not marker.exists() and time.monotonic() < deadline:
                    time.sleep(0.01)
                self.assertTrue(marker.exists())
                worker.close()
                caller.join(timeout=3)
                self.assertFalse(caller.is_alive())
                self.assertEqual(len(errors), 1)
                self.assertIsInstance(errors[0], RuntimeError)
                self.assert_stopped(worker)
            finally:
                worker.close()
                caller.join(timeout=3)


@unittest.skipUnless(os.name == "posix", "SIGTERM lifecycle is exercised on the Linux deployment platform")
class HttpLifecycleTests(unittest.TestCase):
    def setUp(self):
        try:
            import jinja2  # noqa: F401
        except ImportError:
            self.skipTest("Genie template candidate requires Jinja2")
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.directory = Path(self.temp.name)
        self.pid_path = self.directory / "worker.pid"
        self.log_path = self.directory / "server.log"

    def start_server(self, ready=True, cache_mode="reset"):
        worker_path = self.directory / "fake_worker.py"
        worker_path.write_text("#!" + sys.executable + "\nimport json,os,sys,time\n"
            "from pathlib import Path\nPath(" + repr(str(self.pid_path)) + ").write_text(str(os.getpid()))\n"
            + ("time.sleep(30)\n" if not ready else "") +
            "print(json.dumps({'ready':True,'prefix_rewind_api_supported':True}),flush=True)\n"
            "for line in sys.stdin:\n"
            " r=json.loads(line)\n"
            " if r.get('content')=='break':\n"
            "  print('invalid json',flush=True); time.sleep(30)\n"
            " else:\n"
            "  print(json.dumps({'request_id':r['request_id'],'tokens':[1] if r.get('content')=='<s>' else [2],'received':r}),flush=True)\n",
            encoding="utf-8")
        worker_path.chmod(0o700)
        properties = self.directory / "properties.json"
        properties.write_text(json.dumps({"bos_token": "<s>", "eos_token": "</s>",
            "chat_template": "{{ bos_token }}{% for m in messages %}{{ m.content }}{% endfor %}"}), encoding="utf-8")
        with self.log_path.open("wb") as log:
            self.server = subprocess.Popen([sys.executable, str(Path(backend.__file__)),
                "--worker", str(worker_path), "--bundle-config", str(self.directory / "unused-config"),
                "--tokenizer", str(self.directory / "unused-tokenizer"), "--qairt-lib", str(self.directory),
                "--template-properties", str(properties), "--port", "0", "--cache-mode", cache_mode],
                stdout=log, stderr=log)
        self.addCleanup(self.stop_server)
        self.wait_for(lambda: self.pid_path.exists(), "fake worker PID")
        self.worker_pid = int(self.pid_path.read_text())
        if ready:
            self.wait_for(lambda: '"listening"' in self.log_path.read_text(), "HTTP startup")
            first_line = self.log_path.read_text().splitlines()[0]
            self.url = "http://127.0.0.1:" + str(json.loads(first_line)["listening"])

    def wait_for(self, condition, description):
        deadline = time.monotonic() + 5
        while not condition():
            if self.server.poll() is not None or time.monotonic() >= deadline:
                self.fail(description + ": " + self.log_path.read_text())
            time.sleep(0.01)

    def stop_server(self):
        if self.server.poll() is None:
            self.server.terminate()
            try:
                self.server.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.server.kill()
                self.server.wait(timeout=5)
        # A failed regression must not leave even its fake worker behind.
        if hasattr(self, "worker_pid"):
            try:
                os.kill(self.worker_pid, 9)
            except ProcessLookupError:
                pass

    def assert_worker_gone(self):
        with self.assertRaises(ProcessLookupError):
            os.kill(self.worker_pid, 0)

    def test_sigterm_reaps_native_worker_after_http_startup(self):
        self.start_server()
        self.server.terminate()
        self.assertEqual(self.server.wait(timeout=5), 0, self.log_path.read_text())
        self.assert_worker_gone()

    def test_sigterm_reaps_native_worker_during_native_startup(self):
        self.start_server(ready=False)
        self.server.terminate()
        self.assertEqual(self.server.wait(timeout=5), 0, self.log_path.read_text())
        self.assert_worker_gone()

    def test_broken_protocol_returns_gateway_error_and_unhealthy_status(self):
        self.start_server()
        request = Request(self.url + "/tokenize", data=b'{"content":"break"}',
                          headers={"Content-Type": "application/json"})
        with self.assertRaises(HTTPError) as caught:
            urlopen(request, timeout=5)
        with caught.exception as response:
            self.assertEqual(response.code, 502)
            self.assertIn("protocol failed", json.load(response)["error"])
        with self.assertRaises(HTTPError) as caught:
            urlopen(self.url + "/health", timeout=5)
        with caught.exception as response:
            self.assertEqual(response.code, 503)
            self.assertEqual(json.load(response)["status"], "stopped")
        self.assert_worker_gone()

    def test_completion_can_force_reset_on_append_server_without_changing_tokens(self):
        self.start_server(cache_mode="append")
        for fields, expected in (({}, "append"), ({"cache_prompt": True}, "append"),
                                 ({"cache_prompt": False, "cache_mode": "append"}, "reset")):
            with self.subTest(fields=fields):
                request = Request(self.url + "/completion",
                    data=json.dumps({"prompt": "exact rendered history", "n_predict": 1, **fields}).encode(),
                    headers={"Content-Type": "application/json"})
                with urlopen(request, timeout=5) as response:
                    received = json.load(response)["received"]
                self.assertEqual(received["cache_mode"], expected)
                self.assertEqual(received["tokens"], [1, 2])
                self.assertEqual(received["n_predict"], 1)
                self.assertNotIn("prompt", received)

    def test_completion_rejects_nonboolean_cache_prompt(self):
        self.start_server(cache_mode="append")
        for value in (None, 0, 1, "false", [], {}):
            with self.subTest(value=value):
                request = Request(self.url + "/completion",
                    data=json.dumps({"prompt": "exact rendered history", "cache_prompt": value}).encode(),
                    headers={"Content-Type": "application/json"})
                with self.assertRaises(HTTPError) as caught:
                    urlopen(request, timeout=5)
                with caught.exception as response:
                    self.assertEqual(response.code, 400)
                    self.assertIn("cache_prompt must be a boolean", json.load(response)["error"])

    def test_health_identifies_backend_source_and_selected_rewind_mode(self):
        self.start_server(cache_mode="append_rewind")
        with urlopen(self.url + "/health", timeout=5) as response:
            health = json.load(response)
        self.assertEqual(health["cache_mode"], "append_rewind")
        self.assertEqual(health["backend_source_sha256"], hashlib.sha256(Path(backend.__file__).read_bytes()).hexdigest())


class TemplateBoundaryTests(unittest.TestCase):
    def setUp(self):
        try:
            import jinja2  # noqa: F401
        except ImportError:
            self.skipTest("Genie template candidate requires Jinja2")
        self.template = backend.NativeTemplate({"bos_token": "<s>", "eos_token": "</s>",
            "chat_template": "{{ bos_token }}{% for m in messages %}{{ m.content }}{% endfor %}"})

    def test_strips_only_initial_boundary_preserving_user_text(self):
        self.assertEqual(self.template.render({"messages": [{"role": "system", "content": "policy"},
            {"role": "user", "content": "literal <s> and ø"}], "add_generation_prompt": True}),
            "policyliteral <s> and ø")

    def test_pending_action_cannot_be_rendered_as_completed_observation(self):
        with self.assertRaisesRegex(ValueError, "actual user/tool observation"):
            self.template.render({"messages": [{"role": "system", "content": "policy"},
                {"role": "user", "content": "observed"}, {"role": "assistant", "content": "A"}],
                "add_generation_prompt": True})


if __name__ == "__main__":
    unittest.main()
