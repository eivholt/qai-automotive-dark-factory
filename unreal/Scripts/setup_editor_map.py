"""Create/update the current panel demo entry map, without saving runtime copies.

The editor-only preview regenerates through the production scene builders when
the map opens. Explicit launcher flags still select legacy and offline fixtures.
"""
import json
from pathlib import Path
import unreal
import df_authoring_config

MAP = '/Game/Maps/DarkFactoryPanel'
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
assets = unreal.EditorAssetLibrary
if assets.does_asset_exist(MAP):
    if not levels.load_level(MAP):
        raise RuntimeError(f'Cannot load {MAP}')
elif not levels.new_level(MAP):
    raise RuntimeError(f'Cannot create {MAP}')

world = editor.get_editor_world()
mode = unreal.load_class(None, '/Script/DarkFactory.DarkFactoryPanelGameMode')
preview_class = unreal.load_class(None, '/Script/DarkFactory.DarkFactoryEditorPreview')
if mode is None or preview_class is None:
    raise RuntimeError('Build the latest DarkFactory editor module first')
world.get_world_settings().set_editor_property('default_game_mode', mode)
generators = [a for a in actors.get_all_level_actors() if isinstance(a, unreal.DarkFactoryEditorPreview)]
if len(generators) > 1:
    raise RuntimeError('Multiple preview generators found; keep one DarkFactoryEditorPreview')
preview = generators[0] if generators else actors.spawn_actor_from_class(preview_class, unreal.Vector())
preview.set_actor_label('Dark Factory - current panel assembly preview')
preview.set_folder_path('DarkFactory')
recipe = df_authoring_config.load(Path(unreal.Paths.project_dir()) / 'Config/Authoring/preview.json')
poses = (unreal.DarkFactoryEditorPose.AWAITING_PANEL,
         unreal.DarkFactoryEditorPose.SEALANT_APPLIED,
         unreal.DarkFactoryEditorPose.PANEL_PLACED)
preview.set_editor_property('preview_pose', poses[df_authoring_config.POSES.index(recipe['pose'])])

# Save an immediately useful viewport, independent of the user's Play camera.
location = unreal.Vector(-1050, -1450, 800)
rotation = unreal.Rotator(pitch=-20, yaw=55, roll=0)
manifest = Path(__file__).resolve().parents[2] / 'assets/derived/welding_cell/scene.json'
if manifest.exists():
    observer = json.loads(manifest.read_text(encoding='utf-8-sig'))['observer']
    location = unreal.Vector(*observer['location_cm'])
    target = unreal.Vector(*observer['look_at_cm'])
    rotation = unreal.MathLibrary.find_look_at_rotation(location, target)
if recipe['viewport'] is not None:
    location = unreal.Vector(*recipe['viewport']['location_cm'])
    pitch, yaw, roll = recipe['viewport']['rotation_deg']
    rotation = unreal.Rotator(pitch=pitch, yaw=yaw, roll=roll)
editor.set_level_viewport_camera_info(location, rotation)
# Commandlet generation has no live viewport client. Persist its initial view
# in the map as well (LVT_Perspective is index 3), preserving later user views.
preview.initialize_editor_view(location, rotation)
if not unreal.EditorLoadingAndSavingUtils.save_map(world, MAP):
    raise RuntimeError(f'Cannot save {MAP}')
unreal.log(f'DF_EDITOR_MAP_READY {MAP}: current panel game mode and rebuildable preview')
