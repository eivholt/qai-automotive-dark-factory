# Night-vision display QA

Generate actual fixed 1600 x 900 runtime screenshots and model-camera files from the repository root:

```powershell
.\unreal\Scripts\run.ps1 -NightVisionSmoke -Output artifacts\night-vision-v1
```

The smoke mode disables inference, holds the scene still and freezes its sensor frame while rendering green, a second grain frame, monochrome, and a dark observer with a lit preview. It then **recaptures the sensor** with the original augmentation frame/seed. Comparing that new capture with the original also verifies that observer display changes do not affect sensor rendering.

Run the verifier with a Python environment containing Pillow:

```powershell
python scripts/verify_night_vision.py --directory artifacts/night-vision-v1 --output artifacts/night-vision-v1/qa.json
```

If needed, install Pillow in this repository's virtual environment: `python -m pip install Pillow`. On the development host, verification used the already available WSL environment without installing packages or changing its services:

```powershell
wsl.exe -e /home/eivho/cosmos-reason2-finetune/.venv/bin/python /mnt/c/Users/eivho/repos/qai-automotive-dark-factory/scripts/verify_night_vision.py --directory /mnt/c/Users/eivho/repos/qai-automotive-dark-factory/artifacts/night-vision-v1 --output /mnt/c/Users/eivho/repos/qai-automotive-dark-factory/artifacts/night-vision-v1/qa.json
```

Exit 0 means every check passed; exit 1 writes a report explaining failures or missing files. The report includes file hashes, crop bounds, RGB statistics, temporal differences, thresholds and runtime material flags. It checks both materials loaded, green observer and preview views, neutral monochrome views, animated grain on a stationary scene, a dark observer with an independently visible preview, that the sensor remains infrared, and **byte-identical grayscale model PNGs**. HUD labels and borders are excluded from measurements. Missing shaders, static fallback frames or changed model inputs must fail.

All **20 checks passed in each fast/quality variant** on UE 5.8.1 on 11 September 2026. The [portable report](night_vision_validation.json) records this run and its shader-source hashes. Actual screenshots appear in [the night-vision guide](night_vision.md).

These broad image-property checks verify rendered display behavior, not physical infrared accuracy, model accuracy, or the exact mathematical shader implementation. Temporal differences are attributed to the display only when running the supplied stationary/frozen-input smoke mode. Do not substitute interactive screenshots. Use a fresh output directory for each validation run so stale screenshots cannot be mistaken for current output.

## GPU transfer and live bridge checks

```powershell
.\unreal\Scripts\run.ps1 -ReadbackSmoke -Output artifacts\gpu-readback-check
.\unreal\Scripts\run.ps1 -NightVisionSmoke -NightVisionQuality -Output artifacts\quality-check
python scripts/verify_night_vision.py --directory artifacts/quality-check --output artifacts/quality-check/qa.json
python scripts/verify_unreal_bridge.py
```

ReadbackSmoke first runs the live GPU preview without CPU readback, then compares the old deterministic synchronous reference with a fresh asynchronous readback from that same GPU texture. It also rejects a completed old-epoch capture and a cached preview from an obsolete epoch. `readback.json` records those checks; `model_reference.png` and `model_async.png` must have identical hashes. This mode performs one synchronous reference readback solely for the parity test.

The bridge verifier creates its own temporary loopback mock services and tests G, A, R and a disconnected backend using the live async path. Each report requires zero synchronous readbacks and zero CPU preview uploads. The [portable evidence](gpu_capture_validation.json) includes those results and clearly scopes its single-sample enqueue diagnostic. These tests do not measure model accuracy or final GPU frame rate.
