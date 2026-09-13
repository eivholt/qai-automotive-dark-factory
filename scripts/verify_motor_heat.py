#!/usr/bin/env python3
"""Verify actual frozen joint-duty motor heat exports; Pillow only, no engine/model.

Exact eroded stencil masks isolate robot castings in raw thermal images. The
thermal model is an authored demonstration proxy, not calibrated motor physics.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import math
from pathlib import Path
import re

try:
    from PIL import Image, ImageChops, ImageFilter, ImageStat
except ImportError:
    raise SystemExit('Pillow is required; use the environment documented in docs/thermal_qa.md.')

STAGES = ('cold', 'working', 'hot', 'cooling', 'cooled')
DUTY_SECONDS = (0., 12., 84., 84., 84.)
IDLE_SECONDS = (0., 0., 0., 8., 30.)
ROLES = ('fixed_base', 'shoulder', 'upper_arm', 'elbow', 'forearm', 'wrist', 'flange')
CASTING = re.compile(r'^SM_robot_(?:px|nx)_(?:py|ny)_('+'|'.join(ROLES)+r')$')
FAMILIES = ('visible', 'color', 'monochrome', 'raw_thermal', 'raw_nir')
SCENE_CROP = (32, 135, 992, 711)
THRESHOLDS = {
    'driven_surfaces': 28, 'minimum_eroded_pixels': 12, 'minimum_eligible_driven_surfaces': 8,
    'minimum_pass_fraction': .8, 'minimum_hot_minus_cold_mean_u8': 3.,
    'minimum_each_cooldown_mean_drop_u8': .5, 'minimum_high_drop_c': 8.,
    'minimum_hot_p90_minus_p10_u8': 3., 'minimum_eligible_high_drop_surfaces': 8,
    'temperature_tolerance_c': .001, 'position_tolerance_cm': .01,
    'clock_tolerance_seconds': .0001, 'nonmotor_mean_absolute_change_max_u8': .1,
    'maximum_phase_boundary_jump_exclusive_c': .005, 'maximum_cache_midpoint_error_exclusive_c': .005,
    'maximum_radiometry_tables': 32, 'radiometry_samples_per_table': 81,
    'observer_hot_minus_cold_mean_u8': .25, 'observer_each_cooldown_mean_drop_u8': .05,
    'observer_iron_changed_pixel_u8': 3, 'observer_iron_changed_fraction_min': .001,
    'observer_monochrome_rgb_spread_max_u8': 1, 'observer_iron_rgb_spread_min_exclusive_u8': 2,
}


def finite(v):
    return type(v) in (int, float) and math.isfinite(v)


def near(a, b, tolerance=.001):
    return finite(a) and finite(b) and abs(a-b) <= tolerance


def obj(v):
    return v if isinstance(v, dict) else {}


def vector(v, length=3):
    return isinstance(v, list) and len(v) == length and all(finite(x) for x in v)


def transform(v):
    v = obj(v)
    return vector(v.get('position_cm')) and vector(v.get('rotation_xyzw'), 4) and vector(v.get('scale_xyz'))


def identified(values, key='id'):
    if not isinstance(values, list):
        return {}
    result = {}
    for row in values:
        row = obj(row)
        identity = row.get(key)
        if not isinstance(identity, str) or not identity or identity in result:
            return {}
        result[identity] = row
    return result


def spread(image):
    r, g, b = image.split()
    return ImageChops.subtract(ImageChops.lighter(r, ImageChops.lighter(g, b)), ImageChops.darker(r, ImageChops.darker(g, b))).getextrema()[1]


def histogram_stats(image, mask):
    hist = image.histogram(mask=mask)
    count = sum(hist)
    if not count:
        return None
    def percentile(fraction):
        target, total = max(1, math.ceil(count*fraction)), 0
        for i, n in enumerate(hist):
            total += n
            if total >= target:
                return i
    return {'pixels': count, 'mean_u8': sum(i*n for i, n in enumerate(hist))/count,
            'p10_u8': percentile(.1), 'p90_u8': percentile(.9)}


def verify(directory):
    directory = directory.resolve()
    result = {'scope': 'Actual frozen GPU motor heat QA: authored joint-duty temperatures and mesh-local fields, no bulk heat exchange or motor calibration.',
              'directory': str(directory), 'thresholds': THRESHOLDS, 'checks': {}, 'metrics': {'motors': {}}, 'files': {}, 'errors': []}
    checks, metrics = result['checks'], result['metrics']
    def require(name, passed, detail=None):
        checks[name] = bool(passed)
        if not passed and detail:
            result['errors'].append(name+': '+str(detail))
    def reject(value):
        raise ValueError('Nonfinite JSON constant: '+value)
    try:
        data = json.loads((directory/'motor_heat.json').read_text(encoding='utf-8-sig'), parse_constant=reject)
        if not isinstance(data, dict):
            raise ValueError('Expected JSON object')
        require('runtime_json_valid', True)
    except (OSError, ValueError) as exc:
        data = {}
        require('runtime_json_valid', False, exc)
    for flag in ('passed', 'geometry_unchanged', 'nir_unchanged', 'pause_freezes_motor_heat', 'thermal_restored', 'deterministic_seek', 'tool_heat_disabled', 'body_heat_disabled', 'sparks_disabled', 'arcs_disabled'):
        require('runtime_'+flag, data.get(flag) is True)
    require('fixed_pose_time_6', near(data.get('pose_time_seconds'), 6.))
    stages = identified(data.get('stages'), 'name')
    require('exact_five_stages', set(stages) == set(STAGES))
    stages = [obj(stages.get(name)) for name in STAGES]
    models = [obj(stage.get('motor_heat')) for stage in stages]
    poses = [obj(stage.get('pose')) for stage in stages]
    snapshots = [obj(stage.get('thermal_snapshot')) for stage in stages]
    motors = [identified(model.get('profiles')) for model in models]
    identities = set(motors[0])
    require('28_stable_motor_ids', len(identities) == 28 and all(set(m) == identities for m in motors))
    encoding_keys = ('width', 'height', 'min_temperature_c', 'max_temperature_c', 'ambient_temperature_c', 'band_um', 'quantity', 'linear_u8', 'lut_rows')
    encodings = [{k: s.get(k) for k in encoding_keys} for s in snapshots]
    metrics['thermal_encoding'] = encodings[0]
    require('thermal_encoding_fixed', all(e == encodings[0] for e in encodings))
    require('linear_apparent_20_to_120C_encoding', all(e['width'] == 448 and e['height'] == 256 and e['min_temperature_c'] == 20 and e['max_temperature_c'] == 120
            and e['ambient_temperature_c'] == 20 and e['band_um'] == [8, 14] and e['quantity'] == 'apparent_temperature' and e['linear_u8'] is True and e['lut_rows'] == 22 for e in encodings))
    thermal_profiles = []
    metrics['motor_model_self_checks'] = {}
    metrics['radiometry_caches'] = {}
    for name, stage, model, pose, snapshot, duty, idle in zip(STAGES, stages, models, poses, snapshots, DUTY_SECONDS, IDLE_SECONDS):
        require(name+'_motor_model', model.get('model') == 'authored-joint-duty-thermal-v1' and near(model.get('ambient_temperature_c'), 20.)
                and near(model.get('cycle_duration_seconds'), 18.) and near(model.get('integration_step_seconds'), 1/120, 1.e-7))
        require(name+'_motor_clock', near(model.get('duty_time_seconds'), duty, .0001) and near(model.get('idle_seconds'), idle, .0001))
        require(name+'_declared_motor_profile_count', model.get('profile_count') == 28)
        self_check = obj(model.get('self_check'))
        metrics['motor_model_self_checks'][name] = self_check
        require(name+'_native_model_self_checks', all(self_check.get(k) is True for k in ('passed', 'complete_casting_mapping', 'cold_start', 'large_time_finite_bounded', 'seek_order_independent', 'passive_cooling_monotone', 'cycle_phase_knot_continuity'))
                and near(self_check.get('boundary_epsilon_seconds'), 1.e-5, 1.e-9)
                and finite(self_check.get('boundary_count')) and self_check['boundary_count'] >= 13
                and finite(self_check.get('maximum_boundary_jump_c')) and 0 <= self_check['maximum_boundary_jump_c'] < .005)
        cache = obj(snapshot.get('motor_radiometry_cache'))
        metrics['radiometry_caches'][name] = cache
        tables = cache.get('tables', [])
        table_shape = isinstance(tables, list) and bool(tables) and all(isinstance(t, dict) for t in tables)
        require(name+'_radiometry_cache_bounded_and_validated', table_shape and cache.get('maximum_tables') == 32
                and finite(cache.get('table_count')) and cache['table_count'] == len(tables) and 1 <= len(tables) <= 32
                and cache.get('minimum_temperature_c') == 20 and cache.get('maximum_temperature_c') == 100 and cache.get('step_c') == 1
                and cache.get('validated') is True and finite(cache.get('maximum_midpoint_error_c')) and 0 <= cache['maximum_midpoint_error_c'] < .005)
        valid_tables = table_shape and all(finite(t.get('emissivity')) and 0 <= t['emissivity'] <= 1 and near(t.get('ambient_temperature_c'), 20.)
                and t.get('sample_count') == 81 and t.get('validated') is True
                and finite(t.get('maximum_midpoint_error_c')) and 0 <= t['maximum_midpoint_error_c'] < .005 for t in tables)
        require(name+'_radiometry_tables_unique_and_accurate', valid_tables and len({(t['emissivity'], t['ambient_temperature_c']) for t in tables}) == len(tables))
        require(name+'_radiometry_cache_reused_unchanged', bool(cache) and cache == obj(snapshots[0].get('motor_radiometry_cache')))
        require(name+'_pose_and_guards', near(pose.get('motion_seconds'), 6.) and near(pose.get('carriage_offset_y_cm'), 0., .01)
                and pose.get('body_visible') is True and pose.get('inference_enabled') is False and pose.get('training_enabled') is False)
        require(name+'_motor_metadata_matches_pose', pose.get('motor_heat') == stage.get('motor_heat'))
        require(name+'_spatial_fields_on_and_exchange_off', snapshot.get('spatial_fields_enabled') is True and snapshot.get('heat_transfer_enabled') is False
                and snapshot.get('adapter_ready') is False and snapshot.get('training_manifest') is False)
        require(name+'_camera_fixed', transform(snapshot.get('camera_world_transform')) and snapshot.get('camera_world_transform') == snapshots[0].get('camera_world_transform'))
        joint_rows = identified(pose.get('joints'))
        require(name+'_24_fixed_joint_transforms', len(joint_rows) == 24 and all(joint_rows == identified(p.get('joints')) for p in poses))
        profiles = snapshot.get('profiles', [])
        profiles = profiles if isinstance(profiles, list) else []
        valid = bool(profiles) and all(isinstance(p, dict) and isinstance(p.get('surface'), str) and finite(p.get('stencil_id'))
                and int(p['stencil_id']) == p['stencil_id'] and 1 <= p['stencil_id'] <= 255 for p in profiles)
        mapping = {int(p['stencil_id']): p for p in profiles} if valid else {}
        require(name+'_globally_unique_thermal_ids', valid and len(mapping) == len(profiles))
        thermal_profiles.append(mapping)
    require('thermal_profile_set_fixed', bool(thermal_profiles[0]) and all(set(p) == set(thermal_profiles[0]) for p in thermal_profiles))
    geometry_keys = ('surface', 'stencil_id', 'emissivity', 'authored_core_edge_drop_c', 'local_bounds_center', 'local_bounds_extent',
                     'world_transform', 'core_field_axis', 'core_field_offset_normalized', 'core_field_kernel', 'edge_falloff')
    for stencil, ref in thermal_profiles[0].items():
        profiles = [obj(p.get(stencil)) for p in thermal_profiles]
        require(f'profile_{stencil}_geometry_and_field_fixed', transform(ref.get('world_transform'))
                and all(all(p.get(k) == ref.get(k) for k in geometry_keys) for p in profiles))
        require(f'profile_{stencil}_no_bulk_exchange', all(p.get('simulate_exchange') is False for p in profiles))
    roles, motor_stencils = [], set()
    for identity in sorted(identities):
        entries = [obj(m.get(identity)) for m in motors]
        ref = entries[0]
        temperatures = [p.get('temperature_c') for p in entries]
        surface, stencil = ref.get('surface'), ref.get('stencil_id')
        match = CASTING.fullmatch(surface) if isinstance(surface, str) else None
        roles.append(match.group(1) if match else None)
        require(identity+'_scope_and_identity', bool(match) and ref.get('role') == match.group(1)
                and finite(stencil) and int(stencil) == stencil and 1 <= stencil <= 255
                and all(p.get('surface') == surface and p.get('stencil_id') == stencil and p.get('role') == ref.get('role') for p in entries))
        if finite(stencil) and int(stencil) == stencil:
            motor_stencils.add(int(stencil))
        fixed_keys = ('driving_joint_ids', 'maximum_temperature_c', 'heating_rate_c_per_second', 'cooling_tau_seconds', 'core_edge_drop_c', 'reference_speed_deg_per_second', 'driving_joint_weights')
        joints = ref.get('driving_joint_ids')
        valid_parameters = isinstance(joints, list) and bool(joints) and len(set(joints)) == len(joints)
        valid_parameters = valid_parameters and all(isinstance(j, str) and j in identified(poses[0].get('joints')) for j in joints)
        valid_parameters = valid_parameters and all(finite(ref.get(k)) for k in fixed_keys[1:-1])
        weights = ref.get('driving_joint_weights')
        valid_parameters = valid_parameters and isinstance(weights, list) and len(weights) == len(joints) and all(finite(w) and 0 < w <= 1 for w in weights) and near(sum(weights), 1., 1.e-6)
        if valid_parameters:
            valid_parameters = 20 < ref['maximum_temperature_c'] <= 120 and ref['heating_rate_c_per_second'] > 0 and ref['cooling_tau_seconds'] > 0 and ref['core_edge_drop_c'] > 0 and ref['reference_speed_deg_per_second'] > 0
        require(identity+'_fixed_valid_duty_parameters', valid_parameters and all(all(p.get(k) == ref.get(k) for k in fixed_keys) for p in entries))
        require(identity+'_bounded_activity', all(finite(p.get('activity')) and 0 <= p['activity'] <= 1 for p in entries))
        valid_temperatures = valid_parameters and all(finite(t) and 20-.001 <= t <= ref['maximum_temperature_c']+.001 for t in temperatures)
        require(identity+'_bounded_temperature', valid_temperatures)
        require(identity+'_warms_then_cools', valid_temperatures and near(temperatures[0], 20.) and temperatures[0] < temperatures[1] < temperatures[2]
                and temperatures[2] > temperatures[3] > temperatures[4] >= 20.)
        if valid_temperatures:
            require(identity+'_passive_cooldown_matches_tau', all(near(temperatures[i], 20+(temperatures[2]-20)*math.exp(-IDLE_SECONDS[i]/ref['cooling_tau_seconds'])) for i in (3, 4)))
        for name, entry, snapshot in zip(STAGES, entries, thermal_profiles):
            profile = obj(snapshot.get(stencil))
            core, edge = profile.get('core_surface_temperature_c'), profile.get('edge_surface_temperature_c')
            require(name+'_'+identity+'_thermal_profile_matches', profile.get('surface') == surface and near(profile.get('source_temperature_c'), entry.get('temperature_c'))
                    and near(core, entry.get('temperature_c')) and near(profile.get('authored_core_edge_drop_c'), entry.get('core_edge_drop_c'))
                    and profile.get('imported_core_field_enabled') is True
                    and finite(edge) and finite(core) and finite(entry.get('core_edge_drop_c'))
                    and near(edge, min(core, max(20., core-entry['core_edge_drop_c']))))
        metrics['motors'][identity] = {'surface': surface, 'stencil_id': stencil, 'role': ref.get('role'),
                                      'temperature_c': dict(zip(STAGES, temperatures)), 'parameters': {k: ref.get(k) for k in fixed_keys}}
    require('four_of_each_casting_role', len(roles) == 28 and all(roles.count(role) == 4 for role in ROLES))
    require('28_distinct_driven_thermal_ids', len(motor_stencils) == 28)
    for stencil, reference in thermal_profiles[0].items():
        if stencil not in motor_stencils:
            keys = ('source_temperature_c', 'core_surface_temperature_c', 'edge_surface_temperature_c', 'uniform_apparent_temperature_c', 'tool_tip_surface_temperature_c', 'weld_heat_patches')
            require(f'nonmotor_{stencil}_temperature_unchanged', all(all(obj(p.get(stencil)).get(k) == reference.get(k) for k in keys) for p in thermal_profiles))

    images, payloads = {}, {}
    def load(key, filename, raw):
        size = (448, 256) if raw else (1600, 900)
        try:
            if not isinstance(filename, str) or not filename or Path(filename).name != filename or '\\' in filename:
                raise ValueError('Expected a local filename')
            path = directory/filename
            payload = path.read_bytes()
            with Image.open(path) as image:
                if image.format != 'PNG' or image.size != size or image.mode not in ('L', 'RGB', 'RGBA'):
                    raise ValueError('Expected 8-bit PNG at '+str(size))
                if image.mode == 'RGBA' and image.getchannel('A').getextrema() != (255, 255):
                    raise ValueError('Expected opaque PNG')
                rgb = image.convert('RGB')
            images[key], payloads[key] = rgb, payload
            result['files'][key] = {'name': filename, 'size': size, 'bytes': len(payload), 'sha256': hashlib.sha256(payload).hexdigest()}
            require(key+'_valid_png', True)
            if raw:
                require(key+'_grayscale', spread(rgb) == 0)
        except (OSError, ValueError) as exc:
            require(key+'_valid_png', False, exc)
    for name, stage in zip(STAGES, stages):
        names = obj(stage.get('images'))
        for family in FAMILIES:
            load(name+'_'+family, names.get(family), family.startswith('raw_'))
    load('ids', data.get('surface_id_mask'), True)
    load('restored', 'motor_heat_restored_raw_thermal.png', True)
    nir_keys = [n+'_raw_nir' for n in STAGES]
    require('all_motor_states_nir_bytes_identical', all(k in payloads for k in nir_keys) and all(payloads[k] == payloads[nir_keys[0]] for k in nir_keys))
    require('restored_hot_thermal_bytes_identical', 'restored' in payloads and 'hot_raw_thermal' in payloads and payloads['restored'] == payloads['hot_raw_thermal'])
    eligible, heat_pass, field_eligible, field_pass = [], [], [], []
    if 'ids' in images and all(name+'_raw_thermal' in images for name in STAGES):
        ids = images['ids'].convert('L')
        used_ids = {i for i, n in enumerate(ids.histogram()) if n and i}
        require('id_pixels_known_to_thermal_snapshot', used_ids <= set(thermal_profiles[0]))
        raw = {name: images[name+'_raw_thermal'].convert('L') for name in STAGES}
        for identity, entry in metrics['motors'].items():
            stencil = entry['stencil_id']
            if not finite(stencil):
                continue
            mask = ids.point([255 if i == int(stencil) else 0 for i in range(256)]).filter(ImageFilter.MinFilter(3))
            count = mask.histogram()[255]
            entry['eroded_visible_pixels'] = count
            if count < 12:
                entry['render_scope'] = 'Too small or occluded after one-pixel erosion; scalar/association checks still apply.'
                continue
            eligible.append(identity)
            stats = {name: histogram_stats(image, mask) for name, image in raw.items()}
            entry['raw_thermal'] = stats
            rise = stats['hot']['mean_u8']-stats['cold']['mean_u8']
            drops = [stats[a]['mean_u8']-stats[b]['mean_u8'] for a, b in (('hot', 'cooling'), ('cooling', 'cooled'))]
            passes = rise >= 3. and all(d >= .5 for d in drops)
            entry['rendered_heating_cooling'] = {'hot_minus_cold_mean_u8': rise, 'cooldown_drops_u8': drops, 'passed': passes}
            if passes:
                heat_pass.append(identity)
            drop = entry['parameters'].get('core_edge_drop_c')
            if finite(drop) and drop >= 8.:
                field_eligible.append(identity)
                variation = stats['hot']['p90_u8']-stats['hot']['p10_u8']
                entry['hot_field_spread_p90_minus_p10_u8'] = variation
                if variation >= 3.:
                    field_pass.append(identity)
        nonmotor_changes = {}
        for stencil, p in thermal_profiles[0].items():
            if stencil in motor_stencils:
                continue
            mask = ids.point([255 if i == stencil else 0 for i in range(256)]).filter(ImageFilter.MinFilter(3))
            if mask.histogram()[255] < 12:
                continue
            changes = [histogram_stats(ImageChops.difference(raw['cold'], raw[name]), mask)['mean_u8'] for name in STAGES[1:]]
            nonmotor_changes[f'{stencil}:{p["surface"]}'] = max(changes)
        metrics['nonmotor_max_mean_absolute_change_u8'] = nonmotor_changes
        require('nonmotor_rendered_temperatures_unchanged', bool(nonmotor_changes) and all(v <= .1 for v in nonmotor_changes.values()))
    metrics['eligible_driven_surfaces'] = eligible
    metrics['heating_cooling_passing_surfaces'] = heat_pass
    metrics['eligible_high_drop_surfaces'] = field_eligible
    metrics['spatial_variation_passing_surfaces'] = field_pass
    require('enough_visible_driven_castings', len(eligible) >= 8)
    require('most_visible_castings_heat_and_cool', bool(eligible) and len(heat_pass)/len(eligible) >= .8)
    require('enough_visible_high_drop_castings', len(field_eligible) >= 8)
    require('most_hot_castings_keep_spatial_variation', bool(field_eligible) and len(field_pass)/len(field_eligible) >= .8)
    scene_means = {}
    for name in STAGES:
        mono, color = images.get(name+'_monochrome'), images.get(name+'_color')
        if mono:
            crop = mono.crop(SCENE_CROP)
            require(name+'_observer_monochrome_neutral', spread(crop) <= 1)
            scene_means[name] = ImageStat.Stat(crop.convert('L')).mean[0]
        if color:
            require(name+'_observer_iron_has_palette_color', spread(color.crop(SCENE_CROP)) > 2)
    metrics['observer_monochrome_scene_mean_u8'] = scene_means
    metrics['scene_crop_excluding_hud_px'] = SCENE_CROP
    require('observer_monochrome_heats_then_cools', set(scene_means) == set(STAGES) and scene_means['hot']-scene_means['cold'] >= .25
            and scene_means['hot']-scene_means['cooling'] >= .05 and scene_means['cooling']-scene_means['cooled'] >= .05)
    if 'cold_color' in images and 'hot_color' in images:
        difference = ImageChops.difference(images['cold_color'].crop(SCENE_CROP), images['hot_color'].crop(SCENE_CROP))
        channels = difference.split()
        maximum = ImageChops.lighter(channels[0], ImageChops.lighter(channels[1], channels[2]))
        fraction = sum(maximum.histogram()[3:])/(maximum.width*maximum.height)
        metrics['observer_iron_cold_hot_changed_fraction'] = fraction
        require('observer_iron_shows_motor_heat_change', fraction >= .001)
    else:
        require('observer_iron_shows_motor_heat_change', False)
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
    print(json.dumps({'passed': result['passed'], 'check_count': len(result['checks']), 'failed_checks': result['failed_checks'], 'errors': result['errors'], 'report': str(args.output.resolve())}, indent=2))
    return 0 if result['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
