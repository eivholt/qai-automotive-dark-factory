#!/usr/bin/env python3
"""Verify native robot timing, mounted IQ9 props, isolated NIR and HUD FPS.

Pillow only. Reads existing Unreal captures; never launches a simulator/model.
The photographed props and authored schedule are not hardware telemetry or
industrial motion/clearance certification.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import math
from pathlib import Path

try:
    from PIL import Image, ImageChops, ImageStat
except ImportError:
    raise SystemExit('Pillow is required; see docs/thermal_qa.md for the local environment.')

THRESHOLDS = {
    'cycle_seconds': 21., 'bank_cycles': 16, 'robot_count': 4,
    'rest_delay_min_seconds': .2, 'rest_delay_max_seconds': .8,
    'pre_weld_delay_min_seconds': .5, 'pre_weld_delay_max_seconds': 2.,
    'approach_seconds': 1., 'weld_seconds': 7.5, 'withdraw_seconds': 1.,
    'minimum_carriage_margin_seconds': .5,
    'minimum_nir_scene_difference_u8': 1.,
    'minimum_nir_changed_fraction_at_least_3_u8': .01,
    'maximum_raw_rgb_spread_u8': 0,
    'minimum_closeup_stddev_u8': 3.,
    'maximum_seek_numeric_difference': 1e-9,
}
RAW_SIZE, SCREEN_SIZE = (448, 256), (1600, 900)
SCENE_CROP = (32, 135, 992, 711)
ROBOT_PHASES = ('rest_delay', 'approach', 'pre_weld_wait', 'welding', 'withdrawal', 'parked')
PRODUCTION_NAMES = ('rest', 'approach_stagger', 'weld_stagger', 'all_welding', 'parked', 'departure')
NIR_NAMES = ('nir_home_on', 'nir_home_off', 'nir_moved_on', 'visible_home')


def obj(v):
    return v if isinstance(v, dict) else {}


def rows(v):
    return v if isinstance(v, list) else []


def finite(v):
    return type(v) in (int, float) and math.isfinite(v)


def integer(v):
    return finite(v) and int(v) == v


def near(a, b, tolerance=1e-5):
    return finite(a) and finite(b) and abs(a-b) <= tolerance


def vector(v):
    return isinstance(v, list) and len(v) == 3 and all(finite(x) for x in v)


def distance(a, b):
    return math.sqrt(sum((x-y)**2 for x, y in zip(a, b))) if vector(a) and vector(b) else math.inf


def identified(v, key='id'):
    result = {}
    if not isinstance(v, list):
        return result
    for item in v:
        item = obj(item)
        identity = item.get(key)
        if not isinstance(identity, str) or not identity or identity in result:
            return {}
        result[identity] = item
    return result


def difference(a, b):
    diff = ImageChops.difference(a, b)
    r, g, blue = diff.split()
    maximum = ImageChops.lighter(r, ImageChops.lighter(g, blue))
    hist = maximum.histogram()
    return {'mean_absolute_difference_u8': sum(ImageStat.Stat(diff).mean)/3,
            'changed_fraction_at_least_3_u8': sum(hist[3:])/(maximum.width*maximum.height),
            'maximum_channel_difference_u8': maximum.getextrema()[1]}


def spread(image):
    r, g, b = image.split()
    return ImageChops.subtract(ImageChops.lighter(r, ImageChops.lighter(g, b)),
                               ImageChops.darker(r, ImageChops.darker(g, b))).getextrema()[1]


def reject_constant(v):
    raise ValueError('Nonfinite JSON constant: '+v)


def compare_seek(reference, restored, tolerance=1e-9):
    """Require identical JSON structure; bound only finite numeric roundoff."""
    result = {'maximum_numeric_difference': 0., 'numeric_difference_paths': [], 'mismatches': []}

    def compare(first, second, path):
        if isinstance(first, dict) and isinstance(second, dict):
            if set(first) != set(second):
                result['mismatches'].append(path+': object keys differ')
            for key in sorted(set(first)&set(second)):
                compare(first[key], second[key], path+'.'+key)
        elif isinstance(first, list) and isinstance(second, list):
            if len(first) != len(second):
                result['mismatches'].append(path+': array lengths differ')
            for index, (a, b) in enumerate(zip(first, second)):
                compare(a, b, path+'['+str(index)+']')
        elif finite(first) and finite(second):
            delta = abs(first-second)
            result['maximum_numeric_difference'] = max(result['maximum_numeric_difference'], delta)
            if delta:
                result['numeric_difference_paths'].append({'path': path, 'absolute_difference': delta})
            if delta > tolerance:
                result['mismatches'].append(path+': numeric difference exceeds tolerance')
        elif type(first) is not type(second) or first != second:
            result['mismatches'].append(path+': nonnumeric value/type differs')
    compare(reference, restored, 'seek')
    result['passed'] = not result['mismatches']
    return result


def exact_seek_values(value, path='seek'):
    """Collect authored angles, heat values and clocks without transform arrays."""
    selected = {}
    exact_keys = {'motion_seconds', 'cycle_seconds', 'cycle_index', 'body_index', 'heat_time_seconds',
                  'duty_time_seconds', 'idle_seconds', 'angle_deg', 'rest_angle_deg', 'approach_angles_deg',
                  'weld_angles_deg', 'glow_strength'}
    if isinstance(value, dict):
        for key, item in value.items():
            child = path+'.'+key
            if key in exact_keys or 'temperature_c' in key:
                selected[child] = item
            else:
                selected.update(exact_seek_values(item, child))
    elif isinstance(value, list):
        for index, item in enumerate(value):
            selected.update(exact_seek_values(item, path+'['+str(index)+']'))
    return selected


def verify(directory):
    directory = directory.resolve()
    result = {'scope': 'Actual native timing/attachment metadata and rendered observer NIR/props; no industrial safety or EVK telemetry claim.',
              'directory': str(directory), 'thresholds': THRESHOLDS, 'checks': {}, 'metrics': {}, 'files': {}, 'errors': []}
    checks, metrics = result['checks'], result['metrics']
    images, payloads = {}, {}

    def require(name, passed):
        checks[name] = bool(passed)

    def load(key, filename, raw=False):
        try:
            if not isinstance(filename, str) or not filename or Path(filename).name != filename or '\\' in filename:
                raise ValueError('Expected capture filename inside directory')
            data = (directory/filename).read_bytes()
            with Image.open(directory/filename) as source:
                if source.format != 'PNG' or source.size != (RAW_SIZE if raw else SCREEN_SIZE) or source.mode not in ('L', 'RGB', 'RGBA'):
                    raise ValueError('Expected 8-bit PNG of declared capture dimensions')
                if source.mode == 'RGBA' and source.getchannel('A').getextrema() != (255, 255):
                    raise ValueError('Expected opaque capture')
                image = source.convert('RGB')
            images[key], payloads[key] = image, data
            result['files'][key] = {'name': filename, 'sha256': hashlib.sha256(data).hexdigest(), 'bytes': len(data), 'size': image.size}
            require(key+'_valid_png', True)
            if raw:
                require(key+'_raw_grayscale', spread(image) == 0)
        except (OSError, ValueError) as exc:
            require(key+'_valid_png', False)
            result['errors'].append(key+': '+str(exc))

    try:
        data = json.loads((directory/'robot_expansion.json').read_text(encoding='utf-8-sig'), parse_constant=reject_constant)
        if not isinstance(data, dict):
            raise ValueError('Expected object')
        require('runtime_json_valid', True)
    except (OSError, ValueError) as exc:
        data = {}
        require('runtime_json_valid', False)
        result['errors'].append(str(exc))
    for flag in ('passed', 'delays_bounded', 'emitters_gated_by_robot_clock', 'seek_order_independent',
                 'fixed_nir_unchanged_by_observer', 'fixed_thermal_unchanged_by_observer', 'fps_updates_while_scene_paused'):
        require('runtime_'+flag, data.get(flag) is True)
    require('default_cycle_is_21_seconds', near(data.get('cycle_duration_seconds'), 21.))
    seek_before, seek_after = obj(data.get('seek_reference')), obj(data.get('seek_restored'))
    seek_result = compare_seek(seek_before, seek_after)
    metrics['seek_comparison'] = seek_result
    require('seek_snapshots_present_at_same_requested_time', bool(seek_before) and bool(seek_after)
            and near(seek_before.get('motion_seconds'), 9.25, 0.) and near(seek_after.get('motion_seconds'), 9.25, 0.))
    require('seek_structure_and_numeric_roundoff_bounded', bool(seek_before) and bool(seek_after) and seek_result['passed'])
    require('native_seek_tolerance_and_error_match', finite(data.get('seek_numeric_tolerance')) and 0 < data['seek_numeric_tolerance'] <= 1e-9
            and finite(data.get('seek_max_numeric_error')) and 0 <= data['seek_max_numeric_error'] <= data['seek_numeric_tolerance']
            and near(data['seek_max_numeric_error'], seek_result['maximum_numeric_difference'], 1e-15))
    exact_before, exact_after = exact_seek_values(seek_before), exact_seek_values(seek_after)
    require('seek_authored_angles_heat_and_clocks_exact', bool(exact_before) and exact_before == exact_after)
    for key in ('pose', 'tool_heat', 'body_weld_heat', 'motor_heat'):
        first = seek_before if key == 'pose' else obj(seek_before.get(key))
        second = seek_after if key == 'pose' else obj(seek_after.get(key))
        require('seek_'+key+'_robot_timing_exact', bool(obj(first.get('robot_timing')))
                and first.get('robot_timing') == second.get('robot_timing'))
    bank = rows(data.get('schedule_bank'))
    require('sixteen_schedule_entries', len(bank) == 16)
    baseline_ids = set(identified(obj(bank[0]).get('robots'))) if bank else set()
    require('four_stable_robot_identities', len(baseline_ids) == 4)
    robot_indices, delay_history = {}, {identity: [] for identity in baseline_ids}
    schedules = {}

    def timing_checks(prefix, timing, expected_cycle=None):
        timing = obj(timing)
        robots = identified(timing.get('robots'))
        require(prefix+'_default_timing_profile', timing.get('profile') == 'seeded-robot-delays-v1' and timing.get('randomized') is True
                and timing.get('schedule_bank_cycles') == 16 and near(timing.get('cycle_duration_seconds'), 21.))
        require(prefix+'_identities', set(robots) == baseline_ids and len(robots) == 4)
        require(prefix+'_carriage_window', near(timing.get('carriage_departure_start_seconds'), 16.3)
                and near(timing.get('carriage_departure_end_seconds'), 19.8))
        finishes = []
        for identity, robot in robots.items():
            label = prefix+'_'+identity
            fields = ('rest_delay_seconds', 'pre_weld_delay_seconds', 'approach_start_seconds', 'approach_end_seconds',
                      'weld_start_seconds', 'weld_end_seconds', 'withdraw_end_seconds', 'cycle_seconds')
            valid = all(finite(robot.get(k)) for k in fields)
            require(label+'_finite_clocks', valid)
            if not valid:
                continue
            require(label+'_delay_bounds', .2 <= robot['rest_delay_seconds'] <= .8 and .5 <= robot['pre_weld_delay_seconds'] <= 2.)
            require(label+'_full_motion_and_weld_windows', near(robot['approach_start_seconds'], 3.5+robot['rest_delay_seconds'])
                    and near(robot['approach_end_seconds']-robot['approach_start_seconds'], 1.)
                    and near(robot['weld_start_seconds']-robot['approach_end_seconds'], robot['pre_weld_delay_seconds'])
                    and near(robot['weld_end_seconds']-robot['weld_start_seconds'], 7.5)
                    and near(robot['withdraw_end_seconds']-robot['weld_end_seconds'], 1.))
            cycle, index = robot.get('cycle_index'), robot.get('robot_index')
            require(label+'_cycle_and_index', integer(cycle) and cycle >= 0 and integer(index) and 0 <= index < 4
                    and robot.get('schedule_index') == cycle % 16 and (expected_cycle is None or cycle == expected_cycle))
            if identity in robot_indices:
                require(label+'_stable_robot_index', index == robot_indices[identity])
            else:
                robot_indices[identity] = index
            t = robot['cycle_seconds']
            bounds = [robot[k] for k in ('approach_start_seconds', 'approach_end_seconds', 'weld_start_seconds', 'weld_end_seconds', 'withdraw_end_seconds')]
            expected_phase = next((phase for phase, end in zip(ROBOT_PHASES, bounds) if t < end), 'parked')
            active = robot['weld_start_seconds'] <= t < robot['weld_end_seconds']
            require(label+'_phase_matches_clock', 0 <= t < 21. and robot.get('phase') == expected_phase and robot.get('welding_active') is active
                    and near(robot.get('weld_age_seconds'), max(0., min(7.5, t-robot['weld_start_seconds']))))
            finishes.append(robot['withdraw_end_seconds'])
        latest = max(finishes) if finishes else math.inf
        require(prefix+'_robots_park_before_carriage', len(finishes) == 4 and latest <= 15.8+1e-6
                and near(timing.get('latest_robot_parked_seconds'), latest)
                and near(timing.get('carriage_clearance_margin_seconds'), 16.3-latest)
                and 16.3-latest >= .5-1e-6)
        return robots

    for cycle, timing in enumerate(bank):
        robots = timing_checks('bank_'+str(cycle), timing, cycle)
        schedules[cycle] = robots
        for identity, robot in robots.items():
            if identity in delay_history:
                delay_history[identity].append((robot.get('rest_delay_seconds'), robot.get('pre_weld_delay_seconds')))
    require('robot_indices_are_sparse_safe_unique', set(robot_indices.values()) == set(range(4)))
    for identity, history in delay_history.items():
        require(identity+'_delays_vary_across_cycle_bank', len(history) == 16 and len(set(history)) >= 4)
    require('schedule_seed_is_stable', len(bank) == 16 and all(integer(obj(t).get('seed')) for t in bank)
            and len({obj(t).get('seed') for t in bank}) == 1)
    metrics['schedule_bank'] = bank
    probes = rows(data.get('probes'))
    require('timing_probes_cover_two_cycles', len(probes) >= 40)
    seen_phases = {identity: set() for identity in baseline_ids}
    seen_sparse, covered_cycles = False, set()
    attachment_records = []
    heat_history = {}
    for number, probe in enumerate(probes):
        prefix = 'probe_'+str(number)
        probe = obj(probe)
        pose, sparks = obj(probe.get('pose')), obj(probe.get('sparks'))
        absolute = probe.get('absolute_seconds')
        valid_time = finite(absolute) and absolute >= 0
        require(prefix+'_pose_clock_and_guards', valid_time and near(pose.get('motion_seconds'), absolute)
                and pose.get('inference_enabled') is False and pose.get('training_enabled') is False and pose.get('animation_enabled') is False)
        cycle = int(absolute//21) if valid_time else -1
        covered_cycles.add(cycle)
        robots = timing_checks(prefix, pose.get('robot_timing'), cycle)
        expected = {identity for identity, row in robots.items() if row.get('welding_active') is True}
        emitters, pulses = identified(pose.get('active_weld_emitters')), identified(sparks.get('pulse_emitters'))
        require(prefix+'_exact_active_emitter_set', set(emitters) == expected and set(pulses) == expected)
        require(prefix+'_independent_pulse_clock', near(sparks.get('pulse_stagger_seconds'), 0.))
        if expected and len(expected) < 4:
            seen_sparse = seen_sparse or {robot_indices.get(identity) for identity in expected} != set(range(len(expected)))
        for identity, robot in robots.items():
            if identity in seen_phases:
                seen_phases[identity].add(robot.get('phase'))
            reference = obj(schedules.get(cycle % 16, {}).get(identity))
            require(prefix+'_'+identity+'_schedule_matches_bank', all(near(robot.get(k), reference.get(k)) for k in
                    ('rest_delay_seconds', 'pre_weld_delay_seconds', 'approach_start_seconds', 'weld_start_seconds', 'weld_end_seconds', 'withdraw_end_seconds')))
        for identity in expected:
            emitter, pulse, robot = obj(emitters.get(identity)), obj(pulses.get(identity)), obj(robots.get(identity))
            age = robot.get('weld_age_seconds')
            valid_age = finite(age) and 0 <= age < 7.5
            require(prefix+'_'+identity+'_emitter_clock_and_surface', valid_age and emitter.get('emitter_index') == robot_indices.get(identity)
                    and pulse.get('emitter_index') == robot_indices.get(identity) and near(emitter.get('weld_duration_seconds'), 7.5)
                    and near(emitter.get('weld_age_seconds'), age) and vector(emitter.get('surface_position_cm'))
                    and vector(emitter.get('surface_normal')) and finite(emitter.get('tip_to_surface_cm')) and 0 <= emitter['tip_to_surface_cm'] < 2.)
            pulse_index = int(age//1.05) if valid_age else -1
            require(prefix+'_'+identity+'_pulse_uses_local_weld_age', valid_age and pulse.get('pulse_index') == pulse_index
                    and near(pulse.get('pulse_age_seconds'), age-pulse_index*1.05, 1e-4))
        tool_heat, body_heat, motor_heat = [obj(pose.get(k)) for k in ('tool_heat', 'body_weld_heat', 'motor_heat')]
        tools, spots = identified(tool_heat.get('tools')), identified(body_heat.get('spots'))
        require(prefix+'_heat_clocks_follow_pose', near(tool_heat.get('heat_time_seconds'), absolute)
                and near(body_heat.get('heat_time_seconds'), absolute) and near(motor_heat.get('duty_time_seconds'), absolute)
                and body_heat.get('body_index') == cycle)
        require(prefix+'_heat_identity_mapping_complete', set(tools) == set(spots) == baseline_ids)
        motor_profiles = rows(motor_heat.get('profiles'))
        require(prefix+'_motor_heat_bounded', len(motor_profiles) == 28 and all(finite(obj(m).get('temperature_c'))
                and finite(obj(m).get('maximum_temperature_c')) and 20.-1e-4 <= m['temperature_c'] <= m['maximum_temperature_c']+1e-4 for m in motor_profiles))
        for identity, robot in robots.items():
            tool, spot = obj(tools.get(identity)), obj(spots.get(identity))
            temperatures = [tool.get('tip_temperature_c'), tool.get('body_temperature_c'), spot.get('temperature_c')]
            valid_heat = all(finite(t) for t in temperatures)
            require(prefix+'_'+identity+'_heat_bounded', valid_heat and 20.-1e-4 <= temperatures[0] <= 500.+1e-4
                    and 20.-1e-4 <= temperatures[1] <= 105.+1e-4 and 20.-1e-4 <= temperatures[2] <= 420.+1e-4)
            local_time, start, end = robot.get('cycle_seconds'), robot.get('weld_start_seconds'), robot.get('weld_end_seconds')
            if not all(finite(t) for t in (local_time, start, end)) or not valid_heat:
                continue
            if local_time < start:
                require(prefix+'_'+identity+'_new_body_cold_before_own_weld', near(temperatures[2], 20., 1e-4) and near(spot.get('glow_strength'), 0., 1e-4))
                if cycle == 0:
                    require(prefix+'_'+identity+'_first_cycle_tool_cold_before_own_weld', near(temperatures[0], 20., 1e-4)
                            and near(temperatures[1], 20., 1e-4) and near(tool.get('glow_strength'), 0., 1e-4))
            if robot.get('phase') == 'pre_weld_wait':
                owned = [obj(m) for m in motor_profiles if str(obj(m).get('id', '')).startswith('SM_'+identity+'_')]
                require(prefix+'_'+identity+'_motor_warms_from_approach_before_weld', len(owned) == 7
                        and any(finite(m.get('temperature_c')) and m['temperature_c'] > 20.01 for m in owned))
            if near(local_time, (start+end)*.5):
                require(prefix+'_'+identity+'_own_midweld_heats_tool_and_body', min(temperatures) > 20.)
            heat_history.setdefault((cycle, identity), []).append({'time': local_time, 'start': start, 'end': end,
                                                                  'temperatures': temperatures})
        attachment_records.append((prefix, obj(probe.get('iq9'))))
    require('two_exact_probe_cycles', covered_cycles == {0, 1})
    require('sparse_nonprefix_robot_ids_exercised', seen_sparse)
    for identity, phases in seen_phases.items():
        require(identity+'_all_timing_phases_exercised', phases == set(ROBOT_PHASES))
    metrics['probe_count'] = len(probes)
    metrics['heat_gate_summary'] = {}
    for (cycle, identity), history in heat_history.items():
        label = 'cycle_'+str(cycle)+'_'+identity
        mid = [r for r in history if near(r['time'], (r['start']+r['end'])*.5)]
        before_end = [r for r in history if 0 < r['end']-r['time'] < .1]
        tail = sorted([r for r in history if r['time'] > r['end']], key=lambda r: r['time'])
        parked = [r for r in tail if r['time'] > r['end']+1.]
        require(label+'_heat_probes_include_midweld_and_park', bool(mid and before_end and parked))
        require(label+'_tool_and_body_cool_after_own_weld', bool(before_end and parked)
                and all(a < b for a, b in zip(parked[0]['temperatures'], before_end[-1]['temperatures']))
                and all(all(y <= x+1e-4 for x, y in zip(a['temperatures'], b['temperatures'])) for a, b in zip(tail, tail[1:])))
        metrics['heat_gate_summary'][label] = {'midweld': mid[0] if mid else None,
                                              'near_weld_end': before_end[-1] if before_end else None,
                                              'parked': parked[0] if parked else None}

    views = identified(data.get('production_views'), 'name')
    require('six_production_views', set(views) == set(PRODUCTION_NAMES))
    for name, view in views.items():
        pose = obj(view.get('pose'))
        load('production_'+name, view.get('image'))
        active = identified(pose.get('active_weld_emitters'))
        if name in ('rest', 'parked', 'departure'):
            require(name+'_no_active_weld', not active)
        elif name == 'weld_stagger':
            require(name+'_only_subset_welding', 0 < len(active) < 4)
        elif name == 'all_welding':
            require(name+'_four_active_robots', set(active) == baseline_ids and finite(obj(view.get('sparks')).get('spawned')) and obj(view.get('sparks'))['spawned'] > 0)
        elif name == 'approach_stagger':
            phases = {r.get('phase') for r in identified(obj(pose.get('robot_timing')).get('robots')).values()}
            require(name+'_rest_and_approach_coexist', 'rest_delay' in phases and 'approach' in phases)
        attachment_records.append(('production_'+name, obj(view.get('iq9'))))

    nir_views = identified(data.get('nir_views'), 'name')
    require('four_nir_isolation_views', set(nir_views) == set(NIR_NAMES))
    light_rows, fps = {}, []
    for name in NIR_NAMES:
        row = obj(nir_views.get(name))
        for family in ('image', 'raw_nir', 'raw_thermal'):
            load(name+'_'+family, row.get(family), family.startswith('raw_'))
        expected_on = name in ('nir_home_on', 'nir_moved_on')
        require(name+'_observer_nir_channel_and_mode', row.get('observer_view_mask') == (1 if name == 'visible_home' else 4)
                and row.get('observer_ir_channel_2_only') is True and row.get('observer_ir_emitting') is expected_on)
        require(name+'_fixed_nir_channel', row.get('fixed_ir_channel_1_only') is True)
        require(name+'_camera_relative_illuminator', distance(row.get('observer_ir_relative_position_cm'), [15, 0, -6]) < 1e-4
                and near(distance(row.get('observer_ir_position_cm'), row.get('observer_position_cm')), math.sqrt(261), 1e-4))
        require(name+'_fps_live_while_pose_paused', finite(row.get('fps')) and 0 < row['fps'] < 10000
                and integer(row.get('fps_samples')) and row['fps_samples'] > 0
                and obj(row.get('pose')).get('animation_enabled') is False and near(obj(row.get('pose')).get('motion_seconds'), 9.))
        fps.append({'name': name, 'fps': row.get('fps'), 'samples': row.get('fps_samples')})
        light_rows[name] = row
        attachment_records.append((name, obj(row.get('iq9'))))
    home = obj(light_rows.get('nir_home_on'))
    metrics['fixed_sensor_differences'] = {}
    for name, row in light_rows.items():
        require(name+'_fixed_light_did_not_move', distance(row.get('fixed_ir_position_cm'), home.get('fixed_ir_position_cm')) < 1e-5)
        if name != 'nir_moved_on':
            require(name+'_home_camera_and_light_pose_stable', distance(row.get('observer_position_cm'), home.get('observer_position_cm')) < 1e-5
                    and distance(row.get('observer_ir_position_cm'), home.get('observer_ir_position_cm')) < 1e-5)
        for family in ('raw_nir', 'raw_thermal'):
            first, second = 'nir_home_on_'+family, name+'_'+family
            require(name+'_'+family+'_byte_parity', first in payloads and second in payloads and payloads[first] == payloads[second])
            if first in images and second in images:
                stats = difference(images[first], images[second])
                stats['difference_bbox_px'] = ImageChops.difference(images[first], images[second]).getbbox()
                metrics['fixed_sensor_differences'][name+'_'+family] = stats
    moved = obj(light_rows.get('nir_moved_on'))
    require('observer_and_illuminator_move_together', distance(moved.get('observer_position_cm'), home.get('observer_position_cm')) > 100
            and distance(moved.get('observer_ir_position_cm'), home.get('observer_ir_position_cm')) > 100)
    require('fps_samples_increase_across_frozen_pose', len(fps) == 4 and all(finite(v['samples']) for v in fps)
            and all(a['samples'] < b['samples'] for a, b in zip(fps, fps[1:])))
    metrics['fps_samples'] = fps
    metrics['nir_image_differences'] = {}
    for name in ('nir_home_off', 'nir_moved_on'):
        first, second = 'nir_home_on_image', name+'_image'
        if first in images and second in images:
            stats = difference(images[first].crop(SCENE_CROP), images[second].crop(SCENE_CROP))
            metrics['nir_image_differences'][name] = stats
            require(name+'_meaningful_observer_image_difference', stats['mean_absolute_difference_u8'] >= 1.
                    and stats['changed_fraction_at_least_3_u8'] >= .01)
        else:
            require(name+'_meaningful_observer_image_difference', False)
    source_stats = obj(data.get('nir_raw_statistics'))
    for name in NIR_NAMES:
        key = name+'_raw_nir'
        filename = obj(result['files'].get(key)).get('name')
        matching = [r for r in source_stats.values() if isinstance(r, dict) and r.get('image') == filename]
        require(name+'_gpu_nir_source_exactly_grayscale', len(matching) == 1 and matching[0].get('preconversion_max_rgb_channel_spread_u8') == 0
                and matching[0].get('preconversion_neutral_within_one_code') is True and matching[0].get('max_rgb_channel_spread_u8') == 0)

    on, off = obj(data.get('iq9_led_on')), obj(data.get('iq9_led_off'))
    attachment_records += [('led_on', on), ('led_off', off)]
    reference_mounts = identified(obj(attachment_records[0][1]).get('mounts')) if attachment_records else {}
    require('four_unique_prop_mounts', len(reference_mounts) == 4)
    positions = {identity: [] for identity in reference_mounts}
    for label, prop in attachment_records:
        mounts = identified(prop.get('mounts'))
        require(label+'_iq9_prop_scope_and_budget', prop.get('model') == 'iq9-evk-attached-props-v1'
                and prop.get('initialized') is True and prop.get('enabled') is True and prop.get('finite_state') is True
                and prop.get('attached_to_arm') is True and prop.get('mount_count') == 4 and prop.get('thermal_profile_count') == 28
                and prop.get('led_count') == 12 and prop.get('led_light_count') == 12 and set(mounts) == set(reference_mounts))
        for identity, mount in mounts.items():
            reference = obj(reference_mounts.get(identity))
            require(label+'_'+identity+'_attached_local_transform_preserved', mount.get('attached_to_arm') is True
                    and isinstance(mount.get('parent_mesh'), str) and mount['parent_mesh'].endswith('_upper_arm')
                    and mount.get('parent_mesh') == reference.get('parent_mesh')
                    and all(distance(mount.get(k), reference.get(k)) < 1e-5 for k in ('contact_local_cm', 'relative_position_cm', 'relative_rotation_deg')))
            require(label+'_'+identity+'_finite_world_frame', vector(mount.get('world_position_cm'))
                    and all(near(distance(mount.get(k), [0, 0, 0]), 1., 1e-4) for k in ('world_normal', 'world_right', 'world_up')))
            if identity in positions and vector(mount.get('world_position_cm')):
                positions[identity].append(mount['world_position_cm'])
            leds = rows(mount.get('leds'))
            require(label+'_'+identity+'_three_bounded_leds', len(leds) == 3 and {obj(v).get('index') for v in leds} == {0, 1, 2}
                    and all(type(obj(v).get('enabled')) is bool and vector(obj(v).get('world_position_cm'))
                            and near(obj(v).get('emissive_strength'), 100. if obj(v).get('enabled') else 0.)
                            and near(obj(v).get('local_light_lumens'), .45 if obj(v).get('enabled') else 0.) for v in leds)
                    and all(obj(v).get('enabled') is True for v in leds if obj(v).get('index') in (1, 2)))
    require('one_prop_per_robot_upper_arm', len({m.get('parent_mesh') for m in reference_mounts.values()}) == 4)
    for identity, history in positions.items():
        require(identity+'_prop_actually_moves_with_arm', len(history) > 1 and any(distance(history[0], p) > 5. for p in history[1:]))
    on_mounts, off_mounts = identified(on.get('mounts')), identified(off.get('mounts'))
    amber = lambda m: next((obj(led).get('enabled') for led in rows(obj(m).get('leds')) if obj(led).get('index') == 0), None)
    require('decorative_amber_led_animation_changes', set(on_mounts) == set(off_mounts) == set(reference_mounts) and len(on_mounts) == 4
            and any(type(amber(on_mounts[i])) is bool and type(amber(off_mounts[i])) is bool and amber(on_mounts[i]) != amber(off_mounts[i]) for i in on_mounts))
    metrics['iq9_amber_states'] = {'on': {i: amber(m) for i, m in on_mounts.items()}, 'off': {i: amber(m) for i, m in off_mounts.items()}}
    closeups = rows(data.get('iq9_closeups'))
    require('four_prop_closeup_records', len(closeups) == 4)
    require('closeups_cover_all_mounted_props', {obj(obj(v).get('mount')).get('id') for v in closeups} == set(reference_mounts))
    metrics['closeup_image_statistics'] = {}
    for i, closeup in enumerate(closeups):
        key = 'iq9_closeup_'+str(i)
        load(key, obj(closeup).get('image'))
        if key in images:
            stats = ImageStat.Stat(images[key].crop(SCENE_CROP))
            require(key+'_not_blank', max(stats.stddev) >= 3. and max(stats.mean) > 3.)
            crop = images[key].crop(SCENE_CROP)
            white = sum(1 for pixel in crop.getdata() if min(pixel) >= 250)/(crop.width*crop.height)
            metrics['closeup_image_statistics'][key] = {'mean_rgb_u8': stats.mean, 'stddev_rgb_u8': stats.stddev,
                                                      'fraction_all_channels_at_least_250': white}
    result['failed_checks'] = [k for k, passed in checks.items() if not passed]
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
    print(json.dumps({'passed': result['passed'], 'check_count': len(result['checks']), 'failed_checks': result['failed_checks'],
                      'errors': result['errors'], 'report': str(args.output.resolve())}, indent=2))
    return 0 if result['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
