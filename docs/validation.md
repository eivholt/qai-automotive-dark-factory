# Validation and evidence

This page separates current implementation checks from historical model results.
Updating documentation does not rerun fixtures or retrain the model. Private
images, datasets, weights, runtime packages and detailed reports are not distributed.
See [current progress](progress.md) and [model provenance](model_classification.md).

## Second enhancement pass — 16 September 2026

The UE 5.8 build and 124 Python tests pass. The new coverage-contract isolation
tests reject production/study crossover and prompt/policy drift. Rendered
graphics isolation retains 2,012 checks. Thermal radiometry, fields and transport
checks pass after the shared surface-evaluation refactor. The additional
reflection fixture measures 2,344 warmed receiver pixels, zero when the emitter
is hidden, and zero receiver variation at emissivity one.

The initial wrist tilt failed the lateral placement audit. The replacement
51-pose fitted path passes the rendered motion fixture, all 56 mounting-frame
checks and 3,271 pinch checks without relaxing tolerances. Evidence is private
under `artifacts/enhancement-rest-motion-corrected` and
`artifacts/thermal-reflection-fixture-final`.

The final actual-host run with enhanced equipment/replay and corrected lowering
completed three bodies, 27 requests and six thermal acceptance audits; R4 telemetry
passed 5,248 checks and live curtains 27 checks. The 104-check policy suite and
all nine legacy welding suites also pass. Eight complete, unique replay receipts
each retain three images. The
separate coverage feasibility study was unsuccessful (seven false G responses
against provisional labels in 16 development examples), so no new live gate is
enabled. See [implementation details and remaining work](enhancement_second_pass.md).

Final evidence: `artifacts/enhancement-rest-process-final`,
`enhancement-rest-policy-final`, `enhancement-rest-legacy-final`,
`equipment-details-closeups` and the motion/thermal directories above. Actual
equipment, placement, reflection and replay screenshots were reviewed and shown
in chat. Blender and Unreal were stopped afterward. No new held-out model
accuracy or calibrated thermal-reflection performance is claimed.

## Earlier implementation checks — 15 September 2026

The later opt-in enhancement pass also passed 100 native policy checks,
3,271 pinch checks, 56 mounting-frame checks, 681 realism checks and the nine
legacy welding suites. Its final actual-host cooling-recovery run completed
three bodies with 28 requests and six thermal acceptance audits; R4 telemetry
passed 5,442 checks. Graphics retained all 2,012 checks and four identical
baseline sensor hashes. See the [enhancement roadmap](enhancement_roadmap.md)
for the explicit switches and private evidence locations.

| Evidence | Recorded result | Scope |
| --- | --- | --- |
| Latest material revision | UE 5.8 build and 121 Python tests passed | Build/contracts; no new model evaluation |
| Persistent Editor authoring | 21 rendered lifecycle checks passed | Pose/bookmark persistence, reload and Play/Stop isolation; live MCP calls verified |
| Observer graphics | 2,012 checks passed | Fixed/R4 sensor parity across observer graphics modes |
| Before/after material baseline | Four sensor PNG hashes match the earlier dim-light fixture | Fixed NIR/thermal and R4 NIR/thermal preservation across the change |
| Imported welding | Native WeldingSmoke and nine follow-up suites passed | Motion, effects, cooling, thermal fields and lighting |
| Material close-ups | Hose and gripper A/B captures reviewed | Visible finish; no measured frame-time improvement |
| Permanent-frame model integration | Three bodies and six thermal audits in 180.24 s | Existing adapters, not independent accuracy |
| Subsequent Editor process integration | Three bodies, 23 requests and six thermal audits in 179.24 s | Actual model-controlled sequence with Editor-era scene |

Private recent evidence is under `artifacts/unreal-mcp-authoring`,
`artifacts/industrial-finish-graphics-v1`, `artifacts/industrial-finish-legacy-v1`
and `artifacts/industrial-finish-closeups`. Earlier Editor evidence is under
`artifacts/editor-workflow-v1` and `artifacts/editor-process-v1`.

Current host routing is shared960 for presence/sealant/seating and shared896 for
weld. Its 191/192 development decisions predate the permanent mounting frame;
no new held-out result is claimed for the final scene. Historical scores below
must not be used as current-geometry accuracy or IQ9 performance evidence.

## Historical six-weld refinement

The Unreal build and 109 Python tests pass. Native policy passes 91 checks without model requests. Motion QA captures 38 checkpoints, including all six weld pulses, plate-free EVKs and the corrected NIR withdrawal. Camera/PNG QA passes 127,778 checks; thermal QA checks 1,404 targets across train/validation, including every weld pair. A final 8-episode capture keeps all 192 PNGs byte-identical after the withdrawal route change. Procedural thermal/spatial/transfer checks, nine legacy welding suites and 681 realism checks pass. Selected shared448 at 0.98 confidence scores 95/96 development decisions, with zero false G and one cool-weld hold. The actual three-body run passes all six thermal audits, 5,636 camera checks and 27 live-curtain checks. Read the [current model guide](model_classification.md) for provenance and timing.

These new recipes are `rigid-arm-panel-six-weld-scan-v4`, `panel-six-weld-episodes-v4` and `panel-six-weld-contact-core-halo-v5`. Old shared256 compatibility failed: 80/96 letters, 86/96 actions and 7 ambiguous thermal false G decisions. This failed result is preserved and motivates the new compatible continuation. Historical held-out scores below retain their three-contact geometry scope.

## Historical three-contact panel-v3 validation — 14 September 2026

The active process contract is `automotive-panel-process-v3`, with view recipe `rigid-arm-panel-two-bead-scan-v3`, capture recipe `panel-two-bead-episodes-v3` and thermal recipe `panel-two-bead-contact-core-halo-v4`. NIR gates inspect panel presence and seating; thermal gates inspect two front adhesive beads and three weld contacts. All stages receive three lossless 448 x 256 grayscale images from the physical R4 camera. Old door and three-bead panel scores are separate historical evidence.

| Evidence | Recorded result | What it establishes |
| --- | --- | --- |
| Final UE 5.8 build and Python/launcher checks | Build passed; 96 Python tests and 34 launcher checks passed | Compilation, contracts, compatibility guards and report validation |
| Native process policy | 91 checks, zero model requests | Consensus, holds, freshness and invalidation behavior; no model accuracy claim |
| Final scene/motion | 23 checkpoints, 69 modality images; source geometry conserved across 90 components | Reviewed tools, two purple beads, physical poses and bounded geometry audits; no industrial collision certification |
| Current synthetic capture | 64 episodes, 512 batches, 1,536 PNGs; 127,778 camera/PNG and 936 train/validation thermal checks passed | Camera/recipe consistency and observable thermal labels; no classification result |
| Selected shared256 validation | 95/96 letters; 96/96 actions; no false release or wrong presence branch | Exact merged host-serving behavior on 12 validation episodes |
| One-time held-out evaluation | 95/96 letters/actions; no false release or wrong presence branch | Selected host model on 12 separate test episodes; one conservative cool-weld hold |
| Actual model-driven process | Assembly / preassembled / assembly; three forward departures, 30 image-only requests, six thermal acceptance audits passed | Integrated camera-to-model-to-motion behavior without a ground-truth override |
| Final observer graphics | 2,012 checks across 32 states; all 128 fixed/R4 PNGs identical to their camera references | Actual DLSS/Lumen/HWRT toggles and sensor isolation |
| Fixed observer thermal display | 50 checks; all palettes, exact fixed-sensor parity | Observer 0–120 °C independent of sensor 20–120 °C, with no welding-range switch |
| Legacy welding regression | Nine suites, 12,261 checks passed | Retained legacy visual cycle, motion, cooling and sensor isolation |
| Readback and mock transport | Native readback plus ten checks; G/A/R/disconnected mock fixtures passed | Sync/async PNG parity and transport/reaction behavior; mocks are not classification evidence |

The selected model is `panel-two-bead-shared-256-v3`, a shared rank-8 visual/language LoRA served as a merged Torch BF16 model on host port 18191. Its 256 cumulative optimizer updates took 742.78 seconds across two passes; capture, shader preparation, build and validation add separate elapsed time. The full provenance, exact serving alias, confidence policy and private evidence paths are in [model classification](model_classification.md#selected-host-runtime). The previous 128-update candidate was not selected, and the held-out set was not used for further tuning or selection.

Held-out standalone HTTP median/p95 were 49.28/54.55 ms, with 41.17 ms median GPU forward/scoring time. The 30 actual reply events during native Unreal rendering measured 84.43/92.83 ms HTTP median/p95. The three-body run completed in 182.31 wall-clock seconds, including mechanical movement, physical cooling and repeated inspection. These are different timing scopes; inference latency is not production throughput or a sustained graphics benchmark.

The held-out NIR presence/seating and adhesive cases were all correct. Weld was 35/36: one cool weld held A. No incorrect releases or wrong presence branches were observed in this small synthetic set; this does not establish real-camera reliability, adhesive cure, weld strength or IQ9 performance. The six native thermal acceptance audits only inspect outcomes after model decisions and cannot advance the controller. See [process inspection](process_inspection.md) and [scene implementation](process_scene.md) for live evidence and physical limits.

## Reproduce the current checks

Use [README setup](../README.md) for local assets, generated content and the installed UE 5.8 toolchain. The [model workflow](model_classification.md) owns current capture/training/evaluation commands and the separately started compatible host service. Preserve frozen train/validation/test splits and use fresh artifact directories; do not rerun held-out evaluation during routine documentation or visual changes.

```powershell
python -m unittest discover -s tests -v
.\unreal\Scripts\build.ps1

# Native controller fixture: no model requests.
.\unreal\Scripts\run.ps1 -ProcessPolicySmoke -Output artifacts\fresh-panel-policy

# Actual host-model integration: requires the compatible v3 service.
.\unreal\Scripts\run.ps1 -ProcessSmoke -ProcessEndpoint http://127.0.0.1:18191/v1/inspect -Output artifacts\fresh-panel-model
```

`ProcessSmoke` requires assembly/preassembled/assembly departures and all six thermal acceptance audits. The launcher checks matching session identity, explicit native verdicts and Windows exit status. Missing or stale reports fail. `ProcessPolicySmoke`, offline capture and explicit `-NoInference` do not substitute for this actual model run.

Use the focused guides when changing the corresponding subsystem:

- [Observer graphics](observer_graphics.md): DLSS/Lumen/HWRT installation and the rendered graphics parity fixture.
- [Fixed thermal display](thermal_welding_range.md): the observer's 0–120 °C scale and palette/encoding checks; fixed/model sensors remain 20–120 °C.
- [Scene implementation](process_scene.md): two-bead geometry, physical camera/motion audits and panel capture recipes.
- [Factory lighting](factory_lighting.md), [welding effects](welding_effects.md) and [motor heat](robot_motor_heat.md): their rendered regression procedures.
- [Thermal QA](thermal_qa.md), [spatial fields](thermal_spatial_fields.md) and [heat transfer](thermal_heat_transfer.md): separately scoped procedural rendering/solver checks.
- [Panel-v2 history](model_classification_panel_v2_history.md) and [door-process history](model_classification_v1_history.md): previous geometry, contracts and model scores.

## Current limits

The demonstrated model runtime is the host GPU. A task-matched IQ9 export, quantization comparison, new EVK latency/accuracy evaluation and sustained EVK soak remain pending. The selected shared adapter does not resolve the planned shared-versus-per-modality adapter comparison. Rendered labels and authored temperatures are synthetic evidence, not calibrated real-camera or industrial acceptance tests. Graphics parity proves sensor isolation, not a sustained frame-rate improvement. Source-triangle and pose samples do not certify continuous collision-free robot trajectories.

## Historical starter validation — 11 September 2026

This section records the original procedural starter checks from 11 September. **At that time, no new LoRA had been trained, no model weights had been loaded for inference, and no IQ9 service had been contacted or changed.** The current panel host model is documented above. These historical mock responses are not model accuracy or EVK latency evidence.

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

Compact portable evidence is in [validation.json](validation.json). Real sensor images are shown in [the night-vision guide](night_vision.md). The original starter screenshot showed the rendered green night-vision shader with inference disabled. [Night-vision validation](night_vision_validation.json) records all 20 fast-mode checks, image hashes, measured changes and shader-source hashes. This is display validation only.

### Historical GPU capture and live loop

The [GPU capture report](gpu_capture_validation.json) records a GPU-only preview with no CPU readbacks until an image was requested. Asynchronous readback plus the unchanged grayscale/PNG processor produced exactly the same PNG as the synchronous reference. Completed stale jobs and an obsolete preview were rejected. The four live mock runs (G/A/R/disconnected) all passed using asynchronous readback, with zero synchronous live readbacks and zero CPU preview uploads.

One diagnostic sample measured 9.60 ms on the game thread for synchronous capture/readback/processing/encoding and 0.039 ms for enqueueing the asynchronous readback. **These time different amounts of work:** asynchronous GPU transfer and worker encoding finish later. This demonstrates a short enqueue call, not an FPS or GPU-time speedup. GPU preview cadence is now independently configurable; its default 15 Hz is higher than the old 2 Hz CPU preview. Profile the final assets and target PC before choosing that rate.

[Quality-mode evidence](night_vision_quality_validation.json) covers the optional five-sample halo; fast mode uses one source-image sample. No warm GPU-pass timing or final-scene FPS claim is made.

### Reproduce the procedural starter checks

These commands exercise the procedural reference, not the panel model. From the repository root after installing the Python package:

```powershell
python -m unittest discover -s tests -v
.\unreal\Scripts\build.ps1
.\unreal\Scripts\run.ps1 -Scene procedural -Smoke -Output artifacts\fresh-camera-smoke
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

### Verification that remained at the starter milestone

- Actual Xbox hardware interaction has not been physically tested; bindings are implemented and compiled.
- The starter launch path used Unreal Editor in game mode. A standalone cooked Windows distribution was not part of this milestone.
- The primitive process was kinematic. Purchased-asset import, robot choreography and portal contact physics were implemented at later milestones; see the current scene guides above.
- The NIR material/sensor model is an approximation, with no real-camera calibration.
- The pilot was too small for its proposed release gates. Model training, host/EVK accuracy, quantization, latency and soak tests were pending at this milestone. Current panel host results are recorded above; the panel IQ9 export/evaluation remains pending.

### Historical procedural thermal rendering

The thermal GPU smoke passes all 42 image/runtime checks on UE 5.8.1 with D3D12 SM6. It checks overheating, a cold heater strip, a missing warm-sealant segment, opaque occlusion, light independence, three observer/HUD palettes, unchanged NIR pixels, raw/async PNG parity and stale snapshot/preview rejection. Capture-time metadata records part and camera transforms. The fixed thermal preview performs zero CPU readbacks until an explicit image request. [Thermal QA](thermal_qa.md) explains the measurements; [portable evidence](thermal_validation.json) includes image and source hashes. This historical rendering fixture did not measure sustained GPU/frame timings or thermal adapter accuracy. Current panel thermal classification is evaluated separately above.

All four local mock bridge reactions (G/A/R/disconnected) also pass with both cameras active. Live NIR inference uses asynchronous readback, with zero synchronous live readbacks and zero CPU preview uploads. These are transport/reaction checks, not Cosmos or IQ9 accuracy results.

### Historical procedural spatial gradients

The spatial update passes 55 additional GPU field checks and all 42 thermal regressions. The normal-case body centre is approximately 8.4 °C warmer than both sampled edges. From cooling age 15 to 45 seconds the rib loses about 74% of its heat excess above ambient, versus 12% for the body, while powered heaters remain unchanged. Object-local samples remain stable after translation and rotation. [Spatial evidence](thermal_fields_validation.json) records the actual images, ID-filtered measurements and source hashes. NIR visual and asynchronous readback regressions and all 34 Python tests also pass. No calibrated thermal accuracy or GPU speedup claim is made.

All four local mock bridge reactions also pass with spatial thermal fields active, with no synchronous live readbacks or CPU preview uploads. This validates existing NIR transport/reactions, not thermal model accuracy.

## Roof lighting and equipment heat — 16 September 2026

UE 5.8 build and 124 Python tests passed. The focused rendered
`artifacts/equipment-heat-v4` fixture verifies motor core heating from 26 to
49.67 C, exact pause retention, cooling to 22.44 C after 60 seconds stopped,
localized cabinet heat and byte-identical fixed NIR images. Motor/cabinet
closeups and the lower-output roof view were visually reviewed.
`equipment-heat-graphics-v1` passes 2,012 checks; `equipment-heat-legacy-v1`
passes all nine welding suites including factory lighting and motor heat.
`equipment-heat-process-v1` completes three actual-model departures and all six
thermal audits. This is integration evidence, not new held-out accuracy.

## Rounded sparks, EVK blinking, camera support and dark startup

UE 5.8 build and 124 Python tests passed. Rendered panel motion passed 63 checkpoints; pinch/frame checks passed 3,271/56. All nine welding suites passed, including green LED animation and rendered green/amber pulse closeups. Graphics isolation passed all 2,012 checks. Actual input verified dark startup with headlamp off, Tab order 0/1/2/3/0 and L activation. The host-model integration passed assembly/preassembled/assembly with three departures, six thermal acceptance audits and 5,248 camera checks. This is integration evidence, not a new held-out accuracy result. Private evidence: `artifacts/visual-refinement-{motion-v1,legacy-final,graphics-v1,startup,process-v1}`. Screenshots were reviewed and shown in chat; Unreal was closed afterwards.
