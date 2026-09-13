#!/usr/bin/env python3
"""Reconstruct a conservative visual rig from the local DFMESH01 robot scan.

Requires NumPy. No mesh coordinates or triangle payloads are embedded here. The
vertex-count role hints are specific to this example asset, not a generic robot
recognition algorithm. Circular planar boundaries supply axes/centres; rigid
least-squares correspondences transfer each link independently between copies.
A tiny ring-fit residual measures circularity, NOT physical joint accuracy.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct
import numpy as np

ROLE_VERTEX_COUNTS = {
    "fixed_base": 4966, "shoulder": 6242, "upper_arm": 3017, "elbow": 4242,
    "forearm": 1910, "wrist": 1166, "flange": 773, "tool": 9987,
}
TRIANGLE = np.dtype([("indices", "<u4", 3), ("island", "<u4"),
                     ("normals", "<f4", (3, 3)), ("uv", "<f4", (3, 2))])


def unit(value):
    result = np.asarray(value, dtype=float)
    length = np.linalg.norm(result)
    if not np.isfinite(length) or length < 1e-10:
        raise ValueError("Cannot normalize a degenerate joint axis")
    return result / length


class Geometry:
    def __init__(self, path):
        data = path.read_bytes()
        if len(data) < 16 or data[:8] != b"DFMESH01":
            raise ValueError("Not a DFMESH01 scan")
        nv, nt = struct.unpack_from("<II", data, 8)
        if len(data) != 16 + nv * 12 + nt * TRIANGLE.itemsize:
            raise ValueError("Mesh scan size does not match its header")
        self.vertices = np.frombuffer(data, "<f4", nv * 3, 16).reshape(-1, 3).astype(float)
        self.triangles = np.frombuffer(data, TRIANGLE, nt, 16 + nv * 12)
        if not np.isfinite(self.vertices).all() or self.triangles["indices"].max() >= nv:
            raise ValueError("Invalid vertex coordinates or triangle indices")
        self.sha256 = hashlib.sha256(data).hexdigest()
        self.cache = {}

    def island(self, island_id):
        if island_id not in self.cache:
            triangles = self.triangles[self.triangles["island"] == island_id]["indices"]
            points = self.vertices[np.unique(triangles)]
            faces = self.vertices[triangles]
            cross = np.cross(faces[:, 1] - faces[:, 0], faces[:, 2] - faces[:, 0])
            area = np.linalg.norm(cross, axis=1) * .5
            good = area > 1e-7
            if len(points) < 3 or not good.any():
                raise ValueError(f"No usable geometry in island {island_id}")
            self.cache[island_id] = {
                "points": points, "triangles": triangles[good], "faces": faces[good],
                "area": area[good], "normals": cross[good] / (2 * area[good, None]),
            }
        return self.cache[island_id]


def dominant_axis(mesh, guide=None, horizontal=False):
    normals = mesh["normals"].copy()
    if guide is not None:
        guide = unit(guide)
        normals *= np.where(normals @ guide < 0, -1, 1)[:, None]
    else:
        largest = np.argmax(np.abs(normals), axis=1)
        normals *= np.where(normals[np.arange(len(normals)), largest] < 0, -1, 1)[:, None]
    bins, inverse = np.unique(np.round(normals, 4), axis=0, return_inverse=True)
    weights = np.bincount(inverse, weights=mesh["area"])
    if guide is not None:
        weights[bins @ guide < .98] = 0
    if horizontal:
        weights[np.abs(bins[:, 2]) > .25] = 0
    if weights.max() <= 0:
        raise ValueError("No dominant planar axis matches the selected joint")
    selected = inverse == np.argmax(weights)
    return unit((normals[selected] * mesh["area"][selected, None]).sum(axis=0))


def circular_caps(geometry, island_id, axis):
    """Fit cap boundary circles, retaining substantial almost-circular contours.

    An axis is a LINE: the selected cap plane determines the reported point along
    that line but does not affect rotation about it. Holes and mixed boundaries
    usually fail the residual check; they are not silently treated as cylinders.
    """
    mesh = geometry.island(island_id)
    axis = unit(axis)
    u = unit(np.cross(axis, [0, 1, 0] if abs(axis[2]) > .9 else [0, 0, 1]))
    w = np.cross(axis, u)
    parallel = np.abs(mesh["normals"] @ axis) > 1 - 1e-7
    offsets = mesh["faces"].mean(axis=1) @ axis
    planes = np.round(offsets, 2)
    results = []
    for plane in np.unique(planes[parallel]):
        mask = parallel & (planes == plane)
        area = mesh["area"][mask].sum()
        if area < 30:
            continue
        triangles = mesh["triangles"][mask]
        edges = np.sort(np.concatenate([triangles[:, [0, 1]], triangles[:, [1, 2]],
                                        triangles[:, [2, 0]]]), axis=1)
        unique, counts = np.unique(edges, axis=0, return_counts=True)
        points = geometry.vertices[np.unique(unique[counts == 1])]
        if len(points) < 12:
            continue
        xy = np.column_stack([points @ u, points @ w])
        fit = np.linalg.lstsq(np.column_stack([2 * xy, np.ones(len(xy))]),
                              (xy * xy).sum(axis=1), rcond=None)[0]
        center = fit[:2]
        radius = np.sqrt(max(0, fit[2] + center @ center))
        error = np.linalg.norm(xy - center, axis=1) - radius
        rms = np.sqrt(np.mean(error * error))
        if radius < 5 or rms > .02:
            continue
        pivot = center[0] * u + center[1] * w + np.mean(points @ axis) * axis
        results.append({"pivot": pivot, "axis": axis, "radius": float(radius),
                        "rms": float(rms), "area": float(area), "points": len(points)})
    if not results:
        raise ValueError(f"No reliable circular cap found in island {island_id}")
    return results


def largest_cap(geometry, island_id, axis):
    return max(circular_caps(geometry, island_id, axis), key=lambda value: value["area"])


def rigid_correspondence(source, destination):
    if source.shape != destination.shape:
        raise ValueError("Copied-link vertex counts do not match")
    a, b = source - source.mean(axis=0), destination - destination.mean(axis=0)
    u, _, vt = np.linalg.svd(a.T @ b)
    rotation = vt.T @ u.T
    if np.linalg.det(rotation) < 0:
        vt[-1] *= -1
        rotation = vt.T @ u.T
    translation = destination.mean(axis=0) - rotation @ source.mean(axis=0)
    error = np.linalg.norm(source @ rotation.T + translation - destination, axis=1)
    rms = float(np.sqrt(np.mean(error * error)))
    if rms > .01:
        raise ValueError(f"Copied-link vertex correspondence is not rigid: RMS {rms:.5f} cm")
    return rotation, translation, rms


def reconstruct(scan, output):
    document = json.loads((scan / "islands.json").read_text(encoding="utf-8"))
    meshes = [mesh for mesh in document["meshes"] if mesh["name"].endswith("_robots")]
    if len(meshes) != 1:
        raise ValueError("Expected one robot-paint mesh in the example scan")
    metadata = meshes[0]
    geometry = Geometry(scan / (metadata["name"] + ".meshbin"))
    role_islands = {}
    for role, count in ROLE_VERTEX_COUNTS.items():
        role_islands[role] = [int(item["id"]) for item in metadata["islands"]
                              if item["vertices"] == count]
        if len(role_islands[role]) != 4:
            raise ValueError(f"Expected four replicated {role} islands")
    robots = []
    for shoulder in role_islands["shoulder"]:
        center = geometry.island(shoulder)["points"].mean(axis=0)
        robot_id = "robot_" + ("px" if center[0] > 0 else "nx") + "_" + ("py" if center[1] > 0 else "ny")
        anchors = {}
        for role, candidates in role_islands.items():
            anchors[role] = min(candidates, key=lambda candidate:
                np.linalg.norm(geometry.island(candidate)["points"].mean(axis=0) - center))
        robots.append({"id": robot_id, "anchors": anchors})
    if len({robot["id"] for robot in robots}) != 4:
        raise ValueError("Robot instances do not occupy four distinct quadrants")
    for role in ROLE_VERTEX_COUNTS:
        if len({robot["anchors"][role] for robot in robots}) != 4:
            raise ValueError(f"Nearest-component assignment is ambiguous for {role}")
    reference = next(robot for robot in robots if robot["id"] == "robot_px_py")
    anchors = reference["anchors"]
    upper_axis = dominant_axis(geometry.island(anchors["upper_arm"]))
    shoulder_axis = dominant_axis(geometry.island(anchors["shoulder"]), horizontal=True)
    forearm = geometry.island(anchors["forearm"])["points"]
    _, eig = np.linalg.eigh(np.cov(forearm.T))
    forearm_guide = eig[:, -1]
    if forearm_guide @ (geometry.island(anchors["wrist"])["points"].mean(axis=0) - forearm.mean(axis=0)) < 0:
        forearm_guide *= -1
    forearm_axis = dominant_axis(geometry.island(anchors["forearm"]), forearm_guide)
    wrist_axis = dominant_axis(geometry.island(anchors["wrist"]), upper_axis)
    _, flange_eig = np.linalg.eigh(np.cov(geometry.island(anchors["flange"])["points"].T))
    flange_guide = flange_eig[:, 0]
    if flange_guide[2] < 0:
        flange_guide *= -1
    flange_axis = dominant_axis(geometry.island(anchors["flange"]), flange_guide)
    specifications = {
        "base": ("fixed_base", np.array([0., 0., 1.]), 3.),
        "shoulder": ("shoulder", shoulder_axis, 1.),
        "elbow": ("elbow", upper_axis, 1.5),
        "forearm_roll": ("forearm", forearm_axis, 2.),
        "wrist": ("wrist", wrist_axis, 4.),
        "tool_roll": ("flange", flange_axis, 8.),
    }
    fitted = {name: largest_cap(geometry, anchors[role], axis)
              for name, (role, axis, _) in specifications.items()}
    # The two independently baked shoulder pieces need not be perfectly concentric.
    # Report their mesh discrepancy rather than imply the ring-fit residual proves a rig.
    lower_caps = circular_caps(geometry, anchors["upper_arm"], upper_axis)
    lower_cap = min((cap for cap in lower_caps if cap["radius"] > 15), key=lambda cap: cap["pivot"][2])
    offset = lower_cap["pivot"] - fitted["shoulder"]["pivot"]
    shoulder_seam = float(np.linalg.norm(offset - (offset @ shoulder_axis) * shoulder_axis))
    for robot in robots:
        robot["joints"] = {}
        for name, (role, _, amplitude) in specifications.items():
            fit = fitted[name]
            source_id, target_id = anchors[role], robot["anchors"][role]
            rotation, translation, rms = rigid_correspondence(
                geometry.island(source_id)["points"], geometry.island(target_id)["points"])
            robot["joints"][name] = {
                "pivot_cm": np.round(rotation @ fit["pivot"] + translation, 6).tolist(),
                "axis": np.round(unit(rotation @ fit["axis"]), 8).tolist(),
                "min_deg": -amplitude, "max_deg": amplitude,
                "fit": {"method": "planar_boundary_circle_and_per_link_rigid_transfer",
                        "source_island": target_id, "reference_island": source_id,
                        "radius_cm": round(fit["radius"], 6), "radial_rms_cm": round(fit["rms"], 8),
                        "boundary_points": fit["points"], "cap_area_cm2": round(fit["area"], 5),
                        "replica_vertex_rms_cm": round(rms, 8)},
            }
    robots.sort(key=lambda robot: ["robot_px_py", "robot_nx_py", "robot_px_ny", "robot_nx_ny"].index(robot["id"]))
    result = {
        "version": 1, "coordinates": "Unreal centimetres; world rest pose",
        "mesh": metadata["name"], "mesh_sha256": geometry.sha256,
        "islands_sha256": hashlib.sha256((scan / "islands.json").read_bytes()).hexdigest(),
        "method": "Example-specific role vertex counts; fitted circular caps; independently verified rigid link copies",
        "reference_robot": reference["id"],
        "reference_shoulder_mating_cap_offset_from_axis_cm": round(shoulder_seam, 6),
        "limitations": [
            "Approximate visual rig only; no manufacturer kinematics, calibrated pivots, IK, collision or process guarantees.",
            "Ring residuals measure geometric circularity and copy correspondence, not physical joint accuracy.",
            "Source pieces are separately baked; shoulder cap mismatch is retained rather than geometry being silently changed.",
            "Axes and pivots are world rest coordinates; runtime must convert them into each parent joint frame.",
            "Use small authored joint angles; rigid hoses/dress packs do not articulate or simulate flexible cables.",
            "Generated rig metadata and all input meshes remain local assets; publish code examples only.",
        ],
        "robots": robots,
    }
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(result, indent=2, allow_nan=False) + "\n", encoding="utf-8", newline="\n")
    print(json.dumps({"output": str(output), "robots": len(robots), "joints": len(robots) * len(specifications),
                      "maximum_ring_rms_cm": max(cap["rms"] for cap in fitted.values()),
                      "shoulder_mating_offset_cm": shoulder_seam}, indent=2))


def main():
    root = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--scan", type=Path, help="Input scan directory; defaults to OUTPUT/scan")
    parser.add_argument("--output", type=Path, default=root / "assets/derived/welding_cell",
                        help="Derived asset directory containing scan/; writes robot_joints.json")
    args = parser.parse_args()
    output = args.output if args.output.suffix.lower() == ".json" else args.output / "robot_joints.json"
    reconstruct(args.scan or output.parent / "scan", output)


if __name__ == "__main__":
    main()
