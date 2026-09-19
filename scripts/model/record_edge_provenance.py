#!/usr/bin/env python3
"""Bind a private GGUF decoder to its adapted FP16 vision encoder and source export."""
import argparse
import hashlib
import json
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "src"))
from dark_factory.gates import load_gate_contract, MESSAGE_LAYOUT, validate_adapter_training


def sha(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(8 * 1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def validate(report, contract):
    precision = report.get("precision", "").split(" ")[0]
    if report.get("schema") != "automotive-edge-gguf-v1" or precision not in ("Q4_0", "Q8_0", "BF16") or report["precision"] != precision + " text / F16 vision":
        raise ValueError("Unsupported edge export schema or precision")
    source = report["source_export"]
    if source.get("model") != "nvidia/Cosmos-Reason2-2B" or not source.get("adapter"):
        raise ValueError("An adapted automotive Reason2 export is required")
    adapter = source["adapter_provenance"]
    validate_adapter_training(adapter["training_report"], contract, required_stages=contract["stages"])
    if adapter.get("message_layout") != MESSAGE_LAYOUT or adapter.get("contract_version") != contract["version"]:
        raise ValueError("Adapter image layout/contract mismatch")
    if adapter.get("prompt_hashes") != {s: c["prompt_sha256"] for s, c in contract["stages"].items()}:
        raise ValueError("Adapter prompt hashes differ")
    if report["files"]["vision"]["sha256"] != source["files"]["vision_f16"]["sha256"]:
        raise ValueError("Vision encoder must be the matching adapted export")
    if precision == "BF16" and report["files"]["text"]["sha256"] != source["files"]["text_bf16"]["sha256"]:
        raise ValueError("BF16 text must be the matching original export")
    identity = report["files"]["text"]["sha256"] + report["files"]["vision"]["sha256"]
    expected = "automotive-reason2-" + precision.lower() + "-" + hashlib.sha256(identity.encode()).hexdigest()[:24]
    if report.get("model_alias") != expected:
        raise ValueError("Model alias does not bind both weight files")
    return report


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--source-provenance", type=Path, required=True)
    p.add_argument("--text", type=Path, required=True)
    p.add_argument("--vision", type=Path, required=True)
    p.add_argument("--quantizer", type=Path, required=True)
    p.add_argument("--precision", choices=("Q4_0", "Q8_0", "BF16"), default="Q4_0")
    p.add_argument("--output", type=Path, required=True)
    args = p.parse_args()
    if args.output.exists():
        p.error("Choose a new provenance output; previous exports are preserved")
    source = json.loads(args.source_provenance.read_text())
    files = {key: {"name": path.name, "bytes": path.stat().st_size, "sha256": sha(path)}
             for key, path in (("text", args.text), ("vision", args.vision))}
    identity = files["text"]["sha256"] + files["vision"]["sha256"]
    if args.precision == "BF16" and files["text"]["sha256"] != source["files"]["text_bf16"]["sha256"]:
        raise ValueError("BF16 text must match the original export")
    report = {"schema": "automotive-edge-gguf-v1", "precision": args.precision + " text / F16 vision",
              "model_alias": "automotive-reason2-" + args.precision.lower() + "-" + hashlib.sha256(identity.encode()).hexdigest()[:24],
              "files": files, "source_export": source, "source_provenance_sha256": sha(args.source_provenance),
              "quantizer_sha256": sha(args.quantizer), "quantizer_revision": source["llama_revision"],
              "quantization_command": "none" if args.precision == "BF16" else f"llama-quantize INPUT-BF16 OUTPUT {args.precision} 12",
              "validation": "unvalidated export; measured device results are recorded separately"}
    validate(report, load_gate_contract())
    args.output.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({"model_alias": report["model_alias"], "files": files}))


if __name__ == "__main__":
    main()
