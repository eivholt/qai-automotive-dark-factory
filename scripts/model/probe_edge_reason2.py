#!/usr/bin/env python3
"""Offline device probes: real images/logits, never live gate permission or freshness."""
import argparse
import base64
from collections import Counter
import hashlib
import io
import json
from pathlib import Path
import statistics
import sys
import time
from urllib.parse import urlparse
from urllib.request import Request, urlopen

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "src"))
from dark_factory.gates import load_gate_contract, parse_gate_request, camera_content
from dark_factory.gates_scores import topk_label_bounds, decision_from_bounds
from record_edge_provenance import validate
from dark_factory.gates_single_view import VERSION as SINGLE_VIEW_VERSION, CAMERAS, prompt_for, prompt_hashes, aggregate


def image_content(request, contract, *, image_index=None, prompt=None):
    from PIL import Image, ImageChops
    for png in request.pngs:
        with Image.open(io.BytesIO(png)) as im:
            if "A" in im.getbands() and im.getchannel("A").getextrema() != (255, 255):
                raise ValueError("Nonopaque sensor image")
            r, g, b = im.convert("RGB").split()
            if ImageChops.difference(r, g).getbbox() or ImageChops.difference(r, b).getbbox():
                raise ValueError("Only raw grayscale sensor PNGs are allowed")
    selected = request.pngs if image_index is None else [request.pngs[image_index]]
    images = [{"type": "image_url", "image_url": {"url": "data:image/png;base64," + base64.b64encode(png).decode()}}
              for png in selected]
    return camera_content(images, prompt if prompt is not None else contract["stages"][request.identity["stage"]]["prompt"])


def probe(endpoint, provenance, request, contract, timeout, *, image_index=None):
    alias = provenance["model_alias"]
    with urlopen(endpoint + "/models", timeout=5) as response:
        if [row["id"] for row in json.load(response)["data"]] != [alias]:
            raise ValueError("Backend model identity mismatch")
    single_prompt = prompt_for(request.identity["stage"]) if image_index is not None else None
    payload = {"model": alias, "messages": [{"role": "user", "content": image_content(request, contract, image_index=image_index, prompt=single_prompt)}],
               "max_tokens": 1, "temperature": 0, "top_k": 1, "seed": 42,
               "logprobs": True, "top_logprobs": 20, "cache_prompt": False, "stream": False}
    started = time.perf_counter()
    with urlopen(Request(endpoint + "/chat/completions", json.dumps(payload).encode(),
                         {"Content-Type": "application/json"}), timeout=timeout) as response:
        raw = json.load(response)
    latency = (time.perf_counter() - started) * 1000
    if raw.get("model") != alias:
        raise ValueError("Response model identity mismatch")
    bounds = topk_label_bounds(raw["choices"][0]["logprobs"]["content"][0]["top_logprobs"])
    decision, reason = decision_from_bounds(bounds, contract["decision_policy"])
    return {"decision": decision, "reason": reason, "latency_ms": latency, "bounds": bounds,
            "backend_alias": alias, "backend_timings": raw.get("timings"), "usage": raw.get("usage"),
            "image_sha256": [hashlib.sha256(png).hexdigest() for png in (request.pngs if image_index is None else [request.pngs[image_index]])],
            "exceeds_reply_age_budget": latency > 1000 * contract["decision_policy"]["max_reply_age_seconds"]}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    group = p.add_mutually_exclusive_group(required=True)
    group.add_argument("--request-dir", type=Path)
    group.add_argument("--manifest", type=Path)
    p.add_argument("--primary-endpoint", default="http://127.0.0.1:18196/v1")
    p.add_argument("--weld-endpoint", default="http://127.0.0.1:18197/v1")
    p.add_argument("--primary-provenance", type=Path, required=True)
    p.add_argument("--weld-provenance", type=Path, required=True)
    p.add_argument("--stage", choices=("panel_presence", "adhesive", "panel_seated", "weld"))
    p.add_argument("--limit", type=int, default=0)
    p.add_argument("--single-view", action="store_true", help="Offline only: three independent one-image/one-token calls with new prompts and explicit aggregation")
    p.add_argument("--stop-on-error", action="store_true", help="Stop a hardware probe at the first failed batch")
    p.add_argument("--timeout", type=float, default=120)
    p.add_argument("--device-description", required=True, help="Actual device/runtime, not an inferred acceleration claim")
    p.add_argument("--output", type=Path, required=True)
    args = p.parse_args()
    for endpoint in (args.primary_endpoint, args.weld_endpoint):
        url = urlparse(endpoint)
        if url.scheme != "http" or url.hostname not in ("127.0.0.1", "localhost", "::1") or url.username or url.password:
            p.error("Use loopback or a loopback SSH tunnel")
    contract = load_gate_contract()
    primary = validate(json.loads(args.primary_provenance.read_text()), contract)
    weld = validate(json.loads(args.weld_provenance.read_text()), contract)
    samples = []
    if args.request_dir:
        for path in sorted(args.request_dir.glob("qa_latest_*_request.json")):
            payload = json.loads(path.read_text(encoding="utf-8-sig"))
            samples.append((path.name, payload, None))
    else:
        for line in args.manifest.read_text().splitlines():
            row = json.loads(line)
            if row["split"] != "validation":
                continue
            stage = row["stage"]
            if row["contract_version"] != contract["version"] or row["prompt_sha256"] != contract["stages"][stage]["prompt_sha256"]:
                raise ValueError("Manifest contract/prompt mismatch")
            payload = {"contract_version": contract["version"], "request_id": row["sample_id"],
                       "body_id": str(row["body_id"]), "stage": stage, "captured_at_ms": 0,
                       "images": [{"camera_id": im["camera_id"], "png_base64": base64.b64encode((args.manifest.parent / im["path"]).read_bytes()).decode()}
                                  for im in row["images"]]}
            samples.append((row["sample_id"], payload, row["expected_decision"]))
    if args.stage:
        samples = [s for s in samples if s[1]["stage"] == args.stage]
    if args.limit:
        samples = samples[:args.limit]
    if not samples:
        p.error("No matching image batches")
    report = {"scope": "offline-real-Reason2-device-probe-no-live-permits-v1", "device": args.device_description,
              "contract": contract["version"], "decision_policy": contract["decision_policy"], "rows": []}
    if args.single_view:
        report.update(experiment=SINGLE_VIEW_VERSION, adapter_single_view_validated=False,
                      experiment_prompt_hashes=prompt_hashes(),
                      source_prompt_hashes={k: v["prompt_sha256"] for k, v in contract["stages"].items()},
                      label_scope="Original whole-batch labels only; individual views have no inferred ground truth")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    for name, payload, expected in samples:
        request = parse_gate_request(payload, contract)
        is_weld = payload["stage"] == "weld"
        row = {"sample": name, "stage": payload["stage"], "expected": expected}
        try:
            endpoint, provenance = (args.weld_endpoint, weld) if is_weld else (args.primary_endpoint, primary)
            if args.single_view:
                started = time.perf_counter()
                row["views"] = []
                for index, camera in enumerate(CAMERAS):
                    view = {"camera_id": camera}
                    try:
                        view.update(probe(endpoint, provenance, request, contract, args.timeout, image_index=index))
                    except Exception as exc:
                        view.update(decision="A", error=f"{type(exc).__name__}: {exc}")
                    row["views"].append(view)
                    if "error" in view:
                        row["error"] = view["error"]
                        break
                decision, reason = aggregate(payload["stage"], row["views"])
                elapsed = (time.perf_counter() - started) * 1000
                row.update(decision=decision, reason=reason, latency_ms=elapsed,
                           exceeds_reply_age_budget=elapsed > 1000 * contract["decision_policy"]["max_reply_age_seconds"])
            else:
                row.update(probe(endpoint, provenance, request, contract, args.timeout))
        except Exception as exc:
            row.update(decision="A", error=f"{type(exc).__name__}: {exc}")
        report["rows"].append(row)
        values = [r["latency_ms"] for r in report["rows"] if "latency_ms" in r]
        labelled = [r for r in report["rows"] if r["expected"]]
        report["summary"] = {"requests": len(report["rows"]), "errors": sum("error" in r for r in report["rows"]),
                             "latency_median_ms": statistics.median(values) if values else None,
                             "labelled": len(labelled), "correct": sum(r["decision"] == r["expected"] and "error" not in r for r in labelled),
                             "confusion": dict(Counter(r["expected"] + "->" + r["decision"] for r in labelled)),
                             "false_G": sum(r["decision"] == "G" and r["expected"] != "G" for r in labelled)}
        args.output.write_text(json.dumps(report, indent=2) + "\n")
        print(json.dumps({k: v for k, v in row.items() if k not in ("bounds", "image_sha256", "views")}), flush=True)
        if "error" in row and args.stop_on_error:
            break
    if report["summary"]["errors"]:
        sys.exit(1)


if __name__ == "__main__":
    main()
