#!/usr/bin/env python3
"""Merge a private automotive adapter for runtime parity experiments."""
import argparse
import hashlib
import json
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "src"))
from dark_factory.gates import load_gate_contract, MESSAGE_LAYOUT, validate_adapter_training

def sha_file(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(8 * 1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--base", required=True)
    p.add_argument("--adapter", required=True)
    p.add_argument("--output", required=True)
    p.add_argument("--contract")
    args = p.parse_args()
    output = Path(args.output)
    if output.exists() and any(output.iterdir()):
        p.error("Choose a new merge directory; existing weights are never overwritten")
    contract = load_gate_contract(args.contract)
    adapter = Path(args.adapter).resolve()
    training = json.loads((adapter / "training_report.json").read_text(encoding="utf-8"))
    validate_adapter_training(training, contract, hashlib.sha256((Path(args.base)/"config.json").read_bytes()).hexdigest())
    expected = {stage: spec["prompt_sha256"] for stage, spec in contract["stages"].items()}
    if training.get("message_layout") != MESSAGE_LAYOUT:
        raise ValueError("Adapter used a different multi-camera message layout")
    if training["contract_version"] != contract["version"] or training["prompt_hashes"] != expected:
        raise ValueError("Adapter training contract does not match current gates")
    import torch
    from peft import PeftModel
    from transformers import AutoProcessor, Qwen3VLForConditionalGeneration
    torch.set_num_threads(4)
    base = Qwen3VLForConditionalGeneration.from_pretrained(args.base, local_files_only=True, dtype=torch.bfloat16)
    probe_names = [name for name, tensor in base.named_parameters() if name.startswith("model.visual.") and name.endswith("qkv.weight")][:2]
    before = {name: hashlib.sha256(dict(base.named_parameters())[name].detach().contiguous().view(torch.uint8).numpy().tobytes()).hexdigest() for name in probe_names}
    model = PeftModel.from_pretrained(base, adapter, local_files_only=True).merge_and_unload().eval()
    after = {name: hashlib.sha256(dict(model.named_parameters())[name].detach().contiguous().view(torch.uint8).numpy().tobytes()).hexdigest() for name in probe_names}
    vision_checks = {name: {"before_sha256": before[name], "after_sha256": after[name], "changed": before[name] != after[name]} for name in probe_names}
    if training.get("lora_scope") == "multimodal" and not any(item["changed"] for item in vision_checks.values()):
        raise RuntimeError("Multimodal adapter did not change the sampled visual tensors")
    output.mkdir(parents=True, exist_ok=True)
    model.save_pretrained(output, safe_serialization=True, max_shard_size="5GB")
    AutoProcessor.from_pretrained(args.base, local_files_only=True).save_pretrained(output)
    provenance = {"model": "nvidia/Cosmos-Reason2-2B", "adapter": str(adapter), "contract_version": contract["version"],
                  "prompt_hashes": expected, "message_layout": MESSAGE_LAYOUT, "vision_merge_checks": vision_checks, "base_weights_sha256": sha_file(Path(args.base) / "model.safetensors"), "training_report_sha256": hashlib.sha256((adapter/"training_report.json").read_bytes()).hexdigest(),
                  "training_report": training, "scope": "Merged experimental automotive adapter; evaluate before live use."}
    (output / "adapter_provenance.json").write_text(json.dumps(provenance, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"merged": str(output.resolve()), "adapter": str(adapter)}))

if __name__ == "__main__":
    main()
