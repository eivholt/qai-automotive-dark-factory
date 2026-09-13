# Robot motor and joint heat

The imported robots heat as they move and cool during idle periods. Twenty-eight existing casting profiles represent the motor housings and nearby links: fixed base, shoulder, upper arm, elbow, forearm, wrist and flange on each of four robots. Their temperatures feed the existing thermal renderer and its mesh-local warm-core field. Tools and the four car-weld spots retain their separate heat clocks and surface fields.

This is an authored joint-duty thermal proxy. It does not infer torque, electrical current, gearbox losses, material heat capacity or measured motor temperature. The imported bulk heat-transfer network remains disabled; these temperature changes do not imply conductive or radiative energy exchange between imported parts.

## Duty and cooldown

The heat model samples angular speed from the same numerical joint evaluator that moves the robots, at 120 Hz. Each casting has explicit driving joints and a bounded activity in the range 0-1. Its heating rate, cooling time constant and maximum temperature are authored parameters. Larger approach and withdrawal movements therefore contribute to the duty history, while the slow precision weld contributes according to its actual authored joint motion.

For temperature rise `R = T - 20 C`, activity `a`, heating rate `h`, maximum permitted rise `Rmax` and cooling time constant `tau`, a constant-activity integration step solves:

```text
dR/dt = h*a - (h*a/Rmax + 1/tau)*R
```

Activity is the weighted sum of each driving joint's absolute angular speed divided by an authored reference speed, clamped to 0-1 for each joint before weighting. The current role parameters are:

| Casting | Driving joints and weights | Reference speed | Maximum core | Heating rate at activity 1 | Cooling tau |
| --- | --- | --- | --- | --- | --- |
| Fixed base | Base 1.0 | 120 deg/s | 60 C | 30 C/s | 35 s |
| Shoulder | Shoulder 0.8, base 0.2 | 90 deg/s | 92 C | 65 C/s | 30 s |
| Upper arm | Shoulder 0.8, elbow 0.2 | 90 deg/s | 78 C | 45 C/s | 24 s |
| Elbow | Elbow 1.0 | 90 deg/s | 95 C | 70 C/s | 28 s |
| Forearm | Forearm roll 0.7, elbow 0.3 | 45 deg/s | 85 C | 58 C/s | 24 s |
| Wrist | Wrist 0.7, forearm roll 0.3 | 15 deg/s | 90 C | 60 C/s | 22 s |
| Flange | Tool roll 1.0 | 120 deg/s | 75 C | 42 C/s | 18 s |

These rates deliberately make duty and cooldown visible during the short demonstration. Static holding torque is omitted.

The analytic step keeps the temperature bounded and includes cooling while the motor operates. Complete repeated cycles use an affine recurrence, so seeking a later cycle does not require replaying every elapsed frame. An additional idle interval applies `R_idle = R * exp(-idle/tau)`. Scene pause freezes the shared motor duty clock. It does not reset motor temperature when a new car enters.

The existing spatial recipe remains attached to each casting in local coordinates. Its edge temperature is clamped between ambient and the current core temperature; a cold casting cannot acquire a hotter edge simply because its authored core-to-edge drop exceeds its current temperature rise. The longest-axis kernel and emissivity remain unchanged as the motor warms. No new image, mesh or GPU pass is required; the existing 22-row profile LUT receives the updated temperatures.

For dynamic castings, apparent-temperature conversion reuses a bounded cache keyed by emissivity and reflected ambient temperature. Each table has 81 samples from 20 to 100 C at 1 C intervals. Linear interpolation retains continuous motor temperatures; it does not round their state to whole degrees. Each interval midpoint is checked against the native radiometric calculation when its table is created, with error below 0.005 C required. The cache holds at most 32 tables, and other surface paths retain their existing calculation. This avoids repeated spectral integration for every moving casting without adding a render target or enlarging the 22-row LUT.

NIR and visible materials receive no motor incandescence. The fixed NIR sensor must therefore remain byte-identical during a frozen motor-heat comparison. Thermal palettes change presentation only; raw thermal remains 448 x 256 lossless grayscale encoding apparent temperature over 20-120 C.

## Frozen rendered checks

Run the local imported-scene smoke, then the dedicated verifier:

```powershell
.\unreal\Scripts\run.ps1 -Scene welding -WeldingSmoke -Output artifacts\motor-heat
python scripts/verify_motor_heat.py --directory artifacts/motor-heat --output artifacts/motor-heat/motor_heat_validation.json
```

The verifier requires Pillow; the existing environment described in [thermal QA](thermal_qa.md) can run it without another installation. Read the report and process exit code. `WeldingSmoke` produces the motor stage after the existing production, cooling, field, lighting and choreography checks.

All five stages freeze the robots and car at pose time 6 seconds. Tools, body-weld heat, sparks and arcs are disabled for this comparison. Only motor duty and idle history change:

| Stage | Duty clock | Extra idle time |
| --- | --- | --- |
| Cold | 0 s | 0 s |
| Working | 12 s | 0 s |
| Hot | 84 s | 0 s |
| Cooling | 84 s | 8 s |
| Cooled | 84 s | 30 s |

`motor_heat.json` records motor parameters and temperatures, the frozen pose and thermal profiles, and visible/iron/monochrome screenshots plus raw thermal/NIR files for each stage. A shared stencil image identifies exact surfaces. Reapplying the hot state must reproduce its raw thermal PNG exactly. Native checks also exercise deterministic seeks, pause, geometry preservation and independent heat controls. Model self-checks cover cold starts, bounded large-time queries, seek order, cooldown and continuity around phase and cycle boundaries. The verifier requires the radiometry cache to remain bounded, reuse the same validated tables across stages, and retain midpoint error below 0.005 C.

The image verifier erodes each exact casting mask by one pixel to avoid silhouette mixing, then requires at least 12 remaining pixels for a quantitative surface measurement. At least eight driven castings must be visible. At least 80% of eligible castings must brighten by three raw intensity codes from cold to hot and decrease by at least 0.5 codes during each cooldown transition. For eligible castings with an authored drop of at least 8 C, at least 80% must retain a hot-state 90th-to-10th percentile spread of three codes. At least eight such high-drop castings must be visible. Small or occluded surfaces still undergo metadata and profile-association checks.

All 28 scalar temperatures must rise from cold to working to hot, then cool strictly within their configured bounds. Thermal source/core temperatures must match the motor state, edge clamping must remain valid, and non-motor temperatures and geometry must remain unchanged. Every raw PNG must be opaque and grayscale; all five NIR files must match byte-for-byte. The observer monochrome crop must show heating and cooldown, while iron screenshots must remain colored and visibly change. The crop excludes HUD text and sensor panels.

Thresholds were declared before native capture. Source assets, generated packages, screenshots and detailed reports remain private under ignored directories.

## Native rendered validation (2026-09-12)

The completed `artifacts/motor-heat-v1` Unreal run passed all 1,071 motor checks without threshold changes. All 28 casting profiles matched their driving-joint state and frozen thermal profiles. The 25 surfaces with enough eroded sensor pixels all showed the required heating and both cooldown transitions. Their cold-to-hot raw mean increases ranged from 4.65 to 71.42 intensity codes; the smallest measured cooldown drops were 1.43 and 2.09 codes.

Twenty of 21 eligible high-drop castings retained the required within-part variation. The remaining distant shoulder occupied only 12 eroded pixels and had a one-code 90th-to-10th percentile spread, below the three-code threshold. It still passed the heating and cooldown tests. The three castings excluded from pixel measurements were too small or occluded; their scalar, mapping and profile checks passed.

At 84 seconds of duty, core temperatures ranged from 25.55 to 63.32 C: shoulders reached 63.20-63.32 C, elbows 60.41-62.00 C, and lower-duty wrists 25.55-33.81 C. After 30 additional idle seconds, all cores had cooled to 21.42-35.94 C. The thermal images showed warmer central casting regions becoming darker during cooldown in both iron and monochrome views. The monochrome scene mean rose from 0.54 at cold to 7.29 at hot, then fell to 5.06 and 2.18; 14.32% of the iron scene crop changed by at least three channel codes between cold and hot. These image values depend on the declared temperature window and viewpoint.

Fixed NIR PNGs were byte-identical across all five stages, and reapplying the hot state reproduced its thermal PNG exactly. Eligible non-motor sensor regions had zero measured pixel change. Geometry, pause, independent tool/body heat and disabled spark/arc checks passed. The cache used one 81-sample emissivity/ambient table, with a maximum native midpoint error of 0.00013542 C. Model self-checks passed all 13 phase/cycle boundary probes, with a maximum sampled temperature difference of 0.00003315 C across the +/-0.00001-second pairs. These are observations from this local run, not a motor calibration or performance benchmark.
