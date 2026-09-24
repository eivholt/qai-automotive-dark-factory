"""Verify actual streamed uploads overlap rendered R4 scans and remain bounded."""
import argparse
from collections import defaultdict
import json
from pathlib import Path
import statistics


def verify(report, lifecycle=False):
    errors, groups, completed = [], defaultdict(list), []

    def check(value, message):
        if not value:
            errors.append(message)

    stream = report["streaming"]
    check(report["passed"] and report["completed_bodies"] == 3, "actual three-body process did not pass")
    check(stream["available"], "stream capability unavailable")
    check(len(stream["events"]) <= 256, "unbounded streaming telemetry")
    for event in stream["events"]:
        groups[event["request_id"]].append(event)
    captures = defaultdict(list)
    for event in report["inspection_captures"]:
        captures[event["batch_serial"]].append(event)
    for request, events in groups.items():
        done = [e for e in events if e["event"] == "complete"]
        cancelled = any(e["event"] == "cancel" for e in events)
        check(not (done and cancelled), f"{request}: completed canceled epoch")
        if not done:
            continue
        check(len(done) == 1 and done[0]["detail"]["valid"], f"{request}: invalid or repeated final result")
        prepares = [e for e in events if e["event"] == "prepared"]
        sent = [e for e in events if e["event"] == "prepare_sent"]
        submit = [e for e in events if e["event"] == "submit"]
        check([e["detail"]["index"] for e in sent] == [0, 1, 2], f"{request}: uploads not three ordered images")
        check([e["detail"]["index"] for e in prepares] == [0, 1, 2], f"{request}: preparation acknowledgments incomplete")
        check(len(submit) == 1, f"{request}: repeated or missing joint submission")
        if len(prepares) != 3 or len(sent) != 3 or len(submit) != 1:
            continue
        check(prepares[-1]["batch_age_ms"] <= submit[0]["batch_age_ms"] <= done[0]["batch_age_ms"] < 8000,
              f"{request}: premature/stale final result")
        views = captures[done[0]["epoch"]]
        check([v["spot"] for v in views] == [0, 1, 2], f"{request}: actual rendered views incomplete")
        if len(views) == 3:
            check(prepares[0]["batch_age_ms"] < views[1]["batch_age_seconds"]*1000, f"{request}: first encoder did not overlap scan")
            check(prepares[1]["batch_age_ms"] < views[2]["batch_age_seconds"]*1000, f"{request}: early prefill did not overlap scan")
        for e in events:
            check(all(e[k] == done[0][k] for k in ("session_id", "epoch", "body_id", "stage", "captured_at_ms", "prompt_sha256")),
                  f"{request}: preparation identity changed")
        detail = done[0]["detail"]
        completed.append(dict(stage=done[0]["stage"], final_http_ms=detail["final_http_ms"],
            capture_to_reply_ms=detail["capture_to_reply_ms"],
            first_png_to_reply_ms=done[0]["batch_age_ms"]-sent[0]["batch_age_ms"]))
    check(len(completed) == stream["completed_batches"] == report["model_http_requests"], "not every actual gate used streaming")
    if lifecycle:
        check(stream["qa_step"] == 4 and stream["cancelled_batches"] == 2, "pause/backend lifecycle incomplete")
        check(report["backend_switch"]["switches"] == 2, "backend switches not exercised")
        names = [e["event"] for e in stream["events"]]
        check(names.count("qa_pause_after_first_image") == names.count("qa_switch_after_early_prefill") == 1,
              "missing actual cancellation injection")
    summary = {}
    for stage in ("panel_presence", "adhesive", "panel_seated", "weld"):
        rows = [r for r in completed if r["stage"] == stage]
        summary[stage] = dict(batches=len(rows), **{k: statistics.median(r[k] for r in rows)
                            for k in ("final_http_ms", "capture_to_reply_ms", "first_png_to_reply_ms")} if rows else {})
    return dict(passed=not errors, errors=errors, completed_batches=len(completed), by_stage=summary,
                scope="Actual rendered transport/motion and process integration, not held-out model accuracy")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime", type=Path, required=True)
    parser.add_argument("--lifecycle", action="store_true")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    result = verify(json.loads(args.runtime.read_text(encoding="utf-8-sig")), args.lifecycle)
    args.output.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result, indent=2))
    raise SystemExit(int(not result["passed"]))
