# Industrial surface finishes

The free Unity Industrial Props Kit and War FX packs were considered on September
15, 2026. Their standard Asset Store license was not selected for this workflow:
[Unity's Asset Store terms](https://unity.com/legal/as-terms), section 2.2.1.1(g),
require express permission for AI/ML uses, including training and input data.
Unity particle prefabs and shaders also require reauthoring for Unreal. No Unity
Store content was imported.

This is not a blanket exclusion of Unity-compatible content. Some assets have
separate provider or third-party licenses, and explicit permission can change
the assessment. Review each candidate individually. Keeping this demo and its
training data on the user's computers addresses redistribution, but the standard
terms also cover AI inputs/datasets for non-commercial purposes (§3.8(v));
private use alone is not an express exception.

Instead, this project optionally reuses the user's local NVIDIA Isaac 6.0
DigitalTwin `Rubber_Black_Rough_A` and `Metal_Rough_A` texture sets from the adjacent
Isaac demo. These are private inputs, not redistributed assets or a claim of a
new license grant. The original MDL files and a SHA-256 provenance manifest remain
beside the copied textures under ignored `assets/derived/industrial_materials`.

Run `python scripts/assets/prepare_industrial_materials.py` (or pass `--source`
with the OmniversePayload directory), then `unreal/Scripts/build.ps1`. The source
repository is read-only. Content setup rebuilds both native Unreal materials;
code-only installations retain the original plain materials when these optional
generated packages are absent.

The finish applies to the existing 20 spline hose sections, dispenser feed tube,
four rubber gripper pads, and metallic tool fittings. Purchased painted H-bars,
robot castings, car materials, thermal profiles and physics remain unchanged.
It adapts the supplied albedo, normal and packed ORM maps to the existing tint,
roughness and metallic parameters; it is not an exact MDL renderer translation.
ORM green controls roughness; authored metallic values remain authoritative.

Two shared texture sets use three GPU samples each, mipmaps and a 1024-pixel
maximum texture size. No geometry, draw calls, tick work or particle systems are
added. Actual frame-time cost depends on screen coverage; this is not a measured
GPU speedup. Normal strength is deliberately restrained for close-ups.

Post-process UserFlags bit 8 enables the finish only in visible observer modes
and the Editor preview. Every scene capture clears this bit. NIR/thermal observer
modes and legacy fixtures return the original base color, roughness, metallic
and flat normal. `-DFNoIndustrialFinish` disables the runtime observer finish for
comparison. The finish must not be treated as measured infrared reflectance.

Validation: build, Python discovery, rendered WeldingSmoke and its existing
verifiers, plus GraphicsSmoke for fixed/R4 sensor parity. Run
`verify_industrial_finishes.py` through Editor `-ExecCmds="py <absolute path>"`
for before/after hose and gripper close-ups. Screenshots remain private.

September 15 validation: UE 5.8 build and all 121 Python tests passed.
GraphicsSmoke passed 2,012 checks. The fixed NIR, fixed thermal, R4 NIR and R4
thermal baseline PNG SHA-256 hashes also match the prior dim-light fixture
exactly; this checks preservation across the material change, in addition to
within-run observer-setting parity. This is rendering evidence, not a new model
accuracy evaluation.
Rendered WeldingSmoke and all nine follow-up verifiers passed, covering hose
motion, choreography, sparks, tool/body/motor cooling and factory lighting.
