# Panel-surface heat inspection with Cosmos Reason2

The added [permanent mounting frame](panel_mounting_frame.md) changes the
missing-panel images. The existing routed adapters completed the subsequent
three-body integration run with all six thermal acceptance checks in 180.24 s.
This is integration evidence, not a new held-out accuracy result. The adapter
development scores below were measured before the frame was added.

The current panel-v3 scene uses three-edge sealant, a panel raised 5 mm and six
pinch welds. The thermal sealant gate reads heat conducted into the opaque panel
surface. NIR inspects presence and seating. See [contact heat](panel_contact_heat.md)
and [pinch welding](pinch_welder_and_sealant.md).

## Selected host runtime

Two resident adapters are selected explicitly by **stage**, with one inference
per batch. The primary adapter handles presence, sealant readiness and seating;
the weld adapter handles weld cooling. Routing never depends on predictions,
temperatures, private labels or retries. A hold remains a hold.

| Stages | Adapter |
| --- | --- |
| panel_presence, adhesive, panel_seated | panel-contact-shared-960-v7 |
| weld | panel-contact-shared-896-v6 |

Both are rank-8 multimodal LoRAs merged into Cosmos-Reason2-2B for this host
experiment. The two base copies use **8.52 GB allocated CUDA memory**
after warmup. This preserves one forward pass per request but costs more memory
than one resident model. IQ9 deployment and adapter swapping remain future work.

Exact serving-runtime development validation is **191/192 decisions**, with
zero observed premature G approvals or wrong presence branches. One cool sealant
contact batch is conservatively held as R. Sealant cool-G recall is 23/24; weld
cool-G recall is 24/24. Both NIR stages are 24/24. The existing selection criteria
pass at unchanged **0.98** conditional confidence and 0.5 label mass. Gates still
require one fresh three-camera batch. A, transport errors and stale replies hold.

Host HTTP median/p95 is **50.65/54.99 ms** on the RTX 5090,
excluding camera movement, image capture and cooling. These are 24 synthetic
development validation episodes, not real-camera, production or EVK accuracy.
No reserved test predictions were queried. Validation was used for selection.

On the same 192 validation batches, shared896 alone and shared960 alone each
score 190/192 with one premature G: respectively sealant and weld ambiguity.
The selected stage routing scores 191/192 without those approvals. The adapters
also have different training coverage, so this is a local selection experiment,
not evidence that stage routing is generally superior. The earlier exposed
four-edge results remain [historical](model_classification_perimeter_history.md).

Backend: `Cosmos-Reason2-2B-Stage-Router-9aa89569685f2f76f418e54038658706`.
The full weights, prompt hashes and stage bindings are recorded privately in
`artifacts/sealant-three-edge-v1/selected-runtime.json`.

```powershell
# Reuse a matching ready service. Start it only when absent.
wsl.exe bash scripts/model/start_host_service.sh --port 18191 --adapter checkpoints/panel-contact-shared-960-v7 --weld-adapter checkpoints/panel-contact-shared-896-v6 --linear-patch-embedding
Invoke-RestMethod http://127.0.0.1:18191/health
.\unreal\Scripts\run.ps1 -Scene welding -Process -ProcessObserverMode 3 -ProcessEndpoint http://127.0.0.1:18191/v1/inspect
```

Do not use either shared adapter alone as the selected configuration. Existing
`--nir-adapter`/`--thermal-adapter` modality experiments remain supported;
`--weld-adapter` instead requires `--adapter` for the other stages and excludes
modality/GGUF routing. Each resident model validates its allowed stages and
provenance. The common router identity includes both weights and stage bindings.

## Capture and training provenance

| Item | Current value |
| --- | --- |
| Semantics | automotive-panel-process-v3 |
| Physical views | rigid-arm-panel-three-edge-scan-v6 |
| Heat | panel-three-edge-contact-transfer-v8 |
| Offline cohorts | 64 episodes each, seeds 18517 and 27139 |
| Capture recipes | v10 randomized NIR; v12 randomized plus nominal NIR |
| Full train / validation / reserved test episodes | 80 / 24 / 24 |
| Offline three-image batches | 640 / 192 / 192 |
| Additional training-only data | One reviewed live missing-panel batch, 3 images |
| Primary training images / validation images | 1,923 / 576 |
| Shared896 | 128 updates from exposed-sealant shared768, learning rate 0.00003 |
| Shared960 | 64 updates from shared896, learning rate 0.00001 |
| Both fits | Rank 8, batch 6, accumulation 4, seed 42, multimodal scope |
| Logged optimization time | 432.3 + 191.2 seconds; capture/evaluation separate |
| Training peak CUDA allocation | 6.53 GB |

All frames are three lossless 448 x 256 grayscale PNGs in physical spot1/spot2/spot3
order, after three discarded settled renders. Thermal input remains 20-120 C.
Panel contact labels use emissivity 0.74 apparent surface temperature. Source
sealant temperature is separate. HUD, fault states, temperatures, QA coordinates
and case metadata never enter model requests. Partial capture v11 is unused.

Each cohort passes camera, contact-center and complete three-edge coverage
checks before training. Nominal illumination occurs every fifth v12 episode,
covering every presence/seating label without correlating power and label.
Whole episodes retain their declared split. The reviewed live example is a
separate training-only episode with request hash and manual visual label provenance
in `data/panel-contact-combined/live-training-provenance.json`. The repeated live
sequence is integration evidence, not independent accuracy.

Use `prepare_lora.py --grouping shared` for each complete cohort. To combine
prepared train/validation JSONL, preserve each row's split and episode ID, prefix
sample IDs by cohort, and resolve the image paths against the original prepared
file before writing paths relative to the combined file. Never merge reserved
test rows into training or validation. The live hard example is appended only
to train after visual review. See the table for the exact training hyperparameters.

`train_lora.py --init-contract` records the parent's original prompt contract
when migrating shared768 to the new sealant prompt. Current serving checks are
never relaxed. Shared960 uses the already-compatible shared896 parent. The
unmerged training model can differ near boundaries from merged BF16 inference;
selection uses `evaluate_gates.py --split validation --purpose development`
against the actual host, followed by `calibrate_gates.py`.

## Host lifecycle

The WSL user service `dark-factory-panel-model.service` survives closing its
launch shell for the current WSL session, but is not installed for reboot startup.
The launcher refuses existing units or occupied ports. It accepts `DF_MODEL_PYTHON`,
`DF_BASE_MODEL` and `PYTHONPATH`; weights must already exist locally.

```powershell
wsl.exe systemctl --user status dark-factory-panel-model.service
wsl.exe journalctl --user -u dark-factory-panel-model.service -n 80 --no-pager
wsl.exe systemctl --user stop dark-factory-panel-model.service
```

Leave the adjacent physics service untouched. An unavailable or incompatible
endpoint holds the process; `-Process -NoInference` permits visual exploration
with gates held. Assets, datasets, weights and screenshots remain private and
excluded from Git; this repository publishes tutorial text and source examples.

## Rendered integration result

The selected router completed actual ProcessSmoke in **183.41 seconds**:
assembly / preassembled / assembly, three correct departures, 24 model requests,
and all six thermal acceptance audits. The 13 logged decision events have
83.46 ms median / 84.21 ms p95 HTTP latency during rendering; first consensus
batches are not separate logged decision events, so this is a sampled timing.
The physical camera audit passed 4,666 checks and the process curtain audit 27.

This repeated development sequence includes the previously reviewed missing-panel
configuration and is integration evidence, not independent model accuracy.
Private artifacts: `artifacts/sealant-contact-live-routed/` and
`artifacts/sealant-three-edge-v1/validation-routed.json`.
The main service remains ready on 18191; the comparison host and Unreal demo
were closed after verification.

## Rear roof removal regression

The `rear-roof-open-v1` body variant removes the rear roof crossmember from
process mode. Sensor pixels change, so the earlier scores do not establish
parity. A fresh 64-body capture repeats seed 27139 with the original whole-body
splits, labels, camera poses and v12 capture settings. The existing stage router
classified all **96/96 development validation batches** correctly (12 bodies),
with zero false approvals or wrong presence branches. No adapter fitting,
threshold changes or reserved-test inference were performed. This is a geometry
regression on the existing development split, not independent accuracy evidence.

Private evidence: `artifacts/rear-roof-capture64-v1/` and
`artifacts/rear-roof-open-v1/model-validation.json`. The smaller eight-body
capture is a QA-only parity fixture and must not enter training or evaluation.

Actual ProcessSmoke with the open rear roof then completed assembly / preassembled /
assembly in 183.06 seconds, with 24 model requests, three correct departures and
all six thermal acceptance checks. Live camera and curtain audits passed 4,666
and 27 checks. Evidence: `artifacts/rear-roof-live-v1/`. This remains integration
evidence with unchanged adapters and policy, not a new held-out score.

## Fixed one-batch controller

The September 18 [live consensus comparison](consensus_experiment.md) keeps
these adapters and score thresholds unchanged. Four three-body runs compare
one versus two fresh three-view batches: one averages 136.08 s versus 162.32 s,
with no wrong branches or premature thermal acceptance observed. This is repeated
integration evidence, not a new accuracy score. The user selected fixed one-batch
operation; the former consensus switch is removed. Restart model hosts to load
the updated decision policy. Trained prompts and adapter provenance are unchanged.

## IQ9 Q8 export experiment

The [September 18 EVK runtime](evk_model_runtime.md) preserves the routed adapters,
prompts, grayscale contract and confidence thresholds. Both adapted vision
encoders accompany their decoders. On the existing 96-batch development cohort,
Q8_0 text/F16 vision reproduced 96/96 on host and EVK; Q4_0 produced seven false
G decisions and is rejected by the edge router. These results do not establish
new held-out accuracy for the final scene. Use the explicit `evk_hexagon` model
source through a loopback SSH tunnel; the default remains host GPU.
