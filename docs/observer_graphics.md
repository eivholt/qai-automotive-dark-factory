# Observer graphics

Interactive launches use a borderless 2560 × 1440 window. `run.ps1 -Width 1920
-Height 1080` overrides the size. The project enables `bUseBorderlessWindow`
with ordinary windowed mode, so the requested size is retained instead of
expanding to the desktop resolution. Automated capture and smoke fixtures
default to 1600 × 900; sensor targets remain 448 × 256.

On Windows, interactive launches center the borderless window within the full
monitor bounds and place it above the taskbar while focused. Alt-Tab removes
that topmost placement so other applications remain accessible; focusing the
demo restores it. This changes native window stacking only, preserving the
requested client/capture dimensions and ordinary windowed mode. Pass
`-NoTaskbarOverlay` to the launcher to retain normal Windows taskbar stacking.

V-sync is enabled by default for interactive launches. The launcher passes
`-vsync`, overriding older saved preferences, while automated capture/smoke
runs pass `-novsync` to preserve their existing timing. The 60 FPS limit is
retained; V-sync does not request a higher frame rate.

Interactive visible views request DLSS Quality, Lumen GI/reflections and hardware
ray tracing by default when supported; F6/F7/F11 remain their individual toggles.
F11's built-in fullscreen action is disabled in the project and at runtime so
saved input settings cannot consume it before the ray-tracing binding. The
interactive launcher also defaults the visual enhancement pack on; explicit
`-Enhancements:$false` disables it. Automated fixtures preserve their prior defaults.

## OBS capture

Use this setup with the normal interactive launcher:

1. Add a **Window Capture** source and select
   `[UnrealEditor.exe]: DarkFactory (64-bit Development PCD3D_SM6)`.
2. Explicitly select **Windows 10 (1903 and up)** as the capture method
   (Windows Graphics Capture / WGC). Keep **Client Area** enabled. On this
   machine, Automatic chose BitBlt and included an overlapping window.
3. Select **Window title must match** so another Unreal window is not selected
   when this demo closes. Turn off **Capture Cursor** for clean footage.
4. Use **Transform → Fit to Screen** (`Ctrl+F`). If a reused source has an
   invalid position, rotation or size, first use **Reset Transform** (`Ctrl+R`).
5. Enable **Capture Audio** if wanted and disable any duplicate Game Capture
   source to avoid capturing the same audio twice.

The source resolution remains 2560 × 1440. OBS canvas/output resolution is a
separate setting: an existing 1920 × 1080 canvas scales the source to 1080p.
No DX11 fallback or change to borderless mode, DLSS, Lumen or hardware ray
tracing is required for this tested configuration. Launch without
`-RenderOffscreen`; private offscreen smoke launchers are not recording windows.

Local verification on 15 September 2026 used OBS 32.2.2, UE 5.8 DX12 and an
RTX 5090. OBS's **Save Source Screenshot** produced a clean 2560 × 1440 PNG,
with the observer HUD reporting DLSS Quality and Lumen hardware RT. OBS and
Unreal both selected the same GPU. Earlier Game Capture hook attempts failed;
after restarting the demo the hook succeeded, but the old source also retained
an off-canvas position and a 107.88-degree rotation. Therefore the earlier
blank/0 × 0 result does not establish a borderless-window defect. The local
`Scene 2` now uses `DarkFactory - WGC`; its old Game Capture is hidden.
Evidence is private in `artifacts/obs-capture-v1`. This checks capture output,
not recording performance, audio synchronization or model accuracy.

References: [OBS Window Capture](https://obsproject.com/kb/window-capture-sources)
and [Game Capture troubleshooting](https://obsproject.com/kb/game-capture-troubleshooting).

## Visible lighting

Dim visible mode uses 70% of the previous roof-window and headlamp light
output. The reduction is applied to those visible light sources, not camera
exposure; bright mode, NIR/thermal illumination and sensor encoding are retained.

Dim visible and bright-factory views request NVIDIA DLSS Super Resolution Quality, Lumen indirect lighting/reflections, and Lumen hardware ray tracing by default. The HUD reports the active mode; it reports DLSS unavailable when the supported plugin/GPU is missing. The upscaler name is read from the rendered view family on the render thread and copied through a bounded notification to the HUD, so an initializing request is not reported as active NVIDIA rendering.

F6 toggles DLSS, F7 toggles Lumen, and F11 toggles Lumen hardware ray tracing. Hardware RT off retains software Lumen when mesh distance fields are available. Lumen off disables both indirect lighting and reflections. Bright-factory mode applies -1.2 EV observer exposure compensation while Lumen is active to retain white-panel detail; other modes, legacy fixtures and sensor exposures are unchanged. `run.ps1 -NoDLSS -NoLumen -NoHardwareRayTracing` sets startup preferences; each switch can also be used independently. These switches do not disable the project's compiled hardware support or unload its acceleration-structure resources.

NIR and thermal observer modes suspend all three effects and retain the user's visible-view preferences. All fixed and physical R4 sensor cameras explicitly disable Lumen, reflections, hardware RT, temporal AA, motion blur and screen-percentage scaling. The NVIDIA plugin also excludes scene captures. Dynamic R4 cameras register before their custom flags are set, because Unreal registration restores archetype flags. Imported captures pin the renderer shadow/quantization sequence to frame 0; live and offline jobs retain three actual discarded capture renders before publication, and reject a tinted raw source before CPU grayscale conversion. Their lossless 448 x 256 PNG paths and the model's linear 20-120 C thermal window remain independent of observer graphics. Observer-only visible lamps may contribute indirect illumination, while infrared views disable the shared indirect renderer; view-lighting channels continue to isolate direct illumination. Legacy rendered fixtures retain their previous native/FXAA, no-Lumen settings.

## Official dependency

[NVIDIA's DLSS Unreal page](https://developer.nvidia.com/rtx/dlss) supplies a UE 5.8 release. This integration uses official DLSS 4.5 plugin 8.7.2 / NGX 310.6.0 and its StreamlineNGXCommon dependency. It enables Super Resolution, without frame generation or Ray Reconstruction. It does not substitute Unreal TSR and label it DLSS.

```powershell
.\unreal\Scripts\setup_dlss.ps1
.\unreal\Scripts\build.ps1
.\unreal\Scripts\run.ps1 -Process
```

The setup script downloads the pinned official NVIDIA archive, checks its recorded SHA-256, and extracts only the two required plugins under ignored `unreal/Plugins/NVIDIA/`. The archive stays in ignored `assets/runtime_plugins/`. Existing installations are preserved. A source checkout without these private plugins can build the explicit native-rendering fallback; install the plugins and rebuild to enable DLSS. NVIDIA's own license files remain with the local installation; this repository does not redistribute them.

Project renderer support includes DX12/SM6, skin-cache shaders, hardware ray tracing and mesh distance fields. Lumen uses its surface-cache lighting mode. Visible-view overrides enable GI/reflections; global capture defaults remain off. Standalone ray-traced shadows remain disabled to preserve direct-light sensor behavior. The first start after enabling these features can spend time compiling shaders and mesh distance fields. See Epic's [hardware ray tracing guide](https://dev.epicgames.com/documentation/en-us/unreal-engine/hardware-ray-tracing-in-unreal-engine) and [Lumen technical details](https://dev.epicgames.com/documentation/unreal-engine/lumen-technical-details-in-unreal-engine) for platform requirements and resource costs.

## Rendering proof

```powershell
.\unreal\Scripts\run.ps1 -GraphicsSmoke -Output artifacts\observer-graphics-v1
python scripts/verify_observer_graphics.py --directory artifacts/observer-graphics-v1
```

Use a fresh output directory. The fixture runs the physical process scene with inference explicitly disabled. It polls shader/asset compilation to completion, waits for stable startup and freezes one physical pose. It primes all four actual camera targets before taking its first reference, pins the observer render sequence only for this comparison, and uses the fixed 0-120 C observer display to make low-temperature geometry visible. It renders all eight DLSS/Lumen/HWRT preference combinations in each of the four observer modes, recording the actual temporal upscaler, per-view RT permission, screenshots and four raw camera PNGs per state. Each model/fixed NIR/thermal PNG must match its reference byte for byte. The CPU verifier also checks meaningful scene regions, visible rendering differences and unchanged infrared displays. The fixture may wait up to 720 seconds during first-start compilation, with a separate 150-second active-test bound. It does not measure model accuracy or provide a performance benchmark.

## Recorded local result (2026-09-14)

The final fixed 0-120 C observer run is `artifacts/two-bead-graphics-v4-fixed-range`. It passed the native fixture and all 2,012 image/metadata checks. The previous v3 run used a 20-120 C observer window; its reports remain historical evidence.

The UE 5.8 build includes the actual official NVIDIA modules. The final v4 run passed on the RTX 5090 with the fixed 0-120 C observer display. All 128 model/fixed sensor PNGs matched their respective camera references across 32 observer states. Both infrared observer views were unchanged across all eight toggle combinations. NVIDIA runtime logging confirmed Quality reconstruction from 1068 x 601 into 1600 x 900; the rendered view used `FDLSSSceneViewFamilyUpscaler` and enabled per-view hardware RT only where requested.

The final dim and bright views were visually reviewed. The v1-to-v2 exposure comparison in the fixed scene region showed that the bright-floor -0.7 EV compensation reduced near-white coverage (all RGB channels above 245) from 20.71% to 3.48%, with mean RGB falling from 207.73 to 192.20. Dim exposure stayed effectively unchanged. The fixture is capped at 60 FPS and freezes motion; neither the FPS display nor these captures measure a sustained DLSS speedup, Lumen cost, or inference contention. Performance remains unbenchmarked. The final v3 run confirms the corrected render-thread NVIDIA telemetry without the earlier game-thread ensure. The following model-capture pilot also ran without that ensure.

These screenshots are local ignored evidence, available only in a checkout containing the private run artifacts:

![Dim observer with DLSS Quality and Lumen hardware RT](../artifacts/two-bead-graphics-v4-fixed-range/mode0_d1_l1_r1_observer.png)

![Bright observer with DLSS Quality, Lumen hardware RT and compensated exposure](../artifacts/two-bead-graphics-v4-fixed-range/mode3_d1_l1_r1_observer.png)

The final legacy/capture regression in `artifacts/two-bead-legacy-capture-v1` passed all nine welding suites (12,261 checks), native readback plus 10 independent checks with exact sync/async PNG parity, and all four local ephemeral mock bridge cases (G/A/R/ERROR). Actual intermediate robot and IQ9 prop screenshots were reviewed. Two initial choreography failures were stale 86-mesh expectations after the conserved 90-group asset split; only that exact count was corrected, and the original failure evidence is retained. No motion/image thresholds were relaxed. Native `artifacts/two-bead-policy-v1` passed 91 policy checks and the production matching-session report validator. All native jobs exited successfully without engine ensures/asserts/fatal errors. These checks do not establish model accuracy.
