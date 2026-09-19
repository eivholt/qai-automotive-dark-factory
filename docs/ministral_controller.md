# Ministral controller experiment — 19 September 2026

The newer [one-token controller study](single_token_controller.md) uses these
same unmodified weights with a fixed action-code protocol. Native tool calls on
18195 remain available; 18196 adds the faster protocol and omits generated
reasons. This page retains the native-controller measurements and failed speed
experiments for comparison.

The candidate is the publisher's **Ministral-3-3B-Instruct-2512 Q4_K_M GGUF**,
not a new quantization or a controller LoRA. Reason2 and its stage adapters are
unchanged. The host test uses the RTX 5090; the EVK test uses eight CPU threads
and the pinned llama.cpp runtime. Reason2 vision and text remain on the EVK NPU.

Sources: [Mistral model card](https://huggingface.co/mistralai/Ministral-3-3B-Instruct-2512),
[official GGUF](https://huggingface.co/mistralai/Ministral-3-3B-Instruct-2512-GGUF/blob/main/Ministral-3-3B-Instruct-2512-Q4_K_M.gguf),
and the user's [persistent Genie shipping tutorial](https://github.com/eivholt/qai-nemotron/blob/main/tutorial_build_agentic_shipping_iq9075.md).

Exact GGUF SHA-256:

```text
9ed150d4367e68df0ac8e1540f6ddc65b42d0ee26378329d1ecbca60f93fc5f8
```

## Decision quality and prompt

The original Qwen prompt did not transfer reliably. Initial host replay matched
8/9 assembly decisions and passed four fault probes, but requested adhesive
inspection before a missing panel was installed. Clarifying that requirement
fixed the replay. A subsequent actual host run still departed the preassembled
body without inspecting weld cooling, after an ambiguous sealant retry. A
shorter checklist prompt also missed that separate weld check in a live EVK run.
Both failed runs and the independent audit failures are retained privately.

The selected candidate prompt in `configs/live_agent.json` describes the
high-level requirements and includes an illustrative preassembled sequence with
**both independent thermal inspections**. It explicitly distinguishes a retry
from completion of another inspection. It does not contain current-body answers,
temperatures or hidden state. This is a prompt example, not a code-enforced
recipe: the executor continues to allow supported jobs and the audit records
mistakes without vetoing or repairing them.

With this example, host assembly replay matched 9/9 choices; preassembled replay
4/4; the hot-sealant, hot-weld, seating-defect and invalid-presence probes passed
4/4. Sixteen additional synthetic histories varied body IDs, numeric clocks and
an ambiguous sealant retry. All requested the still-missing weld inspection.
Reconstructed model-visible histories from both failed runs also requested the
missing check. These are development probes, not held-out accuracy or a guarantee
that every future body will be handled correctly.

After the final live run, the selected prompt passed all four fault probes and
all 16 varied departure probes on the **EVK** as well. Hot sealant/weld results
led to wait5, a seating defect to hold, and invalid presence to reinspection.
Every varied preassembled history requested its missing weld inspection. These
synthetic probes perform no jobs or Reason2 calls; their changing-prefix timings
are not the live consecutive-turn latency measurement.

Private evidence is in `artifacts/ministral-20260919`. In particular,
`host-process` and `evk-cpu-process` are failed earlier candidates; do not count
their departures as passing validation. Per-request replay reports retain raw
responses, token usage, cache/prefill/decode timings and exact config hashes.

## Measured performance

The first passing EVK run, `evk-example-process`, completed assembly / preassembled /
assembly in **261.310 seconds**, with 22 agent decisions and 11 fresh valid Reason2
batches. Agent median was **3.794 s**; Reason2 median **1.415 s**. Both native and
independent audits passed, with zero rejected calls or ground-truth fields sent
to either model. The physical-camera verifier passed 2,144 telemetry checks.
Its first request took 18.152 s after earlier replay activity, so this is **not**
a clean cold-start measurement.

A separate clean service restart at the normal port 18195 then passed the same
three-body test in **275.569 s** (`evk-final-cold-process`): 22 decisions, 11 fresh
valid vision batches, zero rejected calls/audit failures and 2,144 passing camera
checks. Median agent time was **3.952 s**, Reason2 **1.416 s**; the first decision
took **28.885 s**. Both successful runs used the same final prompt, with fresh
body IDs. This startup cost remains visible; it is not hidden by prewarming.

| Actual EVK integration | Previous Qwen 9B | Ministral 3B, clean restart |
|---|---:|---:|
| Median agent decision | 11.186 s | 3.952 s |
| First policy request | 77.712 s | 28.885 s |
| Three bodies, wall time | 484.345 s | 275.569 s |
| Median Reason2 batch | 1.415 s | 1.416 s |
| Rejected calls / audit failures | 0 / 0 | 0 / 0 |

These compare the complete selected configurations, including their different
prompts, not identical-prompt model kernels. Decisions are about 65% faster and
the full demo 43% shorter. Host GPU fixed-history requests for the selected prompt
were approximately 0.12 s, with the model already warm and Unreal closed.
These differ from simultaneous rendering or a full live host benchmark.

With the controller and both Reason2 adapters resident, Linux reported about
19.5 GiB available out of 34.4 GiB. Service accounting was about 2.9 GiB for
Ministral and 3.5 GiB for the Reason2 service; those counters do not include every
DSP/kernel allocation. Neither service reported an automatic restart.

The initial Ministral deployment passed 158 Python tests, 137 rendered native policy checks and
52 launcher checks. The actual HUD/welding capture was reviewed and shown in
chat. Unreal and the temporary host/candidate servers are closed; the selected
Ministral and Reason2 EVK services and their normal tunnels remain ready.

## Running the model

Download the official file in WSL using an environment with `hf` installed:

```bash
hf download mistralai/Ministral-3-3B-Instruct-2512-GGUF \
  Ministral-3-3B-Instruct-2512-Q4_K_M.gguf \
  --local-dir "$HOME/models/dark-factory-ministral"
bash scripts/agent/start_host.sh
```

The host endpoint is `http://127.0.0.1:18193/v1/chat/completions`. Native Mistral
tool formatting comes from the GGUF's Jinja template in llama.cpp. The existing
Unreal one-call parser, request/body epochs and transport checks are unchanged.

On the prepared EVK, place the exact file under
`~/qai-dark-factory/models/ministral3-3b/`. The launcher verifies its hash before
serving it with alias `dark-factory-agent`. Stop the existing controller before
switching models; the launcher rejects a mismatched active controller rather
than silently accepting it. Keep the Reason2 service running.

```bash
# On the EVK, in ~/qai-dark-factory/repo:
systemctl --user stop dark-factory-edge-agent
bash scripts/model/start_evk_qairt.sh --agent
# Optional Qwen fallback: stop only the controller first, then:
# DF_AGENT_KIND=qwen bash scripts/model/start_evk_qairt.sh --agent
```

The default controller is Ministral; `DF_AGENT_KIND=qwen` explicitly selects the
retained Qwen weights. This selector also works with `scripts/agent/start_host.sh`.
The old CPU-Reason2 stack script explicitly retains its historical Qwen selection.
Use the unchanged EVK endpoints/tunnels and Unreal command in
[live agent control](live_agent_control.md).

## NPU reproduction limitation

The old compiled Ministral bundle was absent from both the workstation and EVK.
A fresh isolated build used the tutorial's `qairt-dev==0.8.1`, QAIRT
`2.47.0.260601`, the exact hash above and its unmodified export helper. After
installing the SDK's dependencies and the ONNX/PyTorch imports required by the
builder, it failed before compilation:

```text
ValueError: Architecture mistral3 not supported
```

The error comes from the SDK GGUF reader's supported-architecture table. This
does not prove the model cannot run on an NPU; it means the previous tutorial's
compiled deployment could not be reproduced with this fresh toolchain. No
architecture alias, changed weights or unvalidated compiled model was deployed.
The isolated environment and failure log remain available for further work.
Do not label the CPU results as NPU performance or reuse the earlier tutorial's
NPU throughput as a factory measurement. `/opt/qairt/current` and the working
Reason2 QAIRT 2.45 runtime were not changed.

## Reproduce the departure probe

This requires a private preassembled-body fixture with its model-visible history:

```bash
python3 scripts/agent/probe_departure.py \
  --runtime artifacts/agent-speed-20260919/preassembled-probe-runtime.json \
  --config configs/live_agent.json \
  --endpoint http://127.0.0.1:18195/v1/chat/completions \
  --output artifacts/ministral-departure-probes.json
```

Run with no competing client. The probes execute no physical jobs and make no
Reason2 requests. Expected choices stay in the evaluator. For rendered
integration, use `-ProcessSmoke -ProcessControl agent -Enhancements` and run
`verify_live_agent.py` and `verify_inspection_arm.py` against the resulting report.

## Further speed experiments, without fine-tuning

Private evidence: `artifacts/ministral-speed-20260919`. The original GGUF,
eight-thread CPU runtime and both Reason2 NPU bundles remain unchanged. These
experiments do not train a controller adapter or alter inspection thresholds.

The CPU sweep tested four, six and eight threads with microbatches 128/256/512.
At 128 prompt tokens and microbatch 128, prompt throughput was 35.8 / 48.8 /
61.6 tokens/s respectively. Six and eight threads had similar empty-context
decode rates, around 18 tokens/s. This synthetic kernel test supports retaining
eight threads; it is not the longer-context live tool-call throughput.

Nine saved assembly prefixes, on otherwise idle EVK services:

| Candidate | First request after restart | Median subsequent requests | Result |
|---|---:|---:|---|
| Existing prompt/runtime | 25.447 s | 3.470 s | 9/9 choices |
| N-gram speculative decoding | 25.892 s | 3.877 s | 9/9; slower, discarded |
| Reasons requested in 3–6 words | 25.478 s | 3.318 s | 9/9 replay; failed live, discarded |

Speculation used `ngram-simple`, history key 4, draft length 8 and normal target
verification. No draft model or synthetic acceptance was used. See the pinned
[llama.cpp speculative-decoding implementation](https://github.com/ggml-org/llama.cpp/blob/0eadefebd3f8f92a86d634a0e5b8fffc9dc792c0/docs/speculative.md).

A second replay retained the model's actual preceding responses, attaching the
next saved observation only when the preceding choice matched its recorded job.
With the shorter reasons and existing Unreal argument serialization, it passed
9/9 at 3.105 s median excluding its already-warm first request. Batch/microbatch
512 passed 9/9, with a 23.164 s cold request but 3.261 s subsequent median. Q8_0
K/V cache passed 9/9 at 30.379 s cold / 3.134 s subsequent median. Neither offered
a useful overall improvement; retain batch256/microbatch128 and F16 K/V cache.

Preserving the original tool-argument key order reduced uncached prompt tokens
by about 18 per turn. However, that candidate selected `release_holder` before
the weld-cooling inspection. The replay stopped immediately, before attaching
any saved result to the wrong action. **This candidate is discarded**; Unreal
keeps its existing reason-first history serialization. A cache hit is not worth
changing decision behavior. Failed evidence remains in `cache-original.json`.

The shorter reason request preserved every process instruction and the explicit
two-thermal-gate example. It passed all four fault probes and 16 varied
preassembled histories, including ambiguous sealant retries. Nevertheless, the
actual Unreal run selected `place_panel` on a preassembled body after valid
presence G and adhesive G. This was a controller error, not a Reason2 branch
error. It invalidated the earlier adhesive observation, which the model then
failed to repeat before release. The independent audit recorded the mistakes
without repairing them. That run was stopped and is retained in `evk-process`.
**Shorter reasons are discarded too.** The original prompt/config hash remains
`c47aa7776b32f7001fced0c7b524e92fec74047b1ecf9dfd182e252864eb477a`.
These failures show why passing saved-history probes is not enough to adopt a
prompt or serialization optimization.

The executor also removes its fixed one-second delay after each tool result.
Agent inference may overlap the authored R4 withdrawal; the next command waits
for the actual parked state before dispatch. A queued reply blocks duplicate
requests, and pause/body turnover clears it with the request epoch. No motion
duration, sensor settling frame, capture TTL or model acceptance rule changes.
Native policy tests include immediate replies during partial withdrawal,
pause cancellation, body turnover and execution without recipe vetoes.

Reproduce the rejected candidate's consecutive-response cache comparison with
the retained private runtime and candidate config:

```bash
python3 scripts/agent/replay_latency.py \
  --runtime artifacts/ministral-20260919/evk-final-cold-process/process_runtime.json \
  --config artifacts/ministral-speed-20260919/short-reasons.json \
  --endpoint http://127.0.0.1:18195/v1/chat/completions \
  --continue-generated --rebuild-arguments \
  --output artifacts/agent-cache-rebuilt.json
```

Omit `--rebuild-arguments` only to reproduce the rejected original-argument
candidate. The benchmark remaps tool-result IDs to generated call IDs and stops
on a choice mismatch. Use `--config configs/live_agent.json` to benchmark the
unchanged production prompt instead. It executes no jobs and is not a substitute
for a live run.

### Selected scheduling change: actual cold-start result

After restoring the original prompt and default runtime, `evk-original-process`
passed assembly / preassembled / assembly from a clean controller restart:

| Actual EVK run | Before | After |
|---|---:|---:|
| Three bodies, wall time | 275.569 s | **253.539 s** |
| Agent median | 3.952 s | 3.991 s |
| First decision | 28.885 s | 28.586 s |
| Reason2 median | 1.416 s | 1.415 s |
| Total delay from tool result to next request | 22.094 s | 0.341 s |

The complete process is **22.031 s / 8.0% faster**. This is a scheduling gain;
there is no demonstrated inference-kernel speedup. Both runs have 22 decisions,
11 fresh three-view batches, the same required jobs/inspections and zero rejected
calls or independent audit failures. Every dispatched job recorded R4 parked;
the camera verifier passed all 2,144 telemetry checks. Sensor inputs still omit
ground truth. Model weights, prompt, adapters and thresholds are unchanged.

The selected code passed the UE 5.8 build, 160 Python tests, 142 rendered native
policy checks and 52 launcher checks. These runs are development integration,
not new held-out accuracy. Unreal and the temporary benchmark service/tunnel are
closed. The normal Ministral and Reason2 services remain ready, with about
19.5 GiB available and zero automatic service restarts.
