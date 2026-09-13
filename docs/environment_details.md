# Factory environment details

Three static additions give the imported welding cell factory context. They are authored visual examples generated from engine primitives; source scene geometry is unchanged.

1. **Pedestrian paths, service-pad chevrons and station signs.** Two green 110 cm paths run outside the machine enclosure, with white edges and directional arrows. Four black/yellow service pads flank the portal approaches without crossing the carriage track. Four blue signs identify W01–W04 using geometric lettering. This takes the general idea of clearly identified aisles from [OSHA 1910.176(a)](https://www.osha.gov/laws-regs/regulations/standardnumber/1910/1910.176); the chosen colors, widths and sign design are illustrative, not an assessment that this model meets workplace requirements.
2. **Overhead utility routing.** Two ladder-style trays have side rails, cross-rungs, paired suspension rods, junction enclosures and parallel service runs. The visible structure is informed by [Eaton B-Line cable tray and ladder systems](https://www.eaton.com/us/en-us/products/support-systems/cable-tray-and-ladder-systems.html) and its [support-system examples](https://www.eaton.com/us/en-us/products/support-systems.html). These are simplified suspended services, without cable-fill, support-load or electrical calculations.
3. **Welding extraction hoods and ducting.** Two open-bottom rectangular capture canopies sit above the body, with recessed slots, stiffening ribs, suspension rods, round take-offs, duct joints and an upward outlet. [Plymovent FlexHood](https://www.plymovent.com/us/products/flexhood) describes modular hoods for robotic welding; [Lincoln Electric's Prism applications](https://promotions.lincolnelectric.com/prism-solutions/) include individual automotive automation cells and complete lines. The model uses these product types as visual references. It does not predict airflow, fume-capture efficiency or worker exposure, and does not redirect the existing optical smoke simulation.

## Layout and budget

The implementation is ADarkFactoryEnvironmentDetails in unreal/Source/DarkFactory/DarkFactoryEnvironmentDetails.h/.cpp.

| Addition | Source instances | Main placement |
|---|---:|---|
| Paths, chevrons and four signs | 150 | Floor datum 6.3477106 cm; paths at X=1100 and -1320 cm |
| Utility trays and supports | 93 | Two routes at X=±750 cm; lowest geometry Z=762 cm |
| Extraction canopies and ducts | 48 | Hoods at Y=±320 cm; lowest geometry Z=639 cm |
| Total | 291 | Ten instanced mesh/material batches; hard limit 512 instances |

The source floor extends beyond the two paths. The central rail and carriage travel corridor remains free of new equipment. Sign posts sit outside the guard enclosure at X=±960 cm and Y=±360 cm. The high services stay above the earlier sampled full-arm envelope of approximately 545 cm, not merely above the electrode tip. At their default positions, both the observer (Z=610 cm) and fixed sensor (Z=520 cm) look down toward the cell below these additions. This is a layout check; the actual runtime views and robot motion still require visual review.

The actor uses only the engine cube/cylinder and seven dynamic instances of existing M_Factory. This material must have MATUSAGE_INSTANCED_STATIC_MESHES enabled by content generation. No textures, purchased assets, fonts, new shader passes, lights, collision shapes, cameras or CPU image operations are added. Shadow casting and shared GI/reflection contribution are disabled. There is no actor tick.

Opaque geometry remains visible in the normal/NIR scene and participates in ordinary scene depth. It does not write custom stencil or consume any of the 244 existing thermal profile IDs. The thermal shader checks scene-depth/custom-depth agreement and falls back to ambient temperature for an unregistered foreground surface, so a hood does not expose registered hot geometry behind it. No thermal heat field is assigned to these decorative details.

## Integration and review

Call Initialize(Cell) once, then SetEnabled(true) for the live demo or the separate feature QA. Initialization leaves every batch hidden. Root integration keeps the actor disabled in legacy WeldingSmoke captures to preserve those baselines. IsEnabled() reports its current setting.

BuildMetadata() provides initialization/enabled/finite state, counts, the 512-instance budget, category bounds and minimum height, station/hood centers, and explicit zero-light/zero-thermal-profile/zero-collision counts.

The final UE 5.8 build and rendered feature QA pass. The independent feature verifier checks the 291-instance / ten-batch budget, finite transforms, category heights and fixed-sensor isolation. Actual overview and elevated screenshots were reviewed for floor placement, readable W01-W04 lettering, trays and hood geometry. A mirrored sign-glyph layout caught during visual review was corrected before the final render. The combined feature report passes 681 checks; see [validation and limits](realism_additions.md#validation-2026-09-13). These views do not certify all possible free-flight or robot swept paths.

References accessed 2026-09-13. Generated images and runtime artifacts remain private.
