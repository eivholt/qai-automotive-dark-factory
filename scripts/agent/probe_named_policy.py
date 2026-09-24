"""Offline policy probes only. Expected choices never enter model messages.

Synthetic tool observations test sequencing, not Reason2 image accuracy. Named
history intentionally makes ID/timestamp variants identical; do not count them
as independent evidence. No physical jobs or model-service settings are changed.
"""
import argparse
import json
from pathlib import Path
import time

from single_token import ACTIONS, Controller, tool_response


def cases(reserved=False):
    def history(events):
        rows = [{"role": "user", "content": json.dumps({"body_id": "offline-body",
            "event": "new_body_arrived_choose_tools_to_complete_the_task",
            "arrival": {"status": "completed", "completed_clock_seconds": 3.2, "holder_engaged": False}})}]
        held = False
        for i, event in enumerate(events):
            code, *spec = event
            tool, key, argument = ACTIONS[code]
            action = tool_response({"code": code}, {}, "offline")["choices"][0]["message"]
            # Stable synthetic pairing, not a label or expected decision.
            action["tool_calls"][0]["id"] = f"call-{i}"
            status = "completed"
            if tool == "inspect":
                decision, valid = spec[:2]
                status = "observed" if valid else "invalid"
                result = dict(stage=argument, decision=decision, valid=valid)
                if len(spec) > 2:
                    result["reason"] = spec[2]
                result["capture_age_at_reply_seconds"] = 9.1 if result.get("reason") == "stale_reply" else 0.3
            else:
                result = {}
                if spec:
                    status = spec[0]
                if code in ("C", "H") and status == "completed":
                    held = code == "C"
            result.update(status=status, completed_clock_seconds=10+i*9, holder_engaged=held)
            rows += [action, {"role": "tool", "tool_call_id": f"call-{i}", "content": json.dumps(result)}]
        return rows

    missing = [("A", "R", True)]
    placed = missing + [("B",), ("C",)]
    ready = placed + [("D", "G", True)]
    welded = ready + [("E",)]
    pre = [("A", "G", True)]
    specs = []
    def add(name, events, allowed):
        specs.append((name, history(events), tuple(allowed)))
    if reserved:
        # Evaluate only after choosing/fixing the development candidate.
        for prefix_name, prefix, code, next_code in (("placed", placed, "D", "E"),
                ("welded", welded+[("F", "G", True)], "G", "H"),
                ("pre-seal", pre+[("G", "G", True)], "D", "I"),
                ("pre-weld", pre+[("D", "G", True)], "G", "I")):
            for a, b in (("R", "A"), ("A", "R"), ("A", "A"), ("R", "R")):
                events = prefix + [(code, a, True), ("K",), (code, b, True), ("J",)]
                add(prefix_name+"-"+a+b+"-waiting", events, (code, "J", "K", "L", "M"))
                add(prefix_name+"-"+a+b+"-recovered", events+[(code, "G", True)], (next_code,))
        for code in ("D", "G"):
            other = "G" if code == "D" else "D"
            add("pre-latest-invalid-"+code, pre+[(code, "G", True), (other, "G", True), (code, "G", False, "stale_reply")], (code, "J", "K", "L", "M"))
        return specs
    normal = [([], "A"), (missing, "B"), (missing+[("B",)], "C"), (placed, "D"),
              (ready, "E"), (welded, "FG"), (welded+[("F", "G", True)], "G"),
              (welded+[("G", "G", True)], "F"),
              (welded+[("F", "G", True), ("G", "G", True)], "H"),
              (welded+[("F", "G", True), ("G", "G", True), ("H",)], "I"),
              (pre, "DG"), (pre+[("D", "G", True)], "G"), (pre+[("G", "G", True)], "D"),
              (pre+[("D", "G", True), ("G", "G", True)], "I"),
              (pre+[("G", "G", True), ("D", "G", True)], "I")]
    for i, (events, allowed) in enumerate(normal):
        add(f"normal-{i}", events, allowed)
    for name, prefix, code in (("presence", [], "A"), ("sealant", placed, "D"),
            ("seating", welded, "F"), ("weld", welded+[("F", "G", True)], "G"),
            ("pre-seal", pre+[("G", "G", True)], "D"), ("pre-weld", pre+[("D", "G", True)], "G")):
        for label, valid, reason in (("A", True, "ambiguous_label_scores"), ("G", False, "stale_reply"), ("G", False, "invalid_reply")):
            add(name+"-"+reason, prefix+[(code, label, valid, reason)], (code, "J", "K", "L", "M"))
        if code != "A":
            add(name+"-red", prefix+[(code, "R", True)], ("M",) if code == "F" else (code, "J", "K", "L", "M"))
    add("premature-sealant-green", missing+[("B",), ("D", "G", True)], ("C",))
    add("premature-thermal-retries", missing+[("B",), ("D", "R", True), ("J",), ("D", "A", True), ("J",), ("D", "G", True)], ("C",))
    add("must-reinspect-after-placement", missing+[("B",), ("D", "G", True), ("C",)], ("D",))
    add("placed-without-sealant", missing+[("C",), ("D", "G", True)], ("M",))
    add("welded-without-placement", missing+[("B",), ("E",)], ("M",))
    add("latest-hot-overrides-green", placed+[("D", "G", True), ("D", "R", True)], ("D", "J", "K", "L", "M"))
    add("failed-sealant", missing+[("B", "failed")], ("B", "M"))
    add("cancelled-placement", missing+[("B",), ("C", "cancelled_by_backend_switch")], ("C", "M"))
    return specs


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--endpoint", default="http://127.0.0.1:18206")
    p.add_argument("--config", type=Path, default=Path("configs/live_agent_legacy.json"))
    p.add_argument("--prompt", type=Path)
    p.add_argument("--policy-examples", type=Path)
    p.add_argument("--observation-format", choices=("named", "json"), default="named")
    p.add_argument("--suite", choices=("development", "reserved"), default="development")
    p.add_argument("--reset", action="store_true")
    p.add_argument("--output", type=Path, required=True)
    args = p.parse_args()
    if args.output.exists():
        p.error("Use a fresh output file; preserve previous failures")
    c = Controller(args.endpoint, json.loads(args.config.read_text()), args.prompt.read_text() if args.prompt else None,
        args.observation_format, benchmark_reset_cache=args.reset,
        policy_examples=args.policy_examples.read_text() if args.policy_examples else None)
    report = dict(scope="Synthetic offline policy probes; no physical actions or image accuracy", suite=args.suite,
        prompt=c.prompt, prompt_sha256=c.prompt_hash, observation_format=args.observation_format,
        reset=args.reset, token_ids=c.token_ids, rows=[])
    args.output.parent.mkdir(parents=True, exist_ok=True)
    for name, history, allowed in cases(args.suite == "reserved"):
        row = dict(case=name, expected_choices=list(allowed)); t=time.perf_counter()
        try:
            response, diagnostic = c.decide(history)
            selected = response["single_token"]["code"]
            row.update(response=response, diagnostic=diagnostic, passed=selected in allowed, selected=selected)
        except Exception as e:
            row.update(passed=False, error=str(e))
            if hasattr(e, "diagnostic"): row["diagnostic"]=e.diagnostic
        row["ms"]=(time.perf_counter()-t)*1000
        report["rows"].append(row)
        report["passed_count"]=sum(x["passed"] for x in report["rows"])
        args.output.write_text(json.dumps(report,indent=2)+"\n")
        print(name,row.get("selected"),row["passed"],round(row["ms"]),flush=True)
    return int(report["passed_count"]!=len(report["rows"]))


if __name__ == "__main__":
    raise SystemExit(main())
