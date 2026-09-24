"""Development probes for an initially present panel's TWO independent cooling gates.

Vary opaque IDs/clocks and insert an ambiguous sealant retry in saved model-visible
history. Expected calls are evaluator-only. No physical jobs or Reason2 requests;
these synthetic probes are not independent model-accuracy validation.

The default legacy suite retains its original 16 cases. The opt-in thermal-order
suite checks both missing stages and both completed orders, including the
presence -> weld -> premature departure counterexample seen in live testing.
"""
from __future__ import annotations

import argparse
import copy
import hashlib
import json
from pathlib import Path
import time
from urllib.request import Request, urlopen

from replay_latency import choice, compact_messages


def cases(runtime, variants):
    messages = compact_messages(runtime["agent"]["current_body_messages"])
    # This fixture must contain an initially present body's actual tool history.
    presence = next(json.loads(m["content"]) for m in messages if m["role"] == "tool")
    if presence.get("stage") != "panel_presence" or presence.get("decision") != "G" or not presence.get("valid"):
        raise ValueError("Requires an initially present panel fixture")
    index = next(i for i, m in enumerate(messages) if m["role"] == "tool"
                 and json.loads(m["content"]).get("stage") == "adhesive"
                 and json.loads(m["content"]).get("decision") == "G")
    for variant in range(variants):
        for retry in (False, True):
            history = copy.deepcopy(messages[:index+1])
            arrival = json.loads(history[0]["content"])
            arrival["body_id"] = hashlib.sha256(f"departure-probe-{variant}".encode()).hexdigest()[:32].upper()
            arrival["arrival"]["completed_clock_seconds"] = round(3.2 + variant * 71.113, 3)
            history[0]["content"] = json.dumps(arrival, separators=(",", ":"))
            for message in history:
                if message["role"] == "tool":
                    result = json.loads(message["content"])
                    result["completed_clock_seconds"] = round(result["completed_clock_seconds"] + variant * 71.113, 3)
                    message["content"] = json.dumps(result, separators=(",", ":"))
            if retry:
                ambiguous_pair = copy.deepcopy(history[-2:])
                call_id = f"retry_{variant}"
                ambiguous_pair[0]["tool_calls"][0]["id"] = call_id
                ambiguous_pair[1]["tool_call_id"] = call_id
                result = json.loads(ambiguous_pair[1]["content"])
                result.update(decision="A", reason="ambiguous_label_scores")
                result["completed_clock_seconds"] = round(result["completed_clock_seconds"] - 0.25, 3)
                ambiguous_pair[1]["content"] = json.dumps(result, separators=(",", ":"))
                history[-2:-2] = ambiguous_pair
            yield f"id{variant}-retry{int(retry)}", history


def thermal_order_cases(runtime):
    """Separate synthetic suite; allowed choices stay outside model messages.

    Only an initially present body's model-visible arrival/presence prefix is
    retained. New paired inspection messages are synthetic observations, not
    claimed live captures. Invalid/stale G cannot supply the missing evidence.
    """
    messages = compact_messages(runtime["agent"]["current_body_messages"])
    index = next(i for i, message in enumerate(messages) if message["role"] == "tool")
    presence = json.loads(messages[index]["content"])
    if (presence.get("stage") != "panel_presence" or presence.get("decision") != "G"
            or presence.get("valid") is not True):
        raise ValueError("Requires an initially present panel fixture")
    if index == 0 or choice(messages[index-1]) != ["inspect", "panel_presence"]:
        raise ValueError("Requires the paired panel-presence inspection call")
    prefix = messages[:index+1]
    used_ids = {call["id"] for message in prefix for call in message.get("tool_calls", [])}

    def history(observations):
        result = copy.deepcopy(prefix)
        for number, (stage, decision, valid, reason) in enumerate(observations, 1):
            call_id = f"thermal_order_probe_{number}"
            while call_id in used_ids:
                call_id += "_"
            observation = copy.deepcopy(presence)
            observation.update(stage=stage, decision=decision, valid=valid,
                               status="observed" if valid else "invalid",
                               completed_clock_seconds=round(presence["completed_clock_seconds"] + number*8, 3),
                               capture_age_at_reply_seconds=9.0 if reason == "stale_reply" else 0.25)
            observation.pop("reason", None)
            if reason:
                observation["reason"] = reason
            result.extend([
                {"role": "assistant", "tool_calls": [{"id": call_id, "type": "function",
                 "function": {"name": "inspect", "arguments": json.dumps({"stage": stage, "reason": "inspect"})}}]},
                {"role": "tool", "tool_call_id": call_id,
                 "content": json.dumps(observation, separators=(",", ":"))},
            ])
        return result

    def cool(stage):
        return stage, "G", True, None

    depart = (("act", "send_forward"),)
    yield "presence-only", history([]), (("inspect", "adhesive"), ("inspect", "weld"))
    yield "missing-weld", history([cool("adhesive")]), (("inspect", "weld"),)
    yield "missing-sealant", history([cool("weld")]), (("inspect", "adhesive"),)
    for first, second in (("adhesive", "weld"), ("weld", "adhesive")):
        yield f"both-{first}-then-{second}", history([cool(first), cool(second)]), depart
    for stage, other in (("adhesive", "weld"), ("weld", "adhesive")):
        allowed = (("inspect", stage), ("wait", 1), ("wait", 5), ("wait", 15), ("hold", None))
        faults = (("ambiguous", "A", True, "ambiguous_label_scores"),
                  ("invalid-green", "G", False, "invalid_reply"),
                  ("stale-green", "G", False, "stale_reply"),
                  ("hot", "R", True, None))
        for name, decision, valid, reason in faults:
            yield f"{stage}-{name}", history([cool(other), (stage, decision, valid, reason)]), allowed
        # A later valid result from the other modality must not conceal stale G.
        yield f"{stage}-stale-before-{other}", history([
            (stage, "G", False, "stale_reply"), cool(other)]), allowed
        yield f"{stage}-recovered", history([
            cool(other), (stage, "A", True, "ambiguous_label_scores"), cool(stage)]), depart


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime", type=Path, required=True)
    parser.add_argument("--config", type=Path, required=True)
    parser.add_argument("--endpoint", required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--variants", type=int, default=8)
    parser.add_argument("--suite", choices=("legacy", "thermal-order"), default="legacy",
                        help="Thermal-order is separate from the historical 16-case suite")
    args = parser.parse_args()
    if not 1 <= args.variants <= 32:
        parser.error("variants must be 1..32")
    config = json.loads(args.config.read_text())
    runtime = json.loads(args.runtime.read_text())
    report = {"schema": "darkfactory-departure-probes-v1", "synthetic": True,
              "new_held_out_accuracy": False, "endpoint": args.endpoint,
              "config_sha256": hashlib.sha256(args.config.read_bytes()).hexdigest(),
              "runtime_sha256": hashlib.sha256(args.runtime.read_bytes()).hexdigest(), "rows": []}
    if args.suite == "thermal-order":
        report.update(schema="darkfactory-thermal-order-probes-v1", suite="thermal-order")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    suite = (thermal_order_cases(runtime) if args.suite == "thermal-order" else
             ((name, history, (("inspect", "weld"),)) for name, history in cases(runtime, args.variants)))
    for name, history, allowed in suite:
        payload = {"model": "dark-factory-agent", "messages": [{"role": "system", "content": config["system_prompt"]}, *history],
                   "tools": config["tools"], "tool_choice": "required", "parallel_tool_calls": False,
                   "temperature": 0, "seed": 42, "max_tokens": 256, "chat_template_kwargs": {"enable_thinking": False}}
        start = time.monotonic()
        with urlopen(Request(args.endpoint, json.dumps(payload).encode(), {"Content-Type": "application/json"}), timeout=120) as response:
            result = json.load(response)
        selected = choice(result["choices"][0]["message"])
        row = {"case": name, "choice": selected, "passed": selected is not None and tuple(selected) in allowed,
               "http_ms": (time.monotonic()-start)*1000, "response": result}
        if args.suite == "thermal-order":
            row["allowed_choices"] = allowed
        report["rows"].append(row)
        report["passed_count"] = sum(r["passed"] for r in report["rows"])
        args.output.write_text(json.dumps(report, indent=2)+"\n")
        print(json.dumps({k: v for k, v in row.items() if k != "response"}), flush=True)
    return int(report["passed_count"] != len(report["rows"]))


if __name__ == "__main__":
    raise SystemExit(main())
