"""Verify actual native observer graphics, frame evidence and sensor independence."""
from __future__ import annotations
import argparse
import itertools
import json
import math
from pathlib import Path
import numpy as np
from PIL import Image


def verify(directory: Path) -> dict:
    report = json.loads((directory / "observer_graphics.json").read_text(encoding="utf-8-sig"))
    checks = []
    def check(name, condition):
        checks.append({"name": name, "passed": bool(condition)})
    check("native_passed", report.get("passed") is True)
    check("schema", report.get("schema") == "observer-graphics-smoke-v1")
    check("explicit_rendering_fixture", report.get("source") == "frozen_rendering_qa_no_inference")
    check("no_model_requests", type(report.get("model_http_requests")) in (int, float) and report["model_http_requests"] == 0)
    check("actual_capture_priming", report.get("capture_prime_frames") == 3)
    check("fixed_sensor_render_sequence", report.get("sensor_render_sequence") == 0)
    check("meaningful_thermal_display_range", report.get("observer_thermal_range") == "0-120 C")
    samples = report.get("samples", [])
    check("all_32_states", len(samples) == 32)
    if len(samples) != 32:
        return {"passed": False, "checks": checks}
    roi = report.get("observer_scene_roi", [])
    valid_roi = len(roi) == 4 and all(type(v) in (int, float) and math.isfinite(v) and int(v) == v for v in roi)
    valid_roi = valid_roi and 0 <= roi[0] < roi[2] <= 1600 and 0 <= roi[1] < roi[3] <= 900 and (roi[2]-roi[0])*(roi[3]-roi[1]) >= 200000
    check("meaningful_in_bounds_scene_roi", valid_roi)
    if not valid_roi:
        return {"passed": False, "checks": checks}
    x0, y0, x1, y1 = map(int, roi)
    references, observers, seen, paths = {}, {}, set(), set()
    sensor_names = {"r4_nir", "r4_thermal", "fixed_nir", "fixed_thermal"}
    for index, sample in enumerate(samples):
        name = f"state_{index}"
        flags = tuple(sample.get(k) for k in ("dlss_requested", "lumen_requested", "hardware_rt_requested"))
        check(name+"_boolean_preferences", all(type(v) is bool for v in flags))
        mode = sample.get("observer_mode")
        key = (mode, *flags)
        check(name+"_unique", key not in seen)
        seen.add(key)
        visible = mode in (0, 3)
        supported = sample.get("dlss_supported") is True
        rt_supported = sample.get("hardware_rt_supported") is True
        dlss = visible and flags[0] and supported
        lumen = visible and flags[1]
        rt = lumen and flags[2] and rt_supported
        check(name+"_dlss_effective", sample.get("dlss_enabled") is dlss)
        check(name+"_actual_nvidia_upscaler", ("DLSS" in sample.get("actual_temporal_upscaler", "")) is dlss)
        check(name+"_lumen_effective", sample.get("lumen_enabled") is lumen)
        exposure = sample.get("observer_exposure_ev")
        check(name+"_observer_only_exposure", type(exposure) in (int, float) and math.isfinite(exposure) and abs(exposure - (-1.2 if mode == 3 and lumen else 0)) < 1e-5)
        check(name+"_hwrt_effective", sample.get("hardware_rt_enabled") is rt)
        check(name+"_actual_raytracing_view", sample.get("actual_view_ray_tracing_allowed") is rt)
        pct = sample.get("screen_percentage")
        check(name+"_render_resolution", type(pct) in (int, float) and math.isfinite(pct) and ((40 < pct < 100) if dlss else pct == 100))
        check(name+"_rendered", sample.get("rendered_view_count", 0) > 0 and sample.get("legacy_fixture") is False)
        image_path = directory / sample["screenshot"]
        check(name+"_unique_observer_path", image_path not in paths)
        paths.add(image_path)
        image = np.asarray(Image.open(image_path).convert("RGB"))
        check(name+"_observer_1600x900", image.shape == (900, 1600, 3))
        crop = image[y0:y1, x0:x1]
        check(name+"_observer_geometry", crop.std() > 3 and np.ptp(crop) > 30)
        observers[key] = crop
        sensors = sample.get("sensors", [])
        check(name+"_four_sensors", {s.get("name") for s in sensors} == sensor_names and len(sensors) == 4)
        for sensor in sensors:
            sid = sensor["name"]
            image_path = directory / sensor["image"]
            check(name+"_"+sid+"_unique_path", image_path not in paths)
            paths.add(image_path)
            png = image_path.read_bytes()
            frame = np.asarray(Image.open(image_path).convert("RGB"))
            check(name+"_"+sid+"_448x256", frame.shape == (256, 448, 3))
            check(name+"_"+sid+"_grayscale", np.array_equal(frame[..., 0], frame[..., 1]) and np.array_equal(frame[..., 0], frame[..., 2]))
            check(name+"_"+sid+"_nonblank", frame.std() > 1 and np.ptp(frame) > 10)
            if sid not in references:
                references[sid] = png
            check(name+"_"+sid+"_independent_png_bytes", png == references[sid] and sensor.get("byte_parity") is True)
            for flag in ("hardware_rt", "gi", "reflections", "temporal_aa", "screen_percentage", "pp_gi", "pp_reflections"):
                check(name+"_"+sid+"_"+flag+"_off", sensor.get(flag) is False)
    expected = {(m, *f) for m in (0, 3, 1, 2) for f in itertools.product((False, True), repeat=3)}
    check("complete_toggle_cross_product", seen == expected)
    for mode in (1, 2):
        reference = observers[(mode, False, False, False)]
        for flags in itertools.product((False, True), repeat=3):
            check(f"mode_{mode}_{flags}_display_unchanged", np.array_equal(reference, observers[(mode, *flags)]))
    # These demonstrate an actual visible rendering change. Sensor independence
    # is checked above using exact complete PNG bytes, not a similarity score.
    check("lumen_changes_visible_scene", np.mean(np.abs(observers[(3, False, False, False)].astype(float)-observers[(3, False, True, False)])) > .1)
    if any(s.get("dlss_supported") for s in samples):
        check("dlss_changes_visible_sampling", np.mean(np.abs(observers[(3, False, False, False)].astype(float)-observers[(3, True, False, False)])) > .05)
    return {"passed": all(c["passed"] for c in checks), "checks": checks, "dlss_supported": samples[0].get("dlss_supported"), "hardware_rt_supported": samples[0].get("hardware_rt_supported")}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--directory", type=Path, required=True)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    try:
        report = verify(args.directory)
    except (KeyError, ValueError, TypeError, OSError, IndexError) as error:
        report = {"passed": False, "error": str(error), "checks": []}
    output = args.output or args.directory / "observer_graphics_verification.json"
    output.write_text(json.dumps(report, indent=2)+"\n", encoding="utf-8")
    passed = sum(c["passed"] for c in report["checks"])
    print(f"Observer graphics: {passed}/{len(report['checks'])} checks passed; {output}")
    raise SystemExit(0 if report["passed"] else 1)


if __name__ == "__main__":
    main()
