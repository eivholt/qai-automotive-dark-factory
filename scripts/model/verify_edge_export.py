#!/usr/bin/env python3
"""Verify both files of a private adapted GGUF bundle before starting a server."""
import argparse
import json
from pathlib import Path
from record_edge_provenance import load_gate_contract, sha, validate


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("directory", type=Path)
    p.add_argument("--provenance", default="edge-provenance.json")
    p.add_argument("--details", action="store_true")
    args = p.parse_args()
    report = validate(json.loads((args.directory / args.provenance).read_text()), load_gate_contract())
    for item in report["files"].values():
        if Path(item["name"]).name != item["name"]:
            raise ValueError("Export file names must be plain local basenames")
        path = args.directory / item["name"]
        if path.stat().st_size != item["bytes"] or sha(path) != item["sha256"]:
            raise ValueError(f"Export hash/size mismatch: {path.name}")
    print(report["model_alias"])
    if args.details:
        print(report["files"]["text"]["name"])
        print(report["files"]["vision"]["name"])


if __name__ == "__main__":
    main()
