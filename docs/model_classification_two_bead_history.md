# Two-bead panel inspection with Cosmos Reason2

The current experiment uses `automotive-panel-process-v3`: exactly two longer, rounded front adhesive beads and three weld contacts. The rear-middle adhesive bead no longer exists. NIR and thermal evidence comes from the physically mounted R4 camera. Observer color, grain, Lumen, ray tracing and temporal upscaling are kept out of model captures; actual sensor parity still requires native verification after graphics changes.

## Selected host runtime

Status recorded on **14 September 2026**: training is complete and the selected checkpoint is saved. The initial 128-update report was saved at **17:48:32 CEST (UTC+02:00)**; the selected continuation at **18:10:12 CEST**. These are the local saved-report timestamps. The two optimization loops took 374.940 and 367.844 seconds: **12 minutes 23 seconds in total**, across 256 updates. Capture preparation, model loading, evaluation, rendering, build checks and integration verification account for additional task time; a long-running development task does not mean the optimizer ran for its entire duration. Restarting Codex does not require retraining a completed, saved adapter.

The selected two-bead adapter is `checkpoints/panel-two-bead-shared-256-v3`, running with Torch BF16, a merged LoRA and explicit `--linear-patch-embedding`. It is frozen after validation and the one-time held-out evaluation. It now serves on default loopback 18191 after the successful native integration run. Exact health/provenance matches the frozen selection; the duplicate 18192 candidate is stopped. Old three-bead adapters remain incompatible with this v3 contract.

These commands assume a prepared Linux/WSL2 Python environment with a CUDA-enabled Torch build and a compatible NVIDIA GPU/driver. The local run used the existing `~/cosmos-reason2-finetune/.venv` environment, including Torch 2.9.0+cu128, Transformers 4.57.3, PEFT 0.20.0, Accelerate 1.12.0 and Pillow 12.0.0. The [optional requirements](../scripts/training/requirements.txt) record those package versions; select the appropriate CUDA wheel for your host rather than treating that file as a complete GPU/driver installer. BF16 is required for this recorded runtime, and the server refuses a silent CPU/mock fallback.

`DF_BASE_MODEL` must point to a separately obtained, complete, unadapted Cosmos-Reason2-2B directory with `config.json`, `model.safetensors` and its processor/tokenizer files. `DF_MODEL_PYTHON` selects the existing environment. The adapter directory must contain compatible saved weights, adapter configuration and `training_report.json`. The launcher forces offline loading; it creates neither an environment nor a base model or trained checkpoint. Keep this environment separate from any adjacent project's production service.

```bash
# From the repository root inside WSL; requires the private selected checkpoint.
export DF_MODEL_PYTHON="$HOME/cosmos-reason2-finetune/.venv/bin/python"
export DF_BASE_MODEL="$HOME/cosmos-reason2-finetune/base/Cosmos-Reason2-2B"
bash scripts/model/start_host_service.sh --port 18191 \
  --adapter checkpoints/panel-two-bead-shared-256-v3 --linear-patch-embedding
```

First reuse a healthy compatible service if one already exists. Otherwise, this start-only launcher submits the host to the WSL systemd user manager and prints `STARTING`; success means the process was started, not that GPU loading and warmup are finished. Check readiness from PowerShell before launching Unreal:

```powershell
Invoke-RestMethod http://127.0.0.1:18191/health |
    Select-Object status, contract_version, backend_alias, mock
.\unreal\Scripts\run.ps1 -Scene welding -Process -ProcessObserverMode 3 `
    -ProcessEndpoint http://127.0.0.1:18191/v1/inspect
```

Start another host only when the endpoint is stopped. A ready service must report `automotive-panel-process-v3`, `mock: false` and the exact selected alias below. The private checkpoint and purchased assets are local prerequisites; neither the public launcher nor a documentation update downloads or recreates them. Use the [asset/build tutorial](dark_factory_tutorial.md) before the first launch.

The exact backend alias is `Cosmos-Reason2-2B-Torch-BF16-dccfe9dbc77f3fde4c565ecb826a8b84-linear-nonoverlap-v1`. Adapter weights SHA256: `c39011c523ae17c1a9148bc8a9b38f1bb9d0a568a34671c4d47488954323c059`. The health response includes the model, processor, adapter and projection provenance. Stock Conv3d remains the general server default; this selected runtime explicitly enables the measured linear projection. Old [panel-v2 evidence](model_classification_panel_v2_history.md) and [door-process evidence](model_classification_v1_history.md) retain their historical scope.

| Frozen runtime evidence | Labels | Gate actions | False G | Wrong presence branch |
| --- | --- | --- | --- | --- |
| Validation, 12 episodes / 96 batches | 95/96 | 96/96 | 0 | 0 |
| Held-out, 12 different episodes / 96 batches | 95/96 | 95/96 | 0 | 0 |

Validation's single error was hot adhesive R classified A, which still waits. The held-out error was a cool weld classified A, delaying release. Held-out NIR presence/seating and adhesive were all correct; clear-cool recall was 12/12 for adhesive and 11/12 for weld. These are small synthetic-camera evaluations, not evidence of industrial safety or EVK performance. No training, policy change or reselection used the test predictions.

Held-out standalone HTTP median/p95 were 49.28/54.55 ms; GPU forward/scoring median was 41.17 ms. These timings exclude arm motion, capture scheduling and physical cooling. The actual native run completed assembly/preassembled/assembly with three forward departures in 182.31 wall-clock seconds (164.50 simulation seconds), with all six thermal acceptance audits below their limits. Its 30 image-only requests included no ground-truth fields; camera and curtain audits passed 5,830/27 checks. Median/p95 HTTP event latency was 84.43/92.83 ms while Unreal rendered. These 30 reply events exclude duplicate phase/body-completion metadata. Live execution is separate operational evidence from the held-out labels.

The later H-bar gripper, shorter stem and fixed-base EVK layout were recaptured separately; the final geometry is `panel-tool-refit-episodes-v3`. With the selected weights and policy unchanged, the same 12 validation episodes again give 95/96 letters and 96/96 gate actions, zero false G and zero wrong presence branches. Standalone HTTP median/p95 were 54.70/61.41 ms. This is a geometry-compatibility check, not an additional held-out evaluation; the original test score above retains its original capture scope. Earlier v1/v2 refit captures remain separate development records.

## Host service lifecycle

The fixed unit is `dark-factory-panel-model.service`. It runs independently of the launch shell and Codex for the current WSL session. This is a transient user service, not Windows-reboot or WSL-shutdown persistence. If systemd user services are unavailable, run `bash scripts/model/start_host.sh` with the same model arguments in a foreground WSL terminal and leave that terminal open; Ctrl+C stops that foreground host.

The helper forwards model arguments unchanged and passes `DF_MODEL_PYTHON`, `DF_BASE_MODEL` and the caller's `PYTHONPATH` explicitly into the service. The foreground launcher prepends this repo's `src` directory and retains offline model loading. Relative model/adapter paths resolve from the repository root. An existing unit or occupied loopback port causes an early refusal before model loading; the helper never replaces or kills another service. Reuse a `/health` response with `status: ready`, the v3 contract, `mock: false` and the selected alias above. A different model at that port must be inspected before choosing a deliberate replacement.

From WSL, use these commands to inspect or explicitly stop only this demo's host:

```bash
systemctl --user status dark-factory-panel-model.service
journalctl --user -u dark-factory-panel-model.service -n 80 --no-pager
# Follow startup/restart logs; Ctrl+C exits the log viewer without stopping the host.
journalctl --user -u dark-factory-panel-model.service -f
# Explicit stop, when model inference is no longer needed:
systemctl --user stop dark-factory-panel-model.service
```

The service restarts on failure after five seconds, with a limit of three starts per 120 seconds. An explicit stop does not restart it. If the limit is reached, inspect and fix the reported error; use `systemctl --user reset-failed dark-factory-panel-model.service` if a failed unit remains, then start the helper again. A stopped, collected transient unit may already be absent. The helper does not automatically retry in a shell loop or fall back to mock inference.

If the demo reports that the model is unavailable at its first gate, check `/health` and these logs. An absent listener is a serving-lifecycle problem, not evidence that training or the saved adapter was lost. Start the selected service only when absent and wait for readiness. A running Unreal process holds until compatible fresh replies arrive; no clock or scene-ground-truth fallback releases it. If the server is ready but Unreal remains held, inspect the current demo log for a contract/provenance, image or transport error before changing the model.

## Four gates, three actual views

| Stage | Input | G | R | A |
| --- | --- | --- | --- | --- |
| `panel_presence` | Three NIR views | Installed: preassembled branch | Clearly missing: assembly branch | Hidden, partial or uncertain |
| `adhesive` | Three thermal views of two real beads | Both beads visible; maximum below 42 C | Either bead above 48 C: continue cooling | Maximum 42-48 C inclusive, hidden or uncertain |
| `panel_seated` | Three NIR views | Panel positioned and flush | Missing/displaced/lifted: exception hold | Required contacts hidden or uncertain |
| `weld` | Three thermal weld-contact views | Maximum below 52 C | Any contact above 58 C: continue cooling | Maximum 52-58 C inclusive, hidden or uncertain |

All stages use `spot1`, `spot2`, `spot3` in that order. Adhesive views map to physical beads `[0,1,0]`: first bead, second bead, then a different physical perspective of the first bead. This is three camera observations of two objects. Weld views still map to three distinct contacts. The scene must not invent a third adhesive temperature or require a nonexistent rear bead.

Every PNG is lossless 448x256 grayscale, with 112 visual tokens per image and 336 per request. Thermal remains linear 20-120 C apparent temperature. The 45/55 C nominal limits and +/-3 C hold bands illustrate process readiness; they are not cure or structural-weld specifications. The observer palette spans 0-120 C independently; it never changes the 20-120 C model pixels.

## Versioned image-only protocol

The exact prompts and hashes are in [process_inspection.json](../configs/process_inspection.json). The new adhesive prompt explicitly describes two beads and the repeated first-bead view. Even stages with unchanged prompt text use the new contract version, so prior adapters are rejected before tensor loading.

`POST /v1/inspect` accepts only `contract_version`, `request_id`, `body_id`, `stage`, `captured_at_ms`, and exactly three `{camera_id,png_base64}` entries. One user message contains the ordered images with newline separators, followed by the stage prompt. The base chat template adds vision/chat boundaries and the assistant-generation boundary, but no default system message. Ground truth, labels, temperatures, filenames, seeds, case names, HUD and previous decisions never reach the model.

The deterministic classifier scores one next-token G/A/R distribution with no reasoning/prose decode, image-token cache or reused KV state. The policy stays unchanged: unmasked letter mass>=0.5 and conditional winner score>=0.8. These scores are not calibrated correctness probabilities. Two matching fresh G or R batches choose the presence branch; all other stages need two G. Errors, malformed images, mismatched provenance, opposite decisions and replies older than 8 seconds hold/reset consensus. Thermal R means wait; seating R is an exception hold. No scene-truth fallback may override the model.

Replies echo identity, camera order, modality, prompt hash, message layout, policy, model/runtime alias and timings. PNG CRC, dimensions, equal RGB channels and opaque alpha are enforced. Native integration verifies these fields and rejects stale or cross-version replies.

## Capture and fresh fitting

New capture recipes are `rigid-arm-panel-two-bead-scan-v3`, `panel-two-bead-episodes-v3`, and `panel-two-bead-contact-core-halo-v4`. Physical weld core/halo temperatures remain illustrative; the combined recipe changes because the adhesive layout changes. Private metadata must contain exactly two adhesive records, three weld records and `adhesive_view_beads:[0,1,0]`. Geometry QA checks the real lens transform, settling, ordering and projected bounds. Pixel QA uses the mapped physical bead target, not the camera index, and checks both observations of the first bead.

The completed capture contains 64 episodes, seed 4307, with 40 train/12 validation/12 test episodes: 320 training, 96 validation and 96 held-out batches (960/288/288 PNGs). Test predictions were withheld until selection was frozen. Each body supplies one presence batch, three adhesive R/A/G batches, one seating batch and three weld R/A/G batches: 512 batches and 1,536 PNGs. Every state/image from a body stays in one split. The 4307 cohort avoids reusing the previously inspected 2907 test cases. Labels use the maximum over exactly two adhesive beads or three weld contacts. A repeated bead view never adds another physical target.

The [native capture commands](process_inspection.md#capture-and-timing) produce the source manifest. The commands below record the completed experiment; use fresh capture, data and checkpoint paths for a new experiment, preserving the frozen 4307 cohort and reports.

```bash
# From repo root inside WSL, only after native capture and image QA.
export DF_MODEL_PYTHON="$HOME/cosmos-reason2-finetune/.venv/bin/python"
export DF_BASE_MODEL="$HOME/cosmos-reason2-finetune/base/Cosmos-Reason2-2B"
"$DF_MODEL_PYTHON" scripts/model/prepare_lora.py \
  --manifest artifacts/panel-two-bead-episodes-v3/manifest.jsonl \
  --output data/panel-two-bead-v3 --grouping shared
"$DF_MODEL_PYTHON" scripts/model/train_lora.py \
  --model "$DF_BASE_MODEL" \
  --train data/panel-two-bead-v3/shared/train.jsonl \
  --validation data/panel-two-bead-v3/shared/validation.jsonl \
  --output checkpoints/panel-two-bead-shared-128-v3 \
  --max-steps 128 --rank 8 --lora-scope multimodal \
  --batch-size 6 --gradient-accumulation 4 --learning-rate 0.0001 \
  --seed 42 --gpu-memory-fraction 0.55 --linear-patch-embedding
```

The selected continuation retains the compatible first adapter and uses a fresh output, optimizer and deterministic sampler:

```bash
"$DF_MODEL_PYTHON" scripts/model/train_lora.py \
  --model "$DF_BASE_MODEL" \
  --train data/panel-two-bead-v3/shared/train.jsonl \
  --validation data/panel-two-bead-v3/shared/validation.jsonl \
  --init-adapter checkpoints/panel-two-bead-shared-128-v3 \
  --output checkpoints/panel-two-bead-shared-256-v3 \
  --max-steps 128 --rank 8 --lora-scope multimodal \
  --batch-size 6 --gradient-accumulation 4 --learning-rate 0.00003 \
  --seed 42 --gpu-memory-fraction 0.55 --linear-patch-embedding
```

This records 256 cumulative updates. The selected report itself records `steps: 128` plus `initial_adapter.previous_steps: 128`; continuation loads compatible adapter weights with a fresh optimizer and sampler. Always perform the validation/freeze sequence below; the checkpoint name alone does not establish qualification.

Choose new output directories; generated data/weights are private. Each optimizer update contains one six-example batch per stage. Each thermal batch contains two complete matched G/A/R episode triplets, preventing stage or label imbalance. The trained LoRA includes language attention/MLP and visual targets. Shared, modality-only and stage-only fitting are different experiments; evaluate them on the same frozen episode splits if compared.

The existing Torch BF16/SDPA environment is reused without changing adjacent deployments. Stock Conv3d is the default; `--linear-patch-embedding` is the recorded opt-in projection using identical parameter layout. Historical profiling showed a large host-specific Conv3d cost. BF16 rounding differs, so the actual serving path must be validated; old speed measurements are not measurements of this new scenario.

## Validation and deployment sequence

1. Review native sensor images and physical-camera/thermal-label audits before fitting.
2. Evaluate only the 96 validation batches using the actual candidate runtime on 18192. Report letters and actions separately, false G, wrong presence branches, each thermal stage's cool recall and timing.
3. Require all NIR validation labels correct with adequate G/R coverage, no false G or wrong branches, all replies valid and at least 90% clear-cool recall per thermal stage. Any additional training is bounded and uses train data only; selection uses validation only.
4. Freeze weights, exact runtime, prompts, routing and policy. Query the new 96 test batches once. Never tune from those results or reuse the old 2907 held-out cohort.
5. Run the actual native assembly/preassembled/assembly sequence with image-only requests and independent thermal-acceptance audits. Then coordinate promotion to 18191 and verify exact health/provenance.

Model HTTP time excludes arm travel, rendering/encoding scheduling, repeated observations and cooldown. Report standalone and concurrent-Unreal timings separately. This is experimental simulation evidence, not industrial safety or IQ9 EVK performance. IQ9 conversion and deployment remain separate work.

## Current verification and historical results

The local evidence is saved under `artifacts/panel-two-bead-model-v3/`: `selected-runtime.json` records the promoted identity and both evaluation summaries, `frozen-selection.json` records selection before held-out use, and `shared256-validation.json` / `shared256-heldout.json` contain batch results. Each checkpoint contains `training_report.json`, including optimizer history and continuation provenance. The independent native result is `artifacts/two-bead-live-v1/process_runtime.json`. These paths are ignored private evidence, not files supplied in a public clone. The public commands above replace any need to depend on one-off helpers in `artifacts/`.

R4 sensors keep a fixed render sequence independent of observer DLSS/Lumen/ray tracing. Ordinary live/offline startup polls asset and shader compilation, waits for five continuous idle seconds, then starts its capture-age clock. Each settled viewpoint performs three actual discarded sensor renders before readback; raw RGB channel spread above one code rejects the frame before CPU grayscale encoding. Sensor flags are applied after component registration to preserve the intended isolation. The rebuilt eight-episode pilot passed 15,979 camera/PNG checks and 108 train/validation thermal contact checks (worst error 0.1872 C), with three actual prime captures per view and startup readiness after 6.193 seconds. Pilot PNG quality checks are not model accuracy evidence.

The v3 contract, old-adapter rejection, two-bead QA mapping and sampling regressions pass 96 Python tests; launcher fixtures pass 34 checks. Synthetic pixel tests verify that a hot second bead cannot be ignored and that a fictional third bead fails the repeated-view audit. Physical scan QA separates the NIR, adhesive and weld pose families while rejecting within-family authored changes or physical drift. These tests do not establish real rendering or model accuracy. The complete fresh capture has 512 batches / 1,536 PNGs and passed 127,778 camera/PNG checks plus 936 train/validation thermal contact checks (worst error 0.3076 C). Preparation preserves 320 training, 96 validation and 96 held-out batches from 40/12/12 whole-body episodes. The first 128-update candidate completed 374.94 seconds of optimizer work, with 5.999 GiB peak CUDA allocation. Exact merged-runtime validation scored 86/96 letters and 94/96 actions: NIR presence 12/12, seating 12/12, weld 36/36 and adhesive 26/36. All 24 clearly cool thermal batches received G. Eight hot adhesive R cases held A; two adhesive A cases wrongly received G at 43.8395/44.1706 C, inside the required 42-48 C hold band. The candidate is not selected. Standalone HTTP median/p95 were 49.62/54.48 ms. A further 128 updates at 3e-5 completed in 367.84 optimizer seconds with the same batch 6/accumulation 4 and dataset. That compatible continuation produced the selected 256-update adapter; the unmerged training-runtime validation was 96/96, while the exact merged serving path scored 95/96. Selection and held-out results above use the actual serving path. Policy and prompts stayed unchanged.

The former 320 model scored 96/96 validation, 95/96 held-out and 96/96 later parked-arm compatibility under the old three-bead panel-v2 contract. Its held-out mistake was adhesive A-to-G at 43.8995 C. Those results remain in the historical guide and do not validate this two-bead refinement. Keep old reports, adapters and captures intact. The repo distributes tutorial/code only, never purchased assets, rendered training images, model weights or runtime packages.
