#!/usr/bin/env python3
"""Evaluate a prespecified stricter abstention grid using validation scores only."""
import argparse
import hashlib
import json
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "src"))
from dark_factory.gates import load_gate_contract, select_decision, MESSAGE_LAYOUT
sys.path.insert(0, str(Path(__file__).resolve().parent))
from dark_factory.gates_scores import decision_from_bounds, SCORING_METHOD
from evaluate_gates import summarize, valid_model_reply

GRID = (.85, .90, .95, .98)


def calibrate(report, contract):
    if report.get("purpose") != "development" or report.get("split") != "validation":
        raise ValueError("Calibration requires an explicitly selected validation report; never test results")
    if report.get("contract_version") != contract["version"] or report.get("message_layout") != MESSAGE_LAYOUT:
        raise ValueError("Calibration requires the current camera/prompt contract")
    if report.get("decision_policy") != contract["decision_policy"]:
        raise ValueError("Calibration input policy does not match the recorded experiment")
    if not report.get("rows") or any(not valid_model_reply(row) for row in report["rows"]):
        raise ValueError("All calibration rows must have valid model and transport identity")
    outcomes = []
    for threshold in GRID:
        if threshold < contract["decision_policy"]["minimum_conditional_probability"]:
            continue
        policy = {**contract["decision_policy"], "minimum_conditional_probability": threshold}
        rows = []
        for row in report["rows"]:
            reply = row["reply"]
            if reply.get("scoring_method") == SCORING_METHOD:
                decision, reason = decision_from_bounds(reply, policy)
            elif reply.get("reason") == "incomplete_label_scores":
                decision, reason = "A", "incomplete_label_scores"
            else:
                decision, reason = select_decision(reply["conditional_label_scores"], reply["label_mass"], policy)
            rows.append({**row, "decision": decision, "calibration_reason": reason})
        stages = {stage: summarize([r for r in rows if r["stage"] == stage]) for stage in contract["stages"]}
        overall = summarize(rows)
        nir_stages = ("panel_presence", "panel_seated")
        nir_ok = all(stages[name].get("labelled_batches", 0) >= 12
            and stages[name].get("correct") == stages[name].get("labelled_batches")
            and all(stages[name].get("per_class", {}).get(label, {}).get("support", 0) >= 3 for label in ("G", "R"))
            for name in nir_stages)
        cool_recalls = {s: stages[s]["per_class"]["G"]["recall"] for s in ("adhesive", "weld")}
        high_cool_recall = all(value is not None and value >= .9 for value in cool_recalls.values())
        qualified = nir_ok and overall["false_proceed"] == 0 and high_cool_recall
        outcomes.append({"threshold": threshold, "decision_policy": policy, "qualified": qualified,
            "summary": overall, "per_stage": stages, "cool_recalls": cool_recalls, "nir_qualified": nir_ok,
            "decisions": [{"sample_id":r["sample_id"], "stage":r["stage"], "expected":r["expected_decision"], "decision":r["decision"]} for r in rows]})
    candidates = [o for o in outcomes if o["qualified"]]
    selected = max(candidates, key=lambda o: (sum(o["cool_recalls"].values()), o["threshold"])) if candidates else None
    return {"scope": "Offline validation-only policy calibration; no new model inference or test selection.",
        "prespecified_grid": list(GRID), "criteria": "Every NIR validation batch correct, at least 12 batches and 3 G/3 R per NIR stage; zero false G; >=90% clear-cool G recall per thermal stage",
        "tie_break": "Maximize combined clear-cool recall, then stricter threshold", "recommended_threshold": selected["threshold"] if selected else None,
        "outcomes": outcomes}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--validation-report", required=True)
    p.add_argument("--output", required=True)
    p.add_argument("--contract")
    args = p.parse_args()
    source = Path(args.validation_report)
    result = calibrate(json.loads(source.read_text(encoding="utf-8")), load_gate_contract(args.contract))
    result["validation_report_sha256"] = hashlib.sha256(source.read_bytes()).hexdigest()
    Path(args.output).write_text(json.dumps(result, indent=2)+"\n",encoding="utf-8")
    print(json.dumps({"recommended_threshold":result["recommended_threshold"], "outcomes":[{"threshold":o["threshold"],"qualified":o["qualified"],"correct":o["summary"]["correct"],"false_proceed":o["summary"]["false_proceed"],"cool_recalls":o["cool_recalls"]} for o in result["outcomes"]]},indent=2))

if __name__ == "__main__":
    main()
