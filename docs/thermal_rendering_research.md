# Thermal rendering implementations

Research checked 2026-09-11 against primary source, author documentation and the AirSim-W paper. This is design evidence, not a benchmark or proof of physical thermal accuracy. No third-party code or material graphs were copied into this repository.

## What the existing implementations provide

| Implementation | Thermal representation | Useful pattern | Main limitation |
| --- | --- | --- | --- |
| Gazebo / Ogre2 | Assigned Kelvin values or UV heat-signature textures | Separate surface heat data from sensor conversion | No emitted/reflected radiance transport in the inspected shader |
| AirSim-W | Temperature/emissivity/response calculation converted to 8-bit object IDs | Precompute a radiance lookup before rendering | Uniform object values; simplified surroundings and optics |
| Cosys-AirSim | Infrared camera plus grayscale/RGB/texture annotation layers | Per-surface textures and independent capture layers | Annotation values do not constitute temperature simulation |
| Unreal stencil examples | Object masks recolored in a post-process material | Explicit visibility test and compact lookup | Gameplay highlighting unless backed by heat data |

### Gazebo: heat signatures rather than a palette alone

Inspected `gz-rendering9` at **f1e4c374f393a50fb0a72922d0ee315eebbbff3b**, under [Apache-2.0](https://github.com/gazebosim/gz-rendering/blob/f1e4c374f393a50fb0a72922d0ee315eebbbff3b/LICENSE). Its [material switcher](https://github.com/gazebosim/gz-rendering/blob/f1e4c374f393a50fb0a72922d0ee315eebbbff3b/ogre2/src/Ogre2ThermalCamera.cc#L278) accepts uniform temperatures or heat-signature textures with minimum/maximum Kelvin bounds. These are assigned surface fields, not a heat-transfer solver. The [tutorial](https://gazebosim.org/api/sensors/9/thermalcameraigngazebo.html) documents opaque occlusion: an intervening object hides the hot object; heat conduction into the cover is not simulated.

The [conversion shader](https://github.com/gazebosim/gz-rendering/blob/f1e4c374f393a50fb0a72922d0ee315eebbbff3b/ogre2/src/media/materials/programs/GLSL/thermal_camera_fs.glsl#L56) distinguishes heat data using reserved color channels, avoiding a separate heat-mask pass. Untagged surfaces receive ambient temperature with optional RGB/depth-derived variation. It clamps temperature and encodes temperature divided by resolution. No emissivity, reflected environment or spectral integration appears in this shader.

The [compositor](https://github.com/gazebosim/gz-rendering/blob/f1e4c374f393a50fb0a72922d0ee315eebbbff3b/ogre2/src/Ogre2ThermalCamera.cc#L993) performs a scene pass and a fullscreen conversion pass. Conversion samples color and conditionally depth; heat-texture sampling happens during scene rendering. [PostRender](https://github.com/gazebosim/gz-rendering/blob/f1e4c374f393a50fb0a72922d0ee315eebbbff3b/ogre2/src/Ogre2ThermalCamera.cc#L1153) downloads the output through `convertFromTexture`. These operations are source observations, not measured timings.

### AirSim-W: simplified radiometry feeding segmentation

Inspected Microsoft AirSim at **d109f0dc25d4ae425eca7b279da33df5f5fcb42e**, under [MIT](https://github.com/microsoft/AirSim/blob/d109f0dc25d4ae425eca7b279da33df5f5fcb42e/LICENSE). The [Python implementation](https://github.com/microsoft/AirSim/blob/d109f0dc25d4ae425eca7b279da33df5f5fcb42e/PythonClient/computer_vision/create_ir_segmentation_map.py#L8) integrates Planck radiance over 8–14 micrometres, weighted by average emissivity and optional camera response. It normalizes the supplied objects' radiances to 8-bit counts and assigns those counts as segmentation/stencil IDs. The expensive integration occurs during preparation, not in every rendered pixel. This adds physical motivation to object recoloring without producing spatial temperature variation inside an object.

The [AirSim-W paper, sections 4.1–4.4](https://www.andrew.cmu.edu/user/feif/Publications/2018_COMPASS_airsim-w.pdf) explicitly neglects atmospheric and background/reflected contributions under its savanna assumptions, and assumes ideal lens transmission. Its Gaussian blur and several noise terms are simplified approximations. Those assumptions should not be transferred uncritically to reflective factory machinery.

### Cosys-AirSim: useful annotation machinery, verify the recipe

Inspected **c82acf9742446c24d6d59c3e33e4d34f8a547ffc**, under [MIT](https://github.com/Cosys-Lab/Cosys-AirSim/blob/c82acf9742446c24d6d59c3e33e4d34f8a547ffc/LICENSE). Its [infrared guide](https://github.com/Cosys-Lab/Cosys-AirSim/blob/c82acf9742446c24d6d59c3e33e4d34f8a547ffc/docs/InfraredCamera.md) repeats the AirSim-W recipe, but the two referenced Python scripts are absent from this pinned tree. Treat that recipe as unverified on this revision.

The actual [annotation system](https://github.com/Cosys-Lab/Cosys-AirSim/blob/c82acf9742446c24d6d59c3e33e4d34f8a547ffc/docs/annotation.md) supports proxy meshes carrying grayscale values, RGB values or fitted textures. Grayscale has uint8 precision; default inclusion matters because excluded meshes cannot occlude in that annotation layer. [AnnotationComponent.cpp](https://github.com/Cosys-Lab/Cosys-AirSim/blob/c82acf9742446c24d6d59c3e33e4d34f8a547ffc/Unreal/Plugins/AirSim/Source/Annotation/AnnotationComponent.cpp#L379) updates material color and texture data, not heat transport. This is useful infrastructure for authored heater or glue heatmaps. Its [readback code](https://github.com/Cosys-Lab/Cosys-AirSim/blob/c82acf9742446c24d6d59c3e33e4d34f8a547ffc/Unreal/Plugins/AirSim/Source/RenderRequest.cpp#L99) waits for request completion and reads surface data; moving work to a render thread does not remove GPU-to-CPU transfer cost.

### Unreal gameplay examples: masks need visibility checks

Neil Gallardo's [thermal-scope material graph](https://neilgilbertg.ca/2024/09/27/unreal-post-process-based-scopes-part-2-2/) uses a stencil bit, SceneDepth/CustomDepth comparison and a chosen thermal color. It is an inspectable gameplay implementation, not thermometry. SilverB1rd's [published HLSL silhouette shader](https://forums.unrealengine.com/t/custom-stencil-radial-silhouette-post-process-materiel-hlsl-pc-full-code-4-13/77049) shows why optional effects need scrutiny: neighboring stencil samples scale with the configured radial sample count. Neither page states a specific open-source reuse license; both are references only.

Epic's [post-process documentation](https://dev.epicgames.com/documentation/en-us/unreal-engine/post-process-materials-in-unreal-engine) notes that CustomDepth adds mesh draws even though it only renders depth, and some stencil examples deliberately reveal occluded objects. A thermal sensor must select the opposite visibility behavior.

## Recommendation for this demo

Use a **stencil profile ID → surface heat lookup → sensor mapping** pipeline. Keep IDs separate from brightness. Reject hidden profiles by comparing SceneDepth and CustomDepth in matching depth units with a small tested tolerance. Opaque covers must remain occluders. Keep the observer palette and gain independent of the fixed thermal sensor's mapping.

Start with assigned temperatures and explicitly approximate apparent heat. Later add UV heat fields for missing glue, disconnected heater strips and cooling gradients; a uniform object ID cannot express those defects. Preserve higher-precision heat data internally if quantitative temperature work becomes necessary. Keep the model's grayscale window fixed and recorded rather than inheriting observer auto-ranging.

For windows and polished metal, prioritize reflected surroundings and material-specific response: [FLIR's glass inspection note](https://support.flir.com/appstories/AppStories/Electrical%26Mechanical/Testing_solar_panels_EN.pdf) describes specular thermal reflections even for high-emissivity glass. Visible transparency is not thermal transparency. A simple emissivity-times-temperature brightness rule is an artistic proxy, not radiance.

Combine visibility, lookup and palette in one conversion pass; account separately for CustomDepth geometry draws and each camera capture. Keep previews on the GPU, read back only required model/save frames, and measure transfers and encoding separately. Validate opaque occlusion, heat-pattern visibility, observer/sensor independence and fixed intensity mapping. Until wavelength response, emissivity, reflection, optics and detector behavior are calibrated, describe this as a thermal rendering proxy.
