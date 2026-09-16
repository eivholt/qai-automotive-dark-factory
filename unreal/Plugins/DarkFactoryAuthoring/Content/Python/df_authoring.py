"""Editor-only façade over the production preview; no generic child-transform persistence."""
import json
import time
from pathlib import Path

import unreal
import toolset_registry
from toolset_registry.registration import Registration
import df_authoring_config as config

MAP = "/Game/Maps/DarkFactoryPanel"
ROOT = Path(unreal.Paths.project_dir()).resolve()
RECIPE = ROOT / "Config/Authoring/preview.json"
_last_preview = None
_tick_handle = None
_next_check = 0.0


def _preview():
    levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if levels.is_in_play_in_editor():
        raise RuntimeError("Stop Play before authoring the preview")
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    if world is None or world.get_outermost().get_name() != MAP:
        raise RuntimeError(f"Open {MAP} before authoring")
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
    matches = [a for a in actors if isinstance(a, unreal.DarkFactoryEditorPreview)]
    if len(matches) != 1:
        raise RuntimeError(f"Expected one preview generator, found {len(matches)}")
    return matches[0]


def _apply(value, preview):
    enum = (unreal.DarkFactoryEditorPose.AWAITING_PANEL,
            unreal.DarkFactoryEditorPose.SEALANT_APPLIED,
            unreal.DarkFactoryEditorPose.PANEL_PLACED)[config.POSES.index(value["pose"])]
    if preview.get_editor_property("preview_pose") != enum:
        preview.set_editor_property("preview_pose", enum)
    if not preview.refresh_preview():
        raise RuntimeError(preview.get_editor_property("preview_status"))
    view = value["viewport"]
    if view is not None:
        pitch, yaw, roll = view["rotation_deg"]
        unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).set_level_viewport_camera_info(
            unreal.Vector(*view["location_cm"]), unreal.Rotator(pitch=pitch, yaw=yaw, roll=roll))
    return _inspect(preview)


def _inspect(preview):
    report = json.loads(preview.get_preview_report())
    # EditorActorSubsystem filters transient generated actors; query the world.
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    actors = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Actor)
    tagged = [a for a in actors if a.actor_has_tag("DarkFactory.GeneratedEditorPreview")]
    report["tagged_actors"] = len(tagged)
    report["valid"] = (report["actors"] == 10 and report["mesh_components"] > 0
                       and report["scene_captures"] == 0 and report["simulated_bodies"] == 0
                       and len(tagged) == report["actors"])
    report["pose"] = config.POSES[int(preview.get_editor_property("preview_pose").value)]
    report["recipe"] = config.load(RECIPE)
    report["recipe_path"] = str(RECIPE)
    report["model_accuracy_evidence"] = False
    if not report["valid"]:
        raise RuntimeError("Invalid preview: " + json.dumps(report))
    return json.dumps(report)


@unreal.uclass()
class DarkFactoryAuthoringTools(unreal.ToolsetDefinition):
    """Durable editor preview recipes, rebuild/validation and source ownership.

    Geometry and motion remain owned by production generators. Never persist
    generated child transforms or treat preview state as model evidence.
    """

    @toolset_registry.tool_call
    @staticmethod
    def inspect_preview() -> str:
        """Validate the current preview and return counts, pose and saved recipe as JSON."""
        return _inspect(_preview())

    @toolset_registry.tool_call
    @staticmethod
    def apply_saved_preview() -> str:
        """Rebuild from the checked-in editor recipe after source changes or asset import.

        Does not reimport assets, compile C++, save packages or modify model state.
        """
        return _apply(config.load(RECIPE), _preview())

    @toolset_registry.tool_call
    @staticmethod
    def save_preview_pose(pose: str) -> str:
        """Apply and persist an editor-only pose in Config/Authoring/preview.json.

        Args:
            pose: AwaitingPanel, SealantApplied or PanelPlaced. Never a live process branch.
        """
        preview = _preview()
        old = config.load(RECIPE)
        value = dict(old, pose=pose)
        config.validate(value)
        try:
            _apply(value, preview)
            config.save(RECIPE, value)
        except Exception:
            _apply(old, preview)
            raise
        return _inspect(preview)

    @toolset_registry.tool_call
    @staticmethod
    def save_viewport_bookmark() -> str:
        """Persist the current observer editor viewport in the recipe; sensors are unchanged."""
        preview = _preview()
        location, rotation = unreal.get_editor_subsystem(
            unreal.UnrealEditorSubsystem).get_level_viewport_camera_info()
        value = config.load(RECIPE)
        value["viewport"] = {"location_cm": [location.x, location.y, location.z],
                             "rotation_deg": [rotation.pitch, rotation.yaw, rotation.roll]}
        config.save(RECIPE, value)
        return _inspect(preview)

    @toolset_registry.tool_call
    @staticmethod
    def authoring_sources() -> str:
        """Return the tracked ownership guide before editing geometry, motions or materials."""
        return (ROOT.parent / "docs/authoring_structure.md").read_text(encoding="utf-8")


_registration = Registration([DarkFactoryAuthoringTools])


def _on_tick(_seconds):
    global _last_preview, _next_check
    if time.monotonic() < _next_check:
        return
    _next_check = time.monotonic() + 0.5
    # Identity, not actor path: loading the same map creates a new generator.
    try:
        preview = _preview()
    except RuntimeError:
        _last_preview = None
        return
    if preview == _last_preview:
        return
    _last_preview = preview
    try:
        _apply(config.load(RECIPE), preview)
        unreal.log("DF_AUTHORING_RECIPE_APPLIED")
    except Exception as error:
        unreal.log_error(f"DF_AUTHORING_RECIPE_FAILED: {error}")


def register():
    global _tick_handle
    _registration.register()
    if _tick_handle is None:
        _tick_handle = unreal.register_slate_post_tick_callback(_on_tick)
