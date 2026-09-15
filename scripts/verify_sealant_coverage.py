"""Check the three-edge conducted panel heat bands in private rendered R4 captures.

Every sampled panel surface point must resolve to its assigned temperature in at
least one of that run's physical views. Front-only radiometry cannot pass this
check when the body rail hides a side. This is QA, never a live gate override.
"""
import argparse
import json
import math
from pathlib import Path


def verify(manifest):
    from PIL import Image
    from verify_panel_thermal import thermal_targets
    rows = [json.loads(line) for line in manifest.read_text(encoding="utf-8-sig").splitlines() if line.strip()]
    failures, samples = [], []
    for row in rows:
        if row["split"] not in ("train", "validation") or row["stage"] != "adhesive":
            continue
        ids, temperatures = thermal_targets(row)
        errors = [[], []]
        for image_entry, view, bead, temperature in zip(row["images"], row["inspection_views"], ids, temperatures):
            points = view.get("sealant_surface_samples_view_cm", [])
            if len(points) != 39 or any(len(p) != 3 or not all(math.isfinite(v) for v in p) or p[0] <= 0 for p in points):
                failures.append(f"{row['sample_id']}: missing or invalid perimeter samples")
                continue
            with Image.open(manifest.parent / image_entry["path"]) as source:
                image = source.convert("RGB")
            if image.size != (448, 256):
                raise ValueError("Wrong sensor size")
            half_tan = math.tan(math.radians(view["fov_deg"] / 2))
            point_errors = []
            for point in points:
                x = round(224 + point[1] / point[0] / half_tan * 224)
                y = round(128 - point[2] / point[0] / half_tan * 224)
                values = []
                for py in range(y - 1, y + 2):
                    for px in range(x - 1, x + 2):
                        if 0 <= px < 448 and 0 <= py < 256:
                            pixel = image.getpixel((px, py))
                            if max(pixel) - min(pixel) > 1:
                                raise ValueError("Sensor is not grayscale")
                            values.append(20 + 100 * pixel[0] / 255)
                # The gate observes metal, with the same 3x3 footprint and
                # tolerance as contact-center QA. Hot cases establish visibility.
                apparent = max(values) if values else None
                point_errors.append(abs(apparent - min(120, temperature)) if apparent is not None else 1000)
            errors[bead].append(point_errors)
        for bead, views in enumerate(errors):
            if len(views) != (2 if bead == 0 else 1):
                failures.append(f"{row['sample_id']}: incomplete physical views for run {bead}")
                continue
            best = [min(values) for values in zip(*views)]
            bad = [index for index, error in enumerate(best) if error > 2.5]
            if bad:
                failures.append(f"{row['sample_id']}: run {bead} occluded or radiometrically incorrect at samples {bad}")
            samples.append(dict(sample=row["sample_id"], run=bead, points=39, maximum_error_c=max(best), failed_points=bad))
    return dict(sampling="Panel contact maximum, 3x3 projected footprint; near-ambient agreement alone cannot establish visibility", passed=bool(samples) and not failures, runs=len(samples), points=39 * len(samples), failures=failures, samples=samples)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    result = verify(args.manifest)
    args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({key:value for key,value in result.items() if key != "samples"}, indent=2))
    raise SystemExit(0 if result["passed"] else 1)
