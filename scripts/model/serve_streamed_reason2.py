#!/usr/bin/env python3
"""Live ordered image preparation with an unchanged final three-image gate."""
import argparse
import json
from pathlib import Path
import signal
import tempfile
import time

from serve_qairt_reason2 import QairtRouter, validate_evidence
from probe_qairt_reason2 import score
from dark_factory.gates import load_gate_contract, parse_gate_request, hold_reply, MAX_GATE_REQUEST_BYTES
from dark_factory.gates_host import make_handler, BoundedHTTPServer
from dark_factory.gates_stream import STREAM_PROTOCOL, StreamCoordinator, grayscale
from dark_factory.protocol import ProtocolError

EXECUTION = "offline-stream-prefill-v1"  # Pinned native ABI; live transport is versioned separately.


def validate_stream_evidence(provenance, evidence, worker_sha, role, contract):
    if evidence.get("complete") is not True or evidence.get("mode") != "parity" or evidence.get("incremental") is not True:
        raise ValueError("Complete incremental parity evidence required")
    if evidence.get("worker_sha256") != worker_sha or evidence.get("bundle_sha256") != provenance["bundle_sha256"]:
        raise ValueError("Streaming evidence worker/bundle mismatch")
    flattened = []
    for row in evidence.get("rows", []):
        stage = contract["stages"][row["stage"]]
        if row.get("source_prompt_sha256") != stage["prompt_sha256"] or row.get("candidate_prompt") != stage["prompt"] or row.get("candidate_prompt_sha256") != stage["prompt_sha256"]:
            raise ValueError("Altered streaming prompt evidence")
        candidate, baseline = row["candidate"], row["baseline"]
        if any(candidate.get(k) != baseline.get(k) for k in ("label_probabilities", "raw_top_id", "prompt_tokens", "decision")):
            raise ValueError("Streaming numerical regression")
        if candidate.get("early_prefill_tokens") != 128 or candidate.get("visual_tokens") != 336:
            raise ValueError("Evidence is not a complete joint three-view prefill")
        flattened.append(dict(candidate, sample=row["sample_id"], stage=row["stage"], expected=row["expected"]))
    validate_evidence(provenance, dict(evidence, candidate_provenance=provenance,
        worker_execution=EXECUTION, rows=flattened), worker_sha, role, contract, EXECUTION)


class StreamingRouter(QairtRouter):
    streaming_protocol = STREAM_PROTOCOL

    def select_execution(self, args):
        return EXECUTION

    def check_evidence(self, provenance, evidence, worker_sha, role, contract):
        validate_stream_evidence(provenance, evidence, worker_sha, role, contract)

    def worker_command(self, args, role):
        return [str(args.worker.resolve()), str(getattr(args, role).resolve()), str(args.qairt_lib.resolve()), "--incremental"]

    def __init__(self, args, contract):
        super().__init__(args, contract)
        self.streams = StreamCoordinator(contract, self.native)

    def native(self, role, op, **fields):
        if not self.is_ready():
            raise RuntimeError("Streaming worker unavailable")
        self.counter += 1
        try:
            reply = self.workers[role].infer(dict(request_id=str(self.counter), op=op, **fields), 8)
            if op == "cancel" and reply.get("error") == "Unknown or expired stream batch":
                # The pinned native ABI clears all state on this error, including
                # an expired batch. No scoring result can be recovered from it.
                return {"cancelled": True}
            if "error" in reply:
                raise RuntimeError(reply["error"])
            return reply
        except Exception:
            self.close()  # Never associate a delayed native response with another job.
            raise

    def inspect(self, request, session=None, epoch=None):
        started = time.perf_counter()
        streaming = None
        if session is not None:
            result, streaming = self.streams.finish(request, session, epoch)
            if result.get("execution") != EXECUTION:
                raise RuntimeError("Unexpected streaming execution")
        else:
            # Explicit compatibility path for older clients, host comparisons,
            # or -NoProcessStreaming. No prepared state is reused here.
            self.streams.expire()
            if self.streams.active:
                raise ProtocolError("Prepared batch requires a matching final stream header")
            with tempfile.TemporaryDirectory(prefix="automotive-stream-baseline-") as temp:
                images = []
                for index, png in enumerate(request.pngs):
                    grayscale(png)
                    path = Path(temp) / f"sensor-{index}.png"
                    path.write_bytes(png)
                    images.append(str(path))
                stage = request.identity["stage"]
                result = self.native("weld" if stage == "weld" else "primary", "baseline",
                    images=images, prompt=self.contract["stages"][stage]["prompt"])
            if result.get("execution") != "offline-stream-vision-v1":
                raise RuntimeError("Unexpected baseline execution")
        result = score(result, self.contract)
        reply = self.make_reply(request, result, (time.perf_counter() - started) * 1000)
        reply.update(native_execution=result["execution"], routing="resident-qairt-streamed-three-view-v1")
        if streaming:
            reply["streaming"] = streaming
        return reply


def streaming_handler(router):
    base = make_handler(router)

    class Handler(base):
        def do_GET(self):
            if self.path != "/v1/stream/capabilities":
                return super().do_GET()
            ready = router.is_ready()
            self.send_json(200 if ready else 503, dict(status="ready" if ready else "unavailable",
                contract_version=router.contract["version"], streaming_protocol=STREAM_PROTOCOL))

        def do_POST(self):
            payload, locked = {}, False
            try:
                if self.path not in ("/v1/inspect", "/v1/stream/prepare", "/v1/stream/cancel"):
                    return self.send_json(404, hold_reply(payload, router.contract, "unknown_endpoint"))
                size = int(self.headers.get("Content-Length", "0"))
                if not 0 < size <= MAX_GATE_REQUEST_BYTES or self.headers.get_content_type() != "application/json":
                    raise ProtocolError("Invalid stream HTTP envelope")
                payload = json.loads(self.rfile.read(size))
                locked = router.lock.acquire(timeout=7)
                if not locked:
                    return self.send_json(429, hold_reply(payload, router.contract, "model_busy"))
                if self.path == "/v1/stream/prepare":
                    result = router.streams.prepare(payload)
                elif self.path == "/v1/stream/cancel":
                    result = router.streams.cancel(payload)
                else:
                    request = parse_gate_request(payload, router.contract)
                    session = self.headers.get("X-DF-Stream-Session")
                    epoch = self.headers.get("X-DF-Stream-Epoch")
                    if (session is None) != (epoch is None):
                        raise ProtocolError("Incomplete stream headers")
                    if epoch is not None and (not epoch.isascii() or not epoch.isdecimal() or not 1 <= int(epoch) < 2**53):
                        raise ProtocolError("Invalid stream epoch header")
                    result = router.inspect(request, session, int(epoch) if epoch is not None else None)
                self.send_json(200, result)
            except (ValueError, ProtocolError) as error:
                self.send_json(400, hold_reply(payload, router.contract, str(error)))
            except Exception as error:
                print("STREAM_INFERENCE_ERROR", type(error).__name__, str(error), flush=True)
                self.send_json(503, hold_reply(payload, router.contract, "model_error"))
            finally:
                if locked:
                    router.lock.release()
    return Handler


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("primary", "weld", "primary-validation", "weld-validation", "worker", "qairt-lib", "logs"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--port", type=int, default=18198)
    args = parser.parse_args()
    router = StreamingRouter(args, load_gate_contract())
    server = BoundedHTTPServer(("127.0.0.1", args.port), streaming_handler(router))
    server.daemon_threads = True

    def stop(*_):
        raise KeyboardInterrupt
    signal.signal(signal.SIGTERM, stop)
    print(json.dumps({"ready": True, "streaming_protocol": STREAM_PROTOCOL}), flush=True)
    try:
        server.serve_forever(poll_interval=.25)
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
        router.close()


if __name__ == "__main__":
    main()
