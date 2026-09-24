# Streaming a joint Reason2 inspection on IQ9

This experiment moves work into the physical camera's travel intervals without
changing the final three-image question. It follows the [two-view study](two_view_npu_experiment.md).
It does not independently classify each image or vote on camera answers.

The native worker is `scripts/model/qairt_stream_worker.cpp`; its paired
driver is `scripts/model/probe_streamed_reason2.py`. It uses the existing FP16
vision/W8 text bundles and stage adapters. No new fine-tuning, image resizing,
prompt shortening, forced label or model export is involved. The live integration
uses `serve_streamed_reason2.py`, which requires complete hash-matched incremental
parity evidence. The ordinary router still rejects the streaming execution identity.

## How the work is divided

1. Begin one inspection with the exact prompt and expected image count.
2. When a real image arrives, normalize it using the previously checked fixed
   grayscale processor, then run the normal vision encoder once. Retain its
   112 main visual embeddings **and all three DeepStack feature levels**.
3. With `--incremental`, prepare the complete sequence's position tables once.
   Dimensions and text are already known; future pixel values are not invented.
   Only complete AR128 blocks whose image features have actually arrived can
   enter the decoder. One such block becomes available after the second image.
4. After the third real image, concatenate features in the original camera order,
   finish the remaining decoder blocks, and read the same complete vocabulary.
   The native greedy token must match its global maximum. Apply the existing
   .98 conditional-confidence/.5 label-mass policy to the final joint answer.

The early block remains part of the same causal sequence and KV cache. It is not
a separate user turn. Short early chunks would introduce padding and extra graph
executions; the prototype deliberately waits for a complete original block.
The native `prefillChunks` helper also executes its final output head during the
early call; that unused output is discarded. No partial answer is returned.

This design follows the pinned upstream
[Qwen3-VL encoder/model wiring](https://github.com/qualcomm/geniex-qairt-plugin/blob/f00a37cd50645456c8fbf0eaf179fa59640fd5bf/models/qwen3_vl/qwen3_vl.cpp),
[position and DeepStack providers](https://github.com/qualcomm/geniex-qairt-plugin/blob/f00a37cd50645456c8fbf0eaf179fa59640fd5bf/core/src/vlm/vlm_input_provider.cpp)
and [chunked prefill implementation](https://github.com/qualcomm/geniex-qairt-plugin/blob/f00a37cd50645456c8fbf0eaf179fa59640fd5bf/core/src/llm/llm_model.cpp).
The stock library and compiled graphs remain unchanged.

## Numerical regression

Both streamed-vision and incremental-prefill variants independently match all
192 paired baseline decisions, raw winning token IDs, prompt token counts and
serialized G/A/R probabilities exactly. They also match the retained original
device reference. Full vocabulary logits are read to calculate those values,
but the complete arrays are not saved, so this is not a claim that every logit
was compared bit for bit.

The retained score is 191/192: presence 24/24, seating 24/24, sealant 71/72 and
weld 72/72. The one error is the unchanged conservative cool-sealant hold. There
are no new false accepts or wrong presence branches in this reused development
cohort. No adapter or prompt changes are needed for streaming.

The incremental path prepares exactly 128 decoder tokens early in every tested
three-image batch. Early-block medians are 183 ms for the primary bundle and
181 ms for weld, measured during the parity experiments. Concatenated main and
DeepStack features occupy at most 11,010,048 bytes (10.5 MiB) per active batch;
this excludes decoder KV, position/text buffers and runtime-owned allocations.
The parity runs may overlap work on the other HTP and are not used for latency
claims. Separate scheduled measurements wait for that competing study to exit.

## Camera-cadence measurements

These are device replays of real PNGs at the recorded camera arrival cadence,
timed from the first image becoming available. Each stage has one excluded
warmup and five measured pairs with alternating order. The baseline already
uses the fused processor and prefill-only scorer. Model loading is excluded;
the original production services stay resident and idle.

| Stage | Baseline | Encode + early prefill | Median paired saving |
| --- | ---: | ---: | ---: |
| Presence | 3,078 ms | 2,628 ms | 451 ms |
| Sealant | 3,261 ms | 2,813 ms | 448 ms |
| Seating | 3,066 ms | 2,622 ms | 448 ms |
| Weld | 3,249 ms | 2,803 ms | 446 ms |

Image encoding alone saves 264–269 ms in separate matched runs; adding one
early decoder block brings the saving to 446–451 ms, approximately 14% of the
measured interval. Every measured timing pair retains identical reported G/A/R
probabilities. This is overlap, not a reduction in total model computation.
The first two encodes, initial reset and early decoder block move off the final
image's critical path. Only one HTP is needed for the measured inspection.

These numbers exclude initial arm deployment, real HTTP uploads, Unreal
integration and the next agent decision. They are not a measured whole-cycle
speedup: earlier thermal inspections can also change how many cooling retries
the agent chooses. The first exploratory schedule files overlapped the separate
HTP1 parity run; they are retained privately but excluded from this table. The
reported files are `incremental-schedule-*` and `vision-schedule-*`.

The native worker lifecycle fixture passes **24 checks per variant**: incomplete
and duplicate inputs, order/identity errors, cancellation after a prepared prefix,
replacement epochs, image decode failure, unexpected fields, eight-second expiry,
consumed batches and exact fresh-score recovery. Python discovery passes 249
tests with two optional SDK skips; native policy smoke passes 151 checks.
These initial offline checks preceded the live integration described below.

## State and cancellation boundaries

The stdin experiment admits one batch per worker, at most three images, strict
ascending indices, and an eight-second local lifetime. A new begin replaces the
previous batch. Cancellation, malformed input, failed image decoding, identity
mismatch, expiration and completion clear visual/position/embedding/KV state.
Incomplete batches cannot score. Expiry is checked on access and again before
publishing the result; retained allocations remain bounded while idle.

The live transport binds prepared features to the exact body, request ID, session,
epoch, stage, prompt hash, capture time and PNG hashes of the unchanged final
request. Pause, backend switch and body turnover invalidate pending preparations
as well as replies. A partial acknowledgment cannot release a process gate.

## Live transport and deployment

Unreal asks the selected EVK server's small `/v1/stream/capabilities` endpoint
whether it supports `ordered-three-view-prefill-v1`. After each actual sensor PNG
is encoded, it posts that image to `/v1/stream/prepare` while R4 continues moving.
Uploads are ordered, with one preparation request in flight. After all three
acknowledgments, `/v1/inspect` receives the unchanged original three-image request,
plus session/epoch HTTP headers. The server verifies byte-identical PNG hashes
before completing one joint classification. It never votes on individual views.

Preparation metadata is transport state; the model receives only its original
prompt and pixels. Display previews still sample the GPU textures, and the three
settling renders at each physical lens pose remain unchanged. Host GPU and older
EVK servers retain the existing final-batch path. A streaming error holds the
current gate; it does not silently retry that partial batch using another path.

There is one active prepared batch across both resident stage bundles, a serial
native worker per bundle, bounded HTTP readers, 16 recent sessions and 64 retired
epochs. Preparation and final results expire after eight seconds. Client age starts
before the first camera image; server age starts at the first preparation, and both
must pass. Cancellation tombstones also reject an upload that arrives after its
cancel. A delayed old cancellation cannot clear a newer batch. The native historical
execution name `offline-stream-prefill-v1` stays pinned to its validated binary;
the separately versioned HTTP protocol is the live boundary.

Install the validated private executable as `runtime/qairt_stream_worker` and
the two complete **original-device/config** parity reports as
`models/qairt-{primary,weld}/streaming-validation.json`. Keep the original worker
and validation reports for rollback. `start_evk_qairt.sh` automatically selects
streaming when its private deployment files exist; incomplete or mismatched
deployments fail rather than falling back silently. No boot autostart is installed.

```bash
# Prepared EVK. Stop the previous automotive service before changing its mode.
systemctl --user stop dark-factory-edge-qairt
bash ~/qai-dark-factory/repo/scripts/model/start_evk_qairt.sh --streaming --agent
curl http://127.0.0.1:18198/v1/stream/capabilities
```

```powershell
# With the usual EVK tunnel running; streaming is negotiated automatically.
.\unreal\Scripts\run.ps1 -Process -Enhancements -ProcessControl agent `
  -ProcessBackendSwitch
```

`-NoProcessStreaming` selects the full-batch path for comparisons. To restore the
original native worker, stop the service and start it with `--baseline --agent`.
Do not load two complete copies of both stage bundles on HTP0 simultaneously:
the candidate startup encountered FastRPC mapping/PD limits despite free host RAM.
Stopping the old automotive workers allowed the validated streaming pair to load.

Reproduce HTTP lifecycle checks with `scripts/model/probe_live_streaming.py`
(`--endpoint`, a private `--request` from ProcessSmoke, and a private `--output`).
Run `-ProcessStreamingSmoke -ProcessModelSource evk_hexagon` for actual flow with
a pause after the first prepared image and an EVK/GPU/EVK switch after early
prefill. This switch fixture cancels real work; it does not submit a GPU inspection.
Use separate normal `-ProcessSmoke -ProcessControl agent` for actual agent choices.
Audit with `scripts/verify_streamed_inspection.py` and the existing camera/agent
verifiers. The streaming audit requires real early preparation to finish before
the next camera capture and verifies all completed requests used three images.

## Live results, 22 September 2026

Before deployment, the final native binary repeated all 192 paired comparisons
on the original HTP0 bundles/configs: unchanged decisions, raw winners, prompt
counts and serialized G/A/R probabilities, retaining 191/192 development accuracy.
These hash-matched reports are required at service startup.

Actual Unreal flow passed assembly/preassembled/assembly departures, all six
thermal acceptance audits and **16/16** completed streamed inspections. Two extra
partial batches were deliberately canceled by pause and backend switching; neither
produced an accepted result. Camera audit: 3,114 checks. The same streaming server
then ran on the ordinary **18198** endpoint for the actual agent test.

The EVK CPU Ministral agent completed three departures in **179.87 seconds**, with
**26 choices and 13/13 valid streamed inspections**, zero independent audit failures
and no rejected tool calls. Two thermal A results caused model-selected waits and
fresh inspections. Agent latency was 1,481 ms median. Camera audit: 2,532 checks.
Every completed inspection in both runs prepared image one before capture two
and image two/early prefill before capture three.

| Agent-run stage | Batches | Final HTTP median | First PNG to answer median | Scan start to answer median |
| --- | ---: | ---: | ---: | ---: |
| Presence | 3 | 591 ms | 2,601 ms | 3,308 ms |
| Sealant | 4 | 763 ms | 2,767 ms | 3,482 ms |
| Seating | 2 | 591 ms | 2,606 ms | 3,314 ms |
| Weld | 4 | 756 ms | 2,749 ms | 3,465 ms |

The final HTTP column excludes the earlier image preparation; it is not total
inference cost or total gate delay. The scan-start column includes physical camera
movement and settling. No matched whole-cycle speedup is claimed: cooling retries
and agent waits vary. These are development integration checks, not new held-out
accuracy or real-camera validation. All three images, prompts, .98/.5 thresholds,
one-batch requirement and freshness rules remain unchanged.

The live HTTP probe also verifies exact baseline/streamed score and image parity,
no acceptance from preparation, consumed-epoch rejection, cancel-before-upload,
cancellation after prefill, late old cancellation, altered/missing images and
incomplete headers. Evidence and captured screenshots remain private under
`artifacts/live-streaming-20260922/`.

## Fresh two-view NIR check

Unreal rendered 32 new randomized episodes with seed 220922 and the current
geometry/materials (`-ProcessCapture -ProcessEpisodes 32 -Enhancements`). The
camera audit passes 63,893 checks. Only the first 26 whole episodes' train and
development rows entered this experiment; the six reserved test episodes were
not classified. Both conditions used the same adapted primary model, thresholds
and pixels, with execution order alternated for each pair.

| NIR stage | Original three images | Joint spot1 + spot2 |
| --- | ---: | ---: |
| Panel presence | 26/26 | 17/26 |
| Panel seating | 26/26 | 19/26 |
| Total | **52/52** | **36/52** |

All nine installed-panel presence cases regress: five wrong missing-panel
branches and four holds. All seven correctly seated cases regress: one R and
six holds. There are no false G results in this sample, but wrong presence R
would still choose the wrong assembly branch. The two-view candidate is rejected;
no live camera motion, prompts or adapter compatibility rules are changed.

Example `episode-0010-case-0`: three images give G probability .9999897401;
the first two give R probability .9926523053. Both label masses are sufficient,
so the wrong R is not rescued by the existing .98 confidence threshold. This
fresh development check overturns the earlier 48/48 two-view result on retained
images. It does not identify whether altered view context, prompt distribution
or their interaction caused the failure, and does not establish that every
possible camera pair fails. No held-out or real-camera accuracy claim is made.

## Reproduce

Build on the EVK with the same pinned headers/libraries as the regular scorer,
passing `scripts/model/qairt_stream_worker.cpp` as the optional fourth argument
to `scripts/model/build_qairt_score_worker.sh`. Use a separate output executable.

Run the paired driver with `parity`, `schedule`, `lifecycle` or `nir-pair`, supplying
`--worker`, `--bundle`, `--qairt-libs`, `--manifest` and a new private `--output`.
Set the existing QAIRT library/DSP environment as described in
[the compiled runtime guide](evk_qairt_experiment.md). Add `--incremental` to
test early decoder work. Without it, only image encoding is streamed.

`parity` alternates baseline/candidate execution order. `schedule` releases real
images at the previously measured 0.963 s NIR/weld and 0.966 s sealant intervals,
alternates execution order and records one warmup plus five measured pairs per
stage. `lifecycle` tests rejection and fresh-score recovery after invalidation.
`nir-pair` is a separate altered-prompt study using spot1/spot2; it refuses thermal
stages and does not produce deployment adapter provenance. Reserved test rows
are rejected by this development driver.

Private source snapshots, native binaries, raw reports and the fresh rendered
capture are under `artifacts/streamed-reason2-20260922/`. They stay outside Git.
