#!/usr/bin/env python3
"""Verify rendered welding-fragment phases and isolated smoke visibility/fading.

Pillow only; never starts Unreal or a model. Frozen on/off differences isolate
smoke from fragment aging. This is authored cinematic visualization QA, not a
resistance-welding process or fume-transport calibration.
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
    raise SystemExit('Pillow is required; use the environment documented in docs/thermal_qa.md.')

RAW_SIZE = (448, 256)
SCREEN_SIZE = (1600, 900)
SCENE_CROP = (32, 135, 992, 711)
THRESHOLDS = {
    'minimum_smoke_mean_absolute_difference_u8': .10,
    'smoke_changed_pixel_min_u8': 3,
    'minimum_smoke_changed_fraction': .001,
    'maximum_late_to_early_smoke_difference_ratio': .5,
    'raw_max_rgb_channel_spread_u8': 0,
}


def finite(value):
    return type(value) in (int, float) and math.isfinite(value)


def near(value, expected, tolerance=.001):
    return finite(value) and finite(expected) and abs(value-expected) <= tolerance


def obj(value):
    return value if isinstance(value, dict) else {}


def vector(value):
    return isinstance(value, list) and len(value) == 3 and all(finite(v) for v in value)


def identified(value, key='name'):
    if not isinstance(value, list):
        return {}
    result = {}
    for row in value:
        row = obj(row)
        identity = row.get(key)
        if not isinstance(identity, str) or not identity or identity in result:
            return {}
        result[identity] = row
    return result


def rgb_spread(image):
    r, g, b = image.split()
    return ImageChops.subtract(ImageChops.lighter(r, ImageChops.lighter(g, b)),
                               ImageChops.darker(r, ImageChops.darker(g, b))).getextrema()[1]


def difference_stats(first, second):
    diff = ImageChops.difference(first, second)
    if diff.mode == 'RGB':
        r, g, b = diff.split()
        maximum = ImageChops.lighter(r, ImageChops.lighter(g, b))
        mean = sum(ImageStat.Stat(diff).mean)/3
    else:
        maximum = diff
        mean = ImageStat.Stat(diff).mean[0]
    histogram = maximum.histogram()
    return {'mean_absolute_difference_u8': mean,
            'changed_fraction_at_least_3_u8': sum(histogram[3:])/(maximum.width*maximum.height),
            'maximum_channel_difference_u8': maximum.getextrema()[1]}


def reject_constant(value):
    raise ValueError('Nonfinite JSON constant: '+value)


class CaptureReader:
    def __init__(self, directory, report):
        self.directory = directory
        self.report = report
        self.images = {}
        self.payloads = {}

    def require(self, name, passed, detail=None):
        self.report['checks'][name] = bool(passed)
        if not passed and detail:
            self.report['errors'].append(name+': '+str(detail))

    def load(self, key, filename, raw=False):
        size = RAW_SIZE if raw else SCREEN_SIZE
        try:
            if not isinstance(filename, str) or not filename or Path(filename).name != filename or '\\' in filename:
                raise ValueError('Expected a filename inside the capture directory')
            path = self.directory/filename
            payload = path.read_bytes()
            with Image.open(path) as image:
                if image.format != 'PNG' or image.size != size or image.mode not in ('L', 'RGB', 'RGBA'):
                    raise ValueError('Expected 8-bit PNG at '+str(size))
                if image.mode == 'RGBA' and image.getchannel('A').getextrema() != (255, 255):
                    raise ValueError('Capture must be opaque')
                rgb = image.convert('RGB')
            self.images[key], self.payloads[key] = rgb, payload
            self.report['files'][key] = {'name': filename, 'size': size, 'bytes': len(payload), 'sha256': hashlib.sha256(payload).hexdigest()}
            self.require(key+'_valid_png', True)
            if raw:
                self.require(key+'_raw_grayscale', rgb_spread(rgb) == 0)
        except (OSError, ValueError) as exc:
            self.require(key+'_valid_png', False, exc)


PHASES = ('burst', 'fountain', 'fallout', 'settled')
REPLAY_TIMES = (4.56, 4.80, 5.22, 5.32)
TAIL_TIMES = (0., 0., 0., 3.48)
PAIR_NAMES = ('early', 'late', 'expired')
PHASE_IMAGES = ('visible', 'nir', 'thermal', 'raw_nir', 'raw_thermal')
PAIR_IMAGES = ('visible_on', 'visible_off', 'nir_on', 'nir_off', 'raw_nir_on', 'raw_nir_off', 'raw_thermal_on', 'raw_thermal_off')
THRESHOLDS.update({
    'spark_capacity': 4096, 'spark_physics_hz': 90, 'spark_maximum_spawn_per_step': 268,
    'smoke_capacity': 128, 'phase_changed_fraction_min': .001,
    'phase_mean_absolute_difference_min_u8': .10,
    'minimum_burst_to_fountain_mean_initial_speed_ratio': 1.3,
    'minimum_fountain_to_fallout_mean_initial_speed_ratio': 1.3,
})


def verify(directory):
    directory = directory.resolve()
    result = {'scope': 'Actual frozen Unreal welding-fragment phases and smoke on/off/fade pairs; authored cinematic proxies, not process calibration.',
              'directory': str(directory), 'thresholds': THRESHOLDS, 'checks': {},
              'metrics': {'phases': {}, 'smoke_pairs': {}}, 'files': {}, 'errors': []}
    reader = CaptureReader(directory, result)
    require, metrics = reader.require, result['metrics']
    try:
        data = json.loads((directory/'welding_effects.json').read_text(encoding='utf-8-sig'), parse_constant=reject_constant)
        if not isinstance(data, dict):
            raise ValueError('Expected JSON object')
        require('runtime_json_valid', True)
    except (OSError, ValueError) as exc:
        data = {}
        require('runtime_json_valid', False, exc)
    for flag in ('passed', 'pause_freezes_effects', 'phase_geometry_fixed_per_capture', 'smoke_toggle_preserves_state', 'smoke_thermal_unchanged', 'expired_smoke_empty'):
        require('runtime_'+flag, data.get(flag) is True)
    phases = identified(data.get('phases'))
    pairs = identified(data.get('smoke_pairs'))
    require('four_exact_effect_phases', set(phases) == set(PHASES))
    require('three_exact_smoke_pairs', set(pairs) == set(PAIR_NAMES))

    def spark_checks(name, spark):
        require(name+'_spark_model', spark.get('model') == 'bounded-swept-world-particles-v3' and spark.get('profile') == 'cinematic-expulsion-v1')
        require(name+'_spark_capacity_and_camera_independent_path', spark.get('capacity') == 4096 and spark.get('thermal_bins') == 12 and spark.get('cpu_image_readbacks') == 0 and spark.get('emitter_count') == 4)
        require(name+'_spark_finite_particle_state', spark.get('finite_particle_state') is True and isinstance(spark.get('particle_state_signature'), str) and bool(spark['particle_state_signature']))
        count_keys = ('alive', 'airborne', 'spawned', 'peak_alive', 'peak_airborne', 'collisions', 'rest_events', 'capacity_drops', 'spawn_attempts')
        valid = all(finite(spark.get(k)) and int(spark[k]) == spark[k] and spark[k] >= 0 for k in count_keys)
        require(name+'_spark_counts_bounded', valid and spark['airborne'] <= spark['alive'] <= spark['peak_alive'] <= 4096
                and spark['airborne'] <= spark['peak_airborne'] <= 4096 and spark['capacity_drops'] == 0 and spark['spawn_attempts'] == spark['spawned'])
        rates = {'maximum_continuous_emission_rate_per_emitter_per_second': 240., 'fountain_rate_per_emitter_per_second': 240.,
                 'fallout_rate_per_emitter_per_second': 100., 'burst_particles_per_emitter_event': 64., 'pulse_period_seconds': 1.05,
                 'pulse_stagger_seconds': .11, 'burst_duration_seconds': .10, 'fountain_end_seconds': .42, 'fallout_end_seconds': .82}
        require(name+'_spark_phase_schedule', all(near(spark.get(k), v) for k, v in rates.items()))
        budget = obj(spark.get('physical_budget'))
        require(name+'_spark_physics_budget', budget.get('fixed_step_hz') == 90 and budget.get('maximum_sweeps_per_step') == 4096
                and budget.get('maximum_sweeps_per_simulated_second') == 4096*90
                and budget.get('maximum_spawn_per_step') == 268
                and finite(budget.get('peak_spawn_attempts_per_step')) and 0 <= budget['peak_spawn_attempts_per_step'] <= 268
                and finite(budget.get('peak_sweeps_per_step')) and 0 <= budget['peak_sweeps_per_step'] <= 4096)
        require(name+'_spark_render_budget', finite(budget.get('render_instance_slots')) and finite(budget.get('maximum_render_instance_slots'))
                and 0 <= budget['render_instance_slots'] <= budget['maximum_render_instance_slots'] <= 12*4096)
        bounds = obj(spark.get('live_bounds_cm'))
        if valid and spark['alive']:
            require(name+'_spark_live_bounds_finite', vector(bounds.get('minimum')) and vector(bounds.get('maximum'))
                    and all(a <= b for a, b in zip(bounds['minimum'], bounds['maximum'])))
        counts = identified(spark.get('phase_counts'))
        require(name+'_three_particle_birth_modes', set(counts) == set(PHASES[:3]))
        for mode, record in counts.items():
            good = all(finite(record.get(k)) and record[k] >= 0 for k in ('spawned', 'alive', 'resting'))
            require(name+'_'+mode+'_mode_counts_bounded', good and record['resting'] <= record['alive'] <= record['spawned'])
            if good and record['spawned'] > 0:
                values = [record.get(k) for k in ('min_initial_speed_cm_s', 'mean_initial_speed_cm_s', 'max_initial_speed_cm_s')]
                require(name+'_'+mode+'_initial_velocity_recorded', all(finite(v) for v in values) and 0 < values[0] <= values[1] <= values[2]
                        and finite(record.get('mean_initial_upward_speed_cm_s')) and finite(record.get('mean_initial_outward_speed_cm_s'))
                        and record['mean_initial_outward_speed_cm_s'] >= 0)
        return counts

    def smoke_checks(name, smoke):
        require(name+'_smoke_authored_profile', smoke.get('model') == 'shared-pulse-optical-wisps-v2' and near(smoke.get('birth_rate_per_emitter'), 28.)
                and near(smoke.get('material_opacity'), .52) and near(smoke.get('minimum_lifetime_seconds'), 3.) and near(smoke.get('maximum_lifetime_seconds'), 4.4))
        require(name+'_smoke_initialized_and_finite', smoke.get('initialized') is True and smoke.get('finite_particle_state') is True
                and isinstance(smoke.get('particle_state_signature'), str) and bool(smoke['particle_state_signature']))
        count_keys = ('alive_particles', 'peak_alive', 'spawned_particles', 'expired_particles', 'dropped_particles', 'cleared_particles')
        valid = all(finite(smoke.get(k)) and int(smoke[k]) == smoke[k] and smoke[k] >= 0 for k in count_keys)
        require(name+'_smoke_pool_bounded', valid and smoke.get('max_particles') == 128 and smoke['alive_particles'] <= smoke['peak_alive'] <= 128
                and smoke['dropped_particles'] == 0 and smoke['alive_particles']+smoke['expired_particles']+smoke['cleared_particles'] == smoke['spawned_particles'])
        budget = obj(smoke.get('budget'))
        require(name+'_smoke_draw_and_cpu_budget', budget.get('draw_components') == 1 and budget.get('instance_capacity') == 128
                and budget.get('custom_data_floats') == 3 and budget.get('simulation_hz') == 60 and budget.get('collision_queries') == 0 and budget.get('cpu_volume_solver') is False)
        modality = obj(smoke.get('modality'))
        require(name+'_smoke_modality_scope', modality.get('visible') is True and modality.get('nir') is True
                and modality.get('thermal') is False and modality.get('custom_depth') is False and modality.get('translucency_before_dof') is True)
        puffs = identified(smoke.get('puffs'), 'id')
        # Puff IDs may be integer counters, unlike named scene components.
        raw_puffs = smoke.get('puffs', [])
        if isinstance(raw_puffs, list) and all(isinstance(p, dict) and finite(p.get('id')) and int(p['id']) == p['id'] for p in raw_puffs):
            puffs = {str(p['id']): p for p in raw_puffs}
            require(name+'_smoke_unique_puff_ids', len(puffs) == len(raw_puffs))
        require(name+'_smoke_all_live_puffs_recorded', valid and isinstance(raw_puffs, list) and len(puffs) == len(raw_puffs) == smoke['alive_particles'])
        for identity, puff in puffs.items():
            require(name+'_puff_'+identity+'_state_bounded', vector(puff.get('position_cm')) and vector(puff.get('origin_cm'))
                    and all(finite(puff.get(k)) for k in ('age_seconds', 'lifetime_seconds', 'radius_cm', 'opacity', 'seed'))
                    and 0 <= puff['age_seconds'] < puff['lifetime_seconds'] and 0 < puff['radius_cm'] and 0 <= puff['opacity'] <= 1)
        require(name+'_smoke_opacity_matches_puffs', finite(smoke.get('total_opacity')) and near(smoke['total_opacity'], sum(p.get('opacity', 0.) for p in puffs.values()), .001))
        if valid and smoke['alive_particles']:
            require(name+'_smoke_bounds_finite', vector(smoke.get('bounds_min_cm')) and vector(smoke.get('bounds_max_cm')) and finite(smoke.get('mean_z_cm'))
                    and all(a <= b for a, b in zip(smoke['bounds_min_cm'], smoke['bounds_max_cm'])))
        return puffs

    for name, replay_time, tail_time in zip(PHASES, REPLAY_TIMES, TAIL_TIMES):
        phase = obj(phases.get(name))
        pose, sparks, smoke = [obj(phase.get(k)) for k in ('pose', 'sparks', 'smoke')]
        require(name+'_replay_clock', near(phase.get('replay_until_cell_seconds'), replay_time) and near(phase.get('emission_free_age_seconds'), tail_time)
                and near(pose.get('motion_seconds'), replay_time))
        require(name+'_model_and_training_disabled', pose.get('inference_enabled') is False and pose.get('training_enabled') is False)
        mode_counts = spark_checks(name, sparks)
        smoke_checks(name, smoke)
        metrics['phases'][name] = {'sparks': sparks, 'smoke': smoke}
        if name != 'settled':
            emitters = sparks.get('pulse_emitters', [])
            emitters = emitters if isinstance(emitters, list) else []
            leading = [e for e in emitters if isinstance(e, dict) and e.get('emitter_index') == 0]
            require(name+'_leading_emitter_phase', len(leading) == 1 and leading[0].get('phase') == name)
            require(name+'_named_mode_actually_spawned', finite(obj(mode_counts.get(name)).get('spawned')) and mode_counts[name]['spawned'] > 0)
        images = obj(phase.get('images'))
        for family in PHASE_IMAGES:
            reader.load(name+'_'+family, images.get(family), family.startswith('raw_'))
    settled = obj(obj(phases.get('settled')).get('sparks'))
    require('fragments_record_physical_collision_and_rest', finite(settled.get('collisions')) and settled['collisions'] > 0
            and finite(settled.get('rest_events')) and settled['rest_events'] > 0)
    mode_counts = identified(settled.get('phase_counts'))
    means = [obj(mode_counts.get(mode)).get('mean_initial_speed_cm_s') for mode in PHASES[:3]]
    metrics['mean_initial_speed_by_mode_cm_s'] = dict(zip(PHASES[:3], means))
    require('fragment_modes_have_distinct_initial_speed', all(finite(v) and v > 0 for v in means)
            and means[0] >= 1.3*means[1] and means[1] >= 1.3*means[2])
    fountain_up = obj(mode_counts.get('fountain')).get('mean_initial_upward_speed_cm_s')
    fallout_up = obj(mode_counts.get('fallout')).get('mean_initial_upward_speed_cm_s')
    require('fountain_starts_more_upward_than_fallout', finite(fountain_up) and finite(fallout_up) and fountain_up > fallout_up)
    metrics['phase_image_changes'] = {}
    for before, after in zip(PHASES, PHASES[1:]):
        for family in ('visible', 'nir'):
            keys = (before+'_'+family, after+'_'+family)
            if all(k in reader.images for k in keys):
                stats = difference_stats(*(reader.images[k].crop(SCENE_CROP) for k in keys))
                metrics['phase_image_changes'][before+'_to_'+after+'_'+family] = stats
                require(before+'_to_'+after+'_'+family+'_changes', stats['mean_absolute_difference_u8'] >= .10 and stats['changed_fraction_at_least_3_u8'] >= .001)
        for family in ('raw_nir', 'raw_thermal'):
            keys = (before+'_'+family, after+'_'+family)
            require(before+'_to_'+after+'_'+family+'_changes', all(k in reader.payloads for k in keys) and reader.payloads[keys[0]] != reader.payloads[keys[1]])

    for name in PAIR_NAMES:
        pair = obj(pairs.get(name))
        on, off = obj(pair.get('smoke_on')), obj(pair.get('smoke_off'))
        smoke_checks(name+'_on', on)
        smoke_checks(name+'_off', off)
        stable = lambda s: {k: v for k, v in s.items() if k not in ('rendering_enabled', 'visible')}
        require(name+'_smoke_draw_toggle_preserves_complete_state', bool(on) and stable(on) == stable(off)
                and on.get('rendering_enabled') is True and off.get('rendering_enabled') is False)
        if name == 'early':
            require(name+'_smoke_is_live_when_shown', finite(on.get('alive_particles')) and on['alive_particles'] > 0 and on.get('visible') is True and off.get('visible') is False)
        elif name == 'expired':
            require('expired_smoke_pool_empty', on.get('alive_particles') == 0 and near(on.get('total_opacity'), 0.) and off.get('alive_particles') == 0)
        images = obj(pair.get('images'))
        for family in PAIR_IMAGES:
            reader.load(name+'_'+family, images.get(family), family.startswith('raw_'))
        metrics['smoke_pairs'][name] = {'on': on, 'off': off, 'image_difference': {}}
        for family in ('visible', 'nir', 'raw_nir'):
            keys = (name+'_'+family+'_on', name+'_'+family+'_off')
            if all(k in reader.images for k in keys):
                crops = [reader.images[k] if family.startswith('raw_') else reader.images[k].crop(SCENE_CROP) for k in keys]
                stats = difference_stats(*crops)
                metrics['smoke_pairs'][name]['image_difference'][family] = stats
                if name == 'early':
                    require(family+'_smoke_visible_in_frozen_pair', stats['mean_absolute_difference_u8'] >= .10 and stats['changed_fraction_at_least_3_u8'] >= .001)
                elif name == 'expired':
                    require(family+'_expired_smoke_has_no_rendered_residue', stats['maximum_channel_difference_u8'] == 0)
        keys = (name+'_raw_thermal_on', name+'_raw_thermal_off')
        require(name+'_smoke_raw_thermal_byte_parity', all(k in reader.payloads for k in keys) and reader.payloads[keys[0]] == reader.payloads[keys[1]])
    early = obj(obj(pairs.get('early')).get('smoke_on'))
    late = obj(obj(pairs.get('late')).get('smoke_on'))
    expired = obj(obj(pairs.get('expired')).get('smoke_on'))
    require('tail_ages_recorded', near(obj(pairs.get('early')).get('emission_free_age_seconds'), .8)
            and near(obj(pairs.get('late')).get('emission_free_age_seconds'), 3.8)
            and near(obj(pairs.get('expired')).get('emission_free_age_seconds'), 8.8))
    require('tail_has_no_new_puffs', finite(early.get('spawned_particles')) and early['spawned_particles'] > 0
            and early['spawned_particles'] == late.get('spawned_particles') == expired.get('spawned_particles'))
    require('smoke_opacity_falls_during_tail', finite(early.get('total_opacity')) and finite(late.get('total_opacity'))
            and 0 <= late['total_opacity'] < early['total_opacity'])
    metrics['smoke_tail_difference_ratios'] = {}
    for family in ('visible', 'nir', 'raw_nir'):
        before = obj(metrics['smoke_pairs']['early']['image_difference'].get(family)).get('mean_absolute_difference_u8')
        after = obj(metrics['smoke_pairs']['late']['image_difference'].get(family)).get('mean_absolute_difference_u8')
        ratio = after/before if finite(before) and before > 0 and finite(after) else None
        metrics['smoke_tail_difference_ratios'][family] = ratio
        require(family+'_smoke_tail_visibly_fades', finite(ratio) and ratio <= .5)
    # The PNG encoder deliberately repeats grayscale into RGB, so a gray saved
    # file alone cannot establish that the GPU sensor was neutral before encoding.
    source_stats = obj(data.get('nir_raw_statistics'))
    metrics['nir_source_statistics'] = {}
    raw_keys = [name+'_raw_nir' for name in PHASES]
    raw_keys += [name+'_raw_nir_'+toggle for name in PAIR_NAMES for toggle in ('on', 'off')]
    for key in raw_keys:
        filename = obj(result['files'].get(key)).get('name')
        matches = [obj(row) for row in source_stats.values() if isinstance(row, dict) and row.get('image') == filename]
        row = matches[0] if len(matches) == 1 else {}
        metrics['nir_source_statistics'][key] = row
        source_spread = row.get('preconversion_max_rgb_channel_spread_u8')
        require(key+'_gpu_source_neutral_before_encoding', len(matches) == 1 and finite(source_spread)
                and int(source_spread) == source_spread and 0 <= source_spread <= 1
                and row.get('preconversion_neutral_spread_limit_u8') == 1
                and row.get('preconversion_neutral_within_one_code') is True)
        image = reader.images.get(key)
        values = ImageStat.Stat(image.getchannel('R')) if image is not None else None
        require(key+'_recorded_statistics_match_png', values is not None and row.get('width') == 448 and row.get('height') == 256
                and row.get('max_rgb_channel_spread_u8') == 0
                and near(row.get('minimum_u8'), values.extrema[0][0], 0.)
                and near(row.get('maximum_u8'), values.extrema[0][1], 0.)
                and near(row.get('mean_u8'), values.mean[0], 1e-6))
    result['failed_checks'] = [name for name, passed in result['checks'].items() if not passed]
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
