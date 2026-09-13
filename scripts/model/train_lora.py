#!/usr/bin/env python3
"""Bounded one-token automotive LoRA training from prepared episode-split manifests.

This experimental runner optimizes the next-label cross entropy only. It does
not run test-set evaluation or decide that an adapter is acceptable for a gate.
"""
from __future__ import annotations
import argparse
from contextlib import nullcontext
import hashlib
import json
import math
from pathlib import Path
import sys
import time
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "src"))
from dark_factory.gates import load_gate_contract, select_decision, camera_content, MESSAGE_LAYOUT, validate_adapter_training
from dark_factory.protocol import validate_png
from dark_factory.gates_training import BalancedBatches


def load_rows(path, contract):
    rows = [json.loads(line) for line in Path(path).read_text(encoding="utf-8").splitlines() if line.strip()]
    if not rows:
        raise ValueError("Training/validation manifest is empty")
    for row in rows:
        spec = contract["stages"][row["stage"]]
        if row["contract_version"] != contract["version"] or row["prompt_sha256"] != spec["prompt_sha256"] or row["prompt"] != spec["prompt"]:
            raise ValueError("Prepared training prompt/contract mismatch")
        if row["answer"] not in ("G", "A", "R") or len(row["images"]) != len(spec["camera_ids"]):
            raise ValueError("Invalid training label or camera count")
        row["images"] = [str((Path(path).resolve().parent / image).resolve()) for image in row["images"]]
    return rows


def initial_adapter_provenance(path, contract, base_config_sha256, rank, scope, targets):
    init_dir = Path(path).resolve()
    init_report = json.loads((init_dir / "training_report.json").read_text(encoding="utf-8"))
    validate_adapter_training(init_report, contract, base_config_sha256)
    if init_report.get("rank") != rank or init_report.get("lora_scope") != scope or init_report.get("target_modules") != targets:
        raise ValueError("Initial adapter rank/scope/targets differ from this recorded run")
    adapter_config = json.loads((init_dir / "adapter_config.json").read_text(encoding="utf-8"))
    actual_targets = adapter_config.get("target_modules")
    same_targets = actual_targets == targets if isinstance(targets, str) else isinstance(actual_targets, list) and set(actual_targets) == set(targets)
    if not same_targets or any(adapter_config.get(k) != v for k,v in {
            "r":rank,"lora_alpha":32,"lora_dropout":.05,"bias":"none","peft_type":"LORA",
            "use_dora":False,"use_rslora":False}.items()):
        raise ValueError("Initial PEFT configuration differs from the supported training architecture")
    if adapter_config.get("rank_pattern") or adapter_config.get("alpha_pattern") or adapter_config.get("modules_to_save"):
        raise ValueError("Initial adapter contains unsupported per-module overrides")
    return {"path": str(init_dir),
        "weights_sha256": hashlib.sha256((init_dir / "adapter_model.safetensors").read_bytes()).hexdigest(),
        "config_sha256": hashlib.sha256((init_dir / "adapter_config.json").read_bytes()).hexdigest(),
        "training_report_sha256": hashlib.sha256((init_dir / "training_report.json").read_bytes()).hexdigest(),
        "previous_steps": init_report["steps"], "restart": "Adapter weights only; fresh optimizer and deterministic sampler"}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--model", required=True, help="Local unadapted base checkpoint")
    p.add_argument("--init-adapter", help="Optional compatible private adapter weights; optimizer/sampler restart is recorded")
    p.add_argument("--train", required=True)
    p.add_argument("--validation", required=True)
    p.add_argument("--output", required=True, help="New private adapter directory")
    p.add_argument("--contract")
    p.add_argument("--max-steps", type=int, default=100)
    p.add_argument("--rank", type=int, default=8)
    p.add_argument("--lora-scope", choices=["language-attention", "multimodal"], default="multimodal")
    p.add_argument("--learning-rate", type=float, default=1e-4)
    p.add_argument("--seed", type=int, default=42)
    p.add_argument("--gpu-memory-fraction", type=float, default=.4)
    p.add_argument("--gradient-accumulation", type=int, default=4)
    p.add_argument("--batch-size", type=int, default=1, help="Actual same-stage GPU minibatch; >=3 matches thermal G/A/R episodes")
    p.add_argument("--no-gradient-checkpointing", action="store_true")
    p.add_argument("--linear-patch-embedding", action="store_true", help="Opt-in tested nonoverlapping patch Conv3d-to-linear execution; parameter layout unchanged")
    p.add_argument("--validation-limit", type=int, default=0, help="Optional small diagnostic subset; full merged-runtime validation is still required")
    args = p.parse_args()
    if not 1 <= args.max_steps <= 10000 or not 1 <= args.rank <= 64 or not 1 <= args.gradient_accumulation <= 64:
        p.error("Bounded training requires 1-10000 steps, rank1-64, accumulation1-64")
    if not 0 < args.gpu_memory_fraction <= .8:
        p.error("GPU memory fraction must be >0 and <=0.8")
    if not 0 < args.learning_rate <= .01:
        p.error("Learning rate must be positive and <=0.01")
    if not 1 <= args.batch_size <= 16:
        p.error("Batch size must be in 1..16")
    output = Path(args.output).resolve()
    if output.exists() and any(output.iterdir()):
        p.error("Choose a new adapter output directory; existing weights are never overwritten")
    contract = load_gate_contract(args.contract)
    training = load_rows(args.train, contract)
    validation = load_rows(args.validation, contract)
    train_episodes = {r["episode_id"] for r in training}
    if train_episodes & {r["episode_id"] for r in validation}:
        raise ValueError("Whole-episode train/validation leakage")
    import torch
    import torch.nn.functional as F
    from PIL import Image, ImageChops
    from peft import LoraConfig, get_peft_model, PeftModel
    from transformers import AutoProcessor, Qwen3VLForConditionalGeneration
    if not torch.cuda.is_available():
        raise RuntimeError("A CUDA GPU is required")
    torch.cuda.set_per_process_memory_fraction(args.gpu_memory_fraction)
    torch.set_num_threads(4)
    torch.set_num_interop_threads(1)
    torch.manual_seed(args.seed)
    processor = AutoProcessor.from_pretrained(args.model, local_files_only=True)
    processor.tokenizer.padding_side = "left"
    processor.image_processor.size = {"shortest_edge": 448 * 256, "longest_edge": 448 * 256}
    base = Qwen3VLForConditionalGeneration.from_pretrained(args.model, local_files_only=True,
        dtype=torch.bfloat16, attn_implementation="sdpa").to("cuda")
    targets = (r".*language_model\.layers\.\d+\.self_attn\.(q_proj|k_proj|v_proj|o_proj)"
        if args.lora_scope == "language-attention" else
        ["q_proj", "k_proj", "v_proj", "o_proj", "gate_proj", "up_proj", "down_proj", "qkv", "linear_fc1", "linear_fc2"])
    config = LoraConfig(r=args.rank, lora_alpha=32, lora_dropout=.05, bias="none", target_modules=targets)
    initial_adapter = None
    if args.init_adapter:
        init_dir = Path(args.init_adapter).resolve()
        initial_adapter = initial_adapter_provenance(init_dir, contract, hashlib.sha256((Path(args.model)/"config.json").read_bytes()).hexdigest(), args.rank, args.lora_scope, targets)
        model = PeftModel.from_pretrained(base, init_dir, local_files_only=True, is_trainable=True)
    else:
        model = get_peft_model(base, config)
    if not args.no_gradient_checkpointing:
        if args.lora_scope == "multimodal":
            model.gradient_checkpointing_enable(gradient_checkpointing_kwargs={"use_reentrant": False})
        else:
            model.get_base_model().model.language_model.gradient_checkpointing_enable()
        model.enable_input_require_grads()
    trainable = [p for p in model.parameters() if p.requires_grad]
    visual_trainable = sum(p.numel() for n,p in model.named_parameters() if p.requires_grad and ".visual." in n)
    if not trainable or (args.lora_scope == "language-attention" and visual_trainable) or (args.lora_scope == "multimodal" and not visual_trainable):
        raise RuntimeError("LoRA branch scope check failed")
    print(json.dumps({"event": "TRAINING_MODEL_READY", "scope": args.lora_scope,
        "trainable_parameters": sum(p.numel() for p in trainable), "visual_trainable_parameters": visual_trainable}), flush=True)
    optimizer = torch.optim.AdamW(trainable, lr=args.learning_rate, fused=True)
    image_hashes = {}
    label_ids = {label: processor.tokenizer.encode(label, add_special_tokens=False) for label in ("G", "A", "R")}
    if any(len(ids) != 1 for ids in label_ids.values()):
        raise RuntimeError("G/A/R must be single tokens")

    def inputs_for(rows):
        conversations = []
        for row in rows:
            images = []
            for path in row["images"]:
                png_data = Path(path).read_bytes()
                validate_png(png_data)
                digest = hashlib.sha256(png_data).hexdigest()
                if path in image_hashes and image_hashes[path] != digest:
                    raise ValueError("A camera image changed during training")
                image_hashes[path] = digest
                with Image.open(path) as source:
                    if source.size != (448, 256):
                        raise ValueError("Training images must preserve the sensor dimensions")
                    if "A" in source.getbands() and source.getchannel("A").getextrema() != (255, 255):
                        raise ValueError("Sensor image alpha must be opaque")
                    image = source.convert("RGB")
                    r,g,b = image.split()
                    if ImageChops.difference(r,g).getbbox() or ImageChops.difference(r,b).getbbox():
                        raise ValueError("Training images must be camera grayscale")
                    images.append(image)
            conversations.append([{"role": "user", "content": camera_content(
                [{"type": "image", "image": image} for image in images], row["prompt"])}])
        inputs = processor.apply_chat_template(conversations, tokenize=True, add_generation_prompt=True,
            padding=True, return_dict=True, return_tensors="pt").to("cuda")
        if any(grid != [1,16,28] for grid in inputs["image_grid_thw"].cpu().tolist()):
            raise ValueError("Training processor resized a camera image")
        if not inputs["attention_mask"][:, -1].all():
            raise ValueError("Next-token training requires an unpadded final prompt token")
        target = torch.tensor([label_ids[row["answer"]][0] for row in rows], device="cuda")
        return inputs, target

    output.mkdir(parents=True, exist_ok=True)
    started = time.perf_counter()
    history = []
    patch_context = nullcontext(0)
    if args.linear_patch_embedding:
        from dark_factory.gates_patch_embed import linear_qwen3_vl_patch_embed
        patch_context = linear_qwen3_vl_patch_embed(model, unsupported="raise")
    with patch_context as patch_count:
        if args.linear_patch_embedding and patch_count != 1:
            raise RuntimeError("Expected exactly one supported vision patch projection")
        model.train()
        optimizer.zero_grad(set_to_none=True)
        sampler = BalancedBatches(training, args.batch_size, args.seed)
        sample_history = []
        micro_step = 0
        for step in range(args.max_steps):
            total_loss = 0.0
            for _ in range(args.gradient_accumulation):
                rows = sampler.next()
                sample_history.extend({"sample_id": row["sample_id"], "episode_id": row["episode_id"],
                    "stage": row["stage"], "label": row["answer"], "micro_batch": micro_step} for row in rows)
                micro_step += 1
                inputs, target = inputs_for(rows)
                with torch.autocast(device_type="cuda", dtype=torch.bfloat16):
                    logits = model(**inputs, use_cache=False, logits_to_keep=1).logits[:, -1].float()
                    loss = F.cross_entropy(logits, target)
                (loss / args.gradient_accumulation).backward()
                total_loss += loss.item() / args.gradient_accumulation
            gradient_norm = float(torch.nn.utils.clip_grad_norm_(trainable, 1.0))
            if not math.isfinite(gradient_norm) or (step == 0 and gradient_norm <= 0):
                raise RuntimeError("Adapter gradient check failed")
            optimizer.step()
            optimizer.zero_grad(set_to_none=True)
            record = {"step": step + 1, "loss": total_loss, "gradient_norm": gradient_norm, "cuda_allocated_bytes": torch.cuda.memory_allocated(), "cuda_peak_allocated_bytes": torch.cuda.max_memory_allocated(), "elapsed_seconds": time.perf_counter() - started}
            history.append(record)
            print(json.dumps(record), flush=True)
        model.eval()
        val = []
        validation_subset = validation[:args.validation_limit] if args.validation_limit > 0 else validation
        with torch.inference_mode():
            for row in validation_subset:
                inputs, target = inputs_for([row])
                with torch.autocast(device_type="cuda", dtype=torch.bfloat16):
                    logits = model(**inputs, use_cache=False, logits_to_keep=1).logits[:, -1].float()
                predicted = processor.tokenizer.decode([int(logits.argmax().item())])
                label_logits = logits[0, [label_ids[k][0] for k in ("G", "A", "R")]]
                mass = float((torch.logsumexp(label_logits, 0) - torch.logsumexp(logits[0], 0)).exp())
                scores = dict(zip(("G", "A", "R"), label_logits.softmax(0).cpu().tolist()))
                decision, reason = select_decision(scores, min(mass, 1.0), contract["decision_policy"])
                val.append({"sample_id": row["sample_id"], "episode_id": row["episode_id"], "expected": row["answer"], "stage": row["stage"], "decision": decision, "reason": reason,
                    "label_mass": mass, "conditional_label_scores": scores, "raw_token": predicted, "cross_entropy": F.cross_entropy(logits, target).item()})
    model.save_pretrained(output, safe_serialization=True)
    report = {"contract_version": contract["version"], "message_layout": MESSAGE_LAYOUT,
        "decision_policy": contract["decision_policy"], "image_contract": contract["image"], "thermal_window_c": contract["thermal_window_c"], "prompt_hashes": {k:v["prompt_sha256"] for k,v in contract["stages"].items()},
        "base_model_config_sha256": hashlib.sha256((Path(args.model)/"config.json").read_bytes()).hexdigest(),
        "training_manifest_sha256": hashlib.sha256(Path(args.train).read_bytes()).hexdigest(),
        "validation_manifest_sha256": hashlib.sha256(Path(args.validation).read_bytes()).hexdigest(),
        "gpu_memory_fraction": args.gpu_memory_fraction, "peak_cuda_allocated_bytes": torch.cuda.max_memory_allocated(),
        "vision_patch_embedding": "linear-nonoverlap-v1" if args.linear_patch_embedding else "stock-conv3d", "patched_projection_count": patch_count,
        "autocast": "bfloat16", "batch_size": args.batch_size, "gradient_checkpointing": not args.no_gradient_checkpointing, "learning_rate": args.learning_rate,
        "initial_adapter": initial_adapter,
        "seed": args.seed, "rank": args.rank, "steps": args.max_steps, "gradient_accumulation": args.gradient_accumulation,
        "image_sha256": image_hashes, "train_episodes": sorted(train_episodes), "trainable_parameters": sum(p.numel() for p in trainable), "visual_trainable_parameters": visual_trainable,
        "lora_scope": args.lora_scope, "target_modules": targets, "lora_alpha": 32, "sampling": "stage-balanced batches; matched thermal G/A/R episode triplets when batch>=3, label-balanced remaining rows; seeded shuffled queues", "sample_history": sample_history,
        "loss_history": history, "validation_raw_tokens": val, "validation_total_batches": len(validation), "validation_evaluated_batches": len(val),
        "validation_gate_summary": {stage: {"batches": sum(r["stage"] == stage for r in val), "correct": sum(r["stage"] == stage and r["decision"] == r["expected"] for r in val), "false_proceed": sum(r["stage"] == stage and r["decision"] == "G" and r["expected"] != "G" for r in val), "holds": sum(r["stage"] == stage and r["decision"] == "A" for r in val)} for stage in contract["stages"]},
        "scope": "Experimental adapter; raw validation fit does not establish held-out gate accuracy or deployment acceptance."}
    (output/"training_report.json").write_text(json.dumps(report, indent=2)+"\n", encoding="utf-8")
    print(json.dumps({"adapter": str(output), "steps": args.max_steps, "validation_batches": len(val)}), flush=True)

if __name__ == "__main__":
    main()
