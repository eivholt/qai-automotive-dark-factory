# Separate IQ9 HTP devices for the agent and inspector

September 21, 2026 experiment. The IQ9 EVK exposes two independently addressable
HTP devices. Keeping Reason2 on device 0 and placing compiled Ministral on device
1 reduces the contention observed when both models use device 0. This is an
explicit candidate, not a change to the normal CPU-agent deployment.

The [pipeline follow-up](dual_htp_pipeline.md) explains residency versus execution,
analyzes the actual motion/inference timeline, and measures early policy-prefix
preparation on device 1. Typical warm turns remain limited by the compiled
64-token input chunk; startup preparation helps the first decision.

## Establish placement before measuring

The installed QAIRT 2.50.40 runtime's `deviceGetPlatformInfo` returns devices 0
and 1, each with core 0, SoC model 77 and architecture 73. Therefore select
`devices[].device_id`, not `core_id=1` or `graphs[].num_cores=2`. Changing graph
core counts is a different export/runtime experiment.

The read-only probe uses matching SDK headers and the loaded runtime:

```bash
g++ -O2 -std=c++17 -Wall -Wextra -Werror \
  -I"$QAIRT_SDK_ROOT/include/QNN" scripts/model/probe_qnn_devices.cpp \
  -ldl -o /private/probe_qnn_devices
LD_LIBRARY_PATH="$QAIRT_SDK_ROOT/lib/aarch64-oe-linux-gcc11.2" \
  /private/probe_qnn_devices \
  "$QAIRT_SDK_ROOT/lib/aarch64-oe-linux-gcc11.2/libQnnHtp.so" \
  > /private/platform.json
python3 scripts/agent/prepare_genie_device.py \
  --bundle-config /private/ministral-qairt250-yarn4096/genie_config.json \
  --platform-info /private/platform.json --device-id 1 \
  --output /private/ministral-device1
```

Preparation retains graph settings, sampler, context length and original model
files; it changes only the requested device and absolute file references. It
rejects unavailable devices/cores, architecture/SoC mismatches and existing
output directories. The private manifest hashes source and generated configs.
Use its `genie_config.json` as `serve_genie_backend.py --bundle-config`, with
the existing tokenizer, runtime, template properties and worker described in
[the compiled NPU experiment](ministral_npu.md). Keep private SDK/model files
out of Git.

Configuration alone does not prove placement. Actual worker file descriptors
show both Reason2 workers using `/dev/fastrpc-cdsp-secure`; baseline Ministral
also uses that device. After relocation, Ministral instead holds
`/dev/fastrpc-cdsp1-secure`. Loaded-library mappings pin each runtime. These
observations establish distinct device handles, not per-kernel utilization or
complete memory/power isolation.

This follows the installed SDK's *Control which HTP device executes model*
documentation (`QNN/general/htp/htp_auto_single_nsp.html`). Qualcomm also describes
distributing workloads over the IQ9075's two HTP resources in its
[FactoryPulse implementation](https://www.qualcomm.com/developer/blog/2026/07/genai-rag-iq9075-factorypulse).
The actual runtime inventory and device handles establish availability on this
EVK rather than relying solely on the example's hardware assumptions.

## Measurement design

Reason2 stays on the original deployed QAIRT 2.45 worker, with shared960 primary
and shared896 weld adapters. This experiment does not combine the separate
prefill-only/fused preprocessing candidate. Ministral uses the existing compiled
QAIRT 2.50.40 model and guarded `append_rewind` worker, unchanged prompts and one
fresh full-vocabulary next-token decision. There is no fine-tuning or action
mask. Runtime health records pin contexts, tokenizer, worker and loaded libraries.

The same saved weld request and actual preceding agent history are used for
both placements. An untimed prime must produce the original seating-inspection
choice before the saved result can be appended. Warm trials submit only the
60-token suffix while retaining 1,231 tokens. Their full-history preparation
cost is excluded: they measure an already warm conversation, not cold throughput.
Five trials per mode follow a separate warmup.

The full-prefill comparison repeats the identical 1,291-token request. The
worker's identical-prompt guard requires reset; every measured call must confirm
`cache_mode=reset`, zero retained tokens and full input submission. Three trials
per mode follow a separate warmup. Reported values are client HTTP medians.

Concurrent requests are independent saved-input stress tests. In the actual
agent loop, an inspection must finish before its result can inform the next
agent decision. No speculative result, physical action or extra acceptance rule
is introduced. The placement blocks run device 0 then device 1, not randomized
or counterbalanced; small latency differences should not be overinterpreted.

Private evidence: `artifacts/npu-placement-20260921/`, with corresponding runtime
files under `~/qai-dark-factory/experiments/npu-placement-20260921` on the EVK.

## Saved-input results

HTTP milliseconds, median; pairs include both calls:

| Cache / placement / scheduling | Agent | Reason2 three-image batch | Pair |
| --- | ---: | ---: | ---: |
| Warm, shared device 0, sequential | 246.8 | 1,281.5 | 1,528.3 |
| Warm, split devices, sequential | 246.1 | 1,274.0 | 1,528.3 |
| Warm, shared device 0, concurrent | 352.9 | 1,432.7 | 1,433.2 |
| Warm, split devices, concurrent | 250.1 | 1,286.2 | 1,286.8 |
| Full prefill, shared device 0, sequential | 4,810.2 | 1,258.9 | 6,069.2 |
| Full prefill, split devices, sequential | 4,872.1 | 1,271.1 | 6,142.9 |
| Full prefill, shared device 0, concurrent | 6,027.8 | 3,805.5 | 6,028.3 |
| Full prefill, split devices, concurrent | 4,898.5 | 1,325.3 | 4,899.0 |

Isolated warm agent/Reason2 medians were 250.2/1,283.1 ms on shared device 0
and 242.2/1,281.8 ms with split devices. Full-prefill isolated medians were
4,893.6/1,289.1 ms and 4,882.5/1,277.1 ms respectively. Each table's cache mode
was verified from every measured response. All four reports passed with stable
`inspect(weld)` choices and G classifier results, with no transport errors.
These are repeated saved-input checks, not new model accuracy measurements.

The clearest gain is protection from heavy concurrent prefill: Reason2 latency
falls from 3.81 to 1.33 seconds, about 65%. Warm contention is also reduced.
Sequential latency is essentially unchanged. Thus split placement is useful for
independent workloads or real input-only preparation that overlaps inspection;
it cannot parallelize an agent decision with the unseen result it depends on.
Energy consumption was not measured, and separate devices still share host
memory and other SoC resources.

A resident snapshot with both Reason2 adapter workers, the compiled agent and
the existing CPU fallback loaded reported 15,508 MiB available memory and no
swap. This establishes headroom for this particular setup, not maximum model
capacity or sustained power/thermal behavior.

## Decision-history checks

Device 1 cached/rewind and independent reset requests agree on all 21 saved
histories through the live HTTP path. The thirteen action probabilities differ
by up to 0.01158; this is winner parity, not bitwise score identity. Cold arrival
took 3.76 s, the two subsequent new-body rewinds took 0.473 s each, and ordinary
append calls had a 0.242 s median. The new arrivals used 1,017/1,023 input tokens.

An initial comparison against the September 19 direct-worker report failed on
two actions. Investigation found that report omitted the initial BOS token;
the live HTTP bridge includes it. Those different inputs must not be used to
attribute differences to device placement. The failed comparison is retained
as `device1-history-parity.json`, separately from the new matched-device check.
Its current cached and reset arms still agree internally on all 21 winners.
The existing missing-sealant departure is reproduced in both modes, so these
speed optimizations do not establish reliable policy behavior.

The fresh matched-device reset replay passes all 21 histories with identical
token arrays and identical winning actions. The maximum difference among the
thirteen action probabilities is 0.006925, so it does not establish bitwise
numerical equality. `device-parity.json` records this separate comparison.
The earlier failed historical comparison remains preserved, not overwritten.

## Actual rendered agent integration

Using the public preparation helper's device-1 config, the first actual
assembly/preassembled/assembly fixture **passed** in 185.490 s. It made 30 agent
decisions and 15 real Reason2 batches, with zero rejected calls and zero
independent audit failures. Median Unreal-observed latency was 269.183 ms for
Ministral and 1,332.401 ms for the three-camera Reason2 batch; maxima were
3,852.907 and 1,349.102 ms respectively. The cold first decision accounts for
the agent maximum.

All 30 selected unmasked tokens matched the actual model-visible history and
dispatched tool. The physical-camera telemetry audit passed 2,920 checks.
Neither that telemetry check nor the saved-input replay establishes new image
classification accuracy. The independent process evaluator did not veto,
repair or suggest an action. The agent handled actual ambiguous thermal results
with waits/reinspection; the trace contains nine G, four A and two R batches.

The native responses show one reset, 27 exact appends and two new-body rewinds.
The latter retained a 949-token policy prefix and took 489.9/488.9 ms at the
proxy, versus 3,831.9 ms for the cold initial request. Every decision still
performed a fresh score call. This validates prefix rewind in this actual live
run, beyond the earlier offline experiment.

There is **no demonstrated whole-cycle speedup**: the earlier compiled agent run
took 185.940 s with 28 decisions/14 batches, while the earlier selected CPU run
took 170.429 s with 22 decisions/11 batches. Those action counts and thermal
inspection times differ. Split placement chiefly removes contention from
independent work, and does not fix the known policy failure in saved histories.
This is one three-body development integration, not a reliability guarantee.

Evidence includes `live-agent-audit.json`, `live-token-audit.json`,
`live-camera-audit.json`, `live-device1/process_runtime.json`, the bounded
`prepared-live-proxy` request logs and private rendered screenshots. The
temporary native/proxy/tunnel services were stopped after testing; original
18195/18196/18198 services stayed running and healthy. Normal launch defaults
remain unchanged. To reproduce the candidate integration with its services
and tunnels ready:

```powershell
.\unreal\Scripts\run.ps1 -ProcessSmoke -ProcessControl agent -Enhancements `
  -ProcessModelSource evk_hexagon `
  -ProcessEndpoint http://127.0.0.1:18198/v1/inspect `
  -AgentEndpoint http://127.0.0.1:18197/v1/chat/completions `
  -Output artifacts/fresh-device1-agent
```

That run preceded streaming and the explicit NPU-agent preset. For the current
combined setup, use `-EvkAgentNpu`; it also preserves the NPU agent when
`-ProcessBackendSwitch` returns from host GPU to EVK. See
[streaming with split HTP devices](streaming_dual_htp.md) for the reusable
launcher and subsequent measurements. The smoke fixture exits after its run.

Final regression: Python discovery in the configured WSL environment passes
246 tests (two optional SDK fixtures skipped), the rebuilt read-only inventory
probe runs successfully against the EVK runtime, and native policy passes 151
checks. The separate actual flow run passes three departures, 15 model batches
and all six thermal acceptance checks at 153.279 s simulation time; its camera
audit also passes 2,920 checks. This flow run uses the unchanged original Reason2
service. Unreal process count was confirmed zero after both fixtures completed.
