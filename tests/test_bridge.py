"""Real local HTTP integration tests; fake EVK never contacts an external host."""

import base64
from contextlib import contextmanager
import http.client
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import struct
import threading
import time
import unittest
import zlib

from dark_factory.bridge import BridgeConfig, BridgeServer, completion_url
from dark_factory.protocol import (
    MAX_REQUEST_BYTES, ProtocolError, load_contract, prompt_sha256,
    strict_decision, validate_png,
)


def chunk(kind, payload):
    return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF)


def png(width=448, height=256):
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)) + chunk(b"IDAT", zlib.compress((b"\0" + b"\x80\x80\x80" * width) * height)) + chunk(b"IEND", b"")


def inspect_body(**overrides):
    return {"request_id": "req-1", "episode_id": "episode-1", "image_base64": base64.b64encode(png()).decode(), **overrides}


def send(server, body=None, *, path="/inspect", method="POST", headers=None):
    connection = http.client.HTTPConnection("127.0.0.1", server.server_port, timeout=2)
    if isinstance(body, dict):
        body = json.dumps(body).encode()
    try:
        connection.request(method, path, body=body, headers=headers or {"Content-Type": "application/json"})
        response = connection.getresponse()
        return response.status, json.loads(response.read())
    finally:
        connection.close()


@contextmanager
def running(server):
    thread = threading.Thread(target=server.serve_forever, kwargs={"poll_interval": 0.01}, daemon=True)
    thread.start()
    try:
        yield server
    finally:
        server.shutdown()
        server.server_close()
        thread.join(timeout=2)


class FakeEVK(ThreadingHTTPServer):
    daemon_threads = True

    def __init__(self, *, answer="G", delay=0, raw=None, status=200, drip=False):
        self.answer, self.delay, self.raw, self.status, self.drip = answer, delay, raw, status, drip
        self.requests = []
        self.client_ports = []
        self.close_after_response = False
        self.entered = threading.Event()
        super().__init__(("127.0.0.1", 0), FakeEVKHandler)

    @property
    def url(self):
        return f"http://127.0.0.1:{self.server_port}"


class FakeEVKHandler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def handle(self):
        try:
            super().handle()
        except OSError:
            pass  # The bridge intentionally resets failed/deadline-expired sockets.

    def log_message(self, *_args):
        pass

    def do_POST(self):
        body = self.rfile.read(int(self.headers["Content-Length"]))
        self.server.requests.append((self.path, json.loads(body)))
        self.server.client_ports.append(self.client_address[1])
        self.server.entered.set()
        time.sleep(self.server.delay)
        data = self.server.raw
        if data is None:
            data = json.dumps({"choices": [{"message": {"content": self.server.answer}}]}).encode()
        try:
            self.send_response(self.server.status)
            self.send_header("Content-Length", str(len(data)))
            if self.server.close_after_response:
                self.send_header("Connection", "close")
                self.close_connection = True
            self.end_headers()
            if self.server.drip:
                for value in data:
                    self.wfile.write(bytes([value]))
                    self.wfile.flush()
                    time.sleep(0.03)
            else:
                self.wfile.write(data)
        except OSError:
            pass


class ProtocolTests(unittest.TestCase):
    def test_contract_integrity(self):
        contract = load_contract()
        self.assertEqual(contract["prompt_sha256"], prompt_sha256(contract))
        self.assertEqual(set(contract["labels"]), {"G", "A", "R"})

    def test_strict_letter_parser(self):
        self.assertEqual(strict_decision(" G\n"), "G")
        for value in ("g", "GREEN", "G because fine", '{"decision":"G"}', "<think>G</think>G", "A R", "", None, ["G"]):
            with self.subTest(value=value), self.assertRaises(ProtocolError):
                strict_decision(value)

    def test_png_validation_checks_pixels_and_crc(self):
        validate_png(png())
        wrong_crc = bytearray(png())
        wrong_crc[29] ^= 1
        bad_pixels = png()[:33] + chunk(b"IDAT", zlib.compress(b"\0")) + chunk(b"IEND", b"")
        for value in (png(224, 128), bytes(wrong_crc), bad_pixels, png()[:-12], b"not png"):
            with self.subTest(length=len(value)), self.assertRaises(ProtocolError):
                validate_png(value)

    def test_endpoint_forms(self):
        for value in ("http://127.0.0.1:18183", "http://127.0.0.1:18183/v1", "http://127.0.0.1:18183/v1/chat/completions/"):
            self.assertEqual(completion_url(value), "http://127.0.0.1:18183/v1/chat/completions")
        for value in ("", "file:///tmp/test", "http://user:pass@localhost", "http://localhost?x=1"):
            with self.subTest(value=value), self.assertRaises(ValueError):
                completion_url(value)


class BridgeHTTPTests(unittest.TestCase):
    def test_mock_cycle_health_and_identifiers(self):
        with running(BridgeServer(BridgeConfig(port=0, mock_decisions=("G", "A", "R")))) as bridge:
            status, health = send(bridge, path="/health", method="GET")
            self.assertEqual(status, 200)
            self.assertTrue(health["mock"])
            self.assertFalse(health["evk_connectivity_checked"])
            for expected in ("G", "A", "R", "G"):
                status, result = send(bridge, inspect_body())
                self.assertEqual(status, 200)
                self.assertEqual(result["decision"], expected)
                self.assertEqual(result["source"], "mock")
                self.assertEqual(result["request_id"], "req-1")
                self.assertEqual(result["episode_id"], "episode-1")
                self.assertGreaterEqual(result["latency_ms"], 0)
                self.assertNotIn("error", result)

    def test_malformed_inputs_hold_and_do_not_advance_mock(self):
        with running(BridgeServer(BridgeConfig(port=0, mock_decisions=("G", "R")))) as bridge:
            for body in (b"{", b"[]", inspect_body(image_base64="%%%"), inspect_body(image_base64=base64.b64encode(png(224, 128)).decode()), inspect_body(ground_truth="G"), inspect_body(request_id="bad id")):
                with self.subTest(body_type=type(body).__name__):
                    status, result = send(bridge, body)
                    self.assertEqual(status, 400)
                    self.assertEqual(result["decision"], "A")
                    self.assertIn("error", result)
            _, result = send(bridge, inspect_body())
            self.assertEqual(result["decision"], "G")

    def test_size_and_content_type_limits(self):
        with running(BridgeServer(BridgeConfig(port=0))) as bridge:
            for headers, expected in (({"Content-Type": "application/json", "Content-Length": str(MAX_REQUEST_BYTES + 1)}, 413), ({"Content-Type": "text/plain"}, 415)):
                status, result = send(bridge, b"{}", headers=headers)
                self.assertEqual(status, expected)
                self.assertEqual(result["decision"], "A")
                self.assertIn("error", result)

    def test_evk_visual_only_payload_and_genuine_amber(self):
        with running(FakeEVK(answer="A")) as evk, running(BridgeServer(BridgeConfig(port=0, backend="evk", evk_url=evk.url))) as bridge:
            _, health = send(bridge, path="/health", method="GET")
            self.assertEqual(evk.requests, [])
            self.assertFalse(health["mock"])
            self.assertEqual(health["evk_url"], evk.url + "/v1/chat/completions")
            self.assertEqual(health["model"], "Cosmos-Reason2-2B-DarkFactory-FT")
            status, result = send(bridge, inspect_body(episode_id="secret-episode-missing", request_id="secret-label-R"))
            self.assertEqual(status, 200)
            self.assertEqual(result["decision"], "A")
            self.assertEqual(result["source"], "evk")
            self.assertNotIn("error", result)
            path, payload = evk.requests[0]
            self.assertEqual(path, "/v1/chat/completions")
            self.assertEqual(len(payload["messages"]), 1)
            self.assertEqual(payload["messages"][0]["role"], "user")
            content = payload["messages"][0]["content"]
            self.assertEqual(content[0]["image_url"]["url"], "data:image/png;base64," + inspect_body()["image_base64"])
            self.assertEqual(content[1]["text"], load_contract()["prompt"])
            self.assertNotIn("secret-", json.dumps(payload))
            for key, expected in (("max_completion_tokens", 1), ("temperature", 0), ("enable_think", False), ("stream", False), ("top_k", 1), ("seed", 42)):
                self.assertEqual(payload[key], expected)
            self.assertNotIn("max_tokens", payload)

    def test_evk_reuses_connection_and_resets_after_invalid_response(self):
        with running(FakeEVK()) as evk, running(BridgeServer(BridgeConfig(port=0, backend="evk", evk_url=evk.url))) as bridge:
            for _ in range(2):
                self.assertEqual(send(bridge, inspect_body())[0], 200)
            self.assertEqual(evk.client_ports[0], evk.client_ports[1])
            evk.answer = "INVALID"
            self.assertEqual(send(bridge, inspect_body())[0], 502)
            evk.answer = "G"
            self.assertEqual(send(bridge, inspect_body())[0], 200)
            self.assertNotEqual(evk.client_ports[-1], evk.client_ports[0])

    def test_evk_reconnects_after_server_closes_connection(self):
        with running(FakeEVK()) as evk, running(BridgeServer(BridgeConfig(port=0, backend="evk", evk_url=evk.url))) as bridge:
            evk.close_after_response = True
            for _ in range(2):
                self.assertEqual(send(bridge, inspect_body())[0], 200)
            self.assertNotEqual(evk.client_ports[0], evk.client_ports[1])

    def test_invalid_model_replies_hold(self):
        for kwargs in ({"answer": "G and R"}, {"answer": "g"}, {"raw": b"not json"}, {"raw": b'{"choices": []}'}, {"status": 500}, {"raw": b"x" * 65537}):
            with self.subTest(kwargs={key: str(value)[:25] for key, value in kwargs.items()}), running(FakeEVK(**kwargs)) as evk, running(BridgeServer(BridgeConfig(port=0, backend="evk", evk_url=evk.url))) as bridge:
                status, result = send(bridge, inspect_body())
                self.assertEqual(status, 502)
                self.assertEqual(result["decision"], "A")
                self.assertIn("error", result)

    def test_timeout_and_dripping_response_hold(self):
        for kwargs in ({"delay": 0.2}, {"drip": True}):
            with self.subTest(kwargs=kwargs), running(FakeEVK(**kwargs)) as evk, running(BridgeServer(BridgeConfig(port=0, backend="evk", evk_url=evk.url, timeout=0.07))) as bridge:
                started = time.monotonic()
                status, result = send(bridge, inspect_body())
                self.assertEqual(status, 504)
                self.assertEqual(result["decision"], "A")
                self.assertEqual(result["error"], "evk_timeout")
                self.assertLess(time.monotonic() - started, 1)

    def test_backend_serialization_rejects_backlog(self):
        with running(FakeEVK(delay=0.15)) as evk, running(BridgeServer(BridgeConfig(port=0, backend="evk", evk_url=evk.url))) as bridge:
            first = []
            thread = threading.Thread(target=lambda: first.append(send(bridge, inspect_body(request_id="first"))))
            thread.start()
            self.assertTrue(evk.entered.wait(timeout=1))
            status, result = send(bridge, inspect_body(request_id="second"))
            self.assertEqual(status, 503)
            self.assertEqual(result["decision"], "A")
            self.assertEqual(result["error"], "backend_busy")
            thread.join(timeout=2)
            self.assertEqual(first[0][0], 200)
            self.assertEqual(len(evk.requests), 1)


if __name__ == "__main__":
    unittest.main()
