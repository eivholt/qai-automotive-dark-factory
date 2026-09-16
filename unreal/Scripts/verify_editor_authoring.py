"""Rendered editor authoring lifecycle fixture. Run with -ExecCmds="py <path>".

Writes private evidence under artifacts/unreal-mcp-authoring. Restores the input
recipe on completion/failure. Does not change model gates or measure accuracy.
"""
import json
from pathlib import Path
import time
import traceback

import unreal
import df_authoring as authoring
import df_authoring_config as config

OUT = Path(unreal.Paths.project_dir()).resolve().parent / 'artifacts/unreal-mcp-authoring'
OUT.mkdir(parents=True, exist_ok=True)
original = config.load(authoring.RECIPE)
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
report = {'passed': False, 'checks': [], 'model_accuracy_evidence': False}
state = 0
due = time.monotonic() + 10
busy = False
handle = None


def require(condition, name):
    report['checks'].append({'name': name, 'passed': bool(condition)})
    if not condition:
        raise RuntimeError(name)


def inspect():
    return json.loads(authoring.DarkFactoryAuthoringTools.inspect_preview())


def capture(name):
    unreal.AutomationLibrary.take_high_res_screenshot(1600, 900, str(OUT / name))


def tick(_dt):
    global state, due, busy
    if busy or time.monotonic() < due:
        return
    busy = True
    try:
        if state == 0:
            baseline = inspect()
            require(baseline['valid'], 'startup preview valid')
            levels.editor_set_viewport_realtime(False)
            levels.editor_set_viewport_realtime(True)
            levels.editor_set_game_view(True)
            # Deterministic overview for image review, independent of restored UI state.
            scene = json.loads((OUT.parent.parent / 'assets/derived/welding_cell/scene.json').read_text(encoding='utf-8-sig'))
            observer = scene['observer']
            location = unreal.Vector(*observer['location_cm'])
            rotation = unreal.MathLibrary.find_look_at_rotation(location, unreal.Vector(*observer['look_at_cm']))
            editor.set_level_viewport_camera_info(location, rotation)
            authoring.DarkFactoryAuthoringTools.save_preview_pose('AwaitingPanel')
            require(inspect()['recipe']['pose'] == 'AwaitingPanel', 'awaiting pose persisted')
        elif state == 1:
            capture('awaiting-panel.png')
        elif state == 2:
            authoring.DarkFactoryAuthoringTools.save_preview_pose('SealantApplied')
            require(inspect()['pose'] == 'SealantApplied', 'sealant pose applied')
            require(config.load(authoring.RECIPE)['pose'] == 'SealantApplied', 'sealant pose on disk')
        elif state == 3:
            capture('sealant-applied.png')
        elif state == 4:
            authoring.DarkFactoryAuthoringTools.save_preview_pose('PanelPlaced')
            before = inspect()
            authoring.DarkFactoryAuthoringTools.apply_saved_preview()
            require(inspect() == before, 'reapply is idempotent')
            authoring.DarkFactoryAuthoringTools.save_viewport_bookmark()
            require(config.load(authoring.RECIPE)['viewport'] is not None, 'viewport persisted')
            expected_location, expected_rotation = editor.get_level_viewport_camera_info()
            authoring.DarkFactoryAuthoringTools.apply_saved_preview()
            actual_location, actual_rotation = editor.get_level_viewport_camera_info()
            require(all(abs(getattr(actual_location, axis) - getattr(expected_location, axis)) < .001
                        for axis in ('x', 'y', 'z')), 'bookmark location roundtrip')
            require(all(abs(getattr(actual_rotation, axis) - getattr(expected_rotation, axis)) < .001
                        for axis in ('pitch', 'yaw', 'roll')), 'bookmark rotation roundtrip')
        elif state == 5:
            capture('panel-placed.png')
        elif state == 6:
            # Screenshot completion is deferred to a render tick; never reload
            # the world in the same tick that requests the screenshot.
            # Save a different marker pose to prove JSON is authoritative on reload.
            p = authoring._preview()
            p.set_editor_property('preview_pose', unreal.DarkFactoryEditorPose.AWAITING_PANEL)
            require(unreal.EditorLoadingAndSavingUtils.save_map(editor.get_editor_world(), authoring.MAP), 'save generator map')
            levels.editor_set_viewport_realtime(False)
            require(levels.load_level(authoring.MAP), 'reload entry map')
        elif state == 7:
            require(inspect()['pose'] == 'PanelPlaced', 'recipe overrides old map pose with realtime off')
            require(inspect()['valid'], 'reload no duplicates or sensors')
            levels.editor_set_viewport_realtime(True)
            levels.editor_request_begin_play()
        elif state == 8:
            require(levels.is_in_play_in_editor(), 'PIE started')
            try:
                authoring._preview()
            except RuntimeError:
                require(True, 'authoring rejected during PIE')
            else:
                require(False, 'authoring rejected during PIE')
            actors = unreal.GameplayStatics.get_all_actors_of_class(editor.get_game_world(), unreal.Actor)
            require(not any(a.actor_has_tag('DarkFactory.GeneratedEditorPreview') for a in actors), 'no preview children in PIE')
            require(sum(isinstance(a, unreal.DarkFactoryEditorPreview) for a in actors) == 0, 'no generator in PIE')
            require(sum(a.get_class().get_name() == 'DarkFactoryProcessController' for a in actors) == 1, 'one runtime process controller')
            levels.editor_request_end_play()
        elif state == 9:
            require(not levels.is_in_play_in_editor(), 'PIE stopped')
            require(inspect()['pose'] == 'PanelPlaced', 'recipe restored after Stop')
            require(inspect()['valid'], 'preview valid after Stop')
            config.save(authoring.RECIPE, original)
            authoring.DarkFactoryAuthoringTools.apply_saved_preview()
            require(unreal.EditorLoadingAndSavingUtils.save_map(editor.get_editor_world(), authoring.MAP), 'restore generator map')
            report['passed'] = True
            report['final_preview'] = inspect()
            (OUT / 'verification.json').write_text(json.dumps(report, indent=2))
            unreal.unregister_slate_post_tick_callback(handle)
            unreal.log('DF_AUTHORING_QA_PASSED')
        state += 1
        due = time.monotonic() + (10 if state == 8 else 5)
    except Exception:
        report['error'] = traceback.format_exc()
        config.save(authoring.RECIPE, original)
        if levels.is_in_play_in_editor():
            levels.editor_request_end_play()
        (OUT / 'verification.json').write_text(json.dumps(report, indent=2))
        unreal.unregister_slate_post_tick_callback(handle)
        unreal.log_error(report['error'])
    finally:
        busy = False


handle = unreal.register_slate_post_tick_callback(tick)
