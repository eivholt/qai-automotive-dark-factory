#!/usr/bin/env python3
"""Verify frozen welding-tool cooling captures using Pillow, without Unreal/model calls.

Visible/NIR incandescence is illustrative, not spectral calibration. Thermal
captures are the declared apparent-temperature proxy. Measurements use fixed
projected tool regions; raw measurements also require the tool's exact stencil ID.
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
    raise SystemExit('Pillow is required; use the existing environment in docs/thermal_qa.md.')

STAGES = ('hot', 'warm', 'cool')
HEAT_TIMES = (12.0, 15.0, 21.0)
FAMILIES = ('visible', 'nir', 'thermal', 'raw_nir', 'raw_thermal')
RAW_SIZE, SCREEN_SIZE = (448, 256), (1600, 900)
THRESHOLDS = {
    'tool_count': 4, 'mean_luma_drop_each_transition_u8_exclusive_min': .05,
    'scalar_drop_each_transition_exclusive_min': 0.0,
    'profile_temperature_tolerance_c': .001, 'position_tolerance_cm': .01,
    'heat_radius_cm': 26.0, 'radius_tolerance_cm': .001,
    'heat_time_tolerance_seconds': .0001, 'minimum_tools_showing_cooling_per_image_family': 1,
    'minimum_selected_sensor_pixels': 1, 'stencil_quantization_tolerance_u8': 0,
    'raw_max_rgb_spread_u8': 0,
}


def finite(value):
    return type(value) in (int, float) and math.isfinite(value)


def obj(value):
    return value if isinstance(value, dict) else {}


def near(a, b, tolerance):
    return finite(a) and finite(b) and abs(a - b) <= tolerance


def vector(value):
    return isinstance(value, list) and len(value) == 3 and all(finite(v) for v in value)


def vector_near(a, b):
    return vector(a) and vector(b) and max(abs(x-y) for x, y in zip(a, b)) <= THRESHOLDS['position_tolerance_cm']


def box(value, size):
    if not isinstance(value, list) or len(value) != 4 or any(type(v) is not int for v in value):
        return None
    x0, y0, x1, y1 = value
    return tuple(value) if 0 <= x0 < x1 <= size[0] and 0 <= y0 < y1 <= size[1] else None


def grayscale_spread(image):
    r, g, b = image.split()
    return ImageChops.subtract(ImageChops.lighter(r, ImageChops.lighter(g, b)),
                               ImageChops.darker(r, ImageChops.darker(g, b))).getextrema()[1]


def drop_metrics(means):
    valid = len(means) == 3 and all(finite(value) for value in means)
    drops = [means[0]-means[1], means[1]-means[2]] if valid else []
    return {'mean_luma_u8': dict(zip(STAGES, means)), 'transition_drops_u8': drops,
            'shows_cooling': valid and all(value > THRESHOLDS['mean_luma_drop_each_transition_u8_exclusive_min'] for value in drops)}


def verify(directory):
    directory = directory.resolve()
    result = {'scope': 'Frozen-geometry cooling QA for illustrative visible/NIR glow and apparent-temperature rendering.',
              'directory': str(directory), 'thresholds': THRESHOLDS,
              'checks': {}, 'metrics': {'tools': {}, 'image_families': {}}, 'files': {}, 'errors': []}
    checks, metrics, errors = result['checks'], result['metrics'], result['errors']
    def require(name, condition, detail=None):
        checks[name] = bool(condition)
        if not condition and detail:
            errors.append(f'{name}: {detail}')
    def reject_constant(value):
        raise ValueError(f'Nonfinite JSON value {value}')
    try:
        runtime = json.loads((directory/'tool_cooling.json').read_text(encoding='utf-8-sig'), parse_constant=reject_constant)
        if not isinstance(runtime, dict):
            raise ValueError('tool_cooling.json must be an object')
        require('runtime_json_valid', True)
    except (OSError, ValueError) as exc:
        runtime = {}
        require('runtime_json_valid', False, str(exc))
    for flag in ('passed', 'geometry_unchanged', 'pause_freezes_tool_heat'):
        require('runtime_' + flag, runtime.get(flag) is True)
    raw_stages = runtime.get('stages', [])
    if not isinstance(raw_stages, list):
        raw_stages = []
    stage_map = {stage.get('name'): stage for stage in raw_stages if isinstance(stage, dict) and isinstance(stage.get('name'), str)}
    require('three_named_stages', len(raw_stages) == 3 and set(stage_map) == set(STAGES))
    stages = [obj(stage_map.get(name)) for name in STAGES]
    tools_by_stage = []
    for name, time, stage in zip(STAGES, HEAT_TIMES, stages):
        require(name + '_heat_time', near(stage.get('heat_time_seconds'), time, THRESHOLDS['heat_time_tolerance_seconds']))
        require(name + '_cycle_boundary_continuous', stage.get('cycle_boundary_continuous') is True)
        values = stage.get('tools', [])
        if not isinstance(values, list):
            values = []
        tools = {tool.get('id'): tool for tool in values if isinstance(tool, dict) and isinstance(tool.get('id'), str) and tool['id']}
        require(name + '_four_unique_tools', len(values) == len(tools) == THRESHOLDS['tool_count'])
        tools_by_stage.append(tools)
    ids = sorted(tools_by_stage[0])
    require('tool_ids_stable', bool(ids) and all(set(tools) == set(ids) for tools in tools_by_stage))
    snapshots = [obj(stage.get('thermal_snapshot')) for stage in stages]
    encoding_fields = ('width', 'height', 'min_temperature_c', 'max_temperature_c', 'quantity', 'linear_u8', 'band_um', 'ambient_temperature_c')
    encodings = [{key: snapshot.get(key) for key in encoding_fields} for snapshot in snapshots]
    metrics['thermal_encoding_by_stage'] = dict(zip(STAGES, encodings))
    require('thermal_encoding_constant', encodings[0] == encodings[1] == encodings[2])
    require('thermal_encoding_fixed_raw', all(e['width'] == 448 and e['height'] == 256 and e['min_temperature_c'] == 20
            and e['max_temperature_c'] == 120 and e['quantity'] == 'apparent_temperature' and e['linear_u8'] is True for e in encodings))
    require('hotspot_lut_rows_22', all(snapshot.get('lut_rows') == 22 for snapshot in snapshots))

    for identifier in ids:
        tools = [obj(stage.get(identifier)) for stage in tools_by_stage]
        metric = {'scalars': {field: [tool.get(field) for tool in tools] for field in ('body_temperature_c', 'tip_temperature_c', 'glow_strength')},
                  'surface': tools[0].get('surface'), 'stencil_id': tools[0].get('stencil_id')}
        metrics['tools'][identifier] = metric
        for field, values in metric['scalars'].items():
            require(identifier + '_' + field + '_strictly_cools', all(finite(v) for v in values) and values[0] > values[1] > values[2])
        stencil = tools[0].get('stencil_id')
        require(identifier + '_stable_surface_and_stencil', isinstance(tools[0].get('surface'), str) and bool(tools[0]['surface'])
                and finite(stencil) and int(stencil) == stencil and 1 <= stencil <= 255
                and all(tool.get('surface') == tools[0]['surface'] and tool.get('stencil_id') == stencil for tool in tools))
        for field in ('tip_world_cm', 'tip_local_cm'):
            require(identifier + '_' + field + '_unchanged', all(vector_near(tools[0].get(field), tool.get(field)) for tool in tools))
        require(identifier + '_radius_26cm', all(near(tool.get('heat_radius_cm'), 26.0, THRESHOLDS['radius_tolerance_cm']) for tool in tools))
        for name, tool, snapshot in zip(STAGES, tools, snapshots):
            profiles = snapshot.get('profiles', [])
            profiles = profiles if isinstance(profiles, list) else []
            matches = [profile for profile in profiles if isinstance(profile, dict) and profile.get('surface') == tool.get('surface')]
            profile = matches[0] if len(matches) == 1 else {}
            require(name + '_' + identifier + '_thermal_profile_matches', len(matches) == 1 and profile.get('tool_hotspot_enabled') is True
                    and profile.get('stencil_id') == tool.get('stencil_id')
                    and near(profile.get('source_temperature_c'), tool.get('body_temperature_c'), THRESHOLDS['profile_temperature_tolerance_c'])
                    and near(profile.get('tool_tip_surface_temperature_c'), tool.get('tip_temperature_c'), THRESHOLDS['profile_temperature_tolerance_c'])
                    and near(profile.get('tool_hotspot_radius_world_cm'), tool.get('heat_radius_cm'), THRESHOLDS['radius_tolerance_cm'])
                    and vector_near(profile.get('tool_hotspot_world_cm'), tool.get('tip_world_cm'))
                    and vector_near(profile.get('tool_hotspot_local_cm'), tool.get('tip_local_cm')))

    def load(filename, expected_size, key):
        try:
            if not isinstance(filename, str) or not filename:
                raise ValueError('Missing image filename')
            path = (directory/filename).resolve()
            if not path.is_relative_to(directory):
                raise ValueError('Image must be within capture directory')
            payload = path.read_bytes()
            with Image.open(path) as image:
                if image.format != 'PNG' or image.size != expected_size or image.mode not in ('RGB', 'RGBA', 'L'):
                    raise ValueError(f'Expected 8-bit PNG {expected_size}, got {image.format} {image.size} {image.mode}')
                rgb = image.convert('RGB')
            result['files'][key] = {'name': filename, 'size': expected_size, 'bytes': len(payload), 'sha256': hashlib.sha256(payload).hexdigest()}
            require(key + '_valid_image', True)
            return rgb
        except (OSError, ValueError) as exc:
            require(key + '_valid_image', False, str(exc))
            return None

    mask = load(runtime.get('surface_id_mask'), RAW_SIZE, 'surface_id_mask')
    require('surface_id_mask_grayscale', mask is not None and grayscale_spread(mask) == 0)
    mask_gray = mask.convert('L') if mask is not None else None
    images = {}
    metrics['whole_frame_means_not_proof'] = {}
    for name, stage in zip(STAGES, stages):
        filenames = obj(stage.get('images'))
        require(name + '_distinct_image_filenames', len({filenames.get(family) for family in FAMILIES if isinstance(filenames.get(family), str)}) == len(FAMILIES))
        for family in FAMILIES:
            key = name + '_' + family
            image = load(filenames.get(family), RAW_SIZE if family.startswith('raw_') else SCREEN_SIZE, key)
            if image is None:
                continue
            images[(name, family)] = image
            metrics['whole_frame_means_not_proof'][key] = ImageStat.Stat(image.convert('L')).mean[0]
            if family.startswith('raw_'):
                require(key + '_grayscale', grayscale_spread(image) <= THRESHOLDS['raw_max_rgb_spread_u8'])

    for family in FAMILIES:
        sensor = family.startswith('raw_')
        family_metric = {'tools': {}, 'usable_tools': [], 'tools_showing_cooling': []}
        metrics['image_families'][family] = family_metric
        for identifier in ids:
            tools = [obj(stage.get(identifier)) for stage in tools_by_stage]
            in_frame_key = 'sensor_in_frame' if sensor else 'observer_in_frame'
            roi_key = 'sensor_roi_px' if sensor else 'observer_roi_px'
            entry = {'in_frame_by_stage': [tool.get(in_frame_key) for tool in tools]}
            family_metric['tools'][identifier] = entry
            require(family + '_' + identifier + '_visibility_stable', all(type(tool.get(in_frame_key)) is bool and tool.get(in_frame_key) == tools[0].get(in_frame_key) for tool in tools))
            if not all(tool.get(in_frame_key) is True for tool in tools):
                entry['excluded_reason'] = 'Not declared in frame at all stages'
                continue
            boxes = [box(tool.get(roi_key), RAW_SIZE if sensor else SCREEN_SIZE) for tool in tools]
            require(family + '_' + identifier + '_roi_valid_and_fixed', all(region is not None for region in boxes) and boxes[0] == boxes[1] == boxes[2])
            if any(region is None for region in boxes) or boxes[0] != boxes[1] or boxes[0] != boxes[2]:
                continue
            entry['roi_px'] = boxes[0]
            selection = None
            if sensor:
                stencil = tools[0].get('stencil_id')
                if mask_gray is None or not finite(stencil) or int(stencil) != stencil or not 1 <= stencil <= 255:
                    entry['excluded_reason'] = 'No valid surface ID mask/stencil'
                    continue
                selection = mask_gray.crop(boxes[0]).point([255 if index == int(stencil) else 0 for index in range(256)])
                count = selection.histogram()[255]
                entry['selected_tool_pixels'] = count
                if count < THRESHOLDS['minimum_selected_sensor_pixels']:
                    entry['excluded_reason'] = 'Tool is occluded or has no visible stencil pixels inside its projected region'
                    continue
            if not all((name, family) in images for name in STAGES):
                entry['excluded_reason'] = 'One or more stage images missing'
                continue
            means = [ImageStat.Stat(images[(name, family)].crop(boxes[0]).convert('L'), mask=selection).mean[0] for name in STAGES]
            entry.update(drop_metrics(means))
            family_metric['usable_tools'].append(identifier)
            if entry['shows_cooling']:
                family_metric['tools_showing_cooling'].append(identifier)
        require(family + '_at_least_one_tool_cools_in_both_transitions', len(family_metric['tools_showing_cooling']) >= THRESHOLDS['minimum_tools_showing_cooling_per_image_family'])

    result['failed_checks'] = [name for name, passed in checks.items() if not passed]
    result['passed'] = not errors and not result['failed_checks']
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
    sys.exit(main())