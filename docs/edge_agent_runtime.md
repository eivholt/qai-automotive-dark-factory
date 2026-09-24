# EVK-owned live agent runtime

`-ProcessControl agent` defaults to an agent harness on the IQ9 EVK. The
deterministic `-ProcessControl flow` comparison remains available and remains
the default when no control mode is specified. `-AgentRuntime local` explicitly
selects the previous Unreal-owned conversation loop.

| Responsibility | Default location |
| --- | --- |
| Load task prompt and eight tool definitions | EVK Python runtime |
| Maintain each body's conversation, call Ministral, dispatch its chosen tool | EVK Python runtime |
| Ministral weights, token scoring and KV/prefix cache | EVK CPU model service |
| Adapted Reason2 vision/text inference and streaming cache | EVK NPU service |
| Render sensor images, move R4, stream images to Reason2, execute authored robot jobs | Unreal on PC |
| HUD, observer controls and independent outcome audit | Unreal on PC |

The harness is ordinary Python CPU orchestration; moving it to the EVK does not
move that code onto the NPU. Neural inference remains in the resident native
model services. The EVK chooses *which* tool to use; the PC executes the simulated
equipment. An inspection tool still owns camera travel, capture, streaming and
the actual Reason2 request, then returns its observations to the EVK. This keeps
the existing image, adapter and freshness contracts intact.

## Startup

On the prepared EVK deployment, from the repository's PowerShell:

```powershell
wsl.exe -d Ubuntu -- ssh ubuntu@192.168.1.158 bash /home/ubuntu/qai-dark-factory/repo/scripts/model/start_evk_qairt.sh --agent
wsl.exe -d Ubuntu -- bash scripts/model/start_evk_tunnel.sh --agent
Invoke-RestMethod http://127.0.0.1:18210/health
.\unreal\Scripts\run.ps1 -Process -ProcessControl agent -Enhancements -ProcessBackendSwitch
```

The stack launcher starts `dark-factory-edge-runtime` alongside the existing
model services. The runtime loads `configs/live_agent.json` from the EVK's repo;
update that deployed copy and restart the runtime when changing its policy.
The model proxy must use the same config. Model artifacts and compiled runtimes
remain private and require the existing deployment preparation.

The equipment API binds only EVK loopback port 18210. The host tunnel forwards it to
WSL/Windows loopback port 18210, and reverse-forwards EVK loopback port 18214 to the host
one-token GPU proxy on 18194. Both directions use SSH; no public HTTP listener is
required. To use F4 / Xbox right-stick click, first start the host Reason2 and
Ministral services described in [live agent control](live_agent_control.md).

F4 changes the **inference** pair, not the harness location. The EVK conversation
persists and its next agent call uses either local port 18196 (Ministral CPU) or
reverse-forwarded port 18214 (host GPU). Unreal routes Reason2 to 18198 or 18191.
The optional `-EvkAgentNpu` profile selects the separately prepared port 18197 agent
experiment; it is not promoted by this harness change. No automatic fallback
is performed. `-ProcessModelSource host_gpu` starts on the GPU comparison.

`-AgentEndpoint` overrides are supported by `-AgentRuntime local`. The remote
runtime uses the fixed profiles above and rejects conflicting overrides.
`-AgentHarnessEndpoint` selects the equipment API, defaulting to
`http://127.0.0.1:18210/v1/cell/exchange`.

## Equipment protocol and ownership

Unreal sends a bounded `POST /v1/cell/exchange` about every 150 ms, including while
paused or executing a job. It sends session/body identity, cancellation epoch,
paused/busy/closed flags, selected inference profile and incremental equipment
events. The API rejects prompts, tool schemas, full conversations and unexpected
fields. Only actual `arrival`, `started` and `result` events enter the EVK history.
Unreal retains a local audit mirror, but does not submit that mirror to the agent.

The EVK's independent 20 ms loop calls the agent after arrival or completion when
the worker is idle. A returned command is retained until Unreal acknowledges
starting it. Results are accepted only for an actually started matching call.
The event sequence and hash-bound replays make lost HTTP acknowledgements safe
to retry without duplicating tool execution. IDs and times belong to transport;
the existing named-history formatter removes them before model inference.

Pause and backend changes increment the epoch. Unstarted commands and late
inference replies are discarded. Already-started atomic physical jobs retain
their identity and can report completion after resuming; canceled inspections
report only observations actually obtained. The server never invents a second
thermal check or repairs an agent choice. A 5 s heartbeat lease, one active worker,
one model request at a time,16 pending events and 40 decisions per body bound
execution. No recipe or temperature veto is added. The independent audit stays
outside model input.

On disconnect, no new jobs start. An already-started actuator job can finish and
queue its result. Pause/resume reconnects within the same process and runtime;
the EVK must still have the matching session state. History is in memory, not
durable. If the harness restarts mid-body, the equipment holds on a scope/event
mismatch; start a fresh demo session. It does not infer completed work or silently
restart assembly. A clean shutdown sends the final events and closes the lease;
the rendered smoke waits for final departure acknowledgement before exiting.

## Verification

`tests/test_edge_agent_runtime.py` covers ownership, no recipe veto, replay,
scope, cancellation, partial thermal observations, leases and final acknowledgements.
`ProcessPolicySmoke` also checks the native equipment packet and cancellation.
Use actual `ProcessSmoke -ProcessControl agent`, `ProcessBackendSmoke
-ProcessControl agent`, and a separate flow smoke for rendered integration.

For private cross-machine QA only, start the runtime with
`DF_AGENT_RUNTIME_QA_LOG=<fresh absolute path>`; this records at most 512 events.
The ordinary service does not persist conversations. Copy that private JSONL and
the runtime `/health` identity alongside the Unreal report, then run:

```powershell
wsl.exe -d Ubuntu -- python3 scripts/verify_edge_agent.py --runtime artifacts/run/process_runtime.json --evk-log artifacts/run/evk.jsonl --identity artifacts/run/identity.json --output artifacts/run/edge-audit.json
```

The verifier reconstructs the EVK model conversations from actual equipment
events, compares model-selected and executed tools, checks actual aarch64 runtime
identity, and requires final acknowledgements. It never drives the live process.
Successful runs demonstrate integration, not new held-out accuracy or a repair
of the known agent-policy failures in [the scope report](agent_demo_scope.md).

September 23 rendered results: EVK-only completed three bodies in 203.557 s with
21 choices and 19 Reason2 batches. EVK/GPU/EVK completed three bodies in 211.606 s
with 23 choices and 21 batches (eight/five/eight). Both had zero process-audit
failures or rejected calls. The cross-machine verifier matched all 44 choices
and all 94 equipment acknowledgements to the EVK's actual conversation; the
same aarch64 runtime PID served both inference routes in the switching run.
These are separate warm integration runs, not a matched performance comparison.

The EVK-only camera fixture has one retained exception among 3,696 checks:
panel-seated spot 0 authored/actual Euler discrepancy 0.000130225 degrees versus
the existing 0.0001-degree bound. Exact joints and physical capture/lens checks
pass; no geometry, poses or tolerance were changed. The switching run passes
all 4,084 camera checks. All 19 EVK-only batches pass the streaming verifier.

The separate host-GPU flow regression passes three departures, 14 actual
inspections, six thermal acceptance checks and all 2,726 camera checks. UE 5.8
build, 169 native policy checks and 52 launcher checks pass. Python discovery
runs 303 tests successfully, with two optional skips. The ordinary EVK runtime
was restored without QA logging, and no Unreal demo process was left running.
