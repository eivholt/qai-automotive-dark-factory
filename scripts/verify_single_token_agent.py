"""Cross-check private one-token model logs against actual Unreal dispatches."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent / "agent"))
from single_token import ACTIONS, VERSION, make_prompt, score_response, observation, format_prompt, format_messages, HISTORY_VERSION, actions_for
from verify_live_agent import verify as verify_live


def verify(runtime, logs, health, config, prompt_override=None, policy_examples=None):
    errors = list(verify_live(runtime)["errors"])
    actions = actions_for(config)
    def check(value, reason):
        if not value:
            errors.append(reason)
    history_format = health.get("observation_format", "json")
    check(history_format in ("json", "named"), "unsupported history format")
    if history_format == "named":
        check(health.get("history_version") == HISTORY_VERSION, "named-history version mismatch")
    if prompt_override is not None and policy_examples is not None:
        raise ValueError("Choose a prompt override or policy examples, not both")
    prompt = format_prompt((prompt_override if prompt_override is not None else make_prompt(config)) + (policy_examples or ""), history_format)
    prompt_hash = hashlib.sha256(prompt.encode()).hexdigest()
    check(health["version"] == VERSION and health["prompt_sha256"] == prompt_hash, "protocol/prompt mismatch")
    tokens = health["token_ids"]
    check(set(tokens) == set(actions) and len(set(tokens.values())) == len(actions), "invalid token map")
    choices = [x for x in runtime["agent"]["trace"] if x["event"] == "agent_choice"]
    observations = [x for x in runtime["agent"]["trace"] if x["event"] == "agent_observation"]
    check(len(logs) == len(choices), "model log/dispatch count differs")
    check(len(observations) == len(choices), "observation/dispatch count differs")
    minimum_probability = 1.
    previous_codes = {}
    for index, (log, event) in enumerate(zip(logs, choices)):
        try:
            check(not log.get("error") and log["prompt_sha256"] == prompt_hash, f"request {index}: error/prompt mismatch")
            if history_format == "named":
                check(log.get("request_body_id") == event["body_id"], f"request {index}: transport body mismatch")
            raw = log["diagnostic"]["response"]
            scores = score_response(raw, tokens)
            check(raw["generation_settings"]["samplers"] == ["temperature"] and raw["generation_settings"]["temperature"] == 0,
                  f"request {index}: changed sampler")
            check(not raw["generation_settings"].get("lora"), f"request {index}: unexpected controller LoRA")
            check(log["response"]["usage"]["completion_tokens"] == 1, f"request {index}: not one token")
            check(log["response"]["single_token"]["code"] == scores["code"], f"request {index}: score/code mismatch")
            messages = log["diagnostic"]["model_messages"]
            check(messages[0] == {"role": "system", "content": prompt}, f"request {index}: altered policy")
            observed = observations[index]
            check(observed["body_id"] == event["body_id"], f"request {index}: observation body mismatch")
            actual_results = []
            for result in observed["detail"]["tool_results"]:
                compact = observation(json.dumps(result))
                for row in [compact, *compact.get("results", [])]:
                    for key in ("completed_clock_seconds", "capture_age_at_reply_seconds"):
                        if key in row:
                            row[key] = round(row[key], 3)
                actual_results.append(compact)
            prior = previous_codes.setdefault(event["body_id"], [])
            check(len(actual_results) == len(prior)+1, f"request {index}: observation/action count mismatch")
            expected = [{"role": "system", "content": prompt}, {"role": "user", "content": json.dumps({
                "body_id": event["body_id"], "event": "new_body_arrived_choose_tools_to_complete_the_task",
                "arrival": actual_results[0]})}]
            for code, result in zip(prior, actual_results[1:]):
                expected.extend([{"role": "assistant", "content": code},
                    {"role": "user", "content": "Tool result: " + json.dumps(result)}])
            expected = format_messages(expected, history_format, actions)
            def canonical(rows):
                return [{"role": row["role"], "content": json.loads(row["content"].removeprefix("Tool result: "))
                         if row["role"] == "user" else row["content"]} for row in rows]
            same = messages == expected if history_format == "named" else canonical(messages) == canonical(expected)
            check(same, f"request {index}: model history differs from actual jobs/observations or canonical field order")
            prior.append(scores["code"])
            tool, key, value = actions[scores["code"]]
            actual = event["detail"]
            check(actual["tool"] == tool, f"request {index}: tool differs from model choice")
            if key:
                check(actual["seconds"] == value if key == "seconds" else actual["argument"] == value, f"request {index}: argument differs from model choice")
            check(actual["reason"] == f"Selected action code {scores['code']}; no generated explanation", f"request {index}: fabricated explanation")
            minimum_probability = min(minimum_probability, scores["winning_probability"])
        except (ValueError, KeyError, TypeError, IndexError) as exc:
            errors.append(f"request {index}: {exc}")
    return {"passed": not errors, "errors": errors, "requests": len(logs), "tokens_per_decision": 1,
            "prompt_sha256": prompt_hash, "minimum_winning_probability": minimum_probability,
            "new_held_out_accuracy": False, "scope": "Actual model next-token scores matched to live dispatch; no recipe correction"}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--runtime", type=Path, required=True)
    p.add_argument("--logs", type=Path, required=True)
    p.add_argument("--health", type=Path, required=True)
    p.add_argument("--config", type=Path, default=Path("configs/live_agent.json"))
    p.add_argument("--prompt", type=Path, help="Explicit independently supplied candidate policy, if used")
    p.add_argument("--policy-examples", type=Path, help="Explicit independently supplied static policy examples, if used")
    p.add_argument("--output", type=Path, required=True)
    args = p.parse_args()
    read = lambda path: json.loads(path.read_text())
    result = verify(read(args.runtime), [read(path) for path in sorted(args.logs.glob("request-*.json"))], read(args.health), read(args.config),
                    args.prompt.read_text() if args.prompt else None,
                    args.policy_examples.read_text() if args.policy_examples else None)
    args.output.write_text(json.dumps(result, indent=2)+"\n")
    print(json.dumps(result, indent=2))
    return int(not result["passed"])


if __name__ == "__main__":
    raise SystemExit(main())
