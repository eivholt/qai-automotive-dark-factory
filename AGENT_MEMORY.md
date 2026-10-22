# Repository agent memory

User direction first recorded on 2026-09-11 and updated on 2026-09-18. Read this alongside AGENTS.md. The current panel-assembly direction below supersedes the earlier door-fault process. Historical implementation entries retain their original validation scope; they do not imply current two-bead panel-v3 model accuracy.

Latest implementation: three-edge sealant beneath a panel raised 5 mm, conducted panel-surface heat, and explicit shared960 primary/shared896 weld routing. The final entry and docs/model_classification.md supersede earlier adapter and geometry status.

## Demonstration objective

Tutorial authoring preference (24 September 2026): the user writes
docs/unreal_dark_factory_tutorial.md by hand. Do not edit that file unless the
user explicitly changes this instruction. Keep assistant topic suggestions in
docs/unreal_dark_factory_tutorial_topics.md; use bullet-level outlines until
the user requests expansion.

Build an Unreal Engine factory simulation demonstrating sensor cameras feeding NVIDIA Cosmos-Reason2-2B to detect anomalies. The simulation should react to model results. Begin with several sensing modalities and use experiments to refine the concrete manufacturing scenarios.

September 23 user priority: the central purpose is to visibly demonstrate an
LLM agent choosing tools to sense and act, including VLM-backed inspection and
simulated actuator jobs. Reducing the complexity of the agentic task is expressly
acceptable. Prioritize a clear, repeatable agent demonstration over maximizing
the number of planning branches, independent subjobs or recovery permutations.
Keep actual LLM tool choices and VLM observations; present the deterministic
work inside each tool honestly. No need to preserve the current thirteen-choice
tool menu as a product requirement. This permission changes future experiment
scope, not historical validation results or the current runtime by itself.

September 23 implementation: the user selected canonical named history as the
default despite known planning failures. `configs/live_agent.json` is now v2 with
eight globally available tools: inspect_panel, prepare_panel, inspect_temperatures,
weld_panel, inspect_seating, finish_body, wait (5 s), hold. Prepare bundles sealant
+ placement/hold; temperatures always obtains actual adhesive and weld results;
finish retracts only an engaged holder before departure. No acceptance, retry,
cooling or branch decisions are hidden in these macros. Preserve partial steps,
cancellation, actual result validity, and source-truth-independent execution.
The old contract is configs/live_agent_legacy.json; -AgentProfile legacy requires
a matching proxy config. Current named-jobs-v4 removes model-visible IDs/times,
canonically orders nested fields, and records actual completed_jobs/steps.
Prompts and sensor contracts remain separate; Reason2 prompts are unchanged.
See docs/agent_demo_scope.md and docs/current_agent_prompts.md. The earlier
44/46 policy result belongs to the old menu; the new 42-case development probe
must retain all failures, including unnecessary waits and invalidation mistakes.

## Production-line sequence and inspection gates

Use 3D assets to depict a section of a production line. Sensor-camera observations and the model's inspection results should act as a gate for subsequent production steps: the simulation advances only when the inspection outcome permits it, and otherwise holds or follows the defined anomaly response.

On 2026-09-13 the user replaced the door-fault task with inner trunk panel assembly. A new body arrives with its panel already installed one time in five. Four camera-only Reason2 stages now use automotive-panel-process-v3: panel_presence (NIR), adhesive (thermal), panel_seated (NIR), weld (thermal). All use three actual R4 camera views in spot1/spot2/spot3 order. The model chooses the presence branch from one fresh three-camera G/R batch; the generator flag must never choose it in live control.

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

## Agent-controlled process study — 16 September 2026

User wants to explore a local LLM choosing process jobs with Reason2 as inspection
tools, following or-edge-agent/IQ9_EVK.md. Begin on the host GPU, then evaluate
IQ9 co-residency. Explicit preference: keep commands at complete-job level
(inspect sealant, place panel, start welding). Unreal owns arm poses, tool
opening/clamping, current timing, physical camera scans and motion completion.
The broker/executor retain freshness, consensus and mechanical interlocks; the
LLM selects jobs rather than generating low-level actuator commands.

New proposed states: the permanent open mounting frame may be absent, requiring
placement and welding without sealant before panel assembly. Sealant can have
gaps/under-dispensing; a binary continuous/not-continuous/A inspection may request
another pass over the SAME full three-edge path, with no region localization.
Initial study limits extra passes to two then review; live probabilities are not
selected. Inspect panel presence first. Preassembled opaque panels skip hidden
frame/coverage checks and retain both existing thermal gates. For missing panels,
check/install/seat-verify/cool/release the frame, apply sealant, verify continuity,
then place the panel and retain the usual thermal/welding/seating/cooling sequence.
Continuity is separate from thermal readiness; reapplication needs updated heat.
New geometry/views/data/adapters and actual integration remain future work.
The prior coverage feasibility experiment failed; do not claim a trained coverage
adapter or send new stages to the current automotive-panel-process-v3 endpoint.

Added scripts/agent/start_host.sh and experiment.py, isolated offline mock jobs
with a real local controller LLM. Base and frame-sealant scenario sets never
connect to Unreal or send fixture labels/expected outcomes to the model.
Qwen3.5-9B Q4_K_M is the leading candidate: matched initial comparison gives
14/16 clean development replays versus Qwen3.5-4B's 10/16, median HTTP decision
372 versus224 ms. Two repeats of eight cases are not independent held-out trials.
The 9B executor blocked two stale-evidence inspection retries; 4B made66 rejected
requests and looped on preassembled cases. The expanded study's compact tool
interface completes16/16 expected outcomes with12/16 clean and6 rejected requests.
These are orchestration experiments with synthetic replies, not Reason2 accuracy.
138 Python tests pass; no production Unreal/controller code changed.

Read-only EVK measurement:34.37 GiB RAM,9.67 GiB available,23.31 GiB unreclaimable
slab; cause unknown and services unchanged. Estimated dedicated-device budget
is15.3–18.3 GiB for Qwen9B Q4 plus two Reason2 Q4-decoder/FP16-vision bundles,
contexts, compute buffers and OS reserve; current free memory is insufficient.
Two multimodal rank8 adapters are47,789,640 bytes each (11,927,552 FP32 params),
but serving currently merges them into two full models. Do not assume decoder-only
LoRA switching preserves vision adaptations. HTP capability/performance and
automotive quantization parity are unverified; resident models can infer serially.

Host co-residency succeeded with Qwen9B and both selected BF16 Reason2 copies:
17.65 GiB total sampled GPU usage including desktop, without Unreal. Reason2
reports8,521,694,208 allocated CUDA bytes.24 existing private-image transport probes
had119.5 ms median while controller work overlapped; not new accuracy/fresh gates.
Private model downloads are in WSL ~/models/dark-factory-agent, traces under
artifacts/agent-research-20260916. Temporary controller and Reason2 hosts were
stopped after testing; leave stopped until execution is requested. No EVK service
was modified. Research, budgets, limitations and commands are documented in
docs/agentic_process_experiment.md and linked from README.

## Consensus experiment — 18 September 2026

User explicitly requested testing one high-confidence batch versus the existing
two-batch rule. Added opt-in launcher -ProcessConsensusBatches 1 (native
-DFProcessConsensusBatches=1), default 2. This approved controller experiment
supersedes the mandatory-two rule only when explicitly selected. It does not
change the frozen model contract, prompts, .98/.5 thresholds, fresh three-view
capture, stage routing or thermal bands. Reports distinguish execution_policy
from model decision_policy; offline capture cannot use the override. A/errors/
staleness/pause still hold. Preserve default two pending a separate decision.
See docs/consensus_experiment.md for actual comparison scope and results.

Four actual host ProcessSmoke runs (order 2/1/1/2, seed42, Enhancements, bright
observer, same adapters) passed all12 departures and24 thermal acceptances,
including strict42/52 C clear-ready boundaries. Mean three-body process time:
one136.08 s vs two162.32 s (16.16% reduction); requests15.5 vs23 (32.61% fewer).
HTTP medians remain82 ms. One-batch hot/uncertain replies still hold; earlier
inspection means necessary cooling rescans replace some saved confirmations.
This repeats three deterministic initial states, not12 independent accuracy
trials. No new held-out score or rare-error bound. Default remains2.
UE build,138 Python tests,41 launcher checks,118 native policy checks and all
four camera audits (14,978 checks) passed. Evidence stays private under
artifacts/consensus-ab-20260918; comparison.json records per-run metrics.
Reviewed two actual HUD captures and showed them in chat. All Unreal instances
and the repo model service were stopped after testing; leave stopped until
execution is requested again. No commit or push requested for this experiment.

## Qwen9B and actual IQ9 inference — 18 September 2026

User requested continuing Qwen3.5-9B Q4_K_M agent tests on host then EVK, followed
by running both selected Reason2 adapters on the EVK. The agent still chooses
complete jobs through synthetic-tool experiments; it does not yet control the
live Unreal sequence. Explicit wait/exception observations and complete-path
prompt clarifications yielded 16/16 clean host base runs and 16/16 expanded runs,
with median decisions 305/343 ms. Two actual IQ9 CPU cases (missing/preassembled)
passed 13 tool decisions without rejection, at 14.84 s median / 49.97 s first
request. The third case was interrupted to free memory and is not a result.

EVK is currently ubuntu@192.168.1.158 through WSL SSH; saved iq9075-evk alias is
stale and was not changed. New files/services are isolated under
~/qai-dark-factory. Existing qai-conveyor-geniex.service remains unchanged/active.
Native llama.cpp 0eadefebd3f8f92a86d634a0e5b8fffc9dc792c0 uses matching GenieX
v0.6.1 Hexagon libraries through a small static-registration loader. Keep native
ggml libraries first in LD_LIBRARY_PATH. Exact runtime/export hashes, commands
and limitations are in docs/evk_model_runtime.md.

Both multimodal LoRAs were merged/exported with their matching adapted F16 vision
encoders. Never swap only decoder LoRAs or mix the projectors. Q4_0 failed the
existing 96-batch development cohort with seven false G (four presence, two
sealant, one weld). Q8_0 and BF16 scored 96/96 on host; Q8_0 also scored 96/96 on
EVK with both adapters resident, no transport failures and 5.52 s median using
six CPU vision threads. This cohort predates the permanent frame; no new held-out
accuracy is claimed. Final eight-thread CPU vision probes were 4.66 s median.
Decoder logs confirm 29/29 layers assigned to HTP0 with some CPU operations.
Vision-on-HTP aborted on its first image request; retain CPU vision. Qwen Q4_K_M
is CPU-only on this prepared EVK; no GPU driver/permission changes were made.

Always pass --cache-ram 0 for these Reason2 backends, alongside no-cache-prompt.
The default 8 GiB RAM prompt cache caused an initial two-backend run to exhaust
memory after 31 successful classifications. The cache-limited full rerun passed.
The EVK has 34.37 GiB RAM but about 24.8 GiB in unreclaimable skbuff kernel slabs;
only 8.2 GiB was initially available. Both Reason2 services now leave about
1.8–2.0 GiB available. A Qwen-plus-two-Reason2 run remains pending REBOOT APPROVAL:
the user was asked because reboot interrupts the existing conveyor service; no
reply or reboot occurred. Do not infer approval from elapsed time. The launcher
requires 18 GiB available for the combined stack; exact combined residency is
still unmeasured. All three weight files/bundles together total 10.23 GiB before
runtime buffers. Do not push the degraded device into another OOM.

New serve_edge_reason2.py routes primary960 to presence/sealant/seating and
weld896 to weld, with verified paired weight identities. It returns explicit
source evk_hexagon. Unreal requires -ProcessModelSource evk_hexagon and the
loopback tunnel endpoint; host_gpu remains default. Preserve all existing
prompt/image contracts, .98/.5, default two batches and 8 s freshness. Only the
overall EVK model-QA timeout is 480 s instead of 240 s, not a gate override.

Actual EVK ProcessSmoke passed assembly/preassembled/assembly: 3 departures in
272.97 s, 23/23 fresh valid replies, 4.85 s median request time, all 6 thermal
acceptances at 20.00–25.04 C, and an uncertain sealant hold followed by reinspection.
4,472 camera checks, 118 rendered native policy checks, 44 launcher checks,
144 Python tests and UE 5.8 build pass. Evidence: artifacts/agent-evk-20260918.
Reviewed actual HUD screenshot and displayed it in chat. Unreal and temporary
host GPU services are stopped. New EVK user services dark-factory-edge-primary,
dark-factory-edge-weld and dark-factory-edge-router remain running. Qwen is
unloaded. WSL dark-factory-edge-tunnel maintains loopback 18198; launch demo with
-Process -Enhancements -ProcessEndpoint http://127.0.0.1:18198/v1/inspect
-ProcessModelSource evk_hexagon. No commit/push was requested for this work.

## Fixed single-batch gates and EVK reboot authorization — 18 September 2026

User now authorizes EVK reboots at will, says conveyor demo models are not needed,
and requests hardcoded one-batch behavior without ProcessConsensusBatches. This
supersedes the earlier pending approval and default-two notes. Disabled/stopped
qai-conveyor-geniex.service; its deployment and model files remain intact.
Reboot reclaimed the approximately 24.8 GiB unreclaimable kernel slab: available
RAM increased from 1.81 GiB with Reason2 loaded to 33.27 GiB before stack startup.

All process gates now use one complete fresh three-camera batch. Presence G/R
branches immediately; other gates accept one fresh G. Removed launcher/native
consensus switches; decision_policy.consecutive_green and runtime required count
both report 1. Preserve .98/.5, 8 s freshness, A/error/stale holds, thermal R waits,
seating R exceptions, physical scans and adapter stage routing. Prompts, hashes,
image processing and training provenance are unchanged; no retraining is required.
Restart existing model servers after updating code/config so reply policy matches.
The historical A/B comparison remains in docs/consensus_experiment.md.

Both adapted Reason2 bundles and Qwen9B now load together on the rebooted EVK.
The Qwen unit uses a bounded 9 GiB MemoryMax; its 7 GiB initial cap was too close
to observed startup allocation. Keep CPU Qwen, HTP decoder/CPU vision Reason2,
RAM prompt caching disabled, and serial model execution. Final measurements and
verification are recorded below when testing finishes.


Fixed-policy follow-up: UE 5.8 build, 143 Python tests, 42 launcher checks and
115 rendered native policy checks pass. Actual host ProcessSmoke passed three
correct departures in 137.89 s, 16/16 valid replies at 82.49 ms median, all six
clear-ready thermal audits and 3,114 physical-camera checks. Its captured welding
image was reviewed and shown in chat. The temporary host service is stopped.
Updated Qwen9B host tests pass 16/16 standard runs (106 decisions, 386.55 ms median)
and 16/16 expanded runs (166 decisions, 371.85 ms median). On the rebooted EVK,
all eight standard cases pass: 53 decisions, zero rejected calls, 12.06 s median,
53.56 s first request, 707.63 s total model time, with both Reason2 bundles loaded.
Agent tools remain synthetic; Qwen is not driving live Unreal. The future live
broker must handle slow agent decisions without extending evidence freshness.


Final co-resident EVK ProcessSmoke passes assembly/preassembled/assembly in
181.50 s: 12/12 valid fresh replies, 4.868 s median / 5.016 s maximum, all six
clear-ready thermal acceptances at 20.10–35.15 C, one conservative uncertain
sealant hold followed by a fresh pass, and 2,338 physical-camera audit checks.
Qwen remained loaded but idle during real Reason2 inference. Across 74 samples,
available memory never fell below 21.49 GiB; unreclaimable slab peaked at only
146.34 MiB. No current-boot OOM was found. Qwen cgroup peak was 7.355 GiB;
primary/weld 1.630/1.570 GiB, excluding some shared/DSP allocations.
This is co-residency/integration evidence, not new held-out accuracy or live
Qwen control. Private summary and all evidence: artifacts/evk-one-batch-20260918.

Reviewed and showed the actual EVK welding screenshot in chat. Unreal and both
temporary host GPU model services are stopped. The test-only 18195 agent SSH
tunnel and memory sampler are stopped. All four EVK user services (primary,
weld, router, agent) remain active; the WSL 18198 router tunnel remains active.
Run with -Process -Enhancements -ProcessEndpoint http://127.0.0.1:18198/v1/inspect
-ProcessModelSource evk_hexagon. No consensus switch. Reproduction/limits are in
docs/evk_model_runtime.md. No commit or push was requested for this work.


## Live agent controller — 18 September 2026

User approved replacing recipe vetoes with a true optional agent mode. This
supersedes the earlier mock-only status for explicit `-ProcessControl agent`.
Flow remains the default selectable comparison. Qwen now calls the actual R4
Reason2 scans and complete mechanical jobs in Unreal. No hidden recipe advances,
automatic reinspection, automatic cooling, branch/permit/next-step flags or source
truth in Qwen observations. Acceptance criteria belong in configs/live_agent.json.
An independent source-aware evaluator records process errors without vetoing or
repairing choices. Syntax, request/body/pause epochs, duplicates, exclusive jobs,
unsupported second pickups and carriage/engaged-holder conflicts stay mechanical
executor checks. Model prompts and .98/.5/one-batch/8-second reply TTL are unchanged.
A historical valid inspection can be older than eight seconds when Qwen acts;
record its age/intervening jobs, never claim it remains a fresh sensor image.

New native ProcessAgent/AgentProtocol code keeps a real assistant/tool conversation
per body. Initial flattened-history prompting looped on inspections; preserving
conversation fixed placement. An early live version welded before the sealant
inspection: it EXECUTED and was flagged by the non-vetoing audit. Explicit
acceptance criteria fixed that development case without adding a code permit.
The final host criteria run passed three departures (missing/preassembled/missing),
29 Qwen decisions, 16 real Reason2 batches, zero rejected calls/audit failures,
212.50 seconds wall time. Qwen median 552.23 ms, max 753.87 ms; Reason2 median
82.23 ms, max 94.12 ms. R4 camera audit: 3,114 checks. This is a development run,
not held-out accuracy; it followed prompt refinement on this same scenario.
Evidence is artifacts/agent-live-20260918/host-criteria. Reviewed actual welding
screenshot and showed it in chat. The agent sometimes performs redundant cooling
and seating inspections; it does not exactly reproduce the flow controller.

UE 5.8 builds; native policy passes 135 checks (20 agent-specific) including actual
execution of a bad recipe, no automatic advancement, scope rejection and canceled
inspection. Python 143 tests and launcher 52 checks pass. Agent mode physical
weld attachment follows completed welding, never an inspection phase. Idle keeps
the holder engaged until release; repeated deposition jobs restart surface heat.
HUD identifies live Qwen, job/reason/timing/audit failures, and actual G/R/A meanings.

EVK combined stack remains resident. First actual agent attempt hit UE's separate
30 s HTTP activity timeout during CPU prefill; added 120 s activity + total limits
ONLY for Qwen. Reason2 freshness remains 8 s. A rerun is in progress; do not claim
EVK live-agent success until its report passes. WSL start_evk_tunnel.sh --agent
now maintains separate router18198 and Qwen18195 user tunnels. Current guide:
docs/live_agent_control.md. Do not leave Unreal running after verification.


Final EVK live-agent rerun succeeded: 3/3 correct departures in 648.77 s, 25 actual
Qwen choices, 14 real Reason2 batches, zero rejected calls/audit failures. Median
Qwen 15.369 s (max/first 52.678 s after a canceled prefill, not clean cold start);
Reason2 median 4.847 s/max 4.986 s. Physical camera audit 2,726 checks. Three memory
samples retained >=21.49 GiB available, no service restarts observed. Qwen remained
CPU-only, both adapted Reason2 decoders on HTP with CPU vision. Longer decisions
cooled targets enough that no thermal waits occurred. Both host and EVK agents
performed redundant sealant/seating inspections; efficiency is not optimal.
This supersedes the pending-edge paragraph above. Reviewed/showed actual EVK
welding screenshot in chat. Evidence: artifacts/agent-live-20260918/evk-timeout-fixed
and summary.json. No held-out accuracy claim. Final flow/legacy checks pending.

Architecture: the conversation/transport/tool-execution loop lives in Unreal on
the PC. EVK mode moves Qwen decision inference and both Reason2 backends to the
EVK, not the Unreal executor or a standalone hardware-control daemon.


Final regression/cleanup: the unchanged flow controller passed three bodies in
137.87 simulation seconds with 16 actual model batches, six thermal audits and
3,114 camera checks. All nine legacy WeldingSmoke reports passed. RealismSmoke
and its Python verifier passed 681 checks (the real engine log was copied into
its private fixture directory before verification). The final build includes
bounded agent outcome history, at most 128 bodies; no runtime model logic changed
after the successful edge run. No further rendered run was needed for that bound.
Unreal is closed. Temporary WSL host Qwen and Reason2 services are stopped. All
four EVK services and both persistent WSL loopback tunnels remain active. Branch
is main; changes remain local and uncommitted. No commit/push was requested.

## Single-image acceleration experiment — 19 September 2026

User asked to split Reason2 inference into single-image requests for acceleration.
Implemented offline probe_edge_reason2.py --single-view with three serial calls,
one image/one output token each, separate versioned single-view prompts and
explicit per-view aggregation in gates_single_view.py. Presence requires unanimous
G/R; other stages require all G, any confident R holds/flags; failed/incomplete
views hold. Do not reuse whole-batch labels as individual view labels. These new
prompts/inputs are not covered by the existing three-view adapter provenance.

Actual IQ9 HTP vision failed on the FIRST single-image request in clip_encode
(FastRPC 0x8000040d, dspqueue_read 0x2e). A separate flash-attention-off trial
also failed and reported dummy-warmup memory/operator problems. The original
backend already encodes images sequentially, so HTTP splitting does not resolve
this runtime/kernel failure. Both isolated test units are gone. CPU vision tests
completed 12 calls across four saved stage batches: median 1.767 s per image,
5.356 s per whole inspection versus 4.804 s for the original batch on the same
images. Seating changed from original G to R/R/R in the split path. Other three
stage decisions agreed. This is a small development probe, not held-out accuracy.
Keep live routing/adapter checks unchanged pending working acceleration and
single-view validation/training. Private evidence: artifacts/single-view-20260919;
reproduction in docs/evk_model_runtime.md. No Unreal visual changes.

Verification: 149 Python tests pass, including exact one-image/one-token HTTP
payload and aggregation/failure tests; rendered ProcessPolicySmoke passes all
135 native checks (synthetic decisions, not model accuracy). Same-image baseline
requests after the failed HTP experiments completed on both deployed adapters.
All four EVK services remain active, temporary port 18199 is closed, Unreal is
closed, and generated reports/logs remain ignored. Changes are uncommitted.

## Compiled QAIRT NPU experiment — 19 September 2026 (in progress)

User requests conveyor-like NPU speeds. Added isolated QAIRT conversion,
training-only 120-image vision calibration, hash-bound packaging, a native
GenieX v0.3.17 score worker and offline validation runner. The worker preserves
full-vocabulary label mass and stops after the first token, avoiding unused
decode. Current original three-view prompts need CL1024; single-view prompts
remain an unvalidated separate experiment. Do not change live gate confidence,
sensor sizes, source encoding, prompt hashes or one-batch behavior to gain speed.

Reference conveyor controls: six one-image calls median 504.64 ms; three
three-image calls median 1169.74 ms. These are direct native runtime checks,
not automotive accuracy or HTTP measurements. QNN's final-partition memory-map
warning was traced with verbose logs to a successful retry from DSP domain 0
to domain 2, with all five contexts successfully loaded, no CPU fallback.

Automotive primary/weld conversion and validation are running in WSL user units
dark-factory-qairt-{primary,weld}-{build,validate}. Work is under
/home/eivho/dark-factory-qairt; private evidence is artifacts/qairt-20260919.
The validation units package, stage and serially test each new bundle, without
activating an HTTP service. The adjacent conveyor repository/deployment remains
read-only. Native EVK Reason2 primary/weld/router services were temporarily
stopped for this test; Qwen remains loaded. Restore or explicitly replace the
automotive Reason2 services after testing. Unreal is closed. Python 153 tests
and rendered ProcessPolicySmoke 135 checks passed. No commit/push requested.
See docs/evk_qairt_experiment.md; final automotive results are pending.

First automotive QAIRT primary validation completed: 115/120, zero false G,
median 1442.55 ms / max 1458.07 ms per original three-view batch. Do not deploy:
four cool/present G cases held A and one cool sealant G became R. Single-view
probe: 27 calls, median579.36 ms/image, thermal760.96 ms, aggregate6/9; still
unvalidated prompts with wrong presence/seating branches. Native tokens match
original468/620/484. Input pixel comparison max error1.19e-7 rules out resizing
or normalization. Injecting actual NPU vision features into unchanged HF decoder
reproduced a presence error (G.999996 -> R.99477); unquantized QAI conversion
retained G.999994. Thus the inspected loss is in quantized vision.

Vision weights are actually INT8, activations INT16; upstream generic w4 names
were misleading. First primary private provenance precision string predates
that inspection; tensor encodings and docs are authoritative. New packaging
records W8A16. Primary encoder final-block36 activations now FLOAT16, recalibrated
on the same120 training images only. Its QuantSim features recover G.999983 in
the diagnosed example. Native compile/validation is still pending in WSL units
dark-factory-qairt-primary-vision-fp16 / primary-lastfp16-validate. Candidate work:
primary/cl1024/vision-last-fp16. Weld baseline validation is running separately.
Full Python suite now155 tests. Native score worker is unchanged; diagnostics
use separate qairt_vision_diagnostic.cpp. Experimental HTTP server exists but is
not running and refuses candidates below the existing191/192 development bar.

Compiled mixed-precision follow-up: the initial link failed on shared scalar
INT/FLOAT constants. split_qairt_vision_constants.py makes six mathematically
identical copies; both revised graphs compiled/linked successfully. Actual IQ9
validation still115/120 primary and71/72 weld, same conservative errors, medians
1425.03/1448.92ms. Native final features still differ from QuantSim. Pixel and
vision-RoPE inputs match reference within1.2e-7/6e-8. Inspected compiler bias
warnings are tiny rounding overflows and do not explain the classification loss.
Both full-FP16 vision alternatives are now linking, retaining original W8-text
contexts. Work roles/cl1024/vision-all-fp16; WSL units dark-factory-qairt-
{primary,weld}-allfp16 and -allfp16-validate. These are still not deployed.
Experimental helper tests7pass; complete prior suite155pass plus new candidate
source-immutability test. Original EVK services still temporarily stopped except
Qwen; restore or replace them after final testing.

## Selected compiled NPU backend — 19 September 2026

This supersedes the in-progress/rejected-candidate state above. Fully FP16
vision recovered primary 119/120 and weld 72/72 on the existing development
cohort: 191/192, no false G or wrong NIR decisions. One cool-sealant G became a
conservative R hold. Median original three-view batches are 1409.49/1410.53 ms.
NIR presence/seating medians are 1228.55/1229.89 ms. All original prompts, image
encoding, .98/.5 thresholds, 8 s freshness and one-batch behavior are unchanged.
This is not new held-out accuracy. Both vision and W8-text/FP16-activation text
run in compiled QAIRT 2.45 contexts on IQ9 HTP, with normal CPU preprocessing.

Selected private bundles: ~/qai-dark-factory/models/qairt-primary and qairt-weld;
each includes exact candidate provenance and validation.json. Native worker is
runtime/qairt_score_worker, with copied pinned v0.3.17 plugin libraries under
runtime/geniex-v0317-qairt245/qairt. No conveyor deployment was modified.
Active EVK service: dark-factory-edge-qairt on loopback18198. Old automotive
primary/weld/router units remain stopped as a retained fallback. Qwen service
and both WSL tunnels stay active. Use start_evk_qairt.sh --agent for the prepared
stack. Qwen still runs CPU-only. Both Reason2 workers plus Qwen leave about17GiB
available; no restart during the rendered run was observed.

Actual Unreal ProcessSmoke -Enhancements passed assembly/preassembled/assembly,
15 fresh valid replies, all six thermal acceptance audits and zero ground-truth
fields sent to models. HTTP median1416.12ms/max1438.24ms;153.38 simulation seconds.
Three A replies and thermal holds caused reinspection without policy overrides.
The existing flow controller was used for this runtime regression; no new full
Qwen agent benchmark is claimed. The actual welding screenshot was reviewed and
shown in chat. Unreal is closed. Final single-image FP16 probe:27 calls,
557.81ms median (NIR553-554ms, sealant733ms), but only6/9 aggregate accuracy with
changed prompts. Keep it offline; original three-view batches are faster overall
than three separate calls (1671.76ms median aggregate) and remain deployed.

Repro source: make_qairt_float_vision.py, replace_qairt_vision.py, native worker,
probe_qairt_reason2.py and serve_qairt_reason2.py. Service startup rejects missing
or incompatible validation and modified worker/bundle hashes. Reference converter
and original weights remain intact. Python discovery156 tests and native rendered
policy135 checks pass. Private reports/logs/images are in artifacts/qairt-20260919;
docs/evk_qairt_experiment.md has commands and limitations. Branch main, uncommitted;
no commit/push requested.

## Agent speed follow-up — 19 September 2026

Compared the user's OR/shipping/model-comparison tutorials; see docs/agent_speed.md.
Qwen remains the exact Qwen3.5-9B Q4_K_M CPU model, now with eight threads.
CompactToolResult removes duplicate model-facing diagnostics and formats numeric
clocks to milliseconds, preserving the full journal, validity/errors and original
freshness logic. No recipe veto or sensor encoding changed. Fixed-history EVK
replay median excluding first requests improved 13.281 s -> 10.985 s; one allowed
inspection-order difference. Native policy 137 checks and Python 156 tests pass.
An isolated Q8 Hexagon controller candidate failed FastRPC mapping before inference;
it is not deployed. Its EVK copy was removed; workstation copy and private logs
remain. Three hash-matched duplicate transfer archives were removed from EVK only.

The first live attempt held on preassembled body2 after three optional seating A
results. Keep this failed evidence in artifacts/agent-speed-20260919/evk-process-smoke.
The final prompt clarifies the user's existing criteria: initial presence G gives
preassembled visual acceptance; both thermal gates are still required, while
panel_seated is for newly welded panels. This is prompt guidance, no code veto.
Host preassembled/fault probes pass. The final actual EVK run passed at
artifacts/agent-speed-20260919/evk-process-final: 3 departures, 22 agent choices,
11 fresh vision batches, zero rejected calls/audit failures. Qwen median 11.186 s,
Reason2 1.415 s, total 484.345 s versus the previous 648.77 s. First policy prefill
took 77.712 s; do not hide that startup cost. Native verdict, verify_live_agent.py
and 2,144 camera telemetry checks passed. This is development integration, not
new held-out accuracy. EVK Qwen and QAIRT Reason2 services remain active with
about 17.1 GiB available and no restarts; host Qwen and Unreal are closed.
All four synthetic fault probes also pass on EVK with the final prompt: hot
sealant/weld -> wait15, seating defect -> hold, invalid presence -> reinspect.
They execute no jobs/Reason2 calls and are not classifier accuracy evidence.

## Ministral controller follow-up — 19 September 2026

User requested the exact previous-project Ministral-3-3B-Instruct-2512 Q4_K_M.
Official GGUF SHA256 is 9ed150d4367e68df0ac8e1540f6ddc65b42d0ee26378329d1ecbca60f93fc5f8.
It is now the default host/EVK text controller; DF_AGENT_KIND=qwen explicitly
selects retained Qwen weights. Both Reason2 QAIRT NPU adapters remain unchanged.
Ministral currently uses eight EVK CPU threads, not NPU. Fresh qairt-dev 0.8.1 +
SDK 2.47.0.260601 export failed before compilation: Architecture mistral3 not
supported. Old compiled bundle was absent; retain isolated environment/failure
log. Do not alias the architecture or claim old tutorial NPU rates for this run.
No reference production deployment or /opt/qairt/current was changed.

Initial prompts passed selected replays but missed the independent weld cooling
inspection on preassembled live bodies. Keep failed host-process/evk-cpu-process
reports. Current configs/live_agent.json uses a shorter prompt plus an explicit
preassembled example requiring BOTH thermal results. This is prompt guidance,
not an executor recipe veto. All supported actions still execute; the independent
audit reports errors and never repairs decisions. HUD now says Agent/LLM AGENT.

Two actual EVK runs passed assembly/preassembled/assembly (six total departures),
22 decisions and 11 fresh valid Reason2 batches each, zero rejected/audit failures.
First pass 261.310 s, median 3.794 s; clean restart at normal 18195 passed 275.569 s,
median 3.952 s, first 28.885 s. Reason2 remains 1.416 s. Each camera verifier passed 2144
telemetry checks. Earlier optimized Qwen was 484.345 s / 11.186 s / 77.712 s first request.
This is development integration, not held-out accuracy. Host final prompt replay
passed 9/9 assembly, 4/4 preassembled, 4/4 faults and 16 varied preassembled histories;
two reconstructed failed histories also pass. Added scripts/agent/probe_departure.py
for the synthetic ID/clock/ambiguous-retry probes; expected choices stay evaluator-only.
Python 158 tests and 52 launcher checks pass. Services show about 19.5 GiB available;
Ministral service 2.9 GiB, Reason2 service 3.5 GiB, excluding some DSP/kernel accounting.
Private evidence: artifacts/ministral-20260919; docs/ministral_controller.md has
startup, provenance, failed candidates and measured limits. Normal endpoint 18195
now serves Ministral; 18198 remains Reason2. Qwen and temporary host Ministral are
stopped. No commit/push requested. A final captured HUD/welding image was shown
in chat. The evk-final-process directory is an interrupted UI-label refresh run;
evk-final-cold-process is the completed final run.

Final selected-prompt EVK probes passed 4/4 faults and 16/16 varied preassembled
histories (including ambiguous sealant retries). Native policy passed 137 checks;
final Python suite passed 158 and launcher checks 52. Unreal and temporary
Ministral candidate/host services are closed. Selected Ministral and unchanged
Reason2 NPU services remain ready via the normal tunnels; about 19.5 GiB is
available and no automatic service restarts occurred. No independent held-out
accuracy claim; these synthetic probes execute no jobs or Reason2 calls.

## Further Ministral speed tests — 19 September 2026

User explicitly excludes fine-tuning Ministral. Benchmarked CPU threads 4/6/8,
microbatches 128/256/512, ngram-simple target-verified speculation (key 4/draft 8),
Q8_0 K/V cache, shorter reasons and preserving original tool argument key order.
Keep the exact original Q4_K_M weights, eight threads, batch 256/ubatch 128,
F16 cache, no speculation and original configs/live_agent.json hash
c47aa7776b32f7001fced0c7b524e92fec74047b1ecf9dfd182e252864eb477a.
No Reason2/adapters/thresholds or accepted prompt changes this turn.

Discarded candidates: ngram replay was slower (3.877 vs 3.470 s warm); batch 512
helped cold prefill but slowed subsequent calls; Q8 cache had a slower cold
request and no useful warm improvement. Preserving generated argument order
saved about 18 uncached tokens but selected release_holder before weld inspection
in continued-history replay. Short 3-6-word reasons passed saved replay/faults
and 16 preassembled probes yet live body 2 incorrectly placed another panel after
presence G + adhesive G. Four audit failures, no automatic correction; stopped that
run after two departures. Failed report is evk-process. Do not adopt either
prompt/history change on saved-history probes alone. Original prompt restored.

Accepted change: remove the fixed 1-second delay after every tool completion.
Ministral thinks while R4 withdraws; a validated pending reply waits for actual
InspectionExtension==0 before dispatch. Pending replies block duplicate requests
and are cleared on pause/body turnover with request epochs. Camera motion,
three settle renders, capture freshness, and all process acceptance rules stay
unchanged. No deterministic recipe veto added. Runtime reports physical arm
readiness at each dispatch; verifier checks it. Native tests exercise fast replies
during withdrawal, pause cancellation and body turnover.

Actual clean-restart evk-original-process passed assembly/preassembled/assembly:
253.539 s vs 275.569 prior, saving 22.031 s / 8.0%; 22 decisions, 11 fresh Reason2 batches,
zero rejected calls/audit failures, zero source fields sent to models. Agent median
3.991 s, first 28.586 s; Reason2 median 1.415 s. Inference itself is not faster.
Tool-result-to-request idle time totals 0.341 s vs 22.094 s. All 2,144 physical camera
checks, 142 rendered policy checks, 160 Python tests and 52 launcher checks pass.
Private evidence: artifacts/ministral-speed-20260919. Updated Ministral/live-agent
guides, README and validation. No new held-out accuracy. Unreal, benchmark server
and temporary tunnel are stopped; normal EVK agent+Reason2 services are active,
about 19.5 GiB available, no auto restarts. No commit/push requested.

replay_latency.py now supports --continue-generated and --rebuild-arguments
for cache experiments. It remaps actual tool call IDs and stops at any choice
mismatch before giving a saved observation to the wrong job; two unit tests cover
that evidence boundary. These are synthetic replay tools, not live integration.

## One-token Ministral controller — 19 September 2026

User approved testing the logits trick without fine-tuning Ministral. Implemented
scripts/agent/single_token.py, serve_single_token.py, start_single_token.sh and
probe_single_token.py. Official Q4_K_M/runtime/config remain unchanged. The derived
prompt preserves policy and replaces JSON/reason output with a fixed A-M menu;
hash a6bb5d377bec78f47c6264e2b25b779e6ce39a23b557d467e58a80bb12ebdf3c.
All 13 actions are always available. Verify token IDs 1065-1077 at startup, request
one token, temperature0 + temperature sampler, unmasked pre-sampling top128 scores.
Require global vocabulary winner == returned action token. No grammar/bias/LoRA,
state mask, recipe veto or fallback action. Omitted label probabilities are unknown
with mass bounds. Fixed HUD reason explicitly says no explanation was generated.
Wait choices are 1/5/15 s; original variable-wait native endpoint remains separate.

HostGPU/EVK each pass 9 assembly, 4 preassembled, 4 faults and 16 departure probes.
Initial empty-sampler probes failed winner equality; exclude non-fixed host files.
Two actual cold three-body EVK runs pass: 197.913/198.266 s, each 24 choices and 12
Reason2 batches, zero rejects/audit failures. Both handle an ambiguous sealant result
with model-selected wait/reinspection, then independently inspect weld. Final agent
median 1.607 s/first 18.036 s, Reason2 median 1.432 s. Compared with native 253.539 s /
3.991 s median: 21.8% total and 59.7% controller improvement. Reason2 untouched.
All 48 raw token winners, histories and observations match actual execution;
2,338 camera checks per run, 169 Python tests, three negative audit checks pass.
No Unreal source changes for this protocol. Small development integration only,
not held-out accuracy or validated autonomous factory control.

EVK start_evk_qairt.sh --agent now starts the normal Ministral backend 18195 plus
dark-factory-edge-agent-single-token proxy 18196 (Ministral only). --agent tunnel
launcher forwards both plus Reason2 18198. Recommended agent command explicitly
uses 18196; native 18195 retains generated reasons. No global Unreal defaults changed.
Host GPU proxy can wrap 18193 on 18194. Never concurrently infer on both proxy/native
endpoints sharing one cache slot. Normal adapter logging disabled; optional empty
QA directory bounded to 64 requests. verify_single_token_agent.py requires complete
smoke logs/health/runtime. Private evidence artifacts/agent-single-token-20260919,
final evk-final-cold-process/proxy-final-cold. docs/single_token_controller.md records
commands/provenance/limits. Proxy uses about 13 MB; about 19.5 GiB available with models resident.
Unreal, host candidate and temporary EVK candidates/tunnel closed; normal services
remain ready. Normal proxy startup and repeated stack/tunnel startup verified;
no QA log argument in the final service. No commit/push requested.

## Training-free IQ9 cache/overlap research - 19 September 2026

User requests novel agent optimization while keeping prompts editable, preferably
without LLM fine-tuning, and suggests reversible welder pre-positioning. Studied
RadixAttention, Sutradhara tool-time prefill, PASTE speculative tools and LayerSkip;
research/measurement details are in docs/agent_optimization_research.md.

Selected two changes with original Q4_K_M/config/one-token prompt/runtime hashes:
1. Static policy-prefix KV persisted privately (96 MiB), keyed by exact weights,
   binary/libs, native args/GGML environment, rendered template and prompt. Cache
   corruption or edits recompute it; no actual observation/decision is stored.
   Native restart restored 945 tokens in 21 ms; total setup 109 ms versus 16.83 s
   on a miss. Startup n_predict=0 emits one setup token on the pinned backend;
   discard the entire response. Native model loading/hash checks remain.
2. After returning the chosen action, prefill only its actual assistant token and
   next user envelope ending at `Tool result: `. No future status/G/R/A/timestamp
   is fabricated. A native-slot mutex serializes this with at most one next real
   request. The full next prompt is authoritative; exact token-prefix matching
   discards stale suffixes after pause/body turnover. Skip I/M. Preparation errors
   are diagnostic only, never a guessed action or rejected next request.

Implemented policy_cache.py and --policy-cache/--prefill-next-turn in the proxy;
start_evk.sh writes runtime identity, start_evk_qairt.sh --agent enables both for
normal Ministral 18195/18196 only. Qwen remains separate. The normal stack rejects
experimental Q8 selection; use an isolated native launcher for those studies.
Do not concurrently call native 18195 and proxy 18196 sharing the one slot.

Actual cached-start three bodies: 172.815 s, 22 choices/11 Reason2 batches,
first 1.636 s, median 1.582 s, Reason2 median 1.415 s. With overlap: 170.429 s,
same 22/11 jobs, first 1.703 s, median 1.416 s, Reason2 median 1.415 s. Both have
zero rejected calls/audit failures; all 44 native token/history/dispatch checks
and 2,144 camera checks per run pass. Overlap: 19 preparations, median 266 ms,
zero errors. Matched-pair median improvement 10.5%, wall saving 2.39 s. Prior
198.266 s run had 24/12, so do not attribute its whole difference to cache.
No new held-out accuracy claim.

Rejected: host rows and alias JSON encodings both release after seating before
weld inspection; unchanged JSON passes 9/9. Conditional what-if probes match
17/24 strict expected sets (not an unsafe rate); include two clear premature
releases, others conservative/different order. Never execute these plans. Real
Turnip Adreno 663/Mesa 25.2.8 Vulkan found via scoped render-group service and
pinned build, but first request timed out after 115 s; stopped. No driver/system
package changes. Official Q8 CPU first two choices 14.41/1.22 s, then inspected
sealant before placement; stop replay, retain Q4. Vulkan build and Q8 probe
launchers remain reproducible, private files/weights only. OpenCL had no platforms.

Welder staging is assessed, not implemented. Existing 1.3 s approach enters R4's
workspace after the first 0.35 s; need a separately checked waiting pose, open
jaws, no current/sparks/heat and cancellation retreat. Max 2.6 s across two new
panels in the three-body fixture. Avoid an extra 1.4 s LLM call merely to save
1.3 s: put preparation inside an existing parallel job with a separately
authorized commit. No Unreal visual/motion changes this turn. Ordinary LoRA
doesn't remove layers or prefill; output already one token, no measured
additional gain to promise.

178 Python tests pass, including invalidation/corruption, no invented observations,
fast next requests, canceled-body history isolation and optional preparation
failure. Rendered model smoke/audits pass. Evidence artifacts/agent-prefill-20260919.
README, one-token guide, research and validation updated. Unreal, host/Vulkan/Q8/QA
candidates and temporary tunnel stopped. Normal 18196 cache+overlap ready, QA logs
off; 18195 and unchanged 18198 active, no automatic restarts, about 16.9 GiB available.
No commit/push requested; preserve pre-existing uncommitted work.

## Vulkan timeout diagnosis follow-up - 19 September 2026

User requested elaboration after commit ef4f7b6. Retrieved the existing EVK kernel
journal, without rerunning inference or changing services. At 14:38:55 UTC, one
second after Vulkan llama-server accepted the request, Adreno reported gpu fault
ring 0 / hangcheck recover and explicitly named the tested llama-server process.
A subsequent synchronous external abort occurred in the GPU recovery/crash-state
path (_a6xx_get_gmu_registers -> a6xx_gpu_state_get -> msm_gpu_crashstate_capture
-> recover_worker), kernel 6.8.0-1077-qcom. The 115 s client timeout is therefore
a symptom of observed GPU execution and recovery failures, not measured slow
throughput. Original fault cause remains unknown; do not blame flash attention,
a shader, Mesa, hardware, OOM or kernel alone without isolation. A whole-machine
panic/reboot is not established. Model loaded in 5.4 s; service peak memory 3.2 GB
under 7 GB limit; SIGKILL ultimately stopped it. Future diagnostics should start
from a clean reboot and isolate tiny inputs/batches, flash attention off and
partial offload. Those variants remain untested. CPU Ministral and NPU Reason2
completed the later live validations. Private journal evidence and research
notes updated; no new code changes, inference tests, commit or push this follow-up.

## Ministral NPU / Reason2 contention - 19 September 2026

User requested actual Ministral NPU execution and shared-NPU measurements. A clean
EVK reboot preceded this work. The isolated dynamic candidate uses official BF16
source quantized directly to Q4_0 decoder / Q8_0 tied head; pinned llama.cpp
0eadefeb plus GenieX 0.6.1. Q4_K/Q6_K matrices in the selected CPU GGUF cannot use
this backend's matrix path. HTP buffers/session prove decoder offload; the large
tied vocabulary head remains CPU. Do not interpret the generic 27/27 layer log
as every operation running on NPU. See docs/ministral_npu.md for exact hashes.

Dynamic candidate is REJECTED for live control. Original prompt passes 33 saved
probes but actual three-body run chooses departure while the holder remains
engaged (one mechanical reject). A holder reminder passes 41 offline probes,
then its actual run releases at 194.368 C without post-weld seating/cooling
confirmation: four independent audit failures, zero mechanical rejections.
Actual choices were not corrected. Both runs have about 269 ms median agent
latency and 1.415 s Reason2 batches; speed does not establish correctness.
The reminder was removed. Original a6bb5d37 prompt and c47aa777 base config stay
selected; normal CPU 18195/18196 and NPU Reason2 18198 remain the working fallback.

Forced-full-prefill contention (five measured plus one warmup per mode) gives
presence agent/Reason2 alone 2.193/1.229 s, simultaneous 2.184/3.263 s; weld alone
2.926/1.408 s, simultaneous 2.924/4.181 s. Pair completion improves only 3.6-4.4%
over serial. This demonstrates shared-compute contention, not an out-of-memory
limit: about 17 GiB remained with both candidates plus fallback resident.
Reason2 decisions remained unchanged on repeated saved inputs. Actual serial
agent/tool flow retains normal Reason2 latency. Avoid overlapping speculative
NPU agent prefill with a needed vision request. Warm CPU replay uses a different
cache regime and cannot establish matched CPU/NPU full-prefill speedup.

New benchmark_reset_cache is offline-only/default false; never change prompt,
action selection or normal cache behavior for a benchmark. The compiled SDK2.50
candidate, exact tokenizer/YaRN preflight, native full-vocabulary Genie sampler
and reset/append transport are documented separately in
docs/ministral_qairt_export.md and docs/ministral_npu.md. Compilation, token
parity, action correctness and useful cache reuse require separate evidence;
source hashes alone do not establish numerically identical exported inference.
Private evidence is artifacts/ministral-npu-contention-20260919. No fine-tuning,
new held-out accuracy, Reason2 adapter changes or new deterministic recipe veto.

The subsequent compiled SDK 2.50.40 / Genie 1.20 path PASSED actual integration.
It exports the original Q4_K_M source into QNN mixed 4/8-bit weights and FP16
activations, with source-faithful Tekken, explicit YaRN and a 4096-token context.
This is not bitwise GGUF execution. Build/preflight/finalizer provenance is
recorded separately; see docs/ministral_qairt_export.md. Both HTP binaries total
3.417 GB. All 22 native prompt token arrays, 33 reset probes and 9 growing-history
choices pass. Append retains only an exact prior input plus the actual sampled
token; otherwise reset. Returned action probabilities differ by up to 0.01349
from reset, so do not claim exact cache-score parity. One-token selection is
unmasked global-vocabulary argmax, with no LLM adapter or changed policy.

Actual compiled run: 185.940 s, 28 choices/14 Reason2 batches, zero rejects/audit
failures; agent median 269.146 ms, first 3852.963 ms; Reason2 median 1332.105 ms.
All 28 raw-winner/history/dispatch checks and 2726 physical-camera telemetry
checks pass. Faster decisions do not make this run faster than the earlier
CPU 170.429 s/22 choices/11 batches: it needed three extra cooling inspections and
waits. This is development integration, not a held-out correctness score.

Compiled full-prefill concurrency slows the agent 22-25% and Reason2 by 2.82-2.97x;
pair completion improves only 3-4%. Warm append stress (1231 retained tokens,
60 submitted, 3 measured plus 1 warmup per mode) is less disruptive: agent alone
252 ms vs 365 ms concurrent; Reason2 alone 1316 ms vs 1481 ms concurrent; pair 1570 ms
serial vs 1482 ms concurrent. All decisions stable. An independent checksum read
overlapped only the excluded first warmup/untimed prime, not measured requests.
About 15.1 GiB was available with compiled agent, both Reason2 bundles and CPU
fallback resident. Shared compute limits overlap; memory is adequate. Keep
dependent agent/vision requests serial and preserve existing overlap with R4
withdrawal rather than speculating future inspection results.

Final state: compiled NPU native 18206 and normal one-token proxy 18197 are active
on EVK, with an 18197 WSL tunnel. Benchmark-reset mode and QA logging are off.
These are explicit transient candidate services; normal startup/default agent
18196 remains CPU with its policy cache/prefill, Reason2 stays 18198. Both rejected
dynamic services and unused experimental tunnels are stopped. Unreal is closed.
Python discovery: 216 tests, one skipped; the separately enabled SDK 2.50 native
protocol/scoring fixture passes 12 tests. No commit/push requested this turn.

## IQ9 GPU agent experiment — 19 September 2026

Keep Reason2 on NPU and Ministral CPU endpoint 18196 as the selected deployment.
The compiled NPU agent is stopped. GPU placement is now executable but is NOT
accepted for live process correctness or a speed improvement. See
docs/ministral_gpu.md and docs/private_turnip.md; private evidence is under
artifacts/ministral-gpu-20260919. Earlier NPU-active session notes are historical.

Stock Mesa 25.2.8 fails batched pipeline creation. Isolated Mesa 26.1.8 changes
that into a GPU/kernel-recovery fault at subgroup size 128. Real driver option
tu_restrict_subgroup_size_64=true avoids the observed fault. CPU-reference tests
then exposed gross Q6_K errors. The opt-in llama-q6-scale32.patch reads the same
signed scale bytes through packed16 and explicit int32 sign extension, preserving
the GGUF. Ten selected numerical cases pass; compiler-matched pristine source
still fails. Reproducible host builders are build_private_turnip.sh and
build_private_q6_vulkan.sh. System drivers and original runtime are unchanged.
Use an isolated executable/plugin directory: GGML_BACKEND_PATH is additive and
does not replace a normally discovered backend. The tested GPU candidate uses
context 4096, batch/ubatch 64, four CPU threads, all 27 layers offloaded, flash attention
ON and the unchanged action prompt. FA off hits a Mesa workgroup-occupancy error
above 1024 cached tokens. start_evk_vulkan.sh is opt-in, never normal startup.

All 33 saved probes pass, but actual three-body ProcessSmoke FAILS: 194.089 s,
21 choices/10 Reason2 batches, agent median 3093.15 ms, vision 1354.67 ms. Body 2
preassembled takes presence->weld inspection->departure without sealant evidence.
Independent audit records one missing_cool_sealant_observation; no veto repairs
it. Zero rejected/transport calls. All 21 raw-token/history comparisons match;
overall audit remains failed. Physical-camera telemetry passes 1950 checks.
Static policy preparation of 35.15 s is excluded from live time; next-turn prefill
completes 18 times without error. These are development results, not new accuracy.

Exact-history cache-reset comparison: CPU chooses sealant D first, GPU weld G;
both then choose invalid departure I when given the weld-first history without
sealant. Matching rendered-prompt hashes rule out different prompt strings;
different contexts (8192/4096) and backend arithmetic remain comparison limits. This is
a shared policy counterexample, not solely a GPU-cache fault. Extend future
policy tests to both thermal orders and each missing stage; do not silently
mask actions, add recipe vetoes or claim that 33 probes validate arbitrary paths.

Final state: CPU 18195/18196 and Reason2 NPU 18198 healthy; CPU policy cache restored.
GPU native 18204/proxy 18207 and their tunnels stopped; NPU-agent candidates stopped;
Unreal closed. GPU --check succeeds on the prepared EVK. Python 216 tests pass
with one skip; shell syntax and diff whitespace pass. No commit/push requested.

## Further model speed experiments — later 19 September 2026

The user asked to keep looking for speed gains. Normal deployment remains
original Ministral CPU 18195/18196 plus Reason2 NPU 18198. Preserve the editable
policy, original Q4_K_M, one unmasked action token and no recipe veto or training.
See docs/ministral_cpu_tuning.md and the final section of docs/ministral_npu.md.
Private evidence: artifacts/ministral-cpu-tuning-20260919.

CPU tests used 21 exact saved GPU-run histories, known policy/tool-envelope
preparation, a single serial candidate and matching prompt hashes. Original
8-thread median: 1316 ms, repeat 1305; six threads 1561; four threads 2079;
microbatch 64/256 both about 1320. Flash attention off: 1176/1100; matched
OpenMP control: 1333; native worker pool: 1133; 16-row attention tile: 1097;
native pool plus flash attention off: 984 ms. All retain the same 21 CPU winners,
including its known error. build_private_cpu.sh builds pinned A78C dynamic CPU
plugins for omp-control/noomp/tile16. Initial linked-only packages failed to
register before inference; corrected DL=ON plugins export ggml_backend_init.
Only the CPU plugin changes in copied private runtimes. Tile16 changes the
numerical attention path; no numerical-equivalence claim. The best combination
needs no custom attention patch. The normal runtime is unchanged.

Tuned CPU ProcessSmoke PASSED: 182.6177 s, 28 choices / 14 Reason2 batches,
agent median 1036.30 ms / maximum 1386.51; Reason2 median 1415.55 ms; zero
rejections or audit failures. All 28 token/history/dispatch checks and 2726
camera checks pass. Extra cooling inspections and waits erase the cycle-time
benefit relative to 170.429 s / 22 choices / 11 batches. Keep opt-in; this is not
faster full-cycle performance or new held-out accuracy. benchmark_cached_suffix.py
records source/prompt hashes and separate preparation costs, primes only known
input, and never dispatches tools. A real CLI replay also passed after the run.

The separate 17-case thermal-order suite in probe_departure.py and
probe_single_token.py passes 14/17 on both original and tuned CPU. Failures:
missing sealant after weld G; stale adhesive G before valid weld G; stale weld G
before valid adhesive G. One accepted wait/reinspect choice differs. Historical
16-departure / 33-probe denominators remain unchanged. Do not mask failures or
claim the original CPU is universally correct. These are synthetic probes.

Genie now has opt-in --cache-mode append_rewind for guarded interior-prefix
rewind on API 1.20.0, preserving exact append first. Boundary/repeated/shorter
cases reset. Every completion requires one fresh global sampler callback,
matching emitted winner, END and exact occupancy. The former ordinary-token
path could accept a missing END; this is now rejected. Bridge health includes
its source hash. API eligibility alone does not certify another SDK build;
actual SDK 2.50.40 library, worker and bridge hashes are recorded privately.

The 42 device calls compare cached versus independent reset on 21 histories.
Token arrays and all 21 NPU winners match; maximum absolute action-probability
delta is 0.005653. New-body arrivals fall from 3823/3827 ms to 481/482 ms,
with 948 shared prefix tokens and 68/74 fresh tokens. Cold first call remains
3821 ms; ordinary append takes about 242–244 ms. These are direct worker round
trips, not HTTP/HUD timing, and only two arrival transitions. No new contention
measurement or live NPU integration is claimed. Both modes also select premature
sealant inspection before placement in requests 3/15 and missing-sealant departure
in request 12. These policy/export limitations remain; they are not cached action
reuse. The NPU agent remains experimental and stopped.

Next unmeasured lead: AR64 exports 64 x 131072 FP16 logits but consumes one final
row. The SDK supports prefill without logits followed by a smaller final-head
graph. This needs a separate export and new cache/numerical validation; do not
assume a gain or combine untested changes.

Final state: original CPU and Reason2 healthy, original policy cache restored;
CPU candidate 18208 / proxy 18209 / tunnel stopped, Genie worker closed, Unreal
closed. Python discovery: 231 tests, one optional fixture skipped; explicitly
enabled SDK protocol fixture: 16 pass. Helper clean/incremental builds, shell
syntax and whitespace checks pass. No commit or push requested this turn.


## Continued speed experiments — 20 September 2026

The user asked to continue optimizing. Preserve original editable Ministral
policy/weights, unmasked global-vocabulary one-token decisions and no recipe
veto. Normal CPU deployment remains unchanged. Evidence root:
artifacts/model-speed-20260920. See docs/ministral_cpu_tuning.md and
new docs/reason2_prefill_optimization.md.

CPU noomp/FAoff baseline reproduced at 984.22 ms, repeat 987.50. Strict affinity
986.37, poll0 988.26, poll100 986.98; no useful gain. Actual thread masks confirm
strict pinning. Native pool + tile16 with FA on 1027.26 ms, slower than FAoff.
All six configurations retain 21 rendered hashes/winners, including the known
wrong departure. build_private_cpu.sh now supports noomp-tile16; clean build
passed. Measured private plugin ba3b9854... differs from generalized helper's
3657b4f0... build; exact hashes/provenance are documented, not conflated.

Reason2 qairt_score_worker.cpp has optional --prefill-only. Same selected
three-image request, processor/DeepStack/MRoPE/prefill/native global sample and
full 151936-logit scoring; skip unused prefill-to-decode-to-prefill KV reshaping.
Require fresh cache and clear providers/reset on failure; compare sampled ID
with independent global argmax, including EOS. No new adapter or prompt.
Do not describe the deployed three-image route as three independent single-image
calls: that earlier changed-prompt experiment remains offline after 6/9 accuracy.

Twenty-four paired batches save 34.88 ms median per case (~2.5–3%), identical
winners/decisions/GAR probabilities. Full existing 192 cases retain 119/120 primary
and 72/72 weld, same one conservative sealant hold, zero false G. All 192 serialized
GAR probabilities and raw winners exactly match retained device validation;
not all vocabulary scores are serialized. Candidate medians 1362.06/1344.12 ms.
New probe/server --prefill-only flags bind evidence to binary, bundle, policy
AND execution path, including every validation row/native reply. Default path
is retained. Do not reuse ordinary-path evidence for candidate activation.

Actual candidate Reason2 + original CPU agent ProcessSmoke passes at 177.5615 s
wall (171.7048 simulation), 24 choices / 12 vision batches, 0 rejections/audit failures.
R2 median 1357.65 ms / max 1399.12; agent 1464.87 ms. All 24 raw-token/history/dispatch
and 2338 physical-camera checks pass. One extra sealant retry/wait makes this
longer than earlier 170.429 s / 22 choices / 11 batches; no full-cycle gain or new heldout
accuracy claim. Original 18195/18196/18198 services restored, experimental vision
18210/proxy 18211 and tunnel stopped; Unreal confirmed closed.

Next Reason2 source lead: AR128 head emits 128 x 151936 logits. A runtime-indexed
Gather of the final valid hidden row before full-vocab lm_head may avoid unused
rows while preserving all decoder/KV computations. Index must be current chunk
length-1, never fixed 127 (actual finals 84/100/108/30). Requires separate graph
export, registered input provider, row 0 scorer and new exact model validation;
not implemented or measured here.

Ministral AR64 prefill-head investigation: SDK 2.50 graph audit removes 535 ops
(8196 -> 7661), retaining 26 KV outputs. DlcModule.save silently saves the original
container; reload detects the unchanged graph. Direct IrDlcSerializer successfully
saves the unmodified graph but exits with failure on both manually pruned and
native-remove-disconnected versions, including eager weights. No candidate was
compiled or deployed and no speed gain is claimed. prepare_ministral_prefill.py is
audit-only; --compile fails before SDK/source access. Six focused tests cover it.
An AR64-KV-only/AR1-logits graph would also disable the current scheduler and lose
the previously measured prefix-rewind benefit; do not assume those gains combine.
See docs/ministral_npu.md. Private graph experiments remain ignored.

Final September 20 checks: Python discovery 238 tests, one skipped; separately
enabled Genie SDK protocol fixture 16 passed. Rendered three-body ProcessSmoke
and token/physical-camera audits passed as recorded above. Original CPU agent,
proxy and Reason2 services are restored; candidate services/tunnel and Unreal are
stopped. No default deployment change and no commit/push in this turn.

## Exact image preprocessing study — later 20 September 2026

User wants more implementation optimizations useful to production LLM/VLM
tutorial readers. See docs/inference_optimization_patterns.md. New opt-in
--prefill-only --fused-processor uses execution prefill-fused-v1, direct final
patch packing and a 3 KiB per-channel 256-byte normalization LUT. Same pinned
STB decoder/tokenizer, same 448x256/P16/T2/M2 layout and original prompt; reject
non-grayscale/nonopaque/wrong-size/16-bit PNGs. No camera-result caching,
quantization, prompt edit or retraining. Compiler uses -ffp-contract=off.
Builder needs the pinned geniex-proc third-party/stb/stb_image.h as well as the
previous headers. Model bundle and final unmasked vocabulary scoring unchanged.

Retrieved private processor-bench.json covers all 192 development batches:
396,361,728 floats bit-identical, tokens and grids identical. Twelve synthetic
comparisons add L/LA/RGB/RGBA, all byte values, spatially asymmetric pixels,
one/two/three images and a Unicode prompt. Four invalid cases rejected, valid
case after failures passes. Reference/candidate medians 61.635818/5.5214345 ms;
median per-case saving 56.1491765 ms. Processor-only, order alternates. A final
nonfinite-LUT/IHDR-bit-depth guards and probe input/response guards were added locally after
these device checks and still need a fresh target build.

Partial actual model pilot console: primary prefill-only 1171.8933 ms, fused
1112.8961, fused repeat 1118.8682, private terminal-KV variant 1110.2171 (18
batches each). Weld fused-repeat 1283.8428 and terminal 1288.2695 (six each).
These are block medians, not completed paired statistics. Terminal KV omission
has no consistent gain and remains private; do not add it to public execution.

CRITICAL CURRENT STATE: EVK 192.168.1.158 became unreachable to ping/SSH during
later weld worker startup. Cause unestablished. Cannot retrieve complete pilot
report, verify its raw winners/probabilities, or run full classification/live
validation. Original automotive services had not been changed/stopped before
the outage; do not claim currently healthy. No USB/serial recovery connection
was found on this host. Asked user asynchronously to check power/network and
power-cycle. User previously authorized reboot. The local hung SSH client PID
119819 was terminated; remote experiment had timeout 400 s and per-worker
finally cleanup, but its final state is unknown until connectivity returns.
No new candidate service/tunnel was started. Never reuse the prior prefill-only
validation to enable fused; router enforces separate complete execution evidence.

Resume by restoring EVK connectivity, collecting
/home/ubuntu/qai-dark-factory/experiments/model-fused-20260920/inference-bench.json
and matching sources/logs/binaries, then rebuilding final worker/probe. Run
scripts/model/verify_qairt_processor.py against both validation manifests, full
probe_qairt_reason2.py with --prefill-only --fused-processor for both roles,
compare original 192 scores, then actual agent ProcessSmoke. Existing private
build.sh/processor_bench.py/inference_bench.py are under artifacts/model-fused-20260920;
adapt previous model-speed-20260920 live scripts to a new output/execution.

Local Python discovery 240 tests, one optional skip; packing permutation/byte
normalization/invalid-input tests and cross-execution validation checks pass.
Rendered verify_unreal_bridge.py passes G/A/R/disconnected with async readbacks;
this is MOCK transport evidence only, not fused model accuracy. Unreal exited.
Documentation records incomplete device acceptance. No default launcher change,
commit or push requested. Preserve all earlier unrelated dirty work.

## Follow-up after user "Proceed" — 20 September 2026

EVK remains unavailable: .158 times out/no route; saved Windows iq9075-evk alias
points at .92, which refuses SSH. Confirmed local Ethernet 192.168.1.108/24.
Bounded port-22 discovery across that local /24 found one SSH service but no
match for the known EVK ed25519 host key. No unrelated host login attempted.
No serial port found. Asked user whether power/network or address changed;
no answer yet. Do not claim remote services healthy or stopped.

Added tests/qairt_fixed_processor_png_test.cpp and its optional Python harness:
DF_GENIEX_PROC_SOURCE points to pinned geniex-proc. It compiles the production
fixed processor with the real STB decoder on the local CPU; tests all supported
PNG channel forms, multiple images, 1-/16-bit rejection, alpha/color/size/malformed
inputs, partial-batch failure and a valid subsequent request. Full discovery with
this fixture enabled: 241 tests, one optional Genie fixture skipped. No model
inference. No new rendered run needed; prior bridge fixture remains its own evidence.

Prepared private artifacts/model-fused-20260920/validate_final.sh for a NEW EVK
experiment directory with current copied model sources. It serializes worker use,
rebuilds both binaries, records source/binary/STB/runtime hashes, verifies all 192
processor batches, runs both role score validations and demands exact original
decisions/winners/GAR probabilities plus matching input metadata. Completed stages
can be retained; incomplete/mismatched reports fail closed and must use fresh output.
This script has shell syntax validation but has NOT run on the unreachable EVK.
Use bounded SSH keepalives and a device-owned bounded systemd job when reconnecting,
so the client network loss cannot silently erase the test run. Original deployment
and services were untouched in this turn; Unreal is not running. No commit/push.

## EVK reboot recovery and completed fused validation — 20 September 2026

This supersedes the unreachable/pending state above. User reboot restored SSH
at 192.168.1.158. Original model services were inactive after boot. Retrieved old
pilot report is 125801 NUL bytes, unusable; no outage root cause established.
Fresh bounded device-owned jobs serialize worker loads with flock. Timing saves
use atomic replacement plus fsync, preserving completed results across SSH loss.

Private evidence: artifacts/model-fused-final-20260920, same directory under
/home/ubuntu/qai-dark-factory/experiments on the EVK. Final sources/worker/probe,
pinned STB and runtime hashes recorded in build-identity.json. Processor repeat:
all 192 batches / 396361728 floats, tokens and grids identical; reference/fused
medians 60.001135/5.5037675 ms, paired saving 54.5093315 ms (~10.9x preprocessing).
Final input guards are included. Full model validation: 119/120 primary, 72/72
weld, same conservative cool-sealant hold, zero false G/errors. Every raw winner,
decision and serialized GAR probability exactly equals retained original reports.
Not new held-out accuracy; only GAR probabilities are serialized, so do not
claim identity of every vocabulary score.

Counterbalanced same-binary timing: 18 primary +6 weld cases, two repeats per
path/input, 96 measured requests, one warmup per block, reversed second case
order. Primary ABBA and weld BAAB, where A=prefill-only/B=fused. Per-case mean
medians primary 1188.4183/1126.7957 ms, weld 1360.9163/1298.4913; median paired
saving 63.4478/62.1580 ms (~5% incremental beyond prefill-only). All reported
scores/winners/decisions/token counts match. No combined original-path timing
claim. Terminal-KV variant remains private/unproven and was not used.

Actual candidate ProcessSmoke with Enhancements and original CPU agent passes:
177.0125 s process wall, 171.0394 s simulation, 24 choices/12 camera batches.
Reason2 median/max 1298.97/1349.07 ms; agent 1458.59 ms. Zero rejects/audit failures;
24 single-token/history/dispatch checks and 2338 physical-camera checks pass.
Preassembled body had one sealant A, then agent wait/reinspection. Prior prefill
177.5615 s has same counts; 0.55 s difference alone is no cycle-speed claim.
Launcher including UE startup/shutdown 200.6611 s recorded separately.

Original 18195 CPU agent, 18196 single-token proxy, 18198 Reason2 restored and checked
healthy on device AND Windows. dark-factory-fused-candidate/proxy and WSL fused
tunnel stopped; Unreal process count 0. Candidate remains explicit opt-in with
prefill-fused-v1 evidence binding. Normal launcher unchanged. README, optimization
notes and validation record updated. Local suite remains 241 tests / one optional
SDK skip from prior turn; no code change here requiring repetition. No commit/push.

## Live EVK / GPU switch — 21 September 2026

User requested both backends running with EVK selected, and explicitly chose an
in-demo switch. `run.ps1 -Process -ProcessControl agent -Enhancements
-ProcessBackendSwitch` defaults to EVK; F4 / Xbox right-stick click switches both
Reason2 and the agent. EVK is Reason2 NPU (18198) plus CPU Ministral token proxy
(18196). Host is Reason2 CUDA (18191) plus CUDA Ministral token proxy (18194,
native 18193). Existing launches without the option keep their defaults. This
does not promote the experimental prefill-only/fused Reason2 workers.

Switching cancels pending HTTP/capture batches and queued decisions, advances
the agent cancellation epoch and rejects old image encodes. A canceled actual
inspection becomes a `cancelled_by_backend_switch` tool result. Body, mechanical
job, pause, completed tool conversation and turn budget survive. Explicit agent
holds remain holds; transport failures can retry on the selected backend. No
fallback, recipe repair, source truth or automatic agent reinspection is added.
HUD and private trace identify the backend; model-visible evidence is unchanged.

UE 5.8 build passed. Python discovery: 245 tests, one optional SDK skip; launcher
fixtures: 52 checks; native policy: 151 checks including nine switch cases.
Private evidence: artifacts/backend-switch-20260921. Actual flow switching smoke
passed assembly/preassembled/assembly with 16 camera batches, six thermal checks
and three departures at 151.997 s simulation time. Routing auditor passes and
physical-camera auditor passes 3114 checks. QA-only ProcessBackendSmoke selects
EVK/GPU/EVK on bodies 1/2/3; interactive runs switch only on user input.

Preserve the failed agent run: routing succeeded on both transitions, but after
two departures Ministral skipped panel placement on body 3, made thermal checks,
tried releasing an unengaged holder, then inspected seating and chose hold.
26 choices / 14 valid camera batches, one mechanical rejection, zero transport
errors. No full agent pass is claimed. Camera audit passes 2726 checks; routing
auditor correctly rejects the incomplete process despite correct endpoints.
Do not mask this with deterministic policy vetoes or discard the failed evidence.
Both EVK/host HUD screenshots were reviewed and shown in chat. Interactive agent
launch uses dark mode, headlamp off and the normal 2560x1440/v-sync settings.

### HUD presentation follow-up — 21 September 2026

User requested removal of "Raw sensor inputs exclude...", yellow key/controller
names with white descriptions, and all HUD font sizes increased by one point.
Shared observer text draw/measure helpers apply that size increase consistently
to status, steps, previews, thermal scales, replay and collider legends. Explicit
key spans cover backend switching, graphics, replay and navigation controls.
The note is removed and its panel shortened. These are observer HUD changes only;
sensor generation and model decisions are unchanged. Private rendered evidence
is under artifacts/hud-labels-20260921.

UE 5.8 build and the actual 2560x1440 EVK flow ProcessSmoke passed: three bodies
departed with thermal acceptance checks respected. Reviewed startup and welding
HUD screenshots, including thermal scales, and showed the captures in chat.
Interactive agent mode is relaunched with EVK selected and both backends ready.

## Separate IQ9 HTP devices and live prefix rewind — 21 September 2026

User authorized trying cached Ministral NPU alongside Reason2, including separate
accelerator placement, and explicitly requires the demo closed when finished.
Runtime QNN inventory exposes device 0/core 0 and device 1/core 0 (SoC 77/v73).
Actual worker handles confirm Reason2 on fastrpc-cdsp-secure and relocated
Ministral on fastrpc-cdsp1-secure. Select device_id, not core_id=1 or num_cores=2.
New read-only probe_qnn_devices.cpp and prepare_genie_device.py reproduce the
inventory/private config without changing source weights or bundle. Public
helper was used for the actual live run. See docs/npu_device_placement.md;
private evidence artifacts/npu-placement-20260921.

Same saved weld requests: five warm trials/mode, three full-prefill trials/mode,
one separate warmup each. Warm primes excluded from timing and actual append
verified (1231 retained +60 fresh tokens); full requests all confirmed reset.
Shared/split concurrent medians: warm agent 352.9/250.1 ms, Reason2
1432.7/1286.2 ms; full agent 6027.8/4898.5 ms, Reason2 3805.5/1325.3 ms.
Sequential latency essentially unchanged. Device blocks are not counterbalanced;
HTTP overlap is not per-kernel profiling. No power measurement or causal
parallelization of a decision with its unseen observation is claimed.

Corrected HTTP history replay: all 21 winners match cached/reset on device 1
(max action-probability delta .01158), and all 21 exact token arrays/winners
match fresh reset on device 0 vs 1 (max delta .006925). Historical direct-worker
replay omitted BOS whereas live HTTP adds it; comparing those different inputs
failed two choices. Preserve that failed report, do not attribute it to device
placement. With correct HTTP inputs the two premature sealant choices disappear,
but the existing missing-sealant departure remains. No reliability claim.

Actual agent ProcessSmoke passed first run: assembly/preassembled/assembly,
185.490 s wall, 30 choices/15 actual Reason2 batches, zero rejects/audit failures.
Agent median/max 269.183/3852.907 ms; Reason2 median/max 1332.401/1349.102 ms.
30 token/history/dispatch checks and 2920 camera telemetry checks pass. Native
cache modes: one cold reset, 27 appends, two body-prefix rewinds at proxy
489.9/488.9 ms, retaining 949 tokens. No recipe veto/mask or new fine-tuning.
More cooling/ambiguity retries prevent a whole-cycle speedup claim. Screenshot
of actual welding was inspected and displayed in chat. Python discovery passes
246 tests in configured WSL environment with two optional SDK fixtures skipped;
native process policy passes 151 checks. Windows system Python lacks Pillow;
its initial suite attempt is preserved separately, not used as a pass.

Temporary placement native/proxy/tunnel services stopped after agent testing;
original CPU agent/proxy and Reason2 18195/18196/18198 remain healthy. Normal
CPU-agent defaults unchanged. The backend-switch EVK preset still selects CPU
18196, so do not combine that flag with this candidate launch. No commit/push.

Final actual flow regression also passes: three departures, 15 batches, six
thermal acceptance checks, 153.279 s simulation time and 2920 camera telemetry
checks. Both fixtures exited; confirmed no Unreal demo process remains.

## Dual-HTP pipeline investigation — 21 September 2026

User requested further optimizations and an explanation of devices, weights,
inference dependencies and one/two/many resident models. See
docs/dual_htp_pipeline.md. This turn changes documentation only; private research
code/evidence stays ignored under artifacts/dual-htp-overlap-20260921. The original
CPU-agent selection and all production workers/contracts remain unchanged.

Private device-1 worker prototype prepares only the exact known policy or chosen
action envelope, retaining input KV while discarding the partial-prefix score.
No invented observation or speculative physical action. Existing END scoring
still executes during preparation; it is not zero-compute prefill. Exact prefix,
BOS/input hashes, occupancy and fresh actual score checks remain required.
Four blocks baseline/prepare/prepare/baseline replay 30 actual prior histories:
120/120 unmasked winners match, all 30 token hashes match, all prepared envelopes
match, max action-probability delta .00970 (not bitwise equality or accuracy).
First decision 3823.7 -> 481.7 ms with 3581.4 ms policy preparation done earlier
(944 known tokens, 71 actual suffix). Warm median 243.27/243.31 ms unchanged.
Typical 29-60 token suffixes still need one AR64 block after six-token envelope
preparation; two 66-token turns cross to 60 and save ~238 ms. Preparing every
turn adds ~6.31 s net worker time per 30-history replay. Do not enable it broadly.
Startup-only preparation is promising but not promoted; requires lifecycle,
cancellation/provenance tests and actual rendered integration first.

Three independent overlapping startup preparations and saved Reason2 weld
requests: medians 3597/1357 ms, pair3598 ms, all valid G. This is safe overlap
evidence, not a whole-cycle gain or a new accuracy test. Prior actual trace has
15 inspection jobs at 4278 ms median, including scans and 1332 ms Reason2; all
15 following agent calls (~269 ms median) fit within camera withdrawal dispatch
gaps (~793 ms median). Do not count already-hidden time as recoverable cycle time.

Current Reason2 is one JOINT classification of three separately framed images
(112 visual tokens each). Both encoder and decoder use HTP0. Do not describe this
as three independent classifications or distribute answers by camera voting.
Next candidates: stream each actual image into encoding during R4 movement;
incremental decoder prefill with exact positions/DeepStack/epoch/freshness;
AR16/32 warm agent graph variants; combine existing fused Reason2 and split HTP
candidates; separately authored harmless mechanical staging. These remain
untested opportunities, not enabled features. Three compiled model instances
(two complete adapted Reason2 bundles plus Ministral) already coexist on two
accelerators; residency is limited by memory/runtime, execution by resources and
dependencies. Weight sharing is not automatic between the current exports.

Final check: bounded research service inactive, no private probe worker, original
18195/18196/18198 health HTTP200, zero Unreal/demo processes. No commit/push.

## Two-view / concurrent Reason2 tests — 22 September 2026

User authorized testing two-image coverage, independent classification and one
versus two HTP workers, keeping Ministral on NPU between inspections. See
docs/two_view_npu_experiment.md and private artifacts/two-view-htp-20260922.
Production defaults, shaders, motion, capture contract and workers unchanged.
No demo was launched; user still requires it closed when testing ends.

576 actual single-image calls cover the existing 192 development batches, using
the original FP16-vision/W8-text bundles and versioned single-view prompts.
All three pairs evaluated offline. Selected pair1+2 for primary, pair2+3 for
weld: presence16/24, sealant65/72, seating18/24, weld69/72 =168/192, three falseG
(ambiguous welds) and two wrong presenceR branches. All-three-independent =164/192;
weld alone72/72 with zero falseG. Every two-view weld pair has falseG (69/71/69
correct of72). Whole-batch truth never becomes per-image training labels. .98/.5
unchanged. All27 pilot single-image winners/probabilities exactly reproduce the
earlier probe; same worker hash. Nine original joint3 controls all correct.

192 new JOINT two-image controls preserve primary stage scores24/24,71/72,24/24,
but weld68/72 has four ambiguous falseG (total187/192). Medians882/1061/884/1067ms
for presence/sealant/seating/weld. Two joint NIR views (48/48 development) are a
promising targeted follow-up without assuming new training is necessary, but
need compatible explicit contract/provenance, physical motion and fresh validation.
No new held-out accuracy or two-view rendered integration is claimed.

Coverage reprojects78 sealant samples and6 weld centers across18+18 existing
mounting-frame train/development captures; test split excluded. Sealant pair1+2,
1+3,2+3 fail9/18,18/18,9/18 rows; all3 pass. Example89.06C contact: view1 samples
20C foreground, view2 out of frame, view3 sees89.02C. Weld pair1+3/2+3 passes all
six-center checks in this limited set, but that does not validate classification.
Matching coarse batch labels cannot override missing contact coverage.

Scheduling uses recorded0.962-0.966s inter-capture gaps, actual timed native calls,
one warmup and5 measured trials/mode/stage, rotated/reversed order. Weld pair
already ready: one/twoHTP1364/727ms. Staggered two views:1645/1646ms, no useful
concurrency benefit. Presence ready1106/574ms, staggered1524/1527ms; sealant
ready1469/763ms, staggered1708/1710ms. Current camera motion/settle cadence lets
one single-image worker finish before next input arrives. Three streamed weld
views ready2607ms vs3231ms batch-after-scan, but independent semantics are not
generally validated; prefer streaming encoding/compatible prefill with the
original joint answer for general implementation.

Ministral compiled QAIRT2.50 remains resident onHTP1 alongside Reason2QAIRT2.45;
actual handles confirm both. Following 30 measured weld observations, agent
uses fresh59-token append and244-246ms native decisions, all expected releaseH.
Untimed full-history priming recorded separately. Five compiled instances plus
CPUfallback resident, MemAvailable4622260KiB (~4.41GiB), no swap configured.
These are saved-image/history timing replays with no physical actions, not fresh
live approvals or whole-cycle improvements. No new CPU/GPU comparison needed
to establish the successful NPU co-residency.

All seven reports complete; all ten original source-bundle hashes per adapter
verified unchanged. Private workers exit; original18195/18196/18198 HTTP200.
No production test-suite or rendered pass claimed for the unimplemented contract.

## Streamed joint Reason2 prototype — 22 September 2026

User approved the recommended follow-up. Implemented standalone native
scripts/model/qairt_stream_worker.cpp and reproducible probe_streamed_reason2.py,
documented in docs/streamed_reason2.md. This is offline only, not wired to Unreal
early-image HTTP uploads; the existing live bridge rejects its execution identity.
Keep the live three-view contract, thresholds, adapters and default services.
Do not describe these timing replays as a live deployment or assembly speedup.

Vision-only streaming retains actual per-image main embeddings plus all three
DeepStack levels. Optional --incremental prepares original full-sequence MRoPE
once and prefills only complete AR128 blocks backed by actual arrived pixels.
One 128-token block is available after image2; no invented future pixels,
independent per-image classifications, partial-answer release or new fine-tuning.
Original full-vocabulary greedy/logit comparison and .98/.5 scoring remain.
One bounded batch, 2–3 ordered images, eight-second lifetime, explicit cancellation,
replacement and complete clearing on error/end. Last encoder runtime allocations
remain bounded; no cross-batch feature reuse. Owned concatenated features peak
at 10.5 MiB, excluding KV, text/position buffers and runtime allocations.

Both initial vision-only and incremental builds independently match all192
original/paired decisions, raw winners, token counts and serialized GAR
probabilities exactly. Full arrays were read but not compared/persisted bitwise.
Retain191/192 development accuracy, with the same conservative sealant hold.
Incremental full parity uses private HTP1 clones; all ten source-model hashes
are recorded. Timings use HTP0 after competing inference has finished.

One warmup +5 measured alternating timing pairs per stage, input arrivals at
recorded .963s NIR/weld and .966s sealant gaps: presence3078->2628ms,
sealant3261->2813ms, seating3066->2622ms, weld3249->2803ms from first image
availability. Paired saving446–451ms; vision-only264–269ms. All timing pairs
retain exact GAR probabilities. Early decoder block~181–183ms. Baseline already
uses fixed preprocessing/prefill-only. Excludes initial deployment/network/
live integration/agent time; cooling retry count could change whole-cycle gain.
Earlier schedule-* files overlapped parity and are excluded; use
incremental-schedule-* / vision-schedule-* for cited timing.

Fresh UE capture32episodes seed220922 with Enhancements, camera audit63893checks.
Only26whole train/development episodes'52NIR batches classified; six reserved
test episodes untouched by the model. Original3views52/52; jointspot1+spot2
36/52: presence17/26, seating19/26. All9installed-presence examples regress
(5wrongR,4holds), all7correctly-seated regress(1R,6holds). No falseG but wrong
presenceR chooses the wrong assembly branch. Reject this two-view candidate;
earlier48/48 retained-image score does not generalize. Do not alter physical
scans or relax adapter compatibility. Other possible pairs are not tested here.
Raw actual counterexample images were inspected and shown in chat.

Native lifecycle checks24per variant include duplicates/order/identity errors,
partial-prefix cancellation/replacement, decode failure, unexpected fields,
expiry and fresh-score recovery. Python249tests pass with2optionalSDKskips;
native policy151checks pass; rendered capture exits normally. No streamed live
ProcessSmoke is claimed. Next live integration must bind prepared features to
body/epoch/stage/prompt/capture time and exact final PNG hashes, retain freshness,
and verify pause/backend-switch/turnover cancellation with actual agent+flow runs.
Private evidence: artifacts/streamed-reason2-20260922. Keep demo closed after work.

## September 22 live streaming integration

Supersedes the preceding prototype-only deployment status. Actual PNG uploads now
start as each R4 image finishes encoding. `DarkFactoryProcessStreaming.cpp` probes
`/v1/stream/capabilities` and uses `ordered-three-view-prefill-v1` when the selected
EVK supports it. Host GPU/older services retain final-batch transport. No image,
prompt, camera pose, settling render, stage route or .98/.5/8s/one-batch policy changed.
Exactly three real images still produce ONE joint Reason2 result; two-view remains rejected.

`serve_streamed_reason2.py` requires full paired incremental evidence bound to the
actual native binary and original HTP0 bundle/config hashes. Final native binary
reran all192 pairs on original device/config: identical GAR probabilities, raw top
IDs, prompt counts and decisions, same191/192 reused development score. Native ABI
execution remains named offline-stream-prefill-v1; live HTTP version is separate.
Prepared features bind body/request/session/epoch/stage/prompt/time/PNG hashes.
The unchanged final request repeats the original PNGs, whose hashes must match.
Preparation ACKs cannot release gates; invalid/stale/missing input holds. Pause,
backend selection, body turnover and EndPlay cancel preparation and final requests.
One in-flight upload, one server batch,16sessions/64retired epochs, bounded telemetry.
No failed streamed batch silently falls back to the baseline path.

Deployment is ACTIVE under normal user service dark-factory-edge-qairt on18198,
using runtime/qairt_stream_worker and models/qairt-{primary,weld}/streaming-validation.json.
`start_evk_qairt.sh` autodetects these files, or accepts --streaming/--baseline.
Original native worker/validation remain intact for rollback. Incomplete/mismatched
streaming files fail startup. CPU Ministral18195/one-token proxy18196 remain selected;
this task does not promote the optional NPU agent. No boot autostart installed.
Do not co-load a second full pair on HTP0: FastRPC mapping/PD capacity failed despite
available host RAM. Stop the old automotive pair before loading its replacement.

Actual flow:3departures assembly/preassembled/assembly,16streamed completed gates,
6thermal acceptance checks;2deliberately canceled partial batches. Pause afterimage1
and EVK/GPU/EVK switch after early prefill recovered with fresh images; the switch
fixture does not submit a GPU inspection. Camera audit3114checks.
Actual agent on deployed18198:3departures,26choices,13streamed gates,179.872s,
zero audit failures/rejected calls. Two thermal A responses led to agent-selected
waits and fresh inspections. Agent median1481ms; camera audit2532checks.
All29completed gates overlap first preparation with travel toview2 and second
preparation/128token prefill with travel toview3. Agent finalHTTP medians591/763/591/756ms
for presence/sealant/seating/weld; firstPNG-to-answer2601/2767/2606/2749ms;
scan-start-to-answer3308/3482/3314/3465ms. FinalHTTP excludes early preparation.
No matched whole-cycle speedup, new held-out accuracy or real-camera claim.

HTTP lifecycle probe passes8cases: exact baseline/streamed scores and PNGs, no
partial acceptance, consumed/reordered/canceled epochs, changed/missing images,
incomplete final headers and fresh recovery. Python259tests pass with2optionalSDK
skips; native policy151checks pass. ReadbackSmoke passes exact sync/async PNG parity,
stale rejection and GPU previews with no CPU preview uploads. Private artifacts:
artifacts/live-streaming-20260922. Actual flow welding screenshot shown in chat.
See docs/streamed_reason2.md for live protocol, reproduction and measured scopes.
Final rendered legacy bridge G/A/R/disconnected checks all pass with actual async
readbacks and zero CPU preview uploads. UE5.8 build and git diff --check pass.
Temporary candidate service/tunnel are stopped; production18198/18195/18196 stay
healthy. Confirmed zero Unreal demo processes at completion, as requested.

## September 23 streaming plus split-HTP agent follow-up

User's "continue with 1" selected compiled Ministral on HTP1 alongside the
newly deployed streaming Reason2 HTP0 service. Runtime handles confirm both
Reason2 workers use fastrpc-cdsp-secure and Ministral uses cdsp1-secure.
Same native binaries/configs as reviewed September 21/22; SDK2.45 Reason2 and
SDK2.50.40 Ministral remain in separate process library environments. Candidate
uses append_rewind, original one-token prompt, no next-envelope prefill.

Independent saved-input replay: one warmup+5 samples per workload/mode,
36trials total. Warm agent isolated/concurrent medians282/235ms and streamed
vision2705/2775ms from first image. Full history agent4685/4672ms and vision
2742/2729ms. Three images arrive963ms apart. All24streamed replies (including
warmups) retain exact baseline scores, raw winner, pixels and token counts;
all24agent calls preserve the selected action and verified cache mode. No
large split-device contention observed; small sample variation is not proof
concurrency speeds individual models. Available memory15491MiB, no swap, with
CPU fallback also resident. No power or sustained thermal claim.

IMPORTANT: first actual combined agent run FAILED on body3. Two departures,
27choices,14streamed inspections,156.227s. Agent median269.275ms, cold3836ms;
Reason2 finalHTTP median713.643ms (early preparation excluded). New-body
rewinds retain949tokens and take487/489ms at proxy. Body3 chose inspect sealant
instead of placement, then weld_panel without placement. Evaluator recorded
welding_without_placement and did not veto. Later seating R caused agent hold.
All27actual unmasked tokens match actual model-visible history and dispatch;
camera2726checks pass, no streaming transport/overlap errors. Overall agent,
token and streaming acceptance reports still FAIL due to failed process.

Exact-token full-reset replay of requests18/20/25 reproduces all3NPU choices;
this is not stale KV. At20 NPU picks inspect(.440) over place(.433), CPU picks
place(.541). At25 both choose invalid welding on the already incomplete
history: NPU .727, CPU .655. Thus near-tie backend variation plus a broader
policy weakness. Do not imply CPU fixes all such histories. No prompt edits,
action masks, thresholds, scene-truth repair or fine-tuning introduced.
Do not promote split NPU based on speed or erase failure after later passes.

Added optional -EvkAgentNpu preset/HUD selection and reusable
scripts/agent/start_evk_compiled_agent.sh (native18206/proxy18197, guarded
append_rewind, explicit prepared private dependencies, readiness/occupied-port
checks). CPU-agent default18196 stays unchanged; F4 EVK profile honors NPU
selection when requested. See docs/streaming_dual_htp.md. Private evidence in
artifacts/streaming-dual-htp-20260923. Actual welding/failure screenshots shown
in chat. Subsequent routing/flow completion evidence follows below.

Follow-up actual EVK/GPU/EVK agent routing PASSES:3departures,30choices,
15inspections (11EVK/fourGPU),166.165s, no audits/rejections. Correct18197->18194
->18197agent endpoints, camera2920checks pass. Warm mixed-backend result does
not erase first full-EVK failure or prove matched cycle speedup. Flow lifecycle
passes3departures/16gates/6thermal checks and pause/switch cancellation; camera
has1failure/3114checks: batch7 weld spot1 actual/authored Euler difference
.0001038516deg versus.0001deg tolerance. Exact joints and physical capture/lens
match; no motion or tolerance changes. Preserve exception rather than reporting
clean full QA. Python260tests pass/2optional skips; UE5.8build/nativepolicy151pass.
Optional compiled-agent services and temporary18197/18206tunnel stopped;
original streaming18198/CPU18195+18196remain healthy. CPU remains default.
Final policy fixture on the final DLL passes151checks; zero Unreal processes
confirmed after it exits. No demo or experimental compiled controller left running.

## September 23 canonical named history and HTP reliability

User requested simpler model-visible history, then further reliable Ministral
HTP1 testing alongside Reason2 HTP0. Implemented opt-in `named-jobs-v3` in
single_token.py: explicit job/inspection names, literal ordered completed_jobs,
no body IDs/numerical timestamps in model input, and fixed JSON member order.
Identity/timing remain in actual transport/audits; invalid/stale results retain
valid=false and reasons. Only actual status=completed act results enter the
ledger. No source truth, state-derived permissions/suggested-next-action fields,
recipe veto, action mask or fine-tuning. Still one unmasked token, all13actions available.
Body IDs in private proxy logs are outside model input. The token auditor now
reconstructs actual named messages byte-for-byte, including canonical order.

Policy stays in configs/live_agent.json. Explicit --policy-examples appends
configs/agent_policy_examples.txt; it does not replace the configured business
policy. Launcher env flags DF_AGENT_OBSERVATION_FORMAT=named and
DF_AGENT_POLICY_EXAMPLES select this research candidate. Normal history remains
json and CPU18196 remains default. Candidate prompt hash
becd0677fd2c8334bbc9ac77b2c7792dfcc1893b65d52e7da36c145e167ec65d.

Important finding: initial named-v2 live run FAILED after sealant with M hold,
where equivalent synthetic facts yielded C placement. Only JSON property order
differed. Full-reset replay reproduced M; changing only that order produced C;
canonical v3 also C. Reordered v2 is exactly equal to the synthetic fixture.
All120permutations now normalize identically in regression tests. Do not assume
semantically identical JSON necessarily gives identical LLM decisions.

Named original policy:40/46synthetic development on NPU and CPU; NPU full-reset
retains6failures. Static examples + canonical history:44/46development and
34/34reserved (reserved first evaluated for v2, reused for canonical v3; not new
held-out evidence). Remaining errors: preassembled/weld-G without adhesive
inspection wrongly departs; placement without sealant wrongly permits welding.
Wrong-action probabilities .792/.897, not just near ties. Keep candidate opt-in.
Shorter prompts, flat logs, descriptive labels, extra summaries and a second
one-token self-review did not beat it. Self-review29/46, semantic single-token
commands43/46, explicit missing-result index40/46; these remain private/offline.

Three fresh canonical live runs (seeds42/43/44) PASS process/token/stream audits:
9bodies,84actualchoices,42streamed inspections,zero process audits/rejections/
finalholds. Nine thermal A results led to actual waits/reinspections. Wall times
178.338/178.520/178.466s. Unreal agent median269.153ms;75warm proxycalls median
255.169ms;9new-body resets median5306.556ms. Removing IDs makes new arrival a
shorter exact prefix: guarded native reuse correctly resets it. Added policy
examples also lengthen prefill. Do not claim former0.49snew-body rewind applies;
no guard relaxed. Reason2 finalHTTP median713.684ms excludes early preparation;
firstPNG stage medians~2.54-2.71s. No matched cycle speedup or image accuracy claim.

Camera QA exceptions preserved: seed43 all2726checks pass; seeds42/44 each fail
one authored/actual Euler comparison at weld spot1 (.000203578/.000123168deg vs
.0001deg). Physical capture/lens and exact joints pass; no camera/tolerance edits.
Concurrent actual flow completed3bodies/16gates and passed3114camera checks plus
stream audit while HTP1 probes ran. Actual handles confirm Reason2 cdsp-secure,
Ministral cdsp1-secure; about15GiBavailable, no swap. Native weights/binaries/SDKs
unchanged. Full-reset old failure replays: request20 D->C,25 E->C; control3 C->C,
seating defect27 M->M. Reused regressions, not new held-out scores.

Python274tests pass/2optionalSDKskips; native policy151checks pass; git diff check
passes. Actual welding screenshot shown in chat. See
docs/agent_history_reliability.md and private artifacts/agent-named-history-20260923.
Experimental compiled services and own18197/18206tunnel stopped after testing;
original18198/18195/18196healthy. Confirmed zero Unreal processes at completion.

## September 23 simplified agent default validation

Current prompt hash is f0ad9a8322fa0249991bae38d95b2ebe8a39c57eb8cffd453e8f36bf6504c11f.
Both host 18194 and EVK CPU proxy 18196 use v2 eight-tool policy, named-jobs-v4
history, and one unmasked token. The HTP1 agent experiment remains stopped;
promoting named history does not change the default compute device. Model-visible
IDs/times are removed while transport epochs and freshness remain intact.
UE 5.8 build, 164 native policy checks and Python discovery (280 tests, two skipped)
passed. Actual host and EVK three-body agent runs passed with zero audit errors,
23 choices and 21 actual three-view Reason2 batches each. All host choices matched
raw model token logs; all 21 EVK batches passed the streaming verifier. Separate
flow ProcessSmoke passed three bodies and six thermal acceptance audits.
42 synthetic development cases returned identical GPU/CPU choices: 37 expected,
three unnecessary pre-weld waits and two unsafe reuse-of-invalidated-evidence
choices (thermal before preparation; seating/weld before a repeat weld). Keep
these failures visible and do not insert recipe repair. See docs/agent_demo_scope.md.
Demo closed after verification; normal model services retain the new defaults.

## September 23 EVK-owned agent harness

User requested moving the actual agent harness onto the EVK by default.
`-ProcessControl agent` now defaults to `-AgentRuntime evk`, EVK inference and
the eight-tool v2 profile. Explicit `-AgentRuntime local` retains the previous
Unreal-owned loop; flow remains the default control-mode comparison. The EVK
loads configs/live_agent.json, maintains per-body conversations, calls Ministral
and dispatches its actual tool choice. Unreal is a polling equipment worker:
camera capture/Reason2 streaming, authored motion, HUD and independent audit.
It sends incremental actual arrival/start/result events, never a prompt, schema,
full history, source truth, recommended action or evaluator feedback.

scripts/agent/edge_runtime.py and serve_edge_runtime.py run as the separate
dark-factory-edge-runtime service on EVK loopback18210. start_evk_qairt.sh --agent
starts it; start_evk_tunnel.sh --agent adds forward18210 and reverse18214->host18194.
F4 switches inference profiles while keeping the conversation and dispatch on
the EVK. The default remains Ministral CPU18196 + Reason2 HTP0 streaming18198;
the HTP1 Ministral experiment is still optional/stopped. Runtime uses no additional
model weights. Conflicting custom AgentEndpoint values require AgentRuntime local.

Preserve monotonic event acknowledgements, exact replay hashes, matching actual
starts/results, body/epoch scoping, one active worker and five-second heartbeat
lease. Pause/switch discards unstarted/late choices but retains actual running
atomic jobs and completed observations; partial temperature bundles report only
real results. No recipe veto, automatic cooling/reinspection, substitute action
or inference fallback. Final smoke departure must be acknowledged before exit.
History is in-memory: a service restart mid-body requires a fresh demo session,
never reconstruction from source truth or automatic replay of actuator jobs.
Unreal's local conversation mirror is for audit only. See docs/edge_agent_runtime.md.

Private actual evidence: artifacts/edge-agent-20260923. EVK-only three bodies:
21 decisions,19 streamed Reason2 batches,203.557s,zero process audits/rejections.
EVK/GPU/EVK three bodies:23 decisions,21 batches (8/5/8),211.606s,zero audits or
rejections. Cross-machine audit matches all44 choices and94 equipment events to
actual EVK conversations, aarch64 PID and final acknowledgements. Backend verifier
now recognizes each actual v2 inspection_result (the old version only read v1
inspect tool_result). Known policy failures remain; no new held-out accuracy or
matched speedup claim. Unchanged one-token prompt hash remains
f0ad9a8322fa0249991bae38d95b2ebe8a39c57eb8cffd453e8f36bf6504c11f.

Camera QA: EVK-only3696checks has one panel_seated spot0 Euler discrepancy
0.000130225deg vs0.0001deg; physical capture/lens and exact joints pass. No pose
or tolerance changes. Mixed backend4084camera checks all pass. EVK-only19batches
pass streaming audit. UE5.8 build/native policy169 and launcher52checks pass;
Python discovery303tests succeeds with two optional skips. Normal EVK runtime
restored without private QA logging after both runs; models/tunnels stay ready.
Separate final flow regression passes three departures,14inspections,six thermal
acceptance checks and2726camera checks. Confirmed zero Unreal processes. Actual
welding screenshot showing IQ9 AGENT / REMOTE TOOLS was displayed in chat.

## October 18 observer HUD simplification

The imported demo omits the top-left title/mode/FPS and graphics-status panels,
capture-panel header/footer, flow recipe reminder and observer thermal-scale
captions; keep the actual scales, camera labels and live process information.
All imported observer shortcuts, including F6/F7/F11 and conditional F4/F5/replay
history, live in the bottom help panel. It starts hidden, slides up over 0.3 s
after five seconds without input, and hides on meaningful input. Keyboard/mouse
is the default; only the last used device's bindings are shown. Ignore idle axis
reports/stick drift and keep help hidden while digital inputs remain held.
Use wall time independently of process pause; preserve procedural fixture HUDs.
ADarkFactoryPlayerController observes input without changing sensor/render paths.
The opt-in -DFHUDSmoke rendered fixture tests startup, idle threshold, animation,
held keys, device switching and drift, and closes itself. UE5.8 build succeeds;
303 Python tests pass with 32 optional skips, and all nine rendered HUD checks
pass at 1280x720. Private captures/logs: artifacts/hud-trim-20261018. This offline
UI check performs no model inference; demo closed after validation.

The observer thermal scale is aligned under the right-hand inspection preview
column, above the resting keybinding panel. Reserve its 50 px height plus gaps
when fitting process camera tiles; use the actual wrapped help height even when
help is hidden, so its idle animation cannot overlap the scale.

October 19 startup diagnosis: the manually launched Unreal client waited on
Build.bat QueryTargets behind an unrelated Overland build. Do not interrupt that
other project's build. Both EVK models and WSL tunnels also stopped immediately
after shell exit because user lingering was disabled. Enabled loginctl lingering
for WSL eivho and EVK ubuntu and restarted the existing stack/tunnels. Windows
health checks for 18198/18196/18210 pass and all four EVK services are active.
This is service-manager persistence, not model boot autostart. Startup guidance
is in docs/edge_agent_runtime.md; preserve private model/deployment files.

October 19 follow-up: lingering alone does not keep a WSL VM alive. The VM shut
down at 19:57, removing all transient SSH tunnels while the EVK services and
agent history remained healthy. run.ps1 now owns a hidden foreground WSL sleep
process for live EVK inference, backend switching or an EVK agent harness, and
ensures the needed tunnels before launching Unreal. It releases that keepalive
in finally after the client exits; it does not start model hosts. The reusable
scripts/model/keep_wsl_alive.ps1 can also watch an existing Unreal demo PID.
Applied that watcher to the existing client without resetting the EVK runtime.
After transport recovery, Space twice reconnects a heartbeat-held conversation
through the existing cancellation/epoch protocol. Do not fabricate equipment
events or reconstruct conversation from source truth to bypass the hold.

Also fixed an actual outage-recovery bug: a late completed tool result cleared
cell.held while leaving the heartbeat error. Result ingestion now preserves an
existing hold until explicit resume changes epoch. A regression covers expiry,
late completion, exact replay, no autonomous retry and successful pause/resume.
Deployed only edge_runtime.py (retaining the previous file) and restarted the
harness with a fresh Unreal session; existing model services were not restarted.
Validation: 304 Python tests pass (32 optional skips), 52 launcher checks, 169
native policy checks, actual EVK agent three-body smoke and separate EVK flow
three-body smoke both pass. Flow retains all six thermal acceptance audits.
Private evidence: artifacts/wsl-session-20261019. Reopened the interactive agent
demo through the updated launcher; new EVK session is connected, unpaused, with
no hold/error. Models and demo intentionally remain running for the user.

October 19 F11/default graphics: disabled bF11TogglesFullscreen in project input
and the player controller's runtime settings to override older saved preferences.
F11 now reaches only the existing hardware-RT toggle; the viewport no longer
consumes it as a fullscreen transition. Supported DLSS Quality, Lumen GI and
reflections and hardware RT already default on in visible views, and their
actual startup activation is now checked. run.ps1 also enables the enhancement
pack by default for interactive runs unless explicitly -Enhancements:$false;
automated fixtures keep their explicit selection and original defaults. Infrared
observer and fixed/model capture isolation remains unchanged.
GraphicsSmoke now sends F11 through the real viewport route in all 32 graphics
states, verifies unchanged window mode/size, actual RT preferences, and all four
fixed/R4 sensor PNGs. UE5.8 build succeeds; 304 Python tests (32 optional skips),
52 launcher checks and 2112 rendered graphics/parity checks pass. Reviewed the
bright all-enabled screenshot and displayed it in chat. Private evidence is in
artifacts/f11-graphics-20261019. The offscreen verification demo exited normally.

October 19 taskbar capture: interactive run.ps1 now passes DFTaskbarOverlay by
default, with -NoTaskbarOverlay as opt-out. The standalone Windows observer
centers its initial borderless window against full monitor bounds, preserves
native client size and uses HWND_TOPMOST only while foreground. It drops topmost
when another window takes focus and never forces normal-launch activation.
No exclusive/fullscreen transition, global taskbar changes or sensor rendering
changes. Automated/offscreen fixtures retain ordinary window stacking. Native
HUD verification explicitly requests foreground once only in its visible fixture;
the first unattended attempt correctly stayed non-topmost while unfocused.
UE5.8 build, 304 Python tests (32 optional skips), 52 launcher checks and ten
visible HUD/native-window checks pass. Actual stacking was above the monitor's
taskbar, with unchanged 2560x1440 client and screenshot dimensions. Reviewed and
displayed the full-size image; private evidence is under
artifacts/taskbar-window-20261019/render-focused. Verification demo closed.


October 22 audio review: ElevenLabs effects remain private auditions, with no
Unreal integration yet. First pass has 18 clips; revision 2 replaces seven and
reuses eleven original sources without new requests. Preserve both batches at
assets/audio/review-20261022 and its revision-2 subdirectory. Reproducible prompt
plans are configs/audio_review.json and configs/audio_review_v2.json; authoring
uses scripts/audio/generate_sound_review.py with an explicit local key file.
No credentials or generated audio belong in Git. Keep unknown paid-request
outcomes recorded rather than automatically retrying them.
User direction: softer electric stepper arm travel and precision sounds; cooling
sheet metal should buckle/oil-can; fluorescent starter chatter/flicker should
settle into a stable hum; NIR and thermal activation should resemble old portable
camera flash capacitors charging; extraction should be soft large heavy fans.
Revised robot/fan previews are attenuated 6-8 dB; all seven have previous-take
comparison players and an updated editorial 55-second mix. Source hashes,
format/peak checks and comparison links pass; perceptual approval is pending.


October 22 sound review revision 3 supersedes the stepper-motor direction.
User approved the other takes from revision 2 and requested research-based robot
motors, quieter cooling, half-length NIR and a similar thermal variant.
Manufacturer research supports synchronous permanent-magnet servos and precision
reduction gears; continuous motor/gear whirr is a design inference, not a measured
recording of this asset. References are in docs/audio_sound_design.md.
configs/audio_review_v3.json and the private revision-3 folder retain two new
travel alternatives and a new precision loop (three paid recordings, 154 reported
credits). Cooling reuses its source at -9 dB; NIR is 1.5 s, preserving its source
pitch with a short overlap edit; thermal uses that same NIR source two semitones
lower and also lasts 1.5 s. Thirteen approved preview WAVs remain byte-identical
to revision 2. All prior raw sources are preserved. All 25 audio player links,
format/level checks and both exact cue lengths pass. Still review-only; do not
wire the new motor takes into Unreal before the user's perceptual review.


October 22 sound review revision 4: generated servo takes are still rejected.
The user provided youtube.com/shorts/FtQbSdRHS6s (ABB IRB140), then explicitly
requested extraction from youtube.com/watch?v=idbnX6iUvtM (Eurobots ABB IRB6700).
Extracted the second video's 41-second audio to private
assets/audio/robot-reference-20261022, preserving source metadata and SHA256.
Private revision-4 review has raw/lightly-cleaned versions of 0.5-7.8 s plus
12.0-21.8 s; original robot pitch and speed are preserved. Cleanup is bounded,
not claimed to yield an isolated motor stem. Code/recipe are
scripts/audio/prepare_recorded_robot_review.py and
configs/recorded_robot_review.json. Dependencies are private assets/audio/.tools.
Thermal is now one octave (-12 semitones) below NIR, both exactly 1.5 s; the
approved NIR file is unchanged. All prior revision-3 files are preserved.
Format, peaks, intervals, hashes and all seven review player links pass.
No paid generation calls, no Unreal integration; perceptual review remains open.

October 22 approved audio integration supersedes the review-only notes above.
The user approved the recorded robot extracts and the final effect revisions.
scripts/audio/prepare_runtime_audio.py now packs 17 roles into private
assets/audio/runtime/bank.dfau with source hashes and edit provenance. Runtime
robot travel uses revision-4 recorded_travel_clean, retaining original speed
and pitch; NIR/thermal use the final 1.5-second cues, with thermal one octave
lower. Remaining effects use approved revision-3 previews. No new paid API calls.

DarkFactoryRecordedAudio replaces the procedural pool with six bounded voices
when the private bank exists: four spatial robot/tool sources, spatial carriage,
and centered room/UI. Actual tool translation/rotation drives travel levels;
actual jobs drive sealant, pickup/placement/release, pinch and weld cues. One
shutter follows each submitted settled R4 readback; observer mode changes drive
charging/fluorescent cues. These hooks observe state only and do not affect
model requests, sensor pixels, gates or mechanical scheduling. Legacy fixtures
and code-only checkouts retain procedural audio. Pause/mute silence all layers;
queue epochs reject stale events, including a pause missed by the audio callback.
PCM is immutable; callback-owned playback has no UObject/file/lock access, eight
queued events and four concurrent one-shots per source.

User feedback found the initial runtime mix too quiet. Recorded output now
defaults to +10 dB, adjustable with run.ps1 -FactorySoundGainDb (-12..18).
The per-voice soft-knee bound of 0.11 is before this output gain. The rendered
native audio fixture plays all 17 roles, checks stale-event rejection, bounded
overload and exact muted silence, and records the actual Unreal master mix.
At the new default that recording measured peak 0.1702, RMS 0.02671 and zero
clipped samples; this is fixture evidence, not a guarantee for every overlap.
Build, 304 Python tests (32 optional skips), 52 launcher checks, 169 native
process policy checks, 681 legacy realism checks and a real three-body EVK flow
run pass. Private verification is under artifacts/audio-integration-20261022.
The initial agent run was explicitly exited after two bodies and is preserved
as interrupted evidence, not counted as a pass. See docs/audio_sound_design.md.
The final offscreen EVK agent run also passes with the +10 dB mix: three bodies
in 205.2 seconds, zero agent audits/rejected calls, 57 actual captures matched
by 57 shutter cues, and no dropped audio events. The QA demo exited normally.
verification-summary.json records both live runs and the native master levels.

October 22 fluorescent/welding timing revision supersedes the initial audio
bank edits. The private bank now has 20 clips: three shaped current variants
and a separate pinch-jaw release, while retaining the six-source pool, +10 dB
default and all source recordings. No additional API generation was needed.
The old seven-second fluorescent audition is pitch-preservingly compressed to
1.5 seconds. DarkFactoryFluorescentTiming.h contains the unchanged six-bank
visible strike curve; it also bakes the startup sound envelope at bank load.
The packer reads the same duration. Reject mismatched bank durations at load.
Switching away from roof-lit mode cancels the sound with a short smoothed fade; a separate
roof generation rejects stale queued starts and supports a clean retrigger.
Keep the visible light outputs and all sensor isolation unchanged.

Recorded foley now follows the authored 150 ms squeeze, 500 ms current, 80 ms
hold and 120 ms jaw release. Alternate three crackle excerpts across six spots,
with explicit pulse attacks/decays and a restrained illustrative 100/300 Hz body.
Start the existing subdued cooling cue after final jaw release. Do not treat
these sounds as measured gun acoustics, weld quality or thermal readiness.
The private dry weld_sequence_preview.wav was shown in chat for audition.
Native audio checks pass for all 20 roles, roof cancellation/stale rejection/
retrigger, bounded output and exact muted silence; the recorded mix has no
clipped samples. UE5.8 build, 304 Python tests (32 skips), 52 launcher checks,
169 policy checks and the full legacy WeldingSmoke pass. The rendered lighting
verifier passes with unchanged fixed NIR/thermal images. Private evidence is
under artifacts/audio-timing-20261022; see docs/audio_sound_design.md.
Both live EVK flow and agent runs pass three bodies with 12 squeeze cues, 12
current pulses (four of each variant) and 12 jaw releases. No audio drops;
the agent has zero audit failures/rejected calls. The lighting verifier passes
2269 checks. Acquire published queue entries before sampling their generation
controls so a newly queued roof startup cannot be rejected against an older
counter snapshot. This callback ordering is covered by the final native check.
The final 20-clip native run passes with peak 0.15005, RMS 0.02650, zero clipped
samples and an exactly silent muted tail. All QA demos are closed.

October 22 observer elevation: Q/E and the shared Xbox trigger axis now use
exponential input smoothing (0.10 s acceleration time constant, 0.0625 s braking
on release), retaining the existing maximum flight/boost speeds. Home, valid
bookmark selection and SetCaptureAutomation clear residual elevation input;
raw manual input still cancels bookmark travel immediately. Fixed/R4 cameras
and process motion are unchanged. UE5.8 build, 304 Python tests (32 optional
skips) and the rendered 681-check RealismSmoke/observer-bookmark and fixed-sensor
parity regression pass. Evidence: artifacts/elevation-smoothing-20261022.
The QA demo closed normally; subjective movement feel remains a user review.
