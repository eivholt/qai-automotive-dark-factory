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


def derive_mount(group, tri, source_island):
    """Fit the small raised cover on the outward stationary junction housing.

    The cover is part of the large connected casting island, not a separately
    named mesh. Identify its actual planar triangles by outward face direction,
    coplanarity and the tall 10 x 19 cm cover silhouette. The housing lid and
    large recessed access panels have different face extents and are excluded.
    """
    centers = tri.mean(axis=1)
    normals = np.cross(tri[:, 1]-tri[:, 0], tri[:, 2]-tri[:, 0])
    areas = np.linalg.norm(normals, axis=1)*.5
    good = areas > 1e-5
    normals[good] /= 2*areas[good, None]
    pivot = np.asarray(group['pivot_cm'])
    outward = np.array([np.sign(pivot[0]), 0., 0.])
    normals *= np.where(normals@outward < 0, -1., 1.)[:, None]
    candidates = good & (normals@outward > .999) & ((centers-pivot)@outward > 60.)
    if not candidates.any():
        raise ValueError('No outward junction housing: '+group['id'])
    normal = (normals[candidates]*areas[candidates, None]).sum(axis=0)
    normal[2] = 0.
    normal = unit(normal)
    right = unit(np.cross([0., 0., 1.], normal))
    up = unit(np.cross(normal, right))
    indices = np.flatnonzero(candidates)
    order = indices[np.argsort(centers[indices]@normal)]
    patches = np.split(order, np.where(np.diff(centers[order]@normal) > .03)[0]+1)
    covers, walls = [], []
    for indices in patches:
        vertices = tri[indices].reshape(-1, 3)
        size = np.array([np.ptp(vertices@right), np.ptp(vertices@up)])
        area = float(areas[indices].sum())
        plane = float(np.average(centers[indices]@normal, weights=areas[indices]))
        if area > 1000. and size[0] > 80. and size[1] > 25.:
            walls.append(plane)
        if 9. < size[0] < 12. and 18. < size[1] < 21. and 170. < area < 210.:
            covers.append((indices, vertices, size, area, plane))
    if len(covers) != 1 or not walls:
        raise ValueError('Expected one raised junction-box cover: '+group['id'])
    indices, vertices, size, area, plane = covers[0]
    protrusion = plane-max(walls)
    if not 5. < protrusion < 8.:
        raise ValueError('Cover does not protrude from the measured housing wall')
    surface = (normal*plane + right*((vertices@right).min()+(vertices@right).max())*.5
               + up*((vertices@up).min()+(vertices@up).max())*.5)
    cover_center = surface.copy()
    # R3's second stock pressing crosses the lower half of this base cover in
    # the relocated process layout. Raise only this prop; its lower enclosure
    # fastener region stays supported on the same measured planar cover.
    shift_up = 6. if group['id']=='SM_robot_px_ny_fixed_base' else 0.
    surface += up*shift_up
    support_y = (-5.3, -2.65, 0.) if shift_up else (-5.3, 0., 5.3)
    top_overhang = max(0., shift_up+5.3-size[1]*.5)
    support_gaps, plate_gaps = [], []
    # The 10.6 cm enclosure slightly overhangs the 10 cm cover laterally.
    # All nine fastener/support probes lie inside the actual flat cover face.
    for dx in (-4., 0., 4.):
        for dy in support_y:
            hits = ray_hits(tri, surface+right*dx+up*dy+normal*2, -normal)
            if len(hits) == 0:
                raise ValueError('Missing cover support: '+group['id'])
            support_gaps.append(float(hits.min()-2))
    if max(abs(v) for v in support_gaps) > .03:
        raise ValueError('Non-planar junction cover support: '+group['id'])
    # Dense rays over the entire enclosure check no source surface protrudes into
    # the 0.30 cm air gap, including its small unsupported side overhangs.
    for dx in np.linspace(-5.3, 5.3, 9):
        for dy in np.linspace(-5.3, 5.3, 9):
            hits = ray_hits(tri, surface+right*dx+up*dy+normal*10, -normal)
            if len(hits) == 0:
                raise ValueError('Missing housing behind enclosure: '+group['id'])
            plate_gaps.append(float(hits.min()-10))
    if min(plate_gaps) < -.03:
        raise ValueError('Enclosure intersects source housing: '+group['id'])
    return dict(id=group['id'].removeprefix('SM_').removesuffix('_fixed_base'),
                parent_mesh=group['id'], parent_joint='',
                mount_recipe='fixed-base-direct-cover-v3',
                contact_local_cm=(surface-pivot).tolist(), contact_source_world_cm=surface.tolist(),
                cover_center_local_cm=(cover_center-pivot).tolist(), mount_shift_up_cm=shift_up,
                enclosure_top_overhang_cm=float(top_overhang), support_y_offsets_cm=list(support_y),
                normal_local=normal.tolist(), right_local=right.tolist(), up_local=up.tolist(),
                backing_plate_present=False, enclosure_size_cm=[10.6,10.6,4.3],
                mounting_gap_cm=.30, surface_patch_max_gap_cm=max(support_gaps),
                surface_patch_min_gap_cm=min(support_gaps), surface_patch_samples=9,
                support_size_cm=[8,float(max(support_y)-min(support_y))], enclosure_clearance_samples=len(plate_gaps),
                enclosure_clearance_min_cm=.30+min(plate_gaps),
                enclosure_lateral_overhang_cm=float((10.6-size[0])*.5),
                source_surface=dict(mesh=source_island['mesh'], island=source_island['island'],
                                    planar_triangles=len(indices), area_cm2=area,
                                    face_size_cm=size.tolist(), wall_protrusion_cm=protrusion,
                                    face_min_source_cm=vertices.min(axis=0).tolist(),
                                    face_max_source_cm=vertices.max(axis=0).tolist()),
                scope='Direct enclosure on measured raised fixed-base cover, with recorded lateral/top overhang; sampled support and clearance, not an engineered mounting certification')


def triangle_box_intersections(tri, half_size):
    """Exact separating-axis tests, including edge axes, in the box frame."""
    tri = tri[((tri.min(axis=1)<=half_size)&(tri.max(axis=1)>=-half_size)).all(axis=1)]
    if not len(tri):
        return 0
    edge = np.roll(tri,-1,axis=1)-tri
    overlap = np.ones(len(tri),dtype=bool)
    axes = [np.cross(edge[:,0],edge[:,1])]
    axes += [np.cross(edge[:,j],np.broadcast_to(axis,(len(tri),3)))
             for j in range(3) for axis in np.eye(3)]
    for axis in axes:
        projected = np.einsum('nij,nj->ni',tri,axis)
        radius = np.abs(axis)@half_size
        overlap &= (projected.min(axis=1)<=radius+1e-8)&(projected.max(axis=1)>=-radius-1e-8)
    return int(overlap.sum())


def audit_process_stock(derived, grouping, mounts, cache):
    """Check actual relocated stock/rack triangles against the rigid enclosure.

    Inflating the box in all three local axes establishes a conservative
    Euclidean clearance lower bound; this is not a swept-motion certificate.
    """
    layout_path = derived/'panel_assembly.json'
    if not layout_path.is_file():
        return
    layout = json.loads(layout_path.read_text(encoding='utf-8'))
    triangles = []
    for group in grouping['groups']:
        if not group['id'].startswith('SM_PanelSupply_'):
            continue
        for island in group['islands']:
            mesh = island['mesh']
            if mesh not in cache:
                cache[mesh] = read_mesh(derived/'scan'/f'{mesh}.meshbin')[:2]
            points, faces = cache[mesh]
            triangles.append(points[faces[faces['island']==island['island']]['v']].astype(float)
                             +np.asarray(layout['supply_delta_cm']))
    stock = np.concatenate(triangles)
    robot_order = ['robot_px_py','robot_nx_py','robot_px_ny','robot_nx_ny']
    for mount in mounts:
        offset = np.asarray(layout['base_offsets_cm'][robot_order.index(mount['id'])])
        normal = np.asarray(mount['normal_local'])
        center = np.asarray(mount['contact_source_world_cm'])+normal*2.45+offset
        basis = np.column_stack([mount['right_local'],mount['up_local'],normal])
        local = (stock-center)@basis
        result = {}
        for name,origin,extent in [('enclosure',np.zeros(3),np.array([5.3,5.3,2.15]))]:
            tri = local-origin
            count = triangle_box_intersections(tri,extent)
            if count:
                raise ValueError(f"{mount['id']} {name} intersects {count} stock/rack triangles")
            lo,hi = 0.,60.
            for _ in range(17):
                mid = (lo+hi)*.5
                if triangle_box_intersections(tri,extent+mid):
                    hi = mid
                else:
                    lo = mid
            result[name] = dict(intersecting_triangles=0,
                                clearance_euclidean_lower_bound_cm=lo)
        mount['process_stock_audit'] = dict(recipe='triangle-box-SAT-and-conservative-inflation-v1',
                triangles=len(stock), parts=result, layout='panel_assembly.json')


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
    cache={}
    mounts=[]
    for group in grouping['groups']:
        if group['role']!='fixed_base':
            continue
        pieces=[]
        source_islands=[]
        for island in group['islands']:
            mesh=island['mesh']
            if mesh not in cache:
                cache[mesh]=read_mesh(derived/'scan'/f'{mesh}.meshbin')[:2]
            points,triangles=cache[mesh]
            selected=triangles[triangles['island']==island['island']]
            pieces.append(points[selected['v']].astype(float))
            source_islands.append(island)
        casting_index=max(range(len(pieces)), key=lambda i:len(pieces[i]))
        mounts.append(derive_mount(group,pieces[casting_index],source_islands[casting_index]))
    audit_process_stock(derived,grouping,mounts,cache)
    if len(mounts)!=4:
        raise RuntimeError('Expected four unique fixed-base mounts')
    report=dict(version=3,source='Prior IQ9EVK five-face photographed/procedural impostor',
                reference_script='unreal_conveyor_demo/Scripts/build_iq9_evk_impostor.py',
                redistributable_assets=False,mount_recipe='fixed-base-direct-cover-v3',enclosure_size_cm=[10.6,10.6,4.3],
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
