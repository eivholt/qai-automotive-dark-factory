#!/usr/bin/env python3
"""Check private -DFCabinetSmoke images, telemetry and fixed-camera isolation.

Uses actual projected glass regions, excluding HUD pixels. Appearance statistics
supplement visual inspection; they are not electronics or weld-quality evidence.
"""
from __future__ import annotations

import argparse
import json
import math
from pathlib import Path

try:
    from PIL import Image, ImageChops, ImageDraw, ImageStat
except ImportError:
    raise SystemExit('Pillow is required; use the project QA environment.')

NAMES = ('dark_off', 'dark_on', 'mode0', 'mode1', 'mode2', 'mode3',
         'phase_idle', 'phase_stagger', 'phase_welding', 'phase_parked',
         'phase_restored', 'pause_before', 'pause_after')
TIMES = (10., 10., 10., 10., 10., 10., 0., 6.5, 10., 16., 10., 10., 10.)
PHASE_NAMES = ('Rest', 'Approach', 'At contact', 'Welding', 'Withdraw', 'Parked')
THRESHOLDS = {'lcd_difference_u8': 8, 'lcd_min_changed_pixels': 100,
              'lcd_min_stddev_u8': 8, 'lcd_max_white_clipped_fraction': .40,
              'led_difference_u8': 3, 'led_min_changed_pixels': 3,
              'gauge_difference_u8': 3, 'gauge_min_changed_pixels': 40,
              'raw_rgb_spread_u8': 0, 'numeric_tolerance': 1e-5}


def finite(value):
    return type(value) in (int, float) and math.isfinite(value)


def near(a, b, tolerance=1e-5):
    return finite(a) and finite(b) and abs(a-b) <= tolerance


def vector(value, length=3):
    return isinstance(value, list) and len(value) == length and all(finite(x) for x in value)


def dot(a, b):
    return sum(x*y for x, y in zip(a, b))


def finite_tree(value):
    if isinstance(value, dict):
        return all(finite_tree(x) for x in value.values())
    if isinstance(value, list):
        return all(finite_tree(x) for x in value)
    return finite(value) if type(value) in (int, float) else True


def polygon(projection, size, inset=.04):
    points = projection.get('corners_px', [])
    if projection.get('valid') is not True or len(points) != 4 or not all(vector(p, 2) for p in points):
        raise ValueError('Invalid projected surface')
    if any(not (0 <= p[0] < size[0] and 135 < p[1] < size[1]-75) for p in points):
        raise ValueError('Control region clipped or overlaps top/bottom HUD')
    center = [sum(p[axis] for p in points)/4 for axis in range(2)]
    points = [(center[0]+(p[0]-center[0])*(1-inset), center[1]+(p[1]-center[1])*(1-inset)) for p in points]
    mask = Image.new('L', size)
    ImageDraw.Draw(mask).polygon([points[i] for i in (0, 1, 3, 2)], fill=255)
    if sum(mask.histogram()[1:]) < 3:
        raise ValueError('Projected surface too small to verify')
    return mask


def difference(a, b, mask, threshold):
    diff = ImageChops.difference(a, b)
    channels = diff.split()
    maximum = ImageChops.lighter(channels[0], ImageChops.lighter(channels[1], channels[2]))
    count = sum(mask.histogram()[1:])
    histogram = maximum.histogram(mask)
    changed = sum(histogram[threshold:])
    return {'pixels': count, 'changed_pixels': changed, 'changed_fraction': changed/count,
            'mean_absolute_difference_u8': sum(ImageStat.Stat(diff, mask).mean)/3,
            'maximum_difference_u8': max(i for i, value in enumerate(histogram) if value),
            'threshold_u8': threshold}


def robot_reference(timing, seconds):
    a, contact, w, end, parked = [timing[k] for k in ('approach_start_seconds', 'approach_end_seconds',
                                                     'weld_start_seconds', 'weld_end_seconds', 'withdraw_end_seconds')]
    if seconds < a:
        return 0, 0.
    if seconds < contact:
        return 1, (seconds-a)/(contact-a)
    if seconds < w:
        return 2, 1.
    if seconds < end:
        return 3, (seconds-w)/(end-w)
    if seconds < parked:
        return 4, (seconds-end)/(parked-end)
    return 5, 1.


def verify(directory):
    checks, errors, metrics = {}, [], {}
    result = {'passed': False, 'checks': checks, 'errors': errors, 'metrics': metrics,
              'thresholds': THRESHOLDS}
    def require(name, passed):
        checks[name] = bool(passed)
    try:
        report = json.loads((directory/'control_cabinet.json').read_text(encoding='utf-8-sig'),
                            parse_constant=lambda x: (_ for _ in ()).throw(ValueError('Nonfinite JSON: '+x)))
    except (OSError, ValueError) as exc:
        errors.append(str(exc)); result['failed_checks'] = ['native_report_readable']; return result
    require('native_passed', report.get('passed') is True)
    for field in ('fixed_sensor_observer_parity', 'power_thermal_unchanged', 'pause_holds_panel_state',
                  'seek_restores_panel_state', 'bounded_resources', 'raw_saved'):
        require(field, report.get(field) is True)
    views = report.get('views', [])
    require('all_views', len(views) == len(NAMES) and [r.get('name') for r in views] == list(NAMES))
    screens, lcd_masks, button_masks, raw_bytes = {}, {}, {}, {}
    for index, row in enumerate(views):
        tag = f'view_{index:02}'
        panel, pose = row.get('panel', {}), row.get('pose', {})
        time = TIMES[index] if index < len(TIMES) else -1
        require(tag+'_finite', finite_tree(row))
        require(tag+'_scope_and_resources', panel.get('initialized') is True and panel.get('enabled') is True
                and panel.get('mesh_count_added') == 0 and panel.get('light_count') == 1
                and panel.get('thermal_profiles_added') == 0 and row.get('thermal_profile_count') == 244
                and panel.get('parent_mesh') == 'SM_Background_robots_screen')
        require(tag+'_unchanged_imported_mesh_count', row.get('imported_mesh_count') == views[0].get('imported_mesh_count'))
        require(tag+'_preview_only', pose.get('inference_enabled') is False and pose.get('training_enabled') is False)
        require(tag+'_clock_and_paused', near(row.get('time'), time) and near(panel.get('clock_seconds'), time)
                and near(panel.get('cycle_seconds'), time) and near(panel.get('cycle_progress'), time/21.)
                and panel.get('paused') is True)
        require(tag+'_power', panel.get('powered') is (index != 0))
        expected_mode = index-2 if 2 <= index <= 5 else 0
        require(tag+'_observer_mode', row.get('observer_mode') == expected_mode)
        screen = panel.get('screen', {})
        axes = [screen.get(k) for k in ('normal_world', 'right_world', 'up_world')]
        valid_axes = all(vector(a) for a in axes)
        require(tag+'_screen_basis', vector(screen.get('center_world_cm')) and valid_axes
                and all(near(dot(a, a), 1.) for a in axes)
                and all(near(dot(axes[a], axes[b]), 0.) for a, b in ((0, 1), (0, 2), (1, 2))))
        require(tag+'_screen_dimensions', finite(screen.get('width_cm')) and 20 < screen['width_cm'] < 100
                and finite(screen.get('height_cm')) and 15 < screen['height_cm'] < 80)
        robots = panel.get('robots', [])
        timing = {r.get('robot_index'): r for r in pose.get('robot_timing', {}).get('robots', [])}
        require(tag+'_four_robot_gauges', len(robots) == 4 and {r.get('index') for r in robots} == set(range(4)))
        for robot in robots:
            robot_index = robot.get('index')
            try:
                phase, progress = robot_reference(timing[robot_index], time)
                require(tag+f'_robot{robot_index}_actual_phase_progress', robot.get('phase') == PHASE_NAMES[phase]
                        and robot.get('phase_code') == phase and near(robot.get('progress'), progress))
            except (KeyError, TypeError, ZeroDivisionError) as exc:
                require(tag+f'_robot{robot_index}_actual_phase_progress', False); errors.append(str(exc))
        buttons = panel.get('buttons', [])
        require(tag+'_twelve_existing_lenses', len(buttons) == 12 and {b.get('index') for b in buttons} == set(range(12)))
        for button in buttons:
            b = button.get('index')
            require(tag+f'_lens{b}_geometry', vector(button.get('center_world_cm'))
                    and finite(button.get('radius_cm')) and .1 <= button['radius_cm'] <= 10)
            require(tag+f'_lens{b}_authored_color_and_power', button.get('color') in ('red', 'green')
                    and finite(button.get('level')) and (0 < button['level'] <= 1 if index != 0 else button['level'] == 0))
        try:
            image = Image.open(directory/row['image']).convert('RGB')
            require(tag+'_image_size', image.size == (1600, 900))
            screens[index] = image
            lcd_masks[index] = polygon(row.get('screen_projection', {}), image.size)
            button_masks[index] = {b['index']: polygon(b, image.size, .15) for b in row.get('button_projections', [])}
            require(tag+'_all_projected_regions_visible', len(button_masks[index]) == 12)
        except (OSError, ValueError, KeyError) as exc:
            require(tag+'_all_projected_regions_visible', False); errors.append(f'{tag}: {exc}')
        if index <= 5:
            require(tag+'_raw_saved', row.get('raw_saved') is True)
            for channel in ('raw_nir', 'raw_thermal'):
                try:
                    path = directory/row[channel]
                    raw_bytes[index, channel] = path.read_bytes()
                    raw = Image.open(path).convert('RGB'); r, g, b = raw.split()
                    require(tag+'_'+channel+'_contract', raw.size == (448, 256)
                            and ImageChops.difference(r, g).getbbox() is None
                            and ImageChops.difference(r, b).getbbox() is None)
                except (OSError, ValueError, KeyError) as exc:
                    require(tag+'_'+channel+'_contract', False); errors.append(str(exc))
    if len(views) == len(NAMES):
        for index in (3, 4, 5):
            for channel in ('raw_nir', 'raw_thermal'):
                require(f'mode{index-2}_{channel}_observer_independence', bool(raw_bytes.get((2, channel)))
                        and raw_bytes.get((2, channel)) == raw_bytes.get((index, channel)))
        require('actual_power_thermal_byte_parity', bool(raw_bytes.get((0, 'raw_thermal')))
                and raw_bytes.get((0, 'raw_thermal')) == raw_bytes.get((1, 'raw_thermal')))
        for first, second, label in ((8, 10, 'seek'), (11, 12, 'pause')):
            for key in ('clock_seconds', 'cycle_seconds', 'cycle_progress', 'robots', 'buttons'):
                require(f'{label}_{key}_identical', views[first]['panel'].get(key) == views[second]['panel'].get(key))
            if first in screens and second in screens and first in lcd_masks:
                d = difference(screens[first], screens[second], lcd_masks[first], 1)
                metrics[label+'_lcd_difference'] = d
                require(label+'_actual_lcd_pixels_identical', d['changed_pixels'] == 0)
    if all(i in screens and i in lcd_masks for i in (0, 1)):
        mask = lcd_masks[1]
        d = difference(screens[0], screens[1], mask, THRESHOLDS['lcd_difference_u8'])
        metrics['lcd_power_difference'] = d
        require('lcd_visibly_emits_in_dark', d['changed_pixels'] >= THRESHOLDS['lcd_min_changed_pixels'])
        gray = screens[1].convert('L'); stats = ImageStat.Stat(gray, mask); histogram = gray.histogram(mask)
        count = sum(histogram)
        metrics['dark_lcd'] = {'mean_u8': stats.mean[0], 'stddev_u8': stats.stddev[0],
                               'white_clipped_fraction': sum(histogram[250:])/count}
        require('lcd_retains_gauge_contrast', stats.stddev[0] >= THRESHOLDS['lcd_min_stddev_u8']
                and sum(histogram[250:])/count <= THRESHOLDS['lcd_max_white_clipped_fraction'])
        led_metrics = {}
        for button, mask in button_masks.get(1, {}).items():
            d = difference(screens[0], screens[1], mask, THRESHOLDS['led_difference_u8'])
            led_metrics[button] = d
            require(f'led{button}_actually_backlit', d['changed_pixels'] >= THRESHOLDS['led_min_changed_pixels'])
            meta = next(x for x in views[1]['panel']['buttons'] if x['index'] == button)
            projection = next(x for x in views[1]['button_projections'] if x['index'] == button)
            center_mask = polygon(projection, screens[1].size, .40)
            rgb = ImageStat.Stat(screens[1], center_mask).mean
            dominant = 1 if meta['color'] == 'green' else 0
            d['center_rgb_u8'] = rgb
            require(f'led{button}_retains_source_color', rgb[dominant] > max(rgb[i] for i in range(3) if i != dominant)+10)
        metrics['button_power_difference'] = led_metrics
    if all(i in screens and i in lcd_masks for i in (6, 8)):
        d = difference(screens[6], screens[8], lcd_masks[8], THRESHOLDS['gauge_difference_u8'])
        metrics['cycle_gauge_difference'] = d
        require('actual_gauges_change_with_cycle', d['changed_pixels'] >= THRESHOLDS['gauge_min_changed_pixels'])
    try:
        runtime = (directory/'runtime.log').read_text(encoding='utf-8', errors='replace')
        require('no_material_or_cabinet_fallback', 'DF_CABINET_READY' in runtime
                and not any(x in runtime for x in ('DF_CABINET_MISSING_', 'Failed to compile Material',
                                                   'Default Material will be used in game', 'Error: Shader')))
    except OSError as exc:
        require('runtime_log_readable', False); errors.append(str(exc))
    metrics['visual_review_required'] = 'Review LCD orientation, four gauge labels/readouts, lens backlights, surrounding spill and all modalities; differences alone do not establish readability.'
    result['failed_checks'] = [name for name, passed in checks.items() if not passed]
    result['passed'] = not result['failed_checks'] and not errors
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
