"""Editor close-ups of the actual hose and tool meshes, with finish A/B captures.

Run via -ExecCmds="py <absolute path>". Does not persist scene/recipe changes.
"""
import json
from pathlib import Path
import time
import traceback
import unreal

out = Path(unreal.Paths.project_dir()).resolve().parent / 'artifacts/industrial-finish-closeups'
out.mkdir(parents=True, exist_ok=True)
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
state = 0
due = time.monotonic() + 15
report = {}
busy = False

def tick(dt):
    global state, due, busy
    if busy or time.monotonic() < due: return
    busy = True
    try:
        world = editor.get_editor_world()
        actors = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Actor)
        parts = [c for a in actors for c in a.get_components_by_class(unreal.StaticMeshComponent)]
        if state == 0:
            levels.editor_set_viewport_realtime(False)
            levels.editor_set_viewport_realtime(True)
            levels.editor_set_game_view(True)
            report['parts'] = {c.get_name():str(c.get_world_location()) for c in parts if 'Hose_' in c.get_name() or 'Gripper' in c.get_name()}
        elif state in (1,4):
            candidates = [c for c in parts if ('Hose_' in c.get_name() and c.get_name().endswith('_2'))] if state == 1 else [c for c in parts if c.get_name() == 'PanelGripperStem']
            if not candidates: raise RuntimeError('Target mesh not found')
            part = candidates[0]
            center, extent, radius = unreal.SystemLibrary.get_component_bounds(part)
            offset = unreal.Vector(110,-145,75) if state == 1 else unreal.Vector(140,-130,80)
            location = center + offset
            editor.set_level_viewport_camera_info(location, unreal.MathLibrary.find_look_at_rotation(location,center))
            for a in actors:
                if isinstance(a,unreal.PostProcessVolume):
                    settings = a.get_editor_property('settings')
                    settings.set_editor_property('override_user_flags',True)
                    settings.set_editor_property('user_flags',0)
                    a.set_editor_property('settings',settings)
        elif state in (2,5):
            unreal.AutomationLibrary.take_high_res_screenshot(1600,900,str(out / ('hoses-before.png' if state == 2 else 'gripper-before.png')))
        elif state in (3,6):
            for a in actors:
                if isinstance(a,unreal.PostProcessVolume):
                    settings = a.get_editor_property('settings')
                    settings.set_editor_property('user_flags',256)
                    a.set_editor_property('settings',settings)
            # Capture after temporal lighting settles on a later tick.
            state += 10
            due = time.monotonic()+5
            return
        elif state in (13,16):
            unreal.AutomationLibrary.take_high_res_screenshot(1600,900,str(out / ('hoses-after.png' if state == 13 else 'gripper-after.png')))
            state -= 10
        elif state == 7:
            report['passed'] = True
            (out/'verification.json').write_text(json.dumps(report,indent=2))
            unreal.unregister_slate_post_tick_callback(handle)
            unreal.SystemLibrary.quit_editor()
        state += 1
        due = time.monotonic()+5
    except Exception:
        report['error'] = traceback.format_exc()
        (out/'verification.json').write_text(json.dumps(report,indent=2))
        unreal.unregister_slate_post_tick_callback(handle)
        unreal.log_error(report['error'])
    finally:
        busy = False

handle = unreal.register_slate_post_tick_callback(tick)
