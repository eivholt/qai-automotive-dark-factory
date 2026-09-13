#!/usr/bin/env python3
"""Verify the exact text/projector pair before starting its GPU backend."""
import argparse
import hashlib
import json
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "src"))
from dark_factory.gates import model_export_alias


def file_sha256(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(8 * 1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def verify_export(directory, text_file, projector_file, alias=None):
    directory = Path(directory)
    report = json.loads((directory / "provenance.json").read_text(encoding="utf-8"))
    if report.get("model") != "nvidia/Cosmos-Reason2-2B":
        raise ValueError("Expected local Cosmos-Reason2-2B export provenance")
    expected_alias = model_export_alias(report)
    if alias is not None and alias != expected_alias:
        raise ValueError("Backend alias disagrees with base/adapter provenance")
    for key, name in (("text_bf16", text_file), ("vision_f16", projector_file)):
        path = directory / name
        recorded = report["files"][key]
        if path.stat().st_size != recorded["bytes"] or file_sha256(path) != recorded["sha256"]:
            raise ValueError("Backend model/projector differs from recorded export: " + key)
    return {"event": "PROCESS_EXPORT_VERIFIED", "model_alias": expected_alias, "model": report["model"], "adapter": report.get("adapter"),
        "text_sha256": report["files"]["text_bf16"]["sha256"], "vision_sha256": report["files"]["vision_f16"]["sha256"]}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--directory", required=True)
    p.add_argument("--text", required=True)
    p.add_argument("--projector", required=True)
    p.add_argument("--alias")
    p.add_argument("--alias-only", action="store_true")
    args = p.parse_args()
    result = verify_export(args.directory, args.text, args.projector, args.alias)
    print(result["model_alias"] if args.alias_only else json.dumps(result), flush=True)

if __name__ == "__main__":
    main()
