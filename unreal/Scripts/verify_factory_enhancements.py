"""Rendered observer-only hall audit. Launch with -DFEnhancements -ExecCmds=py ..."""
import json
from pathlib import Path
import time
import traceback
import unreal

out=Path(unreal.Paths.project_dir()).resolve().parent/'artifacts/factory-enhancements-closeups'
out.mkdir(parents=True,exist_ok=True)
editor=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
levels=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
state=0; due=time.monotonic()+15; busy=False; report={}
views=[((970,-1130,610),(0,-40,320),'assembly-hall.png'),((2100,-2800,1050),(0,100,570),'factory-wide.png'),((1400,-1850,300),(1850,-1400,160),'isaac-racks.png')]

def tick(dt):
    global state,due,busy
    if busy or time.monotonic()<due: return
    busy=True
    try:
        if state==0:
            levels.editor_set_viewport_realtime(False);levels.editor_set_viewport_realtime(True);levels.editor_set_game_view(True)
            actors=unreal.GameplayStatics.get_all_actors_of_class(editor.get_editor_world(),unreal.Actor)
            parts=[p for a in actors for p in a.get_components_by_class(unreal.InstancedStaticMeshComponent) if p.get_name().startswith('SM_Context')]
            report['context_batches']=[{'name':p.get_name(),'instances':p.get_instance_count(),'asset':p.get_editor_property('static_mesh').get_path_name()} for p in parts]
            if len(parts)!=5: raise RuntimeError('Expected five context ISM batches')
        elif state<=6:
            location,target,name=views[(state-1)//2]
            if state%2:
                v=unreal.Vector(*location)
                editor.set_level_viewport_camera_info(v,unreal.MathLibrary.find_look_at_rotation(v,unreal.Vector(*target)))
            else: unreal.AutomationLibrary.take_high_res_screenshot(1600,900,str(out/name))
        else:
            report['passed']=True
            (out/'verification.json').write_text(json.dumps(report,indent=2))
            unreal.unregister_slate_post_tick_callback(handle);unreal.SystemLibrary.quit_editor()
        state+=1;due=time.monotonic()+5
    except Exception:
        report['error']=traceback.format_exc();(out/'verification.json').write_text(json.dumps(report,indent=2))
        unreal.unregister_slate_post_tick_callback(handle);unreal.log_error(report['error'])
    finally: busy=False

handle=unreal.register_slate_post_tick_callback(tick)
