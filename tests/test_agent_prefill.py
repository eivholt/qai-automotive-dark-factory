"""The optional cache job must not race or reject a fast next model request."""
from concurrent.futures import ThreadPoolExecutor
from http.server import ThreadingHTTPServer
import json
from pathlib import Path
import sys
import tempfile
import threading
import time
import unittest
from unittest.mock import patch
from urllib.request import Request, urlopen

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts/agent"))
try:
    import policy_cache
    import serve_single_token as server
    import single_token as st
finally:
    sys.path.pop(0)


class PrefillTests(unittest.TestCase):
    def test_prefix_contains_no_future_observation(self):
        class Template:
            def post(self, path, body):
                return {"prompt": "|".join(m["content"] for m in body["messages"]) + "[END]"}
        messages = [{"role": "system", "content": "policy"}, {"role": "user", "content": "real arrival"}]
        prefix = policy_cache.tool_prefix(Template(), messages, "A")
        self.assertEqual(prefix, "policy|real arrival|A|Tool result: ")
        self.assertEqual(len(messages), 2)
        with self.assertRaises(ValueError):
            policy_cache.tool_prefix(Template(), messages, "not-an-action")

    def test_fast_next_request_waits_for_prefill_without_sharing_stale_history(self):
        self.run_fast_request_case(False)

    def test_failed_optional_prefill_does_not_fail_the_next_request(self):
        self.run_fast_request_case(True)

    def run_fast_request_case(self, fail_prefill):
        ready, warming, release = threading.Event(), threading.Event(), threading.Event()
        captured = {}
        collisions, histories = [], []

        class HTTP(ThreadingHTTPServer):
            def serve_forever(self):
                captured["server"] = self; ready.set()
                super().serve_forever(poll_interval=.01)

        class Controller:
            prompt_hash = "test"
            prompt = "test policy"
            token_ids = {"A": 1065}

            def __init__(self, *a, **kw):
                pass

            def decide(self, history):
                collisions.append(warming.is_set())
                messages = st.model_messages(history, self.prompt)
                histories.append(messages)
                return st.tool_response({"code": "A"}, {}, self.prompt_hash), {"model_messages": messages}

            def post(self, path, body):
                if path == "/apply-template":
                    return {"prompt": "|".join(m["content"] for m in body["messages"])}
                warming.set()
                if not release.wait(3):
                    raise TimeoutError("test release not signaled")
                warming.clear()
                if fail_prefill:
                    raise OSError("simulated optional preparation failure")
                return {"tokens_predicted": 1, "tokens": [1073]}

        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)/"config.json"
            config = {"system_prompt": "policy", "tools": []}; path.write_text(json.dumps(config))
            args = ["serve_single_token", "--port", "0", "--config", str(path), "--prefill-next-turn"]
            with patch.object(sys, "argv", args), patch.object(server, "Controller", Controller), patch.object(server, "ThreadingHTTPServer", HTTP):
                thread = threading.Thread(target=server.main, daemon=True); thread.start()
                try:
                    self.assertTrue(ready.wait(3))
                    address = "http://127.0.0.1:"+str(captured["server"].server_port)
                    def request(body):
                        payload = {"model": "dark-factory-agent", "tool_choice": "required", "parallel_tool_calls": False,
                            "tools": [], "messages": [{"role": "system", "content": "policy"},
                            {"role": "user", "content": json.dumps({"body_id": body, "event": "arrival",
                                "arrival": {"status": "completed", "holder_engaged": False}})}]}
                        with urlopen(Request(address+"/v1/chat/completions", json.dumps(payload).encode(),
                                             {"Content-Type": "application/json"}), timeout=4) as response:
                            return json.load(response)
                    first = request("old body")
                    self.assertEqual(first["single_token"]["code"], "A")
                    self.assertTrue(warming.wait(2))
                    with ThreadPoolExecutor(max_workers=1) as executor:
                        second = executor.submit(request, "new body after cancellation")
                        time.sleep(.08)
                        self.assertFalse(second.done())
                        release.set()
                        self.assertEqual(second.result(timeout=3)["single_token"]["code"], "A")
                    self.assertEqual(collisions, [False, False])
                    self.assertEqual(len(histories[1]), 2)
                    self.assertNotIn("old body", histories[1][1]["content"])
                    self.assertIn("new body after cancellation", histories[1][1]["content"])
                    if fail_prefill:
                        with urlopen(address+"/health", timeout=2) as response:
                            health = json.load(response)
                        self.assertEqual(health["errors"], 0)
                        self.assertGreaterEqual(health["next_turn_prefill"]["errors"], 1)
                finally:
                    release.set()
                    if "server" in captured:
                        captured["server"].shutdown(); captured["server"].server_close()
                    thread.join(timeout=3)
                    self.assertFalse(thread.is_alive())


if __name__ == "__main__":
    unittest.main()
