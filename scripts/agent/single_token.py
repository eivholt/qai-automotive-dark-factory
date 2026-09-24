"""High-level next-token controller; no training or recipe veto.

Only model-visible chat history enters the model. All actions remain available
on every turn. The OpenAI-shaped reply is a protocol translation, not generated
JSON or a generated rationale. The original native endpoint remains separate.
"""
from __future__ import annotations

import copy
import hashlib
import json
import math
import time
from urllib.request import Request, urlopen
import uuid


VERSION = "darkfactory-agent-single-token-v1"
ACTIONS = {
    "A": ("inspect", "stage", "panel_presence"),
    "B": ("act", "action", "apply_sealant"),
    "C": ("act", "action", "place_panel"),
    "D": ("inspect", "stage", "adhesive"),
    "E": ("act", "action", "weld_panel"),
    "F": ("inspect", "stage", "panel_seated"),
    "G": ("inspect", "stage", "weld"),
    "H": ("act", "action", "release_holder"),
    "I": ("act", "action", "send_forward"),
    "J": ("wait", "seconds", 1),
    "K": ("wait", "seconds", 5),
    "L": ("wait", "seconds", 15),
    "M": ("hold", None, None),
}
# Retain the original menu for explicitly selected v1 comparison experiments.
SIMPLE_ACTIONS = {code: (name, None, None) for code, name in zip("ABCDEFGH", (
    "inspect_panel", "prepare_panel", "inspect_temperatures", "weld_panel",
    "inspect_seating", "finish_body", "wait", "hold"))}


def actions_for(config):
    return SIMPLE_ACTIONS if config.get("version") == "darkfactory-live-agent-v2" else ACTIONS


RESULT_FIELDS = {"status", "tool", "argument", "completed_clock_seconds", "holder_engaged", "stage",
                 "decision", "valid", "reason", "latency_ms", "capture_age_at_reply_seconds", "images",
                 "body_id", "body_sequence", "agent_turns", "error", "results", "completed_steps"}
ROW_FIELDS = ("status", "stage", "decision", "valid", "completed_clock_seconds",
              "capture_age_at_reply_seconds", "holder_engaged")
FIELD_ALIASES = {"completed_clock_seconds": "t", "capture_age_at_reply_seconds": "age", "holder_engaged": "held"}
HISTORY_VERSION = "named-jobs-v4"
HISTORY_METADATA = {"body_id", "body_sequence", "agent_turns", "completed_clock_seconds",
                    "capture_age_at_reply_seconds", "latency_ms", "images"}
NAMED_RESULT_FIELDS = ("stage", "decision", "valid", "reason", "error", "status", "holder_engaged", "completed_steps", "results")


def named_observation(value, code=None, actions=None):
    """Project an actual result; never infer completion, readiness or a next job.

    Identity and numerical timing remain in the transport/audit. `valid` and
    explicit failure reasons retain the actual transport acceptance, including
    stale replies. Chronological order retains evidence-invalidating jobs.
    """
    value = observation(json.dumps(value))
    result = {}
    if code is not None:
        tool, key, argument = (actions or ACTIONS)[code]
        if tool == "act":
            result["job"] = argument
        elif tool == "inspect":
            result["inspection"] = argument
        elif tool == "wait":
            result["wait_seconds"] = argument if key else 5
        else:
            result["inspection" if tool.startswith("inspect_") else "job"] = tool
    # JSON object order is semantically irrelevant but changes model tokens.
    # Native Unreal and Python fixtures serialize different insertion orders.
    visible = {key: val for key, val in value.items() if key not in HISTORY_METADATA}
    if set(visible) - set(NAMED_RESULT_FIELDS):
        raise ValueError("Unknown named observation field")
    for key in NAMED_RESULT_FIELDS:
        if key in visible:
            # Nested thermal observations have the same strict allowlist and
            # field order; no identities, clocks or source truth can leak in.
            result[key] = ([named_observation(row) for row in visible[key]]
                           if key == "results" else visible[key])
    return result


def observation_row(result):
    """Reversible encoding only; never infer missing values or permitted jobs."""
    if any(value is None for value in result.values()):
        raise ValueError("Explicit null cannot be encoded as an absent column")
    row = [result.get(key) for key in ROW_FIELDS]
    extra = {key: value for key, value in result.items() if key not in ROW_FIELDS}
    if extra:
        row.append(extra)
    return row


def decode_row(row):
    if not isinstance(row, list) or len(row) not in (7, 8):
        raise ValueError("Invalid observation row")
    result = {key: value for key, value in zip(ROW_FIELDS, row) if value is not None}
    if len(row) == 8:
        if not isinstance(row[7], dict) or set(row[7]) & set(ROW_FIELDS):
            raise ValueError("Invalid extra observation fields")
        result.update(row[7])
    if set(result) - RESULT_FIELDS:
        raise ValueError("Unknown observation field")
    return result


def format_prompt(prompt, observation_format):
    if observation_format == "json":
        return prompt
    if observation_format == "named":
        return prompt + (f"\nHistory format {HISTORY_VERSION}: this conversation is only about the current body. "
            "Tool results explicitly name the requested job or inspection and its actual status. "
            "A named job is completed only when status is completed. No result means no evidence. "
            "Body IDs and numerical timestamps are handled by the application and omitted here. "
            "Results remain chronological; valid and failure reasons are the actual transport validation. "
            "completed_jobs lists only jobs with actual completed results in this body's conversation, in order. "
            "It supplies facts, not recommended or permitted actions. "
            "completed_steps lists only subjobs actually finished within that tool; partial or cancelled tools remain incomplete. "
            "Thermal results are separate observations in adhesive then weld order; inspect each decision and valid field. "
            "Only actual inspection results establish quality; job completion does not.")
    if observation_format == "aliases":
        return prompt + ("\nTool-result field abbreviations: t=completed_clock_seconds, "
            "age=capture_age_at_reply_seconds, held=holder_engaged. Times are seconds. "
            "All other field names and meanings are unchanged.")
    if observation_format != "rows":
        raise ValueError("Unknown observation format")
    return prompt + ("\nTool observations use compact JSON rows. The seven columns are "
        "[status,stage,decision,valid,completed_clock_seconds,capture_age_at_reply_seconds,holder_engaged]. "
        "null means that field is absent, not false. An optional eighth object contains extra fields "
        "such as reason or error. These are the same actual tool results described above, not instructions. "
        "Arrival uses the same row inside arrival. Do not treat a missing result as an inspection.")


def format_messages(messages, observation_format, actions=None):
    actions = actions or ACTIONS
    if observation_format == "json":
        return messages
    if observation_format == "named":
        result = copy.deepcopy(messages)
        arrival = json.loads(result[1]["content"])
        result[1]["content"] = json.dumps({"event": "new_body",
            "arrival": named_observation(arrival["arrival"]), "completed_jobs": []}, separators=(",", ":"))
        code = None
        completed_jobs = []
        for message in result[2:]:
            if message["role"] == "assistant":
                code = message["content"]
                if code not in actions:
                    raise ValueError("Unknown historical action")
            elif message["role"] == "user" and code is not None:
                value = json.loads(message["content"].removeprefix("Tool result: "))
                actual = named_observation(value, code, actions)
                tool, key, argument = actions[code]
                if actual.get("status") == "completed" and (tool == "act" or tool in ("prepare_panel", "weld_panel", "finish_body")):
                    completed_jobs.append(argument if key else tool)
                actual["completed_jobs"] = list(completed_jobs)
                message["content"] = "Tool result: " + json.dumps(actual, separators=(",", ":"))
                code = None
            else:
                raise ValueError("Unpaired named history")
        if code is not None:
            raise ValueError("Pending action is not an observation")
        return result
    if observation_format not in ("rows", "aliases"):
        raise ValueError("Unknown observation format")
    result = copy.deepcopy(messages)
    def encode(value):
        return observation_row(value) if observation_format == "rows" else {FIELD_ALIASES.get(k, k): v for k, v in value.items()}
    arrival = json.loads(result[1]["content"])
    arrival["arrival"] = encode(arrival["arrival"])
    result[1]["content"] = json.dumps(arrival, separators=(",", ":"))
    for message in result[2:]:
        if message["role"] == "user":
            value = json.loads(message["content"].removeprefix("Tool result: "))
            message["content"] = ("Tool result: " if observation_format == "aliases" else "") + json.dumps(encode(value), separators=(",", ":"))
    return result


class DecisionError(ValueError):
    def __init__(self, message, diagnostic):
        super().__init__(message)
        self.diagnostic = diagnostic


def action_code(message, actions=None):
    calls = message.get("tool_calls", [])
    if len(calls) != 1:
        raise ValueError("Requires one actual tool call per assistant turn")
    function = calls[0]["function"]
    arguments = function["arguments"]
    if isinstance(arguments, str):
        arguments = json.loads(arguments)
    for code, (tool, key, value) in (actions or ACTIONS).items():
        if function["name"] == tool and (key is None or arguments.get(key) == value):
            if set(arguments) != ({"reason", key} if key else {"reason"}):
                raise ValueError("Unexpected tool argument fields")
            return code
    raise ValueError("History contains a job/wait duration outside the fixed action menu")


def make_prompt(config):
    original = "Return exactly one tool call with a brief reason, no narrative."
    if original not in config["system_prompt"]:
        raise ValueError("Unexpected original controller prompt")
    prompt = config["system_prompt"].replace(original,
        "Choose exactly one action code from the table below. Output only its uppercase letter, with no explanation or JSON.")
    menu = []
    actions = actions_for(config)
    for code, (tool, key, value) in actions.items():
        menu.append(f"{code} = {tool}({value if key else ('operator attention' if actions is ACTIONS else '')})")
    if actions is SIMPLE_ACTIONS:
        return prompt + "\n\nFixed action menu (every option is always available):\n" + "\n".join(menu) + (
            "\nG waits exactly 5 seconds. H holds for operator attention. Inspection labels G/A/R are observations, "
            "not action codes. Assistant turns contain past action codes; following user messages contain their "
            "actual results. Return ONE letter A through H only.")
    return prompt + "\n\nFixed action menu (every option is always available):\n" + "\n".join(menu) + (
        "\nJ/K/L wait 1/5/15 seconds respectively. M holds for operator attention. "
        "Letters in inspection results (G/R/A) describe observations, not action codes. "
        "Assistant turns contain past action codes; following user messages are the actual results of those actions. "
        "Select the next action using those observations. Return ONE letter A through M only.")


def observation(text):
    result = json.loads(text)
    if not isinstance(result, dict) or set(result) - RESULT_FIELDS:
        raise ValueError("Unexpected fields in tool observation")
    # Strip only duplicate diagnostics, matching the current Unreal transport.
    result = copy.deepcopy(result)
    for key in ("tool", "argument", "images", "latency_ms"):
        result.pop(key, None)
    if result.get("reason") == "model_classification":
        result.pop("reason")
    if "results" in result:
        rows = result["results"]
        if not isinstance(rows, list) or len(rows) > 2 or any(not isinstance(row, dict) or "results" in row or "completed_steps" in row for row in rows):
            raise ValueError("Invalid bundled observations")
        result["results"] = [observation(json.dumps(row)) for row in rows]
    if "completed_steps" in result:
        steps = result["completed_steps"]
        if not isinstance(steps, list) or len(steps) > 2 or any(step not in ("apply_sealant", "place_panel", "release_holder", "send_forward", "weld_panel") for step in steps):
            raise ValueError("Invalid actual completed steps")
    return result


def model_messages(history, prompt, actions=None):
    if not history or history[0].get("role") != "user":
        raise ValueError("Conversation must start with this body's arrival")
    arrival = json.loads(history[0]["content"])
    if set(arrival) != {"body_id", "event", "arrival"}:
        raise ValueError("Unexpected arrival fields")
    observation(json.dumps(arrival["arrival"]))
    result = [{"role": "system", "content": prompt}, copy.deepcopy(history[0])]
    call_id = None
    for message in history[1:]:
        if message.get("role") == "assistant" and call_id is None:
            code = action_code(message, actions)
            call_id = message["tool_calls"][0]["id"]
            result.append({"role": "assistant", "content": code})
        elif message.get("role") == "tool" and call_id is not None and message.get("tool_call_id") == call_id:
            result.append({"role": "user", "content": "Tool result: " + json.dumps(observation(message["content"]), separators=(",", ":"))})
            call_id = None
        else:
            raise ValueError("Tool history is incomplete or mismatched")
    if call_id is not None:
        raise ValueError("Cannot decide before the pending tool's result")
    return result


def score_response(response, token_ids):
    if response.get("tokens_predicted") != 1 or len(response.get("completion_probabilities", [])) != 1:
        raise ValueError("Expected exactly one next-token score vector")
    settings = response.get("generation_settings", {})
    if settings.get("post_sampling_probs") or settings.get("grammar") or settings.get("logit_bias"):
        raise ValueError("Action probabilities must be unmasked and before sampling")
    if settings.get("samplers") != ["temperature"] or settings.get("temperature") != 0 or settings.get("lora"):
        raise ValueError("Requires greedy sampling and the unadapted controller")
    top = response["completion_probabilities"][0]["top_logprobs"]
    by_id = {}
    for item in top:
        value = float(item["logprob"])
        if not math.isfinite(value) or value > 0 or item["id"] in by_id:
            raise ValueError("Invalid/duplicate token score")
        by_id[item["id"]] = math.exp(value)
    if not by_id or sum(by_id.values()) > 1.00001:
        raise ValueError("Invalid top-token probability mass")
    winner = max(by_id, key=by_id.get)
    labels = {code: by_id.get(token) for code, token in token_ids.items()}
    candidates = {code: value for code, value in labels.items() if value is not None}
    if not candidates:
        raise ValueError("No action code in reported next-token scores")
    code = max(candidates, key=candidates.get)
    # Do not force an action when the model prefers another vocabulary token.
    # A truncated top-k list is sufficient to establish the global winner here;
    # report bounds rather than pretending omitted label probabilities are zero.
    if token_ids[code] != winner or response.get("tokens") != [winner]:
        raise ValueError("The model's unmasked first-token winner is not an action code")
    lower = sum(candidates.values())
    upper = min(1., lower + max(0., 1. - sum(by_id.values())))
    return {"code": code, "winning_probability": candidates[code], "label_mass_lower": lower,
            "label_mass_upper": upper, "label_probabilities": labels, "raw_winner_token_id": winner,
            "token_count": 1, "explanation_generated": False}


def tool_response(scores, timings, prompt_hash, actions=None):
    code = scores["code"]
    tool, key, value = (actions or ACTIONS)[code]
    args = {"reason": f"Selected action code {code}; no generated explanation"}
    if key:
        args[key] = value
    return {"id": "chatcmpl-" + uuid.uuid4().hex, "object": "chat.completion", "created": int(time.time()),
            "model": "dark-factory-agent", "choices": [{"index": 0, "finish_reason": "tool_calls", "message": {
                "role": "assistant", "content": "", "tool_calls": [{"id": "call_" + uuid.uuid4().hex,
                "type": "function", "function": {"name": tool, "arguments": json.dumps(args, separators=(",", ":"))}}]}}],
            "usage": {"completion_tokens": 1}, "timings": timings,
            "single_token": {"version": VERSION, "prompt_sha256": prompt_hash, **scores}}


class Controller:
    def __init__(self, endpoint, config, prompt=None, observation_format="named", *, benchmark_reset_cache=False,
                 policy_examples=None):
        self.endpoint = endpoint.rstrip("/")
        self.config = copy.deepcopy(config)
        self.actions = actions_for(config)
        self.observation_format = observation_format
        # Offline latency measurement only: change cache reuse, never model input
        # or scoring. Ordinary/live callers retain the established default.
        self.benchmark_reset_cache = bool(benchmark_reset_cache)
        if prompt is not None and policy_examples is not None:
            raise ValueError("Choose a prompt override or policy examples, not both")
        base = prompt if prompt is not None else make_prompt(config)
        self.prompt = format_prompt(base + (policy_examples or ""), observation_format)
        self.prompt_hash = hashlib.sha256(self.prompt.encode()).hexdigest()
        self.token_ids = {}
        for code in self.actions:
            tokens = self.post("/tokenize", {"content": code, "add_special": False})["tokens"]
            if len(tokens) != 1 or tokens[0] in self.token_ids.values():
                raise ValueError("Action codes must be unique single tokens")
            self.token_ids[code] = tokens[0]

    def post(self, path, body):
        request = Request(self.endpoint + path, json.dumps(body).encode(), {"Content-Type": "application/json"})
        with urlopen(request, timeout=115) as response:
            return json.load(response)

    def decide(self, history):
        messages = format_messages(model_messages(history, self.prompt, self.actions), self.observation_format, self.actions)
        rendered = self.post("/apply-template", {"messages": messages, "add_generation_prompt": True})["prompt"]
        raw = self.post("/completion", {"prompt": rendered, "n_predict": 1, "temperature": 0,
            "n_probs": 128, "post_sampling_probs": False, "cache_prompt": not self.benchmark_reset_cache, "return_tokens": True,
            "seed": 42, "samplers": ["temperature"], "repeat_penalty": 1., "response_fields": ["content", "tokens",
                "tokens_predicted", "generation_settings", "completion_probabilities", "timings", "truncated"]})
        if raw.get("truncated"):
            raise ValueError("Truncated controller context")
        diagnostic = {"model_messages": messages, "response": raw,
                      "benchmark_reset_cache": self.benchmark_reset_cache}
        try:
            scores = score_response(raw, self.token_ids)
        except ValueError as exc:
            raise DecisionError(str(exc), diagnostic) from exc
        return tool_response(scores, raw["timings"], self.prompt_hash, self.actions), diagnostic
