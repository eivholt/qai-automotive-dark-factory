#!/usr/bin/env python3
"""Verify rendered contact/radiative heat-transfer smoke exports with Pillow.

Run this alongside the separate legacy thermal/field verifiers with transfer
turned off in those baseline captures. This report checks a coarse simulation;
it does not establish calibrated thermography or thermal-model accuracy.
"""
from __future__ import annotations

import argparse
import hashlib
import io
import json
import math
from pathlib import Path
import statistics
import sys

try:
    from PIL import Image, ImageChops
except ImportError:
    raise SystemExit("Pillow is required; use an existing Pillow environment (see docs/thermal_qa.md).")

SIZE = (448, 256)
STATES = ("initial", "near", "far", "blocked", "contact", "gap", "moved", "cooled")
FACES = ("+X", "-X", "+Y", "-Y", "+Z", "-Z")
RECEIVER = "HeatTransferReceiver"
SOURCE = "HeatTransferSource"
BLOCKER = "HeatTransferBlocker"
AMBIENT_C = 24.0
AMBIENT_U8 = (AMBIENT_C - 20.0) * 255.0 / 100.0
THRESHOLDS = {
    "minimum_visible_receiver_pixels": 16,
    "minimum_visible_face_pixels": 3,
    "initial_ambient_mean_tolerance_u8": 1.0,
    "near_top20_warming_u8_min": 3.0,
    "radiation_control_top20_difference_u8_min": 2.0,
    "contact_gap_top20_difference_u8_min": 2.0,
    "moved_same_pixel_absolute_difference_u8_max": 1.0,
    "cooldown_top20_drop_u8_min": 1.0,
    "near_facing_warming_c_min": 1.0,
    "facing_away_warming_difference_c_min": 0.5,
    "radiation_control_facing_difference_c_min": 0.25,
    "contact_gap_facing_difference_c_min": 0.5,
    "moved_physical_temperature_drift_c_max": 1e-6,
    "cooldown_facing_drop_c_min": 0.25,
    "energy_absolute_residual_j_max": 0.001,
    "energy_relative_to_supplied_residual_max": 1e-8,
}


def finite(value):
    return type(value) in (int, float) and math.isfinite(value)


def number(value, label):
    if not finite(value):
        raise ValueError(f"{label} must be a finite number")
    return float(value)


def integer(value):
    return finite(value) and value == int(value)


def reject_constant(value):
    raise ValueError(f"Nonfinite JSON number {value}")


def load_gray(path):
    content = path.read_bytes()
    with Image.open(io.BytesIO(content)) as image:
        image.load()
        if image.format != "PNG" or image.size != SIZE or image.mode not in ("L", "RGB", "RGBA"):
            raise ValueError(f"Expected 8-bit PNG {SIZE}, found {image.format} {image.size} {image.mode}")
        red, green, blue = image.convert("RGB").split()
        if ImageChops.difference(red, green).getbbox() or ImageChops.difference(red, blue).getbbox():
            raise ValueError("Pixels must be exactly grayscale")
        if image.mode == "RGBA" and image.getchannel("A").getextrema() != (255, 255):
            raise ValueError("RGBA PNGs must be opaque")
        return list(red.getdata()), {"sha256": hashlib.sha256(content).hexdigest(), "bytes": len(content), "size": list(SIZE)}


def summary(values):
    ordered = sorted(values)
    count = max(1, math.ceil(len(values) * 0.2))
    mean = statistics.mean(values)
    return {"pixels": len(values), "mean_u8": mean,
            "p95_u8": ordered[max(0, math.ceil(len(values) * .95) - 1)],
            "top20_mean_u8": statistics.mean(ordered[-count:]),
            "min_u8": ordered[0], "max_u8": ordered[-1],
            "apparent_mean_c": 20 + mean * 100 / 255}


def profile_map(metadata, label):
    profiles = metadata.get("profiles")
    if not isinstance(profiles, list):
        raise ValueError(f"{label}.profiles must be an array")
    result, ids = {}, set()
    for profile in profiles:
        if not isinstance(profile, dict) or not isinstance(profile.get("surface"), str):
            raise ValueError(f"{label}.profiles has an invalid surface")
        name, stencil = profile["surface"], profile.get("stencil_id")
        if name in result or not integer(stencil) or not 1 <= stencil <= 255 or stencil in ids:
            raise ValueError(f"{label}.profiles has duplicate names/IDs or an invalid stencil_id")
        ids.add(stencil)
        result[name] = profile
    for name in (RECEIVER, SOURCE, BLOCKER):
        if name not in result:
            raise ValueError(f"{label}.profiles is missing {name}")
    return result


def valid_transform(value):
    if not isinstance(value, dict):
        return False
    for key, size in (("position_cm", 3), ("rotation_xyzw", 4), ("scale_xyz", 3)):
        part = value.get(key)
        if not isinstance(part, list) or len(part) != size or not all(finite(v) for v in part):
            return False
    return (abs(sum(v * v for v in value["rotation_xyzw"]) - 1) <= .001
            and all(abs(v) > 1e-8 for v in value["scale_xyz"]))


def face_temperatures(profile, label):
    bulk = number(profile.get("core_surface_temperature_c"), label + ".core_surface_temperature_c")
    deltas = profile.get("face_delta_temperature_c")
    if not isinstance(deltas, list) or len(deltas) != 6:
        raise ValueError(f"{label}.face_delta_temperature_c must have six values in +X,-X,+Y,-Y,+Z,-Z order")
    return bulk, [bulk + number(value, label + ".face_delta_temperature_c") for value in deltas]


def paired_samples(first, second, images, ids, stencil):
    indices = [i for i, (a, b) in enumerate(zip(ids[first], ids[second])) if a == stencil and b == stencil]
    if len(indices) < THRESHOLDS["minimum_visible_receiver_pixels"]:
        return None
    a, b = [images[first][i] for i in indices], [images[second][i] for i in indices]
    return {"first": summary(a), "second": summary(b),
            "mean_signed_delta_u8": statistics.mean(y - x for x, y in zip(a, b)),
            "mean_absolute_delta_u8": statistics.mean(abs(y - x) for x, y in zip(a, b)),
            "max_absolute_delta_u8": max(abs(y - x) for x, y in zip(a, b))}


def rotate(vector, quaternion):
    # Unit-quaternion rotation; camera/profile validation checks the input norm.
    x, y, z, w = quaternion
    vx, vy, vz = vector
    tx, ty, tz = 2 * (y * vz - z * vy), 2 * (z * vx - x * vz), 2 * (x * vy - y * vx)
    return [vx + w * tx + y * tz - z * ty,
            vy + w * ty + z * tx - x * tz,
            vz + w * tz + x * ty - y * tx]


def face_is_front_facing(metadata, profile, face):
    transform, camera = profile.get("world_transform"), metadata.get("camera_world_transform")
    if not valid_transform(transform) or not valid_transform(camera):
        raise ValueError("invalid receiver/camera transform for face visibility")
    center, extent = profile.get("local_bounds_center"), profile.get("local_bounds_extent")
    if any(not isinstance(v, list) or len(v) != 3 or not all(finite(x) for x in v) for v in (center, extent)):
        raise ValueError("invalid receiver bounds for face visibility")
    axis, sign = "XYZ".index(face[1]), 1 if face[0] == "+" else -1
    local = list(center)
    local[axis] += sign * extent[axis]
    scale = transform["scale_xyz"]
    offset = rotate([v * s for v, s in zip(local, scale)], transform["rotation_xyzw"])
    point = [a + b for a, b in zip(transform["position_cm"], offset)]
    normal = [0, 0, 0]
    normal[axis] = sign if scale[axis] > 0 else -sign
    normal = rotate(normal, transform["rotation_xyzw"])
    return sum(n * (c - p) for n, c, p in zip(normal, camera["position_cm"], point)) > 1e-4


def face_regions(metadata, profile, image, ids, stencil, label, errors):
    qa = metadata.get("transfer_qa_regions", {})
    if not isinstance(qa, dict):
        errors.append(label + ".transfer_qa_regions must be an object")
        return {}
    declarations = qa.get("receiver_faces", {})
    if not isinstance(declarations, dict):
        errors.append(label + ".transfer_qa_regions.receiver_faces must be an object")
        return {}
    result = {}
    for face, declaration in declarations.items():
        try:
            if face not in FACES or not isinstance(declaration, dict):
                raise ValueError("invalid face declaration")
            box = declaration.get("box")
            if not isinstance(box, list) or len(box) != 4 or not all(integer(v) for v in box):
                raise ValueError("box must contain four integers")
            left, top, right, bottom = map(int, box)
            if not (0 <= left < right <= SIZE[0] and 0 <= top < bottom <= SIZE[1]):
                raise ValueError("box is outside the image")
            if declaration.get("stencil_id") != stencil:
                raise ValueError("stencil_id does not match receiver")
            front_facing = face_is_front_facing(metadata, profile, face)
            # A back-face projection can hit the same object's front-face ID;
            # stencil membership alone cannot establish which face is visible.
            values = [image[y * SIZE[0] + x] for y in range(top, bottom) for x in range(left, right)
                      if ids[y * SIZE[0] + x] == stencil] if front_facing else []
            result[face] = {"box": box, "front_facing": front_facing, "visible_pixels": len(values), "usable": len(values) >= THRESHOLDS["minimum_visible_face_pixels"]}
            if result[face]["usable"]:
                result[face].update(summary(values))
        except (ValueError, TypeError) as exc:
            errors.append(f"{label}.receiver_faces.{face}: {exc}")
    return result


def verify(directory):
    report = {"scope": "GPU coarse contact/radiative heat-transfer QA; no thermal classifier or calibrated material validation.",
              "directory": str(directory.resolve()), "thresholds": THRESHOLDS,
              "checks": {}, "files": {}, "states": {}, "comparisons": {}, "errors": []}
    checks, errors = report["checks"], report["errors"]
    try:
        content = (directory / "transfer_states.json").read_bytes()
        document = json.loads(content.decode("utf-8-sig"), parse_constant=reject_constant)
        if not isinstance(document, dict) or not isinstance(document.get("states"), dict):
            raise ValueError("Expected an object with a states map of snapshot metadata")
        metadata = document["states"]
        checks["smoke_reports_passed"] = document.get("passed") is True
        report["metadata_sha256"] = hashlib.sha256(content).hexdigest()
    except (OSError, ValueError) as exc:
        metadata = {}
        errors.append(f"transfer_states.json: {exc}")
    images, masks, profiles, temperatures = {}, {}, {}, {}
    for state in STATES:
        for suffix, destination in (("", images), ("_ids", masks)):
            name = "transfer_" + state + suffix
            try:
                destination[state], report["files"][name] = load_gray(directory / (name + ".png"))
            except (OSError, ValueError) as exc:
                errors.append(f"{name}.png: {exc}")
        snapshot = metadata.get(state)
        if not isinstance(snapshot, dict):
            errors.append(f"Metadata missing or invalid for {state}")
            checks[state + "_metadata_valid"] = False
            continue
        checks[state + "_transfer_enabled"] = snapshot.get("heat_transfer_enabled") is True
        checks[state + "_declared_capture_files_match"] = (snapshot.get("image") == "transfer_" + state + ".png" and snapshot.get("id_image") == "transfer_" + state + "_ids.png")
        checks[state + "_thermal_adapter_unavailable"] = snapshot.get("adapter_ready") is False
        expected_gaps = {"initial": 50, "near": 50, "far": 100, "blocked": 50, "contact": 0, "gap": 2}
        if state in expected_gaps:
            exposure_gap = snapshot.get("exposure_gap_cm", snapshot.get("fixture_gap_cm"))
            checks[state + "_expected_exposure_gap"] = finite(exposure_gap) and abs(exposure_gap - expected_gaps[state]) <= 1e-6
        if state in ("contact", "gap"):
            checks[state + "_donor_retracted_for_inspection"] = snapshot.get("donor_retracted_for_surface_inspection") is True
        if state == "moved":
            checks["moved_is_relocation_after_heating"] = snapshot.get("donor_relocated_after_heating") is True
        checks[state + "_linear_encoding_448x256_20_to_120_c"] = (
            snapshot.get("width") == 448 and snapshot.get("height") == 256
            and snapshot.get("min_temperature_c") == 20 and snapshot.get("max_temperature_c") == 120
            and snapshot.get("ambient_temperature_c") == 24 and snapshot.get("linear_u8") is True
            and snapshot.get("quantity") == "apparent_temperature")
        try:
            profiles[state] = profile_map(snapshot, state)
            bulk, faces = face_temperatures(profiles[state][RECEIVER], state + ".receiver")
            temperatures[state] = faces
            exchange = snapshot.get("heat_exchange")
            if not isinstance(exchange, dict):
                raise ValueError(state + ".heat_exchange must be an object")
            elapsed = number(exchange.get("elapsed_seconds"), state + ".elapsed_seconds")
            supplied = number(exchange.get("supplied_energy_j"), state + ".supplied_energy_j")
            residual = number(exchange.get("energy_residual_j"), state + ".energy_residual_j")
            residual_limit = max(THRESHOLDS["energy_absolute_residual_j_max"], abs(supplied) * THRESHOLDS["energy_relative_to_supplied_residual_max"])
            counts = ("nodes", "links", "contact_links", "radiation_links", "blocked_pairs", "integration_steps")
            checks[state + "_solver_counters_valid"] = all(integer(exchange.get(key)) and exchange[key] >= 0 for key in counts) and exchange.get("nodes", 0) >= 2
            if not checks[state + "_solver_counters_valid"]:
                raise ValueError(state + ".heat_exchange has invalid solver counters")
            checks[state + "_solver_valid"] = exchange.get("valid") is True
            checks[state + "_energy_balanced_including_reservoirs"] = abs(residual) <= residual_limit
            expected_elapsed = 0 if state == "initial" else 1200 if state == "cooled" else 600
            checks[state + "_expected_simulated_duration"] = abs(elapsed - expected_elapsed) <= 1e-6
            checks[state + "_solver_advanced_when_expected"] = state == "initial" or exchange.get("integration_steps", 0) > 0
            checks[state + "_metadata_valid"] = True
            report["states"][state] = {"receiver_bulk_c": bulk, "receiver_face_c": dict(zip(FACES, faces)),
                                      "receiver_emissivity": profiles[state][RECEIVER].get("emissivity"),
                                      "heat_exchange": exchange, "energy_residual_limit_j": residual_limit}
            if state in images and state in masks:
                stencil = int(profiles[state][RECEIVER]["stencil_id"])
                allowed_ids = {0, *(int(p["stencil_id"]) for p in profiles[state].values())}
                checks[state + "_visible_ids_declared_in_profiles"] = set(masks[state]).issubset(allowed_ids)
                values = [v for v, surface_id in zip(images[state], masks[state]) if surface_id == stencil]
                checks[state + "_receiver_visible_pixels"] = len(values) >= THRESHOLDS["minimum_visible_receiver_pixels"]
                report["states"][state]["visible_receiver_pixels"] = len(values)
                if checks[state + "_receiver_visible_pixels"]:
                    report["states"][state]["rendered_receiver"] = summary(values)
                report["states"][state]["visible_face_regions"] = face_regions(snapshot, profiles[state][RECEIVER], images[state], masks[state], stencil, state, errors)
        except (ValueError, TypeError) as exc:
            errors.append(str(exc))
            checks[state + "_metadata_valid"] = False
    checks["all_sixteen_pngs_are_grayscale_and_448x256"] = len(images) == len(masks) == len(STATES)
    checks["all_eight_snapshots_valid"] = all(checks.get(state + "_metadata_valid") is True for state in STATES)

    if len(profiles) == len(STATES):
        receiver_ids = {profiles[state][RECEIVER]["stencil_id"] for state in STATES}
        checks["receiver_id_stable_across_states"] = len(receiver_ids) == 1
        baseline = profiles["initial"][RECEIVER]
        transforms = [profiles[state][RECEIVER].get("world_transform") for state in STATES]
        checks["receiver_geometry_stays_fixed"] = all(valid_transform(t) and t == transforms[0] for t in transforms)
        checks["receiver_bounds_stay_fixed"] = all(
            isinstance(baseline.get(key), list) and len(baseline[key]) == 3
            and all(finite(v) for v in baseline[key])
            and all(profiles[state][RECEIVER].get(key) == baseline[key] for state in STATES)
            for key in ("local_bounds_center", "local_bounds_extent"))
        cameras = [metadata[state].get("camera_world_transform") for state in STATES]
        checks["camera_stays_fixed"] = all(valid_transform(t) and t == cameras[0] for t in cameras)
        fovs = [metadata[state].get("camera_horizontal_fov_deg") for state in STATES]
        checks["camera_fov_stays_fixed"] = finite(fovs[0]) and 0 < fovs[0] < 180 and all(fov == fovs[0] for fov in fovs)
        checks["receiver_emissivity_stays_fixed"] = finite(baseline.get("emissivity")) and all(profiles[state][RECEIVER].get("emissivity") == baseline["emissivity"] for state in STATES)
        checks["donor_actually_moves_after_near"] = profiles["near"][SOURCE].get("world_transform") != profiles["moved"][SOURCE].get("world_transform")
        checks["donor_hidden_during_cooldown"] = profiles["cooled"][SOURCE].get("component_visible") is False
        checks["blocker_present_only_in_blocked_case"] = (profiles["blocked"][BLOCKER].get("component_visible") is True
            and all(profiles[state][BLOCKER].get("component_visible") is False for state in STATES if state != "blocked"))
        if len(receiver_ids) == 1:
            stencil = int(next(iter(receiver_ids)))
            pairs = (("initial", "near"), ("far", "near"), ("blocked", "near"), ("gap", "contact"), ("near", "moved"), ("cooled", "moved"))
            for first, second in pairs:
                name = first + "_to_" + second
                if first not in images or second not in images or first not in masks or second not in masks:
                    continue
                measured = paired_samples(first, second, images, masks, stencil)
                checks[name + "_common_receiver_pixels"] = measured is not None
                if measured is None:
                    continue
                report["comparisons"][name] = measured
                top_delta = measured["second"]["top20_mean_u8"] - measured["first"]["top20_mean_u8"]
                measured["top20_signed_delta_u8"] = top_delta
                if first == "initial":
                    checks["initial_receiver_render_matches_ambient"] = abs(measured["first"]["mean_u8"] - AMBIENT_U8) <= THRESHOLDS["initial_ambient_mean_tolerance_u8"]
                    checks["near_receiver_visibly_warms"] = top_delta >= THRESHOLDS["near_top20_warming_u8_min"]
                elif first in ("far", "blocked"):
                    checks["near_visibly_warmer_than_" + first] = top_delta >= THRESHOLDS["radiation_control_top20_difference_u8_min"]
                elif first == "gap":
                    checks["contact_visibly_warmer_than_gap"] = top_delta >= THRESHOLDS["contact_gap_top20_difference_u8_min"]
                elif first == "near":
                    checks["stored_rendered_heat_persists_after_donor_moves"] = measured["max_absolute_delta_u8"] <= THRESHOLDS["moved_same_pixel_absolute_difference_u8_max"]
                elif first == "cooled":
                    checks["receiver_visibly_cools_after_donor_removed"] = top_delta >= THRESHOLDS["cooldown_top20_drop_u8_min"]

    if len(temperatures) == len(STATES):
        # Fixture contract: donor lies toward receiver-local -Y; +Y is opposite.
        facing, away = FACES.index("-Y"), FACES.index("+Y")
        initial = temperatures["initial"]
        checks["initial_receiver_physical_faces_at_ambient"] = max(abs(t - AMBIENT_C) for t in initial) <= 1e-6
        warming = [t - base for t, base in zip(temperatures["near"], initial)]
        report["physical_near_warming_c"] = dict(zip(FACES, warming))
        checks["near_facing_surface_physically_warms"] = warming[facing] >= THRESHOLDS["near_facing_warming_c_min"]
        checks["donor_facing_surface_warms_more_than_away"] = warming[facing] - warming[away] >= THRESHOLDS["facing_away_warming_difference_c_min"]
        for state in ("far", "blocked"):
            checks["near_facing_physically_warmer_than_" + state] = temperatures["near"][facing] - temperatures[state][facing] >= THRESHOLDS["radiation_control_facing_difference_c_min"]
        checks["contact_facing_physically_warmer_than_gap"] = temperatures["contact"][facing] - temperatures["gap"][facing] >= THRESHOLDS["contact_gap_facing_difference_c_min"]
        checks["stored_physical_heat_persists_after_donor_moves"] = max(abs(a - b) for a, b in zip(temperatures["near"], temperatures["moved"])) <= THRESHOLDS["moved_physical_temperature_drift_c_max"]
        checks["receiver_facing_physically_cools_after_donor_removed"] = temperatures["moved"][facing] - temperatures["cooled"][facing] >= THRESHOLDS["cooldown_facing_drop_c_min"]
    report["face_sampling_note"] = "Projected face boxes are diagnostic and stencil-filtered. A physically occluded contact face is not required to be visible. Acceptance uses shared visible receiver pixels and independent physical -Y/+Y face temperatures."
    report["passed"] = bool(checks) and not errors and all(checks.values())
    report["failed_checks"] = [name for name, passed in checks.items() if not passed]
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
