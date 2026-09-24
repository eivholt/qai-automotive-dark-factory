"""Experimental loopback Genie backend for the existing one-token controller.

Loads no CPU model. Template metadata must come from the source GGUF's native
server properties; verify rendered prompts and token IDs before model testing.
The separate protocol adapter still owns policy validation and action decoding.
"""
from __future__ import annotations

import argparse
import hashlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import queue
import signal
import subprocess
import threading
import uuid


class NativeWorker:
    def __init__(self, command, timeout=120):
        self.timeout = timeout
        self.lock = threading.Lock()
        self._close_lock = threading.Lock()
        self._closed = threading.Event()
        self._closing = False
        self._close_error = None
        self._fatal = None
        self.replies = queue.Queue(maxsize=2)
        self.process = subprocess.Popen(command, stdin=subprocess.PIPE,
                                        stdout=subprocess.PIPE, text=True, encoding="utf-8", bufsize=1)
        self.reader = threading.Thread(target=self._read, daemon=True)
        try:
            self.reader.start()
            self.health = self._receive()
            if self.health.get("ready") is not True:
                raise RuntimeError("Native worker did not become ready")
        except BaseException:
            self.close()
            raise

    def _read(self):
        try:
            while True:
                line = self.process.stdout.readline(2 * 1024 * 1024 + 1)
                if not line:
                    raise RuntimeError("Native worker closed its output")
                if len(line) > 2 * 1024 * 1024 or not line.endswith("\n"):
                    raise RuntimeError("Oversized or incomplete worker response")
                reply = json.loads(line)
                if not isinstance(reply, dict):
                    raise RuntimeError("Invalid native worker response")
                try:
                    self.replies.put_nowait(reply)
                except queue.Full as exc:
                    raise RuntimeError("Native worker produced unsolicited responses") from exc
        except Exception as exc:
            if not self._closing:
                self._fatal = RuntimeError("Native worker protocol failed: " + str(exc))
                # No valid transport remains after a broken reader. Terminate
                # first, then wake the caller; do not wait for another request.
                try:
                    self.close()
                except Exception as cleanup_error:
                    self._fatal = RuntimeError(str(self._fatal) + "; cleanup failed: " + str(cleanup_error))
                try:
                    self.replies.put_nowait(self._fatal)
                except queue.Full:
                    pass  # _receive also checks the permanent failure flag.

    def is_running(self):
        return not self._closing and self._fatal is None and self.process.poll() is None and self.reader.is_alive()

    def _receive(self):
        if self._fatal is not None:
            self.close()
            raise self._fatal
        if self._closing:
            self.close()
            raise RuntimeError("Native worker was stopped")
        try:
            reply = self.replies.get(timeout=self.timeout)
        except queue.Empty as exc:
            self.close()
            raise TimeoutError("Native worker timed out and was stopped") from exc
        if self._fatal is not None:
            self.close()
            raise self._fatal
        if self._closing:
            self.close()
            raise RuntimeError("Native worker was stopped")
        if isinstance(reply, Exception):
            self.close()
            raise reply
        if not isinstance(reply, dict):
            self.close()
            raise RuntimeError("Invalid native worker response")
        return reply

    def request(self, body):
        with self.lock:
            if not self.is_running():
                raise RuntimeError("Native worker is no longer running")
            request_id = uuid.uuid4().hex
            data = json.dumps({**body, "request_id": request_id}, separators=(",", ":")) + "\n"
            if len(data.encode()) > 1024 * 1024:
                raise ValueError("Worker request exceeds 1 MiB")
            try:
                self.process.stdin.write(data)
                self.process.stdin.flush()
            except (OSError, ValueError) as exc:
                self.close()
                raise RuntimeError("Native worker input transport failed") from exc
            reply = self._receive()
            if reply.get("request_id") != request_id:
                self.close()
                raise RuntimeError("Native worker response identity mismatch")
            if "error" in reply:
                raise ValueError(reply["error"])
            return reply

    def close(self):
        # The reader may be the closer, or may be waking from EOF while another
        # thread closes. Never hold this lock while joining that reader.
        with self._close_lock:
            first_closer = not self._closing
            self._closing = True
        from_reader = threading.current_thread() is self.reader
        if not first_closer:
            # A reader waking on EOF must not wait for a closer joining it.
            if from_reader:
                return
            if not self._closed.wait(timeout=12):
                raise RuntimeError("Native worker cleanup did not finish")
            if self.reader.is_alive():
                self.reader.join(timeout=2)
            if self.reader.is_alive():
                raise RuntimeError("Native worker reader did not stop after child termination")
            if self._close_error is not None:
                raise self._close_error
            return
        try:
            if self.process.poll() is None:
                self.process.kill()
            self.process.wait(timeout=10)
            if not from_reader and self.reader.is_alive():
                self.reader.join(timeout=2)
            # Killing/reaping releases pipe reads before stream close; closing
            # a BufferedReader while another thread blocks in read can hang.
            if self.reader.is_alive() and not from_reader:
                raise RuntimeError("Native worker reader did not stop after child termination")
            for stream in (self.process.stdin, self.process.stdout):
                if stream is not None:
                    try:
                        stream.close()
                    except (BrokenPipeError, OSError, ValueError):
                        pass
        except BaseException as exc:
            self._close_error = exc
            raise
        finally:
            self._closed.set()
            # Release an HTTP caller waiting for a native reply during shutdown.
            try:
                self.replies.put_nowait(RuntimeError("Native worker was stopped"))
            except queue.Full:
                pass


class NativeTemplate:
    def __init__(self, properties):
        from jinja2 import StrictUndefined
        from jinja2.sandbox import ImmutableSandboxedEnvironment
        self.bos = properties["bos_token"]
        self.eos = properties["eos_token"]
        source = properties["chat_template"]
        if not isinstance(source, str) or not source or not self.bos or not self.eos:
            raise ValueError("Missing source GGUF template or boundary tokens")
        self.sha256 = hashlib.sha256(source.encode()).hexdigest()
        env = ImmutableSandboxedEnvironment(undefined=StrictUndefined)
        def fail(message):
            raise ValueError(message)
        env.globals["raise_exception"] = fail
        self.template = env.from_string(source)

    def render(self, body):
        messages = body.get("messages")
        if (body.get("add_generation_prompt") is not True or not isinstance(messages, list)
                or not 2 <= len(messages) <= 82 or messages[0].get("role") != "system"):
            raise ValueError("Requires a system prompt and complete text-only decision history")
        for i, message in enumerate(messages):
            role = "system" if i == 0 else "user" if i % 2 else "assistant"
            if set(message) != {"role", "content"} or message["role"] != role or not isinstance(message["content"], str):
                raise ValueError("Only alternating system/user/assistant text is supported")
        if messages[-1]["role"] != "user":
            raise ValueError("Cannot infer before an actual user/tool observation")
        text = self.template.render(messages=messages, bos_token=self.bos, eos_token=self.eos,
                                    add_generation_prompt=True, tools=None)
        if not text.startswith(self.bos):
            raise ValueError("Native template must render one initial BOS")
        # llama.cpp apply-template removes the initial BOS; completion tokenization
        # adds it. Match that split explicitly, then audit exact IDs on both paths.
        return text[len(self.bos):]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--worker", type=Path, required=True)
    p.add_argument("--bundle-config", type=Path, required=True)
    p.add_argument("--tokenizer", type=Path, required=True)
    p.add_argument("--qairt-lib", type=Path, required=True)
    p.add_argument("--template-properties", type=Path, required=True)
    p.add_argument("--port", type=int, default=18206)
    p.add_argument("--cache-mode", choices=("reset", "append", "append_rewind"), default="reset",
                   help="append_rewind is an opt-in Genie 1.20.0 prefix-rewind experiment")
    args = p.parse_args()
    source_sha256 = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
    template = NativeTemplate(json.loads(args.template_properties.read_text(encoding="utf-8")))
    def terminate(_signum, _frame):
        raise SystemExit(0)
    previous_sigterm = signal.signal(signal.SIGTERM, terminate)
    worker = None
    try:
        # Install SIGTERM cleanup before native startup, which can take time.
        worker = NativeWorker([str(args.worker.resolve()), str(args.bundle_config.resolve()),
                               str(args.tokenizer.resolve()), str(args.qairt_lib.resolve())])
        if args.cache_mode == "append_rewind" and not worker.health.get("prefix_rewind_api_supported"):
            raise ValueError("Experimental prefix rewind requires reviewed Genie API 1.20.0 / QAIRT 2.50.40")
        busy = threading.Lock()
        counts = {"requests": 0, "errors": 0}
        def encode(text):
            return worker.request({"op": "tokenize", "content": text, "add_special": False})["tokens"]
        bos = encode(template.bos)
        if len(bos) != 1 or len(encode("A")) != 1:
            raise ValueError("Tokenizer inserts unexpected boundaries; prompt parity not established")

        class Handler(BaseHTTPRequestHandler):
            def respond(self, status, body):
                data = json.dumps(body, separators=(",", ":"), allow_nan=False).encode()
                try:
                    self.send_response(status)
                    self.send_header("Content-Type", "application/json")
                    self.send_header("Content-Length", str(len(data)))
                    self.end_headers()
                    self.wfile.write(data)
                except (BrokenPipeError, ConnectionResetError):
                    pass

            def do_GET(self):
                if self.path != "/health":
                    self.respond(404, {"error": "unknown path"})
                    return
                running = worker.is_running()
                self.respond(200 if running else 503, {"status": "ok" if running else "stopped",
                    "candidate": True, "backend": "genie-qnn-htp", "cache_mode": args.cache_mode,
                    "backend_source_sha256": source_sha256,
                    "worker": worker.health, "template_sha256": template.sha256,
                    "bos_token_id": bos[0], "template_properties_sha256": hashlib.sha256(
                        args.template_properties.read_bytes()).hexdigest(), **counts})

            def do_POST(self):
                if self.path not in ("/tokenize", "/apply-template", "/completion"):
                    self.respond(404, {"error": "unknown path"})
                    return
                if not busy.acquire(blocking=False):
                    self.respond(503, {"error": "Native worker is busy; no request queued"})
                    return
                try:
                    self.connection.settimeout(120)
                    length = int(self.headers.get("Content-Length", "0"))
                    if not 0 < length <= 262144:
                        raise ValueError("Invalid request size")
                    raw = self.rfile.read(length)
                    if len(raw) != length:
                        raise ValueError("Incomplete request")
                    body = json.loads(raw)
                    if not isinstance(body, dict):
                        raise ValueError("Expected object")
                    if self.path == "/apply-template":
                        result = {"prompt": template.render(body)}
                    elif self.path == "/tokenize":
                        tokens = encode(body["content"])
                        if body.get("add_special", False) and tokens[:1] != bos:
                            tokens = bos + tokens
                        result = {"tokens": tokens}
                    else:
                        counts["requests"] += 1
                        if not isinstance(body.get("prompt"), str):
                            raise ValueError("Completion requires exact rendered text")
                        if "cache_prompt" in body and not isinstance(body["cache_prompt"], bool):
                            raise ValueError("cache_prompt must be a boolean")
                        tokens = encode(body["prompt"])
                        if tokens[:1] != bos:
                            tokens = bos + tokens
                        # A caller measuring full prefill must override an append-capable
                        # deployment. Omitted/true retains the explicit server default.
                        mode = "reset" if body.get("cache_prompt") is False else args.cache_mode
                        request = {**body, "op": "completion", "tokens": tokens, "cache_mode": mode}
                        request.pop("prompt")
                        result = worker.request(request)
                    result.pop("request_id", None)
                    self.respond(200, result)
                except (ValueError, TypeError, KeyError) as exc:
                    counts["errors"] += 1
                    self.respond(400, {"error": str(exc)})
                except Exception as exc:
                    counts["errors"] += 1
                    self.respond(502, {"error": str(exc)})
                finally:
                    busy.release()

        server = ThreadingHTTPServer(("127.0.0.1", args.port), Handler)
        print(json.dumps({"listening": server.server_address[1], "backend": "candidate-genie-htp", "cache_mode": args.cache_mode}), flush=True)
        try:
            server.serve_forever()
        finally:
            server.server_close()
    finally:
        try:
            if worker is not None:
                worker.close()
        finally:
            signal.signal(signal.SIGTERM, previous_sigterm)


if __name__ == "__main__":
    main()
