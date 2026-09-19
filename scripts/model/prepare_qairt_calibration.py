#!/usr/bin/env python3
"""Prepare private, training-only static image calibration for the QAIRT encoder."""
import argparse
from collections import defaultdict, Counter
import hashlib
import json
from pathlib import Path
import random
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "src"))
from dark_factory.gates import load_gate_contract, validate_adapter_training
from train_lora import load_rows


def prepare(adapter, training, stages, count, seed):
    contract = load_gate_contract()
    report = json.loads((adapter / "training_report.json").read_text())
    validate_adapter_training(report, contract, required_stages=stages)
    digest = hashlib.sha256(training.read_bytes()).hexdigest()
    if digest != report["training_manifest_sha256"]:
        raise ValueError("Calibration must use this adapter's recorded training manifest")
    rows = load_rows(training, contract, "train")
    buckets = defaultdict(list)
    for row in rows:
        if row["stage"] in stages:
            for index, image in enumerate(row["images"]):
                buckets[(row["stage"], row["answer"])].append((row, index, Path(image)))
    expected = {(stage, label) for stage in stages for label in ("G", "A", "R")}
    if set(buckets) != expected:
        raise ValueError("Calibration lacks a stage/label cohort")
    rng = random.Random(seed)
    for key in sorted(buckets):
        rng.shuffle(buckets[key])
    pairs, seen = [], set()
    while len(pairs) < count:
        before = len(pairs)
        for key in sorted(buckets):
            while buckets[key]:
                row, index, image = buckets[key].pop()
                sha = hashlib.sha256(image.read_bytes()).hexdigest()
                if sha in seen:
                    continue
                from PIL import Image, ImageChops
                with Image.open(image) as source:
                    if source.size != (448, 256) or source.format != "PNG":
                        raise ValueError("Calibration requires original 448x256 PNGs")
                    if "A" in source.getbands() and source.getchannel("A").getextrema() != (255, 255):
                        raise ValueError("Calibration requires opaque sensor pixels")
                    r, g, b = source.convert("RGB").split()
                    if ImageChops.difference(r, g).getbbox() or ImageChops.difference(r, b).getbbox():
                        raise ValueError("Calibration requires raw grayscale sensor pixels")
                seen.add(sha)
                pairs.append({"id": f"automotive-{len(pairs):04d}", "frames": [str(image)] * 2,
                              "timestamps_seconds": [0.0, 1.0], "sha256": [sha] * 2,
                              "source": {"split": "train", "stage": row["stage"],
                                         "batch_label_for_stratification_only": row["answer"],
                                         "sample_id": row["sample_id"], "episode_id": row["episode_id"],
                                         "camera_id": row["camera_ids"][index]}})
                break
            if len(pairs) == count:
                break
        if len(pairs) == before:
            raise ValueError("Not enough distinct training images")
    return {"schema_version": 1, "temporal_mode": "duplicate_single_frame", "pairs": pairs,
            "scope": "Unlabeled vision activation calibration; no single-view accuracy claim",
            "adapter": str(adapter), "training_manifest_sha256": digest, "seed": seed,
            "stage_counts": dict(Counter(p["source"]["stage"] for p in pairs)),
            "camera_counts": dict(Counter(p["source"]["camera_id"] for p in pairs))}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--adapter", type=Path, required=True)
    p.add_argument("--training", type=Path, required=True)
    p.add_argument("--stages", nargs="+", required=True, choices=list(load_gate_contract()["stages"]))
    p.add_argument("--count", type=int, default=120)
    p.add_argument("--seed", type=int, default=20260919)
    p.add_argument("--output", type=Path, required=True)
    a = p.parse_args()
    if a.output.exists() or a.count < 1:
        p.error("Use a new output path and a positive image count")
    result = prepare(a.adapter.resolve(), a.training.resolve(), a.stages, a.count, a.seed)
    a.output.parent.mkdir(parents=True, exist_ok=True)
    a.output.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({k: v for k, v in result.items() if k != "pairs"}))


if __name__ == "__main__":
    main()
