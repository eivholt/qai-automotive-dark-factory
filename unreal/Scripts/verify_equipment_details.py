"""Editor-rendered actual sleeve/collar and gripper closeups; private outputs."""
import json
from pathlib import Path
import time
import traceback
import unreal

out=Path(unreal.Paths.project_dir()).resolve().parent/'artifacts/equipment-details-closeups'
out.mkdir(parents=True,exist_ok=True)
editor=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
levels=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
state=0;due=time.monotonic()+15;busy=False;report={}

def tick(dt):
    global state,due,busy
    if busy or time.monotonic()<due:return
    busy=True
    try:
        actors=unreal.GameplayStatics.get_all_actors_of_class(editor.get_editor_world(),unreal.Actor)
        parts=[p for a in actors for p in a.get_components_by_class(unreal.StaticMeshComponent)]
        if state==0:
            levels.editor_set_viewport_realtime(False);levels.editor_set_viewport_realtime(True);levels.editor_set_game_view(True)
            sleeves=[p for p in parts if p.get_name().startswith('Hose_')]
            collars=[p for p in parts if p.get_name().startswith('HoseCollar_')]
            report['sleeves']=len(sleeves);report['collars']=len(collars)
            report['sleeve_assets']=list(set(p.get_editor_property('static_mesh').get_name() for p in sleeves))
            if len(sleeves)!=20 or len(collars)!=24 or report['sleeve_assets']!=['SM_HoseSleeve']:raise RuntimeError('Missing generated hose geometry')
        elif state in (1,3):
            candidates=[p for p in parts if p.get_name().startswith('Hose_') and p.get_name().endswith('_2')] if state==1 else [p for p in parts if p.get_name()=='PanelGripperStem']
            center,extent,radius=unreal.SystemLibrary.get_component_bounds(candidates[0])
            location=center+(unreal.Vector(70,-115,55) if state==1 else unreal.Vector(120,-125,65))
            editor.set_level_viewport_camera_info(location,unreal.MathLibrary.find_look_at_rotation(location,center))
        elif state in (2,4):
            unreal.AutomationLibrary.take_high_res_screenshot(1600,900,str(out/('hose-sleeves.png' if state==2 else 'gripper-pads.png')))
        else:
            report['passed']=True;(out/'verification.json').write_text(json.dumps(report,indent=2))
            unreal.unregister_slate_post_tick_callback(handle);unreal.SystemLibrary.quit_editor()
        state+=1;due=time.monotonic()+5
    except Exception:
        report['error']=traceback.format_exc();(out/'verification.json').write_text(json.dumps(report,indent=2))
        unreal.unregister_slate_post_tick_callback(handle);unreal.log_error(report['error']);unreal.SystemLibrary.quit_editor()
    finally:busy=False

handle=unreal.register_slate_post_tick_callback(tick)
