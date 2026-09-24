"""Loopback equipment API and autonomous agent loop, deployed on IQ9 EVK."""
import argparse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import platform
import threading
import time

from edge_runtime import Runtime


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--port", type=int, default=18210)
    p.add_argument("--config", type=Path, default=Path(__file__).resolve().parents[2]/"configs/live_agent.json")
    p.add_argument("--qa-log", type=Path, help="Fresh private bounded JSONL audit; disabled for ordinary runs")
    args = p.parse_args()
    if platform.machine() != "aarch64":
        p.error("Deploy the default runtime on the EVK; use unit tests for host simulation")
    log = None
    if args.qa_log:
        args.qa_log.parent.mkdir(parents=True, exist_ok=True)
        log = args.qa_log.open("x", encoding="utf-8")
    logged = 0
    def record(event):
        nonlocal logged
        if log and logged < 512:
            log.write(json.dumps(event, separators=(",", ":"))+"\n")
            log.flush()
            logged += 1
    runtime = Runtime(json.loads(args.config.read_text()), {
        ("evk_hexagon", "cpu"): "http://127.0.0.1:18196/v1/chat/completions",
        ("evk_hexagon", "npu"): "http://127.0.0.1:18197/v1/chat/completions",
        ("host_gpu", "cpu"): "http://127.0.0.1:18214/v1/chat/completions",
        ("host_gpu", "npu"): "http://127.0.0.1:18214/v1/chat/completions",
    }, recorder=record)
    stop = threading.Event()
    def loop():
        while not stop.wait(.02):
            runtime.tick()
    threading.Thread(target=loop, name="evk-agent-loop", daemon=True).start()

    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *_):
            pass  # Polling is deliberately not an unbounded access log.

        def send(self, status, result):
            data = json.dumps(result, separators=(",", ":")).encode()
            self.send_response(status)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            try:
                self.wfile.write(data)
            except (BrokenPipeError, ConnectionResetError):
                pass  # Worker will replay its unacknowledged events safely.

        def do_GET(self):
            if self.path == "/health":
                self.send(200, dict(runtime.snapshot(), hostname=platform.node(), machine=platform.machine(), pid=os.getpid()))
            else:
                self.send(404, dict(error="Unknown path"))

        def do_POST(self):
            if self.path != "/v1/cell/exchange":
                self.send(404, dict(error="Unknown path"))
                return
            try:
                length = int(self.headers.get("Content-Length", "0"))
                if not 0 < length <= 262144:
                    raise ValueError("Invalid packet length")
                self.connection.settimeout(5)
                packet = json.loads(self.rfile.read(length))
                self.send(200, runtime.exchange(packet))
            except (ValueError, TypeError, KeyError, IndexError) as exc:
                self.send(409, dict(error=str(exc)))

    server = ThreadingHTTPServer(("127.0.0.1", args.port), Handler)
    print(json.dumps(dict(status="ready", port=args.port, hostname=platform.node(), pid=os.getpid())), flush=True)
    try:
        server.serve_forever()
    finally:
        stop.set()
        server.server_close()
        runtime.pool.shutdown(wait=False, cancel_futures=True)
        if log:
            log.close()


if __name__ == "__main__":
    main()
