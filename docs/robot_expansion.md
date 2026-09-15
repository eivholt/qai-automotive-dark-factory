# Independent robot timing, controller props and observer infrared

The imported welding preview now runs four robots on separate, repeatable schedules. Each robot waits 0.2-0.8 seconds before leaving rest, performs the existing one-second approach, waits another 0.5-2 seconds at the solved contact pose, welds for the full 7.5 seconds, then withdraws over one second. The delay generator uses the scene seed, stable robot identity and a 16-cycle bank. It repeats after 336 seconds; it is authored variation rather than unpredictable factory control.

The default body cycle is 21 seconds. The body arrives during 0-3 seconds, remains stationary while the robots work, departs during 16.3-19.8 seconds, and leaves a 1.2-second empty interval before replacement. Even the latest permitted robot finishes withdrawing by 15.8 seconds, leaving at least 0.5 seconds before departure. These timing margins do not certify swept-volume collision safety or industrial robot trajectories.

Each emitter retains its original robot index when only a subset is welding. Sparks, arc light, smoke, tool heat and the associated body spot use that robot's local weld age. A robot waiting at the surface must not emit or heat its weld prematurely. Tool and motor warmth can persist across body cycles; each new body's weld fields start cold. Motor heating still follows joint activity during approach and withdrawal.

Existing rendered cooling, choreography and effects fixtures explicitly select the synchronized 18-second QA schedule. Those independent regressions remain useful, but their results do not by themselves validate the new default timing. The appended expansion stage restores the default schedule and checks it separately.

## Process station arrangement

The panel process exchanges the two left station functions while retaining logical R2 as welder and R4 as inspector. R2 occupies (-301.093104, -294.770226, 175.584213) cm, 50 cm inward from the former rear-left station; R4 occupies the former welding position at (-240, -100, 175.584213) cm. Their fixed bases carry the existing EVK props. Physical pedestal offsets are independent: the former R2 pedestal supports R4, and the extracted R4 pedestal supports R2. The source grouping now has 91 components plus the separate rack H-bar copy.

R2 uses the original 0.7-scale gun directly on its native mating cap, without a generated rod or extra flange. R4 retains one rigid camera mount, rotated +60 degrees about tool-frame Y with the same (0, 0, -24) cm translation; refitted joints preserve world lens targets. Logical identities, image order and the legacy visual-preview choreography remain unchanged. New rendered motion, sensor and model checks are pending for this arrangement.

## Four mounted IQ9 props

Four small IQ9 EVK appearance props attach to the outward raised junction-box covers on the robots' fixed bases with mounting plates. They reuse the earlier project's privately supplied five-face photographic appearance. The enclosure dimensions are approximately 10.6 x 10.6 x 4.3 cm; this is a visual prop, not an electronics CAD reconstruction. Its fixed parent-relative position and rotation follow process-only base relocation while remaining stationary through arm-joint motion. The private mount recipe is `fixed-base-outward-junction-cover-v2`; stale upper-arm manifests are rejected. Each source cover is approximately 10.06 by 19.38 cm, and the 13 cm plate overhangs laterally by about 1.47 cm per side. R3 mounts 6 cm higher on that same cover to clear the stored panels; its plate has 2.81 cm of top overhang, with the lower support region on the flat cover. Sampled support and non-penetration rays establish an illustrative visual placement, not an engineered mounting specification.

Each prop has two steady green LEDs and an authored irregular amber activity LED. The LEDs are decorative, not live EVK telemetry, inference status or actual network activity. Their twelve small local lights and twelve emissive points are bounded. The four props add 28 opaque thermal surfaces: illustrative 30 C enclosure/faces and 24 C mounting plates. They do not participate in a calibrated hardware thermal model. The imported scene still cannot send images to the old inspection prompt or generate four-module training labels.

Photographic textures, mounts, generated Unreal packages and closeups remain private under ignored asset/artifact directories. The repository retains reproducible code and tutorial instructions.

After preparing the welding-cell source groups, regenerate the mounts with a Python environment containing NumPy and the privately supplied reference photographs:

```bash
python scripts/assets/prepare_iq9_evk.py --reference-repo ../qai-physics-reasoning
```

The generator retains photograph hashes and checks the complete bracket/enclosure boxes against the relocated rack and all five stock panels. Rebuild generated Unreal content with `unreal/Scripts/build.ps1` when setting up the prop materials for the first time. Later mount-only changes are read from the private manifest on the next launch.


## Separate infrared sources and FPS

The fixed NIR camera keeps its stationary illuminator and view-lighting channel 1. Observer NIR uses a separate spotlight attached to the viewing camera, at local position (15, 0, -6) cm, with a 1,500-lumen authored intensity, 24/44-degree inner/outer cones and 2,400 cm attenuation radius. It uses view-lighting channel 2 and emits only in observer NIR mode. Turning or flying the observer therefore moves its infrared beam without moving the sensor illuminator.

These are Unreal view-lighting channels, not the independent primitive lighting-channel flags. Shared indirect, reflection, volumetric and translucent light paths stay disabled for the observer lamp. Visible and thermal views disable it; the visible headlamp retains its dim-view preference, and bright factory mode retains its own lighting. Shared weld emissions remain visible in the relevant channels. Fixed raw sensor images must remain identical when the observer beam or pose changes.

The upper-left HUD displays FPS in every observer mode. It measures real elapsed time between game frames, smooths frame duration with a 0.5-second time constant, then takes the reciprocal. It continues updating while the cell animation is paused. This is a live frame-rate indicator, not a GPU timer, model latency metric or performance guarantee. It never appears in the fixed sensor PNGs and introduces no image readback or CPU preview upload.

## Rendered verification

Run the local scene smoke and the Pillow verifier:

```powershell
.\unreal\Scripts\run.ps1 -Scene welding -WeldingSmoke -Output artifacts\robot-expansion
python scripts/verify_robot_expansion.py --directory artifacts/robot-expansion --output artifacts/robot-expansion/robot_expansion_validation.json
```

The appended `robot_expansion.json` records all 16 schedules for four robots, dense boundary/mid-weld probes over two cycles, six production screenshots, four observer-light isolation states and four prop closeups. `scripts/verify_robot_expansion.py` checks:

- Stable robot IDs and indices, varying bounded delays, complete one-second approach/withdrawal and 7.5-second weld intervals, correct phase transitions and the minimum departure margin. Structural before/after seek snapshots retain exact keys, list ordering and nonnumeric values, with at most 1e-9 absolute numerical transform roundoff. Timing objects, authored joint angles, temperatures and heat clocks must remain exactly equal.
- Exact expected active-emitter sets at every probe, including non-prefix subsets, and pulse ages derived from each robot's weld clock. Empty emitter output cannot pass while a robot should be welding.
- Cold first-cycle tools before their own weld, cold replacement-body spots before their own weld, mid-weld heat, bounded temperatures and cooling after withdrawal. Later-cycle residual tool warmth is allowed. Motor castings warm during approach before welding.
- Four mounted props with unchanged parent-relative transforms and stationary world positions during arm-joint movement, bounded LEDs, a changed amber activity pattern and persistent green indicators. Closeup PNGs must be nonblank; manual review establishes that the intended props are actually visible.
- Camera-relative observer illumination, fixed illuminator position, correct mode/channel state, positive FPS and increasing frame samples while scene pose remains fixed.
- Exact fixed NIR and thermal PNG byte parity across home light-on, same-view light-off, moved observer light-on and visible observer mode. The fixed NIR GPU output and encoded raw PNG must be exactly grayscale.

The two observer NIR image comparisons exclude the HUD using crop (32, 135)-(992, 711). Light-on versus light-off, and home versus moved view, must each differ by at least one mean channel code, with at least 1% of pixels changing by three codes or more. These modest thresholds are declared before final rendered validation; a metadata-only light toggle is insufficient.

## Historical upper-arm results (12 September 2026)

These results describe the former upper-arm mounts and precede both the fixed-base placement and the left-station swap. The full native run in private `artifacts/robot-expansion-final` passed all nine independent verifiers on 2026-09-12, totaling 12,261 checks with no lowered image thresholds. The Unreal build and 34 Python unit tests also passed.

| Verifier | Passed checks |
| --- | ---: |
| Default robot expansion | 5,835 |
| Canonical production cycle | 214 |
| Welding effects | 360 |
| Tool cooling | 129 |
| Body weld heat | 352 |
| Imported thermal fields | 1,599 |
| Factory lighting | 2,269 |
| Robot choreography | 348 |
| Motor heat | 1,155 |

The schedule bank contained all 64 robot/cycle combinations. Measured rest delays ranged from 0.20059 to 0.79945 seconds; pre-weld dwells ranged from 0.53144 to 1.97439 seconds. Weld starts ranged from 5.46978 to 7.23736 seconds, and the latest withdrawal ended at 15.73736 seconds, leaving a minimum measured departure margin of 0.56264 seconds. All 84 timing probes over two cycles passed complete/sparse emitter mapping and per-robot tool/body/motor heat checks.

The independent seek comparison found a maximum numerical transform difference of 4.12115e-13, below the 1e-9 tolerance. All timing objects, authored angles, temperature values and heat clocks were exactly equal. Image comparisons still require exact bytes; they do not use this numerical transform tolerance.

Fixed NIR and thermal PNGs matched byte-for-byte across all four observer conditions, and each fixed NIR source render had zero RGB channel spread before encoding. The observer light on/off crop changed by 21.732 mean channel codes with 47.45% of pixels changing by at least three codes. Moving the observer changed 14.885 mean codes and 48.92% of pixels. Thus the observer beam changes the observer image while both fixed sensors remain stable. Frozen prop tests pin LED time before changing pose and retain stable light render proxies; a transient LED update must not contaminate a sensor comparison.

All four props kept their local attachment transforms and moved with their arms. The two green LEDs remained on, while two amber states changed between sampled activity times. Revised closeups used settled bright-factory lighting, headlamp off and a temporary -2 exposure bias; board photographs, mounting borders and LED glow are visible without the earlier near-headlamp clipping. This QA exposure adjustment does not change live observer settings. The FPS sample counter advanced from 5,364 to 5,479 while the machine pose stayed frozen. Its values include deliberate CPU-heavy offline replay and are not a steady-state performance benchmark.

The aggregate is `artifacts/robot-expansion-final/all_verifiers.json`; per-suite reports and actual screenshots remain in that same ignored directory. The 28 added prop surfaces account for the increased per-surface field/motor check counts. Canonical 18-second fixtures and default 21-second timing have separate evidence; neither substitutes for procedural capture/bridge checks.

## Fixed-base mount verification (14 September 2026)

This evidence predates the left-station swap and direct gun/camera refit. The `tool-refit-legacy-v2` run passes 6,219 expansion checks and 12,645 across all nine welding suites. Unlike the historical upper-arm placement above, all four props remain fixed through joint motion while following their base's process relocation. Each cover has nine sampled support rays and 81 mounting-plate clearance rays; minimum sampled gap is 0.29996 cm. Photo hashes, decorative LEDs and 28 thermal profiles remain unchanged. The separate bright process closeups in `tool-refit-motion-v4` show the requested outward cover placement. These are visual mounting checks, not an engineered enclosure or live EVK telemetry.


## Direct EVK mounts and six-weld layout (14 September 2026)

Current props omit the backing plate. The enclosure itself sits 0.30 cm in front of the measured outward base cover; source photos, decorative LEDs and the R3 vertical clearance adjustment are retained. Four enclosures plus their five image faces use 24 thermal surfaces. The reproducible mount recipe is `fixed-base-direct-cover-v3`; older plate clearances and 28-surface measurements above describe the earlier prop. The current six-weld render includes close-ups of all four bare mounts.
