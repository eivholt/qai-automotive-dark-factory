#!/usr/bin/env python3
"""Verify actual production-cycle smoke exports without running Unreal or a model.

Requires Pillow only. The declared authored thermal proxy is not a calibrated
thermal camera. Live-cycle screenshots have different poses: HUD tests inspect
both thermal views within each image, never compare those poses pixel-for-pixel.
Only the declared frozen sensor/headlamp pairs are compared across files.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import sys

try:
    from PIL import Image, ImageChops, ImageStat
except ImportError:
    raise SystemExit('Pillow is required; use the existing environment documented in docs/thermal_qa.md.')

RAW_SIZE = (448, 256)
SCREEN_SIZE = (1600, 900)
COLOR_BOX = (1264, 268, 1572, 440)
# Current HUD image bounds: x1260..1576, y513.14..693.71 at 1600x900.
# Exclude its colored caption and the separate temperature scale.
MONO_BOX = (1264, 517, 1572, 690)
HEADLIGHT_BOX = (50, 100, 1200, 800)
HUD_NAMES = ('01_visible_rest.png', '03_nir.png', '04_thermal.png', '05_thermal_black_hot.png')
HEADLIGHT_NAMES = ('06_headlight_off.png', '07_headlight_on.png')
SENSOR_SUFFIXES = ('rooflight_off', 'rooflight_on', 'thermal_view')
THRESHOLDS = {
    'expected_meshes': 76, 'expected_joints': 24, 'expected_body_panels': 29,
    'expected_carriage_holders': 2, 'expected_robot_bases': 4,
    'cycle_duration_seconds': 18.0, 'empty_station_dwell_seconds': 1.0,
    'empty_phase_start_seconds': 17.0, 'arrival_phase_end_seconds': 3.0,
    'previous_cycle_duration_seconds': 22.0,
    'rest_carriage_offset_y_cm': -1800.0, 'weld_carriage_offset_y_cm': 0.0,
    'departure_carriage_offset_y_cm': 1800.0,
    'robot_approach_seconds': 1.0, 'robot_withdraw_seconds': 1.0,
    'parked_base_angle_abs_deg': 180.0, 'base_angle_tolerance_deg': .01,
    'working_base_angle_abs_max_deg': 12.0,
    'spark_continuous_rate_per_emitter_per_second': 240.0, 'spark_fallout_rate_per_emitter_per_second': 100.0,
    'spark_burst_particles_per_emitter_event': 64, 'spark_pulse_period_seconds': 1.05,
    'spark_capacity': 4096, 'spark_physics_hz': 90,
    'spark_peak_alive_exclusive_min': 500, 'spark_peak_airborne_exclusive_min': 50,
    'portal_entry_position_cm': (4.505665, -1204.08264, 6.3477106),
    'portal_exit_position_cm': (4.505665, 1219.15393, 6.3477106),
    'portal_position_tolerance_cm': .01, 'portal_yaw_tolerance_deg': .01,
    'portal_opening_width_cm': 420.0, 'portal_opening_height_cm': 390.0,
    'portal_tunnel_length_cm': 1100.0, 'portal_rail_count': 2, 'portal_central_shortened_strip_count': 6,
    'source_rail_centers_x_cm': (-49.70725, 58.71854), 'source_rail_top_z_cm': 90.974921,
    'portal_reset_cover_thickness_cm': 20.0,
    'carriage_offset_tolerance_cm': .01,
    'tip_standoff_min_cm': .1, 'tip_standoff_max_cm': 1.2,
    'expected_curtains': 2, 'curtain_peak_anchor_error_cm_exclusive_max': 10.0,
    'raw_hot_pixel_u8_exclusive_min': 220,
    'hud_colored_channel_spread_u8_exclusive_min': 5,
    'hud_colored_pixel_fraction_exclusive_min': .01,
    'hud_monochrome_channel_spread_u8_max': 2,
    'hud_nonblank_luma_range_u8_exclusive_min': 0,
    'headlight_mean_luma_gain_u8_exclusive_min': 1.0,
    'headlight_changed_luma_u8_min': 1,
    'headlight_changed_fraction_exclusive_min': .01,
}


def finite(value):
    return type(value) in (int, float) and math.isfinite(value)


def mapping(value):
    return value if isinstance(value, dict) else {}


def vector3(value):
    return isinstance(value, list) and len(value) == 3 and all(finite(v) for v in value)


def near(value, expected, tolerance=.01):
    return finite(value) and finite(expected) and abs(value - expected) <= tolerance


def identified_rows(value):
    if not isinstance(value, list):
        return {}
    rows = {}
    for raw in value:
        row = mapping(raw)
        identity = row.get('id')
        if not isinstance(identity, str) or not identity or identity in rows:
            return {}
        rows[identity] = row
    return rows


def reject_constant(value):
    raise ValueError(f'Nonfinite JSON number {value}')


def image_stats(image):
    red, green, blue = image.split()
    spread = ImageChops.subtract(ImageChops.lighter(red, ImageChops.lighter(green, blue)),
                                 ImageChops.darker(red, ImageChops.darker(green, blue)))
    gray = image.convert('L')
    low, high = gray.getextrema()
    return {
        'mean_luma_u8': ImageStat.Stat(gray).mean[0],
        'luma_range_u8': high - low,
        'max_channel_spread_u8': spread.getextrema()[1],
        'mean_channel_spread_u8': ImageStat.Stat(spread).mean[0],
        'colored_pixel_fraction': sum(spread.histogram()[THRESHOLDS['hud_colored_channel_spread_u8_exclusive_min'] + 1:]) / (image.width * image.height),
        'hot_pixels_above_220': sum(gray.histogram()[THRESHOLDS['raw_hot_pixel_u8_exclusive_min'] + 1:]),
    }


def verify(directory):
    report = {
        'scope': 'Offline QA of actual production-cycle smoke exports; authored visual/thermal simulation, no inference or training.',
        'directory': str(directory.resolve()), 'thresholds': THRESHOLDS,
        'regions': {'thermal_color': COLOR_BOX, 'thermal_monochrome': MONO_BOX, 'frozen_headlight_comparison': HEADLIGHT_BOX},
        'comparison_note': 'Live-cycle poses may differ. Each HUD image is checked independently. Cross-file equality is required only for the explicitly frozen raw sensor triplets.',
        'checks': {}, 'metrics': {}, 'files': {}, 'errors': [],
    }
    checks, metrics, errors = report['checks'], report['metrics'], report['errors']
    runtime = {}
    try:
        runtime = json.loads((directory / 'welding_preview.json').read_text(encoding='utf-8-sig'), parse_constant=reject_constant)
        if not isinstance(runtime, dict):
            raise ValueError('Expected a JSON object')
        checks['runtime_json_valid'] = True
    except (OSError, ValueError) as exc:
        errors.append(f'welding_preview.json: {exc}')
        checks['runtime_json_valid'] = False
        runtime = {}
    checks['runtime_passed'] = runtime.get('passed') is True
    for field, expected in (('mesh_count', THRESHOLDS['expected_meshes']), ('joint_count', THRESHOLDS['expected_joints']),
                            ('body_panel_count', THRESHOLDS['expected_body_panels']), ('carriage_holder_count', THRESHOLDS['expected_carriage_holders'])):
        metrics[field] = runtime.get(field)
        checks[field] = finite(runtime.get(field)) and runtime[field] == expected
    for field in ('nir_inference_requests', 'model_or_training_readbacks'):
        metrics[field] = runtime.get(field)
        checks[field + '_zero'] = finite(runtime.get(field)) and runtime[field] == 0
    checks['inference_disabled'] = runtime.get('inference_enabled') is False
    checks['training_disabled'] = runtime.get('training_enabled') is False
    metrics['offline_qa_readbacks'] = {key: runtime.get(key) for key in ('offline_nir_qa_readbacks', 'offline_thermal_qa_readbacks')}
    report['readback_note'] = 'Explicit offline QA image readbacks are permitted; model/training publication and readbacks must remain zero.'
    for key, target in (('rest_pose', THRESHOLDS['rest_carriage_offset_y_cm']), ('motion_pose', THRESHOLDS['weld_carriage_offset_y_cm'])):
        offset = mapping(runtime.get(key)).get('carriage_offset_y_cm')
        metrics[key + '_carriage_offset_y_cm'] = offset
        checks[key + '_carriage_offset'] = finite(offset) and abs(offset - target) <= THRESHOLDS['carriage_offset_tolerance_cm']

    emitters = mapping(runtime.get('motion_pose')).get('active_weld_emitters', [])
    checks['four_surface_contact_emitters'] = isinstance(emitters, list) and len(emitters) == 4
    metrics['weld_tip_standoffs_cm'] = [mapping(e).get('tip_to_surface_cm') for e in emitters] if isinstance(emitters, list) else []
    checks['physical_tips_follow_contact_surfaces'] = len(metrics['weld_tip_standoffs_cm']) == 4 and all(
        finite(value) and THRESHOLDS['tip_standoff_min_cm'] <= value <= THRESHOLDS['tip_standoff_max_cm']
        for value in metrics['weld_tip_standoffs_cm'])
    profiles = runtime.get('registered_thermal_profiles')
    metrics['registered_thermal_profiles'] = profiles
    checks['thermal_profiles_fit_stencil_table'] = finite(profiles) and 0 < profiles <= 254

    cycle = mapping(runtime.get('production_cycle'))
    for field in ('cycle_duration_seconds', 'empty_station_dwell_seconds'):
        metrics[field] = runtime.get(field)
        checks[field + '_matches_shorter_cycle'] = near(runtime.get(field), THRESHOLDS[field], 1e-6)
    for field in ('robot_approach_seconds', 'robot_withdraw_seconds'):
        metrics[field] = runtime.get(field)
        checks[field + '_one_second'] = near(runtime.get(field), THRESHOLDS[field], 1e-6)
        checks[field + '_faster_than_previous_1_5_seconds'] = finite(runtime.get(field)) and 0 < runtime[field] < 1.5
    checks['runtime_holders_follow_carriage'] = cycle.get('holders_follow_carriage') is True
    checks['runtime_bases_parked_180_degrees'] = cycle.get('bases_parked_180_degrees') is True
    poses = {key: mapping(runtime.get(key)) for key in ('rest_pose', 'motion_pose')}
    poses.update({key: mapping(cycle.get(key)) for key in ('arrival_pose', 'parked_pose', 'departure_pose', 'empty_pose', 'next_body_pose')})
    metrics['carriage_poses'] = {}
    reference_holders = identified_rows(poses['rest_pose'].get('carriage_holders'))
    for name, pose in poses.items():
        checks[name + '_metadata_present'] = bool(pose)
        checks[name + '_cycle_duration_18_seconds'] = near(pose.get('cycle_duration_seconds'), THRESHOLDS['cycle_duration_seconds'], 1e-6)
        holders = identified_rows(pose.get('carriage_holders'))
        offset = pose.get('carriage_offset_y_cm')
        checks[name + '_two_identified_holders'] = len(holders) == THRESHOLDS['expected_carriage_holders']
        checks[name + '_same_holder_ids'] = len(reference_holders) == THRESHOLDS['expected_carriage_holders'] and set(holders) == set(reference_holders)
        metrics['carriage_poses'][name] = {'offset_y_cm': offset, 'body_visible': pose.get('body_visible'), 'holders': list(holders.values())}
        same_local = attached = translated = bool(holders)
        for identity, holder in holders.items():
            local, world = holder.get('local_position_cm'), holder.get('world_position_cm')
            reference = reference_holders.get(identity, {}).get('local_position_cm')
            attached &= holder.get('attached_to_carriage') is True
            same_local &= vector3(local) and vector3(reference) and all(near(a, b, THRESHOLDS['carriage_offset_tolerance_cm']) for a, b in zip(local, reference))
            translated &= vector3(local) and vector3(world) and finite(offset) and all(
                near(world[axis] - local[axis], offset if axis == 1 else 0.0, THRESHOLDS['carriage_offset_tolerance_cm']) for axis in range(3))
        checks[name + '_holders_attached_to_carriage'] = attached
        checks[name + '_holder_local_coordinates_unchanged'] = same_local
        checks[name + '_holders_follow_carriage_translation'] = translated
        visible = name != 'empty_pose'
        checks[name + '_holder_visibility'] = bool(holders) and all(h.get('visible') is visible for h in holders.values())
        checks[name + '_body_visibility'] = pose.get('body_visible') is visible
    for name in ('rest_pose', 'motion_pose', 'parked_pose'):
        joints = identified_rows(poses[name].get('joints'))
        bases = {identity: row.get('angle_deg') for identity, row in joints.items() if identity.endswith('_base')}
        metrics[name + '_base_angles_deg'] = bases
        checks[name + '_four_base_joints'] = len(bases) == THRESHOLDS['expected_robot_bases']
        if name == 'motion_pose':
            checks[name + '_bases_near_working_zero'] = bool(bases) and all(finite(angle) and abs(angle) <= THRESHOLDS['working_base_angle_abs_max_deg'] for angle in bases.values())
        else:
            checks[name + '_bases_parked_180_degrees'] = bool(bases) and all(finite(angle) and near(abs(angle), THRESHOLDS['parked_base_angle_abs_deg'], THRESHOLDS['base_angle_tolerance_deg']) for angle in bases.values())
    checks['base_ids_match_across_poses'] = all(
        set(metrics['rest_pose_base_angles_deg']) == set(metrics[name + '_base_angles_deg'])
        for name in ('motion_pose', 'parked_pose')) and len(metrics['rest_pose_base_angles_deg']) == THRESHOLDS['expected_robot_bases']
    departure_offset = poses['empty_pose'].get('carriage_offset_y_cm')
    checks['empty_pose_carriage_at_departure_endpoint'] = near(departure_offset, THRESHOLDS['departure_carriage_offset_y_cm'])
    metrics['body_tunnel_coverage'] = {
        'rest_world_min_cm': poses['rest_pose'].get('carriage_bounds_min_cm'),
        'rest_world_max_cm': poses['rest_pose'].get('carriage_bounds_max_cm'),
        'working_min_cm': poses['motion_pose'].get('working_body_bounds_min_cm'),
        'working_max_cm': poses['motion_pose'].get('working_body_bounds_max_cm'),
        'departure_offset_y_cm': departure_offset,
    }
    rest_max = metrics['body_tunnel_coverage']['rest_world_max_cm']
    working_min = metrics['body_tunnel_coverage']['working_min_cm']
    checks['whole_body_behind_entry_at_rest'] = vector3(rest_max) and rest_max[1] < THRESHOLDS['portal_entry_position_cm'][1]
    checks['whole_body_beyond_exit_before_hide'] = vector3(working_min) and near(departure_offset, THRESHOLDS['departure_carriage_offset_y_cm']) and working_min[1] + departure_offset > THRESHOLDS['portal_exit_position_cm'][1]
    checks['next_body_cycle_started'] = cycle.get('next_body_cycle_started') is True
    empty_pose, next_pose = poses['empty_pose'], poses['next_body_pose']
    empty_time, empty_phase = empty_pose.get('motion_seconds'), empty_pose.get('cycle_seconds')
    next_time, next_phase = next_pose.get('motion_seconds'), next_pose.get('cycle_seconds')
    duration = THRESHOLDS['cycle_duration_seconds']
    metrics['shorter_cycle'] = {
        'empty_motion_seconds': empty_time, 'empty_cycle_seconds': empty_phase,
        'next_body_motion_seconds': next_time, 'next_body_cycle_seconds': next_phase,
        'next_body_cycle_index': next_pose.get('cycle_index'), 'next_body_index': next_pose.get('body_index'),
    }
    checks['empty_pose_during_one_second_dwell'] = finite(empty_phase) and THRESHOLDS['empty_phase_start_seconds'] <= empty_phase < duration and empty_pose.get('cycle_index') == 0
    checks['empty_pose_elapsed_time_matches_first_cycle'] = finite(empty_time) and near(empty_time, empty_phase, 1e-6)
    checks['next_body_is_second_cycle'] = next_pose.get('cycle_index') == 1 and next_pose.get('body_index') == 1
    checks['next_body_captured_during_arrival'] = finite(next_phase) and 0 <= next_phase < THRESHOLDS['arrival_phase_end_seconds'] and next_pose.get('body_visible') is True
    checks['next_body_clock_wraps_at_18_seconds'] = finite(next_time) and finite(next_phase) and near(next_time - next_phase, duration, 1e-6)
    checks['next_body_arrives_before_previous_22_second_restart'] = finite(next_time) and duration <= next_time < THRESHOLDS['previous_cycle_duration_seconds']
    next_offset = next_pose.get('carriage_offset_y_cm')
    checks['next_body_holder_carriage_arriving'] = finite(next_offset) and THRESHOLDS['rest_carriage_offset_y_cm'] <= next_offset < THRESHOLDS['weld_carriage_offset_y_cm']
    checks['runtime_particles_collided_and_rested'] = cycle.get('particles_collided_and_rested') is True
    checks['runtime_particles_expired_after_cooling'] = cycle.get('particles_expired_after_cooling') is True
    during, after = mapping(cycle.get('sparks_during_weld')), mapping(cycle.get('sparks_after_cycle'))
    metrics['particles'] = {'during_weld': during, 'after_cycle': after,
                            'frozen_before_cooling': mapping(cycle.get('frozen_sparks_before_cooling'))}
    for field in ('alive', 'collisions', 'rest_events'):
        value = during.get(field)
        checks['welding_particles_' + field + '_positive'] = finite(value) and value > 0
    for name, spark in metrics['particles'].items():
        capacity, alive, peak_alive, airborne, peak_airborne = [spark.get(key) for key in ('capacity', 'alive', 'peak_alive', 'airborne', 'peak_airborne')]
        checks[name + '_spark_pool_4096'] = capacity == THRESHOLDS['spark_capacity']
        checks[name + '_spark_capacity_drops_zero'] = spark.get('capacity_drops') == 0
        checks[name + '_spark_counts_bounded'] = all(finite(v) for v in (capacity, alive, peak_alive, airborne, peak_airborne)) and 0 <= airborne <= alive <= peak_alive <= capacity and airborne <= peak_airborne <= capacity
    checks['spark_cinematic_expulsion_profile'] = during.get('model') == 'bounded-swept-world-particles-v3' and during.get('profile') == 'cinematic-expulsion-v1'
    checks['spark_continuous_rate_240_per_emitter'] = near(during.get('maximum_continuous_emission_rate_per_emitter_per_second'), THRESHOLDS['spark_continuous_rate_per_emitter_per_second']) and near(during.get('fountain_rate_per_emitter_per_second'), 240.)
    checks['spark_fallout_rate_100_per_emitter'] = near(during.get('fallout_rate_per_emitter_per_second'), THRESHOLDS['spark_fallout_rate_per_emitter_per_second'])
    checks['spark_burst_64_per_emitter_event'] = during.get('burst_particles_per_emitter_event') == THRESHOLDS['spark_burst_particles_per_emitter_event']
    checks['spark_pulse_period_1point05_seconds'] = near(during.get('pulse_period_seconds'), THRESHOLDS['spark_pulse_period_seconds'])
    checks['spark_stagger_and_phase_timing'] = near(during.get('pulse_stagger_seconds'), .11) and near(during.get('burst_duration_seconds'), .10) and near(during.get('fountain_end_seconds'), .42) and near(during.get('fallout_end_seconds'), .82)
    checks['spark_particle_state_finite'] = during.get('finite_particle_state') is True
    checks['spark_four_emitters'] = during.get('emitter_count') == 4
    checks['spark_peak_alive_above_500'] = finite(during.get('peak_alive')) and during['peak_alive'] > THRESHOLDS['spark_peak_alive_exclusive_min']
    checks['spark_peak_airborne_above_50'] = finite(during.get('peak_airborne')) and during['peak_airborne'] > THRESHOLDS['spark_peak_airborne_exclusive_min']
    budget = mapping(during.get('physical_budget'))
    checks['spark_physics_remains_90_hz'] = budget.get('fixed_step_hz') == THRESHOLDS['spark_physics_hz']
    checks['spark_collision_sweeps_recorded'] = finite(budget.get('collision_sweeps')) and budget['collision_sweeps'] > 0
    checks['spark_spawn_work_bounded_per_step'] = budget.get('maximum_spawn_per_step') == 268 and finite(budget.get('peak_spawn_attempts_per_step')) and 0 < budget['peak_spawn_attempts_per_step'] <= 268
    checks['spark_peak_sweeps_bounded_by_pool'] = finite(budget.get('peak_sweeps_per_step')) and 0 < budget['peak_sweeps_per_step'] <= THRESHOLDS['spark_capacity']
    timing = mapping(during.get('cpu_live_tick'))
    metrics['spark_cpu_live_tick'] = timing
    checks['spark_live_cpu_timing_samples_present'] = finite(timing.get('samples')) and timing['samples'] > 0
    for prefix in ('physics', 'update_rendering', 'advance_total'):
        mean, maximum = timing.get(prefix + '_mean_ms'), timing.get(prefix + '_max_ms')
        checks['spark_live_' + prefix + '_timing_valid'] = finite(mean) and finite(maximum) and 0 <= mean <= maximum
    report['performance_note'] = 'Spark CPU timing covers live Tick physics and instance updates only; direct frozen QA advances, GPU completion and other actors are excluded. This reports cost without imposing an arbitrary FPS threshold.'
    checks['particles_expired_after_cycle'] = finite(after.get('alive')) and after['alive'] == 0
    portals = cycle.get('curtain_portals')
    checks['two_curtain_portals'] = isinstance(portals, list) and len(portals) == THRESHOLDS['expected_curtains']
    metrics['curtains'] = portals if isinstance(portals, list) else []
    if isinstance(portals, list):
        # Match geometry by longitudinal position, not metadata array order.
        ordered_portals = sorted(portals, key=lambda raw: mapping(raw).get('actor_origin_cm')[1] if vector3(mapping(raw).get('actor_origin_cm')) else math.inf)
        for index, raw in enumerate(ordered_portals):
            portal = mapping(raw)
            checks[f'curtain_{index}_finite_physics'] = portal.get('finite_physics_state') is True
            contacts = portal.get('carriage_contact_events')
            checks[f'curtain_{index}_carriage_contacts'] = finite(contacts) and contacts > 0
            peak = portal.get('peak_anchor_error_cm')
            checks[f'curtain_{index}_peak_anchor_error_below_10cm'] = finite(peak) and 0 <= peak < THRESHOLDS['curtain_peak_anchor_error_cm_exclusive_max']
            role = 'entry' if index == 0 else 'exit'
            expected_position = THRESHOLDS['portal_' + role + '_position_cm']
            origin, yaw = portal.get('actor_origin_cm'), portal.get('actor_yaw_degrees')
            checks[f'{role}_portal_measured_origin'] = vector3(origin) and all(near(a, b, THRESHOLDS['portal_position_tolerance_cm']) for a, b in zip(origin, expected_position))
            expected_yaw = 180.0 if role == 'entry' else 0.0
            checks[f'{role}_portal_yaw'] = finite(yaw) and abs((yaw - expected_yaw + 180) % 360 - 180) <= THRESHOLDS['portal_yaw_tolerance_deg']
            for field in ('opening_width_cm', 'opening_height_cm', 'tunnel_length_cm'):
                checks[f'{role}_portal_{field}'] = near(portal.get(field), THRESHOLDS['portal_' + field])
            checks[f'{role}_portal_two_rail_continuations'] = portal.get('rail_continuation_count') == THRESHOLDS['portal_rail_count']
            checks[f'{role}_portal_rail_continuation_length'] = near(portal.get('rail_continuation_length_cm'), THRESHOLDS['portal_tunnel_length_cm'])
            checks[f'{role}_portal_six_shortened_strips'] = portal.get('rail_cut_strips') == THRESHOLDS['portal_central_shortened_strip_count']
            strip_count, shortened = portal.get('strips'), portal.get('shortened_strip_indices')
            cut_count = THRESHOLDS['portal_central_shortened_strip_count']
            central_indices = list(range((int(strip_count) - cut_count) // 2, (int(strip_count) + cut_count) // 2)) if finite(strip_count) and strip_count == int(strip_count) and strip_count >= cut_count and (strip_count - cut_count) % 2 == 0 else []
            checks[f'{role}_portal_shortened_strips_are_central'] = len(central_indices) == cut_count and shortened == central_indices
            for field in ('outer_strip_rest_ground_clearance_cm', 'cut_strip_rest_rail_clearance_cm'):
                checks[f'{role}_portal_{field}_positive'] = finite(portal.get(field)) and portal[field] > 0
            opening_min, opening_max = portal.get('opening_world_min_cm'), portal.get('opening_world_max_cm')
            checks[f'{role}_portal_world_aperture_finite'] = vector3(opening_min) and vector3(opening_max) and all(a <= b for a, b in zip(opening_min, opening_max))
            rails = portal.get('rail_continuations')
            checks[f'{role}_portal_rail_geometry_present'] = isinstance(rails, list) and len(rails) == THRESHOLDS['portal_rail_count'] and all(
                vector3(mapping(rail).get(field)) for rail in rails for field in ('center_world_cm', 'start_top_world_cm', 'end_top_world_cm'))
            if checks[f'{role}_portal_rail_geometry_present']:
                checks[f'{role}_portal_actual_rail_lengths'] = all(near(math.dist(rail['start_top_world_cm'], rail['end_top_world_cm']), THRESHOLDS['portal_tunnel_length_cm'], THRESHOLDS['portal_position_tolerance_cm']) for rail in rails)
                sorted_rails = sorted(rails, key=lambda rail: rail['start_top_world_cm'][0])
                outward = -1.0 if role == 'entry' else 1.0
                rail_z, tolerance = THRESHOLDS['source_rail_top_z_cm'], THRESHOLDS['portal_position_tolerance_cm']
                checks[f'{role}_portal_rails_join_measured_source_rails'] = all(
                    all(near(actual, expected, tolerance) for actual, expected in zip(rail['start_top_world_cm'], (rail_x, expected_position[1], rail_z)))
                    for rail, rail_x in zip(sorted_rails, THRESHOLDS['source_rail_centers_x_cm']))
                checks[f'{role}_portal_rails_extend_outward_at_source_height'] = all(
                    all(near(actual, expected, tolerance) for actual, expected in zip(rail['end_top_world_cm'], (rail_x, expected_position[1] + outward * THRESHOLDS['portal_tunnel_length_cm'], rail_z)))
                    for rail, rail_x in zip(sorted_rails, THRESHOLDS['source_rail_centers_x_cm']))
                checks[f'{role}_portal_declared_rail_top_matches_source'] = near(portal.get('world_rail_top_z_cm'), rail_z, tolerance)
            # TunnelResetCover is an authored 20 cm thick box centered at local
            # Y=tunnel_length_cm. Compare the complete body against its inner face.
            tunnel_length = portal.get('tunnel_length_cm')
            half_cover = THRESHOLDS['portal_reset_cover_thickness_cm'] * .5
            backcap_inner_y = margin = None
            if vector3(origin) and finite(tunnel_length):
                backcap_inner_y = origin[1] + (-tunnel_length + half_cover if role == 'entry' else tunnel_length - half_cover)
                body_bound = metrics['body_tunnel_coverage']['rest_world_min_cm' if role == 'entry' else 'working_max_cm']
                if vector3(body_bound) and (role == 'entry' or finite(departure_offset)):
                    margin = body_bound[1] - backcap_inner_y if role == 'entry' else backcap_inner_y - (body_bound[1] + departure_offset)
            metrics['body_tunnel_coverage'][role + '_backcap_inner_y_cm'] = backcap_inner_y
            metrics['body_tunnel_coverage'][role + '_body_to_backcap_margin_cm'] = margin
            checks[f'{role}_whole_body_clear_of_backcap'] = finite(margin) and margin > 0


    images = {}
    def load(name, expected_size):
        path = directory / name
        try:
            payload = path.read_bytes()
            with Image.open(path) as source:
                if source.format != 'PNG' or source.size != expected_size:
                    raise ValueError(f'Expected PNG {expected_size}, got {source.format} {source.size}')
                image = source.convert('RGB')
            report['files'][name] = {'bytes': len(payload), 'sha256': hashlib.sha256(payload).hexdigest(), 'size': expected_size}
            checks['valid_' + name] = True
            images[name] = image
            return image
        except (OSError, ValueError) as exc:
            checks['valid_' + name] = False
            errors.append(f'{name}: {exc}')
            return None

    for modality in ('nir', 'thermal'):
        names = tuple(f'sensor_{modality}_{suffix}.png' for suffix in SENSOR_SUFFIXES)
        for name in names:
            image = load(name, RAW_SIZE)
            if image is not None:
                value = image_stats(image)
                metrics[name] = value
                if modality == 'thermal':
                    checks[name + '_raw_grayscale'] = value['max_channel_spread_u8'] == 0
                    checks[name + '_has_hot_pixels'] = value['hot_pixels_above_220'] > 0
        checks[modality + '_frozen_png_bytes_identical'] = (
            all(name in report['files'] for name in names)
            and len({report['files'][name]['sha256'] for name in names if name in report['files']}) == 1)

    for name in HUD_NAMES:
        image = load(name, SCREEN_SIZE)
        if image is None:
            continue
        color, mono = image_stats(image.crop(COLOR_BOX)), image_stats(image.crop(MONO_BOX))
        metrics[name] = {'thermal_color': color, 'thermal_monochrome': mono}
        checks[name + '_thermal_color_visible'] = color['colored_pixel_fraction'] > THRESHOLDS['hud_colored_pixel_fraction_exclusive_min']
        checks[name + '_thermal_monochrome_gray'] = mono['max_channel_spread_u8'] <= THRESHOLDS['hud_monochrome_channel_spread_u8_max']
        checks[name + '_thermal_monochrome_nonblank'] = mono['luma_range_u8'] > THRESHOLDS['hud_nonblank_luma_range_u8_exclusive_min']
    for name in HEADLIGHT_NAMES:
        load(name, SCREEN_SIZE)
    if all(name in images for name in HEADLIGHT_NAMES):
        off, on = [images[name].crop(HEADLIGHT_BOX).convert('L') for name in HEADLIGHT_NAMES]
        diff = ImageChops.difference(off, on)
        gain = ImageStat.Stat(on).mean[0] - ImageStat.Stat(off).mean[0]
        fraction = sum(diff.histogram()[THRESHOLDS['headlight_changed_luma_u8_min']:]) / (off.width * off.height)
        metrics['headlight'] = {'mean_luma_gain_u8': gain, 'changed_fraction': fraction,
                                'off_mean_luma_u8': ImageStat.Stat(off).mean[0], 'on_mean_luma_u8': ImageStat.Stat(on).mean[0]}
        checks['headlight_visible_luma_gain'] = gain > THRESHOLDS['headlight_mean_luma_gain_u8_exclusive_min']
        checks['headlight_visible_changed_fraction'] = fraction > THRESHOLDS['headlight_changed_fraction_exclusive_min']

    report['failed_checks'] = [key for key, passed in checks.items() if not passed]
    report['passed'] = not errors and not report['failed_checks']
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--directory', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    report = verify(args.directory)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2, allow_nan=False) + '\n', encoding='utf-8')
    print(json.dumps({'passed': report['passed'], 'failed_checks': report['failed_checks'],
                      'errors': report['errors'], 'report': str(args.output.resolve())}, indent=2))
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    sys.exit(main())