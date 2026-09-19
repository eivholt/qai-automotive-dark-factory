"""Offline agent/tool experiment: real controller LLM, synthetic inspection replies.

No Unreal transport, camera access or physical actions. Fixture names and future
replies never enter the model conversation. This is NOT classifier validation.
"""
from __future__ import annotations

import argparse
from collections import defaultdict
from copy import deepcopy
import hashlib
import json
from pathlib import Path
import statistics
import sys
import time
from urllib.error import HTTPError, URLError
from urllib.request import Request, urlopen
from urllib.parse import urlparse

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "src"))
from dark_factory.gates import load_gate_contract

STAGES = ("panel_presence", "adhesive", "panel_seated", "weld")
ACTIONS = ("apply_sealant", "place_panel", "spot_weld", "release_panel", "depart")
SYSTEM = """You operate an automotive panel assembly cell through tools.
Complete one body or explicitly hold if inspection cannot authorize work.
Commands are complete jobs: the executor owns all arm poses, gripping, camera
scanning, tool opening/closing and weld-current timing. Never plan joint motions.
Reason2 is the inspection tool; do not invent its results or infer hidden state.
Each inspect tool obtains one fresh three-camera Reason2 batch and validates it.
One valid batch grants a permit: presence G means already installed; presence R
means missing. All other gates require one fresh G batch. Thermal R means wait,
then inspect again. Never infer permission from a hot result or from elapsed time.
A, stale, invalid or unavailable evidence requires hold. Seating R requires hold.
The state flags are authoritative observations: exception=true means call hold;
cooling_wait_required=true means wait, then reinspect the same thermal stage.
Repeated hot R results NEVER authorize welding or release, regardless of count.
For missing: inspect presence, apply sealant, place and hold panel, inspect adhesive
coolness, spot weld, inspect panel_seated, inspect weld coolness, release, depart.
The place_panel action already engages and maintains the gripper. There is no
mechanical action named hold. The hold TOOL ends processing for an exception.
For preassembled: inspect presence, inspect adhesive coolness, inspect weld coolness,
depart. Skip assembly, welding, seating inspection and release for preassembled.
The executor validates evidence and motion prerequisites; it does not choose tools
for you. Never treat a rejected action as completed. Never use waiting to override
an inspection. Use one tool per turn, with no explanatory prose. Keep retrying hot
thermal inspections after wait; explicitly hold on uncertainty or unavailable tools.
"""


def tool(name, description, field=None, values=None):
    properties = {} if field is None else {field: {"type": "string", "enum": list(values)}}
    return {"type": "function", "function": {"name": name, "description": description,
        "parameters": {"type": "object", "properties": properties,
                       "required": list(properties), "additionalProperties": False}}}


TOOLS = [tool("inspect", "Collect one fresh three-camera Reason2 batch and validate it for this body.", "stage", STAGES),
         tool("act", "Execute a mechanical command. place_panel maintains the grip until release_panel.", "action", ACTIONS),
         tool("wait", "Wait for cooling; this never grants permission."),
         tool("hold", "Stop this body and request review; no departure.")]


def fixtures():
    normal = {"panel_presence": ["R"], "adhesive": ["G"],
              "panel_seated": ["G"], "weld": ["G"]}
    specs = [
        ("missing", {}, "departed"),
        ("preassembled", {"panel_presence": ["G"]}, "departed"),
        ("thermal_waits", {"adhesive": ["R", "R", "G"], "weld": ["R", "G"]}, "departed"),
        ("presence_first_batch_commits", {"panel_presence": ["R", "G"]}, "departed"),
        ("seating_exception", {"panel_seated": ["R"]}, "held"),
        ("ambiguous", {"adhesive": ["A"]}, "held"),
        ("unavailable", {"panel_presence": ["unavailable"]}, "held"),
        ("stale", {"panel_presence": ["stale"]}, "held"),
    ]
    return [(name, {**deepcopy(normal), **streams}, expected) for name, streams, expected in specs]


class ReplayCell:
    """Interlocked mock executor; no next-action policy or live actuator channel."""
    stage_names = STAGES
    presence_stages = ("panel_presence",)
    thermal_stages = ("adhesive", "weld")
    seating_stages = ("panel_seated",)
    def __init__(self, streams):
        self.streams = deepcopy(streams)
        self.indices = defaultdict(int)
        self.contract = load_gate_contract()
        self.epoch = 1
        self.now = 0.0
        self.seen = set()
        self.votes = {}
        self.permits = {}
        self.branch = None
        self.completed = []
        self.terminal = None
        self.exception = False
        self.wait_required = False

    def status(self):
        return {"body_id": "body-current", "epoch": self.epoch, "branch": self.branch,
                "completed_actions": self.completed[:], "permits": dict(self.permits),
                "exception": self.exception, "cooling_wait_required": self.wait_required,
                "terminal": self.terminal}

    def accept(self, stage, reply):
        """Accept only distinct, in-scope, fresh replies; clear permits on failure."""
        identity = (reply["epoch"], reply["request_id"])
        age = self.now - reply["captured_at"]
        if (reply["epoch"] != self.epoch or reply["body_id"] != "body-current"
                or reply["stage"] != stage or identity in self.seen
                or not 0 <= age <= self.contract["decision_policy"]["max_reply_age_seconds"]
                or not reply["valid"] or reply["decision"] not in ("G", "R")):
            self.votes.pop(stage, None)
            self.permits.pop(stage, None)
            self.exception = True
            return {"accepted": False, "hold_required": True}
        self.seen.add(identity)
        label = reply["decision"]
        last, count = self.votes.get(stage, (None, 0))
        count = count + 1 if last == label else 1
        self.votes[stage] = (label, count)
        self.permits.pop(stage, None)
        if stage in self.seating_stages and label == "R":
            self.exception = True
        if stage in self.thermal_stages and label == "R":
            self.wait_required = True
        if count >= 1 and (label == "G" or stage in self.presence_stages):
            self.permits[stage] = label
            if stage == "panel_presence":
                self.branch = "preassembled" if label == "G" else "missing"
        return {"accepted": True, "matching_batches": count, "hold_required": self.exception}

    def inspect(self, stage):
        if stage not in self.stage_names:
            raise ValueError("Unknown inspection stage")
        if self.exception:
            raise ValueError("Inspection exception requires hold")
        if self.wait_required:
            raise ValueError("Wait before another thermal inspection")
        if stage == "panel_presence":
            if self.branch is not None:
                raise ValueError("Presence branch is already committed for this body")
        elif stage == "adhesive":
            if not self.branch or (self.branch == "missing" and "place_panel" not in self.completed):
                raise ValueError("Adhesive inspection requires the panel in position")
        elif stage == "panel_seated":
            if self.branch != "missing" or "spot_weld" not in self.completed:
                raise ValueError("Seating inspection requires a newly welded panel")
        elif stage == "weld":
            if self.branch == "preassembled":
                ready = self.permits.get("adhesive") == "G"
            else:
                ready = "spot_weld" in self.completed and self.permits.get("panel_seated") == "G"
            if not ready:
                raise ValueError("Weld cooling inspection prerequisites missing")
        index = self.indices[stage]
        sequence = self.streams[stage]
        value = sequence[min(index, len(sequence) - 1)]
        self.indices[stage] += 1
        self.now += 1
        reply = {"body_id": "body-current", "epoch": self.epoch, "stage": stage,
                 "request_id": f"batch-{sum(self.indices.values())}", "captured_at": self.now,
                 "decision": value if value in ("G", "A", "R") else "A", "valid": True}
        if value == "stale":
            reply.update(decision="G", captured_at=self.now - 61)
        elif value == "unavailable":
            reply["valid"] = False
        return {"inspection": reply, "evidence": self.accept(stage, reply), "state": self.status()}

    def act(self, action):
        if action not in ACTIONS:
            raise ValueError("Unknown action")
        if self.exception or self.wait_required:
            raise ValueError("Inspection hold cannot be overridden")
        # Repeated acknowledged commands are idempotent in this bound body/epoch.
        if action in self.completed:
            return {"already_completed": action, "state": self.status()}
        missing = self.branch == "missing"
        seal_ready = self.permits.get("adhesive") == "G"
        weld_ready = self.permits.get("weld") == "G"
        seated = self.permits.get("panel_seated") == "G"
        allowed = {
            "apply_sealant": missing and self.permits.get("panel_presence") == "R",
            "place_panel": missing and "apply_sealant" in self.completed,
            "spot_weld": missing and "place_panel" in self.completed and seal_ready,
            "release_panel": missing and "spot_weld" in self.completed and seated and weld_ready,
            "depart": seal_ready and weld_ready and (self.branch == "preassembled" or
                       (missing and seated and "release_panel" in self.completed)),
        }
        if not allowed[action]:
            raise ValueError("Action prerequisites/evidence missing")
        self.completed.append(action)
        if action == "depart":
            self.terminal = "departed"
        return {"completed": action, "state": self.status()}

    def call(self, name, arguments):
        expected = {"inspect": {"stage"}, "act": {"action"}, "wait": set(), "hold": set()}
        if name not in expected or not isinstance(arguments, dict) or set(arguments) != expected[name]:
            raise ValueError("Unknown tool or malformed arguments")
        if self.terminal:
            raise ValueError("Body is already terminal")
        if name == "inspect":
            batches = []
            for _ in range(1):
                observed = self.inspect(arguments["stage"])
                # The tool validates full identity/freshness internally. Avoid
                # duplicating the whole state for every batch in LLM context.
                batches.append({"request_id": observed["inspection"]["request_id"],
                                "decision": observed["inspection"]["decision"] if observed["evidence"]["accepted"] else None,
                                **observed["evidence"]})
                if self.stop_inspection() or arguments["stage"] in self.permits:
                    break
            return {"batches": batches, "state": self.status()}
        if name == "act":
            return self.act(arguments["action"])
        if name == "hold":
            self.terminal = "held"
        else:
            self.now += 1
            self.wait_required = False
        return self.status()

    def stop_inspection(self):
        return self.exception or self.wait_required


EXPANDED_STAGES = STAGES + ("frame_presence", "frame_seated", "frame_weld", "sealant_coverage")
EXPANDED_ACTIONS = ACTIONS + ("place_frame", "weld_frame", "release_frame", "top_up_sealant")
EXPANDED_SYSTEM = SYSTEM + """
Additional process variant: first inspect panel_presence, as usual. If preassembled,
skip frame/coverage inspections: the opaque panel hides those surfaces. If missing,
inspect frame_presence before applying sealant. G means the open mounting frame is installed;
R means it is missing. On R: place_frame (already grips), weld_frame with NO sealant,
inspect frame_seated, inspect frame_weld cooling, release_frame. Then continue
the missing-panel sequence. On frame presence G skip frame repair.
Frame seating R requires hold; frame thermal R requires wait then fresh inspection.
For a missing panel: after apply_sealant, inspect sealant_coverage BEFORE place_panel.
G means the whole three-edge bead is continuous. R means a gap: top_up_sealant over
the SAME complete three-edge path, then inspect again. There are at most two top-ups.
Coverage A/errors require hold. Further gaps after both top-ups require hold.
No region localization is needed. Do not place a panel over unapproved sealant.
After a top-up, inspect coverage again. Temperature readiness remains a separate
adhesive inspection after the panel is placed; continuous does not mean cool.
For preassembled, the COMPLETE path remains: inspect panel_presence, inspect
adhesive, inspect weld, depart. Skipping frame/coverage does NOT skip thermal gates.
After installing a missing frame, frame_weld approval MUST be followed by
act(release_frame) before apply_sealant. A cool frame can still be held by the tool.
"""
EXPANDED_TOOLS = [tool("inspect", "Validate one fresh three-camera batch for the requested inspection.", "stage", EXPANDED_STAGES),
                  tool("act", "Execute a command. Placement maintains gripping until release.", "action", EXPANDED_ACTIONS),
                  *TOOLS[2:]]


class ExpandedReplayCell(ReplayCell):
    """Proposed frame/rework study only; these new stages have NO trained adapter."""
    stage_names = EXPANDED_STAGES
    presence_stages = ("panel_presence", "frame_presence")
    thermal_stages = ("adhesive", "weld", "frame_weld")
    seating_stages = ("panel_seated", "frame_seated")

    def __init__(self, streams):
        super().__init__(streams)
        self.frame_branch = None
        self.rework_required = False
        self.top_ups = 0

    def status(self):
        return {**super().status(), "frame_branch": self.frame_branch,
                "sealant_rework_required": self.rework_required, "top_ups_completed": self.top_ups}

    def frame_ready(self):
        return self.frame_branch == "installed" or "release_frame" in self.completed

    def accept(self, stage, reply):
        result = super().accept(stage, reply)
        if stage == "frame_presence" and stage in self.permits:
            self.frame_branch = "installed" if self.permits[stage] == "G" else "missing"
        if stage == "sealant_coverage" and result["accepted"] and reply["decision"] == "R":
            self.rework_required = True
            if self.top_ups >= 2:
                self.exception = True
                result["hold_required"] = True
        return result

    def stop_inspection(self):
        return super().stop_inspection() or self.rework_required

    def inspect(self, stage):
        if stage == "frame_presence" and self.frame_branch:
            raise ValueError("Frame presence branch already committed")
        if stage == "frame_presence" and self.branch != "missing":
            raise ValueError("Inspect panel presence first; installed panels occlude the frame")
        if stage in ("frame_seated", "frame_weld") and "weld_frame" not in self.completed:
            raise ValueError("Frame must have been installed and welded")
        if stage == "frame_weld" and self.permits.get("frame_seated") != "G":
            raise ValueError("Verify frame seating before frame cooling")
        if stage == "sealant_coverage":
            if "apply_sealant" not in self.completed or "place_panel" in self.completed:
                raise ValueError("Inspect exposed applied sealant before placing a panel")
            if self.rework_required:
                raise ValueError("Observed gap requires a top-up before reinspection")
        return super().inspect(stage)

    def act(self, action):
        if self.exception or self.wait_required:
            raise ValueError("Inspection hold cannot be overridden")
        if action == "apply_sealant" and not self.frame_ready():
            raise ValueError("A verified mounting frame is required before sealant")
        if action == "top_up_sealant":
            if not self.rework_required:
                raise ValueError("No observed gap authorizes another sealant pass")
            if self.top_ups >= 2 or "place_panel" in self.completed:
                raise ValueError("Sealant rework limit/visibility interlock")
            self.top_ups += 1
            self.rework_required = False
            self.votes.pop("sealant_coverage", None)
            self.permits.pop("sealant_coverage", None)
            self.completed.append(f"top_up_sealant_{self.top_ups}")
            return self.status()
        if action in ("place_frame", "weld_frame", "release_frame"):
            if action in self.completed:
                return {"already_completed": action, "state": self.status()}
            allowed = {
                "place_frame": self.frame_branch == "missing",
                "weld_frame": "place_frame" in self.completed,
                "release_frame": "weld_frame" in self.completed and self.permits.get("frame_seated") == "G"
                                 and self.permits.get("frame_weld") == "G",
            }
            if not allowed[action]:
                raise ValueError("Frame installation/verification prerequisites missing")
            self.completed.append(action)
            return self.status()
        if action == "place_panel" and self.permits.get("sealant_coverage") != "G":
            raise ValueError("Sealant continuity must be accepted before placement")
        return super().act(action)


def expanded_fixtures():
    normal = fixtures()[0][1]
    normal.update(frame_presence=["G"], frame_seated=["G"],
                  frame_weld=["G"], sealant_coverage=["G"])
    specs = [
        ("frame_present_panel_missing", {}, "departed"),
        ("frame_missing", {"frame_presence": ["R"]}, "departed"),
        ("preassembled", {"panel_presence": ["G"]}, "departed"),
        ("sealant_gap", {"sealant_coverage": ["R", "G"]}, "departed"),
        ("persistent_gap", {"sealant_coverage": ["R", "R", "R"]}, "held"),
        ("combined_recovery", {"frame_presence": ["R"], "frame_weld": ["R", "G"],
                               "sealant_coverage": ["R", "G"], "adhesive": ["R", "G"]}, "departed"),
        ("uncertain_frame", {"frame_presence": ["A"]}, "held"),
        ("coverage_unavailable", {"sealant_coverage": ["unavailable"]}, "held"),
    ]
    return [(name, {**deepcopy(normal), **streams}, expected) for name, streams, expected in specs]


def run_case(endpoint, model, spec, max_turns, expanded=False, timeout=90):
    name, streams, expected = spec
    cell = (ExpandedReplayCell if expanded else ReplayCell)(streams)
    messages = [{"role": "system", "content": EXPANDED_SYSTEM if expanded else SYSTEM},
                {"role": "user", "content": "A body has arrived. Inspect and process it. " + json.dumps(cell.status())}]
    trace, rejected, failure = [], 0, None
    for _ in range(max_turns):
        payload = {"model": model, "messages": messages, "tools": EXPANDED_TOOLS if expanded else TOOLS,
                   "parallel_tool_calls": False, "tool_choice": "required", "temperature": 0, "seed": 42,
                   "max_tokens": 256, "chat_template_kwargs": {"enable_thinking": False}}
        started = time.perf_counter()
        req = Request(endpoint + "/chat/completions", data=json.dumps(payload).encode(),
                      headers={"Content-Type": "application/json"})
        try:
            with urlopen(req, timeout=timeout) as response:
                result = json.load(response)
        except (HTTPError, URLError, TimeoutError, ConnectionError) as exc:
            failure = f"Controller transport failed: {exc}"
            if isinstance(exc, HTTPError):
                failure += ": " + exc.read(2048).decode(errors="replace")
            break
        elapsed = (time.perf_counter() - started) * 1000
        choice = result["choices"][0]
        msg = choice["message"]
        record = {"latency_ms": round(elapsed, 3), "message": msg, "usage": result.get("usage"),
                  "timings": result.get("timings"), "finish_reason": choice["finish_reason"]}
        trace.append(record)
        calls = msg.get("tool_calls", [])
        if len(calls) != 1 or choice["finish_reason"] == "length":
            failure = "Expected exactly one complete tool call"
            break
        messages.append({k: msg[k] for k in ("role", "content", "tool_calls") if k in msg})
        call = calls[0]
        try:
            observation = cell.call(call["function"]["name"], json.loads(call["function"]["arguments"]))
        except (ValueError, TypeError, KeyError) as exc:
            rejected += 1
            observation = {"rejected": str(exc), "state": cell.status()}
        record["observation"] = observation
        messages.append({"role": "tool", "tool_call_id": call["id"], "content": json.dumps(observation)})
        if cell.terminal:
            break
    latencies = sorted(row["latency_ms"] for row in trace)
    return {"case": name, "expected_terminal": expected, "terminal": cell.terminal,
            "passed": cell.terminal == expected and rejected == 0 and failure is None
                      and (expected != "held" or cell.exception),
            "rejected_tool_calls": rejected, "failure": failure, "turns": len(trace),
            "latency_median_ms": statistics.median(latencies) if latencies else None,
            "total_llm_ms": round(sum(latencies), 3),
            "completed_actions": cell.completed, "trace": trace}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--endpoint", default="http://127.0.0.1:18193/v1")
    parser.add_argument("--model", default="dark-factory-agent")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--max-turns", type=int, default=32)
    parser.add_argument("--repeat", type=int, default=1)
    parser.add_argument("--scenario-set", choices=("panel", "frame-sealant"), default="panel")
    parser.add_argument("--case", help="Run one named development case, e.g. missing or combined_recovery")
    parser.add_argument("--timeout", type=float, default=90, help="Per-controller HTTP timeout; never changes inspection freshness")
    args = parser.parse_args()
    url = urlparse(args.endpoint)
    if url.scheme != "http" or url.hostname not in ("localhost", "127.0.0.1", "::1") or url.username or url.password:
        parser.error("This experiment only supports a local HTTP model endpoint")
    if not 1 <= args.repeat <= 20 or not 1 <= args.max_turns <= 64:
        parser.error("repeat must be 1..20 and max-turns 1..64")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    expanded = args.scenario_set == "frame-sealant"
    report = {"scope": "real-LLM-synthetic-tools-no-Unreal-no-Reason2-inference-v1",
              "model": args.model, "endpoint": args.endpoint,
              "system_prompt": EXPANDED_SYSTEM if expanded else SYSTEM,
              "tools": EXPANDED_TOOLS if expanded else TOOLS,
              "script_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
              "scenario_set": args.scenario_set,
              "contract": "agent-frame-sealant-study-v1" if expanded else load_gate_contract()["version"], "cases": []}
    for repeat in range(args.repeat):
        specs = expanded_fixtures() if expanded else fixtures()
        if args.case:
            specs = [spec for spec in specs if spec[0] == args.case]
            if not specs:
                parser.error("Unknown case for this scenario set")
        for spec in specs:
            row = run_case(args.endpoint.rstrip("/"), args.model, spec, args.max_turns, expanded, args.timeout)
            row["repeat"] = repeat
            report["cases"].append(row)
            args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
            print(json.dumps({k: v for k, v in row.items() if k != "trace"}), flush=True)
    sys.exit(0 if all(row["passed"] for row in report["cases"]) else 1)


if __name__ == "__main__":
    main()
