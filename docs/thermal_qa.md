# Thermal GPU QA

The current smoke also captures eight heat-transfer states and runs 138 additional checks. See [heat-transfer implementation and evidence](thermal_heat_transfer.md). The baseline tests below explicitly disable exchange to retain their isolated-field reference; transfer tests enable it and restore the prior scene afterward.

The thermal renderer and this QA flow describe a simulated **apparent temperature** image. They do not provide a calibrated thermal camera, validated process thermography, or a trained thermal classifier. The existing Cosmos adapter contract remains the fixed grayscale NIR inspection input; thermal exports must never be sent to that endpoint as if the modality were interchangeable.

Generate the actual GPU smoke exports from the repository root:

```powershell
.\unreal\Scripts\run.ps1 -ThermalSmoke -Output artifacts\thermal-v1
```

The verifier reads these actual GPU smoke exports from `artifacts/thermal-v1`. It uses Pillow only and does not run Unreal or install dependencies. The existing WSL environment already has Pillow:

```powershell
wsl.exe -e /home/eivho/cosmos-reason2-finetune/.venv/bin/python /mnt/c/Users/eivho/repos/qai-automotive-dark-factory/scripts/verify_thermal.py --directory /mnt/c/Users/eivho/repos/qai-automotive-dark-factory/artifacts/thermal-v1 --output /mnt/c/Users/eivho/repos/qai-automotive-dark-factory/artifacts/thermal-v1/qa.json
```

The portable equivalent is `python scripts/verify_thermal.py --directory artifacts/thermal-v1 --output artifacts/thermal-v1/qa.json`. Exit 0 means every supplied measurement/check passed. Exit 1 writes the failed checks and missing/invalid inputs. A report is rendering evidence only; it cannot approve a thermal model or adapter.

## Required captures

| Artifact | Shape | Frozen comparison |
| --- | --- | --- |
| `normal.png` | 448 × 256 | Baseline temperatures and emissivities |
| `overheated.png` | 448 × 256 | One target module hotter |
| `heater_fault.png` | 448 × 256 | Heater colder than its normal operating condition |
| `sealant_gap.png` | 448 × 256 | A cool gap interrupts heated sealant; neighboring intact sealant stays unchanged |
| `light_on.png`, `light_off.png` | 448 × 256 | Same scene, thermal state, emissivity and view with visible/NIR illumination changed |
| `occluded.png` | 448 × 256 | Same overheated module, covered by an ambient opaque mesh which does **not** write custom depth/stencil |
| `nir_before.png`, `nir_after.png` | 448 × 256 | Identical frozen NIR input before and after all thermal view/export changes |
| `white_hot.png`, `black_hot.png`, `iron.png` | 1600 × 900 | Same stationary observer, thermal scene and display range; only palette changes |
| `runtime.json` | JSON | Material flags, encoding, projected regions and request/readback counters |

Raw thermal PNGs are grayscale RGB with a **linear 20–120 °C apparent-temperature range**, approximately 0.392 °C per 8-bit count. The declared rendering model uses the 8–14 µm band, emissivity/reflected-ambient mixing, and ambient 24 °C. These values describe the configured simulation. A raw export has no palette, HUD, visible-light shading, auto-range changes or NIR grain.

## Runtime metadata contract

Required true booleans are `observer_material_loaded`, `sensor_material_loaded`, `temperature_lut_loaded`, `emissivity_lut_loaded`, `nir_contract_unchanged`, `async_snapshot_matches_raw`, `radiometry_self_test`, `stale_snapshot_rejected`, `stale_preview_rejected`, and `passed`. Required false booleans are `thermal_sent_to_nir_contract` and `adapter_ready`.

`nir_inference_requests_before` and `nir_inference_requests_after` are equal nonnegative integers during the smoke. `thermal_inference_requests` and `cpu_readbacks_in_live_thermal_preview` are zero. Explicit offline PNG exports may perform readbacks; those must not be counted as live GPU-preview readbacks. The verifier also compares `nir_contract_version` and `nir_prompt_sha256` against the repository's actual inspection contract.

```json
{
  "thermal_encoding": {
    "width": 448,
    "height": 256,
    "min_temperature_c": 20,
    "max_temperature_c": 120,
    "ambient_temperature_c": 24,
    "band_um": [8, 14],
    "quantity": "apparent_temperature",
    "linear_u8": true
  }
}
```

`qa_regions` contains `target_module`, `heater`, `sealant_gap`, `sealant_intact`, and `occluder`. Every value is an integer `[left, top, right, bottom]` box in the **raw fixed-camera image**, with exclusive right/bottom bounds and at least 16 pixels. Project inset surface regions from the fixed camera; avoid bounding boxes dominated by unrelated background. The occluder region must sit inside the previously visible hot target and its added cover. These simulator annotations are offline QA data and never model prompt content.

Observer palette checks use `[30, 150, 990, 700]` in the 1600 × 900 screenshot, excluding HUD text. If the HUD exports `qa_screen_regions.thermal_preview`, that same four-integer format enables independent preview palette checks. Otherwise the report explicitly marks preview palette checks as unperformed.

## What the report verifies

The checks use broad image properties rather than exact reference images or copies of the shader equations: grayscale raw pixels; hotter module and colder heater/sealant-gap regions; unchanged intact sealant; ambient appearance where the hot custom-depth surface is hidden behind a main-depth-only occluder; byte-identical raw thermal captures despite visible/NIR light changes; inverse white-hot/black-hot luminance; and chromatic iron colors. Screenshot display encoding can be sRGB, so palette inversion uses luminance correlation instead of requiring an exact pixel sum of 255.

NIR snapshots must be byte-identical and grayscale. Loaded-material/LUT metadata, model-contract identity and request counters provide complementary evidence of inference isolation; image appearance alone cannot establish that no request was sent. The current thermal adapter remains unavailable, so thermal anomalies have no model-driven decision claim.

Optional `mode_frame_ms` maps mode names to positive measured total-frame durations in milliseconds. The report retains sample counts and the first frame, then reports warm mean/p50/p95 after dropping that first frame. Missing timing samples are reported as unmeasured. These numbers are total-frame observations, not GPU-only shader timings, and the verifier imposes no invented FPS or latency target. Use an explicit profiler budget and controlled resolution/render settings for a performance acceptance gate.

## Checked GPU run

The completed `artifacts/thermal-v1` run passed **all 42 checks**, including both observer and fixed-camera preview palettes. No verifier thresholds were changed to obtain this result. The raw cases and all three palette screenshots were also inspected visually.

| Measurement | Actual result |
| --- | --- |
| Overheated module region versus normal | +119 counts, approximately +46.67 °C apparent |
| Faulted heater region versus normal | −99 counts, approximately −38.82 °C apparent |
| Sealant-gap region versus normal | −76 counts, approximately −29.80 °C apparent |
| Neighboring intact sealant | Exactly unchanged |
| Hot surface behind opaque, non-custom-depth cover | 23.92 °C apparent, consistent with quantized 24 °C ambient |
| Visible/NIR lights changed | Raw thermal PNGs byte-identical |
| NIR before/after | PNGs byte-identical and grayscale |
| White-hot versus black-hot correlation | Observer −0.999834; thermal preview −1.0 |
| Async thermal snapshot, radiometry self-test and material/LUT flags | Passed |
| Scene change during pending snapshot | Stale PNG and stale GPU preview rejected |
| Inference requests during thermal smoke / live preview CPU readbacks | 0 / 0 |

The projected raw regions are small inset patches (16–40 pixels), chosen to avoid silhouettes and neighboring objects. They verify these controlled cases rather than general defect coverage. The cover hides the targeted surface; parts of the hot module that remain physically exposed are still visible, as expected.

The measured thermal-preview screenshot region is `[1054, 535, 1348, 703]`. Detailed hashes, regions, statistics and runtime flags are in the generated `artifacts/thermal-v1/qa.json`. The unchanged NIR PNG SHA-256 is `a5ecfe9fe65d7f479939a724d3d83f895a7f9d58ebb58dbe8e2472ec2f06c863`.

No per-mode frame timing samples were supplied in this run, so performance is explicitly reported as **unmeasured**. The run demonstrates rendering, export and inference isolation; it does not establish thermal model accuracy or a performance target.

Portable results and source hashes are retained in [thermal_validation.json](thermal_validation.json). The rendered run used Unreal 5.8.1, D3D12 SM6 and an NVIDIA GeForce RTX 5090. Snapshot metadata was checked for matching dimensions, all 78 part transforms, camera FOV/pose, episode identity and capture-submission time. These checks do not claim GPU exposure timestamps.

## Checked object-local thermal field run

The separate `artifacts/thermal-fields-v1` GPU run passed **all 55 spatial-field checks** and **all 42 existing thermal checks**. No thresholds were changed. The uniform, reference, cooled, translated and rotated raw frames were inspected visually: the warm interior and cooler border stay attached to each module, while the thin ribs cool more strongly with age.

Run both independent reports with Python and Pillow:

```powershell
python scripts/verify_thermal_fields.py --directory artifacts/thermal-fields-v1 --output artifacts/thermal-fields-v1/field_qa.json
python scripts/verify_thermal.py --directory artifacts/thermal-fields-v1 --output artifacts/thermal-fields-v1/qa.json
```

Use the existing WSL interpreter shown above if Pillow is unavailable in the Windows Python environment. Each command writes a JSON report and exits 0 only when its checks pass.

The additional field captures are `field_reference.png`, `field_translated.png`, `field_rotated.png`, their corresponding `field_*_ids.png` images, `field_uniform.png`, and `field_cooled.png`. All eight are grayscale 448 × 256 PNGs. `field_regions.json` declares projected sample boxes and stencil IDs for the body core, near/far body edges, thin rib and powered heater in each pose, plus the two cooling ages. Visible-ID images are offline QA annotations and never enter sensor exports or model inference.

Every sample filters its projected box by the declared visible stencil ID and requires at least three matching pixels. The checks compare broad temperature differences rather than reproduce the shader equations. They require warmer body cores than both same-face edges and thin ribs; similar local temperatures after actual translation/rotation; cooling of the body and rib, with a larger fractional excess-temperature loss in the rib; a steady powered heater; and a flat uniform control. ID membership and changed body masks guard against sampling neighboring surfaces or reusing an unchanged pose image.

| Field measurement | Actual result |
| --- | --- |
| Reference body core | 40.88 °C apparent |
| Reference near/far body edges | 32.43 / 32.50 °C apparent, approximately 8.4 °C cooler than the core |
| Reference thin rib | 31.32 °C apparent |
| Largest sample-mean spread across all three poses | 1.134 counts, approximately 0.445 °C; body-core spread 0.125 count |
| Uniform-control body samples | Exactly flat across all 48 selected pixels |
| Body core, age 15 → 45 seconds | 40.88 → 38.87 °C apparent; 11.90% of excess above 24 °C ambient lost |
| Thin rib, age 15 → 45 seconds | 31.32 → 25.91 °C apparent; 73.93% of excess above ambient lost |
| Powered heater, age 15 → 45 seconds | Exactly unchanged at 62.67 °C apparent |

The existing anomaly, light-independence, opaque-occlusion, palette, async-snapshot and NIR-isolation checks also passed on this run. The recorded body/edge/rib samples contain 14–16 correctly identified pixels per 4 × 4 box. Detailed hashes, sample coverage, temperatures and checks are in `field_qa.json`; the independent existing guards are in `qa.json`. This evidence describes the procedural thermal field and apparent-temperature rendering, not a calibrated heat-conduction model or a trained thermal adapter.
