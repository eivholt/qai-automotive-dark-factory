# Spatial thermal fields

Research and numerical check: 2026-09-11. **This guide documents the legacy isolated-cooling comparison (O disables exchange).** The default coupled model is described in [heat transfer](thermal_heat_transfer.md). The demo now evaluates a temperature pattern in each component's local coordinates, so its warmer regions move with the part. Smaller passive features approach ambient faster. These are authored surface-temperature approximations; the [thermal rendering guide](thermal_rendering.md) describes the separate camera, radiometry and image contract.

## Physical basis and limits

A warmer central region with cooler edges can be plausible for an internally heated component losing heat through its boundaries. It is not a universal temperature pattern. At steady state, a constant-conductivity solid with volumetric heating satisfies `k ∇²T + q_gen = 0`; boundary convection, mounting contacts and heat-source locations determine the solution. Externally heated surfaces can instead be hotter than the interior. [MIT's heat-transfer equation sheet](https://ocw.mit.edu/courses/2-051-introduction-to-heat-transfer-fall-2015/e32f70f7e00f34953447b606cea0421f_MIT2_051F15_EqnSheet_Q2_v3.pdf) provides the governing equations.

The camera represents **radiation from the visible surface**, including reflected surroundings. It does not see the internal core through an opaque housing. Internal heating can warm that housing and create a surface pattern. The shader preserves depth rejection: a cooler cover hides a hot part behind it. [FLIR's explanation of thermal occlusion](https://www.flir.com/en-gb/discover/home-outdoor/can-thermal-imaging-see-through-walls/) distinguishes surface heating from seeing through an object.

For spatially uniform temperature and small Biot number, the cooling time constant is `τ = ρ cp V / (h A)`. Larger exposed area relative to volume gives faster cooling under otherwise comparable conditions. [MIT's transient heat-transfer derivation](https://web.mit.edu/course/16/16.unified/www/FALL/thermodynamics/notes/node129.html) explains those assumptions. The implementation borrows this relative size dependence, then adds an authored spatial field; their combination is not a solution of the heat equation.

A passive rib attached to a warm body is better described by conduction from its base and loss along its exposed surface. The fin equation contains `m² = h P / (k Ac)`. Thin actively powered strips can remain hot, and strongly coupled ribs need not cool independently. [MIT's fin analysis](https://web.mit.edu/16.unified/www/SPRING/propulsion/notes/node128.html) explains the role of conduction and boundary conditions. The starter does not solve this coupling.

## Implemented field

[DarkFactoryThermalRig.cpp](../unreal/Source/DarkFactory/DarkFactoryThermalRig.cpp) derives side lengths `x, y, z` in centimetres from the mesh-local bounding box and its world scale. Rotation does not change these lengths. The CPU computes:

```text
V = x y z
A = 2(x y + x z + y z)
Lc = max(0.01 cm, V / A)
τ = (30 seconds/cm) Lc
retention = exp(-coolingAge / τ)       # passive components
retention = 1                        # powered window heaters
coreSurface = ambient + (source - ambient) retention
edgeSurface = ambient + (coreSurface - ambient) edgeFraction
```

Ambient is 24 °C. The common coefficient of 30 seconds/cm is an illustrative setting, not a measured material property. Bounding-box volume and area approximate solid boxes; they do not describe hollow housings, internal construction, real exposed area or thermal contact. A below-ambient part approaches ambient from below, so its boundary can be warmer than its centre.

The [thermal post-process shader](../unreal/Shaders/ThermalPostProcess.usf) reconstructs the visible world position from depth and transforms it to normalized mesh-local coordinates `q`. Bounding-box faces lie at a coordinate of ±1. The scalar field is:

```text
w = exp2(-falloff * max(dot(q, q) - 1, 0))
surfaceTemperature(w) = edgeSurface + (coreSurface - edgeSurface) w
```

Face centres have `w = 1`; box edges and corners decrease smoothly. `edgeSurface` is the interpolation endpoint approached by the falloff, not an assertion that every geometric edge reaches that exact temperature. This produces a broad warm surface centre without camera-dependent edge darkening. For other geometry the appearance can differ: a centred sphere may have a nearly uniform surface. Mesh-specific source/contact fields should replace this generic shape when final assets and construction are known.

| Component | Source and field settings | Interpretation |
| --- | --- | --- |
| Battery modules | 45 °C normally; 95 °C fault; edge fraction 0.22, falloff 2.4 | Broad casing pattern; passive cooling age reduces both |
| Module ribs | Same initial source as their module; default field | Their smaller `V/A` cools faster; no base-to-tip conduction model |
| Robot pedestal / arm parts | 38 / 48 °C; default field | Illustrative warm components; motor locations are not resolved |
| Window heater strips | 65 °C; faulty strip 24 °C; edge fraction 0.55, falloff 1.1 | Powered strips retain their source regardless of cooling age |
| Sealant beads | 58 °C; edge fraction 0.32, falloff 1.8 | Passive cooling with broader warm centre; no cure kinetics |

Battery field parameters should remain replaceable. [NREL's thermal-characterization work](https://research-hub.nlr.gov/en/publications/battery-thermal-characterization-2/) combines measurements and models of cells, interconnects and cooling; primitive geometry does not supply that information.

## Radiometry and GPU work

For each changed profile, the CPU evaluates the existing flat-response 8–14 µm Planck integration at edge, midpoint and core surface temperatures. Each evaluation mixes emitted radiance with reflected 24 °C ambient radiance using emissivity, then inverts the band radiance to obtain apparent temperatures `Pe`, `Pm`, `Pc`. The GPU approximates their smooth curve:

```text
a = 2(Pc + Pe - 2 Pm)
b = 4 Pm - 3 Pe - Pc
apparentTemperature = clamp(Pe + b w + a w², min(Pe,Pc), max(Pe,Pc))
```

There is no per-pixel Planck integration or iterative inversion. The existing observer and fixed thermal capture use the same field. The fixed sensor continues to encode apparent temperature linearly over 20–120 °C in grayscale; observer palettes remain presentation only. This changes the thermal input distribution and uses the separate `dark-factory-thermal-sensor-v3` contract; v2 introduced the isolated fields. It does not change the NIR PNG contract or establish a trained thermal adapter.

The original 256 × 5 RGBA32F profile texture occupied **20 KiB**. The shared current texture is 256 × 22 (**88 KiB**) for coupled heat transfer, one optional tool hotspot, four optional car-weld patches, and 255 registered component IDs plus background. Rows 12–13 hold the tool hotspot; rows 14–21 hold four weld pairs, each with world-space centre/inverse radius squared and surface temperature. The spatial shader contains eleven source-level LUT loads, including all five hotspot-enable checks. Exchange adds one face read, an active tool adds one temperature read, and each active weld patch adds one temperature read. Current imported tools use twelve reads; the one/two-spot car panels use twelve/thirteen. The shader also uses scene depth, custom depth and stencil, three affine dot products, `exp2` and the quadratic radiometric fit. Everything shares the existing thermal conversion pass. These counts are not GPU timing measurements. Turning fields off retains the body-only uniform comparison.

Profile radiometry is recomputed on temperature/cooling/scale changes. Translation and rotation update cached affine transforms; the texture uploads only when profiles or transforms change. Uploads use `UpdateTextureRegions` without recreating the texture. The current 88 KiB upload is CPU-to-GPU parameter traffic, not image readback. Scene capture, custom depth, pixel shading, optional snapshot readback and PNG encoding still have costs. The recorded validation of the spatial implementation before tool hotspots passed all 55 spatial-field checks and all 42 existing thermal checks. The numerical measurements below cover their stated temperature ranges; they do not establish the same error bound for hot tool tips or car-weld patches. GPU timings remain unmeasured; no frame-rate improvement is claimed.

## Numerical approximation check

A standalone C++17 check used the actual [radiometry helper](../unreal/Source/DarkFactory/DarkFactoryThermalRadiometry.cpp), float shader coefficients and clamping, with fused multiply-add disabled. The reference inverse was bracketed with a monotonic radiance table at 0.0005 °C spacing; the maximum-error sample was also evaluated with the helper's 24-step binary inverse.

| Sampled range | Evaluations | Largest checked error | Conservative reference-bracket bound over sampled points |
| --- | ---: | ---: | ---: |
| Core 22–95 °C every 0.5 °C; emissivity 0.70/0.85/0.90/0.92/0.94/0.95/0.96; edge fractions 0.22/0.32/0.55; weight 0–1 every 0.002 | 1,546,587 | 0.025077 °C | 0.025542 °C |
| Twelve current component/source combinations, including module/rib normal and fault, robot parts, heater normal/fault, sealant, pane and plate; cooling age 0–90 seconds every second; same weight grid | 547,092 | 0.008870 °C | 0.009331 °C |

The first maximum occurred at core 95 °C, emissivity 0.70, edge fraction 0.22 and weight 0.206. These are dense sampled checks of the approximation against the implemented radiometry, not a continuous mathematical bound, a GPU precision measurement or physical camera accuracy. Retest changed temperature ranges or emissivities. The 8-bit 20–120 °C encoding itself has steps of approximately 0.392 °C.

## Controls and reproducibility

- **J** toggles spatial fields. Off restores the original uniform source-temperature appearance, including removal of cooling-age attenuation; it is not an equal-average-temperature comparison.
- **U** advances the selected cooling age by 15 seconds, cycling through 0–1800 seconds. It starts at 15 seconds and resets to 15 when the thermal case changes. Time does **not** advance automatically with wall-clock time.
- **H** selects the thermal case; **T** selects thermal observer mode; **K** changes its palette; **F9** saves the fixed thermal sensor snapshot.

Snapshots record the field model, cooling age, source/core/edge temperatures, emissivity, shape parameters, characteristic length, retention and component transforms. Preserve those parameters with future training episodes. This remains an authored surface model with flat spectral response, no atmospheric attenuation, no directional reflection geometry and no coupled heat transport. It cannot establish absolute-temperature accuracy or justify seeing through opaque parts.

Actual GPU evidence is recorded in [thermal QA](thermal_qa.md) and [portable spatial validation](thermal_fields_validation.json). The tests use visible stencil-ID masks to restrict samples to the intended surfaces and verify that temperature fields follow translated and rotated parts.
