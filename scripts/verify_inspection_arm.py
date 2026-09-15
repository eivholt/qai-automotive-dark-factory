#!/usr/bin/env python3
"""Verify private rigid-mounted inspection-camera capture telemetry.

Examples:
  python scripts/verify_inspection_arm.py --manifest artifacts/scan/manifest.jsonl --output artifacts/scan/arm-checks.json
  python scripts/verify_inspection_arm.py --runtime artifacts/live/process_runtime.json --output artifacts/live/arm-checks.json

This checks capture geometry, ordering and PNG format, not model accuracy,
thermal calibration, occlusion, swept-volume clearance or industrial kinematics.
Runtime reports do not contain sensor PNG paths; PNG checks require a manifest.
"""
from __future__ import annotations

import argparse
import hashlib
import io
import json
import math
from pathlib import Path, PureWindowsPath

VIEW_RECIPE = 'rigid-arm-thermal-scan-v1'
CAPTURE_RECIPES = ('process-arm-scan-episodes-v1', 'process-arm-scan-episodes-v2')
THERMAL_RECIPE_V2 = 'authored-exposed-weld-core-halo-v2'
THERMAL_STAGES = ('adhesive', 'weld')
PANEL_VIEW_RECIPE = 'rigid-arm-panel-three-edge-scan-v6'
PANEL_CAPTURE_RECIPE = 'panel-three-edge-contact-episodes-v12'
PANEL_CAPTURE_RECIPES = (PANEL_CAPTURE_RECIPE, 'panel-three-edge-contact-episodes-v10')
PANEL_THERMAL_RECIPE = 'panel-three-edge-contact-transfer-v8'
PANEL_CONTRACT = 'automotive-panel-process-v3'
PANEL_STAGES = ('panel_presence', 'adhesive', 'panel_seated', 'weld')
THRESHOLDS = {
    'width': 448, 'height': 256, 'lens_position_tolerance_cm': 1e-4,
    'lens_rotation_tolerance_deg': 1e-4, 'maximum_normalized_corner_extent': .8,
    'frustum_roundoff_tolerance': 1e-6, 'minimum_consecutive_travel_cm': 30.,
    'maximum_live_batch_age_seconds_exclusive': 8., 'maximum_runtime_records': 192,
}


def finite(value):
    try:
        return type(value) in (int, float) and math.isfinite(value)
    except OverflowError:
        return False


def integer(value, minimum=0):
    return finite(value) and value >= minimum and value == int(value)


def vector(value, length=3):
    return isinstance(value, list) and len(value) == length and all(finite(x) for x in value)


def finite_tree(value):
    if isinstance(value, dict):
        return all(finite_tree(x) for x in value.values())
    if isinstance(value, list):
        return all(finite_tree(x) for x in value)
    return finite(value) if type(value) in (int, float) else True


def mapping(value):
    return value if isinstance(value, dict) else {}


def nonempty(value):
    return isinstance(value, str) and bool(value.strip())


def distance(a, b):
    return math.dist(a, b)


def rotation_error(a, b):
    return max(abs((x-y+180.) % 360.-180.) for x, y in zip(a, b))


def same_pose_table(reference, actual):
    """Keep authored values exact; allow only established transform roundoff.

    Native FK rebuilds endpoints from component-relative transforms. Repeated
    parenting updates introduce sub-nanometre location noise in those results;
    joint recipes and spot identities must nevertheless remain exactly equal.

    Regression: tiny FK drift is accepted, physical drift and authored edits are not.

    >>> from copy import deepcopy
    >>> pose = {'spot_index': 0, 'joint_angles_deg': [0.] * 6,
    ...         'lens_location_cm': [0., 0., 100.], 'lens_rotation_deg': [0., 0., 0.]}
    >>> reference = [dict(deepcopy(pose), spot_index=i) for i in range(3)]
    >>> changed = deepcopy(reference); changed[1]['lens_location_cm'][0] += 2e-10
    >>> same_pose_table(reference, changed)
    True
    >>> changed[1]['lens_location_cm'][0] += .001
    >>> same_pose_table(reference, changed)
    False
    >>> changed = deepcopy(reference); changed[1]['lens_rotation_deg'][1] += .001
    >>> same_pose_table(reference, changed)
    False
    >>> changed = deepcopy(reference); changed[1]['joint_angles_deg'][0] += 1e-10
    >>> same_pose_table(reference, changed)
    False
    >>> changed = deepcopy(reference); changed[1]['spot_index'] = 2
    >>> same_pose_table(reference, changed)
    False
    """
    if not isinstance(reference, list) or not isinstance(actual, list) or len(reference) != 3 or len(actual) != 3:
        return False
    for a, b in zip(reference, actual):
        if not isinstance(a, dict) or not isinstance(b, dict) or set(a) != set(b):
            return False
        for key in a:
            if key == 'lens_location_cm':
                if not vector(a[key]) or not vector(b[key]) or distance(a[key], b[key]) > THRESHOLDS['lens_position_tolerance_cm']:
                    return False
            elif key == 'lens_rotation_deg':
                if not vector(a[key]) or not vector(b[key]) or not rotation_error(a[key], b[key]) <= THRESHOLDS['lens_rotation_tolerance_deg']:
                    return False
            elif a[key] != b[key]:
                return False
    return True


class Audit:
    view_recipe = VIEW_RECIPE
    capture_stages = THERMAL_STAGES

    def __init__(self, source, mode):
        self.result = {
            'schema': 'darkfactory-inspection-arm-verification-v1',
            'source': str(Path(source).resolve()), 'mode': mode,
            'scope': 'capture_geometry_and_transport_only', 'model_accuracy_evidence': False,
            'thermal_calibration_evidence': False, 'thresholds': THRESHOLDS.copy(),
            'checks': {}, 'errors': [], 'metrics': {},
            'limitations': [
                'Recorded endpoint poses do not certify the complete moving-arm path or collision clearance.',
                'Frustum bounds do not prove visibility through occluding geometry; inspect actual sensor images.',
                'Runtime capture ages end at image enqueue, not HTTP completion; reply freshness is a separate native check.',
            ],
        }
        self.extents, self.ages = [], []
        self.serials, self.batch_serials, self.travels, self.batch_spans = [], [], [], []
        self.image_paths = set()
        self.mounts, self.pose_tables = [], []
        self.image_count = self.batch_count = 0

    def require(self, name, condition):
        if name in self.result['checks']:
            raise ValueError('Duplicate check name: '+name)
        self.result['checks'][name] = bool(condition)

    def error(self, name, exception):
        self.require(name, False)
        self.result['errors'].append(str(exception))

    def read_json(self, path):
        return json.loads(Path(path).read_text(encoding='utf-8-sig'))

    def check_arm(self, raw, view, prefix):
        self.require(prefix+'_object', isinstance(raw, dict) and bool(raw))
        arm = mapping(raw)
        self.require(prefix+'_schema', arm.get('schema') == 'authored-r4-inspection-scan-v1')
        self.require(prefix+'_configured', arm.get('configured') is True)
        self.require(prefix+'_settled', arm.get('settled') is True)
        self.require(prefix+'_requested_spot', integer(arm.get('requested_spot'))
                     and arm.get('requested_spot') == view.get('spot'))
        for key, expected in (('transition_seconds', .8), ('elapsed_seconds', .8),
                              ('progress', 1.), ('inspection_extension', 1.)):
            self.require(prefix+'_'+key, finite(arm.get(key)) and abs(arm[key]-expected) <= 1e-6)
        ids = ['robot_nx_ny_'+role for role in ('base', 'shoulder', 'elbow', 'forearm_roll', 'wrist', 'tool_roll')]
        self.require(prefix+'_joint_order', arm.get('joint_ids') == ids)
        current, target = arm.get('current_joint_angles_deg'), arm.get('target_joint_angles_deg')
        self.require(prefix+'_current_six_angles', vector(current, 6))
        self.require(prefix+'_target_six_angles', vector(target, 6))
        if vector(current, 6) and vector(target, 6):
            self.require(prefix+'_actual_joints_at_target', max(abs(a-b) for a, b in zip(current, target)) <= 1e-4)
        location, quaternion, offset = (arm.get(key) for key in (
            'fixed_mount_location_cm', 'fixed_mount_quaternion_xyzw', 'optical_offset_cm'))
        self.require(prefix+'_mount_location', vector(location) and distance(location, [0., 0., -24.]) <= 1e-4)
        self.require(prefix+'_mount_unit_quaternion', vector(quaternion, 4)
                     and abs(math.hypot(*quaternion)-1.) <= 1e-6)
        self.require(prefix+'_optical_offset', vector(offset) and distance(offset, [21., 0., 0.]) <= 1e-4)
        if vector(location) and vector(quaternion, 4) and vector(offset):
            self.mounts.append((location, quaternion, offset))
        poses = arm.get('poses')
        self.require(prefix+'_three_authored_poses', isinstance(poses, list) and len(poses) == 3)
        if isinstance(poses, list):
            self.require(prefix+'_pose_order', [mapping(pose).get('spot_index') for pose in poses] == [0, 1, 2])
            for index, raw_pose in enumerate(poses):
                pose = mapping(raw_pose)
                self.require(prefix+f'_pose_{index}_six_angles', vector(pose.get('joint_angles_deg'), 6))
                for key in ('lens_location_cm', 'lens_rotation_deg'):
                    self.require(prefix+f'_pose_{index}_'+key, vector(pose.get(key)))
                if index == view.get('spot'):
                    self.require(prefix+'_target_matches_authored_pose', vector(target, 6)
                                 and pose.get('joint_angles_deg') == target)
                    if vector(pose.get('lens_location_cm')) and vector(arm.get('lens_location_cm')):
                        self.require(prefix+'_actual_at_authored_lens_position', distance(pose['lens_location_cm'], arm['lens_location_cm']) <= 1e-3)
                    if vector(pose.get('lens_rotation_deg')) and vector(arm.get('lens_rotation_deg')):
                        self.require(prefix+'_actual_at_authored_lens_rotation', rotation_error(pose['lens_rotation_deg'], arm['lens_rotation_deg']) <= 1e-4)
            if len(poses) == 3:
                self.pose_tables.append(poses)
        for key in ('lens_location_cm', 'lens_rotation_deg'):
            self.require(prefix+'_'+key, vector(arm.get(key)))
        if vector(arm.get('lens_location_cm')) and vector(view.get('lens_location_cm')):
            self.require(prefix+'_mount_matches_reported_lens_position', distance(arm['lens_location_cm'], view['lens_location_cm']) <= 1e-4)
        if vector(arm.get('lens_rotation_deg')) and vector(view.get('lens_rotation_deg')):
            self.require(prefix+'_mount_matches_reported_lens_rotation', rotation_error(arm['lens_rotation_deg'], view['lens_rotation_deg']) <= 1e-4)

    def check_view(self, raw, prefix, live):
        self.require(prefix+'_object', isinstance(raw, dict))
        view = mapping(raw)
        self.require(prefix+'_finite_numbers', finite_tree(view))
        for key in ('body_id', 'stage'):
            self.require(prefix+'_'+key, nonempty(view.get(key)))
        self.require(prefix+'_thermal_stage', view.get('stage') in self.capture_stages)
        for key in ('body_sequence', 'batch_serial', 'capture_serial'):
            self.require(prefix+'_'+key, integer(view.get(key), 1))
        self.require(prefix+'_spot', integer(view.get('spot')) and view['spot'] in (0, 1, 2))
        self.require(prefix+'_clock', finite(view.get('clock_seconds')) and view['clock_seconds'] >= 0)
        age = view.get('batch_age_seconds')
        self.require(prefix+'_age_nonnegative', finite(age) and age >= 0)
        if finite(age):
            self.ages.append(age)
        if live:
            self.require(prefix+'_age_below_8_seconds', finite(age) and 0 <= age < 8.)
        self.require(prefix+'_arm_settled', view.get('arm_settled') is True)
        self.require(prefix+'_render_warmup_finished', integer(view.get('settle_frames_remaining'))
                     and view['settle_frames_remaining'] == 0)
        self.check_arm(view.get('arm'), view, prefix+'_joint_scan')
        for key in ('location_cm', 'rotation_deg', 'lens_location_cm', 'lens_rotation_deg'):
            self.require(prefix+'_'+key, vector(view.get(key)))
        if vector(view.get('location_cm')) and vector(view.get('lens_location_cm')):
            self.require(prefix+'_capture_at_physical_lens', distance(view['location_cm'], view['lens_location_cm']) <= 1e-4)
        if vector(view.get('rotation_deg')) and vector(view.get('lens_rotation_deg')):
            self.require(prefix+'_capture_uses_physical_lens_rotation', rotation_error(view['rotation_deg'], view['lens_rotation_deg']) <= 1e-4)
        fov = view.get('fov_deg')
        self.require(prefix+'_fov', finite(fov) and 8. <= fov <= 110.)
        corners = view.get('framing_corners_view_cm')
        corners_valid = isinstance(corners, list) and len(corners) == 16 and all(vector(x) for x in corners)
        self.require(prefix+'_sixteen_finite_framing_corners', corners_valid)
        if corners_valid:
            self.require(prefix+'_all_targets_in_front', all(point[0] > 0 for point in corners))
            if finite(fov) and 0 < fov < 180 and all(point[0] > 0 for point in corners):
                tangent = math.tan(math.radians(fov)/2.)
                try:
                    extent = max(max(abs(y)/(x*tangent), abs(z)*448./256./(x*tangent)) for x, y, z in corners)
                except (ZeroDivisionError, OverflowError):
                    extent = math.inf
                if finite(extent):
                    self.extents.append(extent)
                self.require(prefix+'_all_targets_inside_framing_margin', finite(extent) and extent <= .8+1e-6)
        if integer(view.get('capture_serial'), 1):
            self.serials.append(view['capture_serial'])
        return view

    def check_batch(self, raw, prefix, live, row=None):
        self.batch_count += 1
        self.require(prefix+'_three_views', isinstance(raw, list) and len(raw) == 3)
        if not isinstance(raw, list):
            return
        views = [self.check_view(view, prefix+f'_view_{index}', live) for index, view in enumerate(raw)]
        self.require(prefix+'_spot_order', [view.get('spot') for view in views] == [0, 1, 2])
        for key in ('body_id', 'body_sequence', 'stage', 'batch_serial'):
            values = [view.get(key) for view in views]
            self.require(prefix+'_same_'+key, bool(values) and all(value == values[0] for value in values))
        if views and integer(views[0].get('batch_serial'), 1):
            self.batch_serials.append(views[0]['batch_serial'])
        if row is not None:
            for key in ('body_id', 'stage'):
                self.require(prefix+'_row_matches_'+key, bool(views) and all(view.get(key) == row.get(key) for view in views))
        clocks = [view.get('clock_seconds') for view in views]
        ages = [view.get('batch_age_seconds') for view in views]
        serials = [view.get('capture_serial') for view in views]
        self.require(prefix+'_capture_serials_strictly_increase', len(serials) == 3 and all(finite(x) for x in serials)
                     and all(b > a for a, b in zip(serials, serials[1:])))
        self.require(prefix+'_batch_age_monotonic', len(ages) == 3 and all(finite(x) for x in ages)
                     and all(b >= a for a, b in zip(ages, ages[1:])))
        if len(clocks) == 3 and all(finite(x) for x in clocks):
            self.batch_spans.append(clocks[-1]-clocks[0])
            self.require(prefix+'_clock_monotonic', all(b >= a for a, b in zip(clocks, clocks[1:])))
            if not live:
                self.require(prefix+'_offline_thermal_clock_frozen', clocks[0] == clocks[1] == clocks[2])
        for index, (a, b) in enumerate(zip(views, views[1:])):
            valid = vector(a.get('location_cm')) and vector(b.get('location_cm'))
            travelled = distance(a['location_cm'], b['location_cm']) if valid else None
            self.require(prefix+f'_physical_travel_{index}_to_{index+1}', valid and finite(travelled) and travelled >= 30.)
            if valid and finite(travelled):
                self.travels.append(travelled)

    def check_png(self, item, root, prefix):
        from PIL import Image, ImageChops
        self.require(prefix+'_image_object', isinstance(item, dict))
        item = mapping(item)
        relative = item.get('path')
        relative_valid = nonempty(relative) and not Path(relative).is_absolute() and not PureWindowsPath(relative).anchor
        self.require(prefix+'_relative_image_path', relative_valid)
        if not relative_valid:
            return None
        path = (root/relative).resolve()
        self.require(prefix+'_image_stays_in_capture_directory', path.is_relative_to(root))
        if not path.is_relative_to(root):
            return None
        self.require(prefix+'_unique_image_path', str(path) not in self.image_paths)
        self.image_paths.add(str(path))
        try:
            raw = path.read_bytes()
            self.require(prefix+'_8bit_png_ihdr', len(raw) >= 33 and raw[:8] == b'\x89PNG\r\n\x1a\n'
                         and raw[12:16] == b'IHDR' and raw[24] == 8 and raw[25] in (0, 2, 6))
            with Image.open(io.BytesIO(raw)) as checked:
                checked.verify()
            with Image.open(io.BytesIO(raw)) as image:
                image.load()
                self.require(prefix+'_png_format', image.format == 'PNG')
                self.require(prefix+'_448_by_256', image.size == (448, 256))
                self.require(prefix+'_single_frame', not getattr(image, 'is_animated', False))
                self.require(prefix+'_grayscale_or_rgb', image.mode in ('L', 'RGB', 'RGBA'))
                r, g, b, alpha = image.convert('RGBA').split()
                self.require(prefix+'_equal_rgb_channels', ImageChops.difference(r, g).getextrema() == (0, 0)
                             and ImageChops.difference(r, b).getextrema() == (0, 0))
                self.require(prefix+'_alpha_opaque', alpha.getextrema() == (255, 255))
                pixels_hash = hashlib.sha256(r.tobytes()).hexdigest()
            self.image_count += 1
            return hashlib.sha256(raw).hexdigest(), pixels_hash
        except (OSError, ValueError, SyntaxError) as exc:
            self.error(prefix+'_readable_png', exc)
            return None

    def manifest(self, source):
        source = Path(source).resolve()
        try:
            lines = source.read_text(encoding='utf-8-sig').splitlines()
        except (OSError, ValueError) as exc:
            self.error('manifest_readable', exc)
            return
        rows, identifiers = [], set()
        for line_number, line in enumerate(lines, 1):
            if not line.strip():
                continue
            try:
                rows.append((line_number, json.loads(line)))
            except ValueError as exc:
                self.error(f'line_{line_number}_json', exc)
        self.require('manifest_has_rows', bool(rows))
        thermal_count = 0
        for line_number, raw in rows:
            prefix = f'line_{line_number}'
            self.require(prefix+'_object', isinstance(raw, dict))
            row = mapping(raw)
            self.require(prefix+'_finite_numbers', finite_tree(row))
            sample = row.get('sample_id')
            self.require(prefix+'_sample_id', nonempty(sample))
            self.require(prefix+'_unique_sample_id', nonempty(sample) and sample not in identifiers)
            if nonempty(sample):
                identifiers.add(sample)
            self.require(prefix+'_body_id', nonempty(row.get('body_id')))
            self.require(prefix+'_view_recipe', row.get('view_recipe') == VIEW_RECIPE)
            if 'episode_id' in row or 'split' in row or 'capture_recipe' in row:
                self.require(prefix+'_capture_recipe', row.get('capture_recipe') in CAPTURE_RECIPES)
                if row.get('capture_recipe') == 'process-arm-scan-episodes-v2':
                    self.require(prefix+'_thermal_recipe_v2', row.get('thermal_recipe') == THERMAL_RECIPE_V2
                                 and mapping(row.get('ground_truth')).get('weld_heat_recipe') == THERMAL_RECIPE_V2)
            stage = row.get('stage')
            self.require(prefix+'_stage', stage in ('doors', *THERMAL_STAGES))
            thermal = stage in THERMAL_STAGES
            expected_cameras = ['spot1', 'spot2', 'spot3'] if thermal else ['left', 'right']
            images = row.get('images')
            self.require(prefix+'_images_array', isinstance(images, list) and len(images) == len(expected_cameras))
            if isinstance(images, list):
                self.require(prefix+'_camera_order', [mapping(item).get('camera_id') for item in images] == expected_cameras)
                hashes = [self.check_png(item, source.parent, prefix+f'_image_{index}') for index, item in enumerate(images)]
                if thermal:
                    self.require(prefix+'_three_distinct_pngs', len(hashes) == 3 and all(hashes) and len({h[0] for h in hashes if h}) == 3)
                    self.require(prefix+'_three_distinct_pixel_arrays', len(hashes) == 3 and all(hashes) and len({h[1] for h in hashes if h}) == 3)
            if thermal:
                thermal_count += 1
                self.check_batch(row.get('inspection_views'), prefix+'_batch', False, row)
            else:
                self.require(prefix+'_doors_have_no_thermal_views', row.get('inspection_views') in (None, []))
        self.require('manifest_has_thermal_batches', thermal_count > 0)
        self.result['metrics'].update(manifest_rows=len(rows), thermal_rows=thermal_count)

    def runtime(self, source):
        try:
            raw = self.read_json(source)
        except (OSError, ValueError) as exc:
            self.error('runtime_readable', exc)
            return
        self.require('runtime_object', isinstance(raw, dict))
        report = mapping(raw)
        self.require('runtime_view_recipe', report.get('view_recipe') == self.view_recipe)
        captures = report.get('inspection_captures')
        self.require('runtime_has_bounded_capture_records', isinstance(captures, list) and 3 <= len(captures) <= 192)
        if not isinstance(captures, list):
            return
        groups, current, prior = [], [], None
        for raw in captures:
            view = mapping(raw)
            batch = view.get('batch_serial')
            if current and batch != prior:
                groups.append(current)
                current = []
            current.append(raw)
            prior = batch
        if current:
            groups.append(current)
        for index, group in enumerate(groups):
            self.check_batch(group, f'batch_{index}', True)
        clocks = [mapping(view).get('clock_seconds') for view in captures]
        self.require('runtime_capture_clock_monotonic', bool(clocks) and all(finite(x) for x in clocks)
                     and all(b >= a for a, b in zip(clocks, clocks[1:])))
        self.result['metrics']['runtime_capture_records'] = len(captures)
        self.result['metrics']['runtime_pngs_checked'] = False
        self.result['metrics']['native_process_verdict'] = report.get('passed') if type(report.get('passed')) is bool else None

    def pose_tables_unchanged(self):
        return all(same_pose_table(self.pose_tables[0], poses) for poses in self.pose_tables)

    def finish(self):
        self.require('has_complete_capture_evidence', self.batch_count > 0 and len(self.serials) >= 3)
        self.require('capture_serials_globally_increase', bool(self.serials)
                     and all(b > a for a, b in zip(self.serials, self.serials[1:])))
        self.require('batch_serials_globally_increase', bool(self.batch_serials)
                     and all(b > a for a, b in zip(self.batch_serials, self.batch_serials[1:])))
        self.require('fixed_mount_unchanged_across_captures', bool(self.mounts)
                     and all(mount == self.mounts[0] for mount in self.mounts))
        self.require('authored_pose_table_unchanged_across_captures', bool(self.pose_tables)
                     and self.pose_tables_unchanged())
        self.result['metrics'].update(
            batches=self.batch_count, pngs_checked=self.image_count,
            minimum_consecutive_camera_travel_cm=min(self.travels, default=None),
            maximum_normalized_corner_extent=max(self.extents, default=None),
            maximum_capture_enqueue_age_seconds=max(self.ages, default=None),
            maximum_batch_simulation_span_seconds=max(self.batch_spans, default=None),
        )
        self.result['checks_count'] = len(self.result['checks'])
        self.result['failed_checks'] = [name for name, good in self.result['checks'].items() if not good]
        self.result['passed'] = not self.result['failed_checks'] and not self.result['errors']
        return self.result


class PanelAudit(Audit):
    """Separate two-bead panel-v3 path; the legacy door/thermal checks stay unchanged."""
    view_recipe = PANEL_VIEW_RECIPE
    capture_stages = PANEL_STAGES

    def __init__(self, source, mode):
        super().__init__(source, mode)
        self.modality_pose_tables = {'nir': [], 'adhesive': [], 'weld': []}
        self.result['contract'] = PANEL_CONTRACT

    def check_arm(self, raw, view, prefix):
        arm = mapping(raw)
        self.require(prefix+'_actual_sensor_prime_captures', type(view.get('sensor_warmup_captures')) is int
                     and view['sensor_warmup_captures'] == 3)
        modality = 'nir' if view.get('stage') in ('panel_presence', 'panel_seated') else 'thermal'
        family = 'nir' if modality == 'nir' else view.get('stage')
        self.require(prefix+'_stage_uses_correct_sensor_modality', arm.get('scan_modality') == modality)
        self.require(prefix+'_stage_uses_correct_physical_pose_family', arm.get('scan_family') == family)
        before = len(self.pose_tables)
        super().check_arm(raw, view, prefix)
        if len(self.pose_tables) > before and family in self.modality_pose_tables:
            self.modality_pose_tables[family].append(self.pose_tables[-1])

    def pose_tables_unchanged(self):
        # NIR, two-bead adhesive and three-contact weld scans have distinct
        # authored pose tables. Each table and the shared tool-relative mount
        # must remain stable across cameras and bodies.
        return all(not poses or all(same_pose_table(poses[0], p) for p in poses)
                   for poses in self.modality_pose_tables.values())

    def manifest(self, source):
        source = Path(source).resolve()
        try:
            lines = source.read_text(encoding='utf-8-sig').splitlines()
        except (OSError, ValueError) as exc:
            self.error('manifest_readable', exc)
            return
        rows, identifiers, episodes = [], set(), {}
        for line_number, line in enumerate(lines, 1):
            if not line.strip():
                continue
            try:
                rows.append((line_number, json.loads(line)))
            except ValueError as exc:
                self.error(f'line_{line_number}_json', exc)
        self.require('manifest_has_rows', bool(rows))
        stage_counts = {stage: 0 for stage in PANEL_STAGES}
        for line_number, raw in rows:
            prefix = f'line_{line_number}'
            self.require(prefix+'_object', isinstance(raw, dict))
            row = mapping(raw)
            self.require(prefix+'_finite_numbers', finite_tree(row))
            sample = row.get('sample_id')
            self.require(prefix+'_sample_id', nonempty(sample))
            self.require(prefix+'_unique_sample_id', nonempty(sample) and sample not in identifiers)
            if nonempty(sample): identifiers.add(sample)
            self.require(prefix+'_body_id', nonempty(row.get('body_id')))
            self.require(prefix+'_contract', row.get('contract_version') == PANEL_CONTRACT)
            self.require(prefix+'_view_recipe', row.get('view_recipe') == PANEL_VIEW_RECIPE)
            self.require(prefix+'_thermal_recipe', row.get('thermal_recipe') == PANEL_THERMAL_RECIPE
                         and mapping(row.get('ground_truth')).get('weld_heat_recipe') == PANEL_THERMAL_RECIPE)
            truth = mapping(row.get('ground_truth'))
            beads, welds = truth.get('adhesive'), truth.get('welds')
            self.require(prefix+'_two_physical_adhesive_beads', isinstance(beads, list) and len(beads) == 2)
            self.require(prefix+'_adhesive_bead_ids', isinstance(beads, list) and [mapping(x).get('index') for x in beads] == [0, 1])
            self.require(prefix+'_six_weld_contacts', isinstance(welds, list) and len(welds) == 6)
            bead_map = truth.get('adhesive_view_beads')
            self.require(prefix+'_two_bead_camera_mapping', isinstance(bead_map, list) and len(bead_map) == 3
                         and all(type(x) is int for x in bead_map) and bead_map == [0, 1, 0])
            stage = row.get('stage')
            self.require(prefix+'_stage', stage in PANEL_STAGES)
            if stage in stage_counts: stage_counts[stage] += 1
            self.require(prefix+'_expected_decision', row.get('expected_decision') in ('G', 'A', 'R'))
            scale = row.get('nir_illuminator_power_scale')
            self.require(prefix+'_bounded_physical_nir_power', finite(scale) and .8-1e-6 <= scale <= 1.2+1e-6)
            images = row.get('images')
            self.require(prefix+'_three_images', isinstance(images, list) and len(images) == 3)
            if isinstance(images, list):
                self.require(prefix+'_camera_order', [mapping(item).get('camera_id') for item in images] == ['spot1', 'spot2', 'spot3'])
                hashes = [self.check_png(item, source.parent, prefix+f'_image_{index}') for index, item in enumerate(images)]
                occluded = (stage in ('panel_presence', 'panel_seated') and row.get('expected_decision') == 'A'
                            and mapping(row.get('ground_truth')).get('occluded') is True)
                if not occluded:
                    self.require(prefix+'_three_distinct_pngs', len(hashes) == 3 and all(hashes) and len({h[0] for h in hashes if h}) == 3)
                    self.require(prefix+'_three_distinct_pixel_arrays', len(hashes) == 3 and all(hashes) and len({h[1] for h in hashes if h}) == 3)
                else:
                    # A rigid opaque lens cover can legitimately yield identical
                    # pixels. Physical pose/order/frustum tests below still apply.
                    self.require(prefix+'_opaque_cover_has_three_valid_pngs', len(hashes) == 3 and all(hashes))
            self.check_batch(row.get('inspection_views'), prefix+'_batch', False, row)
            if 'episode_id' in row or 'split' in row or 'capture_recipe' in row:
                self.require(prefix+'_capture_recipe', row.get('capture_recipe') in PANEL_CAPTURE_RECIPES)
                self.require(prefix+'_episode_identity', nonempty(row.get('episode_id')) and row.get('episode_id') == row.get('body_id'))
                self.require(prefix+'_split', row.get('split') in ('train', 'validation', 'test'))
                if nonempty(row.get('episode_id')): episodes.setdefault(row['episode_id'], []).append(row)
        for number, episode in enumerate(episodes.values()):
            prefix = f'episode_{number}'
            self.require(prefix+'_eight_ordered_stage_batches', [row.get('stage') for row in episode]
                         == ['panel_presence', 'adhesive', 'adhesive', 'adhesive', 'panel_seated', 'weld', 'weld', 'weld'])
            self.require(prefix+'_single_split', len({row.get('split') for row in episode}) == 1)
            self.require(prefix+'_coherent_nir_source_power', bool(episode) and all(row.get('nir_illuminator_power_scale') == episode[0].get('nir_illuminator_power_scale') for row in episode))
            for stage in THERMAL_STAGES:
                self.require(prefix+'_'+stage+'_hot_ambiguous_cool', [row.get('expected_decision') for row in episode if row.get('stage') == stage] == ['R', 'A', 'G'])
        self.require('manifest_has_all_four_stages', all(stage_counts.values()))
        self.result['metrics'].update(manifest_rows=len(rows), stage_rows=stage_counts, episodes=len(episodes))

    def runtime(self, source):
        try:
            report = mapping(self.read_json(source))
        except (OSError, ValueError) as exc:
            self.error('panel_runtime_readable', exc)
            return
        self.require('runtime_panel_contract', report.get('contract_version') == PANEL_CONTRACT)
        super().runtime(source)

    def finish(self):
        result = super().finish()
        result['metrics']['physical_pose_families_seen'] = [name for name, poses in self.modality_pose_tables.items() if poses]
        return result


def select_audit(path, mode, contract):
    if contract not in ('auto', 'legacy', 'panel'):
        raise ValueError('Unknown inspection contract: '+str(contract))
    if contract == 'auto':
        try:
            text = Path(path).read_text(encoding='utf-8-sig')
            raw = json.loads(next(line for line in text.splitlines() if line.strip())) if mode == 'manifest' else json.loads(text)
            contract = 'panel' if mapping(raw).get('view_recipe') == PANEL_VIEW_RECIPE else 'legacy'
        except (OSError, ValueError, StopIteration):
            contract = 'legacy'  # The regular reader reports the malformed input.
    return PanelAudit(path, mode) if contract == 'panel' else Audit(path, mode)


def verify_manifest(path, contract='auto'):
    audit = select_audit(path, 'manifest', contract)
    audit.manifest(path)
    return audit.finish()


def verify_runtime(path, contract='auto'):
    audit = select_audit(path, 'runtime', contract)
    audit.runtime(path)
    return audit.finish()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument('--manifest', type=Path, help='Offline JSONL, including each batch and its relative sensor PNG paths.')
    source.add_argument('--runtime', type=Path, help='Live process_runtime.json; checks telemetry only, no PNGs.')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--contract', choices=('auto', 'legacy', 'panel'), default='auto', help='Select the strict legacy thermal or four-stage panel path; auto detects the recorded view recipe.')
    args = parser.parse_args()
    selected = args.manifest if args.manifest is not None else args.runtime
    if args.output.resolve() == selected.resolve():
        parser.error('--output must not overwrite the input report or manifest')
    try:
        result = verify_manifest(args.manifest, args.contract) if args.manifest is not None else verify_runtime(args.runtime, args.contract)
    except ImportError as exc:
        parser.error('Pillow is required for manifest PNG checks; use the project QA Python environment. '+str(exc))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2, allow_nan=False)+'\n', encoding='utf-8', newline='\n')
    print(json.dumps({'passed': result['passed'], 'checks': result['checks_count'],
                      'failed_checks': result['failed_checks'], 'errors': result['errors'],
                      'report': str(args.output.resolve())}, indent=2))
    return 0 if result['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
