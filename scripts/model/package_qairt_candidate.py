#!/usr/bin/env python3
"""Prepare an isolated, hash-bound QAIRT candidate; this does not deploy it."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "src"))
from dark_factory.gates import load_gate_contract, validate_adapter_training

FILES = [f"part{i}_of_4.bin" for i in range(1, 5)] + ["vision_encoder.bin", "metadata.json", "tokenizer.json",
    "embedding_weights.raw", "genie_config.json", "htp_backend_ext_config.json"]


def sha(path):
    h = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(8 * 1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--root", type=Path, required=True, help="Role work root containing merged/calibration.json/cl1024")
    p.add_argument("--reference", type=Path, required=True)
    p.add_argument("--role", choices=("primary", "weld"), required=True)
    p.add_argument("--context", type=int, choices=(512, 1024), default=1024)
    args = p.parse_args()
    work = args.root / f"cl{args.context}"
    destination = work / "bundle"
    if destination.exists():
        raise ValueError("Candidate already exists; do not overwrite recorded model binaries")
    if not (work / "export-complete.txt").exists():
        raise ValueError("Export did not complete")
    sources = [path.parent for path in (work / "export").rglob("metadata.json") if all((path.parent / f).is_file() for f in FILES)]
    if len(sources) != 1:
        raise ValueError("Expected exactly one complete downloaded export")
    source = sources[0]
    metadata = json.loads((source / "metadata.json").read_text())
    if not metadata["tool_versions"]["qairt"].startswith("2.45.0."):
        raise ValueError("Candidate requires the matching QAIRT 2.45 runtime")
    inputs = metadata["model_files"]["part1_of_4.bin"]["inputs"]
    if not {"visual_pos_masks", "deepstack_visual_embeds_0", "deepstack_visual_embeds_1", "deepstack_visual_embeds_2"} <= inputs.keys():
        raise ValueError("Full trained DeepStack interface is required")
    merged = json.loads((args.root / "merged/adapter_provenance.json").read_text())
    contract = load_gate_contract()
    stages = ["weld"] if args.role == "weld" else ["panel_presence", "adhesive", "panel_seated"]
    validate_adapter_training(merged["training_report"], contract, required_stages=stages)
    calibration = json.loads((args.root / "calibration.json").read_text())
    if calibration["training_manifest_sha256"] != merged["training_report"]["training_manifest_sha256"]:
        raise ValueError("Calibration came from another adapter's training data")
    if any(pair["source"]["split"] != "train" for pair in calibration["pairs"]):
        raise ValueError("Calibration must be training-only")
    subprocess.run([sys.executable, str(args.reference / "scripts/prepare_geniex_bundle.py"),
        "--source", str(source), "--destination", str(destination), "--hardlink",
        "--model-id", f"qwen3_vl_cosmos_reason2_automotive_{args.role}"], check=True)
    files = {f: sha(destination / f) for f in FILES}
    identity = hashlib.sha256(json.dumps(files, sort_keys=True).encode()).hexdigest()
    provenance = {**merged, "schema": "automotive-qairt-candidate-v1", "role": args.role, "stages": stages,
        "context_length": args.context, "precision": "W8 text matrices / FP16 text activations / calibrated W8A16 vision",
        "model_alias": "automotive-reason2-qairt-" + identity[:24], "bundle_sha256": files,
        "calibration_sha256": sha(args.root / "calibration.json"), "calibration_images": len(calibration["pairs"]),
        "reference_revision": (work / "reference-revision.txt").read_text().strip(),
        "quantization": json.loads((work / "w8-text/text_w8_matrices.json").read_text()),
        "compiler": metadata["tool_versions"], "deployment_validated": False}
    (destination / "automotive_provenance.json").write_text(json.dumps(provenance, indent=2) + "\n")
    shutil.copyfile(args.root / "merged/adapter_provenance.json", destination / "adapter_provenance.json")
    print(json.dumps({"bundle": str(destination), "model_alias": provenance["model_alias"]}))


if __name__ == "__main__":
    main()
