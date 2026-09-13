# Fixture motion and cycle indicators

The optional `ADarkFactoryCellMechanisms` actor adds four locating/clamping fixture surrogates, a two-tier stack light, and entry/exit photoelectric sensor posts. All motion and status use the welding cell's simulation clock. Pausing freezes the mechanism state; seeking directly to a time produces that time's pose. These additions do not call the model, decide weld quality, or control carriage movement.

## Reference designs and deliberate simplifications

DESTACO supplies pneumatic and other workholding systems for automotive welding and assembly. Its product families include toggle clamps, locating-pin packages and enclosed power clamps. This supports showing a separate fixture-closing step before robot work; the demo's dimensions and strokes are authored rather than copied from a commercial product. [DESTACO workholding systems](https://www.destaco.com/category/clamping?page=2)

Banner describes retroreflective sensing as an emitter and receiver in one housing, with a reflector opposite it; an interrupted return path switches the sensor output. Real shiny targets can require polarization, and dirt or smoke reduce available signal. The demo uses geometric interruption of the existing moving carriage collision boxes. It does not simulate optical power, polarization, surface reflectance, alignment error or smoke attenuation. No visible beam spans the aisle. [Banner Photoelectric Sensor Solutions, pages 2–3](https://info.bannerengineering.com/cs/groups/public/documents/literature/b_51180814.pdf)

PATLITE describes stack lights as a way to display machine status. Here green means the authored secured portion of the cycle; amber means transfer or fixture clearance. These are application-specific display choices. Neither color reports an inspection result, an actual fault, or a certified safe state. [PATLITE signal towers](https://shop.patlite.com/PATLITE-Signal-Towers-s/139.htm)

## Implemented layout and timing

Four blue housings and yellow toggle-style arms stand on the imported side platforms at world Z=85.592865 cm, Y=−92/+92 cm. Their pivots sit 48 cm outside the measured car's X bounds. The arms rise through 75 degrees to open, with visible rod/link assemblies. At initialization, the car briefly takes its stationary 3.4-second pose; any deferred body-mesh compilation and Chaos query state are completed. Nine horizontal complex traces across each pad face then find the outermost surface on the actual body panels. The original simulation clock and pose are restored before initialization returns, with no intervening ticks or captures. Each arm length is fitted once so the closed pad remains 0.2 cm outside that sampled sheet envelope. The permitted arm-length range is 30–65 cm. If surface binding fails, a logged and metadata-declared fallback uses a 42 cm noncontact guide with 2 cm clearance outside the body bounding box. This demonstrates fixture actuation without calculating clamp force or deforming the body. All added components have collision disabled; they cannot catch the carriage, curtains or spark sweeps.

| Event | Default 21 s cycle | Explicit canonical 18 s QA cycle |
|---|---:|---:|
| Car arrives | 3.00 s | 3.00 s |
| Fixtures close, quintic ease | 3.00–3.35 s | 3.00–3.35 s |
| Earliest robot approach | at least 3.70 s | 3.50 s |
| Fixtures open, quintic ease | 15.80–16.20 s | 13.00–13.40 s |
| Carriage departure starts | 16.30 s | 13.50 s |

The tower stands at (280, −710, 6.3477106) cm. Green is steady from closing completion to opening start; amber is steady otherwise. Simulation pause retains the displayed phase. This is a sequence indicator, not a motor-running sensor.

Entry and exit beam pairs lie at Y=−1050/+1050 cm, inside the portal curtain planes and outside the robot workspace. Their posts sit 40 cm outside the car's X bounds on the ground tile; the beam height is 175 cm. Each update transforms the finite beam segment into each enabled carriage shell/holder box's local coordinates and tests intersection. A small amber lens lights while any box interrupts that segment. Hidden cars have disabled collision boxes and therefore do not produce occupancy. The sparse shell boxes approximate the visible geometry, so tiny openings and detailed optical behavior are outside this model.

## Rendering, integration and checks

The actor uses 57 engine primitive mesh components, the existing `M_Factory` paint material and separate `M_WeldingArc` material instances for opaque indicator lenses. It creates no light components, textures, render targets, GPU readbacks or thermal LUT entries. Emissive lenses are shared visible/NIR geometry. Unregistered opaque surfaces use thermal ID 0 and the imported rig's 20 °C ambient fallback, while scene depth continues to occlude hotter objects behind them.

Spawn the actor, call `Initialize(Cell)`, and add it as a capture tick prerequisite. Call `UpdateFromCell()` after explicit diagnostic pose seeks; ordinary ticks already follow the cell. `SetEnabled(false)` hides the actor for legacy image fixtures. `BuildMetadata()` records the clock, closure, arm/pad positions and clearances, timing margins, tower state, beam endpoints and names of the intersected carriage boxes. All candidate boxes, including disabled ones, also carry their center, world-to-local matrix rows, half extents and local beam endpoints for an independent intersection check. Surface-binding metadata distinguishes fitted pad contact from the fallback. It exposes finite-state and zero-light/zero-collision/zero-profile declarations.

The final native build and rendered feature QA pass. All four contacts bind to actual source panels with 0.2 cm sampled clearance; there are no fallback guides. Twelve sampled cycle stages cover closing/opening, entry/exit beam crossings and the hidden replacement interval. Independent checks derive all 64 seeded robot schedules and reproduce 240 oriented-box beam tests. All schedules fit the fixture intervals, and the fixed sensors retain exact parity during frozen observer changes. See the combined [681-check feature validation](realism_additions.md#validation-2026-09-13). Source bounds and these timing checks do not certify full industrial swept volumes, clamp mechanics or personnel safety.
