#!/usr/bin/env python3
"""Create a fresh FP16-vision candidate while preserving compiled-text inputs."""
import argparse
import json
import os
from pathlib import Path
import shutil


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--source", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    a = p.parse_args()
    if a.output.exists():
        raise ValueError("Use a fresh candidate directory")
    enc = json.loads((a.source / "vision_encoder.encodings").read_text())
    counts = {}
    for group in ("activation_encodings", "param_encodings"):
        counts[group] = len(enc[group])
        enc[group] = [{"name": e["name"], "bw": 16, "dtype": "FLOAT", "enc_type": "PER_TENSOR"} for e in enc[group]]
    metadata = json.loads((a.source / "args.json").read_text())
    metadata["vision_mixed_precision"] = {"all_vision_float16": True, "encoding_counts": counts}
    shutil.copytree(a.source, a.output, copy_function=os.link)
    for name, value in (("vision_encoder.encodings", enc), ("args.json", metadata)):
        (a.output / name).unlink()
        (a.output / name).write_text(json.dumps(value, indent=2) + "\n")
    print(json.dumps({"output": str(a.output), **counts}))


if __name__ == "__main__":
    main()
