# Panel-process indicators

HUD, cabinet and stack lights display `FDarkFactoryProcessStatus` from the controller. They never infer acceptance from the scene's preassembled flag, authored temperatures or repeating visual-cycle clock. Their output is excluded from all model images.

## Observer HUD

Four cards show Panel/NIR, Adhesive/thermal, Seating/NIR and Weld/thermal. Ten physical steps appear in two rows of five, with pending, active, complete, skipped, hold and exception states. The presence gate shows matching fresh G/R branch consensus; ordinary acceptance requires two fresh G batches. A missing panel's chosen assembly branch is normal progress, not a rejection.

Three tiles show actual capture targets for the active NIR or thermal stage. Thermal color is GPU presentation of the raw grayscale targets. Targets with another body/stage identity are withheld and labeled awaiting capture. Previews are inspection snapshots, not continuous-video evidence; they add no CPU readback, PNG upload or extra camera render. Observer FPS, movement, modalities, palettes and light controls remain independent. The observer scale is fixed at 0-120 C, while these model-camera previews retain 20-120 C. Both display 120+ for saturated heat; no observer welding-range switch remains.

The preassembled branch explicitly marks adhesive application, pickup/placement, welding, seating inspection and holder retraction as skipped. It still runs both thermal gates. A seating exception keeps the holding arm engaged and displays the hold; a timer does not turn an exception into a pass.

## Physical cabinet

The existing 13-island glass mesh, source texture/lens colors, one bounded spill light and thermal profiles are retained. Process LCD rows are `PANL`, `SEAL`, `SEAT` and `COOL`; a selected missing-panel branch uses an assembly state, rather than red fault styling. Legacy mode retains its original cycle gauges.

| Process row | Green source-lens index | Red source-lens index |
| --- | --- | --- |
| Panel presence | 2 | 7 |
| Adhesive readiness | 3 | 6 |
| Panel seating | 4 | 8 |
| Weld cooldown | 5 | 9 |

Indices reference the private cabinet placement metadata. The assembly branch uses green readiness and a blue LCD `ASMB` indication. Pausing freezes simulation-clock animation. These are display outputs, not live electronics or industrial safety controls. Regenerate `setup_control_cabinet.py` after shader edits, preserve legacy CabinetSmoke behavior, and inspect actual process text/lens exposure after rendering.

## Stack lights and clamps

Green shows mechanical work, amber shows inspection/cooling/pause, and flashing red shows a seating exception or transport failure. Presence R chooses assembly and does not trigger a rejection alarm. Clamps and the holding tool remain governed by the process state through extended inspections. Fixture movement is authored and does not establish clamp force or certified clearance.

Hide cabinet displays and stack-light surfaces from each NIR/thermal model capture, including lazily created components. Leave panel geometry, clamps, grippers and other real occluders visible. Display hiding prevents later images from feeding earlier model decisions back to the classifier.

## Verification

The current two-bead `two-bead-live-v1` rendered model run completed assembly/preassembled/assembly with three departures and all six thermal acceptance audits; the final policy fixture passed 91 checks without model calls. These reports verify the exercised branches and controller behavior. They do not exhaust every possible HUD, LCD or lamp appearance: inspect branch, waiting, exception and skipped states after UI changes. The launcher validates a fresh session and explicit JSON verdict, not Windows exit code alone. See [current validation](validation.md).

The panel model smoke requires three departures with assembly/preassembled/assembly branches and six independent thermal acceptance audits. The CPU launcher regression checks synthetic reports without starting Unreal or loading model weights:

```powershell
pwsh -NoProfile -File tests/test_process_launcher.ps1
```

Old three-card/door-return reports are historical and do not validate these four-row indicators.
