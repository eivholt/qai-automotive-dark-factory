#!/usr/bin/env python3
"""Verify existing native captures for the ten authored realism additions.

Pillow only; does not launch Unreal or a model. Numeric checks cover the
authored schedule, geometric photoeyes and clamps, not industrial safety.
Nonblank images and localized differences supplement a separate visual review.
"""
from __future__ import annotations

import argparse
from array import array
import hashlib
import json
import math
import re
import sys
import wave
from pathlib import Path

try:
    from PIL import Image, ImageChops, ImageStat
except ImportError:
    raise SystemExit("Pillow is required; use the project's existing QA environment.")

RAW_SIZE, SCREEN_SIZE = (448, 256), (1600, 900)
SCENE_CROP = (32, 135, 1200, 800)
STAGE_TIMES = (1.2, 2.5, 3.2, 3.5, 6.5, 10., 15.6, 16., 16.25, 18., 20.6, 21.1)
THRESHOLDS = {
    'cycle_seconds': 21., 'numeric_tolerance': 1e-5,
    'minimum_scene_stddev_u8': 2.,
    'witness_change_threshold_u8': 3, 'minimum_witness_changed_pixels': 5,
    'maximum_witness_changed_fraction': .10,
    'raw_rgb_spread_u8': 0,
}
TIMING_FIELDS = ('rest_delay_seconds', 'pre_weld_delay_seconds', 'approach_start_seconds',
                 'approach_end_seconds', 'weld_start_seconds', 'weld_end_seconds', 'withdraw_end_seconds')


def obj(value):
    return value if isinstance(value, dict) else {}


def rows(value):
    return value if isinstance(value, list) else []


def finite(value):
    return type(value) in (int, float) and math.isfinite(value)


def integer(value):
    return finite(value) and int(value) == value


def near(a, b, tolerance=1e-5):
    return finite(a) and finite(b) and abs(a-b) <= tolerance


def vector(value):
    return isinstance(value, list) and len(value) == 3 and all(finite(x) for x in value)


def finite_tree(value):
    if isinstance(value, dict):
        return all(finite_tree(x) for x in value.values())
    if isinstance(value, list):
        return all(finite_tree(x) for x in value)
    return finite(value) if type(value) in (int, float) else True


def identified(value, key='id'):
    result = {}
    for item in rows(value):
        item = obj(item)
        identity = item.get(key)
        if not isinstance(identity, str) or not identity or identity in result:
            return {}
        result[identity] = item
    return result


def bounds_valid(low, high):
    return vector(low) and vector(high) and all(a <= b for a, b in zip(low, high))


def ease(value):
    x = max(0., min(1., value))
    return x*x*x*(x*(x*6.-15.)+10.)


def mix32(value):
    value &= 0xffffffff
    value ^= value >> 16
    value = value*0x7feb352d & 0xffffffff
    value ^= value >> 15
    value = value*0x846ca68b & 0xffffffff
    return value ^ (value >> 16)


def expected_timing(seed, robot_index, cycle_index):
    """Independent fixed-width reference, unaffected by scene-detail actors."""
    first = mix32((seed & 0xffffffff) ^ ((robot_index+1)*0x9e3779b9)
                  ^ ((cycle_index % 16+1)*0x85ebca6b))
    second = mix32(first ^ 0x68bc21eb)
    rest, dwell = .2+.6*(first & 0xffffff)/16777215., .5+1.5*(second & 0xffffff)/16777215.
    approach = 3.5+rest
    weld = approach+1.+dwell
    return dict(zip(TIMING_FIELDS, (rest, dwell, approach, approach+1., weld, weld+7.5, weld+8.5)))


def segment_box(start, end, half_extent):
    """Finite segment versus a box in its local coordinate frame."""
    if not all(vector(v) for v in (start, end, half_extent)) or any(x <= 0 for x in half_extent):
        raise ValueError('Invalid photoeye segment/box geometry')
    enter, leave = 0., 1.
    for a, b, extent in zip(start, end, half_extent):
        delta = b-a
        if abs(delta) < 1e-9:
            if a < -extent or a > extent:
                return False
        else:
            first, last = sorted(((-extent-a)/delta, (extent-a)/delta))
            enter, leave = max(enter, first), min(leave, last)
            if enter > leave:
                return False
    return True


def spread(image):
    red, green, blue = image.split()
    return ImageChops.subtract(ImageChops.lighter(red, ImageChops.lighter(green, blue)),
                               ImageChops.darker(red, ImageChops.darker(green, blue))).getextrema()[1]


def difference(first, second):
    diff = ImageChops.difference(first, second)
    red, green, blue = diff.split()
    maximum = ImageChops.lighter(red, ImageChops.lighter(green, blue))
    changed = sum(maximum.histogram()[THRESHOLDS['witness_change_threshold_u8']:])
    return {'mean_absolute_difference_u8': sum(ImageStat.Stat(diff).mean)/3,
            'changed_pixels_at_least_3_u8': changed,
            'changed_fraction_at_least_3_u8': changed/(maximum.width*maximum.height),
            'maximum_channel_difference_u8': maximum.getextrema()[1]}


def reject_constant(value):
    raise ValueError('Nonfinite JSON constant: '+value)


def verify(directory):
    directory = directory.resolve()
    result = {'scope': 'Native authored realism metadata, geometric timing checks and actual images; visual review and industrial validation are separate.',
              'directory': str(directory), 'thresholds': THRESHOLDS, 'checks': {}, 'metrics': {}, 'files': {}, 'errors': []}
    checks, metrics = result['checks'], result['metrics']
    images, payloads = {}, {}

    def require(name, passed):
        checks[name] = bool(passed)

    def load(key, filename, raw=False):
        try:
            if not isinstance(filename, str) or not filename or Path(filename).name != filename or '\\' in filename:
                raise ValueError('Expected capture filename inside directory')
            path = directory/filename
            if path.resolve().parent != directory:
                raise ValueError('Capture path escapes directory')
            payload = path.read_bytes()
            with Image.open(path) as original:
                if original.format != 'PNG' or original.size != (RAW_SIZE if raw else SCREEN_SIZE) or original.mode not in ('L', 'RGB', 'RGBA'):
                    raise ValueError('Expected 8-bit PNG at declared capture dimensions')
                if original.mode == 'RGBA' and original.getchannel('A').getextrema() != (255, 255):
                    raise ValueError('Expected opaque capture')
                image = original.convert('RGB')
            images[key], payloads[key] = image, payload
            result['files'][key] = {'name': filename, 'bytes': len(payload), 'size': list(image.size),
                                    'sha256': hashlib.sha256(payload).hexdigest()}
            require(key+'_valid_png', True)
            crop = image if raw else image.crop(SCENE_CROP)
            stats = ImageStat.Stat(crop.convert('L'))
            metrics[key+'_image'] = {'mean_luminance_u8': stats.mean[0], 'stddev_u8': stats.stddev[0]}
            require(key+'_nonblank', stats.stddev[0] > (0. if raw else THRESHOLDS['minimum_scene_stddev_u8']))
            if raw:
                require(key+'_grayscale', spread(image) == 0)
        except (OSError, ValueError) as exc:
            require(key+'_valid_png', False)
            result['errors'].append(key+': '+str(exc))

    try:
        data = json.loads((directory/'realism.json').read_text(encoding='utf-8-sig'), parse_constant=reject_constant)
        if not isinstance(data, dict):
            raise ValueError('Expected JSON object')
        require('runtime_json_valid', True)
        require('all_metadata_numbers_finite', finite_tree(data))
    except (OSError, ValueError) as exc:
        data = {}
        require('runtime_json_valid', False)
        result['errors'].append(str(exc))
    try:
        log = (directory/'runtime.log').read_text(encoding='utf-8-sig')
        shader_failures = [line.strip() for line in log.splitlines() if re.search(
            r'failed to compile material|default material will be used|(?:material|shader)[^\r\n]*\.(?:ush|usf):[^\r\n]*error:', line, re.I)]
        metrics['shader_or_material_failure_lines'] = shader_failures[:24]
        require('runtime_log_present', bool(log))
        require('no_shader_compile_error_or_material_fallback', not shader_failures)
    except (OSError, ValueError) as exc:
        require('runtime_log_present', False)
        result['errors'].append('runtime.log: '+str(exc))
    require('ten_authored_features', data.get('feature_count') == 10)
    for flag in ('passed', 'sensor_observer_parity', 'witness_thermal_unchanged', 'sound_mute_roundtrip', 'audio_preview_saved'):
        require('runtime_'+flag, data.get(flag) is True)

    stages = rows(data.get('stages'))
    require('twelve_unique_named_stages', len(stages) == len(STAGE_TIMES) and len(identified(stages, 'name')) == len(STAGE_TIMES))
    require('requested_stage_times', len(stages) == len(STAGE_TIMES)
            and all(near(obj(row).get('time'), expected) for row, expected in zip(stages, STAGE_TIMES)))
    baseline_environment, robot_ids, seed_values = None, None, set()
    occupancy = {'entry_photoeye': [], 'exit_photoeye': []}
    for index, stage in enumerate(stages):
        stage = obj(stage)
        prefix = 'stage_'+str(index)
        absolute = stage.get('time')
        pose, environment, mechanism, sound = (obj(stage.get(k)) for k in ('pose', 'environment', 'mechanisms', 'sound'))
        valid_time = finite(absolute) and absolute >= 0
        cycle_index = int(absolute//21.) if valid_time else -1
        cycle_time = absolute % 21. if valid_time else -1.
        require(prefix+'_pose_clock_and_preview_guards', valid_time and near(pose.get('motion_seconds'), absolute)
                and near(pose.get('cycle_seconds'), cycle_time) and pose.get('cycle_index') == cycle_index
                and near(pose.get('cycle_duration_seconds'), 21.) and pose.get('animation_enabled') is False
                and pose.get('inference_enabled') is False and pose.get('training_enabled') is False)
        require(prefix+'_source_geometry_counts', pose.get('mesh_count') == 76 and pose.get('joint_count') == 24
                and pose.get('body_panel_count') == 29 and pose.get('carriage_holder_count') == 2)
        load(prefix, stage.get('image'))

        features = identified(environment.get('features'), 'name')
        expected_features = {'pedestrian_markings_and_station_signs', 'overhead_utility_trays', 'welding_extraction_hoods'}
        require(prefix+'_environment_ready', environment.get('initialized') is True and environment.get('enabled') is True
                and environment.get('finite_state') is True and environment.get('feature_count') == 3
                and set(features) == expected_features)
        count, budget = environment.get('instance_count'), environment.get('instance_budget')
        require(prefix+'_environment_bounded', integer(count) and integer(budget) and 0 < count <= budget <= 512
                and integer(environment.get('draw_components')) and 0 < environment['draw_components'] <= 16
                and all(integer(f.get('instances')) and f['instances'] > 0 for f in features.values())
                and sum(f.get('instances', 0) for f in features.values()) == count)
        require(prefix+'_environment_no_new_lights_physics_or_thermal_ids',
                all(environment.get(k) == 0 for k in ('lights_added', 'collision_shapes_added', 'thermal_profiles_added'))
                and environment.get('tick_enabled') is False)
        require(prefix+'_environment_floor_and_counts', near(environment.get('floor_datum_cm'), 6.3477106)
                and environment.get('pedestrian_lane_count') == 2 and near(environment.get('pedestrian_lane_width_cm'), 110.)
                and environment.get('station_sign_count') == 4 and environment.get('utility_tray_count') == 2
                and environment.get('extraction_hood_count') == 2)
        for name, feature in features.items():
            low, high = feature.get('bounds_min_cm'), feature.get('bounds_max_cm')
            valid = bounds_valid(low, high)
            require(prefix+'_'+name+'_finite_bounds', valid and near(feature.get('minimum_z_cm'), low[2]))
            if name != 'pedestrian_markings_and_station_signs':
                require(prefix+'_'+name+'_above_robot_sweep', valid and low[2] >= 600.)
        require(prefix+'_environment_station_and_hood_positions',
                len(rows(environment.get('station_centers_cm'))) == 4 and len(rows(environment.get('hood_centers_cm'))) == 2
                and all(vector(v) for k in ('station_centers_cm', 'hood_centers_cm') for v in rows(environment.get(k))))
        if baseline_environment is None:
            baseline_environment = environment
        require(prefix+'_environment_static', environment == baseline_environment)

        timing = obj(pose.get('robot_timing'))
        robots = identified(timing.get('robots'))
        if robot_ids is None:
            robot_ids = set(robots)
        seed = timing.get('seed')
        if integer(seed):
            seed_values.add(int(seed))
        require(prefix+'_seeded_schedule_profile', timing.get('profile') == 'seeded-robot-delays-v1'
                and timing.get('randomized') is True and timing.get('schedule_bank_cycles') == 16
                and integer(seed) and near(timing.get('cycle_duration_seconds'), 21.)
                and near(timing.get('carriage_departure_start_seconds'), 16.3)
                and near(timing.get('carriage_departure_end_seconds'), 19.8)
                and len(robots) == 4 and set(robots) == robot_ids)
        indices, approach_starts, withdraw_ends = [], [], []
        for identity, robot in robots.items():
            robot_index = robot.get('robot_index')
            valid = integer(seed) and integer(robot_index) and 0 <= robot_index < 4 and valid_time
            expected = expected_timing(int(seed), int(robot_index), cycle_index) if valid else {}
            require(prefix+'_'+identity+'_independent_seeded_clock', valid
                    and all(near(robot.get(k), expected[k]) for k in TIMING_FIELDS)
                    and robot.get('cycle_index') == cycle_index and robot.get('schedule_index') == cycle_index % 16
                    and near(robot.get('cycle_seconds'), cycle_time))
            if valid:
                indices.append(int(robot_index))
                approach_starts.append(expected['approach_start_seconds'])
                withdraw_ends.append(expected['withdraw_end_seconds'])
        require(prefix+'_unique_robot_indices', sorted(indices) == [0, 1, 2, 3])
        require(prefix+'_safe_carriage_departure', len(withdraw_ends) == 4 and max(withdraw_ends) <= 15.8+1e-5
                and near(timing.get('carriage_clearance_margin_seconds'), 16.3-max(withdraw_ends)))
        require(prefix+'_mechanisms_clock', mechanism.get('initialized') is True and mechanism.get('enabled') is True
                and mechanism.get('finite_state') is True and mechanism.get('randomized_robot_timing') is True
                and near(mechanism.get('motion_seconds'), absolute) and near(mechanism.get('cycle_seconds'), cycle_time)
                and near(mechanism.get('cycle_duration_seconds'), 21.))
        closure = ease((cycle_time-3.)/.35) if cycle_time < 15.8 else 1.-ease((cycle_time-15.8)/.4)
        phase = 'open' if closure <= 0 else 'closed' if closure >= 1 else 'closing' if cycle_time < 15.8 else 'opening'
        require(prefix+'_independent_clamp_curve', near(mechanism.get('fixture_closure'), closure)
                and near(mechanism.get('fixture_angle_deg'), 75.*(1.-closure), 1e-3) and mechanism.get('fixture_phase') == phase)
        require(prefix+'_clamps_clear_robots_and_departure', near(mechanism.get('close_start_seconds'), 3.)
                and near(mechanism.get('close_end_seconds'), 3.35) and near(mechanism.get('open_start_seconds'), 15.8)
                and near(mechanism.get('open_end_seconds'), 16.2) and len(approach_starts) == 4
                and near(mechanism.get('first_robot_approach_seconds'), min(approach_starts))
                and near(mechanism.get('latest_robot_withdrawal_seconds'), max(withdraw_ends))
                and min(approach_starts) >= 3.35 and max(withdraw_ends) <= 15.8
                and near(mechanism.get('open_before_departure_margin_seconds'), .1))
        fixtures = identified(mechanism.get('fixtures'))
        require(prefix+'_four_finite_clamps', mechanism.get('fixture_count') == 4 and len(fixtures) == 4
                and all(vector(f.get('pivot_world_cm')) and vector(f.get('pad_world_cm'))
                        and finite(f.get('pad_to_body_aabb_gap_cm')) and finite(f.get('arm_length_cm'))
                        and 30 <= f['arm_length_cm'] <= 65
                        and near(f.get('arm_angle_deg'), 75*(1.-closure), 1e-3) for f in fixtures.values()))
        for identity, fixture in fixtures.items():
            pivot, pad = fixture.get('pivot_world_cm'), fixture.get('pad_world_cm')
            length, body_x = fixture.get('arm_length_cm'), fixture.get('body_side_x_cm')
            valid = vector(pivot) and vector(pad) and finite(length) and finite(body_x)
            if valid:
                side = 1. if pivot[0] > body_x else -1.
                angle = math.radians(75.*(1.-closure))
                expected_pad = [pivot[0]-side*length*math.cos(angle), pivot[1], pivot[2]+length*math.sin(angle)]
                require(prefix+'_'+identity+'_pad_matches_rigid_arm',
                        all(near(a, b, 1e-3) for a, b in zip(pad, expected_pad))
                        and near(fixture.get('pad_to_body_aabb_gap_cm'), side*(pad[0]-body_x)-4., 1e-3))
                contact = fixture.get('surface_contact_world_cm')
                # The source sheet can be inset from the whole body's AABB.
                # Check the fitted closed contact instead of requiring an AABB gap.
                require(prefix+'_'+identity+'_fitted_closed_source_contact', fixture.get('surface_contact_bound') is True
                        and isinstance(fixture.get('contact_mesh'), str) and bool(fixture['contact_mesh'])
                        and vector(contact) and near(fixture.get('closed_surface_gap_cm'), .2)
                        and near(side*(pivot[0]-side*length-contact[0])-4., .2, 1e-3))
            else:
                require(prefix+'_'+identity+'_pad_matches_rigid_arm', False)
                require(prefix+'_'+identity+'_fitted_closed_source_contact', False)
        stack = obj(mechanism.get('stack_light'))
        green = 3.35 <= cycle_time < 15.8
        require(prefix+'_stack_matches_secured_or_transfer_clock', stack.get('green_run') is green
                and stack.get('amber_transfer_clearance') is (not green)
                and stack.get('state') == ('secured_cycle' if green else 'transfer_clearance'))
        eyes = identified(mechanism.get('photoeyes'))
        require(prefix+'_entry_exit_photoeyes_present', mechanism.get('photoeye_count') == 2 and set(eyes) == set(occupancy))
        eye_metrics = {}
        for identity, eye in eyes.items():
            start, end = eye.get('beam_start_world_cm'), eye.get('beam_end_world_cm')
            blockers = rows(eye.get('blocking_carriage_colliders'))
            require(prefix+'_'+identity+'_beam_and_occupied_metadata', vector(start) and vector(end)
                    and near(start[1], -1050. if identity == 'entry_photoeye' else 1050.)
                    and near(end[1], start[1]) and near(start[2], 175.) and near(end[2], 175.)
                    and eye.get('occupied') is bool(blockers) and eye.get('indicator_emitting') is bool(blockers)
                    and all(isinstance(name, str) and name for name in blockers))
            boxes = rows(eye.get('tested_carriage_boxes'))
            hits, geometry_valid = [], bool(boxes)
            for box in boxes:
                box = obj(box)
                try:
                    transform = box.get('world_to_local_rows')
                    if not (isinstance(transform, list) and len(transform) == 3
                            and all(isinstance(r, list) and len(r) == 4 and all(finite(x) for x in r) for r in transform)
                            and vector(start) and vector(end) and vector(box.get('center_world_cm'))):
                        raise ValueError('Missing finite affine world-to-local transform')
                    local_start = [sum(r[i]*start[i] for i in range(3))+r[3] for r in transform]
                    local_end = [sum(r[i]*end[i] for i in range(3))+r[3] for r in transform]
                    for expected_point, reported in ((local_start, box.get('beam_start_local_cm')),
                                                     (local_end, box.get('beam_end_local_cm'))):
                        if not vector(reported) or not all(near(a, b, 1e-4) for a, b in zip(expected_point, reported)):
                            raise ValueError('Affine transform disagrees with reported local segment')
                    hit = segment_box(local_start, local_end, box.get('half_extent_cm'))
                    if not isinstance(box.get('name'), str) or type(box.get('enabled')) is not bool:
                        raise ValueError('Missing box identity/enabled state')
                    if box['enabled'] and hit:
                        hits.append(box['name'])
                except ValueError:
                    geometry_valid = False
            require(prefix+'_'+identity+'_independent_segment_box_occupancy', geometry_valid
                    and len(boxes) == pose.get('carriage_collision_shapes') and len(identified(boxes, 'name')) == len(boxes)
                    and sorted(hits) == sorted(blockers)
                    and eye.get('occupied') is bool(hits))
            low, high = pose.get('carriage_bounds_min_cm'), pose.get('carriage_bounds_max_cm')
            require(prefix+'_'+identity+'_occupied_within_carriage_bounds', bounds_valid(low, high)
                    and (not blockers or (low[1]-1e-4 <= start[1] <= high[1]+1e-4
                                          and low[2]-1e-4 <= start[2] <= high[2]+1e-4)))
            if identity in occupancy:
                occupancy[identity].append(bool(blockers))
            eye_metrics[identity] = {'reported_occupied': eye.get('occupied'), 'computed_hits': hits, 'tested_boxes': len(boxes)}
        metrics[prefix+'_mechanisms'] = {'time': absolute, 'closure': closure, 'photoeyes': eye_metrics}
        require(prefix+'_mechanisms_no_additional_physics_or_lights', mechanism.get('collision_enabled') is False
                and mechanism.get('new_light_count') == 0 and mechanism.get('thermal_profile_count') == 0)

        spots = identified(obj(pose.get('body_weld_heat')).get('spots'))
        require(prefix+'_four_witness_patches', len(spots) == 4 and set(spots) == set(robots))
        expected_strengths = {}
        for identity, spot in spots.items():
            robot = obj(robots.get(identity))
            weld_start = robot.get('weld_start_seconds')
            expected = max(0., min(1., (cycle_time-weld_start-.2)/.8)) if finite(weld_start) else -1.
            expected_strengths[identity] = expected
            require(prefix+'_'+identity+'_witness_follows_own_weld_clock', near(spot.get('witness_strength'), expected)
                    and vector(spot.get('world_position_cm')) and vector(spot.get('world_normal')))
        if near(absolute, 21.1) or near(absolute, 15.6):
            require(prefix+'_new_body_clean_or_completed_welds_marked', len(spots) == 4
                    and all(near(s.get('witness_strength'), 0. if near(absolute, 21.1) else 1.) for s in spots.values()))
        metrics[prefix+'_witness_strengths'] = expected_strengths
        sources = rows(sound.get('sources'))
        require(prefix+'_sound_finite_and_paused', sound.get('initialized') is True and sound.get('enabled') is True
                and sound.get('scene_animating') is False and near(sound.get('source_motion_seconds'), absolute)
                and sound.get('maximum_sources') == 6 and len(sources) == 6
                and all(obj(s).get('finite_audio') is True and near(obj(s).get('output_gate'), 0.)
                        and vector(obj(s).get('position_cm')) for s in sources))
        require(prefix+'_sound_envelopes_bounded', bool(sources) and all(finite(obj(s).get(k)) and 0 <= s[k] <= 1
                for s in sources for k in ('motor_envelope', 'weld_envelope', 'output_gate'))
                and sound.get('collision_queries') == 0 and sound.get('sensor_readbacks') == 0)
    require('stable_schedule_seed', len(seed_values) == 1)
    for identity, states in occupancy.items():
        require(identity+'_both_occupied_and_clear_observed', any(states) and not all(states))
    metrics['photoeye_occupancy_by_stage'] = occupancy

    sensor_views = identified(data.get('sensor_views'), 'name')
    require('three_observer_sensor_views', set(sensor_views) == {'visible', 'nir_moved', 'thermal'})
    for name, view in sensor_views.items():
        require('sensor_'+name+'_frozen_pose', near(view.get('time'), 10.)
                and near(obj(view.get('pose')).get('motion_seconds'), 10.) and view.get('raw_saved') is True)
        load('sensor_'+name, view.get('image'))
        for channel in ('nir', 'thermal'):
            load('sensor_'+name+'_'+channel, view.get('raw_'+channel), True)
    for channel in ('nir', 'thermal'):
        keys = ['sensor_'+name+'_'+channel for name in ('visible', 'nir_moved', 'thermal')]
        require('fixed_'+channel+'_bytes_identical_across_observer_modes_and_move',
                all(k in payloads for k in keys) and len({payloads[k] for k in keys}) == 1)

    bookmarks = rows(data.get('bookmarks'))
    require('five_unique_bookmarks', len(bookmarks) == 5 and sorted([obj(b).get('index') for b in bookmarks
            if integer(obj(b).get('index'))]) == list(range(5)) and len(identified(bookmarks, 'name')) == 5)
    for index, bookmark in enumerate(bookmarks):
        load('bookmark_'+str(index), obj(bookmark).get('image'))

    witnesses = rows(data.get('witness_views'))
    enabled_rows = [obj(w) for w in witnesses if obj(w).get('enabled') is True]
    disabled_rows = [obj(w) for w in witnesses if obj(w).get('enabled') is False]
    require('one_same_pose_witness_on_off_pair', len(witnesses) == 2 and len(enabled_rows) == len(disabled_rows) == 1)
    for label, candidates in (('on', enabled_rows), ('off', disabled_rows)):
        if not candidates:
            continue
        row = candidates[0]
        heat = obj(obj(row.get('pose')).get('body_weld_heat'))
        spots = rows(heat.get('spots'))
        require('witness_'+label+'_fixed_cold_old_body', near(row.get('time'), 15.7) and near(heat.get('heat_time_seconds'), 20.5)
                and heat.get('witness_enabled') is (label == 'on') and len(spots) == 4
                and all(near(obj(s).get('witness_strength'), 1. if label == 'on' else 0.) for s in spots)
                and row.get('raw_saved') is True)
        load('witness_'+label, row.get('image'))
        for channel in ('nir', 'thermal'):
            load('witness_'+label+'_'+channel, row.get('raw_'+channel), True)
    require('cosmetic_witness_leaves_thermal_bytes_exact',
            all(k in payloads for k in ('witness_on_thermal', 'witness_off_thermal'))
            and payloads.get('witness_on_thermal') == payloads.get('witness_off_thermal'))
    pair_present = all(k in images for k in ('witness_on', 'witness_off'))
    if pair_present:
        change = difference(images['witness_on'].crop(SCENE_CROP), images['witness_off'].crop(SCENE_CROP))
        metrics['visible_witness_difference'] = change
        require('visible_witness_difference_is_present_and_localized',
                change['changed_pixels_at_least_3_u8'] >= THRESHOLDS['minimum_witness_changed_pixels']
                and change['changed_fraction_at_least_3_u8'] <= THRESHOLDS['maximum_witness_changed_fraction'])
    else:
        require('visible_witness_difference_is_present_and_localized', False)
    if all(k in images for k in ('witness_on_nir', 'witness_off_nir')):
        metrics['raw_nir_witness_difference_not_required'] = difference(images['witness_on_nir'], images['witness_off_nir'])
    audio = obj(data.get('audio_preview'))
    dsp = obj(audio.get('dsp_self_check'))
    dsp_sources = rows(dsp.get('sources'))
    require('audio_preview_and_six_native_dsp_checks', audio.get('passed') is True and dsp.get('passed') is True
            and len(dsp_sources) == 6 and all(obj(s).get('passed') is True
                and finite(obj(s).get('active_rms')) and s['active_rms'] > 0
                and finite(obj(s).get('active_peak')) and 0 < s['active_peak'] < 1
                and near(obj(s).get('settled_muted_peak'), 0., 0.) for s in dsp_sources))
    try:
        wav_path = directory/'factory_sound_preview.wav'
        with wave.open(str(wav_path), 'rb') as wav:
            if wav.getnchannels() != 1 or wav.getsampwidth() != 2 or wav.getcomptype() != 'NONE':
                raise ValueError('Expected mono 16-bit PCM audition')
            frames, rate = wav.getnframes(), wav.getframerate()
            samples = array('h', wav.readframes(frames))
        if sys.byteorder != 'little':
            samples.byteswap()
        if not samples or len(samples) != frames or rate <= 0:
            raise ValueError('Empty or truncated audition')
        peak = max(abs(x) for x in samples)/32767.
        rms = math.sqrt(sum(x*x for x in samples)/len(samples))/32767.
        metrics['audio_file'] = {'frames': frames, 'sample_rate_hz': rate, 'duration_seconds': frames/rate,
                                 'peak': peak, 'rms': rms, 'bytes': wav_path.stat().st_size}
        require('audio_preview_actual_file_matches_metadata', near(frames/rate, 21., 1/rate)
                and near(audio.get('duration_seconds'), frames/rate)
                and audio.get('sample_rate_hz') == rate and audio.get('channels') == 1
                and 0 < peak < 1 and rms > 0
                and near(audio.get('peak_absolute_sample'), peak, 1/32767.)
                and near(audio.get('rms'), rms, 1/32767.))
    except (OSError, ValueError, wave.Error) as exc:
        require('audio_preview_actual_file_matches_metadata', False)
        result['errors'].append('factory_sound_preview.wav: '+str(exc))
    metrics['visual_review_required'] = 'Inspect fixtures, signage, overhead utilities, smoke/extraction context, bookmarks and witness closeups; statistics alone do not prove visual quality.'
    result['failed_checks'] = [name for name, passed in checks.items() if not passed]
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
    print(json.dumps({'passed': result['passed'], 'checks': len(result['checks']),
                      'failed_checks': result['failed_checks'], 'errors': result['errors'],
                      'report': str(args.output.resolve())}, indent=2))
    return 0 if result['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
