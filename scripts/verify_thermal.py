#!/usr/bin/env python3
"""QA for actual GPU thermal smoke exports; does not run a thermal classifier.

Requires Pillow. All temperatures below refer to the declared simulated apparent
temperature encoding, not calibrated radiometric camera measurements.
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
    from PIL import Image, ImageChops, ImageStat
except ImportError:
    raise SystemExit("Pillow is required; use an existing Pillow environment (see docs/thermal_qa.md).")

RAW_SIZE = (448, 256)
SCREEN_SIZE = (1600, 900)
RAW_NAMES = ("normal", "overheated", "heater_fault", "glue_gap", "light_on", "light_off", "occluded")
NIR_NAMES = ("nir_before", "nir_after")
SCREEN_NAMES = ("white_hot", "black_hot", "iron")
REGION_NAMES = ("target_module", "heater", "glue_gap", "glue_intact", "occluder")
OBSERVER_BOX = (30, 150, 990, 700)
THRESHOLDS = {
    "roi_min_pixels": 16,
    "anomaly_mean_delta_u8_min": 5.0,
    "anomaly_changed_fraction_min": 0.10,
    "changed_pixel_delta_u8_min": 3,
    "intact_glue_mean_drift_u8_max": 3.0,
    "glue_gap_intact_contrast_u8_min": 5.0,
    "occlusion_mean_drop_u8_min": 10.0,
    "occlusion_ambient_mean_tolerance_c": 3.0,
    "occlusion_ambient_p95_tolerance_c": 5.0,
    "palette_gray_mean_spread_u8_max": 2.0,
    "palette_gray_p95_spread_u8_max": 4,
    "white_black_correlation_max": -0.60,
    "white_black_luma_stddev_min": 3.0,
    "iron_mean_spread_u8_min": 3.0,
    "iron_color_fraction_min": 0.03,
}


def p95(histogram):
    target, count = sum(histogram) * .95, 0
    for value, frequency in enumerate(histogram):
        count += frequency
        if count >= target:
            return value
    return 255


def percentile(values, q):
    return sorted(values)[max(0, math.ceil(len(values) * q) - 1)] if values else None


def stats(image):
    red, green, blue = image.split()
    high = ImageChops.lighter(red, ImageChops.lighter(green, blue))
    low = ImageChops.darker(red, ImageChops.darker(green, blue))
    spread = ImageChops.subtract(high, low)
    gray = image.convert("L")
    gray_stats = ImageStat.Stat(gray)
    return {
        "mean_u8": round(gray_stats.mean[0], 5),
        "stddev_u8": round(gray_stats.stddev[0], 5),
        "p95_u8": p95(gray.histogram()),
        "min_u8": gray.getextrema()[0], "max_u8": gray.getextrema()[1],
        "mean_rgb": [round(value, 5) for value in ImageStat.Stat(image).mean],
        "mean_channel_spread_u8": round(ImageStat.Stat(spread).mean[0], 5),
        "p95_channel_spread_u8": p95(spread.histogram()),
        "max_channel_spread_u8": spread.getextrema()[1],
        "colored_pixel_fraction": round(sum(spread.histogram()[6:]) / (image.width * image.height), 6),
    }


def validate_box(value, size, name):
    if (not isinstance(value, (list, tuple)) or len(value) != 4
            or any(type(x) is not int for x in value)):
        raise ValueError(f"{name} must be [left, top, right, bottom] integer pixels")
    left, top, right, bottom = value
    if not (0 <= left < right <= size[0] and 0 <= top < bottom <= size[1]):
        raise ValueError(f"{name} is outside its image")
    if (right - left) * (bottom - top) < THRESHOLDS["roi_min_pixels"]:
        raise ValueError(f"{name} must cover at least {THRESHOLDS['roi_min_pixels']} pixels")
    return tuple(value)


def compare(first, second):
    a, b = first.convert("L"), second.convert("L")
    diff = ImageChops.difference(a, b)
    n = a.width * a.height
    return {
        "mean_signed_delta_u8": round(ImageStat.Stat(b).mean[0] - ImageStat.Stat(a).mean[0], 5),
        "mean_absolute_delta_u8": round(ImageStat.Stat(diff).mean[0], 5),
        "changed_fraction": round(sum(diff.histogram()[THRESHOLDS["changed_pixel_delta_u8_min"]:]) / n, 6),
    }


def inverse_correlation(first, second):
    a, b = first.convert("L"), second.convert("L")
    stat_a, stat_b = ImageStat.Stat(a), ImageStat.Stat(b)
    mean_a, mean_b = stat_a.mean[0], stat_b.mean[0]
    denominator = stat_a.stddev[0] * stat_b.stddev[0]
    covariance = sum((x - mean_a) * (y - mean_b) for x, y in zip(a.getdata(), b.getdata())) / (a.width * a.height)
    return {
        "pearson_luma_correlation": round(covariance / denominator, 6) if denominator else None,
        "white_hot_stddev_u8": round(stat_a.stddev[0], 5),
        "black_hot_stddev_u8": round(stat_b.stddev[0], 5),
    }


def verify(directory):
    report = {
        "scope": "GPU thermal rendering QA; apparent-temperature proxy, no thermal model or radiometric calibration.",
        "directory": str(directory.resolve()), "thresholds": THRESHOLDS,
        "checks": {}, "files": {}, "stats": {}, "errors": [],
    }
    checks, errors = report["checks"], report["errors"]
    runtime = {}
    try:
        runtime = json.loads((directory / "runtime.json").read_text(encoding="utf-8-sig"))
        if not isinstance(runtime, dict):
            raise ValueError("runtime.json must be an object")
        report["runtime"] = runtime
    except (OSError, ValueError) as exc:
        errors.append(f"runtime.json: {exc}")
        checks["runtime_valid"] = False
        runtime = {}
    for key in ("observer_material_loaded", "sensor_material_loaded", "temperature_lut_loaded", "emissivity_lut_loaded", "nir_contract_unchanged", "async_snapshot_matches_raw", "radiometry_self_test", "stale_snapshot_rejected", "stale_preview_rejected", "passed"):
        checks["runtime_" + key] = runtime.get(key) is True
    for key in ("thermal_sent_to_nir_contract", "adapter_ready"):
        checks["runtime_" + key + "_false"] = runtime.get(key) is False
    before, after = runtime.get("nir_inference_requests_before"), runtime.get("nir_inference_requests_after")
    checks["no_inference_requests_during_thermal_smoke"] = (
        type(before) is int and type(after) is int and before >= 0 and before == after
        and type(runtime.get("thermal_inference_requests")) is int
        and runtime["thermal_inference_requests"] == 0)
    checks["live_thermal_preview_has_no_cpu_readbacks"] = (
        type(runtime.get("cpu_readbacks_in_live_thermal_preview")) is int
        and runtime["cpu_readbacks_in_live_thermal_preview"] == 0)
    try:
        contract = json.loads((Path(__file__).resolve().parents[1] / "configs/inspection_contract.json").read_text(encoding="utf-8-sig"))
        actual_hash = hashlib.sha256(contract["prompt"].encode()).hexdigest()
        checks["nir_contract_identity_matches_repository"] = (
            runtime.get("nir_contract_version") == contract["version"]
            and runtime.get("nir_prompt_sha256") == actual_hash == contract["prompt_sha256"])
    except (OSError, ValueError, KeyError, TypeError) as exc:
        errors.append(f"NIR inspection contract: {exc}")
        checks["nir_contract_identity_matches_repository"] = False

    encoding = runtime.get("thermal_encoding", {})
    if not isinstance(encoding, dict):
        encoding = {}
    checks["declared_linear_apparent_temperature_encoding"] = (
        encoding.get("width") == 448 and encoding.get("height") == 256
        and encoding.get("min_temperature_c") == 20 and encoding.get("max_temperature_c") == 120
        and encoding.get("ambient_temperature_c") == 24 and encoding.get("band_um") == [8, 14]
        and encoding.get("quantity") == "apparent_temperature" and encoding.get("linear_u8") is True)
    report["encoding_note"] = "20..120 degC apparent temperature, 100/255 = 0.392157 degC per count; simulated 8-14 um radiance with emissivity/reflected ambient."

    regions = {}
    declared_regions = runtime.get("qa_regions", {})
    for name in REGION_NAMES:
        try:
            regions[name] = validate_box(declared_regions.get(name), RAW_SIZE, name)
        except (ValueError, AttributeError) as exc:
            errors.append(str(exc))
    checks["projected_raw_regions_valid"] = len(regions) == len(REGION_NAMES)
    report["qa_regions"] = regions

    images = {}
    for name in (*RAW_NAMES, *NIR_NAMES, *SCREEN_NAMES):
        try:
            path = directory / (name + ".png")
            content = path.read_bytes()
            with Image.open(path) as source:
                source.load()
                expected = SCREEN_SIZE if name in SCREEN_NAMES else RAW_SIZE
                if source.format != "PNG" or source.size != expected:
                    raise ValueError(f"Expected PNG {expected}, got {source.format} {source.size}")
                images[name] = source.convert("RGB")
                report["files"][name] = {"sha256": hashlib.sha256(content).hexdigest(), "bytes": len(content), "size": list(source.size)}
                if name not in SCREEN_NAMES:
                    image_stats = stats(images[name])
                    report["stats"][name] = image_stats
                    checks[name + "_exact_grayscale"] = image_stats["max_channel_spread_u8"] == 0
        except (OSError, ValueError) as exc:
            errors.append(f"{name}.png: {exc}")
    checks["all_expected_pngs_valid"] = len(images) == 12
    for first, second, check in (("nir_before", "nir_after", "nir_png_bytes_unchanged"),
                                 ("light_on", "light_off", "thermal_independent_of_visible_and_nir_lights")):
        if first in images and second in images:
            checks[check] = report["files"][first]["sha256"] == report["files"][second]["sha256"]
            report["stats"][check] = compare(images[first], images[second])

    # One-sided contrasts check the intended physical direction without copying
    # shader formulas or relying on one exact reference screenshot.
    comparisons = (("overheated", "target_module", 1), ("heater_fault", "heater", -1), ("glue_gap", "glue_gap", -1))
    for case, region, sign in comparisons:
        if "normal" in images and case in images and region in regions:
            value = compare(images["normal"].crop(regions[region]), images[case].crop(regions[region]))
            report["stats"][case + "_contrast"] = value
            checks[case + "_has_expected_local_temperature_change"] = (
                sign * value["mean_signed_delta_u8"] >= THRESHOLDS["anomaly_mean_delta_u8_min"]
                and value["changed_fraction"] >= THRESHOLDS["anomaly_changed_fraction_min"])
    if "normal" in images and "glue_gap" in images and all(key in regions for key in ("glue_gap", "glue_intact")):
        intact = compare(images["normal"].crop(regions["glue_intact"]), images["glue_gap"].crop(regions["glue_intact"]))
        gap_mean = stats(images["glue_gap"].crop(regions["glue_gap"]))["mean_u8"]
        intact_mean = stats(images["glue_gap"].crop(regions["glue_intact"]))["mean_u8"]
        report["stats"]["glue_intact_control"] = intact
        report["stats"]["glue_gap_intact_contrast_u8"] = round(intact_mean - gap_mean, 5)
        checks["glue_gap_preserves_intact_heated_glue"] = (
            abs(intact["mean_signed_delta_u8"]) <= THRESHOLDS["intact_glue_mean_drift_u8_max"]
            and intact_mean - gap_mean >= THRESHOLDS["glue_gap_intact_contrast_u8_min"])
    if "overheated" in images and "occluded" in images and "occluder" in regions:
        box = regions["occluder"]
        difference = compare(images["overheated"].crop(box), images["occluded"].crop(box))
        occluded = stats(images["occluded"].crop(box))
        apparent_mean = 20 + occluded["mean_u8"] * 100 / 255
        apparent_p95 = 20 + occluded["p95_u8"] * 100 / 255
        report["stats"]["occlusion"] = {**difference, "apparent_mean_c": round(apparent_mean, 5), "apparent_p95_c": round(apparent_p95, 5)}
        checks["opaque_cover_blocks_hot_custom_depth_surface"] = (
            -difference["mean_signed_delta_u8"] >= THRESHOLDS["occlusion_mean_drop_u8_min"]
            and apparent_mean <= 24 + THRESHOLDS["occlusion_ambient_mean_tolerance_c"]
            and apparent_p95 <= 24 + THRESHOLDS["occlusion_ambient_p95_tolerance_c"])

    screen_regions = {"observer": OBSERVER_BOX}
    optional_screens = runtime.get("qa_screen_regions", {})
    if isinstance(optional_screens, dict) and "thermal_preview" in optional_screens:
        try:
            screen_regions["thermal_preview"] = validate_box(optional_screens["thermal_preview"], SCREEN_SIZE, "thermal_preview")
        except ValueError as exc:
            errors.append(str(exc))
    report["qa_screen_regions"] = screen_regions
    report["thermal_preview_palette_checked"] = "thermal_preview" in screen_regions
    for region, box in screen_regions.items():
        for name in SCREEN_NAMES:
            if name not in images:
                continue
            value = stats(images[name].crop(box))
            report["stats"][name + "_" + region] = value
            if name in ("white_hot", "black_hot"):
                checks[name + "_" + region + "_neutral"] = (
                    value["mean_channel_spread_u8"] <= THRESHOLDS["palette_gray_mean_spread_u8_max"]
                    and value["p95_channel_spread_u8"] <= THRESHOLDS["palette_gray_p95_spread_u8_max"])
            else:
                checks["iron_" + region + "_uses_color"] = (
                    value["mean_channel_spread_u8"] >= THRESHOLDS["iron_mean_spread_u8_min"]
                    and value["colored_pixel_fraction"] >= THRESHOLDS["iron_color_fraction_min"])
        if "white_hot" in images and "black_hot" in images:
            value = inverse_correlation(images["white_hot"].crop(box), images["black_hot"].crop(box))
            report["stats"]["white_black_" + region + "_inverse"] = value
            checks["white_black_" + region + "_inverse"] = (
                value["pearson_luma_correlation"] is not None
                and value["pearson_luma_correlation"] <= THRESHOLDS["white_black_correlation_max"]
                and min(value["white_hot_stddev_u8"], value["black_hot_stddev_u8"]) >= THRESHOLDS["white_black_luma_stddev_min"])

    performance = {"measured": False, "modes": {}, "note": "Optional total-frame timings; no claim of GPU-only time or target FPS without an explicit measured budget."}
    timings = runtime.get("mode_frame_ms", {})
    if isinstance(timings, dict):
        for mode, values in timings.items():
            if not isinstance(values, list) or not values or any(type(value) not in (int, float) or not math.isfinite(value) or value <= 0 for value in values):
                errors.append(f"mode_frame_ms.{mode} must be a nonempty array of positive finite milliseconds")
                continue
            # Keep warm-up visible and exclude exactly the first sample consistently.
            warm = values[1:]
            performance["modes"][mode] = {"samples": len(values), "first_frame_ms": values[0], "warm_samples": len(warm), "warm_p50_ms": percentile(warm, .5), "warm_p95_ms": percentile(warm, .95), "warm_mean_ms": statistics.mean(warm) if warm else None}
        performance["measured"] = bool(performance["modes"])
    report["performance"] = performance
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
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"passed": report["passed"], "failed_checks": report["failed_checks"], "errors": report["errors"], "report": str(args.output.resolve())}, indent=2))
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
