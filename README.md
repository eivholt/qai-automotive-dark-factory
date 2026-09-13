# Automotive dark factory

An Unreal Engine preparation demo for factory inspection with **NVIDIA Cosmos-Reason2-2B on a Qualcomm IQ9 EVK**, independent Xbox-controller navigation, and separate NIR and thermal camera views.

When the locally prepared welding-cell assets are present, the interactive demo uses four articulated robots and separately addressable car-body panels. The source package, derived geometry and runtime remain local. Without those assets, the demo uses a procedural four-module battery-tray inspection cell. It includes normal, missing, misaligned and obscured NIR cases; dim roof-window light in the normal observer view; a green phosphor display; a thermal proxy with stored body/surface heat, nearby radiation and contact transfer, overheating, heater-fault and glue-gap cases; and pass/hold/reject reactions through the NIR bridge. It requires no purchased assets. The final manufacturing scenario remains open.

**The imported welding cell is a visual motion preview with inference and training disabled until its scenario contract is defined. Procedural inference is a declared mock.** A new factory adapter has not been trained or deployed. Thermal images have a separate snapshot path and are not sent to the NIR model contract; a thermal adapter is pending. Both infrared modalities are rendering approximations, not calibrated cameras.

![Actual Unreal render with green phosphor shading and animated grain](docs/media/night-vision-green.png)

## Quick start on Windows

Requirements: Unreal Engine 5.8 with C++ build tools and Python 3.11+. The current development host has UE 5.8.1.

From the repository root, prepare the Python environment:

```powershell
python -m venv .venv
.\.venv\Scripts\python.exe -m pip install -e .
```

Build the Unreal module and generate its empty map, factory material, NIR shaders and thermal shaders:

```powershell
.\unreal\Scripts\build.ps1
```

For the procedural reference scene, start a mock bridge in one terminal. This sequence exercises the three reactions; it does not inspect the image:

```powershell
.\.venv\Scripts\python.exe -m dark_factory.bridge --mock-cycle G,A,R
```

Open the procedural reference scene in another terminal:

```powershell
.\unreal\Scripts\run.ps1 -Scene procedural
```

Reset or change the scene to start a new inspection. Use `run.ps1 -NoInference` to explore the camera and collect samples without a bridge. Both scripts accept `-EngineRoot` if Unreal is installed elsewhere.

In the procedural scene, two roof-window light proxies provide dim visible fill, initially **80 lumens each**. Tune the authored setting with `run.ps1 -Scene procedural -NoInference -RoofLightLumens 80`; it is not a measured factory illumination level. `-RoofLightLumens 0` removes the roof fill; also turn off the observer headlamp with L for a zero-light visible comparison. Offline dataset capture, `-Smoke` and `-NightVisionSmoke` explicitly disable both roof fill and the headlamp for calibration.

Capture the visible/NIR/thermal comparison with UnrealÃ¢â‚¬â„¢s native viewport screenshot path:

```powershell
.\unreal\Scripts\run.ps1 -ModalityScreenshots -NoInference -Output artifacts\modalities
```

The sequence saves visible, green/monochrome NIR, and white-hot/black-hot/iron thermal screenshots from one fixed viewpoint, plus `modalities.json`. It also compares saved NIR and thermal sensor PNGs with roof lighting off/on. Check the report for the outcome. After capture, the simulator stays open in thermal view with inference paused.

## Import the local welding cell

Place the purchased FBX and JPEGs in `assets/Autodesk_FBX_2009/`. The entire `assets/` tree is ignored. The following commands build a small preparation tool against Unreal's installed FBX SDK, recover connected pieces and approximate robot joints, export grouped geometry, and import separate static meshes:

```powershell
.\.venv\Scripts\python.exe -m pip install -r scripts\assets\requirements.txt
.\scripts\assets\prepare_welding_cell.ps1 -Python .\.venv\Scripts\python.exe
.\unreal\Scripts\import_welding_cell.ps1 -Mode prepared -Manifest assets\derived\welding_cell\manifest.json
.\unreal\Scripts\run.ps1 -Scene welding
```

The current preparation produces 76 mesh components, including 29 body-panel pieces and two complete moving holders, plus 24 joints across four robots. The demo repeats a 21-second arrival, approach, precision-welding, withdrawal and departure cycle, with seeded per-robot rest delays of 0.2-0.8 seconds and pre-weld dwells of 0.5-2 seconds. Arms park with their bases turned 180 degrees away and approach/withdraw in one second. Physics strip curtains align with the rail ends and conceal the entry/exit tunnels; articulated spline hoses follow the arms. Burst, fountain and falling-fragment phases share a bounded 4,096-fragment pool, with an additional 128-puff optical smoke pool. Sparks bounce against surfaces, settle and fade, with visible/NIR illumination and independent thermal cooling profiles. See [production cycle, effects and controls](docs/welding_production_cycle.md) for the authored motion and physics assumptions. Four car-weld spots heat and cool on three body panels in visible, NIR and thermal views, following the moving car and resetting for each new body. These authored surface fields are independent of the tools' retained heat; see [car-weld heat](docs/welding_body_heat.md). Twenty-eight warm robot castings also use authored mesh-local thermal gradients; [imported thermal fields](docs/imported_thermal_fields.md) explains their shape and limits. The imported bulk heat-transfer network remains disabled.

Ten [researched realism additions](docs/realism_additions.md) add floor markings, overhead utilities, extraction hoods, animated body clamps, a stack light, entry/exit presence sensors, spatial machinery sound, production progress, five smooth observer viewpoints and persistent weld witness marks. **B / Xbox X** cycles viewpoints; **M** mutes sound. The existing control cabinet now has an [illuminated LCD and LED buttons](docs/control_cabinet.md), with GPU gauges driven by the simulated production cycle. These details follow the authored cycle and do not report model inspection outcomes.

`run.ps1` defaults to `-Scene auto`: it selects the prepared local cell when available, otherwise the procedural reference. `-Scene welding` requires a valid local manifest and meshes. Use **Space / Xbox View** to pause or resume the car, robot, particle and curtain motion; observer movement and modality switching remain independent. The six visible roof-light proxies are adjustable with `-WeldingRoofLightLumens` (default 80 each). The fixed NIR camera retains its own illumination. The sensor panel shows colored thermal and monochrome thermal together, sharing one camera render. The observer defaults to the iron color palette when entering thermal; K / Xbox D-pad left retains white-hot and black-hot options.

Run the imported visual/motion and camera-isolation check:

```powershell
.\unreal\Scripts\run.ps1 -Scene welding -WeldingSmoke -Output artifacts\welding-smoke
```

See [asset preparation, geometry and validation](docs/welding_cell_asset_import.md) for the source structure, local reports and limitations. Existing procedural smoke/dataset flags retain the procedural scene.

## Controls

Case selection, randomization, inference, NIR training saves and heat-exchange controls below apply to the procedural reference scene. Imported mode keeps those paths disabled and runs an authored production cycle with physical curtains and sparks.

| Input | Action |
| --- | --- |
| Xbox left / right stick | Fly / look |
| Xbox triggers | Down / up |
| Xbox A / X | Procedural scene: reset with new seed / next case |
| B / Xbox X | Imported scene: cycle five observer viewpoints; manual flight cancels the transition |
| M | Imported scene: mute / unmute synthesized factory sounds |
| F8 / Xbox left-stick click | Observer collider overlay: off â†’ physics bodies â†’ physics + query bounds â†’ off |
| Tab / Xbox Y | Cycle observer: dim visible Ã¢â€ â€™ NIR Ã¢â€ â€™ thermal Ã¢â€ â€™ bright factory Ã¢â€ â€™ dim visible |
| I / N / Xbox B | Toggle observer NIR / dim visible |
| T | Toggle observer thermal / NIR |
| Xbox Menu | Return observer home |
| Space / Xbox View | Pause / resume the imported production cycle and physics |
| L | Toggle headlamp preference; it emits only in dim visible mode and resumes there when requested |
| WASD + mouse; Q / E | Fly at 900 cm/s and look; inverted mouse pitch; down / up |
| Shift / Home | Boost to 2700 cm/s / return observer home |
| 1 / 2 / 3 / 4 | Normal / missing / misaligned / obscured |
| R | Reset current case with a new random seed |
| V | Toggle NIR green phosphor / monochrome display |
| K / Xbox D-pad left | Cycle thermal white-hot / black-hot / iron-like palette |
| H / Xbox D-pad right | Next thermal case |
| J / Xbox D-pad up | Spatial thermal fields / original uniform comparison |
| U / Xbox D-pad down | Advance simulated thermal time by 15 seconds (wrap after 1800) |
| O / Xbox right-stick click | Heat exchange (default on) / legacy isolated cooling |
| F9 | Queue raw thermal PNG + metadata snapshot |
| Xbox LB / RB or [ / ] | Decrease / increase NIR display gain |
| Comma / period | Decrease / increase NIR display grain |
| C / P | Queue a fresh NIR PNG + metadata capture / toggle NIR inference |

Bright factory mode starts the grouped overhead lamps with a brief staggered flicker, then settles to steady illumination. It suppresses the observer headlamp while retaining its preference for the return to dim mode. Both fixed infrared cameras keep their own inputs. See [factory lighting and its isolation checks](docs/factory_lighting.md).

The fixed NIR sensor stays in active-IR mode during the interactive demo. The HUD shades its render target directly on the GPU, with a default sensor refresh of 15 Hz. This preview is before CPU sensor augmentation, so it is not an exact copy of the saved/model PNG. Display gain, grain and color leave those PNGs unchanged. Moving the observer or switching its modality does not change the fixed sensor.

The default NIR display shader uses one texture sample. Launch with `run.ps1 -NightVisionQuality` for an optional five-sample halo. Palette and grain run on the GPU; the animated grain pattern changes at 24 Hz. Live inference and saves use one asynchronous GPU readback job at a time, followed by grayscale augmentation and PNG encoding on a CPU worker. Offline dataset capture remains synchronous for determinism. [The night-vision guide](docs/night_vision.md) describes the controls and capture settings.

The fixed thermal sensor has a separate 10 Hz GPU preview and linear 448 Ãƒâ€” 256 grayscale output over 20Ã¢â‚¬â€œ120 Ã‚Â°C apparent temperature. CPU profile updates integrate Planck radiance over 8Ã¢â‚¬â€œ14 Ã‚Âµm, mix emissivity with reflected ambient radiance, and upload a small lookup texture. A GPU depth check rejects hidden surfaces; observer palettes do not alter saved thermal pixels. F9 saves snapshots in `unreal/Saved/Thermal/`. A small energy-conserving network adds contact conduction, distance-dependent radiation and retained surface warmth after a source moves. O toggles exchange; U advances simulated time. H cycles to a source/receiver fixture and frames it with the observer. The updated thermal GPU smoke passes 138 transfer checks plus 42 existing and 55 spatial-field checks; sustained GPU costs have not yet been measured. See [thermal rendering and controls](docs/thermal_rendering.md).

G releases the tray, A holds it, and R engages the reject gate. Invalid, expired or obsolete replies hold. These reactions currently belong to the NIR inspection loop.

Collider bounds draw through surfaces in the observer HUD across all four modalities. Green identifies static collision, cyan kinematic colliders, orange dynamic bodies, and purple query-only mesh bounds. Hidden active carriage boxes are included; disabled collision disappears immediately. Mesh bounds enclose their collision geometry and are not exact box-shaped colliders. See [collider overlay](docs/collider_overlay.md).

## Dataset and training preparation

Create a reproducible NIR capture plan, then render it in Unreal. Thermal snapshot collection is separate; its training contract and dataset workflow remain to be defined.

```powershell
.\.venv\Scripts\python.exe scripts\training\prepare.py plan --output data\pilot\plan.jsonl
.\unreal\Scripts\run.ps1 -Plan data\pilot\plan.jsonl -Output data\pilot
.\.venv\Scripts\python.exe scripts\training\prepare.py validate --manifest data\pilot\manifest.jsonl --dataset-root data\pilot
.\.venv\Scripts\python.exe scripts\training\prepare.py freeze --manifest data\pilot\manifest.jsonl --dataset-root data\pilot --output artifacts\pilot-frozen
```

The default plan has 300 captures across 100 matched nuisance families; it is a starter dataset size, below the proposed final evaluation gates. Whole families stay in one split. Inspect the actual images before using the data.

Run a CPU-only training preparation check:

```powershell
.\.venv\Scripts\python.exe scripts\training\train_lora.py --manifest data\pilot\plan.jsonl --dataset-root data\pilot --output outputs\factory-lora --dry-run
```

See [training and export](docs/training.md) for processor validation, optional CUDA dependencies, LoRA training and IQ9 release gates. The training script uses exactly one supervised class token and freezes the vision tower. LoRA alone does not guarantee lower inference latency.

## Connect a prepared IQ9 candidate

Use the address and model ID of the **new factory candidate**. This starts only the local bridge; it does not modify the EVK service:

```powershell
.\.venv\Scripts\python.exe -m dark_factory.bridge --backend evk --evk-url http://YOUR_EVK_IP:18183 --model YOUR_FACTORY_MODEL_ID
```

The bridge sends a direct embedded 448 x 256 lossless PNG, one versioned user prompt, and requests one deterministic G/A/R token. The old parcel adapter uses different labels and is not a factory classifier. [Bridge configuration and protocol](docs/bridge.md) cover timeouts and compatibility.

## Checks and tutorial

```powershell
.\.venv\Scripts\python.exe -m unittest discover -s tests -v
.\unreal\Scripts\run.ps1 -Smoke -Output artifacts\render-smoke
.\unreal\Scripts\run.ps1 -NightVisionSmoke -Output artifacts\night-vision-smoke
.\unreal\Scripts\run.ps1 -ReadbackSmoke -Output artifacts\readback-smoke
.\unreal\Scripts\run.ps1 -ThermalSmoke -Output artifacts\thermal-v1
```

- [Full tutorial](docs/dark_factory_tutorial.md)
- [Night-vision shaders, controls and limitations](docs/night_vision.md)
- [Bright factory lighting and startup checks](docs/factory_lighting.md)
- [Thermal rendering, sensor encoding and controls](docs/thermal_rendering.md)
- [Heat-transfer model and rendered evidence](docs/thermal_heat_transfer.md)
- [Rendered thermal QA](docs/thermal_qa.md)
- [Thermal implementation research](docs/thermal_rendering_research.md)
- [Infrared research and shader design](docs/infrared_research.md)
- [Existing implementations and performance techniques](docs/night_vision_implementations.md)
- [Rendered night-vision QA](docs/night_vision_qa.md)
- [Scenario and asset selection](docs/scenario_design.md)
- [Validation evidence](docs/validation.md)
- [Precursor: IQ9 Physics Reasoning](https://github.com/eivholt/qai-physics-reasoning/blob/main/docs/isaac_sim_conveyor_reason2_tutorial.md)

Generated Unreal content, captures, datasets, model weights and licensed assets are ignored by Git. Source scripts recreate the starter content.

The welding tools now cool in visible, NIR and thermal views after each weld. See the [tool-cooling guide](docs/welding_tool_cooling.md).
