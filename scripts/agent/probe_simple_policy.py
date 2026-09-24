"""Offline development probes for the eight-tool agent; never operates the scene.

Synthetic results test planning, not camera accuracy. Expected choices stay
outside model inputs. Keep failed choices in the report; never repair them.
"""
import argparse
import json
from pathlib import Path
import time

from single_token import Controller, SIMPLE_ACTIONS, tool_response


def inspection(stage, decision="G", valid=True):
    return dict(stage=stage, decision=decision, valid=valid,
                status="observed" if valid else "invalid")


def temperatures(adhesive="G", weld="G", valid=True):
    return dict(status="completed", results=[inspection("adhesive", adhesive, valid),
                                             inspection("weld", weld, valid)])


def history(events):
    rows = [dict(role="user", content=json.dumps(dict(body_id="offline-only",
        event="new_body_arrived_choose_tools_to_complete_the_task",
        arrival=dict(status="completed", holder_engaged=False, completed_clock_seconds=3.2))))]
    held = False
    for i, (code, result) in enumerate(events):
        message = tool_response(dict(code=code), {}, "offline", SIMPLE_ACTIONS)["choices"][0]["message"]
        message["tool_calls"][0]["id"] = f"offline-{i}"
        if code == "B" and result.get("status") == "completed":
            held = True
        rows.extend([message, dict(role="tool", tool_call_id=f"offline-{i}",
            content=json.dumps(dict(holder_engaged=held, completed_clock_seconds=10+i*10, **result)))])
    return rows


def cases():
    present = [("A", inspection("panel_presence"))]
    missing = [("A", inspection("panel_presence", "R"))]
    prepared = missing + [("B", dict(status="completed", completed_steps=["apply_sealant", "place_panel"]))]
    ready = prepared + [("C", temperatures())]
    welded = ready + [("D", dict(status="completed", completed_steps=["weld_panel"]))]
    seated = welded + [("E", inspection("panel_seated"))]
    yield "arrival", history([]), "A"
    yield "present_needs_both_temperatures", history(present), "C"
    yield "missing_needs_preparation", history(missing), "B"
    yield "prepared_needs_temperatures", history(prepared), "C"
    yield "prepared_cool_can_weld", history(ready), "D"
    yield "weld_needs_new_seating_and_temperatures", history(welded), "CE"
    yield "seated_needs_postweld_temperatures", history(seated), "C"
    yield "assembly_ready_to_finish", history(seated + [("C", temperatures())]), "F"
    yield "preassembled_ready_to_finish", history(present + [("C", temperatures())]), "F"
    yield "temperature_first_then_seating", history(welded + [("C", temperatures())]), "E"
    yield "reverse_order_ready", history(welded + [("C", temperatures()), ("E", inspection("panel_seated"))]), "F"
    for branch, base in (("present", present), ("prepared", prepared), ("seated", seated)):
        for stage in ("adhesive", "weld"):
            for decision, valid in (("R", True), ("A", True), ("G", False)):
                result = temperatures()
                row = result["results"][0 if stage == "adhesive" else 1]
                row.update(decision=decision, valid=valid, status="observed" if valid else "invalid")
                # Before welding, only sealant readiness authorizes welding.
                # Weld heat will be re-measured after the weld job anyway.
                allowed = "D" if branch == "prepared" and stage == "weld" else ("GH" if decision == "R" else "CGH")
                yield f"{branch}_{stage}_{decision}_{valid}", history(base + [("C", result)]), allowed
    yield "seating_defect", history(welded + [("E", inspection("panel_seated", "R"))]), "H"
    yield "seating_uncertain", history(welded + [("E", inspection("panel_seated", "A"))]), "EGH"
    yield "presence_uncertain", history([("A", inspection("panel_presence", "A"))]), "AGH"
    yield "presence_invalid_green", history([("A", inspection("panel_presence", "G", False))]), "AGH"
    for status in ("failed", "partial", "cancelled_by_pause"):
        yield f"prepare_{status}", history(missing + [("B", dict(status=status, completed_steps=["apply_sealant"]))]), "H"
    for status in ("cancelled_by_pause", "cancelled_by_backend_switch"):
        yield f"thermal_{status}", history(present + [("C", dict(status=status, results=[inspection("adhesive")]))]), "CGH"
    yield "empty_thermal_bundle", history(present + [("C", dict(status="completed", results=[]))]), "CGH"
    yield "prepare_invalidates_earlier_thermal", history(missing + [("C", temperatures())] + prepared[1:]), "C"
    yield "reweld_invalidates_seating_and_weld", history(seated + [("C", temperatures()), ("D", dict(status="completed", completed_steps=["weld_panel"]))]), "CE"
    yield "wait_is_not_thermal_confirmation", history(present + [("C", temperatures("R", "R")), ("G", dict(status="completed"))]), "CGH"


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--endpoint", default="http://127.0.0.1:18193")
    p.add_argument("--config", type=Path, default=Path("configs/live_agent.json"))
    p.add_argument("--output", type=Path, required=True)
    args = p.parse_args()
    controller = Controller(args.endpoint, json.loads(args.config.read_text()))
    report = dict(scope="Synthetic development planning probes; not image accuracy or physical execution",
                  prompt=controller.prompt, prompt_sha256=controller.prompt_hash, rows=[])
    args.output.parent.mkdir(parents=True, exist_ok=True)
    for name, messages, allowed in cases():
        row = dict(case=name, expected_choices=list(allowed))
        start = time.monotonic()
        try:
            reply, diagnostic = controller.decide(messages)
            selected = reply["single_token"]["code"]
            row.update(selected=selected, passed=selected in allowed, response=reply, diagnostic=diagnostic)
        except Exception as exc:
            row.update(passed=False, error=str(exc))
        row["elapsed_ms"] = (time.monotonic()-start)*1000
        report["rows"].append(row)
        report["passed"] = sum(r["passed"] for r in report["rows"])
        report["total"] = len(report["rows"])
        args.output.write_text(json.dumps(report, indent=2)+"\n")
        print(name, row.get("selected", "ERROR"), row["passed"], flush=True)
    print(f"Passed {report['passed']}/{report['total']}")


if __name__ == "__main__":
    main()
