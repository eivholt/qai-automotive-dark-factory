# Factory lighting

Tab / Xbox Y cycles four observer views: dim visible, NIR, thermal, bright factory, then dim visible again. Bright factory mode switches on the overhead fluorescent-style banks. They start at different times, briefly flicker, and settle to steady illumination after about 1.5 seconds. Returning to another mode switches the banks off; entering bright factory again restarts the sequence.

Interactive launcher and Editor Play start in dim visible mode with the headlamp
off. L opts into the headlamp; roof lighting remains the last Tab/Y state. An
explicit `-ProcessObserverMode` selection overrides the starting view.

The headlamp emits only in dim visible mode. L changes its requested state outside bright mode and is ignored while the roof banks are on. Bright factory mode suppresses its emission while preserving that preference, so returning to dim visible restores the requested lamp state. This gives the observer separate controls for local light and general factory illumination.

With the roof banks off, visible illumination uses 70% of its former output:
each nominal 80-lumen roof-window proxy emits 56 lumens, and the observer
headlamp emits 1,050 instead of 1,500 lumens. Bright factory mode restores the
window proxies to their nominal output and retains the existing fluorescent
bank intensity. The active-NIR sources, thermal encoding, cabinet emissions
and welding effects are unchanged. This is a light-output reduction; the
tonemapped screenshot brightness does not scale linearly by exactly 30%.

The September 15 reduction passes the rendered lighting fixture, 2,012 graphics
checks across 32 states and 116 Python tests. Fixed/R4 sensor PNGs retain parity;
all eight lighting-fixture sensor PNGs also match the previous baseline exactly.
The repeated legacy run passes all nine suites. Its first run retained two
observer-NIR cooling/reset screenshot failures that did not recur; the cause
is not established. Evidence is private under `artifacts/dim-light-v1` and
`artifacts/dim-light-legacy-v1/v2`; no threshold was relaxed. Unreal is closed.

Six banks provide eighteen fluorescent-style tubes and six grouped lights. Each bank uses an instanced tube mesh, a housing mesh and one downward rect light: twelve mesh components and six unshadowed direct lights total. The imported cell uses 1,200 authored lumens per bank; the smaller procedural cell uses 480. Both the direct lights and visible tube emission are at 60% of the original settings (one third lower than the previous revision); the startup timing is unchanged. All banks stop ticking once steady or off. These settings were adjusted against the renderer and are not measured factory illuminance. They illuminate visible channel 0. The fixture meshes are observer-only and hidden from fixed scene captures; they neither write thermal custom depth nor alter the sensor geometry. Fixed active-NIR illumination remains on its own channel, and the thermal camera continues to use authored surface-temperature profiles. Startup is a deterministic visual envelope; lamp temperatures and electrical behavior are outside this approximation. The lighting control does not enable imported-scene inference or training.

## Native verification

The frozen lighting sequence runs at the end of the imported scene smoke. It holds the car, robots and camera poses fixed, with tool/body glow and sparks disabled. The fixture state progresses through dim, partial startup, steady bright and restored dim views. A separate mode cycle exercises 0 → 1 → 2 → 3 → 0 and an explicit headlamp-on request during mode 3.

```powershell
.\unreal\Scripts\run.ps1 -Scene welding -WeldingSmoke -Output artifacts\factory-lighting
.\.venv\Scripts\python.exe scripts\verify_factory_lighting.py --directory artifacts\factory-lighting --output artifacts\factory-lighting\factory_lighting_validation.json
```

The verifier requires Pillow. The existing WSL Python environment can run the same verifier against its mounted repository path. It reads local PNG/JSON files and performs no Unreal or model calls. Keep these captures private under ignored `artifacts/`.

`factory_lighting.json` records runtime flags, light-bank snapshots, a dense startup sample sequence and the observer-mode cycle. Four 1600 × 900 screenshots (`factory_dim.png`, `factory_startup.png`, `factory_bright.png`, `factory_off.png`) show the actual renderer. An additional `factory_fixtures.png` frames the overhead banks for visual inspection; its different viewpoint is excluded from brightness comparisons. Eight 448 × 256 raw PNGs record fixed NIR and thermal outputs for off, startup, on and restored states.

The acceptance criteria are set before inspecting the captures:

- Bright scene mean luminance must exceed dim by at least 8 codes; at least 15% of scene pixels must brighten by 12 codes. At most 8% of bright scene pixels may reach luminance 250 or above. The scene crop excludes HUD and controls.
- Restored dim must match the original scene within mean absolute luminance difference 1 code and 99th-percentile difference 3 codes, with the same headlamp preference.
- Every raw NIR PNG must be byte-identical across all four light states, as must every raw thermal PNG. Raw images must remain opaque and grayscale.
- Each bank gain must stay in [0, 1]. Off gains are zero, steady gains are one, and startup samples must show both stagger between banks and changes over time, including a downward flicker step. Re-entry releases the QA clock override and must restart the live envelope near zero. Metadata documents the deterministic envelope; the partial-startup screenshot must differ from both the dim and steady-bright scene by at least 1 luminance code on average.
- Mode 3 must suppress headlamp emission even when requested on, and restored mode 0 must recover the request. Visible-light isolation settings and the four-mode cycle must match the runtime contract.

## Rendered result

The final `artifacts/factory-lighting-v2` run passed all **2,269 lighting checks**. Bright scene mean luminance was 126.35 versus 54.27 in dim mode; 62.55% of the comparison crop brightened by at least 12 codes. Clipped highlights occupied 4.46% of the crop. Restored dim differed by 0.093 codes on average, with a 99th-percentile difference of one code. The floor and the additional fixture view were visually reviewed.

All six banks showed downward flicker steps in the sampled startup envelope. The mode cycle, headlamp interlock and live-clock re-entry checks passed. Fixed NIR and thermal PNGs were byte-identical with the roof lights off, during startup, fully on and switched off again.

The same final run passed the production-cycle (208), tool-cooling (129), car-weld heat (352) and casting-field (1,459) checks. The procedural NIR observer regression passed 20 checks, the Unreal build succeeded, and all 34 Python unit tests passed. No GPU frame-time benchmark was taken; the bounded light/component counts and disabled steady-state tick describe the implementation cost, not measured frame-time performance.
