"""Replay saved live-agent conversation prefixes against a candidate server.

Only the already model-visible messages/tools enter requests. Expected choices
remain evaluator-only. Fixed-history agreement is NOT closed-loop accuracy.
Run with an otherwise idle controller; its prompt cache affects these timings.
"""
from __future__ import annotations

import argparse
import copy
import hashlib
import json
from pathlib import Path
import statistics
import time
from urllib.request import Request, urlopen


def choice(message):
    calls = message.get("tool_calls", [])
    if len(calls) != 1:
        return None
    fn = calls[0]["function"]
    args = fn["arguments"]
    if isinstance(args, str):
        args = json.loads(args)
    return [fn["name"], args.get("stage", args.get("action", args.get("seconds")))]


def compact_observation(observation):
    """Drop duplicated diagnostics, retaining evidence, time and mechanical state."""
    omitted = {"tool", "argument", "latency_ms", "images"}
    result = {k: v for k, v in observation.items() if k not in omitted}
    if result.get("reason") == "model_classification":
        del result["reason"]
    for key in ("completed_clock_seconds", "capture_age_at_reply_seconds"):
        if key in result:
            result[key] = round(result[key], 3)
    return result


def compact_messages(messages):
    messages = copy.deepcopy(messages)
    for message in messages:
        if message["role"] == "tool":
            message["content"] = json.dumps(compact_observation(json.loads(message["content"])), separators=(",", ":"))
        elif message["role"] == "user":
            content = json.loads(message["content"])
            if "arrival" in content:
                content["arrival"] = compact_observation(content["arrival"])
            message["content"] = json.dumps(content, separators=(",", ":"))
    return messages


def fault_probes(messages):
    """Synthetic last-observation changes, never predictions of future replies."""
    specs = [("hot_sealant", "adhesive", "R", True, {"wait", "inspect", "hold"}),
             ("hot_weld", "weld", "R", True, {"wait", "inspect", "hold"}),
             ("seating_defect", "panel_seated", "R", True, {"hold"}),
             ("invalid_presence", "panel_presence", "A", False, {"inspect", "wait", "hold"})]
    for name, stage, code, valid, allowed in specs:
        for index, message in enumerate(messages):
            if message["role"] != "tool":
                continue
            result = json.loads(message["content"])
            if result.get("stage") != stage:
                continue
            history = copy.deepcopy(messages[:index+1])
            result.update(decision=code, valid=valid, status="observed" if valid else "invalid",
                          reason="model_classification" if valid else "stale_reply")
            history[-1]["content"] = json.dumps(result, separators=(",", ":"))
            yield name, history, allowed
            break


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime", type=Path, required=True)
    parser.add_argument("--endpoint", default="http://127.0.0.1:18195/v1/chat/completions")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--config", type=Path, help="Explicit candidate prompt/tools; saved history stays unchanged")
    parser.add_argument("--compact", action="store_true")
    parser.add_argument("--continue-generated", action="store_true", help="Retain actual earlier replies; stop on any choice mismatch before supplying its saved tool result")
    parser.add_argument("--rebuild-arguments", action="store_true", help="With --continue-generated, reproduce the old Unreal reason-first argument serialization for cache comparison")
    parser.add_argument("--fault-probes", action="store_true", help="Synthetic hot/invalid/defective last observations; no live jobs")
    parser.add_argument("--limit", type=int, default=0)
    parser.add_argument("--timeout", type=float, default=120)
    args = parser.parse_args()
    if args.rebuild_arguments and not args.continue_generated:
        parser.error("--rebuild-arguments requires --continue-generated")
    if args.continue_generated and args.fault_probes:
        parser.error("Independent fault prefixes cannot continue a generated conversation")
    runtime = json.loads(args.runtime.read_text(encoding="utf-8"))
    agent = runtime["agent"]
    if args.config:
        config = json.loads(args.config.read_text(encoding="utf-8"))
        agent.update(system_prompt=config["system_prompt"], tools=config["tools"])
    messages = agent["current_body_messages"]
    report = {"schema": "darkfactory-agent-prefix-replay-v1", "runtime_sha256": hashlib.sha256(args.runtime.read_bytes()).hexdigest(),
              "config_sha256": hashlib.sha256(args.config.read_bytes()).hexdigest() if args.config else None,
              "endpoint": args.endpoint, "compact_observations": args.compact, "synthetic_fault_probes": args.fault_probes,
              "continue_generated": args.continue_generated, "rebuild_arguments": args.rebuild_arguments,
              "new_held_out_accuracy": False, "rows": []}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    cases = [(index, messages[:index], choice(expected), None) for index, expected in enumerate(messages) if expected["role"] == "assistant"]
    if args.fault_probes:
        cases = [(name, history, None, allowed) for name, history, allowed in fault_probes(messages)]
    generated, call_ids = {}, {}
    for index, history, expected, allowed in cases:
        if args.continue_generated:
            history = [copy.deepcopy(generated.get(i, message)) for i, message in enumerate(history)]
            for message in history:
                if message["role"] == "tool":
                    message["tool_call_id"] = call_ids.get(message["tool_call_id"], message["tool_call_id"])
        if args.compact:
            history = compact_messages(history)
        payload = {"model": "dark-factory-agent", "messages": [{"role": "system", "content": agent["system_prompt"]}, *history],
                   "tools": agent["tools"], "tool_choice": "required", "parallel_tool_calls": False,
                   "temperature": 0, "seed": 42, "max_tokens": 256, "chat_template_kwargs": {"enable_thinking": False}}
        row = {"message_index": index, "expected_choice": expected}
        if allowed is not None:
            row["allowed_tools"] = sorted(allowed)
        start = time.perf_counter()
        try:
            with urlopen(Request(args.endpoint, json.dumps(payload).encode(), {"Content-Type": "application/json"}), timeout=args.timeout) as response:
                result = json.load(response)
            row.update(http_ms=(time.perf_counter()-start)*1000, response=result,
                       choice=choice(result["choices"][0]["message"]))
            row["agrees"] = (row["choice"] is not None and row["choice"][0] in allowed) if allowed is not None else row["choice"] == expected
            if args.continue_generated and row["agrees"]:
                message = copy.deepcopy(result["choices"][0]["message"])
                if args.rebuild_arguments:
                    fn = message["tool_calls"][0]["function"]
                    arguments = json.loads(fn["arguments"])
                    arguments = {"reason": arguments["reason"], **{k: v for k, v in arguments.items() if k != "reason"}}
                    fn["arguments"] = json.dumps(arguments, separators=(",", ":"))
                generated[index] = message
                call_ids[messages[index]["tool_calls"][0]["id"]] = message["tool_calls"][0]["id"]
        except Exception as exc:
            row.update(error=str(exc), agrees=False, http_ms=(time.perf_counter()-start)*1000)
        report["rows"].append(row)
        report["agreement"] = sum(r["agrees"] for r in report["rows"])
        report["median_http_ms"] = statistics.median(r["http_ms"] for r in report["rows"])
        args.output.write_text(json.dumps(report, indent=2)+"\n", encoding="utf-8")
        print(json.dumps({k: v for k, v in row.items() if k != "response"}), flush=True)
        if row.get("error") or (args.continue_generated and not row["agrees"]) or (args.limit and len(report["rows"]) >= args.limit):
            break
    return int(report["agreement"] != len(report["rows"]))


if __name__ == "__main__":
    raise SystemExit(main())
