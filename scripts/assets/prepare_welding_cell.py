"""Regroup local welding-cell mesh islands into approximate rigid robot links.

Run after fbx_prepare scan and reconstruct_robot_joints.py. All geometry and
manifests stay in assets/. The published script contains no mesh payloads.
Requires numpy. This is an authored visual rig, not industrial kinematics.
"""
import argparse
import hashlib
import json
import math
import struct
from collections import Counter
from pathlib import Path
import numpy as np

PREFIX = 'Car_Body_Welding_Robotics_Cell_'
TRIANGLE = np.dtype([('v', '<u4', (3,)), ('island', '<u4'),
                     ('normals', '<f4', (3, 3)), ('uv', '<f4', (3, 2))])
ROLE_JOINT = {'fixed_base': '', 'shoulder': 'base', 'upper_arm': 'shoulder',
              'elbow': 'elbow', 'forearm': 'forearm_roll', 'wrist': 'wrist',
              'flange': 'tool_roll', 'tool': 'tool_roll', 'dress_pack': 'base'}
PARENTS = {'base': '', 'shoulder': 'base', 'elbow': 'shoulder',
           'forearm_roll': 'elbow', 'wrist': 'forearm_roll', 'tool_roll': 'wrist'}
TEMPERATURES = {'fixed_base': 25, 'shoulder': 44, 'upper_arm': 34, 'elbow': 57,
                'forearm': 39, 'wrist': 43, 'flange': 49, 'tool': 72, 'dress_pack': 28}

def read_mesh(path):
    raw = path.read_bytes()
    if raw[:8] != b'DFMESH01':
        raise ValueError('Unsupported mesh scan format: ' + str(path))
    nv, nt = struct.unpack_from('<II', raw, 8)
    if len(raw) != 16 + nv * 12 + nt * TRIANGLE.itemsize:
        raise ValueError('Truncated mesh scan: ' + str(path))
    points = np.frombuffer(raw, dtype='<f4', count=nv * 3, offset=16).reshape(-1, 3)
    triangles = np.frombuffer(raw, dtype=TRIANGLE, count=nt, offset=16 + nv * 12)
    labels = np.zeros(nv, dtype=np.uint32)
    labels[triangles['v'].reshape(-1)] = np.repeat(triangles['island'], 3)
    return points, triangles, labels


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2, allow_nan=False) + '\n', encoding='utf-8')


PARK_BASE_DEGREES = {'robot_px_py': -180., 'robot_nx_py': -180.,
                     'robot_px_ny': -180., 'robot_nx_ny': 180.}
PORTAL_ENTRY = [4.505665, -1204.08264, 6.3477106]
PORTAL_EXIT = [4.505665, 1219.15393, 6.3477106]
CYCLE_DURATION_SECONDS = 18
THERMAL_CORE_EDGE_DROP_C = {
    'elbow': 30., 'flange': 22., 'shoulder': 20., 'wrist': 18.,
    'forearm': 15., 'upper_arm': 10., 'fixed_base': 3.,
}


def update_thermal_fields(scene):
    """Set authored casting gradients; preserve geometry, source temperatures and IK.

    Tool, body-weld and particle cooling are independent runtime fields. No bulk
    heat capacity/contact simulation is inferred from imported bounding boxes.
    Validate every component before changing any of the thermal records.
    """
    expected = {f'SM_{robot}_{role}': drop for robot in PARK_BASE_DEGREES
                for role, drop in THERMAL_CORE_EDGE_DROP_C.items()}
    components = scene.get('components')
    if not isinstance(components, list):
        raise ValueError('Thermal preparation requires scene.components')
    assignments, identities = [], set()
    for component in components:
        identity = component.get('id')
        if not isinstance(identity, str) or not identity or identity in identities:
            raise ValueError('Missing or duplicate component identity in thermal preparation')
        identities.add(identity)
        thermal = component.get('thermal')
        if not isinstance(thermal, dict):
            raise ValueError('Missing authored thermal profile: ' + identity)
        temperature, emissivity = thermal.get('temperature_c'), thermal.get('emissivity')
        if type(temperature) not in (int, float) or not math.isfinite(temperature) or not -100 <= temperature <= 500:
            raise ValueError('Invalid authored source temperature: ' + identity)
        if type(emissivity) not in (int, float) or not math.isfinite(emissivity) or not 0 <= emissivity <= 1:
            raise ValueError('Invalid authored emissivity: ' + identity)
        drop = expected.get(identity, 0.)
        if not 0 <= drop <= temperature or temperature-drop < 0:
            raise ValueError('Authored casting field would have a negative surface-temperature floor: ' + identity)
        assignments.append((thermal, identity, drop, temperature))
    if set(expected) - identities:
        raise ValueError('Missing warm robot castings: ' + ', '.join(sorted(set(expected)-identities)))
    for thermal, _, drop, _ in assignments:
        thermal['core_edge_drop_c'] = drop
        thermal['simulate_exchange'] = False
    return {'model': 'authored-imported-casting-gradients-v1',
            'spatial_components': len(expected), 'uniform_base_components': len(assignments)-len(expected),
            'minimum_edge_temperature_c': min(t-drop for _, _, drop, t in assignments),
            'maximum_core_edge_drop_c': max(expected.values()),
            'role_core_edge_drop_c': dict(THERMAL_CORE_EDGE_DROP_C)}



APPROACH_KEY_PROGRESS = [.20, .56, .80]
APPROACH_BASE_REMAINING = [.85, .30]
APPROACH_LINK_ANGLES_DEG = {
    'shoulder': [65., 95.], 'elbow': [-35., -15.],
    'forearm_roll': [0., 0.], 'wrist': [0., 0.], 'tool_roll': [65., 95.],
}


def _approach_hover_key(rows, robot, emitter):
    # Solve base/shoulder/elbow only; distal axes retain the first solved work pose.
    tip = np.asarray(emitter.get('location_cm'), dtype=float)
    if tip.shape != (3,) or not np.isfinite(tip).all():
        raise ValueError('Invalid local electrode position: ' + robot)
    specs = [rows[robot + '_' + role] for role in PARENTS]
    for row in specs:
        for field in ('pivot_cm', 'axis'):
            value = np.asarray(row.get(field), dtype=float)
            if value.shape != (3,) or not np.isfinite(value).all():
                raise ValueError('Invalid approach FK ' + field + ': ' + row['id'])
        if abs(np.linalg.norm(row['axis'])-1.) > .001:
            raise ValueError('Non-unit approach FK axis: ' + row['id'])
    first = np.array([row['weld_angles_deg'][0] for row in specs])
    last = np.array([row['weld_angles_deg'][-1] for row in specs])

    def position(angles):
        transforms = {}
        for row, angle in zip(specs, angles):
            rotation, translation = transforms.get(row['parent_joint'], (np.eye(3), np.zeros(3)))
            transforms[row['id']] = (rotation @ rotation_matrix(row['axis'], angle),
                                      rotation @ row['pivot_cm'] + translation)
        rotation, translation = transforms[robot + '_tool_roll']
        return rotation @ tip + translation

    target = .5*(position(first)+position(last)) + np.array([0., 0., 100.])
    angles = first.copy()
    for _ in range(120):
        current = position(angles)
        error = target-current
        if np.linalg.norm(error) < .01:
            return angles.tolist(), float(np.linalg.norm(error))
        jacobian = np.empty((3, 3))
        for axis in range(3):
            shifted = angles.copy()
            shifted[axis] += .002
            jacobian[:, axis] = (position(shifted)-current)/.002
        step = jacobian.T @ np.linalg.solve(jacobian @ jacobian.T + .005*np.eye(3), error)
        step *= min(1., 3./max(float(np.max(np.abs(step))), 1e-12))
        angles[:3] = np.clip(angles[:3]+step, -175., 175.)
    raise ValueError('Approach hover solve did not converge: ' + robot)


def update_approach_choreography(scene):
    # Author three keys without changing rest poses, solved weld knots or meshes.
    # Runtime quintic interpolation connects rest -> keys -> first seam pose,
    # and reverses from the last seam pose. This is an illustrative trajectory.
    rows = scene.get('joints')
    if not isinstance(rows, list) or not isinstance(scene.get('cycle'), dict):
        raise ValueError('Approach preparation requires joints and cycle metadata')
    by_id = {j.get('id'): j for j in rows}
    expected = {robot + '_' + role for robot in PARK_BASE_DEGREES for role in PARENTS}
    if len(by_id) != len(rows) or set(by_id) != expected:
        raise ValueError('Approach preparation requires the exact 24 unique robot joints')
    updates = []
    for robot in PARK_BASE_DEGREES:
        for index, role in enumerate(PARENTS):
            row = by_id[robot + '_' + role]
            rest, weld, limits = row.get('rest_angle_deg'), row.get('weld_angles_deg'), row.get('range_deg')
            if type(rest) not in (int, float) or not math.isfinite(rest) or abs(rest) > 180:
                raise ValueError('Invalid rest angle: ' + row['id'])
            if not isinstance(weld, list) or len(weld) != 5 or any(
                    type(a) not in (int, float) or not math.isfinite(a) or abs(a) > 12 for a in weld):
                raise ValueError('Invalid solved seam angles: ' + row['id'])
            if not isinstance(limits, list) or len(limits) != 2 or any(
                    type(a) not in (int, float) or not math.isfinite(a) or abs(a) > 180 for a in limits) or limits[0] > limits[1]:
                raise ValueError('Invalid authored joint limits: ' + row['id'])
            if role == 'base':
                if rest != PARK_BASE_DEGREES[robot]:
                    raise ValueError('Parked base direction changed: ' + row['id'])
                angles = [weld[0] + (rest-weld[0])*fraction for fraction in APPROACH_BASE_REMAINING]
            else:
                angles = list(APPROACH_LINK_ANGLES_DEG[role])
            updates.append((row, robot, index, angles))
    emitters = {e.get('id'): e for e in scene['cycle'].get('emitters', [])}
    if set(emitters) != set(PARK_BASE_DEGREES):
        raise ValueError('Approach preparation requires four solved electrode emitters')
    hover = {robot: _approach_hover_key(by_id, robot, emitters[robot]) for robot in PARK_BASE_DEGREES}
    # No mutation until all records and four bounded solves have succeeded.
    for row, robot, index, angles in updates:
        angles.append(hover[robot][0][index])
        row['approach_angles_deg'] = angles
        row['range_deg'] = [min(*row['range_deg'], row['rest_angle_deg'], *row['weld_angles_deg'], *angles),
                            max(*row['range_deg'], row['rest_angle_deg'], *row['weld_angles_deg'], *angles)]
    scene['cycle'].update(
        approach_model='dramatic-approach-v1',
        approach_key_progress=list(APPROACH_KEY_PROGRESS),
        approach_hover_height_cm=100.,
        approach_hover_max_residual_cm=max(residual for _, residual in hover.values()),
        phase_boundaries_seconds=[0, 3, 3.5, 3.7, 4.06, 4.3, 4.5, 12, 12.2, 12.44, 12.8, 13, 13.5, 17, CYCLE_DURATION_SECONDS],
        robot_motion_sequence='lift and roll 3.5..4.5 through normalized keys .20/.56/.80, precision weld 4.5..12, reverse keys 12..13',
        approach_scope='Authored visual motion; sampled source geometry checks are not continuous collision or industrial trajectory validation.')
    return {'model': 'dramatic-approach-v1', 'joints': len(updates),
            'key_progress': list(APPROACH_KEY_PROGRESS), 'approach_seconds': 1.,
            'withdrawal_seconds': 1., 'cycle_seconds': CYCLE_DURATION_SECONDS,
            'rest_and_solved_weld_angles_preserved': True}


def update_park_cycle(scene):
    """Update parked poses, timing, approach and thermal fields; retain IK seam knots."""
    for row in scene['joints']:
        for robot, angle in PARK_BASE_DEGREES.items():
            if row['id'] == robot + '_base':
                row['rest_angle_deg'] = angle
                row['range_deg'] = [min(angle, *row['weld_angles_deg']), max(angle, *row['weld_angles_deg'])]
    scene['cycle'].update(duration_seconds=CYCLE_DURATION_SECONDS,
                          arrival_offset_cm=-1800., departure_offset_cm=1800.,
                          phase_boundaries_seconds=[0, 3, 3.5, 4.1, 4.5, 12, 12.4, 13, 13.5, 17, CYCLE_DURATION_SECONDS],
                          robot_motion_sequence='folded unpark 3.5..4.1, unfold 4.1..4.5, precision weld 4.5..12, fold 12..12.4, park 12.4..13',
                          portal_entry_cm=PORTAL_ENTRY, portal_exit_cm=PORTAL_EXIT,
                          portal_tunnel_length_cm=1100.,
                          portal_entry_yaw_deg=180., portal_exit_yaw_deg=0.,
                          portal_opening_width_cm=420., portal_opening_height_cm=390.,
                          rail_half_gauge_cm=54.212878, rail_width_cm=30.0364,
                          rail_height_cm=13.332, rail_top_above_ground_cm=84.62721,
                          curtain_rail_clearance_cm=2., curtain_ground_clearance_cm=8.)
    update_thermal_fields(scene)
    update_approach_choreography(scene)


def prepare_carriage_holders(root):
    """Split two complete source holder assemblies without rebuilding robot groups.

    Yellow connected islands define each frame. Contained non-yellow hardware
    below Z110cm belongs to its rail carriage; full-length rails, platforms, car
    panels and robot links are excluded. Every source island remains assigned once.
    """
    metadata = json.loads((root / 'scan/islands.json').read_text(encoding='utf-8'))
    report = json.loads((root / 'grouping_report.json').read_text(encoding='utf-8'))
    scene = json.loads((root / 'scene.json').read_text(encoding='utf-8'))
    manifest = json.loads((root / 'manifest.json').read_text(encoding='utf-8'))
    islands = {(m['name'], a['id']): a for m in metadata['meshes'] for a in m['islands']}
    groups = {g['id']: g for g in report['groups']}
    owners = {}
    for g in groups.values():
        for member in g['islands']:
            key = (member['mesh'], member['island'])
            if key in owners:
                raise ValueError('Duplicate prior island assignment')
            owners[key] = g['id']
    if set(owners) != set(islands):
        raise ValueError('Existing grouped assets do not exactly cover the source scan')
    yellow = next(m for m in metadata['meshes'] if m['name'] == PREFIX + 'robots_yellow')
    seeds = []
    for a in yellow['islands']:
        lo, hi = np.array(a['min']), np.array(a['max'])
        span = hi-lo
        if abs((lo[0]+hi[0])*.5)<20 and 180<span[0]<240 and 130<span[1]<180 and lo[2]>70 and hi[2]<210:
            seeds.append(a)
    if len(seeds) != 2:
        raise ValueError('Expected exactly two connected yellow body holder frames')
    affected = set()
    assembly_rows = []
    for seed in sorted(seeds, key=lambda a: a['min'][1]):
        lo, hi = np.array(seed['min']), np.array(seed['max'])
        group_id = 'SM_CarriageHolder_' + ('Front' if (lo[1]+hi[1])<0 else 'Rear')
        selected = {(yellow['name'], seed['id'])}
        for key, island in islands.items():
            suffix = key[0].removeprefix(PREFIX)
            if suffix not in {'chrome', 'robots_blue', 'robots_plastic', 'robots_white'}:
                continue
            low, high = np.array(island['min']), np.array(island['max'])
            if (np.all(low[:2]>=lo[:2]-3) and np.all(high[:2]<=hi[:2]+3)
                    and low[2]>=73 and high[2]<=110):
                selected.add(key)
        for key in selected:
            previous = owners[key]
            if previous != group_id and not previous.startswith('SM_Background_'):
                raise ValueError('Holder selection overlaps a robot/body group: '+str(key))
            if previous != group_id:
                groups[previous]['islands'] = [i for i in groups[previous]['islands'] if (i['mesh'], i['island']) != key]
                affected.add(previous)
        members = [{'mesh': mesh, 'island': island} for mesh, island in sorted(selected)]
        low = np.min([islands[key]['min'] for key in selected], axis=0)
        high = np.max([islands[key]['max'] for key in selected], axis=0)
        pivot = (low+high)*.5
        triangles = sum(islands[key]['triangles'] for key in selected)
        groups[group_id] = {'id': group_id, 'role': 'carriage_holder', 'parent_joint': '',
                            'pivot_cm': pivot.tolist(), 'triangles': triangles, 'islands': members}
        if any(owners[key] != group_id for key in selected):
            affected.add(group_id)
        for key in selected:
            owners[key] = group_id
        materials = Counter()
        for key in selected:
            materials[key[0].removeprefix(PREFIX)] += islands[key]['triangles']
        assembly_rows.append({'id': group_id, 'island_count': len(selected), 'triangles': triangles,
                              'bounds_min_cm': low.tolist(), 'bounds_max_cm': high.tolist(),
                              'materials_triangles': dict(materials)})
    if not affected:
        return {'changed': False, 'geometry_conserved': True, 'assemblies': assembly_rows}
    components = {c['id']: c for c in scene['components']}
    mesh_rows = {m['id']: m for m in manifest['meshes']}
    for identifier, group in groups.items():
        if not group['islands']:
            raise ValueError('Holder split unexpectedly emptied an existing mesh: '+identifier)
        group['triangles'] = sum(islands[i['mesh'], i['island']]['triangles'] for i in group['islands'])
        if identifier not in affected:
            continue
        low = np.min([islands[i['mesh'], i['island']]['min'] for i in group['islands']], axis=0)
        high = np.max([islands[i['mesh'], i['island']]['max'] for i in group['islands']], axis=0)
        pivot = np.array(group['pivot_cm'])
        mesh_rows[identifier] = {'id': identifier, 'fbx_node': identifier,
                                 'expected_bounds_min_cm': (low-pivot).tolist(),
                                 'expected_bounds_max_cm': (high-pivot).tolist()}
        if identifier.startswith('SM_CarriageHolder_'):
            components[identifier] = {'id': identifier, 'asset_path': '/Game/WeldingCell/Meshes/'+identifier+'.'+identifier,
                                      'parent_joint': '', 'location_cm': pivot.tolist(), 'rotation_deg': [0,0,0],
                                      'scale_xyz': [1,1,1], 'role': 'carriage_holder', 'attachment': 'carriage',
                                      'thermal': {'temperature_c': 20, 'emissivity': .88,
                                                  'simulate_exchange': False, 'core_edge_drop_c': 0}}
    source_triangles = sum(m['triangles'] for m in metadata['meshes'])
    grouped_triangles = sum(g['triangles'] for g in groups.values())
    if source_triangles != grouped_triangles or sum(len(g['islands']) for g in groups.values()) != len(islands):
        raise AssertionError('Holder regrouping did not conserve source geometry')
    scene['components'] = [components[key] for key in sorted(components)]
    report.update(groups=[groups[key] for key in sorted(groups)], components=len(groups),
                  assigned_triangles=grouped_triangles, geometry_conserved=True,
                  roles=dict(Counter(g['role'] for g in groups.values())))
    manifest['meshes'] = [mesh_rows[key] for key in sorted(mesh_rows)]
    with (root / 'assignments.tsv').open('w',encoding='utf-8',newline='\n') as stream:
        stream.write('# source_mesh island_id export_group pivot_x_cm pivot_y_cm pivot_z_cm\n')
        for (mesh, island), identifier in sorted(owners.items()):
            pivot=groups[identifier]['pivot_cm']
            stream.write(f'{mesh}\t{island}\t{identifier}\t{pivot[0]:.9g}\t{pivot[1]:.9g}\t{pivot[2]:.9g}\n')
    write_json(root / 'scene.json',scene)
    write_json(root / 'manifest.json',manifest)
    write_json(root / 'grouping_report.json',report)
    write_json(root / 'holder_delta_manifest.json', {'version':1,'fbx':'holder_delta.fbx',
                                                   'meshes':[mesh_rows[key] for key in sorted(affected)]})
    (root / 'holder_delta_groups.txt').write_text('\n'.join(sorted(affected))+'\n',encoding='utf-8')
    result={'changed':True,'geometry_conserved':True,'source_triangles':source_triangles,
            'assigned_triangles':grouped_triangles,'source_islands':len(islands),'assigned_islands':len(owners),
            'total_meshes':len(groups),'changed_meshes':sorted(affected),'assemblies':assembly_rows,
            'unmodified_meshes':sorted(set(groups)-affected)}
    write_json(root / 'holder_grouping_report.json',result)
    return result


def prepare(root):
    scan = root / 'scan'
    metadata = json.loads((scan / 'islands.json').read_text(encoding='utf-8'))
    rig = json.loads((root / 'robot_joints.json').read_text(encoding='utf-8'))
    meshes = {m['name']: m for m in metadata['meshes']}
    paint = meshes[PREFIX + 'robots']
    points, _, vertex_islands = read_mesh(scan / (paint['name'] + '.meshbin'))
    robots = rig['robots']
    if len(robots) != 4:
        raise ValueError('This asset recipe expects four robots; inspect a changed source before proceeding')
    groups, assignments, anchors = {}, {}, []
    joint_rows = []

    def add_group(group_id, pivot, parent='', temperature=20, emissivity=.9, role='background'):
        groups[group_id] = {'id': group_id, 'pivot': np.asarray(pivot, dtype=float),
                            'parent': parent, 'temperature': temperature, 'emissivity': emissivity,
                            'role': role, 'islands': [], 'triangles': 0,
                            'lo': np.full(3, np.inf), 'hi': np.full(3, -np.inf)}
        return group_id

    def assign(mesh, island, group_id):
        key = (mesh['name'], island['id'])
        if key in assignments:
            raise ValueError('Duplicate island assignment: ' + str(key))
        assignments[key] = group_id
        g = groups[group_id]
        g['islands'].append({'mesh': mesh['name'], 'island': island['id']})
        g['triangles'] += island['triangles']
        g['lo'] = np.minimum(g['lo'], island['min'])
        g['hi'] = np.maximum(g['hi'], island['max'])

    for ri, robot in enumerate(robots):
        robot_id = robot['id']
        for name, joint in robot['joints'].items():
            parent = PARENTS[name]
            pivot = np.asarray(joint['pivot_cm'], dtype=float)
            parent_pivot = np.asarray(robot['joints'][parent]['pivot_cm']) if parent else np.zeros(3)
            joint_rows.append({'id': robot_id + '_' + name,
                               'parent_joint': robot_id + '_' + parent if parent else '',
                               'pivot_cm': (pivot - parent_pivot).tolist(), 'rotation_deg': [0, 0, 0],
                               'axis': joint['axis'],
                               'range_deg': [joint['min_deg'], joint['max_deg']],
                               'period_seconds': 11 + ri * 1.7 + list(PARENTS).index(name) * .9,
                               'phase_deg': 0})
        island_by_id = {a['id']: a for a in paint['islands']}
        robot['group_ids'] = {}
        for role, joint in ROLE_JOINT.items():
            pivot_key = joint or 'base'
            pivot = robot['joints'][pivot_key]['pivot_cm']
            group_id = add_group('SM_' + robot_id + '_' + role, pivot,
                                 robot_id + '_' + joint if joint else '',
                                 TEMPERATURES[role], .94 if role == 'dress_pack' else .87, role)
            robot['group_ids'][role] = group_id
            if role == 'dress_pack':
                continue
            island_id = robot['anchors'][role]
            cloud = points[vertex_islands == island_id]
            if not len(cloud):
                raise ValueError('Anchor island has no vertices')
            # Uniform subsampling bounds the geometric nearest-surface search cost.
            cloud = cloud[::max(1, len(cloud) // 2500)]
            anchors.append({'robot': ri, 'role': role, 'cloud': cloud, 'group': group_id})
            assign(paint, island_by_id[island_id], group_id)

    def nearest(center, robot_index=None):
        best = (float('inf'), None)
        for anchor in anchors:
            if robot_index is not None and anchor['robot'] != robot_index:
                continue
            delta = anchor['cloud'] - center
            distance = float(np.min(np.einsum('ij,ij->i', delta, delta)))
            if distance < best[0]:
                best = (distance, anchor)
        return math.sqrt(best[0]), best[1]

    # Every remaining painted island belongs to its geometrically nearest major link.
    for island in paint['islands']:
        if (paint['name'], island['id']) in assignments:
            continue
        center = (np.array(island['min']) + island['max']) / 2
        _, anchor = nearest(center)
        assign(paint, island, anchor['group'])

    for mesh in metadata['meshes']:
        if mesh['name'] == paint['name']:
            continue
        suffix = mesh['name'].removeprefix(PREFIX)
        background = add_group('SM_Background_' + suffix, [0, 0, 0],
                               temperature=20, emissivity=.65 if suffix == 'chrome' else .92)
        for island in mesh['islands']:
            lo, hi = np.array(island['min']), np.array(island['max'])
            center, span = (lo + hi) / 2, hi - lo
            in_robot_region = abs(center[0]) < 510 and abs(center[1]) < 430 and center[2] > 125
            if suffix == 'robots_plastic' and in_robot_region and max(span) > 100:
                # Preserve the supplied rigid dress-pack asset under base yaw.
                # Runtime replaces its display with joint-anchored spline hoses.
                candidates = [(np.linalg.norm(center[:2] - np.asarray(r['joints']['shoulder']['pivot_cm'])[:2]), i)
                              for i, r in enumerate(robots)]
                ri = min(candidates)[1]
                assign(mesh, island, robots[ri]['group_ids']['dress_pack'])
                continue
            if in_robot_region and max(span) < 110:
                distance, anchor = nearest(center)
                limit = 18 if anchor['role'] in ('tool', 'flange', 'wrist') else 12
                if distance < limit:
                    assign(mesh, island, anchor['group'])
                    continue
            # Keep independently authored car-body islands available for later
            # missing-panel and fixture scenarios. Do not infer anomaly labels.
            if (suffix == 'chrome' and abs(center[0]) < 175 and lo[1] > -450
                    and hi[1] < 410 and lo[2] > 130 and max(span) > 15):
                group_id = add_group('SM_BodyPanel_' + str(island['id']).zfill(4), center,
                                     temperature=23, emissivity=.74, role='body_panel')
                assign(mesh, island, group_id)
            else:
                assign(mesh, island, background)

    groups = {k: g for k, g in groups.items() if g['islands']}
    source_triangles = sum(m['triangles'] for m in metadata['meshes'])
    expected_islands = sum(len(m['islands']) for m in metadata['meshes'])
    if len(assignments) != expected_islands or sum(g['triangles'] for g in groups.values()) != source_triangles:
        raise AssertionError('Source geometry is not assigned exactly once')
    if len(groups) > 255:
        raise ValueError('Too many thermal stencil profiles; consolidate only background groups')
    components, mesh_rows, report_groups = [], [], []
    with (root / 'assignments.tsv').open('w', encoding='utf-8', newline='\n') as stream:
        stream.write('# source_mesh island_id export_group pivot_x_cm pivot_y_cm pivot_z_cm\n')
        for (mesh, island), group_id in sorted(assignments.items()):
            pivot = groups[group_id]['pivot']
            stream.write(f'{mesh}\t{island}\t{group_id}\t{pivot[0]:.9g}\t{pivot[1]:.9g}\t{pivot[2]:.9g}\n')
    for group_id, g in sorted(groups.items()):
        position = [0, 0, 0] if g['parent'] else g['pivot'].tolist()
        components.append({'id': group_id, 'asset_path': '/Game/WeldingCell/Meshes/' + group_id + '.' + group_id,
                           'parent_joint': g['parent'], 'location_cm': position,
                           'rotation_deg': [0, 0, 0], 'scale_xyz': [1, 1, 1],
                           'thermal': {'temperature_c': g['temperature'], 'emissivity': g['emissivity'],
                                       'simulate_exchange': False, 'core_edge_drop_c': 0}})
        mesh_rows.append({'id': group_id, 'fbx_node': group_id,
                          'expected_bounds_min_cm': (g['lo'] - g['pivot']).tolist(),
                          'expected_bounds_max_cm': (g['hi'] - g['pivot']).tolist()})
        report_groups.append({'id': group_id, 'role': g['role'], 'parent_joint': g['parent'],
                              'pivot_cm': g['pivot'].tolist(), 'triangles': g['triangles'],
                              'islands': g['islands']})
    scene = {'schema_version': 1, 'units': 'cm', 'coordinates': 'Unreal X-forward Y-right Z-up',
             'description': 'Local approximate rigid-link welding-cell visual preview; no industrial motion calibration or inspection adapter.',
             'components': components, 'joints': joint_rows,
             'observer': {'location_cm': [970, -1130, 610], 'look_at_cm': [0, -40, 245]},
             'sensor': {'location_cm': [740, -860, 520], 'look_at_cm': [0, -35, 270], 'horizontal_fov_deg': 58}}
    thermal_fields = update_thermal_fields(scene)
    write_json(root / 'scene.json', scene)
    write_json(root / 'manifest.json', {'version': 1, 'fbx': 'prepared.fbx', 'meshes': mesh_rows})
    report = {'source_triangles': source_triangles, 'assigned_triangles': sum(g['triangles'] for g in groups.values()),
              'source_islands': expected_islands, 'assigned_islands': len(assignments),
              'geometry_conserved': True, 'components': len(components), 'joints': len(joint_rows),
              'roles': dict(Counter(g['role'] for g in groups.values())),
              'thermal_fields': thermal_fields,
              'scan_sha256': hashlib.sha256((scan / 'islands.json').read_bytes()).hexdigest(),
              'rig_sha256': hashlib.sha256((root / 'robot_joints.json').read_bytes()).hexdigest(),
              'limitations': ['Joint pivots reconstructed from surface geometry; approximate visual motion only.',
                              'Source hoses are retained rigid; runtime display uses authored joint-anchored spline surrogates.',
                              'No collision/clearance or certified robot-trajectory validation.',
                              'Authored casting gradients and independent tool/weld hotspots; no imported bulk heat-transfer bodies.'],
              'groups': report_groups}
    write_json(root / 'grouping_report.json', report)
    print(json.dumps({k: v for k, v in report.items() if k != 'groups'}, indent=2))

def rotation_matrix(axis, angle_deg):
    x, y, z = np.asarray(axis, dtype=float)
    cross = np.array([[0, -z, y], [z, 0, -x], [-y, x, 0]])
    angle = np.deg2rad(angle_deg)
    return np.eye(3) + np.sin(angle) * cross + (1 - np.cos(angle)) * (cross @ cross)


def joint_pose(robot, angles):
    result = {}
    for i, name in enumerate(PARENTS):
        spec = robot['joints'][name]
        parent = PARENTS[name]
        rotation, position = result.get(parent, (np.eye(3), np.zeros(3)))
        pivot = np.asarray(spec['pivot_cm'])
        relative = pivot - np.asarray(robot['joints'][parent]['pivot_cm']) if parent else pivot
        result[name] = (rotation @ rotation_matrix(spec['axis'], angles[i]), rotation @ relative + position)
    return result['tool_roll']


def closest_surface(point, triangles, preferred_normal):
    """Exact point/triangle distance, including triangle edges; no vertex proxy."""
    a, b, c = triangles[:, 0], triangles[:, 1], triangles[:, 2]
    e, f = b - a, c - a
    n = np.cross(e, f)
    nn = np.einsum('ij,ij->i', n, n)
    w = point - a
    ee = np.einsum('ij,ij->i', e, e)
    ff = np.einsum('ij,ij->i', f, f)
    ef = np.einsum('ij,ij->i', e, f)
    we = np.einsum('ij,ij->i', w, e)
    wf = np.einsum('ij,ij->i', w, f)
    denominator = ee * ff - ef * ef
    safe = np.where(np.abs(denominator) > 1e-12, denominator, 1)
    u, v = (ff * we - ef * wf) / safe, (ee * wf - ef * we) / safe
    candidate = a + u[:, None] * e + v[:, None] * f
    dist = np.where((u >= 0) & (v >= 0) & (u + v <= 1) & (nn > 1e-12),
                    np.sum((candidate - point) ** 2, axis=1), np.inf)
    for start, end in ((a, b), (b, c), (c, a)):
        edge = end - start
        ratio = np.clip(np.einsum('ij,ij->i', point - start, edge) /
                        np.maximum(np.einsum('ij,ij->i', edge, edge), 1e-20), 0, 1)
        edge_point = start + ratio[:, None] * edge
        edge_dist = np.sum((edge_point - point) ** 2, axis=1)
        replace = edge_dist < dist
        candidate[replace] = edge_point[replace]
        dist = np.minimum(dist, edge_dist)
    index = int(np.argmin(dist))
    normal = n[index] / max(float(np.linalg.norm(n[index])), 1e-12)
    if np.dot(normal, preferred_normal) < 0:
        normal *= -1
    return candidate[index], normal, float(np.sqrt(dist[index])), index


def upper_surface(xy, triangles):
    a, e, f = triangles[:, 0], triangles[:, 1]-triangles[:, 0], triangles[:, 2]-triangles[:, 0]
    determinant = e[:, 0]*f[:, 1]-e[:, 1]*f[:, 0]
    safe = np.where(np.abs(determinant)>1e-10, determinant, 1.)
    delta = xy-a[:, :2]
    u = (delta[:, 0]*f[:, 1]-delta[:, 1]*f[:, 0])/safe
    v = (e[:, 0]*delta[:, 1]-e[:, 1]*delta[:, 0])/safe
    z = a[:, 2]+u*e[:, 2]+v*f[:, 2]
    valid = (np.abs(determinant)>1e-10)&(u>=0)&(v>=0)&(u+v<=1)
    if not valid.any():
        raise ValueError('No visible body surface under candidate seam')
    index = int(np.argmax(np.where(valid,z,-np.inf)))
    normal = np.cross(e[index],f[index]); normal /= np.linalg.norm(normal)
    if normal[2]<0: normal *= -1
    if normal[2]<.75:
        raise ValueError('Candidate seam is not on an accessible top sheet')
    return np.array([xy[0],xy[1],z[index]]),normal


def solve_tool_position(robot, tip, target, initial=None):
    """Bounded damped least squares, degrees, with numeric FK Jacobian."""
    angles = np.zeros(6) if initial is None else np.asarray(initial).copy()
    for _ in range(120):
        rotation, position = joint_pose(robot, angles)
        current = rotation @ tip + position
        error = target - current
        if np.linalg.norm(error) < .015:
            return angles, float(np.linalg.norm(error))
        jacobian = np.zeros((3, 6))
        for axis in range(6):
            offset = angles.copy()
            offset[axis] += .002
            r, p = joint_pose(robot, offset)
            jacobian[:, axis] = (r @ tip + p - current) / .002
        step = jacobian.T @ np.linalg.solve(jacobian @ jacobian.T + .025 * np.eye(3), error)
        step *= min(1., 1.5 / max(np.max(np.abs(step)), 1e-12))
        angles = np.clip(angles + step, -12, 12)
    raise ValueError('Bounded electrode IK did not converge for ' + robot['id'])


def prepare_cycle(root):
    """Add deterministic process poses without changing/re-exporting any mesh."""
    scene = json.loads((root / 'scene.json').read_text(encoding='utf-8'))
    rig = json.loads((root / 'robot_joints.json').read_text(encoding='utf-8'))
    groups = json.loads((root / 'grouping_report.json').read_text(encoding='utf-8'))
    bounds = {m['id']: m for m in json.loads((root / 'manifest.json').read_text(encoding='utf-8'))['meshes']}
    body = [c for c in scene['components'] if c['id'].startswith('SM_BodyPanel_')]
    lo = np.min([np.array(bounds[c['id']]['expected_bounds_min_cm']) + c['location_cm'] for c in body], axis=0)
    hi = np.max([np.array(bounds[c['id']]['expected_bounds_max_cm']) + c['location_cm'] for c in body], axis=0)
    # Source zero-angle pose places tools at the body. Fold elbow outward while
    # raising shoulder slightly; this is authored visual motion, not certified IK.
    rest = {'base': 0, 'shoulder': 25, 'elbow': -65, 'forearm_roll': 0, 'wrist': 0, 'tool_roll': 0}
    precision = {'base': 0, 'shoulder': .10, 'elbow': .16, 'forearm_roll': .15, 'wrist': .55, 'tool_roll': .35}
    emitters = []
    body_triangles = []
    for mesh in sorted({i['mesh'] for g in groups['groups'] if g['id'].startswith('SM_BodyPanel_') for i in g['islands']}):
        points, triangles, _ = read_mesh(root / 'scan' / (mesh + '.meshbin'))
        ids = [i['island'] for g in groups['groups'] if g['id'].startswith('SM_BodyPanel_') for i in g['islands'] if i['mesh'] == mesh]
        body_triangles.append(points[triangles['v'][np.isin(triangles['island'], ids)]].astype(float))
    body_triangles = np.concatenate(body_triangles)
    scan_cache = {}
    tool_groups = {g['id']: g for g in groups['groups']}
    for robot in rig['robots']:
        for role, joint in robot['joints'].items():
            row = next(j for j in scene['joints'] if j['id'] == robot['id'] + '_' + role)
            row['rest_angle_deg'] = rest[role]
            row['weld_amplitude_deg'] = precision[role]
            row['range_deg'] = [min(rest[role], -precision[role]), max(rest[role], precision[role])]
        clouds = []
        assigned = tool_groups['SM_' + robot['id'] + '_tool']['islands']
        for mesh_name in sorted({a['mesh'] for a in assigned}):
            if mesh_name not in scan_cache:
                points, _, labels = read_mesh(root / 'scan' / (mesh_name + '.meshbin'))
                scan_cache[mesh_name] = (points, labels)
            points, labels = scan_cache[mesh_name]
            ids = [a['island'] for a in assigned if a['mesh'] == mesh_name]
            clouds.append(points[np.isin(labels, ids)])
        tool = np.concatenate(clouds).astype(float)
        pivot = np.asarray(robot['joints']['tool_roll']['pivot_cm'])
        axis = np.asarray(robot['joints']['tool_roll']['axis'])
        projection = (tool - pivot) @ axis
        end = tool[projection <= np.quantile(projection, .03)].mean(axis=0)
        tip = end - pivot
        _, _, original_gap, _ = closest_surface(end, body_triangles, axis)
        face_normals = np.cross(body_triangles[:, 1]-body_triangles[:, 0], body_triangles[:, 2]-body_triangles[:, 0])
        area2 = np.linalg.norm(face_normals, axis=1)
        accessible = (np.abs(face_normals @ axis) / np.maximum(area2, 1e-12) > .75) & (area2 > 4.)
        weld_triangles = body_triangles[accessible]
        centres = weld_triangles.mean(axis=1)
        candidates = np.argsort(np.sum((centres-end)**2,axis=1))[:80]
        # A closest point can lie on a thin underside/bevel. Choose the topmost
        # visible sheet at interior candidate positions and verify the whole seam.
        accepted = False
        for candidate in candidates:
            centre = centres[candidate]
            sites, normals, angles, residuals = [], [], [], []
            try:
                for offset in np.linspace(-1., 1., 5):
                    target, surface_normal = upper_surface(centre[:2]+np.array([0,offset]),body_triangles)
                    solved, residual = solve_tool_position(robot, tip, target+surface_normal*.6,angles[-1] if angles else None)
                    sites.append(target); normals.append(surface_normal); angles.append(solved); residuals.append(residual)
                if np.ptp(np.array(sites)[:,2])>3:
                    continue
                distances = []
                for segment in range(4):
                    for alpha in np.linspace(0.,1.,9):
                        q=angles[segment]*(1-alpha)+angles[segment+1]*alpha
                        rotation,position=joint_pose(robot,q)
                        _,_,distance,_=closest_surface(rotation@tip+position,body_triangles,axis)
                        distances.append(distance)
                if all(.1<=d<=1.2 for d in distances):
                    accepted=True
                    break
            except ValueError:
                continue
        if not accepted:
            raise ValueError('No reachable, continuously clear electrode contact patch: '+robot['id'])
        for index, role in enumerate(PARENTS):
            row = next(j for j in scene['joints'] if j['id'] == robot['id'] + '_' + role)
            row['work_angle_deg'] = float(angles[0][index])
            row['weld_angles_deg'] = [float(q[index]) for q in angles]
            row['weld_amplitude_deg'] = 0
            row['range_deg'] = [min(rest[role], *row['weld_angles_deg']), max(rest[role], *row['weld_angles_deg'])]
        emitters.append({'id': robot['id'], 'parent_joint': robot['id'] + '_tool_roll',
                         'location_cm': tip.tolist(), 'direction': (-axis).tolist(),
                         'surface_positions_cm': [p.tolist() for p in sites],
                         'surface_normals': [n.tolist() for n in normals],
                         'position_source': 'distal physical electrode; bounded FK solved to actual body triangles',
                         'contact_qa': {'original_surface_gap_cm': original_gap, 'target_standoff_cm': .6,
                                        'max_ik_residual_cm': max(residuals), 'max_abs_work_angle_deg': float(np.max(np.abs(angles))),
                                        'interpolated_standoff_min_cm': min(distances), 'interpolated_standoff_max_cm': max(distances)}})
    # Rail-end portals: clear the complete body inside the covered extensions.
    arrival = min(-1800.0, PORTAL_ENTRY[1]-50.0-float(hi[1]))
    departure = max(1800.0, PORTAL_EXIT[1]+50.0-float(lo[1]))
    scene['cycle'] = {'version': 1, 'duration_seconds': CYCLE_DURATION_SECONDS,
                      'phase_boundaries_seconds': [0, 3, 4.5, 12, 13.5, 17, CYCLE_DURATION_SECONDS],
                      'arrival_offset_cm': arrival, 'departure_offset_cm': departure,
                      'working_body_bounds_min_cm': lo.tolist(), 'working_body_bounds_max_cm': hi.tolist(),
                      'emitters': emitters,
                      'scope': 'Authored synchronized visual process; no industrial trajectory or inspection validation.'}
    update_park_cycle(scene)
    write_json(root / 'scene.json', scene)
    print(json.dumps({'cycle_seconds': CYCLE_DURATION_SECONDS, 'body_panels': len(body), 'body_min_cm': lo.tolist(),
                      'body_max_cm': hi.tolist(), 'arrival_offset_cm': arrival,
                      'departure_offset_cm': departure, 'emitters': emitters}, indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=Path('assets/derived/welding_cell'))
    parser.add_argument('--cycle-only', action='store_true', help='Recompute cycle metadata and IK seam knots; preserve all mesh payloads')
    parser.add_argument('--approach-only', action='store_true', help='Update lifted approach keys only; preserve geometry, thermal profiles and solved weld angles')
    parser.add_argument('--holders-only', action='store_true', help='Split two holder assemblies and update park/travel metadata; retain solved IK')
    args = parser.parse_args()
    root = args.output.resolve()
    if args.approach_only:
        if args.holders_only or args.cycle_only:
            parser.error('Choose approach-only, holders-only or cycle-only')
        scene=json.loads((root/'scene.json').read_text(encoding='utf-8'))
        print(json.dumps(update_approach_choreography(scene),indent=2))
        write_json(root/'scene.json',scene)
    elif args.holders_only:
        if args.cycle_only:
            parser.error('Choose holders-only or cycle-only')
        print(json.dumps(prepare_carriage_holders(root),indent=2))
        scene=json.loads((root/'scene.json').read_text(encoding='utf-8'))
        update_park_cycle(scene)
        write_json(root/'scene.json',scene)
    else:
        if not args.cycle_only:
            prepare(root)
            prepare_carriage_holders(root)
        prepare_cycle(root)
