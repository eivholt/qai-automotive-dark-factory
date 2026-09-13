#!/usr/bin/env python3
"""Evaluate camera-only process batches; labels stay in this offline client.

Manifest JSONL: sample_id, body_id, stage, images[{camera_id,path}],
expected_decision (optional), split/episode_id (optional). Generated evidence is private.
"""
from __future__ import annotations
import argparse
import base64
from collections import Counter, defaultdict
import json
from pathlib import Path
import statistics
import sys
import time
import urllib.error
import urllib.request

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "src"))
from dark_factory.gates import load_gate_contract, hold_reply, parse_gate_request, IDENTITY_FIELDS, MESSAGE_LAYOUT


def percentile(values, p):
    if not values:
        return None
    values = sorted(values)
    q = (len(values) - 1) * p
    low = int(q)
    return values[low] + (values[min(low + 1, len(values) - 1)] - values[low]) * (q - low)


def valid_model_reply(row):
    return row.get("identity_valid") is True and row["reply"].get("valid") is True


def gate_action(stage, decision):
    if decision == "G":
        return "proceed"
    if stage == "doors" and decision == "R":
        return "return"
    return "hold"


def summarize(rows):
    summary = {"requests": len(rows), "valid_replies": sum(valid_model_reply(r) for r in rows)}
    for key in ("end_to_end_ms", "preprocessing_ms", "inference_ms"):
        values = [r[key] for r in rows if isinstance(r.get(key), (int, float))]
        summary[key] = {"mean": statistics.mean(values), "median": statistics.median(values), "p95": percentile(values, .95)} if values else None
    labelled = [r for r in rows if r.get("repeat", 0) == 0 and r.get("expected_decision") in ("G", "A", "R")]
    if labelled:
        summary["labelled_batches"] = len(labelled)
        summary["correct"] = sum(valid_model_reply(r) and r["expected_decision"] == r["decision"] for r in labelled)
        summary["invalid_reply_holds"] = sum(not valid_model_reply(r) for r in labelled)
        summary["accuracy"] = summary["correct"] / len(labelled)
        action_rows = [r for r in labelled if r.get("stage") in ("doors", "adhesive", "weld")]
        if action_rows:
            correct_actions = sum(valid_model_reply(r) and gate_action(r["stage"], r["decision"]) == gate_action(r["stage"], r["expected_decision"]) for r in action_rows)
            summary["action_correct"] = correct_actions
            summary["action_accuracy"] = correct_actions / len(action_rows)
            summary["action_scope"] = "Thermal A and R both hold; door R returns. Literal letter accuracy is reported separately."

        summary["false_proceed"] = sum(r["decision"] == "G" and r["expected_decision"] != "G" for r in labelled)
        summary["holds"] = sum(r["decision"] == "A" for r in labelled)
        summary["confusion_expected_predicted"] = dict(Counter(r["expected_decision"] + "->" + (r["decision"] if valid_model_reply(r) else "ERROR") for r in labelled))
        summary["per_class"] = {}
        for label in ("G", "A", "R"):
            support = sum(r["expected_decision"] == label for r in labelled)
            predicted = sum(valid_model_reply(r) and r["decision"] == label for r in labelled)
            true_positive = sum(valid_model_reply(r) and r["expected_decision"] == label and r["decision"] == label for r in labelled)
            summary["per_class"][label] = {"support": support, "predicted": predicted,
                "precision": true_positive / predicted if predicted else None,
                "recall": true_positive / support if support else None}
        non_green = sum(r["expected_decision"] != "G" for r in labelled)
        summary["false_proceed_rate"] = summary["false_proceed"] / non_green if non_green else None
        episodes = defaultdict(list)
        for row in labelled:
            episodes[row["episode_id"]].append(row)
        summary["episodes"] = {str(k): {"batches": len(v), "all_correct": all(valid_model_reply(r) and r["decision"] == r["expected_decision"] for r in v),
                                     "false_proceed": sum(r["decision"] == "G" and r["expected_decision"] != "G" for r in v)} for k, v in episodes.items()}
    return summary


def select_samples(samples, split, purpose):
    selected = [s for s in samples if s.get("split") == split] if split else samples
    if not selected:
        raise ValueError("No samples selected")
    if purpose == "heldout":
        if split != "test" or any(s.get("split") != "test" for s in selected):
            raise ValueError("Held-out reporting requires explicit --split test")
    elif any(s.get("split") == "test" for s in selected):
        raise ValueError("Test episodes require explicit --purpose heldout --split test after model selection")
    return selected


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--manifest", required=True)
    p.add_argument("--output", required=True)
    p.add_argument("--endpoint", default="http://127.0.0.1:18191/v1/inspect")
    p.add_argument("--contract")
    p.add_argument("--repeat", type=int, default=1)
    p.add_argument("--limit", type=int, default=0)
    p.add_argument("--split", default="", help="Select an explicit manifest split; never create per-frame splits")
    p.add_argument("--purpose", choices=["development", "heldout", "transport_probe"], default="development")
    args = p.parse_args()
    contract = load_gate_contract(args.contract)
    manifest = Path(args.manifest).resolve()
    samples = [json.loads(line) for line in manifest.read_text(encoding="utf-8").splitlines() if line.strip()]
    samples = select_samples(samples, args.split, args.purpose)
    if args.limit:
        samples = samples[:args.limit]
    rows = []
    for repeat in range(args.repeat):
        for i, sample in enumerate(samples):
            stage = sample["stage"]
            stage_contract = contract["stages"][stage]
            if sample.get("contract_version", contract["version"]) != contract["version"]:
                raise ValueError("Manifest contract mismatch")
            if sample.get("prompt_sha256", stage_contract["prompt_sha256"]) != stage_contract["prompt_sha256"]:
                raise ValueError("Manifest prompt mismatch; explicitly recapture/version changed prompt experiments")
            payload = {"contract_version": contract["version"], "request_id": f"eval-{repeat}-{i}",
                "body_id": str(sample["body_id"]), "stage": stage, "captured_at_ms": time.monotonic_ns() // 1_000_000,
                "images": [{"camera_id": im["camera_id"], "png_base64": base64.b64encode((manifest.parent / im["path"]).read_bytes()).decode()} for im in sample["images"]]}
            parse_gate_request(payload, contract)
            started = time.perf_counter()
            req = urllib.request.Request(args.endpoint, json.dumps(payload).encode(), {"Content-Type": "application/json"})
            try:
                with urllib.request.urlopen(req, timeout=30) as response:
                    reply = json.load(response)
            except (urllib.error.URLError, ValueError, TimeoutError) as exc:
                reply = hold_reply(payload, contract, type(exc).__name__)
            end_to_end_ms = (time.perf_counter() - started) * 1000
            identity_ok = all(reply.get(k) == payload[k] for k in IDENTITY_FIELDS)
            identity_ok = identity_ok and reply.get("message_layout") == MESSAGE_LAYOUT
            identity_ok = identity_ok and reply.get("decision_policy") == contract["decision_policy"]
            identity_ok = identity_ok and reply.get("prompt_sha256") == stage_contract["prompt_sha256"]
            identity_ok = identity_ok and reply.get("camera_ids") == stage_contract["camera_ids"] and reply.get("modality") == stage_contract["modality"]
            decision = reply.get("decision") if identity_ok and reply.get("valid") is True and reply.get("decision") in ("G", "A", "R") else "A"
            row = {"sample_id": sample["sample_id"], "episode_id": sample.get("episode_id", sample["body_id"]), "stage": stage,
                "repeat": repeat, "decision": decision, "expected_decision": sample.get("expected_decision", "") if args.purpose != "transport_probe" else "",
                "identity_valid": identity_ok, "end_to_end_ms": end_to_end_ms,
                "preprocessing_ms": reply.get("timing", {}).get("preprocessing_ms"),
                "inference_ms": reply.get("timing", {}).get("inference_ms"), "reply": reply}
            rows.append(row)
            print(json.dumps({k: row[k] for k in ("sample_id", "stage", "decision", "expected_decision", "end_to_end_ms")}), flush=True)
    report = {"purpose": args.purpose, "split": args.split, "decision_policy": contract["decision_policy"], "message_layout": MESSAGE_LAYOUT, "contract_version": contract["version"], "manifest": str(manifest),
        "backend_aliases": sorted({r["reply"].get("backend_alias", "unversioned") for r in rows}),
        "vision_projection_methods": sorted({r["reply"].get("vision_patch_embedding", "unversioned") for r in rows}),
        "scoring_methods": sorted({r["reply"].get("scoring_method", "unversioned") for r in rows}), "model_is_experimental": True, "accuracy_scope": "Captured development examples only; not EVK or production accuracy" if args.purpose != "heldout" else "Held-out episodes from this synthetic camera distribution only",
        "summary": summarize(rows), "per_stage": {stage: summarize([r for r in rows if r["stage"] == stage]) for stage in contract["stages"]}, "rows": rows}
    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report["summary"], indent=2))

if __name__ == "__main__":
    main()
