# Reason2 prefill-only scoring on IQ9 — 20 September 2026

The opt-in `qairt_score_worker --prefill-only` path removes unused preparation
for autoregressive decoding. The normal worker pipeline remains the default.
There is no new adapter, prompt change, action mask or lower-resolution input.

A subsequent [fixed-processor experiment](inference_optimization_patterns.md)
builds on this path but has a separate execution identity and incomplete live
validation. The measurements below validate prefill-only, not that newer option.

The selected model still consumes one request containing three lossless
448 × 256 grayscale images and the original stage prompt. The earlier
single-image experiment remains offline; its changed prompts did not retain
the three-image accuracy. This optimization preserves the selected route.

## What changes

The [pinned GenieX implementation](https://github.com/qualcomm/geniex-qairt-plugin/blob/f00a37cd50645456c8fbf0eaf179fa59640fd5bf/core/src/llm/llm_model.cpp#L728-L817)
runs chunked prefill, converts every layer's
KV cache to decode layout, samples the first token, then converts the cache
back when our callback stops generation. No decode is executed. The candidate
uses the same processor, embeddings, vision encoder, DeepStack, MRoPE providers,
chunked prefill and native global sampler, but leaves KV in prefill layout.
Every request begins with a fresh cache. Exceptions clear embedding/position
state and reset KV; there is no reuse across different images.

The worker independently reads all 151,936 logits and computes the same global
winner and G/A/R probabilities. Its native sampled token must equal that global
winner, including EOS. No label-only scoring or forced output is introduced.
Timing fields separate reset, processing, vision, transformer prefill, sampling
and score calculation; `worker_with_reset_ms` includes reset.

## Device measurements

Twenty-four paired development batches used the same newly built worker in
ordinary and prefill-only modes, with one temporary worker resident at a time.
Both normal stage models stayed resident but idle. An initial attempt to keep
two additional workers loaded exceeded available DSP sessions before inference;
the benchmark was changed to sequential loading. This was not a model result.

| Stage, six batches each | Ordinary worker | Prefill-only |
| --- | ---: | ---: |
| Panel presence | 1,204 ms | 1,169 ms |
| Sealant readiness | 1,383 ms | 1,347 ms |
| Panel seating | 1,206 ms | 1,172 ms |
| Weld readiness | 1,376 ms | 1,340 ms |

Median per-case saving is **34.88 ms**, approximately 2.5–3% depending on stage.
All 24 winners, decisions and serialized G/A/R probabilities are identical.
These are sequential timing blocks, not randomized trials or an energy study.
The pilot selected fixed strides and is not a balanced accuracy sample.

The complete existing development set was then rerun with the candidate:

- Primary: **119/120**, zero false G; one unchanged conservative sealant hold.
- Weld: **72/72**, zero false G.
- All **192** raw winners, gate decisions and G/A/R probabilities exactly match
  the retained device validation. Other vocabulary probabilities are not
  serialized, so this is not proof that every logit is bitwise identical.
- Median candidate batch times: primary **1,362.06 ms**, weld **1,344.12 ms**.
  Those medians have a different stage mix from the 24-case pilot.

This is reused development data, not new held-out or real-camera accuracy.
Physical cooling and repeated inspections can dominate overall process time;
a small inference saving does not establish a faster assembly cycle.

The rendered `ProcessSmoke -ProcessControl agent -Enhancements` also passed:
three departures in assembly/preassembled/assembly order, **24** agent choices,
**12** actual inspection batches, zero rejected calls and zero audit failures.
All 24 raw-token/history/dispatch comparisons and **2,338** camera checks pass.
Reason2 replies measured **1,357.65 ms median / 1,399.12 ms maximum**; the unchanged
original CPU agent measured 1,464.87 ms median. The run took **177.56 s** including
startup (171.70 s simulation clock), compared with 170.43 s for the earlier
22-choice/11-batch CPU run. One extra sealant retry and wait prevents a claim
of overall cycle acceleration. These are different live timing histories.
The candidate remains opt-in; original services were restored and Unreal closed.

## Reproduce and select explicitly

Build the worker with `scripts/model/build_qairt_score_worker.sh` against the
same pinned GenieX headers and libraries described in
[the compiled runtime guide](evk_qairt_experiment.md). Keep its output separate
from the deployed executable. Run both routed datasets with
`probe_qairt_reason2.py run --prefill-only` and new report paths, retaining all
usual bundle/provenance arguments.

`serve_qairt_reason2.py --prefill-only` requires complete development evidence
bound to that exact worker, bundle, decision policy **and execution path**.
Ordinary worker evidence cannot authorize this path, or vice versa. Each native
reply must also report the selected path. The ordinary launcher is unchanged.

Private evidence lives in `artifacts/model-speed-20260920/`:
`reason2-pilot.json`, `reason2-primary-full.json`, `reason2-weld-full.json` and
`reason2-comparison.json`, plus `reason2-live/` and its three audit reports.
Generated model packages and images stay private.

## Next source-guided opportunity

The transformer prefill dominates the measured CPU-copy saving. In the primary
pilot, typical subdivisions were about 85 ms preprocessing, 349 ms vision,
708 ms transformer prefill and 23 ms reset; final full-vocabulary scoring was
about 3 ms. Thermal prompts require more prefill work.

The retained Reason2 export computes an AR128 output head for all 128 rows,
although only one valid row is consumed. A future graph could gather the last
valid hidden-state row *before* the full-vocabulary head, retaining all decoder
and KV rows. The index must be runtime `current_chunk_length - 1`: final chunks
are padded, so hardcoding row 127 is incorrect. This needs a supported dynamic
Gather, a correctly registered input provider, a row-zero scorer, separate
bundle compilation and full numerical/model validation. It is not implemented
or a measured speed gain in this experiment.
