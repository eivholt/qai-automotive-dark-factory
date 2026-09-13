#!/usr/bin/env python3
"""Verify observer collider diagnostics from -DFColliderSmoke captures.

Checks real sensor PNG isolation, rendered wireframes outside the legend/HUD,
physics/query filtering and moving/disabled collision bounds. Corner transforms
are independently reconstructed with scalar arithmetic. This does not certify
exact mesh contact geometry or industrial collision safety.
"""
from __future__ import annotations

import argparse
import json
import math
from pathlib import Path

try:
    from PIL import Image, ImageChops, ImageStat
except ImportError:
    raise SystemExit("Pillow is required; use the project's existing QA environment.")

SCENE_CROP = (32, 170, 1190, 760)
KINDS = {'simulated', 'kinematic', 'static', 'query'}
GEOMETRIES = {'box', 'simple_bounds', 'complex_bounds', 'component_bounds'}
CAR_PROXIES = {'CarriageFloor', 'CarriageSideLeft', 'CarriageSideRight',
               'CarriageFront', 'CarriageRear', 'CarriageRoof', 'CarriageHood',
               'CarriageTrunk', 'CarriageHolderCollision0', 'CarriageHolderCollision1'}


def finite(x):
    return type(x) in (int, float) and math.isfinite(x)


def vec(x, size=3):
    return isinstance(x, list) and len(x) == size and all(finite(y) for y in x)


def near(a, b, tolerance=1e-6):
    return finite(a) and finite(b) and abs(a-b) <= tolerance


def same_tree(a, b, tolerance=1e-9):
    if type(a) is not type(b):
        return type(a) in (float, int) and type(b) in (float, int) and near(a, b, tolerance)
    if isinstance(a, dict):
        return a.keys() == b.keys() and all(same_tree(a[k], b[k], tolerance) for k in a)
    if isinstance(a, list):
        return len(a) == len(b) and all(same_tree(x, y, tolerance) for x, y in zip(a, b))
    return near(a, b, tolerance) if type(a) in (int, float) else a == b


def identity(box):
    return box.get('actor', ''), box.get('component', ''), box.get('instance', -1)


def indexed(boxes):
    return {identity(box): box for box in boxes}


def car_boxes(boxes):
    return {box['component']: box for box in boxes if box.get('component') in CAR_PROXIES}


def difference(a, b, threshold=12):
    diff = ImageChops.difference(a, b)
    r, g, blue = diff.split()
    maximum = ImageChops.lighter(r, ImageChops.lighter(g, blue))
    changed = sum(maximum.histogram()[threshold:])
    return {'changed_pixels': changed, 'threshold_u8': threshold,
            'mean_absolute_difference_u8': sum(ImageStat.Stat(diff).mean)/3,
            'maximum_difference_u8': maximum.getextrema()[1],
            'changed_fraction': changed/(maximum.width*maximum.height)}


def verify(directory):
    checks, errors, metrics = {}, [], {}
    result = {'passed': False, 'checks': checks, 'errors': errors, 'metrics': metrics,
              'thresholds': {'scene_crop': SCENE_CROP, 'wireframe_difference_u8': 12,
                             'minimum_physics_changed_pixels': 300,
                             'minimum_query_added_pixels': 100,
                             'world_corner_tolerance_cm': 1e-6}}
    def require(name, passed):
        checks[name] = bool(passed)
    try:
        report = json.loads((directory/'collider_overlay.json').read_text(encoding='utf-8-sig'),
                            parse_constant=lambda x: (_ for _ in ()).throw(ValueError('Nonfinite JSON: '+x)))
    except (OSError, ValueError) as exc:
        result.update(failed_checks=['read_native_report'])
        errors.append(str(exc))
        return result
    require('native_passed', report.get('passed') is True)
    for key in ('fixed_sensor_overlay_parity', 'cell_state_unchanged_by_overlay', 'raw_saved', 'overlay_modes_rendered'):
        require(key, report.get(key) is True)
    views, motion = report.get('views', []), report.get('motion_views', [])
    require('all_requested_views', len(views) == 16 and len(motion) == 4)
    screens = {}
    for index, row in enumerate(views+motion):
        stem = f'view_{index:02}'
        overlay = row.get('overlay', {})
        boxes = overlay.get('boxes', [])
        mode = overlay.get('mode')
        require(stem+'_mode', mode in (0, 1, 2))
        require(stem+'_bounded_count', overlay.get('box_count') == len(boxes) and len(boxes) <= 512
                and overlay.get('truncated') is False)
        require(stem+'_unique_ids', len(indexed(boxes)) == len(boxes))
        require(stem+'_line_count', finite(overlay.get('last_drawn_lines'))
                and 0 <= overlay['last_drawn_lines'] <= len(boxes)*12)
        require(stem+'_visible_count', finite(overlay.get('last_visible_boxes'))
                and 0 <= overlay['last_visible_boxes'] <= len(boxes))
        require(stem+'_screen_bounds_state', overlay.get('screen_bounds_valid') is (overlay.get('last_drawn_lines', 0) > 0))
        if overlay.get('screen_bounds_valid'):
            low_screen, high_screen = overlay.get('screen_min'), overlay.get('screen_max')
            require(stem+'_screen_clipped', vec(low_screen, 2) and vec(high_screen, 2)
                    and all(-1e-6 <= a <= b <= limit+1e-6
                            for a, b, limit in zip(low_screen, high_screen, (1600, 900))))
        require(stem+'_query_filter', mode != 1 or all(b.get('kind') != 'query' for b in boxes))
        require(stem+'_disabled_empty', mode != 0 or (not boxes and overlay.get('last_drawn_lines') == 0))
        require(stem+'_category_counts', overlay.get('physics_count') == sum(b.get('kind') != 'query' for b in boxes)
                and overlay.get('query_count') == sum(b.get('kind') == 'query' for b in boxes)
                and overlay.get('simulated_count') == sum(b.get('kind') == 'simulated' for b in boxes))
        for box_index, box in enumerate(boxes):
            tag = stem+f'_box_{box_index:03}'
            low, high = box.get('local_min_cm'), box.get('local_max_cm')
            corners, transform = box.get('world_corners_cm'), box.get('transform_rows')
            valid = (vec(low) and vec(high) and all(x <= y for x, y in zip(low, high))
                     and isinstance(corners, list) and len(corners) == 8 and all(vec(c) for c in corners)
                     and isinstance(transform, list) and len(transform) == 3 and all(vec(r, 4) for r in transform))
            require(tag+'_finite_geometry', valid)
            require(tag+'_documented_kind', box.get('kind') in KINDS and box.get('geometry') in GEOMETRIES
                    and bool(box.get('actor')) and bool(box.get('component')))
            require(tag+'_approximation_label', box.get('approximate') is (box.get('geometry') != 'box'))
            if valid:
                maximum_error = 0.
                for corner in range(8):
                    local = [high[axis] if corner & (1 << axis) else low[axis] for axis in range(3)]
                    expected = [sum(transform[axis][j]*local[j] for j in range(3))+transform[axis][3]
                                for axis in range(3)]
                    maximum_error = max(maximum_error, *(abs(a-b) for a, b in zip(expected, corners[corner])))
                require(tag+'_world_transform', maximum_error <= 1e-6)
                metrics['maximum_corner_error_cm'] = max(metrics.get('maximum_corner_error_cm', 0), maximum_error)
        try:
            image = Image.open(directory/row['image']).convert('RGB')
            require(stem+'_screen_dimensions', image.size == (1600, 900))
            screens[index] = image.crop(SCENE_CROP)
        except (OSError, ValueError, KeyError) as exc:
            require(stem+'_image_saved', False); errors.append(str(exc))
    if len(views) == 16:
        for mode in range(4):
            group = views[mode*4:mode*4+4]
            require(f'mode{mode}_names', [r.get('name') for r in group] == ['off', 'physics', 'all', 'off_restored'])
            require(f'mode{mode}_observer', all(r.get('observer_mode') == mode for r in group))
            first = group[0]
            for sequence, row in enumerate(group):
                require(f'mode{mode}_{sequence}_cell_unchanged', same_tree(first.get('pose'), row.get('pose')))
                require(f'mode{mode}_{sequence}_physics_unchanged', same_tree(first.get('portal_physics'), row.get('portal_physics')))
                require(f'mode{mode}_{sequence}_no_inference', row.get('pose', {}).get('inference_enabled') is False
                        and row.get('pose', {}).get('training_enabled') is False)
                for channel in ('raw_nir', 'raw_thermal'):
                    try:
                        baseline = (directory/first[channel]).read_bytes()
                        candidate = (directory/row[channel]).read_bytes()
                        require(f'mode{mode}_{sequence}_{channel}_byte_parity', bool(baseline) and baseline == candidate)
                        raw = Image.open(directory/row[channel]).convert('RGB')
                        r, g, b = raw.split()
                        require(f'mode{mode}_{sequence}_{channel}_contract', raw.size == (448, 256)
                                and ImageChops.difference(r, g).getbbox() is None
                                and ImageChops.difference(r, b).getbbox() is None)
                    except (KeyError, OSError, ValueError) as exc:
                        require(f'mode{mode}_{sequence}_{channel}_saved', False); errors.append(str(exc))
            physics_overlay, all_overlay = group[1]['overlay'], group[2]['overlay']
            require(f'mode{mode}_current_scene_inventory', physics_overlay.get('physics_count') == 114
                    and physics_overlay.get('query_count') == 0 and physics_overlay.get('simulated_count') == 84
                    and all_overlay.get('physics_count') == 114 and all_overlay.get('query_count') == 72
                    and all_overlay.get('simulated_count') == 84)
            physics, all_boxes = indexed(physics_overlay['boxes']), indexed(all_overlay['boxes'])
            require(f'mode{mode}_physics_subset', bool(physics) and physics.keys() < all_boxes.keys()
                    and all(value.get('kind') == all_boxes[key].get('kind')
                            and same_tree(value.get('transform_rows'), all_boxes[key].get('transform_rows'))
                            and all(a >= b-1e-6 for a, b in zip(value['local_min_cm'], all_boxes[key]['local_min_cm']))
                            and all(a <= b+1e-6 for a, b in zip(value['local_max_cm'], all_boxes[key]['local_max_cm']))
                            for key, value in physics.items()))
            require(f'mode{mode}_queries_added', all(all_boxes[key].get('kind') == 'query' for key in all_boxes.keys()-physics.keys()))
            for sequence, minimum in ((1, 300), (2, 100)):
                a, b = mode*4+sequence-1, mode*4+sequence
                if a in screens and b in screens:
                    delta = difference(screens[a], screens[b]); metrics[f'mode{mode}_overlay{sequence}'] = delta
                    require(f'mode{mode}_overlay{sequence}_visible_wireframes', delta['changed_pixels'] >= minimum)
            if mode*4 in screens and mode*4+3 in screens:
                delta = difference(screens[mode*4], screens[mode*4+3]); metrics[f'mode{mode}_off_restoration'] = delta
                require(f'mode{mode}_off_restoration', delta['maximum_difference_u8'] == 0)
    if len(motion) == 4 and len(views) == 16:
        stages = {row.get('name'): row for row in motion}
        require('motion_stage_names', set(stages) == {'moving', 'hidden', 'restored', 'near_plane'})
        if set(stages) == {'moving', 'hidden', 'restored', 'near_plane'}:
            original = car_boxes(views[2]['overlay']['boxes'])
            moved = car_boxes(stages['moving']['overlay']['boxes'])
            hidden = car_boxes(stages['hidden']['overlay']['boxes'])
            restored = car_boxes(stages['restored']['overlay']['boxes'])
            require('all_ten_actual_car_boxes_present', set(original) == CAR_PROXIES and set(moved) == CAR_PROXIES)
            require('disabled_car_boxes_disappear', not hidden and stages['hidden']['pose'].get('body_visible') is False)
            hidden_overlay = stages['hidden']['overlay']
            require('hidden_scene_inventory', hidden_overlay.get('physics_count') == 104
                    and hidden_overlay.get('query_count') == 41 and hidden_overlay.get('simulated_count') == 84)
            require('reenabled_car_boxes_return', set(restored) == CAR_PROXIES and stages['restored']['pose'].get('body_visible') is True)
            offset = stages['moving']['pose'].get('carriage_offset_y_cm')
            require('carriage_actually_moved', finite(offset) and offset > 100.)
            for name in CAR_PROXIES:
                if name in original and name in moved and name in restored:
                    require(name+'_moved_with_carriage', all(
                        near(b[0], a[0]) and near(b[1]-a[1], offset) and near(b[2], a[2])
                        for a, b in zip(original[name]['world_corners_cm'], moved[name]['world_corners_cm'])))
                    require(name+'_restored', same_tree(original[name], restored[name]))
            near_overlay = stages['near_plane']['overlay']
            require('near_plane_clipped_and_drawn', near_overlay.get('last_drawn_lines', 0) > 0
                    and near_overlay.get('last_visible_boxes', 0) < near_overlay.get('box_count', 0))
    runtime = directory/'runtime.log'
    if runtime.exists():
        log = runtime.read_text(encoding='utf-8', errors='replace')
        require('native_log_completed', 'DF_COLLIDER_SMOKE passed=1' in log)
        require('no_material_compile_fallback', 'Failed to compile Material' not in log and 'Default Material will be used' not in log)
    metrics['visual_review_required'] = 'Review wireframe placement, legend readability, near-plane clipping and all modalities; image differences alone do not certify usable framing.'
    result['failed_checks'] = [key for key, passed in checks.items() if not passed]
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
