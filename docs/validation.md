# Validation and evidence

September 23: the new eight-tool default and canonical named history passed
164 native policy checks, Python discovery (280 tests, two skipped), and actual
three-body agent runs on both host GPU and IQ9. A separate three-body flow
regression passed with all six thermal audits. The 42-case planning probe still
has two unsafe stale-evidence choices and three conservative extra waits. See
[full evidence and limits](agent_demo_scope.md#september-23-validation).

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

## IQ9 Q8 inference — 18 September 2026

Both routed adapted Q8_0/F16 bundles reproduced 96/96 on an existing development
cohort on host and IQ9. Q4_0 was rejected after seven false G results. No new
held-out accuracy is claimed. The real EVK ProcessSmoke then passed three correct
departures, six thermal audits and 23/23 fresh valid replies in 272.97 seconds,
with 4.85 s median HTTP latency and unchanged two-batch/.98/.5/8 s policy.
Camera telemetry passed 4,472 checks; native rendered policy passed 118 checks;
UE 5.8 build, 144 Python tests and 44 launcher checks passed. An invalid edge
request returned A/invalid with the correct EVK source. Screenshots were reviewed
and shown in chat; Unreal is closed. See [runtime details](evk_model_runtime.md)
for failed quantization, OOM/cache and vision-offload trials and remaining
combined-controller memory limits. Private evidence: `artifacts/agent-evk-20260918/`.


## Fixed one-batch policy — 18 September 2026

The user replaced the selectable one/two-batch experiment with fixed one-batch
operation. Both reply decision policy and native execution reports now advertise
one complete fresh three-camera batch. Confidence .98, label mass .5, eight-second
freshness, prompts, adapter weights and all hold rules are unchanged.

UE 5.8 build, **143 Python tests**, **42 launcher checks**, and **115 rendered
native policy checks** pass. The current host actual ProcessSmoke passed all
three assembly/preassembled/assembly departures and six thermal acceptances in
**137.89 s**, with **16/16 valid replies**, **82.49 ms median** request time,
and **3,114 physical-camera audit checks**. Three A and two hot-thermal R replies
held normally; the other two R replies selected assembly. Acceptance temperatures
were 22.32–45.49 C, each below its stage's clear-ready limit. This is repeated
integration evidence, not a new held-out accuracy result.

Updated Qwen9B host tool experiments pass **16/16 standard runs** and **16/16
expanded runs**, with no rejected calls and median decisions **386.55/371.85 ms**.
These use synthetic inspections/jobs; no live agent control is claimed.
Evidence is private under `artifacts/evk-one-batch-20260918/`; EVK measurements
are summarized in [the runtime guide](evk_model_runtime.md).


The fixed-policy EVK ProcessSmoke also passes while Qwen9B remains resident:
**181.50 s**, **12/12 fresh valid replies**, **4.868 s median / 5.016 s maximum**,
three correct departures, six clear-ready thermal acceptances (20.10–35.15 C),
and **2,338 camera checks**. One A correctly held before reinspection. The full
EVK Qwen standard suite passed **8/8 cases / 53 decisions**, 12.06 s median,
with synthetic tool replies and no rejected calls. At least **21.49 GiB** RAM
remained available across the 74 sampled co-residency observations. No current-boot
OOM was found. All three models stay loaded on EVK; Unreal and temporary host
services are stopped. Screenshots were reviewed and displayed in chat.


## Live agent integration — 18 September 2026

See [live agent control](live_agent_control.md) for implementation and commands.
The optional mode uses actual Qwen tool calls, actual rendered camera batches and
Reason2, with a non-vetoing process evaluator. Default flow behavior remains.

Development exposed two real issues: flattened history led to repeated irrelevant
inspections, and an early conversation prompt skipped sealant inspection before
welding. That bad weld was executed and audited, demonstrating that no hidden
recipe gate corrected the model. Keeping real assistant/tool messages and making
inspection-based acceptance criteria explicit fixed the tested cases. These are
prompt-refined development results, not independent or held-out accuracy.

The host run completed three bodies in **212.50 s**, with **29 Qwen decisions**,
**16 valid Reason2 batches**, zero rejected calls and zero audit failures. Qwen
median/max were **552.23/753.87 ms**; Reason2 **82.23/94.12 ms**. It independently
chose weld-temperature inspection before seating, waited on two hot-weld results,
and made some redundant inspections. The camera audit passed **3,114 checks**.
Private evidence: `artifacts/agent-live-20260918/host-criteria/`.

Final native policy adds 20 agent checks to the existing 115: **135 pass**.
These exercise non-vetoing bad-recipe execution, no implicit stage advancement,
exclusive jobs, body turnover, pause cancellation, duplicate/old response scope,
schema bounds and evaluator evidence invalidation. **143 Python tests**, **52
launcher checks**, and UE 5.8 build pass. Actual screenshots were reviewed and
shown in chat. Initial EVK live integration exposed Unreal's independent 30 s HTTP
activity deadline; Qwen now has both total and activity deadlines of 120 s, while
Reason2 keeps its 8 s capture-to-reply requirement. Final edge results follow.


Final live EVK run passed all three bodies in **648.77 s**, with **25 Qwen
choices**, **14/14 valid Reason2 batches**, no rejected calls and zero independent
audit failures. Qwen median/max were **15.369/52.678 s**; Reason2 **4.847/4.986 s**.
The first request followed an earlier canceled prefill, so this is not a clean
cold-start latency measurement. All three models remained resident; three memory
samples showed at least **21.49 GiB available**, with no service restarts observed.
The physical-camera audit passed **2,726 checks**. Evidence:
`artifacts/agent-live-20260918/evk-timeout-fixed/` and the private summary.json.

The EVK run needed no thermal waits: surfaces cooled during its longer agent
decisions. Both runs made extra sealant/seating inspections, including seating
on the already assembled body. Qwen remains CPU-bound on the EVK; the requested
GGUF runs, but this is much slower than host GPU control. This is prompt-refined
three-body development evidence, not new held-out accuracy. The actual EVK HUD
and welding screenshot were inspected and shown in chat.


Final regression/cleanup: the unchanged flow controller passed three bodies in
137.87 simulation seconds with 16 actual model batches, six thermal audits and
3,114 camera checks. All nine legacy WeldingSmoke reports passed. RealismSmoke
and its Python verifier passed 681 checks (the real engine log was copied into
its private fixture directory before verification). The final build includes
bounded agent outcome history, at most 128 bodies; no runtime model logic changed
after the successful edge run. No further rendered run was needed for that bound.
Unreal is closed. Temporary WSL host Qwen and Reason2 services are stopped. All
four EVK services and both persistent WSL loopback tunnels remain active.

## Compiled IQ9 NPU runtime — 19 September 2026

QAIRT 2.45 / GenieX 0.3.17 with W8 text, FP16 text activations and fully FP16 vision
retains 191/192 on the existing development cohort: primary 119/120, weld 72/72,
all NIR decisions correct and no false G. The only error is a conservative
cool-sealant hold. Original three-view prompts, lossless 448 x 256 images and
.98/.5/one-batch/8-second policy are unchanged. This is not new held-out accuracy.
Earlier W8A16 and final-block-FP16 vision candidates scored 186/192 and were rejected.

Actual `ProcessSmoke -Enhancements` with the EVK endpoint passed three correct
departures, six thermal acceptance audits and 15/15 fresh valid replies. HTTP
median/max: 1416.12/1438.24 ms; simulation 153.38 s. No ground-truth fields were sent
to either vision backend. Qwen stayed loaded but this run used the flow controller;
it does not establish a new complete agent benchmark. Unreal closed after testing.

The separate 27-call single-image probe reached 557.81 ms median, but aggregate 6/9
accuracy with changed prompts remains insufficient for deployment. Both validated
three-view workers are now served by `dark-factory-edge-qairt` on 18198; Qwen stays
CPU-only on 18195, with about 17 GiB available RAM. Python discovery 156 tests and
rendered native ProcessPolicySmoke 135 checks passed. Reference conversion source
remains unchanged. Reports, bundles and reviewed screenshots remain private under
`artifacts/qairt-20260919`. See [the NPU guide](evk_qairt_experiment.md).

## EVK agent speed follow-up — 19 September 2026

With eight CPU threads, compact model-facing tool results reduce fixed-history
median decisions from 13.281 s to 10.985 s. The real three-body agent run then
passed in 484.345 wall seconds:
22 Qwen decisions, 11 fresh valid Reason2 batches, no rejected commands or audit
failures, and zero ground-truth fields sent to models. Medians: Qwen 11.186 s,
Reason2 1.415 s. First-policy prefill: 77.712 s. The previous live EVK run took
648.77 s; this total improvement includes the earlier NPU vision/text deployment.

The prompt clarifies the existing preassembled visual-acceptance criterion;
initial presence G needs both thermal gates but no post-welding seating check.
The earlier attempt held on repeated optional seating A results and remains
recorded as a failure. No threshold, sensor prompt, freshness rule, model weight,
executor recipe check or audit veto was changed. Actor motion remains model-led.

The final native verdict, verify_live_agent.py, 2,144 camera telemetry checks,
137 policy checks and 156 Python tests passed. This is development integration,
not new held-out accuracy. Both QAIRT workers and Qwen stayed loaded, with about
17.1 GiB available and no service restarts. Unreal and temporary host Qwen closed.
The selected run is `artifacts/agent-speed-20260919/evk-process-final/`; see
[the latency study](agent_speed.md) for tutorial sources, replay and fault probes,
startup costs and the rejected Hexagon controller candidate.

The final prompt also passed all four synthetic EVK fault probes after the live
run: wait on hot sealant/welds, hold on defective seating, and reinspection after
invalid presence evidence. These probes execute no scene jobs or Reason2 calls.

## September 19 Ministral controller comparison

The exact official Ministral-3-3B-Instruct-2512 Q4_K_M now serves controller
requests on EVK CPU port 18195, alongside the unchanged Reason2 NPU service.
Two final-prompt live runs passed assembly/preassembled/assembly, with zero
rejected calls, audit failures or ground-truth fields sent to the models.
Each used 22 agent choices and 11 fresh Reason2 batches. The clean-restart run
took 275.569 s, with 3.952 s median agent latency, a 28.885 s first request and
1.416 s median Reason2 latency. The earlier passing run took 261.310 s with a
partly warm policy prefix. Each physical-camera audit passed 2,144 checks.

Earlier host and EVK prompt candidates skipped the weld-temperature check on
the preassembled body. Their failures are preserved; the selected prompt adds
an explicit example requiring two separate thermal results. No recipe veto or
model-output repair was added. The Q4_K_M NPU export attempt failed before
compilation on unsupported `mistral3` architecture; no NPU agent result is claimed.
Native policy smoke, 158 Python tests and 52 launcher checks pass. See
[Ministral controller](ministral_controller.md) and private
`artifacts/ministral-20260919/evk-final-cold-process` for provenance and limits.

After the final live run, the selected prompt also passed all four synthetic
fault probes and 16/16 varied preassembled departure probes on the EVK. Native
policy smoke passed 137 checks. Unreal and temporary candidate/host services
are closed; the selected EVK controller and Reason2 services remain ready.

## 19 September 2026 — additional Ministral speed experiments

No fine-tuning. The selected change overlaps agent inference with R4 withdrawal
and removes the fixed one-second pause after each completed tool. A validated
reply waits for the actual parked arm before dispatch; pause/body turnover clears
queued replies. Prompt, weights, CPU flags, Reason2 adapters and sensor contracts
are unchanged. Other tested settings are documented in docs/ministral_controller.md.
Shorter reasons and argument-order retention failed decision checks and were
reverted; the failed live short-reason run remains private, not counted as passing.

- UE 5.8 build, 160 Python tests, 142 rendered ProcessPolicySmoke checks and 52
  launcher checks passed. Native tests include immediate replies during partial
  withdrawal, cancellation, body turnover and no recipe veto.
- Actual cold EVK ProcessSmoke with Enhancements, original prompt and default
  runtime passed all three bodies in 253.539 s (previous 275.569 s): 22 decisions,
  11 fresh three-view inspections, no rejects/audit errors/ground-truth leakage.
- Agent median 3.991 s /first 28.586 s; Reason2 median 1.415 s. No inference speedup
  claimed. Removing idle delays accounts for about22 s / 8.0% total improvement.
- verify_live_agent.py passes, including parked-arm dispatch checks;
  verify_inspection_arm.py passes all 2,144 camera telemetry checks.
- Evidence: artifacts/ministral-speed-20260919/evk-original-process. The separate
  evk-process directory is the rejected short-reason candidate, stopped after
  process audit failures. All reports/screenshots remain private.
- Unreal and the temporary benchmark service/tunnel are closed. Normal EVK
  Ministral and unchanged Reason2 NPU services remain ready; no auto restarts.

These are development integration results, not independent held-out accuracy.

## 19 September 2026 — one-token Ministral actions

The same official Q4_K_M, without fine-tuning, now supports a separate fixed
A–M action-code prompt and code-only assistant history. All 13 actions are always
available. The protocol adapter requires the unmasked global token winner to be
an action and returns that exact tool; no grammar, bias, recipe correction or
generated explanation. Native endpoint 18195 remains available; 18196 is the
new adapter. Reason2, camera recipes, thresholds and physical execution are unchanged.

- Host GPU and EVK each pass 9/9 assembly, 4/4 preassembled, 4/4 faults and 16/16
  varied preassembled histories. These are synthetic probes, not live accuracy.
- Two actual cold EVK three-body runs pass in 197.913 / 198.266 s. Each has 24
  choices, 12 Reason2 batches and no rejected actions or independent audit errors.
  An ambiguous sealant result triggers an agent-selected wait/reinspection, with
  the separate weld inspection retained before departure.
- Final agent median 1.607 s, first 18.036 s; Reason2 median 1.432 s. Previous
  native controller: 253.539 s total / 3.991 s median. Total time improves 21.8%;
  median controller latency improves 59.7%. Cold prefill remains substantial.
- All 48 model token winners match actual dispatches. Their model-visible histories
  match real observations and prior actions. No source-only fields or audit results
  enter either model. Each run passes all 2,338 physical-camera checks.
- 169 Python tests pass, shell syntax checks pass, and three deliberate audit-log
  alterations are detected. No Unreal source was changed for this protocol.
- Initial probes without the temperature sampler produced sampled-token/argmax
  mismatches and are excluded. Accepted runs explicitly use greedy sampling,
  pre-sampling scores, no controller LoRA and winner equality.

Private evidence: `artifacts/agent-single-token-20260919`, final run
`evk-final-cold-process`. See [the one-token guide](single_token_controller.md)
for prompt hashes, score limitations and startup. This is six-body development
integration, not new held-out accuracy. Unreal and temporary services are closed;
normal EVK controller/adapter/Reason2 services remain ready without QA request logging.

## 19 September 2026 - exact policy cache and tool-time prefill

Official Ministral Q4_K_M, native runtime, policy hashes, one-token selection and
Reason2 NPU adapters remain unchanged. Added a model/runtime/template/policy-bound
static KV cache (96 MiB) and processing of the next known conversation envelope
after returning an action. No future observations, expected decisions, state
masks, recipe vetoes or controller adapters were added.

A fresh native-process restart restored 945 policy tokens in 21 ms; total proxy
cache preparation including hashing was 109 ms. A miss cost 16.83 s once. Native
model loading/hash verification still occurs. Setup output is always discarded.

Two actual rendered three-body runs passed assembly/preassembled/assembly:

| Integration | Cache only | Cache + overlap |
|---|---:|---:|
| Wall time | 172.815 s | 170.429 s |
| First decision | 1.636 s | 1.703 s |
| Median agent | 1.582 s | 1.416 s |
| Median Reason2 batch | 1.415 s | 1.415 s |
| Agent decisions / camera batches | 22 / 11 | 22 / 11 |
| Rejected calls / independent audit failures | 0 / 0 | 0 / 0 |
| Physical camera checks | 2,144 | 2,144 |

All 44 selected raw vocabulary winners matched actual tool dispatch and real
observation histories. The overlap run completed 19 preparations at 266 ms median,
zero errors. The prior 198.266 s cold run made 24 decisions / 12 batches, so its
entire time difference is not an isolated cache improvement. This is a small
paired development timing study, not independent held-out accuracy.

Rejected experiments: rows and field aliases lost the weld-inspection requirement;
conditional future-action probes included premature releases; official Q8 CPU
replay inspected sealant before placement; real Adreno Vulkan timed out after
115 s. None is selected. See [the full study](agent_optimization_research.md) for
research links, exact scope and limitations. Physical speculative staging remains
unimplemented pending a separate cleared waiting pose; no weld current starts
early. Ordinary LoRA has no measured incremental speed/accuracy gain here.

178 Python tests pass, including cache invalidation/corruption, fresh-body request
isolation, fast-request serialization and optional prefill errors. Normal EVK
18196 has cache/overlap enabled and QA logging disabled; Reason2 unchanged,
temporary services/tunnel and Unreal stopped. Private evidence:
`artifacts/agent-prefill-20260919/{evk-cache-process,evk-overlap-process,proxy-cache,proxy-overlap}`.

## 19 September 2026 - dynamic Ministral NPU and contention experiments

The isolated dynamic GenieX v0.6.1 / Hexagon v73 candidate uses the official
BF16 GGUF directly quantized to a Q4_0 decoder and a Q8_0 tied embedding/output
head on CPU. It is a different quantization/backend from the selected Q4_K_M
CPU controller. Actual logs show an HTP session, 1,624 MiB HTP model buffers,
416 MiB HTP KV buffers and CPU/CPU_REPACK head storage. Aggregate `27/27`
offload logging does not account for tensor overrides; no per-op placement or
simultaneous accelerator-kernel trace was collected. About 17 GiB remained free
with the CPU backup and both NPU roles resident.

Saved-input HTTP replay used unchanged request bytes, five measured trials and
one separately reported warmup per mode. Agent-only, classifier-only,
agent-then-matching-inspection and simultaneous independent stress modes each
allow one outstanding request per endpoint. These probes perform no physical
actions and provide no fresh inspection evidence. Forced full-prefill NPU
timings were:

| Saved stage | Agent-only p50 | Reason2-only p50 | Sequential pair p50 | Simultaneous pair p50 | Simultaneous Reason2 slowdown |
| --- | ---: | ---: | ---: | ---: | ---: |
| Presence | 2192.6 ms | 1228.9 ms | 3407.4 ms | 3263.0 ms | 2.655× |
| Weld | 2926.5 ms | 1408.4 ms | 4332.3 ms | 4182.0 ms | 2.969× |

All four NPU/NPU and CPU/NPU stage reports completed without errors; Reason2
remained R for presence and G for weld across modes. This is repeated-input
stability, not accuracy. The CPU-agent comparison used warmed identical-prompt
caches, while the NPU agent evaluated all 1021/1291 tokens with `cache_n=0`.
Their different quantizations and cache regimes do not establish a matched
CPU/NPU inference speedup. Measured overlap is HTTP overlap only.

Both actual rendered dynamic-agent integrations failed:

- Original prompt `a6bb5d...`: 184.904 s, 31 choices, 15 Reason2 batches,
  agent/Reason2 medians 269.1/1414.8 ms. A premature `send_forward` was rejected
  mechanically before the model released the holder and retried. Recovery and
  three departures do not make the run a pass.
- Holder clarification `64e005...`: 41/41 offline checks preceded a failed
  185.689 s live run, with 31 choices, 16 batches and medians 269.0/1415.2 ms.
  Four audit failures occurred across two first-body actions: holder release at
  194.368°C without post-weld seating or cool-weld confirmation, followed by
  departure still missing post-weld seating. Both actions executed; there were
  zero mechanically rejected calls. The independent evaluator recorded the
  errors without vetoing, correcting or feeding source truth to the model.

Neither dynamic candidate nor clarified prompt is promoted. The original base
configuration remains unchanged, and the validated CPU controller remains
selected. The earlier unsupported-`mistral3` compiled export failure belongs to
SDK 2.47. The separate SDK 2.50 compiled candidate's completed runtime and live
checks are recorded below. Latest Python discovery ran 216
tests, with one skipped; protocol and lifecycle tests do not establish model
fidelity.

Private evidence: `artifacts/ministral-npu-contention-20260919/`, including
`dynamic-live-audit.json`, `dynamic-clarified-live-audit.json`, both native
`process_runtime.json` records, the CPU/NPU baseline and NPU full-prefill replay
reports, source/launch manifests and native journal. See
[Ministral NPU experiments](ministral_npu.md) for full hashes, p50/p95 values,
cache controls and reproduction; [compiled export](ministral_qairt_export.md)
records the separate experimental path. These are development experiments,
not new held-out accuracy results.

## 19 September 2026 - compiled Ministral NPU runtime and live validation

The SDK 2.50.40 candidate compiled in 27 minutes 47 seconds and its 3.417 GB
finalized bundle passed graph metadata and transferred-file hash checks. Actual
EVK loading/inference succeeded with Genie API 1.20.0 and matching v73 libraries.
It derives from the original Q4_K_M source but uses QNN mixed encodings and FP16
activations/KV/logits, so this is not bitwise native GGUF execution. The original
one-token prompt and unchanged base configuration are retained.

- All 22 captured native prompt token arrays match. Full-reset probes pass
  33/33: assembly 9, preassembled 4, faults 4 and departure 16.
- A saved growing history preserves all nine reset/append choices. Eight actual
  append calls have 254.2 ms median HTTP latency; the nine reset decisions have
  4593.2 ms median. The initial append-mode call still resets. Maximum action
  probability difference is 0.0134879; no complete-logit or bitwise equivalence
  claim is made.
- Actual rendered assembly/preassembled/assembly passes in 185.940 s: 28
  choices, 14 Reason2 batches, no rejected calls or independent audit failures.
  Agent median/first/maximum are 269.146/3852.963/3853.599 ms; Reason2 median is
  1332.105 ms. All 28 raw winner/history/dispatch checks and 2,726 physical-camera
  telemetry checks pass. The camera audit does not read PNG pixels.
- The earlier CPU cache/overlap run took 170.429 s with 22 choices/11 batches.
  Faster decisions did not shorten the compiled run's cycle: it included three
  extra cooling inspections and waits. CPU remains the default. This one
  three-body integration does not establish future reliability or new accuracy.

Compiled full-prefill contention uses five measured trials plus one separately
reported warmup per mode. Timings confirm actual reset, zero retained tokens
and full submitted inputs of 1021/1291 tokens. Median HTTP measurements:

| Saved stage | Isolated agent / Reason2 | Simultaneous agent / Reason2 | Sequential / simultaneous pair | Agent / Reason2 slowdown |
| --- | ---: | ---: | ---: | ---: |
| Presence | 3824.9 / 1156.6 ms | 4786.1 / 3262.1 ms | 4980.5 / 4786.7 ms | 1.251× / 2.820× |
| Weld | 5023.2 / 1313.8 ms | 6138.5 / 3907.6 ms | 6345.9 / 6139.2 ms | 1.222× / 2.974× |

Both models slow here; the earlier dynamic candidate's agent was approximately
unchanged. Pair speedup is only 1.0405× / 1.0337×. All calls succeed and Reason2
remains R/G respectively across each report's 15 measured classifier calls.
This is independent saved-input stress replay with HTTP overlap, not physical
causal flow, kernel overlap or accuracy.

The separate warm weld replay used three measured trials and one excluded
warmup per mode, with 16 untimed actual-history primes. Each prime had to select
the recorded preceding action before its real saved result was appended. Every
measured agent call retained 1,231 tokens, submitted 60 and ended at 1,291 with
native append confirmed. Measured request bytes were unchanged; there were no
physical actions or invented observations. Median HTTP measurements:

| Mode | Compiled agent | Reason2 | Pair makespan |
| --- | ---: | ---: | ---: |
| Agent only | 252.14 ms | — | — |
| Classifier only | — | 1316.15 ms | — |
| Sequential | 253.57 ms | 1316.69 ms | 1570.28 ms |
| Simultaneous independent stress | 364.73 ms | 1480.55 ms | 1481.55 ms |

Simultaneous agent/Reason2 latency rises 44.65%/12.49%; pair ratio 1.0599× means
5.65% less wall time. All nine measured classifier calls remain G and every
measured agent chooses the weld inspection. No calls fail. Prime cost is outside
the timed region, so this is prepared-history suffix performance, not cold
end-to-end throughput. Three samples per mode do not establish robust tails.

A checksum audit at 18:57:40–18:57:43 UTC overlapped only the excluded first
warmup/untimed prime; the first measured action completed at 18:57:48 and all
measured calls followed the audit, as recorded in `warm-run-prime-utc-journal.txt`.
All three measured trials per mode are retained. Dependent NPU work should remain sequential:
warm independent sharing gives a modest pair-time benefit, while full prefill
substantially delays Reason2. This is not kernel-overlap or accuracy evidence.

Private evidence under `artifacts/ministral-npu-contention-20260919/` includes
`genie-first-check.json`, `genie-reset-*.json`, `genie-cache-parity.json`,
`genie-live-{audit,camera-audit,token-audit}.json`, the actual `genie-live/` run,
`genie-placement-and-memory.txt`, `genie-full-prefill-{panel_presence,weld}.json`
and `genie-warm-append-weld/report.json` with its priming/timing journal.
See [the NPU experiment record](ministral_npu.md) for p50/p95, manifests,
cache/placement limits and reproducible commands. Dynamic Q4_0 and its clarified
prompt remain separate rejected experiments. Compiled native 18206/proxy 18197
were ready with normal append behavior and no QA logs at the end of that
experiment. They were subsequently stopped for the GPU work below.

## IQ9 GPU candidate, 19 September 2026

Private Mesa 26.1.8 plus real subgroup-64 restriction resolves the observed
batched execution fault. A separately built Q6_K scale-loading patch passes
10/10 selected CPU-reference operations; the compiler-matched pristine plugin
still fails Q6_K. These checks establish a scoped workaround, not universal
backend correctness. Flash attention must stay enabled in the tested candidate:
the regular attention path fails above 1024 cached tokens with a workgroup
occupancy error. Original GGUF, editable policy and one-token argmax are unchanged.

Saved probes pass 33/33. Actual rendered agent integration **fails**, despite
three departures: 194.089 s, 21 choices, ten Reason2 batches, zero rejected calls
and one independent process-audit failure. The preassembled body departs without
a cool-sealant observation. Agent median is 3093.15 ms; Reason2 median is 1354.67 ms.
The GPU's 35.15 s static-policy prime is separate startup work, excluded from
the live duration. Next-turn envelope prefill completes 18 times without error.

All 21 raw-winner/history/dispatch comparisons match, but the overall token audit
correctly fails with the native process verdict. Physical-camera telemetry passes
1950 checks. Cache-cleared exact-history replay shows CPU and GPU both choose
premature departure once presented with the weld-first history; CPU chooses a
different first thermal inspection. The new counterexample is a shared policy
limitation, not new held-out accuracy. No recipe veto or forced action was added.

See [the GPU experiment record](ministral_gpu.md) for reproducible private driver
and backend builds, chronology, numerical failures, actual prompts, timings and
private evidence under `artifacts/ministral-gpu-20260919/`. CPU remains selected
alongside Reason2 on the NPU; the compiled NPU-agent candidate remains stopped.

## IQ9 CPU tuning and guarded NPU rewind, later 19 September 2026

The same 21 recorded model-visible histories were tested with thread/microbatch,
flash-attention and private CPU-library variants. All retained the original CPU
winners, including its known missing-sealant failure. Native worker pool plus
flash attention off measured 984 ms median versus 1,305–1,316 ms baseline.
The corrected dynamic-plugin builder verifies the loader entry point; initial
linked-only packages failed before inference and are not successful candidates.

Actual tuned-CPU ProcessSmoke passed: 182.6177 s, 28 choices, 14 Reason2 batches,
agent median 1,036.30 ms and Reason2 1,415.55 ms, zero rejects/audit failures.
All 28 token/history/dispatch comparisons and 2,726 camera checks pass. Three
extra cooling inspections/waits make it slower overall than the earlier
170.429 s / 22-choice baseline. This is development integration only.

The separate thermal-order suite passes 14/17 for both original and tuned CPU;
missing sealant and stale evidence can still lead to departure. It does not
silently change the historical 16-departure/33-probe denominators.

The rebuilt optional Genie worker completed 42 device score requests comparing
guarded append/rewind with reset. Tokenization and all 21 winners match; maximum
absolute action-probability difference is 0.005653. Two actual new-body histories
take 480.72/481.58 ms versus 3,823.04/3,826.91 ms reset. Both paths share additional
policy errors; no live NPU integration or accuracy result is claimed for rewind.
Fresh sampler/END/occupancy checks pass; library and bridge hashes are recorded.

Python discovery: 231 tests, one optional fixture skipped; enabled SDK native
protocol fixture: 16 tests pass. Shell syntax, helper clean/incremental builds,
real cached-suffix CLI check and whitespace checks pass. Original CPU/Reason2
services remain selected and healthy; candidates and Unreal are stopped.
See [CPU record](ministral_cpu_tuning.md) and [NPU record](ministral_npu.md).

## Continued IQ9 speed experiments, 20 September 2026

CPU noomp/FAoff repeats at 984/987 ms. Strict affinity and poll 0/100 remain
986–988 ms; noomp+tile16 is slower at 1,027 ms. All six configurations preserve
the same 21 rendered prompt hashes and winners, including the known failure.

Reason2's explicit prefill-only worker skips unused decode KV conversions.
Twenty-four paired batches save 34.88 ms median per case. Complete existing
validation remains 191/192, zero false G: all 192 raw winners, decisions and
reported G/A/R probabilities are exactly unchanged. This is reused development
data, not new held-out accuracy or equality of every vocabulary score.

Actual rendered candidate Reason2 + original CPU agent ProcessSmoke passes:
177.5615 s wall, 171.7048 s simulation, 24 choices / 12 vision batches, zero
rejected calls or audit failures. Reason2 median/max 1,357.65/1,399.12 ms; agent
median 1,464.87 ms. All 24 raw-token/history/dispatch comparisons and 2,338 camera
checks pass. One extra sealant retry/wait makes the cycle longer than the earlier
170.429 s / 22-choice run. The normal services were restored and Unreal closed.

See [Reason2 implementation and measurements](reason2_prefill_optimization.md).
Private evidence is under `artifacts/model-speed-20260920/`. Tests include the
native prefill-only lifecycle/failure-cleanup fixture and rejection of validation
from a different execution path. Python discovery: 238 tests, one skip.

## Fixed sensor processor, later 20 September 2026

The IQ9 reference-processor comparison passes all 192 existing batches:
396,361,728 float elements, tokens and image grids are bit-identical. Twelve
additional synthetic cases cover all byte values, asymmetric spatial patterns,
L/LA/RGB/RGBA inputs, one/two/three images and changed Unicode text. Wrong-size,
colored, nonopaque and 16-bit inputs are rejected; a following valid input passes.
Isolated reference/candidate medians are 61.636/5.521 ms, with a 56.149 ms
median per-case saving. See [method and code](inference_optimization_patterns.md).

The incomplete model pilot printed primary medians 1171.89 ms prefill-only,
1112.90 ms fused and 1118.87 ms fused repeat, 18 batches each. These are block
medians, not a completed paired study. The EVK became unreachable during later
worker startup. The completed raw processor report was retrieved; the complete
model pilot was not. Full score comparison and actual model-driven ProcessSmoke
remain pending. No cause for the device outage is established. Normal service
configuration was not changed; runtime health cannot currently be confirmed.

Python discovery passes 240 tests with one optional skip. The separate rendered
local mock bridge passes G/A/R/disconnected with async readbacks and no live
synchronous readbacks. This verifies existing transport/capture behavior, not
the candidate's accuracy or live deployment. Unreal exited. Private evidence:
`artifacts/model-fused-20260920/processor-bench.json` and `bridge-smoke/`.

The follow-up local run enables the actual pinned PNG decoder fixture and passes
241 tests with one optional SDK fixture skipped. The final IHDR guard rejects
1-bit input as well as 16-bit; partial-batch failure/recovery, malformed/wrong-size,
colored and nonopaque PNGs also pass. Device validation is still pending: direct
SSH and a bounded local-network search for the known EVK SSH host key found no
reachable match. No additional device inference or live accuracy is claimed.

## Fused processor recovery and final validation, 20 September 2026

The user's reboot restored EVK access at 192.168.1.158. The earlier interrupted
pilot report was recovered but contains only NUL bytes; its score data cannot
be recovered. No root cause for the outage has been established. Fresh runs use
a bounded device-owned job, serial model workers and a flushed atomic timing
report. Sources, binaries, pinned decoder and runtime libraries have recorded
hashes under `artifacts/model-fused-final-20260920/`.

The final worker, including its later input guards, passes the full processor
comparison: all 396,361,728 float elements, tokens and grids match across 192
batches. Reference/candidate medians are 60.001/5.504 ms; median paired saving
54.509 ms. Complete classification is 119/120 primary plus 72/72 weld, with the
same conservative sealant hold, zero false G and no errors. Every raw winner,
decision and serialized G/A/R probability equals the original retained report.
This is the existing development cohort, not new held-out accuracy.

Matched timing uses 18 primary and six weld inputs, two repeats per path/input,
96 measured calls, counterbalanced block/case order and one discarded warmup per
block. Per-input means have primary medians 1188.42 ms prefill-only / 1126.80 ms
fused; weld 1360.92 / 1298.49 ms. Median paired savings are 63.45 / 62.16 ms,
with exact matching decisions/winners/GAR scores throughout. These incremental
latency gains do not establish a full-cycle speedup.

Actual candidate Reason2 + original CPU Ministral ProcessSmoke passes at
177.0125 s process wall / 171.0394 s simulation, with 24 choices and 12 camera
batches. Reason2 median/max is 1298.97/1349.07 ms; agent median 1458.59 ms.
Zero audit failures or rejected commands; all 24 raw-token/history/dispatch
comparisons and 2338 physical-camera checks pass. One preassembled-body sealant
ambiguity causes an agent wait and fresh reinspection. Recipe vetoes remain off.
The earlier prefill-only run took 177.5615 s with the same counts; a single
0.55 s difference is not a repeatable cycle-speed result. Launcher wall time
including Unreal startup/shutdown is separately recorded as 200.6611 s.

Original CPU/native agent, token proxy and Reason2 services are healthy on
18195/18196/18198 from the device and Windows. Both experimental services and
their tunnel are stopped; no Unreal process remains. Existing local validation
is 241 Python tests with one optional SDK skip and a separate passing rendered
mock bridge; this turn completed the missing device and actual model evidence.
No default launcher change, commit or push.

## Live backend switching, 21 September 2026

`-ProcessBackendSwitch` adds paired EVK/host GPU selection with EVK as the initial
backend. F4 or Xbox right-stick click switches both Reason2 and the agent.
Cancellation preserves completed tool history and ongoing mechanical work while
invalidating pending decisions, partial camera batches and late encodes. The
native fixture checks nine switching cases, including paused state, explicit
agent holds and exhausted turn budgets. It does not certify swept arm clearance.

UE 5.8 builds successfully. Python discovery passes 245 tests with one optional
SDK fixture skipped; launcher validation passes 52 checks and native policy
validation passes 151 checks. Evidence is private under
`artifacts/backend-switch-20260921/`.

The actual flow-mode `-ProcessBackendSmoke` passes three departures, all six
thermal acceptance audits and 16 camera batches at 151.997 s simulation time.
The routing auditor confirms EVK / host GPU / EVK for bodies 1 / 2 / 3 and the
physical-camera auditor passes 3114 checks. Automatic backend selection per body
exists only in this explicit QA fixture. These are integration results, not
held-out classification accuracy or a latency comparison.

The actual agent-mode switching run is retained as a **failure**. Both endpoint
changes worked and the first two bodies departed. On the third body, Ministral
skipped panel placement, requested thermal inspections, tried to release a
holder that was not engaged, inspected seating and chose hold. It made 26 agent
choices and 14 valid Reason2 batches, with one mechanical rejection and no
transport errors. The process stopped after two departures. Its camera audit
passes 2726 checks; the routing verifier correctly fails the overall incomplete
process even though all recorded calls used the expected endpoints. No recipe
veto, automatic repair or hidden reinspection was introduced to obtain a pass.

Captured EVK and GPU HUD images were inspected and shown in chat. The switch
fixture invokes the same native selection method as the input bindings; this
does not separately establish physical keyboard/controller input delivery.

## IQ9 HTP placement and live rewind, 21 September 2026

[Separate device placement](npu_device_placement.md) is an opt-in experiment:
Reason2 stays on device 0; compiled Ministral uses device 1. QNN inventory and
actual worker FastRPC handles establish placement. Five warm and three full
saved trials per mode, with separate warmups, pass. Concurrent full-prefill
Reason2 median falls from 3805.5 to 1325.3 ms; sequential timing is essentially
unchanged. These are independent replay requests, not speculative process jobs.

All 21 matched HTTP token arrays/winners agree across devices, and all 21
cached/reset winners agree on device 1. Score differences remain. The historical
direct-worker replay omitted BOS and must not be treated as identical live
input; its initial comparison failure is retained. The known missing-sealant
departure remains in the corrected saved-history tests. No accuracy claim.

Actual compiled-agent run passes three bodies in 185.490 s, with 30 decisions,
15 Reason2 batches, zero rejections/audit failures and median agent/vision
latencies 269.183/1332.401 ms. All 30 token/history/dispatch checks and 2920
camera telemetry checks pass. Native modes are one reset, 27 appends and two
rewinds, with new-body proxy calls 489.9/488.9 ms. Actual flow also passes three
departures, all six thermal acceptance checks and 15 model batches at 153.279 s
simulation time; its camera audit passes 2920 checks. Native policy passes 151
checks, and configured WSL Python discovery passes 246 tests with two optional
SDK fixtures skipped. The Windows system-Python attempt lacked Pillow; its
failure is retained separately and is not claimed as a passing test.

Private evidence: artifacts/npu-placement-20260921. The public preparation helper
was used for live integration; model/runtime/config identity and actual rendered
screenshots are retained privately. The welding screenshot was shown in chat.
Temporary native/proxy/tunnel services are stopped, original services remain
healthy, launch defaults are unchanged, and Unreal process count is zero.

## Streamed joint Reason2 prototype, 22 September 2026

The [native streaming experiment](streamed_reason2.md) preserves all 192 original
decisions, winning IDs, prompt-token counts and reported G/A/R probabilities in
each of two variants: early vision encoding and early encoding plus one AR128
decoder block. Retained development score 191/192, unchanged conservative sealant
hold, no false G or wrong presence branches. This is reused development evidence.
Full vocabulary logits are read; complete arrays were not compared bitwise.

Separate timing runs use actual sensor PNGs released at the recorded camera
cadence, one warmup plus five alternating measured pairs per stage. Vision-only
saves 264–269 ms; incremental prefill saves 446–451 ms from first image availability.
All measured pairs preserve the reported probabilities. These are offline EVK
replays, excluding initial arm deployment/network/live process/agent time.
No live streaming deployment or whole-cycle acceleration is claimed.

Fresh rendered 32-episode capture (seed 220922, Enhancements) passes 63,893 physical
camera/PNG checks. Only 26 whole train/development episodes' 52 NIR batches were
classified; reserved test episodes were excluded. Original three views: 52/52;
spot1+spot2: 36/52, including five wrong presence R branches. Reject the two-view
NIR candidate; the earlier 48/48 historical result does not validate fresh scenes.

Both native variants pass 24 lifecycle checks including partial-prefix
cancellation, identity/order/duplicate errors, decode failure, expiry and exact
fresh-score recovery. Python discovery passes 249 tests with two optional SDK skips;
native ProcessPolicySmoke passes 151 checks. Actual capture is rendered and exits;
it is not a live streamed ProcessSmoke. Production defaults and camera poses
are unchanged. Evidence remains private in artifacts/streamed-reason2-20260922.

## Live streamed Reason2 integration, 22 September 2026

The subsequent [live integration](streamed_reason2.md#live-results-22-september-2026)
is deployed on the prepared EVK's normal 18198 endpoint. Full paired validation
repeated on the actual original HTP0 bundles/configs: all 192 original decisions,
raw winning IDs, prompt counts and serialized GAR probabilities unchanged;
retained 191/192 development accuracy. The service requires these hash-bound reports.

Actual rendered flow completed all three bodies and six thermal acceptance checks,
with 16 complete streamed gates. The explicit streaming fixture additionally
canceled two partial batches: pause after image one and a backend switch after
early prefill. Both resumed with fresh evidence, and canceled epochs never released
a gate. Camera audit passes 3,114 checks. The switch immediately returns to EVK;
it verifies cancellation and does not constitute a new GPU inference benchmark.

Actual rendered agent on the deployed service: three departures, 26 model-selected
actions, 13 valid streamed gates, no independent audit errors or rejected tool calls.
Two thermal A responses produced model-selected waits and reinspections. Wall time
179.872 s; agent median 1,481 ms; final Reason2 HTTP median 748 ms. This last metric
excludes overlapped preparation. Stage medians from scan start are 3.308/3.482/3.314/
3.465 s for presence/sealant/seating/weld. Camera audit passes 2,532 checks.
Both streaming audits verify actual preparation overlap for all 29 completed gates.
These are integration results, not new held-out accuracy or a matched cycle speedup.

The actual HTTP lifecycle probe passes eight cases, including exact final pixel/
score parity with the full-batch path, cancellation ordering, partial/changed-image
rejection, invalid headers and fresh-score recovery. Python discovery: 259 tests,
two optional SDK skips. Native policy: 151 checks. ReadbackSmoke verifies exact
sync/async PNG parity, stale rejection and GPU-only preview delivery.
Private reports/screenshots: artifacts/live-streaming-20260922.
The separate rendered bridge G/A/R/disconnected checks also pass, retaining async
readback and zero CPU preview uploads. UE5.8 build and whitespace checks pass.
Temporary streaming candidate service/tunnel are stopped; deployed EVK services
remain healthy and no Unreal demo process remains.

## Streaming with split HTP devices, 23 September 2026

The [combined experiment](streaming_dual_htp.md) places the existing compiled
Ministral candidate on HTP1 alongside streaming Reason2's two HTP0 workers.
Actual FastRPC handles verify placement. In 36 bounded independent replay
trials, all 24 streamed results preserve exact baseline scores, tokens and
pixels; all 24 agent calls select the recorded action. Full-prefill concurrent
medians are 4,672 ms agent and 2,729 ms vision from the first image. These are
saved-input timing results, not live scheduling or new image accuracy.

The first actual combined agent run **fails**: 27 choices, 14 streamed gates,
two departures, then body 3 welds without placement and subsequently holds on
seating R. Agent median269ms, cold3836ms; final Reason2 HTTP median714ms.
The evaluator records the wrong job without vetoing it. All27 full-vocabulary
token choices match the actual model-visible histories and dispatched tools;
the token audit nevertheless fails overall because the process failed.
Camera telemetry passes2726checks. The streaming audit finds no transport or
overlap errors but also fails overall on the incomplete process.

Three exact-token full-reset NPU replays reproduce the original choices,
including the invalid weld. CPU selects placement at the earlier near-tie,
but also selects welding on the later incomplete history. This distinguishes
backend choice variation and policy weakness from stale cache reuse. Original
failure artifacts remain intact; no prompt/action mask/threshold repair was
introduced. The CPU-agent default is unchanged.

The separate flow lifecycle fixture passes three departures,16complete gates,
six thermal checks and pause/switch cancellation, in152.433s simulation time.
Its camera audit has one failure among3114checks: weld spot1 actual/authored
Euler error0.0001038516degrees exceeds0.0001degrees. Exact joint targets and
physical-lens/capture agreement pass. Preserve this small geometry QA exception;
neither motion nor tolerance was changed as part of device placement.

The optional launcher validates device1/core0 dependencies, starts the compiled
backend/proxy with isolated SDK paths, waits for readiness and refuses occupied
ports without changing existing services. `-EvkAgentNpu` selects18197 and labels
HTP0/HTP1; the backend audit now requires that selected port after returning
from GPU. Python discovery260tests passes, two optional SDK fixtures skipped;
UE5.8 build and native policy151checks pass. Private evidence and the actual
welding/failure screenshots shown in chat are under
artifacts/streaming-dual-htp-20260923.

Subsequent actual EVK/GPU/EVK agent routing passes all three departures in
166.165s,30choices/15inspections (11EVK/fourGPU), with no audit errors or rejected
calls. EVK choices use18197 and GPU choices18194; physical camera2920checks pass.
This warmed mixed-backend integration does not supersede the failed full-EVK
run and is not a matched performance comparison. Temporary compiled services
and their forwarding tunnel were stopped; original18198/18195/18196 stay healthy.

## Canonical agent history — September 23

The [named-history follow-up](agent_history_reliability.md) normalizes JSON
member order, makes completed jobs explicit and removes model-visible IDs and
numerical timestamps. A full-reset replay reproduced a hold caused solely by
member order; changing only that order restored placement. All 120 permutations
of the representative observation now normalize identically. Token audits
compare exact canonical messages against actual dispatched-job observations.

The candidate with static policy examples passes 44/46 synthetic development
cases and 34/34 reserved cases (the latter reused for the final canonical
format). Two known policy errors remain; these are not image-accuracy results
and the CPU-agent default is unchanged. The failed initial named-v2 live run
and unsuccessful prompt/review/representation experiments remain recorded.

Three canonical full-EVK runs with seeds 42/43/44 complete nine bodies,
84 one-token choices and 42 streamed inspections with zero process-audit
errors. Process, token and streaming verifiers pass for each. Median agent
latency is 269 ms; new-body proxy resets take 5.307 s median. Reason2's final
HTTP median is 714 ms, excluding early preparation. Source truth remains
outside both models and no deterministic policy repair was added.

Camera validation retains two exceptions: seeds 42/44 each fail one of 2,726
checks, at 0.000203578/0.000123168 degrees versus the unchanged 0.0001-degree
authored-pose tolerance. Seed 43 passes all checks. A separate concurrent flow
run completes three bodies/16 inspections and passes all 3,114 camera checks.
The actual device handles confirm Reason2 HTP0 and Ministral HTP1. Full-reset
replays correct both original premature-inspection/welding failure histories.

Python discovery passes 274 tests with two optional SDK skips; native policy
passes 151 checks. All private evidence is under
`artifacts/agent-named-history-20260923`. The actual welding screenshot was shown
in chat. Experimental compiled services/tunnel are stopped, normal services
remain healthy, and zero Unreal processes remain.
