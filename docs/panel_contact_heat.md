# Three-edge sealant and panel contact heat

The process deposits two purple sealant runs along the front and two sides of
its inner trunk panel. The fourth, rear edge is free. Generated black backing
bands, legs and webs are removed completely. The rounded beads are about 4 mm
high and 18 mm wide; the installed panel and its holder/weld targets are raised
5 mm. The original purchased meshes remain unchanged.

## What the thermal gate sees

The sealant sits under opaque steel after placement. The camera observes heat
conducted into the visible panel surface, not radiation through the steel.
Its prompt explicitly describes two heated contact bands covering three edges.
The illustrative apparent-surface limit remains 45 C, with the unchanged +/-3 C
ambiguity band. This is a surface-temperature readiness proxy, not a measure of
hidden sealant temperature, chemical cure, bond strength or weld quality.

Each run retains its own exponentially cooling source. Contact begins only once
the panel is installed (or on arrival for a preassembled body). An analytic
first-order metal response with a 0.35-second time constant follows that source.
It heats with a short lag and retains warmth during source cooling. Simulation
pause and deterministic seeking preserve the response. A new body resets it.
The panel's existing 23 C baseline is retained as the lower field bound.

This is an authored local reservoir approximation, not an energy-conserving
whole-body model. It intentionally does not apply the procedural solid-box
solver to the hollow car shell. Offline captures assign explicit panel-surface
temperatures to cover hot, ambiguous and cool cases, using the same 8-14 um
emissivity/reflected-ambient conversion as live rendering. Private labels and
thermal state never enter model requests or replace model decisions.

## GPU implementation

Sixteen capsule segments (eight per run) describe continuous mesh-local heat
bands on each panel instance. The central half-radius is uniform, then a smooth
fringe falls back to the panel baseline across a 3 cm radius. Their transforms
follow the actual held or incoming panel. Opaque scene-depth checks remain in
place. The existing thermal material combines these bands with the six weld
halo/core pairs before apparent-temperature conversion.

The existing LUT grows from 256 x 38 to 256 x 70 RGBA32F texels: 280 KiB per
upload. Rows 38-69 store start/radius and end/temperature; row 15.B holds the
active segment count. Other profiles have zero segments and skip their loads.
There are no new heat meshes, draw calls, post-process passes or image readbacks.
The two original bead meshes remain for visible/NIR presentation before seating.
Observer palettes remain display-only; model PNGs remain linear 20-120 C.

## Reproduction and validation

Regenerate panel poses using `scripts/assets/prepare_panel_motion.py`, then build
with `unreal/Scripts/build.ps1` so `setup_content.py` rebuilds generated materials.
The source FBX and copied pinch gun do not require reimport for this refinement.
Run panel motion, WeldingSmoke, ThermalSmoke and ProcessPolicySmoke. Compile
`scripts/tests/contact_heat_check.cpp` for independent ODE/analytic response,
bounds, equal-time-constant and deterministic replay checks; retain the existing
standalone heat-solver regressions.

Capture new whole-body episodes and run `verify_inspection_arm.py`,
`verify_panel_thermal.py` and `verify_sealant_coverage.py`. The coverage audit
checks all 39 interior path samples per run, including side contacts, against
rendered pixels. Keep its tolerance and occlusion failures. Train a compatible
adapter, evaluate development validation, then run actual ProcessSmoke with
assembly / preassembled / assembly and six thermal acceptance audits.

| Recipe | Identifier |
| --- | --- |
| Semantics | automotive-panel-process-v3 |
| Motion | three-edge-sealant-pinch-paths-v9 |
| Physical views | rigid-arm-panel-three-edge-scan-v6 |
| Capture | panel-three-edge-contact-episodes-v12 |
| Thermal | panel-three-edge-contact-transfer-v8 |

Earlier exposed-sealant accuracy belongs to its original geometry and prompt.
The reserved test episodes must not guide adapter selection. See
[model status](model_classification.md) for the selected runtime and measured results.

Capture v12 includes the exact live NIR power in every fifth episode, alongside
0.8-1.2 randomized power. The stride is coprime to the three presence and four
seating cases, so nominal illumination is not correlated with one label. The
v10 cohort remains a separate source of randomized examples. Both use the same
geometry, prompt, heat recipe and image encoding; keep their provenance separate.
The partial development v11 capture is unused.

## Completed checks

UE 5.8 build and generated-material rebuild passed. The final motion fixture
passed 47 checkpoints and 1,655 pinch/sequence checks. The hot/cool observer
captures show contact apparent maxima falling from 49.75 C to 31.57 C over
14 seconds, then 28.91 C on departure without resetting the contact clock.
Both full 64-episode cohorts pass camera checks and 12,168 thermal path samples
each across training/validation. The procedural thermal, spatial and transfer
suites pass 43/55/138 checks; all nine legacy welding suites pass. Analytic
contact-response and standalone heat-solver checks pass, as do 116 Python tests.
Actual routed model integration completes all three bodies and six thermal gates;
see [model status](model_classification.md) for accuracy scope and remaining hold.
