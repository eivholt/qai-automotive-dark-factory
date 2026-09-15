# Capture and fine-tuning workflows

## Current automotive panel-v3 model

The imported demo has two adhesive beads and six weld contacts. One shared multimodal LoRA covers the four NIR/thermal gates under `automotive-panel-process-v3`. The [current model guide](model_classification.md) records the six-weld capture, training commands, candidate selection and serving status. The [earlier three-contact experiment](model_classification_two_bead_history.md) retains its completed shared256 training and held-out results; those scores do not validate the new views.

The seed 4307 capture contains 64 whole-body episodes: 40 training, 12 validation and 12 reserved test episodes, with 320/96/96 three-image batches. All states and views of a body stay in one split. The six-weld refinement reuses development validation explicitly and does not query the historically exposed test cohort. A future independent held-out accuracy claim needs new body episodes.

Every gate receives three lossless 448 x 256 grayscale PNGs. Adhesive mapping is `[0,1,0]`; weld pairs are `[0,1]`, `[2,3]`, `[4,5]`. Sensors encode 20-120 C independently of the 0-120 C observer palette. Review actual camera coverage and thermal pixels before fitting, then validate through the exact host runtime and actual Unreal process. Images, private labels, model weights and purchased geometry remain local.

The selected shared448 lineage starts from compatible shared256, adds 128 updates at 3e-5 and then 64 at 1e-5. The second fit expands to 50 training episodes / 400 batches with ten new training episodes from seed 7319. Rank 8, multimodal targets, BF16, batch 6 and accumulation 4 stay fixed. Reports record parent adapter and manifest hashes. The 0.98 confidence calibration gives 95/96 development decisions, zero false G and one conservative cool-weld hold. A separate thermal-only continuation did not improve the result and was not selected. Shared versus per-modality adapters remains an open experiment; selecting a shared adapter does not establish that it is best for future modalities.

## Procedural battery-tray preparation

The remainder documents the reusable starter pipeline in `scripts/training/`. Its one-image contract, proposed release gates and optional trainer are separate from the current automotive workflow above.

The battery-tray cell is a replaceable preparation scenario. Four modules should be present, seated and aligned. `G` means visible normal completion, `R` means a visible missing or misaligned module, and `A` means the image cannot support a reliable inspection. The fixed wording, label mapping, PNG dimensions and generation settings live in [inspection_contract.json](../configs/inspection_contract.json). Changing the task wording or camera processing requires a new dataset and evaluated adapter version.

This repository provides a deterministic capture planner, strict dataset validator, frozen dataset export and an opt-in BF16 LoRA trainer. No battery-tray adapter is selected for the current demo. This procedural workflow requires no purchased assets, and planning creates metadata rather than evidence of model accuracy.

### Reuse from the previous project

The [previous tutorial](https://github.com/eivholt/qai-physics-reasoning/blob/main/docs/isaac_sim_conveyor_reason2_tutorial.md) and [SpeedV1 scripts](https://github.com/eivholt/qai-physics-reasoning/tree/main/scripts/reason2_finetune) establish the single image, image-first user prompt, one `G/A/R` output token, merged adapter and direct PNG transport to one resident GenieX + QAIRT worker on IQ9. The new label semantics require a new adapter and fresh gates; a parcel adapter is not evidence of battery inspection accuracy.

Cosmos-Reason2-2B uses the Qwen3-VL architecture, so the optional trainer loads `Qwen3VLForConditionalGeneration`, as in [NVIDIA's model card](https://huggingface.co/nvidia/Cosmos-Reason2-2B). It deliberately specializes the model to this short classifier contract; NVIDIA's general long-reasoning examples serve a different task.

The previous accepted model adapted both visual and language projections. This starter freezes the vision tower and trains decoder LoRA as a conservative first experiment. That may be inadequate for a new geometry or night-vision domain. Compare a separate candidate with `freeze_vision: false`, which adds the visual `qkv/linear_fc1/linear_fc2` projections, only after the baseline and error analysis justify it. Freeze base weights in both cases.

### Reproducible capture plans

From the repository root, with Python 3.11 or later:

```powershell
python scripts/training/prepare.py plan --output artifacts/planned/manifest.jsonl
python scripts/training/prepare.py validate --manifest artifacts/planned/manifest.jsonl --dataset-root artifacts/planned --plan-only
python scripts/training/train_lora.py --manifest artifacts/planned/manifest.jsonl --dataset-root artifacts/planned --output artifacts/adapters/pilot --dry-run
```

These commands need neither ML packages nor a GPU. The defaults in [randomization.json](../configs/randomization.json) produce 100 matched groups, 300 planned images and an exact 80/10/10 group allocation. Each group contains one G, one A and one R episode; R alternates missing and misaligned modules. Increase group counts after the scenario is selected. The default validation and test panels are deliberately small preparation panels and do not meet the proposed release sample counts.

Material reflectance/roughness, tray translation, camera mount perturbation, field of view, exposure, sensor gain, additive noise and illumination are drawn once per nuisance family and copied unchanged across its G/A/R labels. A separate state RNG chooses the target module and defect/occlusion parameters. The occlusion fraction controls cover geometry; it is not a measured visible-pixel fraction. This avoids labels being encoded by light level, camera pose, background or material. Camera perturbation means different installation episodes; the sensor remains fixed during each episode and independent of controller-driven spectator motion.

The planner allocates whole nuisance families to splits before frames are captured. Related seeds, sequential frames, matched labels and repeated views of the same physical setup must never move independently into another split. Determinism applies to metadata draws; pixel-identical Unreal rendering also depends on engine version, renderer settings, assets, GPU, settle timing and temporal history.

Active illumination is a visible-renderer proxy for an NIR camera. It is not a spectral sensor model or thermal simulation. Keep all balanced training groups in `active_nir_proxy` mode. Capture an independent sensor-off/poor-visibility stress panel labelled A: switching the sensor off only for A in ordinary balanced groups creates a shortcut. Do not randomize valid images so aggressively that their G/R labels become unobservable; visually audit renders and relabel uncertain examples A.

### Capture metadata and freeze

The Unreal batch runner accepts a plan and writes actual images plus capture metadata. From PowerShell:

```powershell
./unreal/Scripts/run.ps1 -Plan artifacts/planned/manifest.jsonl -Output artifacts/captures
```

The executable flags are `-DFPlan=<absolute plan path>` and `-DFOutput=<fresh output directory>`. Output is `manifest.jsonl` plus the image paths specified by the plan. Wait for `DF_DATASET_COMPLETE` before validation/freezing. See the [Unreal tutorial](dark_factory_tutorial.md) for build and launch prerequisites. Plans are read-only recipes; captured rows must record the actual applied parameters and `capture_status: captured`. Never change planned rows into training data without rendering and checking their images.

Each JSONL row includes:

| Field | Purpose |
| --- | --- |
| `image` | Dataset-relative path to an 8-bit lossless 448 × 256 PNG |
| `sample_id`, `episode_id`, `frame_index` | Unique capture and sequence identity |
| `leakage_group`, `split`, `seed` | Related nuisance family and its immutable train/validation/test assignment |
| `label`, `state` | Simulator ground truth, used only in metadata/training supervision |
| `sensor`, `randomization` | Actual camera, lighting, material and sensor parameters |
| `contract_version`, `prompt_sha256` | Runtime/training task identity |
| `image_sha256` | Added when images are frozen, detecting later changes |

No label, seed, case name, HUD or simulator state may appear in the inference image or user prompt. Capture after state changes have reached the renderer and confirm that the saved PNG agrees with its metadata. Audit examples of each split/class and inspect the hardest G/R and A boundaries.

After real capture, substitute the actual paths:

```powershell
python scripts/training/prepare.py validate --manifest artifacts/captures/manifest.jsonl --dataset-root artifacts/captures
python scripts/training/prepare.py freeze --manifest artifacts/captures/manifest.jsonl --dataset-root artifacts/captures --output artifacts/frozen/candidate-v1
```

Freeze refuses a nonempty destination. It saves the hashed manifest, class/split counts, portable `train.jsonl`, `validation.jsonl`, `test.jsonl` and an unevaluated release manifest. Images remain in the original capture directory; pass that directory as `--dataset-root`. The portable SFT records use an image-first, user-only prompt and an assistant completion containing one letter.

Validation rejects group/episode leakage, unequal G/A/R counts within matched groups/splits, contradictory labels, duplicate frame IDs, prompt drift, unsafe image paths, corrupt/wrong-size PNGs, changed image hashes, exact image content repeated across splits and identical images with different labels. Grouping prevents known near-duplicate sequences crossing splits; byte hashes do not detect every visually similar render or re-encoded duplicate. Review image similarity and independent asset/camera coverage before calling a test set independent. Repeated frames are not additional independent evidence.

Keep held-out assets, nuisance seeds and final test examples outside curriculum tuning. Diagnose failures on validation only; select a final candidate before opening its frozen independent test results. Quantization calibration uses a representative subset of training images, including low-reflectance/high-noise cases, never the test set.

### Optional model-host preflight and training

The optional requirements follow the already installed previous-project WSL environment: Torch 2.9.0 (the observed wheel is `2.9.0+cu128`), Transformers 4.57.3, PEFT 0.20.0, Accelerate 1.12.0 and Pillow 12.0.0. NVIDIA's [reference inference script](https://github.com/nvidia-cosmos/cosmos-reason2/blob/main/scripts/inference_sample.py) confirms the core Torch/Transformers/Accelerate/Pillow baseline. Use a separate Linux/CUDA environment and the appropriate CUDA wheel; this requirements file is not a hardware-independent deployment lock. Record `pip freeze` for the actual training host. Do not install GPU packages merely to run dataset preparation.

The previous local environment can be reused read-only for processor checks. The following commands illustrate the Linux training host; the base checkpoint can be a local directory or `nvidia/Cosmos-Reason2-2B --revision <full-commit-hash>`. A mutable Hub branch is rejected for actual training.

```bash
python scripts/training/train_lora.py \
  --manifest artifacts/frozen/candidate-v1/manifest.jsonl \
  --dataset-root artifacts/captures \
  --model /path/to/Cosmos-Reason2-2B \
  --output artifacts/adapters/battery-v1 \
  --check-processor
```

Processor preflight loads no model weights, validates that G/A/R each map to a distinct single token, checks image decoding without resizing, verifies 112 visual tokens, checks the image-first chat template has no inserted thinking prefix, and refuses a prompt/image combination exceeding the intended 512-token context. The exported GenieX template must independently be verified against this recorded prompt template: server defaults can add system text or reasoning even when client messages do not.

Remove `--check-processor` to run the explicitly requested training on a BF16-capable CUDA GPU. Use `--max-steps 5` for an initial optimization smoke check before a full candidate. The trainer uses only the train split. All prompt/image positions and EOS are masked; exactly one class token contributes causal next-token loss per example. Runtime generation must also be deterministic, reasoning-disabled and limited to one token. Defaults are rank 32, alpha 32, dropout 0.05, learning rate 1e-4, one epoch, batch size 1 and accumulation 8; tune only against validation.

Outputs include the adapter, processor, run configuration/manifest/template hashes, class token IDs, package versions, training metrics and local base-model file hashes (or immutable Hub revision). Successful training is recorded as `TRAINED_NOT_RELEASED`, never an accuracy claim.

### Export and IQ9 release gates

Use [training.json](../configs/training.json) as a proposed gate checklist, not a claim that any gate has passed. Freeze sufficient unique captures and report per-class recall/confusion, false G decisions on defects or unobservable frames, invalid output rate, capture-to-decision latency, warm p50/p95 and cold start separately. The proposed minimum panels are 60 validation and 30 independent test images per class; a finite panel with zero false G does not establish zero deployment risk. Set an explicit latency target once the manufacturing cycle is chosen.

The prior SpeedV1 release had stronger perfect-score gates for its own frozen task. Preserve that workflow shape and remeasure this task:

1. Evaluate the adapter on frozen host validation and untouched test images using unconstrained one-token generation; reject out-of-contract tokens rather than selecting the best G/A/R logit.
2. Merge the adapter into a clean, identical BF16 base using the previous project's `merge_adapter.py`. Archive adapter/base/processor hashes and compare merged predictions to adapter predictions. Merging removes runtime adapter plumbing; quantization is a separate step.
3. Reuse the previous `build_evk_speed_v1.sh` as a reference for a separately named candidate: task-matched 448 × 256 calibration, CL512 context, full DeepStack interface, W8 text with the known vision precision route, and one vision plus four text QAIRT contexts. Update task paths, contract and tokenizer IDs; do not point its output at the accepted parcel bundle.
4. Gate the exported/quantized bundle directly on IQ9 with lossless PNG payloads and the new prompt. Verify the installed QAIRT, GenieX, OS/firmware and bundle versions rather than assuming compatibility from the previous run. Record all hashes/versions in `release_candidate.json`.
5. Run a resident-worker soak of at least 360 requests and the packaged Unreal path, including G/A/R, sensor-off, late/stale replies, timeouts and recovery. Repeated soak frames measure sustained behavior and latency, not 360 independent accuracy examples.

Use the existing resident GenieX + QAIRT direct-PNG API architecture. Provisioning, worker replacement, quantization/export and deployment changes are intentionally deferred until a candidate exists and is evaluated. This preparation does not modify the previous production service.

A LoRA adapter can improve task accuracy and make short answers reliable. It does not by itself accelerate the vision encoder or prefill. Reduced image tokens, a shorter prompt, one-token decoding, resident inference and the chosen precision each affect latency; benchmark them separately before attributing speedups.
