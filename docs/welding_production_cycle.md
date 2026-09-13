# Repeating welding-cell preview

The local welding cell runs a coordinated visual production cycle. It is not yet connected to a welding-specific model contract or inspection gate. The procedural reference scene retains its separate test cases and mock inference workflow.

## Observer

Mouse vertical look is inverted; the controller's pitch direction is unchanged. Movement is 900 cm/s, with Shift boosting it to 2700 cm/s. Tab / Xbox Y cycles dim visible, NIR, thermal and bright factory; K / D-pad left cycles observer thermal palettes. The fixed panel keeps NIR, iron-color thermal and monochrome thermal together.

The observer carries a shadowed headlamp in dim visible mode. L toggles its preference; bright factory suppresses emission and ignores L, then restores the preference when returning to dim mode. See [factory lighting](factory_lighting.md) for the independent overhead banks and startup behavior. Its direct illumination uses only view channel 0, with indirect, volumetric and reflection contributions disabled. It is disabled for legacy calibration automation. Fixed NIR uses channel 1; thermal measures the authored surface-temperature proxy independently of RGB lighting.

## Cycle

| Time within the 18-second cycle | Action |
| --- | --- |
| 0-3 s | A body moves from the entry tunnel to the fixture; arms remain withdrawn. |
| 3-3.5 s | The body settles while the arms remain parked. |
| 3.5-4.5 s | Arms sweep through three raised, rolled approach poses before aligning with the seams. |
| 4.5-12 s | Small precision movements and pulsed welding arcs produce sparks. |
| 12-13 s | Arms reverse the approach poses and return to their withdrawn, 180-degree base positions. |
| 13-13.5 s | Arms remain parked before the carrier departs. |
| 13.5-17 s | The body moves through the exit curtain. |
| 17-18 s | The station remains empty for one second before the next body arrives. |

The next car starts arriving at 18 seconds, four seconds sooner than the earlier 22-second loop. Welding, withdrawal and departure keep their existing timing.

Space / Xbox View pauses the complete scene motion, including spark aging and curtain simulation. Each loop increments a body index and reuses the same local mesh components after hiding the previous body behind the tunnel; this avoids continuously creating actors or assets.

The 29 body pieces and two complete holder assemblies share a carriage that travels +/-1800 cm along the Y lane. The holders include their wheel, drive and fastener geometry; stationary rail geometry stays in place. Eight sparse car-shell colliders and two holder-box proxies push the curtains. The original static meshes handle detailed spark queries. Offline inverse kinematics adjusts the source working pose to place each electrode 0.6 cm above an actual, top-visible body surface. Five solved joint poses follow a 2 cm seam; the preparation step checks the interpolated physical tip against the original body triangles. Each approach and withdrawal lasts one second, compared with 1.5 seconds previously. Three intermediate poses make the arms lift and roll through a larger sweep, with eased transitions into the original rest and welding poses. Withdrawal reverses these intermediate poses, starting from the last solved seam point. Parked base angles are -180 degrees for three arms and +180 for the fourth, choosing the direction from sampled lane/fence clearance. Five joint-anchored spline sections per robot replace the original rigid hoses at runtime. The original purchased meshes are retained unchanged.

The `dramatic-approach-v1` profile uses normalized key times 0, 0.20, 0.56, 0.80 and 1 within the one-second approach. These correspond to elapsed 3.5, 3.7, 4.06, 4.3 and 4.5 seconds. Every segment uses quintic easing with zero endpoint velocity and acceleration. Withdrawal traverses the same intermediate poses in reverse during 12-13 seconds. Signed base angles are interpolated directly so the intended 180-degree turn is retained. The five solved precision-welding poses and their body-surface targets remain separate from this approach choreography.

The first raised pose uses shoulder 65 degrees, elbow -35 degrees and tool roll 65 degrees. The second extends the lift to shoulder 95 degrees, elbow -15 degrees and tool roll 95 degrees. Forearm roll and wrist are zero in both poses. The base retains 85%, then 30%, of its signed rest-to-work angular difference. At normalized 0.80, each robot reaches its own solved hover position 100 cm above the seam midpoint, with the distal rolls already aligned to their working values. The last segment lowers the electrode to the unchanged seam endpoint. This keeps the larger movement in the raised approach while retaining the short precision weld.

A source-geometry audit sampled 101 poses per robot before rendering. The selected approach increased sampled tool travel to 8.33-8.87 m, from 4.34-4.63 m on the earlier path, and reached tool heights of 4.42-4.55 m. The highest sampled robot mesh point was 5.45 m, below the 7.10 m fixture level. Conservative sampled fence and early car-lane margins were at least 62.35 cm and 94.54 cm. These are transformed source-geometry measurements; sparse self-proximity checks do not establish mesh-intersection clearance.

These are authored demonstration trajectories. The geometry checks do not establish certified robot motion, tool accuracy, full self-collision clearance or a real welding process.

## Curtains and hidden entrances

Two portals align with the measured source rail mouths at Y=-1204.08264 and +1219.15393 cm, X=4.505665 cm. Their jambs stand on the measured tiled floor at Z=6.3477106 cm. The openings remain 420 x 390 cm; opaque tunnels extend 1100 cm outward. Two rail continuations per tunnel match the source gauge and rail-head elevation. The central six strips end 2.3 cm above the raised rail bed, while the outer strips end 8.3 cm above ground; the native tunnel floor is recessed 0.2 cm to avoid coplanar overlap with the source tiles. Each curtain has 14 strips made from three hinged rigid segments: 42 Chaos bodies and 42 constraints per portal. Gravity, low-friction contact, damping and weak hinge drives make them deflect under the moving body and settle afterwards. The imported scene requests 120 Hz physics substeps with a maximum of 12 per frame; the shared in-memory override restores previous settings when the last participating world closes. Each body uses 32 position and 12 velocity solver iterations, with nonlinear hinge solving and no corrective teleport projection. Adjacent strips ignore each other's collision to bound constraint/contact cost; they collide with the carriage and static supports.

The shrouds hide the offscreen body reset. Frame, curtain and tunnel geometry has explicit ambient thermal profiles and blocks thermal visibility. No motion is prescribed to make the strips swing: the car collision drives their movement. This follows Unreal's constrained-body approach to hanging objects. [Epic physics components documentation](https://dev.epicgames.com/documentation/unreal-engine/physics-components-in-unreal-engine)

## Sparks, lighting and cooling

`DarkFactoryWeldingSparks` owns one bounded physical particle simulation shared by every camera. It emits up to 80 particles per second per tool (320 across the cell), five times the previous rate, and caps the pool at 1024 particles. A fixed 90 Hz integration step applies gravity, drag, sphere-swept world collision, restitution and tangential damping. Collision uses a 1.2-2 mm grain radius; the larger visible glow is separate from that physical shape. A new fragment ignores only its emitting torch for the first 100 ms so it can leave the electrode envelope; car and surrounding surfaces remain collidable throughout. Settled particles store the hit component and a local contact position, so a landed particle follows a moving surface while cooling and fading.

Sparks and arcs originate at those surface contact sites. Rendering uses GPU-instanced opaque mesh particles, grouped into 12 authored temperature ranges. Per-instance data fades their emissive material; elongated instances show short airborne streaks. Four arc meshes and four bounded light sources illuminate nearby visible and NIR surfaces. Their arc intensities stay unchanged; residual aggregate light is normalized so the denser spray does not increase scene illumination fivefold. The thermal shader reads the particle/arc stencil profiles, so hot fragments remain detectable as they cool even when their visible glow fades. The raw thermal window remains 20-120 C and hot sources saturate it; temperatures are a stylized proxy, capped at 500 C in the profile table, not a calibrated spectral welding measurement.

The same thermal camera render still feeds both thermal palettes. Sparks add GPU geometry and CPU physics work, but no camera, live image readback or CPU image upload. The actor updates after Chaos; NIR and thermal captures follow its completed state. Scene-camera position does not determine spark collisions. Epic documents the visibility limitations of depth-buffer particle collision and the separate hardware requirements of experimental GPU ray-traced collision; the bounded world-query approach avoids those dependencies for this multi-camera demo. [Epic particle collision documentation](https://dev.epicgames.com/documentation/unreal-engine/gpu-raytracing-collisions-in-niagara-for-unreal-engine)

Sparks leave fading hot particles at contact points. They do not yet inject energy into the production cell's bulk material heat-transfer solver. The procedural heat-transfer model remains available separately until the imported body's wall thickness, contacts and heat sources are authored.

Each welded car contact also leaves a localized surface heat field. Four spots on three panels glow in visible/NIR and retain a cooling thermal signature after withdrawal. The marks move with the body and reset when its replacement enters; they do not enable the imported bulk heat solver. See [car-weld surface heat](welding_body_heat.md) for parameters and rendered checks.

Twenty-eight warm robot castings use authored mesh-local thermal gradients that rotate with their links. Their source temperatures remain fixed during the visual production loop; these gradients do not enable bulk heat exchange. See [imported thermal fields](imported_thermal_fields.md) for the recipe and rendering limits.

The welding tools also retain heat after the arc stops: their localized visible/NIR glow fades while the thermal tip and housing cool. The shared state pauses with motion and carries residual heat into subsequent cycles. See [tool cooling](welding_tool_cooling.md) for parameters and same-pose image checks.

## Regeneration and checks

Build and regenerate the code-created materials with:

```powershell
.\unreal\Scripts\build.ps1
.\unreal\Scripts\run.ps1 -Scene welding
```

The existing asset preparation recipe also regenerates the cycle metadata. `prepare_welding_cell.py --approach-only` applies the authored approach poses and solves the raised hover using the existing prepared rig; it preserves imported mesh payloads and all five solved welding knots. Its reusable helper is `update_approach_choreography(scene)`, which `update_park_cycle(scene)` also invokes. `prepare_welding_cell.py --cycle-only` recomputes cycle metadata and the welding IK knots for already prepared geometry without reimporting FBX. Use the smaller approach-only update when changing just this choreography. Source assets, generated geometry/materials and all rendered diagnostics remain ignored by Git.

`run.ps1 -Scene welding -WeldingSmoke -Output artifacts\production-cycle` exercises a complete live cycle and a following body, captures phases and modalities, records curtain contacts/constraint state and spark bounce/rest/cooling, and freezes the complete scene for byte-level NIR/thermal headlamp and roof-light isolation checks. Run `python scripts/verify_production_cycle.py --directory artifacts/production-cycle --output artifacts/production-cycle/qa.json` to check the report, raw sensors and rendered HUD together. The verifier requires an 18-second period, a hidden-body snapshot during the 17-18 second empty interval, and a visible second body arriving before the former 22-second restart. Inspect the JSON report as well as the process exit code. Legacy model/training guards remain part of this check.

`WeldingSmoke` also exports `robot_choreography.json`, nine 1600 x 900 bright-factory approach screenshots and a withdrawn screenshot. The 107 native pose pairs sample the approach and withdrawal at 0.01 normalized intervals, plus offsets of +/-0.0001 around each intermediate knot. Tool and body heat and sparks are disabled only for these frozen visual comparisons. Run the additional verifier without changing the existing production criteria:

```powershell
python scripts/verify_robot_choreography.py --directory artifacts/production-cycle --output artifacts/production-cycle/robot_choreography_validation.json
```

It requires all 24 joints to stay within their declared bounds, hit the authored knots and unchanged first/last weld endpoints, and reverse the same intermediate transforms through normalized 0.80. It checks fixed car/holder geometry, the 18-second period, and model/training guards. Internal knot pairs must differ by no more than 0.02 degrees and 0.1 cm at the tool; dense angle steps must respect the easing derivative bound. Each robot must travel at least 750 cm along its sampled tool path, reach Z=400 cm, and move at least two non-base joints 15 degrees beyond their rest/work endpoint range. These are visual-motion acceptance thresholds, not equipment limits or collision certification. A HUD-free image crop must show at least 1% of pixels changing by five intensity codes between rest, raised and working views. Inspect all captured poses as well as the numeric report; the finite sample set cannot exclude collisions between samples.

Physics parameters and emitter density are deliberately bounded. GPU time, CPU collision cost and stability should be profiled again when adding more cells or changing target hardware.

## Rendered dramatic-approach validation (2026-09-12)

The `artifacts/robot-choreography-v1` render passed all 348 choreography checks. Its 107 native pose pairs measured approach tool paths of 8.33-8.87 m and peak tip heights of 4.42-4.55 m, matching the source-geometry audit. All 24 joints reached the authored knots and original welding endpoints within tolerance; intermediate reverse poses matched, and the internal knot checks found no position discontinuity. The nine bright-mode approach frames and withdrawn frame were visually reviewed. Rest-to-raised and raised-to-working comparisons changed 19.0% and 15.8% of the scene crop by at least five image codes.

The same rendered run passed production (208), tool cooling (129), car-weld heat (352), casting fields (1,459) and factory lighting (2,269) checks. The Unreal build and 34 Python unit tests passed. The prepared scene's mesh records, cameras, joint pivots/rest/welding angles and emitter targets matched the pre-change metadata exactly. Rendering used an isolated preview manifest whose 24 approach arrays matched the final prepared scene with zero numeric difference.

The preparation helper passed replay/idempotence, preservation and invalid-input checks. A separate source audit found no line-segment crossings against actual car-panel triangles for any of the eight sampled electrode approach/withdrawal paths. Floor/fence/early-lane extrema and selected nonadjacent-part proximity were also checked. These finite electrode and vertex samples do not certify the complete swept volume of each gun, hose or link. Source assets, diagnostic manifests and images remain private under ignored `assets/` and `artifacts/`.

## Earlier 22-second-cycle validation, before the layout and density update

A rendered 1600 x 900 run completed the full cycle and began the next body. The production verifier passed all checks, including 74 imported meshes, 24 joints, 29 carriage panels, four electrode contact sites and 210 thermal profiles. The sampled working pose kept each tip approximately 0.6 cm from its corresponding surface; offline interpolation checks measured 0.48-0.62 cm against actual body triangles.

The final run recorded 176 live particles during welding, including 51 airborne, with no pool overflows. Particles bounced, rested and expired after cooling. The two curtains' maximum recorded hinge-anchor separations were 0.37 and 0.54 cm. Fixed NIR and thermal PNGs remained byte-identical across the frozen headlamp/roof-light and observer-mode changes. Iron-color and monochrome thermal previews remained visible together. The existing procedural NIR, thermal, spatial-field and heat-transfer regressions also passed (20, 42, 55 and 138 checks).

These are observations from one local rendered run, not performance or reliability guarantees across hardware. Re-run the smoke and image verifier when changing geometry, timings or physics settings. Captures and detailed diagnostics remain private under `artifacts/production-cycle-v3/`.


## Earlier 22-second-cycle validation of the layout and density update (2026-09-12)

The updated 1600 x 900 rendered cycle passed 184 production checks and 129 same-pose tool-cooling checks. The scene contained 76 imported components, 24 joints, 29 body panels, two carriage holders and 216 thermal profiles. Both holders retained their local transforms throughout arrival, welding and departure, then hid with the body. All four parked bases measured 180 degrees; the working poses retained the existing electrode contact checks.

The final cycle reached 898 live sparks, with a peak of 258 airborne, zero pool drops and no surviving particles at the empty-station check. The spark actor's measured live CPU work averaged 0.512 ms per tick and peaked at 3.823 ms through the welding sample. This includes pre-weld empty ticks and rendering-instance updates, but excludes GPU completion, other actors and offline QA advances; it is not a frame-time benchmark.

Entry and exit curtains recorded 463 and 709 contacts. Peak hinge-anchor separations stayed below 0.63 cm, and the curtains returned near rest after departure. Rail joins were checked against the original source X coordinates and elevation, including outward continuation lengths and positive body clearance from both tunnel back walls. Close-up visible captures confirmed resting strip clearance over the rail bed. Alignment shots reset only the already-frozen diagnostic curtains after the dynamic cycle and cooling comparisons are complete.

Frozen fixed NIR and thermal sensor PNGs remained byte-identical across observer lighting and modality changes. Visible, NIR and both thermal palettes retained tool cooling. The Unreal build and 34 Python unit tests passed. Detailed images, JSON reports and CPU counters are private under `artifacts/layout-update-final/`; FBX round-trip geometry checks are described in the asset-import tutorial.


## Validation of the shorter repeat (2026-09-12)

The 18-second cycle passed 208 production checks, 129 cooling checks and 34 Python unit tests; the Unreal build passed. The first body and both holders were hidden at elapsed 17.55 s, and the next body was entering at 19.24 s with body index 1. Curtain contacts and bounded hinge error, frozen sensor parity, and continuous tool cooling across the earlier cycle wrap passed. Captures and reports remain private under `artifacts/faster-cycle/`.
