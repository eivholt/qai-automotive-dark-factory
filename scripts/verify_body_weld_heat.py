#!/usr/bin/env python3
"""Check fixed-pose body-weld cooling renders. Requires Pillow; never calls Unreal.

Observer ROI measurements are unmasked. Raw sensor ROI measurements select only
exact body-surface stencil IDs. Authored glow/thermal proxies are not calibration.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import math
from pathlib import Path

from PIL import Image, ImageChops, ImageStat

STAGES = ('hot', 'warm', 'cool', 'fresh')
TIMES = (12., 14.5, 17., 18.)
FAMILIES = ('visible', 'nir', 'thermal', 'raw_nir', 'raw_thermal')
RAW_SIZE, SCREEN_SIZE = (448, 256), (1600, 900)
THRESHOLDS = {
    'spots': 4, 'unique_surfaces': 3, 'radius_cm': 12.,
    'position_tolerance_cm': .01, 'scalar_tolerance': .001, 'maximum_temperature_c': 420.,
    'contact_error_max_cm_exclusive': .5,
    'heat_time_tolerance_seconds': .0001,
    'mean_luma_drop_each_cooling_transition_u8_exclusive_min': .05,
    'fresh_luma_rise_above_cool_u8_max': .05,
    'minimum_spots_showing_cooling_per_family': 2,
    'minimum_selected_sensor_pixels': 3,
    'fresh_raw_thermal_baseline_mean_tolerance_u8': 1.,
    'raw_rgb_spread_max_u8': 0,
}


def finite(value):
    return type(value) in (int, float) and math.isfinite(value)


def obj(value):
    return value if isinstance(value, dict) else {}


def near(a, b, tolerance=.001):
    return finite(a) and finite(b) and abs(a-b) <= tolerance


def vector(value):
    return isinstance(value, list) and len(value) == 3 and all(finite(v) for v in value)


def vector_near(a, b):
    return vector(a) and vector(b) and max(abs(x-y) for x, y in zip(a, b)) <= THRESHOLDS['position_tolerance_cm']


def roi(value, size):
    if not isinstance(value, list) or len(value) != 4 or any(type(v) is not int for v in value):
        return None
    a, b, c, d = value
    return tuple(value) if 0 <= a < c <= size[0] and 0 <= b < d <= size[1] else None


def spread(image):
    r, g, b = image.split()
    return ImageChops.subtract(ImageChops.lighter(r, ImageChops.lighter(g, b)),
                               ImageChops.darker(r, ImageChops.darker(g, b))).getextrema()[1]


def verify(directory):
    directory = directory.resolve()
    result = {'scope': 'Body-local weld heating/cooling and fresh-body reset; illustrative optical and apparent-temperature proxies.',
              'directory': str(directory), 'thresholds': THRESHOLDS, 'checks': {},
              'metrics': {'spots': {}, 'image_families': {}}, 'files': {}, 'errors': []}
    checks, metrics = result['checks'], result['metrics']

    def require(name, value, detail=None):
        checks[name] = bool(value)
        if not value and detail:
            result['errors'].append(name + ': ' + str(detail))

    def reject_constant(value):
        raise ValueError('Nonfinite JSON constant: ' + value)

    try:
        runtime = json.loads((directory/'body_weld_heat.json').read_text(encoding='utf-8-sig'), parse_constant=reject_constant)
        if not isinstance(runtime, dict):
            raise ValueError('Body-heat metadata must be an object')
        require('runtime_json_valid', True)
    except (OSError, ValueError) as exc:
        runtime = {}
        require('runtime_json_valid', False, exc)
    for flag in ('passed', 'geometry_unchanged', 'pause_freezes_body_heat', 'lighting_unchanged',
                 'tool_heat_disabled', 'sparks_disabled', 'arcs_disabled'):
        require('runtime_' + flag, runtime.get(flag) is True)
    require('fixed_pose_time_13_seconds', near(runtime.get('pose_time_seconds'), 13., .0001))
    metrics['declared_model_scope'] = runtime.get('model_scope', runtime.get('scope'))
    raw_stages = runtime.get('stages', [])
    raw_stages = raw_stages if isinstance(raw_stages, list) else []
    stage_map = {x.get('name'): x for x in raw_stages if isinstance(x, dict) and isinstance(x.get('name'), str)}
    require('four_named_stages', len(raw_stages) == 4 and set(stage_map) == set(STAGES))
    stages = [obj(stage_map.get(name)) for name in STAGES]
    by_stage = []
    for name, time, stage in zip(STAGES, TIMES, stages):
        require(name + '_heat_time', near(stage.get('heat_time_seconds'), time, THRESHOLDS['heat_time_tolerance_seconds']))
        values = stage.get('spots', [])
        values = values if isinstance(values, list) else []
        spots = {x['id']: x for x in values if isinstance(x, dict) and isinstance(x.get('id'), str) and x['id']}
        require(name + '_four_unique_spots', len(values) == len(spots) == 4)
        by_stage.append(spots)
    identifiers = sorted(by_stage[0])
    require('spot_ids_constant', len(identifiers) == 4 and all(set(x) == set(identifiers) for x in by_stage))
    body_indices = [x.get('body_index') for x in stages]
    metrics['body_indices'] = dict(zip(STAGES, body_indices))
    require('fresh_body_index_increments_once', all(finite(x) and int(x) == x and x >= 0 for x in body_indices)
            and body_indices[0] == body_indices[1] == body_indices[2] and body_indices[3] == body_indices[0]+1)
    surfaces = [by_stage[0][i].get('surface') for i in identifiers]
    valid_surfaces = all(isinstance(x, str) and x.startswith('SM_BodyPanel_') for x in surfaces)
    surface_counts = {x: surfaces.count(x) for x in set(surfaces)} if valid_surfaces else {}
    metrics['spots_per_surface'] = surface_counts
    require('three_body_surfaces_one_with_two_spots', valid_surfaces and sorted(surface_counts.values()) == [1, 1, 2])

    snapshots = [obj(x.get('thermal_snapshot')) for x in stages]
    fields = ('width', 'height', 'min_temperature_c', 'max_temperature_c', 'quantity', 'linear_u8', 'band_um', 'ambient_temperature_c')
    encodings = [{key: snapshot.get(key) for key in fields} for snapshot in snapshots]
    metrics['thermal_encoding_by_stage'] = dict(zip(STAGES, encodings))
    require('thermal_encoding_constant', all(e == encodings[0] for e in encodings))
    require('thermal_encoding_expected', all(e['width'] == 448 and e['height'] == 256 and e['min_temperature_c'] == 20
            and e['max_temperature_c'] == 120 and e['quantity'] == 'apparent_temperature' and e['linear_u8'] is True
            and e['band_um'] == [8, 14] and finite(e['ambient_temperature_c']) for e in encodings))
    require('body_heat_lut_22_rows', all(s.get('lut_rows') == 22 for s in snapshots))
    require('thermal_remains_separate_from_training', all(s.get('training_manifest') is False and s.get('adapter_ready') is False
            and s.get('heat_transfer_enabled') is False for s in snapshots))
    matched_profiles = {}
    for identifier in identifiers:
        spots = [obj(x.get(identifier)) for x in by_stage]
        entry = {'surface': spots[0].get('surface'), 'stencil_id': spots[0].get('stencil_id'),
                 'scalars': {field: [s.get(field) for s in spots] for field in ('temperature_c', 'glow_strength')}}
        metrics['spots'][identifier] = entry
        for field, values in entry['scalars'].items():
            require(identifier + '_' + field + '_cools', all(finite(x) for x in values)
                    and values[0] > values[1] > values[2] >= values[3] >= 0)
        require(identifier + '_temperature_bounded', all(finite(v) and 20.-.001 <= v <= 420.+.001 for v in entry['scalars']['temperature_c']))
        require(identifier + '_fresh_resets_to_20c_zero_glow', near(spots[3].get('temperature_c'), 20.) and near(spots[3].get('glow_strength'), 0.))
        stencil = spots[0].get('stencil_id')
        require(identifier + '_stable_surface_stencil', valid_surfaces and finite(stencil) and int(stencil) == stencil and 1 <= stencil <= 255
                and all(s.get('surface') == spots[0].get('surface') and s.get('stencil_id') == stencil for s in spots))
        for field in ('local_position_cm', 'world_position_cm'):
            require(identifier + '_' + field + '_fixed', all(vector_near(spots[0].get(field), s.get(field)) for s in spots))
        require(identifier + '_radius_12cm', all(near(s.get('radius_cm'), 12.) for s in spots))
        require(identifier + '_contact_validated', all(finite(s.get('contact_error_cm')) and 0 <= s['contact_error_cm'] < .5 for s in spots))
        for name, spot, snapshot in zip(STAGES, spots, snapshots):
            profiles = snapshot.get('profiles', [])
            profiles = profiles if isinstance(profiles, list) else []
            matches = [p for p in profiles if isinstance(p, dict) and p.get('surface') == spot.get('surface')]
            profile = matches[0] if len(matches) == 1 else {}
            matched_profiles[(name, identifier)] = profile
            require(name + '_' + identifier + '_profile_stencil_matches', len(matches) == 1 and profile.get('stencil_id') == spot.get('stencil_id'))
            patches = profile.get('weld_heat_patches', [])
            patches = patches if isinstance(patches, list) else []
            matches = [p for p in patches if isinstance(p, dict) and vector_near(p.get('local_position_cm'), spot.get('local_position_cm'))]
            patch = matches[0] if len(matches) == 1 else {}
            require(name + '_' + identifier + '_thermal_patch_matches', len(matches) == 1
                    and vector_near(patch.get('world_position_cm'), spot.get('world_position_cm'))
                    and near(patch.get('radius_cm'), spot.get('radius_cm'))
                    and near(patch.get('surface_temperature_c'), spot.get('temperature_c')))
            require(name + '_' + identifier + '_shared_surface_patch_count', len(patches) == surface_counts.get(spot.get('surface')))

    verify_live_poses(directory, by_stage[0], require, metrics)

    def load_image(filename, size, key):
        try:
            if not isinstance(filename, str) or not filename:
                raise ValueError('Missing image filename')
            path = (directory/filename).resolve()
            if not path.is_relative_to(directory):
                raise ValueError('Image path escapes capture directory')
            payload = path.read_bytes()
            with Image.open(path) as image:
                if image.format != 'PNG' or image.size != size or image.mode not in ('L', 'RGB', 'RGBA'):
                    raise ValueError('Expected 8-bit PNG at ' + str(size))
                if image.mode == 'RGBA' and image.getchannel('A').getextrema() != (255, 255):
                    raise ValueError('PNG must be opaque')
                rgb = image.convert('RGB')
            result['files'][key] = {'name': filename, 'size': size, 'bytes': len(payload), 'sha256': hashlib.sha256(payload).hexdigest()}
            require(key + '_valid_image', True)
            return rgb
        except (OSError, ValueError) as exc:
            require(key + '_valid_image', False, exc)
            return None

    mask = load_image(runtime.get('surface_id_mask', 'body_weld_heat_surface_ids.png'), RAW_SIZE, 'surface_id_mask')
    require('surface_id_mask_neutral', mask is not None and spread(mask) == 0)
    mask = mask.convert('L') if mask is not None else None
    images = {}
    for name, stage in zip(STAGES, stages):
        names = obj(stage.get('images'))
        require(name + '_distinct_image_names', len({names.get(k) for k in FAMILIES if isinstance(names.get(k), str)}) == len(FAMILIES))
        for family in FAMILIES:
            image = load_image(names.get(family), RAW_SIZE if family.startswith('raw_') else SCREEN_SIZE, name+'_'+family)
            if image is not None:
                images[name, family] = image
                if family.startswith('raw_'):
                    require(name+'_'+family+'_grayscale', spread(image) == 0)
    for family in FAMILIES:
        sensor = family.startswith('raw_')
        fm = {'selection': 'Exact surface ID inside fixed sensor ROI' if sensor else 'Unmasked fixed observer ROI',
              'spots': {}, 'usable_spots': [], 'spots_showing_cooling': [], 'spots_returning_to_baseline': []}
        metrics['image_families'][family] = fm
        for identifier in identifiers:
            spots = [obj(x.get(identifier)) for x in by_stage]
            visibility = 'sensor_in_frame' if sensor else 'observer_in_frame'
            roi_key = 'sensor_roi_px' if sensor else 'observer_roi_px'
            entry = {'in_frame': [s.get(visibility) for s in spots]}
            fm['spots'][identifier] = entry
            require(family+'_'+identifier+'_visibility_fixed', all(type(s.get(visibility)) is bool and s.get(visibility) == spots[0].get(visibility) for s in spots))
            if not all(s.get(visibility) is True for s in spots):
                entry['excluded_reason'] = 'Not in frame at every stage'
                continue
            boxes = [roi(s.get(roi_key), RAW_SIZE if sensor else SCREEN_SIZE) for s in spots]
            valid = all(b is not None and b == boxes[0] for b in boxes)
            require(family+'_'+identifier+'_roi_fixed', valid)
            if not valid:
                continue
            entry['roi_px'] = boxes[0]
            selection = None
            if sensor:
                stencil = spots[0].get('stencil_id')
                if mask is None or not finite(stencil) or int(stencil) != stencil or not 1 <= stencil <= 255:
                    entry['excluded_reason'] = 'Invalid sensor ID mask or stencil'
                    continue
                selection = mask.crop(boxes[0]).point([255 if i == int(stencil) else 0 for i in range(256)])
                count = selection.histogram()[255]
                entry['selected_pixels'] = count
                if count < THRESHOLDS['minimum_selected_sensor_pixels']:
                    entry['excluded_reason'] = 'Too few visible target-surface pixels'
                    continue
            if not all((name, family) in images for name in STAGES):
                entry['excluded_reason'] = 'Missing stage image'
                continue
            means = [ImageStat.Stat(images[name, family].crop(boxes[0]).convert('L'), mask=selection).mean[0] for name in STAGES]
            drops = [means[i]-means[i+1] for i in range(3)]
            cooling = all(d > .05 for d in drops[:2])
            reset = means[0]-means[3] > .05 and means[3] <= means[2]+.05
            entry.update(mean_luma_u8=dict(zip(STAGES, means)), transition_drops_u8=drops,
                         shows_cooling=cooling, fresh_below_hot_and_not_brighter_than_cool=reset)
            fm['usable_spots'].append(identifier)
            if cooling:
                fm['spots_showing_cooling'].append(identifier)
            if reset:
                fm['spots_returning_to_baseline'].append(identifier)
            if family == 'raw_thermal':
                baseline = matched_profiles.get(('fresh', identifier), {}).get('uniform_apparent_temperature_c')
                expected = max(0., min(255., (baseline-20)*255/100)) if finite(baseline) else None
                entry['fresh_expected_uniform_panel_u8'] = expected
                entry['fresh_baseline_mean_error_u8'] = means[3]-expected if expected is not None else None
                require(identifier+'_fresh_raw_thermal_matches_panel_baseline', expected is not None and abs(means[3]-expected) <= THRESHOLDS['fresh_raw_thermal_baseline_mean_tolerance_u8'])
        require(family+'_at_least_two_spots_cool', len(fm['spots_showing_cooling']) >= 2)
        require(family+'_at_least_two_spots_reset', len(fm['spots_returning_to_baseline']) >= 2)
    result['failed_checks'] = [name for name, passed in checks.items() if not passed]
    result['passed'] = not result['failed_checks'] and not result['errors']
    return result



def verify_live_poses(directory, reference, require, metrics):
    """Independent production snapshots establish attachment and body replacement."""
    try:
        def reject(value):
            raise ValueError('Nonfinite JSON constant: ' + value)
        report = json.loads((directory/'welding_preview.json').read_text(encoding='utf-8-sig'), parse_constant=reject)
        if not isinstance(report, dict):
            raise ValueError('Production metadata must be an object')
        require('live_production_json_valid', True)
    except (OSError, ValueError) as exc:
        report = {}
        require('live_production_json_valid', False, exc)
    require('live_production_runtime_passed', report.get('passed') is True)
    cycle = obj(report.get('production_cycle'))
    poses = {'rest': obj(report.get('rest_pose')), 'arrival': obj(cycle.get('arrival_pose')),
             'work': obj(report.get('motion_pose')), 'parked': obj(cycle.get('parked_pose')),
             'departure': obj(cycle.get('departure_pose')), 'empty': obj(cycle.get('empty_pose')),
             'next_body': obj(cycle.get('next_body_pose'))}
    require('live_seven_pose_snapshots', all(bool(pose) for pose in poses.values()))
    times = [pose.get('motion_seconds') for pose in poses.values()]
    require('live_pose_time_order', all(finite(t) and t >= 0 for t in times)
            and all(a < b for a, b in zip(times, times[1:])))
    duration = poses['rest'].get('cycle_duration_seconds')
    require('live_cycle_duration_consistent', finite(duration) and duration > 17
            and all(near(pose.get('cycle_duration_seconds'), duration, .0001) for pose in poses.values()))
    identifiers = set(reference)
    lm = {'poses': {}, 'spots': {identifier: {} for identifier in sorted(identifiers)}}
    metrics['live_attachment_and_reset'] = lm
    maps = {}
    for name, pose in poses.items():
        heat = obj(pose.get('body_weld_heat'))
        values = heat.get('spots', [])
        values = values if isinstance(values, list) else []
        spots = {spot['id']: spot for spot in values if isinstance(spot, dict) and isinstance(spot.get('id'), str) and spot['id']}
        maps[name] = spots
        require('live_'+name+'_four_consistent_spots', len(values) == len(spots) == 4 and set(spots) == identifiers)
        require('live_'+name+'_heat_clock_matches_motion', near(heat.get('heat_time_seconds'), pose.get('motion_seconds'), .0001))
        index = pose.get('body_index')
        valid_index = finite(index) and index >= 0 and int(index) == index
        require('live_'+name+'_body_index_matches_heat', valid_index and heat.get('body_index') == index)
        require('live_'+name+'_body_index_matches_cycle', valid_index and finite(pose.get('motion_seconds'))
                and finite(duration) and duration > 0 and index == math.floor(pose['motion_seconds']/duration))
        require('live_'+name+'_model_training_disabled', pose.get('inference_enabled') is False and pose.get('training_enabled') is False)
        require('live_'+name+'_carriage_offset_finite', finite(pose.get('carriage_offset_y_cm')))
        lm['poses'][name] = {'motion_seconds': pose.get('motion_seconds'), 'heat_time_seconds': heat.get('heat_time_seconds'),
                             'body_index': index, 'carriage_offset_y_cm': pose.get('carriage_offset_y_cm')}
        for identifier in sorted(identifiers):
            spot = obj(spots.get(identifier))
            baseline = reference[identifier]
            valid_position = vector_near(spot.get('local_position_cm'), baseline.get('local_position_cm'))
            require('live_'+name+'_'+identifier+'_same_panel_local_site', valid_position
                    and spot.get('surface') == baseline.get('surface') and spot.get('stencil_id') == baseline.get('stencil_id')
                    and near(spot.get('radius_cm'), 12.))
            temperature, glow = spot.get('temperature_c'), spot.get('glow_strength')
            require('live_'+name+'_'+identifier+'_finite_bounded_heat', finite(temperature) and 20.-.001 <= temperature <= 420.+.001
                    and finite(glow) and glow >= 0)
            if name in ('rest', 'arrival', 'next_body'):
                require('live_'+name+'_'+identifier+'_cold_body', near(temperature, 20.) and near(glow, 0.))
            elif name == 'work':
                require('live_'+name+'_'+identifier+'_body_heated', finite(temperature) and temperature > 20.+.001 and finite(glow) and glow > 0)
            lm['spots'][identifier][name] = {'temperature_c': temperature, 'glow_strength': glow,
                                            'world_position_cm': spot.get('world_position_cm')}
    first_index = poses['rest'].get('body_index')
    require('live_fresh_body_index_one', first_index == 0 and poses['next_body'].get('body_index') == 1)
    for identifier in sorted(identifiers):
        rest = obj(maps['rest'].get(identifier))
        origin = rest.get('world_position_cm')
        initial_offset = poses['rest'].get('carriage_offset_y_cm')
        for name, pose in poses.items():
            spot = obj(maps[name].get(identifier))
            offset = pose.get('carriage_offset_y_cm')
            expected = [origin[0], origin[1]+offset-initial_offset, origin[2]] if vector(origin) and finite(offset) and finite(initial_offset) else None
            actual = spot.get('world_position_cm')
            require('live_'+name+'_'+identifier+'_follows_carriage', vector_near(actual, expected))
            lm['spots'][identifier][name]['expected_world_from_carriage_cm'] = expected
            lm['spots'][identifier][name]['attachment_error_cm'] = max(abs(a-b) for a, b in zip(actual, expected)) if vector(actual) and vector(expected) else None
        cooling = [obj(maps[name].get(identifier)) for name in ('parked', 'departure', 'empty')]
        temperatures = [spot.get('temperature_c') for spot in cooling]
        glows = [spot.get('glow_strength') for spot in cooling]
        require('live_'+identifier+'_stored_heat_cools_after_withdrawal', all(finite(t) for t in temperatures)
                and temperatures[0] > temperatures[1] > temperatures[2] > 20.)
        require('live_'+identifier+'_glow_fades_after_withdrawal', all(finite(g) for g in glows)
                and glows[0] > glows[1] >= glows[2] >= 0)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--directory', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    result = verify(args.directory)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2, allow_nan=False)+'\n', encoding='utf-8')
    print(json.dumps({'passed': result['passed'], 'failed_checks': result['failed_checks'], 'errors': result['errors'], 'report': str(args.output.resolve())}, indent=2))
    return 0 if result['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
