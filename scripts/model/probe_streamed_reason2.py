#!/usr/bin/env python3
"""Offline paired tests of joint scoring with early vision encoding.

No HTTP listener, simulator action, live acceptance or per-image classification.
Only original images and prompts enter the worker; truth stays in the report.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import statistics
import time

from probe_qairt_reason2 import Worker, score, sha_file, load_gate_contract


def nir_pair_prompt(row):
    if row["stage"] not in ("panel_presence", "panel_seated"):
        raise ValueError("Two-view candidate is NIR-only")
    prompt = row["prompt"]
    if "Inspect three NIR" not in prompt or "in order spot1, spot2, spot3" not in prompt:
        raise ValueError("Unexpected source NIR prompt")
    return prompt.replace("Inspect three NIR", "Inspect two NIR").replace(
        "in order spot1, spot2, spot3", "in order spot1, spot2")


def save(path, data):
    temporary = path.with_suffix(".tmp")
    temporary.write_text(json.dumps(data, indent=2, allow_nan=False) + "\n")
    temporary.replace(path)


def summarize(report):
    rows = report["rows"]
    if report["mode"] == "lifecycle":
        return {"checks": len(report.get("checks", []))}
    if not rows:
        raise ValueError("No measured rows")
    if report["mode"] == "schedule":
        result = {}
        for stage in sorted({row["stage"] for row in rows}):
            measured = [row for row in rows if row["stage"] == stage and row["trial"] > 0]
            if Counter(row["mode"] for row in measured) != {"baseline": 5, "streamed": 5}:
                raise ValueError("Incomplete timing pairs")
            pairs = {trial: {r["mode"]: r for r in measured if r["trial"] == trial} for trial in range(1, 6)}
            if any(set(pair) != {"baseline", "streamed"} for pair in pairs.values()):
                raise ValueError("Repeated or missing timing trial")
            result[stage] = {
                **{mode + "_median_ms": statistics.median(r["elapsed_ms"] for r in measured if r["mode"] == mode)
                   for mode in ("baseline", "streamed")},
                "median_paired_saving_ms": statistics.median(pair["baseline"]["elapsed_ms"] -
                    pair["streamed"]["elapsed_ms"] for pair in pairs.values()),
                "exact_probability_pairs": sum(pair["baseline"]["result"]["label_probabilities"] ==
                    pair["streamed"]["result"]["label_probabilities"] for pair in pairs.values()),
            }
        return result
    if len({r["sample_id"] for r in rows}) != len(rows):
        raise ValueError("Repeated evaluation sample")
    summary = {}
    for stage in sorted({row["stage"] for row in rows}):
        selected = [r for r in rows if r["stage"] == stage]
        summary[stage] = {"batches": len(selected),
            "expected_labels": dict(Counter(r["expected"] for r in selected)),
            "exact_probabilities": sum(r["baseline"]["label_probabilities"] == r["candidate"]["label_probabilities"] for r in selected),
            "same_decisions": sum(r["baseline"]["decision"] == r["candidate"]["decision"] for r in selected),
            **{mode: {"correct": sum(r[mode]["decision"] == r["expected"] for r in selected),
                      "false_green": sum(r[mode]["decision"] == "G" and r["expected"] != "G" for r in selected),
                      "wrong_presence_red": sum(stage == "panel_presence" and r[mode]["decision"] == "R" and r["expected"] != "R" for r in selected)}
               for mode in ("baseline", "candidate")}}
    return summary


class Probe:
    def __init__(self, worker, contract):
        self.worker, self.contract, self.serial = worker, contract, 0

    def call(self, op, **fields):
        self.serial += 1
        reply = self.worker.infer(dict(op=op, request_id=f"probe-{self.serial}", **fields), 20)
        if "error" in reply:
            raise ValueError(reply["error"])
        return reply

    def baseline(self, paths, prompt):
        return score(self.call("baseline", images=paths, prompt=prompt), self.contract)

    def streamed(self, paths, prompt, gap=0):
        identity = f"batch-{self.serial}"
        start = time.monotonic()
        begin = self.call("begin", batch_id=identity, prompt=prompt, image_count=len(paths))
        frames = []
        for index, image in enumerate(paths):
            # Absolute arrivals, not delay-after-work. Emulates measured R4 timing.
            delay = start + index * gap - time.monotonic()
            if delay > 0:
                time.sleep(delay)
            frames.append(self.call("append", batch_id=identity, index=index, image=image))
        result = score(self.call("score", batch_id=identity), self.contract)
        result.update(end_to_end_ms=1000 * (time.monotonic() - start), begin=begin, frames=frames)
        return result


def lifecycle(probe, paths, prompt, report, output):
    reference = probe.baseline(paths, prompt)
    checks = report["checks"] = []

    def checked(name, action, reject=False):
        try:
            action()
        except ValueError:
            if not reject:
                raise
        else:
            if reject:
                raise AssertionError(f"{name} should have been rejected")
        checks.append(name)
        save(output, report)

    def begin(identity="epoch-1", count=3):
        return probe.call("begin", batch_id=identity, prompt=prompt, image_count=count)

    def append(index=0, identity="epoch-1", image=None):
        return probe.call("append", batch_id=identity, index=index, image=image or paths[0])

    def final(identity="epoch-1"):
        return probe.call("score", batch_id=identity)

    def equivalent():
        result = probe.streamed(paths, prompt)
        for key in ("label_probabilities", "raw_top_id", "prompt_tokens", "decision"):
            if result[key] != reference[key]:
                raise AssertionError(f"Fresh stream differs after invalidation: {key}")

    checked("score without begin", final, True)
    for count in (0, 1, 4, -1, True, 3.0):
        checked(f"invalid image count {count!r}", lambda: begin(count=count), True)
    begin(); append()
    checked("incomplete batch", final, True)
    checked("failed batch cannot append", lambda: append(1), True)
    begin(); append()
    checked("duplicate image", append, True)
    begin()
    checked("out of order image", lambda: append(1), True)
    begin(); append()
    checked("wrong identity", lambda: append(1, "epoch-2"), True)
    checked("identity error discards old context", final, True)
    begin(); append(); append(1)
    checked("cancel with a prepared prefix", lambda: probe.call("cancel", batch_id="epoch-1"))
    checked("cancelled score", final, True)
    begin(); append(); append(1)
    checked("replacement epoch", lambda: begin("epoch-2"))
    checked("replaced epoch cannot score", final, True)
    begin(); append()
    checked("failed pixel decode", lambda: append(1, image=paths[0] + ".absent"), True)
    checked("decode error drops cached pixels", final, True)
    begin(); append(); append(1)
    checked("extra untrusted field", lambda: probe.call("score", batch_id="epoch-1", ground_truth="G"), True)
    checked("fresh batch parity after errors", equivalent)
    checked("already consumed batch", lambda: final(f"batch-{probe.serial - 5}"), True)
    begin(); append(); append(1)
    time.sleep(8.05)
    checked("expired batch", final, True)
    checked("fresh batch parity after expiration", equivalent)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=("parity", "schedule", "nir-pair", "lifecycle"))
    parser.add_argument("--worker", type=Path, required=True)
    parser.add_argument("--bundle", type=Path, required=True)
    parser.add_argument("--qairt-libs", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--limit", type=int, default=0)
    parser.add_argument("--incremental", action="store_true")
    args = parser.parse_args()
    if args.output.exists():
        parser.error("Use a new private report path")
    contract = load_gate_contract()
    manifest = json.loads(args.manifest.read_text())
    rows = manifest["rows"]
    if any(row["split"] == "test" for row in rows):
        raise ValueError("Leave the reserved test split untouched in this development experiment")
    if args.mode == "nir-pair":
        rows = [r for r in rows if r["stage"] in ("panel_presence", "panel_seated")]
    if args.limit:
        rows = rows[:args.limit]
    for row in rows:
        expected = contract["stages"][row["stage"]]
        if row["prompt"] != expected["prompt"] or row["prompt_sha256"] != expected["prompt_sha256"]:
            raise ValueError("Original prompt provenance mismatch")
        for image in row["images"]:
            path = args.manifest.parent / image["path"]
            if sha_file(path) != image["sha256"]:
                raise ValueError("Input hash mismatch")
            image["absolute_path"] = str(path.resolve())
    if not rows:
        raise ValueError("Empty experiment")
    files = [f"part{i}_of_4.bin" for i in range(1, 5)] + ["vision_encoder.bin", "metadata.json",
             "tokenizer.json", "embedding_weights.raw", "genie_config.json", "htp_backend_ext_config.json"]
    report = {"schema": "offline-streamed-reason2-v1", "mode": args.mode,
        "scope": "Development experiment, no live approval or held-out claim",
        "manifest_sha256": sha_file(args.manifest), "worker_sha256": sha_file(args.worker),
        "bundle_sha256": {f: sha_file(args.bundle / f) for f in files},
        "decision_policy": contract["decision_policy"], "rows": []}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    worker = None
    try:
        command = [str(args.worker), str(args.bundle), str(args.qairt_libs)]
        if args.incremental:
            command.append("--incremental")
        report["incremental"] = args.incremental
        worker = Worker(command, args.output.with_suffix(".log"))
        report["worker_pid"] = worker.process.pid
        probe = Probe(worker, contract)
        paths = lambda row: [im["absolute_path"] for im in row["images"]]
        report["warmup"] = probe.baseline(paths(rows[0]), rows[0]["prompt"])
        if args.mode == "lifecycle":
            lifecycle(probe, paths(rows[0]), rows[0]["prompt"], report, args.output)
        elif args.mode == "schedule":
            selected = {}
            for row in rows:
                selected.setdefault(row["stage"], row)
            for stage, row in selected.items():
                for trial in range(6):  # First is explicitly excluded from summaries.
                    for mode in (["baseline", "streamed"] if trial % 2 else ["streamed", "baseline"]):
                        gap = .966 if stage == "adhesive" else .963
                        start = time.monotonic()
                        if mode == "streamed":
                            result = probe.streamed(paths(row), row["prompt"], gap)
                        else:
                            time.sleep(2 * gap)
                            result = probe.baseline(paths(row), row["prompt"])
                        report["rows"].append({"stage": stage, "trial": trial, "mode": mode,
                            "elapsed_ms": 1000 * (time.monotonic() - start), "result": result})
                        save(args.output, report)
        else:
            for index, row in enumerate(rows):
                # Alternate the paired execution order to reduce drift bias.
                calls = ["baseline", "candidate"] if index % 2 else ["candidate", "baseline"]
                results = {}
                candidate_prompt = nir_pair_prompt(row) if args.mode == "nir-pair" else row["prompt"]
                for mode in calls:
                    if mode == "baseline":
                        results[mode] = probe.baseline(paths(row), row["prompt"])
                    elif args.mode == "nir-pair":
                        results[mode] = probe.baseline(paths(row)[:2], candidate_prompt)
                    else:
                        results[mode] = probe.streamed(paths(row), candidate_prompt)
                report["rows"].append({"sample_id": row["sample_id"], "stage": row["stage"],
                    "expected": row["answer"], "image_sha256": [im["sha256"] for im in row["images"]],
                    "source_prompt_sha256": row["prompt_sha256"], "candidate_prompt": candidate_prompt,
                    "candidate_prompt_sha256": hashlib.sha256(candidate_prompt.encode()).hexdigest(), **results})
                save(args.output, report)
                print(json.dumps({"index": index, "stage": row["stage"], "expected": row["answer"],
                    "baseline": results["baseline"]["decision"], "candidate": results["candidate"]["decision"]}), flush=True)
        report["summary"] = summarize(report)
        report["complete"] = True
    except BaseException as error:
        report["error"] = str(error)
        raise
    finally:
        if worker:
            worker.close()
        save(args.output, report)


if __name__ == "__main__":
    main()
