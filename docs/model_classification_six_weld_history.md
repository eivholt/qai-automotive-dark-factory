# Historical six-front-weld model, before continuous perimeter sealant

This records the earlier six-weld views and prompt. Its adapter is incompatible with the current sealant prompt; its deployment status is historical.

# Six-weld panel inspection with Cosmos Reason2

The current `automotive-panel-process-v3` scene has two front adhesive beads and six exposed weld contacts. Three physical R4 views cover weld pairs `[0,1]`, `[2,3]`, `[4,5]`; adhesive views retain `[0,1,0]`. The stage prompts, image-only transport and gate policy remain unchanged.

## Selected host runtime

The selected six-weld runtime is `checkpoints/panel-six-weld-shared-448-v4`, with conditional label confidence increased from 0.80 to **0.98**. The existing prespecified validation-only abstention grid selected this setting. Temperature limits remain 45/55 C, ambiguity bands remain +/-3 C, label mass remains 0.5, and both gates still require two matching fresh batches. Lower-confidence output holds A.

Exact serving validation is **95/96 labels/actions, zero false G and zero wrong presence branches**. One cool weld batch holds A; adhesive cool recall is 12/12 and weld cool recall 11/12. HTTP median/p95 is **53.45/58.85 ms**. These are repeated development episodes, not a new independent held-out test. The actual model-driven integration completed successfully, as recorded below.

Backend alias: `Cosmos-Reason2-2B-Torch-BF16-8f759484957330487be026656f6244d4-linear-nonoverlap-v1`. Weights SHA256: `016e01e859bd821c8b3d57dfdb835c733330b8da84e441c834a818a3f075f288`. The prior shared256 adapter scored 80/96 labels, 86/96 actions and seven false G on the changed views. Its earlier 95/96 held-out result belongs to the [three-contact experiment](model_classification_two_bead_history.md).

```powershell
# Reuse a healthy matching service. Run this only when it is absent.
wsl.exe bash scripts/model/start_host_service.sh --port 18191 --adapter checkpoints/panel-six-weld-shared-448-v4 --linear-patch-embedding
Invoke-RestMethod http://127.0.0.1:18191/health
.\unreal\Scripts\run.ps1 -Scene welding -Process -ProcessObserverMode 3 -ProcessEndpoint http://127.0.0.1:18191/v1/inspect
```


The host uses CUDA Torch BF16 and the recorded `--linear-patch-embedding` path. `DF_MODEL_PYTHON` selects the separately prepared WSL environment and `DF_BASE_MODEL` must name a complete local Cosmos-Reason2-2B base. The private adapter includes weights, configuration and `training_report.json`; nothing is downloaded by the launcher.

## Host service lifecycle

The repository's `scripts/model/start_host_service.sh` starts `dark-factory-panel-model.service` under the WSL user manager. Reuse a compatible healthy service; the launcher refuses an existing unit or occupied port. It survives closing the launch shell for the current WSL session, but is not installed for Windows reboot or WSL shutdown. Check readiness with `Invoke-RestMethod http://127.0.0.1:18191/health`. Inspect logs with `wsl.exe journalctl --user -u dark-factory-panel-model.service -n 80 --no-pager`.

An unavailable endpoint or stale/incompatible reply always holds. No scene label, timer or private temperature can release a model gate. Only stop this repository's service when replacing its runtime; leave adjacent projects' services alone.

## Capture and fresh fitting

The current view/capture/thermal recipes are `rigid-arm-panel-six-weld-scan-v4`, `panel-six-weld-episodes-v4` and `panel-six-weld-contact-core-halo-v5`. Lossless grayscale remains 448 x 256. Thermal sensors encode 20-120 C; observer palettes span 0-120 C independently. Each sensor pose requires three actual discarded renders before publication.

The private capture contains 64 whole-body episodes, seed 4307: 40 train / 12 validation / 12 reserved test episodes, or 320 / 96 / 96 three-image batches. No episode crosses splits. The development validation cohort is reused explicitly for this geometry refinement; it is not a new independent test. No reserved test predictions are requested for this refinement.

After native capture and camera/thermal audits, prepare a fresh output:

```bash
PYTHONPATH=src "$DF_MODEL_PYTHON" scripts/model/prepare_lora.py \
  --manifest artifacts/six-weld-episodes-v3/manifest.jsonl \
  --grouping shared --output data/panel-six-weld-v4
PYTHONPATH=src "$DF_MODEL_PYTHON" scripts/model/train_lora.py \
  --model "$DF_BASE_MODEL" \
  --train data/panel-six-weld-v4/shared/train.jsonl \
  --validation data/panel-six-weld-v4/shared/validation.jsonl \
  --init-adapter checkpoints/panel-two-bead-shared-256-v3 \
  --output checkpoints/panel-six-weld-shared-384-v4 \
  --max-steps 128 --rank 8 --lora-scope multimodal --batch-size 6 \
  --gradient-accumulation 4 --learning-rate 0.00003 --seed 42 \
  --gpu-memory-fraction 0.55 --linear-patch-embedding
```

This continuation starts with compatible adapter weights, a fresh optimizer and sampler; it records the parent adapter hashes and new train/validation manifest hashes. It does not overwrite or relabel the old checkpoint. Exact merged-server validation and the actual Unreal process run are required before promotion. The public clone includes neither the capture nor checkpoints; reproduce them locally following [capture commands](process_inspection.md#capture-and-timing).

The first six-weld candidate (`shared384`) scored 95/96 through the serving API, with one ambiguous weld A incorrectly released as G, and was not selected. A follow-up adds ten independently seeded training episodes from a 16-episode capture with seed 7319. The other six episodes are excluded from training. The combined training split has 400 batches from 50 bodies; validation remains the same 96 batches from 12 bodies.

To reproduce this combination, first capture the additional episodes and retain their native split assignments. Build a private combined manifest containing the original train/validation rows plus only `split == "train"` rows from the additional capture. Prefix each sample ID with its dataset seed, keep episode IDs unchanged, and resolve image paths against each source manifest's directory. Then run `prepare_lora.py --grouping shared` on that combined manifest into `data/panel-six-weld-augmented-v4`. Do not copy validation or test episodes into the training split.

The follow-up uses the same training command with the augmented train/validation paths, `--init-adapter checkpoints/panel-six-weld-shared-384-v4`, a fresh `--output checkpoints/panel-six-weld-shared-448-v4`, `--max-steps 64` and `--learning-rate 0.00001`. Every other training setting stays the same. The first six-weld fit remains a failed development candidate, not a promoted model.

A thermal-only 64-update continuation was also tested with the existing two-model modality router, retaining shared448 for NIR. It did not correct the ambiguous weld error, even across the stricter confidence grid, and is not selected. One shared model with calibrated abstention uses fewer resident model resources. Preserve the failed candidates and their results as development evidence.

The selected lineage adds 128 updates in 401.85 seconds and 64 updates in 200.04 seconds to the prior 256-update adapter. The separate unselected thermal experiment took 201.30 seconds. All optimizer timings exclude loading, capture, validation and native rendering; GPU peak allocation was approximately 6 GiB. No test predictions were used for these refinements.

## Validation and deployment sequence

1. Verify the candidate's `/health` identity against its adapter/configuration/training-report hashes, all four training stages, prompts, BF16 backend and projection method.
2. Query only the development validation manifest through the exact merged serving runtime. Reject candidates with false G or wrong presence branches; require at least 90% clear-cool recall per thermal stage, as in the prespecified calibration tool; preserve every failed report.
3. Freeze the accepted candidate and policy. For this refinement, the existing seed 4307 test cohort stays unqueried because it has historical held-out exposure; a future independent accuracy claim needs new body episodes.
4. Run actual `-ProcessSmoke` against the candidate endpoint, separately from `-ProcessPolicySmoke`. Require assembly/preassembled/assembly, three forward departures and six action-time thermal audits. Camera/curtain postchecks must pass.
5. Promote only that identity to the normal host service and reopen the interactive demo. Prior weights stay available; never weaken gates or use private scene state to force a release.

## Rendering and motion evidence

The full capture passes 127,778 camera/PNG checks and 1,404 thermal target checks, including each of the six welds. The narrow adhesive uses a 3x3 pixel center region to exclude adjacent steel; weld cores retain 5x5 peak sampling. Temperature tolerance remains 2.5 C and labels use the maximum over actual contacts. Missing or occluded target centers fail. A final 8-episode recapture preserves all 192 raw PNGs byte-for-byte after the NIR withdrawal change.

The Unreal build, 109 Python tests, 91 native policy checks, 38 rendered motion checkpoints, procedural thermal/spatial/transfer suites, nine legacy welding suites and 681 realism checks pass. Camera clearance sampling covers 25 poses per route leg plus the three stage-family transitions against the parked welder; this is not full swept-volume or industrial collision certification. The final NIR withdrawal routes via spot0 within the existing 0.8 seconds, keeping settled sensor poses unchanged.

## Final native integration

The selected runtime passed the actual Unreal process: assembly / preassembled / assembly, three departures, all six thermal acceptance audits and 29 image-only requests, in 183.41 wall-clock seconds. Camera and live-curtain postchecks passed 5,636 and 27 checks. Actual HTTP reply median/p95 during rendering was 84.27/84.93 ms across 29 distinct reply outcomes, excluding phase/body duplicates. No ground-truth fields were sent to the model. The exact frozen identity and calibrated policy now serve on primary 18191; the duplicate candidate service is stopped. The interactive demo was reopened. Private evidence: `artifacts/six-weld-live-v1` and `artifacts/panel-six-weld-model-v4/selected-runtime.json`.

## Earlier experiments

[Three-contact two-bead model](model_classification_two_bead_history.md), [three-bead panel-v2](model_classification_panel_v2_history.md), and [door inspection](model_classification_v1_history.md) retain their original results and scope. The present task has no real-camera or IQ9 deployment accuracy evidence.
