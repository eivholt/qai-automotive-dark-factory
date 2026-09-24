"""Offline cached-suffix timings from saved single-token model-visible histories.

Use an otherwise idle native llama-server. This script changes its prompt cache,
but never launches a service, executes a tool, or contacts Unreal. Each timed
call follows preparation of only the policy or the known next-tool envelope;
the last actual observation is withheld until the timed call. Fixed-history
scores are development comparisons, never closed-loop or held-out accuracy.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import statistics
import time

from policy_cache import policy_prefix, tool_prefix, warm
from single_token import ACTIONS, Controller, VERSION, observation, score_response


SCHEMA = "darkfactory-cached-suffix-benchmark-v1"
MAX_LOGS = 64
MAX_LOG_BYTES = 2 * 1024 * 1024


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def validate_messages(messages, prompt):
    """Validate the existing JSON observation contract without reencoding it."""
    if not isinstance(messages, list) or not 2 <= len(messages) <= 130 or len(messages) % 2:
        raise ValueError("Requires a bounded complete model-visible conversation")
    for index, message in enumerate(messages):
        if not isinstance(message, dict) or set(message) != {"role", "content"} or not isinstance(message["content"], str):
            raise ValueError("Unexpected model-message fields")
        role = "system" if index == 0 else "user" if index % 2 else "assistant"
        if message["role"] != role:
            raise ValueError("Model-message roles are incomplete or out of order")
    if messages[0]["content"] != prompt:
        raise ValueError("Saved system prompt differs from Controller.prompt")
    arrival = json.loads(messages[1]["content"])
    if not isinstance(arrival, dict) or set(arrival) != {"body_id", "event", "arrival"}:
        raise ValueError("Unexpected arrival fields")
    observation(json.dumps(arrival["arrival"]))
    for index in range(2, len(messages), 2):
        if messages[index]["content"] not in ACTIONS:
            raise ValueError("Unknown recorded action code")
        result = messages[index+1]["content"]
        if not result.startswith("Tool result: "):
            raise ValueError("Requires the unchanged JSON tool-result envelope")
        observation(result.removeprefix("Tool result: "))


def load_records(directory, prompt):
    directory = Path(directory)
    files = sorted(path for path in directory.glob("request-*.json")
                   if re.fullmatch(r"request-\d{3}\.json", path.name))
    if not 1 <= len(files) <= MAX_LOGS:
        raise ValueError(f"Requires 1..{MAX_LOGS} request-NNN.json logs")
    prompt_hash = sha256(prompt.encode())
    records = []
    for path in files:
        if path.stat().st_size > MAX_LOG_BYTES:
            raise ValueError(f"Diagnostic log exceeds {MAX_LOG_BYTES} bytes: {path.name}")
        raw = path.read_bytes()
        document = json.loads(raw)
        if document.get("version") != VERSION or document.get("prompt_sha256") != prompt_hash:
            raise ValueError(f"Incompatible diagnostic provenance: {path.name}")
        messages = document["diagnostic"]["model_messages"]
        validate_messages(messages, prompt)
        records.append({"source": path.name, "source_sha256": sha256(raw),
                        "messages_sha256": sha256(json.dumps(messages, separators=(",", ":")).encode()),
                        "messages": messages})
    return records


def known_prefix(controller, messages):
    validate_messages(messages, controller.prompt)
    if len(messages) == 2:
        return "policy", policy_prefix(controller)
    return "tool-envelope", tool_prefix(controller, messages[:-2], messages[-2]["content"])


def completion(controller, rendered):
    # Match Controller.decide's ordinary unmasked, cached one-token request.
    return controller.post("/completion", {"prompt": rendered, "n_predict": 1, "temperature": 0,
        "n_probs": 128, "post_sampling_probs": False, "cache_prompt": True, "return_tokens": True,
        "seed": 42, "samplers": ["temperature"], "repeat_penalty": 1., "response_fields": ["content", "tokens",
            "tokens_predicted", "generation_settings", "completion_probabilities", "timings", "truncated"]})


def run_record(controller, record, pass_index):
    messages = record["messages"]
    prefix_kind, prefix = known_prefix(controller, messages)
    start = time.monotonic()
    prep = warm(controller, prefix)
    prep_ms = (time.monotonic()-start)*1000
    start = time.monotonic()
    rendered = controller.post("/apply-template", {"messages": messages, "add_generation_prompt": True})["prompt"]
    template_ms = (time.monotonic()-start)*1000
    if not rendered.startswith(prefix):
        raise ValueError("Prepared envelope is not a prefix of the full rendered prompt")
    start = time.monotonic()
    raw = completion(controller, rendered)
    http_ms = (time.monotonic()-start)*1000
    row = {key: record[key] for key in ("source", "source_sha256", "messages_sha256")}
    row.update({"pass": pass_index, "prefix_kind": prefix_kind, "prefix_sha256": sha256(prefix.encode()),
                "rendered_sha256": sha256(rendered.encode()), "prep_ms": prep_ms,
                "prep_timings": prep.get("timings"), "discarded_prep_tokens": prep.get("tokens_predicted", 0),
                "template_ms": template_ms, "http_ms": http_ms, "tokens": raw.get("tokens"),
                "timings": raw.get("timings"), "response": raw})
    try:
        if raw.get("truncated"):
            raise ValueError("Truncated controller context")
        row["score"] = score_response(raw, controller.token_ids)
    except ValueError as exc:
        row["error"] = str(exc)
    return row


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--logs", type=Path, required=True)
    parser.add_argument("--endpoint", required=True, help="Idle native llama-server URL, not the tool proxy")
    parser.add_argument("--config", type=Path, default=Path("configs/live_agent_legacy.json"))
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--passes", type=int, default=1)
    args = parser.parse_args()
    if not 1 <= args.passes <= 5:
        parser.error("passes must be 1..5")
    config_bytes = args.config.read_bytes()
    controller = Controller(args.endpoint, json.loads(config_bytes), observation_format="json")
    records = load_records(args.logs, controller.prompt)
    protected = {args.config.resolve(), *(args.logs / record["source"] for record in records)}
    if args.output.resolve() in {path.resolve() for path in protected}:
        parser.error("output must not overwrite configuration or input logs")
    report = {"schema": SCHEMA, "fixed_history_not_accuracy": True, "new_held_out_accuracy": False,
              "executes_tools": False, "endpoint": controller.endpoint, "logs": str(args.logs.resolve()),
              "config_sha256": sha256(config_bytes), "prompt_sha256": controller.prompt_hash,
              "token_ids": controller.token_ids, "passes": args.passes, "rows": []}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    for pass_index in range(args.passes):
        for record in records:
            row = run_record(controller, record, pass_index)
            report["rows"].append(row)
            report["median_ms"] = statistics.median(item["http_ms"] for item in report["rows"])
            report["median_template_ms"] = statistics.median(item["template_ms"] for item in report["rows"])
            report["score_errors"] = sum("error" in item for item in report["rows"])
            args.output.write_text(json.dumps(report, indent=2)+"\n", encoding="utf-8")
            print(json.dumps({key: row[key] for key in row if key not in ("response", "prep_timings")}), flush=True)
    return int(report["score_errors"] != 0)


if __name__ == "__main__":
    raise SystemExit(main())
