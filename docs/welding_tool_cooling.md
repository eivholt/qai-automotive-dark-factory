# Welding-tool cooling and capture plan

The welding-tool preview uses one shared heat state for visible glow, the NIR view and thermal rendering. The following parameters are authored demonstration values. They do not describe a measured welding tool, a calibrated optical response or validated heat transport through the purchased mesh.

## Shared heat state

The production cycle lasts 18 seconds, with one second empty after departure. Welding heats each tool from 4.5 to 12 seconds. At the start of the first cycle, its body and tip are at the 20 °C ambient temperature. During welding, the body approaches 105 °C with a 2.0-second time constant and the tip approaches 500 °C with a 0.8-second time constant. After welding, each temperature decays exponentially toward ambient: the body uses a 3.8-second time constant and the tip uses a 2.3-second time constant.

For a constant target, the analytic update is `T(t) = target + (T(start) - target) * exp(-elapsed / tau)`. Later cycles carry the remaining heat across the cycle boundary; wrapping the animation clock at 18 seconds must not reset a still-warm tool to ambient. The next weld starts at elapsed 22.5 seconds, so the 12/15/21-second QA samples still cover the same first heating and cooling interval. The shorter cycle leaves 10.5 seconds between heating intervals; later cycles therefore retain more warmth than the earlier 22-second schedule.

| Elapsed heat time from the first start | Stage | Approximate body temperature | Approximate tip temperature |
| --- | --- | --- | --- |
| 12 s | Hot, immediately after welding | 103.0 °C | 500.0 °C |
| 15 s | Warm, after 3 s of cooling | 57.7 °C | 150.2 °C |
| 21 s | Cool, after 9 s of cooling | 27.8 °C | 29.6 °C |

The implementation uses the same tip heat retention for the `ToolHeatGlow` material parameter and a local thermal hotspot with a 26 cm radius. The thermal material receives the temperature state through the shared 22-row / 88 KiB lookup texture. The apparent-temperature output still depends on emissivity and the declared reflected environment; its fixed display range can saturate the hottest pixels. Visible/NIR glow is a visualization driven by the shared state, not a spectral blackbody calibration.

The same LUT now has four optional [car-weld patches](welding_body_heat.md) per panel in rows 14–21. These have an independent per-body heat clock; the tool's continuous heating and residual warmth are unchanged.

## Frozen capture experiment

The offline check holds machinery at a 6-second welding pose so the tool tips stay in the camera views. Robot joints, the carriage and both camera poses remain fixed. Arc and spark geometry, their flash lights, and curtain simulation are stopped. Roof illumination, NIR illumination, observer settings and deterministic grain parameters remain unchanged within each modality. Only `SetToolHeatTimeForQA(seconds)` changes the analytic heat time to 12, 15 and 21 seconds.

For each stage, save native 1600 × 900 screenshots:

- `tool_cooling_{hot|warm|cool}_visible.png`
- `tool_cooling_{hot|warm|cool}_nir.png`
- `tool_cooling_{hot|warm|cool}_thermal.png`

Also save the distinct 448 × 256 raw images `tool_cooling_{hot|warm|cool}_raw_nir.png` and `tool_cooling_{hot|warm|cool}_raw_thermal.png`. Raw files and observer screenshots must not use the same basename. Thermal raw images use the declared fixed grayscale encoding; NIR raw images retain the NIR contract. These are explicit offline QA exports and do not publish to the model, a dataset or training.

## Metadata and verification requirements

`tool_cooling.json` supplies `passed`, `geometry_unchanged`, `pause_freezes_tool_heat`, the frozen `surface_id_mask` filename, and three `stages` named `hot`, `warm` and `cool`. Each stage contains `heat_time_seconds`, `cycle_boundary_continuous`, four tool records, explicit image mappings and a `thermal_snapshot`.

Each tool record has stable `id`, `surface` and `stencil_id` values; `body_temperature_c`, `tip_temperature_c`, `glow_strength` and `heat_radius_cm`; `tip_world_cm` and `tip_local_cm`; `observer_in_frame` and `sensor_in_frame`; and integer `[left, top, right, bottom]` boxes in `observer_roi_px` and `sensor_roi_px`. The image mapping uses `visible`, `nir`, `thermal`, `raw_nir` and `raw_thermal` keys. Observer and raw files must be distinct.

The snapshot profiles are matched by `surface` and `stencil_id`. Their `source_temperature_c`, `tool_tip_surface_temperature_c`, `tool_hotspot_radius_world_cm`, and hotspot local/world positions must agree with the shared tool state. This catches a material update that reaches the visible glow but leaves the thermal LUT stale. The fixed thermal range and 22-row LUT declaration are checked separately. The broad body-to-tip radiance fit is an approximation; this checker does not assert sub-0.1 °C radiometric accuracy for a 20–500 °C field.

The verifier requires strict cooling of body temperature, tip temperature and glow for all four tools. Geometry-related tool metadata and regions must remain fixed. For every observer image family (visible, NIR, thermal) and both raw sensor families, at least one usable tool must show a mean-luminance decrease greater than **0.05 8-bit counts at each transition**. All per-tool measurements and exclusions are reported; one visible tool cannot stand in for all four tools' scalar-state checks.

Raw measurements intersect the projected sensor region with the exact 8-bit stencil ID in `tool_cooling_surface_ids.png`. Zero selected pixels means the tool is occluded or absent from that region and cannot count as image evidence. At least one selected pixel is required; the report exposes this count so tiny coverage is visible. Observer regions are projected boxes without an observer stencil mask, so they include some static background; the low mean-change threshold compensates for this dilution. `*_in_frame` records projection availability and does not itself prove absence of occlusion.

Raw NIR and thermal images must be grayscale RGB at 448 × 256. Observer screenshots must be 1600 × 900. Saturation can hide a temperature change at an individual hot pixel; the test therefore uses regional means and still requires evidence at both transitions. Every threshold and tolerance is written into the JSON report.

The whole frame is unsuitable for this test because static machinery dominates its mean. Cross-modality pixel equality is also inappropriate: palettes, exposure and sensor encoding differ. The test compares a fixed region over heat time within one modality, while metadata verifies that all modalities used the same analytic heat state.

## Run the verifier

After the runtime captures have completed:

```powershell
python scripts/verify_tool_cooling.py --directory artifacts/tool-cooling-welding --output artifacts/tool-cooling-welding/cooling_qa.json
```

Use any existing Pillow environment. The repository's current WSL environment can run the same script:

```powershell
wsl.exe -e /home/eivho/cosmos-reason2-finetune/.venv/bin/python /mnt/c/Users/eivho/repos/qai-automotive-dark-factory/scripts/verify_tool_cooling.py --directory /mnt/c/Users/eivho/repos/qai-automotive-dark-factory/artifacts/tool-cooling-welding --output /mnt/c/Users/eivho/repos/qai-automotive-dark-factory/artifacts/tool-cooling-welding/cooling_qa.json
```

Exit 0 means every declared check passed; exit 1 writes missing inputs, measured failures and exclusions into the report. The script does not launch Unreal, install dependencies or call a model. Whole-frame means are included only as context and cannot satisfy a cooling check.

## Earlier validation with the 22-second production cycle

The earlier actual GPU captures in `artifacts/tool-cooling-welding`, using the 14-row LUT before car-weld fields, passed **129 checks**. The saved report is `artifacts/tool-cooling-welding/cooling_qa.json`. All four tools' body/tip temperatures and glow decrease, their thermal snapshot profiles agree, geometry remains fixed, and pause/cycle-boundary checks pass.

| Image evidence | Tools meeting both cooling transitions |
| --- | --- |
| Visible observer regions | 3 of 4 |
| NIR observer regions | 3 of 4 |
| Thermal observer regions | 4 of 4 |
| Stencil-masked raw NIR regions | 3 of 4 |
| Stencil-masked raw thermal regions | 4 of 4 |

For `robot_nx_ny`, 35 sensor pixels match its tool stencil inside the projected region. Their raw thermal mean falls from **225.54 → 136.51 → 18.17** 8-bit counts; raw NIR falls from **184.23 → 172.86 → 109.71**. These are regional image values, not direct tip-temperature readings. Optical image evidence is established for three tools in this camera arrangement; the fourth tool's shared heat state and thermal cooling are validated, but its visible/NIR region does not meet the two-transition criterion.

A negative control reused the hot images for all three stages while retaining decreasing temperature metadata. The verifier correctly failed all five image-family cooling checks. That control is stored separately under `artifacts/tool-cooling-verifier-negative`; original captures were not modified.

This is rendering and synchronization evidence for an authored demonstration, not a validated welding-temperature measurement. Re-run the verifier after changing geometry, cameras, material response or cooling parameters.