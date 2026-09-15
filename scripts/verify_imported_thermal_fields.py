#!/usr/bin/env python3
"""Verify imported robot thermal fields against a frozen uniform rendering.

Uses Pillow only. Exact eroded stencil masks isolate surfaces in raw images;
observer screenshots check palette presentation, not material temperatures.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import math
from pathlib import Path
import re

from PIL import Image, ImageChops, ImageFilter, ImageStat

RAW_SIZE = (448, 256)
SCREEN_SIZE = (1600, 900)
ROLES = ('fixed_base', 'shoulder', 'upper_arm', 'elbow', 'forearm', 'wrist', 'flange')
CASTING = re.compile(r'^SM_robot_(?:px|nx)_(?:py|ny)_(' + '|'.join(ROLES) + r')$')
THRESHOLDS = {'configured_fields': 28, 'minimum_eroded_surface_pixels': 12,
              'minimum_high_drop_c': 8., 'minimum_eligible_high_drop_fields': 8,
              'minimum_high_drop_pass_fraction': .8, 'minimum_on_p90_minus_p10_u8': 3,
              'minimum_mean_drop_from_uniform_u8': 2., 'maximum_uniform_mask_span_u8': 1,
              'maximum_uniform_mean_error_u8': 1., 'maximum_flat_profile_change_u8': 1,
              'profile_tolerance_c': .001, 'offset_normalized': -.18,
              'offset_tolerance': .00001, 'raw_rgb_spread_max_u8': 0,
              'monochrome_scene_rgb_spread_max_u8': 1, 'iron_scene_rgb_spread_min_u8_exclusive': 2}


def finite(value):
    return type(value) in (int, float) and math.isfinite(value)


def near(a, b, tolerance=.001):
    return finite(a) and finite(b) and abs(a-b) <= tolerance


def obj(value):
    return value if isinstance(value, dict) else {}


def vector(value, length=3):
    return isinstance(value, list) and len(value) == length and all(finite(x) for x in value)


def transform(value):
    value = obj(value)
    return vector(value.get('position_cm')) and vector(value.get('rotation_xyzw'), 4) and vector(value.get('scale_xyz'))


def rgb_spread(image):
    r, g, b = image.split()
    return ImageChops.subtract(ImageChops.lighter(r, ImageChops.lighter(g, b)),
                               ImageChops.darker(r, ImageChops.darker(g, b))).getextrema()[1]


def histogram_metrics(image, mask):
    histogram = image.histogram(mask=mask)
    count = sum(histogram)
    if not count:
        return None
    values = [i for i, n in enumerate(histogram) if n]
    def percentile(fraction):
        target = max(1, math.ceil(count*fraction))
        total = 0
        for i, n in enumerate(histogram):
            total += n
            if total >= target:
                return i
        return 255
    return {'pixels': count, 'min_u8': values[0], 'max_u8': values[-1],
            'mean_u8': sum(i*n for i, n in enumerate(histogram))/count,
            'p10_u8': percentile(.1), 'p90_u8': percentile(.9),
            'span_u8': values[-1]-values[0]}


def verify(directory):
    directory = directory.resolve()
    result = {'scope': 'Imported casting field geometry/render QA; authored surface temperatures, not heat-transfer calibration.',
              'thresholds': THRESHOLDS, 'directory': str(directory), 'checks': {}, 'metrics': {'profiles': {}}, 'files': {}, 'errors': []}
    checks, metrics = result['checks'], result['metrics']
    def require(name, passed, detail=None):
        checks[name] = bool(passed)
        if not passed and detail:
            result['errors'].append(name + ': ' + str(detail))
    try:
        def reject(value):
            raise ValueError('Nonfinite JSON constant: ' + value)
        runtime = json.loads((directory/'imported_thermal_fields.json').read_text(encoding='utf-8-sig'), parse_constant=reject)
        if not isinstance(runtime, dict):
            raise ValueError('Runtime report must be an object')
        require('runtime_json_valid', True)
    except (OSError, ValueError) as exc:
        runtime = {}
        require('runtime_json_valid', False, exc)
    for flag in ('passed', 'geometry_unchanged', 'nir_unchanged', 'thermal_restored'):
        require('runtime_'+flag, runtime.get(flag) is True)
    snapshots = [obj(runtime.get('snapshot_'+name)) for name in ('on', 'off')]
    enc_keys = ('width', 'height', 'min_temperature_c', 'max_temperature_c', 'ambient_temperature_c', 'quantity', 'linear_u8', 'band_um', 'lut_rows')
    encodings = [{key: s.get(key) for key in enc_keys} for s in snapshots]
    metrics['encodings'] = dict(zip(('on', 'off'), encodings))
    require('thermal_encoding_unchanged', encodings[0] == encodings[1])
    require('thermal_encoding_expected', all(e['width'] == 448 and e['height'] == 256 and e['min_temperature_c'] == 20
            and e['max_temperature_c'] == 120 and e['quantity'] == 'apparent_temperature' and e['linear_u8'] is True
            and e['band_um'] == [8, 14] and e['lut_rows'] == 70 and finite(e['ambient_temperature_c']) for e in encodings))
    require('spatial_toggle_recorded', snapshots[0].get('spatial_fields_enabled') is True and snapshots[1].get('spatial_fields_enabled') is False)
    require('no_imported_heat_exchange_or_training', all(s.get('heat_transfer_enabled') is False and s.get('adapter_ready') is False and s.get('training_manifest') is False for s in snapshots))
    require('camera_transform_fixed', transform(snapshots[0].get('camera_world_transform')) and snapshots[0].get('camera_world_transform') == snapshots[1].get('camera_world_transform'))
    profile_maps = []
    for name, snapshot in zip(('on', 'off'), snapshots):
        rows = snapshot.get('profiles', [])
        rows = rows if isinstance(rows, list) else []
        named = [p for p in rows if isinstance(p, dict) and isinstance(p.get('surface'), str) and p['surface']]
        require(name+'_profiles_have_surface_names', bool(rows) and len(named) == len(rows))
        stencils = [p.get('stencil_id') for p in named]
        valid = all(finite(x) and int(x) == x and 1 <= x <= 255 for x in stencils)
        require(name+'_unique_valid_stencils', bool(rows) and valid and len(set(stencils)) == len(rows))
        # Component names are actor-local: separate portals intentionally reuse
        # names. The GPU's globally unique stencil ID identifies each surface.
        profiles = {f"{int(p['stencil_id']):03d}:{p['surface']}": p for p in named
                    if finite(p.get('stencil_id')) and int(p['stencil_id']) == p['stencil_id'] and 1 <= p['stencil_id'] <= 255}
        metrics[name+'_profile_count'] = len(rows)
        metrics[name+'_distinct_component_names'] = len({p['surface'] for p in named})
        profile_maps.append(profiles)
    on_profiles, off_profiles = profile_maps
    require('profile_set_unchanged', bool(on_profiles) and set(on_profiles) == set(off_profiles))
    fields, roles = [], []
    for surface, profile in on_profiles.items():
        off = obj(off_profiles.get(surface))
        drop = profile.get('authored_core_edge_drop_c')
        match = CASTING.fullmatch(profile['surface'])
        configured = finite(drop) and drop > 0
        if configured:
            fields.append(surface)
            if match:
                roles.append(match.group(1))
        entry = {'surface': profile['surface'], 'stencil_id': profile.get('stencil_id'), 'configured': configured,
                 'role': match.group(1) if match else None, 'authored_core_edge_drop_c': drop,
                 'core_temperature_c': profile.get('core_surface_temperature_c'), 'edge_temperature_c': profile.get('edge_surface_temperature_c')}
        metrics['profiles'][surface] = entry
        require(surface+'_drop_valid', finite(drop) and drop >= 0)
        require(surface+'_field_scope', configured == bool(match) and profile.get('imported_core_field_enabled') is configured
                and off.get('imported_core_field_enabled') is False)
        require(surface+'_no_exchange', profile.get('simulate_exchange') is False and off.get('simulate_exchange') is False)
        fixed = ('stencil_id', 'authored_core_edge_drop_c', 'core_surface_temperature_c', 'edge_surface_temperature_c',
                 'uniform_apparent_temperature_c', 'local_bounds_center', 'local_bounds_extent', 'world_transform',
                 'core_field_axis', 'core_field_offset_normalized', 'core_field_kernel')
        require(surface+'_profile_geometry_and_parameters_fixed', all(profile.get(k) == off.get(k) for k in fixed)
                and transform(profile.get('world_transform')))
        if configured:
            extent = profile.get('local_bounds_extent')
            valid_extent = vector(extent) and all(v > 0 for v in extent)
            axis = max(range(3), key=lambda i: extent[i]) if valid_extent else None
            require(surface+'_axis_matches_longest_local_extent', valid_extent and profile.get('core_field_axis') == axis)
            require(surface+'_offset_minus_point18', near(profile.get('core_field_offset_normalized'), -.18, .00001))
            kernel = profile.get('core_field_kernel')
            require(surface+'_imported_core_kernel', isinstance(kernel, str) and kernel.startswith('imported-core-v1:'))
            ambient = snapshots[0].get('ambient_temperature_c')
            core, edge = profile.get('core_surface_temperature_c'), profile.get('edge_surface_temperature_c')
            valid_temp = all(finite(v) for v in (ambient, core, edge, drop))
            require(surface+'_core_edge_temperatures_bounded', valid_temp and ambient-.001 <= edge <= core+.001
                    and near(edge, min(core, max(ambient, core-drop))))
        else:
            require(surface+'_unconfigured_field_metadata', profile.get('core_field_axis') == -1 and near(profile.get('core_field_offset_normalized'), 0.))
    metrics['configured_fields'] = fields
    require('exactly_28_casting_fields', len(fields) == 28)
    require('four_instances_of_seven_casting_roles', len(roles) == 28 and all(roles.count(role) == 4 for role in ROLES))

    images, payloads = {}, {}
    names = {'on': 'imported_fields_on.png', 'off': 'imported_fields_off.png', 'restored': 'imported_fields_restored.png',
             'ids': 'imported_fields_ids.png', 'nir_on': 'imported_fields_nir_on.png', 'nir_off': 'imported_fields_nir_off.png',
             'color': 'imported_fields_color.png', 'monochrome': 'imported_fields_monochrome.png', 'uniform_color': 'imported_fields_uniform_color.png'}
    for key, filename in names.items():
        expected = SCREEN_SIZE if key in ('color', 'monochrome', 'uniform_color') else RAW_SIZE
        try:
            path = directory/filename
            payload = path.read_bytes()
            with Image.open(path) as image:
                if image.format != 'PNG' or image.size != expected or image.mode not in ('L', 'RGB', 'RGBA'):
                    raise ValueError('Expected opaque 8-bit PNG at '+str(expected))
                if image.mode == 'RGBA' and image.getchannel('A').getextrema() != (255, 255):
                    raise ValueError('Transparent capture is invalid')
                rgb = image.convert('RGB')
            images[key], payloads[key] = rgb, payload
            result['files'][key] = {'name': filename, 'size': expected, 'bytes': len(payload), 'sha256': hashlib.sha256(payload).hexdigest()}
            require(key+'_image_valid', True)
            if expected == RAW_SIZE:
                require(key+'_image_grayscale', rgb_spread(rgb) == 0)
        except (OSError, ValueError) as exc:
            require(key+'_image_valid', False, exc)
    require('nir_png_bytes_identical', 'nir_on' in payloads and payloads.get('nir_on') == payloads.get('nir_off'))
    require('restored_raw_thermal_bytes_identical', 'on' in payloads and payloads.get('on') == payloads.get('restored'))
    require('field_toggle_changes_raw_thermal', 'on' in payloads and 'off' in payloads and payloads['on'] != payloads['off'])
    eligible, varying = [], []
    if all(key in images for key in ('on', 'off', 'ids')):
        mask_ids = images['ids'].convert('L')
        on, off = images['on'].convert('L'), images['off'].convert('L')
        for surface, profile in on_profiles.items():
            stencil = profile.get('stencil_id')
            entry = metrics['profiles'][surface]
            if not finite(stencil) or int(stencil) != stencil or not 1 <= stencil <= 255:
                continue
            mask = mask_ids.point([255 if i == int(stencil) else 0 for i in range(256)]).filter(ImageFilter.MinFilter(3))
            count = mask.histogram()[255]
            entry['eroded_visible_pixels'] = count
            if count < 12:
                entry['render_eligibility'] = 'Fewer than12 pixels after one-pixel erosion; absent, small or occluded'
                continue
            entry['render_eligibility'] = 'Eligible exact-ID surface'
            a, b = histogram_metrics(on, mask), histogram_metrics(off, mask)
            entry['on'], entry['off'] = a, b
            apparent = profile.get('uniform_apparent_temperature_c')
            expected = min(255., max(0., (apparent-20)*255/100)) if finite(apparent) else None
            entry['expected_uniform_mean_u8'] = expected
            require(surface+'_uniform_reference_flat', b['span_u8'] <= 1)
            require(surface+'_uniform_reference_matches_profile', expected is not None and abs(b['mean_u8']-expected) <= 1.)
            if entry['configured']:
                span = a['p90_u8']-a['p10_u8']
                mean_drop = b['mean_u8']-a['mean_u8']
                passed = span >= 3 and mean_drop >= 2.
                entry.update(p90_minus_p10_u8=span, mean_drop_from_uniform_u8=mean_drop, passes_variation=passed)
                if entry['authored_core_edge_drop_c'] >= 8.:
                    eligible.append(surface)
                    if passed:
                        varying.append(surface)
            else:
                difference = histogram_metrics(ImageChops.difference(on, off), mask)
                entry['flat_profile_max_change_u8'] = difference['max_u8']
                require(surface+'_unconfigured_pixels_stay_flat', difference['max_u8'] <= 1)
    fraction = len(varying)/len(eligible) if eligible else 0.
    metrics['eligible_high_drop_fields'] = eligible
    metrics['high_drop_fields_passing_variation'] = varying
    metrics['high_drop_pass_fraction'] = fraction
    require('at_least_eight_eligible_high_drop_fields', len(eligible) >= 8)
    require('at_least_eighty_percent_high_drop_fields_vary', bool(eligible) and fraction >= .8)
    # Central-left scene area excludes top labels, bottom controls and right HUD.
    scene_box = (round(SCREEN_SIZE[0]*.02), round(SCREEN_SIZE[1]*.15), round(SCREEN_SIZE[0]*.62), round(SCREEN_SIZE[1]*.79))
    metrics['observer_palette_scene_crop_px'] = scene_box
    if 'monochrome' in images:
        value = rgb_spread(images['monochrome'].crop(scene_box))
        metrics['monochrome_scene_rgb_spread_u8'] = value
        require('monochrome_observer_neutral', value <= 1)
    for key in ('color', 'uniform_color'):
        if key in images:
            value = rgb_spread(images[key].crop(scene_box))
            metrics[key+'_scene_rgb_spread_u8'] = value
            require(key+'_observer_has_iron_color', value > 2)
    if 'color' in images and 'uniform_color' in images:
        difference = ImageChops.difference(images['color'].crop(scene_box), images['uniform_color'].crop(scene_box))
        value = sum(ImageStat.Stat(difference).mean)/3
        metrics['observer_field_vs_uniform_mean_abs_rgb_difference_u8'] = value
        require('observer_shows_field_toggle', value > 0)
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
    args.output.write_text(json.dumps(result, indent=2, allow_nan=False)+'\n', encoding='utf-8')
    print(json.dumps({'passed': result['passed'], 'failed_checks': result['failed_checks'], 'errors': result['errors'], 'report': str(args.output.resolve())}, indent=2))
    return 0 if result['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
