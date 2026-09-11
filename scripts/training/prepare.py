"""CPU-only capture planning, dataset freeze, and portable SFT records."""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "src"))
from dark_factory.dataset import canonical_hash, file_sha256, generate_manifest, read_jsonl, resolve_image, training_records, validate_manifest, write_jsonl
from dark_factory.protocol import load_contract, prompt_sha256


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    plan = sub.add_parser("plan", help="Write metadata only; does not render images")
    plan.add_argument("--config", type=Path, default=REPO / "configs/randomization.json")
    plan.add_argument("--output", type=Path, required=True)
    for name in ("validate", "freeze"):
        command = sub.add_parser(name)
        command.add_argument("--manifest", type=Path, required=True)
        command.add_argument("--dataset-root", type=Path, required=True)
        if name == "validate":
            command.add_argument("--plan-only", action="store_true", help="Skip real image checks for a capture plan")
        else:
            command.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    contract = load_contract()
    if args.command == "plan":
        config = json.loads(args.config.read_text(encoding="utf-8-sig"))
        rows = generate_manifest(config, contract)
        report = validate_manifest(rows, contract=contract)
        write_jsonl(args.output, rows)
        report["capture_status"] = "planned_no_images_rendered"
    else:
        rows = read_jsonl(args.manifest)
        report = validate_manifest(rows, args.dataset_root, contract, require_images=not getattr(args, "plan_only", False))
        if args.command == "freeze":
            if args.output.exists() and any(args.output.iterdir()):
                raise ValueError("Freeze output must be empty; keep earlier frozen manifests immutable")
            args.output.mkdir(parents=True, exist_ok=True)
            for row in rows:
                row["image_sha256"] = file_sha256(resolve_image(args.dataset_root, row["image"]))
            write_jsonl(args.output / "manifest.jsonl", rows)
            records = training_records(rows, contract)
            for split in ("train", "validation", "test"):
                write_jsonl(args.output / f"{split}.jsonl", [row for row in records if row["split"] == split])
            report["manifest_sha256"] = canonical_hash(rows)
            report["dataset_root"] = str(args.dataset_root.resolve())
            (args.output / "dataset_report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
            training_config = json.loads((REPO / "configs/training.json").read_text())
            release = {"schema_version": 1, "status": "NOT_EVALUATED", "scenario": rows[0].get("scenario"), "contract_version": contract["version"], "prompt_sha256": prompt_sha256(contract), "dataset_manifest_sha256": report["manifest_sha256"], "training_config_sha256": canonical_hash(training_config), "base_model": training_config["model"], "base_revision": None, "adapter_sha256": None, "merged_checkpoint_sha256": None, "qairt_bundle_sha256": None, "token_ids": None, "processor_sha256": None, "qairt_version": None, "geniex_version": None, "evk_os_build": None, "gates": training_config["release_gates"], "evaluation": {"host_validation": None, "host_test": None, "evk_direct_png": None, "evk_soak": None, "packaged_unreal": None}}
            (args.output / "release_candidate.json").write_text(json.dumps(release, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
