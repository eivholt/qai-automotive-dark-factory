# Infrared vision: evidence and Unreal design

Use **active NIR inspection with an intensifier-style green display**. These are two choices: illumination forms the image; the display palette presents it. Keep the existing grayscale model PNGs separate from the observer and UI effects.

## Three different imaging mechanisms

An active NIR camera receives reflected near-infrared illumination. It needs returning photons, so shadows, blocked sightlines and poorly reflecting surfaces still matter. In complete visible darkness, dedicated IR illumination can supply those photons. Conventional day/night cameras commonly present this as grayscale; material brightness can differ from its visible-light appearance. This supports the fixed illuminated inspection cell, but ordinary Unreal material colors are not measured NIR reflectance. [Axis, IR in surveillance](https://whitepapers.axis.com/en-us/ir-in-surveillance)

An image intensifier converts incoming photons into electrons at a photocathode, multiplies electrons in a microchannel plate, then converts them back into visible light at a phosphor screen. Hamamatsu specifies P43 as yellowish green, with peak emission around 545 nm. **Green is the output light, not the wavelength of the infrared scene.** The input may include visible light and NIR, depending on the photocathode. [Hamamatsu, Image intensifier, pp. 3–4](https://www.hamamatsu.com.cn/content/dam/hamamatsu-photonics/sites/documents/99_SALES_LIBRARY/etd/II_TII0007E.pdf)

White-phosphor intensifiers also exist. Phosphor color therefore does not identify a different kind of infrared illumination. Tube halos are bright regions surrounding concentrated sources; they are not a universal glow around every object. [Photonis/Exosens, Night vision at a glance](https://www.exosens.com/sites/default/files/inline-files/Photonis%20NV_One%20pager_09112023_compressed.pdf)

Thermal cameras instead detect emitted thermal infrared energy and display differences in that signal. A green tint cannot turn a reflected-light renderer into thermal imaging. The current demo has no surface-temperature or thermal-emission model. [FLIR, Thermal imaging and night vision](https://www.flir.com/en-au/discover/ots/thermal-vs-night-vision/)

## What noise and gain should suggest

Photon shot noise follows counting statistics: for mean signal S electrons, its standard deviation is approximately √S. Absolute noise grows with signal, while relative noise falls as signal grows. Read noise adds a floor even in darkness. In a simplified sensor, SNR is approximately S/√(S + read_noise²), before additional noise sources. [Hamamatsu, CMOS sensor technical note, noise section](https://www.hamamatsu.com/content/dam/hamamatsu-photonics/sites/documents/99_SALES_LIBRARY/ssd/cmos_linear_kmpd9017e.pdf)

Ordinary gain increases brightness and noise together; it generally does not improve SNR. Excessive gain can saturate digitized values and reduce dynamic range. Full-well saturation and output clipping are related but distinct limits. Electron multiplication inside an intensifier is also different from multiplying finished screen pixels. [Hamamatsu, Camera Simulation Engine](https://www.hamamatsu.com/sp/sys/en/camera_simulator/index.html)

## Recommended visual treatment

These are artistic starting points inferred from those mechanisms, not measured settings for a particular camera:

- Derive one intensity channel and map it through near-black, dark green, medium green and pale yellow-green. Preserve useful brightness steps across the modules. Avoid a constant green overlay that fills empty shadows.
- Add fine, zero-mean animated grain with a small noise floor and a modest signal-dependent component. Keep relative grain more conspicuous in dim areas. Scaling noise with tone-mapped luminance is a visual approximation, not photon simulation.
- Let gain raise signal and grain; use a smooth highlight shoulder before final clipping. Do not recover geometry from a completely black input.
- Add restrained, thresholded halos around the brightest image regions. A few neighbor samples can imitate a soft halo, but cannot reproduce tube electron optics or optical flare.
- Apply a mild, aspect-correct vignette that leaves every tray corner readable. Treat it as lens/display styling. Strong tunnel masks, scanlines, large grain and honeycomb patterns should be optional effects rather than defaults.
- Freeze grain time/seed when taking comparison screenshots. Presentation grain must never alter training labels or exported model pixels.

## Unreal implementation boundary

Use a Post Process material on the spectator camera, plus a separate UI material for its sensor preview. Sample the previous pass through PostProcessInput0 and output through Emissive Color. Apply the presentation after tonemapping so subsequent filmic grading does not remap the green palette; this is also suitable for visible screen-space grain after temporal antialiasing. Match UVs to the viewport and use actual texture dimensions for neighbor offsets. [Epic, Post Process Materials](https://dev.epicgames.com/documentation/unreal-engine/post-process-materials-in-unreal-engine)

UE 5.8 names the location `BL_SceneColorAfterTonemapping`; check the installed engine enum when generating materials. [Epic, EBlendableLocation](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/EBlendableLocation)

This remains a presentation approximation: no wavelength-dependent quantum efficiency, calibrated electron counts, spectral material response, measured lens shading, phosphor persistence, intensifier gain statistics or temperature field. Validate the look visually and verify that toggling it leaves the fixed 448 × 256 grayscale inference/capture path unchanged.

