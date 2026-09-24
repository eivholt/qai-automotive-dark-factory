"""Measure exact known next-turn prefix work moved into tool execution time.

Offline replay only. No future result is guessed and no tool is executed. A
prefill response is discarded even if the pinned backend emits a setup token.
"""
import argparse
import copy
import json
from pathlib import Path
import statistics
import time

from single_token import Controller, model_messages, action_code
from policy_cache import policy_prefix, warm, tool_prefix


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--endpoint", required=True)
    p.add_argument("--runtime", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--config", type=Path, default=Path("configs/live_agent_legacy.json"))
    args = p.parse_args()
    history = json.loads(args.runtime.read_text())["agent"]["current_body_messages"]
    c = Controller(args.endpoint, json.loads(args.config.read_text()))
    report = {"scope": "Offline known-prefix overlap replay; no future observation or physical tool",
              "prompt_sha256": c.prompt_hash, "runs": []}
    for overlap in (False, True):
        c.post("/slots/0?action=erase", {})
        warm(c, policy_prefix(c))
        run = {"overlap": overlap, "rows": []}; report["runs"].append(run)
        generated = copy.deepcopy(history)
        for index, message in enumerate(history):
            if message["role"] != "assistant":
                continue
            messages = model_messages(generated[:index], c.prompt)
            start = time.monotonic()
            response, diagnostic = c.decide(generated[:index])
            elapsed = (time.monotonic()-start)*1000
            selected = response["choices"][0]["message"]
            code = action_code(selected)
            row = {"index": index, "code": code, "expected": action_code(message),
                   "decision_ms": elapsed, "diagnostic": diagnostic}
            run["rows"].append(row)
            if code != action_code(message):
                row["mismatch"] = True
            else:
                generated[index] = selected
                if index+1 < len(generated):
                    generated[index+1]["tool_call_id"] = selected["tool_calls"][0]["id"]
                if overlap and index+2 < len(generated):
                    prefix = tool_prefix(c, messages, code)
                    actual = c.post("/apply-template", {"messages": model_messages(generated[:index+2], c.prompt),
                                                       "add_generation_prompt": True})["prompt"]
                    if not actual.startswith(prefix):
                        raise ValueError("Known prefix differs from the next actual request")
                    start = time.monotonic()
                    row["prefill_response"] = warm(c, prefix)
                    row["prefill_ms"] = (time.monotonic()-start)*1000
            run["median_subsequent_ms"] = statistics.median(r["decision_ms"] for r in run["rows"][1:]) if len(run["rows"])>1 else None
            args.output.write_text(json.dumps(report, indent=2)+"\n")
            print(json.dumps({"overlap": overlap, **{k:v for k,v in row.items() if k not in ("diagnostic", "prefill_response")}}), flush=True)
            if row.get("mismatch"):
                break
    return int(any(r.get("mismatch") for run in report["runs"] for r in run["rows"]))


if __name__ == "__main__":
    raise SystemExit(main())
