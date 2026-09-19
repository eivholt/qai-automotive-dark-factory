# Factory enhancement roadmap

All ten ideas were retained in AGENT_MEMORY.md on September 15. The first
implementation pass covers priorities 1, 3, 6, 7 and 8 behind explicit switches.

| Priority | Enhancement | First-pass status |
| --- | --- | --- |
| 1 | Fault, corrective action and fresh inspection | Optional slow sealant-cooling case and model-triggered local cooling |
| 2 | Inspect sealant coverage before panel placement | Separate offline fault capture and base-model study; not ready for a live gate |
| 3 | Smooth robot motion and settling | Quintic transitions for process-arm travel; existing endpoints/timings retained |
| 4 | Hose bundles, protective sleeves and clamps | Ribbed spline sleeves, joint collars and bounded authored sway; more wiring remains |
| 5 | Physical panel seating and pad compression | Pad compression and slight final wrist tilt; squeeze-out/contact physics remain |
| 6 | Pinch-weld squeeze/pulse/hold/release | Existing jaw cycle retained, phase-specific HUD text and restrained expulsion option |
| 7 | Factory building and surrounding operations | Blender-generated hall/trusses/open roof strips plus converted Isaac rack meshes |
| 8 | Surface materials and localized wear | Isaac floor texture and filtered directional metal grain; further contact wear/stamping detail planned |
| 9 | Thermal reflections and apparent temperature | Observer-only one-bounce GPU screen-space experiment; model path unchanged |
| 10 | Inspection replay/presentation mode | F5 / Page Up / Page Down: eight actual requests, exact inputs, scores, latency and actions |

See [second-pass implementation and limits](enhancement_second_pass.md), including
the unsuccessful coverage feasibility result and remaining model work.

## Try the first pass

On the prepared machine with the compatible model service running:

```powershell
.\unreal\Scripts\run.ps1 -Scene welding -Process -Enhancements -FaultRecovery -ProcessObserverMode 3
```

`-Enhancements` enables factory context, quintic process travel and restrained
pinch-weld expulsion. Add `-DramaticWelding` to retain the original spark intensity
and burst size. `-FaultRecovery` is separate: it changes the thermal scenario.
The default scene and legacy fixtures retain their established behavior when
these switches are absent. Use the same `-DFEnhancements` flag when opening the
Editor to preview the hall. The industrial finish shader's subtle brushed grain
also applies to its existing visible-observer metallic fittings.

## Model-controlled recovery

Every third body, starting with body one, has sealant passive cooling time
constants multiplied by eight. This private source variation is not supplied to
Reason2 or read by the controller. Two consecutive valid thermal R responses
at the sealant gate may activate a local blower. A, invalid replies, a stage
change and pause break this rejection consensus. Existing request freshness,
confidence and label-mass checks run before this logic.

The blower accelerates decay of the sealant and conducted contact-surface heat
using a six-second illustrative cooling envelope. Its rotor follows accumulated
active simulation time and pauses with the process. Gate approval still requires
one fresh G batch. No timer, temperature threshold or fault flag releases the
body. The existing HUD/cabinet/stack-light hold remains in effect until model
acceptance; the status text explains active cooling. This first version handles
thermal recovery, not automatic panel reseating or sealant reapplication.

This is an authored forced-cooling proxy, not CFD or evidence of adhesive cure.
The experiment is excluded from offline capture mode. Existing development
accuracy does not validate this new fault distribution.

## Motion and welding

Quintic time interpolation gives zero velocity and acceleration at travel
segment endpoints. It does not add IK or change source waypoints, phase durations,
settled camera poses, extrusion paths or weld locations. It can increase peak
speed inside a segment; it is not an industrial velocity/torque-limited planner.
Existing waypoint stops and source-fit clearance limitations remain.

Pinch timing stays squeeze 0.15 s, current 0.5 s, hold 0.08 s and release 0.12 s.
The restrained option uses 30% source intensity for continuous effects and 20%
of the original burst fragment count. Thermal weld fields are independent of
this visual adjustment. Shared particle state is still used by all modalities.

## Blender and assets

Blender **5.2.2 LTS** was verified with a background Python API call and used to
convert local Isaac USD rack frames/shelves to FBX. No Blender MCP connection was
available or installed. Blender does not need to remain open:

```powershell
& 'C:\Program Files\Blender Foundation\Blender 5.2\blender.exe' --background --factory-startup --python scripts/assets/prepare_factory_context.py
.\unreal\Scripts\build.ps1
```

The generator creates source-hashed private FBXs and a manifest in
`assets/derived/factory_context`. The optional Unreal setup imports those meshes
and the adjacent Isaac warehouse floor texture. Source assets are read-only;
no materials or generated content are redistributed.

The hall uses five instanced mesh components: shell, roof, floor, rack frames and
shelves. It adds no tick, collision, lights, thermal profiles or reflection/GI
contribution. The first pass is **visible-observer context**: materials discard
pixels outside observer UserFlags bit 8, including sensor and infrared views.
A modest emissive background fill keeps distant geometry readable without
changing sensor lighting. This is a presentation proxy, not a physically lit
building envelope. The new floor sits below the retained cell floor, so it
does not replace the working cell's established ground surface.

## Validation workflow

`verify_enhancements.ps1 -Mode Graphics|Process|Motion|Realism -Output <fresh path>`
runs native rendered fixtures with the enhancement switches. Process additionally
enables the cooling fault and requires the actual existing model host. Run the
corresponding Python verifiers and inspect screenshots; native policy checks
alone do not establish model accuracy. `verify_factory_enhancements.py` runs via
Editor `-ExecCmds="py <absolute path>"` for hall/rack screenshots and closes the
Editor afterward. Final local outcomes are recorded in AGENT_MEMORY.md.

The final first-pass checks passed: UE 5.8 build, 121 Python tests, 100 native
policy checks, rendered panel motion, 3,271 pinch checks, 56 mounting-frame checks,
2,012 graphics checks, 681 realism checks and nine legacy welding suites. Four
fixed/R4 sensor baseline hashes remain identical to the preceding material-only
revision. The hall audit recorded five ISM batches and 63 instances; renders were
reviewed and shown in chat.

The final actual-host recovery run completed assembly/preassembled/assembly with
28 model requests and six thermal acceptance audits. Body one activated cooling
after model rejection, then advanced only on fresh acceptance. R4 telemetry
passed 5,442 checks. This is development integration evidence, not held-out
accuracy or a performance benchmark. Final evidence is private under
`artifacts/enhancements-process-final`, `enhancements-motion-v1`,
`enhancements-graphics-v1`, `enhancements-realism-v1`, `enhancements-legacy-v1`
and `factory-enhancements-closeups`.
