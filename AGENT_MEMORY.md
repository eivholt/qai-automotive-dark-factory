# Repository agent memory

User direction first recorded on 2026-09-11 and updated on 2026-09-15. Read this alongside AGENTS.md. The current panel-assembly direction below supersedes the earlier door-fault process. Historical implementation entries retain their original validation scope; they do not imply current two-bead panel-v3 model accuracy.

Latest implementation: three-edge sealant beneath a panel raised 5 mm, conducted panel-surface heat, and explicit shared960 primary/shared896 weld routing. The final entry and docs/model_classification.md supersede earlier adapter and geometry status.

## Demonstration objective

Build an Unreal Engine factory simulation demonstrating sensor cameras feeding NVIDIA Cosmos-Reason2-2B to detect anomalies. The simulation should react to model results. Begin with several sensing modalities and use experiments to refine the concrete manufacturing scenarios.

## Production-line sequence and inspection gates

Use 3D assets to depict a section of a production line. Sensor-camera observations and the model's inspection results should act as a gate for subsequent production steps: the simulation advances only when the inspection outcome permits it, and otherwise holds or follows the defined anomaly response.

On 2026-09-13 the user replaced the door-fault task with inner trunk panel assembly. A new body arrives with its panel already installed one time in five. Four camera-only Reason2 stages now use automotive-panel-process-v3: panel_presence (NIR), adhesive (thermal), panel_seated (NIR), weld (thermal). All use three actual R4 camera views in spot1/spot2/spot3 order. The model chooses the presence branch from two matching fresh G/R batches; the generator flag must never choose it in live control.

For a missing panel, arm A/R1 applies adhesive, arm B/R3 picks up and holds a panel, R4 checks adhesive temperature, arm C/R2 welds, R4 confirms panel seating with NIR and then weld cooldown thermally, arm B releases/retracts, and the body exits forward. For a preassembled panel, skip adhesive application, pickup/placement, welding, seating inspection and holding-arm retraction; still run presence and both thermal gates. Seating R holds an exception with the holder engaged, thermal R waits, and A/errors/stale evidence always hold. Wrong presence R is also a branch error because it could assemble over an existing panel. Doors and fault-return logic are superseded.

On 2026-09-14 the visual refinement replaces the three adhesive patches with two longer rounded purple front beads, omitting rear-bracket adhesive. The dispenser follows each bead during extrusion and has a visible flange connection. Restore the purchased welding tool at a smaller scale; color the dispenser and suction gripper, and move the side platforms/clamps clear of the relocated bases. Enable supported DLSS and observer Lumen hardware ray tracing with separate disable toggles, while sensor images retain their fixed independent rendering. The corresponding two-bead contract and adapter now have their own frozen validation/held-out evidence; prior panel-v2 scores retain their old geometry scope.

The current layout exchanges the two left physical stations while retaining logical R2 welding and R4 inspection. The rear-left welder base is (-301.093104,-294.770226,175.584213) cm, 50 cm inward from the previous rear station; the camera base occupies the former welder station at (-240,-100,175.584213) cm. The source gun remains at 0.7 scale and mounts directly at its native cap, without the generated extension rod or extra flange. The one rigid camera mount rotates +60 degrees about tool-frame Y, while refitted joints preserve its world lens targets. The six-weld refinement and its validation are recorded at the end of this memory; earlier results below retain their previous-layout scope.

The panel rack automatically restocks when the current body starts departing: the picked outer sheet reappears in its original slot. Other sheets never advance, and each new body starts with a full rack. Keep it depleted during assembly and inspection holds.

The controller/UI show ten physical steps and four model gates, including skipped states. Cabinet rows are PANL/SEAL/SEAT/COOL. R4 has one rigid camera mount and three real scanning positions; it never welds. Preserve lossless 448 x 256 grayscale and model thermal 20-120 C independently of observer graphics. Adhesive mapping [0,1,0] observes two front beads; weld pairs [[0,1],[2,3],[4,5]] cover all six contacts. Current host selection is panel-six-weld-shared-448-v4 with confidence 0.98, serving on 18191 after successful native integration. Development validation is95/96 with zero false G or wrong branches and one conservative cool-weld hold. Earlier shared256 held-out results retain their old three-contact geometry scope. No new held-out queries or IQ9 deployment occurred. Full current evidence is at the end of this memory.

The thermal thresholds are illustrative temperature-readiness limits, not proof of adhesive cure or weld quality. A model hold must remain a hold; do not substitute scene ground truth when accuracy is insufficient. Compare shared and modality-specific LoRA adapters using separate whole-body episodes.

## Initial modalities and anomaly scenarios

| Modality | Intended inspection |
| --- | --- |
| 1. Near-infrared (NIR) | Identify parts that have fallen off a rig, or are about to come loose. |
| 2. Thermal imaging | Identify overheating, heating-element faults and temperature-dependent process defects, including examples involving vehicle windows and sealant placement. |
| 3. Temporarily illuminated visible imaging | Temporarily enable local inspection lighting to look for surface defects, such as paint scratches. |

Thermal imaging needs its own temperature/emission representation; the current NIR rendering proxy and a color palette do not establish thermal sensing. Local visible inspection lighting is intentional and compatible with the dark-factory demonstration.

## Adapter experiments

The LoRA adapter strategy is undecided. Compare a single shared adapter across modalities with dedicated adapters per modality. Select the organization based on experiments rather than committing to an architecture in advance. Keep training, evaluation and deployment input contracts consistent for each candidate.

## Observer experience

Startup preference (15 September 2026): launch the interactive demo in a
borderless 2560 x 1440 window. The launcher supports -Width/-Height overrides;
automated capture and smoke fixtures retain 1600 x 900 defaults. Keep sensor
render-target dimensions independent of the observer window.

V-sync preference (15 September 2026): interactive launches enable V-sync,
including installations with older saved preferences. Keep the existing 60 FPS
limit. Automated capture and smoke launches explicitly disable V-sync to keep
their timing independent of the desktop refresh rate.

OBS capture check (15 September 2026): preserve this borderless window mode.
Use Window Capture with explicit Windows 10 (1903 and up) / WGC, Client Area,
exact DarkFactory title matching and Fit to Screen. Automatic selected BitBlt
and included an overlapping window. A direct OBS source screenshot verified
2560 x 1440 with DLSS Quality and Lumen hardware RT enabled. Local Scene 2
uses DarkFactory - WGC with application audio and no cursor; the old Game
Capture is hidden. Its hooks failed earlier, recovered after a demo restart,
and its transform was also rotated/off canvas; do not claim a proven engine
window defect. The OBS canvas remains the user's existing 1920 x 1080.
No engine code or rendering settings changed for this fix. See
docs/observer_graphics.md and private artifacts/obs-capture-v1.

Dim-light preference (15 September 2026): when fluorescent roof banks are off,
reduce visible roof-window spill and the observer headlamp to 70% of their
previous output. Nominal window settings remain 80 lm, emitting 56 lm in dim
mode; the headlamp emits 1,050 lm. Bright mode restores nominal window output
and preserves fluorescent bank levels. Do not reduce NIR illumination, thermal
encoding or change camera exposure to implement this lighting adjustment.

Rendered dim-light QA confirmed 56 lm in dim/restored modes and 80 lm in bright
mode. GraphicsSmoke passes 2,012 checks over all 32 graphics/modality states,
with four fixed/R4 sensor PNGs identical across the states. All eight lighting
fixture sensor PNGs also match the preceding baseline byte for byte. The 116
Python tests pass. The first legacy render flagged two observer-NIR body-cooling
image checks; an unchanged repeat passes those checks and closely matches the
earlier baseline. Cause is not established; retain the first report and do not
describe the initial run as a full pass. Actual dim-view screenshot was shown
in chat. Evidence: artifacts/dim-light-v1 and artifacts/dim-light-legacy-v1/v2.

Final repeat passes all nine legacy welding suites, including factory lighting.
The first observer-NIR screenshot failure remains recorded. The demo is closed;
the complete dim-light record is artifacts/dim-light-v1/verification.json.

User workflow preference (14 September 2026): whenever visual changes are verified with captured images, show the actual verification images directly in the Codex chat with concise captions. Use absolute local Markdown image paths; do not leave the images only in artifact folders or summarize them without showing them. Keep screenshots private and ignored by Git.

The observer viewpoint should switch between modalities, inspired by the vision-mode switching in Splinter Cell games. Preserve independent Xbox-controller navigation on PC.

The normal visible observer view should remain navigable under dim light entering from roof windows; the user does not want this view to be completely black. Keep true zero-light scenes as explicit offline calibration conditions. Roof-window light must remain independent of fixed NIR and thermal sensor inputs. Provide native Unreal captures comparing the implemented observer modalities from the same scene and viewpoint.

Observer mode changes are presentation controls and must remain independent of the inspection sensors and their model inputs. The fixed NIR sensor stays in NIR when the observer changes modes. Additional thermal and locally illuminated inspection sensors should retain their own capture settings and explicit lighting/inspection sequences.

## Planning status

The current manufacturing scenario is the inner trunk panel assembly sequence above. Keep its inspection cell replaceable and continue developing reusable capture, randomization, training and model-response infrastructure. The historical door scenario remains recorded only as prior development evidence. Locally supplied assets are recorded below.

## Local asset preparation

On 2026-09-12 the user supplied the Car Body Welding Robotics Cell FBX package under `assets/Autodesk_FBX_2009/`. Ignore the entire root `/assets/` folder and keep source assets and generated content local. The current four-gate panel scenario is described above.

The current source grouping has 91 mesh components, including two complete carriage-holder assemblies and 29 separate body-panel pieces, and 24 approximate joints across four robots. Source hierarchy, geometric transforms and millimetres are baked into validated Unreal-centimetre geometry. The source had ten material-group meshes and no rig; reproducible SDK/Python scripts regroup its connected islands and reconstruct approximate pivots. All source triangles, UVs and normals are conserved before Unreal removes degenerates. See `docs/welding_cell_asset_import.md` for commands and evidence.

Interactive `run.ps1` defaults to the local welding cell when prepared assets exist, with procedural fallback. Space / Xbox View pauses its repeating production cycle and physics. Original rigid hose meshes remain hidden while smooth joint-anchored spline surrogates follow larger arm motions; no calibrated industrial kinematics or full self-collision clearance is claimed. Preserve the independent observer and fixed sensor cameras.

The welding cell retains its visual preview mode. Its old battery-tray inference prompt, cases and training exports remain disabled. Explicit -Process now uses automotive-panel-process-v3, a compatible host GPU endpoint and a new private capture/training path; retained automotive-process-v1 door adapters are incompatible. Thermal profiles are illustrative: 28 warm robot castings use authored mesh-local gradients, with separate dynamic tool and car-weld hotspots. Imported bulk heat exchange remains disabled until thickness, heat capacity and contact surfaces are authored. Welding tools now have an explicit authored heating/cooling envelope and localized tip hotspot shared with visible/NIR glow. The procedural scene retains its full spatial heat-transfer model. Generated geometry, screenshots and audit files stay private under ignored assets/, Unreal Content/ and artifacts/.

## Publication scope

The user clarified that this repository will publish only the tutorial and code examples. Never redistribute 3D assets, the demo runtime, training images or model weights through this project. Keep those artifacts local/private; code examples may describe how readers generate or obtain their own artifacts.

Asset-license review should assess the planned local simulation, rendered-image inference and LoRA training independently of redistribution. A code-and-tutorial-only publication scope does not itself grant permission for machine-learning use.

## Thermal visual direction

The user requested less uniform thermal examples: warmer central body regions, cooler edges and thin parts. Represent these as spatial surface-temperature patterns while preserving opaque occlusion and independent sensor views. Thin passive features and actively powered heating elements need different heating/cooling behavior.

- Thermal bodies must exchange heat with nearby surfaces: contact conduction and blocked/distance-dependent radiation, with stored warmth persisting after the source moves. Current implementation is a deterministic bulk/six-face network feeding GPU patches; use `docs/thermal_heat_transfer.md` for assumptions, controls and validation. Preserve the separate thermal sensor-v3 contract and frozen capture metadata.

- On 2026-09-12 the user chose colored thermal as the default presentation and asked to retain monochrome. The welding HUD shows fixed color (iron) and white-hot monochrome previews together, sharing one raw thermal camera target. Observer thermal starts in iron; K / Xbox D-pad left retains all three palettes. Keep the raw thermal grayscale encoding independent of presentation.

## Production-cycle implementation direction (2026-09-12)

The user requested inverted mouse pitch, faster movement and an observer headlamp that cannot affect NIR or thermal. Default flight is now 900 cm/s, Shift 2700; L toggles the visible-only headlamp. Controller pitch is unchanged.

The user requested reset/withdrawn robot poses, rapid approach, precision welding and return to rest. A body enters quickly from out of sight, gets welded, exits and is replaced in a repeating cycle. The current authored cycle is 18 seconds. Departure remains at 13.5-17 s, followed by one second empty before the next car. The 29 body panels and two holder assemblies share a Y-axis carriage moving +/-1800 cm. Entrances are aligned to source rail ends at Y=-1204.08264/+1219.15393, X=4.505665, floor Z=6.3477106 cm. Eight sparse car-shell colliders and two holder proxies push 14 three-segment Chaos curtain strips per portal. Opaque tunnels conceal the local mesh reuse/reset. Keep the cycle replaceable by future model-gated steps.

Welding creates a bounded physical particle effect shared by all views: GPU-instanced rendering, swept world collisions with gravity/bounce/rest, per-particle fade and 12 temperature buckets. Four localized arc lights affect visible and NIR; thermal reads authored heat profiles. Sparks must cool/fade after landing and follow moving surfaces. The imported bulk heat solver remains disabled; car-weld surface temperatures are authored fields, not an energy-conserving injection into sheet metal or the floor. Preserve the model/training guards until the welding inspection contract is specified. Keep both colored and monochrome thermal previews.

- Production-cycle follow-up: preparation solves five joint poses along each 2 cm body-surface seam, with approximately 0.6 cm electrode standoff. Imported curtains use bounded Chaos substeps, low-friction contact and nonlinear hinges. Spark grain collision radius is separate from the larger visible glow; only its own source tool is excluded for the first 100 ms. Verify with `scripts/verify_production_cycle.py` after a rendered WeldingSmoke run.

- Welding-tool cooling (2026-09-12): all four tools heat during welding and cool during withdrawal/rest in visible, NIR and both thermal palettes. The shared analytic heat clock pauses with scene motion and preserves residual warmth between cycles. Existing PBR materials receive a localized emissive field; thermal uses a moving hotspot in the current 22-row/88 KiB LUT. These are authored optical/thermal proxies. Run WeldingSmoke and scripts/verify_tool_cooling.py to check same-pose hot/warm/cool captures; retain source assets and generated materials privately. See docs/welding_tool_cooling.md.

- Layout/motion refinement: rest bases turn 180 degrees away from the lane. The later dramatic approach update replaces the earlier fold/yaw staging with three lifted and rolled intermediate poses. Approach and withdrawal each take one second. Portal tunnels extend 1100 cm beyond the measured rail ends with rail continuations and central-strip rail clearance. Holders include connected wheel/drive hardware, while stationary rails remain fixed. Spark emission is 80/s/tool (fivefold), bounded at 1024 live particles; retain GPU instancing, cooling and physical contacts. Run production and tool-cooling image checks after changes.

- Faster repeat (2026-09-12): cycle period is 18 s, empty dwell 17-18 s, next welding begins at elapsed 22.5 s. Preserve the continuous heat clock at the 18 s wrap. Cooling QA remains at elapsed 12/15/21 s; all three samples precede the next weld. Fast metadata updates can call prepare_welding_cell.update_park_cycle(scene), preserving solved IK and mesh records while also applying the casting thermal recipe; --cycle-only recomputes IK.

- Car-weld cooling: four 12 cm surface fields bind once by complex mesh trace to the midpoint of each 2 cm seam on three exact panels (two on SM_BodyPanel_0386). They heat during cycle 4.5ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â€šÂ¬Ã…â€œ12 s toward 420 ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â°C with tau 0.65 s, then cool toward 20 ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â°C with tau 2.5 s. Each new body resets these fields at the 18 s wrap; tool heat remains continuous and independent. Warm-orange visible/NIR emission and thermal patches use the same compact kernel and follow the carriage. The fixed thermal sensor stays 20ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â€šÂ¬Ã…â€œ120 ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â°C and may saturate. See docs/welding_body_heat.md. The preceding rendered body-heat run passed 352 body-heat, 208 production and 129 tool-cooling checks; this evidence predates the new casting-gradient update.

- Imported casting gradients: update_thermal_fields(scene) assigns core_edge_drop_c to 28 warm robot parts (elbow30, flange22, shoulder20, wrist18, forearm15, upper_arm10, fixed_base3 C). Tools, dress packs, body panels, holders and disconnected background retain zero base drop; tool/body hotspots remain independent. The GPU uses normalized mesh-local coordinates, the longest local axis with X/Y tie preference, core offset -0.18, r2=0.45*dot(q,q)+0.55*q_axis^2 and weight=exp2(-2.4*r2). Row13 packs tip C, imported-core mode, axis and offset; the LUT remains22 rows/88KiB. The imported bulk network remains disabled. See docs/imported_thermal_fields.md. Rendered field QA passed 1,459 checks: 20 of 21 sufficiently visible high-drop castings show within-part variation; the tiny distant shoulder region has no measurable spread. Exact NIR/restoration parity passed, along with production 208, tool cooling 129, body heat 352 and procedural thermal/spatial/transfer 42/55/138 checks.


## Factory lighting control

The user requested a fourth observer mode: Tab / Xbox Y cycles dim visible (0), NIR (1), thermal (2), bright factory (3), then dim visible. Bright factory mode starts grouped overhead fluorescent-style light banks with a deterministic staggered startup envelope lasting about 1.5 seconds, then steady output. Its lights use visible channel 0 and leave fixed NIR/thermal inputs unchanged. Entering mode 3 suppresses headlamp emission while retaining the requested preference; returning to mode 0 restores that preference. L changes the preference, and the headlamp emits only in dim visible mode.

Keep the lamp startup state separate from car/robot motion and thermal surface temperatures. Frozen native WeldingSmoke captures exercise off/startup/steady/restored light states, all four observer modes, and headlamp suppression/restoration. Verify actual screenshot brightness and raw sensor byte parity with scripts/verify_factory_lighting.py; metadata-only envelope checks do not establish rendered lighting or calibration. See docs/factory_lighting.md for the capture contract and thresholds.

- Bright factory observer mode (2026-09-12): Tab / Xbox Y cycles dim visible ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ NIR ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ thermal ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ bright factory ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ dim visible. Six roof banks (18 instanced tubes) flicker on with staggered startup for 1.5 s, then remain steady with actor tick disabled. Imported lights use 2,000 authored lumens per bank; procedural uses 800. The headlamp emits only in dim visible, its preference survives mode changes, and L is ignored in bright mode. Fixtures and direct lights are isolated from fixed NIR/thermal captures. Run WeldingSmoke plus scripts/verify_factory_lighting.py after changes. The final rendered lighting run passed 2,269 checks, exact sensor parity, headlamp/cycle/re-entry checks and 34 Python unit tests; see docs/factory_lighting.md.

- Dramatic robot choreography (2026-09-12): the user requested larger, impressive rest-to-weld motion. `dramatic-approach-v1` uses three authored intermediate joint poses at normalized 0.20/0.56/0.80, with quintic easing through each segment. Approach remains 3.5-4.5 s; withdrawal reverses the intermediate poses during 12-13 s. Preserve signed 180-degree rest bases, the five solved seam poses, the 18-second body cycle, tool/body heat state and model guards. `WeldingSmoke` appends 107 sampled native approach/withdrawal pairs, nine bright approach images and a withdrawn image. Run scripts/verify_robot_choreography.py alongside the unchanged production verifier, and visually inspect the sweep. These samples and source-geometry audits do not certify continuous collision clearance or industrial motion. The selected raised recipe uses shoulder/elbow/tool-roll 65/-35/65 degrees at 0.20 and 95/-15/95 at 0.56, with forearm/wrist zero and base rest-to-work fractions 0.85/0.30. The 0.80 pose solves a hover 100 cm above the seam with distal rolls aligned, then descends to the unchanged weld endpoint. Use prepare_welding_cell.py --approach-only or update_approach_choreography(scene) to regenerate it without mesh reimport or changing the welding knots. A 101-pose source audit measured 8.33-8.87 m tip paths and 4.42-4.55 m peak tip heights; The matching native render passed 348 choreography checks, with 107 approach/withdrawal pose pairs and ten reviewed screenshots, alongside production 208, tool cooling 129, body heat 352, casting fields 1,459 and factory lighting 2,269 checks. The Unreal build and 34 Python unit tests passed. Eight sampled electrode paths had no segment crossings against car-panel triangles; this does not certify complete moving-part swept volumes. See docs/welding_production_cycle.md.

- Joint-duty motor heat (2026-09-12): the user requested motor/joint castings to warm with robot operation and cool at idle. The new authored-joint-duty-thermal-v1 model drives 28 casting core temperatures from bounded joint activity sampled at 120 Hz, with an exact bounded heating/cooling step, repeat-cycle recurrence and passive idle decay. This supersedes the earlier fixed casting-temperature assumption while preserving mesh-local gradients, tool/body heat independence, the 22-row LUT and disabled imported bulk heat exchange. Motor warmth has no visible/NIR emissive component. Scene pause freezes its clock; replacing the car does not reset it. WeldingSmoke appends five pose-6 stages at duty/idle 0/0, 12/0, 84/0, 84/8 and 84/30, with exact NIR and hot-restoration parity. Run scripts/verify_motor_heat.py; see docs/robot_motor_heat.md.

- Motor radiometry cache: dynamic castings reuse apparent-temperature tables keyed by emissivity/ambient, bounded to 32 tables of 81 samples over 20-100 C. Linear interpolation preserves continuous motor state; initialization validates interval midpoints against the native radiometry function within 0.005 C. The GPU LUT remains 22 rows and no additional camera/pass is introduced. Motor QA checks cache reuse/bounds/validation, model boundary continuity, exact hot restoration and NIR parity.

- Motor native validation completed (2026-09-12): artifacts/motor-heat-v1 passed 1,071 checks with unchanged thresholds. All 25 eligible driven casting masks heated and cooled; 20/21 high-drop masks retained the required spatial variation, with one distant 12-pixel shoulder below the variation threshold. All 28 scalar temperatures and profile mappings passed. Hot cores ranged 25.55-63.32 C, cooling to 21.42-35.94 C after 30 s idle; eligible nonmotor pixels changed by zero. Five-stage NIR and restored-hot thermal PNGs matched exactly. One 81-sample radiometry table had maximum midpoint error 0.00013542 C; maximum sampled phase/cycle boundary temperature difference was 0.00003315 C. Iron and monochrome views showed heating and fading. See docs/robot_motor_heat.md for scope and repeat commands.

- Cinematic welding expulsion and smoke (2026-09-12): the user requested more dramatic burst, fountain and downward/falling-fragment phases. `cinematic-expulsion-v1` supersedes the earlier 80/s-tool and 1,024-particle settings: four staggered 1.05 s pulses emit one 64-fragment burst, then fountain up to 240/s and fallout up to 100/s, in a 4,096-fragment pool with 90 Hz swept world collisions, cooling and 12 thermal bins. Preserve the distinction between discrete bursts and continuous rates. Smoke `shared-pulse-optical-wisps-v2` uses one 128-puff instanced pool, 60 Hz stored-age motion, weighted 28 puffs/s/tool, 12-17 cm initial radius and 3-4.4 s life; no collision/volume solver or thermal surface. These are authored cinematic expulsion/optical proxies informed by TWI/KUKA references, not normal-weld labels, measured ejection speeds or calibrated fumes. See docs/welding_effects.md.

- Welding effects capture: imported fixed NIR now has an after-tone-map GPU grayscale pass to remove slight color introduced by filmic/translucent composition. This is an imported rendering revision, not byte parity with earlier imported datasets; the procedural capture/training path remains separate. Keep source-neutrality measurements before CPU encoding. Offline effects QA pins `r.Test.FreezeTemporalSequences=1` and restores it on exit, while paired smoke-on/off captures share pose, physical state and observer mode. This removes unrelated stochastic image residue without weakening exact empty-smoke comparisons. Run WeldingSmoke, scripts/verify_welding_effects.py and scripts/verify_production_cycle.py after changes.

- Welding effects native validation: private artifacts/welding-effects-v4 passed 360 effects and 214 production checks with unchanged image thresholds; all ten effects NIR preconversion RGB spreads were zero. The live sequence peaked at 1,692 fragments/859 airborne, recorded 8,653 collisions and 2,888 rests, dropped none and cleared completely. Early smoke changed visible/NIR/raw-NIR by mean 0.4063/0.3203/1.4688 codes; late difference fell 99.87%/99.94%/99.96%, and expired images matched exactly. Raw thermal on/off PNGs matched exactly. Actual phase and smoke screenshots were reviewed. Spark live-tick CPU time over 1,021 calls averaged 0.890 ms, maximum 6.720 ms, excluding GPU completion and other actors; this is not FPS. Existing tool/body/field/factory/choreography/motor verifiers passed 129/352/1459/2269/348/1071 checks. Generated media/assets/reports remain private.

- Post-effects procedural regressions also passed: async readback/reference exact PNG parity and stale-result rejection, 20 night-vision image checks, and mock G/A/R/error decisions with async sensor capture, zero synchronous bridge reads and zero CPU preview uploads. Private evidence: artifacts/procedural-regression-v4/summary.json.

- Robot expansion request (2026-09-12): the user requested independent robot delays, an IQ9 EVK prop on each arm, separate fixed-sensor versus observer NIR illuminators, and HUD FPS. The new default is a 21 s body cycle with seeded per-robot rest delay 0.2-0.8 s, full 1 s approach, pre-weld dwell 0.5-2 s, full 7.5 s weld and 1 s withdrawal. A deterministic 16-cycle schedule bank repeats after 336 s. Departure 16.3-19.8 s leaves at least 0.5 s after the latest robot parks; empty dwell is 1.2 s. Earlier 18 s synchronized fixtures remain explicit QA-only regressions. Stable robot emitter indices must survive sparse active subsets; sparks/smoke/arcs/tool/body heat use local weld age, while motor warmth follows motion and persists between cycles.

- IQ9 props and observer view: four private photographic IQ9 props attach to upper arms, with fixed local transforms, two steady green LEDs and authored irregular amber activity. They are visual props, not live telemetry or additional EVK inference endpoints. Twenty-eight opaque prop thermal profiles bring the imported total to 244, within the existing table. Observer NIR uses a camera-mounted view-channel-2 spotlight; the fixed NIR illuminator remains on view channel 1. The observer lamp is disabled outside NIR mode and cannot affect fixed sensor captures through shared GI/reflection paths. FPS is a smoothed real game-frame interval indicator inside the HUD, outside model PNGs. See docs/robot_expansion.md and scripts/verify_robot_expansion.py; final rendered validation is recorded below.

- Expansion seek QA compares saved before/after snapshots structurally, allowing at most 1e-9 absolute F64 numerical roundoff while requiring robot timing, joint angles, heat clocks and temperature values exactly equal. The v2 investigation found only transform differences up to 4.263e-13 cm; this numerical allowance does not relax sensor-image byte parity. The final expansion run passes this independent comparison, as recorded below.

- Final robot expansion validation (2026-09-12): private artifacts/robot-expansion-final passed all nine independent verifiers, totaling 12,261 checks: expansion 5,835; canonical production 214; effects 360; tools 129; body 352; imported fields 1,599; factory lighting 2,269; choreography 348; motor heat 1,155. All 64 schedules and 84 probes passed, including complete sparse emitter mapping, per-robot heat gating and at least 0.56264 s measured departure clearance. Maximum seek transform difference was 4.12115e-13 with exact timing/angles/temperature clocks. Fixed NIR and thermal PNGs were byte-identical across observer lamp/pose changes; source RGB spread was zero. Observer on/off image difference was 21.732 codes over 47.45% of inspected pixels at three-code threshold; moved-view difference was 14.885 codes. IQ9 attachments/LEDs and readable revised closeups passed review; FPS samples advanced while machine pose stayed frozen. Frozen LED time must be set before pose updates, with stable light proxies to avoid transient sensor differences. The Unreal build and 34 Python tests passed. Aggregate: artifacts/robot-expansion-final/all_verifiers.json. No image thresholds or legacy criteria were weakened.

- Thermal scale HUD (2026-09-13): the user requested a temperature scale for thermal displays. Horizontal bars show the configured apparent-temperature window, currently 20-120 C, with minimum/midpoint/maximum labels (20/70/120), low on the left and high on the right. Temperatures outside the window clamp to endpoint colors; ticks are range values, not measured image statistics. Fixed preview bars stay iron and white-hot monochrome, while the observer thermal legend follows K / Xbox D-pad left palette selection, including black-hot. The procedural preview also has a bar. Reuse M_UI_Thermal and a transient linear 256x1 grayscale ramp, one 1 KiB startup upload and three fixed-palette MIDs. This is HUD-only: no shader/capture/LUT changes, sensor readbacks or per-frame image uploads; raw thermal/NIR contracts stay separate and unchanged. See docs/thermal_rendering.md.

- Thermal scale validation: UE build and all nine native welding suites passed in artifacts/thermal-scale-v1; procedural ThermalSmoke also passed at 1280x720 in artifacts/thermal-scale-procedural. Reviewed readable labels/layout at 1600x900 and 1280x720. Private pixel checks confirmed increasing white-hot, decreasing black-hot and colored iron ramps. Controlled frozen NIR/thermal expansion frames and all compared raw thermal frames matched the previous baseline exactly. Early legacy NIR fixtures are not cross-run byte-stable before the temporal-freeze stage; their within-run lighting parity still passed. No shader or capture implementation was changed. The interactive welding demo was restarted.

## Factory realism additions (2026-09-13)

The user requested research and implementation of ten engaging, realistic elements. The imported scene now includes: (1) floor paths/chevrons/station signs, (2) overhead trays/service runs, (3) extraction hoods/ducts, (4) four animated body fixtures fitted to actual panel surfaces, (5) a cycle-status tower, (6) geometric entry/exit photoeyes, (7) six spatial synthesized machinery/weld/extractor sources, (8) a progress/body-sequence/per-robot-phase HUD, (9) five smoothly interpolated observer viewpoints, and (10) persistent cosmetic weld witness rings in the existing GPU sheet material. Research and implementation are in docs/realism_additions.md and its linked detail documents.

B / Xbox X cycles imported viewpoints; Home / Xbox Menu resets the observer; manual flight cancels a transition. M mutes sound; scene pause silences it while retaining the mute preference. Fixed sensor cameras remain independent. Witness rings grow at each robot's own weld contact and reset for the replacement body; their color/roughness change is independent of heat. Clamps and presence sensors are authored visual/sequence cues, not force simulation or certified interlocks. The status strip counts simulated bodies, never accepted inspections. Imported legacy preview inference/training remains disabled; opt-in process mode uses the separate automotive inspection contract.

The default remains the 21-second randomized cycle (the earlier 18-second values above are legacy QA). Runtime additions use 291 static instances in ten batches, 57 mechanism mesh components and six fixed audio sources, with no extra camera captures, readbacks or thermal IDs. The profile count stays 244. RealismSmoke enables these details; legacy WeldingSmoke disables them to retain prior baselines. Generated assets/audio/renders remain private. Final native build, 34 Python tests, 681 new feature checks and all nine existing suites (12,261 checks) pass. Frozen observer changes retain exact fixed NIR/thermal PNG parity. Source material audit passes all ten materials and unchanged generator replay. Audio DSP is numerically checked; device listening/balance is not established by offline results.

- Observer collider overlay (2026-09-13): the user requested a physics bounding-box view. F8 / Xbox left-stick click cycles off, physics bodies and physics + query bounds. HUD-only color wireframes follow actual enabled colliders and live transforms, including hidden carriage proxies; disabled collision is excluded. Non-box/complex mesh envelopes are labeled approximate. Keep lines outside fixed NIR/thermal captures, keep collision itself unchanged and preserve near-plane/screen clipping. See docs/collider_overlay.md and run ColliderSmoke plus scripts/verify_collider_overlay.py after changes.

- Collider overlay validation: UE build, 34 Python unit tests and 8,003 independent native/image checks pass (artifacts/collider-overlay-v1). Twenty rendered views confirm 114 physics/72 query bounds when a car is present, 104/41 while empty, ten proxy translations/restoration, bounded near-plane projection and exact fixed NIR/thermal PNG parity. Actual overlay screenshots in dim/thermal and inside-car views were reviewed. Overlay off restores scene pixels exactly.

- Illuminated robot cabinet (2026-09-13): the user requested an illuminating LCD with gauges and LED buttons. One existing glass-material component contains the LCD, eleven round red/green lenses and one status LED. Geometry-derived masks retain the purchased cabinet and source swatches; a generated GPU material shows actual simulated cycle/robot phases, plus one small screen-spill light. No additional mesh, collider, capture, font texture, render target or thermal ID. Pause freezes readouts. Optical brightness does not alter thermal temperature. See docs/control_cabinet.md; new feature is enabled interactively and in CabinetSmoke, with legacy QA retaining original cabinet material.

- Cabinet validation: UE build, 34 Python tests and 606 native/image checks pass in artifacts/control-cabinet-v2. All twelve lighted lenses preserve source colors; LCD gauges visibly advance and pause/seek pixels restore exactly. Raw thermal power parity and fixed NIR/thermal observer-mode parity are exact. Actual visible/bright/NIR closeups reviewed; initial mirrored graphics corrected and LCD-only matte/zero-specular finish added for NIR readability. Original material/geometry and 244-profile count remain unchanged outside the enabled runtime presentation.

- Post-cabinet regression: all nine existing welding suites passed (12,261 independent checks) in artifacts/cabinet-regression with unchanged criteria.

## Host model-gate experiment (2026-09-13)

Process mode is opt-in with `unreal/Scripts/run.ps1 -Process`. The imported source has no separate doors, so four windowed door inserts and three adhesive strips are authored runtime additions; source geometry remains untouched. Two NIR views cover opposite sides, while R4 carries the thermal inspection camera and does not weld. The raw model images remain lossless 448 x 256 grayscale; iron/white-hot palettes are observer presentation only. Native policy QA is distinct from actual host inference. See `docs/process_inspection.md`, `docs/model_classification.md`, `docs/process_scene.md` and `docs/process_ui.md` for current implementation and measured limitations. No automotive model has been deployed to the EVK.

- Selected host experiment: two GPU-resident BF16 Cosmos models route doors/NIR to `process-shared-128-linear-v1` and both thermal gates to `process-shared-256-linear-v1`, with explicit `--linear-patch-embedding`. Both adapters were fitted on the shared corpus; modality-only fitting is still untested. The frozen routed validation had 84/84 correct actions; the single untouched test had 82/84 with zero observed false G, one healthy false rejection and one cool adhesive hold. Keep that limitation visible. Test median response was 54.2 ms; resident Torch allocation was 8.52 GB. See the selected-runtime command and provenance in docs/model_classification.md.

- Actual model-driven native run passed in `artifacts/process-selected-live-v1`: healthy/fault/healthy bodies exited/returned/exited, all four thermal acceptance audits passed, and the production launcher confirmed the matching session and explicit verdict. Median native HTTP decision latency was 68.01 ms while rendering; capture/consensus takes additional time. The final source build, 81 Python tests, 58 native policy checks, 28 launcher checks, all nine legacy suites (12,261 checks), 681 realism and 606 cabinet checks pass. No timers or ground truth override model holds. Camera coverage/radiometry and observer-mode sensor parity were checked independently. Keep screenshots, training data and weights private and preserved. No automotive EVK deployment has occurred.

- Earlier inspection-arm parking update: R4 deploys only for adhesive and weld-cooling inspections and smoothly returns to its authored withdrawn rest pose during arrival, NIR door checks, welding, departure and return. Transition duration is 0.8 seconds on the pausable simulation clock. Thermal capture waits for the unchanged calibrated active endpoint; pause freezes motion. Offline thermal captures explicitly seek that endpoint, while NIR captures use the resting R4 pose. The original adapter validation/test evidence remains tied to its retained dataset.

- R4 parking validation: UE build, 81 Python tests, 71 native policy checks, full three-body model-driven smoke and all nine legacy welding suites (12,261 checks) pass. Actual active/parked rendered poses were reviewed. A fresh seven-batch offline capture completed, with all 18 thermal images pixel-identical to the original matching episode; NIR images differ, so retained model accuracy metrics are not reasserted for a new dataset. Evidence is under `artifacts/process-r4-rest-*`.


- Portal curtain repair (2026-09-13): the user reported strange curtain behavior. Live startup could leave constraint components without bound Chaos handles, so strips now hold their authored rest pose through the first physics step, then bind once using the original body-local frames. Explicit pause/QA owns startup; no periodic pose reset was added. Metadata records actual live/unbroken handles. Suspension swing is +/-78 degrees and lower bends +/-12 degrees, replacing +/-82 at all three joints that allowed back-folding. Pause/resume preserves linear/angular velocity; explicit reset discards it.

- Process curtain clearance: shared ProcessScene constants use +/-2050 cm carriage endpoints, 3.2-second smoothstep and 1400 cm hidden tunnels. Peak speed stays approximately 9.61 m/s. Arrival settles for 0.2 seconds; return/departure retain 0.4 seconds before replacement. The earlier +/-1800 cm endpoints left strips supported by the returned car, allowing the next body's reversal to trap them. Legacy motion/tunnels are unchanged. Dedicated CurtainSmoke tests four passes and 10-second unassisted settling with body removal after return/exit; that interval is not a live delay. Separately run verify_curtains.py --process on actual ProcessSmoke telemetry to cover immediate replacement and real startup. Preserve completed model-smoke reports during teardown so destroyed constraints do not overwrite the final live snapshot.


- Curtain validation: final build and 81 Python tests pass. Dedicated artifacts/curtain-v3 passes 216 independent checks with 42 live hinges per portal, zero ceiling contacts, peak deflection 105.59 degrees and at most 3.479 degrees after 10 seconds settling. Maximum anchor separation was 6.739 cm; moving-pause position drift and linear-velocity restore error were zero. Actual model-driven artifacts/curtain-process-live-v4 passes healthy exit / loose-door return / healthy exit plus 27 independent live-physics checks: all 84 hinges live, peak deflection 105.64 degrees and peak anchor separation 2.245 cm, including immediate replacement. Native policy smoke passes 71 checks. The unchanged legacy curtain branch passed all nine welding suites (12,261 checks) in artifacts/curtain-legacy-v1. Reviewed entry/return/exit action and settled renders. Final classification evidence is operational compatibility of these runs; earlier held-out accuracy remains tied to its original dataset. Generated reports, meshes and screenshots stay private.

- Escape quit control (2026-09-13): Escape quits the demo directly, without a confirmation dialog.

- Local model cleanup (2026-09-13): retain the selected LoRAs `checkpoints/process-shared-128-linear-v1` and `checkpoints/process-shared-256-linear-v1`, plus `checkpoints/process-base-bf16` for the existing optional base GGUF launcher. The original WSL base is also retained. The port 18191 service merges the base and selected adapters in memory; obsolete disk merges, derived adapter GGUF exports and unselected adapter weights were removed. Small training reports, configurations, README files and provenance from retired directories are archived at `artifacts/process-model-v1/retired-checkpoints/<old-directory>/`; inventory: `artifacts/process-model-v1/model-cleanup-20260913.json`. Datasets, evaluation evidence, screenshots and reusable tools remain retained. Regenerate retired adapter GGUF exports for optional adapter experiments; preserve historical metrics as records of their original runs.

- Physical R4 scan (2026-09-13): the inspection arm now moves through three authored six-joint poses with a rigidly mounted camera, replacing the earlier camera-only gimbal aiming. Each trip uses 0.8-second quintic easing on the pausable process clock; the rest/deploy envelope remains. Thermal capture waits for the physical target plus three rendering settle frames. Pause freezes arm/lens and cancels partial images and pose audits; resume restarts at spot 0. Offline capture snaps each requested pose at the same frozen clock and relative cycle pose. Observer RGB/NIR lighting, navigation and palettes remain independent. Private captures use view_recipe `rigid-arm-thermal-scan-v1` and capture_recipe `process-arm-scan-episodes-v1`. These motions are illustrative, not industrial kinematics or collision certification. The UE build, 81 Python tests and 82 native policy checks pass; the policy run sends no HTTP requests. Original retained-camera 82/84 test accuracy does not validate the two changed thermal views, and no adapter has been retrained for this change.

- Physical R4 scan validation: `artifacts/inspection-arm-live-v1` passed the actual-model healthy exit / loose-door return / healthy exit sequence and all four thermal acceptance audits. Its 63 thermal captures/21 batches passed 3,894 scan/transport checks and 27 live-curtain checks; actual arm positions and raw thermal views were reviewed. The eight-episode `inspection-arm-capture-v1` capture produced 56 batches/160 PNGs, passed 11,311 scan/image checks and kept every offline thermal batch clock frozen. Only its 42 train/validation batches from six bodies were queried: 40/42 literal labels, 42/42 gate actions, zero observed false G; two hot adhesive R cases answered A and held. All 14 test batches were excluded. Median/p95 host responses were 55.34/76.77 ms. No training or adapter-selection change; retain earlier 82/84 as historical test evidence. CPU sampling of the source meshes found arm-to-car AABB clearance of at least 72.16 cm across 405 poses, not swept-volume certification. The current legacy render passes all nine suites/12,261 checks in `inspection-arm-legacy-v1`.

- Physical-scan radiometry audit: all 48 thermal batches agree with their pixel-derived G/A/R labels. Maximum expected-versus-rendered peak error is 0.496 grayscale levels (0.194 C); all target projections have at least 25.6 px margin and no white saturation. Audit: `artifacts/inspection-arm-capture-v1/thermal-pixel-audit.json`. This is rendering QA across the new capture, not model evaluation of the reserved test rows.

- Process tuning and welding display (2026-09-13): the user requested 10% dimmer roof banks, 20% slower motion from rest, 20% shorter adhesive cooldown, and a thermal scale that represents welding temperatures above 1000 C. Roof direct output and tube emission use 0.9 gain (imported 1800 lumens/bank). Process approaches use speed scale 0.8: welding arms 1.25 seconds, R4 deployment 1.0 second; withdrawal and between-spot scans retain their previous durations. Adhesive tau is the original seeded 7-22 second draw times 0.8. The imported thermal observer adds independent 20-120 / 20-1600 C ranges via F10 / Xbox right-stick click; fixed model imagery remains 20-120 with saturated 120+ labels. Process welds have an illustrative compact 1600 C core and 420 C halo; legacy 420 C fixtures remain separate. Preserve opaque occlusion and distinguish apparent surface temperature from buried nugget temperature. See docs/thermal_welding_range.md. Validation for this tuning must be recorded separately from older adapter accuracy.

- Thermal/timing validation: final build and 81 Python tests pass. Native policy passes 89 checks; live host-model run completes healthy exit / loose-door return / healthy exit with four valid thermal acceptance audits. Dedicated high/low thermal range QA passes 79 checks, including exact fixed NIR/thermal PNG parity, palette inversion and restoration; the high-range hot spot reads about 1253 C apparent from about 1589 C assigned surface temperature. All nine legacy suites pass 12,261 checks; procedural thermal/spatial/transfer pass 43/55/138. Procedural screenshot QA now takes actual HUD image bounds so the larger temperature legend is excluded without relaxing palette thresholds. Fresh v2 capture: 56 batches / 160 PNGs, 11,367 geometry checks. Only 42 train/validation batches were queried: 39/42 literal, 41/42 action, zero false G; three disagreements were conservative holds and rendered labels were confirmed. Median/p95 response 51.76/54.91 ms. All 14 test batches excluded; no model training or policy change. Private reports use artifacts/thermal-tuning-*; details in docs/thermal_welding_range.md.


## Historical three-bead panel-v2 results (2026-09-14)

These paragraphs describe the previous geometry/runtime. The old 320 adapter and endpoint status below are historical; current two-bead v3 requires a compatible new adapter.

The current Python contract, image-only API, presence G/R consensus, stage-scoped adapter provenance and split guards pass 87 unit tests. The selected shared320 LoRA uses rank 8, language/vision targets and the opt-in linear patch projection. Its lineage is 128 initial updates, 128 compatible continuation updates, then 64 updates on v3 data at 1e-5 with batch 6/accumulation 4. Each thermal batch contains two matched G/A/R episode triplets. The final 64 updates took 194.08 seconds with 6.42 GB peak Torch allocation. Earlier 128/256 candidates remain development records; old v1 weights/data are historical and incompatible with the panel contract.

Selection was frozen after 96/96 v3 validation. One v3 held-out evaluation scored 95/96 letters/actions, with no wrong presence branches and one adhesive A-to-G at 43.8995 C apparent/surface. Both NIR stages and weld were entirely correct; all 24 clearly cool thermal cases were accepted. This is an experimental simulation model, not production qualification. Held-out HTTP median/p95 were 47.59/53.11 ms; GPU forward/scoring median 39.78 ms, excluding arm motion/capture/cooldown.

Final v4 capture completed 512 batches/1,536 PNGs from 64 episodes, seed 2907, preserving 40/12/12 whole-episode splits. Camera/PNG QC passed 122,658 checks and train/validation thermal QC 936. Frozen320 compatibility on the same 12 validation episodes passed 96/96 with zero false G or wrong branches. Of 288 validation PNGs, 255 were byte-identical to v3; 24 presence and 9 missing-panel seating images changed. All 216 thermal validation images were unchanged. No v4 test images were queried, and the original 95/96 test result remains explicitly v3 evidence. Compatibility HTTP median/p95 were 51.05/61.57 ms.

The final native run passed assembly/preassembled/assembly with three departures and six thermal audits; 27 image-only requests included no ground-truth fields. Camera/curtain postchecks passed 5,086/27. Median HTTP event latency was 84.68 ms with Unreal rendering, compared with 51.05 ms standalone v4 compatibility. Frozen320 is promoted to default 18191; duplicate 18192 is stopped and adjacent 18084 was untouched. The model fixture requires assembly/preassembled/assembly branches, three forward departures and six action-time thermal audits; these audits can fail a report but never override model decisions. Current commands, evidence and limitations: docs/process_inspection.md and docs/model_classification.md. Keep historical v1 paragraphs and docs/model_classification_v1_history.md intact.

- Panel-v2 heat refinement: the exposed contact core retains 1600 C and 0.65-second cooldown; the 420 C halo now cools with a 5.0-second time constant so heat persists through NIR seating inspection. New offline episodes include mixed contact temperatures and label the maximum across the batch, never each individual image independently. This is panel-exposed-contact-core-halo-v3, separate from the old door-surface 2.5-second halo.

## Current two-bead panel-v3 implementation (2026-09-14)

Contract automotive-panel-process-v3 keeps four stages and three images per stage. Adhesive mapping [0,1,0] refers to exactly two longer rounded front beads; the rear-middle bead is removed. Native metadata uses rigid-arm-panel-two-bead-scan-v3 / panel-two-bead-episodes-v3 / panel-two-bead-contact-core-halo-v4 and records adhesive_view_beads plus two adhesive and three weld entries. Readiness labels use max over the real targets, never an invented third bead.

The updated protocol and mapping regressions pass 96 Python tests; 34 launcher fixtures pass. Completed rendering and frozen model results are recorded below. Private runners are under artifacts/panel-two-bead-model-v3, capture at artifacts/panel-two-bead-episodes-v3, fresh seed 4307, 64 episodes/512 batches/1,536 PNGs. Initial fitting starts from the unadapted base with 128 updates, rank 8, multimodal, batch 6, accumulation 4 and LR1e-4 and recorded linear patch projection. Do not initialize from or relabel the incompatible old 320 adapter. Wait for physical geometry, PNG/thermal QA and explicit GPU handoff before training or model requests; final test is used once after candidate/runtime/policy freeze. Preserve old v1 and panel-v2 records in their historical scopes.

Sensor initialization refinement: apply fixed/R4 capture flags after UE component registration, preserve three actual discarded sensor captures at each settled R4 pose, and reject raw colored sensor frames before PNG grayscale conversion. Imported sensor frame sequence is pinned for deterministic NIR rendering. Interactive visible observer DLSS Quality/Lumen/hardware ray tracing are independent display options (F6/F7/F11); fixed/R4 sensors and observer NIR/thermal retain native scale with GI/reflections/ray tracing off, and legacy smoke fixtures preserve their former graphics defaults. Rendered parity remains required before new capture.

The first two-bead pilot correctly failed its raw grayscale guard at engine frame 7: three actual sensor primes occurred while startup assets/material resources were still settling. The controller now polls asset/shader compilation and requires five continuous idle seconds before ordinary live/offline capture begins; fresh reply timing starts afterward. Four native policy cases exercise compiler-busy/reset/stable-idle behavior. The grayscale guard remains strict. The rebuilt pilot-v4 passes 64 batches/192 PNGs, 15,979 camera/PNG checks and 108 train/validation thermal contacts, worst error 0.18719 C; its readiness timestamp is 6.193 seconds and every view has three actual prime captures. The failed pilot produced no manifest and is not training data. No pilot model queries were made.

Fresh two-bead dataset completed at artifacts/panel-two-bead-episodes-v3: seed4307, 64 whole-body episodes, 512 batches/1,536 PNGs; independent audits pass127,778 camera/PNG checks and936 train/validation thermal contacts, maximum error0.3076 C. Preparation writes data/panel-two-bead-v3/shared with320train/96validation/96test batches and40/12/12 episode splits. Source manifest SHA256018a7fe1eef25566ffe55edf636d1a751f2b27cfce8c0cadfb6b4d9987c3d697. These audits establish image quality only; the separate frozen model results are below.

- Observer thermal update (2026-09-14): the user requested a fixed observer PoV window of 0-120 C. Remove the F10/right-stick range switch and interactive high-range state. Retain all three palettes and 120+ saturation marking; fixed/model cameras stay linear 20-120 C. Physical weld cores still reach the authored 1600 C and simply saturate in the observer view. Earlier selectable 20-1600 C screenshots and checks above are historical. Current display QA checks three palettes/restoration, real GPU 0-120 mapping, hot-weld clipping and exact sensor parity.

The two-bead 128-update candidate completed from the unadapted base in 374.94 optimizer seconds (5.999 GiB peak). Exact runtime validation: 86/96 letters, 94/96 actions; presence 12/12, seating 12/12, weld 36/36, adhesive 26/36, all 24 clear-cool G. Two adhesive A-to-G errors at 43.8395/44.1706 C disqualify it; eight hot R became safe A. Median/p95 HTTP were 49.62/54.48 ms. This candidate was retained as an unsuccessful development experiment. The authorized continuation adds 128 updates from compatible 128 to a fresh 256 output at 3e-5, with the same batch 6/accumulation 4, data and policy. The test set was untouched until that later candidate was frozen.

- Fixed observer and graphics validation (2026-09-14): artifacts/observer-fixed-range-v1 passed native and all 50 checks. The independent 0-120 C observer mapping agrees over 105,404 un-clipped pixels within 0.833 grayscale levels; fixed/model 20-120 C PNGs are unchanged, all three palettes and restoration pass, and hot weld pixels saturate. Actual iron and 0/60/120+ scale reviewed. Final artifacts/two-bead-graphics-v4-fixed-range passed native and 2,012 checks across 32 observer states with exact fixed/R4 NIR/thermal PNG parity for all DLSS/Lumen/HWRT combinations. These fixtures send no model requests and do not benchmark performance.

The final legacy/capture regression in `artifacts/two-bead-legacy-capture-v1` passed all nine welding suites (12,261 checks), native readback plus 10 independent checks with exact sync/async PNG parity, and all four local ephemeral mock bridge cases (G/A/R/ERROR). Actual intermediate robot and IQ9 prop screenshots were reviewed. Two initial choreography failures were stale 86-mesh expectations after the conserved 90-group asset split; only that exact count was corrected, and the original failure evidence is retained. No motion/image thresholds were relaxed. Native `artifacts/two-bead-policy-v1` passed 91 policy checks and the production matching-session report validator. All native jobs exited successfully without engine ensures/asserts/fatal errors. These checks do not establish model accuracy.

The two-bead shared256 continuation is selected and frozen. It adds 128 updates at 3e-5 to compatible 128, with the same batch 6/accumulation 4 and data, in 367.84 optimizer seconds. Exact merged Torch validation: 95/96 letters, 96/96 actions, zero false G or wrong presence branches; one hot adhesive R holds A. Frozen held-out: 95/96 letters/actions, zero false G or wrong branches; one cool weld G holds A, with adhesive cool recall 12/12 and weld 11/12. NIR stages are 24/24. No test-driven tuning/reselection occurred. Held-out standalone HTTP median/p95 = 49.28/54.55 ms; GPU median 41.17 ms. The exact candidate passed native ProcessSmoke and was promoted to 18191; duplicate 18192 was stopped after verification. Checkpoint: checkpoints/panel-two-bead-shared-256-v3. Exact alias: Cosmos-Reason2-2B-Torch-BF16-dccfe9dbc77f3fde4c565ecb826a8b84-linear-nonoverlap-v1; weights SHA256: c39011c523ae17c1a9148bc8a9b38f1bb9d0a568a34671c4d47488954323c059. This remains small synthetic-camera evidence only.

Final two-bead integration/promotion: artifacts/two-bead-live-v1 passed assembly/preassembled/assembly, three forward departures, and all six thermal acceptance checks (adhesive/weld: 29.55/27.16, 34.06/20.26, 33.71/27.10 C). Wall time was 182.31 s and simulation time 164.50 s. All 30 requests were image-only with zero ground-truth fields; camera/curtain verifiers passed 5,830/27 checks. Median/p95 native HTTP time was 84.43/92.83 ms across the 30 actual reply events, excluding duplicated phase/body metadata. Frozen shared256 now serves on primary 18191 with exact health/provenance; duplicate 18192 PID741141 was stopped after verification. Adjacent 18084 was untouched. Private startup: artifacts/panel-two-bead-model-v3/serve256-primary.sh; authoritative record: selected-runtime.json and primary-health.json in that folder. No further training, policy changes or test reuse followed the frozen held-out result.

- Training status audit (2026-09-14): both two-bead training logs end in completed adapter-save records. Initial128 completed at 17:48:32 CEST; selected256 continuation completed at 18:10:12 CEST. Each run performed 128 optimizer updates; recorded optimization is 374.940 + 367.844 = 742.784 seconds (12 min 23 sec), separate from scene preparation, rendering, evaluation and deployment. No newer incomplete training artifacts were found. README and current guides now prioritize panel-v3 startup and distinguish its NIR/thermal workflow from procedural reference training and historical results.

- Host lifetime recovery (2026-09-14): interactive run panel-interactive-20260914-182031 completed five bodies, then lost the model endpoint between 18:26:04 and 18:26:10 CEST during body 6 presence consensus. It correctly held on 683 connection failures; the host log ended without an exception and WSL did not restart. The precise termination cause is unrecorded. Replace the unsupervised primary launch with repository-scoped transient user-systemd unit dark-factory-panel-model.service, with bounded restart-on-failure (5-second delay; three starts per 120 seconds). This survives launch-shell/Codex lifetime for the current WSL session, not Windows/WSL shutdown. Keep compatible weights/policy unchanged, reuse an already healthy endpoint, and never stop adjacent services. Recovery details are private in artifacts/panel-two-bead-model-v3/connection-recovery-20260914.json.

- Service recovery verification: the restarted interactive demo completed at least three bodies with no model connection errors and the same selected256 alias. The service remained active under the WSL user manager. scripts/model/start_host_service.sh now provides reproducible detached startup with existing-unit/port refusal and explicit STARTING/readiness messaging. Ten isolated launcher tests and the full 106-test Python suite passed; a real duplicate-start attempt refused without changing the running PID or healthy endpoint. No capture, model policy, weights or Unreal code changed for this recovery.

- Tool/base refinement direction (2026-09-14): copy the original blue rack H-bars for all three gripper frame members, retaining the rack material; shorten the connecting stem by one quarter (122 to 91.5 cm), shifting the pad/frame assembly 30.5 cm and refitting robot poses to preserve exact panel contacts. Rotate the adhesive wrist at rest to clear the fixed-base housing. Move all four IQ9 props from upper arms to the outward raised junction covers on fixed bases, matching the user screenshot; retain decorative LEDs and thermal profiles. R4 lenses, two beads and three weld contact positions stay fixed; verify motion and recaptured model-view compatibility after these geometry changes.

- Completed tool/base refinement (14 September 2026): the gripper now reuses three copies of source rack H-bar island158 (732 triangles each), with the original blue rack material. Auxiliary export/import preserves all90source components,3681islands and1761208source triangles. Stem122->91.5cm and flange/panel offset150->119.5cm; the main bar uses8cm depth to meet the stem. Refit panel endpoints remain within0.0021cm. R1rest wrist turns20degrees; R3parked wrist unfolds45degrees. R2contact yaw135/135/210degrees clears the lower holder while retaining source gun, actual weld contacts/normals, R4poses and adhesive paths. Revised source-triangle/proxy audits check the actual weld entry and conservative112x8x8cm main-bar envelope; no checked tool/casting intersections. Existing rigid-panel rack/insertion fits remain explicitly excluded from clearance claims.
- All four IQ9 props now parent to the outward raised fixed-base junction covers (`fixed-base-outward-junction-cover-v2`), so they remain stationary during joint motion and follow process base relocation. R3 mounts6cm higher on the same cover to clear stored panels; its13cmplate has2.810cm top overhang and a supported lower fastener region. Generator checks complete enclosure/bracket boxes against80280relocated rack/stock triangles, with no overlaps. R3conservative lower-bound clearance is0.579cm enclosure/0.126cm bracket;9support probes and81plate probes preserve0.30cm base gap. Photo hashes, decorative LEDs and28thermal profiles are unchanged. First renders exposed the stock overlap, which was corrected before acceptance.
- Final refit evidence: `tool-refit-motion-v4` passes29rendered checkpoints with tool/rest/EVKcloseups; `tool-refit-legacy-v2` passes all9welding suites/12645checks, including6219expansion checks; `tool-refit-policy-v1` passes91native policy checks and launch validation; Python discovery passes106tests. Final `panel-tool-refit-episodes-v3` contains64episodes/512batches/1536PNGs with seed4307 and unchanged whole-episode splits. Camera/PNG QA passes127778checks and train/validation thermal QA936contacts, maxerror0.308C. Frozen shared256 compatibility remains95/96letters,96/96actions, zero false proceed/wrong branches; one hot adhesiveR->Ahold. HTTPmedian/p95=54.70/61.41ms. These reuse the same12validation episodes; weights/policy unchanged, no new held-out queries or training. Preserve prior captures as separate development evidence.

- Final `tool-refit-live-v3` passes assembly/preassembled/assembly with three departures, all six thermal acceptance checks and30image-only model requests, in182.44wall-clock seconds. Camera/curtain postchecks pass5830/27; no ground-truth fields were sent. Native policy and live model tests remain separate. The selected host service stays healthy with no restarts; the interactive demo was reopened after verification.

- Left-station swap preparation (14 September 2026): logical identities and spot1/spot2/spot3 image order stay unchanged while R2/R4 exchange station functions. Source robots_tiles island7 is now separately exported as SM_AssemblyPedestal_R4 (92 triangles); grouping preserves 1,761,208 triangles, 3,681 islands, every existing component pivot and the source FBX hash across 91 source components. The rack H-bar remains an auxiliary copy. Physical pedestal offsets are separate from logical robot base offsets. The direct source-gun mount preserves the measured cap interface; the process-only rigid camera mount is Ry(+60 degrees) times the prior quaternion, at the unchanged (0,0,-24) cm translation. Earlier tool-refit renders, scores and live runs do not validate this layout. Rebuild, render the changed trajectories and sensors, check gate compatibility and run the actual model sequence before recording new acceptance evidence.


## Current six-weld refinement (14 September 2026)

The user requested removal of the EVK backing plates and the returning diagonal rear-middle support. The two front adhesive/contact flanges now receive six welds, three on each, ordered left to right; there is no rear-middle weld or generated support. Logical R2/R4 retain the swapped stations. R2 uses its directly mounted source gun at 0.7 scale and parks at [-150,0,0,0,0,0]; a 25-sample check of each corrected camera route leg clears the parked welder; final NIR withdrawal goes via spot0. The holder keeps its 91.5 cm stem and blue source H-bars, grips 65 cm off center, and seats four articulated 8 cm pads on solid panel surfaces.

EVKs retain their source photos and LEDs, attach directly with a 0.30 cm enclosure gap and add 24 thermal surfaces, without mounting plates. Exactly three physical model images remain per stage. Weld thermal views now cover pairs [[0,1],[2,3],[4,5]], while NIR and adhesive targets keep their prior definitions. Contract automotive-panel-process-v3 and prompts are unchanged; mechanical recipe six-front-weld-direct-gun-paths-v7, view rigid-arm-panel-six-weld-scan-v4, capture panel-six-weld-episodes-v4 and thermal panel-six-weld-contact-core-halo-v5 distinguish new evidence. Every one of six welds has its own halo, 1600 C exposed surface core and cooling clock. The GPU LUT is 256x38 floats (152 KiB), with twelve bounded pairs and an active count so surfaces without welds skip those loads. Visible/NIR emission and weld witness material branches support six marks, preserve source inputs and replay idempotently.

Native motion currently passes 38 rendered checkpoints; the rebuilt thermal fixture and thermal/spatial/transfer checks pass. Paired-contact capture, calibrated model validation and actual live integration are complete as recorded below; earlier three-contact model scores retain their original scope.


- Six-weld capture and fitting (14 September 2026): all six contacts are at Y=-241 cm on exposed source metal, 6 cm behind the two adhesive bead centers. Stepped backing flanges have lower support wings under this row. Full seed4307 capture passes127,778camera checks and1,404thermal checks, maximum error0.318C. Narrow adhesive QA uses3x3on-bead centers; the prior5x5sample crossed onto warm steel in one cold case. Welds retain5x5peak sampling and2.5Cerror tolerance. Final192PNGrecapture is byte-identical after the NIR withdrawal route fix. The latter routes spot2 via spot0 in0.8s;25samples per leg plus stage-family transitions clear the parked welder. Native motion has38rendered checkpoints, Python109tests and calibrated policy91checks pass; legacy nine suites, thermal/spatial/transfer and681realism checks pass.

- Selected six-weld candidate: checkpoints/panel-six-weld-shared-448-v4, backend Cosmos-Reason2-2B-Torch-BF16-8f759484957330487be026656f6244d4-linear-nonoverlap-v1, weights SHA256016e01e859bd821c8b3d57dfdb835c733330b8da84e441c834a818a3f075f288. New training adds128steps at3e-5 (401.85s), then64steps at1e-5 (200.04s), to the old256 lineage. The second fit uses50whole training bodies/400batches, adding10training episodes from independent seed7319. All validation remains the same12development bodies/96batches. No new held-out query. A separate thermal64continuation (201.30s) with modality routing did not improve the residual error and is not selected. The existing prespecified confidence grid selected0.98 over0.80: exact runtime gives95/96labels/actions, zero falseG/wrong presence branches, adhesive cool12/12 and weld cool11/12. One cool weld holdsA; HTTPmedian/p95=53.45/58.85ms. Thermal limits45/55C, +/-3C ambiguity, labelmass0.5 and two fresh matching batches remain unchanged. The final model run and primary service promotion completed successfully.


- Final six-weld integration (14 September 2026): The selected runtime passed the actual Unreal process: assembly / preassembled / assembly, three departures, all six thermal acceptance audits and 29 image-only requests, in 183.41 wall-clock seconds. Camera and live-curtain postchecks passed 5,636 and 27 checks. Actual HTTP reply median/p95 during rendering was 84.27/84.93 ms across 29 distinct reply outcomes, excluding phase/body duplicates. No ground-truth fields were sent to the model. The exact frozen identity and calibrated policy now serve on primary 18191; the duplicate candidate service is stopped. The interactive demo was reopened. Private evidence: `artifacts/six-weld-live-v1` and `artifacts/panel-six-weld-model-v4/selected-runtime.json`.


- Imported camera (14 September 2026): `assets/Camera/CameraExport.blend` now supplies the R4 housing/lens appearance. Rebuild with `unreal/Scripts/import_inspection_camera.ps1 -Blender <Blender5.0+>`; generator and importer are reproducible, source and all derived FBX/maps/packages stay ignored. Preserve mount-local optical origin (21,0,0) cm and rigid arm poses; visible front rim is (20.5,0,0). Two opaque atlas materials, three LODs (97,534 / 14,630 / 3,901 total triangles), existing two thermal profiles. Eight episodes / 192 sensor PNGs are byte-identical to `six-weld-pilot-final`; camera15,979 and thermal162 checks, Python109 tests pass. Sampled conservative camera boxes clear the parked welder on all within-stage inspection paths. See `docs/inspection_camera_asset.md` and private `artifacts/camera-import-*` evidence. Show actual verification images inline in chat.

- Camera final integration: 41 rendered motion checkpoints, all nine WeldingSmoke suites, native policy91checks and actual model assembly/preassembled/assembly all passed. Live: three departures, six thermal acceptance checks,29image-only requests,183.52s; live camera5,636checks pass. No new adapter training was needed for the byte-identical eight-episode sensor fixture. Private evidence `artifacts/camera-import-live-v1`; actual camera closeups are in `artifacts/camera-import-motion-v2`.


## Pinch gun and perimeter sealant (15 September 2026)

The user requested a visibly opposed pinch weld, animation of the existing gun's jointed electrodes, Sealant/Seal naming, and continuous contact strips including the sides. Leave the demo closed after this task. Continue showing actual verification images inline in the chat when making visual changes.

Process mode now copies the source gun into chassis and two bearing-pivot jaw meshes, retaining all 43,422 source triangles, UVs, normals and original materials. Positive 0.7 scale and the native cap mount remain, with no extension rod. The jaws open 14 degrees for travel, close to a 1.2 cm sheet-stack gap, hold through the current pulse and release before transferring. Six contact normals set the opposing direction; work yaw varies 0/0/0/15/30/45 degrees. The choreography is authored, not force-controlled industrial kinematics. Legacy whole guns remain unchanged.

Two 41-point sealant runs cover front, side and rear contacts, fitted to exposed source ledges and around ventilation holes. Runtime builds two rounded purple bead meshes once, reveals them along the dispenser path and retains separate cooling clocks. Sealant application takes 13.5 seconds and pinch welding 11.5 seconds. The isolated rear-middle patch/support remains absent. The three rigid R4 thermal poses now inspect the full right/left/right perimeter; NIR framing and six weld pairs remain. Internal stage key adhesive remains stable; user labels use Sealant and the cabinet row reads SEAL.

Current view/motion/heat recipes: rigid-arm-panel-perimeter-scan-v5 / perimeter-sealant-pinch-paths-v8 / panel-perimeter-contact-core-halo-v6. Capture v5 covers the revised geometry; v6 adds near-ambient thermal examples; v7 additionally randomizes 28 robot casting temperatures independently of labels, frozen within each offline episode and bounded by existing profiles. This offline nuisance variation does not modify live motor heat. Current sealant prompt SHA is 5cdbc5c87e70601842c48e1b408fc754ff15f2430c2e875d6d61bc8219ca651f; the older six-weld adapter cannot serve it. Runtime thermal encoding and gate limits/policy are unchanged. See docs/pinch_welder_and_sealant.md; final model selection and verification must be recorded separately below.


- Final perimeter adapter: checkpoints/panel-perimeter-shared-768-v5, backend Cosmos-Reason2-2B-Torch-BF16-123a74ff20dcfbc802caa4aff93a9424-linear-nonoverlap-v1, weights SHA256 6e9422b830ebbe247c9e600d41773372f0656cb428318d07028689eb4ec04519. This refinement added 128+64+128 updates to the prior448 lineage, with an explicit original-contract warm start for the changed sealant prompt. Latest fitting uses100whole training bodies/800three-image batches from seed4307/7319/9157. Final development validation includes36bodies/288batches (adding6v8 bodies from seed14791), all288labels/actions correct, no falseG/wrongbranches. HTTPmedian/p95 52.84/58.52ms standalone; threshold.98 and all gate semantics unchanged. No reserved test queries. The v8 cohort's20training bodies were not fitted. Shared576/640 were intermediate candidates that held on live input; only768 is selected. Exact evidence stays in artifacts/pinch-sealant-v1/selected-runtime.json.

- Capture v8 keeps unwelded held-panel weld fields at ambient, matching the live sequence; preassembled/post-weld captures retain residual heat. Camera geometry and NIR processing are unchanged; all48same-seed NIR PNGs across8episodes are byte-identical before/after motor and contact-state thermal changes. The three physical views and full-perimeter coverage audits pass. Cold coverage uses a3x3median to reject isolated black opening pixels; hot samples independently establish exposed geometry. Never use this private QA as a model input or live fallback.

- Actual final perimeter integration: artifacts/pinch-sealant-live-768 passed assembly/preassembled/assembly, three departures,29image-only requests and all6thermal acceptance checks in201.26wall-clock seconds. Camera/transport5,636 and live-curtain27postchecks pass. No ground-truth fields sent. The service on18191 serves the selected768 identity; adjacent18084 is untouched. The user asked for the demo to remain closed after work; do not reopen it at the end.

Final regression: all nine legacy WeldingSmoke suites pass (12,613 checks), alongside 91 native policy checks, 113 Python tests, 44 rendered motion checkpoints, 1,375 pinch checks, 606 cabinet checks and 43/55/138 thermal checks. Artifacts: `artifacts/pinch-sealant-legacy-v1` and `artifacts/pinch-sealant-v1/final-verification.json`.

## Three-edge sealant and raised panel (15 September 2026)

The user requested removal of the black strip, sealant on only the three
panel/body contact edges, and a small panel lift. Generated backings, webs and
legs are removed; the free rear edge has no sealant. Two rounded purple runs
are 4 mm high, while the seated panel, holder targets and six weld sites are
raised 5 mm. Original source assets and the pinch-gun mechanism remain intact.

The user explicitly selected inspection of heat transferred into the panel
surface when the thinner sealant became hidden. The thermal gate now reads
opaque metal contact bands, with a local analytic 0.35 s response to each cooling
sealant source and sixteen GPU capsule segments. No through-steel rendering or
whole-car solid-box solver is used. This remains an illustrative surface
readiness proxy, not adhesive cure or hidden material thermometry. New prompt
and capture/heat recipe hashes require a newly validated adapter. See
docs/panel_contact_heat.md for implementation and reproduction.

The completed host selection uses two resident adapters: primary
panel-contact-shared-960-v7 for presence, sealant and seating, with
panel-contact-shared-896-v6 for weld only. Explicit --weld-adapter routing uses
the stage alone, one forward pass, never a predicted result. Shared896 failed
live presence; shared960 alone falsely approved one ambiguous weld validation
batch. The routed combination scores 191/192 across 24 development validation
bodies, with no false G/wrong branches and one conservative cool-sealant hold.
Confidence stays .98 and label mass .5. Median/p95 standalone HTTP is
50.65/54.99 ms; resident CUDA allocation is 8.52 GB. No reserved test inference
or IQ9 deployment occurred.

Training used two full 64-body cohorts (seeds 18517/27139, recipes v10/v12),
80 train / 24 validation / 24 reserved test episodes, plus one manually reviewed
live missing-panel batch in a separate training-only episode. This gives 641
training batches/1,923 images, and 192 validation batches/576 images. The added
fits took 128+64 updates (432.3+191.2 seconds logged optimization), from the
previous shared768 lineage. The live example is not independent evaluation.

Actual routed ProcessSmoke completed assembly/preassembled/assembly in 183.41 s,
24 model requests, three correct departures and all six thermal acceptance
checks. The 13 logged decision events measured 83.46/84.21 ms median/p95 while
rendering. Camera checks 4,666 and process curtain checks 27 passed. Motion 47,
pinch 1,655, thermal 43/55/138, all nine legacy welding suites, 116 Python tests,
and both numerical heat checks passed. Actual visible and hot/cool thermal
screenshots were shown in chat. Unreal and the temporary 18192 comparison host
are closed; the selected 18191 model service remains ready. Current reproduction
and remaining accuracy limits are in docs/model_classification.md and
docs/panel_contact_heat.md. No commit or push was performed for this request.


## Rear roof clearance (15 September 2026)

The user requested removal of the back-most roof section intersecting the
welder. Process-only body variant `rear-roof-open-v1` omits
`SM_BodyPanel_0385` (source chrome island 385), including mesh collision.
It stays omitted through visibility toggles and body turnover. The existing
roof curtain collider is refitted in carriage-local space to retained roof
sections (Y about -90.662 to 44.929 cm). Source assets, imported component
conservation, thermal profile IDs, other roof pieces and legacy bodies remain
unchanged. Robot motion, all six pinch welds, sealant and sensor poses are intact.

A 50-checkpoint motion run and 3,275 pinch/body checks passed, including matching
visible/NIR/thermal closeups and a final-weld closeup. Separate CurtainSmoke
passed 216 checks, with entry/return/exit contact and settled renders inspected.
The full 64-body seed27139 capture preserves the original v12 splits and labels;
127,778 camera checks, 1,404 thermal samples and 12,168 sealant coverage points
passed. Existing shared960/shared896 stage routing scored 96/96 on its 12-body
development validation split, with no false approvals or wrong branches. No
training, policy changes or reserved-test inference occurred. The eight-body
parity pilot is QA-only, not a training/evaluation cohort. Sensor pixels changed,
so prior PNG parity is not claimed. All nine legacy welding suites, 116 Python
tests and 34 launcher tests passed. Live integration completion is recorded below.

Evidence is private under `artifacts/rear-roof-*`; implementation and reproduction
notes are in `docs/pinch_welder_and_sealant.md` and `docs/model_classification.md`.

Final rear-roof integration: actual ProcessSmoke passed assembly / preassembled /
assembly in 183.06 seconds, 24 model requests, three correct
departures and all six thermal acceptance audits. Live camera 4,666 and curtain
27 checks passed, alongside 91 native policy checks. No ground-truth fields
were sent to the model. Unreal is closed; the existing 18191 model host remains
ready. The actual visible/NIR/thermal roof-clearance renders were shown in chat.
Final record: `artifacts/rear-roof-open-v1/final-verification.json`. No commit or
push was performed for this request.


## Two body clamps (15 September 2026)

The user requested removing the two body clamps inside relocated robot bases.
`ADarkFactoryCellMechanisms::Initialize` now skips the negative-Y station in
process mode: no `fixture_nx_ny` or `fixture_px_ny` parts are created. Retained
`fixture_nx_py` and `fixture_px_py` at Y=+92 cm still fit their pads 0.2 cm outside
source body panels and use the existing process closure/release timing. Layout
metadata is `panel-two-clamps-py-v1`. Sixteen primitive pieces are omitted; the
process mechanism actor has 43 components including its red stack-light tier.
Legacy initialization retains four clamps and its original 57 components.
No purchased geometry, physical collider or thermal profile was changed.

The process motion fixture now has 52 checkpoints and records actual mechanism
metadata. Every checkpoint verifies two fitted positive-Y clamps. Left/right
base closeups show the removed stations clear and the retained pads on the body;
those actual screenshots were shown in chat. Native motion and 3,275 pinch checks,
116 Python tests and the 681-check legacy RealismSmoke passed. The 192-image
same-seed parity fixture has 60 byte-identical images; the largest changed pixel
fraction is 0.433%, so exact parity is not claimed. Camera 15,979, thermal162 and
sealant-coverage1,404 checks passed. This capture is QA-only, not training or
accuracy evaluation. Final live integration and welding regression follow below.

Final two-clamp integration passed assembly / preassembled / assembly with
three correct departures, 24 image-only model requests and all six thermal
acceptance checks in 183.00 s. Live camera 4,666 and curtain 27 checks,
91 native policy checks and all nine legacy welding suites passed. The same
shared960/shared896 adapters and thresholds remain selected; no new offline
accuracy score or training is claimed. Unreal is closed; the 18191 model host
remains ready. Evidence: `artifacts/two-clamps-v1/final-verification.json` and
`artifacts/two-clamps-live-v1`. No commit or push was performed.


### Rack clearance revision (15 September 2026)

Relocated the process rack and all five stock sheets 45 cm outward and 70 cm
rearward: rack center approximately (515,-510,117) cm, source translation
(1652.2,-836.94,0) cm. Refit all pickup/lift endpoints and retained the original
seated-panel, sealant, weld and R4 targets; motion recipe is
`rear-rack-sealant-pinch-paths-v10`. The first rear-only candidate was rejected
because it overlapped the adjacent cabinet; the final diagonal placement is in
`artifacts/rack-clearance-motion-v2` and the private production descriptor.

The 56-checkpoint motion fixture and 3,275 pinch checks pass. Rest, pickup, lift
and restock screenshots were inspected; actual rest/lift images were shown in
chat. All five sheets stay on the rack until pickup, four remain during holding,
and the outer sheet returns for the next body. A 492-pose R3 casting audit finds
no intersections with the rack/four remaining sheets. Conservative rest bounds
have 1.68 cm minimum arm/stock separation and 7.56 cm stock/cabinet separation.
The existing source-fit sheet/rack intersection during initial extraction is
retained: four early contact samples occur in both original and changed motion;
no picked-panel/cabinet contact or contact after the lift is reported. This is
sampled visual evidence, not continuous collision certification.

All 116 Python tests, native 91-check policy fixture and nine legacy welding
suites pass. The eight-episode same-seed QA capture passes 15,979 camera checks,
162 thermal contacts and 1,404 sealant coverage points. Of 192 sensor PNGs,
108 are byte-identical; the largest changed pixel fraction is 5.414%, so do not
claim exact sensor parity. This pilot is QA-only, never training/accuracy data.
The same shared960/shared896 stage routing and model thresholds are preserved.

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


### Walk platforms support the remaining clamps (15 September 2026)

Moved both complete imported scaffold/deck assemblies forward and inward in
process mode. `front-clamp-support-v1` uses source offsets (70,600,0) cm for R1
and (-80,600,0) cm for R2, replacing the old +/-480 cm outboard offsets. Relative
to the previous displayed layout, the moves are 410/400 cm inward and 600 cm
forward. Deck centers are approximately (242.71,608.01) and (-243.93,608.01) cm;
rear edges begin at Y=50.42 cm, ahead of the relocated robot pedestals, with
minimum conservative pedestal separation 56.71 cm. No source meshes were
regenerated, resized or reauthored; the original height Z=85.592865 cm supports
the clamp feet. The existing two-clamp layout and pad/body contacts stay fixed.

The moved decks extend beneath the exit photoeyes. Their foot plates now sit
on the decks and their lower stems are shortened, retaining exactly the same
world optical heads, beam coordinates and occupancy logic. Entry posts and
legacy geometry keep their original ground mounts. No new primitives, lights
or thermal profiles were added.

PanelMotionSmoke now has 58 checkpoints, including an overall platform view
and exit-post closeup. Every checkpoint traces nine actual deck surface points
beneath each of the two clamp feet and two exit-post feet. All 2,088 samples
pass with maximum vertical discrepancy 0.000003806 cm. Both clamp closeups and
the platform/exit layout were visually inspected; actual overview and clamp
images were shown in chat. The native motion fixture, 3,275 pinch checks,
116 Python tests and 681 legacy RealismSmoke checks pass. The same-seed
eight-episode capture passes 15,979 camera, 162 thermal and 1,404 sealant coverage
checks. Of 192 sensor PNGs, 156 are byte-identical; the largest changed pixel
fraction is 5.0655%, so exact parity is not claimed. This is a QA-only pilot,
not model training or independent accuracy evidence. Final physics and live
integration results are recorded below after completion.


Platform regression follow-up: all nine legacy WeldingSmoke suites passed.
Both rendered CurtainSmoke runs passed the native physics/settling validator
and were visually reviewed for entry, return and exit. Supplemental QA retains
one timing failure per run: maximum frame deltas 0.120287/0.115981 s exceed the
unchanged 0.101 s bound; the other 215 checks pass. Do not describe these as
full supplemental curtain passes or relax the threshold. Evidence is in
`artifacts/clamp-platform-curtains-v1`, `artifacts/clamp-platform-curtains-v2`
and `artifacts/clamp-platform-v1/curtains*.json`. Native ProcessPolicySmoke
passes all 91 checks. Live process completion follows below.


Final platform integration completed assembly / preassembled / assembly with
three correct departures, 23 image-only model requests and all six thermal
acceptance checks in 180.30 s. Live camera QA passes 4,472 checks and live curtain
QA passes 27. The separate CurtainSmoke timing flags described above remain
recorded; no threshold was relaxed. No new training or model accuracy result
is claimed. Unreal is closed; the existing host endpoint remains available.
Final record: `artifacts/clamp-platform-v1/final-verification.json`.

### Permanent panel mounting frame and top-down placement (15 September 2026)

The body now permanently carries a copy of source panel 386 outer stampings,
with the central flat portion cut away. It supports both sealant runs before
the full replacement panel arrives. Keep the front and two side sealant edges;
there is no rear bead. This supersedes the earlier unsupported opening while
preserving the rejection of primitive black backing strips.

`prepare_panel_frame.py` clips the private source into 43,928 retained triangles
around a 183 x 76 cm opening, X=-90..93/Y=-322..-246 cm. Source FBX, original
island assignments, UVs/normals and material are retained. Surface-area
conservation passes; all 82 sealant points have zero measured source-support
gap. Unreal builds one static GPU mesh once and appends one ambient thermal
surface, preserving existing profile indices and the 70-row/280 KiB LUT.
The frame stays attached to every carriage, independent of panel presence.

`top-down-frame-pinch-paths-v11` retains rack extraction, turns the panel
horizontal behind the body, reaches a 90 cm hover, then lowers through 35/8 cm
clearances to the unchanged +5 mm seat. Release goes upward. All motion uses
R3 joints with the existing rigid gripper. No panel, weld or settled R4 target
was moved. Exact source edge audits still find peripheral body intersections
late in descent (5.10-5.52 s), including at the source installed position.
Angled/rearward candidates did not remove the final asset-fit overlap; retain
the simpler top-down path and do not claim swept-volume clearance.

The 63-checkpoint rendered motion fixture, 56 frame checks, 3,271 pinch checks
and 116 Python tests pass. Fresh eight-episode QA-only capture passes 15,979
camera checks, 162 thermal samples and 1,404 sealant coverage samples. Of 192
PNGs, 60 are byte-identical to the preceding platform fixture; maximum changed
pixel fraction is 4.2376%. This capture is neither training nor an independent
accuracy dataset. Actual frame/sealant, lowering and placed-panel screenshots
were inspected and shown in chat.

Actual model integration completed assembly / preassembled / assembly with
23 image-only requests, three correct departures and all six thermal acceptance
checks in 180.24 s. Live physical-camera and curtain checks pass 4,472 and 27
checks. Shared960/shared896 routing, prompts, thresholds and consensus remain
unchanged. No new adapter training or held-out accuracy is claimed. Frame
provenance is included in private scene metadata; prior model scores predate it.
Evidence lives under `artifacts/panel-frame-v1`, `panel-frame-motion-v1`,
`panel-frame-capture-v1` and `panel-frame-live-v1`. See docs/panel_mounting_frame.md.

Frame follow-up: all nine legacy WeldingSmoke verification suites and native
91-check ProcessPolicySmoke pass. All Unreal fixtures exited and the demo is
closed. Final record: `artifacts/panel-frame-v1/final-verification.json`.
The source-fit intersection audit is retained as a limitation, not a pass.


## Current Unreal Editor entry point — 15 September 2026

The user's empty editor/old Play scene came from an empty runtime-built map and
missing launcher flags in PIE. `/Game/Maps/DarkFactoryPanel` is now the Editor
startup and default game map, with `DarkFactoryPanelGameMode`. Without explicit
scene/QA selection it starts the current panel process and bright observer view.
Explicit launcher scene selection retains legacy/procedural/fixture behavior.

`DarkFactoryEditorPreview` rebuilds the current imported process layout, tools,
source mounting frame, camera, rack, EVKs, cabinet, mechanisms, portals and lights
using the production builders. Its three static poses are awaiting panel,
sealant applied and panel placed. Only the generator is saved; its tagged
children are transient and rebuilt after map load, including with viewport
realtime disabled. The children deliberately remain visible in editor Game View.
The game mode removes the generator/children before runtime BeginPlay. No preview
scene captures, inference or simulated bodies run. Preview edits do not decide
live branches, and manual child edits are not persistent. Use source generation
for lasting changes and the Details Refresh Preview button after reimporting.

Map generation is `unreal/Scripts/setup_editor_map.py`, included by setup_content.
It saves ignored packages locally without source-control checkout. The new map
and its initial overview camera are generated on this workstation. See
`docs/unreal_editor.md` and the README Editor section.

Rendered editor lifecycle QA passed 27 checks: 10 preview actors / 356 mesh
components, zero captures/physics, idempotent refresh, map save/reload, two
Play/Stop cycles, one runtime scene/controller and no leaked preview actors in
PIE. Live PIE received two fresh Reason2 missing-panel decisions and began
sealant application with the existing shared960 adapter. This is integration
proof, not a new accuracy result. Actual editor and PIE screenshots were shown
in chat. Early QA reports preserve the initial save/hidden-window failures;
Game View initially hid editor-only children and was corrected. Evidence is
private under `artifacts/editor-workflow-v1`.

Final editor runtime validation: UE build and map generation pass, 116 Python
tests and 34 launcher-policy checks pass, and all nine legacy WeldingSmoke
suites pass. Actual ProcessSmoke completes assembly/preassembled/assembly in
179.24 seconds with 23 requests and all six thermal acceptance audits. Physical
R4 runtime telemetry passes 4,472 checks; live curtain telemetry passes 27.
The DLSS marketplace metadata now matches the installed plugin, removing the
unrelated project-out-of-date notice. No adapters or model thresholds changed.

All three editor poses and the reflected Refresh Preview action passed rendered
checks after the final build. Native ProcessPolicySmoke also passed. The final
record is artifacts/editor-workflow-v1/verification.json. Editor and demo are
closed; the existing model service is unchanged.


## Repository cleanup — 15 September 2026

Removed 307 obsolete local files: the launcher pinned to the retired six-weld
adapter, its stale current_demo pointer, removed-door audit scripts, old
pose-search drafts, a one-off runtime patcher, redundant two-bead material
wrappers, bytecode caches, dead Unreal PID files, empty logs and a temporary
Blender download listing. Retained two PID files whose numeric IDs are currently
in use. No processes were stopped for cleanup.

All 45,512 artifact/document/Saved image and video paths retain their exact sizes
and modification times. Purchased assets, imported content, Blender tools,
checkpoints, datasets, source generators, current QA tools, historical model
provenance and failure reports remain intact. The generic Python ignore template
was reduced to rules relevant to this repository, retaining private asset,
render, checkpoint and local-plugin exclusions. Private deletion hashes and
verification are under artifacts/cleanup-2026-09-15.

Cleanup validation: rebuilt with UE 5.8; all 116 Python tests and 34 launcher
checks pass. No retained helper imports a removed draft. The staged publication
audit contains only source, configuration and documentation. No new screenshots,
purchased/generated assets, datasets or model weights are included.


## Local Unreal MCP enabled (15 September 2026)

Enabled bundled ModelContextProtocol and AllToolsets for Editor targets only,
with local ignored EditorPerProjectUserSettings auto-start at 127.0.0.1:8000/mcp
and a corresponding user-level Codex MCP entry. UE 5.8 build passed. Direct MCP
initialize/discovery and read-only scene calls returned DarkFactoryPanel and
18 actors including the generated preview cell. Private evidence is in
artifacts/unreal-mcp-setup. The editor was left open for MCP access, with no
Play session started; this launch caps editor FPS at 15. The current agent
tool registry may need client reconnect/restart to expose the new tools.
Keep durable scene edits in generators/config rather than transient preview
children; MCP is editor authoring infrastructure, separate from Reason2.
See docs/unreal_editor.md.


## Persistent editor authoring layer (15 September 2026)

DarkFactoryAuthoring is an Editor-only Python/toolset plugin. Its five MCP tools
inspect/rebuild the production preview, persist a pose and viewport bookmark in
tracked unreal/Config/Authoring/preview.json, and expose the source ownership
guide in docs/authoring_structure.md. Map load and setup_editor_map.py apply this
recipe; direct Details pose changes and generated-child transforms remain
transient experiments. Geometry/motion still belongs in production builders and
preparation scripts, not arbitrary serialized copies. Authoring rejects PIE and
other maps; preview settings never choose model branches or sensor contracts.

AllToolsets is now explicitly disabled. It pulled in Game Features and caused
map-generation commandlets to exit with asset-manager errors. The authoring
plugin enables only EditorToolset, AutomationTestToolset, SlateInspectorToolset,
NiagaraToolsets and PhysicsToolsets alongside the MCP server/registry. With this
configuration the UE 5.8 build and map-generation commandlet pass. Existing local
MCP auto-start and Codex HTTP configuration remain on port 8000.

Python discovery passes 121 tests, including recipe validation and atomic-write
failure checks. Private MCP/QA evidence is in artifacts/unreal-mcp-authoring.
The first rendered lifecycle pass checked 19 invariants successfully, but one
screenshot was scheduled in the same tick as reload and captured an empty world;
the final fixture separates these frames. The first-pass evidence is retained.
See the final verification.json for the current rendered result. No model
accuracy claim or adapter change accompanies this editor infrastructure work.

Final authoring validation: 21 rendered lifecycle checks pass, including
bookmark position/rotation roundtrip, saved-recipe precedence on reload, and
PIE exclusion/restoration. Fixed Unreal Rotator positional-argument axis ordering
by using named pitch/yaw/roll; retained the failed screenshots privately. All
three final pose screenshots were inspected and shown in chat. Live MCP returned
the source guide, persisted/restored a pose, rejected an invalid pose, and
validated the restored AwaitingPanel preview after Play/Stop. The recipe is
restored to AwaitingPanel with null bookmark. The editor remains open for MCP,
with Play stopped. No commit/push was requested for this authoring work.
## September 15: optional industrial finishes

- Reuse private Isaac DigitalTwin Rubber_Black_Rough_A / Metal_Rough_A maps using scripts/assets/prepare_industrial_materials.py and setup_industrial_materials.py. Unity standard Asset Store AI-input/training restrictions ruled out the researched free packs.
- Native materials add observer-visible microtexture to existing hoses, rubber pads/feed tube and metal tool hardware. Post-process UserFlags bit 8 is reserved for this finish; clear it on every scene capture. Preserve exact legacy parameters when off, unchanged thermal profiles and painted purchased assets. No extra geometry/tick work; 1024 texture cap. See docs/industrial_finishes.md.

Final finish validation: UE 5.8 build, 121 Python tests, 2,012 graphics checks and
all nine welding verifiers passed. Fixed/R4 NIR and thermal baseline PNG hashes
match the prior dim-light fixture exactly. Hose/gripper before/after screenshots
were inspected and shown in chat. Evidence is in artifacts/industrial-finish-graphics-v1,
industrial-finish-legacy-v1 and industrial-finish-closeups. No new model score or
GPU timing is claimed. Editor and demo were closed after these checks; the
previous authoring entry's open-Editor status is historical.

User clarified that runtime builds are compiled/run only on their computers;
only tutorial/code/configuration are intended for publication. Do not blanket-ban
Unity-compatible content: separately licensed assets or explicit author permission
remain candidates. The researched standard-license packs were not imported;
private/non-commercial use alone does not remove their AI-input/training clause.

Documentation now has a current overview in docs/progress.md and a current
validation table before historical results. Preserve the distinction between
pre-frame development accuracy and later frame/Editor integration checks.

## Approved enhancement backlog — 15 September 2026

User asked to retain all ten suggestions and start items 1, 3, 6, 7 and 8:

1. Fault-and-recovery sequences: visible faults, model holds, corrective actions
   and fresh inspection before release. Never bypass model acceptance.
2. Pre-placement sealant coverage inspection for continuity, width and position,
   with targeted second application (future model-stage/contract work).
3. Smooth robot acceleration, coordinated wrist motion and gentle final approach;
   preserve settled inspection poses and add believable carrying/settling motion.
4. Better hose assemblies: bundled cables, ribbed sleeves, clamps, strain relief
   and bounded secondary movement.
5. Physical panel seating: pad compression, initial tilt, progressive contact,
   subtle squeeze-out and visually inspectable uneven-gap faults.
6. Authentic pinch-weld squeeze/current/hold/release, localized discoloration and
   an optional dramatic spark setting.
7. Factory building: roof trusses, skylights, utilities, distant production and
   loading context; start by searching the previous Isaac Sim assets. Favor
   instancing and lightweight distant geometry.
8. Surface-material pass: brushed sheet metal, stamping marks, wear at actual
   fixture contacts, restrained floor grime and weld discoloration.
9. Thermal reflections and material-dependent apparent temperature (future,
   explicit sensor contract/revalidation work).
10. Inspection replay/presentation mode showing real images, classification,
    confidence, latency and resulting action; annotations remain observer-only.

User reports Blender 5.2.2 installed. Verify the actual executable/version and
use its headless Python API for asset preparation where useful; check whether a
Blender MCP connection exists rather than assuming one is installed.

First implementation pass: Blender 5.2.2 LTS is verified at
C:/Program Files/Blender Foundation/Blender 5.2/blender.exe; background bpy works
and was used to convert local Isaac rack USDs. No Blender MCP tool is connected.
Blender is launched per job and does not need to stay open. The user explicitly
asked this during implementation; continue with the headless API.

See docs/enhancement_roadmap.md for opt-in -Enhancements and -FaultRecovery.
Five observer-only context ISM batches add hall walls/trusses, open roof strips,
an extended Isaac-textured floor and converted racks. No collision, lighting,
GI/reflection or sensor geometry contribution is intended; material opacity uses
observer UserFlags bit 8. Process motion optionally uses quintic travel easing
with unchanged waypoints/end times and physical R4 poses. Pinch current/hold
timings stay fixed; -DramaticWelding restores the existing expulsion intensity.
Metal finish adds derivative-filtered directional grain.

The first recovery is slow sealant cooling, not panel reseating: every third
body starting at one has 8x passive cooling tau. Two fresh valid sealant R
batches activate a local cooling proxy. A/errors/pause break rejection consensus;
only existing two-fresh-G policy advances. Source fault state is never controller
or model input. Cooling uses accumulated simulation time; it is an illustrative
surface cooling envelope, not CFD/cure certification. Offline captures exclude
the fault experiment. No new adapter or accuracy claim is implied.

Final enhancement validation: UE 5.8 build, 121 Python tests and 100 native
policy checks pass. Enhanced PanelMotionSmoke passes, including 3,271 pinch and
56 mounting-frame checks. Graphics passes 2,012 checks; all four fixed/R4 sensor
baseline hashes match the previous material-only revision exactly. Realism
passes 681 checks, and all nine legacy welding verifiers pass. The initial
realism verifier only lacked its expected runtime.log filename; its failed
report is retained and the runner now copies the actual engine log to that name.

Final actual-host recovery integration completes three bodies with 28 requests
and all six thermal audits; body one activates cooling following two model R
batches and waits for fresh G consensus. R4 telemetry passes 5,442 checks. No new
held-out accuracy, adapter training or GPU timing is claimed. Private evidence
is artifacts/enhancements-process-final, enhancements-motion-v1,
enhancements-graphics-v1, enhancements-realism-v1 and enhancements-legacy-v1.
The hall has five ISM batches/63 instances; screenshots in
artifacts/factory-enhancements-closeups were reviewed and shown in chat.
Blender, Editor and demo are stopped; the existing model service is unchanged.
Use run.ps1 -Scene welding -Process -Enhancements -FaultRecovery -ProcessObserverMode 3
to opt into this first pass. All ten ideas remain in the backlog; do not treat
items 2/4/5/9/10 or full industrial robot dynamics as implemented.

## Second enhancement pass — 16 September 2026

The user asked to continue the remaining list. Items 2/4/5/9/10 now have the
implementations described in docs/enhancement_second_pass.md, superseding the
first-pass status above. They remain opt-in with -Enhancements.

- Twenty spline sections use a Blender-generated corrugated sleeve; 24 collars
  share adjacent hose thermal profiles. Authored process-clock sway preserves
  anchors. Pads compress from 3.8 to 3 cm and broaden under load. The final
  lowering uses 51 fitted R3 poses and a 0.6-degree tilt; prepare_panel_lowering.py
  writes private panel_lowering.json, tied to panel authoring provenance, without
  changing settled model poses. An initial direct wrist offset failed the
  lateral-placement audit and was replaced; do not restore it or loosen limits.
- F5 toggles actual-request replay, Page Up/Down browse eight retained requests.
  Twenty-four GC-owned decoded textures are the bounded history. Store actual
  request identity, scores/label mass, latency and resulting controller action;
  never substitute mutable live targets or synthetic rationale. Cancelled or
  mismatched requests cannot appear accepted. Live GPU preview behavior remains.
- Observer-only thermal reflections use up to 24 screen-depth steps / 3 m and
  one reflection bounce, stopping on opaque depth. Row15 alpha carries emissivity
  without changing LUT size; an 11 um Planck proxy mixes reflected radiance.
  All fixed/model materials explicitly disable it. -NoThermalReflections disables
  observer reflections. This is a screen-space display approximation, not
  calibrated 8–14 um reflection radiometry. Keep ThermalSurfaceEvaluation.ush
  shared between direct and reflected visible-surface evaluation.
- Separate offline automotive-sealant-coverage-study-v1 contract/capture/evaluator.
  -ProcessCapture -SealantCoverageCapture creates eight physical fault cases per
  whole-body episode. The production loader rejects this contract. Four episodes
  produced 32 triplets / 96 PNGs; only 16 training-development triplets were scored
  by the base model: median 60.0 ms, one source-label agreement, seven false G
  against provisional source labels. No new LoRA trained and no live coverage
  gate/reapplication enabled. Improve image observability and review labels
  before training; validation/test episodes remain uninspected by the model.

Additional bundled wiring, contact physics/squeeze-out and subtle new gap faults
remain future work. Do not describe this pass as implementing those items fully.

Final second-pass validation: UE 5.8 build, 124 Python tests, 104 native policy
checks, 2,012 graphics isolation checks, corrected rendered panel motion,
56 frame checks and 3,271 pinch checks passed. Thermal/radiometry/fields/transfer
passed; the isolated reflection fixture had 2,344 reflected receiver pixels,
zero reflected warmth from a hidden source and zero receiver variation at e=1.
All nine legacy welding suites passed. Final actual-host run completed
assembly/preassembled/assembly with 27 requests and six thermal acceptance
checks; camera 5,248 and curtains 27 checks passed. Eight unique completed replay
receipts each retained three images. These are development integration results,
not new held-out accuracy. Evidence is under artifacts/enhancement-rest-*-final,
enhancement-rest-motion-corrected, thermal-reflection-fixture-final,
equipment-details-closeups and sealant-coverage-study-v1. Screenshots of equipment,
placement, reflection A/B and actual replay were shown in chat. Unreal/Blender
are stopped; the existing model service remains unchanged. Changes are not committed.

## Replay key conflict fix — 16 September 2026

F5 inherited Unreal's `viewmode shadercomplexity` debug binding, which consumed
input before the pawn replay action. DefaultInput.ini now subtracts that binding
and the inherited Page Up/Down debug-target commands. Keep F5 for replay and
Space / Xbox View for simulation pause; these are independent. Restart to load
input configuration; F3 restores lit rendering in an older running instance.
No binary or content regeneration is needed for this configuration-only fix.
A separate rendered no-inference game instance received actual window key events:
F5 toggled replay to live, Space paused, F5 toggled replay while still paused,
and F5 then Space returned to live/resumed with normal lit rendering throughout.
Six private screenshots and the input harness are in artifacts/replay-key-fix;
this is input/rendering validation, not model accuracy. Test instance was closed.
README and enhancement guide now distinguish replay from pause explicitly.

## Model service recovery — 16 September 2026

The repo-specific WSL service had been launched without model arguments:
health was ready but adapter was null and vision_patch_embedding was stock-conv3d.
Windows and WSL both reached port 18191; Unreal repeatedly timed out and reported
connection unavailable. Stopped only dark-factory-panel-model.service and
restarted with shared960 primary, shared896 weld and --linear-patch-embedding.
Health now reports both selected adapters and linear-nonoverlap-v1 on RTX 5090.
The rendered enhanced recovery integration passed assembly/preassembled/assembly,
all three departures and six thermal audits with no timeout/connection errors.
Recorded decision-event latency median was 82 ms (15 samples, range 77–94 ms).
Private evidence: artifacts/model-service-recovery-20260916. This is service and
integration validation, not a new accuracy benchmark. Model service remains
active with zero restarts; the automated Unreal instance exited. README now
requires checking adapter identities and projection mode, not just ready status.

## Softer roof lighting and equipment heat — 16 September 2026

RoofOutputScale is now .6 (imported banks 1,200 lm, procedural 480 lm), one third
below the previous .9 output. Tube emission scales with it. Bright observer
Lumen exposure is -1.2 EV instead of -.7; dim and all infrared exposures remain
unchanged. Update the graphics verifier's expected bright EV accordingly.

Process-mode equipment heat uses prepare_equipment_heat.py and private
assets/derived/welding_cell/equipment_heat.json. Four source motors are already
owned by SM_CarriageHolder_Front/Rear, NOT SM_Background_robots_blue; convert fitted
source points to holder-local pivots so heat moves with the motors. Five existing
profiles (two holders, cabinet gray/black surfaces, LCD) use bounded existing GPU
capsule fields; no new meshes, profiles, shader variants or LUT rows. Motor duty
comes from actual carriage speed (500 cm/s reference), approaching 62 C with
2.8 s response during movement and cooling toward 20 C with 24 s response when
stopped. Start warm at26 C; pause and equal-clock seeks add no heat, residual heat
persists between bodies, clock rewind resets. Cabinet exterior starts34 C and
approaches38 C, up to44 C with carriage duty, tau18 s; LCD warmth is lower. These
are illustrative external surface temperatures, no bulk solver or internal view.
Legacy cycle/QA temperatures remain unchanged. Missing optional descriptor warns.

The EquipmentHeat mode in verify_enhancements.ps1 checks rendered closeups and
heat/pause/cooling/NIR parity. Its frozen fixture must publish the travelled
carriage transform to the LUT before returning to the reference pose in the same
frame, and prime NIR over actual rendered frames. Earlier v1-v3 diagnostics are
retained privately; v4 passes with26 ->49.67 ->22.44 C motor core, stable paused
heat and identical fixed-NIR PNGs. Cabinets reach37.91 C. Guides:
docs/equipment_heat.md, docs/factory_lighting.md, docs/observer_graphics.md.

Final equipment/lighting validation: UE 5.8 build and 124 Python tests pass;
equipment-heat-v4 passes heat/pause/cooling and exact NIR parity; all 2,012 graphics
isolation checks and nine legacy welding suites pass. Actual host integration
in artifacts/equipment-heat-process-v1 completes assembly/preassembled/assembly,
three departures and all six thermal audits. No ground truth sent to the model.
No new model accuracy or GPU timing claim. Motor and cabinet thermal closeups
were reviewed and shown in chat. Automated Unreal processes are closed; model
service remains running. No commit or push was requested.

## Spark shape, EVK blinking, camera support and dark startup — 16 September 2026

Sparks now share a startup-built 48-triangle rounded mesh, stretched along velocity
for tapered streaks and round at rest. Opaque GPU instances retain the same bounded
pool, collision state, cooling, emissive fades and thermal depth; no material or
shader change and no translucent sorting. Roof tubes keep their own geometry.
EVK amber activity remains irregular; both green LEDs now blink (heartbeat and
double pulse), staggered by robot. Use simulation clock and pinned QA time, keep
stable positive-intensity light proxies with color/black changes; decorative only.
The process inspection camera has a 4 cm rod plus two collars joining the wrist
origin to the existing housing. No optical origin, mount or joint changes. The
three non-colliding supports share the existing uniform housing thermal profile.
Interactive launcher and Editor Play default to dark observer mode with headlamp
off; Tab/Y cycle stays dark -> NIR -> thermal -> roof-lit -> dark. L opts into the
headlamp in dark mode. Explicit -ProcessObserverMode overrides remain supported;
legacy welding QA still explicitly enables the headlamp. Editor preview lighting
is unchanged. These startup requirements supersede earlier bright-Play defaults.

Validation for the visual refinement: UE 5.8 build and 124 Python tests pass;
rendered motion passes all 63 checkpoints, with 3,271 pinch and 56 frame checks.
All nine legacy suites pass in artifacts/visual-refinement-legacy-final, including
new green LED animation checks and the two rendered pulse closeups. The graphics
fixture passes 2,012 checks; frozen R4 NIR/thermal PNGs are byte-identical to the
prior equipment-heat fixture at its reference inspection pose. The fixed overview
images may change because they see the added support and blinking LEDs; observer
mode/graphics isolation still passes. Actual key-event screenshots verify dark
startup/headlamp off and Tab order 0/1/2/3/0, then explicit L activation.
Actual host integration in artifacts/visual-refinement-process-v1 passes three
assembly/preassembled/assembly departures, all six thermal audits and 5,248 camera
checks, with no ground truth sent. No new held-out accuracy claim. Actual spark,
LED, camera and startup renders were shown in chat. All Unreal instances are
closed; the compatible model service remains running. No commit/push requested.

## Live EVK LED clock correction — 16 September 2026

User observed that EVK LEDs did not actually blink in panel process mode.
GetMotionTime() is the legacy pose coordinate, held constant during many process
phases, while GetEffectTime() selects continuous ProcessClock. IQ9 UpdateLeds now
uses GetEffectTime(), preserving pinned QA, legacy timing and explicit pause.
The previous pinned green/amber screenshots missed live clock progression.
New IQ9Live fixture runs actual controller/prop ticks in a no-inference inspection
hold, checks every LED toggles in both running intervals and remains unchanged
while paused, then resumes. No manually set time or LED override is allowed.
Private iq9-live-clock-v1 passes 481 samples and 119 state transitions with
constant robot pose and natural process time; live green/amber screenshots saved.

Live LED correction validation: UE 5.8 build, 124 Python tests, actual IQ9Live
clock/pause/resume fixture, all nine legacy welding suites and all 2,012 graphics
isolation checks pass. Evidence: artifacts/iq9-live-clock-{v1,legacy-v1,graphics-v1}.
The live pulse screenshots were reviewed and shown in chat. Unreal test instances
are closed; model service unchanged. Restart the demo to load the rebuilt DLL.

The user subsequently requested stopping the model host. Stopped only
dark-factory-panel-model.service; verified inactive/dead and no listener on
port 18191. Leave it stopped until model execution is requested again.
