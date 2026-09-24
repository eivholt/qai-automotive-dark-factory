# IQ9 cached-token CPU experiments — 19 September 2026

These experiments retain the original Ministral Q4_K_M weights, editable policy,
complete recorded observations and unmasked one-token global winner. They test
execution settings and CPU kernels, without training, action masks or a process
veto. Reason2 remains resident on NPU. The normal CPU services remain separate
from the temporary loopback `18208` benchmark server.

## Method

The workload contains 21 exact model-visible histories from the later GPU live
run, including its weld-first, missing-sealant counterexample. Each measurement
prepares only the static policy or the already-selected action and next tool
message envelope. The last actual result enters only the timed request. Prompt
render hashes, token counts, probabilities, temperatures and CPU clocks are
recorded privately. Preparation time is recorded separately, not omitted from
the evidence. These are fixed-history comparisons, not continued conversations
or new model-accuracy measurements.

Only one candidate performs inference at a time. All use context 8192, one slot,
the same native server/template, and the original GGUF. The two existing CPU
services and Reason2 stay resident but idle. No CPU governor or system driver
changes are made. The initial matrix uses 21 requests per configuration, with
an eight-thread baseline repeated after the other configurations.

| Original CPU library | Median timed request |
| --- | ---: |
| Eight threads, batch 256 / microbatch 128 | 1,316 ms |
| Six threads for both prefill and decode | 1,561 ms |
| Four threads for both prefill and decode | 2,079 ms |
| Eight threads, microbatch 64 | 1,322 ms |
| Eight threads, microbatch 256 | 1,326 ms |
| Eight-thread baseline repeated | 1,305 ms |

All six configurations select the same 21 tokens on these histories. This
includes the incorrect departure on request 12: agreement is not correctness.
The selected settings already beat the smaller thread counts; changing the
microbatch limit brings no useful gain. Actual new suffixes contain 23–78 tokens,
and template rendering takes about 4 ms, so JSON/template overhead is not the
dominant cost.

| CPU implementation comparison | Median timed request |
| --- | ---: |
| Original library, flash attention off | 1,176 ms |
| Original library, flash attention off repeated | 1,100 ms |
| Compiler-matched OpenMP control | 1,333 ms |
| Native worker pool, OpenMP disabled | 1,133 ms |
| OpenMP with 16-row flash-attention tile | 1,097 ms |
| Native worker pool plus flash attention off | **984 ms** |

These candidates also retain all 21 baseline action winners. The combined
candidate is about 25% faster than the original eight-thread baseline on this
replay. It uses the existing non-flash attention implementation and no custom
attention patch. Each row is one 21-request sweep unless labeled repeated;
these are not randomized trials or confidence intervals. Preparation/startup,
physical motion and Reason2 are outside the timed request median.

## Source-guided candidates

The [pinned llama.cpp CPU implementation](https://github.com/ggml-org/llama.cpp/blob/0eadefebd3f8f92a86d634a0e5b8fffc9dc792c0/ggml/src/ggml-cpu/ops.cpp#L9317-L9341)
uses tiled flash attention only when there are at least 64 query rows. Most of
these appends take its fallback path. CPU flash attention off and a private
16-row tile are therefore relevant comparisons. Increasing a configured batch
limit does not make a 53-token suffix contain 64 query rows.

The tile candidate changes the shared query-tile constant in both the scratch
planner and kernel. It keeps all keys, values and masks. However, it changes
floating-point execution: the tiled path retains FP32 query/accumulator work
where the F16 fallback uses lower-precision intermediate work. It cannot be
described as bit-identical execution. Smaller tiles also repeat key/value
packing more often, so they need measurement rather than an assumed speedup.

The original binary uses OpenMP. Its [OpenMP branch](https://github.com/ggml-org/llama.cpp/blob/0eadefebd3f8f92a86d634a0e5b8fffc9dc792c0/ggml/src/ggml-cpu/ggml-cpu.c#L3419-L3453)
does not use the custom worker polling loop; changing `--poll` alone would not
test that loop. A private OpenMP-disabled CPU library and a compiler-matched
OpenMP control isolate this alternative. GCC also documents separate
[OpenMP spin controls](https://gcc.gnu.org/onlinedocs/libgomp/GOMP_005fSPINCOUNT.html).
DOTPROD repacking for K-quants is already enabled; this A78C has no I8MM.

Only the CPU library is replaced in copied private runtime directories; the
original server and other libraries remain byte-identical. Models and the
normal deployed runtime are not rewritten. Builds use pristine source pin
`0eadefebd3f8f92a86d634a0e5b8fffc9dc792c0`, GCC 13.3 and the Cortex-A78C feature
set without I8MM/SVE. Build manifests and binary hashes remain private.
The first linked-library packages could not register with the original dynamic
loader and never ran inference. The corrected packages export
`ggml_backend_init`; `build_private_cpu.sh` now checks that entry point.

## Reproduction and policy checks

Build the optional plugin on the x86 Ubuntu host:

```bash
DF_CPU_VARIANT=noomp bash scripts/model/build_private_cpu.sh
```

Copy the original pinned runtime into a separate private directory and replace
only its `libggml-cpu.so` with the generated dynamic plugin. The helper also
supports `omp-control` and `tile16`; the fastest measured combination uses
`noomp`, with no tile patch. Launch that isolated copy on the EVK with:

```bash
DF_EDGE_LLAMA_BIN=/private/copied-runtime/bin \
DF_AGENT_PORT=18208 DF_AGENT_CACHE_DIR=/private/tuned-policy-cache \
bash scripts/agent/start_evk.sh --flash-attn off
```

Use a separate one-token proxy/cache directory for a process experiment. The
normal launcher still selects the original runtime. Do not overwrite it or
reuse a cache manifest bound to a different binary or attention configuration.

`scripts/agent/benchmark_cached_suffix.py` is the reusable form of the original
private benchmark driver. It checks saved prompt provenance, discards every
preparation response, records hashes/tokens/timings, and never dispatches a tool:

```bash
python scripts/agent/benchmark_cached_suffix.py \
  --logs /private/single-token-qa-logs \
  --endpoint http://127.0.0.1:18208 \
  --output /private/cached-suffix-report.json --passes 1
```

Use an otherwise idle native endpoint. This changes its prompt cache. Do not
point it at a live process controller while Unreal is making requests.

`probe_single_token.py --suite thermal-order` adds a separate 17-case synthetic
suite: each missing thermal stage, both completed orders, ambiguous/invalid/
stale/hot results, and recovery. Expected choices remain outside model input.
Unlike the old broad fault probes, it checks the exact requested stage. The
historical 16 departure probes and previous 33-probe denominator are unchanged.
The observed live missing-sealant history is also retained separately; synthetic
cases do not replace that counterexample.

The original and combined CPU candidate each pass **14/17** new thermal-order
cases. Both fail missing sealant after weld G and stale evidence followed by a
valid result from the other stage. One accepted response changes from a short
wait to reinspection; these probes permit both. Thus the optimization preserves
this suite's acceptance result, but the unchanged agent policy still needs work.

## Actual rendered process run

The combined candidate completed assembly / preassembled / assembly in
**182.618 s**, with 28 agent choices, 14 actual Reason2 batches, zero rejected
calls and zero independent audit failures. Agent median was **1,036 ms**
(maximum 1,387 ms); Reason2 median was **1,416 ms**. All 28 unmasked-token /
actual-history / dispatched-job comparisons and all 2,726 physical-camera
checks pass. The static policy cache and known-envelope preparation remained
enabled, with no speculative observations or modified prompts.

The earlier selected CPU run measured 1,416 ms agent latency and 170.429 s
overall, but made only 22 choices and 11 camera batches. The faster candidate
needed three extra cooling inspections and waits; it did not improve total
cycle time. This is one development integration comparison with different
decision histories, not an isolated 27% end-to-end acceleration claim.

The candidate remains opt-in. The original CPU 18195/18196 services and Reason2
18198 are healthy, the original policy cache is restored, candidate servers and
tunnel are stopped, and Unreal is closed. The reusable benchmark also completed
one real diagnostic-log request successfully after the run. Python discovery
passes 231 tests with one optional fixture skipped; explicitly enabling the
native SDK fixture passes its 16 protocol tests. Build helper clean/incremental
checks and shell syntax pass. No new held-out model accuracy is claimed.

Private evidence: `artifacts/ministral-cpu-tuning-20260919/` and the corresponding
EVK experiment directory. No new held-out accuracy is claimed.

## Native-pool follow-up — 20 September 2026

The next sweep retained the same 21 recorded histories, original weights,
eight threads, batch 256 / microbatch 128 and known-input preparation. It tested
native-pool scheduling after the OpenMP-disabled build had made those controls
relevant. Only one candidate ran inference at a time.

| Native-pool configuration | Median timed request |
| --- | ---: |
| Flash attention off, default scheduling | 984.215 ms |
| Flash attention off, strict worker affinity | 986.370 ms |
| Flash attention off, polling disabled | 988.258 ms |
| Flash attention off, polling level 100 | 986.978 ms |
| Flash attention off, default scheduling repeated | 987.500 ms |
| Flash attention on, 16-row query tile | 1,027.263 ms |

Strict placement used `--cpu-range 0-7 --cpu-strict 1` and the matching batch
arguments. Observed per-thread affinity confirms individual worker placement
on CPUs 0 through 7; other runs allowed workers on the full 0–7 set. Thus this
was an applied placement change, not merely an accepted CLI argument. Polling
tests set both `--poll` and `--poll-batch` to 0 or 100, separately from affinity.
The five flash-attention-off measurements differ by less than 0.5%; none
establishes a useful gain over the existing candidate. Idle power was not
measured.

The final row completes the native-pool / attention-tile combination missing
from the earlier sweep. It is about 4% slower than native-pool flash attention
off on this workload. All six runs preserve all 21 rendered-prompt hashes and
CPU action winners, including the known incorrect departure without a sealant
inspection. These are fixed-history execution comparisons, not new accuracy
or full-cycle results. Keep the earlier no-OpenMP / flash-attention-off build
as the CPU tuning candidate; normal deployment remains unchanged.

`build_private_cpu.sh` now supports `DF_CPU_VARIANT=noomp-tile16`. The measured
private build used CPU plugin SHA-256
`ba3b9854aa3ae42c7b010ac52e1bdbabb11ed92d045ad855c89abc5dac582e16`
and archive SHA-256
`27d08c07c46194002b9c27e659f19bc257bf2ed9d99adc86679f17e7b7fbd6c1`.
The subsequent clean build through the generalized helper produced plugin
`3657b4f00de89aab2fd4d42601b20b797b006743a027deabe84fa14dcaf5feca`
and archive
`b5e3e5b16395c31bcf2347211e49a275b5774ba95dc5dc77dca3a5cf28e15ca1`.
Build paths and recorded provenance differ; the measured timing belongs to the
first binary, not to the later reproducibility build.

Private reports and affinity/temperature snapshots are under
`artifacts/model-speed-20260920/`: `poolbase.json`, `strict.json`,
`poolpoll0.json`, `poolpoll100.json`, `poolrepeat.json` and `noomptile16.json`.
