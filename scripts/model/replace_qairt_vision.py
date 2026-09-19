#!/usr/bin/env python3
"""Join a same-adapter precision experiment with byte-identical compiled text."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
from package_qairt_candidate import FILES, sha


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ("original", "reference", "export", "checkpoint", "output"):
        p.add_argument("--" + name, type=Path, required=True)
    a = p.parse_args()
    original = json.loads((a.original / "automotive_provenance.json").read_text())
    sources = [f.parent for f in a.export.rglob("vision_encoder.bin") if (f.parent / "metadata.json").is_file()]
    if len(sources) != 1 or a.output.exists():
        raise ValueError("Require one encoder export and a new output directory")
    args = json.loads((a.checkpoint / "args.json").read_text())
    source = json.loads((a.checkpoint / "source_checkpoint.json").read_text())
    merged = json.loads((Path(source["source_checkpoint"]) / "adapter_provenance.json").read_text())
    if merged["training_report_sha256"] != original["training_report_sha256"] or merged["vision_merge_checks"] != original["vision_merge_checks"]:
        raise ValueError("Replacement encoder belongs to another adapter")
    subprocess.run([sys.executable, str(a.reference / "scripts/prepare_geniex_bundle.py"), "--source", str(a.original),
        "--destination", str(a.output), "--vision-replacement-bundle", str(sources[0]), "--hardlink",
        "--model-id", f"qwen3_vl_cosmos_reason2_automotive_{original['role']}_lastfp16"], check=True)
    files = {f: sha(a.output / f) for f in FILES}
    for f in [f"part{i}_of_4.bin" for i in range(1, 5)] + ["embedding_weights.raw", "tokenizer.json"]:
        if files[f] != original["bundle_sha256"][f]:
            raise ValueError("Vision replacement changed a retained text asset")
    vision_precision = "FP16 vision" if args.get("vision_mixed_precision", {}).get("all_vision_float16") else "W8A16 vision with final-block FLOAT16 activations"
    updated = dict(original, bundle_sha256=files, deployment_validated=False,
        model_alias="automotive-reason2-qairt-" + hashlib.sha256(json.dumps(files, sort_keys=True).encode()).hexdigest()[:24],
        precision="W8 text / FP16 text activations / " + vision_precision,
        vision_precision_override={"source_alias": original["model_alias"], "args": args,
            "encodings_sha256": sha(a.checkpoint / "vision_encoder.encodings"), "onnx_sha256": sha(a.checkpoint / "vision_encoder.onnx")})
    # The source bundle was hard-linked; replace through a new inode.
    temporary = a.output / ".automotive_provenance.json.tmp"
    temporary.write_text(json.dumps(updated, indent=2) + "\n")
    temporary.replace(a.output / "automotive_provenance.json")
    (a.output.parent / "bundle-ready.txt").write_text(updated["model_alias"] + "\n")
    print(json.dumps({"bundle": str(a.output), "alias": updated["model_alias"]}))


if __name__ == "__main__":
    main()
