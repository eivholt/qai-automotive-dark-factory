> Historical exposed four-edge sealant results. The current three-edge panel-contact heat implementation uses new captures, prompts and adapter validation; see [current model status](model_classification.md).

# Perimeter sealant and pinch-weld inspection with Cosmos Reason2

The current automotive-panel-process-v3 scene uses two continuous sealant runs
around the panel contacts and six pinch welds. R4 takes three physical images for
each of four model stages. NIR inspects presence and seating; thermal views inspect
sealant and weld cooling. See [physical implementation](pinch_welder_and_sealant.md).

## Selected host runtime

The selected shared adapter is `checkpoints/panel-perimeter-shared-768-v5`.
All four stages use the same rank-8 multimodal LoRA merged into the local
Cosmos-Reason2-2B base, with Torch BF16 and the recorded linear patch projection.
The model has not been deployed to IQ9. Weights and datasets remain private.

Development validation of the exact serving runtime is **288/288 letters and gate
actions**, with zero observed false G and zero wrong presence branches. Both thermal
stages accept every clearly cool validation batch. HTTP median/p95 is
**52.84/58.52 ms** on the RTX 5090; median GPU forward/scoring is 43.75 ms.
These timings exclude robot travel, capture, cooling and full-cycle throughput.
The 36 whole-body validation episodes include reused development cohorts; they
are not a new independent held-out test. No reserved test predictions were queried.

The conditional confidence threshold remains **0.98**, label mass 0.5, and gates
require two matching fresh batches. Temperature limits remain 45/55 C with +/-3 C
ambiguity bands. A, transport errors, stale replies and incompatible identities
always hold. No private label, temperature or timer can release a model gate.

Backend alias: `Cosmos-Reason2-2B-Torch-BF16-123a74ff20dcfbc802caa4aff93a9424-linear-nonoverlap-v1`.
Adapter weights SHA256: `6e9422b830ebbe247c9e600d41773372f0656cb428318d07028689eb4ec04519`.
The frozen local identity is recorded in
`artifacts/pinch-sealant-v1/selected-runtime.json`.

```powershell
# Reuse a healthy matching service. Start it only when absent.
wsl.exe bash scripts/model/start_host_service.sh --port 18191 --adapter checkpoints/panel-perimeter-shared-768-v5 --linear-patch-embedding
Invoke-RestMethod http://127.0.0.1:18191/health
.\unreal\Scripts\run.ps1 -Scene welding -Process -ProcessObserverMode 3 -ProcessEndpoint http://127.0.0.1:18191/v1/inspect
```

The old shared448 adapter is incompatible with the revised sealant prompt. Its
95/96 development result belongs to the [previous six-weld scene](model_classification_six_weld_history.md).
Do not relabel an old adapter's training report to bypass prompt compatibility.

## Host service lifecycle

`scripts/model/start_host_service.sh` starts `dark-factory-panel-model.service`
under the WSL user manager. It refuses an existing unit or occupied port. The
service survives closing its launch shell during the current WSL session, but is
not installed for Windows reboot or WSL shutdown. Health must report ready and the
expected contract, prompt hashes and adapter identity before launching the process.

```powershell
wsl.exe systemctl --user status dark-factory-panel-model.service
wsl.exe journalctl --user -u dark-factory-panel-model.service -n 80 --no-pager
# Stop this repository's service when finished:
wsl.exe systemctl --user stop dark-factory-panel-model.service
```

The helper accepts `DF_MODEL_PYTHON`, `DF_BASE_MODEL` and `PYTHONPATH` overrides.
The base must be complete locally; the launcher does not download weights. Leave
the adjacent physics project's deployment untouched. An unavailable endpoint
holds the process and does not mean saved training has been lost.

## Capture and fitting

The current physical view recipe is `rigid-arm-panel-perimeter-scan-v5`, heat
recipe `panel-perimeter-contact-core-halo-v6`, and capture recipe
`panel-perimeter-episodes-v8`. Frames remain three lossless 448 x 256 grayscale
PNGs in spot1/spot2/spot3 order. Thermal encoding remains linear 20-120 C; observer
palettes are independent. Each actual rigid arm pose receives three discarded
renders before publication. Prompts come from `configs/process_inspection.json`.

Capture revisions preserve distinct provenance:

| Recipe | Added coverage | Seed / whole-body episodes |
| --- | --- | --- |
| Perimeter v5 | Complete sealant geometry and revised arm views | 4307 / 64 |
| Perimeter v6 | Near-ambient sealant and weld cooldown | 7319 / 32 |
| Perimeter v7 | Independent, episode-frozen motor/casting temperatures | 9157 / 64 |
| Perimeter v8 | No residual weld field on an unwelded held panel | 14791 / 32 |

The latest fit uses the first three cohorts: **100 training bodies / 800 batches /
2,400 PNGs** and 30 validation bodies / 240 batches. There are 100 presence,
100 seating, 300 sealant and 300 weld training batches. The final serving evaluation
adds six v8 validation bodies, giving 36 bodies / 288 batches. The 20 v8 training
bodies were not fitted. All 36 reserved test bodies remain unqueried. Whole-body
splits prevent different views, stages or thermal cases of one body crossing splits.

Training added 128 updates at 3e-5 to shared448, then 64 at 1e-5, then 128 at 2e-5:
320 new updates, cumulative lineage 768. All runs used rank 8, multimodal targets,
batch 6, gradient accumulation 4, seed 42 and the linear patch embedding.
Peak Torch allocation was about 6.48 GB. Logged optimizer times were 402.4,
200.6 and 441.7 seconds; the last run overlapped offscreen rendering and is not
an isolated training-speed benchmark.

The first candidate was conservative on cool sealant; the second passed its
offline cohort but still held on live frames containing a warm robot casting.
Adding near-ambient coverage and independently varied background casting heat
resolved that live-input mismatch. These attempts and exact request snapshots
remain private development records. Gate thresholds were never relaxed.

A prompt-changing warm start requires `train_lora.py --init-contract` pointing to
the saved original contract. It checks the parent's original provenance and
records changed prompt hashes; architecture, image encoding, stages and camera
identities must remain compatible. Runtime loading still requires the current
contract. The new sealant prompt SHA is
`5cdbc5c87e70601842c48e1b408fc754ff15f2430c2e875d6d61bc8219ca651f`.

## Reproduce a fresh experiment

Capture into a new directory with the current recipe, then run the camera,
thermal and whole-perimeter coverage audits before fitting. The current v8
capture can differ from historical v5-v7 images; retain its own provenance.

```powershell
.\unreal\Scripts\run.ps1 -ProcessCapture -ProcessEpisodes 64 -ProcessDatasetSeed 14791 -Output artifacts\my-perimeter-capture
```

```bash
python scripts/verify_inspection_arm.py --contract panel --manifest artifacts/my-perimeter-capture/manifest.jsonl --output artifacts/my-perimeter-capture/camera-qa.json
python scripts/verify_panel_thermal.py --manifest artifacts/my-perimeter-capture/manifest.jsonl --output artifacts/my-perimeter-capture/thermal-qa.json
python scripts/verify_sealant_coverage.py --manifest artifacts/my-perimeter-capture/manifest.jsonl --output artifacts/my-perimeter-capture/coverage-qa.json
PYTHONPATH=src "$DF_MODEL_PYTHON" scripts/model/prepare_lora.py --manifest artifacts/my-perimeter-capture/manifest.jsonl --grouping shared --output data/my-perimeter
PYTHONPATH=src "$DF_MODEL_PYTHON" scripts/model/train_lora.py --model "$DF_BASE_MODEL" --train data/my-perimeter/shared/train.jsonl --validation data/my-perimeter/shared/validation.jsonl --init-adapter checkpoints/panel-perimeter-shared-768-v5 --output checkpoints/my-perimeter --max-steps 64 --rank 8 --lora-scope multimodal --batch-size 6 --gradient-accumulation 4 --learning-rate 0.00001 --seed 42 --gpu-memory-fraction 0.55 --linear-patch-embedding
```

These commands continue a compatible local adapter. They are not a claim that
64 updates will qualify a new experiment. Evaluate the exact merged service on
validation data, freeze the selection, and separately run actual ProcessSmoke.
Do not query reserved test rows during development or replace a model hold with
an offline temperature audit.

## Native integration and limits

The native policy fixture and actual-model ProcessSmoke are separate. The actual
fixture requires assembly/preassembled/assembly, three departures and six thermal
acceptance audits. The audits can fail a report but cannot change model actions.
The selected runtime completed assembly/preassembled/assembly with three forward
departures, all six thermal acceptance audits and 29 image-only requests in
201.26 wall-clock seconds. Camera/transport postchecks passed 5,636 checks and
live curtains passed 27. During rendering, HTTP median/p95 was
84.00/96.98 ms across 29 distinct reply outcomes. No ground-truth
fields were sent. Private evidence is `artifacts/pinch-sealant-live-768`.
The demo was closed after verification.

This is a small simulated-camera development experiment. Assigned emissivity,
surface temperatures, authored robot motion and synthetic nuisance distributions
are illustrative. There is no real-camera, weld-quality or IQ9 accuracy claim.

## Earlier experiments

[Previous six-weld model](model_classification_six_weld_history.md),
[three-contact two-bead model](model_classification_two_bead_history.md),
[three-bead panel-v2](model_classification_panel_v2_history.md) and
[door inspection](model_classification_v1_history.md) retain their original scope.
