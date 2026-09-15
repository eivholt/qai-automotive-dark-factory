# Historical door-process model experiments

This document preserves the earlier `automotive-process-v1` door/adhesive/weld experiment and its recorded results. The current scene is the inner trunk panel assembly described in [model classification](model_classification.md). None of the accuracy, latency, camera coverage or selected-adapter claims below validates the new panel contract. Commands below belong to the historical source revision and must not be run against the current v2 loader. Old private adapters and evidence are retained; v2 rejects them.

---

# Image-only automotive process classification

The experimental host path uses NVIDIA Cosmos-Reason2-2B on the host CUDA GPU. It has a new, independent three-gate contract; the earlier parcel adapter and battery-tray bridge are not used. Imported-scene requests contain only camera PNGs and transport identity. Scene labels, temperatures, randomization state and the HUD never enter the model message.

| Gate | Model input | G | R | A |
|---|---|---|---|---|
| Doors | Two 448 x 256 NIR PNGs, left then right | Four doors visibly seated | Missing, loose or fallen door: return body | Hold for another observation |
| Adhesive | Three 448 x 256 grayscale thermal closeups | Every visible strip below 42 C (45 C limit, 3 C margin) | Wait for cooling | Hold for clearer evidence |
| Weld cooling | Three thermal closeups from inspection arm | Every visible spot below 52 C (55 C limit, 3 C margin) | Wait for cooling | Hold for clearer evidence |

The 45 C and 55 C values are illustrative demonstration limits frozen in this contract version, not welding or adhesive process specifications. They refer to apparent temperature in the fixed linear 20-120 C sensor window. The explicit +/-3 C ambiguity band produces A for adhesive maxima 42-48 C and weld maxima 52-58 C, inclusive; hotter observations produce R. Changing a limit requires a new coordinated contract, native controller update, capture labels, prompts and hashes; editing only this JSON is insufficient. The display palette is never sent to the model. A learned VLM's grayscale judgement is not a calibrated thermometer; retain that distinction in the tutorial and evaluate threshold boundary cases explicitly.

## Selected host runtime

The current demonstration configuration routes NIR doors to the `process-shared-128-linear-v1` checkpoint and both thermal gates to `process-shared-256-linear-v1`. Both adapters were trained on the same shared three-stage corpus; this is a validation-selected routing experiment, not evidence that modality-only training is superior. Separate-modality-only training remains a future controlled comparison. At startup, the service merges the local base with each selected adapter in memory. The two BF16 models remain resident on the host GPU, avoiding adapter swaps and preserving the exact evaluated numerics; no on-disk merged checkpoint is required.

Start the selected configuration from this repository root inside WSL:

```bash
cd /mnt/c/Users/eivho/repos/qai-automotive-dark-factory
bash scripts/model/start_host.sh --port 18191 \
  --nir-adapter checkpoints/process-shared-128-linear-v1 \
  --thermal-adapter checkpoints/process-shared-256-linear-v1 \
  --linear-patch-embedding
curl http://127.0.0.1:18191/health
```

The script uses the local Python/base-checkpoint defaults described below; override `DF_MODEL_PYTHON` and `DF_BASE_MODEL` on another machine. These private checkpoint directories are experiment outputs and are not in Git. Omitting the adapter flags starts the unadapted baseline. Torch BF16/SDPA is the host default; `--linear-patch-embedding` explicitly selects the tested `linear-nonoverlap-v1` projection. Stock Conv3d remains the default without that flag. No llama.cpp backend is needed for this selected runtime.

Wait for `PROCESS_MODEL_READY` and health `ready`. The frozen router alias is `Cosmos-Reason2-2B-Modality-Router-dd78c3c266f1961de0b7268dd1f7565e`; health also exposes each stage's content-bound model alias, projection revision and base/adapter/config provenance. Every reply includes the router alias and its actual `stage_backend_alias`. The unchanged decision policy is minimum G/A/R mass 0.5, conditional probability 0.8, two fresh consecutive G observations, and an 8-second reply-age limit.

| Frozen routed runtime | Doors | Adhesive | Weld | Overall |
|---|---:|---:|---:|---:|
| Validation literal labels | 12/12 | 35/36 | 36/36 | 83/84 |
| Validation action decisions | 12/12 | 36/36 | 36/36 | 84/84 |
| Untouched test literal/actions | 11/12 | 35/36 | 36/36 | 82/84 |
| Test clear-cool G recall | N/A | 11/12 | 12/12 | 23/24 thermal |
| Test false G | 0 | 0 | 0 | 0 |

Selection was frozen on validation before a single test evaluation. The 12 test episodes contain one falsely rejected healthy body and one clear-cool adhesive hold; all six faulty test bodies were returned. The test was not used for further fitting, routing changes or threshold selection. These small synthetic-camera results do not establish industrial accuracy or EVK performance. They describe the retained earlier camera views. The later rigid R4 arm scan preserves spot 0 but changes thermal views 1 and 2; its `rigid-arm-thermal-scan-v1` recipe is evaluated separately in the development check below. No adapter retraining accompanies this scene change. The native continuous process smoke is a separate end-to-end check, including QA-only actual-temperature audits.

On the frozen test, median host end-to-end latency was 54.2 ms and p95 was 75.0 ms; medians were 55.8 ms doors, 55.3 ms adhesive and 53.3 ms weld. Both resident models used 8.52 GB of Torch allocation with 8.69 GB peak (driver/Unreal allocations are separate). This trades approximately twice the model memory for stable per-modality selection. A shared-base adapter-switching implementation may reduce memory, but it would need its own numerical and latency validation.

The private records are `artifacts/process-model-v1/selected-router-v1.json`, `routed128-256-validation.json` and `routed128-256-test.json`. Earlier unselected weights have been retired; their reports and provenance remain archived as described below. The frozen routing decision uses only validation evidence.

To reproduce these two private checkpoints after creating the versioned 64-episode capture, use new output directories or a clean local workspace. The second command starts from the first adapter's weights with a fresh optimizer; it does not overwrite them. Exact weights and model decisions can vary with hardware/library versions, so repeat validation and freeze a new selection before using a reserved test split.

```bash
export DF_MODEL_PYTHON="$HOME/cosmos-reason2-finetune/.venv/bin/python"
export DF_BASE_MODEL="$HOME/cosmos-reason2-finetune/base/Cosmos-Reason2-2B"
"$DF_MODEL_PYTHON" scripts/model/prepare_lora.py \
  --manifest artifacts/process-episodes-v1/manifest.jsonl \
  --output data/process/episodes-v1 --grouping shared
common=(--model "$DF_BASE_MODEL" \
  --train data/process/episodes-v1/shared/train.jsonl \
  --validation data/process/episodes-v1/shared/validation.jsonl \
  --max-steps 128 --batch-size 4 --gradient-accumulation 3 \
  --gpu-memory-fraction 0.55 --validation-limit 7 \
  --linear-patch-embedding --seed 42)
"$DF_MODEL_PYTHON" scripts/model/train_lora.py "${common[@]}" \
  --output checkpoints/process-shared-128-linear-v1 --learning-rate 0.0001
"$DF_MODEL_PYTHON" scripts/model/train_lora.py "${common[@]}" \
  --init-adapter checkpoints/process-shared-128-linear-v1 \
  --output checkpoints/process-shared-256-linear-v1 --learning-rate 0.00003
```

## Physical arm scan development check

The later physical R4 scan was evaluated with the unchanged selected adapters, after its new eight-episode capture produced 56 batches/160 PNGs. Only the 42 train/validation batches from six bodies were sent to the model; all 14 test batches from the remaining two bodies were excluded. This was a development compatibility check for `rigid-arm-thermal-scan-v1`, not a new withheld-test result or an adapter selection experiment.

| New scan development batches | Doors | Adhesive | Weld | Overall |
|---|---:|---:|---:|---:|
| Literal labels | 6/6 | 16/18 | 18/18 | 40/42 |
| Gate actions | 6/6 | 18/18 | 18/18 | 42/42 |
| Observed false G | 0 | 0 | 0 | 0 |

Both literal errors were hot adhesive R cases answered A; both responses held the process. All 42 replies were valid. Median host response time was 55.34 ms and p95 was 76.77 ms. The separate native model-driven run completed healthy exit / loose-door return / healthy exit, with all four action-time thermal readiness audits passing. No training, routing, threshold or adapter-selection change followed this evaluation. The earlier 82/84 test result above remains tied to its retained original views. Private evidence: `artifacts/inspection-arm-capture-v1/development-evaluation.json` and `artifacts/inspection-arm-live-v1/process_runtime.json`.

## Local model retention

The 2026-09-13 cleanup retains these three local checkpoint directories:

- `checkpoints/process-shared-128-linear-v1` for NIR doors.
- `checkpoints/process-shared-256-linear-v1` for both thermal gates.
- `checkpoints/process-base-bf16` for the existing optional base GGUF backend launcher.

The original WSL base checkpoint is also retained. The service on port 18191 loads that base and the two adapters, then merges in memory. Unused on-disk merged checkpoints and derived adapter GGUF/projector exports, including disk merges of the selected pair, were removed. Optional adapter GGUF experiments require regeneration with the retained merge/export scripts; the base GGUF files remain available for their existing launcher.

Unselected adapter weights and the failed empty batch4 directory were also removed. Training reports, configurations, README files and provenance from removed directories are archived privately under `artifacts/process-model-v1/retired-checkpoints/<old-directory>/`. The cleanup inventory is `artifacts/process-model-v1/model-cleanup-20260913.json`. Datasets, evaluation evidence, screenshots and reusable tooling remain retained. Historical metrics below describe the recorded runs; archived metadata does not restore retired weights.

## What transfers from the previous project

The [previous tutorial](https://github.com/eivholt/qai-physics-reasoning/blob/main/docs/isaac_sim_conveyor_reason2_tutorial.md) found that lossless images and consistent camera/preprocessing contracts matter, and that a task-specific one-token classifier avoids expensive output formatting and long sequential decoding. Its reported 58.9 ms host result concerned a fine-tuned parcel task, one 112-visual-token image and a different runtime; it is not a performance estimate for these new multi-camera gates.

NVIDIA's [Reason2 inference reference](https://docs.nvidia.com/cosmos/latest/reason2/reference.html) documents Transformers and recommends vLLM for deployment. The [official minimal inference example](https://github.com/nvidia-cosmos/cosmos-reason2/blob/main/scripts/inference_sample.py) places media before the text and specifies Qwen3-VL pixel budgeting. This implementation preserves those input conventions while performing just one forward pass to inspect next-token G/A/R logits. It generates no reasoning trace or prose.

The unadapted baseline is evaluated separately from the new automotive LoRA candidates. Neither baseline nor adapter scores are calibrated probabilities of correctness. Classification abstains unless G/A/R together receive at least 0.5 of next-token probability mass and the winning letter receives at least 0.8 of their conditional mass. These initial experimental thresholds require validation. A softmax over three letters alone must not be presented as model confidence.

## Running the isolated host service

Use an existing local Cosmos-Reason2-2B checkpoint and a CUDA environment with PyTorch 2.9, Transformers 4.57.3 and Pillow. The optional adapter path additionally needs PEFT. This project does not download model weights or modify the adjacent project's environments or deployments.

```bash
# From this repository inside WSL; override these paths when needed.
export DF_MODEL_PYTHON="$HOME/cosmos-reason2-finetune/.venv/bin/python"
export DF_BASE_MODEL="$HOME/cosmos-reason2-finetune/base/Cosmos-Reason2-2B"
bash scripts/model/start_host.sh --port 18191
curl http://127.0.0.1:18191/health
```

Wait for `PROCESS_MODEL_READY`: the service loads BF16 weights and warms all three input shapes before opening its loopback endpoint. One inference is allowed at a time; concurrent inference requests return an amber hold. The image processor preserves 448 x 256 pixels, corresponding to 112 visual tokens per image: 224 for doors and 336 for a thermal batch. CPU preprocessing, GPU inference and complete service latency are measured separately. No cache reuses image embeddings, classification decisions or KV state between observations.

`POST /v1/inspect` accepts exactly:

```json
{
  "contract_version": "automotive-process-v1",
  "request_id": "body-12-doors-4",
  "body_id": "body-12",
  "stage": "doors",
  "captured_at_ms": 42390,
  "images": [
    {"camera_id": "left", "png_base64": "<camera PNG bytes as base64>"},
    {"camera_id": "right", "png_base64": "<camera PNG bytes as base64>"}
  ]
}
```

Thermal camera IDs are `spot1`, `spot2`, `spot3`, in that order. The service echoes identity without showing it to the model, and returns `decision`, `valid`, `prompt_sha256`, camera order, modality, model identity, the exact decision-policy object, timing and diagnostic label scores. A stale service configuration is rejected even when prompt text is unchanged. RGB/RGBA PNGs from Unreal are accepted only when RGB channels are exactly equal; nonopaque alpha and colored display captures are rejected. PNG dimensions, CRCs and decompressed size are validated before decoding.

The controller must match every identity field and prompt hash, reject stale replies, and require two consecutive G replies from distinct fresh capture batches. A new body/stage resets this history. R means return only at the door gate; at the thermal gates it means continue waiting. Transport failures, invalid images, unexpected tokens, wrong camera order, unknown stages and stale requests hold the process. Two consecutive replies reduce isolated variation but do not fix systematic model mistakes on nearly identical frames. A model result does not directly command an industrial robot.

## Optional GGUF host backend

The initial Transformers BF16 path was unusually slow on this host even with Unreal closed. The optional llama.cpp CUDA backend was an experimental fast path before the selected Torch projection optimization. Its base-model export (BF16 text, F16 vision projector) is retained in `checkpoints/process-base-bf16`, because the existing optional backend scripts reference it. A fresh workspace must generate that export before starting the backend. It never loads the previous parcel adapter. Both runtimes retain the same image batches, prompt text and abstention policy; floating-point implementations can still differ, so compare their actual gate outcomes.

```bash
# Base GGUF files are retained locally. On a fresh workspace, first run:
# bash scripts/model/export_base_gguf.sh
# Terminal 1: GPU backend, dedicated loopback port 18192.
bash scripts/model/start_llama_backend.sh
# Terminal 2: validated image-only process endpoint, port 18191.
bash scripts/model/start_host.sh --llama-endpoint http://127.0.0.1:18192/v1/chat/completions
```

The backend runs one slot with 1024 context tokens, 112 visual tokens per image, flash attention and prompt caching disabled. It requests one greedy token with the top 20 unmasked pre-sampling log probabilities. The `gar-unmasked-topk-intervals-v1` scorer handles omitted tiny class probabilities conservatively, rather than treating them as exact zeros. Backend token accounting is reported as received and should not be directly equated with Transformers token counts. Export provenance records input weights, configuration, GGUF/projector hashes and the llama.cpp revision. Runtime image embedding and prompt caches are disabled; CUDA execution graphs may be reused without reusing prior image contents.

Early speed probes used adjacent image markers that this llama.cpp revision incorrectly treated as consecutive video frames. Their latency and classification numbers are retained as debugging evidence only, not valid independent-camera benchmarks. The corrected layout, `independent-camera-images-newline-v1`, places a newline text item between every camera image in inference, training and prompt experiments. Health, replies, training provenance and evaluation reports identify this revision; clients reject other layouts. CUDA execution graphs may still be reused without reusing image contents.

On the corrected 84-batch development validation set, the base model's median end-to-end latency was 117 ms for two NIR views and 127-133 ms for three thermal views. These measurements include the Windows-to-WSL loopback transport and shared host GPU conditions. They are not a general hardware benchmark or an EVK estimate.

## Reproducible evaluation

Native capture produces private JSONL rows with `sample_id`, `body_id`, `stage`, ordered `images[{camera_id,path}]`, `expected_decision`, `contract_version` and `prompt_sha256`. Ground truth is kept only in this offline manifest. The evaluator builds a new allowlisted request and never forwards the expected label, ground-truth object, seed or filenames.

```powershell
python scripts/model/evaluate_gates.py --manifest artifacts/process-episodes-v1/manifest.jsonl --output artifacts/process-model-v1/development.json --split validation
```

Reports include per-stage confusion matrices, false proceeds, holds, episode-level outcomes, preprocessing/inference/end-to-end latency and prompt/model identity. Invalid transport or identity replies never earn correct-A credit. Action accuracy is reported separately: thermal A and R both hold, whereas door R returns the body; literal letter accuracy is never relabeled. Do not pool repeated frames into independent accuracy samples. Development capture results support debugging only. `--purpose heldout` requires explicit `test` episode splits; keep all views and time steps from the same randomized body in one split. Independent capture review must establish that the camera can see every relevant area before interpreting model errors.

## Adapter comparison preparation

Prepare the same episode assignment for three comparable experiments:

```powershell
python scripts/model/prepare_lora.py --manifest artifacts/process-episodes-v1/manifest.jsonl --output data/process/shared --grouping shared
python scripts/model/prepare_lora.py --manifest artifacts/process-episodes-v1/manifest.jsonl --output data/process/modality --grouping modality
python scripts/model/prepare_lora.py --manifest artifacts/process-episodes-v1/manifest.jsonl --output data/process/stage --grouping stage
```

The exporter writes one fixed prompt, ordered image batch and one G/A/R target per sample. A stable episode hash supplies 70/15/15 train/validation/test buckets unless a manifest already records a split; conflicting splits for one episode are rejected. With a small dataset the realized proportions can differ substantially. These files prepare training; they do not establish an adapter's accuracy.

`train_lora.py` is a bounded PEFT runner for a new multimodal adapter targeting language attention/MLP and visual qkv/merger branches. The `--lora-scope language-attention` option retains a frozen-vision ablation. It optimizes only the next G/A/R token, rejects overlapping train/validation episodes and preserves the prompt/image contract. It writes private safetensors and a provenance/loss report to a new directory, never over an existing adapter. Its validation raw-token fit is not an acceptance test.

```bash
$DF_MODEL_PYTHON scripts/model/train_lora.py --model "$DF_BASE_MODEL" \
  --train data/process/shared/shared/train.jsonl \
  --validation data/process/shared/shared/validation.jsonl \
  --output checkpoints/process-shared-experiment-01 --max-steps 64 \
  --lora-scope multimodal --gradient-accumulation 1 --batch-size 4 --validation-limit 7
```

For an optional GGUF experiment, regenerate the disposable on-disk merge and export both the text weights and updated vision projector to new private paths, then validate using separate loopback ports. The selected Torch runtime above merges in memory and does not need these files:

```bash
$DF_MODEL_PYTHON scripts/model/merge_lora.py --base "$DF_BASE_MODEL" \
  --adapter checkpoints/process-shared-experiment-01 --output checkpoints/process-merged-01
bash scripts/model/export_lora_gguf.sh checkpoints/process-merged-01 checkpoints/process-gguf-01
# GPU backend; run in its own terminal.
DF_GGUF_DIR=checkpoints/process-gguf-01 \
DF_GGUF_MODEL_FILE=Cosmos-Reason2-2B-Automotive-LoRA-BF16.gguf \
DF_GGUF_PROJECTOR_FILE=mmproj-Cosmos-Reason2-2B-Automotive-LoRA-F16.gguf \
DF_LLAMA_PORT=18194 \
  bash scripts/model/start_llama_backend.sh
# Validated process proxy; run in another terminal.
bash scripts/model/start_host.sh --port 18193 \
  --llama-endpoint http://127.0.0.1:18194/v1/chat/completions \
  --llama-provenance checkpoints/process-gguf-01/provenance.json
python scripts/model/evaluate_gates.py --manifest artifacts/process-episodes-v1/manifest.jsonl \
  --split validation --endpoint http://127.0.0.1:18193/v1/inspect \
  --output artifacts/process-model-v1/adapter-validation.json
```

The merge and service loaders reject an adapter trained on a different camera contract, prompt revision, base configuration or multi-image layout. A visual LoRA must never reuse the base projector. The backend launcher verifies both GGUF hashes and derives its model alias from their content; the proxy checks the served model list and response identity against that alias, preventing a base/adapter mismatch. Select the candidate and fix its configuration before invoking the evaluator with `--purpose heldout --split test`. The test-mode flag is an explicit guard against accidental test use during development.

Evaluate resulting adapters using the same abstention/consensus policy and untouched test episodes; do not choose an adapter using its test results repeatedly. The initial 40 development batches are for camera and gate debugging and are insufficient to establish accuracy. Generate additional independent episodes before running a meaningful training comparison.

Compare a shared adapter, separate NIR/thermal adapters, and separate stage adapters with identical held-out episodes. Balance healthy/loose/fallen/missing doors, door positions, camera occlusion, borderline strip/spot temperatures, and nuisance hot equipment. Include hard A examples. Use false-proceed rate and anomaly recall as primary quality measures, then compare latency and adapter switching cost. Preserve the exact prompt/image contract when evaluating a merged BF16 adapter and later an EVK export. Test quantized deployment on the same untouched episodes before claiming parity.

Private assets, generated images, experiment outputs, weights and adapters stay ignored. Only code and tutorial examples are published.

## Initial native-camera evidence

The corrected native captures show distinct NIR side views, visible seated/absent lower door panels, and three thermal closeups that contain both adhesive and weld bounds. The expanded capture comprises 64 whole body episodes: 40 train, 12 validation and 12 test episodes withheld from model selection, with seven camera batches per body. Each body contributes one door inspection, three adhesive temperature states and three weld temperature states. All its stages and time steps stay in the same split. The door camera pose, field of view and material gain vary only in offline capture; the live sensor remains fixed.

An integration defect invalidated the first fast-backend classification results: consecutive same-size image markers were automatically merged as video frames by the locally installed llama.cpp revision. Token counts exposed the issue. Inserting newline separators restored independent image processing and the expected 337-token two-view door prompt. Earlier all-A reports and the associated first prompt comparison must not be attributed to the base model's visual ability.

The valid independent-image baseline then showed a real accuracy problem on the expanded validation set: six of 12 door batches were correct, five faulty bodies received false G, and one fault held A. All 36 adhesive and all 36 weld batches held A, including the clear hot/cool examples. Each thermal gate therefore matched only its 12 deliberately ambiguous labels. The general base model does not yet provide usable gating accuracy for these views. The current 0.8 conditional-score threshold was not relaxed to manufacture successful transitions.

The one-step PEFT plumbing smoke performed a real CUDA optimizer update in 5.91 seconds and saved a 12.9 MB private rank 8 adapter with 3,211,264 trainable parameters. Nonzero LoRA B matrices verified actual updates. It used an earlier prompt/layout revision. Its archived report and provenance remain historical, undeployed evidence of the training pipeline, not an accuracy evaluation; the obsolete adapter weights were removed.

The thermal prompts now state an explicit +/-3 C ambiguity band. The first 40-batch capture and its old thermal hashes cannot be used as current training evidence without an explicitly versioned relabeling experiment. The shared adapter experiment uses the expanded capture and current hashes. Sampling balances stages and available labels; actual batches of at least three preserve matched thermal G/A/R observations from one body. Remaining batch slots rotate labels, and door batches balance G/R. Seeded queues provide reproducibility. Training records every sampled example, image hashes, gradient norms, loss, peak GPU allocation and limited diagnostic validation. The early full validation runs used the merged fast runtime. Test episodes are withheld from model training, validation and selection; independent sensor-quality audits cover all images.

The first 32-step attention-only pilot retained thermal A predictions on all five thermal diagnostic examples and was not exported. The next experiment uses the visual and MLP LoRA branches documented in the previous project, which specifically noted that language-only adapters could retain the base SAFE prior. Export always regenerates the vision projector from the merged model; sampled visual tensors are compared before and after merging and recorded in provenance.

The 64-step multimodal single-example pilot trained 11,927,552 parameters, including 3,211,264 visual parameters, in 428 seconds. Full validation of its separately merged text and vision exports reached 42/84 correct, with zero false proceeds. However, all six healthy doors held or rejected, all 24 thermal boundary cases rejected, and only 12/24 clearly cool thermal observations passed. This pilot is not selected for live use. Median end-to-end latency was 130 ms. The six paired Torch/fast diagnostics kept identical raw winning letters; one healthy door changed from G to A because its conditional G score fell from 0.924 to 0.673. This demonstrates why deployment validation uses the exact exported runtime.

The first batch4 attempt without gradient checkpointing exceeded its CUDA allocation cap on the third update; it produced no candidate adapter. The revised bounded run enables checkpointing and explicit BF16 autocast for PEFT adapter operations, and records per-step GPU allocation.

## Bounded score transport and current adapter evidence

A confident clear-cool image sometimes places its tiny alternative R probability outside the backend's top 20 tokens. Requiring all three letters in that response caused unnecessary holds. The interval scorer uses the known G/A/R mass as a lower bound; each omitted class is bounded above by the smallest returned top-K probability. For a known winner with probability `p`, the conditional lower bound is `p / (known_mass + missing_count * cutoff)`, capped using total mass at most one. A class is accepted only when the known mass and worst-case conditional probability meet the existing thresholds. Missing or malformed top-K lists still fail closed. No logit bias or forced-label renormalization manufactures support. Replies expose the method, missing classes, and mass/conditional intervals; they do not present guessed point probabilities as exact.

The shared batch4 run completed 64 optimizer updates in 1,648 seconds with 5.69 GB peak Torch allocation. It sampled 190 distinct training rows: all 40 door rows and 75 rows from each thermal stage. With corrected bounded score transport, full validation achieved 73/84 literal labels and 78/84 actions: doors 8/12, adhesive 32/36, weld 33/36. All 24 clearly cool thermal cases passed, but four faulty doors and two borderline adhesive cases falsely passed. Stricter validation-only thresholds 0.85, 0.90, 0.95 and 0.98 did not qualify. Training-door fit was only 31/40, with four false passes and five fault holds. This adapter is not selected; test episodes remain withheld from model selection and prediction.

`calibrate_gates.py` records that prespecified stricter threshold grid on an explicitly selected validation report. It requires zero false G, all 12 validation doors correct, and at least 90% clear-cool recall for each thermal stage. It never changes model weights, prompts or test predictions. Literal letter accuracy and action accuracy remain separate.

An optional prototype grouped equal-grid vision attention calls while preserving independent camera tokens. Twelve CPU parity tests passed, but the bounded GPU probe found no useful acceleration: stock/batched door forward time was 4.868/4.863 seconds, adhesive 7.377/7.852 seconds, and weld 7.186/7.143 seconds. Maximum BF16 logit differences of 0.25-0.3125 failed the probe's declared tolerance. The experiment was archived privately and is not adopted. A stock Torch operator profile is used to locate the actual bottleneck before further optimization or training.

The subsequent stock PEFT diagnostic reproduced all four faulty-door false G predictions in Torch, so they are not caused by GGUF conversion. All 2.139 billion parameter elements were on CUDA and `CUDA_LAUNCH_BLOCKING` was unset. CUPTI instrumentation exceeded its bounded run and was terminated; a lightweight leaf-module CUDA-event profile then isolated `visual.patch_embed.proj`: its Conv3d consumed 5,222 ms, versus about 54 ms across all other measured leaf modules. Ordinary uninstrumented door forwards took 4.79-4.89 seconds. These module timings localize the bottleneck, rather than serving as a hardware benchmark. The nonoverlapping full-patch convolution is now the target of a separately tested linear-projection experiment; no installed libraries are modified.

The isolated projection probe replaced only the full-patch Conv3d execution with a linear matrix product over the same frozen five-dimensional weights. Nine CPU tests cover forward and gradient agreement, parameter identity, PEFT dropout and checkpoint recomputation. On the three measured RTX 5090 fixtures, stock/linear forwards were 4,822/34 ms for doors, 7,235/40 ms for adhesive and 8,367/39 ms for weld. All three decisions matched, but BF16 all-vocabulary logits differed by up to 0.25, so this is not a claim of bitwise runtime parity. The training flag `--linear-patch-embedding` is opt-in, recorded as `linear-nonoverlap-v1`, restores the original module on exit and changes no installed library. Exact-runtime validation remains required after fitting and exporting.

A fresh bounded experiment uses 128 optimizer updates, actual batch size 4 and accumulation 3 with seed 42. Each update combines one matched batch from each stage, addressing both limited door exposure and single-stage update oscillation. It keeps the camera data, prompt hashes, confidence thresholds and thermal ambiguity bands unchanged. The run originally used new adapter and export directories. Earlier unselected weights and unused exports were later removed, with their reports and provenance archived.

The 128-update accumulated run finished in 178.91 seconds, sampling 512 examples per stage and all 280 distinct training rows. Exact GGUF validation improved to 77/84 literal labels and 80/84 actions, with all 12 doors correct and zero false G. The opt-in Torch runtime (`--linear-patch-embedding`) reached 78/84 literal labels and the same safe actions, at median end-to-end times 51/61/58 ms for doors/adhesive/weld, respectively. Both held four of 12 clearly cool adhesive observations, so neither qualified under the predeclared readiness-recall requirement. Torch training-set fit was 272/280; all eight misses were adhesive holds, while all 40 doors and 120 weld examples were correct. A bounded weights-only continuation at 3e-5 restarts the optimizer and deterministic sampler, preserving a hashed link to this initial adapter. Both selected adapters remain retained; obsolete earlier weights and disk exports were subsequently removed.

The Torch service merges the compatible adapter in memory and records full-vocabulary scoring, projection revision, base/adapter/config hashes and a content-bound runtime alias in health and replies. Stock Conv3d remains the default; choosing the linear execution flag requires validation on that exact runtime. The one-token GGUF and Torch paths are alternatives, not claimed numerically identical.

The lower-rate continuation completed 128 further updates in 176.79 seconds. It corrected adhesive readiness (35/36 validation, all 12 cool G) and reached 36/36 weld validation, but two healthy door regressions left 10/12 doors. Rather than changing thresholds or fitting against test, the selected runtime keeps the earlier checkpoint for NIR and the later checkpoint for thermal, then validates that exact fixed route. This shows interference between shared-data checkpoints; it does not isolate the effect of modality-only training.

### Thermal range and timing development check (2026-09-13)

The v2 process heat/timing recipe retains the model's 20-120 C sensor window, prompt hashes, adapters and policy. Its eight-episode private capture has 56 batches / 160 images; only the 42 train/validation batches were queried. The 14 test batches were excluded. The unchanged router answered all 42 validly: 39/42 literal labels, 41/42 gate actions, zero false G. Two hot adhesive samples answered A instead of R (both hold), and one cool weld sample answered A instead of G (a conservative extra hold). Median/p95 HTTP response was 51.76/54.91 ms. The separate native three-body run completed both healthy exits and the loose-door return with all thermal acceptance audits passing. This is small development/integration evidence; previous test accuracy remains attached to its earlier capture. Details and rendered checks: [welding temperature ranges](thermal_welding_range.md).


## Historical native process verification

The following records apply only to the former door process and its successive camera/heat recipes. They do not validate panel-v2.

### Implementation verification in this checkout

The physical three-pose scan update passes the UE 5.8 build, 81 Python tests and 82 native policy checks in `artifacts/inspection-arm-policy-v1`. These checks include rigid lens attachment, distinct physical endpoints, frustum coverage, no readback during arm travel, pause cancellation and fresh spot-0 restart; they make no HTTP requests. Rendered scans and actual inference require separate checks.

The original process implementation passed the UE 5.8 build and 58 native policy checks. The launcher passes 28 reusable PowerShell regression checks. A separate private process check also rejects the failed live report despite Unreal returning exit code zero. The latest native policy report passes the production launcher validator with its actual launch session. The policy run submits zero HTTP requests and therefore establishes controller behavior, not model accuracy. It exercises physical cooling during holds, two fresh G replies, fault return and cleanup, pause/stale-result handling, disabled inference, exact smoke-body outcomes and the separate thermal acceptance audit.

The retained original 64-episode capture contains 448 batches and 1,280 PNGs. An independent audit of all 1,152 thermal PNGs found the relevant strip/spot peaks visible and all hot/borderline/cool labels consistent with the fixed sensor window; maximum peak disagreement was 0.21 C. Captured NIR side views are distinct, and healthy/missing-door examples were visually reviewed. Across all four observer modes, the two NIR and three thermal sensor PNGs are byte-for-byte identical in a frozen paired check. These checks assess this authored renderer, not real-camera calibration or generalization.

The previous visual-cycle regression also passes all nine existing suites (12,261 checks), the separate RealismSmoke passes 681 checks with reviewed mechanism/HUD screenshots, and cabinet/readback checks retain their existing criteria. Private reports live under `artifacts/process-policy-v6`, `artifacts/process-observer-parity.json`, `artifacts/process-thermal-pixel-audit.json`, `artifacts/process-legacy-v2`, `artifacts/process-realism-v1`, `artifacts/process-cabinet-v2` and `artifacts/process-readback-v1`. Model experiments and their separate acceptance status are recorded in [model classification](model_classification.md).

### Selected model and complete process run

The frozen host configuration routes NIR to the 128-update adapter and thermal to its 128-update lower-learning-rate continuation. Both were trained on the same shared corpus; modality-only fitting has not yet been compared. On the untouched 12-episode test, 82 of 84 actions were correct with zero observed false G: one healthy door batch was falsely rejected, and one cool adhesive batch held. Median host response time was 54.2 ms. This is a small synthetic test on the retained earlier camera views, and the remaining errors are documented in the model guide. The new physical three-pose scan changes thermal views 1 and 2 and requires separate development evaluation. No adapter has been retrained for that change.

A subsequent native `ProcessSmoke` passed with actual model inference: bodies 1 and 3 departed after welding, while faulty body 2 returned through entry. The production launcher validator confirmed the native verdict and matching launch session. The run lasted 98.28 simulation seconds (110.16 seconds including startup/shutdown); 45 recorded decision replies had median native HTTP latency 68.01 ms while Unreal rendered. These HTTP timings exclude capture scheduling and the required second G observation. Both adhesive approvals were below 45 C (40.29 and 36.98 C), and both weld approvals were below 55 C (35.75 and 30.49 C). These temperatures were audited only after the model actions and did not control them.

The run saved 14 screenshots at 1600 x 900; actual adhesive, door rejection and weld-release views were reviewed. Private evidence is under `artifacts/process-selected-live-v1`, including `process_runtime.json`, `launcher_verification.json`, `verified_summary.json` and per-session screenshots. The final Python suite passes 81 tests. Successful native sequencing does not erase the held-out classification errors or establish EVK or real-factory performance.


## Historical scene and physical-camera validation

The following records apply only to the former door process and its successive camera/heat recipes. They do not validate panel-v2.

### Initial capture audit

The initial private 64-episode experiment contains 448 batches and 1,280 lossless images: 64 two-view door batches and 384 three-view thermal batches. All images are 448 x 256 and exact grayscale. Every thermal batch's visible maximum matches the apparent-temperature prediction within 0.537 of an 8-bit code, and the pixel-derived readiness label agrees with the recorded label. This establishes the illustrative rendering contract, not measured physical temperature or model accuracy. Door cases include 32 seated, 11 loose, 11 fallen and 10 missing bodies; native generated-mesh audits report no degenerate triangles or winding/normal mismatches. Whole episodes remain in one split.

The audit found a small physical placement defect in five negative-side fallen samples: the former 4 cm rest offset let a 4.1 cm handle penetrate the floor by 0.1 cm. Runtime contact clearance was increased to 4.35 cm. Those floor pieces lie outside the fixed NIR crops; the captured door-aperture evidence and readiness labels remain valid. Existing images and the original audit finding were retained rather than silently replaced. Private reports: `artifacts/process-dataset-physical-audit.json` and `artifacts/process-thermal-pixel-audit.json`.

The unchanged legacy welding regression also passes all nine independent suites (12,261 checks) against the corrected 1600 x 900 native capture in `artifacts/process-legacy-v2`. The first run used 1280 x 720 and was rejected by the existing image-contract checks; it was rerendered at the required resolution without relaxing any criteria.

The R4 parking update follows the frozen adapter evaluation on the original episode capture. Original validation/test numbers describe that retained dataset; the updated process is checked separately with live model inference. The change does not retrain against the reserved test episodes.

R4 parking validation passes the UE 5.8 build, 81 Python tests, 71 native policy checks (including 13 new arm/capture checks), the complete healthy/fault/healthy model-driven sequence, and the existing nine welding suites (12,261 checks). Rendered adhesive and welding views were reviewed: the camera arm deploys for thermal inspection and is parked while the other three weld. A fresh one-episode offline capture completed all seven batches/20 PNGs without waiting indefinitely. All 18 thermal PNGs match the original episode pixels exactly; the two NIR images differ in the changed scene. Private reports are in `artifacts/process-r4-rest-policy-v1`, `process-r4-rest-live-v1`, `process-r4-rest-capture-v1` and `process-r4-rest-legacy-v1`.

### Physical three-pose scan validation

The rigid-camera scan passes the UE 5.8 build, 81 Python tests and all 82 native policy checks in `artifacts/inspection-arm-policy-v1`. The policy fixture retains all 71 earlier checks and adds physical joint travel, intermediate poses, fixed lens attachment, three distinct endpoint positions separated by more than 32 cm, all-corner frustum coverage, capture suppression during travel, pause/cancellation and ordered restart. It sends no HTTP requests and does not establish rendered camera quality or model accuracy. The original 82/84 test result and pixel-parity checks above remain historical evidence for their retained camera views; no adapter retraining accompanies this motion change.

The actual-model run in `artifacts/inspection-arm-live-v1` completed healthy exit / loose-door return / healthy exit, with all four thermal acceptance audits passing. Its 63 thermal captures formed 21 complete batches and passed 3,894 independent physical-capture/transport checks; the live curtain audit passed 27 checks. The three rendered arm positions and raw thermal views were visually reviewed. A CPU sweep of the source meshes across 405 sampled arm poses found at least 72.16 cm separation from the car axis-aligned bounding box; this is sampled geometry evidence, not full swept-volume certification. The unchanged legacy branch also passed all nine rendered welding suites (12,261 checks) in `artifacts/inspection-arm-legacy-v1`.

The new eight-episode capture in `artifacts/inspection-arm-capture-v1` contains 56 batches and 160 PNGs. It passed 11,311 scan/image checks, with the simulation clock frozen across every offline thermal batch. A separate development evaluation sent only the 42 train/validation batches from six bodies to the unchanged selected adapters; the 14 test batches were excluded. Literal labels matched in 40/42 cases and gate actions in 42/42, with zero observed false G. Two hot adhesive batches returned A instead of R, both correctly holding. This small development check does not replace the earlier withheld test. No training or adapter-selection change was made; see [model classification](model_classification_v1_history.md#physical-arm-scan-development-check).

The independent thermal pixel audit agrees with the expected G/A/R band for all 48 thermal batches. Maximum rendered peak error is 0.496 grayscale levels (0.194 C), target projections retain at least 25.6 px margin, and no view saturates to white. Cold adhesive can be darker than the surrounding 23 C sheet, so the audit also samples its projected center instead of treating a whole-view maximum as the strip temperature. All weld maxima are present in their target regions. The report is `artifacts/inspection-arm-capture-v1/thermal-pixel-audit.json`; inspecting rendering geometry/radiometry does not query the reserved test images through the model.
