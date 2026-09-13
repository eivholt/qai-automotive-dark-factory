#!/usr/bin/env python3
"""Verify private CurtainSmoke physics telemetry and screenshot availability.

Screenshots establish that a rendered view exists; joint/settling/pause claims
come from the native per-frame measurements, not from nonblank image pixels.

Use --directory DIR --output REPORT for curtains.json, or add --process to
check the curtain telemetry in a completed three-body process_runtime.json.
The latter reports scene physics separately from model accuracy and does not
require the live curtain to have settled while a departing body is nearby.
"""
from __future__ import annotations

import argparse
import json
import math
from pathlib import Path, PureWindowsPath

try:
    from PIL import Image, ImageStat
except ImportError:
    raise SystemExit('Pillow is required; use the project QA environment.')

PASSES = (
    ('entry_forward', 0, -2050., 0.),
    ('entry_return', 0, 0., -2050.),
    ('entry_forward_repeat', 0, -2050., 0.),
    ('exit_forward', 1, 0., 2050.),
)
THRESHOLDS = {
    'screen_width': 1600, 'screen_height': 900,
    'scene_stddev_u8_exclusive_min': 1.,
    'max_samples': 7000, 'moving_samples_exclusive_min': 20,
    'settled_deflection_degrees_exclusive_max': 10.,
    'peak_deflection_degrees_exclusive_max': 120.,
    'anchor_error_cm_exclusive_max': 10.,
    'pause_duration_seconds_min': .75,
    'pause_pose_drift_cm_exclusive_max': .1,
    'pause_rotation_drift_degrees_exclusive_max': .1,
    'velocity_restore_error_exclusive_max': .1,
}


def finite(value):
    return type(value) in (int, float) and math.isfinite(value)


def finite_tree(value):
    if isinstance(value, dict):
        return all(finite_tree(item) for item in value.values())
    if isinstance(value, list):
        return all(finite_tree(item) for item in value)
    return finite(value) if type(value) in (int, float) else True


def near(value, expected, tolerance=1e-4):
    return finite(value) and abs(value-expected) <= tolerance


def bounded(value, high):
    return finite(value) and 0 <= value < high


def mapping(value):
    return value if isinstance(value, dict) else {}


def vector(value, length=2):
    return isinstance(value, list) and len(value) == length and all(finite(x) for x in value)


def verify(directory):
    directory = Path(directory).resolve()
    checks, errors, metrics = {}, [], {}
    result = {'schema': 'darkfactory-curtain-verification-v1', 'directory': str(directory),
              'checks': checks, 'errors': errors, 'metrics': metrics,
              'thresholds': THRESHOLDS, 'model_accuracy_evidence': False}

    def require(name, passed):
        checks[name] = bool(passed)

    def finish():
        result['failed_checks'] = [name for name, good in checks.items() if not good]
        result['checks_count'] = len(checks)
        result['passed'] = not result['failed_checks'] and not errors
        return result

    try:
        report = json.loads((directory/'curtains.json').read_text(encoding='utf-8-sig'))
    except (OSError, ValueError) as exc:
        require('native_report_readable', False)
        errors.append(str(exc))
        return finish()
    require('native_report_object', isinstance(report, dict))
    report = mapping(report)
    require('native_numbers_finite', finite_tree(report))
    if not checks['native_numbers_finite']:
        errors.append('Native report contains a nonfinite number.')
        return finish()
    require('native_schema', report.get('schema') == 'darkfactory-curtain-smoke-v1')
    require('native_source', report.get('source') == 'chaos_physics_fixture')
    require('session_identity', isinstance(report.get('session_id'), str) and bool(report['session_id'].strip()))
    require('native_passed', report.get('passed') is True)
    require('no_model_http_requests', type(report.get('model_http_requests')) is int and report['model_http_requests'] == 0)
    require('one_initial_reset_per_portal', type(report.get('initial_resets_per_portal')) is int and report['initial_resets_per_portal'] == 1)
    require('no_measurement_resets', type(report.get('measurement_resets')) is int and report['measurement_resets'] == 0)
    require('process_travel_distance', near(report.get('travel_distance_cm'), 2050.))
    require('process_travel_duration', near(report.get('travel_seconds'), 3.2))
    require('settle_duration', near(report.get('settle_seconds'), 10.))
    frame_delta = report.get('max_frame_delta_seconds')
    require('measured_frame_delta_bounded', finite(frame_delta) and 0 < frame_delta <= .101)
    tolerance = frame_delta+1e-4 if finite(frame_delta) and 0 < frame_delta <= .101 else .101

    def portal_metadata(prefix, raw):
        metadata = mapping(raw)
        require(prefix+'_metadata_present', bool(metadata))
        require(prefix+'_finite_physics', metadata.get('finite_physics_state') is True)
        require(prefix+'_all_constraints_live', type(metadata.get('live_constraints')) is int and metadata['live_constraints'] == 42)
        require(prefix+'_initial_binding_completed', metadata.get('initial_binding_pending') is False)
        require(prefix+'_process_tunnel_length', near(metadata.get('tunnel_length_cm'), 1400.))
        for field, maximum in (
            ('current_deflection_degrees', 10.), ('peak_deflection_degrees', 120.),
            ('current_anchor_error_cm', 10.), ('peak_anchor_error_cm', 10.),
        ):
            require(prefix+'_'+field, bounded(metadata.get(field), maximum))
        require(prefix+'_peak_not_below_current_deflection', finite(metadata.get('peak_deflection_degrees'))
                and finite(metadata.get('current_deflection_degrees'))
                and metadata['peak_deflection_degrees']+1e-4 >= metadata['current_deflection_degrees'])
        contacts = metadata.get('contact_events_by_component')
        require(prefix+'_contact_counters_valid', isinstance(contacts, dict)
                and all(type(v) is int and v >= 0 for v in contacts.values()))
        require(prefix+'_no_tunnel_ceiling_contacts', isinstance(contacts, dict) and contacts.get('TunnelCeiling', 0) == 0)
        return metadata

    image_metrics = {}
    used_paths = []

    def screenshot(prefix, value):
        require(prefix+'_path_present', isinstance(value, str) and bool(value))
        if not isinstance(value, str) or not value:
            return
        # Native reports can be evaluated in WSL. Resolve their Windows absolute
        # screenshots by filename within the same private report directory.
        path = Path(value.replace('\\', '/'))
        if PureWindowsPath(value).is_absolute() and not path.is_absolute():
            path = directory/PureWindowsPath(value).name
        elif not path.is_absolute():
            path = directory/path
        path = path.resolve()
        require(prefix+'_inside_fixture', path.is_relative_to(directory))
        if not path.is_relative_to(directory):
            return
        used_paths.append(path)
        try:
            with Image.open(path) as source:
                require(prefix+'_png', source.format == 'PNG')
                require(prefix+'_size', source.size == (1600, 900))
                source.load()
                gray = source.convert('L')
                # Omit HUD/status bands when checking that scene pixels exist.
                crop = gray.crop((40, 150, min(1200, gray.width), min(800, gray.height))) if gray.width >= 1200 and gray.height >= 800 else gray
                stats = ImageStat.Stat(crop)
                image_metrics[prefix] = {'path': str(path), 'size': list(source.size),
                                         'mean_u8': stats.mean[0], 'stddev_u8': stats.stddev[0]}
                require(prefix+'_scene_nonblank', stats.stddev[0] > THRESHOLDS['scene_stddev_u8_exclusive_min'])
        except (OSError, ValueError) as exc:
            require(prefix+'_readable', False)
            errors.append(str(exc))

    passes = report.get('passes')
    require('exact_pass_sequence', isinstance(passes, list) and len(passes) == len(PASSES)
            and [mapping(row).get('name') for row in passes] == [row[0] for row in PASSES])
    pass_rows = {}
    for index, (name, portal, start, end) in enumerate(PASSES):
        row = mapping(passes[index]) if isinstance(passes, list) and index < len(passes) else {}
        pass_rows[name] = row
        require(name+'_portal', row.get('portal_index') == portal and type(row.get('portal_index')) is int)
        require(name+'_start_offset', near(row.get('start_offset_cm'), start))
        require(name+'_end_offset', near(row.get('end_offset_cm'), end))
        removed = name in ('entry_return', 'exit_forward')
        require(name+'_body_removed_before_settling', row.get('body_removed_before_settling') is removed)
        colliders = row.get('carriage_colliders_during_settle')
        require(name+'_settled_carriage_collision_state', type(colliders) is int
                and (colliders == 0 if removed else colliders > 0))
        require(name+'_actual_contacts', type(row.get('contact_events_delta')) is int and row['contact_events_delta'] > 0)
        require(name+'_moving_samples', type(row.get('moving_samples')) is int and row['moving_samples'] > 20)
        duration = row.get('travel_duration_seconds')
        require(name+'_measured_travel_duration', finite(duration) and 3.2-1e-4 <= duration <= 3.2+tolerance)
        start_clock, end_clock = row.get('move_start_clock_seconds'), row.get('move_end_clock_seconds')
        require(name+'_movement_clock_interval', finite(start_clock) and finite(end_clock) and start_clock >= 0
                and finite(duration) and near(end_clock-start_clock, duration))
        clear_clock, settled_clock = row.get('clear_clock_seconds'), row.get('settled_clock_seconds')
        after_clear = row.get('settled_after_clear_seconds')
        require(name+'_full_settling_time', finite(after_clear) and after_clear >= 10.-1e-4)
        require(name+'_settled_clock_interval', finite(clear_clock) and finite(settled_clock) and finite(end_clock)
                and clear_clock+1e-4 >= end_clock and finite(after_clear) and near(settled_clock-clear_clock, after_clear))
        portal_metadata(name+'_settled', row.get('settled_metadata'))
        screenshot(name+'_during', row.get('during_screenshot'))
        screenshot(name+'_settled', row.get('settled_screenshot'))
    require('eight_distinct_screenshot_paths', len(used_paths) == 8 and len(set(used_paths)) == 8)
    metrics['screenshots'] = image_metrics

    final = report.get('final_portals')
    require('two_final_portals', isinstance(final, list) and len(final) == 2)
    if isinstance(final, list):
        for index, metadata in enumerate(final[:2]):
            portal_metadata(f'final_portal_{index}', metadata)

    samples = report.get('samples')
    require('bounded_nonempty_per_frame_samples', isinstance(samples, list) and 80 < len(samples) <= THRESHOLDS['max_samples'])
    samples = samples[:THRESHOLDS['max_samples']] if isinstance(samples, list) else []
    valid_shape = True
    finite_physics = True
    angle_bounds = True
    anchor_bounds = True
    monotonic = True
    paused_clock_frozen = True
    previous = None
    grouped = {name: [] for name, *_ in PASSES}
    paused_count = 0
    for raw in samples:
        row = mapping(raw)
        clock, offset = row.get('clock_seconds'), row.get('offset_cm')
        finite_pair = row.get('finite_physics_state')
        angles, anchors = row.get('deflection_degrees'), row.get('anchor_error_cm')
        shape = (finite(clock) and clock >= 0 and finite(offset) and isinstance(row.get('pass'), str)
                 and type(row.get('paused')) is bool and isinstance(finite_pair, list) and len(finite_pair) == 2
                 and all(type(v) is bool for v in finite_pair) and vector(angles) and vector(anchors))
        valid_shape &= shape
        if not shape:
            continue
        finite_physics &= all(finite_pair)
        angle_bounds &= all(bounded(value, 120.) for value in angles)
        anchor_bounds &= all(bounded(value, 10.) for value in anchors)
        if previous is not None:
            monotonic &= clock >= previous['clock_seconds']
            if row['paused'] and previous['paused']:
                paused_clock_frozen &= near(clock, previous['clock_seconds']) and near(offset, previous['offset_cm'])
        previous = row
        paused_count += int(row['paused'])
        if row['pass'] in grouped:
            grouped[row['pass']].append(row)
    require('sample_shapes_and_numbers_valid', valid_shape)
    require('all_sample_physics_finite', finite_physics)
    require('all_sample_deflections_below_120', angle_bounds)
    require('all_sample_anchor_errors_below_10cm', anchor_bounds)
    require('sample_clock_monotonic', monotonic)
    require('pause_has_per_frame_evidence', paused_count > 1)
    require('paused_samples_freeze_clock_and_carriage', paused_clock_frozen)
    for name, portal, start, end in PASSES:
        rows = grouped[name]
        moving = [row for row in rows if not row['paused'] and min(start, end)+.01 < row['offset_cm'] < max(start, end)-.01]
        require(name+'_trace_has_actual_motion', len(moving) > 20)
        require(name+'_trace_reaches_endpoint', any(near(row['offset_cm'], end, .01) for row in rows))
        require(name+'_trace_stays_in_travel_bounds', bool(rows) and all(min(start, end)-.01 <= row['offset_cm'] <= max(start, end)+.01 for row in rows))
        direction = 1. if end > start else -1.
        require(name+'_trace_moves_in_correct_direction', bool(rows) and all(
            direction*(b['offset_cm']-a['offset_cm']) >= -.01 for a, b in zip(rows, rows[1:])))
        declared = pass_rows[name]
        clear_clock, settled_clock = declared.get('clear_clock_seconds'), declared.get('settled_clock_seconds')
        require(name+'_last_sample_matches_settled_clock', bool(rows) and finite(settled_clock)
                and near(rows[-1]['clock_seconds'], settled_clock))
        settling = [row for row in rows if finite(clear_clock) and finite(settled_clock)
                    and clear_clock-1e-4 <= row['clock_seconds'] <= settled_clock+1e-4]
        require(name+'_trace_covers_full_settling_interval', len(settling) > 1
                and near(settling[0]['clock_seconds'], clear_clock)
                and near(settling[-1]['clock_seconds'], settled_clock)
                and settling[-1]['clock_seconds']-settling[0]['clock_seconds'] >= 10.-1e-4
                and all(near(row['offset_cm'], end, .01) for row in settling))
        require(name+'_settling_samples_have_no_missing_frame_interval', len(settling) > 1
                and all(0 <= b['clock_seconds']-a['clock_seconds'] <= tolerance
                        for a, b in zip(settling, settling[1:])))
    metrics['sample_count'] = len(samples)
    metrics['paused_samples'] = paused_count

    pause = mapping(report.get('pause'))
    require('moving_pause_tested', pause.get('tested') is True)
    require('pause_duration', finite(pause.get('duration_seconds')) and pause['duration_seconds'] >= .75-1e-4)
    require('pause_starts_deflected', finite(pause.get('deflection_before_degrees')) and pause['deflection_before_degrees'] > 5.)
    require('pause_starts_with_momentum', any(finite(pause.get(key)) and pause[key] > 1.
            for key in ('linear_velocity_before_cm_s', 'angular_velocity_before_deg_s')))
    for field in ('peak_pose_drift_cm', 'peak_rotation_drift_degrees',
                  'linear_velocity_restore_error_cm_s', 'angular_velocity_restore_error_deg_s'):
        require('pause_'+field, bounded(pause.get(field), .1))
    metrics['pause'] = pause
    metrics['visual_review_required'] = ('Review both portals during contact and after settling. Nonblank PNGs do not establish '
                                         'plausible strip shape, physics accuracy, or visibility of every strip.')
    return finish()



def verify_process(directory):
    """Check live three-body curtain integrity; not model accuracy or settling."""
    directory = Path(directory).resolve()
    checks, errors = {}, []
    result = {'schema': 'darkfactory-process-curtain-verification-v1', 'directory': str(directory),
              'scope': 'scene_physics_only', 'model_accuracy_evidence': False,
              'final_settling_required': False, 'checks': checks, 'errors': errors}

    def require(name, passed):
        checks[name] = bool(passed)

    def finish():
        result['failed_checks'] = [name for name, good in checks.items() if not good]
        result['checks_count'] = len(checks)
        result['passed'] = not result['failed_checks'] and not errors
        result['scene_physics_passed'] = result['passed']
        return result

    try:
        report = json.loads((directory/'process_runtime.json').read_text(encoding='utf-8-sig'))
    except (OSError, ValueError) as exc:
        require('process_report_readable', False)
        errors.append(str(exc))
        return finish()
    require('process_report_object', isinstance(report, dict))
    report = mapping(report)
    require('process_numbers_finite', finite_tree(report))
    if not checks['process_numbers_finite']:
        errors.append('Process report contains a nonfinite number.')
        return finish()
    require('actual_host_gpu_source', report.get('source') == 'host_gpu')
    require('three_body_model_smoke_fixture', report.get('model_smoke') is True)
    require('native_model_smoke_passed', report.get('passed') is True)
    require('three_bodies_completed', type(report.get('completed_bodies')) is int and report['completed_bodies'] == 3)
    portals = report.get('curtain_portals')
    require('two_live_portals', isinstance(portals, list) and len(portals) == 2)
    if isinstance(portals, list):
        for index, raw in enumerate(portals[:2]):
            portal = mapping(raw)
            prefix = f'portal_{index}'
            require(prefix+'_constraint_count', type(portal.get('constraints')) is int and portal['constraints'] == 42)
            require(prefix+'_all_constraints_live', type(portal.get('live_constraints')) is int and portal['live_constraints'] == 42)
            require(prefix+'_initial_binding_completed', portal.get('initial_binding_pending') is False)
            require(prefix+'_process_tunnel_length', near(portal.get('tunnel_length_cm'), 1400.))
            require(prefix+'_finite_physics', portal.get('finite_physics_state') is True)
            require(prefix+'_peak_deflection_below_120_degrees', bounded(portal.get('peak_deflection_degrees'), 120.))
            require(prefix+'_peak_anchor_error_below_10cm', bounded(portal.get('peak_anchor_error_cm'), 10.))
            contacts = portal.get('carriage_contact_events')
            require(prefix+'_actual_carriage_contacts', type(contacts) is int and contacts > 0)
            components = portal.get('contact_events_by_component')
            require(prefix+'_contact_counters_valid', isinstance(components, dict)
                    and all(type(value) is int and value >= 0 for value in components.values()))
            require(prefix+'_no_ceiling_contacts', isinstance(components, dict) and components.get('TunnelCeiling', 0) == 0)
    return finish()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--directory', type=Path, required=True)
    parser.add_argument('--process', action='store_true', help='Check live process_runtime.json curtain telemetry instead of curtains.json.')
    parser.add_argument('--output', '--report', dest='output', type=Path, required=True)
    args = parser.parse_args()
    result = verify_process(args.directory) if args.process else verify(args.directory)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2, allow_nan=False)+'\n', encoding='utf-8', newline='\n')
    print(json.dumps({'passed': result['passed'], 'checks': len(result['checks']),
                      'failed_checks': result['failed_checks'], 'errors': result['errors'],
                      'report': str(args.output.resolve())}, indent=2))
    return 0 if result['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
