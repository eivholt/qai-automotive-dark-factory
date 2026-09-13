# Local welding-cell import and articulation

Validated locally on 2026-09-12 with Unreal Engine 5.8. This pipeline replaces the procedural visuals with the supplied car-body welding cell when its prepared assets are available. It is a visual motion preview; manufacturing steps and inspection gates still need their own scenario definition.

## Local inputs and outputs

Place `Car_Body_Welding_Robotics_Cell.FBX` and the adjacent JPEGs in `assets/Autodesk_FBX_2009/`. The entire root `/assets/` folder is ignored, as are generated Unreal `.uasset`/`.umap` files and `artifacts/` diagnostics. The preparation wrapper checks the original FBX hash; the importer checks all five source files before and after import.

Publish only tutorial text and code examples. Keep purchased assets, derived meshes, runtime builds, rendered captures, datasets and weights local/private under the repository's publication policy.

| Source property | Verified value |
| --- | --- |
| Format | Binary FBX 6.1 (`6100`), FBX 2009 export |
| File size | 200,511,808 bytes |
| Hierarchy | One null/group with ten material-group mesh children |
| Geometry | 911,619 vertices, 917,358 polygons |
| SDK triangulation | 1,761,208 triangles |
| Connectivity | 3,681 disconnected vertex islands |
| Attributes | One UV layer and one normal layer per mesh |
| Units / orientation | Millimetres, Y-up with root pre-rotation and geometric transforms |
| Animation | No source rig, skinning or animation takes |

The original meshes group surfaces by material across the entire cell. They are unsuitable as robot links or thermal bodies without regrouping. Source transforms include nonuniform scale, so raw local vertex bounds do not represent assembled dimensions.

## Prepare, import and run

Build the demo first using the main README. Preparation requires Unreal's bundled FBX SDK, MSVC C++ tools, and Python with NumPy. Pillow is used only for optional diagnostic projections.

```powershell
.\.venv\Scripts\python.exe -m pip install -r scripts\assets\requirements.txt
.\scripts\assets\prepare_welding_cell.ps1 -Python .\.venv\Scripts\python.exe
.\unreal\Scripts\import_welding_cell.ps1 -Mode prepared -Manifest assets\derived\welding_cell\manifest.json
.\unreal\Scripts\run.ps1 -Scene welding
```

`prepare_welding_cell.ps1` builds a local helper against Unreal's FBX SDK and writes only derived data under ignored `assets/`. It also supports `-UseWSL -WslDistribution Ubuntu -WslPython /path/to/python` when NumPy is installed in WSL. `-ScanOnly` stops after connectivity analysis. Use `-SkipBuild -SkipScan` only with an unchanged tool and source; scan provenance is checked.

The pipeline performs these steps:

1. Triangulate and bake source hierarchy, geometric transforms, units and axis conversion into Unreal centimetres. Transform normals using the inverse transpose.
2. Extract connected vertex islands, preserving triangle winding, corner UVs and normals.
3. Recover approximate joint centres and axes from circular surface geometry. Locate the four copies using each individual link's geometry, since their source poses differ.
4. Regroup islands into robot links, body-panel pieces, two carriage holders and static background meshes. Require every source island and triangle to be assigned exactly once.
5. Export an FBX with identity nodes and geometry local to each component pivot. Write an import manifest and a separate runtime scene manifest.
6. Import static-mesh assets with Unreal's legacy asset-only `FbxFactory`, then explicitly relink local textures and generate PBR materials. Validate every imported mesh against expected bounds before reporting success.

Asset-only import avoids the root-actor assertion encountered with the legacy scene-import factory in an unattended commandlet. The runtime actor assembles the validated assets from `scene.json`; it does not depend on an imported scene Blueprint.

Generated local files are under `assets/derived/welding_cell/`:

| File | Purpose |
| --- | --- |
| `scan/` | SDK connectivity data and source/tool provenance |
| `robot_joints.json` | Reconstructed rest pivots, axes, link anchors and fit diagnostics |
| `assignments.tsv` | One export-group assignment per source island |
| `prepared.fbx` | Separated meshes with component-local geometry |
| `manifest.json` | Import node names and expected local bounds |
| `scene.json` | Runtime mesh references, joint hierarchy, cameras and thermal profiles |
| `grouping_report.json` | Component roles, source membership and conservation checks |

Unreal assets are saved under `/Game/WeldingCell/`. A material-only update uses the same import command with `-SkipMeshImport`. `-Mode raw` is an optional diagnostic import of the ten original material groups; it does not create the articulated runtime.

## Separated geometry and motion

The prepared cell contains **76 mesh components**, including **29 separately addressable body-panel pieces**, **two complete carriage-holder assemblies**, and **24 transform joints across four robots**. Each robot has base yaw, shoulder, elbow, forearm roll, wrist and tool roll. Body pieces and holders are attached to the moving carriage. Body pieces can also be displaced, hidden or used in a chosen missing/loose-part scenario. The holder groups gather yellow supports and their wheel/drive hardware from multiple source material groups. `attachment: carriage` in the scene manifest declares their parenting. Their names identify extracted geometry; they are not verified manufacturing part labels.

The actor loads meshes and validates the hierarchy before creating components. `scene.json` locations and pivots are parent-relative centimetres; rotations use Unreal pitch/yaw/roll in degrees. Axes are local to their joint's rest frame. The generated scene uses identity rest rotations. Mesh data is local to its assigned pivot, so a link's transform moves all of its grouped islands together.

The runtime now uses a coordinated 22-second cycle with withdrawn arms, fast approach, small precision-welding movements and withdrawal. The body moves from an opaque entry tunnel, stops for welding, exits through another curtain, then is reused out of sight for the next body index. Space / Xbox View pauses the cycle and its physical effects. See [production-cycle implementation](welding_production_cycle.md).

Joint centres are reconstructed from source geometry, not industrial specifications; one source shoulder has a roughly 3.79 cm mating-centre offset. The four original rigid dress packs remain intact but hidden at runtime, replaced by five joint-anchored spline sections per robot. The carriage uses sparse shell colliders to push real hinged Chaos strips. Detailed visible meshes provide spark collision queries. This remains an authored demonstration trajectory without calibrated industrial IK or complete self-collision validation.

## Materials and sensor views

The importer explicitly relinks the three referenced 4096 x 4096 JPEGs: robot paint, floor tiles and control screen. The supplied 1024 x 1024 `term` JPEG is unreferenced. Old absolute texture paths and a Max procedural map cannot be used directly. Ten approximate PBR materials are generated; there are no supplied roughness/metallic/normal maps or measured infrared properties.

Chrome/body surfaces use an authored rough-steel preview: linear base colour `(0.38, 0.42, 0.45)`, metallic `0.55`, roughness `0.42`. This keeps the body visible without an environment reflection setup. Neither these constants nor the visible textures establish real NIR reflectance.

The observer retains free flight and visible, green/monochrome NIR and thermal palette controls. Fixed sensor cameras use the manifest viewpoint. Visible roof lights stay on view channel 0; NIR illumination stays on channel 1. The thermal observer starts with the iron color palette. The sensor panel retains both color and white-hot monochrome previews; these use separate display materials over the same raw thermal camera target and remain fixed while the observer changes palette. Live thumbnails sample GPU render targets without routine CPU readback or another scene capture. Native smoke captures use a separate offline QA path.

Imported thermal profiles assign illustrative source temperatures and emissivity per component. Twenty-eight warm robot castings now use authored mesh-local core-to-edge temperature fields that follow joint motion. Tools retain independent dynamic tip/housing cooling, and four car-weld surface patches heat and cool with each body. Spark/arc profiles provide separate cooling-particle temperatures; portals and curtains remain at ambient. **The imported bulk contact/radiation network and procedural thermal cases remain disabled.** The full heat-transfer network is available in `-Scene procedural`; applying it to the imported cell requires meaningful thickness, heat capacity, contacts and sources. A hollow car or disconnected material group must not be treated as a solid bounding-box thermal body. See [imported thermal fields](imported_thermal_fields.md) for the casting recipe, GPU kernel and validation status.

The existing battery-tray model prompt, labels, inspection gates and dataset paths are disabled in imported mode. P/C, procedural case selection and legacy scenario changes cannot send these images to the old adapter or export incorrectly labelled training data. F9 remains a private thermal snapshot with imported-scene metadata. A production sequence, inspection region, anomaly states and modality-specific capture contract are the next scenario decisions.

## Validation and limitations

Run the native imported-scene check:

```powershell
.\unreal\Scripts\run.ps1 -Scene welding -WeldingSmoke -Output artifacts\welding-smoke
```

Inspect `welding_preview.json` as well as the process status. The check exercises a full car/robot/curtain cycle and captures rest/moving visible views, NIR and thermal views; checks joint limits; compares raw sensor PNGs across observer modes and roof-light changes; and verifies that legacy inference/export/case controls do not mutate the imported scene or publish data.

Verified locally:

- All 74 assets imported at their expected paths; maximum component-bounds error was 0.000019 cm.
- Independent SDK reimport reconstructed every one of the 1,761,208 oriented source triangles, preserving corner positions and UVs exactly and normals within floating-point precision. Rest-pivot reconstruction error was below 0.00002 cm.
- Raw and separated Unreal imports both built 1,756,439 triangles. The identical difference from the SDK total consists of importer-removed degenerate triangles.
- Source hashes were unchanged. Three texture links and all ten generated materials were checked explicitly.
- The rendered imported smoke passed: 74 components, 24 moving joints, 74 thermal profiles, byte-identical fixed NIR and thermal PNGs across display/lighting changes, and zero inference or training readbacks.
- The procedural thermal regression passed all 42 modality, 55 spatial-field and 138 heat-transfer checks. All 34 Python unit tests passed.
- Visible rest/motion renders and both infrared displays were visually inspected. The normal view shows the assembled car, robots, fixtures and fencing; the original import validation predates the larger production cycle and its spline hose substitutes; new cycle evidence is recorded separately.

Local evidence is in `artifacts/asset-inspection/welding_prepared_import.json`, `welding_material_preview_update.json`, `assets/derived/welding_cell/review_roundtrip_result.json`, and `artifacts/welding-smoke-dim/`. These reports and rendered images are ignored and must not be copied into published tutorial media.

Nanite, automatic collision and lightmap-UV generation are disabled for this initial import. There are no authored LODs. Approximately 1.76 million triangles are rendered through multiple views; separation alone does not establish acceptable GPU cost. Profile the observer and both sensor captures at the target resolution before adding more production stations. Visual/material approximations, robot trajectories and model accuracy remain separate validation tasks.


## Updating only the carriage-holder assets

Normal preparation now also writes `holder_delta.fbx`, `holder_delta_groups.txt` and `holder_delta_manifest.json`. The delta contains the two holder assemblies plus the five background groups from which their hardware was extracted; the other 69 mesh packages do not need importing. `holder_grouping_report.json` records membership, bounds and source conservation. The front holder contains 133 connected islands / 86,248 triangles; the rear contains 187 islands / 110,368 triangles. Source rails and floor plates remain stationary.

For an existing prepared scene, `prepare_welding_cell.py --holders-only --output assets/derived/welding_cell` updates only holder assignments and the parked-pose/cycle metadata. It preserves the five solved welding poses for every joint and is idempotent. It requires an existing source scan, grouping report, import manifest and scene. `--cycle-only` instead recomputes the welding trajectory; these options cannot be combined.

After a holder-only preparation, regenerate both FBXs with the already built SDK helper. The subset mode takes a newline-separated group list, rejects empty/duplicate/unknown group names, and still requires complete source-island assignments. Run from the repository root:

```powershell
.\.venv\Scripts\python.exe scripts\assets\prepare_welding_cell.py --holders-only
$SourceFbx = 'assets/Autodesk_FBX_2009/Car_Body_Welding_Robotics_Cell.FBX'
$Derived = 'assets/derived/welding_cell'
$PreviousAssetPath = $env:PATH
$env:PATH = 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\ThirdParty\FBX\2020.2\Win64;' + $PreviousAssetPath
try {
    & artifacts/asset-tools/fbx_prepare.exe group $SourceFbx "$Derived/assignments.tsv" "$Derived/prepared.fbx"
    if ($LASTEXITCODE) { throw 'Full FBX export failed' }
    & artifacts/asset-tools/fbx_prepare.exe group-subset $SourceFbx "$Derived/assignments.tsv" "$Derived/holder_delta.fbx" "$Derived/holder_delta_groups.txt"
    if ($LASTEXITCODE) { throw 'Subset FBX export failed' }
    & artifacts/asset-tools/fbx_prepare.exe scan "$Derived/holder_delta.fbx" "$Derived/holder_delta_scan"
    if ($LASTEXITCODE) { throw 'Round-trip scan failed' }
} finally { $env:PATH = $PreviousAssetPath }
.\.venv\Scripts\python.exe scripts\assets\verify_welding_export.py --prepared-scan assets/derived/welding_cell/holder_delta_scan --manifest assets/derived/welding_cell/holder_delta_manifest.json --report assets/derived/welding_cell/holder_export_validation.json
.\unreal\Scripts\import_welding_cell.ps1 -Mode prepared -Manifest assets/derived/welding_cell/holder_delta_manifest.json -SkipMaterialBuild -Report artifacts/asset-inspection/welding_holders_import.json
```

Close the running demo/editor before importing. `-SkipMaterialBuild` reuses the existing ten materials while assigning all five holder material slots. A fresh scene should use the full `manifest.json` import first. The normal preparation wrapper exports both files automatically; the lower-level commands above avoid recomputing an existing validated seam. Rebuild the SDK helper after changing its source; its scan-provenance guard requires a fresh source scan when the tool changes.

The reusable `verify_welding_export.py` checks the selected node set, triangle order and winding, corner positions, normals, UVs, local bounds, and original FBX hash against scan provenance. It also accepts a full prepared-FBX scan with `manifest.json`. It exits nonzero on failed checks. This is a geometry audit; Unreal material/package validation and the rendered cycle test remain separate.

The 2026-09-12 holder delta round trip passed for all seven meshes / 666,328 triangles: position and UV error were exactly zero, maximum normal error was `5.96e-8`, and maximum local-bounds error was `8.08e-6 cm`. The full regrouping retained all 1,761,208 source triangles and 3,681 islands. No thresholds were relaxed: position/bounds tolerance is `0.0002 cm`, normal tolerance `0.000002`, and UV tolerance zero. These figures describe the SDK export before Unreal removes degenerate triangles.
