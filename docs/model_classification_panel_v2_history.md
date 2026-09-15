# Historical three-bead panel-v2 experiment

This is retained evidence for automotive-panel-process-v2, before removing the rear-middle bead and changing tools/rendering. Its320 adapter and95/96 test result do not validate the current two-bead contract. Commands below describe the historical source/runtime; current v3 code deliberately rejects those old adapters.

# Image-only panel assembly classification

The current host experiment uses NVIDIA Cosmos-Reason2-2B with `automotive-panel-process-v2`. A physical R4 camera inspects an inner trunk panel using NIR and thermal views. The old door adapters, prompts and datasets are incompatible and are rejected before model tensors load. The parcel/EVK deployment remains separate.

| Stage ID | Modality | G | R | A |
| --- | --- | --- | --- | --- |
| `panel_presence` | NIR | Panel installed: take the preassembled branch | Panel clearly missing: perform assembly | Hold for sufficient evidence |
| `adhesive` | Thermal | Every contact below 42 C | Any adhesive area above 48 C: wait | Maximum 42-48 C inclusive, occluded or uncertain |
| `panel_seated` | NIR | Panel aligned and flush at visible contacts | Missing, displaced, tilted or lifted panel: exception hold | Hold for sufficient evidence |
| `weld` | Thermal | Every contact below 52 C | Any contact above 58 C: wait | Maximum 52-58 C inclusive, occluded or uncertain |

Each stage receives exactly three lossless 448 x 256 grayscale PNGs, ordered `spot1`, `spot2`, `spot3`. Each image contributes 112 visual tokens, for 336 per batch. NIR frames cover the panel and its seating edges; thermal frames cover its contact regions. The three viewpoints come from physical arm motion with a rigid lens mount, never a hidden independently aimed camera. See [the process sequence](process_inspection.md) and [scene implementation](process_scene.md).

The 45/55 C nominal thermal limits and +/-3 C ambiguity bands are illustrative temperature-readiness rules, not adhesive cure specifications or weld qualification. Pixels stay linear in apparent temperature from 20 to 120 C. Observer palettes and the separate 20-1600 C display range never enter model PNGs. The VLM is not a calibrated thermometer.

## Selected host runtime

The validation-selected candidate is `checkpoints/panel-process-shared-320-v3`: one shared rank 8 LoRA for all four stages, merged into the base in memory. The exact runtime uses Torch BF16/SDPA, full-vocabulary G/A/R scoring and the explicit `--linear-patch-embedding` option. Selection was frozen after 96/96 correct validation batches on the final v3 capture. One subsequent held-out evaluation scored 95/96 (98.96%): one adhesive ambiguity-band A became G. There were no wrong presence branches, and both NIR stages and weld were entirely correct. The erroneous case had a maximum apparent/surface temperature of 43.8995 C: below the nominal 45 C limit, but inside the 42-48 C band requiring A. It is a real premature approval under the prompt's policy; the test result was not used to tune or reselect the model.

The frozen model now runs on the default loopback port 18191 after the final native integration passed. The duplicate development service on 18192 is stopped. To reproduce the selected candidate with these retained private weights, from the repository root inside WSL:

```bash
export DF_MODEL_PYTHON="$HOME/cosmos-reason2-finetune/.venv/bin/python"
export DF_BASE_MODEL="$HOME/cosmos-reason2-finetune/base/Cosmos-Reason2-2B"
bash scripts/model/start_host.sh --port 18191 \
  --adapter checkpoints/panel-process-shared-320-v3 \
  --linear-patch-embedding
```

Run `unreal/Scripts/run.ps1 -Process` to use the default 18191 endpoint. Do not launch a second service on an occupied port. The content-bound backend alias is `Cosmos-Reason2-2B-Torch-BF16-19650542770128155cb5d5fc5eb91dea-linear-nonoverlap-v1`. Health must report the current contract, prompts, policy and this adapter. The old door adapters remain incompatible.

Held-out HTTP round-trip latency was median 47.59 ms / p95 53.11 ms; GPU forward/scoring median 39.78 ms. These are simulation-only results on 12 withheld synthetic episodes (96 batches), excluding arm travel, rendering, cooldown and two-batch consensus. They do not establish industrial safety or EVK performance. Native full-process behavior is a separate check. The later R3 parked-arm clearance change passed a frozen-model compatibility check on all 96 v4 validation batches: every letter/action was correct, with zero false G or wrong presence branches. Of 288 validation PNGs, 255 remained byte-identical to v3; 24 presence and 9 missing-panel seating images changed, while all 216 thermal images were unchanged. No training, reselection or test reuse occurred. The 95/96 held-out result remains evidence for v3, not a newly measured v4 test score. V4 compatibility HTTP median/p95 were 51.05/61.57 ms; GPU forward/scoring median 42.52 ms.

The final native run passed assembly/preassembled/assembly branching, all three forward departures and all six thermal acceptance audits. It submitted 27 camera-only model requests and no ground-truth request fields; independent camera and live-curtain audits passed 5,086 and 27 checks. Median native HTTP event latency was 84.68 ms with Unreal rendering, compared with 51.05 ms in the standalone v4 compatibility benchmark. These values describe model HTTP response time, not full gate or production-cycle duration. The run completed at 154.60 simulation seconds. Evidence is private at `artifacts/panel-live-final-v1`.

## Runtime contract

The exact prompts and SHA-256 values live in [process_inspection.json](../configs/process_inspection.json). The host builds one user message containing ordered images, newline separators between images, then that stage's prompt. The installed Cosmos/Qwen3-VL template supplies chat/vision delimiters and an assistant-generation boundary; it injects no default system message. Labels, filenames, seed, case, temperatures, HUD, branch state and prior decisions never enter this message.

`POST /v1/inspect` accepts exactly these fields:

```json
{
  "contract_version": "automotive-panel-process-v2",
  "request_id": "body-12-panel-presence-4",
  "body_id": "body-12",
  "stage": "panel_presence",
  "captured_at_ms": 42390,
  "images": [
    {"camera_id": "spot1", "png_base64": "<lossless camera PNG>"},
    {"camera_id": "spot2", "png_base64": "<lossless camera PNG>"},
    {"camera_id": "spot3", "png_base64": "<lossless camera PNG>"}
  ]
}
```

Transport identity is echoed but is not shown to the model. Replies contain `decision`, `valid`, prompt hash, camera order, modality, model alias/provenance, decision policy and preprocessing/inference/total latency. `independent-camera-images-newline-v1` identifies the message layout. PNG dimensions, CRC, decompressed size, equal RGB channels and fully opaque alpha are checked. Display-colored or malformed images fail to an invalid A hold.

The fixed initial policy requires at least 0.5 total unmasked G/A/R probability mass and 0.8 conditional support for the winning letter. These scores are not calibrated probabilities of correctness. A single deterministic next-token forward produces the decision; there is no prose or reasoning decode, image-embedding cache or reused KV state.

Two matching fresh G or R batches are required to select the presence branch. A, an opposite result, an invalid reply, pause or a new body/stage resets consensus. The other gates require two fresh G batches; seating R holds an exception and thermal R continues cooling. Replies older than eight seconds from batch start cannot advance the scene. Repeated similar observations do not correct systematic model errors.

## Local GPU setup and candidates

Use the existing local Cosmos base with the host CUDA environment. The established environment has PyTorch 2.9, Transformers 4.57.3, Pillow and PEFT; no script downloads weights or changes the adjacent project's deployment.

```bash
# From the repository root inside WSL.
export DF_MODEL_PYTHON="$HOME/cosmos-reason2-finetune/.venv/bin/python"
export DF_BASE_MODEL="$HOME/cosmos-reason2-finetune/base/Cosmos-Reason2-2B"
# Unadapted panel baseline, explicitly for development:
bash scripts/model/start_host.sh --port 18192 --linear-patch-embedding
```

Wait for `PROCESS_MODEL_READY` and inspect `/health` for `status: ready`, the v2 version and all four current prompts. Torch BF16/SDPA is the default runtime. `--linear-patch-embedding` is an explicit opt-in execution path using the same nonoverlapping Conv3d weights as a linear projection; stock Conv3d remains the default without the flag. Historical GPU profiling isolated a severe Conv3d cost on this host, and the opt-in path removed that bottleneck. BF16 logits can differ, so exact-runtime validation is required; old timing measurements are not panel-v2 benchmarks.

After creating a new compatible shared adapter, a candidate can be tested with:

```bash
bash scripts/model/start_host.sh --port 18192 \
  --adapter checkpoints/panel-process-shared-128-v2 \
  --linear-patch-embedding
```

That path is a trained private development output, not a distributed or selected model. The compatible continuation is `checkpoints/panel-process-shared-256-v2`; it also remains a development candidate because validation did not meet every fixed acceptance criterion. Do not start two services on the same port. Coordinate native capture, training and evaluation so GPU contention does not invalidate latency comparisons. A modality comparison can use `--nir-adapter` and `--thermal-adapter`; both models remain resident and merge their adapters in memory. Training provenance must cover every stage routed to each model. This uses more memory than a shared model; measure it with the actual candidate.

## Capture, splits and fitting

Native v2 captures record `view_recipe=rigid-arm-panel-scan-v2`, `capture_recipe=panel-assembly-episodes-v2` and `thermal_recipe=panel-exposed-contact-core-halo-v3`. Every row has explicit contract version, prompt hash, stage, ordered image paths and a private expected label. Old captures cannot silently inherit the current version or prompts. Keep all stages, states and images of a body in one split.

The 64-episode recipe has 40 training, 12 validation and 12 test episodes. Each episode contributes one presence batch, three adhesive temperature batches, one seating batch and three weld temperature batches: 512 batches and 1,536 PNGs. The model-selection seed-2907 capture at `artifacts/panel-episodes-v3/manifest.jsonl` completed successfully with all 512 batches and 1,536 PNGs. Independent native-frame/PNG verification passed 122,658 checks; the train/validation thermal-contact audit passed 936 checks. Test episodes were withheld from training and selection, then evaluated once after the complete 320 candidate was frozen; sensor-quality auditing is separate. Review panel coverage, seating-fault visibility, physical lens attachment, grayscale/window consistency and heat-region labels before attributing mistakes to the model.

```bash
"$DF_MODEL_PYTHON" scripts/model/prepare_lora.py \
  --manifest artifacts/panel-episodes-v3/manifest.jsonl \
  --output data/panel-process-new-candidate --grouping shared
"$DF_MODEL_PYTHON" scripts/model/train_lora.py \
  --model "$DF_BASE_MODEL" \
  --train data/panel-process-new-candidate/shared/train.jsonl \
  --validation data/panel-process-new-candidate/shared/validation.jsonl \
  --output checkpoints/panel-process-new-candidate \
  --max-steps 128 --batch-size 4 --gradient-accumulation 4 \
  --rank 8 --learning-rate 0.0001 --seed 42 \
  --gpu-memory-fraction 0.55 --linear-patch-embedding
```

These are fresh-candidate examples; choose new output directories. The retained selected 320 weights have recorded lineage: 128 initial updates,128 continuation updates on the earlier capture, then 64 updates on final v3 data at learning rate 1e-5, batch 6 and accumulation 4. The final 64 updates took 194.08 seconds and peaked at 6.42 GB of Torch allocation. Each thermal batch contained two complete G/A/R episode triplets, with all four stages represented per update. This is a bounded starting experiment, not an acceptance guarantee. Training begins from the unadapted base; a continuation must have the same v2 provenance. It optimizes one G/A/R target token and records image hashes, stage coverage, sampled rows, gradients, loss, runtime and peak GPU allocation. The default multimodal LoRA includes language attention/MLP and visual branches. The language-attention-only option is an ablation.

Stage-balanced batches prevent thermal examples from overwhelming the two NIR tasks. For batch4, the first three thermal examples are matched G/A/R observations from one episode; the remaining label rotates. Accumulation4 combines one batch from each stage per optimizer update. NIR sampling balances available labels. Include missing/present panels, displaced/lifted seating faults and genuinely ambiguous or occluded views. Test rows are rejected by the trainer. Preparation records camera IDs, modality and split, and rejects episode leakage.

Use the same episode splits with `--grouping modality` or `--grouping stage` for controlled adapter comparisons. Shared-data checkpoint routing, modality-only fitting and stage-only fitting are different experiments. Retain the distinction when reporting results.

## Evaluate and select

```bash
"$DF_MODEL_PYTHON" scripts/model/evaluate_gates.py \
  --manifest artifacts/panel-episodes-v3/manifest.jsonl \
  --split validation --purpose development \
  --endpoint http://127.0.0.1:18192/v1/inspect \
  --output artifacts/panel-process-v3/new-candidate-validation.json
```

The evaluator sends only the allowlisted image/identity request, never its labels or ground truth. Report literal labels and process actions separately. Presence G and R choose different branches; both wrong G (skipping needed assembly) and wrong R (assembling onto an installed panel) are errors. Seating R is an exception hold; thermal A/R both wait. Invalid transport cannot earn correct-A credit. Report false G, wrong presence branches, per-stage confusion, clear-cool recall and latency; repeated benchmark frames are not independent accuracy samples.

The stricter validation-only threshold tool retains its fixed 0.85/0.90/0.95/0.98 grid. Its candidate criteria require all NIR validation cases correct, at least 12 cases and three G/three R examples per NIR stage, no false G, and at least 90% clear-cool recall per thermal stage. Do not use test predictions to tune prompts, weights, routing or policy. Only after freezing the complete candidate/runtime/policy may evaluation use `--purpose heldout --split test` once. A real native process smoke then checks actual assembly/skipping, fresh capture, movement and thermal acceptance separately.

Latency fields distinguish CPU preprocessing, GPU forward/scoring and HTTP service time. They exclude arm movement, rendering/PNG scheduling, repeated-gate observations and physical cooldown. None is a whole-cycle throughput measurement.

## Optional exports

The optional GGUF scripts retain a separate BF16 text/F16 projector backend. Visual LoRA changes require regenerating the projector from the merged model; never pair an adapted text model with an unchanged base projector. Export hashes and training provenance are checked. The top-K interval scorer handles omitted tiny G/A/R probabilities conservatively, while Torch scores the full vocabulary. These runtimes are not assumed numerically interchangeable. Select and test the actual runtime intended for use; IQ9 conversion/deployment remains separate work.

## Historical evidence and retained files

The complete [v1 door-process experiment](model_classification_v1_history.md) retains its original camera/model narrative, successful and failed candidates, projection research and test results. Its 82/84 test decisions, later physical-scan checks and 39/42 thermal-tuning development result do not validate the panel task. The old base and two selected LoRAs remain private; earlier retired weights have archived reports, not restorable models.

The fresh shared128 candidate completed 128 optimizer updates in 272.60 seconds with 5.77 GB peak Torch allocation. Exact Torch serving validation produced 93/96 correct letters and 94/96 correct process actions. Both NIR stages were 12/12 correct; adhesive was 35/36 (one R held as A), and weld was 34/36 (two borderline A cases incorrectly became G). All 24 clearly cool thermal cases were G. This candidate fails the zero-false-G selection rule. HTTP round-trip median was 51.88 ms and p95 59.07 ms; median GPU forward/scoring was 43.14 ms. A compatible 128-update continuation at learning rate 3e-5 completed in 266.34 seconds, producing shared256 with the same prompts, dataset and policy. Its exact serving validation scored 94/96 letters and actions: presence 12/12, seating 12/12, adhesive 36/36 and weld 34/36. It had zero false G and zero wrong presence branches, but two cool weld G cases became A. Weld clear-cool recall was 10/12 (83.3%), below the prespecified 90% requirement, so shared256 is also unselected. HTTP median was 51.32 ms and p95 57.28 ms; median GPU forward/scoring was 42.96 ms. Neither earlier candidate queried test episodes. Only the subsequently selected 320 runtime received the frozen test evaluation.

Before final selection, a separate development run with shared256 and the earlier v8 geometry completed all three correct branches/departures, six actual thermal acceptance audits, 5,650 camera checks and 27 live curtain checks. Median native HTTP event latency was 84.69 ms while Unreal rendered; the run took 173.85 seconds. This demonstrates the operational capture/model/process loop, including warm-NIR seating, but does not select shared256 or validate later geometry changes. Evidence is private at `artifacts/panel-live-development-v1`.

Current Python v2 contract/provenance/sampling regressions pass 87 tests, including batch 6/accumulation 4 coverage. The finalized role/gripper geometry was recaptured as v3 with unchanged episode splits and verified before 320 fitting/selection. The old 256 checkpoint scored 93/96 on these new views (one seating false G and two cool weld holds). The 320 continuation corrected all validation cases; the separate test result above remains 95/96. The final v4 parked-arm update received validation-only compatibility as reported above; intermediate transfer/withdraw choreography is checked independently from the frozen gate endpoints. Assets, images, weights, runtime packages and experiment artifacts remain excluded from Git; this repository publishes tutorial and source examples only.
