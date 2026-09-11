import importlib.util
import contextlib
import io
import json
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import struct
import sys
import tempfile
import threading
from unittest.mock import patch
import zlib

from dark_factory.dataset import generate_manifest, write_jsonl
from dark_factory.protocol import load_contract
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location(
    "evaluate_bridge", Path(__file__).resolve().parents[1] / "scripts/evaluate_bridge.py")
evaluation = importlib.util.module_from_spec(spec)
spec.loader.exec_module(evaluation)


class EvaluationTests(unittest.TestCase):
    def test_error_is_not_correct_abstention_and_false_pass_fails(self):
        rows = [
            {"expected": "G", "decision": "G", "elapsed_ms": 2000, "error": None},
            {"expected": "A", "decision": "A", "elapsed_ms": 100, "error": "timeout"},
            {"expected": "R", "decision": "G", "elapsed_ms": 120, "error": None},
        ]
        report = evaluation.summarize(rows, 1, 1, 0, 1000, False)
        self.assertFalse(report["gate_passed"])
        self.assertEqual(report["confusion"]["A"]["ERROR"], 1)
        self.assertEqual(report["confusion"]["A"]["A"], 0)
        self.assertEqual(report["false_green"], 1)
        self.assertEqual(report["warm_p95_ms"], 120)
        self.assertEqual(report["first_request_ms"], 2000)

    def test_mock_never_passes_accuracy_gate(self):
        rows = [{"expected": x, "decision": x, "elapsed_ms": 2, "error": None}
                for x in ("G", "A", "R")]
        report = evaluation.summarize(rows, 1, 1, 0, None, True)
        self.assertIsNone(report["gate_passed"])
        self.assertEqual(report["evidence"], "plumbing_only")

    def test_missing_class_and_slow_warm_latency_fail(self):
        rows = [{"expected": "G", "decision": "G", "elapsed_ms": 200, "error": None}] * 2
        report = evaluation.summarize(rows, 1, .9, 0, 100, False)
        self.assertFalse(report["checks"]["minimum_per_class"])
        self.assertFalse(report["checks"]["warm_latency"])



class FakeEvaluationBridge(ThreadingHTTPServer):
    daemon_threads = True

    def __init__(self, source="evk", response_mode="correct", initial_drift=False, final_drift=False):
        contract = load_contract()
        self.health = {"status": "ok", "backend": source, "mock": source == "mock",
                       "model": "fake-model" if source == "evk" else None,
                       "evk_url": "http://test.invalid/v1/chat/completions" if source == "evk" else None,
                       "contract_version": contract["version"], "prompt_sha256": contract["prompt_sha256"]}
        if initial_drift:
            self.health["prompt_sha256"] = "wrong"
        self.response_mode, self.final_drift = response_mode, final_drift
        self.requests = []
        self.health_calls = 0
        super().__init__(("127.0.0.1", 0), FakeEvaluationHandler)


class FakeEvaluationHandler(BaseHTTPRequestHandler):
    def log_message(self, *_args):
        pass

    def respond(self, value):
        data = json.dumps(value).encode()
        self.send_response(200)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        health = dict(self.server.health)
        self.server.health_calls += 1
        if self.server.final_drift and self.server.health_calls > 1:
            health["model"] = "changed-during-run"
        self.respond(health)

    def do_POST(self):
        body = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
        index = len(self.server.requests)
        self.server.requests.append(body)
        result = {"request_id": body["request_id"], "episode_id": body["episode_id"],
                  "source": self.server.health["backend"], "decision": ("G", "A", "R")[index % 3],
                  "latency_ms": 1}
        mode = self.server.response_mode
        if mode == "source_mismatch":
            result["source"] = "mock"
        elif mode == "id_mismatch":
            result["request_id"] = "obsolete-request"
        elif mode == "error_empty":
            result["error"] = ""
        elif mode == "error_null":
            result["error"] = None
        elif mode == "array":
            result = [result]
        self.respond(result)


def evaluation_png(gray):
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xffffffff)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 448, 256, 8, 0, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress((b"\0" + bytes([gray]) * 448) * 256)) + chunk(b"IEND", b""))


class EvaluationHTTPTests(unittest.TestCase):
    def run_evaluation(self, bridge, *, allow_mock=False):
        thread = threading.Thread(target=bridge.serve_forever, kwargs={"poll_interval": 0.01}, daemon=True)
        thread.start()
        try:
            with tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                config = json.loads((Path(__file__).resolve().parents[1] / "configs/randomization.json").read_text())
                config.update(groups=3, frames_per_episode=1,
                              split_fractions={"train": 1/3, "validation": 1/3, "test": 1/3})
                rows = generate_manifest(config)
                for index, row in enumerate(rows):
                    image = root / row["image"]
                    image.parent.mkdir(parents=True, exist_ok=True)
                    image.write_bytes(evaluation_png(index))
                    row["capture_status"] = "captured"
                manifest = root / "manifest.jsonl"
                output = root / "report.json"
                write_jsonl(manifest, rows)
                argv = ["evaluate_bridge.py", "--manifest", str(manifest), "--image-root", str(root),
                        "--url", f"http://127.0.0.1:{bridge.server_port}", "--output", str(output),
                        "--minimum-per-class", "1", "--timeout", "2"]
                if allow_mock:
                    argv.append("--allow-mock")
                with patch.object(sys, "argv", argv), contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
                    try:
                        exit_code = evaluation.main()
                    except SystemExit as exc:
                        exit_code = exc.code
                report = json.loads(output.read_text()) if output.exists() else None
                return exit_code, report
        finally:
            bridge.shutdown()
            bridge.server_close()
            thread.join(timeout=2)

    def test_correct_replies_write_pass_report_and_send_pixels_only(self):
        bridge = FakeEvaluationBridge()
        exit_code, report = self.run_evaluation(bridge)
        self.assertEqual(exit_code, 0)
        self.assertTrue(report["gate_passed"])
        self.assertEqual(report["errors"], 0)
        self.assertEqual(bridge.health_calls, 2)
        self.assertTrue(report["checks"]["stable_bridge_identity"])
        for request in bridge.requests:
            self.assertEqual(set(request), {"request_id", "episode_id", "image_base64"})

    def test_bad_replies_write_failure_report_and_do_not_count_correct_amber(self):
        for mode in ("source_mismatch", "id_mismatch", "error_empty", "error_null", "array"):
            with self.subTest(mode=mode):
                exit_code, report = self.run_evaluation(FakeEvaluationBridge(response_mode=mode))
                self.assertEqual(exit_code, 1)
                self.assertFalse(report["gate_passed"])
                self.assertEqual(report["errors"], 3)
                self.assertEqual(report["confusion"]["A"]["A"], 0)
                self.assertEqual(report["confusion"]["A"]["ERROR"], 1)

    def test_initial_contract_drift_stops_before_inference(self):
        bridge = FakeEvaluationBridge(initial_drift=True)
        exit_code, report = self.run_evaluation(bridge)
        self.assertEqual(exit_code, 2)
        self.assertIsNone(report)
        self.assertEqual(bridge.requests, [])

    def test_mid_run_identity_drift_invalidates_all_accuracy_credit(self):
        exit_code, report = self.run_evaluation(FakeEvaluationBridge(final_drift=True))
        self.assertEqual(exit_code, 1)
        self.assertFalse(report["gate_passed"])
        self.assertFalse(report["checks"]["stable_bridge_identity"])
        self.assertEqual(report["errors"], 3)
        self.assertEqual(report["confusion"]["G"]["G"], 0)

    def test_mock_requires_opt_in_and_can_only_write_plumbing_report(self):
        bridge = FakeEvaluationBridge(source="mock")
        exit_code, report = self.run_evaluation(bridge)
        self.assertEqual(exit_code, 2)
        self.assertIsNone(report)
        self.assertEqual(bridge.requests, [])
        exit_code, report = self.run_evaluation(FakeEvaluationBridge(source="mock"), allow_mock=True)
        self.assertEqual(exit_code, 0)
        self.assertIsNone(report["gate_passed"])
        self.assertEqual(report["evidence"], "plumbing_only")


if __name__ == "__main__":
    unittest.main()
