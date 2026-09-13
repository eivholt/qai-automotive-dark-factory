#!/usr/bin/env python3
"""Verify native factory-lighting screenshots, bank states and sensor isolation.

Pillow only; no Unreal/model calls. Brightness is measured in display codes, not lux.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import math
from pathlib import Path

from PIL import Image, ImageChops, ImageStat

RAW_SIZE, SCREEN_SIZE = (448, 256), (1600, 900)
SNAPSHOTS = ('dim', 'startup', 'bright', 'headlamp_requested', 'restored', 'retrigger')
SENSOR_STATES = ('off', 'on', 'startup', 'restored')
THRESHOLDS = {'mean_brightness_increase_u8_min': 8., 'brightened_pixel_increase_u8_min': 12,
              'brightened_pixel_fraction_min': .15, 'bright_clipped_luma_u8': 250,
              'bright_clipped_fraction_max': .08, 'restored_mean_absolute_difference_u8_max': 1.,
              'restored_p99_absolute_difference_u8_max': 3,
              'startup_mean_absolute_difference_u8_min': 1.,
              'bank_count': 6, 'tubes_per_bank': 3, 'startup_duration_seconds': 1.5,
              'sample_time_tolerance_seconds': .0001, 'retrigger_elapsed_seconds_max': .05, 'gain_tolerance': .00001,
              'flicker_down_step_min': .05, 'raw_rgb_spread_max_u8': 0}


def finite(value):
    return type(value) in (int, float) and math.isfinite(value)


def near(a, b, tolerance=.00001):
    return finite(a) and finite(b) and abs(a-b) <= tolerance


def obj(value):
    return value if isinstance(value, dict) else {}


def vector(value):
    return isinstance(value, list) and len(value) == 3 and all(finite(v) for v in value)


def gray_spread(image):
    r, g, b = image.split()
    return ImageChops.subtract(ImageChops.lighter(r, ImageChops.lighter(g, b)),
                               ImageChops.darker(r, ImageChops.darker(g, b))).getextrema()[1]


def percentile(image, fraction):
    histogram = image.histogram()
    target, total = max(1, math.ceil(sum(histogram)*fraction)), 0
    for i, count in enumerate(histogram):
        total += count
        if total >= target:
            return i
    return 255


def verify(directory):
    directory = directory.resolve()
    result = {'scope': 'Observer fluorescent-style startup and visible illumination; exact fixed-sensor isolation. Authored lighting, not photometric calibration.',
              'directory': str(directory), 'thresholds': THRESHOLDS, 'checks': {},
              'metrics': {'snapshots': {}, 'startup': {}}, 'files': {}, 'errors': []}
    checks, metrics = result['checks'], result['metrics']
    def require(name, passed, detail=None):
        checks[name] = bool(passed)
        if not passed and detail:
            result['errors'].append(name+': '+str(detail))
    try:
        def reject(value):
            raise ValueError('Nonfinite JSON constant: '+value)
        runtime = json.loads((directory/'factory_lighting.json').read_text(encoding='utf-8-sig'), parse_constant=reject)
        if not isinstance(runtime, dict):
            raise ValueError('Runtime report must be an object')
        require('runtime_json_valid', True)
    except (OSError, ValueError) as exc:
        runtime = {}
        require('runtime_json_valid', False, exc)
    for flag in ('passed', 'geometry_unchanged', 'nir_unchanged', 'thermal_unchanged'):
        require('runtime_'+flag, runtime.get(flag) is True)
    require('observer_cycle_four_modes', runtime.get('cycle') == [0, 1, 2, 3, 0])
    snapshots = obj(runtime.get('snapshots'))
    require('six_named_snapshots', all(isinstance(snapshots.get(name), dict) for name in SNAPSHOTS))
    expected_modes = {'dim': 0, 'startup': 3, 'bright': 3, 'headlamp_requested': 3, 'restored': 0, 'retrigger': 3}
    isolation_expected = {'view_channel0': True, 'view_channel1': False, 'view_channel2': False,
                          'cast_shadows': False, 'indirect_lighting_intensity': 0., 'volumetric_scattering_intensity': 0.,
                          'affect_global_illumination': False, 'affect_reflection': False, 'affect_translucent_lighting': False,
                          'geometry_hidden_in_scene_capture': True, 'geometry_custom_depth': False, 'geometry_collision': False,
                          'geometry_cast_shadows': False, 'geometry_affect_indirect': False, 'geometry_visible_in_reflections': False}
    baseline_banks = None

    def light_state(name, light):
        nonlocal baseline_banks
        light = obj(light)
        require(name+'_initialized', light.get('initialized') is True)
        require(name+'_enabled_flag_valid', type(light.get('enabled')) is bool)
        require(name+'_imported_scene', light.get('imported_scene') is True)
        require(name+'_fixture_counts', light.get('bank_count') == 6 and light.get('tube_count') == 18 and light.get('light_count') == 6 and light.get('mesh_component_count') == 12)
        require(name+'_startup_duration', near(light.get('startup_duration_seconds'), 1.5, .0001))
        elapsed, activation = light.get('startup_elapsed_seconds'), light.get('activation_count')
        require(name+'_time_and_activation_valid', finite(elapsed) and elapsed >= 0 and finite(activation) and int(activation) == activation and activation >= 0)
        isolation = obj(light.get('isolation'))
        require(name+'_light_and_geometry_isolation', all((isolation.get(key) is value if type(value) is bool else near(isolation.get(key), value)) for key, value in isolation_expected.items()))
        rows = light.get('banks', [])
        rows = rows if isinstance(rows, list) else []
        banks = {int(b['index']): b for b in rows if isinstance(b, dict) and finite(b.get('index')) and int(b['index']) == b['index']}
        require(name+'_six_unique_banks', len(rows) == len(banks) == 6 and set(banks) == set(range(6)))
        if baseline_banks is None and len(banks) == 6:
            baseline_banks = banks
        for index, bank in banks.items():
            prefix = name+'_bank'+str(index)
            gain = bank.get('gain')
            nominal, intensity = bank.get('nominal_lumens'), bank.get('intensity_lumens')
            tube, nominal_tube = bank.get('tube_emissive_strength'), bank.get('nominal_tube_emissive_strength')
            require(prefix+'_gain_bounded', finite(gain) and 0 <= gain <= 1)
            require(prefix+'_positive_nominal_outputs', finite(nominal) and nominal > 0 and finite(nominal_tube) and nominal_tube > 0)
            require(prefix+'_light_output_matches_gain', finite(gain) and finite(nominal) and near(intensity, nominal*gain, .02))
            require(prefix+'_tube_output_matches_gain', finite(gain) and finite(nominal_tube) and near(tube, nominal_tube*gain, .002))
            require(prefix+'_geometry_and_delay_valid', vector(bank.get('position_cm')) and vector(bank.get('light_position_cm'))
                    and finite(bank.get('start_delay_seconds')) and 0 <= bank['start_delay_seconds'] < 1.5
                    and bank.get('tube_count') == 3 and type(bank.get('fixture_visible')) is bool and type(bank.get('light_visible')) is bool)
            if baseline_banks is not None:
                first = baseline_banks.get(index, {})
                require(prefix+'_authored_properties_fixed', all(bank.get(key) == first.get(key) for key in ('position_cm', 'light_position_cm', 'start_delay_seconds', 'nominal_lumens', 'nominal_tube_emissive_strength', 'tube_count')))
            if light.get('enabled') is False:
                require(prefix+'_off_gain_zero', near(gain, 0.) and bank.get('light_visible') is False)
            elif finite(elapsed) and elapsed >= 1.5:
                require(prefix+'_steady_gain_one', near(gain, 1.) and bank.get('light_visible') is True)
        return banks

    bank_snapshots = {}
    for name in SNAPSHOTS:
        snapshot = obj(snapshots.get(name))
        requested, emitting = snapshot.get('headlamp_requested'), snapshot.get('headlamp_emitting')
        mode = snapshot.get('observer_mode')
        require(name+'_observer_mode', mode == expected_modes[name])
        require(name+'_headlamp_flags_valid', type(requested) is bool and type(emitting) is bool)
        require(name+'_headlamp_mode_gating', type(requested) is bool and emitting is (requested and mode == 0))
        light = obj(snapshot.get('lights'))
        require(name+'_factory_mode_gating', light.get('enabled') is (mode == 3))
        banks = light_state(name, light)
        bank_snapshots[name] = banks
        metrics['snapshots'][name] = {'observer_mode': mode, 'headlamp_requested': requested, 'headlamp_emitting': emitting,
                                      'startup_elapsed_seconds': light.get('startup_elapsed_seconds'), 'activation_count': light.get('activation_count'),
                                      'bank_gains': {str(i): b.get('gain') for i, b in banks.items()}}
    dim, restored, requested = (obj(snapshots.get(name)) for name in ('dim', 'restored', 'headlamp_requested'))
    require('mode3_suppresses_explicit_headlamp_request', requested.get('headlamp_requested') is True and requested.get('headlamp_emitting') is False)
    require('dim_headlamp_preference_restored', dim.get('headlamp_requested') is True and restored.get('headlamp_requested') is True
            and dim.get('headlamp_emitting') is True and restored.get('headlamp_emitting') is True)
    startup = obj(obj(snapshots.get('startup')).get('lights'))
    bright = obj(obj(snapshots.get('bright')).get('lights'))
    retrigger = obj(obj(snapshots.get('retrigger')).get('lights'))
    require('partial_startup_time', near(startup.get('startup_elapsed_seconds'), .35, .0001))
    require('bright_is_settled', finite(bright.get('startup_elapsed_seconds')) and bright['startup_elapsed_seconds'] >= 1.5)
    require('reentry_restarts_startup', finite(retrigger.get('startup_elapsed_seconds')) and 0 <= retrigger['startup_elapsed_seconds'] <= .05
            and retrigger.get('startup_time_pinned') is False
            and finite(retrigger.get('activation_count')) and finite(bright.get('activation_count'))
            and retrigger['activation_count'] > bright['activation_count']
            and len(bank_snapshots['retrigger']) == 6 and all(near(b.get('gain'), 0.) for b in bank_snapshots['retrigger'].values()))
    if 'startup_time_override_seconds' in retrigger:
        require('reentry_uses_live_startup_clock', finite(retrigger['startup_time_override_seconds']) and retrigger['startup_time_override_seconds'] < 0)
    samples = runtime.get('startup_samples', [])
    samples = samples if isinstance(samples, list) else []
    require('dense_startup_sample_times', len(samples) == 41 and all(near(obj(sample).get('startup_elapsed_seconds'), i*.05, .0001) for i, sample in enumerate(samples)))
    sample_banks = []
    for i, sample in enumerate(samples):
        require('sample'+str(i)+'_time_pinned_and_enabled', obj(sample).get('startup_time_pinned') is True and obj(sample).get('enabled') is True)
        sample_banks.append(light_state('sample'+str(i), sample))
    variation, flickering = [], []
    for index in range(6):
        gains = [banks.get(index, {}).get('gain') for banks in sample_banks]
        valid = bool(gains) and all(finite(gain) for gain in gains)
        down = [a-b for a, b in zip(gains, gains[1:]) if finite(a) and finite(b)]
        changing = valid and min(gains) == 0 and max(gains) == 1 and any(.01 < gain < .99 for gain in gains)
        metrics['startup'][str(index)] = {'gains': gains, 'has_partial_output': changing, 'maximum_down_step': max(down, default=0)}
        if changing:
            variation.append(index)
        if max(down, default=0) > .05:
            flickering.append(index)
    require('all_banks_start_and_settle', len(variation) == 6)
    require('startup_contains_actual_flicker', bool(flickering))
    metrics['flickering_banks'] = flickering
    stagger = any(len({round(b.get('gain'), 5) for b in banks.values() if finite(b.get('gain'))}) >= 2 for banks in sample_banks)
    require('startup_bank_outputs_are_staggered', stagger)
    delays = [b.get('start_delay_seconds') for b in (baseline_banks or {}).values()]
    require('authored_start_delays_are_staggered', len(delays) == 6 and all(finite(d) for d in delays) and len(set(delays)) >= 2)

    images, payloads = {}, {}
    filenames = {name: 'factory_'+name+'.png' for name in ('dim', 'bright', 'startup', 'off')}
    if (directory/'factory_fixtures.png').is_file():
        filenames['fixtures'] = 'factory_fixtures.png'
    for sensor in ('nir', 'thermal'):
        for state in SENSOR_STATES:
            filenames[sensor+'_'+state] = 'factory_'+sensor+'_'+state+'.png'
    for key, filename in filenames.items():
        size = SCREEN_SIZE if key in ('dim', 'bright', 'startup', 'off', 'fixtures') else RAW_SIZE
        try:
            path = directory/filename
            payload = path.read_bytes()
            with Image.open(path) as image:
                if image.format != 'PNG' or image.size != size or image.mode not in ('L', 'RGB', 'RGBA'):
                    raise ValueError('Expected 8-bit PNG at '+str(size))
                if image.mode == 'RGBA' and image.getchannel('A').getextrema() != (255, 255):
                    raise ValueError('Capture must be opaque')
                rgb = image.convert('RGB')
            images[key], payloads[key] = rgb, payload
            result['files'][key] = {'name': filename, 'size': size, 'bytes': len(payload), 'sha256': hashlib.sha256(payload).hexdigest()}
            require(key+'_image_valid', True)
            if size == RAW_SIZE:
                require(key+'_raw_grayscale', gray_spread(rgb) == 0)
        except (OSError, ValueError) as exc:
            require(key+'_image_valid', False, exc)
    for sensor in ('nir', 'thermal'):
        keys = [sensor+'_'+state for state in SENSOR_STATES]
        require(sensor+'_all_light_states_byte_identical', all(key in payloads for key in keys)
                and all(payloads.get(key) == payloads.get(keys[0]) for key in keys))
    crop = (32, 135, 992, 711)
    metrics['scene_crop_excluding_hud_px'] = crop
    if all(key in images for key in ('dim', 'bright', 'startup', 'off')):
        scenes = {key: images[key].crop(crop).convert('L') for key in ('dim', 'bright', 'startup', 'off')}
        means = {key: ImageStat.Stat(image).mean[0] for key, image in scenes.items()}
        increase = means['bright']-means['dim']
        positive_difference = ImageChops.subtract(scenes['bright'], scenes['dim']).histogram()
        count = scenes['bright'].width*scenes['bright'].height
        fraction = sum(positive_difference[12:])/count
        clipping = sum(scenes['bright'].histogram()[250:])/count
        metrics['scene_mean_luma_u8'] = means
        metrics['bright_mean_increase_u8'] = increase
        metrics['fraction_brightened_at_least_12_u8'] = fraction
        metrics['bright_clipped_fraction'] = clipping
        require('scene_mean_brightness_increases', increase >= 8.)
        require('scene_broadly_brightens', fraction >= .15)
        require('bright_scene_not_overexposed', clipping <= .08)
        difference = ImageChops.difference(scenes['dim'], scenes['off'])
        restored_mean, restored_p99 = ImageStat.Stat(difference).mean[0], percentile(difference, .99)
        metrics['restored_mean_absolute_difference_u8'] = restored_mean
        metrics['restored_p99_absolute_difference_u8'] = restored_p99
        require('dim_scene_restores', restored_mean <= 1. and restored_p99 <= 3)
        for other in ('dim', 'bright'):
            value = ImageStat.Stat(ImageChops.difference(scenes['startup'], scenes[other])).mean[0]
            metrics['startup_vs_'+other+'_mean_absolute_difference_u8'] = value
            require('partial_startup_render_differs_from_'+other, value >= 1.)
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
