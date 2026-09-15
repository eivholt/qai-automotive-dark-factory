# Inner trunk panel assembly scene

The current [permanent mounting frame](panel_mounting_frame.md) retains the
source panel's outer stampings on every process body. The centre is cut away;
sealant rests on these metal flanges before R3 lowers the full panel from above.
This supersedes descriptions below of an unsupported empty opening or insertion
from underneath. Original source assignments and settled inspection poses remain
unchanged; previous model results retain their original geometry scope.

`ADarkFactoryProcessScene` supplies the replaceable physical setup for `automotive-panel-process-v3`. The imported cell's separate visual preview keeps its legacy timing and QA branches; the conserved source regroup preserves the original geometry in 91 source components plus a copied rack H-bar and three auxiliary pinch-gun pieces. The process now installs an inner trunk panel; the former supplemental doors, loose-door simulation and return branch are superseded.

## Parts and robot roles

The process uses the locally supplied vehicle geometry and a panel supply position. Arm A/R1 applies adhesive, arm B/R3 picks up and holds the panel, arm C/R2 performs spot welding, and R4 inspects. The holder stays engaged through adhesive cooling, welding, seating inspection and weld cooling. Only an accepted cooldown permits release and retraction.

R2 uses the original purchased welding head at 70% positive uniform scale, attached directly to its measured native mating cap with a -90-degree axial roll. The generated extension rod and extra flange are removed. Six-joint arm motion aligns the opposing electrodes across each contact. Two source pin-jointed jaws open, squeeze and release; there is no independently aimed gimbal. Sparks, visible/NIR glow and the thermal hotspot follow that actual electrode. The former generated C-head is removed. R3 uses a rigid 119.5 cm flange-to-panel offset, with vacuum pads at the panel surface. The stock rack sits near `(515, -510)` cm, shifted 45 cm outward and 70 cm rearward from its earlier process position. All five stock sheets move with the rack, and refitted pickup/lift joint poses keep the outer sheet within reach while clearing the parked arm and adjacent cabinet. The holder lifts above the stock, follows an alternate base sweep, approaches the body from behind and retracts along the reverse detour over 2.5 seconds. Its raised parking pose clears the stock; R1 briefly turns aside during transfer. The robot always picks the outer sheet. It stays absent through assembly and inspection, then reappears as the finished body starts moving away. The other stock sheets stay fixed; every new body starts with a full rack. These waypoints preserve the adhesive deposition and welding pulse times. These are static mesh/rigid-transform additions and reuse existing thermal profile IDs.

The `left-stations-swapped-v1` layout exchanges the two left station functions while retaining logical robot identities. Coordinates below are base-joint positions in Unreal centimetres.

| Physical station | Logical arm and purpose | Base position (cm) |
| --- | --- | --- |
| Rear-left, 50 cm inward from the old rear station | R2 / arm C, welding | (-301.093104, -294.770226, 175.584213) |
| Former welding station | R4, NIR/thermal inspection | (-240, -100, 175.584213) |

Pedestals follow physical stations independently of logical arm names: the former R2 pedestal supports the camera station, and the separately extracted R4 pedestal supports the rear welder. Base meshes, joint roots and attached EVK props receive the same parent-space translation; source joint definitions and legacy-preview layout remain unchanged.

The dispenser has a visible flange connection, reservoir, nozzle and colored mechanical details; its process-only outer wrist turns 20 degrees at rest to clear the base housing. The holding gripper uses three copies of the purchased rack H-bar, with the original rack blue material and dark suction pads. Its connecting stem is 91.5 cm, shortened 25% from 122 cm. Refit joint poses preserve the original panel pickup and seating transforms. The holder's parked wrist unfolds another 45 degrees to keep its pads clear of the upper arm. Welding-arm yaw adjusts around each unchanged contact normal to clear the lower holding-arm pose. Side walk platforms are split into original decks and scaffold frames. In process mode both complete assemblies move inward and forward beneath the retained clamps: R1 source offset (70,600,0) cm and R2 (-80,600,0) cm. Their rear edges begin at Y=50.42 cm, ahead of the robot pedestals. Deck height remains Z=85.592865 cm. The exit photoeye posts stand on these decks with shortened stems and unchanged optical heads; legacy layout remains unchanged.

Two continuous purple sealant runs follow the front ledges, exposed side contacts and rear contact, routing around the source ventilation slots. Each rounded bead is a static GPU mesh with tapered ends and extrusion ridges. Its backing follows the fitted path. A masked PBR material reveals deposited arc length beneath the moving nozzle using the same progress function as the actual arm. Shape construction runs once, without per-frame mesh rebuilds. R4's thermal poses now cover the full runs. The former isolated rear-middle patch and support stay absent. See [geometry, timing and verification](pinch_welder_and_sealant.md).

One in five bodies is generated with the panel already installed. The generator controls visible scene state only. The controller learns presence from two matching fresh NIR G/R batches and never reads the generator flag to choose the production branch. Preassembled bodies still receive both thermal checks; assembly actions and the post-installation seating gate are explicitly skipped. A seating R holds an exception without releasing the part. See [the complete sequence](process_inspection.md).

The source functions expose panel supply/held transforms, six contact positions, stage motion and private assembly metadata. These are authored geometric proxies rather than calibrated robot kinematics, gripper forces, flexible-panel contact or adhesive mechanics. Preserve imported pivots, material links, local asset hashes and the independent legacy fixture. Do not claim motion clearance from a few sampled endpoints alone. The purchased panel and body have interlocking fitted seams: the last 20 cm of this rigid insertion still passes through body flanges. The final seated position matches the source asset, but the insertion is a visual approximation rather than a physically deforming sheet assembly.

## Prior six-front-weld validation (before continuous sealant)

The current six-weld motion fixture passes 38 rendered checkpoints. The two front flanges each receive three welds, with a 9.5-second welding phase; the old rear-middle weld, flange and diagonal support are removed. Six independent halo/core pairs drive temperature and cooling, with six visible weld marks. The holder uses a 65 cm off-center grip and four surface-fitted 8 cm pads, and the rear welder parks clear of the camera withdrawal path. The selected calibrated shared448 runtime scores 95/96 development decisions with zero false G and one conservative cool-weld hold. Its actual assembly/preassembled/assembly run passed, including all six thermal audits. See the [current model guide](model_classification.md); earlier evidence below retains its original scope.

## Earlier tool and base refinement evidence (14 September 2026)

The `two-bead-source-tool-paths-v5` refit retains the R4 lens poses, adhesive paths and thermal contact positions. Sampled panel endpoints remain within 0.0021 cm of their original world targets. The revised weld orientations preserve electrode contacts within 0.0005 cm. Conservative H-bar envelopes and source-triangle checks found no checked tool/casting intersections over the audited dispenser, holder and welding routes; this remains bounded visual clearance evidence with the panel/rack and final insertion exceptions described above.

The built `tool-refit-motion-v4` fixture captures 29 checkpoints, including parked tool and all four base EVK closeups, with no model requests. The final `tool-refit-legacy-v2` run passes all nine legacy welding suites and 12,645 checks, including fixed-sensor invariance and stationary EVK attachment. The Python suite passes 106 tests. Private evidence stays under `artifacts/`.

Final `panel-tool-refit-episodes-v3` capture retains seed 4307 and the same 64 whole-body episodes: 512 batches and 1,536 lossless grayscale images. It passes 127,778 camera/PNG checks and 936 train/validation thermal contact checks, maximum error 0.308 C. Frozen shared256 validation gives 95/96 labels and 96/96 gate actions, with no false proceed or wrong presence branch. Its single hot-adhesive R-to-A prediction holds. These are compatibility results on the existing 12 validation episodes; no new held-out query or model tuning was performed. Earlier refit captures are retained separately. The final main H-bar has an 8 cm depth, meeting the shortened stem without a gap and remaining inside the clearance envelope already audited.

The final `tool-refit-live-v3` model run completes assembly/preassembled/assembly in 182.44 seconds, with three departures and six thermal acceptance checks. All 30 requests are image-only; runtime camera/curtain audits pass 5,830/27 checks. The separate native policy run passes 91 checks.

## Reproducing the local assets

Run the standard preparation and import from the repository root. The selected
Python interpreter needs NumPy; use the existing native `-Python` option or WSL
`-WslPython` option for your environment. The example uses WSL's `python3`:

```powershell
.\scripts\assets\prepare_welding_cell.ps1 -UseWSL -WslPython python3
.\unreal\Scripts\import_welding_cell.ps1 -Mode prepared -Manifest assets/derived/welding_cell/manifest.json
```

Preparation now runs `prepare_panel_assembly.py` and `prepare_panel_motion.py`
after the base grouping. It produces 90 source mesh components and the sibling
`panel_assembly.json` pose descriptor before exporting `prepared.fbx`. The pose and regrouping
helpers honor the launcher's custom `-Output` directory through `--root`.
Original FBX hashes, all 1,761,208 triangles and 3,681 connected islands are
preserved. `prepare_gripper_rack_bar.py` copies the 732 triangles of rack island 158 into a separate auxiliary export; it does not change the original rack or the conserved source assignment table. The standard importer includes this one extra mesh, instantiated three times by the gripper, alongside the original 90 source components. The fence stays in the source grouping and is hidden with collision
disabled in the panel process. Generated assets and pose descriptors stay private.

For an already prepared installation, `-SkipBuild -SkipScan` reuses the validated
FBX tool and unchanged source scan. After preparation, import only the 19 changed
panel/rack/fence/pedestal/floor-fixture and residual background groups with:

```powershell
.\unreal\Scripts\import_welding_cell.ps1 -Mode prepared -Source assets/derived/welding_cell/panel_delta.fbx -Manifest assets/derived/welding_cell/panel_delta_manifest.json
```

Stop the demo and any capture run before preparing or importing geometry. A fresh
installation needs the full import first; the delta does not contain the existing
robot and car meshes. See [the asset setup guide](welding_cell_asset_import.md)
for SDK, source-file and material-generation prerequisites.

## Surface heat and continuous time

Adhesive surfaces follow per-episode assigned initial temperatures and exponential cooldown on the continuous process clock. The established time-scale factor is 0.8; readiness remains 45 C nominal with a +/-3 C model ambiguity band. A mechanical pose hold does not pause cooling. Explicit simulation pause does.

Welds retain the illustrative compact 1600 C/2 cm exposed-hot-metal core and broader 420 C/12 cm halo, with 0.65/5.0-second cooling constants. Their maximum is composed from both fields. The longer panel-halo cooldown preserves residual heat through the intervening NIR seating scan. Thermal capture includes mixed contacts: one hot or borderline region may limit a batch while the others are cool; the label uses the maximum relevant contact temperature. These are surface proxies, not a thermal view through steel into a buried molten nugget. Opaque occlusion and the low-temperature radiometry fit remain intact. Adhesive emissivity is assigned 1.0; imported sheet profiles use their recorded emissivity. Apparent temperature, not display color or assigned core temperature alone, defines grayscale evidence. See [welding temperature ranges](thermal_welding_range.md).

The fixed sensor remains linear 20-120 C even while a very hot weld saturates. The observer uses a fixed 0-120 C display range with selectable palettes; F10/right-stick welding-range switching is removed. Sensor/model encoding remains independent of observer display settings. Imported bulk heat exchange remains disabled without authored contact/thickness data. Tool residual heat, physical sparks, smoke aging and motor warmth use the appropriate continuous simulation clock; stage waits must not manufacture mechanical activity or freeze cooling effects.

## Physical multispectral inspection

R4 has one visible rigid camera mount and separate triplets of authored six-joint NIR and thermal inspection poses. In the swapped layout, the mount is rotated +60 degrees about the tool-frame Y axis relative to its previous orientation; its local translation remains (0, 0, -24) cm. This is a fixed mounting angle, not a camera gimbal. Refit joint poses preserve the earlier world lens targets. Legacy fixtures retain their original mount and approach profile. NIR and thermal capture components share the physical lens transform. They use distinct materials/illumination and framing appropriate to the same panel/contact geometry. Wider NIR views expose panel seating; thermal views each cover two of the six front weld contact regions. Every stage sends three images in spot1/spot2/spot3 order.

A live batch moves to a spot, waits for physical settlement and three render settle frames, captures and commits the PNG, then proceeds to the next spot. The lens must agree with the actual capture transform. There is no virtual aim correction. Only complete triplets with one body/stage/batch identity are published. Pausing or changing identity cancels partial results; resume restarts at spot 0. The eight-second freshness limit applies to the complete batch and response. Live HUDs reuse GPU targets and do not add image readbacks.

Offline capture snaps the same physical poses while freezing heat and process time across each triplet. Metadata records lens/camera transforms, joint states, framing corners, settle state and capture order. Current two-bead panel data uses `view_recipe=rigid-arm-panel-two-bead-scan-v3` and `capture_recipe=panel-two-bead-episodes-v3`; older panel-v2 and thermal-arm data retain their original geometry and semantics separately. Camera/geometry review must precede model evaluation. Offline NIR episodes vary physical illuminator power coherently over 0.8-1.2 times its normal output, retaining fixed exposure and identical PNG processing; ordinary interactive power remains 14,000 authored lumens.

## Transport and resource boundaries

The body and its rail holders move together. Process travel remains 2050 cm over 3.2 seconds with 1400 cm concealment tunnels, preserving full curtain clearance before body reuse. Portal physics, pause behavior and live constraint handles require their separate rendered verification. New body initialization resets body/adhesive/weld state and per-gate history; robot tools may retain residual warmth.

The panel helper owns scene state; the controller owns image scheduling and model transitions. Private `BuildMetadata` and explicit offline case preparation support labels/audits only. Temperatures, generated branch state, supply counts, stage labels, case IDs and debug masks never enter model requests. Runtime images exclude cabinet displays and indicator lights, while ordinary physical occluders remain visible.

## Current refinement validation

The two-bead geometry uses contract `automotive-panel-process-v3`, view recipe `rigid-arm-panel-six-weld-scan-v4`, capture recipe `panel-six-weld-episodes-v4` and thermal recipe `panel-six-weld-contact-core-halo-v5`. Each stage still has three physical images. Adhesive views map to bead indices `[0, 1, 0]`, covering exactly two real beads; weld views cover pairs `[0,1]`, `[2,3]`, `[4,5]`. The rendered geometry and capture checks below are complete; model fitting and evaluation are tracked separately. The historical results retain their original scene/contract scope.


The final `two-bead-motion-v3` fixture passes all 23 checkpoints and saves 69 observer/NIR/thermal images with zero model requests. The three bright observer closeups visibly show the nozzle at the advancing purple bead, the blue/purple gripper with pads seated on the panel, and the restored orange source gun with its fixed adapter and downward electrode. The bright-light closeups resolve the earlier headlamp washout and the holder-arm obstruction of the gun. The actively heated electrode region remains bright; exact tip alignment is also checked numerically. These are physical appearance and motion checks, not model accuracy or industrial collision certification.

The private `panel-refinement-prep-v1/final-refinement-audit.json` records the conserved 90-component source layout and final `two-bead-source-tool-paths-v4` descriptor. Bounded source-triangle/proxy checks find no intersections in 165 final gun-route samples or 351 dispenser-route samples. Across 200 nozzle samples, the maximum error against the shared bead-progress path is 0.048 cm. The three original electrode anchors have approximately 0.60 cm authored standoff, face their contact normals, and exactly share the spark/thermal-hotspot location. The previously documented rigid-sheet seating limitations remain; these sampled checks are not full swept-volume certification.

The new `panel-two-bead-episodes-v3` capture contains 64 whole-body episodes, 512 batches and 1,536 raw PNGs. Its `model-camera-verification.json` passes 127,778 camera/PNG checks across the NIR, adhesive and weld pose families, with a frozen simulation clock within every offline batch. Its `model-thermal-verification.json` passes all 936 training/validation thermal contact checks. Test episodes remain excluded from that radiometry audit by default. These reports establish capture geometry, transport consistency and visible thermal labels; they do not measure Reason2 classification accuracy.

The actual `two-bead-live-v1` process run used the selected shared256 Reason2 adapter and completed assembly/preassembled/assembly with three forward departures in 182.31 seconds. All 30 requests used model-only evidence; all six separate QA thermal acceptance audits passed without changing model decisions. The same run passed 5,830 physical camera checks and 27 live curtain checks. This verifies the integrated mechanical/model sequence, not a new held-out accuracy estimate. The selected adapter is now served on the default port 18191; its separately frozen held-out result is 95/96 labels/actions, with one conservative cool-weld hold and no false releases or wrong presence branches. See [model classification](model_classification.md) for dataset, selection and timing evidence.

## Prior panel-v2 validation history

The current build and all 87 Python tests pass. The native policy run passes all 83 checks, including three stale-encoder publication cases, with zero measured HTTP requests. The launcher suite passes 34 checks. The private `panel-motion-v4` run saved 21 physical checkpoints with 63 observer/NIR/thermal images of the compact head, longer gripper and raised parking/transfer path. Tool telemetry rises from 20 C to 281 C during the three weld samples, then falls to 22.4 C during the cooldown sample with fading visible/NIR glow. These are authored motion and camera tests, not model decisions or industrial collision certification.

The initial `panel-episodes-v2` capture contains 64 episodes, 512 batches and 1,536 PNGs. It passes 122,658 camera/PNG consistency checks. All 936 training/validation thermal contact-center checks match the private apparent-temperature labels within 0.317 C. The thermal audit projects the actual contact/adhesive centers and samples only their neighborhoods; it does not substitute a warm robot elsewhere in the image. It excludes test episodes by default. Run `scripts/verify_panel_thermal.py --manifest <manifest.jsonl> --output <report.json>` in the QA Python environment.

The conserved 86-component grouping also passes all nine legacy welding suites (12,261 checks), 681 environment/realism checks, 606 cabinet checks and 216 dedicated curtain checks. The production/choreography/realism verifiers now expect the deliberate 86-component regroup; all motion, lighting, image and physics criteria are unchanged. Private evidence uses `panel-legacy-v1`, `panel-realism-v1`, `panel-cabinet-v1` and `panel-curtains-v1`.

The first pilot exposed a bead hidden by the source side frame and a support web covering a weld core. Both side beads now face inward, and the side mounting webs route below the sheet. The independent native ray metadata and rendered pixel check preserve opaque surface occlusion; labels were not changed to accommodate invisible hot surfaces. Fresh model training/evaluation is separate from these rendering checks.

The separated adhesive/welding paths were checked at 41 fractions along each of 20 segments (820 sampled configurations), with no strict surface intersections in the audited body, tool and neighboring-arm surfaces. This is sampled visual geometry evidence, not continuous collision certification. The `panel-episodes-v3` recapture uses the final fixed tool/gripper/rack layout and passes the same 122,658 camera/PNG checks and all 936 train/validation thermal contact checks (maximum error 0.317 C). Its model validation remains separate. Before the per-body restock simplification, the historical stock-feed audit sampled 315 configurations across all five supply counts; every pickup uses the reachable outer slot, with no post-pick lift or return intersections in the audited geometry. Intended gripping contact and the rigid final-seating approximation are excluded explicitly, not reported as clearance passes.

The final raised-rest/top-feed build passes the 83 native policy checks again (`panel-policy-v3`) with zero HTTP requests. Its `panel-episodes-v4` capture has 64 episodes, 512 batches and 1,536 PNGs; all 122,658 camera/PNG checks and 936 train/validation thermal contact checks pass. A separate synchronized R1/R3 quarter-turn audit reports no intersections in 21 samples per turn window against the audited arm/tool geometry. These renderer and motion checks are independent of model accuracy.

The actual frozen320 model run `panel-live-final-v1` completed assembly/preassembled/assembly and all three forward departures in 165.95 seconds, with 27 real model requests and no ground-truth fields sent. All six QA-only acceptance audits were below their nominal thresholds: adhesive/weld maxima were 29.82/34.38 C, 33.99/20.24 C and 36.49/34.15 C. The holder remained engaged until weld acceptance, and the preassembled body skipped assembly and welding. The public launcher validated the run identity and outcomes; physical camera verification passed 5,086 checks and live curtain verification passed 27. Median HTTP response was 84.68 ms while Unreal rendered; this is separate from standalone evaluation timing and from full mechanical cycle time. These three successful bodies are integration evidence, not a new held-out accuracy estimate.

Historical door geometry audits, 12,261-check legacy runs, physical R4 scan checks and previous model results are preserved under [v1 scene and camera history](model_classification_v1_history.md#historical-scene-and-physical-camera-validation). Those reports retain their original images and recipe identifiers and do not validate the panel task. Keep new model scores, policy tests, rendering/radiometry QA and industrial claims separate.

The 2026-09-14 rack simplification keeps all stock transforms fixed and respawns the picked outer sheet only after positive departure movement. The existing `panel-restock-motion-v1` rendered fixture passes 579 stock-lifecycle checks across 21 checkpoints and 553 motion samples: five sheets before pickup, four throughout assembly/inspection/retraction, then five during departure. Preassembled bodies retain five. Screenshot review confirms the same source slot is restored. The build, 87 Python tests and native 83-check policy fixture also pass.
The separate `panel-restock-live-v1` run also completed assembly/preassembled/assembly with three correct departures and all six thermal acceptance audits. This is operational regression evidence for the rack change, not a new model-accuracy measurement.


## Perimeter sealant and pinch-gun revision, 15 September 2026

The current implementation and regeneration steps are in
[pinch welding and sealant](pinch_welder_and_sealant.md). Two continuous contact
runs replace the front-only strips; R4 physically views right/left/right, and
the six welds use the source gun's two animated opposed jaws. The native cap mount
has no extension rod. Source triangle/material conservation and the original
legacy guns remain intact. Application/welding take 13.5/11.5 seconds.

The revised motion passes 44 rendered checkpoints and 1,375 electrode/sequence checks.
The current model integration passes assembly/preassembled/assembly, three
forward departures and all six thermal acceptance audits in 201.26 seconds.
See the [current model guide](model_classification.md) for the selected 768 adapter,
288/288 development validation, capture revisions and limits. Earlier validation
paragraphs above describe their original geometry and model versions.

The 2026-09-15 rack relocation moves the complete supply assembly 45 cm outward
and 70 cm rearward, to approximately (515,-510) cm. The authored
`rear-rack-sealant-pinch-paths-v10` pickup/lift poses preserve the seated panel,
sealant, weld and inspection-camera targets. The 56-checkpoint native motion run
includes rest, pickup, lift and restock closeups. A private source-triangle audit
found no R3 casting intersections against the rack/four remaining sheets across
492 unique native casting poses. Conservative parked arm/stock bounds separate
by at least 1.68 cm; stock/cabinet bounds separate by 7.56 cm. The picked sheet
clears the adjacent cabinet and subsequent transfer, but retains the earlier
source-fit rack contact during initial extraction: four sampled early contacts
occur in both old and new runs. This is bounded visual evidence, not continuous
collision certification. Evidence is private under `artifacts/rack-clearance-v1`
and `artifacts/rack-clearance-motion-v2`.

Final rack integration completed assembly / preassembled / assembly with three
correct departures, 23 image-only requests and six thermal acceptance checks in
179.15 s. Live curtain checks pass (27). Supplemental live camera QA has one
strict tolerance flag among 4,472 checks: batch6/view1 reports 0.000104900-degree
lens rotation drift against the 0.0001-degree limit, while all six physical
joints match target within 8e-15 degrees, capture is settled, and location drift
is 0.000143 cm. This is numerical transform drift; the failed supplemental
verdict is retained, with no threshold relaxation or camera-pose changes.
The rendered capture camera audit passes. No new model accuracy or training is
claimed. Unreal is closed. Final evidence: `artifacts/rack-clearance-v1/final-verification.json`.
