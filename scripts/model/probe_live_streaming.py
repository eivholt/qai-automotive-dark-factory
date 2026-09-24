#!/usr/bin/env python3
"""Exercise live streaming HTTP with a private, image-only three-view request."""
import argparse
import copy
import json
from pathlib import Path
import sys
import time
import urllib.error
import urllib.request
import uuid

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "src"))
from dark_factory.gates import load_gate_contract, parse_gate_request


def run(endpoint, original):
    contract = load_gate_contract()
    parse_gate_request(original, contract)
    root = endpoint.removesuffix("/inspect")
    session, checks, timing = uuid.uuid4().hex, [], []

    def call(path, payload=None, headers=None):
        request = urllib.request.Request(root + path, data=None if payload is None else json.dumps(payload).encode(),
            headers={"Content-Type": "application/json", **(headers or {})})
        started = time.perf_counter()
        try:
            response = urllib.request.urlopen(request, timeout=12)
        except urllib.error.HTTPError as error:
            response = error
        with response:
            result = json.load(response)
            timing.append(dict(path=path, status=response.status, ms=(time.perf_counter()-started)*1000))
            return response.status, result

    def batch(epoch):
        payload = copy.deepcopy(original)
        payload.update(request_id=uuid.uuid4().hex, captured_at_ms=time.perf_counter()*1000)
        envelope = {k: v for k, v in payload.items() if k != "images"}
        envelope.update(session_id=session, epoch=epoch, prompt_sha256=contract["stages"][payload["stage"]]["prompt_sha256"])
        return payload, envelope

    def prepare(payload, envelope, indices=range(3)):
        for index in indices:
            status, ack = call("/stream/prepare", dict(envelope, image=payload["images"][index]))
            assert status == 200 and ack.get("prepared") is True, ack
            assert "decision" not in ack and "valid" not in ack

    def finish(payload, envelope):
        return call("/inspect", payload, {"X-DF-Stream-Session": session, "X-DF-Stream-Epoch": str(envelope["epoch"])})

    def rejected(reply):
        status, result = reply
        assert status == 400 and result["valid"] is False and result["decision"] == "A", result

    status, capability = call("/stream/capabilities")
    assert status == 200 and capability["streaming_protocol"] == "ordered-three-view-prefill-v1"
    base, envelope = batch(1)
    status, baseline = call("/inspect", base)
    assert status == 200 and baseline["valid"] is True, baseline
    prepare(base, envelope)
    status, streamed = finish(base, envelope)
    assert status == 200 and streamed["valid"] is True, streamed
    for key in ("decision", "conditional_label_scores", "label_mass", "raw_top_token", "image_sha256", "prompt_tokens", "visual_tokens"):
        assert baseline[key] == streamed[key], key
    assert streamed["streaming"]["early_prefill_tokens"] == 128
    checks.append("unchanged three-view scores, pixels and single joint decision")
    rejected(finish(base, envelope))
    checks.append("completed epoch cannot replay")

    payload, envelope = batch(2)
    call("/stream/cancel", envelope)
    rejected(call("/stream/prepare", dict(envelope, image=payload["images"][0])))
    checks.append("cancel-before-prepare rejects delayed upload")

    payload, envelope = batch(3)
    prepare(payload, envelope, range(2))
    status, cancel = call("/stream/cancel", envelope)
    assert status == 200 and cancel["cancelled"]
    rejected(finish(payload, envelope))
    rejected(call("/stream/prepare", dict(envelope, image=payload["images"][2])))
    checks.append("cancel after early prefill rejects final and late third image")

    previous = envelope
    payload, envelope = batch(4)
    prepare(payload, envelope)
    call("/stream/cancel", previous)
    status, fresh = finish(payload, envelope)
    assert status == 200 and fresh["conditional_label_scores"] == baseline["conditional_label_scores"], fresh
    checks.append("late old cancellation preserves fresh batch")

    payload, envelope = batch(5)
    prepare(payload, envelope)
    wrong = copy.deepcopy(payload)
    wrong["images"][0]["png_base64"] = payload["images"][1]["png_base64"]
    rejected(finish(wrong, envelope))
    checks.append("changed final pixels hold")

    payload, envelope = batch(6)
    prepare(payload, envelope, range(1))
    rejected(finish(payload, envelope))
    checks.append("partial preparation cannot release gate")

    payload, envelope = batch(7)
    prepare(payload, envelope)
    rejected(call("/inspect", payload, {"X-DF-Stream-Epoch": "7"}))
    status, fresh = finish(payload, envelope)
    assert status == 200 and fresh["conditional_label_scores"] == baseline["conditional_label_scores"]
    checks.append("incomplete stream headers hold and fresh complete request recovers")
    return dict(passed=True, checks=checks, timing=timing, baseline=baseline, streamed=streamed,
                scope="HTTP lifecycle and development pixel parity, not held-out accuracy")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--endpoint", required=True)
    parser.add_argument("--request", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    result = run(args.endpoint, json.loads(args.request.read_text(encoding="utf-8-sig")))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(dict(passed=result["passed"], checks=result["checks"]), indent=2))
