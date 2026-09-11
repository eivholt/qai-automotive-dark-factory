# Automotive dark factory

An Unreal Engine preparation demo for factory inspection with **NVIDIA Cosmos-Reason2-2B on a Qualcomm IQ9 EVK**, independent Xbox-controller navigation, and separate NIR and thermal camera views.

The current scene is a procedural four-module battery-tray inspection cell. It includes normal, missing, misaligned and obscured NIR cases; dim roof-window light in the normal observer view; a green phosphor display; a thermal proxy with stored body/surface heat, nearby radiation and contact transfer, overheating, heater-fault and glue-gap cases; and pass/hold/reject reactions through the NIR bridge. It requires no purchased assets. The final manufacturing scenario remains open.

**Default inference is a declared mock.** A new factory adapter has not been trained or deployed. Thermal images have a separate snapshot path and are not sent to the NIR model contract; a thermal adapter is pending. Both infrared modalities are rendering approximations, not calibrated cameras.

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

Start a mock bridge in one terminal. This sequence exercises the three reactions; it does not inspect the image:

```powershell
.\.venv\Scripts\python.exe -m dark_factory.bridge --mock-cycle G,A,R
```

Open the scene in another terminal:

```powershell
.\unreal\Scripts\run.ps1
```

Reset or change the scene to start a new inspection. Use `run.ps1 -NoInference` to explore the camera and collect samples without a bridge. Both scripts accept `-EngineRoot` if Unreal is installed elsewhere.

Two roof-window light proxies provide dim visible fill, initially **80 lumens each**. Tune the authored setting with `run.ps1 -NoInference -RoofLightLumens 80`; it is not a measured factory illumination level. `-RoofLightLumens 0` selects a zero-light visible view. Offline dataset capture, `-Smoke` and `-NightVisionSmoke` explicitly disable these lights for calibration.

Capture the six implemented observer presentations with Unreal’s native viewport screenshot path:

```powershell
.\unreal\Scripts\run.ps1 -ModalityScreenshots -NoInference -Output artifacts\modalities
```

The sequence saves visible, green/monochrome NIR, and white-hot/black-hot/iron thermal screenshots from one fixed viewpoint, plus `modalities.json`. It also compares saved NIR and thermal sensor PNGs with roof lighting off/on. Check the report for the outcome. After capture, the simulator stays open in thermal view with inference paused.

## Controls

| Input | Action |
| --- | --- |
| Xbox left / right stick | Fly / look |
| Xbox triggers | Down / up |
| Xbox A / X | Reset with new seed / next case |
| Tab / Xbox Y | Cycle observer: dim visible → NIR → thermal |
| I / N / Xbox B | Toggle observer NIR / dim visible; return from thermal to NIR |
| T | Toggle observer thermal / NIR |
| Xbox Menu | Return observer home |
| WASD + mouse; Q / E | Fly and look; down / up |
| Shift / Home | Move faster / return observer home |
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

The fixed NIR sensor stays in active-IR mode during the interactive demo. The HUD shades its render target directly on the GPU, with a default sensor refresh of 15 Hz. This preview is before CPU sensor augmentation, so it is not an exact copy of the saved/model PNG. Display gain, grain and color leave those PNGs unchanged. Moving the observer or switching its modality does not change the fixed sensor.

The default NIR display shader uses one texture sample. Launch with `run.ps1 -NightVisionQuality` for an optional five-sample halo. Palette and grain run on the GPU; the animated grain pattern changes at 24 Hz. Live inference and saves use one asynchronous GPU readback job at a time, followed by grayscale augmentation and PNG encoding on a CPU worker. Offline dataset capture remains synchronous for determinism. [The night-vision guide](docs/night_vision.md) describes the controls and capture settings.

The fixed thermal sensor has a separate 10 Hz GPU preview and linear 448 × 256 grayscale output over 20–120 °C apparent temperature. CPU profile updates integrate Planck radiance over 8–14 µm, mix emissivity with reflected ambient radiance, and upload a small lookup texture. A GPU depth check rejects hidden surfaces; observer palettes do not alter saved thermal pixels. F9 saves snapshots in `unreal/Saved/Thermal/`. A small energy-conserving network adds contact conduction, distance-dependent radiation and retained surface warmth after a source moves. O toggles exchange; U advances simulated time. H cycles to a source/receiver fixture and frames it with the observer. The updated thermal GPU smoke passes 138 transfer checks plus 42 existing and 55 spatial-field checks; sustained GPU costs have not yet been measured. See [thermal rendering and controls](docs/thermal_rendering.md).

G releases the tray, A holds it, and R engages the reject gate. Invalid, expired or obsolete replies hold. These reactions currently belong to the NIR inspection loop.

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
