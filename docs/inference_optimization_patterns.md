# Removing unnecessary work from LLM and VLM inference

This study keeps the model's task intact and looks for implementation work that
the application does not consume. The strongest results come from understanding
the request's lifetime: which inputs are already known, which outputs are used,
and whether another token will ever be generated.

These are IQ9 development measurements, not portable speed guarantees or new
accuracy evaluations. Preserve the editable policy, full-vocabulary winner and
fresh visual evidence when applying the ideas elsewhere.

## New experiment: fuse fixed sensor preprocessing

The subsequent [streamed Reason2 experiment](streamed_reason2.md) builds on this
processor to encode each real image during camera motion and process a complete
decoder block early. It preserves the joint three-image question and requires
no adapter training. Its separate fresh-data test rejects dropping the third
NIR image. Live streaming transport is not yet enabled.

The selected Reason2 route receives three lossless 448 × 256 grayscale PNGs.
The [pinned Qwen processor](https://github.com/qualcomm/geniex-proc/blob/2ec6abcac6db8473fa26ea2f882f99652723cc7b/src/processors/qwen2vl.cpp)
uses a general sequence of resize, float conversion, normalization, channel
transpose, temporal duplication, patch rearrangement and concatenation. Those
steps create intermediate arrays even when the camera already supplies the
required dimensions.

Fusing preprocessing is an established production technique: NVIDIA DALI
combines normalization, layout conversion and casting in one operation on CPU
or GPU. Our experiment specializes that principle for the existing IQ9 input
contract and checks numerical identity; it does not use DALI or claim a new
general algorithm. [DALI operator documentation](https://docs.nvidia.com/deeplearning/dali/main-user-guide/docs/operations/nvidia.dali.fn.crop_mirror_normalize.html)

The candidate writes each decoded pixel directly into the final patch layout:

```text
[image, block_y, block_x, merge_y, merge_x, channel, time, patch_y, patch_x]
```

An 8-bit channel has only 256 possible values. We precompute a 256-entry
normalization table per channel using the same separately rounded float
operations as the reference:

```text
scaled = float(byte) * float(rescale)
centered = scaled - float(mean[channel])
normalized = centered / float(std[channel])
```

This table is 3 KiB. It replaces repeated floating-point arithmetic without
approximating it. Grayscale channels still use their own normalization tables;
the model still receives all three channels and both temporal copies. The
candidate does not cache camera content or skip vision inference.

The implementation retains the pinned STB decoder and original tokenizer. It
accepts only the fixed sensor profile and opaque grayscale 8-bit PNGs. Wrong
dimensions, colored pixels, nonopaque alpha and 16-bit input fail explicitly.
General-purpose resizing is outside this specialization. Do not compile the
normalization code with fast-math or fused multiply/subtract.

Code:

- [Direct layout and lookup tables](../scripts/model/qairt_fixed_patches.h)
- [PNG validation and original token conventions](../scripts/model/qairt_fixed_processor.h)
- [Pinned-reference comparison executable](../scripts/model/qairt_processor_probe.cpp)
- [Repeatable manifest comparison](../scripts/model/verify_qairt_processor.py)

### Measurements and current acceptance

On the IQ9, all **192 existing development batches** produce bit-for-bit
identical processed pixels, identical tokens and identical image grids. That
comparison covers **396,361,728 float elements**. The earlier prototype also passed twelve synthetic comparisons
add every possible byte value, asymmetric spatial patterns, L/LA/RGB/RGBA PNGs,
one/two/three images and a different Unicode prompt. Four invalid-input cases
are rejected; a valid request after rejection still matches.

| Isolated processing of a three-camera batch | Median |
| --- | ---: |
| Original processor, including conversion into the worker's vector | 60.001 ms |
| Direct packing and normalization lookup | 5.504 ms |
| Per-case saving | 54.509 ms |

This final-build repeat is about **10.9× faster preprocessing**, not 10.9× faster inference. The
reference/candidate order alternates between batches. These timings are from
a processor-only executable, without resident model execution in that process.

After a user reboot restored access, the final worker was rebuilt on the IQ9.
All **192 full model results** match the retained original deployment: identical
decisions, raw winning token IDs and serialized G/A/R probabilities (maximum
absolute difference zero). The result remains **119/120 primary + 72/72 weld**,
with the same conservative cool-sealant hold and zero false G. These are reused
development examples, not new held-out accuracy; the report does not serialize
every vocabulary score.

The earlier interrupted pilot report was recovered but contained only NUL
bytes and cannot supply model-score evidence. The cause of that device outage
remains unestablished. Fresh measurements use a bounded device-owned job and
an atomically replaced, flushed timing report.

The fresh inference comparison uses the **same final executable** for both
paths, with one persistent worker loaded at a time. It selects two existing
cases per stage/label: 18 primary and six weld batches. Primary block order is
prefill/fused/fused/prefill; weld uses the reverse. Each block discards one warmup,
and the second pair reverses case order. There are 96 measured requests, with
two repeats per path/input. Prompts and source PNG hashes are checked.

| Three-camera model request | Prefill-only | Plus fused processor | Median paired saving |
| --- | ---: | ---: | ---: |
| Primary, 18 inputs | 1,188.42 ms | 1,126.80 ms | 63.45 ms |
| Weld, six inputs | 1,360.92 ms | 1,298.49 ms | 62.16 ms |

The first two columns are medians of per-input two-run means; the last is the
median of paired differences, so it need not equal their subtraction. Every
input improves in this small run (minimum saving 52.39/60.36 ms). All repeated
winning IDs, decisions, G/A/R probabilities and token counts match exactly.
This is approximately another **5% off inference latency beyond prefill-only**;
it is not a combined comparison against the original callback path or evidence
of a faster complete assembly cycle. Primary subset timings differ from the
full 120-case validation because their stage proportions differ.

Actual Unreal agent integration also passes with the candidate Reason2 workers
and the original CPU Ministral controller: **177.01 s process wall time**, 171.04 s
simulation time, 24 agent choices and 12 three-camera inspections. Reason2
median/max latency is **1,298.97/1,349.07 ms**; agent median is 1,458.59 ms.
There are zero audit failures or rejected commands. All 24 one-token/history/
dispatch comparisons and 2,338 physical-camera checks pass. The preassembled
body needed one sealant reinspection after an ambiguous result.

The earlier prefill-only run took 177.56 s with the same choice/inspection
counts. This single 0.55 s difference does not establish a repeatable cycle
speedup. Fresh visual classification and the editable agent policy remain
responsible for every decision; no recipe veto or ground-truth fallback was added.

The original services on 18195/18196/18198 were restored and checked from both
the device and Windows. Candidate services/tunnel and Unreal are stopped.
The normal launcher is unchanged. The new
`--prefill-only --fused-processor` path requires its own complete validation,
bound to its executable, model bundle, decision policy and `prefill-fused-v1`
execution identity. The rebuilt worker includes the nonfinite normalization
table and explicit PNG IHDR bit-depth guards.

The final guards also pass a local native fixture using the actual pinned STB
decoder. It exercises L/LA/RGB/RGBA, multiple images, 1-bit and 16-bit rejection,
colored/nonopaque/wrong-size/malformed inputs and recovery after a partially
processed batch. Enable it with `DF_GENIEX_PROC_SOURCE` pointing at the pinned
`geniex-proc` checkout and run Python unittest discovery. This is a CPU contract
test, not a replacement for IQ9 score or rendered integration validation.

Private evidence: `artifacts/model-fused-final-20260920/processor-final.json`,
`build-identity.json`, `reason2-{primary,weld}-full.json` and
`score-comparison.json`, plus `matched-timing.json`. The earlier prototype/synthetic evidence remains under
`artifacts/model-fused-20260920/processor-bench.json`.
Rendered evidence is `reason2-live/process_runtime.json`, the three
`reason2-live-*-audit.json` files and `proxy-live/`. These artifacts stay private.

### Reproduce

Use the pinned headers/libraries in the [QAIRT guide](evk_qairt_experiment.md),
including `geniex-proc/third-party/stb/stb_image.h`. Build on the EVK:

```bash
bash scripts/model/build_qairt_score_worker.sh \
  "$GENIEX_SOURCE" "$QAIRT_PLUGIN_LIBS" "$PRIVATE/processor_probe" \
  "$PWD/scripts/model/qairt_processor_probe.cpp"

python3 scripts/model/verify_qairt_processor.py \
  --probe "$PRIVATE/processor_probe" --bundle "$PRIMARY_BUNDLE" \
  --manifest "$PRIMARY_VALIDATION/manifest.json" "$WELD_VALIDATION/manifest.json" \
  --output "$PRIVATE/processor-equivalence.json"
```

The environment must resolve the same runtime libraries used for the worker.
Then build the ordinary worker and run `probe_qairt_reason2.py run` separately
for each role with `--prefill-only --fused-processor`, preserving the existing
prompts, images and provenance. Only matching complete evidence can enable
the candidate service. Run the actual three-body agent fixture afterwards.

## Other patterns with measured evidence

| Pattern | Why it helps here | Evidence and limits |
| --- | --- | --- |
| Treat action selection as a single-token decision | Avoid generating a tool-call document and explanation | [One-token controller](single_token_controller.md); unmasked global winner, no fine-tuning |
| Prepare only the already-known next-turn prefix | Move policy/tool-envelope prefill into physical job time | [Prefix study](agent_optimization_research.md); fresh observations remain unevaluated until received |
| Keep a classification request in prefill layout | No decode follows its first score, so decode KV layout conversions are unused | [Prefill-only worker](reason2_prefill_optimization.md); 34.88 ms median saving, existing scores and rendered integration checked |
| Rewind to an identical interior prefix | Reuse the policy across bodies while evaluating the actual new suffix | [Ministral NPU study](ministral_npu.md); two arrivals 3.82 → 0.48 s, numerical differences and policy errors recorded |
| Match CPU execution to the short remaining suffix | A kernel optimized for long prefill can lose on a small append | [CPU study](ministral_cpu_tuning.md); 1.31 → 0.98 s replay, full cycle did not improve |
| Explicitly place independent workloads on different HTP devices | Avoid making a short inspection queue behind another model's long prefill | [IQ9 placement study](npu_device_placement.md); heavy-overlap Reason2 3.81 → 1.33 s, device handles verified; dependent calls still serialize |

## A plausible optimization that did not establish a gain

The final prefill chunk writes KV state that our independent, one-token
classification request immediately discards. A private candidate preserved all
earlier writes and all graph computations but omitted that final writeback.
It was slightly faster in one primary block and slower in the weld block than
a fused-processor control. The incomplete pilot does not establish a repeatable
gain, so this change is not exposed by the tracked worker.

This would be inappropriate for a conversation that continues from that cache.
Any future version needs an explicit terminal-request lifetime, reuse rejection,
failure cleanup and full numerical tests. The existing generic prefill API
promises a complete cache; silently changing that promise would be a bug.

## What to investigate next

The larger remaining opportunity is the output head: Reason2's compiled AR128
graph computes vocabulary scores for 128 rows while classification consumes
one. A supported runtime-indexed gather *before* the output projection could
retain the full vocabulary for the last valid row while avoiding unused rows.
The KV and transformer computations must remain intact, and the valid row is
not always the final padded row. This requires separate graphs and validation.
The [Ministral graph experiment](ministral_npu.md) also illustrates why deleting
unused graph outputs is not sufficient: serialization and runtime scheduling
can invalidate an otherwise attractive optimization.

Measure time to a correct useful decision, including retries, image age and
physical process completion. Faster inference can change when a cooling
inspection occurs and cause another wait; adding isolated speedups does not
prove a faster assembly cycle.
