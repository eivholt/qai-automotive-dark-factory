# IQ9 agent optimization experiments — 19 September 2026

This study targets the editable, untrained Ministral policy. The established
baseline is one unmasked action token, 1.607 s median IQ9 CPU decisions and
198.266 s for three real Unreal bodies. Reason2 remains a separate adapted NPU
vision tool. Training images, model files and experiment logs stay private.

## Research and hypotheses

1. **Reuse computation without changing the prompt.**
   [RadixAttention/SGLang](https://www.lmsys.org/blog/2024-01-17-sglang/) reuses
   shared KV prefixes. Our single-session llama.cpp already does this between
   decisions. Explicit policy prefill and persisted cache can also reduce the
   first request after service restart. Cache identity must bind the exact model,
   runtime, template, policy and cache settings; changing a prompt must invalidate
   or truncate the cache, never reuse an old decision.
2. **Move prefill off the critical path.**
   [Sutradhara](https://arxiv.org/abs/2601.12967) overlaps tool execution with
   processing the already-known part of the next prompt. Its published A100
   results are not IQ9 predictions. Our potential adaptation is bounded cache
   preparation while a mechanical job runs, without inventing its result.
3. **Separate reversible preparation from commitment.**
   [PASTE](https://arxiv.org/abs/2603.18897) speculates tool invocations and
   withholds their results until the LLM confirms them.
   [Speculative tool-call research](https://arxiv.org/abs/2512.15834) also studies
   resident tool caches. For this scene, preparation could move an open, unpowered
   welding tool to a collision-checked waiting pose. It must not apply current,
   attach a panel or publish an inspection before actual authorization.
4. **Reduce observation tokens without changing information.** Reversible rows
   or field aliases save text, but semantic equivalence for a human does not
   imply equivalent model decisions. Actual continued-history probes are required.
5. **Accelerate prefill on the otherwise idle GPU.**
   [llama.cpp's supported build backends](https://github.com/ggml-org/llama.cpp/blob/master/docs/build.md)
   include Vulkan. This keeps the same GGUF and editable prompt and leaves the
   NPU available for Reason2. Device availability, real offload and joint model
   performance need measurement; a software Vulkan device is not GPU acceleration.

These are hypotheses, not transferable speedup claims from the papers.

## Rejected input compression

Two lossless encodings were tested on the host GPU with the same current
assembly history and actual generated prior action codes:

- Seven-column observation rows with an explicit field legend.
- Only abbreviating completion time, capture age and holder state; retaining
  `status`, `stage`, `decision` and `valid` unchanged.

Both selected holder release after seating, before the required weld-temperature
inspection. The unchanged JSON format passed all nine decisions on that history.
Each candidate stopped at its first mismatch before attaching a saved result
to the wrong action. Neither is enabled in the service. This is a measured
regression on a development replay, not a claim about every compact format.
The experimental formats remain available only through the offline probe.

## Device investigation

The IQ9 has eight Cortex-A78C CPU cores. Its current user session was not in the
render-device group. An unprivileged Vulkan probe therefore exposed only CPU
llvmpipe, while a read-only privileged probe found **Turnip Adreno 663**, Mesa
25.2.8. OpenCL reported no platforms (`-1001`). Merely finding `libOpenCL.so` or
`libvulkan.so` is insufficient evidence of usable GPU inference.

The candidate Vulkan build uses the existing pinned llama.cpp revision
`0eadefebd3f8f92a86d634a0e5b8fffc9dc792c0`, in its own build directory. Compiler
headers/tools are extracted under this project's private runtime; no system
driver or adjacent project's service is replaced. GPU test processes receive
the render group only within their dedicated systemd service.

The real Adreno device was opened and the model became ready, but its first
controller request timed out after **115 s**, without a usable action. The GPU
candidate was stopped. This particular driver/build/configuration is rejected;
it does not establish that every IQ9 GPU backend is unusable. No drivers, system
packages or existing Reason2 services were changed.

An official publisher **Q8_0** CPU export was also tested, not a re-quantization of
Q4. Its first two replay decisions took 14.41/1.22 s, but its third decision
requested sealant inspection before panel placement. Continued replay stopped
before supplying a result for the wrong tool. Q4_K_M remains selected. The Q8
SHA-256 is `8c2b72eb5861304fcfd5e82f1eddd6efa4115737f4239fd216a028a8852413ef`.
The optional `DF_AGENT_QUANT=q8_0` launcher is for offline experiments only;
the validated stack still selects the original Q4 model.

## Conditional planning was not reliable enough

A host GPU probe asked for the next action under explicitly hypothetical
G/R/A/invalid future observations. Only 17/24 matched the probe's expected sets.
Some mismatches were conservative reinspection or alternative inspection order,
so this fraction is not an unsafe-action rate or model accuracy score. Two clear
failures selected holder release without the required weld acceptance, including
one invalid weld result. No hypothetical plan or observation reached Unreal.
Precomputing a decision table and executing it later is **not enabled**.

## Implemented: persist the exact static policy KV prefix

`scripts/agent/policy_cache.py` prepares the policy at proxy startup and saves
one bounded native slot file, about 100.7 MB (96 MiB). It never saves a live
observation or a selected job. The native launcher hashes the model, binary and
shared libraries; the manifest additionally binds arguments, GGML environment,
rendered template prefix and policy. A mismatch or corrupt file recomputes the
prefix. Prompt edits require a service restart, not adapter training.

| Initial isolated IQ9 Q4 experiment | Measured time |
|---|---:|
| First decision without cached policy | 17.467 s |
| One-time policy preparation | 14.951 s |
| Save cache | 66 ms |
| First decision after preparation | 1.505 s |
| Restore cache | 25 ms |
| First decision after restore | 1.522 s |

The initial uncached CPU measurement overlapped GPU model loading for several
seconds; use the subsequent clean live run below for the deployment comparison.
This optimization removes startup policy evaluation from the process path. It
does not remove new-observation prefill from each ordinary decision. The native
server still loads the weights and verifies their hash on each service launch.

The pinned backend emits one setup token even with `n_predict=0`. The entire
preparation response is discarded, never translated into a tool call. The next
real request reuses only its exact matching prefix. The saved slot contains 945
tokens; a fresh native-process restart restored it in 21 ms, with 109 ms total
proxy cache setup including file hashing. An uncached policy took 16.83 s in that
normal-service run. No generated startup action is dispatched.

The first clean rendered cached-start run completed assembly / preassembled /
assembly in **172.815 s**, with 22 agent choices, 11 actual NPU inspection batches,
zero rejects/audit failures and all 2,144 camera checks passing. First decision
was **1.636 s**, median **1.582 s**; Reason2 median **1.415 s**. All 22 native token
winners matched dispatched jobs and unmodified actual observation histories.
The earlier 198.266 s run had 24 choices / 12 batches, so the entire 25.45 s
wall-time difference must not be attributed to caching. This is development
integration, not a new held-out reliability result.

## Implemented: prefill the known next-turn envelope while a tool executes

After sending a chosen action to Unreal, the adapter can immediately process
that **already-selected** assistant token and the next user-message envelope
ending at `Tool result: `. It stops before the first character of the unknown
result. The full actual history is still supplied on the next request; native
exact-prefix matching discards any suffix that differs. No future G/R/A,
completion status, timestamp, expected next action or scene truth is supplied.

This adapts the overlap idea in Sutradhara without conditional decision tables.
The response releases the physical job first; a separate native-slot mutex
serializes optional prefill against the next real request. At most one real
request can wait; concurrent duplicate requests still receive busy. Pause/body
turnover may leave an unused prefix, which cannot execute anything. The next
full prompt remains authoritative. Preparation errors are diagnostic only and
cannot replace a model response with a guessed action.

An IQ9 continued-history replay kept all nine choices. Median subsequent
response time fell from **1.181 s to 1.064 s** (9.9%); preparation took about
0.29 s outside the decision path. This trades slightly more total computation
for overlap, rather than making the kernels faster. The feature is exposed as
`--prefill-next-turn` and enabled in the normal EVK stack; the native tool-call
service remains unchanged.

| Actual rendered EVK run | Policy cache only | Cache + tool-time preparation |
|---|---:|---:|
| Three bodies | 172.815 s | **170.429 s** |
| Agent median | 1.582 s | **1.416 s** |
| First agent decision | 1.636 s | 1.703 s |
| Reason2 batch median | 1.415 s | 1.415 s |
| Agent choices / vision batches | 22 / 11 | 22 / 11 |
| Rejected calls / audit failures | 0 / 0 | 0 / 0 |

The latter run prepared 19 envelopes, median **266 ms**, with zero preparation
errors. Both runs chose the same jobs and passed 2,144 physical-camera checks
and full native-token/history/dispatch audits. This single matched pair suggests
**10.5% lower median agent latency** and **2.39 s** less total time; it is not a
statistical performance distribution or independent model-accuracy evaluation.
It trades extra setup calls for overlap; energy use was not measured.

The normal endpoint on 18196 now enables both optimizations with QA logging off.
The final readiness check reports a restored policy and no errors. Reason2 on
18198 is unchanged; host/Vulkan/Q8/QA candidates and the extra tunnel are stopped,
and Unreal is closed. About 16.9 GiB is available with both selected models
resident; cgroup memory excludes some shared/DSP allocations. All **178 Python
tests** pass, including cache invalidation/corruption, no invented observation,
a fast next request, stale-body history isolation and optional prefill failure.
No new Unreal geometry, state transition or mechanical motion was introduced.

## Mechanical overlap assessment

Agent inference already overlaps R4 withdrawal, with a pending reply waiting for
the actual arm to park. The welding approach itself is 1.3 s: 0.35 s folding at
the outward base angle, 0.55 s swinging inward, then 0.40 s approaching the first
spot. Squeeze/current must remain separately authorized.

The whole inward path cannot simply start during thermal inspection: it shares
R4's workspace. A future two-phase tool should expose a checked waiting pose
(`prepare_welder`) and a distinct authorized commit (`weld_panel`). Preparation
must keep jaws open, current/sparks/heat off, preserve sensor poses and retreat on
pause, abort or a different job. Clearance must cover the sweep and R4 withdrawal,
not just the final pose. The absolute approach-time opportunity in the present
three-body fixture is 2.6 s (two new panels), about 1.3% of the 198 s baseline.
Adding a separate LLM round trip just for preparation could cost more than the
1.3 s it saves. Prefer including reversible preparation in an existing parallel
high-level job, with a separate current-enable commit. No speculative physical
movement is enabled by this study.

## Fine-tuning assessment

Ordinary LoRA on the same model does not eliminate its transformer layers or
prefill work, and output is already one token. There is no measured basis for
promising a further speedup or accuracy gain from a controller adapter here.
Distilling to a smaller model or training early exits could reduce compute, but
would require a separate accuracy and prompt-change evaluation.

[LayerSkip](https://arxiv.org/abs/2404.16710) trains layer dropout and early-exit
losses and reports gains on other Llama workloads. Its self-speculative decoding
benefits do not directly translate to our one-token, prefill-dominated decisions.
Those published gains are not an estimate for Ministral on this EVK. Retraining
is not required for exact prefix reuse, GPU offload or reversible mechanical
preparation, so it is not the first choice for this task.

Private evidence root: `artifacts/agent-prefill-20260919/`.
