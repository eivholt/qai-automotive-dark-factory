#!/usr/bin/env python3
"""Check object-local thermal gradients, thin features and cooling on GPU exports.

This complements verify_thermal.py; it does not replace modality, anomaly,
lighting, occlusion, palette or inference-isolation checks. Requires Pillow.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import statistics
import sys

try:
    from PIL import Image, ImageChops
except ImportError:
    raise SystemExit("Pillow is required. Use an existing Pillow environment, such as the project's documented WSL Python.")

SIZE = (448, 256)
POSES = ("reference", "translated", "rotated")
SAMPLES = ("body_core", "body_edge_near", "body_edge_far", "thin_rib", "heater")
AMBIENT_U8 = (24.0 - 20.0) * 255.0 / 100.0
THRESHOLDS = {
    "minimum_target_pixels": 3,
    "core_edge_difference_u8_min": 5.0,
    "core_rib_difference_u8_min": 5.0,
    "pose_mean_spread_u8_max": 6.0,
    "pose_body_mask_changed_pixels_min": 4,
    "uniform_body_pooled_stddev_u8_max": 1.0,
    "uniform_body_mean_spread_u8_max": 1.0,
    "cooling_initial_excess_above_ambient_u8_min": 5.0,
    "cooling_mean_drop_u8_min": 1.0,
    "rib_additional_fractional_cooling_min": 0.03,
    "powered_heater_mean_drift_u8_max": 1.0,
}


def integer(value):
    return type(value) in (int, float) and math.isfinite(value) and value == int(value)


def region(value, label):
    if not isinstance(value, dict):
        raise ValueError(f"{label} must contain box and stencil_id")
    box, stencil = value.get("box"), value.get("stencil_id")
    if not isinstance(box, list) or len(box) != 4 or not all(integer(v) for v in box):
        raise ValueError(f"{label}.box must contain four integer pixel coordinates")
    box = tuple(int(v) for v in box)
    if not (0 <= box[0] < box[2] <= SIZE[0] and 0 <= box[1] < box[3] <= SIZE[1]):
        raise ValueError(f"{label}.box is outside the image")
    if not integer(stencil) or not 1 <= stencil <= 255:
        raise ValueError(f"{label}.stencil_id must be an integer from 1 to 255")
    return {"box": box, "stencil_id": int(stencil)}


def load_gray(path):
    content = path.read_bytes()
    with Image.open(path) as image:
        image.load()
        if image.format != "PNG" or image.size != SIZE or image.mode not in ("L", "RGB", "RGBA"):
            raise ValueError(f"Expected an 8-bit PNG {SIZE}, found {image.format} {image.size} {image.mode}")
        rgb = image.convert("RGB")
        red, green, blue = rgb.split()
        if ImageChops.difference(red, green).getbbox() or ImageChops.difference(red, blue).getbbox():
            raise ValueError("Pixels are not exactly grayscale")
        return red.copy(), {"sha256": hashlib.sha256(content).hexdigest(), "bytes": len(content), "size": list(image.size)}


def sample_values(image, ids, declaration):
    box, stencil = declaration["box"], declaration["stencil_id"]
    return [value for value, surface_id in zip(image.crop(box).getdata(), ids.crop(box).getdata()) if surface_id == stencil]


def value_stats(values, area):
    mean = statistics.mean(values)
    return {
        "target_pixels": len(values), "box_pixels": area,
        "coverage_fraction": round(len(values) / area, 6),
        "mean_u8": round(mean, 6), "stddev_u8": round(statistics.pstdev(values), 6),
        "min_u8": min(values), "max_u8": max(values),
        "apparent_mean_c": round(20.0 + mean * 100.0 / 255.0, 6),
    }


def changed_body_pixels(first, second, stencil):
    return sum((a == stencil) != (b == stencil) for a, b in zip(first.getdata(), second.getdata()))


def verify(directory):
    report = {
        "scope": "GPU scalar thermal-field QA, with visible-stencil-filtered samples; procedural thermal proxy, not a heat-transport or model validation.",
        "directory": str(directory.resolve()), "encoding": {"size": list(SIZE), "min_apparent_c": 20, "max_apparent_c": 120, "ambient_c": 24, "ambient_u8": AMBIENT_U8},
        "thresholds": THRESHOLDS, "checks": {}, "files": {}, "samples": {}, "stats": {}, "errors": [],
    }
    checks, errors = report["checks"], report["errors"]
    try:
        metadata = json.loads((directory / "field_regions.json").read_text(encoding="utf-8-sig"))
        if not isinstance(metadata, dict) or not isinstance(metadata.get("poses"), dict):
            raise ValueError("Expected a poses object in field_regions.json")
        report["metadata"] = metadata
    except (OSError, ValueError) as exc:
        errors.append(f"field_regions.json: {exc}")
        metadata = {"poses": {}}
    regions = {}
    for pose in POSES:
        regions[pose] = {}
        data = metadata["poses"].get(pose, {})
        for name in SAMPLES:
            try:
                regions[pose][name] = region(data.get(name), f"poses.{pose}.{name}")
            except (ValueError, AttributeError) as exc:
                errors.append(str(exc))
    checks["all_pose_sample_declarations_valid"] = all(len(values) == len(SAMPLES) for values in regions.values())
    if checks["all_pose_sample_declarations_valid"]:
        checks["semantic_stencil_ids_match_across_poses"] = all(
            len({regions[pose][name]["stencil_id"] for pose in POSES}) == 1 for name in SAMPLES)
        for pose in POSES:
            samples = regions[pose]
            body = samples["body_core"]["stencil_id"]
            checks[pose + "_body_regions_share_surface"] = all(samples[name]["stencil_id"] == body for name in ("body_edge_near", "body_edge_far"))
            checks[pose + "_rib_and_heater_are_distinct_surfaces"] = len({body, samples["thin_rib"]["stencil_id"], samples["heater"]["stencil_id"]}) == 3

    images = {}
    names = [f"field_{pose}{suffix}" for pose in POSES for suffix in ("", "_ids")] + ["field_uniform", "field_cooled"]
    for name in names:
        try:
            images[name], report["files"][name] = load_gray(directory / (name + ".png"))
        except (OSError, ValueError) as exc:
            errors.append(f"{name}.png: {exc}")
    checks["all_eight_pngs_are_8bit_grayscale_and_448x256"] = len(images) == len(names)

    values_by_pose = {}
    for pose in POSES:
        raw, ids = images.get("field_" + pose), images.get("field_" + pose + "_ids")
        values_by_pose[pose] = {}
        report["samples"][pose] = {}
        if raw is None or ids is None:
            continue
        for name, declaration in regions[pose].items():
            values = sample_values(raw, ids, declaration)
            box = declaration["box"]
            area = (box[2] - box[0]) * (box[3] - box[1])
            checks[pose + "_" + name + "_visible_surface_pixels"] = len(values) >= THRESHOLDS["minimum_target_pixels"]
            if len(values) < THRESHOLDS["minimum_target_pixels"]:
                report["samples"][pose][name] = {"target_pixels": len(values), "box_pixels": area}
                continue
            values_by_pose[pose][name] = values
            report["samples"][pose][name] = value_stats(values, area)
        measured = values_by_pose[pose]
        if all(name in measured for name in ("body_core", "body_edge_near", "body_edge_far", "thin_rib")):
            means = {name: statistics.mean(values) for name, values in measured.items()}
            contrasts = {name: means["body_core"] - means[name] for name in ("body_edge_near", "body_edge_far", "thin_rib")}
            report["stats"][pose + "_core_contrasts_u8"] = contrasts
            checks[pose + "_core_hotter_than_both_edges"] = min(contrasts[name] for name in ("body_edge_near", "body_edge_far")) >= THRESHOLDS["core_edge_difference_u8_min"]
            checks[pose + "_thin_rib_cooler_than_core"] = contrasts["thin_rib"] >= THRESHOLDS["core_rib_difference_u8_min"]

    if regions["reference"].get("body_core") and "field_reference_ids" in images:
        body = regions["reference"]["body_core"]["stencil_id"]
        for pose in ("translated", "rotated"):
            if "field_" + pose + "_ids" in images:
                changed = changed_body_pixels(images["field_reference_ids"], images["field_" + pose + "_ids"], body)
                report["stats"][pose + "_body_mask_changed_pixels"] = changed
                checks[pose + "_actually_changes_projected_body"] = changed >= THRESHOLDS["pose_body_mask_changed_pixels_min"]
    for name in SAMPLES:
        if all(name in values_by_pose[pose] for pose in POSES):
            means = [statistics.mean(values_by_pose[pose][name]) for pose in POSES]
            spread = max(means) - min(means)
            report["stats"][name + "_pose_mean_spread_u8"] = round(spread, 6)
            checks[name + "_object_local_temperature_invariant"] = spread <= THRESHOLDS["pose_mean_spread_u8_max"]

    # Uniform and cooled controls retain the reference geometry, hence the same
    # visible-ID mask. Background/neighbor pixels never enter these statistics.
    controls = {}
    reference_ids = images.get("field_reference_ids")
    for control in ("uniform", "cooled"):
        controls[control] = {}
        report["samples"][control] = {}
        if reference_ids is None or "field_" + control not in images:
            continue
        for name, declaration in regions["reference"].items():
            values = sample_values(images["field_" + control], reference_ids, declaration)
            checks[control + "_" + name + "_visible_surface_pixels"] = len(values) >= THRESHOLDS["minimum_target_pixels"]
            if len(values) < THRESHOLDS["minimum_target_pixels"]:
                continue
            controls[control][name] = values
            box = declaration["box"]
            report["samples"][control][name] = value_stats(values, (box[2] - box[0]) * (box[3] - box[1]))
    body_names = ("body_core", "body_edge_near", "body_edge_far")
    if all(name in controls["uniform"] for name in body_names):
        pooled = [value for name in body_names for value in controls["uniform"][name]]
        means = [statistics.mean(controls["uniform"][name]) for name in body_names]
        deviation, spread = statistics.pstdev(pooled), max(means) - min(means)
        report["stats"]["uniform_body"] = {"pooled_stddev_u8": round(deviation, 6), "region_mean_spread_u8": round(spread, 6), "target_pixels": len(pooled)}
        checks["uniform_control_body_is_flat"] = (deviation <= THRESHOLDS["uniform_body_pooled_stddev_u8_max"] and spread <= THRESHOLDS["uniform_body_mean_spread_u8_max"])

    cooling = metadata.get("cooling", {})
    initial_age = cooling.get("initial_age_seconds") if isinstance(cooling, dict) else None
    cooled_age = cooling.get("cooled_age_seconds") if isinstance(cooling, dict) else None
    checks["cooling_age_control_is_15_to_45_seconds"] = (
        type(initial_age) in (int, float) and type(cooled_age) in (int, float)
        and initial_age == 15 and cooled_age == 45)
    cooling_stats = {}
    for name in ("body_core", "thin_rib", "heater"):
        if name not in values_by_pose["reference"] or name not in controls["cooled"]:
            continue
        initial = statistics.mean(values_by_pose["reference"][name])
        cooled = statistics.mean(controls["cooled"][name])
        excess = initial - AMBIENT_U8
        fractional_loss = 1.0 - max(0.0, cooled - AMBIENT_U8) / excess if excess > 0 else None
        cooling_stats[name] = {"initial_mean_u8": initial, "cooled_mean_u8": cooled, "mean_drop_u8": initial - cooled, "initial_excess_u8": excess, "fraction_of_excess_lost": fractional_loss}
        if name == "heater":
            checks["powered_heater_stays_unchanged_during_cooling"] = abs(initial - cooled) <= THRESHOLDS["powered_heater_mean_drift_u8_max"]
        else:
            checks[name + "_has_measurable_initial_heat_excess"] = excess >= THRESHOLDS["cooling_initial_excess_above_ambient_u8_min"]
            checks[name + "_cools_over_30_seconds"] = initial - cooled >= THRESHOLDS["cooling_mean_drop_u8_min"]
    if all(name in cooling_stats for name in ("body_core", "thin_rib")):
        body_fraction = cooling_stats["body_core"]["fraction_of_excess_lost"]
        rib_fraction = cooling_stats["thin_rib"]["fraction_of_excess_lost"]
        checks["thin_rib_loses_heat_excess_faster_than_body"] = (
            body_fraction is not None and rib_fraction is not None
            and rib_fraction - body_fraction >= THRESHOLDS["rib_additional_fractional_cooling_min"])
    report["stats"]["cooling"] = cooling_stats
    report["passed"] = not errors and all(checks.values())
    report["failed_checks"] = [key for key, passed in checks.items() if not passed]
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--directory", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    report = verify(args.directory)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2, allow_nan=False) + "\n", encoding="utf-8")
    print(json.dumps({"passed": report["passed"], "failed_checks": report["failed_checks"], "errors": report["errors"], "report": str(args.output.resolve())}, indent=2))
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
