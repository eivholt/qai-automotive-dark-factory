"""Copy the prior local IQ9 appearance plates and derive four robot-surface mounts.

Requires NumPy and the existing private welding-cell scan/grouping report.
Only derived data under assets/iq9_evk is written; the previous project is read-only.
This imports the prior photographed/impostor prop, not confidential CAD geometry.
"""
import argparse
import hashlib
import json
import shutil
from pathlib import Path
import numpy as np
from prepare_welding_cell import read_mesh

FACES = ('top', 'front', 'back', 'left', 'right')


def unit(v):
    n = np.linalg.norm(v)
    if n < 1e-8:
        raise ValueError('Degenerate mount axis')
    return v / n


def ray_hits(tri, start, direction):
    e1 = tri[:, 1] - tri[:, 0]
    e2 = tri[:, 2] - tri[:, 0]
    h = np.cross(np.broadcast_to(direction, e2.shape), e2)
    det = np.einsum('ij,ij->i', e1, h)
    good = np.abs(det) > 1e-8
    inverse = np.zeros_like(det)
    inverse[good] = 1 / det[good]
    s = start - tri[:, 0]
    u = inverse * np.einsum('ij,ij->i', s, h)
    q = np.cross(s, e1)
    v = inverse * (q @ direction)
    t = inverse * np.einsum('ij,ij->i', e2, q)
    mask = good & (u >= -1e-6) & (v >= -1e-6) & (u+v <= 1+1e-6) & (t >= 0)
    return t[mask]


def derive_mount(group, tri, observer):
    centers = tri.mean(axis=1)
    normals = np.cross(tri[:, 1]-tri[:, 0], tri[:, 2]-tri[:, 0])
    areas = np.linalg.norm(normals, axis=1) * .5
    good = areas > 1e-5
    normals[good] /= 2*areas[good, None]
    lo, hi = tri.reshape(-1, 3).min(axis=0), tri.reshape(-1, 3).max(axis=0)
    center = (lo+hi)*.5
    toward = unit(np.asarray(observer)-center)
    # FBX mirrored copies can have opposite triangle winding; choose the outer
    # casting side consistently before testing a surface patch.
    normals *= np.where(np.einsum('ij,ij->i',normals,centers-center) < 0,-1.,1.)[:,None]
    # Large outer casting sides, away from upper/lower hinge envelopes. The
    # source links are nearly upright; authored motion carries this rigid mount.
    candidates = np.flatnonzero(good & (centers[:, 2] > lo[2]+.28*(hi[2]-lo[2]))
                                & (centers[:, 2] < lo[2]+.70*(hi[2]-lo[2]))
                                & (np.abs(normals[:, 2]) < .3))
    order = sorted(candidates, key=lambda i: -(np.dot(normals[i], toward)*20
                    - np.linalg.norm((centers[i]-center)*[.4,.4,.1])*.05 + min(areas[i],60)*.01))
    best = None
    for i in order:
        n = normals[i]
        if np.dot(n, toward) < .1:
            continue
        x = unit(np.cross([0.,0.,1.], n))
        y = unit(np.cross(n, x))
        surface = centers[i]
        gaps = []
        # A13cm plate must sit against an actual near-planar surface, not its AABB.
        for dx in (-6.5,0.,6.5):
            for dy in (-6.5,0.,6.5):
                offset = surface+x*dx+y*dy
                hits = ray_hits(tri, offset+n*2, -n)
                if len(hits) == 0:
                    gaps = []; break
                gaps.append(float(hits.min()-2))
            if not gaps:
                break
        if gaps and min(gaps) > -.3 and max(gaps) < .8:
            best = (surface,n,x,y,gaps); break
    if best is None:
        raise ValueError('No validated13cm mounting patch: '+group['id'])
    surface,n,x,y,gaps = best
    pivot = np.asarray(group['pivot_cm'])
    return dict(id=group['id'].removeprefix('SM_').removesuffix('_upper_arm'),
                parent_mesh=group['id'], parent_joint=group['parent_joint'],
                contact_local_cm=(surface-pivot).tolist(),
                normal_local=n.tolist(), right_local=x.tolist(), up_local=y.tolist(),
                plate_size_cm=[13,13,.35], enclosure_size_cm=[10.6,10.6,4.3],
                mounting_gap_cm=.30, surface_patch_max_gap_cm=max(gaps),
                surface_patch_min_gap_cm=min(gaps), surface_patch_samples=9,
                scope='Authored13cm bracket on a sampled planar casting surface; not an engineered mount or swept-clearance certification')


def prepare(repo, reference):
    out = repo/'assets/iq9_evk'
    plates = out/'Impostor'
    plates.mkdir(parents=True, exist_ok=True)
    source = reference/'unreal_conveyor_demo/ContentSource/IQ9EVK/Impostor'
    files=[]
    for face in FACES:
        original=source/f'iq9_{face}.png'
        digest=hashlib.sha256(original.read_bytes()).hexdigest()
        destination=plates/original.name
        if not destination.is_file() or hashlib.sha256(destination.read_bytes()).hexdigest()!=digest:
            shutil.copyfile(original,destination)
        if hashlib.sha256(original.read_bytes()).hexdigest()!=digest:
            raise RuntimeError('Reference source changed during copy')
        files.append(dict(face=face,file=str(destination.relative_to(out)),sha256=digest,
                          source=str(original),bytes=destination.stat().st_size))
    derived=repo/'assets/derived/welding_cell'
    grouping=json.loads((derived/'grouping_report.json').read_text(encoding='utf-8'))
    scene=json.loads((derived/'scene.json').read_text(encoding='utf-8'))
    cache={}
    mounts=[]
    for group in grouping['groups']:
        if group['role']!='upper_arm':
            continue
        pieces=[]
        for island in group['islands']:
            mesh=island['mesh']
            if mesh not in cache:
                cache[mesh]=read_mesh(derived/'scan'/f'{mesh}.meshbin')[:2]
            points,triangles=cache[mesh]
            selected=triangles[triangles['island']==island['island']]
            pieces.append(points[selected['v']].astype(float))
        mounts.append(derive_mount(group,np.concatenate(pieces),scene['observer']['location_cm']))
    if len(mounts)!=4:
        raise RuntimeError('Expected four unique arm mounts')
    report=dict(version=1,source='Prior IQ9EVK five-face photographed/procedural impostor',
                reference_script='unreal_conveyor_demo/Scripts/build_iq9_evk_impostor.py',
                redistributable_assets=False,enclosure_size_cm=[10.6,10.6,4.3],
                files=files,mounts=sorted(mounts,key=lambda m:m['id']),
                led_layout='Prior amber activity + two steady green indicators; deterministic simulation-only animation')
    (out/'mounts.json').write_text(json.dumps(report,indent=2,allow_nan=False)+'\n',encoding='utf-8')
    print(json.dumps(dict(output=str(out/'mounts.json'),mounts=report['mounts']),indent=2))


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo',type=Path,default=Path(__file__).resolve().parents[2])
    parser.add_argument('--reference-repo',type=Path)
    args=parser.parse_args()
    prepare(args.repo,args.reference_repo or args.repo.parent/'qai-physics-reasoning')
