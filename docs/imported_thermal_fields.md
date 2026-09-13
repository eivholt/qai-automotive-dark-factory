# Imported robot thermal fields

The warm castings in the imported welding cell now have authored surface-temperature variation. The earlier component profiles all had `core_edge_drop_c=0`, so their spatial interpolation returned the same temperature everywhere. The recipe now sets nonzero drops on seven casting roles across four robots: 28 components in total.

These are illustrative warm-core patterns. They do not estimate temperatures from visible textures, infer wall thickness from a bounding box, or solve heat conduction through the purchased geometry. The imported bulk contact/radiation network remains disabled. Independent tool cooling, car-weld patches and spark temperatures continue to use their existing runtime states.

## Reproducible component recipe

`update_thermal_fields(scene)` in [prepare_welding_cell.py](../scripts/assets/prepare_welding_cell.py) applies these values without changing source temperatures, emissivity, geometry, transforms, joints or solved IK knots:

| Robot role | Authored core temperature | Core-to-edge drop | Edge bound |
| --- | ---: | ---: | ---: |
| Elbow | 57 C | 30 C | 27 C |
| Flange | 49 C | 22 C | 27 C |
| Shoulder | 44 C | 20 C | 24 C |
| Wrist | 43 C | 18 C | 25 C |
| Forearm | 39 C | 15 C | 24 C |
| Upper arm | 34 C | 10 C | 24 C |
| Fixed base | 25 C | 3 C | 22 C |

Each row applies to all four `SM_robot_*` groups. Tool base drops stay zero because tool housing/tip heat has its own dynamic field. Dress packs, the 23 C body panels, holders and disconnected background groups also retain zero base drop. Car-weld spots remain independent of that uniform panel baseline.

The helper validates finite source temperatures and emissivity, all 28 expected component IDs and nonnegative edge bounds before changing any thermal record. It is idempotent and keeps `simulate_exchange=false`. Full preparation and the fast `update_park_cycle(scene)` helper both apply it. For this update, call the thermal helper on the existing local `scene.json`; full regrouping, FBX import and IK recomputation are unnecessary. `--cycle-only` also recomputes IK, so it is not the schedule/thermal-only path.

## GPU field

Unreal reconstructs the visible pixel's world position from depth, then uses the component's affine LUT rows to obtain normalized mesh-local coordinates `q`. Local bounding-box extrema map to -1 and +1. The longest local half-extent selects the core axis; equal extents prefer X, then Y. The field rotates and translates with the component and remains independent of the camera viewpoint.

```text
axis = longest mesh-local bounds axis
q[axis] -= -0.18
r2 = 0.45 * dot(q, q) + 0.55 * q[axis] * q[axis]
weight = exp2(-2.4 * r2)
edge = min(core, max(ambient, core - authored_drop))
surface_temperature = lerp(edge, core, weight)
```

The slight offset places the warm core toward the negative end of the chosen local axis. The kernel has no flat interior plateau, so curved and irregular casting surfaces can show a gradient inside their bounds. Core and edge are bounds on the authored field; a particular surface need not reach either extreme. This is a shape attached to each casting, not a measured internal motor or thermal mesh.

The procedural box-field formula remains unchanged. Tools keep their compact tip hotspot, and car-weld spots keep their compact, order-independent maximum blend. J / Xbox D-pad up selects the existing uniform thermal comparison; that comparison also suppresses thermal tool/weld spatial fields, while their visible/NIR material state is separate.

CPU profile updates still calculate radiometric samples using the existing 8-14 micrometre Planck-band/emissivity approximation. The GPU interpolates apparent temperature and preserves the scene-depth/custom-depth occlusion check. No RGB luminance, extra camera, CPU image processing or image upload supplies these gradients.

## LUT and metadata

The shared LUT remains **256 x 22 RGBA float, 88 KiB**. Row 13 now packs `(tool_tip_temperature_c, imported_core_mode, dominant_local_axis, core_offset_normalized)`. No new row or image pass is needed. The baseline spatial shader has twelve source-level LUT loads; optional exchange adds one. Tools reuse the row-13 temperature, while one/two active car-weld patches bring their panels to thirteen/fourteen loads. These source counts are not measured GPU transactions or frame-time results.

Snapshot profiles expose `imported_core_field_enabled`, `core_field_axis`, `core_field_offset_normalized` and `core_field_kernel`, alongside source/core/edge temperatures. The enabled flag reflects the actual spatial-comparison setting. A component without an authored drop reports axis -1; toggling the comparison does not erase configured parameters.

The raw thermal camera remains linear 448 x 256 grayscale over 20-120 C apparent temperature. White-hot, black-hot and iron are display palettes over that same image. NIR material and camera paths are unchanged by the casting recipe.

## Validation

The in-memory recipe check passed: exactly 28 nonzero drops and 48 uniform base profiles, unchanged geometry/IK/source temperatures/emissivity, a minimum edge bound of 20 C, and unchanged results on replay. Invalid temperatures and missing expected roles were rejected before any partial thermal update.

The rendered `artifacts/imported-fields-v1` run passed **1,459 imported-field checks** using `scripts/verify_imported_thermal_fields.py`. Its frozen-pose comparison disables tool heat, car-weld heat and sparks, then captures spatial fields on, off and restored with identical geometry. Exact stencil-ID masks, eroded by one pixel, isolate each visible surface. All 216 profiles are retained; component names alone are not unique across the two portal actors.

Of the 24 castings with a drop of at least 8 C, 21 had at least 12 visible pixels after erosion. Twenty of those 21 passed both a 3-code minimum p90-minus-p10 spread and a 2-code minimum mean reduction from the uniform reference. Their spreads were 8-38 codes in the fixed 20-120 C sensor window. The distant `SM_robot_nx_py_shoulder` had only 12 selected pixels and showed no spread in this view; the report retains that limitation. Small or occluded parts do not provide image evidence. The four low-drop bases are reported separately.

Uniform-reference pixels stayed within one code of their assigned apparent temperature. Restoring the spatial field reproduced the thermal PNG exactly, and NIR PNGs remained byte-identical. Iron and monochrome screenshots were checked separately and the on/off comparison was visually reviewed. This establishes within-part variation at the captured pose; it is not a thermal calibration or a rendered proof of every surface throughout motion.

The same run passed 208 production-cycle, 129 tool-cooling and 352 car-weld-heat checks. A separate procedural ThermalSmoke run passed 42 radiometric, 55 spatial-field and 138 heat-transfer checks. The Unreal build and all 34 Python unit tests passed.

After rebuilding generated materials with `setup_content.py`, run `unreal/Scripts/run.ps1 -Scene welding -WeldingSmoke -NoInference -Output artifacts/imported-fields`, then verify its captures:

```powershell
python scripts/verify_imported_thermal_fields.py --directory artifacts/imported-fields --output artifacts/imported-fields/imported_fields_qa.json
```

The verifier requires Pillow. Also run the existing production, tool-cooling and car-weld verifiers, and the procedural ThermalSmoke checks required by AGENTS.md after thermal shader edits. Source assets and rendered diagnostics remain private under ignored `assets/`, Unreal `Content/` and `artifacts/`.
