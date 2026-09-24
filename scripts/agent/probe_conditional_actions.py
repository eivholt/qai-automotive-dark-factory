"""Offline what-if action probes. Hypotheses never become live observations."""
import argparse
import copy
import json
from pathlib import Path
import time

from single_token import Controller, ACTIONS, score_response


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--logs", type=Path, required=True)
    p.add_argument("--endpoint", required=True)
    p.add_argument("--output", type=Path, required=True)
    args = p.parse_args()
    c = Controller(args.endpoint, json.loads(Path("configs/live_agent_legacy.json").read_text()))
    rows = []
    seen = set()
    for file in sorted(args.logs.glob("request-*.json")):
        messages = json.loads(file.read_text())["diagnostic"]["model_messages"]
        if len(messages) < 4 or messages[-2]["role"] != "assistant":
            continue
        code = messages[-2]["content"]
        if code not in ACTIONS or ACTIONS[code][0] != "inspect":
            continue
        stage = ACTIONS[code][2]
        prior_codes = [m["content"] for m in messages[2:-2] if m["role"] == "assistant"]
        new_panel = "C" in prior_codes
        key = (stage, new_panel)
        if key in seen:
            continue
        seen.add(key)
        for decision, valid in (("G", True), ("R", True), ("A", True), ("G", False)):
            scenario = copy.deepcopy(messages)
            scenario[-1]["content"] = (
                f"OFFLINE CONDITIONAL PLAN, not an actual observation. The pending tool is inspect({stage}). "
                f"IF its future result has stage={stage}, decision={decision}, valid={str(valid).lower()}, "
                "which action would you choose next? No tool has returned this hypothetical result. "
                "Select only the action for that condition using the existing policy. "
                "This is planning only: nothing will execute. Return one action code.")
            if not valid or decision == "A":
                allowed = {code, "J", "K", "L", "M"}
            elif decision == "R":
                allowed = {"B"} if stage == "panel_presence" else {"M"} if stage == "panel_seated" else {"J", "K", "L", "M"}
            else:
                allowed = {"D", "G"} if stage == "panel_presence" else {"E" if new_panel else "G"} if stage == "adhesive" else {"G"} if stage == "panel_seated" else {"H" if new_panel else "I"}
            prompt = c.post("/apply-template", {"messages": scenario, "add_generation_prompt": True})["prompt"]
            start = time.monotonic()
            raw = c.post("/completion", {"prompt": prompt, "n_predict": 1, "temperature": 0,
                "samplers": ["temperature"], "n_probs": 128, "post_sampling_probs": False,
                "return_tokens": True, "cache_prompt": True, "repeat_penalty": 1., "seed": 42})
            row = {"stage": stage, "new_panel": new_panel, "hypothetical_code": decision, "valid": valid,
                   "allowed_evaluator_only": sorted(allowed), "http_ms": (time.monotonic()-start)*1000,
                   "model_messages": scenario, "response": raw}
            try:
                row["selected"] = score_response(raw, c.token_ids)["code"]
                row["passes"] = row["selected"] in allowed
            except ValueError as exc:
                row.update(error=str(exc), passes=False)
            rows.append(row)
            report = {"scope": "Offline conditional feasibility only, no dispatch or real inspections",
                      "passed": sum(r["passes"] for r in rows), "total": len(rows), "rows": rows}
            args.output.write_text(json.dumps(report, indent=2)+"\n")
            print(json.dumps({k: v for k, v in row.items() if k not in ("model_messages", "response")}), flush=True)
    return int(not all(r["passes"] for r in rows))


if __name__ == "__main__":
    raise SystemExit(main())
