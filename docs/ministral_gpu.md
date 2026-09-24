# Ministral GPU experiments on IQ9

September 19, 2026 development record. The purpose is to run the editable
Ministral agent on the EVK's Adreno GPU while leaving the NPU available for
Reason2. The CPU controller remains the selected fallback; the experimental
NPU agent has been stopped during GPU tests. Reason2 remains on the NPU with
its existing adapters and sensor contract. No controller fine-tuning, model
prompt change, action mask or automatic action repair is introduced here.

**Current result: GPU execution works, but the candidate fails live process validation.**
It passes all 33 saved decision probes, then omits a required sealant inspection
on the actual preassembled body. CPU remains selected.
Private Mesa 26.1.8, its actual 64-lane subgroup restriction, a local
Q6_K signed-scale shader fix and flash attention enabled are all part of this
tested configuration. Ten selected backend-operation checks also pass. The
first uncached policy decision takes **39.328 s**; the eight subsequent cached
decisions take **1.618–3.318 s**. The actual run and its failure below supersede
any impression that these saved probes establish live process correctness.

The [compiled NPU candidate](ministral_npu.md) passed one actual three-body
integration run, but simultaneous heavy agent prefill delayed Reason2 by
roughly three times. This motivates testing separate accelerators; it does
not prove that GPU placement will improve the complete assembly cycle.

## Device and model

- IQ9 EVK, Ubuntu, Adreno 663; system Turnip/Mesa 25.2.8 and a separate
  per-process Mesa 26.1.8 candidate, on kernel `6.8.0-1077-qcom`.
- Both drivers default to subgroup size 128; the later private-driver tests
  explicitly select and report 64. The GPU has 32 KiB shared memory, FP16
  support, no accelerated integer dot product and no cooperative matrix cores.
- Original `Ministral-3-3B-Instruct-2512-Q4_K_M.gguf`, 2,138,615,808 bytes;
  SHA-256 `9ed150d4367e68df0ac8e1540f6ddc65b42d0ee26378329d1ecbca60f93fc5f8`.
- Isolated llama.cpp source revision
  `0eadefebd3f8f92a86d634a0e5b8fffc9dc792c0`, built by
  `scripts/model/build_evk_vulkan.sh`. The archive build reports
  `build_commit: unknown`; the recorded source identity is separate from that
  generated version field.
- GPU processes receive render-device access in their dedicated test service.
  Finding llvmpipe in an unprivileged probe does not establish GPU execution.

Weights, executables and experiment logs remain private. No system driver or
adjacent project's deployment is replaced.

## Results so far

### Original full-prompt failure

The earlier full-offload experiment loaded the model but produced no usable
action before a 115-second client timeout. The kernel recorded a GPU fault
and hang recovery for that exact Vulkan process shortly after dispatch,
followed by an external abort in `_a6xx_get_gmu_registers` during crash-state
collection. The original fault's cause is still unknown. It is not a measured
GPU throughput result. That experiment used context 8192, batch 256, ubatch
128 and flash attention enabled. See the
[earlier investigation](agent_optimization_research.md#device-investigation).

### Bounded tests on system Mesa 25.2.8

After a clean reboot, increasing offload while holding the input to one token
completed successfully. Increasing the prompt and batch to eight then exposed
a separate userspace crash:

| Configuration | Measured result |
| --- | --- |
| One GPU layer, one input token, batch/ubatch 1 | One measured forward pass: **105.04 ms** |
| Eight GPU layers, one input token, batch/ubatch 1 | Completed: **803.41 ms** |
| Full layer offload (`-ngl 99`), one input token, batch/ubatch 1 | Completed: **229.73 ms** |
| Full layer offload, eight input tokens, batch/ubatch 8 | **SIGSEGV** in the userspace Turnip library |
| Same eight-token test with `GGML_VK_DISABLE_F16=1` | **SIGSEGV** again |
| Same eight-token test with `IR3_SHADER_DEBUG=nopreamble` | **SIGSEGV** again |

All these tests used flash attention off, four CPU threads, no benchmark warmup
and one measured repetition. They selected actual Turnip Adreno 663. Each
successful run establishes bounded execution, including the full-offload
configuration; it does not establish agent-policy correctness, sustained
reliability or representative prompt throughput. The one-token timings must
not be used to rank configurations: initialization and shader state are not a
matched warm performance experiment. No new GPU kernel fault was recorded in
the per-test journal checks for this system-Mesa sequence.

The eight-token GDB trace has its first ten frames inside
`libvulkan_freedreno.so`, followed by `ggml_vk_create_pipeline_func`, shader
loading and matrix multiplication. This localizes the observed SIGSEGV to
userspace pipeline creation; it does not identify the underlying compiler bug,
prove valid shader inputs, or establish that the earlier kernel GPU fault had
the same cause. GDB itself exits successfully after collecting the crash, so
its service exit code must not be counted as an inference pass.

Private evidence is under `artifacts/ministral-gpu-20260919/`:
`tiny-layer1-faoff.json`, `layer-batch-probes.json`, `layers8-p1.json`,
`all-p1.json`, `all-p8.log`, `p8-gdb.txt`, `p8-nof16-report.json` and
`p8-nopreamble-report.json`, with their logs and kernel checks.

### Private Mesa 26.1.8 comparison

A private Mesa **26.1.8** cross-build completed on the host from the official
source archive, SHA-256
`b320f65874fd9653ac6c0bd1616605387344e1247411a50c797b5f3fb9dc0b55`.
The package was transferred into this project's private EVK runtime, its
archive and manifest checksums verified, and all `ldd` dependencies resolved.
Only candidate processes select this ICD; system Mesa remains unchanged. The
[private Turnip build record](private_turnip.md) contains exact build inputs,
driver/package hashes and the per-process wrapper.

| Configuration with private Mesa 26.1.8 | Observed result |
| --- | --- |
| Full offload, one input token, batch/ubatch 1, flash attention off | Completed: **1.156 s**, one unwarmed sample |
| Full offload, eight input tokens, batch/ubatch 8, flash attention off | GPU fault and hang recovery; no result before the **50 s** service limit |
| Same eight-token workload after reboot, with serialized submissions, one node per submission, async/fusion/graph optimization disabled and `TU_DEBUG=flushall` | GPU fault and hang recovery again; no result before the **35 s** service limit |

The successful one-token timing includes an unmeasured amount of cold shader
and runtime overhead. It is not a useful speed comparison with the earlier
single samples. Both failed eight-token runs reported the benchmark process
as the offending task and again hit a synchronous external abort in
`_a6xx_get_gmu_registers` during crash-state recovery. A clean reboot separated
the failed driver experiments. The serialized test combines several switches,
so it does not isolate the effect of each switch; it establishes that this
combination did not prevent the failure.

With its default subgroup width, the newer driver changes the observed
eight-token failure from a userspace SIGSEGV to a GPU execution fault. This
does not identify a common root cause with the original full-prompt
failure. Evidence: `turnip2618-p1.json`, `turnip2618-p8-report.json`,
`turnip2618-p8-kernel.txt`, `turnip2618-p8-serialized-report.json` and matching
logs under the same private experiment directory.

### Actual subgroup restriction and numerical tests

Setting Mesa's `tu_restrict_subgroup_size_64=true` changes the driver-reported
execution width to 64, as confirmed in the actual startup log. This is a
driver-supported restriction, not pretending that a 128-lane device has
different shader constants. The private Mesa 26.1.8 candidate then completed
all these bounded full-offload, flash-attention-off tests without the observed
GPU fault:

| Input tokens / batch / ubatch | Recorded unwarmed execution time |
| --- | --- |
| 8 / 8 / 8, repeated after clock synchronization | **1.2056 s** |
| 64 / 16 / 16 | **4.482 s** |
| 128 / 32 / 32 | **4.400 s** |
| 256 / 64 / 64 | **8.989 s** |

The first eight-token subgroup-64 run overlapped device clock synchronization:
its benchmark reported 31.6 seconds inside a 6.56-second service lifetime.
That timing is invalid and is excluded; the synchronized repeat is listed
above. All entries are single unwarmed samples, not a matched performance
distribution. More decisively, subsequent numerical failures make them
unusable as a model-performance claim.

Targeted `test-backend-ops` comparisons against CPU references found:

| Tested operation | Result |
| --- | --- |
| Tiny F32, F16 and Q4_K multiplication | Passed the selected cases |
| Tiny Q6_K multiplication, one/eight columns | Failed: normalized errors **7.3325 / 4.8971** |
| Model-sized Q6_K feed-forward down projection, eight/32 columns | Failed: normalized errors **4.0832 / 4.1275** |
| Model-sized Q4_K attention-query and feed-forward up projections, eight/32 columns | Passed the selected cases |
| Selected attention score/value matrix layouts | Passed the selected cases |

The Q6_K tolerance is **0.0005**, so these are large correctness failures,
not small floating-point drift. Disabling FP16, disabling compiler preambles,
and combining full synchronization controls did not repair the tested tiny
Q6_K operation. Successful Q4_K/attention samples are scoped to those exact
operations and shapes; they do not validate all remaining model operations.

Evidence is `turnip2618-p8-wave64*` for the invalid-timing first attempt,
`turnip2618-wave64-*` for later bounded execution, and `ops-wave64-*` for
numerical cases and mitigation failures. These results describe the unpatched
backend; the following comparison isolates a candidate repair.

### Q6_K scale-loading repair

The original Q4_K_M model includes Q6_K tensors. The private patch
[`llama-q6-scale32.patch`](../scripts/model/llama-q6-scale32.patch) reads each
signed scale byte through the existing packed 16-bit storage alias, then
extracts and sign-extends it explicitly in 32-bit arithmetic. It changes
neither the original GGUF nor its tensor values, quantization, tokenizer or
agent prompt. This is an inference-backend change, not model fine-tuning.

With the same private driver, subgroup restriction and host shader compiler:

| Backend | Numerical result |
| --- | --- |
| Rebuilt unmodified pinned source | Tiny Q6_K eight-column multiplication still fails: normalized error **2.5331**, tolerance **0.0005** |
| Patched pinned source | **10/10** selected operation cases pass, including tiny Q6_K, model-sized vector/feed-forward shapes, 32/64-column matrix multiplication, Q4_K and selected attention layouts |

The matched unmodified build rules out the host compiler change alone as the
explanation for these passing cases. It does not establish the exact driver
compiler defect or universal correctness across every operation and device.
Disabling FP16 or compiler preambles was insufficient in the earlier tests.

Independent source and generated-SPIR-V review confirms both Q6 aliases keep
member offsets `0/128/192/208` and a **210-byte** block stride. Exhaustive host
checking covers 131,072 signed-byte extractions; scale indices remain within
`0..15`. The repair uses 16-bit memory loads, not unaligned 32-bit block loads.
It covers the active Adreno matvec, matmul, get-rows and standalone-dequant
paths. Integer-dot MMVQ and NV cooperative-matrix-2 paths are outside its scope;
this device reports neither capability. The patch is therefore a bounded
Adreno workaround, not a validated general Vulkan Q6_K fix.

Private evidence: `ops-wave64-paired-v2-tiny-q6k-n8*` and
`ops-wave64-q6patch-v2-*`. A separate attempt routing Q6_K tensors to the CPU
returned a non-action on its first 1,021-token policy prompt after **36.08 s**;
that hybrid candidate was stopped and is not an accepted fallback.

### Actual agent histories and flash attention

The patched full-GPU server with flash attention **off** answered the first
1,021-token policy prompt correctly with action `A`, taking about **35.5 s**.
Its next, 1,081-token cached request failed with Mesa's message that a shader
with a workgroup barrier could not fit enough concurrent waves. The server
exited with status 1. This was a userspace failure, not another kernel GPU
fault; one correct first answer did not establish a usable configuration.

Flash attention **on** then passed a bounded 128-token test and the complete
continued nine-action assembly replay. All nine choices matched the expected
actions without forced labels, grammar, logit masks or repaired choices. The
first request has no prompt cache; subsequent requests reuse the real policy
and generated-action history. HTTP timings were **39.328 s** for the first
request and **1.618–3.318 s** for the eight cached requests. This small replay
does not establish a speed advantage over the CPU or performance during
simultaneous Reason2 inspection.

The subsequent saved probes pass 4/4 preassembled choices, 4/4 fault cases and
16/16 departure-check cases: **33/33 total**, including the assembly replay.
The varied fault/departure histories replace substantial cached suffixes and
take longer than ordinary appended live turns; their medians are 6.38/7.57 s.
These cases establish development agreement, not new held-out accuracy.

Evidence: `patched-assembly.json`, `patched-faoff-server.txt`,
`patched-fa128*`, `patched-fa-command.json` and the four
`patched-fa-{assembly,preassembled,faults,departure}.json` reports.

### Actual three-body integration

The rendered Unreal run completed three departures in **194.089 s**, with
21 agent choices and ten Reason2 camera batches. Median decision latency was
**3,093.15 ms** (maximum 4,269.36 ms); Reason2 median was **1,354.67 ms**
(maximum 1,432.79 ms). The next-turn envelope optimization was enabled, with
18 completed preparations and zero prefill errors. Before launching, a single
static-policy-only prime took 35.15 s client time; this startup work is excluded
from the run duration. Its one emitted token was discarded, never dispatched.

**The integration failed.** Body 2 was preassembled. The agent chose presence,
weld inspection, then departure without a sealant-temperature observation.
The independent audit recorded `missing_cool_sealant_observation`; no recipe
veto repaired the action. Both assembled bodies followed the required sequence.
There were no rejected calls or transport errors, but successful transport and
three departures do not make the process result acceptable.

All 21 raw vocabulary-winner/history/dispatch comparisons matched. The overall
token audit correctly remains failed because it includes the process verdict.
The physical-camera telemetry audit passed 1,950 checks; this is not a new
image-accuracy result. Evidence: `gpu-live/process_runtime.json`,
`gpu-live-{audit,camera-audit,token-audit}.json`, `proxy-live/`,
`gpu-live-static-prime.json` and `gpu-live-final-health.json` in the private
experiment directory. Exact-history CPU/GPU replay is recorded separately;
do not repair this failure by masking actions or adding a controller rule.

The exact model-visible histories were then submitted with `cache_prompt=false`
to the selected CPU backend and the GPU candidate. Rendered-prompt hashes match
between backends for both requests. After presence G, CPU chooses sealant
inspection D (probability 0.553), while GPU chooses weld inspection G (0.575),
reproducing its live choice with a cleared cache. Selecting weld first is not
itself the departure violation. After that weld result, **both backends choose
premature departure I**: CPU probability 0.986 and GPU 0.992, despite the missing
sealant observation. Thus the live failure cannot be attributed solely to stale
GPU cache or the repaired Q6 shader. It exposes a shared policy weakness after
a different inspection order. The CPU and GPU contexts are 8192/4096 respectively;
these replays do not isolate every source of their first-choice difference.

Private `live-failure-comparison.json` preserves both original QA request hashes,
rendered-prompt hashes, unmasked probabilities and timings. The earlier departure
probe suite tested missing weld evidence; its 16 passing cases did not establish
correctness when sealant evidence is missing instead. Future policy work needs
both inspection orders and each missing-stage case, retaining the editable prompt
and unmasked choice rather than hiding the failure behind deterministic repair.

Final service state: CPU native 18195 and agent proxy 18196 are healthy with the
policy cache restored; Reason2 NPU endpoint 18198 is ready. Experimental GPU
native 18204/proxy 18207 and their tunnels are stopped, as are the NPU-agent
candidates. Unreal is closed. The prepared GPU launcher passes `--check`;
Python discovery runs 216 tests with one skipped, and shell syntax/whitespace
checks pass. These code checks do not change the failed live acceptance verdict.

## Reproducing the isolated candidate

[`build_private_q6_vulkan.sh`](../scripts/model/build_private_q6_vulkan.sh)
cross-builds only the plugin on the prepared Ubuntu x86_64 host. It pins and
checks the original source archive, shader compiler packages and patch, keeps
source/build trees private, and installs no host or EVK system packages. Run
`bash scripts/model/build_private_q6_vulkan.sh`; set
`DF_Q6_VARIANT=baseline` to build its unmodified compiler-matched control.
The [private Turnip helper](private_turnip.md) builds the driver separately.
Verify package checksums after transfer before selecting either candidate.

Backend isolation matters: `GGML_BACKEND_PATH` adds a library **after** normal
backend discovery; it does not override an already discovered Vulkan plugin.
The tested server uses a separate `q6-server-bin` directory containing copied
pinned `llama-server`/`llama-bench` executables, the same pinned runtime
libraries, and **only the patched Vulkan plugin**. Do not merely symlink the
server executable into a new directory or add the patch beside the original
plugin and assume it is selected. Inspect the loaded-backend path in startup
logs and verify the actual `Adreno 663`, warp size `64`, and GPU layer count.

The following EVK-local command reproduces the tested server settings after
those private files have been staged. It assumes the caller has render-device
access; the experiment used a dedicated service with supplementary group
`render`. It binds only loopback and does not change the selected CPU service:

```bash
edge_root="$HOME/qai-dark-factory"
candidate_bin="$edge_root/runtime/vulkan-ops-test/q6-server-bin"
turnip_wrapper="$edge_root/runtime/private-turnip-26.1.8/package/with-turnip"
env -u GGML_BACKEND_PATH tu_restrict_subgroup_size_64=true \
  LD_LIBRARY_PATH="$candidate_bin" \
  "$turnip_wrapper" "$candidate_bin/llama-server" \
  --model "$edge_root/models/ministral3-3b/Ministral-3-3B-Instruct-2512-Q4_K_M.gguf" \
  --alias dark-factory-agent --host 127.0.0.1 --port 18204 \
  --device Vulkan0 --n-gpu-layers 99 --ctx-size 4096 --parallel 1 \
  --batch-size 64 --ubatch-size 64 --threads 4 --threads-batch 4 \
  --flash-attn on --fit off --no-warmup --no-context-shift \
  --cache-ram 0 --jinja --reasoning off -lv 4
```

This is an experimental command, not a replacement for the selected CPU
controller. Keep the original model hash above, preserve the existing Reason2
NPU service, and leave the NPU-agent candidate stopped during comparison.

The opt-in `scripts/agent/start_evk_vulkan.sh` now uses these settings and private
paths instead of the earlier failed system-driver configuration. It checks the
original model hash, requires the private runtime/driver, rejects an additional
`GGML_BACKEND_PATH`, and supports `--check` without starting inference. Override
`DF_GPU_LLAMA_BIN`/`DF_GPU_TURNIP_ROOT` only for a separately validated private
build. CPU startup remains unchanged. The native GPU endpoint is 18204; a
separate `serve_single_token.py --backend http://127.0.0.1:18204 --port 18207
--prefill-next-turn` exposes the existing editable action protocol on loopback.

## Diagnostic order and upstream evidence

1. Keep the original GGUF and start with flash attention off, short inputs,
   small batches and partial offload. Increase one dimension at a time;
   preserve the command, binary identity, result and kernel log for each run.
2. If a fault recurs, the pinned backend provides
   `GGML_VK_DISABLE_ASYNC`, `GGML_VK_DISABLE_FUSION`,
   `GGML_VK_DISABLE_GRAPH_OPTIMIZE`, `GGML_VK_SERIALIZE_SUBMISSIONS` and
   `GGML_VK_MAX_NODES_PER_SUBMIT`. Serializing individual nodes helps locate
   failures but is not a performance configuration. Separate arithmetic
   variants can disable FP16 or MMVQ through `GGML_VK_DISABLE_F16` and
   `GGML_VK_DISABLE_MMVQ`. These presence-based switches must be unset to turn
   them off; setting them to `0` still activates them. These are proposed
   diagnostics, not established remedies. See the
   [pinned Vulkan source](https://github.com/ggml-org/llama.cpp/blob/0eadefebd3f8f92a86d634a0e5b8fffc9dc792c0/ggml/src/ggml-vulkan/ggml-vulkan.cpp).
3. Do not assume cooperative-matrix or integer-dot-product switches help this
   device: both capabilities already report disabled. A documented
   [Adreno subgroup-128 matmul defect](https://github.com/ggml-org/llama.cpp/issues/25734)
   has its scoped medium/large tile correction in our pinned source. Changing
   only every shader subgroup/tile constant to 64 would not reproduce that
   correction and can break small tiles. The distinct driver-supported
   subgroup-64 restriction above prevents the observed bounded-workload fault,
   but by itself leaves the separate Q6_K numerical failure. The scale-loading
   patch addresses the tested numerical cases independently.
4. Before acceptance, compare numerical behavior and actual continued agent
   histories. A separate `GGML_VULKAN_CHECK_RESULTS=ON` build can check GPU
   operations against CPU references. An
   [upstream Turnip numerical failure](https://github.com/ggml-org/llama.cpp/issues/25906)
   on a different Adreno demonstrates why a completed request alone is
   insufficient evidence of correctness.
5. Mesa documents `TU_DEBUG=flushall` and compiler controls such as
   `IR3_SHADER_DEBUG=nofp16` or `nopreamble` for
   [hang diagnosis](https://docs.mesa3d.org/drivers/freedreno.html#gpu-hang-debugging).
   The private driver comparison above was motivated by the
   [Mesa 26.1 release notes](https://docs.mesa3d.org/relnotes/26.1.0.html)
   inclusion of a llama.cpp backend-test crash fix and IR3 compiler fixes.
   None is tied to this EVK's original fault, and upgrading the driver alone
   did not resolve the eight-token workload. Replacing system Mesa is not required
   for an isolated ICD experiment.

## Acceptance beyond tiny benchmarks

After bounded tests with the selected driver pass, the actual controller
requires the unchanged policy and observations, not a generic chat prompt.
Use the isolated private loopback server above: one slot, context 4096, flash
attention on, batch/ubatch 64, `--fit off`, `--no-warmup` and
`--no-context-shift`. Flash attention off was useful for earlier diagnosis but
failed actual continuation. The existing `scripts/agent/start_evk_vulkan.sh`
still contains the rejected original configuration (context 8192, batch
256/ubatch 128 and the original plugin); it must not be mistaken for this
tested candidate merely because it also enables flash attention.

The native `/completion` request already used by `single_token.py` generates
one token with greedy sampling, 128 unmasked pre-sampling probabilities and
no grammar, bias or LoRA. A valid response must have the same tokenizer/action
IDs, a global vocabulary winner that is an action, no truncation, and intact
policy/history provenance. Run a bounded first actual history, then
`probe_single_token.py --continue-generated` for assembly and preassembled
histories, followed by the separate fault/departure probes. Stop continued
replay at a mismatch before associating a saved observation with the wrong
tool. These remain development probes, not independent model accuracy.

Only a passing actual three-body Unreal run, with native-token/history audits
and unchanged Reason2 responses, can establish the integration result. Measure
warm decisions, body-start decisions and Reason2 separately; then compare
serial and overlapping requests. GPU and NPU still share system memory and
power, so separate execution units do not guarantee zero contention. Until
those tests pass, the CPU controller remains selected.

## OpenCL alternative

The [pinned llama.cpp OpenCL backend](https://github.com/ggml-org/llama.cpp/blob/0eadefebd3f8f92a86d634a0e5b8fffc9dc792c0/docs/backend/OPENCL.md)
supports Q4_K/Q6_K and describes recent A6xx IoT drivers as usable. Its published
Linux verification is not an IQ9075 validation. The existing EVK probe reported
no OpenCL platforms (`-1001`), so this path first needs a compatible GPU
implementation. Installing headers or an ICD loader alone cannot supply one.
The [Khronos ICD loader](https://github.com/KhronosGroup/OpenCL-ICD-Loader/blob/main/README.md)
allows a candidate vendor runtime to be registered privately through
`OCL_ICD_VENDORS` or `OCL_ICD_FILENAMES`.

Rusticl is a separate OpenCL implementation over Gallium; it is not an OpenCL
interface supplied automatically by Turnip. Enabling an available Rusticl
device still requires checking the extensions llama.cpp needs. See
[Mesa's Rusticl documentation](https://docs.mesa3d.org/rusticl.html). No OpenCL
Ministral execution or performance result is claimed here.
