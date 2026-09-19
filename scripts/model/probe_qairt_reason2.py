#!/usr/bin/env python3
"""Offline QAIRT validation. Batch labels never enter the native model worker."""
import argparse
from contextlib import suppress
from collections import Counter
import hashlib
import json
import math
from pathlib import Path
import queue
import shutil
import statistics
import subprocess
import sys
import threading
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "src"))
from dark_factory.gates import load_gate_contract, select_decision, validate_adapter_training
from dark_factory.gates_single_view import prompt_for, aggregate, VERSION
from train_lora import load_rows


def sha_file(path):
    h = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(8 * 1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def score(reply, contract):
    if "error" in reply:
        raise ValueError(reply["error"])
    if reply.get("scoring_method") != "gar-full-vocabulary-softmax-v1" or reply.get("generated_tokens") != 1:
        raise ValueError("Expected one token with unmasked full-vocabulary scores")
    p = reply.get("label_probabilities", {})
    if set(p) != {"G", "A", "R"} or any(type(x) not in (float, int) or not math.isfinite(x) or not 0 <= x <= 1 for x in p.values()):
        raise ValueError("Invalid label probabilities")
    mass = sum(p.values())
    if mass > 1.000001 or reply.get("vocab_size") != 151936:
        raise ValueError("Invalid vocabulary/mass")
    conditional = {k: v / mass if mass else 1 / 3 for k, v in p.items()}
    decision, reason = select_decision(conditional, min(mass, 1.0), contract["decision_policy"])
    return dict(reply, decision=decision, reason=reason, label_mass=mass, conditional_label_scores=conditional)


class Worker:
    """One persistent local child; serial requests, bounded waits, no stale replies."""
    def __init__(self, command, log, timeout=120):
        self.log = log.open("w", encoding="utf-8")
        self.lines = queue.Queue()
        self.process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                        stderr=self.log, text=True, bufsize=1)
        def read():
            for line in self.process.stdout:
                try:
                    item = json.loads(line)
                except ValueError:
                    self.log.write(line)
                    continue
                self.lines.put(item)
            self.lines.put(None)
        self.reader = threading.Thread(target=read, daemon=True)
        self.reader.start()
        try:
            ready = self.receive(timeout)
            if ready.get("ready") is not True or ready.get("schema") != "automotive-qairt-score-worker-v1" or ready.get("label_ids") != [38, 32, 49]:
                raise ValueError("Unexpected worker identity")
        except Exception:
            self.close()
            raise

    def receive(self, timeout):
        item = self.lines.get(timeout=timeout)
        if not isinstance(item, dict):
            raise RuntimeError("Worker exited without a response")
        return item

    def infer(self, request, timeout):
        started = time.perf_counter()
        self.process.stdin.write(json.dumps(request) + "\n")
        self.process.stdin.flush()
        reply = self.receive(timeout)
        if reply.get("request_id") != request["request_id"]:
            raise ValueError("Worker response identity mismatch")
        reply["roundtrip_ms"] = (time.perf_counter() - started) * 1000
        return reply

    def close(self):
        if self.process.poll() is None:
            with suppress(OSError):
                self.process.stdin.close()
            try:
                self.process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                self.process.terminate()
                try:
                    self.process.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    self.process.kill()
                    self.process.wait()
        self.reader.join(timeout=2)
        self.process.stdout.close()
        with suppress(OSError):
            self.process.stdin.close()
        self.log.close()


def prepare(args, contract):
    from PIL import Image, ImageChops
    if args.output.exists():
        raise ValueError("Use a new private dataset directory")
    rows = [r for r in load_rows(args.manifest, contract, "validation") if r["stage"] in args.stages]
    if args.limit:
        rows = rows[:args.limit]
    if not rows:
        raise ValueError("No matching validation batches")
    (args.output / "images").mkdir(parents=True)
    for row in rows:
        images = []
        for path in row["images"]:
            with Image.open(path) as im:
                if im.size != (448, 256) or im.format != "PNG":
                    raise ValueError("Expected original sensor PNG")
                if "A" in im.getbands() and im.getchannel("A").getextrema() != (255, 255):
                    raise ValueError("Nonopaque sensor pixels")
                r, g, b = im.convert("RGB").split()
                if ImageChops.difference(r, g).getbbox() or ImageChops.difference(r, b).getbbox():
                    raise ValueError("Expected raw grayscale sensor pixels")
            sha = sha_file(path)
            name = f"images/{sha}.png"
            if not (args.output / name).exists():
                shutil.copyfile(path, args.output / name)
            images.append({"path": name, "sha256": sha})
        row["images"] = images
    (args.output / "manifest.json").write_text(json.dumps({"schema": "automotive-qairt-validation-v1",
        "source_sha256": sha_file(args.manifest), "scope": "Existing development validation, not new held-out accuracy",
        "rows": rows}, indent=2) + "\n")
    print(json.dumps({"batches": len(rows), "stages": dict(Counter(r["stage"] for r in rows))}))


def run(args, contract):
    manifest = json.loads(args.manifest.read_text())
    if manifest["schema"] != "automotive-qairt-validation-v1":
        raise ValueError("Unknown validation schema")
    provenance = json.loads(args.provenance.read_text())
    rows = manifest["rows"][:args.limit or None]
    validate_adapter_training(provenance["training_report"], contract, required_stages=set(r["stage"] for r in rows))
    if args.output.exists():
        raise ValueError("Use a new report path")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    files = [f"part{i}_of_4.bin" for i in range(1, 5)] + ["vision_encoder.bin", "metadata.json", "tokenizer.json", "embedding_weights.raw", "genie_config.json", "htp_backend_ext_config.json"]
    report = {"scope": manifest["scope"], "backend": "QAIRT 2.45 / GenieX v0.3.17 / IQ9 NPU",
              "validation_source_sha256": manifest["source_sha256"],
              "candidate_provenance": provenance, "bundle_sha256": {f: sha_file(args.bundle / f) for f in files},
              "worker_sha256": sha_file(args.worker), "decision_policy": contract["decision_policy"], "rows": []}
    if provenance.get("schema") != "automotive-qairt-candidate-v1" or provenance.get("bundle_sha256") != report["bundle_sha256"]:
        raise ValueError("Bundle does not match its recorded adapter export")
    if not set(r["stage"] for r in rows) <= set(provenance["stages"]):
        raise ValueError("Validation requested an unconfigured stage route")
    if args.single_view:
        report.update(experiment=VERSION, adapter_single_view_validated=False,
                      label_scope="Whole-batch labels only; individual views have no ground truth")
    worker = Worker([str(args.worker.resolve()), str(args.bundle.resolve()), str(args.qairt_lib.resolve())], args.output.with_suffix(".worker.log"))
    try:
        for index, source in enumerate(rows):
            stage = source["stage"]
            if source["split"] != "validation" or source["prompt_sha256"] != contract["stages"][stage]["prompt_sha256"] or source["prompt"] != contract["stages"][stage]["prompt"]:
                raise ValueError("Validation split or prompt mismatch")
            paths = [(args.manifest.parent / im["path"]).resolve() for im in source["images"]]
            if [sha_file(p) for p in paths] != [im["sha256"] for im in source["images"]]:
                raise ValueError("Validation image changed")
            row = {"sample": source["sample_id"], "stage": stage, "expected": source["answer"],
                   "image_sha256": [im["sha256"] for im in source["images"]]}
            started = time.perf_counter()
            try:
                if args.single_view:
                    row["views"] = [dict(score(worker.infer({"request_id": f"{index}-{j}", "images": [str(path)],
                        "prompt": prompt_for(stage)}, args.timeout), contract), camera_id=f"spot{j+1}") for j, path in enumerate(paths)]
                    row["decision"], row["reason"] = aggregate(stage, row["views"])
                else:
                    row.update(score(worker.infer({"request_id": str(index), "images": list(map(str, paths)),
                        "prompt": source["prompt"]}, args.timeout), contract))
            except Exception as exc:
                row.update(decision="A", error=f"{type(exc).__name__}: {exc}")
            row["total_ms"] = (time.perf_counter() - started) * 1000
            report["rows"].append(row)
            rs = report["rows"]
            report["summary"] = {"batches": len(rs), "correct": sum(r["decision"] == r["expected"] and "error" not in r for r in rs),
                "errors": sum("error" in r for r in rs), "false_G": sum(r["decision"] == "G" and r["expected"] != "G" for r in rs),
                "confusion": dict(Counter(r["expected"] + "->" + r["decision"] for r in rs)),
                "median_ms": statistics.median(r["total_ms"] for r in rs), "max_ms": max(r["total_ms"] for r in rs)}
            args.output.write_text(json.dumps(report, indent=2) + "\n")
            print(json.dumps({k: row[k] for k in ("sample", "stage", "expected", "decision", "total_ms")}), flush=True)
            if "error" in row:
                raise RuntimeError(row["error"])
    finally:
        worker.close()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    sub = p.add_subparsers(dest="operation", required=True)
    prep = sub.add_parser("prepare")
    prep.add_argument("--manifest", type=Path, required=True)
    prep.add_argument("--stages", nargs="+", required=True)
    prep.add_argument("--output", type=Path, required=True)
    prep.add_argument("--limit", type=int, default=0)
    probe = sub.add_parser("run")
    for option in ("manifest", "provenance", "bundle", "worker", "qairt-lib", "output"):
        probe.add_argument("--" + option, type=Path, required=True)
    probe.add_argument("--single-view", action="store_true")
    probe.add_argument("--timeout", type=float, default=30)
    probe.add_argument("--limit", type=int, default=0)
    args = p.parse_args()
    contract = load_gate_contract()
    (prepare if args.operation == "prepare" else run)(args, contract)


if __name__ == "__main__":
    main()
