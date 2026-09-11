# Preparation validation — 11 September 2026

This records starter integration checks. **No new LoRA was trained, no model weights were loaded for inference, and no IQ9 service was contacted or changed.** Mock responses are not accuracy or EVK latency evidence.

| Check | Result |
| --- | --- |
| Unreal C++ build | Win64 Development Editor build passed with UE 5.8.1 |
| Generated content | Empty map, factory material and four HLSL night-vision materials (fast/quality observer and UI) generated; zero errors |
| Night-vision display | 20 rendered checks passed in each fast/quality variant: green/mono, temporal grain, dark observer and input isolation |
| Fresh sensor recapture | Byte-identical grayscale PNG after display changes, with identical augmentation seed/frame |
| Rendered sensor cases | Normal, missing, misaligned, obscured and visible-dark captures inspected |
| Observer/sensor isolation | Rendered dark observer with active infrared sensor still lit |
| Python suite | 34 tests passed: bridge, timeouts, connection reuse, protocol, dataset integrity and evaluation |
| Randomized pilot | 30 real PNGs, 30 unique hashes, 10 matched nuisance groups |
| Pilot split | Train: 8 per class; validation: 1 per class; test: 1 per class |
| Pixel checks | All 30 images nonblank and equal-channel grayscale; no split leakage |
| Actual-image processor check | Passed against the existing local Cosmos-Reason2-2B processor without loading weights |
| Visual/context budget | 112 visual tokens; 209 total tokens including one class answer |
| Class tokens | G = 38; A = 32; R = 49 |
| Live Unreal + mock G | Passed: tray moved 290 cm |
| Live Unreal + mock A | Passed: no tray movement |
| Live Unreal + mock R | Passed: no tray movement; reject gate engaged |
| Live Unreal + disconnected bridge | Passed: held with no tray movement |

The actual-image processor check used the existing WSL environment: Torch 2.9.0+cu128, Transformers 4.57.3, PEFT 0.20.0, Accelerate 1.12.0, and Pillow 12.0.0. Those checks establish input compatibility, not model prediction quality.

The pilot's frozen manifest hash is `4a58de93dd8d8bcb2be88be6e56fcbc0b59885a77a2a33d3132f150b7e94e55b`. Its prompt hash is `a43db23a0d91a26fad7dac074d7fdb2867da3ac2d44622f73034f2c2f16efaf0`. The local pilot, freeze outputs and detailed verification remain under `artifacts/pilot-v1/` and are intentionally ignored by Git.

Compact portable evidence is in [validation.json](validation.json). Real sensor images are shown in [the night-vision guide](night_vision.md). The README screenshot shows the rendered green night-vision shader with inference disabled. [Night-vision validation](night_vision_validation.json) records all 20 fast-mode checks, image hashes, measured changes and shader-source hashes. This is display validation only.

## GPU capture and live loop

The [GPU capture report](gpu_capture_validation.json) records a GPU-only preview with no CPU readbacks until an image was requested. Asynchronous readback plus the unchanged grayscale/PNG processor produced exactly the same PNG as the synchronous reference. Completed stale jobs and an obsolete preview were rejected. The four live mock runs (G/A/R/disconnected) all passed using asynchronous readback, with zero synchronous live readbacks and zero CPU preview uploads.

One diagnostic sample measured 9.60 ms on the game thread for synchronous capture/readback/processing/encoding and 0.039 ms for enqueueing the asynchronous readback. **These time different amounts of work:** asynchronous GPU transfer and worker encoding finish later. This demonstrates a short enqueue call, not an FPS or GPU-time speedup. GPU preview cadence is now independently configurable; its default 15 Hz is higher than the old 2 Hz CPU preview. Profile the final assets and target PC before choosing that rate.

[Quality-mode evidence](night_vision_quality_validation.json) covers the optional five-sample halo; fast mode uses one source-image sample. No warm GPU-pass timing or final-scene FPS claim is made.

## Reproduce the checks

From the repository root after installing the Python package:

```powershell
python -m unittest discover -s tests -v
.\unreal\Scripts\build.ps1
.\unreal\Scripts\run.ps1 -Smoke -Output artifacts\fresh-camera-smoke
```

Run `python scripts/verify_unreal_bridge.py` to reproduce all four live checks with automatically managed temporary loopback mocks. They do not contact the IQ9.

For individual live-loop checks, start a fixed mock response in one terminal:

```powershell
python -m dark_factory.bridge --mock-decision G
```

Then run:

```powershell
.\unreal\Scripts\smoke_bridge.ps1 -Expected G
```

Repeat with A and R after restarting the mock with the corresponding fixed decision. Stop the mock and run `smoke_bridge.ps1 -Expected ERROR` to verify a disconnected service holds. The scripts write JSON reports and screenshots under `unreal/Saved/Smoke/`. Fresh capture runs need a new output directory.

## Remaining verification

- Actual Xbox hardware interaction has not been physically tested; bindings are implemented and compiled.
- The current launch path uses Unreal Editor in game mode. A standalone cooked Windows distribution has not been built.
- The primitive process is kinematic. Contact physics, purchased assets and manufacturing-specific timing remain scenario work.
- The NIR material/sensor model is an approximation, with no real-camera calibration.
- The pilot is too small for the proposed release gates. Model training, host/EVK accuracy, quantization, latency and soak tests remain pending.

## Thermal sensor rendering

The thermal GPU smoke passes all 42 image/runtime checks on UE 5.8.1 with D3D12 SM6. It checks overheating, a cold heater strip, a missing warm-glue segment, opaque occlusion, light independence, three observer/HUD palettes, unchanged NIR pixels, raw/async PNG parity and stale snapshot/preview rejection. Capture-time metadata records part and camera transforms. The fixed thermal preview performs zero CPU readbacks until an explicit image request. [Thermal QA](thermal_qa.md) explains the measurements; [portable evidence](thermal_validation.json) includes image and source hashes. Sustained GPU/frame timings and thermal adapter accuracy remain unmeasured.

All four local mock bridge reactions (G/A/R/disconnected) also pass with both cameras active. Live NIR inference uses asynchronous readback, with zero synchronous live readbacks and zero CPU preview uploads. These are transport/reaction checks, not Cosmos or IQ9 accuracy results.

## Spatial thermal gradients

The spatial update passes 55 additional GPU field checks and all 42 thermal regressions. The normal-case body centre is approximately 8.4 °C warmer than both sampled edges. From cooling age 15 to 45 seconds the rib loses about 74% of its heat excess above ambient, versus 12% for the body, while powered heaters remain unchanged. Object-local samples remain stable after translation and rotation. [Spatial evidence](thermal_fields_validation.json) records the actual images, ID-filtered measurements and source hashes. NIR visual and asynchronous readback regressions and all 34 Python tests also pass. No calibrated thermal accuracy or GPU speedup claim is made.

All four local mock bridge reactions also pass with spatial thermal fields active, with no synchronous live readbacks or CPU preview uploads. This validates existing NIR transport/reactions, not thermal model accuracy.
