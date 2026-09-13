# Illuminated robot control cabinet

The imported control cabinet has a backlit LCD showing cycle progress, elapsed seconds, active welding robots and four individual robot phase/progress rows. Its five green and six red button lenses, plus a green status indicator below the LCD, illuminate in the dark. Existing button colors and cabinet geometry are preserved. The readouts come from the authored simulation clock; they do not represent measured current, real robot telemetry, model decisions or weld quality.

The material runs on the existing `SM_Background_robots_screen` component, which contains exactly thirteen separate glass surfaces. `scripts/assets/prepare_control_cabinets.py` derives their face locations, dimensions, normals and color assignments from the private source triangles and texture atlas. It writes `assets/derived/welding_cell/control_cabinets.json` with source hashes. The LCD is approximately 43.42 by 32.75 cm. No mesh reimport or new collider is needed.

`ADarkFactoryControlCabinet` temporarily replaces this component's material with `M_ControlCabinet`; disabling the feature restores the original material. `ControlCabinet.usf` draws the gauges, small numeric font, phase rows and colored lens emission directly on the GPU. It reuses the source texture and requires no font texture, render target, scene-color sample or image upload. Scalar updates are capped to 20 simulation-time samples per second, with explicit immediate updates for deterministic diagnostic seeks. There is no wall-clock shader Time node. Pausing freezes the gauges and retains the backlight.

The LCD uses a matte, zero-specular inner display region to keep its graphics readable under the close NIR illuminator. Surrounding bezel and buttons retain their authored texture and ordinary surface finish. A single small rectangular light supplies blue screen spill (10 authored lumens, 110 cm radius), with no shadow, GI, reflection or translucent-lighting contribution. Actual device emission in a measured NIR band is not simulated; the existing NIR rendering proxy observes the scene material emission.

Visible graphics and LEDs do not become hot thermal glyphs. The existing glass component keeps its original thermal profile and geometry. There are still 76 imported meshes and 244 thermal profiles. Changing display power leaves the raw thermal PNG unchanged. Observer modality/headlamp changes remain independent of the fixed sensor cameras.

## Generate and run

For an already prepared local cell, run from the repository root in the asset-preparation Python environment:

```powershell
python scripts/assets/prepare_control_cabinets.py
.\unreal\Scripts\build.ps1
.\unreal\Scripts\run.ps1 -Scene welding
```

The regular `prepare_welding_cell.ps1` pipeline now derives the cabinet placement manifest. The material generator runs during content setup or a new welding-cell import; it can also be run alone as `unreal/Scripts/setup_control_cabinet.py` through Unreal's Python commandlet. It modifies only the new generated material and never overwrites the imported texture, source material or geometry.

```powershell
.\unreal\Scripts\run.ps1 -Scene welding -CabinetSmoke -Output artifacts\control-cabinet
python scripts/verify_control_cabinet.py --directory artifacts/control-cabinet --output artifacts/control-cabinet/verification.json
```

The feature is enabled in the interactive demo and its dedicated CabinetSmoke captures. Older welding/realism/collider fixtures retain the original cabinet appearance. Dedicated QA frames the controls in darkness and all observer modalities, compares power off/on, checks every colored lens, derives robot phases independently from cell timing, and verifies pause/seek stability, exact thermal power parity and fixed-camera independence. Assets, generated Unreal packages, screenshots and reports remain local/private.

Validation on 2026-09-13: the UE 5.8 Development build, 34 Python unit tests and all 606 independent cabinet checks pass. Thirteen rendered states cover dark power off/on, all four observer modalities, robot-cycle phases, restored seeks and pause stability. Actual closeups were reviewed for readable text, gauge layout and lens appearance. A mirrored display orientation found during review was corrected, and LCD-only specular reflection was reduced to improve NIR contrast under the close camera illuminator.

The powered LCD changes 77,432 pixels at the eight-code threshold (92% of its checked region), retains 47.99-code grayscale standard deviation and has no white-clipped pixels in the dark-view test. Every one of the twelve lenses visibly brightens while retaining its source red/green color. Cycle progression changes 6,711 LCD pixels; the repeated paused and restored-seek LCD images match exactly. Raw thermal power on/off PNGs and fixed NIR/thermal PNGs across observer modes are byte-identical. Placement regeneration is deterministic, and an independent source-binary audit checks all thirteen surface fits. Private evidence is stored in artifacts/control-cabinet-v2 and artifacts/control-cabinet-placement-audit.json.

All nine existing welding regression suites also pass after this change: 12,261 independent checks in artifacts/cabinet-regression, with the original cabinet material retained by those fixtures.
