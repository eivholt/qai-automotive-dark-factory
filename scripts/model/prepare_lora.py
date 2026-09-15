#!/usr/bin/env python3
"""Create reproducible private one-token LoRA manifests, split by whole episode."""
import argparse
from collections import Counter, defaultdict
import hashlib
import json
import os
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "src"))
from dark_factory.gates import load_gate_contract, CONTRACT_VERSION


def episode_split(episode_id, salt):
    bucket = int(hashlib.sha256((salt + ":" + str(episode_id)).encode()).hexdigest()[:8], 16) % 100
    return "train" if bucket < 70 else "validation" if bucket < 85 else "test"


def prepare(samples, root, output, contract, grouping, salt):
    result = defaultdict(list)
    assigned = {}
    for sample in samples:
        episode = str(sample.get("episode_id", sample["body_id"]))
        split = sample.get("split") or episode_split(episode, salt)
        if split not in ("train", "validation", "test"):
            raise ValueError("Unknown dataset split")
        if episode in assigned and assigned[episode] != split:
            raise ValueError("An episode spans multiple dataset splits")
        assigned[episode] = split
        label = sample["expected_decision"]
        if label not in ("G", "A", "R"):
            raise ValueError("Each training example requires one explicit label")
        stage = sample["stage"]
        spec = contract["stages"][stage]
        if sample.get("contract_version") != contract["version"]:
            raise ValueError("Training capture contract version mismatch")
        if sample.get("prompt_sha256") != spec["prompt_sha256"]:
            raise ValueError("Training capture prompt hash mismatch")
        if [im["camera_id"] for im in sample["images"]] != spec["camera_ids"]:
            raise ValueError("Training camera batch mismatch")
        group = "shared" if grouping == "shared" else spec["modality"] if grouping == "modality" else stage
        source_images = [(root / im["path"]).resolve() for im in sample["images"]]
        if any(not path.is_file() for path in source_images):
            raise ValueError("Missing training camera image")
        images = [Path(os.path.relpath(path, (output / group).resolve())).as_posix() for path in source_images]
        result[(group, split)].append({"sample_id": sample["sample_id"], "episode_id": episode,
            "stage": stage, "images": images, "camera_ids": spec["camera_ids"], "modality": spec["modality"], "split": split,
            "prompt": spec["prompt"], "answer": label,
            "contract_version": contract["version"], "prompt_sha256": spec["prompt_sha256"]})
    output.mkdir(parents=True, exist_ok=True)
    counts = {}
    for (group, split), rows in result.items():
        folder = output / group
        folder.mkdir(exist_ok=True)
        (folder / (split + ".jsonl")).write_text("".join(json.dumps(row) + "\n" for row in rows), encoding="utf-8")
        counts[group + "/" + split] = {"batches": len(rows), "episodes": len({r["episode_id"] for r in rows}), "labels": dict(Counter(r["answer"] for r in rows))}
    report = {"contract_version": contract["version"], "grouping": grouping, "split_salt": salt,
        "whole_episode_splits": assigned, "counts": counts,
        "warning": "This prepares data only. Select adapters using validation episodes; evaluate the frozen selection once on held-out test episodes. Training fit is not acceptance."}
    (output / "preparation.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return report


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--manifest", required=True)
    p.add_argument("--output", required=True, help="An ignored/private output directory")
    p.add_argument("--grouping", choices=["shared", "modality", "stage"], default="modality")
    p.add_argument("--split-salt", default=CONTRACT_VERSION)
    p.add_argument("--contract")
    args = p.parse_args()
    manifest = Path(args.manifest).resolve()
    samples = [json.loads(line) for line in manifest.read_text(encoding="utf-8").splitlines() if line.strip()]
    print(json.dumps(prepare(samples, manifest.parent, Path(args.output), load_gate_contract(args.contract), args.grouping, args.split_salt), indent=2))

if __name__ == "__main__":
    main()
