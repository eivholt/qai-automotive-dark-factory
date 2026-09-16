# Project authoring structure

The production C++ builders and asset preparation scripts own the scene. The
editor preview and Play consume those same builders. The editor-only
DarkFactoryAuthoring plugin provides an MCP façade with a tracked preview recipe;
it does not turn generated actors into independently authored runtime content.

## What is persistent

`unreal/Config/Authoring/preview.json` stores a validated pose and optional editor
viewport bookmark. It is loaded when the preview generator changes (including
map reload), through `apply_saved_preview`, and during `setup_editor_map.py`.
The pose never selects the live body's branch. A null viewport preserves normal
editor navigation; map generation uses the imported overview as its fallback.
The three supported poses are AwaitingPanel, SealantApplied and PanelPlaced.

`unreal/Plugins/DarkFactoryAuthoring/Content/Python` owns tool registration,
recipe validation and editor application. It is enabled only for Editor targets.
Recipe writes validate first and atomically replace one fixed JSON file. There
is no generic file-write, generated-transform export or model-policy tool.

Use `save_preview_pose`, `save_viewport_bookmark`, `apply_saved_preview`,
`inspect_preview` and `authoring_sources` through the
`df_authoring.DarkFactoryAuthoringTools` toolset. Apply/inspect checks one
generator, ten generated actors, nonempty meshes, zero preview captures and zero
simulated mesh bodies. These checks do not establish model accuracy or collision
clearance. Mutating tools reject Play mode and other maps.

## Where lasting scene changes belong

Paths below are relative to the repository root.

| Change | Authoritative source | Rebuild / verification |
| --- | --- | --- |
| Imported islands, material assignments, joints | `scripts/assets/prepare_welding_cell.py` | `prepare_welding_cell.ps1`, `unreal/Scripts/import_welding_cell.ps1`; welding checks |
| Panel contact geometry and mounting frame | `scripts/assets/prepare_panel_assembly.py`, `prepare_panel_frame.py`; `unreal/Source/DarkFactory/DarkFactoryProcessScene.cpp` | Prepare/import current panel assets; frame, coverage, thermal and model integration checks |
| Pickup, sealant and pinch trajectories | `scripts/assets/prepare_panel_motion.py`; `unreal/Source/DarkFactory/DarkFactoryWeldingCell.cpp` | Motion preparation and PanelMotionSmoke/pinch/camera checks |
| Cabinet, EVK and environment details | Corresponding `DarkFactoryControlCabinet`, `DarkFactoryIQ9Props`, `DarkFactoryEnvironmentDetails` C++ classes and their preparation scripts | Relevant cabinet/prop/realism fixtures |
| Display materials and thermal fields | `unreal/Shaders` and associated `unreal/Scripts/setup_*.py` | Regenerate materials; modality and sensor-parity fixtures |
| Optional industrial finishes | `scripts/assets/prepare_industrial_materials.py`, `unreal/Shaders/IndustrialSurface.usf`, `unreal/Scripts/setup_industrial_materials.py` | Prepare private textures, rebuild materials; graphics parity, welding and close-up checks |
| Gates, capture and inference | `unreal/Source/DarkFactory` controller/capture code and `src` host code | Protocol, transport, capture and actual model integration tests |
| Preview composition and cleanup | `unreal/Source/DarkFactory/DarkFactoryEditorPreview.cpp`, panel game mode | Editor load/refresh/reload/Play/Stop validation |

For a geometry edit: inspect through MCP, change the owning source, run its
preparation/build/import steps, then apply the saved preview and inspect the
result. Review the source diff and rendered screenshots. Moving a generated
child with generic MCP tools is only a temporary experiment; a refresh discards
it. Arbitrary robot transforms cannot safely be persisted without coordinated
joint, tool, sensor and thermal changes, so this façade intentionally does not
offer that operation.

Generated Unreal packages, purchased sources, renders and datasets stay private.
Source scripts, the authoring plugin, recipe and documentation remain in Git.
The Reason2 endpoint and raw sensor contract are independent of this editor MCP
connection. Keep the existing AGENTS.md validation requirements for each change.
