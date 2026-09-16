"""Fit private, localized carriage-drive and cabinet heat fields to source meshes."""
import argparse
import hashlib
import json
from pathlib import Path

import numpy as np
from prepare_welding_cell import read_mesh


def generate(root):
    hashes = {}
    def bounds(material, island):
        path = root / 'scan' / f'Car_Body_Welding_Robotics_Cell_{material}.meshbin'
        hashes[path.name] = hashlib.sha256(path.read_bytes()).hexdigest()
        p, t, _ = read_mesh(path)
        v = p[np.unique(t[t['island'] == island]['v'])].astype(float)
        if not len(v):
            raise ValueError(f'Missing {material} island {island}')
        return v.min(0), v.max(0)

    def capsule(center, axis, half_length, radius):
        direction = np.eye(3)[axis] * half_length
        return dict(start_cm=(center-direction).tolist(), end_cm=(center+direction).tolist(), radius_cm=radius)

    scene = json.loads((root/'scene.json').read_text())
    groups = json.loads((root/'grouping_report.json').read_text())['groups']
    motors = {}
    for island in (36, 67, 98, 129):
        lo, hi = bounds('robots_blue', island)
        if not np.all((hi-lo > 13) & (hi-lo < 16)):
            raise ValueError('Source motor barrel changed; review fitted fields')
        owner = next(g['id'] for g in groups if any(
            m['mesh'].endswith('_robots_blue') and m['island'] == island for m in g['islands']))
        if not owner.startswith('SM_CarriageHolder_'):
            raise ValueError('Motor source ownership changed')
        component = next(c for c in scene['components'] if c['id'] == owner)
        local = (lo+hi)/2-np.array(component['location_cm'])
        motors.setdefault(owner, []).append(capsule(local, 1, 22., 19.))
    cabinet = []
    for island in (0, 43):
        lo, hi = bounds('robots_plastic_gray', island)
        center = (lo+hi)/2
        center[1] = lo[1]
        center[2] = hi[2]-55
        cabinet.append(capsule(center, 2, 45., 62.))
    lo, hi = bounds('robots_plastic_gray', 87)
    if hi[2]-lo[2] < 250:
        raise ValueError('Source control cabinet changed')
    for x in (lo[0], hi[0]):
        center = (lo+hi)/2
        center[0] = x
        center[2] = hi[2]-50
        cabinet.append(capsule(center, 2, 28., 55.))
    center = (lo+hi)/2
    center[1] = lo[1]
    cabinet.append(capsule(center, 2, 35., 52.))
    glass = json.loads((root/'control_cabinets.json').read_text())['screen']
    screen = [capsule(np.array(glass['center_local_cm']), 0, 12., 32.)]
    return dict(version=1, scope='Illustrative external surface fields, not internal thermal visibility',
                source_sha256=hashes, groups=[
                    *[dict(mesh=owner, role='carriage_motor', segments=fields) for owner, fields in motors.items()],
                    dict(mesh='SM_Background_robots_plastic_gray', role='cabinet', segments=cabinet),
                    dict(mesh='SM_Background_robots_plastic_black', role='cabinet', segments=cabinet),
                    dict(mesh='SM_Background_robots_screen', role='screen', segments=screen)])


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--root', type=Path, default=Path('assets/derived/welding_cell'))
    args = parser.parse_args()
    result = generate(args.root)
    (args.root/'equipment_heat.json').write_text(json.dumps(result, indent=2)+'\n')
    print('Fitted four carriage motors and localized cabinet/vent/LCD heat fields')
