# Imported inspection camera

The optional local `assets/Camera/CameraExport.blend` replaces R4's primitive camera housing and lens. The supplied model provides the enclosure, lens barrel, focus hardware, cable, connectors, fasteners and labels. It is an appearance prop for the demo's NIR/thermal sensor; its branding does not establish those capabilities for the depicted real camera.

## Rebuild

Close the demo/editor before replacing its packages. Use Blender 5.0 or newer; the source was authored in Blender 5.0. The recorded conversion used 5.0.1. From the repository root:

```powershell
.\unreal\Scripts\import_inspection_camera.ps1 -Blender 'C:\path\to\blender.exe'
.\unreal\Scripts\build.ps1 -SkipContent
.\unreal\Scripts\run.ps1 -Scene welding -Process -ProcessObserverMode 3
```

The wrapper runs Blender in the background with automatic embedded scripts disabled, then imports into UE 5.8. `-SkipPrepare` reimports an already prepared manifest. The Blender source is never saved over. Its SHA256 and the generated files' hashes are recorded and checked before Unreal import.

Generated FBX, PNG atlases and manifest stay under `assets/derived/inspection_camera/`; Unreal packages live under `/Game/InspectionCamera`. Both are ignored. A code-only checkout without these packages retains the primitive fallback and logs that choice. No purchased geometry or baked texture belongs in Git.

## Conversion and rendering

`scripts/assets/prepare_inspection_camera.py` evaluates the source modifiers and separates two rigid meshes: housing and lens. Source shader graphs, including labels and material mixes, are baked into base-color, tangent-normal and packed roughness/metallic maps. The housing atlas is 2048 square; the lens atlas is 1024 square. The eight source UV layers are temporarily preserved as named corner attributes so baking can use a new atlas UV. Only the atlas UV is exported. Planar lettering is simplified before export.

`unreal/Scripts/import_inspection_camera.py` reuses the existing audited FBX importer, checks local bounds, assigns opaque PBR materials, creates simple collider boxes and builds three mesh detail levels. The imported GPU geometry totals 97,534 / 14,630 / 3,901 triangles at the three levels, with two material sections. Normal-map green is converted for Unreal. The glass is an opaque coated-glass approximation to avoid extra translucent/refraction passes. The asset does not add a tick, a light, a capture or another thermal profile.

The generated geometry is enlarged uniformly by 1.5 and aligned to the existing rigid camera mount. Its visible front rim is at mount-local `(20.5,0,0)` cm; the actual optical origin remains `(21,0,0)` cm looking along +X. The established mount quaternion, arm poses, view FOV calculation, illuminator and three-frame capture settling are unchanged. The housing retains 28 C / emissivity 0.93, and the lens 27 C / emissivity 0.86. These remain illustrative assigned temperatures.

## Recorded verification, 14 September 2026

- UE import preserved the source hash and passed both mesh-bounds checks (maximum error below 0.001 cm).
- Eight deterministic episodes, 64 batches and 192 lossless sensor PNGs were recaptured with seed 4307. Every PNG was byte-identical to the prior six-weld camera fixture. The camera verifier passed 15,979 checks and the thermal verifier passed 162 samples. This supports retaining the existing adapter for these unchanged inputs; it is not a new accuracy measurement.
- Conservative boxes around the imported meshes cleared the parked welder across 25 samples per inspection path leg, including NIR withdrawal via spot 0 and all three stage-family transitions. This is sampled geometry verification, not continuous collision certification.
- The rendered panel-motion fixture includes dedicated visible, NIR and thermal camera closeups. Review the enclosure, lens direction, labels, attachment and resting pose in the actual renders, not only bounds or nonblank-image checks.
- The actual host-model process completed assembly / preassembled / assembly with three departures, 29 image-only requests and all six thermal acceptance checks in 183.52 seconds. The live camera verifier passed 5,636 checks. No model ground-truth fields were sent.
- The panel-motion fixture passed all 41 checkpoints. WeldingSmoke and all nine associated suites passed. Python discovery passed all 109 tests, and the native process-policy fixture passed 91 checks.

Private evidence is under `artifacts/camera-import-v1`, `artifacts/camera-import-pilot-v1` `artifacts/camera-import-motion-v2`, `artifacts/camera-import-legacy-v1` and `artifacts/camera-import-live-v1`. Rebuild and repeat capture comparison when changing the model's size, placement, material lighting paths or mount. Never compensate for a prop change by independently aiming the hidden sensor.
