#!/usr/bin/env python3
"""Check frozen Unreal robot choreography exports; requires Pillow, no engine/model.

This verifies sampled transforms, authored endpoints and actual screenshot changes.
It does not certify continuous collision clearance, industrial motion or robot speed.
"""
from __future__ import annotations

import argparse
import bisect
import hashlib
import json
import math
from pathlib import Path

try:
    from PIL import Image, ImageChops, ImageStat
except ImportError:
    raise SystemExit('Pillow is required; use the existing environment documented in docs/thermal_qa.md.')

KNOTS = (0., .20, .56, .80, 1.)
SHOT_PROGRESS = (0., .1, .2, .38, .56, .68, .8, .9, 1.)
SAMPLE_PROGRESS = sorted([i/100 for i in range(101)] + [p+d for p in KNOTS[1:-1] for d in (-.0001, .0001)])
SCENE_CROP = (32, 135, 992, 711)
THRESHOLDS = {
    'joint_count': 24, 'robot_count': 4, 'trajectory_samples': 107,
    'cycle_seconds': 18., 'approach_start_seconds': 3.5, 'approach_duration_seconds': 1.,
    'withdraw_start_seconds': 12., 'withdraw_duration_seconds': 1.,
    'numeric_angle_tolerance_deg': .001, 'position_tolerance_cm': .01,
    'join_angle_difference_max_deg': .02, 'join_tip_difference_max_cm': .1,
    'tip_path_length_min_cm': 750., 'tip_peak_height_min_cm': 400.,
    'nonbase_excursion_min_deg': 15., 'nonbase_excursion_joint_count_min': 2,
    'quintic_max_normalized_derivative': 1.875,
    'changed_pixel_min_u8': 5, 'changed_scene_fraction_min': .01,
}


def finite(value):
    return type(value) in (int, float) and math.isfinite(value)


def near(value, target, tolerance=.001):
    return finite(value) and finite(target) and abs(value-target) <= tolerance


def vector(value, length=3):
    return isinstance(value, list) and len(value) == length and all(finite(v) for v in value)


def distance(a, b):
    return math.dist(a, b)


def mapping(value):
    return value if isinstance(value, dict) else {}


def rows(value):
    result = {}
    if not isinstance(value, list):
        return result
    for row in value:
        row = mapping(row)
        key = row.get('id')
        if not isinstance(key, str) or not key or key in result:
            return {}
        result[key] = row
    return result


def reject_constant(value):
    raise ValueError('Nonfinite JSON number: '+value)


def verify(directory):
    result = {
        'scope': 'Actual Unreal screenshots and dense sampled joint/tip transforms; authored visual choreography, not continuous collision or industrial motion certification.',
        'directory': str(directory.resolve()), 'thresholds': THRESHOLDS,
        'checks': {}, 'metrics': {}, 'files': {}, 'errors': [],
    }
    checks, metrics = result['checks'], result['metrics']

    def require(name, passed):
        checks[name] = bool(passed)

    try:
        data = json.loads((directory/'robot_choreography.json').read_text(encoding='utf-8-sig'), parse_constant=reject_constant)
        if not isinstance(data, dict):
            raise ValueError('Expected a JSON object')
        require('runtime_json_valid', True)
    except (OSError, ValueError) as exc:
        data = {}
        require('runtime_json_valid', False)
        result['errors'].append(str(exc))
    require('runtime_passed', data.get('passed') is True)
    for key in ('approach_start_seconds', 'approach_duration_seconds', 'withdraw_start_seconds', 'withdraw_duration_seconds'):
        require(key, near(data.get(key), THRESHOLDS[key]))

    trajectory = data.get('trajectory', [])
    valid_samples = isinstance(trajectory, list) and len(trajectory) == len(SAMPLE_PROGRESS)
    if valid_samples:
        valid_samples = all(isinstance(row, dict) and finite(row.get('progress')) for row in trajectory)
    trajectory = sorted(trajectory, key=lambda row: row['progress']) if valid_samples else []
    require('dense_progress_samples', valid_samples and all(near(row['progress'], p, 1.e-7) for row, p in zip(trajectory, SAMPLE_PROGRESS)))
    reference = mapping(trajectory[0].get('approach')) if trajectory else {}
    require('reference_geometry_present', all(vector(reference.get(k)) for k in ('working_body_bounds_min_cm', 'working_body_bounds_max_cm', 'carriage_bounds_min_cm', 'carriage_bounds_max_cm'))
            and len(rows(reference.get('carriage_holders'))) == 2)
    joint_refs = rows(reference.get('joints'))
    tip_refs = rows(reference.get('robot_tips'))
    require('reference_has_24_unique_joints', len(joint_refs) == 24)
    require('reference_has_four_unique_tips', len(tip_refs) == 4)
    robot_ids = sorted(tip_refs)
    per_robot = {rid: sorted(jid for jid in joint_refs if jid.startswith(rid+'_')) for rid in robot_ids}
    require('four_six_joint_robots', len(per_robot) == 4 and all(len(ids) == 6 for ids in per_robot.values()))
    declarations_valid = bool(joint_refs) and all(
        all(finite(j.get(k)) for k in ('angle_deg', 'minimum_deg', 'maximum_deg', 'rest_angle_deg'))
        and j['minimum_deg'] <= j['maximum_deg']
        and vector(j.get('approach_angles_deg'), 3) and vector(j.get('weld_angles_deg'), 5)
        and all(j['minimum_deg']-.001 <= a <= j['maximum_deg']+.001
                for a in [j['rest_angle_deg'], *j['approach_angles_deg'], *j['weld_angles_deg']])
        for j in joint_refs.values())
    require('authored_joint_declarations_valid', declarations_valid)

    paths = {'approach': [], 'withdraw': []}
    for direction in paths:
        paths[direction] = [mapping(row.get(direction)) for row in trajectory]
        poses = paths[direction]
        require(direction+'_pose_count', len(poses) == 107)
        require(direction+'_fixed_carriage_and_contract', bool(poses) and all(
            p.get('motion_profile') == 'dramatic-approach-v1'
            and p.get('approach_keyframe_normalized_times') == list(KNOTS)
            and near(p.get('cycle_duration_seconds'), 18.)
            and near(p.get('robot_approach_seconds'), 1.) and near(p.get('robot_withdraw_seconds'), 1.)
            and near(p.get('carriage_offset_y_cm'), 0., .01)
            and p.get('body_visible') is True and p.get('body_index') == 0
            and p.get('inference_enabled') is False and p.get('training_enabled') is False
            and p.get('mesh_count') == 76 and p.get('body_panel_count') == 29 and p.get('carriage_holder_count') == 2
            for p in poses))
        require(direction+'_actual_pose_times', bool(poses) and all(
            near(p.get('motion_seconds'), 3.5+t['progress'] if direction == 'approach' else 13.-t['progress'], 1.e-6)
            for p, t in zip(poses, trajectory)))
        require(direction+'_stable_finite_joint_and_tip_identity', bool(poses) and all(
            set(rows(p.get('joints'))) == set(joint_refs) and set(rows(p.get('robot_tips'))) == set(tip_refs)
            and all(finite(j.get('angle_deg')) and vector(j.get('joint_pivot_world_cm')) for j in rows(p.get('joints')).values())
            and all(vector(t.get('position_cm')) and vector(t.get('direction')) and near(math.sqrt(sum(x*x for x in t['direction'])), 1., .001)
                    for t in rows(p.get('robot_tips')).values())
            for p in poses))
        require(direction+'_declared_geometry_fixed', bool(poses) and all(
            all(p.get(key) == reference.get(key) for key in ('working_body_bounds_min_cm', 'working_body_bounds_max_cm', 'carriage_bounds_min_cm', 'carriage_bounds_max_cm', 'carriage_holders'))
            for p in poses))

    usable = declarations_valid and len(trajectory) == 107 and all(
        checks.get(d+'_stable_finite_joint_and_tip_identity') for d in paths)
    metrics['joints'] = {}
    metrics['robots'] = {}
    if usable:
        joint_paths = {d: [rows(p['joints']) for p in poses] for d, poses in paths.items()}
        tip_paths = {d: [rows(p['robot_tips']) for p in poses] for d, poses in paths.items()}
        index = lambda p: min(range(len(trajectory)), key=lambda i: abs(trajectory[i]['progress']-p))
        for jid, ref in joint_refs.items():
            metric = {'intermediate_angles_deg': ref['approach_angles_deg'], 'rest_angle_deg': ref['rest_angle_deg'],
                      'first_weld_angle_deg': ref['weld_angles_deg'][0], 'last_weld_angle_deg': ref['weld_angles_deg'][-1]}
            for direction in paths:
                js = [pose[jid] for pose in joint_paths[direction]]
                values = [j['angle_deg'] for j in js]
                keys = [ref['rest_angle_deg'], *ref['approach_angles_deg'], ref['weld_angles_deg'][0 if direction == 'approach' else -1]]
                require(jid+'_'+direction+'_unchanged_declarations_and_limits', all(
                    all(j.get(k) == ref[k] for k in ('minimum_deg', 'maximum_deg', 'rest_angle_deg', 'approach_angles_deg', 'weld_angles_deg'))
                    and ref['minimum_deg']-.001 <= j['angle_deg'] <= ref['maximum_deg']+.001 for j in js))
                require(jid+'_'+direction+'_exact_authored_knots', all(near(values[index(p)], angle) for p, angle in zip(KNOTS, keys)))
                inside, step_bounded = True, True
                max_slope = max(abs(keys[i+1]-keys[i])/(KNOTS[i+1]-KNOTS[i]) for i in range(4))*1.875
                for i, row in enumerate(trajectory):
                    s = min(3, max(0, bisect.bisect_right(KNOTS, row['progress'])-1))
                    inside &= min(keys[s:s+2])-.001 <= values[i] <= max(keys[s:s+2])+.001
                    if i:
                        step_bounded &= abs(values[i]-values[i-1]) <= max_slope*(row['progress']-trajectory[i-1]['progress'])+.001
                require(jid+'_'+direction+'_segment_bounds', inside)
                require(jid+'_'+direction+'_dense_steps_bounded', step_bounded)
                metric[direction+'_max_sampled_step_deg'] = max(abs(b-a) for a, b in zip(values, values[1:]))
                metric[direction+'_max_join_difference_deg'] = max(abs(values[index(p+.0001)]-values[index(p-.0001)]) for p in KNOTS[1:-1])
                require(jid+'_'+direction+'_joins_continuous', metric[direction+'_max_join_difference_deg'] <= .02)
            require(jid+'_intermediate_reverse_same_pose', all(
                near(joint_paths['approach'][i][jid]['angle_deg'], joint_paths['withdraw'][i][jid]['angle_deg'])
                and distance(joint_paths['approach'][i][jid]['joint_pivot_world_cm'], joint_paths['withdraw'][i][jid]['joint_pivot_world_cm']) <= .01
                for i, row in enumerate(trajectory) if row['progress'] <= .8+1.e-8))
            metrics['joints'][jid] = metric
        for rid, joint_ids in per_robot.items():
            metric = {}
            bases = [jid for jid in joint_ids if jid.endswith('_base')]
            require(rid+'_one_180_degree_parked_base', len(bases) == 1 and near(abs(joint_refs[bases[0]]['rest_angle_deg']), 180.))
            excursions = {}
            for jid in joint_ids:
                if jid in bases:
                    continue
                ref = joint_refs[jid]
                endpoints = [ref['rest_angle_deg'], ref['weld_angles_deg'][0], ref['weld_angles_deg'][-1]]
                excursions[jid] = max(max(min(endpoints)-v, v-max(endpoints), 0.) for v in ref['approach_angles_deg'])
            metric['nonbase_excursion_beyond_endpoint_range_deg'] = excursions
            require(rid+'_large_nonbase_excursions', sum(v >= 15. for v in excursions.values()) >= 2)
            for direction in paths:
                points = [pose[rid]['position_cm'] for pose in tip_paths[direction]]
                arc = sum(distance(a, b) for a, b in zip(points, points[1:]))
                chord = distance(points[0], points[-1])
                height = max(p[2] for p in points)
                joins = max(distance(points[index(p-.0001)], points[index(p+.0001)]) for p in KNOTS[1:-1])
                metric[direction] = {'sampled_tip_path_length_cm': arc, 'endpoint_chord_cm': chord,
                                     'path_to_chord_ratio': arc/chord if chord else None, 'peak_tip_z_cm': height,
                                     'max_sampled_tip_step_cm': max(distance(a, b) for a, b in zip(points, points[1:])),
                                     'max_join_tip_difference_cm': joins}
                require(rid+'_'+direction+'_large_tip_sweep', arc >= 750. and height >= 400.)
                require(rid+'_'+direction+'_tip_joins_continuous', joins <= .1)
            require(rid+'_intermediate_tip_reverse_same_pose', all(
                distance(tip_paths['approach'][i][rid]['position_cm'], tip_paths['withdraw'][i][rid]['position_cm']) <= .01
                for i, row in enumerate(trajectory) if row['progress'] <= .8+1.e-8))
            metrics['robots'][rid] = metric

    shots = data.get('shots', [])
    valid_shots = isinstance(shots, list) and len(shots) == 9 and all(isinstance(s, dict) for s in shots)
    require('nine_declared_approach_images', valid_shots and all(near(s.get('progress'), p, 1.e-7) for s, p in zip(shots, SHOT_PROGRESS)))
    images = {}
    for i in range(10):
        filename = f'robot_approach_{i:02d}.png' if i < 9 else 'robot_withdrawn.png'
        try:
            path = directory/filename
            payload = path.read_bytes()
            with Image.open(path) as image:
                if image.format != 'PNG' or image.size != (1600, 900) or image.mode not in ('RGB', 'RGBA'):
                    raise ValueError('Expected 1600 x 900 RGB PNG')
                if image.mode == 'RGBA' and image.getchannel('A').getextrema() != (255, 255):
                    raise ValueError('Expected opaque PNG')
                images[i] = image.convert('RGB').crop(SCENE_CROP).convert('L')
            result['files'][filename] = {'bytes': len(payload), 'sha256': hashlib.sha256(payload).hexdigest()}
            require(filename+'_valid', True)
        except (OSError, ValueError) as exc:
            require(filename+'_valid', False)
            result['errors'].append(filename+': '+str(exc))
        if i < 9 and valid_shots:
            shot = shots[i]
            require(filename+'_declared_filename', shot.get('file') == filename)
            pose = mapping(shot.get('pose'))
            target = joint_paths['approach'][index(SHOT_PROGRESS[i])] if usable else {}
            actual = rows(pose.get('joints'))
            require(filename+'_pose_matches_trajectory', bool(target) and set(actual) == set(target)
                    and near(pose.get('motion_seconds'), 3.5+SHOT_PROGRESS[i])
                    and all(near(actual[jid].get('angle_deg'), j['angle_deg']) for jid, j in target.items()))
    withdrawn = mapping(data.get('withdrawn_pose'))
    require('withdrawn_pose_at_13_seconds', near(withdrawn.get('motion_seconds'), 13.) and near(withdrawn.get('carriage_offset_y_cm'), 0., .01))
    withdrawn_joints = rows(withdrawn.get('joints'))
    require('withdrawn_pose_returns_to_all_rest_angles', bool(joint_refs) and set(withdrawn_joints) == set(joint_refs)
            and all(near(withdrawn_joints[jid].get('angle_deg'), j.get('rest_angle_deg')) for jid, j in joint_refs.items()))
    metrics['scene_crop_excluding_hud_px'] = SCENE_CROP
    metrics['image_changes'] = {}
    for a, b, label in ((0, 4, 'rest_to_raised'), (4, 8, 'raised_to_work'), (0, 8, 'rest_to_work')):
        if a in images and b in images:
            diff = ImageChops.difference(images[a], images[b])
            fraction = sum(diff.histogram()[5:])/(diff.width*diff.height)
            metrics['image_changes'][label] = {'mean_absolute_luma_difference_u8': ImageStat.Stat(diff).mean[0], 'fraction_changed_at_least_5_u8': fraction}
            require(label+'_visibly_changes', fraction >= .01)
        else:
            require(label+'_visibly_changes', False)
    result['failed_checks'] = [key for key, passed in checks.items() if not passed]
    result['passed'] = not result['failed_checks'] and not result['errors']
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--directory', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    result = verify(args.directory)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2, allow_nan=False)+'\n', encoding='utf-8', newline='\n')
    print(json.dumps({'passed': result['passed'], 'check_count': len(result['checks']), 'failed_checks': result['failed_checks'], 'errors': result['errors'], 'report': str(args.output.resolve())}, indent=2))
    return 0 if result['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
