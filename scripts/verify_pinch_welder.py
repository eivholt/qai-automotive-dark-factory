"""Validate rendered process telemetry for an opposed-electrode clamp cycle."""
import argparse
import json
import math
from pathlib import Path


def verify(path):
    report = json.loads(path.read_text(encoding="utf-8-sig"))
    samples = {s["name"]: s for s in report["samples"]}
    failures, checks = [], []

    def check(name, passed):
        checks.append(name)
        if not passed:
            failures.append(name)

    check("native motion run completed", report["passed"] and report["model_http_requests"] == 0)
    for name in ("pinch_open", "pinch_released"):
        arm = samples[name]["arm_state"]
        check(name + " has three source pieces", arm["pinch_part_count"] == 3)
        check(name + " clears sheet stack", 14 < arm["pinch_gap_cm"] < 16)
    for index in range(6):
        sample = samples[f"weld_pulse_{index+1}"]
        arm = sample["arm_state"]
        low, high = arm["pinch_tips_world_cm"]
        contact = arm["contacts"][index]
        normal, point = contact["normal"], contact["position_cm"]
        offsets = [sum((tip[k]-point[k])*normal[k] for k in range(3)) for tip in (low, high)]
        center = [(a+b)/2 for a,b in zip(low, high)]
        target = [point[k]-.55*normal[k] for k in range(3)]
        direction = [(b-a)/arm["pinch_gap_cm"] for a,b in zip(low, high)]
        check(f"weld {index+1} opposed electrodes", sum(a*b for a,b in zip(direction, normal)) > .999)
        check(f"weld {index+1} straddles sheet", -1.18 < offsets[0] < -1.12 and .02 < offsets[1] < .08)
        check(f"weld {index+1} centered", math.dist(center, target) < .03)
        check(f"weld {index+1} closed gap", abs(arm["pinch_gap_cm"]-1.2) < .01)
        check(f"weld {index+1} no spacer", not arm["welder_extension_rod"])
    for sample in report["motion_samples"]:
        arm = sample["physical_arm_state"]
        check("bounded jaw angle", 1.16 <= arm["pinch_angle_deg"] <= 14.00001)
        check("bounded jaw gap", 1.19 <= arm["pinch_gap_cm"] <= 14.76)
        check("rear roof stays absent through body turnover",
              arm.get("body_geometry_recipe") == "rear-roof-open-v1"
              and arm.get("removed_roof_component") == "SM_BodyPanel_0385"
              and arm.get("rear_roof_hidden") is True
              and arm.get("rear_roof_collision_disabled") is True)
        lo, hi = arm.get("roof_collider_min_cm", []), arm.get("roof_collider_max_cm", [])
        check("roof collider clears removed crossmember",
              len(lo) == len(hi) == 3 and -91 < lo[1] < -90 and 44 < hi[1] < 45)
    return dict(passed=not failures, checks=len(checks), failures=failures,
                scope="Native electrode transforms and sequencing; not swept-volume or weld-quality certification")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--motion", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    result = verify(args.motion)
    args.output.write_text(json.dumps(result, indent=2)+"\n", encoding="utf-8")
    print(json.dumps(result, indent=2))
    raise SystemExit(0 if result["passed"] else 1)
