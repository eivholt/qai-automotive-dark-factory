# Welding fragments and smoke

The welding-cell preview uses a deliberately enlarged particle effect to make four tools readable from the observer camera. Burst, fountain and falling-fragment phases share one bounded world simulation. Hot fragments retain collision, bounce, resting-surface attachment and cooling across visible, NIR and thermal views. Smoke is a separate pooled plume that rises, expands and fades from stored particle state.

These effects are authored demonstrations. Their displayed density, timing, brightness and heat are not calibrated welding-process measurements. The implementation must not use the four-module inspection prompt or generate labeled welding training data until a welding-specific contract is defined.

## Reference and artistic interpretation

Resistance spot welding squeezes sheet metal between electrodes while current produces localized heating. KUKA describes this automotive joining process and its electrode contact; the demonstration retains tools at their existing solved body-surface sites. [KUKA spot welding](https://www.kuka.com/en-us/applications/automated-welding/spot-welding)

The visual reference is molten-metal expulsion, rather than a requirement for every normal weld to throw sparks. TWI relates splash to process current, time, force, contact conditions and fit-up. Its automotive WeldZero article describes expelled molten metal and the cleaning it can require even when the joint is not defective. This matters for future inspection labels: a dramatic effect is not itself a validated defect classifier. [TWI splash guidance](https://www.twi-global.com/technical-knowledge/faqs/faq-how-can-i-minimise-weld-splash-for-resistance-spot-weld-quality-and-safety-reasons), [TWI/BMW WeldZero](https://www.twi-global.com/media-and-events/press-releases/2022/weldzero-improved-automotive-resistance-spot-welding)

The reviewed TWI reference photograph shows bright narrow streaks spreading laterally and upward near electrode contact. The authored effect enlarges that fan into distinct burst, fountain and falling-fragment phases for readability. Its timing, speeds, recurring fountain and persistent smoke are artistic parameters, not measurements extracted from that photograph. Reference images remain private under ignored artifacts.

## Authored phases and bounded work

Each emitter repeats a 1.05-second pulse, staggered by 0.11 seconds from its neighbor. Phase age begins at that emitter's offset after welding starts at cycle time 4.5 seconds.

| Local pulse age | Emission | Initial speed range |
| --- | --- | --- |
| 0-0.10 s | One burst of at most 64 fragments | 900-1800 cm/s |
| 0.10-0.42 s | Fountain, up to 240 fragments/s | 450-1000 cm/s |
| 0.42-0.82 s | Lower-energy falling-fragment phase, up to 100 fragments/s | 100-400 cm/s |
| 0.82-1.05 s | No new fragments from that pulse | Existing fragments continue moving and cooling |

Ejection remains on the outward side of the actual sheet surface. On an upward-facing panel the lower-energy phase can begin with an upward component; gravity then makes it fall. It does not create particles inside the car to force a downward look. Fragment lifetime ranges are 2.8-3.7 seconds for bursts, 3.3-4.4 for fountains and 3.8-4.8 for falling fragments. The physical grain remains separate from its larger display streak.

The shared pool holds at most 4096 fragments. Physics runs at 90 Hz, with at most one sphere sweep per airborne grain per step, a declared ceiling of 4096 sweeps per step and at most 268 spawn attempts per step. Resting grains follow their contact surface without another sweep. Twelve existing thermal bins render the authored cooling state. The 240/s value bounds continuous emission per emitter; it does not include the discrete burst event and must not be presented as an all-in instantaneous rate.

Smoke uses one instanced component with at most 128 puffs, three custom-data values per instance and a 60 Hz state update. The `shared-pulse-optical-wisps-v2` profile uses a weighted birth rate of 28 puffs/s per tool. Initial radii are 12-17 cm and expand at 11-17 cm/s; initial rise speeds are 33-46 cm/s with 4.4 cm/s2 buoyant acceleration and 10-22 cm lateral curl. Stored age drives the motion and quadratic opacity tail. The material uses authored opacity 0.52 and neutral unlit gray 0.22, with a broad noisy soft edge. Lifetimes remain 3.0-4.4 seconds. The smoke actor performs no collision queries or volume-fluid solve. This is a bounded optical proxy, not simulated fume transport or thermal attenuation.

## Shared state and capture isolation

Fragment collision uses world geometry rather than observer-camera depth. Physical grain radius remains separate from the larger emissive display shape. A resting fragment follows the surface it struck. The same world-space particles are rendered by all cameras; switching observer modes must not restart their random stream, motion or cooling.

Smoke rendering uses stored age and seed rather than global shader time. A draw-only visibility switch provides a frozen comparison without deleting or advancing the plume. Visible and NIR images may contain the same geometric smoke; smoke contributes no thermal profile or calibrated volumetric thermal attenuation. Frozen smoke visibility changes must leave raw thermal identical.

The fixed NIR and thermal cameras remain independent of the observer. Observer images contain HUD and presentation effects; raw sensor captures retain their separate 448 x 256 grayscale contracts. Offline diagnostic readbacks are explicitly permitted for this QA and are not a live-preview rendering path.

The imported fixed NIR camera uses a small GPU grayscale pass after tone mapping. Desaturating before the filmic color transform alone can leave a slight color difference after translucent smoke is composited. The final pass makes the sensor render neutral before preview or readback; offline QA measures the source RGB spread, and the existing PNG encoder retains deterministic grayscale. This is an imported NIR rendering revision: earlier imported captures are not byte-equivalent and must not be treated as an unchanged dataset. It does not change the procedural inspection capture or training path. The verifier separately checks source-neutrality metadata for each effects NIR image and the actual encoded PNG channels, so CPU conversion cannot conceal a failed source check.

## Rendered verification

The dedicated verifier checks actual phase screenshots and actor metadata, plus paired smoke-on and smoke-hidden images from identical particle states. A later no-emission pair measures fading without confusing smoke loss with fragments expiring. Early smoke must produce a mean absolute image difference of at least 0.10 intensity code and affect at least 0.1% of the inspected pixels by three channel codes. Its late difference energy must fall by at least 50%. The scene crop excludes HUD text and sensor panels.

Pool capacity, emission-event bounds, fixed-step collision work and render-instance counts constrain the demonstration. CPU timing reports cover the documented actor work only; they do not establish GPU completion time or overall FPS. Existing production checks for physical contacts, resting fragments, zero capacity drops and complete clearing remain applicable.

Run the local rendered smoke and both verifiers:

```powershell
.\unreal\Scripts\run.ps1 -Scene welding -WeldingSmoke -Output artifacts\welding-effects
python scripts/verify_welding_effects.py --directory artifacts/welding-effects --output artifacts/welding-effects/welding_effects_validation.json
python scripts/verify_production_cycle.py --directory artifacts/welding-effects --output artifacts/welding-effects/production_validation.json
```

The dedicated `welding_effects.json` contains four phase records. Burst, fountain and falling-fragment samples replay from 4.5 seconds to 4.56, 4.80 and 5.22 seconds in fixed steps. Their names refer to the leading emitter; staggered neighboring emitters can occupy different phases. The settled sample replays to 5.32 seconds and ages the effects another 3.48 seconds without births while the pose stays fixed. Each record includes the actual pose, fragment/smoke metadata, visible/NIR/thermal screenshots and both raw sensors. Independent tool, car-weld and motor heat clocks are zero for these comparisons.

Smoke pairs independently replay the same history to 5.32 seconds, then move to a fixed withdrawn pose at 13 seconds and clear only the fragment/arc effect. Cumulative no-emission smoke ages are 0.8, 3.8 and 8.8 seconds for early, late and expired pairs. Each pair captures visible, observer NIR, raw NIR and raw thermal with smoke drawing enabled and disabled. Each observer on/off pair stays in the same view and lighting state before changing modes. The offline effects stage temporarily sets `r.Test.FreezeTemporalSequences=1`, restoring the previous setting on exit. This pins stochastic shadow and final-dither sequences while the physical scene is frozen, allowing an empty-smoke comparison to require exact pixel equality. It does not freeze the live demo, alter the image thresholds, or manufacture a plume difference. The draw toggle must preserve every physical puff record and its state signature. Late smoke may have expired completely; the expired pair must be empty with no image residue.

The verifier requires distinct emitted speed populations, actual births in each named phase, finite world state, physical fragment collisions/rest and bounded pools/work. Adjacent phase screenshots must visibly change. For smoke, both observer views and raw NIR must show an early plume and the required fade; raw thermal must remain byte-identical when smoke drawing changes. The existing production verifier retains zero capacity drops, populated airborne particles, collisions/resting and complete empty-station clearing requirements.

## Actual rendered validation

The native `artifacts/welding-effects-v4` run on 2026-09-12 passed all 360 dedicated effects checks and all 214 production checks. No image thresholds were lowered. Native scene/pause/draw-state checks passed, and all ten fixed NIR captures had zero RGB spread before CPU grayscale encoding. The earlier pre-tone-map desaturation failure was corrected in the imported GPU render path.

| Frozen sample | Live fragments | Airborne fragments |
| --- | ---: | ---: |
| Burst, 4.56 s | 64 | 64 |
| Fountain, 4.80 s | 229 | 227 |
| Fallout, 5.22 s | 457 | 440 |
| Settled, 5.32 s plus 3.48 s without births | 132 | 0 |

The independent live production sequence peaked at 1,692 live and 859 airborne fragments. It recorded 8,653 collision events and 2,888 rest events, zero pool drops, and zero live particles after clearing. Peak measured work was 69 birth attempts and 864 sweeps per simulation step, within the declared bounds. Events can count repeated contacts; they are not unique-particle counts.

The smoke-only history produced 18 early puffs, four nearly transparent late puffs and zero expired puffs. Early mean absolute on/off differences were 0.4063 codes in the visible observer crop, 0.3203 in observer NIR and 1.4688 across raw NIR. Respectively 1.91%, 2.05% and 2.70% of pixels changed by at least three channel codes. Late difference energy fell by 99.87%, 99.94% and 99.96%. Expired pairs had exactly zero pixel difference in all three families. Every smoke on/off raw thermal PNG pair matched byte-for-byte.

Reviewed screenshots show an initial contact fan, a larger fountain and widely dispersed falling fragments across all three modalities. Smoke forms localized wisps above the contact regions and fades away; it is clearer in NIR than against the bright visible car body. The thermal plume is deliberately absent, as required by the declared optical-only smoke model.

The recorded spark actor live-tick sample covered 1,021 calls, with 0.890 ms mean and 6.720 ms maximum CPU advance time, including render-instance updates. It excludes other actors, reset work, direct offline QA advances and GPU completion. These numbers describe this local sample and do not establish scene FPS or a portable performance guarantee.

The same render passed the existing tool-cooling 129, body-heat 352, imported-field 1,459, factory-lighting 2,269, choreography 348 and motor-heat 1,071 checks. Separate procedural regressions also passed: exact async/reference PNG parity with stale-capture rejection, 20 night-vision image checks, and mock G/A/R/error handling with asynchronous capture. Their report is `artifacts/procedural-regression-v4/summary.json`. Private reports and comparison images are under `artifacts/welding-effects-v4`, including `welding_effects_validation.json`, `production_validation.json`, `welding-phases-comparison.jpg` and `welding-smoke-comparison.jpg`.

Purchased assets, generated Unreal packages, screenshots and detailed diagnostics remain private; this repository publishes code and tutorial material only.

## Rounded spark geometry

Spark instances use one startup-built 48-triangle rounded mesh instead of cubes. Velocity stretch produces tapered streaks, and resting particles become round droplets. The bounded particle pool, temperature bins, collision sweeps, cooling and emissive fades are unchanged. Rendering stays opaque and GPU-instanced, preserving consistent visible/NIR/thermal silhouettes without translucent sorting. Roof fixtures retain their existing meshes and shared emissive material.
