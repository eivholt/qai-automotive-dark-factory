"""Isolated exact-prefix cache experiment. Generates no dispatchable tool calls."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import time

from single_token import Controller, model_messages
from policy_cache import policy_prefix, warm



def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--endpoint", required=True)
    p.add_argument("--runtime", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--config", type=Path, default=Path("configs/live_agent_legacy.json"))
    args = p.parse_args()
    c = Controller(args.endpoint, json.loads(args.config.read_text()))
    history = json.loads(args.runtime.read_text())["agent"]["current_body_messages"][:1]
    prefix = policy_prefix(c)
    actual = c.post("/apply-template", {"messages": model_messages(history, c.prompt), "add_generation_prompt": True})["prompt"]
    if not actual.startswith(prefix):
        raise ValueError("Policy prefix does not match the actual first request")
    report = {"prompt_sha256": c.prompt_hash, "prefix_sha256": hashlib.sha256(prefix.encode()).hexdigest(),
              "scope": "Offline cache timings; no physical tools executed", "rows": []}
    def timed(name, fn):
        start = time.monotonic()
        result = fn()
        row = {"name": name, "http_ms": (time.monotonic()-start)*1000, "result": result}
        report["rows"].append(row)
        args.output.write_text(json.dumps(report, indent=2)+"\n")
        print(json.dumps({"name": name, "http_ms": row["http_ms"]}), flush=True)
        return result
    filename = "policy-" + report["prefix_sha256"] + ".bin"
    c.post("/slots/0?action=erase", {})
    timed("uncached_first_decision", lambda: c.decide(history))
    c.post("/slots/0?action=erase", {})
    timed("policy_prefill", lambda: warm(c, prefix))
    timed("save_policy", lambda: c.post("/slots/0?action=save", {"filename": filename}))
    timed("warm_first_decision", lambda: c.decide(history))
    c.post("/slots/0?action=erase", {})
    timed("restore_policy", lambda: c.post("/slots/0?action=restore", {"filename": filename}))
    timed("restored_first_decision", lambda: c.decide(history))
    decisions = [row["result"][0]["single_token"]["code"] for row in report["rows"] if "decision" in row["name"]]
    report["choices_identical"] = len(set(decisions)) == 1 and decisions[0] == "A"
    args.output.write_text(json.dumps(report, indent=2)+"\n")
    return int(not report["choices_identical"])


if __name__ == "__main__":
    raise SystemExit(main())
