# Implementation progress — 16 September 2026

This overview describes the current implementation. Detailed guides retain
reproduction commands and historical provenance. A documentation update does
not rerun tests, train models or establish new accuracy results.

Latest visual refinements add rounded GPU-instanced welding sparks, staggered
amber/green EVK LED blinking, and a rigid wrist-to-camera support rod without
moving the inspection lens. Launcher and Editor Play start dark with the
headlamp off; Tab/Y cycles dark, NIR, thermal and roof-lit factory last.

[Equipment heat](equipment_heat.md) adds travel-driven carriage motor warmth,
cooling at rest and localized cabinet/vent heat using existing GPU surface fields.
Roof-bank output is reduced from 1,800 to 1,200 lumens, with a further half-stop
reduction in bright-mode Lumen exposure. This changes the rendered environment;
it does not establish a new model-accuracy result.

The subsequent [enhancement first pass](enhancement_roadmap.md) adds opt-in
model-triggered cooling recovery, quintic process travel, restrained pinch-weld
effects, a Blender/Isaac factory hall and floor/metal surface detail. All ten
enhancement ideas are retained there with their implementation status. The
final recovery integration completed three bodies and six thermal audits;
this does not change the model-accuracy scope below.

The [second enhancement pass](enhancement_second_pass.md) adds ribbed hoses,
joint collars, compliant-looking pad motion, final seating tilt, GPU thermal
reflection experiments and exact-request inspection replay. Its actual-model
integration completed three bodies with six thermal acceptance audits. The new
offline sealant-coverage study is separate: base-model errors prevent enabling
that proposed gate, and no new adapter was trained in this pass.

## Assembly and inspection

Four articulated robots perform the process: R1 dispenses sealant, R3 picks and
holds the panel, R2 pinch-welds, and R4 carries a rigid inspection camera. The
rack restocks after departure. Every body retains a source-derived open mounting
frame; the holder brings a full panel above the body and lowers it onto the frame.

Two continuous purple sealant runs cover the front and side contacts, leaving
the rear edge free. The panel sits 5 mm higher over 4 mm beads. Six front-flange
welds use the source gun at 70% scale with animated jaws. Sealant inspection reads
heat conducted into the opaque panel surface, not hidden beads through metal.
The interfering rear roof crossmember and two clamps inside robot bases are
omitted only in process mode. The retained clamps rest on relocated platforms,
and the rack is moved clear of the holder's parked position.

R4 physically visits three settled views per stage. Protocol stages remain
`panel_presence`, `adhesive`, `panel_seated` and `weld`; user-facing text uses
Sealant. Missing panels pass application, placement, sealant cooling, welding,
seating confirmation, weld cooling, holder retraction and departure. One in five
bodies is generated preassembled, but only fresh model consensus selects the
branch. Preassembled bodies still require both thermal gates. Errors, ambiguous
predictions and stale replies hold. HUD, cabinet and stack lights reflect the controller.

See [process inspection](process_inspection.md), [mounting frame](panel_mounting_frame.md),
[contact heat](panel_contact_heat.md) and [pinch welding](pinch_welder_and_sealant.md).

## Rendering and interaction

- Visible, active-NIR and thermal observer views are independent of model cameras.
  Visible modes include dim roof-window spill/headlamp and bright fluorescent banks.
- NIR has a separate camera-following observer illuminator. Model captures retain
  their own illumination and lossless 448 × 256 grayscale contract.
- Thermal includes colored/monochrome palettes, local gradients, tool/motor
  cooling and panel contact heat. Observer range is 0–120 °C; model thermal is
  20–120 °C. Hot weld cores can exceed these display ranges.
- Shared sparks have burst, fountain and fallout phases with bounce and cooling;
  bounded optical smoke affects visible/NIR. Portal curtains use physics.
- Visible observer DLSS/Lumen/hardware ray tracing remain isolated from sensors.
  Default launch is borderless 2560 × 1440 with V-sync and a 60 FPS cap. Xbox
  navigation, FPS display, collider overlay and Escape-to-quit are available.
- Optional rubber/metal finishes reuse private Isaac textures on existing hoses,
  pads, feed tube and tool fittings. Two shared 1K-capped GPU texture sets add no
  geometry or CPU animation. A/B captures were reviewed and shown in chat; all
  four baseline sensor PNGs match the prior fixture exactly.

See [observer graphics](observer_graphics.md), [industrial finishes](industrial_finishes.md)
and [thermal rendering](thermal_rendering.md). These remain illustrative rendering
proxies rather than calibrated infrared reflectance or industrial measurements.

## Editor and project structure

`/Game/Maps/DarkFactoryPanel` is the startup map. Its saved generator rebuilds the
current scene before Play, without inference, scene captures or simulated bodies.
Transient preview children are removed before runtime begins.

The local MCP server at `127.0.0.1:8000/mcp` was verified through initialization,
discovery and tool calls. The Editor-only DarkFactoryAuthoring plugin provides
five tools for inspection, refresh, saved pose, viewport bookmark and source
ownership. `unreal/Config/Authoring/preview.json` persists preview settings;
production C++ and asset generators still own lasting scene edits. AllToolsets
is disabled in favor of focused toolsets so commandlet generation works.
MCP requires the Editor to be running; Reason2 is a separate service.

See [Editor workflow](unreal_editor.md) and [authoring structure](authoring_structure.md).

## Model status and remaining work

The selected host route uses `panel-contact-shared-960-v7` for presence, sealant
and seating, and `panel-contact-shared-896-v6` for weld cooling. Routing is by
stage, never prediction. Confidence remains 0.98 with label mass 0.5 and two
fresh consensus batches. Exact routed development validation was 191/192 with
one conservative sealant hold and no observed premature approvals. This result
predates the permanent mounting frame. The recorded 50.65/54.99 ms HTTP
median/p95 is a development-host measurement, not EVK latency or cycle time.

The frame and subsequent Editor workflow passed actual three-body integration
with all six thermal acceptance audits. These short runs are integration
evidence, not held-out evaluation. Recent authoring/material work did not change
adapters or measure new model accuracy. See [model classification](model_classification.md).

Pending work includes held-out evaluation of the final scene, the planned
shared-versus-per-modality adapter comparison, IQ9 export/quantization and
accuracy/latency validation, and sustained performance measurements.

## Publication and assets

Only tutorial, code and configuration are intended for publication. Runtime
builds, purchased/generated assets, rendered datasets, screenshots and model
weights remain private. The adjacent Isaac project is a read-only reference.

No Unity Store content was imported during the material review. The researched
packs used standard terms with AI-use restrictions; private/non-commercial use
does not itself supply an exemption. Unity-compatible assets with separate
licenses or explicit permission remain candidates for individual assessment.
See the source links and preparation steps in the [material guide](industrial_finishes.md).
