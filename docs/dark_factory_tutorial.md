# Dark factory inspection with Unreal and Cosmos Reason2

The current demonstration assembles an inner trunk panel in a locally supplied automotive welding cell. A robot-mounted NIR/thermal camera sends images to Cosmos-Reason2-2B on the host GPU, and four model gates control the production sequence. The selected contact-heat adapters run on the host GPU and IQ9 EVK; see the current model guide for validation scope. The separate procedural battery-tray scene remains available for learning capture, mock transport and dataset preparation.

Start with the [README](../README.md) for installation and launch commands. The [process guide](process_inspection.md) describes the live sequence, [scene guide](process_scene.md) covers tools and choreography, and [model guide](model_classification.md) records the exact prompts, adapter provenance and measured results. This repository publishes tutorial and source examples only: purchased assets, generated Unreal content, runtime packages, training images and model weights stay private.

The precursor is [Physics-aware Edge AI](https://github.com/eivholt/qai-physics-reasoning/blob/main/docs/isaac_sim_conveyor_reason2_tutorial.md). This project carries forward independent sensor cameras, synthetic data, bounded answers and a resident model worker. Its old parcel adapter, the removed door adapter and three-bead panel adapters do not satisfy the current `automotive-panel-process-v3` contract.

## 1. Run the current panel process

Prepare/import your local assets and build using the README. Start the compatible host model service described in [selected host runtime](model_classification.md#selected-host-runtime), then launch:

```powershell
.\unreal\Scripts\run.ps1 -Scene welding -Process -ProcessEndpoint http://127.0.0.1:18191/v1/inspect
```

For scene exploration without model calls, use `-Process -NoInference`. This does not establish model acceptance or exercise a complete live gated cycle. Without `-Process`, the imported cell runs its separate [legacy visual production cycle](welding_production_cycle.md).

The selected runtime routes `panel-contact-shared-960-v7` to presence/sealant/seating and `panel-contact-shared-896-v6` to weld. The host uses Torch BF16; the EVK uses merged Q8_0 decoders with their matching adapted F16 vision encoders. The canonical model guide contains reproducible commands, hashes and confidence policy; a checkpoint name alone is not a compatibility check.

```text
Xbox / mouse -> observer camera + graphics / HUD        (presentation)

R4 robot joints -> rigid NIR or thermal lens -> GPU render target
                                                    |-> GPU HUD preview
                                                    |-> bounded async readback
                                                        -> grayscale PNG triplet
                                                        -> host Reason2 stage prompt
                                                        -> fresh gated decision
                                                        -> physical next step / hold

Offline: same physical poses, frozen episode clock -> synchronous PNG capture
Future: validated task-specific export -> IQ9 resident worker
```

The observer can move independently. R4 physically positions its camera for each inspection; a hidden camera does not pivot independently of the arm. Fixed sensor previews remain independent of both the observer and R4 process captures.

## 2. Define the four model gates

One generated body in five starts with the panel already installed, but only the model's fresh presence result selects the live branch.

| Stage | Images and decision | Result after one fresh three-camera batch |
| --- | --- | --- |
| `panel_presence` | NIR: G = installed; R = missing | One G batch selects the preassembled path; one R batch selects assembly |
| `adhesive` | Thermal: heat transferred to the panel along three contact edges | One G batch permits welding on the assembly path, or the next inspection on the preassembled path; R/A wait |
| `panel_seated` | NIR: panel placement after welding | One G batch permits weld-cooldown inspection; R holds an exception with the holder engaged; A holds |
| `weld` | Thermal: six weld contacts | One G batch permits holder retraction and departure, or direct departure for the preassembled body; R/A wait |

Every stage uses three lossless 448 x 256 grayscale PNGs from the physical R4 poses in `spot1`, `spot2`, `spot3` order. Adhesive views map to the two real beads as `[0, 1, 0]`; there is no rear-middle bead. Weld views cover pairs [0,1], [2,3], [4,5]. Capture waits for settled joints and three actual discarded render captures. Eight-second batch/reply freshness, stage identity and the compatible scoring policy apply before a result can act. Errors, stale replies and incomplete evidence always hold. A scene timer or generator flag cannot override that hold.

For a missing panel, R1 applies the two rounded purple adhesive beads, R3 picks and holds the panel, R4 checks adhesive readiness, R2 spot welds, and R4 confirms seating and weld cooldown. R3 then retracts and the body exits. Preassembled bodies run presence and both thermal gates while skipping assembly, welding, seating inspection and holder retraction. The rack restores its picked sheet when the current body starts departing. The HUD and cabinet indicators reflect these model-controlled steps.

The adhesive/weld limits are illustrative 45/55 °C apparent-temperature readiness thresholds, with ±3 °C ambiguity bands. They are not measurements of chemical cure or structural weld quality. Both wrong presence directions matter: a wrong R could assemble over an installed panel. See [process inspection](process_inspection.md) for timings and complete policy.

## 3. Keep observer rendering separate from evidence

Tab / Xbox Y cycles dim visible, NIR, thermal and bright factory. Dim visible uses roof-window spill and an optional headlamp; bright mode starts grouped fluorescent-style lights with a brief staggered flicker and suppresses the headlamp. L changes the headlamp preference outside bright mode. I / N / Xbox B toggles visible/NIR; B / Xbox X cycles observer bookmarks. Esc quits; Space / Xbox View pauses scene motion and cancels incomplete process evidence. Resume starts a fresh spot-0 batch.

The NIR observer uses a camera-following illuminator, green/monochrome display, grain and lens effects. V changes phosphor presentation, brackets / Xbox LB/RB adjust display gain, and comma/period adjust grain. The fixed NIR illuminator and model PNG path remain independent. The fast NIR display shader uses one source sample; `-NightVisionQuality` selects the optional five-sample display variant. These counts describe the display pass, not total rendering cost. See [night vision](night_vision.md) and [factory lighting](factory_lighting.md).

The observer thermal scale is fixed at **0–120 °C**, with iron color by default and white-hot/black-hot palettes on K / Xbox D-pad left. There is no F10/right-stick welding-range switch. Fixed and model thermal sensors remain linear **20–120 °C**; the HUD sensor previews use that sensor window. Physical weld surface fields can exceed 120 °C and saturate the display without changing those fields or the sensor encoding. See [thermal rendering](thermal_rendering.md) and [fixed display range](thermal_welding_range.md).

Visible observer views request real NVIDIA DLSS Quality, Lumen GI/reflections and hardware ray tracing on supported installations. F6 / F7 / F11 toggle them separately; `-NoDLSS`, `-NoLumen` and `-NoHardwareRayTracing` set startup preferences. Observer NIR/thermal modes suspend these effects, and all fixed/R4 sensor cameras disable them. [Observer graphics](observer_graphics.md) documents installation, fallback behavior and byte-identical sensor checks across all graphics combinations.

NIR is an active-illumination rendering proxy, not measured infrared reflectance. Thermal uses assigned surface temperatures, emissivity and an 8–14 μm apparent-temperature model, with mesh-local gradients and explicit cooling fields. Imported bulk heat exchange is disabled until physical thickness/contact data exists; the procedural thermal scene has a separate heat-transfer demonstration. Neither visual model is calibrated real-camera evidence.

## 4. Capture and freeze whole episodes

Live previews sample GPU render targets directly. Image requests use bounded asynchronous GPU readback and worker PNG encoding; obsolete epochs and partial batches are rejected. Model images exclude HUD, cabinet display and gate lamps. Ground truth, temperatures, seed/case labels and prior decisions never enter the model prompt or images.

Offline capture uses the same physical lens poses and PNG contract with a frozen process/heat clock across each triplet. Use the [model workflow](model_classification.md) for panel capture, camera/radiometry audits, preparation and freezing. Its current recipes are `rigid-arm-panel-two-bead-scan-v3`, `panel-two-bead-episodes-v3` and `panel-two-bead-contact-core-halo-v4`. A capture plan is not a rendered dataset.

The current capture contains 64 whole-body episodes: 40 train, 12 validation and 12 test. Each body supplies eight batches: presence, adhesive G/A/R, seating and weld G/A/R. This gives 512 batches and 1,536 PNGs; all states/views of a body stay in one split. The seed-4307 cohort is separate from the old three-bead dataset. Inspect geometry coverage, occlusion, visible defect size, grayscale output and thermal contact labels before fitting. Nuisance variation must not encode the answer through brightness, background or camera placement.

Do not treat source metadata or a nonblank image as proof that an anomaly is observable. Keep test episodes out of tuning and select the complete adapter/runtime/prompt/policy before opening test predictions. Geometry, bead placement, physical scan poses or sensor-processing changes require compatible recapture and new evaluation.

## 5. Fit and evaluate the actual serving path

The selected shared adapter covers both NIR and thermal across all four prompts. It adapts visual and language projections with rank-8 LoRA; a frozen-vision procedural starter remains a separate experiment. Shared versus modality-specific adapters is still an open comparison. See [model classification](model_classification.md) for the trained candidate and [training](training.md) for the distinct procedural preparation path.

A bounded answer reduces generated output, but LoRA alone does not promise faster inference. Vision encoding and model prefill still run. The current host scorer evaluates G/A/R probabilities with minimum total label mass and conditional-confidence checks; uncertainty maps to A. Validate that complete scoring policy and the exact merged runtime rather than only an unmerged training checkpoint.

The selected shared256 runtime scored 95/96 literal labels and 96/96 actions on validation, then 95/96 labels/actions on the one-time held-out panel. The held-out error conservatively held a cool weld; there were no false releases or wrong presence branches in that small set. The full live assembly/preassembled/assembly sequence also passed. [Validation](validation.md) separates these model results from policy, rendering and transport checks and links measured timings. This is synthetic-scene development evidence; IQ9 and real-camera results are still pending.

Report per-stage confusion, conservative holds, false releases, both presence-branch errors, malformed replies, transport failures and warm/cold latency separately. Inference milliseconds exclude physical scanning, capture scheduling, cooling and gate scheduling; full production-cycle time must be measured separately.

## 6. Prepare the IQ9 deployment

The previous project provides an export/build reference: merge the selected adapter, build host reference artifacts, calibrate and export a task-matched QAIRT bundle, and validate one resident GenieX worker. The panel task uses three images per request, so its processor, template, visual-token/context budget and deployed graph must be verified explicitly rather than copying a single-image parcel bundle.

An adapter directory is not automatically hot-swappable on IQ9. Record base revision, adapter weights, processor/template, prompts, scoring policy, quantization/export configuration and artifact hashes. Compare host and IQ9 predictions on identical PNG triplets, then run an independent held-out evaluation, sustained soak and native client test. Keep the adjacent project's running production service untouched. The current host results do not establish IQ9 accuracy, quantized equivalence or EVK latency.

## Procedural reference: battery tray and thermal sandbox

Select `run.ps1 -Scene procedural` for the asset-free battery-tray reference. Its [single-image contract](../configs/inspection_contract.json) defines G as four visible seated/aligned modules, R as a visibly missing/raised/misaligned module, and A as insufficient visual evidence. The mock bridge's G proceeds, R requests rejection/rework, and A or transport failure holds. These are reference semantics, distinct from panel presence R selecting assembly.

The default mock returns scripted tokens and does not analyze pixels. Exercise G/A/R and disconnect the service; change the scene while a request is pending to verify stale-reply rejection. [Historical starter validation](validation.md#historical-starter-validation--11-september-2026) retains these checks. The reference conveyor/reject gate moves kinematically and does not establish manufacturing physics or model accuracy.

The [training guide](training.md) preserves the procedural deterministic planner, balanced matched nuisance families, captured-manifest validator, freeze/export commands and optional decoder-only trainer. It supervises a one-letter image-first answer with the reference prompt. Its [bridge evaluator](../scripts/evaluate_bridge.py) supports plumbing-only mock runs, which cannot pass a model-accuracy gate. These files are not the panel capture/training workflow.

The procedural sensor preview defaults to 15 Hz, configured by `[DarkFactory.Sensor] PreviewHz` or `-DFPreviewHz=` from 1 to 30 Hz. It precedes the reference CPU grayscale/noise augmentation, so it is not an exact displayed copy of the saved PNG. C requests a fresh NIR capture; browsing alone does not require CPU readback. Offline calibration and training use synchronous capture with the same reference augmentation/encoding function. For zero-light calibration, pass `-RoofLightLumens 0` and turn off the visible headlamp with L.

The independent thermal sandbox supports H / Xbox D-pad right for cases, J / D-pad up for spatial versus uniform fields, U / D-pad down for cooling-age increments, and O / right-stick click for its heat-transfer comparison. These are procedural experiment controls; right-stick does not switch the observer thermal range or enable imported bulk heat exchange. F9 saves a thermal snapshot under its separate metadata contract. See [spatial fields](thermal_spatial_fields.md), [heat transfer](thermal_heat_transfer.md), [thermal research](thermal_rendering_research.md) and [thermal QA](thermal_qa.md). Its older rendering reports do not establish panel-adapter accuracy.

`run.ps1 -NightVisionSmoke` verifies NIR presentation/input isolation. `run.ps1 -ReadbackSmoke` verifies GPU previews, sync/async PNG parity and stale-epoch rejection. For model, motion, graphics or thermal changes, use the corresponding current fixtures listed in [validation](validation.md); these tests do not substitute for sustained frame-time profiling.
