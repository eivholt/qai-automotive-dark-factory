# Pinch welding and continuous sealant contacts

The process gun now approaches across the panel edge, with opposing electrodes
above and below the rendered sheet stack. Its original source geometry supplies
the chassis, bearing pins, lower clevises, rods and electrode caps. The two jaws
open for travel, squeeze before each current pulse, hold briefly and release
before moving to the next contact. There are still six weld sites.

Industrial servo spot guns use several arm configurations: ABB describes
robot-carried X-type variants in its [GWT X9 documentation](https://www.abb.com/global/en/areas/robotics/solutions/functional-modules/spot-welding-functional-module/gwt-x9-spot-welding-gun),
and Comau offers C and X chassis in its [compact gun family](https://www.comau.com/en/our-offer/systems/body-in-white-manufacturing/products/compact-welding-gun/).
The demo adapts the visible hinges in the supplied mesh; it does not reproduce
either manufacturer's calibrated mechanism, force controller or electrical model.

## Source geometry and animation

Process bodies omit the rear roof crossmember `SM_BodyPanel_0385`, which
intersected the welder during the six-contact sweep. `InitializePanelAssembly`
hides that component and disables its mesh queries; body visibility changes
preserve the exclusion. The roof curtain collider is refitted to the remaining
roof sections in carriage-local coordinates. No source FBX, mesh grouping,
material, robot pose or legacy body geometry changes. Private capture metadata
records this body variant as `rear-roof-open-v1` inside `panel_assembly`.

The rendered motion fixture includes matching visible/NIR/thermal rear-roof
closeups and a final-weld closeup. `verify_pinch_welder.py` checks that the mesh
stays hidden and non-colliding across both body cycles, and that the shortened
roof collider stays clear of the removed crossmember.

`scripts/assets/prepare_pinch_welder.py` copies and partitions the original gun's
43,422 triangles into three auxiliary meshes. The original source assignments,
FBX, UVs, normals, material links and legacy whole gun remain intact. All pieces
retain the source gun's local coordinate frame. Runtime hinge transforms rotate
about the measured bearing pins, rather than the mesh bounds center.

The native attachment cap, positive 0.7 scale and direct wrist mount remain;
there is no extension rod. Process mode hides the original whole gun and draws
its three parts: two additional mesh draws, unchanged copied triangle count,
two small rigid transforms per update, and no per-frame geometry generation.
The existing world-space heat field and material instances are shared, keeping
tool cooling in visible, NIR and thermal. Sparks still originate at the exposed
sheet contact and share their physical particle state across views.

The authored open angle is 14 degrees per jaw, producing about 14.75 cm between
the contact centers. Closure uses about 1.17 degrees and a 1.2 cm rendered stack
gap. These dimensions fit the supplied demonstration geometry, not a measured
sheet gauge. Each weld gets 0.15 seconds to squeeze, a 0.5-second current pulse,
0.08 seconds of hold and 0.12 seconds of release. Translation follows release.
The complete welding phase is 11.5 seconds. `verify_pinch_welder.py` checks actual
native tip transforms, all six opposing contacts, open/released gaps and bounded
motion; it does not certify swept-volume clearance or weld quality.

## Sealant path and inspection

Two purple runs follow half of the front contact and one side each; the rear
edge is free. Generated black backing meshes are removed. The 4 mm high bead
sits below a panel raised 5 mm, with the holder and all six weld targets moved
accordingly. Each run retains 41 fitted path samples and independent cooling.
The dispenser follows the same arc-length progress as its GPU reveal over
13.5 seconds. Meshes are built once, with rounded sections and tapered ends.

After placement, the thermal camera inspects heat conducted into the opaque
panel above the sealant. See [panel contact heat](panel_contact_heat.md) for the
GPU implementation, thermal proxy, recipes and validation requirements.

R4 uses three real arm poses, ordered right/left/right. The camera mount and
lens remain rigid. The thermal prompt describes the complete runs, including
their sides, and its changed SHA rejects the previous adapter at serving time.
The internal protocol stage key remains `adhesive`; observer labels use
**Sealant**, and the cabinet row reads **SEAL**. The earlier source names now use Sealant or Seal, including the procedural
thermal fixture.

Current recipes:

| Contract element | Identifier |
| --- | --- |
| Gate semantics | `automotive-panel-process-v3` |
| Motion | `three-edge-sealant-pinch-paths-v9` |
| Physical views | `rigid-arm-panel-three-edge-scan-v6` |
| Dataset capture | `panel-three-edge-contact-episodes-v12` |
| Surface heat | `panel-three-edge-contact-transfer-v8` |

Raw model frames remain three lossless 448 x 256 grayscale PNGs per stage.
Thermal encoding remains 20-120 C, with the same 45/55 C limits and ambiguity
bands. HUD, palettes, temperatures, faults and QA sample coordinates never enter
model requests. Model consensus still owns every gate; mechanical completion
and cooling timers cannot override an inference hold.

## Reproduction and checks

The standard cell preparation now runs the pinch splitter before panel pose
generation and exports `pinch_gun.fbx`. The full/panel import automatically imports
and validates the three auxiliary parts. For an existing prepared cell, run the
splitter, export its assignment subset using the FBX helper, and import
`pinch_gun_manifest.json` with `-SkipMaterialBuild` before regenerating panel poses.
All copied geometry and generated Unreal assets remain private and ignored.

After rebuilding, run the rendered panel motion fixture, WeldingSmoke,
ThermalSmoke, CabinetSmoke and ProcessPolicySmoke. Re-capture panel episodes and
run all three sensor audits:

```bash
python scripts/verify_inspection_arm.py --contract panel --manifest artifacts/CAPTURE/manifest.jsonl --output artifacts/CAPTURE/camera-qa.json
python scripts/verify_panel_thermal.py --manifest artifacts/CAPTURE/manifest.jsonl --output artifacts/CAPTURE/thermal-qa.json
python scripts/verify_sealant_coverage.py --manifest artifacts/CAPTURE/manifest.jsonl --output artifacts/CAPTURE/coverage-qa.json
```

The coverage audit samples 39 interior surface points per run. Every point must
match its assigned temperature within 2.5 C in at least one assigned physical
view. A correct temperature at the front cannot conceal an occluded side. This
is private offline QA, not a runtime fallback or a temperature input to Reason2.
Warm sealant uses the bright extremum; sealant colder than the 23 C panel uses
the dark median in the same 3x3 footprint. Near-ambient agreement alone is
not visibility proof; hot examples expose hidden portions of the same geometry.
Capture v6 added near-ambient sealant 20.05–24.8 C and weld 20.1–26 C cases alongside
the retained warmer readiness examples. This covers cooling during the longer
mechanical sequence without changing any live temperature or gate threshold.

If continuing an adapter across a prompt revision, `train_lora.py --init-contract`
requires the saved original contract explicitly. It validates the parent against
that contract and records parent prompt hashes, changed stages and source hashes.
Base architecture, image encoding, stage modalities and camera identities must
still match. Serving compatibility is not relaxed: only a newly trained adapter
with current provenance can load for the revised prompt. Evaluate the merged
serving runtime and the actual three-body ProcessSmoke before selection.

Capture v7 additionally varies the 28 robot casting temperatures independently
of target temperature and labels, within each casting's existing ambient/maximum
limits. The seeded variation is fixed for a whole offline episode, preserves
mesh-local gradients, and does not modify live motor heating. It covers the
warm robot visible at the edge of the wider side-contact view. Recipes v5/v6
remain accepted by the geometry auditor because their physical views and encoding
are identical; their thermal nuisance coverage differs and is recorded separately.

ProcessSmoke saves at most four QA-only latest request JSON files, one per stage,
containing the exact image-only HTTP payload. This supports inspection of live
versus offline input differences; it adds no private fields to the request and
is disabled during normal interactive runs. All such payloads remain ignored.

Capture v8 corrects unwelded held panels to use ambient, undeployed weld fields,
matching live pre-weld frames. Preassembled and post-weld captures retain varied
residual weld heat. This is an offline capture-state correction; live cooling,
thermal encoding and model policy remain unchanged.


## Completed verification

The final build and 113 Python tests pass. The motion fixture records 44 rendered
checkpoints and passes 1,375 pinch-electrode checks. Procedural thermal, spatial
fields and transfer regressions pass 43/55/138 checks. CabinetSmoke passes 606 checks,
including raw-sensor parity, pause/seek and all LCD/button views. Its resource
expectation is 240 thermal profiles after the previously removed four EVK backing
plates; this change adds no cabinet thermal profiles.

The selected 768 adapter scores 288/288 development batches with the unchanged 0.98
confidence policy. Actual ProcessSmoke completes assembly/preassembled/assembly
with 29 image-only requests, three departures and all six acceptance audits.
See [model evidence and limits](model_classification.md). The demo is closed
following verification; no training job remains running.

Local rendered evidence is under `artifacts/pinch-sealant-motion-framed`, with
`pinch_open_observer.png`, `pinch_clamped_observer.png`,
`pinch_released_observer.png` and `sealant_left_end_observer.png`. Those renders
and all purchased/generated geometry remain private.

Final regression: all nine legacy WeldingSmoke suites pass (12,613 checks), alongside 91 native policy checks, 113 Python tests, 44 rendered motion checkpoints, 1,375 pinch checks, 606 cabinet checks and 43/55/138 thermal checks. Artifacts: `artifacts/pinch-sealant-legacy-v1` and `artifacts/pinch-sealant-v1/final-verification.json`.
