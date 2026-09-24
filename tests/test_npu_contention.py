"""Saved-input contention probes preserve evidence and bound endpoint work."""
import base64
import contextlib
import copy
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import importlib.util
import io
import json
from pathlib import Path
import struct
import tempfile
import threading
import time
import unittest
from unittest.mock import patch
import zlib

spec = importlib.util.spec_from_file_location("npu_contention", Path(__file__).resolve().parents[1] /
                                             "scripts/agent/benchmark_npu_contention.py")
bench = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bench)
from dark_factory.gates import hold_reply


def png():
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xffffffff)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 448, 256, 8, 0, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress((b"\0" + bytes([30]) * 448) * 256)) + chunk(b"IEND", b""))


def agent_reply(code="A"):
    return {"choices": [{"finish_reason": "stop", "message": {"role": "assistant", "content": code}}]}


class ContentionTests(unittest.TestCase):
    def setUp(self):
        self.contract = bench.load_gate_contract()
        self.agent = {"model": "dark-factory-agent", "messages": [{"role": "system", "content": "Existing prompt"},
                      {"role": "user", "content": "Existing arrival"}], "temperature": 0, "max_tokens": 1}
        self.classifier = {"contract_version": self.contract["version"], "request_id": "r1", "body_id": "b1",
                           "stage": "panel_presence", "captured_at_ms": 123,
                           "images": [{"camera_id": camera, "png_base64": base64.b64encode(png()).decode()}
                                      for camera in ("spot1", "spot2", "spot3")]}
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.endpoints = {"agent": "http://localhost:18201/v1/chat/completions",
                          "classifier": "http://localhost:18202/v1/inspect"}
        self.sources = {}
        for role in self.endpoints:
            path = self.root / (role + ".json")
            # Deliberate whitespace must survive every HTTP request.
            path.write_bytes((json.dumps(getattr(self, role), indent=3) + " \n").encode())
            self.sources[role] = bench.load_payload(path, role, self.contract)

    def classify(self, decision="G"):
        response = hold_reply(self.classifier, self.contract, "model_classification", valid=True)
        response["decision"] = decision
        return response

    def run_benchmark(self, post, repeats=2, warmup=1):
        with patch.object(bench, "post_json", post), contextlib.redirect_stdout(io.StringIO()):
            return bench.benchmark(self.endpoints, self.sources, self.contract, repeats, warmup, 2,
                                   self.root / "report.json")

    def test_all_modes_replay_identical_bytes_and_never_overlap_same_endpoint(self):
        active = {"agent": 0, "classifier": 0}
        maxima = dict(active)
        seen = []
        overlapping = []
        lock = threading.Lock()
        def post(endpoint, raw, timeout):
            role = next(key for key, value in self.endpoints.items() if value == endpoint)
            self.assertEqual(raw, self.sources[role]["raw"])
            with lock:
                active[role] += 1
                maxima[role] = max(maxima[role], active[role])
                overlapping.append(sum(active.values()))
                seen.append(role)
            time.sleep(.004)
            with lock:
                active[role] -= 1
            return (agent_reply() if role == "agent" else self.classify()), "digest", 100
        report = self.run_benchmark(post)
        self.assertTrue(report["passed"])
        self.assertEqual(maxima, {"agent": 1, "classifier": 1})
        self.assertEqual(max(overlapping), 2)
        self.assertEqual(seen.count("agent"), 9)
        self.assertEqual(seen.count("classifier"), 9)
        self.assertEqual(len(report["warmup"]), 4)
        self.assertEqual(len(report["rows"]), 8)
        self.assertEqual(report["summary"]["modes"]["agent_only"]["calls"]["agent"]["count"], 2)
        for row in report["rows"]:
            if row["mode"] == "sequential":
                self.assertGreaterEqual(row["calls"][1]["start_ms"], row["calls"][0]["end_ms"])
                self.assertEqual(row["http_overlap_ms"], 0)
            if row["mode"] == "concurrent":
                self.assertGreater(row["http_overlap_ms"], 0)
        saved = json.loads((self.root / "report.json").read_text())
        self.assertEqual(saved, report)
        self.assertNotIn("png_base64", (self.root / "report.json").read_text())

    def test_sequential_mismatch_never_sends_saved_inspection(self):
        calls = []
        def post(endpoint, raw, timeout):
            calls.append(endpoint)
            return agent_reply("B"), "digest", 100
        with patch.object(bench, "post_json", post):
            row = bench.run_trial("sequential", 0, self.endpoints, self.sources, self.contract, 2)
        self.assertFalse(row["ok"])
        self.assertEqual(calls, [self.endpoints["agent"]])
        self.assertEqual(row["calls"][0]["result"]["code"], "B")

    def test_classifier_disagreement_is_evidence_not_corrected(self):
        counts = {"classifier": 0}
        def post(endpoint, raw, timeout):
            if endpoint == self.endpoints["agent"]:
                return agent_reply(), "digest", 100
            counts["classifier"] += 1
            return self.classify("R" if counts["classifier"] == 3 else "G"), "digest", 100
        report = self.run_benchmark(post, repeats=1, warmup=0)
        self.assertFalse(report["passed"])
        self.assertFalse(report["aborted"])
        stability = report["summary"]["classifier_stability"]
        self.assertEqual(stability["baseline_decision"], "G")
        self.assertEqual(stability["mismatches"], [{"mode": "concurrent", "repeat": 0, "decision": "R"}])
        self.assertEqual(report["rows"][-1]["calls"][1]["result"]["decision"], "R")

    def test_timeout_aborts_before_any_followup_request(self):
        calls = []
        def post(endpoint, raw, timeout):
            calls.append(endpoint)
            raise TimeoutError("fixture timeout")
        report = self.run_benchmark(post)
        self.assertTrue(report["aborted"])
        self.assertEqual(calls, [self.endpoints["agent"]])
        self.assertEqual(len(report["rows"]), 0)
        self.assertEqual(report["warmup"][0]["calls"][0]["error_type"], "TimeoutError")
        self.assertEqual(report["warmup_summary"]["modes"]["agent_only"]["calls"]["agent"]["count"], 0)

    def test_errors_invalid_codes_and_replay_identity_are_rejected(self):
        bad = [{"error": "busy"}, agent_reply("N"), agent_reply("A then B"),
               {"choices": [{"finish_reason": "length", "message": {"role": "assistant", "content": "A"}}]}]
        for response in bad:
            with self.subTest(response=response), self.assertRaises(ValueError):
                bench.validate_response("agent", response, self.agent, self.contract)
        for field, value in (("decision", "Y"), ("valid", False), ("request_id", "other"),
                             ("captured_at_ms", 124), ("prompt_sha256", "old"),
                             ("decision_policy", {"minimum_label_mass": .1})):
            response = self.classify()
            response[field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                bench.validate_response("classifier", response, self.classifier, self.contract)
        # A legitimate ambiguous model hold remains a successful classification.
        self.assertEqual(bench.validate_response("classifier", self.classify("A"), self.classifier,
                                                self.contract)["decision"], "A")

    def test_tool_call_and_single_token_code_must_agree(self):
        response = {"choices": [{"finish_reason": "tool_calls", "message": {"role": "assistant",
                    "tool_calls": [{"function": {"name": "inspect", "arguments":
                                   '{"stage":"weld","reason":"Observe actual surface"}'}}]}}],
                    "single_token": {"code": "G"}}
        self.assertEqual(bench.validate_response("agent", response, self.agent, self.contract)["argument"], "weld")
        response["single_token"]["code"] = "A"
        with self.assertRaises(ValueError):
            bench.validate_response("agent", response, self.agent, self.contract)

    def test_evaluator_fields_bad_png_and_streaming_inputs_are_rejected(self):
        for role, extra in (("agent", {"expected": "A"}), ("classifier", {"expected": "G"}),
                            ("agent", {"stream": True})):
            path = self.root / "bad.json"
            path.write_text(json.dumps({**getattr(self, role), **extra}))
            with self.subTest(role=role, extra=extra), self.assertRaises(ValueError):
                bench.load_payload(path, role, self.contract)
        payload = copy.deepcopy(self.classifier)
        payload["images"][0]["png_base64"] = "invalid"
        path.write_text(json.dumps(payload))
        with self.assertRaises(ValueError):
            bench.load_payload(path, "classifier", self.contract)
        for text in ('{"messages": [], "messages": []}', '{"score": NaN}'):
            with self.assertRaises(ValueError):
                bench.strict_json(text)

    def test_endpoint_validation_and_percentiles(self):
        self.assertEqual(bench.endpoint_url("http://127.0.0.1:80", "classifier"), "http://127.0.0.1:80/v1/inspect")
        for url in ("file:///request", "http://user:pass@localhost", "http://localhost/private", "http://localhost/?key=x"):
            with self.subTest(url=url), self.assertRaises(ValueError):
                bench.endpoint_url(url, "agent")
        self.assertEqual(bench.stats([1, 2, 3, 4])["p50_ms"], 2.5)
        self.assertAlmostEqual(bench.stats([1, 2, 3, 4])["p95_ms"], 3.85)
        self.assertIsNone(bench.stats([])["p95_ms"])

    def test_repetition_and_timeout_limits(self):
        for repeats, warmup, timeout in ((0, 0, 1), (101, 0, 1), (1, 11, 1), (1, 0, float("nan")), (1, 0, 301)):
            with self.subTest(values=(repeats, warmup, timeout)), self.assertRaises(ValueError):
                bench.benchmark(self.endpoints, self.sources, self.contract, repeats, warmup, timeout,
                                self.root / "limits.json")

    def test_server_cache_telemetry_is_retained_without_unbounded_payloads(self):
        response = agent_reply()
        response["timings"] = {"cache_mode": "reset", "retained_context_tokens": 0,
                               "submitted_prompt_n": 1381, "reused_tokens_measured": None,
                               "prompt_ms": 12.5, "ignored_prompt": "private", "ttft_ms": float("nan")}
        with patch.object(bench, "post_json", return_value=(response, "digest", 100)):
            row = bench.call_endpoint("agent", self.endpoints["agent"], self.sources["agent"],
                                      self.contract, 2, time.perf_counter())
        self.assertTrue(row["ok"])
        self.assertEqual(row["server_timings"], {"cache_mode": "reset", "retained_context_tokens": 0,
                         "submitted_prompt_n": 1381, "reused_tokens_measured": None, "prompt_ms": 12.5})

    def test_http_transport_preserves_bytes_rejects_status_and_limits_response(self):
        requests = []
        reply = {"status": 200, "body": b'{"ok": true}'}
        class Handler(BaseHTTPRequestHandler):
            def log_message(self, *_):
                pass
            def do_POST(self):
                requests.append(self.rfile.read(int(self.headers["Content-Length"])))
                self.send_response(reply["status"])
                self.send_header("Content-Length", str(len(reply["body"])))
                self.end_headers()
                self.wfile.write(reply["body"])
        server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        thread = threading.Thread(target=server.serve_forever, kwargs={"poll_interval": .01}, daemon=True)
        thread.start()
        try:
            endpoint = f"http://127.0.0.1:{server.server_port}/v1/inspect"
            raw = self.sources["classifier"]["raw"]
            result, digest, size = bench.post_json(endpoint, raw, 2)
            self.assertEqual(result, {"ok": True})
            self.assertEqual(len(digest), 64)
            self.assertEqual(size, len(reply["body"]))
            self.assertEqual(requests, [raw])
            reply.update(status=503, body=b'{"error":"busy"}')
            with self.assertRaisesRegex(ValueError, "HTTP status 503"):
                bench.post_json(endpoint, raw, 2)
            reply.update(status=200, body=b"x" * (bench.MAX_RESPONSE_BYTES + 1))
            with self.assertRaisesRegex(ValueError, "byte limit"):
                bench.post_json(endpoint, raw, 2)
        finally:
            server.shutdown()
            server.server_close()
            thread.join(2)

    def test_http_wall_deadline_bounds_a_trickling_response_body(self):
        # A socket read timeout alone can be kept alive by slow byte delivery.
        class Handler(BaseHTTPRequestHandler):
            def log_message(self, *_):
                pass
            def do_POST(self):
                self.rfile.read(int(self.headers["Content-Length"]))
                self.send_response(200)
                self.send_header("Content-Length", "1000")
                self.end_headers()
                try:
                    for _ in range(100):
                        self.wfile.write(b" ")
                        self.wfile.flush()
                        time.sleep(.02)
                except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
                    pass
        server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        thread = threading.Thread(target=server.serve_forever, kwargs={"poll_interval": .01}, daemon=True)
        thread.start()
        try:
            start = time.perf_counter()
            with self.assertRaises((TimeoutError, OSError, bench.http.client.HTTPException)):
                bench.post_json(f"http://127.0.0.1:{server.server_port}/v1/inspect", b"{}", .12)
            self.assertLess(time.perf_counter() - start, .8)
        finally:
            server.shutdown()
            server.server_close()
            thread.join(2)


if __name__ == "__main__":
    unittest.main()
