"""Copy one purchased rack H-bar for the gripper without changing source ownership.

The auxiliary export uses a private copy of the complete assignment table. The
90 original components and all source triangles remain unchanged in the cell.
"""
import argparse
import hashlib
import json
from pathlib import Path


def prepare(root: Path):
    mesh = "Car_Body_Welding_Robotics_Cell_robots_blue"
    island_id = 158
    name = "SM_PanelGripperRackBar"
    scan = json.loads((root / "scan/islands.json").read_text(encoding="utf-8"))
    source = next(m for m in scan["meshes"] if m["name"] == mesh)
    island = next(i for i in source["islands"] if i["id"] == island_id)
    if island["triangles"] != 732:
        raise ValueError("Pinned rack H-bar source geometry changed")
    low, high = island["min"], island["max"]
    pivot = [(a + b) / 2 for a, b in zip(low, high)]
    assignments = root / "assignments.tsv"
    before = assignments.read_bytes()
    lines = before.decode("utf-8").splitlines()
    replaced = 0
    for index, line in enumerate(lines):
        fields = line.split()
        if len(fields) == 6 and fields[0] == mesh and fields[1] == str(island_id):
            lines[index] = "\t".join([mesh, str(island_id), name, *[format(v, ".12g") for v in pivot]])
            replaced += 1
    if replaced != 1:
        raise ValueError("Expected exactly one original rack-bar assignment")
    (root / "gripper_bar_assignments.tsv").write_text("\n".join(lines) + "\n", encoding="utf-8")
    (root / "gripper_bar_groups.txt").write_text(name + "\n", encoding="utf-8")
    manifest = {"version": 1, "fbx": "gripper_bar.fbx", "meshes": [{
        "id": name, "fbx_node": name,
        "expected_bounds_min_cm": [a - p for a, p in zip(low, pivot)],
        "expected_bounds_max_cm": [b - p for b, p in zip(high, pivot)]}]}
    (root / "gripper_bar_manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    report = {"schema": "gripper-rack-bar-copy-v1", "source_mesh": mesh,
              "source_island": island_id, "copied_triangles": 732, "source_axis": "Z",
              "source_material": "robots_blue", "source_pivot_cm": pivot,
              "source_size_cm": [b - a for a, b in zip(low, high)],
              "source_assignments_sha256": hashlib.sha256(before).hexdigest(),
              "source_assignments_unchanged": assignments.read_bytes() == before,
              "note": "Auxiliary copy; not a reassignment or an extra original scene component"}
    (root / "gripper_bar_report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return report


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path("assets/derived/welding_cell"))
    print(json.dumps(prepare(parser.parse_args().root), indent=2))
