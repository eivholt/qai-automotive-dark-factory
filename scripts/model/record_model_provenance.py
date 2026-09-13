#!/usr/bin/env python3
"""Record hashes of private local model exports; no weights are redistributed."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

def sha(path):
    h = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(8 * 1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--base", required=True)
    p.add_argument("--gguf", required=True)
    p.add_argument("--llama-source", required=True)
    p.add_argument("--automotive-adapter", action="store_true")
    args = p.parse_args()
    base, gguf, llama = Path(args.base), Path(args.gguf), Path(args.llama_source)
    adapter_provenance = json.loads((base / "adapter_provenance.json").read_text(encoding="utf-8")) if args.automotive_adapter else None
    text_name = "Cosmos-Reason2-2B-Automotive-LoRA-BF16.gguf" if args.automotive_adapter else "Cosmos-Reason2-2B-base-BF16.gguf"
    vision_name = "mmproj-Cosmos-Reason2-2B-Automotive-LoRA-F16.gguf" if args.automotive_adapter else "mmproj-Cosmos-Reason2-2B-base-F16.gguf"
    files = {"input_weights": base / "model.safetensors", "model_config": base / "config.json",
             "text_bf16": gguf / text_name, "vision_f16": gguf / vision_name}
    report = {"model": "nvidia/Cosmos-Reason2-2B", "adapter": adapter_provenance["adapter"] if adapter_provenance else None, "adapter_provenance": adapter_provenance, "dtype": "BF16 text, F16 vision projection", "files": {k: {"path": str(v.resolve()), "sha256": sha(v), "bytes": v.stat().st_size} for k,v in files.items()},
              "llama_revision": subprocess.check_output(["git", "-C", str(llama), "rev-parse", "HEAD"], text=True).strip()}
    (gguf / "provenance.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"model": report["model"], "adapter": report["adapter"], "llama_revision": report["llama_revision"]}))

if __name__ == "__main__":
    main()
