#!/usr/bin/env python3
"""Verify rendered night-vision display effects and unchanged model pixels.

Run after unreal/Scripts/run.ps1 -NightVisionSmoke -Output DIR.
Requires Pillow only; no model, network, NumPy, or Unreal Python imports.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import sys

try:
    from PIL import Image, ImageChops, ImageStat
except ImportError:
    raise SystemExit("Pillow is required; use a Python environment with Pillow installed. See docs/night_vision_qa.md.")

SCREEN_SIZE = (1600, 900)
MODEL_SIZE = (448, 256)
# Fixed 1600x900 run.ps1 viewport. Crops exclude labels, borders and status text.
CROPS = {"observer": (30, 150, 990, 710), "preview": (1055, 114, 1525, 375)}
SCREENS = ("green", "green_next_frame", "monochrome", "observer_dark")
THRESHOLDS = {
    "green_channel_mean_min": 8.0,
    "green_mean_advantage_min": 6.0,
    "green_dominant_fraction_min": 0.30,
    "monochrome_mean_channel_spread_max": 2.0,
    "monochrome_p95_channel_spread_max": 4,
    "grain_green_mean_abs_change_min": 0.25,
    "grain_green_abs_change_stddev_min": 0.30,
    "grain_changed_pixel_fraction_min": 0.05,
    "dark_luma_mean_max": 3.0,
    "dark_max_channel_p95_max": 12,
    "dark_pixels_above_12_fraction_max": 0.02,
}


def quantile(histogram: list[int], fraction: float) -> int:
    target = sum(histogram) * fraction
    count = 0
    for value, frequency in enumerate(histogram):
        count += frequency
        if count >= target:
            return value
    return 255


def fraction_above(image: Image.Image, cutoff: int) -> float:
    histogram = image.histogram()
    return sum(histogram[cutoff + 1:]) / (image.width * image.height)


def image_stats(image: Image.Image) -> dict:
    red, green, blue = image.split()
    high = ImageChops.lighter(red, ImageChops.lighter(green, blue))
    low = ImageChops.darker(red, ImageChops.darker(green, blue))
    spread = ImageChops.subtract(high, low)
    green_advantage = ImageChops.darker(ImageChops.subtract(green, red), ImageChops.subtract(green, blue))
    dominance = ImageChops.multiply(green_advantage.point(lambda x: 255 if x >= 3 else 0),
                                    green.point(lambda x: 255 if x >= 10 else 0))
    means = ImageStat.Stat(image).mean
    luma = image.convert("L")
    return {
        "mean_rgb": [round(value, 5) for value in means],
        "luma_mean": round(ImageStat.Stat(luma).mean[0], 5),
        "luma_p95": quantile(luma.histogram(), .95),
        "mean_green_advantage": round(means[1] - max(means[0], means[2]), 5),
        "green_dominant_fraction": round(fraction_above(dominance, 0), 6),
        "mean_channel_spread": round(ImageStat.Stat(spread).mean[0], 5),
        "p95_channel_spread": quantile(spread.histogram(), .95),
        "max_channel_spread": spread.getextrema()[1],
        "max_channel_p95": quantile(high.histogram(), .95),
        "pixels_above_12_fraction": round(fraction_above(high, 12), 6),
    }


def green_enough(stats: dict) -> bool:
    return (stats["mean_rgb"][1] >= THRESHOLDS["green_channel_mean_min"]
            and stats["mean_green_advantage"] >= THRESHOLDS["green_mean_advantage_min"]
            and stats["green_dominant_fraction"] >= THRESHOLDS["green_dominant_fraction_min"])


def monochrome_enough(stats: dict) -> bool:
    return (stats["mean_channel_spread"] <= THRESHOLDS["monochrome_mean_channel_spread_max"]
            and stats["p95_channel_spread"] <= THRESHOLDS["monochrome_p95_channel_spread_max"])


def grain_stats(first: Image.Image, second: Image.Image) -> dict:
    difference = ImageChops.difference(first, second)
    stats = ImageStat.Stat(difference)
    changed = ImageChops.lighter(*difference.split()[:2])
    changed = ImageChops.lighter(changed, difference.getchannel("B"))
    return {
        "mean_absolute_rgb_change": [round(value, 5) for value in stats.mean],
        "absolute_rgb_change_stddev": [round(value, 5) for value in stats.stddev],
        "changed_pixel_fraction": round(fraction_above(changed, 0), 6),
        "scope": "Same frozen model input and stationary smoke scene; pixel changes demonstrate temporal display variation, not calibrated sensor noise.",
    }


def animated_enough(stats: dict) -> bool:
    return (stats["mean_absolute_rgb_change"][1] >= THRESHOLDS["grain_green_mean_abs_change_min"]
            and stats["absolute_rgb_change_stddev"][1] >= THRESHOLDS["grain_green_abs_change_stddev_min"]
            and stats["changed_pixel_fraction"] >= THRESHOLDS["grain_changed_pixel_fraction_min"])


def verify(directory: Path) -> dict:
    report = {
        "scope": "Rendered display-shader QA only; no model inference or spectral-camera validation.",
        "directory": str(directory.resolve()), "screen_size": list(SCREEN_SIZE),
        "crops": {key: list(box) for key, box in CROPS.items()},
        "thresholds": THRESHOLDS, "checks": {}, "stats": {}, "files": {}, "errors": [],
    }
    checks = report["checks"]
    try:
        runtime = json.loads((directory / "runtime.json").read_text(encoding="utf-8-sig"))
        if not isinstance(runtime, dict):
            raise ValueError("runtime.json must be an object")
        report["runtime"] = runtime
        for key in ("observer_material_loaded", "preview_material_loaded", "model_png_unchanged", "model_recapture_unchanged", "sensor_infrared_locked", "passed"):
            checks["runtime_" + key] = runtime.get(key) is True
    except (OSError, ValueError) as exc:
        report["errors"].append(f"runtime.json: {exc}")
        checks["runtime_valid"] = False

    images = {}
    for name in (*SCREENS, "model_before", "model_after"):
        path = directory / (name + ".png")
        try:
            content = path.read_bytes()
            with Image.open(path) as source:
                source.load()
                if source.format != "PNG":
                    raise ValueError("Expected PNG encoding")
                expected = MODEL_SIZE if name.startswith("model_") else SCREEN_SIZE
                if source.size != expected:
                    raise ValueError(f"Expected {expected}, found {source.size}; run the fixed viewport smoke")
                images[name] = source.convert("RGB")
                report["files"][name] = {"sha256": hashlib.sha256(content).hexdigest(), "bytes": len(content), "size": list(source.size)}
        except (OSError, ValueError) as exc:
            report["errors"].append(f"{path.name}: {exc}")
    checks["all_expected_pngs_valid"] = len(images) == 6
    if "model_before" in images and "model_after" in images:
        checks["model_png_bytes_identical"] = report["files"]["model_before"]["sha256"] == report["files"]["model_after"]["sha256"]
        for name in ("model_before", "model_after"):
            stats = image_stats(images[name])
            report["stats"][name] = stats
            checks[name + "_exact_grayscale"] = stats["max_channel_spread"] == 0

    for name in SCREENS:
        if name not in images:
            continue
        regions = {region: image_stats(images[name].crop(box)) for region, box in CROPS.items()}
        report["stats"][name] = regions
        if name in ("green", "green_next_frame"):
            for region, stats in regions.items():
                checks[name + "_" + region + "_green"] = green_enough(stats)
        elif name == "monochrome":
            for region, stats in regions.items():
                checks[name + "_" + region + "_neutral"] = monochrome_enough(stats)
        elif name == "observer_dark":
            scene = regions["observer"]
            checks["dark_observer_near_black"] = (
                scene["luma_mean"] <= THRESHOLDS["dark_luma_mean_max"]
                and scene["max_channel_p95"] <= THRESHOLDS["dark_max_channel_p95_max"]
                and scene["pixels_above_12_fraction"] <= THRESHOLDS["dark_pixels_above_12_fraction_max"])
            checks["dark_observer_preview_still_green"] = green_enough(regions["preview"])

    if "green" in images and "green_next_frame" in images:
        for region, box in CROPS.items():
            stats = grain_stats(images["green"].crop(box), images["green_next_frame"].crop(box))
            report["stats"][region + "_temporal_difference"] = stats
            checks[region + "_animated_grain"] = animated_enough(stats)
    report["passed"] = not report["errors"] and all(checks.values())
    report["failed_checks"] = [name for name, passed in checks.items() if not passed]
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--directory", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    report = verify(args.directory)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"passed": report["passed"], "failed_checks": report["failed_checks"],
                      "errors": report["errors"], "report": str(args.output.resolve())}, indent=2))
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
