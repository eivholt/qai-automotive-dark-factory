"""Audit panel thermal contact pixels against private offline surface labels.

Uses projected physical ROI centers, never whole-frame maxima (robots can be warm).
A failed sample is an occlusion/radiometry problem, not permission to relabel it.
"""
import argparse
import json
import math
from pathlib import Path


CONTRACT = "automotive-panel-process-v3"
THERMAL_RECIPE = "panel-three-edge-contact-transfer-v8"


def thermal_targets(row):
    """Return explicit physical target IDs/temperatures; view count is not bead count."""
    if row.get("contract_version") != CONTRACT or row.get("thermal_recipe") != THERMAL_RECIPE:
        raise ValueError("Wrong panel-contact thermal contract or recipe")
    truth = row["ground_truth"]
    if truth.get("weld_heat_recipe") != THERMAL_RECIPE:
        raise ValueError("Wrong combined surface recipe")
    if truth.get("sealant_thermal_target") != "conducted-panel-surface":
        raise ValueError("Thermal gate must inspect the opaque panel contact surface")
    beads, welds = truth["adhesive"], truth["welds"]
    if not isinstance(beads, list) or len(beads) != 2 or [b.get("index") for b in beads] != [0, 1]:
        raise ValueError("Exactly two physical adhesive beads with IDs0/1 are required")
    if not isinstance(welds, list) or len(welds) != 6:
        raise ValueError("Exactly six physical weld contacts are required")
    bead_map = truth.get("adhesive_view_beads")
    if not isinstance(bead_map, list) or bead_map != [0, 1, 0] or any(type(x) is not int for x in bead_map):
        raise ValueError("Adhesive camera targets must be exactly [0,1,0]")
    if row["stage"] == "adhesive":
        ids, values = bead_map, beads
    elif row["stage"] == "weld":
        if truth.get("weld_view_contacts") != [[0,1],[2,3],[4,5]]:
            raise ValueError("Each physical view must cover its two assigned welds")
        ids, values = list(range(6)), welds
    else:
        raise ValueError("A thermal stage is required")
    targets = [values[index]["apparent_c"] for index in ids]
    if any(type(t) not in (int, float) or not math.isfinite(t) or t < 20 for t in targets):
        raise ValueError("Invalid apparent contact temperature")
    if len(row["images"]) != 3 or len(row["inspection_views"]) != 3:
        raise ValueError("Exactly three images and physical views are required")
    if [im["camera_id"] for im in row["images"]] != ["spot1", "spot2", "spot3"]:
        raise ValueError("Wrong camera order")
    if [v["spot"] for v in row["inspection_views"]] != [0, 1, 2]:
        raise ValueError("Wrong physical view order")
    return ids, targets


def verify(path, splits=("train", "validation")):
    from PIL import Image
    rows = [json.loads(line) for line in path.read_text(encoding="utf-8-sig").splitlines() if line.strip()]
    samples = []
    failures = []
    for row in rows:
        if row["split"] not in splits:
            continue
        stage = row["stage"]
        if stage not in ("adhesive", "weld"):
            continue
        observed = []
        try:
            target_ids, targets = thermal_targets(row)
        except (KeyError, TypeError, ValueError, IndexError) as exc:
            failures.append(f"{row.get('sample_id', '<unknown>')}: {exc}")
            continue
        for index, (entry, view) in enumerate(zip(row["images"], row["inspection_views"])):
            points = view["framing_corners_view_cm"]
            image = Image.open(path.parent / entry["path"]).convert("RGB")
            width, height = image.size
            half_tan = math.tan(math.radians(view["fov_deg"] / 2))
            # Alternating bounds identify both physical welds in a paired view;
            # adhesive retains its strip bounds in the odd entries.
            for part in range(2 if stage == "weld" else 1):
                selected = points[part::2] if stage == "weld" else points[0::2]
                center = [sum(p[axis] for p in selected)/len(selected) for axis in range(3)]
                x = width/2 + center[1]/center[0]/half_tan*width/2
                y = height/2 - center[2]/center[0]/half_tan*width/2
                values = []
                # Contact heat uses a 3x3 center on the metal band. Weld
                # fields retain their wider peak-sampling region.
                radius = 2 if stage == "weld" else 1
                for py in range(round(y)-radius, round(y)+radius+1):
                    for px in range(round(x)-radius, round(x)+radius+1):
                        if 0 <= px < width and 0 <= py < height:
                            pixel = image.getpixel((px, py))
                            if max(pixel)-min(pixel) > 1:
                                failures.append(f"non-grayscale {entry['path']}")
                            values.append(20+100*pixel[0]/255)
                apparent = max(values) if values else None
                target_index = 2*index+part if stage == "weld" else index
                target = targets[target_index]
                error = abs(apparent-min(120, target)) if apparent is not None else float("inf")
                good = error <= 2.5
                if not good:
                    failures.append(f"{entry['path']}: contact {target_ids[target_index]} center {apparent} C vs {target:.2f} C")
                samples.append(dict(image=entry["path"], stage=stage, view=index, contact=target_ids[target_index], pixel_center=[x,y],
                                    apparent_c=apparent, target_c=target, error_c=error, passed=good))
                observed.append(apparent if apparent is not None else 120)
        maximum = max(observed)
        threshold = 45 if stage == "adhesive" else 55
        label = "G" if maximum < threshold-3 else "R" if maximum > threshold+3 else "A"
        if label != row["expected_decision"]:
            failures.append(f"{row['sample_id']}: rendered maximum {maximum:.2f} C implies {label}, label {row['expected_decision']}")
    return dict(passed=bool(samples) and not failures, samples=len(samples), failures=failures, contacts=samples)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--manifest", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--split", nargs="+", choices=("train", "validation", "test"), default=["train", "validation"])
    a = p.parse_args()
    result = verify(a.manifest, a.split)
    a.output.write_text(json.dumps(result, indent=2)+"\n", encoding="utf-8")
    print(json.dumps({k:v for k,v in result.items() if k != "contacts"}, indent=2))
    return 0 if result["passed"] else 1

if __name__ == "__main__":
    raise SystemExit(main())
