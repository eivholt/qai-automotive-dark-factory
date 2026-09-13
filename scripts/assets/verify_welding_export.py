"""Verify an SDK-reimported prepared FBX against its original source scan.

Requires NumPy. Run fbx_prepare scan on the exported FBX before this command.
This checks geometry; Unreal package, material-slot and runtime audits are separate.
"""
import argparse
import hashlib
import json
from pathlib import Path

import numpy as np
from prepare_welding_cell import read_mesh


def load(path):
    return json.loads(path.read_text(encoding='utf-8'))


def verify(root, prepared_scan, manifest_path, source):
    scan = load(root / 'scan/islands.json')
    groups = {g['id']: g for g in load(root / 'grouping_report.json')['groups']}
    manifest = load(manifest_path)
    exported = {m['name']: m for m in load(prepared_scan / 'islands.json')['meshes']}
    identifiers = [m['id'] for m in manifest['meshes']]
    if len(identifiers) != len(set(identifiers)) or set(identifiers) != set(exported):
        raise ValueError('Exported node set must exactly match the selected manifest')
    cache = {m['name']: read_mesh(root / 'scan' / (m['name'] + '.meshbin'))[:2]
             for m in scan['meshes']}
    rows = []
    for expected in manifest['meshes']:
        identifier = expected['id']
        group = groups[identifier]
        points, triangles, _ = read_mesh(prepared_scan / (identifier + '.meshbin'))
        positions, normals, uvs = [], [], []
        for mesh in scan['meshes']:
            name = mesh['name']
            members = [i['island'] for i in group['islands'] if i['mesh'] == name]
            if not members:
                continue
            source_points, source_triangles = cache[name]
            selected = source_triangles[np.isin(source_triangles['island'], members)]
            positions.append(source_points[selected['v']] - np.asarray(group['pivot_cm'], dtype=np.float32))
            normals.append(selected['normals'])
            uvs.append(selected['uv'])
        expected_positions = np.concatenate(positions)
        if len(triangles) != len(expected_positions) or len(triangles) != group['triangles']:
            raise ValueError('Triangle count changed: ' + identifier)
        position_error = float(np.abs(points[triangles['v']] - expected_positions).max())
        normal_error = float(np.abs(triangles['normals'] - np.concatenate(normals)).max())
        uv_error = float(np.abs(triangles['uv'] - np.concatenate(uvs)).max())
        bounds_error = float(max(np.abs(points.min(axis=0) - expected['expected_bounds_min_cm']).max(),
                                 np.abs(points.max(axis=0) - expected['expected_bounds_max_cm']).max()))
        passed = position_error <= .0002 and normal_error <= .000002 and uv_error == 0 and bounds_error <= .0002
        rows.append(dict(id=identifier, passed=passed, triangles=len(triangles),
                         islands=len(exported[identifier]['islands']), max_position_error_cm=position_error,
                         max_bounds_error_cm=bounds_error, max_normal_error=normal_error, max_uv_error=uv_error))
    source_hash = hashlib.sha256(source.read_bytes()).hexdigest()
    recorded_hash = load(root / 'scan/source.json')['source_sha256'].lower()
    source_matches = source_hash == recorded_hash
    return dict(passed=source_matches and all(row['passed'] for row in rows),
                source_sha256=source_hash, source_matches_scan=source_matches,
                exported_meshes=len(rows), exported_triangles=sum(row['triangles'] for row in rows),
                thresholds=dict(max_position_error_cm=.0002, max_bounds_error_cm=.0002,
                                max_normal_error=.000002, max_uv_error=0),
                checks=rows,
                scope='SDK round-trip triangle order, winding, corner positions, normals, UVs and local bounds; '
                      'Unreal packages/material slots and runtime checks are separate.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=Path('assets/derived/welding_cell'))
    parser.add_argument('--prepared-scan', type=Path, required=True)
    parser.add_argument('--manifest', type=Path, required=True)
    parser.add_argument('--source', type=Path, default=Path('assets/Autodesk_FBX_2009/Car_Body_Welding_Robotics_Cell.FBX'))
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    result = verify(args.output, args.prepared_scan, args.manifest, args.source)
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(result, indent=2, allow_nan=False) + '\n', encoding='utf-8')
    print(json.dumps(result, indent=2))
    return 0 if result['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
