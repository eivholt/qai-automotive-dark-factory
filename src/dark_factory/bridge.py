"""Local, bounded HTTP bridge from Unreal's fixed camera to IQ9 or a mock.

Run ``python -m dark_factory.bridge --help``. No model runs in mock mode.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import http.client
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import math
import os
from pathlib import Path
import socket
import threading
import time
from typing import Any
from urllib.parse import urlsplit

from .protocol import (
    DECISIONS, MAX_REQUEST_BYTES, InspectionRequest, ProtocolError,
    load_contract, parse_inspection, prompt_sha256, strict_decision, valid_identifier,
)

MAX_RESPONSE_BYTES = 64 * 1024


class BackendError(RuntimeError):
    def __init__(self, code: str, status: int = 502):
        super().__init__(code)
        self.code, self.status = code, status


def completion_url(endpoint: str) -> str:
    """Accept a server origin, /v1 base URL or the full completion endpoint."""
    parts = urlsplit(endpoint)
    if parts.scheme not in ("http", "https") or not parts.hostname or parts.username or parts.password or parts.query or parts.fragment:
        raise ValueError("EVK URL must be an http(s) URL without credentials, query or fragment")
    _ = parts.port  # Reject invalid/out-of-range ports at startup.
    path = parts.path.rstrip("/")
    if path.endswith("/v1/chat/completions"):
        return endpoint.rstrip("/")
    if path.endswith("/v1"):
        return endpoint.rstrip("/") + "/chat/completions"
    return endpoint.rstrip("/") + "/v1/chat/completions"


@dataclass(frozen=True)
class BridgeConfig:
    backend: str = "mock"
    host: str = "127.0.0.1"
    port: int = 18190
    evk_url: str = ""
    model: str = "Cosmos-Reason2-2B-DarkFactory-FT"
    timeout: float = 15.0
    client_timeout: float = 5.0
    mock_decisions: tuple[str, ...] = ("A",)
    token_parameter: str = "max_completion_tokens"
    contract_path: str | Path | None = None
    api_key: str = ""

    def __post_init__(self) -> None:
        if self.backend not in ("mock", "evk"):
            raise ValueError("backend must be mock or evk")
        if self.host not in ("127.0.0.1", "localhost"):
            raise ValueError("Bridge binds only to loopback; run it on the Unreal PC")
        if not 0 <= self.port <= 65535:
            raise ValueError("port must be between 0 and 65535")
        if any(not math.isfinite(value) or value <= 0 or value > 300 for value in (self.timeout, self.client_timeout)):
            raise ValueError("timeouts must be finite and between 0 and 300 seconds")
        if not self.mock_decisions or any(code not in DECISIONS for code in self.mock_decisions):
            raise ValueError("mock decisions must contain only G, A, R")
        if self.token_parameter not in ("max_completion_tokens", "max_tokens"):
            raise ValueError("Unsupported token parameter")
        if self.backend == "evk":
            completion_url(self.evk_url)
            if not self.model.strip():
                raise ValueError("EVK model name is required")


def _close_socket(connection: socket.socket | None) -> None:
    if connection is not None:
        try:
            connection.shutdown(socket.SHUT_RDWR)
        except OSError:
            pass


class Inspector:
    def __init__(self, config: BridgeConfig):
        self.config = config
        self.contract = load_contract(config.contract_path)
        self._lock = threading.Lock()
        self._mock_index = 0
        self._connection: http.client.HTTPConnection | None = None

    def close(self) -> None:
        if self._connection is not None:
            self._connection.close()
            self._connection = None

    def inspect(self, request: InspectionRequest) -> str:
        # Avoid EVK backlog; Unreal also sends one request at a time.
        if not self._lock.acquire(blocking=False):
            raise BackendError("backend_busy", 503)
        try:
            if self.config.backend == "mock":
                code = self.config.mock_decisions[self._mock_index % len(self.config.mock_decisions)]
                self._mock_index += 1
                return code
            return self._request_evk(request)
        finally:
            self._lock.release()

    def model_payload(self, request: InspectionRequest) -> dict[str, Any]:
        generation = dict(self.contract["generation"])
        # GenieX's prior deployment uses max_completion_tokens. An explicit
        # compatibility switch supports servers requiring the older spelling.
        generation.pop("max_completion_tokens", None)
        generation[self.config.token_parameter] = 1
        return {
            "model": self.config.model,
            "messages": [{"role": "user", "content": [
                {"type": "image_url", "image_url": {"url": "data:image/png;base64," + request.image_base64}},
                {"type": "text", "text": self.contract["prompt"]},
            ]}],
            **generation,
        }

    def _request_evk(self, request: InspectionRequest) -> str:
        parts = urlsplit(completion_url(self.config.evk_url))
        factory = http.client.HTTPSConnection if parts.scheme == "https" else http.client.HTTPConnection
        if self._connection is None:
            self._connection = factory(parts.hostname, parts.port, timeout=self.config.timeout)
        conn = self._connection
        timed_out = threading.Event()
        active_socket = [conn.sock]
        success = False

        def expire() -> None:
            timed_out.set()
            _close_socket(active_socket[0] or conn.sock)

        # Socket timeouts plus this deadline bound the exchange, including a
        # backend that drips response bytes. OS DNS resolution can outlive it;
        # use the EVK's IP address when a hard network deadline is required.
        timer = threading.Timer(self.config.timeout, expire)
        timer.daemon = True
        headers = {"Content-Type": "application/json"}
        if self.config.api_key:
            headers["Authorization"] = "Bearer " + self.config.api_key
        try:
            timer.start()
            conn.request("POST", parts.path, body=json.dumps(self.model_payload(request)).encode("utf-8"), headers=headers)
            active_socket[0] = conn.sock
            response = conn.getresponse()
            if response.status != 200:
                raise BackendError(f"evk_http_{response.status}")
            data = response.read(MAX_RESPONSE_BYTES + 1)
            if timed_out.is_set():
                raise BackendError("evk_timeout", 504)
            if len(data) > MAX_RESPONSE_BYTES:
                raise BackendError("evk_response_too_large")
            try:
                result = json.loads(data)
                choices = result["choices"]
                if not isinstance(choices, list) or len(choices) != 1:
                    raise ProtocolError("Expected one completion")
                decision = strict_decision(choices[0]["message"]["content"])
                success = True
                return decision
            except (ValueError, KeyError, IndexError, TypeError) as exc:
                raise BackendError("evk_invalid_response") from exc
        except BackendError:
            raise
        except (OSError, http.client.HTTPException) as exc:
            if timed_out.is_set() or isinstance(exc, (TimeoutError, socket.timeout)):
                raise BackendError("evk_timeout", 504) from exc
            raise BackendError("evk_unreachable") from exc
        finally:
            timer.cancel()
            if not success:
                self.close()


def result_payload(source: str, started: float, request_id: str = "", episode_id: str = "", decision: str = "A", error: str | None = None) -> dict[str, Any]:
    result: dict[str, Any] = {
        "request_id": request_id, "episode_id": episode_id, "decision": decision,
        "source": source, "latency_ms": round((time.monotonic() - started) * 1000, 3),
    }
    if error is not None:
        result["error"] = error
    return result


class BridgeServer(ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = True

    def __init__(self, config: BridgeConfig):
        self.config, self.inspector = config, Inspector(config)
        self._workers = threading.BoundedSemaphore(8)
        super().__init__((config.host, config.port), BridgeHandler)

    def server_close(self) -> None:
        self.inspector.close()
        super().server_close()

    def process_request(self, request: socket.socket, client_address: Any) -> None:
        if not self._workers.acquire(blocking=False):
            body = json.dumps(result_payload(self.config.backend, time.monotonic(), error="bridge_busy")).encode()
            try:
                request.settimeout(0.2)
                request.sendall(b"HTTP/1.0 503 Service Unavailable\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: " + str(len(body)).encode() + b"\r\n\r\n" + body)
            except OSError:
                pass
            self.shutdown_request(request)
            return
        try:
            super().process_request(request, client_address)
        except Exception:
            self._workers.release()
            raise

    def process_request_thread(self, request: socket.socket, client_address: Any) -> None:
        try:
            super().process_request_thread(request, client_address)
        finally:
            self._workers.release()


class BridgeHandler(BaseHTTPRequestHandler):
    server: BridgeServer
    server_version = "DarkFactoryBridge/0.1"

    def setup(self) -> None:
        super().setup()
        self.connection.settimeout(self.server.config.client_timeout)
        self._read_deadline = threading.Timer(self.server.config.client_timeout, _close_socket, (self.connection,))
        self._read_deadline.daemon = True
        self._read_deadline.start()

    def finish(self) -> None:
        self._read_deadline.cancel()
        super().finish()

    def log_message(self, format: str, *args: Any) -> None:
        # Log no client payload, image, credentials or model text.
        pass

    def _send(self, status: int, payload: dict[str, Any]) -> None:
        self._read_deadline.cancel()
        body = json.dumps(payload, allow_nan=False).encode("utf-8")
        try:
            self.send_response(status)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Connection", "close")
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)
        except (OSError, ConnectionError):
            pass  # A disconnected Unreal request must time out to hold.
        self.close_connection = True

    def do_GET(self) -> None:
        if self.path != "/health":
            self._send(404, {"error": "not_found"})
            return
        contract = self.server.inspector.contract
        self._send(200, {
            "status": "ok", "backend": self.server.config.backend,
            "mock": self.server.config.backend == "mock",
            "model": self.server.config.model if self.server.config.backend == "evk" else None,
            "evk_url": completion_url(self.server.config.evk_url) if self.server.config.backend == "evk" else None,
            "contract_version": contract["version"], "prompt_sha256": prompt_sha256(contract),
            "image": contract["image"], "evk_connectivity_checked": False,
        })

    def do_POST(self) -> None:
        started = time.monotonic()
        request_id = episode_id = ""
        source = self.server.config.backend

        def fail(status: int, message: str) -> None:
            self._send(status, result_payload(source, started, request_id, episode_id, error=message))

        if self.path != "/inspect":
            fail(404, "not_found")
            return
        if self.headers.get_content_type() != "application/json":
            fail(415, "content_type_must_be_application_json")
            return
        lengths = self.headers.get_all("Content-Length", [])
        if self.headers.get("Transfer-Encoding") or len(lengths) != 1:
            fail(400, "one_content_length_required")
            return
        try:
            length = int(lengths[0])
        except ValueError:
            fail(400, "invalid_content_length")
            return
        if not 0 < length <= MAX_REQUEST_BYTES:
            fail(413, "request_too_large_or_empty")
            return
        try:
            data = self.rfile.read(length)
            self._read_deadline.cancel()
            if len(data) != length:
                raise ProtocolError("Truncated request")
            payload = json.loads(data)
            if isinstance(payload, dict):
                request_id = payload.get("request_id") if valid_identifier(payload.get("request_id")) else ""
                episode_id = payload.get("episode_id") if valid_identifier(payload.get("episode_id")) else ""
            request = parse_inspection(payload)
        except (ProtocolError, ValueError, UnicodeError, RecursionError) as exc:
            fail(400, "invalid_request: " + (str(exc) if isinstance(exc, ProtocolError) else "invalid_json"))
            return
        except (OSError, TimeoutError):
            fail(408, "request_timeout")
            return
        try:
            decision = self.server.inspector.inspect(request)
        except BackendError as exc:
            fail(exc.status, exc.code)
            return
        except Exception:
            fail(500, "bridge_internal_error")
            return
        self._send(200, result_payload(source, started, request_id, episode_id, decision))


def main(argv: list[str] | None = None) -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--backend", choices=("mock", "evk"), default=os.getenv("DARK_FACTORY_BACKEND", "mock"))
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=int(os.getenv("DARK_FACTORY_PORT", "18190")))
    parser.add_argument("--evk-url", default=os.getenv("DARK_FACTORY_EVK_URL", ""))
    parser.add_argument("--model", default=os.getenv("DARK_FACTORY_MODEL", "Cosmos-Reason2-2B-DarkFactory-FT"))
    parser.add_argument("--timeout", type=float, default=float(os.getenv("DARK_FACTORY_TIMEOUT", "15")))
    parser.add_argument("--client-timeout", type=float, default=5.0)
    mock = parser.add_mutually_exclusive_group()
    mock.add_argument("--mock-decision", choices=("G", "A", "R"), default="A")
    mock.add_argument("--mock-cycle", metavar="G,A,R", help="Repeat these scripted decisions once per valid request")
    parser.add_argument("--token-parameter", choices=("max_completion_tokens", "max_tokens"), default="max_completion_tokens")
    parser.add_argument("--contract", type=Path)
    args = parser.parse_args(argv)
    values = vars(args)
    cycle = values.pop("mock_cycle")
    decision = values.pop("mock_decision")
    values["mock_decisions"] = tuple(part.strip() for part in cycle.split(",")) if cycle is not None else (decision,)
    values["contract_path"] = values.pop("contract")
    values["api_key"] = os.getenv("DARK_FACTORY_API_KEY", "")
    try:
        server = BridgeServer(BridgeConfig(**values))
    except (ValueError, OSError) as exc:
        parser.error(str(exc))
    print(f"Dark factory bridge http://{server.server_address[0]}:{server.server_address[1]} | source={args.backend}", flush=True)
    if args.backend == "mock":
        print("SCRIPTED MOCK: no model inference; decisions are not accuracy evidence.", flush=True)
    try:
        server.serve_forever(poll_interval=0.2)
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
