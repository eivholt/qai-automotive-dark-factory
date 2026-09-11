#!/usr/bin/env python3
"""Sequential frozen-dataset gate; never treats mock replies as model evidence."""
from __future__ import annotations

import argparse
import base64
import hashlib
import json
import math
from pathlib import Path
import statistics
import sys
import time
from urllib.error import HTTPError
from urllib.request import Request, urlopen

LABELS = ("G", "A", "R")


def percentile(values: list[float], q: float) -> float | None:
    return sorted(values)[max(0, math.ceil(q * len(values)) - 1)] if values else None


def summarize(rows: list[dict], minimum_per_class: int, min_recall: float,
              max_false_green: int, max_p95_ms: float | None, mock: bool) -> dict:
    confusion = {label: {out: 0 for out in (*LABELS, "ERROR")} for label in LABELS}
    for row in rows:
        prediction = row["decision"] if not row.get("error") else "ERROR"
        confusion[row["expected"]][prediction] += 1
    counts = {label: sum(confusion[label].values()) for label in LABELS}
    recalls = {label: confusion[label][label] / counts[label] if counts[label] else 0
               for label in LABELS}
    false_green = sum(confusion[label]["G"] for label in ("A", "R"))
    errors = sum(confusion[label]["ERROR"] for label in LABELS)
    # First request is retained for accuracy but separated from warm latency.
    warm = [row["elapsed_ms"] for row in rows[1:] if not row.get("error")]
    warm_p95 = percentile(warm, .95)
    checks = {
        "minimum_per_class": all(n >= minimum_per_class for n in counts.values()),
        "per_class_recall": all(r >= min_recall for r in recalls.values()),
        "false_green": false_green <= max_false_green,
        "no_errors": errors == 0,
        "warm_latency": max_p95_ms is None or (
            warm_p95 is not None and warm_p95 <= max_p95_ms),
    }
    return {
        "evidence": "plumbing_only" if mock else "model_evaluation",
        "gate_passed": None if mock else all(checks.values()),
        "checks": checks,
        "samples": len(rows), "counts": counts, "confusion": confusion,
        "recall": recalls, "false_green": false_green, "errors": errors,
        "first_request_ms": rows[0]["elapsed_ms"] if rows else None,
        "warm_mean_ms": statistics.mean(warm) if warm else None,
        "warm_p50_ms": percentile(warm, .5), "warm_p95_ms": warm_p95,
        "warm_successful_requests": len(warm),
    }


def read_json(url: str, timeout: float, payload: dict | None = None) -> dict:
    request = Request(url, data=None if payload is None else json.dumps(payload).encode(),
                      headers={"Content-Type": "application/json"})
    with urlopen(request, timeout=timeout) as response:
        content = response.read(65537)
        if len(content) > 65536:
            raise ValueError("Bridge response exceeds 64 KiB.")
        value = json.loads(content)
    if not isinstance(value, dict):
        raise ValueError("Bridge response must be a JSON object.")
    return value


def health_identity(health: dict, contract: dict) -> tuple:
    from dark_factory.protocol import prompt_sha256
    source = health.get("source", health.get("backend"))
    if health.get("status") != "ok" or source not in ("mock", "evk"):
        raise ValueError("Bridge health must be ok with source/backend mock or evk.")
    if "backend" in health and health["backend"] != source:
        raise ValueError("Bridge health has conflicting source/backend fields.")
    if "mock" in health and health["mock"] is not (source == "mock"):
        raise ValueError("Bridge health has a conflicting mock flag.")
    if (health.get("contract_version") != contract["version"]
            or health.get("prompt_sha256") != prompt_sha256(contract)):
        raise ValueError("Bridge prompt/contract differs from the dataset evaluator.")
    return (source, health.get("contract_version"), health.get("prompt_sha256"),
            health.get("model"), health.get("evk_url"))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--image-root", type=Path, required=True)
    parser.add_argument("--split", choices=("validation", "test"), default="test")
    parser.add_argument("--url", default="http://127.0.0.1:18190")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--timeout", type=float, default=15)
    parser.add_argument("--minimum-per-class", type=int, default=30)
    parser.add_argument("--min-recall", type=float, default=1)
    parser.add_argument("--max-false-green", type=int, default=0)
    parser.add_argument("--max-p95-ms", type=float)
    parser.add_argument("--allow-mock", action="store_true",
                        help="Plumbing test only; gate_passed is null, never accuracy evidence.")
    args = parser.parse_args()
    if (not math.isfinite(args.timeout) or args.timeout <= 0 or args.minimum_per_class < 1
            or not 0 <= args.min_recall <= 1 or args.max_false_green < 0
            or (args.max_p95_ms is not None and (not math.isfinite(args.max_p95_ms) or args.max_p95_ms <= 0))):
        parser.error("Invalid timeout or gate thresholds.")
    from dark_factory.protocol import load_contract
    contract = load_contract()
    try:
        health = read_json(args.url.rstrip("/") + "/health", args.timeout)
        identity = health_identity(health, contract)
    except (HTTPError, OSError, ValueError) as exc:
        parser.error(str(exc))
    source = identity[0]
    mock = source == "mock"
    if mock and not args.allow_mock:
        parser.error("Mock bridge cannot produce model evaluation; use EVK or --allow-mock.")
    manifest_bytes = args.manifest.read_bytes()
    samples = [json.loads(line) for line in manifest_bytes.decode("utf-8-sig").splitlines()
               if line.strip()]
    # Validate the whole manifest before selecting a split: episode leaks elsewhere matter.
    from dark_factory.dataset import validate_manifest
    validate_manifest(samples, root=args.image_root, require_images=True)
    selected = [row for row in samples if row["split"] == args.split]
    if not selected:
        parser.error("The requested split contains no images.")
    image_root = args.image_root.resolve()
    results = []
    for index, sample in enumerate(selected):
        image = (image_root / sample["image"]).resolve()
        if not image.is_relative_to(image_root):
            parser.error("Image path escapes image root.")
        content = image.read_bytes()
        request_id = f"gate-{index:06d}"
        episode_id = sample["episode_id"]
        payload = {"request_id": request_id, "episode_id": episode_id,
                   "image_base64": base64.b64encode(content).decode("ascii")}
        start = time.perf_counter()
        error = None
        decision = "A"
        try:
            reply = read_json(args.url.rstrip("/") + "/inspect", args.timeout, payload)
            if reply.get("request_id") != request_id or reply.get("episode_id") != episode_id:
                raise ValueError("Response identity mismatch.")
            if reply.get("source") != source:
                raise ValueError("Backend source changed during gate.")
            if "error" in reply or reply.get("decision") not in LABELS:
                raise ValueError(reply.get("error") or "Invalid decision.")
            decision = reply["decision"]
        except (HTTPError, OSError, ValueError, KeyError) as exc:
            error = str(exc)
        elapsed_ms = 1000 * (time.perf_counter() - start)
        results.append({"sample_id": sample["sample_id"], "episode_id": episode_id,
                        "image_sha256": hashlib.sha256(content).hexdigest(),
                        "expected": sample["label"], "decision": decision,
                        "error": error, "elapsed_ms": elapsed_ms})
    final_health = None
    identity_error = None
    try:
        final_health = read_json(args.url.rstrip("/") + "/health", args.timeout)
        if health_identity(final_health, contract) != identity:
            raise ValueError("Bridge identity changed during evaluation.")
    except (HTTPError, OSError, ValueError) as exc:
        identity_error = str(exc)
        # A restart or reconfiguration makes every reply's model/contract
        # attribution uncertain. Preserve predictions but count them as errors.
        for row in results:
            row["error"] = row["error"] or identity_error
    report = summarize(results, args.minimum_per_class, args.min_recall,
                       args.max_false_green, args.max_p95_ms, mock)
    report["checks"]["stable_bridge_identity"] = identity_error is None
    if identity_error is not None and not mock:
        report["gate_passed"] = False
    report.update({"bridge_health": health, "bridge_health_final": final_health,
                   "bridge_identity_error": identity_error, "split": args.split,
                   "manifest_sha256": hashlib.sha256(manifest_bytes).hexdigest(),
                   "thresholds": {"minimum_per_class": args.minimum_per_class,
                                  "min_recall": args.min_recall,
                                  "max_false_green": args.max_false_green,
                                  "max_p95_ms": args.max_p95_ms},
                   "rows": results})
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({k: v for k, v in report.items() if k != "rows"}, indent=2))
    return 0 if mock or report["gate_passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
