"""Summarize completed, actual-model ProcessSmoke consensus experiments.

Repeated three-body development fixtures are integration/timing evidence, not
independent accuracy trials. Thermal observations are at reply/acceptance time,
not ground-truth labels for the three images captured at earlier times.
"""
from __future__ import annotations

import argparse
from collections import Counter, defaultdict
import json
from pathlib import Path
from statistics import mean, median, quantiles


def summarize(path: Path) -> tuple[dict, dict]:
    report = json.loads(path.read_text(encoding="utf-8-sig"))
    assert report["source"] == "host_gpu" and report["model_smoke"] is True
    assert report["passed"] is True and report["completed_bodies"] == 3
    assert report["body_outcomes_correct"] is True and report["thermal_limits_respected"] is True
    count = report["execution_policy"]["required_consensus_batches"]
    assert count in (1, 2) and report["decision_policy"]["consecutive_green"] == 2
    assert report["ground_truth_fields_sent_to_model"] == report["legacy_bridge_requests"] == 0
    decisions = report["model_qa_decisions"]
    latency = sorted(row["latency_ms"] for row in decisions if row["latency_ms"] > 0)
    phases = [row for row in report["events"] if row["event"] in ("phase", "body_completed")]
    phase_seconds = defaultdict(float)
    for start, end in zip(phases, phases[1:]):
        if start["event"] == "phase":
            phase_seconds[start["phase"]] += end["clock_seconds"] - start["clock_seconds"]
    end_times = [row["clock_seconds"] for row in phases if row["event"] == "body_completed"]
    assert len(end_times) == 3
    starts = [0.0, *end_times[:-1]]
    thermal = report["thermal_gate_checks"]
    assert len(thermal) == 6
    groups = defaultdict(list)
    for row in decisions:
        groups[f'{row["body_sequence"]}/{row["stage"]}'].append(row["decision"] if row["valid"] else "ERROR")
    result = {
        "report": str(path.resolve()), "session_id": report["session_id"],
        "consensus_batches": count, "process_seconds": report["clock_seconds"],
        "body_seconds": [end - start for start, end in zip(starts, end_times)],
        "model_requests": report["model_http_requests"], "decision_observations": len(decisions),
        "decisions": dict(Counter(row["decision"] for row in decisions)),
        "invalid_decisions": sum(not row["valid"] for row in decisions),
        "http_median_ms": median(latency),
        "http_p95_ms": quantiles(latency, n=100, method="inclusive")[94] if len(latency) > 1 else latency[0],
        "phase_seconds": dict(phase_seconds), "gate_sequences": dict(groups),
        "thermal_acceptances": thermal,
        "acceptances_outside_clear_ready_band": sum(not row["clear_ready_at_acceptance"] for row in thermal),
    }
    signature = {key: report[key] for key in ("contract_version", "prompt_hashes", "view_recipe", "scoring_method", "decision_policy", "backend_alias")}
    return result, signature


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("reports", nargs="+", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.output.resolve() in [path.resolve() for path in args.reports]:
        parser.error("Output must not overwrite an input report")
    pairs = [summarize(path) for path in args.reports]
    assert all(signature == pairs[0][1] for _, signature in pairs), "Model/render contracts differ"
    runs = [row for row, _ in pairs]
    assert len({row["session_id"] for row in runs}) == len(runs), "Duplicate run"
    grouped = {}
    for count in (1, 2):
        rows = [row for row in runs if row["consensus_batches"] == count]
        assert rows, f"Missing {count}-batch runs"
        grouped[str(count)] = {
            "runs": len(rows), "bodies": 3 * len(rows),
            "mean_process_seconds": mean(row["process_seconds"] for row in rows),
            "mean_model_requests": mean(row["model_requests"] for row in rows),
            "invalid_decisions": sum(row["invalid_decisions"] for row in rows),
            "acceptances_outside_clear_ready_band": sum(row["acceptances_outside_clear_ready_band"] for row in rows),
        }
    seconds_saved = grouped["2"]["mean_process_seconds"] - grouped["1"]["mean_process_seconds"]
    result = {
        "scope": "Repeated three-body live integration; not independent model accuracy",
        "contract": pairs[0][1], "runs": runs, "grouped": grouped,
        "mean_seconds_saved": seconds_saved,
        "process_time_reduction_percent": 100 * seconds_saved / grouped["2"]["mean_process_seconds"],
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2, allow_nan=False) + "\n", encoding="utf-8")
    print(json.dumps({key: value for key, value in result.items() if key not in ("runs", "contract")}, indent=2))


if __name__ == "__main__":
    main()
