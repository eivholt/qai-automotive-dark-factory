"""Bounded saved-input HTTP replay for controller/Reason2 device contention.

No physical actions run. Sequential replay waits for a matching inspect choice;
concurrent replay is an independent stress/throughput experiment, not a causal
agent flow. Request files are sent byte for byte, including their old identities
and timestamps. Results are timing/integration evidence, never fresh gate evidence
or held-out accuracy. Run on otherwise idle endpoints. Cache policy belongs to
the server and must be recorded separately; this client does not change it.
"""
from __future__ import annotations

import argparse
from collections import Counter
from concurrent.futures import ThreadPoolExecutor
import hashlib
import http.client
import json
import math
from pathlib import Path
import socket
import sys
import threading
import time
from urllib.parse import urlsplit

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "src"))
sys.path.insert(0, str(Path(__file__).resolve().parent))
from dark_factory.gates import (IDENTITY_FIELDS, MAX_GATE_REQUEST_BYTES, MESSAGE_LAYOUT,
                                load_gate_contract, parse_gate_request)
from single_token import ACTIONS, action_code

SCHEMA = "darkfactory-npu-contention-replay-v1"
MODES = ("agent_only", "classifier_only", "sequential", "concurrent")
MAX_RESPONSE_BYTES = 262144
TIMING_FIELDS = {"inference_ms", "ttft_ms", "prompt_n", "prompt_ms", "prompt_per_token_ms",
                 "prompt_per_second", "predicted_n", "predicted_ms", "predicted_per_token_ms",
                 "predicted_per_second", "cache_n", "cache_mode", "cache_mode_requested",
                 "cache_fallback", "submitted_prompt_n", "retained_context_tokens",
                 "context_occupancy", "reused_tokens_measured", "preprocessing_ms", "total_ms"}
AGENT_FIELDS = {"model", "messages", "tools", "tool_choice", "parallel_tool_calls",
                "temperature", "seed", "max_tokens", "max_completion_tokens", "top_p",
                "stream", "stop", "chat_template_kwargs", "cache_prompt", "response_format",
                "logprobs", "top_logprobs", "presence_penalty", "frequency_penalty", "n"}


def strict_json(data):
    def pairs(items):
        result = {}
        for key, value in items:
            if key in result:
                raise ValueError("Duplicate JSON field")
            result[key] = value
        return result
    def invalid(_):
        raise ValueError("Non-finite JSON value")
    return json.loads(data, object_pairs_hook=pairs, parse_constant=invalid)


def endpoint_url(value, role):
    url = urlsplit(value)
    path = "/v1/chat/completions" if role == "agent" else "/v1/inspect"
    if (url.scheme not in ("http", "https") or not url.hostname or url.username
            or url.password or url.query or url.fragment or url.path not in ("", "/", path)):
        raise ValueError("Endpoint must be an HTTP(S) origin or the matching inference path, without credentials/query")
    # Accessing port also validates its syntax and range.
    _ = url.port
    return value.rstrip("/") if url.path == path else value.rstrip("/") + path


def load_payload(path, role, contract):
    limit = 262144 if role == "agent" else MAX_GATE_REQUEST_BYTES
    with Path(path).open("rb") as stream:
        raw = stream.read(limit + 1)
    if not 0 < len(raw) <= limit:
        raise ValueError(f"{role} request exceeds its byte limit")
    payload = strict_json(raw)
    if role == "classifier":
        parse_gate_request(payload, contract)
    elif (not isinstance(payload, dict) or set(payload) - AGENT_FIELDS
          or not isinstance(payload.get("model"), str) or not payload["model"]
          or not isinstance(payload.get("messages"), list) or not payload["messages"]
          or payload.get("stream", False) is not False or payload.get("n", 1) != 1):
        raise ValueError("Expected one non-streaming OpenAI chat request, without evaluator fields")
    return {"raw": raw, "payload": payload, "sha256": hashlib.sha256(raw).hexdigest(),
            "bytes": len(raw)}


def post_json(endpoint, raw, timeout):
    """No redirects/retries; a wall deadline closes the socket even during reads."""
    url = urlsplit(endpoint)
    cls = http.client.HTTPSConnection if url.scheme == "https" else http.client.HTTPConnection
    connection = cls(url.hostname, url.port, timeout=timeout)
    expired = threading.Event()
    connected_socket = []
    def expire():
        expired.set()
        # HTTPConnection drops its own socket reference for Connection: close
        # once headers arrive; retain it while HTTPResponse reads the body.
        sock = connected_socket[0] if connected_socket else connection.sock
        if sock is not None:
            try:
                sock.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
            sock.close()
    timer = threading.Timer(timeout, expire)
    timer.daemon = True
    timer.start()
    try:
        connection.request("POST", url.path, body=raw, headers={
            "Content-Type": "application/json", "Accept": "application/json", "Connection": "close"})
        connected_socket.append(connection.sock)
        if expired.is_set():
            raise TimeoutError("HTTP wall deadline expired")
        with connection.getresponse() as response:
            status = response.status
            data = response.read(MAX_RESPONSE_BYTES + 1)
        if expired.is_set():
            raise TimeoutError("HTTP wall deadline expired")
        if len(data) > MAX_RESPONSE_BYTES:
            raise ValueError("Response exceeds byte limit")
        if status != 200:
            # Do not copy arbitrary server text or request data into the report.
            raise ValueError(f"HTTP status {status}")
        return strict_json(data), hashlib.sha256(data).hexdigest(), len(data)
    finally:
        timer.cancel()
        connection.close()


def validate_response(role, response, request, contract):
    if not isinstance(response, dict) or response.get("error") is not None:
        raise ValueError("Endpoint returned an error or a non-object response")
    if role == "agent":
        choices = response.get("choices")
        if not isinstance(choices, list) or len(choices) != 1:
            raise ValueError("Agent must return exactly one choice")
        choice = choices[0]
        if choice.get("finish_reason") not in ("stop", "tool_calls"):
            raise ValueError("Agent did not finish a complete choice")
        message = choice.get("message", {})
        if message.get("role") != "assistant":
            raise ValueError("Agent returned a non-assistant message")
        if message.get("tool_calls"):
            code = action_code(message)
        else:
            content = message.get("content")
            code = content.strip() if isinstance(content, str) else ""
            if code not in ACTIONS:
                raise ValueError("Agent did not return one supported action code/tool call")
        if "single_token" in response and response["single_token"].get("code") != code:
            raise ValueError("Agent action code and tool call disagree")
        tool, key, value = ACTIONS[code]
        return {"code": code, "tool": tool, "argument": value if key else None}
    stage = contract["stages"][request["stage"]]
    if (response.get("valid") is not True or response.get("decision") not in ("G", "A", "R")
            or any(response.get(key) != request[key] for key in IDENTITY_FIELDS)):
        raise ValueError("Invalid classifier decision or mismatched replay identity")
    required = {"prompt_sha256": stage["prompt_sha256"], "camera_ids": stage["camera_ids"],
                "modality": stage["modality"], "decision_policy": contract["decision_policy"],
                "message_layout": MESSAGE_LAYOUT}
    if any(response.get(key) != value for key, value in required.items()):
        raise ValueError("Classifier response differs from the recorded sensor/prompt/policy contract")
    return {"decision": response["decision"], "stage": request["stage"], "valid": True}


def call_endpoint(role, endpoint, source, contract, timeout, epoch):
    started = time.perf_counter()
    row = {"role": role, "start_ms": (started - epoch) * 1000}
    try:
        response, digest, size = post_json(endpoint, source["raw"], timeout)
        row.update(response_sha256=digest, response_bytes=size,
                   result=validate_response(role, response, source["payload"], contract), ok=True)
        # Bounded provenance only; raw replies may contain large token vectors.
        row["backend"] = {key: response[key][:240] for key in
                          ("source", "model", "backend_alias", "stage_backend_alias")
                          if isinstance(response.get(key), str)}
        timings = response.get("timings", response.get("timing", {}))
        if isinstance(timings, dict):
            row["server_timings"] = {key: (value[:300] if isinstance(value, str) else value)
                for key, value in timings.items() if key in TIMING_FIELDS and
                (value is None or isinstance(value, (str, bool)) or
                 isinstance(value, (int, float)) and math.isfinite(value))}
    except Exception as exc:
        row.update(ok=False, error_type=type(exc).__name__, error=str(exc)[:300])
    ended = time.perf_counter()
    row.update(end_ms=(ended - epoch) * 1000, http_ms=(ended - started) * 1000)
    return row


def run_trial(mode, index, endpoints, sources, contract, timeout):
    epoch = time.perf_counter()
    def call(role):
        return call_endpoint(role, endpoints[role], sources[role], contract, timeout, epoch)
    row = {"mode": mode, "repeat": index, "calls": []}
    if mode in ("agent_only", "classifier_only"):
        row["calls"].append(call(mode.removesuffix("_only")))
    elif mode == "sequential":
        agent = call("agent")
        row["calls"].append(agent)
        if (agent["ok"] and agent["result"]["tool"] == "inspect"
                and agent["result"]["argument"] == sources["classifier"]["payload"]["stage"]):
            row["calls"].append(call("classifier"))
        else:
            row["error"] = "Classifier not sent: agent did not select this saved request's inspection"
    elif mode == "concurrent":
        # Two total workers, one call per distinct endpoint. Join both before the
        # next trial, including failure cases; no queued follow-up model work.
        barrier = threading.Barrier(2)
        def simultaneous(role):
            barrier.wait(timeout=timeout)
            return call(role)
        with ThreadPoolExecutor(max_workers=2) as pool:
            futures = [pool.submit(simultaneous, role) for role in ("agent", "classifier")]
            row["calls"] = [future.result() for future in futures]
    else:
        raise ValueError("Unknown benchmark mode")
    row["makespan_ms"] = (time.perf_counter() - epoch) * 1000
    row["ok"] = "error" not in row and all(call["ok"] for call in row["calls"])
    if len(row["calls"]) == 2:
        a, b = row["calls"]
        row["dispatch_skew_ms"] = abs(a["start_ms"] - b["start_ms"])
        row["http_overlap_ms"] = max(0, min(a["end_ms"], b["end_ms"]) - max(a["start_ms"], b["start_ms"]))
    return row


def stats(values):
    values = sorted(values)
    def percentile(q):
        if not values:
            return None
        pos = (len(values) - 1) * q
        low, high = math.floor(pos), math.ceil(pos)
        return values[low] + (values[high] - values[low]) * (pos - low)
    return {"count": len(values), "p50_ms": percentile(.5), "p95_ms": percentile(.95)}


def summarize(rows):
    summary = {}
    for mode in MODES:
        trials = [row for row in rows if row["mode"] == mode]
        calls = [call for row in trials for call in row["calls"]]
        summary[mode] = {"trials": len(trials), "failed_trials": sum(not r["ok"] for r in trials),
                         "makespan": stats([r["makespan_ms"] for r in trials if r["ok"]]),
                         "calls": {role: stats([c["http_ms"] for c in calls if c["role"] == role and c["ok"]])
                                   for role in ("agent", "classifier")}}
        for role in ("agent", "classifier"):
            own = [c for c in calls if c["role"] == role]
            summary[mode]["calls"][role]["errors"] = sum(not c["ok"] for c in own)
    for mode in ("sequential", "concurrent"):
        for role in ("agent", "classifier"):
            target = summary[mode]["calls"][role]
            baseline = summary[role + "_only"]["calls"][role]
            for p in ("p50", "p95"):
                target[p + "_slowdown"] = (target[p + "_ms"] / baseline[p + "_ms"]
                    if target[p + "_ms"] is not None and baseline[p + "_ms"] else None)
    reference = next((c["result"]["decision"] for row in rows if row["mode"] == "classifier_only"
                      for c in row["calls"] if c["ok"]), None)
    decisions = Counter()
    mismatches = []
    for row in rows:
        for call in row["calls"]:
            baseline = summary[call["role"] + "_only"]["calls"][call["role"]]["p50_ms"]
            call["slowdown_vs_baseline_p50"] = call["http_ms"] / baseline if call["ok"] and baseline else None
            if call["role"] == "classifier" and call["ok"]:
                decision = call["result"]["decision"]
                decisions[decision] += 1
                call["matches_baseline_decision"] = decision == reference if reference is not None else None
                if reference is not None and decision != reference:
                    mismatches.append({"mode": row["mode"], "repeat": row["repeat"], "decision": decision})
    sequential = summary["sequential"]["makespan"]["p50_ms"]
    concurrent = summary["concurrent"]["makespan"]["p50_ms"]
    return {"modes": summary, "classifier_stability": {"baseline_decision": reference,
            "decision_counts": dict(decisions), "mismatches": mismatches,
            "stable": not mismatches if reference is not None and decisions else None},
            "concurrent_pair_speedup_vs_sequential_p50": sequential / concurrent if sequential and concurrent else None}


def benchmark(endpoints, sources, contract, repeats, warmup, timeout, output):
    if not (1 <= repeats <= 100 and 0 <= warmup <= 10 and math.isfinite(timeout) and .1 <= timeout <= 300):
        raise ValueError("Require repeats 1..100, warmup 0..10 and timeout 0.1..300 seconds")
    if endpoints["agent"] == endpoints["classifier"]:
        raise ValueError("Agent and classifier endpoints must be distinct")
    report = {"schema": SCHEMA, "scope": "saved-input replay; no physical actions or fresh gate evidence",
              "concurrent_scope": "independent stress/throughput experiment, not causal agent flow",
              "sequential_scope": "agent then matching saved inspection; no invented future observation",
              "new_held_out_accuracy": False, "request_bytes_unchanged": True,
              "cache_reset": False, "percentile_method": "linear interpolation over successful calls/trials",
              "cache_interpretation": "Identical saved requests use each endpoint's existing cache policy. "
                  "Repeated prompts do not measure growing-prefix append reuse; this is not a matched-cache kernel comparison.",
              "overlap_interpretation": "HTTP overlap is measured; simultaneous accelerator execution is not established.",
              "mode_order": list(MODES), "endpoints": endpoints, "repeats": repeats,
              "warmup_per_mode": warmup, "timeout_seconds": timeout,
              "inputs": {role: {k: source[k] for k in ("sha256", "bytes")} for role, source in sources.items()},
              "stage": sources["classifier"]["payload"]["stage"], "warmup": [], "rows": [], "aborted": False}
    output = Path(output)
    output.parent.mkdir(parents=True, exist_ok=True)
    def save():
        report["summary"] = summarize(report["rows"])
        report["warmup_summary"] = summarize(report["warmup"])
        report["passed"] = (not report["aborted"] and len(report["rows"]) == repeats * len(MODES)
                            and report["summary"]["classifier_stability"]["stable"] is True)
        output.write_text(json.dumps(report, indent=2, allow_nan=False) + "\n", encoding="utf-8")
    save()
    for mode in MODES:
        for phase, count in (("warmup", warmup), ("rows", repeats)):
            for index in range(count):
                row = run_trial(mode, index, endpoints, sources, contract, timeout)
                report[phase].append(row)
                if not row["ok"]:
                    # After a timeout/disconnect the server may still be busy.
                    # Stop rather than risk a second outstanding native job.
                    report["aborted"] = True
                save()
                print(json.dumps({"phase": phase, "mode": mode, "repeat": index,
                                  "ok": row["ok"], "makespan_ms": row["makespan_ms"]}), flush=True)
                if report["aborted"]:
                    return report
    return report


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--agent-endpoint", required=True)
    p.add_argument("--classifier-endpoint", required=True)
    p.add_argument("--agent-payload", type=Path, required=True, help="Private exact OpenAI-shaped request JSON")
    p.add_argument("--classifier-payload", type=Path, required=True, help="Private exact /v1/inspect request JSON")
    p.add_argument("--repeats", type=int, default=5)
    p.add_argument("--warmup", type=int, default=1, help="Separately reported trials per mode")
    p.add_argument("--timeout", type=float, default=120)
    p.add_argument("--output", type=Path, required=True)
    args = p.parse_args()
    try:
        if args.output.resolve() in (args.agent_payload.resolve(), args.classifier_payload.resolve()):
            raise ValueError("Output must not overwrite an input request")
        contract = load_gate_contract()
        endpoints = {role: endpoint_url(getattr(args, role + "_endpoint"), role) for role in ("agent", "classifier")}
        sources = {role: load_payload(getattr(args, role + "_payload"), role, contract) for role in endpoints}
        report = benchmark(endpoints, sources, contract, args.repeats, args.warmup, args.timeout, args.output)
    except (ValueError, OSError) as exc:
        p.error(str(exc))
    return int(not report["passed"])


if __name__ == "__main__":
    raise SystemExit(main())
