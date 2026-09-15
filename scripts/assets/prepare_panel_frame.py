"""Cut a permanent mounting ring from a private copy of the source trunk panel.

Original island ownership, FBX files and the replacement panel stay untouched.
The binary contains local positions, source normals and UVs; Unreal builds one
static GPU mesh at startup. Nothing is remeshed during the production cycle.
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path

import numpy as np
from prepare_welding_cell import read_mesh


APERTURE = (-90., 93., -322., -246.)  # xmin, xmax, ymin, ymax in source cm


def split_polygon(poly, axis, plane, direction):
    """Split attribute-bearing vertices at a half-space, interpolating UV/normal."""
    inside, outside = [], []
    for a, b in zip(poly, np.roll(poly, -1, axis=0)):
        da, db = direction * (a[axis]-plane), direction * (b[axis]-plane)
        (inside if da >= 0 else outside).append(a)
        if (da >= 0) != (db >= 0):
            p = a + (b-a) * da/(da-db)
            inside.append(p)
            outside.append(p)
    return np.asarray(inside), np.asarray(outside)


def prepare(root):
    source = root/'scan/Car_Body_Welding_Robotics_Cell_chrome.meshbin'
    v, triangles, _ = read_mesh(source)
    triangles = triangles[triangles['island'] == 386]
    positions = v[triangles['v']].astype(float)
    origin = (positions.min((0, 1))+positions.max((0, 1)))/2
    source_corners = np.concatenate((positions, triangles['normals'], triangles['uv']), axis=2)
    x0, x1, y0, y1 = APERTURE
    planes = ((0, x0, 1), (0, x1, -1), (1, y0, 1), (1, y1, -1))
    result = []
    removed_area = 0.

    def area(p):
        return np.linalg.norm(np.cross(p[1, :3]-p[0, :3], p[2, :3]-p[0, :3]))/2

    for tri in source_corners:
        poly = tri
        for axis, plane, direction in planes:
            if len(poly) < 3:
                break
            poly, retained = split_polygon(poly, axis, plane, direction)
            for k in range(1, len(retained)-1):
                piece = retained[[0, k, k+1]]
                if area(piece) > 1.e-8:
                    result.append(piece)
        for k in range(1, len(poly)-1):
            removed_area += area(poly[[0, k, k+1]])
    corners = np.asarray(result)
    original_area = sum(area(t) for t in source_corners)
    retained_area = sum(area(t) for t in corners)
    assert abs(original_area-retained_area-removed_area) < 1.e-5
    assert 1000 < len(corners) < 100000 and np.isfinite(corners).all()
    # Every existing sealant sample must still have actual metal immediately below it.
    layout = json.loads((root/'panel_assembly.json').read_text())
    pp = corners[:, :, :3]
    a, u, w = pp[:, 0], pp[:, 1]-pp[:, 0], pp[:, 2]-pp[:, 0]
    den = u[:, 0]*w[:, 1]-u[:, 1]*w[:, 0]
    safe = np.where(abs(den) > 1.e-8, den, 1)
    errors = []
    for path in layout['adhesive_paths']:
        for x, y, z in path['points_cm']:
            b = ((x-a[:, 0])*w[:, 1]-(y-a[:, 1])*w[:, 0])/safe
            c = (u[:, 0]*(y-a[:, 1])-u[:, 1]*(x-a[:, 0]))/safe
            hit = (abs(den) > 1.e-8) & (b >= -1.e-6) & (c >= -1.e-6) & (b+c <= 1.000001)
            assert hit.any(), f'Unsupported sealant at {x}, {y}'
            errors.append(float(abs((a[:, 2]+b*u[:, 2]+c*w[:, 2])[hit].max()-z)))
    assert max(errors) < .002
    corners[:, :, :3] -= origin
    corners[:, :, 3:6] /= np.linalg.norm(corners[:, :, 3:6], axis=2, keepdims=True)
    data = struct.pack('<8sI3d', b'DFFRAME1', len(corners), *origin) + corners.astype('<f4').tobytes()
    (root/'panel_frame.meshbin').write_bytes(data)
    report = dict(recipe='source-panel-open-frame-v1', source_island=386,
                  source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
                  source_fbx=json.loads((root/'scan/source.json').read_text()),
                  derived_sha256=hashlib.sha256(data).hexdigest(),
                  aperture_cm=list(APERTURE), origin_cm=origin.tolist(),
                  source_triangles=len(triangles), frame_triangles=len(corners),
                  original_surface_area_cm2=original_area, retained_surface_area_cm2=retained_area,
                  removed_surface_area_cm2=removed_area, support_samples=len(errors),
                  max_sealant_support_gap_cm=max(errors), source_ownership_unchanged=True)
    (root/'panel_frame.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path('assets/derived/welding_cell'))
    prepare(parser.parse_args().root)
