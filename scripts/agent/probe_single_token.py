"""Offline probes for an unfine-tuned one-token controller; never executes jobs."""
from __future__ import annotations
import argparse
import copy
import hashlib
import json
from pathlib import Path
import statistics
import time

from single_token import Controller, VERSION
from replay_latency import choice, fault_probes
from probe_departure import cases as departure_cases, thermal_order_cases


def choice_matches(selected, expected=None, *, allowed_tools=None, allowed_choices=None):
    """Keep legacy broad fault checks separate from exact thermal-stage choices."""
    if allowed_choices is not None:
        return selected is not None and tuple(selected) in allowed_choices
    if allowed_tools:
        return selected is not None and selected[0] in allowed_tools
    return selected == expected


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--runtime", type=Path, required=True)
    p.add_argument("--config", type=Path, default=Path("configs/live_agent_legacy.json"))
    p.add_argument("--prompt", type=Path, help="Explicit offline candidate prompt")
    p.add_argument("--observation-format", choices=("json", "rows", "aliases", "named"), default="json")
    p.add_argument("--endpoint", default="http://127.0.0.1:18203")
    p.add_argument("--suite", choices=("replay", "faults", "departure", "thermal-order"), default="replay")
    p.add_argument("--continue-generated", action="store_true")
    p.add_argument("--output", type=Path, required=True)
    args = p.parse_args()
    if args.continue_generated and args.suite != "replay":
        p.error("Only replay can continue its generated conversation")
    runtime = json.loads(args.runtime.read_text())
    config = json.loads(args.config.read_text())
    controller = Controller(args.endpoint, config, args.prompt.read_text() if args.prompt else None, args.observation_format)
    messages = runtime["agent"]["current_body_messages"]
    if args.suite == "departure":
        cases = [(name, history, ["inspect", "weld"], None) for name, history in departure_cases(runtime, 8)]
    elif args.suite == "thermal-order":
        cases = [(name, history, None, allowed) for name, history, allowed in thermal_order_cases(runtime)]
    elif args.suite == "faults":
        cases = [(name, history, None, allowed) for name, history, allowed in fault_probes(messages)]
    else:
        cases = [(index, messages[:index], choice(message), None) for index, message in enumerate(messages) if message["role"] == "assistant"]
    report = {"schema": VERSION + "-probe", "endpoint": args.endpoint, "suite": args.suite,
              "runtime_sha256": hashlib.sha256(args.runtime.read_bytes()).hexdigest(),
              "config_sha256": hashlib.sha256(args.config.read_bytes()).hexdigest(),
              "prompt_sha256": controller.prompt_hash, "prompt": controller.prompt, "token_ids": controller.token_ids,
              "new_held_out_accuracy": False, "observation_format": args.observation_format,
              "continue_generated": args.continue_generated, "rows": []}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    generated, call_ids = {}, {}
    for index, history, expected, allowed in cases:
        if args.continue_generated:
            history = [copy.deepcopy(generated.get(i, m)) for i, m in enumerate(history)]
            for message in history:
                if message["role"] == "tool":
                    message["tool_call_id"] = call_ids.get(message["tool_call_id"], message["tool_call_id"])
        row = {"case": index, "expected": expected, "allowed": sorted(allowed) if allowed else None}
        if args.suite == "thermal-order":
            row.update(allowed=None, allowed_choices=allowed)
        start = time.monotonic()
        try:
            response, diagnostic = controller.decide(history)
            selected = choice(response["choices"][0]["message"])
            agrees = choice_matches(selected, expected,
                allowed_tools=allowed if args.suite != "thermal-order" else None,
                allowed_choices=allowed if args.suite == "thermal-order" else None)
            row.update(choice=selected, agrees=agrees, response=response, diagnostic=diagnostic)
            if args.continue_generated and agrees:
                generated[index] = response["choices"][0]["message"]
                call_ids[messages[index]["tool_calls"][0]["id"]] = generated[index]["tool_calls"][0]["id"]
        except Exception as exc:
            row.update(error=str(exc), agrees=False)
            if hasattr(exc, "diagnostic"):
                row["diagnostic"] = exc.diagnostic
        row["http_ms"] = (time.monotonic()-start)*1000
        report["rows"].append(row)
        report["agreement"] = sum(x["agrees"] for x in report["rows"])
        report["median_http_ms"] = statistics.median(x["http_ms"] for x in report["rows"])
        args.output.write_text(json.dumps(report, indent=2)+"\n")
        print(json.dumps({k: v for k, v in row.items() if k not in ("response", "diagnostic")}), flush=True)
        if args.continue_generated and not row["agrees"]:
            break
    return int(report["agreement"] != len(cases))


if __name__ == "__main__":
    raise SystemExit(main())
