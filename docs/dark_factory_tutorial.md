# Dark factory inspection with Unreal and Cosmos Reason2

This tutorial starts a vehicle-manufacturing demonstration before the final factory assets and anomaly have been selected. A primitive battery tray stands in for a future inspection station. The complete target loop is:

```text
Xbox controller -> spectator camera                     (presentation only)
Fixed inspection camera -> GPU render target -> shaded HUD preview
                                  |
                      on-demand async readback
                                  |
                      CPU grayscale/noise + PNG -> local bridge -> IQ9 EVK
                                  ^                               |
                                  +-- simulated cell <- valid G/A/R

Offline dataset/calibration: synchronous capture -> same grayscale/noise + PNG
```

The simulator runs on the Windows PC. The IQ9 receives a fixed camera image and one text prompt, and returns a bounded visual decision. A separate observer can fly through the factory without moving the model's sensor.

The precursor is [Physics-aware Edge AI](https://github.com/eivholt/qai-physics-reasoning/blob/main/docs/isaac_sim_conveyor_reason2_tutorial.md). Its final implementation uses a native Unreal scene, synthetic training data, a compact user-only prompt, and a resident GenieX QAIRT service. This project carries forward those design principles; the old parcel adapter is not trained to inspect this tray.

## 1. Establish an observable task

The starter contract defines four rectangular modules seated side by side in a tray:

| Token | Visible evidence | Simulation action |
| --- | --- | --- |
| G | All four modules visibly present, seated and aligned | Proceed |
| A | Occlusion or image quality prevents reliable inspection | Hold |
| R | A module visibly missing, raised or misaligned | Reject / request rework |

A transport error also holds, but it is logged separately from a model's A prediction. An error is never a correct A in evaluation.

The placeholders include normal, missing, misaligned and obscured states. They are for testing the integration and for learning how to generate data. The conveyor and reject gate move kinematically, and primitive collisions are disabled for observer fly-through. They do not simulate contact mechanics or establish an industrial defect-detection benchmark. The [scenario guide](scenario_design.md) describes how to replace them.

A single still image is appropriate for gross seating or presence inspection at a defined pause in a manufacturing sequence. If the task depends on motion, timing or whether a robot completed a sequence, design a temporal input contract and retrain/evaluate that contract explicitly.

## 2. Separate darkness, sensor illumination and display

The virtual factory has no visible work lighting. The observer can use an assisted view to navigate. A fixed sensor uses an active near-infrared rendering proxy and exports grayscale RGB images. Both the observer and fixed-camera preview use display shaders with shaded greens, animated grain, soft highlight compression and mild lens falloff. The default shader uses one texture sample; `run.ps1 -NightVisionQuality` selects a five-sample variant with an optional bright-region halo. These sample counts describe the display shader, not the complete scene renderer.

Press I or N, or Xbox Y or B, to toggle the observer's assisted view and visible darkness. The fixed sensor stays in active-IR mode throughout the interactive demo. Sensor illumination is disabled only by internal offline calibration for control images. Observer movement and display controls leave sensor pose and model input unchanged. Press V for green/monochrome, [ or ] (Xbox LB/RB) for display gain, and comma/period for grain.

The HUD reads the sensor render target directly on the GPU, refreshing at 15 Hz by default. Configure `[DarkFactory.Sensor] PreviewHz` or pass `-DFPreviewHz=` to the executable for a rate from 1 to 30 Hz. Display palette and grain run on the GPU; the grain pattern changes at 24 Hz independently of camera refresh. The preview precedes CPU sensor augmentation, so it is not an exact copy of the saved/model PNG.

When inference or a save needs pixels, one asynchronous `FRHIGPUTextureReadback` job transfers the image. The game thread polls readiness without waiting; a CPU worker performs deterministic grayscale augmentation and PNG encoding. Browsing alone requires no CPU readback. Press C to queue a fresh sensor capture and metadata save. Stale scene epochs are rejected before a completed job can affect the new inspection. Offline datasets and calibration retain synchronous capture for deterministic ordering and use the same augmentation/encoding function.

The rendering proxy retains geometry and occlusion but does not model measured infrared material spectra or camera electronics. It is also not a thermal camera. See [the night-vision guide](night_vision.md) for HLSL and controls, [the infrared research](infrared_research.md) for imaging mechanisms, [existing rendering implementations](night_vision_implementations.md) for performance techniques, and [rendered QA](night_vision_qa.md) for verification.

`run.ps1 -NightVisionSmoke` exercises display variants and checks that presentation changes preserve model PNGs. `run.ps1 -ReadbackSmoke` checks GPU browsing without CPU readback, synchronous/asynchronous PNG byte parity, stale-epoch rejection and observer/sensor independence. These checks do not substitute for profiling sustained frame time or IQ9 request latency.

## 3. Start with a declared mock backend

Follow the [repository quick start](../README.md) to build the generated Unreal scene and launch the local bridge. Its default mock backend returns scripted tokens; it does not analyze pixels. Use it to verify capture, HTTP transport, HUD status and reversible scene reactions before loading a model.

Exercise all three responses and disconnect the service. The scene should proceed on a valid G, hold on A or a failure, and reject on R. Change the scene while a request is outstanding and verify that the old response cannot control the new inspection.

The bridge receives only image data and opaque request/episode identifiers. It does not forward identifiers or simulator labels to Cosmos. It sends one image-first user message with the versioned prompt in [inspection_contract.json](../configs/inspection_contract.json).

## 4. Generate and inspect synthetic data

Domain randomization varies nuisance factors independently of the answer: materials, roughness, tray position, camera placement, illuminator intensity, gain and noise. Each matched family contains the same nuisance configuration for G, A and R. This avoids a shortcut such as learning that brighter images always mean a defect.

Plan captures deterministically, render them in Unreal, then validate and freeze the captured manifest. This offline path captures and encodes synchronously; it does not use the interactive preview cadence or asynchronous job scheduling. A capture plan is not a dataset. Never mark missing images as captured or use simulator metadata as a substitute for checking the pixels.

Keep entire episodes and matched nuisance families in a single split. A validation frame must not be a near duplicate of a training frame from the same randomized scene. The validator also checks duplicate image hashes across splits. After selecting the final scenario, add difficult backgrounds, geometry and sensor conditions that are completely absent from training.

Inspect a contact sheet before training. Confirm labels from the actual image, usable defect size, settled rendering, and that neither labels nor UI leaked into the image. The [training guide](training.md) contains the executable commands.

## 5. Teach the bounded answer

The intended model remains [NVIDIA Cosmos-Reason2-2B](https://huggingface.co/nvidia/Cosmos-Reason2-2B). NVIDIA provides a customizable physical-AI model and [post-training examples](https://github.com/nvidia-cosmos/cosmos-reason2/tree/main/examples/cosmos_rl).

The local training path prepares a new LoRA adapter using the same image-first prompt as runtime. It supervises the response token only and validates that G, A and R each tokenize as one token in the chosen processor/template. Vision weights remain frozen in the initial recipe.

There are two distinct goals:

- Accuracy: adapt the model to the actual inspection task, materials and sensor distribution.
- Latency: avoid generating a prose explanation when the application needs one bounded decision.

LoRA alone does not promise faster inference. The base vision encoder and language-model prefill still run. The practical latency opportunity is a short prompt, an appropriate visual-token budget, one output token, a resident worker, and measured deployment optimization.

The bridge uses a one-token completion API. It does not claim to implement a new runtime that reads restricted logits directly. If the backend later selects only among G/A/R logits, validate that behavior independently: forced closed-set output can hide uncertainty, so the A class and out-of-distribution tests still matter.

## 6. Evaluate before exporting

Use frozen validation and independent test splits. Report per-class recall, confusion, false G decisions on A/R cases, malformed replies, transport failures, and elapsed request latency. Keep the first request separate from warm latency.

The evaluator in [scripts/evaluate_bridge.py](../scripts/evaluate_bridge.py) supports an explicit plumbing-only mock run. Such a run cannot pass a model-accuracy gate. For actual evidence, use the new adapter through the EVK backend and save the report alongside the model and dataset hashes.

Set thresholds before tuning on the final task. The script's strict defaults are starter checks, not industrial acceptance criteria. A small synthetic test set cannot establish real-factory reliability.

## 7. Prepare the IQ9 deployment

The previous project provides the known export/build path: merge the adapter, build host reference artifacts, calibrate and export a task-matched QAIRT bundle, and validate it through one resident GenieX worker. Preserve the exact image size, processor, prompt, output tokens and context budget across those stages.

A LoRA adapter directory is not automatically a hot-swappable IQ9 deployment. The selected runtime may require merging, quantization, graph export and a fresh bundle. Record base model revision, adapter hash, processor/template identity, export configuration and final artifact hashes.

Point this project's bridge at the new candidate service only when that bundle is ready. The starter does not replace or restart the previous project's EVK worker. Compare host and EVK predictions on identical PNG files, then run a sustained soak and a rendered client test.

No fine-tuning job, model export or new IQ9 accuracy/latency result is implied by the starter setup.

## 8. Replace the placeholder cell

After choosing the scenario, replace the primitive geometry and animation while preserving the independent sensor and observer cameras. Update the visual inspection definition, prompt version, randomization ranges and labels together. Regenerate data and repeat host, quantized-EVK and end-to-end tests.

For the final tutorial, retain a reproducible asset-import procedure, a few representative sensor images, frozen evidence records and launch instructions. Keep licensed assets and large generated datasets outside Git.

## Explore the thermal sensor

The independent thermal prototype is described in [thermal rendering and controls](thermal_rendering.md). Press Tab or Xbox Y to switch observer modalities, H to cycle temperature/process cases, K to cycle thermal palettes, and F9 to save linear apparent-temperature pixels with metadata. Its GPU preview stays separate from the fixed NIR sensor and the NIR G/A/R gate. A thermal training contract and adapter are still pending. See [thermal research](thermal_rendering_research.md) and [actual GPU QA](thermal_qa.md) for design evidence and limitations.

The thermal view now includes object-local surface gradients and thin-part cooling. J / Xbox D-pad up compares spatial fields with the original uniform mode; U / D-pad down advances the selected cooling age by 15 seconds. Powered heaters maintain their source. [Spatial thermal fields](thermal_spatial_fields.md) documents the approximation and its measured rendering behavior.
