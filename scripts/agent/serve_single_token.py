"""Loopback-only protocol adapter for the experimental one-token controller.

Unreal still owns jobs, observations, epochs and mechanical interlocks. This
adapter only reformats the actual conversation, scores one token and translates
the selected code back into a tool call. It never filters actions by scene state.
"""
from __future__ import annotations
import argparse
import hashlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import threading
import time
from urllib.parse import urlsplit

from single_token import Controller, VERSION, ACTIONS, DecisionError


def validate_payload(payload, config):
    if not isinstance(payload, dict):
        raise ValueError("Expected a request object")
    messages = payload.get("messages", [])
    if not isinstance(messages, list) or not messages or len(messages) > 82 or messages[0] != {"role": "system", "content": config["system_prompt"]}:
        raise ValueError("Unexpected controller prompt or history length")
    if payload.get("tools") != config["tools"] or payload.get("model") != "dark-factory-agent":
        raise ValueError("Unexpected tool/model contract")
    if payload.get("tool_choice") != "required" or payload.get("parallel_tool_calls") is not False:
        raise ValueError("Requires one complete tool call")
    return messages[1:]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--backend", default="http://127.0.0.1:18195")
    p.add_argument("--port", type=int, default=18196)
    p.add_argument("--config", type=Path, default=Path("configs/live_agent.json"))
    p.add_argument("--prompt", type=Path)
    p.add_argument("--policy-cache", type=Path, help="Native launcher's private slot-save directory with identity.json")
    p.add_argument("--prefill-next-turn", action="store_true", help="After replying, prepare only the known next-turn envelope while the job runs")
    p.add_argument("--qa-log-dir", type=Path, help="Private bounded logs for at most 64 QA requests; disabled normally")
    args = p.parse_args()
    url = urlsplit(args.backend)
    if url.scheme != "http" or url.hostname not in ("127.0.0.1", "localhost") or url.path or url.username or url.password:
        p.error("Backend must be an HTTP loopback origin")
    config = json.loads(args.config.read_text())
    controller = Controller(args.backend, config, args.prompt.read_text() if args.prompt else None)
    cache_info = {"source": "disabled", "decision_reused": False}
    if args.policy_cache:
        from policy_cache import prepare
        cache_info = prepare(controller, args.policy_cache)
    config_hash = hashlib.sha256(args.config.read_bytes()).hexdigest()
    lock = threading.Lock()
    model_lock = threading.Lock()
    counters = {"requests": 0, "errors": 0}
    prefill = {"enabled": args.prefill_next_turn, "completed": 0, "errors": 0, "last_ms": None}
    if args.qa_log_dir:
        args.qa_log_dir.mkdir(parents=True, exist_ok=True)
        if any(args.qa_log_dir.iterdir()):
            p.error("Use a fresh empty QA log directory; existing evidence must not be overwritten")

    class Handler(BaseHTTPRequestHandler):
        def respond(self, status, body):
            data = json.dumps(body, separators=(",", ":")).encode()
            try:
                self.send_response(status)
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len(data)))
                self.end_headers()
                self.wfile.write(data)
            except (BrokenPipeError, ConnectionResetError):
                pass  # Unreal cancelled; its epoch check owns dispatch freshness.

        def do_GET(self):
            if self.path != "/health":
                self.respond(404, {"error": "unknown path"})
                return
            self.respond(200, {"status": "ok", "version": VERSION, "backend": args.backend,
                "config_sha256": config_hash, "prompt_sha256": controller.prompt_hash,
                "actions": ACTIONS, "token_ids": controller.token_ids, "fine_tuned": False,
                "policy_cache": cache_info,
                "next_turn_prefill": prefill,
                "recipe_vetoes_enabled": False, **counters})

        def do_POST(self):
            if self.path != "/v1/chat/completions":
                self.respond(404, {"error": "unknown path"})
                return
            try:
                length = int(self.headers.get("Content-Length", "0"))
                if not 0 < length <= 262144:
                    raise ValueError("Invalid request size")
                self.connection.settimeout(120)
                payload = json.loads(self.rfile.read(length))
                history = validate_payload(payload, config)
            except (ValueError, TypeError, KeyError) as exc:
                self.respond(400, {"error": str(exc)})
                return
            if not lock.acquire(blocking=False):
                self.respond(503, {"error": "Controller is busy; no request queued"})
                return
            record = {"version": VERSION, "prompt_sha256": controller.prompt_hash}
            start = time.monotonic()
            preparation = None
            # At most one actual request waits for a previous envelope prefill.
            # Both use one native slot. Cache matching discards any stale suffix;
            # a prepared prefix cannot dispatch a job or supply an observation.
            model_lock.acquire()
            try:
                counters["requests"] += 1
                response, diagnostic = controller.decide(history)
                record.update(diagnostic=diagnostic, response=response)
                self.respond(200, response)
                if args.prefill_next_turn and response["single_token"]["code"] not in ("I", "M"):
                    preparation = (diagnostic["model_messages"], response["single_token"]["code"])
            except Exception as exc:
                counters["errors"] += 1
                record["error"] = str(exc)
                if isinstance(exc, DecisionError):
                    record["diagnostic"] = exc.diagnostic
                self.respond(502, {"error": str(exc)})
            finally:
                record["http_ms"] = (time.monotonic()-start)*1000
                request_number = counters["requests"]
                lock.release()
                # The HTTP response has already released the physical job. No
                # expected completion status, gate code or timestamp is guessed.
                try:
                    if preparation is not None:
                        from policy_cache import tool_prefix, warm
                        began = time.monotonic()
                        raw = warm(controller, tool_prefix(controller, *preparation))
                        prefill.update(completed=prefill["completed"]+1, last_ms=(time.monotonic()-began)*1000)
                        record["next_turn_prefill"] = {"ms": prefill["last_ms"], "discarded_response": raw}
                except Exception as exc:
                    prefill["errors"] += 1
                    record["next_turn_prefill"] = {"error": str(exc)}
                    print(f"Optional next-turn prefill failed: {exc}", flush=True)
                finally:
                    model_lock.release()
                if args.qa_log_dir and request_number <= 64:
                    try:
                        (args.qa_log_dir / f"request-{request_number:03d}.json").write_text(json.dumps(record, indent=2)+"\n")
                    except OSError as exc:
                        print(f"QA log failure: {exc}", flush=True)

    print(json.dumps({"version": VERSION, "port": args.port, "backend": args.backend,
                      "prompt_sha256": controller.prompt_hash, "token_ids": controller.token_ids}), flush=True)
    server = ThreadingHTTPServer(("127.0.0.1", args.port), Handler)
    server.daemon_threads = True
    server.serve_forever()


if __name__ == "__main__":
    main()
