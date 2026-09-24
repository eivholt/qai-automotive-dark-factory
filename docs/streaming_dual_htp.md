# Streaming Reason2 with the compiled Ministral controller

This optional configuration combines streaming Reason2 on IQ9 HTP device 0
with the compiled Ministral-3-3B controller on device 1. It uses the existing
one-token action prompt without fine-tuning. Reason2 still makes one joint
decision from three actual images; camera motion, confidence thresholds and
freshness rules are unchanged.

**Research option, not the recommended default:** the September 23 combined
live run failed on body 3. It skipped panel placement and selected welding;
the independent evaluator recorded `welding_without_placement` without vetoing
the action. The later seating inspection returned R and the agent held.
Only two bodies departed. Speed and correct device placement do not establish
adequate agent behavior. The CPU-agent default remains selected.

The subsequent [named-history experiment](agent_history_reliability.md) adds
explicit jobs, removes model-visible IDs/timestamps and canonicalizes JSON field
order. Its optional policy examples improve the synthetic development result
to 44/46, with two known policy errors remaining. Follow that guide for the
explicit candidate flags; the historical failures below remain part of the
evidence and ordinary startup keeps the original history format.

The separate QAIRT processes use different runtime versions: Reason2 uses
2.45.0; the compiled Ministral bundle uses 2.50.40 with guarded append and
prefix rewind. Keep their library environments separate. Both Reason2 adapters
stay loaded on HTP0, while the controller retains its own weights and KV state
on HTP1. These devices still share system memory and other SoC resources.

## Start the optional controller

Prepare the compiled bundle and native worker using
[the Ministral guide](ministral_npu.md), then create a device-1 configuration
using [the placement helper](npu_device_placement.md). These private files are
not distributed. On the currently prepared EVK, the exact paths are:

```bash
cd ~/qai-dark-factory/repo
export DF_GENIE_CONFIG="$HOME/qai-dark-factory/experiments/npu-placement-20260921/device1-prepared/genie_config.json"
export DF_GENIE_WORKER="$HOME/qai-dark-factory/experiments/npu-placement-20260921/genie_single_token_worker"
export DF_NATIVE_PROPERTIES="$HOME/qai-dark-factory/research/ministral-npu-20260919/native-props.json"
export DF_QAIRT_SDK="$HOME/qai-dark-factory/runtime/qairt-2.50.40.260831"
bash scripts/agent/start_evk_compiled_agent.sh --check
bash scripts/agent/start_evk_compiled_agent.sh
```

The check validates local files and requested HTP1/core0 placement. It does
not establish model accuracy or actual device use. The two transient user
services expose native inference on loopback 18206 and the tool-call proxy
on 18197. The launcher verifies readiness and rejects occupied ports. It
does not replace the CPU services, change Reason2 or install boot autostart.
`DF_AGENT_QA_LOG_DIR` optionally enables bounded private request logs in a
fresh directory; leave it unset for ordinary use. No speculative next-turn
prefill is enabled on this compiled candidate.

Use the existing streaming Reason2 service on 18198. From WSL, forward the
candidate agent port in addition to the existing model tunnels:

```bash
ssh -N -o ExitOnForwardFailure=yes -L 18197:127.0.0.1:18197 ubuntu@192.168.1.158
```

In Windows PowerShell:

```powershell
.\unreal\Scripts\run.ps1 -Process -ProcessControl agent -Enhancements `
  -ProcessBackendSwitch -EvkAgentNpu
```

This selects EVK first, with Reason2 on 18198 and the compiled agent on 18197.
F4/right-stick click switches to the existing host GPU services and back to
the selected EVK agent. The HUD identifies HTP0/HTP1. Both GPU services must
already be available to use that alternative. Explicit endpoint overrides for
the initial profile are preserved. Omitting `-EvkAgentNpu` keeps the existing
CPU-agent EVK preset (18196).

For a bounded, automatically closing three-body test, replace `-Process`
with `-ProcessSmoke` and add a fresh `-Output artifacts/your-run` directory.
To stop only this optional controller on the EVK:

```bash
systemctl --user stop dark-factory-compiled-agent-single-token dark-factory-compiled-agent
```

## Interpretation

The actual combined run made 27 agent choices and 14 streamed inspections in
156.227 seconds before the hold. Median agent latency was 269 ms, with a
3.836-second cold first call; median final Reason2 HTTP latency was 714 ms.
Both new-body requests used guarded prefix rewind. All 27 logged unmasked
winning tokens match the actual conversation and dispatched action. Physical
camera telemetry passes 2,726 checks, and the streaming audit found no transport
or overlap errors. The overall agent, token and streaming acceptance reports
still **fail** because the process failed. These checks must not be described
as a successful three-body integration or a whole-cycle speedup.

Exact-token replays of three critical histories (new body, after sealant, and
before the invalid weld) reproduce all three original NPU choices with a full
cache reset. After sealant, NPU probabilities were 0.440 for inspection versus
0.433 for placement; the CPU model preferred placement at 0.541. Given the
later history in which placement had already been skipped, **both** backends
selected welding (NPU 0.727, CPU 0.655). Thus there is a model/backend difference
near an earlier tie and a broader policy weakness on the resulting history.
This is not evidence of stale KV state, and the unchanged CPU default is not
a guarantee of correct policy behavior. No prompt, action mask, confidence
threshold or hidden deterministic repair was added to make this test pass.

A separate actual EVK → host GPU → EVK routing fixture then completed all
three bodies in 166.165 seconds: 30 choices, 15 inspections (11 EVK, four GPU),
zero audit failures/rejected calls and 2,920 passing camera checks. Both EVK
bodies used proxy 18197; the GPU body used 18194. This warmed, mixed-backend run
validates selection and switching, not a matched speed comparison or a remedy
for the earlier failed run.

The flow lifecycle run completed three bodies, 16 gates and all six thermal
acceptance checks. Pause/switch cancellation passed. One of its 3,114 camera
checks failed: authored/actual rotation difference 0.0001038516 degrees exceeded
the existing 0.0001-degree tolerance. Physical-lens/capture agreement and exact
joint targets passed. This small geometry QA exception is recorded without
changing camera motion or relaxing the threshold. Flow was a functional check,
not an isolated timing comparison with the agent.

UE 5.8 builds and 260 Python tests pass (two optional SDK skips). Native policy
passes 151 checks. The optional compiled services and temporary forwarding tunnel
were stopped after testing; original streaming Reason2 and CPU-agent services
remain healthy. No interactive demo is left running. Full private evidence is
under `artifacts/streaming-dual-htp-20260923`.

September 23 independent saved-input measurements used one warmup and five
measured trials per row. The same three weld images arrived 963 ms apart.
The table measures HTTP elapsed time; streamed vision starts at the first PNG,
including the subsequent image-arrival intervals. It excludes physical camera
deployment. Agent history priming is outside the measured interval.

| Agent workload | Isolated agent | Concurrent agent | Isolated streamed vision | Concurrent streamed vision |
|---|---:|---:|---:|---:|
| Exact cached append | 282 ms | 235 ms | 2,705 ms | 2,775 ms |
| Full history prefill | 4,685 ms | 4,672 ms | 2,742 ms | 2,729 ms |

All 24 streamed batches, including warmups, retain exactly the baseline's
reported G/A/R probabilities, raw winning token, token counts and image hashes.
All 24 measured/warmup agent calls select the same action, with verified
append/reset cache telemetry. There are 36 trials total, including the six
warmups. These small timing samples show no large contention penalty on split
devices; the variation does not establish that concurrency accelerates either
model. A 70 ms increase in the warm vision median is about 2.6%.

Actual process handles are `fastrpc-cdsp-secure` for both Reason2 variants and
`fastrpc-cdsp1-secure` for Ministral. With the CPU fallback also resident, the
snapshot reported 15,491 MiB available memory (approximately 15.1 GiB) and
no swap. This is one operating snapshot,
not a maximum-capacity or sustained-power test.

Independent controller work can overlap streaming vision on the other device.
An agent decision that depends on a new inspection result must still wait for
that result. The live controller already overlaps decisions with camera
withdrawal; it does not speculate about unseen images or start welding early.
Faster agent calls therefore need not shorten the complete assembly cycle.

The compiled controller remains opt-in. Earlier saved histories exposed a
missing-sealant departure error, including with full cache resets. Runtime
placement and cache reuse do not repair that policy weakness. Development
integration and repeated-image timing are not held-out accuracy measurements.
