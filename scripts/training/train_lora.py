"""Explicit opt-in BF16 LoRA training with exactly one supervised class token."""
from __future__ import annotations

import argparse
import importlib.metadata
import json
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "src"))
from dark_factory.dataset import LABELS, canonical_hash, file_sha256, read_jsonl, resolve_image, validate_manifest
from dark_factory.protocol import load_contract, prompt_sha256


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--dataset-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--config", type=Path, default=REPO / "configs/training.json")
    parser.add_argument("--model", help="Local model directory or Hugging Face repository")
    parser.add_argument("--revision", help="Full immutable Hub commit hash (required for a remote model)")
    parser.add_argument("--dry-run", action="store_true", help="Validate plan/config without importing Torch, downloading a model or training")
    parser.add_argument("--check-processor", action="store_true", help="Check exact class tokens/context on one real frame, then exit without loading weights")
    parser.add_argument("--max-steps", type=int, default=-1, help="Optional short training pilot")
    args = parser.parse_args()
    config = json.loads(args.config.read_text(encoding="utf-8-sig"))
    contract = load_contract()
    rows = read_jsonl(args.manifest)
    report = validate_manifest(rows, args.dataset_root, contract, require_images=not args.dry_run)
    if config["objective"] != "one_class_token_only" or not config["bf16"]:
        raise ValueError("This baseline implements one-class-token BF16 LoRA")
    train_rows = [row for row in rows if row["split"] == "train"]
    model_name = args.model or config["model"]
    revision = args.revision or config.get("revision")
    provenance = {"status": "DRY_RUN" if args.dry_run else "STARTED", "model": model_name, "revision": revision, "contract_version": contract["version"], "prompt_sha256": prompt_sha256(contract), "manifest_sha256": report["manifest_sha256"], "training_config_sha256": canonical_hash(config), "train_samples": len(train_rows), "freeze_vision": config["freeze_vision"], "objective": config["objective"], "image_size": [contract["image"]["width"], contract["image"]["height"]]}
    if args.dry_run:
        print(json.dumps({**provenance, "dataset": report, "next_step": "Capture and freeze actual Unreal images, then check processor on a pinned model."}, indent=2))
        return
    local_model = Path(model_name).is_dir()
    if not local_model and (not revision or not re.fullmatch(r"[0-9a-fA-F]{40}", revision)):
        raise ValueError("Use a local model directory or --revision with an immutable 40-character model commit hash")
    if not args.check_processor and args.output.exists() and any(args.output.iterdir()):
        raise ValueError("Training output must be empty to preserve previous runs")

    # ML imports stay here: every planning/validation/test path runs on stdlib.
    import torch
    from PIL import Image
    from transformers import AutoProcessor, Qwen3VLForConditionalGeneration, Trainer, TrainingArguments, set_seed
    from peft import LoraConfig, get_peft_model

    set_seed(config["seed"])
    processor = AutoProcessor.from_pretrained(model_name, revision=revision, local_files_only=local_model)
    processor.tokenizer.padding_side = "right"
    token_ids = {label: processor.tokenizer.encode(label, add_special_tokens=False) for label in LABELS}
    if any(len(value) != 1 for value in token_ids.values()) or len({value[0] for value in token_ids.values()}) != 3:
        raise ValueError(f"G/A/R must be three distinct single tokens: {token_ids}")
    for label, ids in token_ids.items():
        if processor.tokenizer.decode(ids, skip_special_tokens=False) != label:
            raise ValueError("Class-token decoding does not preserve the exact response")
    messages = [{"role": "user", "content": [{"type": "image"}, {"type": "text", "text": contract["prompt"]}]}]
    prefix = processor.apply_chat_template(messages, tokenize=False, add_generation_prompt=True, enable_thinking=False)
    if "<think>" in prefix:
        raise ValueError("Chat template inserts hidden reasoning; it must match the one-token runtime")

    def collate(batch):
        images = []
        for row in batch:
            with Image.open(resolve_image(args.dataset_root, row["image"])) as source:
                source.load()  # Reject corrupt PNG payloads, not just invalid headers.
                if source.size != (contract["image"]["width"], contract["image"]["height"]):
                    raise ValueError("Image geometry changed")
                images.append(source.convert("RGB"))
        inputs = processor(text=[prefix] * len(batch), images=images, padding=True, do_resize=False, return_tensors="pt")
        inputs.pop("token_type_ids", None)
        # Every image has identical geometry and one shared prompt; no pad may
        # separate the assistant prefix and target. Refuse incompatible changes.
        if not bool(inputs["attention_mask"].all()):
            raise ValueError("Unexpected variable prompt lengths in fixed-camera contract")
        if inputs["input_ids"].shape[1] + 1 > config["max_context_tokens"]:
            raise ValueError("Prompt/image exceed the intended CL512 export context")
        expected_visual = contract["image"]["width"] * contract["image"]["height"] // (32 * 32)
        if any(int(grid.prod()) // 4 != expected_visual for grid in inputs["image_grid_thw"]):
            raise ValueError("Processor changed visual-token geometry")
        answers = torch.tensor([[token_ids[row["label"]][0]] for row in batch], dtype=inputs["input_ids"].dtype)
        inputs["input_ids"] = torch.cat((inputs["input_ids"], answers), dim=1)
        inputs["attention_mask"] = torch.cat((inputs["attention_mask"], torch.ones_like(answers)), dim=1)
        labels = torch.full_like(inputs["input_ids"], -100)
        labels[:, -1:] = answers
        inputs["labels"] = labels
        # Model's causal shift predicts exactly the class from the prompt. EOS,
        # image placeholders, prompt, and reasoning tokens receive no loss.
        assert bool((labels.ne(-100).sum(dim=1) == 1).all())
        return inputs

    sample = collate(train_rows[:1])
    provenance.update({"token_ids": token_ids, "prompt_template_sha256": canonical_hash(prefix), "context_tokens_including_answer": sample["input_ids"].shape[1], "visual_tokens": 112, "versions": {name: importlib.metadata.version(name) for name in ("torch", "transformers", "peft", "accelerate", "Pillow")}})
    if args.check_processor:
        print(json.dumps({**provenance, "status": "PROCESSOR_CHECKED_NO_WEIGHTS_LOADED"}, indent=2))
        return
    if not torch.cuda.is_available() or not torch.cuda.is_bf16_supported():
        raise RuntimeError("BF16 training requires a compatible CUDA GPU; use --dry-run on this machine")
    model = Qwen3VLForConditionalGeneration.from_pretrained(model_name, revision=revision, local_files_only=local_model, dtype=torch.bfloat16, attn_implementation="sdpa")
    model.config.use_cache = False
    targets = ["q_proj", "k_proj", "v_proj", "o_proj", "gate_proj", "up_proj", "down_proj"]
    if not config["freeze_vision"]:
        targets += ["qkv", "linear_fc1", "linear_fc2"]
    model = get_peft_model(model, LoraConfig(r=config["lora_rank"], lora_alpha=config["lora_alpha"], lora_dropout=config["lora_dropout"], target_modules=targets, bias="none", task_type="CAUSAL_LM"))
    if config["freeze_vision"]:
        for name, parameter in model.named_parameters():
            if "visual" in name:
                parameter.requires_grad = False
    trainable = [name for name, parameter in model.named_parameters() if parameter.requires_grad]
    if not trainable or any("lora_" not in name for name in trainable):
        raise RuntimeError("Only LoRA parameters may be trainable")
    if config["freeze_vision"] and any("visual" in name for name in trainable):
        raise RuntimeError("Vision tower must remain frozen")
    training_args = TrainingArguments(output_dir=str(args.output), num_train_epochs=config["epochs"], max_steps=args.max_steps, per_device_train_batch_size=config["batch_size"], gradient_accumulation_steps=config["gradient_accumulation"], learning_rate=config["learning_rate"], warmup_ratio=0.05, lr_scheduler_type="cosine", optim="adamw_torch", bf16=True, gradient_checkpointing=config["gradient_checkpointing"], gradient_checkpointing_kwargs={"use_reentrant": False}, logging_steps=1, save_strategy="no", report_to="none", remove_unused_columns=False, seed=config["seed"], data_seed=config["seed"])
    trainer = Trainer(model=model, args=training_args, train_dataset=train_rows, data_collator=collate)
    args.output.mkdir(parents=True, exist_ok=True)
    provenance["trainable_parameters"] = sum(parameter.numel() for parameter in model.parameters() if parameter.requires_grad)
    provenance["target_modules"] = targets
    if local_model:
        provenance["local_model_files_sha256"] = {str(path.relative_to(model_name)): file_sha256(path) for path in sorted(Path(model_name).rglob("*")) if path.is_file() and path.suffix in (".json", ".safetensors")}
    (args.output / "training_provenance.json").write_text(json.dumps(provenance, indent=2) + "\n", encoding="utf-8")
    result = trainer.train()
    trainer.save_model(str(args.output))
    processor.save_pretrained(str(args.output))
    provenance.update({"status": "TRAINED_NOT_RELEASED", "metrics": result.metrics, "adapter_files_sha256": {path.name: file_sha256(path) for path in args.output.glob("adapter*") if path.is_file()}})
    (args.output / "training_provenance.json").write_text(json.dumps(provenance, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(provenance, indent=2))


if __name__ == "__main__":
    main()
