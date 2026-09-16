# Unreal Editor workflow

Open `unreal/DarkFactory.uproject` with the installed Unreal Engine 5.8.
`/Game/Maps/DarkFactoryPanel` is the startup and default game map. It shows the
current panel assembly cell before Play, including the mounting frame, relocated
stations, panel rack, imported camera, pinch welder, sealant dispenser and EVKs.

The old `/Game/Maps/DarkFactory` map is an empty legacy entry point: it builds its
scene only when Play begins. Use **File > Open Level** or the Content Browser to
open **DarkFactoryPanel** if the editor restores that old map from a prior session.

## Preview and edit

The Outliner contains **Dark Factory - current panel assembly preview**. Select
it to find **Preview Pose** and **Refresh Preview** in Details. The three poses
show a body awaiting its panel, sealant applied, and the panel placed. Refresh
after reimporting local assets. The preview uses the production geometry and
pose builders with a frozen illustrative clock; it performs no inference,
sensor capture or physics simulation. Cabinet preview text is not a model result.

The generated actors are transient. Only the preview generator is saved in the
map. The tracked `unreal/Config/Authoring/preview.json` recipe owns the initial
pose and optional viewport bookmark, applied on map load and after Play/Stop.
Use the DarkFactoryAuthoring MCP tools to save changes to that recipe; changing
the Details pose alone is a temporary preview. This keeps old copies from
accumulating after updates, but means manual edits to generated children do not
survive regeneration. Make lasting geometry changes in the scene preparation
scripts/builders, rebuild/reimport, then apply the saved preview. See the
[authoring source map](authoring_structure.md). Ordinary editor viewport
navigation and G to hide editor icons work.

## Play

Press **Play** for the current model-gated panel process in dark observer mode,
with the headlamp off. Tab / Xbox Y cycles dark, NIR, thermal, then roof-lit
factory. L enables the headlamp in dark mode. Shift+F1
releases the mouse to the editor; the toolbar's Stop ends Play.

The current map's game mode enables the process before the runtime world starts.
It removes generated preview actors before BeginPlay, so they cannot duplicate
geometry, enter physics or appear in model captures. Stop returns to the saved
preview. The preview pose does not choose the live body's state or model branch.

Run the compatible model host on `http://127.0.0.1:18191` before playing; see
[host startup](../README.md#run-the-panel-demo-on-a-prepared-installation). The
editor uses `/v1/inspect` just like the launcher. An unavailable host holds at
the camera gate. It never advances using preview state, ground truth or a timer.

The launcher explicitly selects its scene, so its existing behavior remains:
`-Scene welding -Process` runs assembly; `-Scene welding` runs the separate
legacy visual cycle; `-Scene procedural` runs the procedural reference. Offline
fixtures retain their explicit scene/capture settings.

## Generate or repair the map

Generated Unreal content and purchased assets remain private and ignored by Git.
A code-only checkout first needs the [local asset preparation/import](../README.md#import-the-local-welding-cell).
With the editor closed, rebuild the module and generated content:

```powershell
.\unreal\Scripts\build.ps1
```

For only the entry map, after compiling the latest module:

```powershell
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' `
  "$PWD\unreal\DarkFactory.uproject" -run=pythonscript `
  "-script=$PWD\unreal\Scripts\setup_editor_map.py" -Unattended -NullRHI -NoSound
```

`setup_content.py` includes this map generator. It sets the map's game-mode
override, retains one preview actor and saves an initial overview camera. The
map is saved locally without checking ignored generated packages into revision
control. If local assets are missing, **Preview Status** explains what must be
prepared; this does not silently substitute the old procedural scene.

## Local Unreal MCP connection

The project enables UE 5.8's bundled `ModelContextProtocol` and the editor,
automation-test, Slate inspector, Niagara and physics toolsets through the
Editor-only DarkFactoryAuthoring plugin. `AllToolsets` is explicitly disabled:
its unrelated Game Features dependency requires asset-manager setup and caused
our content-generation commandlet to fail. These experimental tools provide
live editor inspection and authoring, separate from the Reason2 service.

On the prepared workstation, local Editor Preferences enable automatic startup
at `http://127.0.0.1:8000/mcp`. These preferences live in ignored
`Saved/Config/WindowsEditor/EditorPerProjectUserSettings.ini`, not shared runtime
configuration. On another workstation, enable **Auto Start Server** under
**Editor Preferences > General > Model Context Protocol**, or execute
`ModelContextProtocol.StartServer` in the editor console.

The local Codex user configuration contains:

```toml
[mcp_servers.unreal]
url = "http://127.0.0.1:8000/mcp"
```

Keep the editor open while using this connection. Restart/reconnect the client
if the new tools do not appear. Discovery uses `list_toolsets`,
`describe_toolset`, and `call_tool`; three top-level tools is expected.
Changes to generated preview actors remain temporary: record durable changes
in the scene generators/configuration and refresh the preview. MCP viewport
images are not model sensor inputs or evidence of sensor parity.

The project-specific `df_authoring.DarkFactoryAuthoringTools` toolset exposes
`inspect_preview`, `apply_saved_preview`, `save_preview_pose`,
`save_viewport_bookmark` and `authoring_sources`. These methods operate only on
the current panel preview, reject Play mode and preserve the production builders
as the geometry authority. Use the source map before editing any generated actor.

For the rendered authoring lifecycle fixture, launch the editor with
`-ExecCmds="py <absolute repo path>/unreal/Scripts/verify_editor_authoring.py"`.
It checks pose persistence, refresh/reload with realtime disabled, viewport
bookmarking and Play/Stop isolation, restores the starting recipe and leaves
the editor open without Play. Its screenshots and report stay in ignored
`artifacts/unreal-mcp-authoring`. It is not an accuracy evaluation.

## Verification on the prepared workstation

The subsequent persistent-authoring revision passed 21 rendered lifecycle checks
and all 121 Python tests. Live MCP calls exercised inspection, pose persistence,
invalid-pose rejection and restoration. The recipe was restored to AwaitingPanel
with no bookmark. See `artifacts/unreal-mcp-authoring/verification.json` and the
[current validation summary](validation.md). The earlier scene/runtime results
below retain their original scope and test counts.

The UE 5.8 build and map-generation commandlet passed. Rendered lifecycle QA
checked automatic construction, refresh, save/reload with realtime disabled,
Game View visibility and two Play/Stop cycles. The preview contained 10 actors,
356 mesh components, no scene captures and no simulated bodies. Each Play
session contained one runtime cell/controller and no preview actors.

Live PIE selected assembly from two fresh NIR responses. The separate actual
ProcessSmoke completed assembly / preassembled / assembly in 179 seconds, using
23 model requests and passing all six thermal acceptance audits. Inspection-arm
telemetry passed 4,472 checks and live curtain telemetry passed 27. These are
integration checks with the existing adapters, not new model-accuracy estimates.

All 116 Python tests, 34 launcher-policy checks and nine legacy welding suites
passed. All three rendered preview poses, the reflected Refresh Preview action
and the separate native ProcessPolicySmoke also passed. Private evidence and screenshots are in `artifacts/editor-workflow-v1`,
`artifacts/editor-process-v1` and `artifacts/editor-legacy-v1`. Early failed QA
attempts remain recorded alongside the corrected runs.
