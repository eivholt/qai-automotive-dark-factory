# Permanent mounting frame and placement from above

The process body carries a copy of the source inner trunk panel's outer
stampings, with its central flat area cut away. This `source-panel-open-frame-v1`
mesh remains on both missing-panel and preassembled bodies and moves with the
carriage. The removable replacement is still the full source panel.

Run `scripts/assets/prepare_panel_frame.py` after preparing the panel motion.
The normal `prepare_welding_cell.ps1` pipeline includes it. The generator clips
private chrome island 386 against a central opening, preserving source winding,
normals and interpolated UVs. It records source hashes, retained/removed surface
area and support checks in private `panel_frame.json`, and writes
`panel_frame.meshbin`. Original FBX files and island ownership are unchanged.
The local asset files and generated geometry must stay out of Git.

Unreal builds one static GPU mesh at startup. It does not deform, remesh or run
a new solver each frame. The frame adds one ambient illustrative thermal
surface at the end of the existing surface list, without changing the fixed
70-row LUT or its 280 KiB size. Its opaque geometry participates in visible,
NIR and thermal occlusion. The replacement panel retains the existing conducted
contact-heat model; this is not a measured structural/thermal simulation.

Both purple sealant runs sit directly on the retained frame, along the front
and two side contacts. The rear edge remains free of sealant. The bead remains
4 mm high and the full seated panel is raised 5 mm relative to its original
source position. The cutout is 183 by 76 cm; its edges follow the source
coordinates X=-90..93 and Y=-322..-246 cm, retaining the six front weld contacts.

`top-down-frame-pinch-paths-v11` keeps the existing rack pickup, then turns the
panel horizontal behind the body. It reaches a 90 cm hover above the final
position and lowers through 35 cm and 8 cm clearances before seating. The
holder releases upward before returning to rest. Every pose moves actual R3
joints and the same rigid gripper; there is no independent panel pivot. This
is authored visual choreography, not certified industrial kinematics.

A sampled source-triangle audit finds peripheral panel/body intersections in
the last part of the descent (approximately 5.10-5.52 s). The unmodified source
panel also intersects the body at its installed position. These asset-fit
overlaps remain; the successful motion checks do not imply a collision-free
mechanical assembly. The broad transfer and above-frame hover clear the body
in the sampled audit. Angled/rearward alternatives were examined privately,
but did not eliminate the original final-fit intersections, so the visibly
simple top-down path is retained.

Frame provenance is included in private scene metadata. The model receives
only the same three grayscale inspection images and versioned prompts. The
added frame changes missing-panel evidence, so earlier adapter accuracy must
not be presented as validation of these new images. Rendering, geometry QA
and live model integration are recorded separately from independent accuracy.

Verification includes `PanelMotionSmoke`, `verify_panel_frame.py`,
`verify_pinch_welder.py`, physical R4 capture/thermal/coverage checks, Python
tests, legacy `WeldingSmoke`, native policy checks and actual `ProcessSmoke`.
The new motion checkpoints show the empty frame, supported sealant, raised
panel, lowering and final placement. Show actual captured images in chat when
reviewing this change, and close the demo after verification.

The September 15 check passed 63 rendered checkpoints, 56 frame/support checks,
3,271 pinch-welder checks and 116 Python tests. Eight QA-only capture episodes
passed 15,979 physical camera checks, 162 thermal contact samples and 1,404
sealant coverage points. These images were not used for training or accuracy
evaluation. Actual ProcessSmoke completed assembly/preassembled/assembly with
23 image-only requests and six thermal acceptance checks in 180.24 s; live
camera and curtain checks passed 4,472 and 27 checks respectively. No model
prompt, adapter, threshold, consensus rule or thermal encoding was changed.

All nine legacy welding suites and the native 91-check process policy fixture
also pass. Unreal was closed after testing. The final private record is
`artifacts/panel-frame-v1/final-verification.json`; the source-fit audit retains
its intersection findings rather than treating them as a clearance pass.
