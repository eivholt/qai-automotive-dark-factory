# Factory realism and production feedback

Ten additions build on the imported automotive welding preview. They are code-generated details; purchased assets and generated Unreal content, audio auditions and screenshots remain private. The existing inspection adapter/model guards remain in place.

| # | Addition | Behavior |
| --- | --- | --- |
| 1 | Floor lanes and station markings | Side walkways, threshold chevrons and W01-W04 identification. |
| 2 | Overhead utilities | Ladder-style cable trays, supports and service runs above robot travel. |
| 3 | Extraction infrastructure | Open-bottom welding hoods and ducts above the cell. |
| 4 | Body fixture clamps | Close after arrival, remain secured during robot operation and release before departure. |
| 5 | Stack-light tower | Indicates machine transfer/clearance or secured operation; it is not a quality decision. |
| 6 | Photoelectric presence sensors | Entry/exit occupancy follows actual visible carriage collider intersections. |
| 7 | Spatial soundscape | Bounded synthesized conveyor, robot, welding and extractor sounds follow the production clock. |
| 8 | Production-status display | Cycle progress, body sequence and four individual robot phases from simulation state. |
| 9 | Observer camera bookmarks | Five inspection viewpoints, smooth transitions and immediate manual override. |
| 10 | Persistent weld witness marks | Cosmetic electrode rings develop at the four authored seam contacts and remain until the body is replaced. |

## Research and design choices

KUKA describes body-in-white parts being joined in geometrical fixtures and lists spot welding among the principal processes. This motivates showing the body being located and clamped before robot work, rather than leaving it apparently unsupported. The added fixture motion is illustrative; it is not a physical clamping or industrial interlock simulation. [KUKA body-in-white production](https://www.kuka.com/en-us/products/production-systems/systems/karosseriebau)

TWI explains that electrode pressure normally leaves a surface impression during resistance spot welding. The demo adds a burnished center and a darker annulus through the existing sheet material's color/roughness branches. The 3.6 cm displayed diameter is deliberately enlarged for readability. It does not displace mesh geometry, change temperature, or establish weld quality. It follows each robot's own weld start and resets at the replacement-body cycle boundary. Existing hot glow and thermal cooling remain independent. [TWI surface marking on resistance spot welds](https://www.twi-global.com/technical-knowledge/faqs/faq-how-can-i-minimise-the-surface-marking-on-resistance-spot-welded-joints)

Epic's camera system separates the player view from other camera actors. The bookmarks move only the existing observer pawn, retaining its attached headlamp/NIR lamp, palette and free-flight controls. Fixed sensor camera transforms do not change. A 1.65-second eased transition includes a modest vertical arc; any manual camera input cancels it. These are authored spectator paths, not collision-certified routes. [Epic camera overview](https://dev.epicgames.com/documentation/unreal-engine/cameras-in-unreal-engine)

The supporting research and implementation constraints for items 1-3, 4-6 and 7-8 are in [environment details](environment_details.md), [cell mechanisms](cell_mechanisms.md), and [production feedback](production_feedback.md).

## Controls and reproduction

- **B / Xbox X:** cycle line overview, weld inspection, opposite side, line exit and utilities overview.
- **Home / Xbox Menu:** return to the observer home viewpoint.
- **Q/E / Xbox triggers:** elevation eases in and brakes more quickly on release; home, bookmarks and automated captures clear residual elevation motion.
- **M:** mute/unmute factory sounds.
- Existing **Tab / Xbox Y**, **K / D-pad left**, and **Space / Xbox View** retain modality, palette and cycle-pause behavior.

Run `unreal/Scripts/build.ps1` to build code and regenerate materials. Existing imported PBR materials are augmented by `setup_weld_witness.py`; the same generator runs during content setup. It preserves the original source color/roughness branches and existing thermal/glow code, defaults to disabled on shared materials, and verifies repeated application does not duplicate nodes. The marker's analytic mask runs on the GPU; it needs no texture asset or additional capture.

`run.ps1 -Scene welding -RealismSmoke -Output artifacts/realism` runs the new additions in a deterministic rendered inspection. Existing `-WeldingSmoke` fixtures explicitly disable them so earlier thermal and motion regressions keep their original environment. New decoration surfaces use opaque scene-depth occlusion and ambient thermal fallback; the 244 registered thermal profiles and raw thermal encoding stay unchanged.

## Validation (2026-09-13)

The UE 5.8 Development build and 34 Python unit tests pass. The final native feature run, stored privately in `artifacts/realism-final`, passes all 681 independent checks in `scripts/verify_realism.py`. It samples 12 cycle stages, all five viewpoints, both presence-sensor crossings, all four mesh-fitted clamp contacts, witness growth/reset, fixed-camera isolation and synthesized audio. The independent schedule check covers all 64 robot schedules in the 16-cycle repeat bank. All nine existing welding regression suites also pass: 12,261 checks in `artifacts/realism-regression`.

Fixed NIR and raw thermal PNGs match exactly across the frozen observer modality/viewpoint changes. A cooled witness on/off comparison changes 2,559 main-view pixels by at least three code values while raw thermal remains identical. The 1.65-second camera transition reaches the independently expected midpoint and finishes within 0.000016 cm of its endpoint. Actual rendered images were reviewed for station lettering, floor marks, elevated utilities, clamps, HUD placement and the cooled weld ring. Both the 1600 x 900 and 1280 x 720 HUD layouts keep production status, sensor previews, thermal scales and controls readable.

A read-only Unreal material audit verifies all ten imported materials retain connected source color branches, including all three texture-linked materials. Replaying the witness generator reports all ten unchanged; no duplicate material nodes are created. Materials and screenshots remain ignored local artifacts.

The six audio kernels produce finite bounded samples, identical repeated PCM and exact silence after mute settles. The 21-second first-cycle offline audition peaks at 0.1627 with RMS 0.0238, before Unreal's spatial mixer. A live-device listening/balance check remains separate from this numeric evidence.

The static environment uses 291 instances in ten mesh/material batches, no tick and no new lights. Mechanisms add 57 primitive mesh components; audio has six fixed sources. Witness masks run in the existing GPU material. These additions introduce no sensor captures, image readbacks or thermal profile IDs. These are resource bounds, not a measured end-to-end FPS improvement.
