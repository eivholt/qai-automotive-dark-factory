# Existing night-vision implementations and performance lessons

This review inspected source, render-pass setup and licensing at the immutable revisions linked below. It did not install these implementations, copy third-party code or benchmark them on this demo. Pass and texture-sample counts are derived from source; compiler optimization, render resolution and platform determine actual GPU cost.

The practical recommendation is to retain a combined GPU display pass, make halo sampling genuinely optional, activate it only on intended views, and measure capture/readback separately from display shading. A compute-shader rewrite is not automatically faster.

## 1. ReShade: Alepap preset flattened by Zackin5

Revision `07335e011ffd6bc21b921dd810856ab52c985322`.

The technique declares **23 passes** after expanding its macros: one backup, three pseudo-thermal stages, one initial night-vision stage, sixteen bloom/adaptation stages, and two final stages. Several use reduced-resolution targets, so this does not mean 23 full-resolution passes. Disabling the thermal composite boolean does not remove the three earlier thermal passes from the technique. This is a useful example of why a visually disabled feature can still consume rendering work. [Technique source, lines 188 onward](https://github.com/Zackin5/Misc-ReShade-Shaders/blob/07335e011ffd6bc21b921dd810856ab52c985322/nvg_alepap.fx#L188)

Its grain helper evaluates three noise channels using eight permutation-texture samples each. The main shader calls that helper twice: **48 source-level permutation samples per shaded pixel**, before other image samples. A small procedural hash can avoid those texture accesses when the desired look only needs fine monochrome grain; visual quality and instruction cost still need measurement. [Grain source](https://github.com/Zackin5/Misc-ReShade-Shaders/blob/07335e011ffd6bc21b921dd810856ab52c985322/nvg_alepap/nvg_film_grain.fxh)

The bloom implementation uses a half-resolution source and a seven-level pyramid. Its ordinary downsample/upsample filters use five taps; the higher-quality branches use more. The useful pattern is to blur large halos at reduced resolution. That entire pyramid is excessive for our small local halo unless a visual requirement justifies it. [Bloom source](https://github.com/Zackin5/Misc-ReShade-Shaders/blob/07335e011ffd6bc21b921dd810856ab52c985322/nvg_alepap/nvg_bloom.fxh)

**License:** mixed. The grain header explicitly carries MIT notices for prod80 and Stefan Gustavson. The bloom header says copyright/all rights reserved; the inspected repository has no umbrella license establishing a uniform reuse grant. Treat this collection as a reference, not a package to transplant. Its pseudo-thermal processing also derives imagery from existing rendered pixels; it does not establish a temperature model.

## 2. Unity URP: Project PLATEAU NightVisionURP

Revision `c28b7f50aa51079b62dd416ef8d0ba321c3c3927`.

The shader itself has one pass and one color-texture sample. It computes luminance, applies a sensitivity curve and multiplies by a configurable color. It has no grain, halo or bloom. This illustrates how little shader work the basic palette needs. [Shader source](https://github.com/Project-PLATEAU/PLATEAU-SDK-Toolkits-for-Unity/blob/c28b7f50aa51079b62dd416ef8d0ba321c3c3927/PlateauToolkit.Rendering/Runtime/PostEffects/NightVisionURP.shader)

The renderer feature, however, acquires a full-size temporary ARGB32 target and issues **two blits**: copy scene color to the temporary target, then apply the material back to scene color. It returns the temporary resource during cleanup. `AddRenderPasses` enqueues the effect without an explicit camera-type filter in this file. Thus, shader pass count alone misses the copy and allocation/resource-management path. [Renderer source, including blits at lines 70–71](https://github.com/Project-PLATEAU/PLATEAU-SDK-Toolkits-for-Unity/blob/c28b7f50aa51079b62dd416ef8d0ba321c3c3927/PlateauToolkit.Rendering/Runtime/PostEffects/NightVisionURP.cs#L70)

**License:** MIT, copyright MLIT Japan. [Pinned license](https://github.com/Project-PLATEAU/PLATEAU-SDK-Toolkits-for-Unity/blob/c28b7f50aa51079b62dd416ef8d0ba321c3c3927/LICENSE)

**Lesson:** combine simple operations, account for intermediate copies, and explicitly choose which cameras receive the effect. This is an inspected example, not a verified drop-in implementation for current Unity or Unreal.

## 3. Unreal: A57R4L SceneViewExtensionTemplate

Revision `d2852f95f69a65c0d9b1c6e286fd9b557bb85a46`.

This template hooks a post-processing callback after Motion Blur. Its sample compute shader performs one scene-color sample and swaps channels. The extension allocates an output texture, dispatches one compute pass, then explicitly copies that output back into scene color. The enable check occurs inside the callback, after registration. Those implementation choices demonstrate the RDG integration, but provide no evidence that this approach beats an ordinary post-process material. [Extension source](https://github.com/A57R4L/SceneViewExtensionTemplate/blob/d2852f95f69a65c0d9b1c6e286fd9b557bb85a46/SceneViewExtensionTemplate/Source/SceneViewExtensionTemplate/Private/CustomSceneViewExtension.cpp#L27), [compute shader](https://github.com/A57R4L/SceneViewExtensionTemplate/blob/d2852f95f69a65c0d9b1c6e286fd9b557bb85a46/SceneViewExtensionTemplate/Shaders/PostProcessCS.usf)

**License:** MIT, copyright Ossi Luoto; Unreal itself remains subject to its own license. [Pinned license](https://github.com/A57R4L/SceneViewExtensionTemplate/blob/d2852f95f69a65c0d9b1c6e286fd9b557bb85a46/LICENSE)

Epic's current callback contract recommends subscribing only when an effect will produce a GPU resource and requires honoring an active override output. These details matter before adapting an older template. [Epic, SubscribeToPostProcessingPass](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/ISceneViewExtension/SubscribeToPostProcessingPass)

## Apply the patterns to this demo

These are recommendations from the review, not benchmark results or a claim that every optimization below is already implemented:

| Area | Recommended approach |
| --- | --- |
| Core display | Keep palette, gain, rolloff, procedural grain and vignette together. Draw the preview only over its UI rectangle. |
| Halo | Offer a one-sample core path and a center-plus-four-neighbor halo path. Use a uniform branch or shader variant that actually skips fetches when halo is off; multiplying the result by zero is insufficient evidence. Check compiled code/profile. |
| Grain | Use pixel coordinates and a frame/seed hash; avoid noise-texture bandwidth unless its quality improves the result. Updating the grain pattern at 24 Hz still shades every rendered frame; it does not reduce the number of fullscreen draws. |
| Larger bloom | Add a reduced-resolution blur only if the local halo fails visually. Include its downsample, intermediate targets, upsample and composite in timings. |
| Activation | Keep observer/preview presentation separate from the model sensor. Bypass unused passes instead of paying for a neutral output. An engine-wide view extension needs explicit view/camera filtering. |
| CPU/GPU transfers | Keep display pixels on the GPU. Read back the fixed sensor only when capture/inference needs it; reusing a GPU texture for preview avoids a display-only readback/upload loop. |

Readback is a separate potential bottleneck from shading. The original capture path included scene capture, `ReadPixels`, CPU grayscale augmentation and PNG encoding. The updated live path uses one bounded asynchronous GPU readback and worker-thread grayscale/PNG processing; deterministic offline capture retains the synchronous reference. Profile each stage. If synchronization dominates, investigate a bounded ring of GPU readbacks, consume only ready entries, and preserve capture timestamp, episode ID and buffer lifetime. Never wait immediately after enqueueing simply to reproduce synchronous behavior. Epic exposes `FRHIGPUTextureReadback` and an explicit readiness check. [Texture readback API](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/RHI/FRHIGPUTextureReadback), [IsReady API](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/RHI/FRHIGPUMemoryReadback/IsReady)

Asynchronous transfer trades immediate availability for deferred completion; it does not make the data free. Unity documents that GPU readback can avoid CPU/GPU stalls at the cost of latency. Keep queues bounded, reject stale episodes and measure capture-to-decision age alongside frame time. [Unity, efficient texture access](https://unity.com/blog/engine-platform/accessing-texture-data-efficiently)

For a controlled comparison, hold resolution, camera, scene, lighting, capture frequency and inference mode constant. Measure display disabled, core display, halo enabled and capture enabled separately. Report warmed GPU pass time, game/render-thread time and frame-time percentiles; avoid assigning unrelated model-service latency to the night-vision shader. Preserve [model-input separation checks](night_vision_qa.md) during any optimization.

