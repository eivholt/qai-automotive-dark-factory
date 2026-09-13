# Car-weld surface heat

Four spots on the car now heat during welding and cool afterward in visible, NIR and both thermal presentations. Their temperatures and positions come from one authored state in [DarkFactoryWeldingCell.cpp](../unreal/Source/DarkFactory/DarkFactoryWeldingCell.cpp). The fields represent a visible surface effect; they do not solve sheet-metal conduction, a weld pool, melting or a calibrated infrared response. Imported bulk heat exchange remains disabled.

## Binding heat to the car

At startup, each robot's five-point, approximately 2 cm seam supplies its midpoint and surface normal. A complex component trace runs 3 cm in each direction along that normal against the actual body panels. The nearest hit must be within 0.5 cm of the authored midpoint; initialization fails if a seam cannot bind. This search happens once, before the carriage moves. In editor runs, only candidate meshes wait for asynchronous asset compilation and collision-state creation before tracing. Each hit is then stored in that panel's local coordinates.

| Robot | Heated panel |
| --- | --- |
| `robot_px_py` | `SM_BodyPanel_0390` |
| `robot_nx_py` | `SM_BodyPanel_0372` |
| `robot_px_ny` and `robot_nx_ny` | `SM_BodyPanel_0386` (two distinct spots) |

All four spots use a **12 cm world-space radius**. The deliberately broad field covers the short seam; its width is an artistic parameter, not a predicted heat-affected zone. Only the bound panel's material and thermal stencil profile receive that spot. The local centre follows the panel through carriage motion and the offscreen body reset. Nearby meshes cannot acquire heat merely by entering the radius. Within one thin panel, this radial field is not a surface-geodesic or through-thickness solution.

## Heating, cooling and reset

For cycle phase `p = elapsed % 18`, the shared temperature is:

```text
active = clamp(p - 4.5, 0, 7.5)
cooling = max(p - 12, 0)
T = 20 + 400 * (1 - exp(-active / 0.65)) * exp(-cooling / 2.5)
```

The field starts at ambient **20 °C**, heats during **4.5–12 s** toward **420 °C**, then cools with a **2.5 s** time constant. Approximate analytic values are 420.0 °C at 12 s, 167.2 °C at 14.5 s and 74.1 °C at 17 s. At the **18 s** wrap, the replacement body's spots reset to 20 °C. Cold entries retain their positions and radius with zero glow. Pausing the production cycle freezes this heat clock.

[Tool cooling](welding_tool_cooling.md) remains independent: the same tools retain residual warmth across cycles. A new car's reset must not reset tool temperatures.

## Shared spatial field and modality response

Each surface point uses the compact kernel below, where `d` is distance from the transformed spot centre and `r = 12 cm`:

```text
s = clamp(1 - d² / r², 0, 1)
weight = s² * (3 - 2s)
glow = 180 * clamp((T - 70) / 350, 0, 1)²
```

Visible/NIR materials add `glow × weight × (1, 0.08, 0.008)` as warm-orange emission. Existing PBR channels and source emission remain intact. The NIR capture retains its existing grayscale processing. These emission values are an authored visualization, not a claim that metal visibly glows at 70 °C or a spectral calculation.

Thermal uses the same positions, radii and weights to blend the original panel surface temperature toward each spot temperature. It takes the maximum candidate, so overlapping patches are order-independent and cannot cool prior heat. Emissivity/reflected-ambient radiometry is applied afterward. Depth rejection and the exact component stencil prevent heat showing through other opaque geometry.

[DarkFactoryThermalRig](../unreal/Source/DarkFactory/DarkFactoryThermalRig.h) accepts up to four patches per imported mesh. Its **256 × 22 RGBA32F LUT is 88 KiB**: rows 14–21 hold four world-position/inverse-radius and temperature pairs. Transform and temperature updates share an upload; no additional SceneCapture, per-pixel CPU solver or image readback is introduced. Unused pairs are zero. The existing tool hotspot stays in rows 12–13.

The fixed thermal sensor still encodes **20–120 °C apparent temperature** in 448 × 256 grayscale. Hot patch centres can saturate, including at the 14.5 s sample; a clipped pixel cannot establish the true surface temperature. Palettes remain display-only. Frozen snapshots include each profile's `weld_heat_patches`, with local/world positions, radius and surface temperature. Continuous heat updates preserve capture epochs and pending snapshot metadata. No welding inference or training contract is enabled.

## Frozen capture check

The rendered comparison holds the car, robot and cameras at the same **13 s geometry pose**, suppresses arc/spark effects, and changes only body heat time: **12 / 14.5 / 17 / fresh 18 s**. It records visible/NIR/thermal screenshots, separate raw sensor PNGs and exact panel-ID masks. Metadata checks verify all four temperatures, stable local positions, correct moving transforms, the two spots on panel `0386`, pause behavior and fresh-body reset. Image evidence must compare the same masked regions within each modality; saturated centres and whole-frame means are insufficient. All generated captures stay local and are not training exports.


## Local validation (2026-09-12)

The final rendered run passed **352 car-weld heat checks**, **208 production checks**, and **129 tool-cooling checks**. All four fields bound to the intended three panels and retained their local centres through seven live-cycle poses. They cooled after withdrawal, followed departure, and reset to 20 C with zero glow on the next body. The shared panel retained both of its distinct spots.

Two front spots showed both cooling transitions and fresh-body reset in every image family: visible, NIR, thermal, raw NIR and raw thermal. The other two are occluded in this camera arrangement; their scalar, attachment and profile checks pass, but these views do not establish their visible cooling. For the left front spot, the exact-panel raw NIR region falls from 103.29 to 101.62 to 88.71 to 88.25 counts across hot/warm/cool/fresh. These are regional pixel values, not direct temperatures. The fresh raw thermal regions return to the original panel baseline within one quantized code.

The Unreal build, 34 Python unit tests, and thermal rendering/spatial-field/heat-transfer regressions passed (42/55/138 checks). A native material replay audit confirmed all ten V2 materials unchanged on reapplication, with no duplicate graph additions. Fixed sensor lighting and observer-mode parity remain covered by the production regression. All image evidence and reports remain private in `artifacts/body-weld-heat-final/` and `artifacts/body-weld-thermal-regression/`.
