#!/usr/bin/env python3
"""Serve two validated automotive QAIRT bundles through the existing gate protocol."""
import argparse
from collections import Counter
import hashlib
import io
import json
from pathlib import Path
import signal
import sys
import tempfile
import threading
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "src"))
from dark_factory.gates import load_gate_contract, validate_adapter_training, hold_reply
from dark_factory.gates_host import BoundedHTTPServer, make_handler
from probe_qairt_reason2 import Worker, score, sha_file, worker_execution


def validate_evidence(provenance, evidence, worker_sha, role, contract, execution="pipeline-callback-v1"):
    stages = {"weld": 72} if role == "weld" else {"panel_presence": 24, "panel_seated": 24, "adhesive": 72}
    validate_adapter_training(provenance["training_report"], contract, required_stages=list(stages))
    if provenance.get("schema") != "automotive-qairt-candidate-v1" or provenance.get("role") != role or set(provenance["stages"]) != set(stages):
        raise ValueError("Wrong candidate stage routing")
    if evidence.get("experiment") or evidence.get("candidate_provenance") != provenance:
        raise ValueError("Evidence used different weights or experimental prompts")
    if evidence.get("worker_sha256") != worker_sha or evidence.get("bundle_sha256") != provenance["bundle_sha256"] or evidence.get("decision_policy") != contract["decision_policy"]:
        raise ValueError("Evidence does not match this worker/bundle/policy")
    if execution not in ("pipeline-callback-v1", "prefill-only-v1", "prefill-fused-v1", "offline-stream-prefill-v1") or evidence.get("worker_execution", "pipeline-callback-v1") != execution:
        raise ValueError("Evidence used a different native execution path")
    rows = evidence.get("rows", [])
    if Counter(r["stage"] for r in rows) != stages or len({r["sample"] for r in rows}) != len(rows):
        raise ValueError("Incomplete or repeated development validation")
    errors = []
    for row in rows:
        if row.get("execution", "pipeline-callback-v1") != execution:
            raise ValueError("Validation row used a different native execution path")
        if "error" in row or row.get("decision") != score(row, contract)["decision"]:
            raise ValueError("Failed inference or inconsistent validation score")
        if row["decision"] != row["expected"]:
            errors.append(row)
    # Match the retained 191/192 development baseline: at most one conservative
    # cool-sealant hold, never a false G or a wrong NIR branch/seating decision.
    if len(errors) > (1 if role == "primary" else 0) or any(r["stage"] != "adhesive" or r["expected"] != "G" or r["decision"] not in ("A", "R") for r in errors):
        raise ValueError("Compiled accuracy is below the retained development baseline")


class QairtRouter:
    source = "evk_hexagon"
    dtype = "W8 text / FP16 text activations / W8A16 vision"
    # Same complete-logits/mass calculation as the Torch host protocol. The
    # native worker's wire schema is recorded separately; no UE policy change.
    scoring_method = "gar-full-vocabulary-logits-v1"
    vision_patch_embedding = "qairt-npu-compiled"
    gpu_name = "IQ9 Hexagon NPU / QAIRT vision and text"
    model_name = "nvidia/Cosmos-Reason2-2B/automotive-qairt-stage-adapters"

    def select_execution(self, args):
        return worker_execution(args.prefill_only, args.fused_processor)

    def check_evidence(self, provenance, evidence, worker_sha, role, contract):
        validate_evidence(provenance, evidence, worker_sha, role, contract, self.execution)

    def worker_command(self, args, role):
        command = [str(args.worker.resolve()), str(getattr(args, role).resolve()), str(args.qairt_lib.resolve())]
        if args.prefill_only:
            command.append("--prefill-only")
        if args.fused_processor:
            command.append("--fused-processor")
        return command

    def __init__(self, args, contract):
        self.contract, self.lock, self.workers = contract, threading.Lock(), {}
        self.provenance, self.adapter, self.stage_backend_aliases = {}, {}, {}
        self.counter = 0
        self.execution = self.select_execution(args)
        worker_sha = sha_file(args.worker)
        for role in ("primary", "weld"):
            bundle = getattr(args, role).resolve()
            provenance = json.loads((bundle / "automotive_provenance.json").read_text())
            evidence = json.loads(getattr(args, role + "_validation").read_text())
            self.check_evidence(provenance, evidence, worker_sha, role, contract)
            for name, expected in provenance["bundle_sha256"].items():
                if Path(name).name != name or sha_file(bundle / name) != expected:
                    raise ValueError("Compiled model file hash mismatch")
            self.provenance[role] = provenance
            self.adapter[role] = provenance["adapter"]
            for stage in provenance["stages"]:
                self.stage_backend_aliases[stage] = provenance["model_alias"]
        self.dtype = "; ".join(f"{role}: {p['precision']}" for role, p in self.provenance.items())
        self.model_config_sha256 = self.provenance["primary"]["training_report"]["base_model_config_sha256"]
        self.model_alias = "automotive-qairt-router-" + hashlib.sha256(json.dumps(self.stage_backend_aliases, sort_keys=True).encode()).hexdigest()[:24]
        args.logs.mkdir(parents=True, exist_ok=True)
        try:
            for role in ("primary", "weld"):
                command = self.worker_command(args, role)
                self.workers[role] = Worker(command, args.logs / f"{role}.log")
        except Exception:
            self.close()
            raise

    def is_ready(self):
        return len(self.workers) == 2 and all(w.process.poll() is None for w in self.workers.values())

    def close(self):
        for worker in self.workers.values():
            worker.close()
        self.workers.clear()

    def inspect(self, request):
        from PIL import Image, ImageChops
        started = time.perf_counter()
        stage = request.identity["stage"]
        role = "weld" if stage == "weld" else "primary"
        if not self.is_ready():
            raise RuntimeError("Compiled worker unavailable")
        self.counter += 1
        # Local temporary pixels are removed after every request; the model sees
        # only images, the versioned prompt and an opaque transport counter.
        with tempfile.TemporaryDirectory(prefix="automotive-qairt-") as temp:
            paths = []
            for index, png in enumerate(request.pngs):
                with Image.open(io.BytesIO(png)) as im:
                    if "A" in im.getbands() and im.getchannel("A").getextrema() != (255, 255):
                        raise ValueError("Sensor PNG alpha must be opaque")
                    r, g, b = im.convert("RGB").split()
                    if ImageChops.difference(r, g).getbbox() or ImageChops.difference(r, b).getbbox():
                        raise ValueError("Observer palettes are not model input")
                path = Path(temp) / f"camera-{index}.png"
                path.write_bytes(png)
                paths.append(str(path))
            preprocessing_ms = (time.perf_counter() - started) * 1000
            try:
                result = score(self.workers[role].infer({"request_id": str(self.counter), "images": paths,
                    "prompt": self.contract["stages"][stage]["prompt"]}, 8), self.contract)
                if result.get("execution", "pipeline-callback-v1") != self.execution:
                    raise ValueError("Native worker used an unexpected execution path")
            except Exception:
                # A timed-out native job cannot later become another request's
                # response. Require a clean service restart after worker failure.
                self.close()
                raise
        elapsed = (time.perf_counter() - started) * 1000
        return self.make_reply(request, result, elapsed, preprocessing_ms)

    def make_reply(self, request, result, elapsed, preprocessing_ms=0):
        stage = request.identity["stage"]
        role = "weld" if stage == "weld" else "primary"
        reply = hold_reply(request.identity, self.contract, result["reason"], valid=True)
        reply.update(source=self.source, decision=result["decision"], model=self.model_name,
            adapter=self.adapter[role], backend_alias=self.model_alias, stage_backend_alias=self.stage_backend_aliases[stage],
            routing="resident-qairt-weld-override-v1", latency_ms=elapsed, scoring_method=self.scoring_method,
            native_scoring_method=result["scoring_method"],
            native_execution=self.execution,
            vision_patch_embedding=self.vision_patch_embedding, label_mass=result["label_mass"],
            conditional_label_scores=result["conditional_label_scores"], raw_top_token=result["raw_top_token"],
            image_sha256=[hashlib.sha256(png).hexdigest() for png in request.pngs],
            prompt_tokens=result["prompt_tokens"], visual_tokens=result["visual_tokens"],
            timing={"preprocessing_ms": preprocessing_ms, "inference_ms": result["latency_ms"], "total_ms": elapsed})
        return reply


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ("primary", "weld", "primary-validation", "weld-validation", "worker", "qairt-lib", "logs"):
        p.add_argument("--" + name, type=Path, required=True)
    p.add_argument("--port", type=int, default=18198)
    p.add_argument("--prefill-only", action="store_true", help="Opt-in native path requiring matching complete validation")
    p.add_argument("--fused-processor", action="store_true", help="Opt-in fixed processor requiring --prefill-only and matching validation")
    args = p.parse_args()
    router = QairtRouter(args, load_gate_contract())
    server = BoundedHTTPServer(("127.0.0.1", args.port), make_handler(router))
    server.daemon_threads = True
    def stop(*_):
        raise KeyboardInterrupt
    signal.signal(signal.SIGTERM, stop)
    print(json.dumps({"ready": True, "backend": router.model_alias, "source": router.source}), flush=True)
    try:
        server.serve_forever(poll_interval=.25)
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
        router.close()


if __name__ == "__main__":
    main()
