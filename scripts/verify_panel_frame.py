"""Check source-frame support and the actual rendered top-down placement path."""
import argparse
import json
import math
from pathlib import Path


def verify(frame_path, motion_path):
    frame = json.loads(frame_path.read_text(encoding='utf-8-sig'))
    motion = json.loads(motion_path.read_text(encoding='utf-8-sig'))
    failures, checks = [], []

    def check(name, passed):
        checks.append(name)
        if not passed:
            failures.append(name)

    check('source geometry unchanged', frame['source_ownership_unchanged'])
    check('nonempty frame and opening', 0 < frame['retained_surface_area_cm2'] < frame['original_surface_area_cm2'])
    check('clipped surface area conserved', abs(frame['retained_surface_area_cm2'] + frame['removed_surface_area_cm2'] - frame['original_surface_area_cm2']) < 1.e-5)
    check('all 82 sealant samples supported', frame['support_samples'] == 82 and frame['max_sealant_support_gap_cm'] < .002)
    check('rendered fixture completed', motion['passed'] and motion['model_http_requests'] == 0)
    samples = {s['name']: s for s in motion['samples']}
    for name in ('permanent_frame_empty', 'frame_with_sealant', 'panel_above_frame', 'panel_lowering', 'panel_on_frame', 'preassembled_presence'):
        sample = samples[name]
        check(name+' frame visible', sample['scene']['mounting_frame_visible'])
        check(name+' screenshot exists', (motion_path.parent/sample['observer_screenshot']).is_file())
    check('empty frame does not change ground truth', not samples['permanent_frame_empty']['scene']['panel_present'])
    check('placed panel present', samples['panel_on_frame']['scene']['panel_present'])
    final = samples['panel_on_frame']['arm_state']['held_panel_location_cm']
    above = samples['panel_above_frame']['arm_state']['held_panel_location_cm']
    check('90 cm hover reached', abs(above[2]-final[2]-90) < .02 and math.dist(above[:2], final[:2]) < .02)
    placement = [s for s in motion['motion_samples'] if s['body_sequence'] == 1 and s['phase'] == samples['panel_on_frame']['phase'] and s['stage_seconds'] >= 4.75]
    previous_z = above[2]+.01
    for s in placement:
        pos = s['physical_arm_state']['held_panel_location_cm']
        check('panel stays above frame until seated', pos[2] >= final[2]-.02)
        check('placement descends monotonically', pos[2] <= previous_z+.02)
        check('placement remains over opening', math.dist(pos[:2], final[:2]) < 3.)
        previous_z = pos[2]
    return dict(passed=not failures, checks=len(checks), failures=failures,
                scope='Source support and sampled native joint motion; not continuous collision certification or model accuracy')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--frame', type=Path, required=True)
    parser.add_argument('--motion', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    result = verify(args.frame, args.motion)
    args.output.write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(result, indent=2))
    raise SystemExit(0 if result['passed'] else 1)
