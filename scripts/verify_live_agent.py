"""Audit a recorded real Unreal/LLM/Reason2 run; never drives the simulation."""
from __future__ import annotations
import argparse
from collections import Counter
import json
from pathlib import Path
import statistics


def verify(report):
    errors = []
    def check(good, message):
        if not good:
            errors.append(message)
    a = report.get("agent", {})
    check(report.get("control_mode") == "agent", "not agent mode")
    check(a.get("schema") == "darkfactory-live-agent-v1", "wrong agent schema")
    check(a.get("recipe_vetoes_enabled") is False, "recipe vetoes enabled")
    check(report.get("passed") is True, "native run failed")
    check(report.get("completed_bodies") == 3, "incomplete three-body run")
    check(a.get("audit_failures") == 0 and a.get("rejected_calls") == 0, "audit failures or rejected calls")
    check(a.get("held") is False, "agent held")
    check(report.get("ground_truth_fields_sent_to_model") == 0, "source labels sent to models")
    trace = a.get("trace", [])
    choices = [e for e in trace if e["event"] == "agent_choice"]
    results = [e for e in trace if e["event"] == "tool_result"]
    inspections = [e for e in results if e["detail"]["tool"] == "inspect"]
    check(len(choices) == a.get("requests") and len(choices) > 0, "missing agent decisions")
    # Current runtime records physical dispatch readiness. Older archived runs
    # predate this field and retain their original verification contract.
    if any("inspection_arm_parked" in e["detail"] for e in choices):
        check(all(e["detail"].get("inspection_arm_parked") is True for e in choices), "agent job dispatched before camera parked")
    check(len(inspections) == report.get("model_http_requests") and len(inspections) > 0, "missing vision results")
    bodies = {e["body_sequence"]: e["body_id"] for e in trace}
    check(set(bodies) == {1, 2, 3} and len(set(bodies.values())) == 3, "body scope mismatch")
    check([o.get("body_sequence") for o in a.get("outcomes", [])] == [1, 2, 3], "wrong departures")
    allowed_observation = {"body_id", "event", "clock_seconds", "tool_results", "holder_engaged"}
    allowed_result = {"status", "tool", "argument", "completed_clock_seconds", "holder_engaged", "stage", "decision",
                      "valid", "reason", "latency_ms", "capture_age_at_reply_seconds", "images", "body_id", "body_sequence", "agent_turns", "error"}
    for event in trace:
        if event["event"] == "agent_observation":
            obs = event["detail"]
            check(set(obs) <= allowed_observation, "unexpected controller fields in agent observation")
            for row in obs["tool_results"]:
                check(set(row) <= allowed_result, "unexpected source fields in tool result")
    for e in inspections:
        d = e["detail"]
        check(d.get("valid") is True, "invalid vision reply")
        check(d.get("images") == 3 and 0 <= d.get("capture_age_at_reply_seconds", -1) < 8, "stale/incomplete vision batch")
    for audit in a.get("audits", []):
        check(not audit["failures"], "independent recipe audit failed")
        check(audit["source_preassembled"] == (audit["body_sequence"] == 2), "wrong fixture source")
        check(audit["body_id"] == bodies.get(audit["body_sequence"]), "audit body mismatch")
        if audit["action"] == "weld_panel":
            check(audit["sealant_apparent_c"] < 45, "sealant hot at actual weld start")
        if audit["action"] in ("release_holder", "send_forward"):
            check(audit["sealant_apparent_c"] < 45 and audit["weld_apparent_c"] < 55, "hot release/departure")
    body_choices = {}
    for body in (1, 2, 3):
        actions = [e["detail"]["argument"] for e in choices if e["body_sequence"] == body and e["detail"]["tool"] == "act"]
        # Outcome validation only: these expectations never reach the LLM.
        expected = ["send_forward"] if body == 2 else ["apply_sealant", "place_panel", "weld_panel", "release_holder", "send_forward"]
        check(actions == expected, f"body {body} incorrect jobs: {actions}")
        body_choices[body] = [e["detail"]["tool"] + ":" + e["detail"]["argument"] for e in choices if e["body_sequence"] == body]
    def latency(values):
        return {"median_ms": statistics.median(values), "maximum_ms": max(values)} if values else {}
    return {"passed": not errors, "errors": errors, "source": report.get("source"),
            "scope": "Three-body development integration; not held-out accuracy",
            "wall_seconds": a.get("wall_seconds"), "agent_decisions": len(choices), "vision_batches": len(inspections),
            "agent_latency": latency([e["detail"]["latency_ms"] for e in choices]),
            "vision_latency": latency([e["detail"]["latency_ms"] for e in inspections]),
            "vision_codes": dict(Counter(e["detail"]["decision"] for e in inspections)), "body_choices": body_choices}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("report", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    result = verify(json.loads(args.report.read_text(encoding="utf-8-sig")))
    text = json.dumps(result, indent=2)
    print(text)
    if args.output:
        args.output.write_text(text + "\n")
    raise SystemExit(0 if result["passed"] else 1)
