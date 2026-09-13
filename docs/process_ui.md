# Model-gated process indicators

The process HUD, cabinet and stack light read `FDarkFactoryProcessStatus` from the process controller. They never derive acceptance from the scene seed, missing-door flag, authored temperature or old repeating production clock. The controller owns all decisions; these components only show them.

## Observer HUD

The three cards represent door inspection in NIR, adhesive inspection in thermal, and post-weld cooling inspection in thermal. Each shows pending, inspecting, waiting, passed, rejected or unavailable/hold. The active gate includes the fresh consecutive `G` count out of two. The panel also shows the current body, process phase, backend string, last decision and latency, completed/returned counts and pause/transport messages. All HUD drawing stays outside scene-capture/model pixels. Its metadata comes from the same status structure.

Process mode replaces the overview thumbnails with the actual gate capture targets: two raw NIR views for doors, or three R4 thermal spot views. Thermal color is a GPU presentation of the grayscale targets. Each thermal tile has its own material instance; no CPU readback, image upload or extra capture is used by the HUD. Labels identify them as inspection captures rather than continuous video. Unavailable/current-body-invalid targets show an awaiting-capture tile. The legacy overview previews remain unchanged outside process mode; FPS and modality controls remain available.

## Physical cabinet

The existing 13-island glass mesh, source texture, source lens colors, one screen spill light and thermal profiles are preserved. `ProcessMode` selects a GPU LCD with three gate rows, body number, phase elapsed time and pass streak. Mode zero retains the original robot/cycle gauges for legacy fixtures. Material updates remain limited to 20 simulation-clock samples per second, with immediate updates for state transitions.

Three existing green/red lens pairs show the gates:

| Gate | Green button index | Red button index |
| --- | --- | --- |
| Door NIR | 3 | 6 |
| Adhesive thermal | 4 | 8 |
| Weld cooling thermal | 5 | 9 |

Indices refer to `control_cabinets.json`, including its rectangular status LED at index zero. Green is steady after acceptance and pulses while inspecting/waiting. Red pulses on rejection or an unavailable model. Pending lenses retain only a faint backlight. Pause freezes blinking. These are display outputs, not instrumented electronics or safety controls.

Regenerate the private material with `unreal/Scripts/setup_control_cabinet.py` after editing `unreal/Shaders/ControlCabinet.usf`. Run CabinetSmoke and `scripts/verify_control_cabinet.py` to protect the legacy graphics, then inspect the process-specific gate states in a rendered process run. No new texture, render target, thermal surface or capture is added by the cabinet.

## Stack light and fixtures

Process mode adds one red tower segment and its cap lazily: two bounded meshes, zero lights and zero thermal profiles. The legacy green/amber tower is unchanged when process mode has never been enabled.

- Green: body transfer or active welding.
- Amber: inspection, cooling hold, or pause.
- Flashing red: rejected body or model/transport error; it uses the simulation clock.

Fixtures close over the first 0.35 seconds of the door-inspection phase, then stay closed through adhesive inspection, welding and weld cooling. They open for arrival, departure or return. This overrides the old fixed-cycle release time so an extended model or cooling hold cannot release the fixture prematurely. Fixture motion remains authored and does not certify industrial clearance or clamp force.

Hide the cabinet glass and the three stack-lamp surfaces from every model camera using `GetDisplaySurface()` and `HideProcessIndicatorsFrom()`. Keep clamps, supports and other physical occluders visible; reapply lamp hiding after the lazy red segment is created. This prevents a feedback path in which a later model invocation reads previous gate results from illuminated physical displays. Their world-space placement alone is not an evidence-isolation guarantee.

## Launcher regression check

The launcher validates the native smoke report as well as the Windows process exit code. Each launch has a new session ID; missing, stale or failed reports cannot pass. Model smoke requires the expected three body destinations and four successful thermal acceptance audits. Policy smoke requires every reported check to pass with no model requests.

Run the CPU-only launcher regression suite with:

```powershell
pwsh -NoProfile -File tests/test_process_launcher.ps1
```

The script extracts the production validator from `run.ps1` and checks synthetic reports, including a simulated exit code zero paired with a failed report. It does not start Unreal or load assets/models. Results and synthetic fixtures are retained under ignored `artifacts/process-launcher-tests`; `-OutputDirectory` selects another output root.
