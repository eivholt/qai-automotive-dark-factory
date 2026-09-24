# Live agent control

September 23 current default: agent mode now uses eight named tools and canonical
job histories, with IDs/timestamps excluded from the model and deterministic
JSON field order. `prepare_panel` bundles application + placement/hold;
`inspect_temperatures` returns both actual thermal results; `finish_body` retracts
an engaged holder and departs. Wait is fixed at five seconds. No fine-tuning,
recipe veto, automatic cooling or automatic quality retry was added. All eight
actions remain available each turn. See [tool scope](agent_demo_scope.md) and
[exact prompts and tools](current_agent_prompts.md). The original four-function,
thirteen-choice interface remains an explicit legacy comparison, not the current
default. Earlier measurements below belong to that original interface.

September 19, 2026. `-ProcessControl agent` connects Ministral-3-3B-Instruct-2512 Q4_K_M to the
actual Unreal scene and actual adapted Cosmos-Reason2 inspections. The default
`-ProcessControl flow` preserves the existing deterministic controller for comparison.
The older [Python experiments](agentic_process_experiment.md) used synthetic tools;
they are separate from this live integration.

## Responsibility boundary

The default harness is now [deployed on the EVK](edge_agent_runtime.md). It owns
the policy, conversation and autonomous tool loop. Unreal is the equipment
worker, retaining camera capture/Reason2 transport, authored motion and HUD.
`-AgentRuntime local` selects the previous PC harness for comparison.

The agent receives the task objectives, acceptance criteria and tool descriptions in
[`configs/live_agent.json`](../configs/live_agent.json). A body arrival begins a
new conversation. Subsequent messages contain the actual assistant tool calls and
their results: inspection codes, validity, timings and completed mechanical jobs.
The agent sees no source faults, temperatures, expected branches, permissions,
next-step flags or independent evaluator feedback. The native tool-call endpoint
shows its short generated reason on the HUD; this is not an internal reasoning
transcript. The faster [one-token adapter](single_token_controller.md) instead
shows the selected action code and explicitly states that no explanation was
generated.

Current model-facing histories name jobs and remove numeric times/IDs entirely;
application transport retains millisecond precision. The full-precision journal
and freshness checks remain unchanged. The EVK controller uses eight CPU threads. See the
[Ministral study](ministral_controller.md) for current measurements and failures;
[agent latency study](agent_speed.md) retains the earlier Qwen results.
The native tool-call cold three-body run takes 253.539 s, with 3.991 s median agent
decisions and 1.415 s Reason2 batches. Removing fixed inter-tool delays saved
22.031 s; model inference itself did not become faster. The original prompt
and weights are retained after faster-looking prompt/cache candidates failed
decision checks. The separate one-token protocol retains that policy but changes
the output/history format: two cold runs take 197.913 / 198.266 s, with 1.607 s
median controller latency in the final run. See its guide for validation and the exact
derived prompt hash. No controller fine-tuning was performed.

The original comparison tools are `inspect(stage)`, `act(action)`, `wait(seconds)` and `hold(reason)`.
Supported jobs are `apply_sealant`, `place_panel`, `weld_panel`, `release_holder`
and `send_forward`. Each runs its complete authored choreography.
With the default EVK harness, conversation and tool dispatch run on the EVK;
the mechanical executor and camera/Reason2 HTTP client run inside Unreal on the
PC. Controller inference and both Reason2 backends also default to the EVK. Inspection
deploys R4, captures its three physical views and obtains one Reason2 result.
It never starts the next process job. Jobs end in an idle state; the agent decides
what happens next. The agent now thinks while the camera retracts, replacing the
old one-second delay after every tool. A fast reply is queued until R4 is actually
parked; it cannot start overlapping motion. Heat evolves while the agent thinks
or waits. Models remain resident; agent and Reason2 inference remain serial.

The prompt states what constitutes acceptable assembly, including positive
cooling evidence before welding/release. It includes an illustrative preassembled
sequence to clarify the two independent cooling requirements. No deterministic
controller chooses the next step. The LLM can choose
inspection order, retries, waits, skips and repeated applications/weld passes.
There is no autonomous joint planning: tool paths remain authored motion clips.

The executor retains bounded schemas, one tool per reply, body/request/pause
epochs, duplicate-response consumption, supported physical capabilities and
exclusive motion ownership. Carriage motion conflicts with an engaged holder;
a second pickup for the same body's single-panel holder is not implemented.
These mechanical limitations return a tool error. They do not check temperatures
or visual acceptance. Missing observations, a hot classification or an incorrect
recipe do **not** veto a supported job. This behavior is for the simulated demo.

An independent evaluator observes each dispatched job and records missing
inspections, wrong initial branches, incomplete assembly, unnecessary work on a
preassembled body and hot welding/release. It may read source truth solely for
this audit. Its findings do not enter the agent's conversation or change execution.
The HUD shows its cumulative failure count. A smoke run returns failure only
after the run; the evaluator does not repair a bad decision during execution.

`panel_seated` inspection no longer magically attaches the panel in agent mode.
Actual welding-job completion fixes it to the carriage. Repeated sealant or weld
jobs restart their surface heating. Holder engagement persists across idle and
inspection jobs and ends on an actual release job.

## Camera and time contracts

Reason2 prompts, adapter routing, 448 Ã— 256 grayscale images, confidence 0.98,
label mass 0.5, one three-camera batch and the **8-second capture-to-reply limit**
are unchanged. No observer overlay is sent to either vision model. Invalid or
expired vision replies become invalid observations, never positive evidence.
The agent chooses whether to retry, wait or hold; there is no hidden reinspection loop.

The adhesive thermal view measures conducted heat on the **placed panel**. It
cannot measure this target before placement; that capability is explained in
the prompt. Presence/seating codes are NIR; sealant/weld codes are thermal.

The 8-second limit validates receipt of a sensor result. It is not a promise
that a later agent action uses an image less than eight seconds old. The agent
and audit retain observation times and the intervening jobs. EVK decisions can
take longer than that limit; the audit records observation age at action. There
is no extended sensor TTL, automatic refreshing, age-based recipe veto or timer
that replaces a cooling inspection. Real industrial control would need a separate
validated safety architecture; this experiment measures simulated agent choices.
The agent has a separate 120-second total and HTTP inactivity deadline, including a
cold CPU prompt prefill. Unreal's default 30-second inactivity timeout was too
short for that EVK prefill; the vision request deadlines were not changed.

Space / Xbox View pauses mechanical state and cancels partial captures and
outstanding agent requests, including a completed reply awaiting arm clearance.
Resume retains completed work, continues an interrupted
mechanical job, and lets the agent choose again after a canceled inspection. Responses
from previous epochs cannot execute. A transport failure, malformed/truncated
agent response or exhausted 40-decision body budget stops agent dispatch and
shows an error. There is no fallback to the flow controller. Pause/resume can
retry a failed connection; a decision-budget exhaustion requires a new run.

## Run on the host GPU

Start the existing Reason2 service and Ministral in WSL:

```powershell
wsl.exe bash scripts/model/start_host_service.sh --port 18191 --adapter checkpoints/panel-contact-shared-960-v7 --weld-adapter checkpoints/panel-contact-shared-896-v6 --linear-patch-embedding
wsl.exe bash scripts/agent/start_host.sh
```

The second command remains in its terminal. Start the default one-token proxy
in another terminal after the native model on 18193 is ready:

```powershell
wsl.exe env DF_SINGLE_TOKEN_BACKEND=http://127.0.0.1:18193 DF_SINGLE_TOKEN_PORT=18194 bash scripts/agent/start_single_token.sh --prefill-next-turn
```

Wait for `/health` on 18191 and 18194, then use a separate PowerShell:

```powershell
.\unreal\Scripts\run.ps1 -Process -ProcessControl agent -Enhancements -AgentRuntime local
```

## Run with all models on the EVK

Start the prepared stack as described in [EVK runtime](evk_model_runtime.md):

```powershell
wsl.exe ssh ubuntu@192.168.1.158 bash /home/ubuntu/qai-dark-factory/repo/scripts/model/start_evk_qairt.sh --agent
wsl.exe bash scripts/model/start_evk_tunnel.sh --agent
.\unreal\Scripts\run.ps1 -Process -ProcessControl agent -Enhancements
```

The Ministral CPU service and both adapted Reason2 QAIRT NPU vision/text workers
stay resident. Do not run a competing benchmark against these ports during the
demo. Unreal only contacts host loopback tunnels; model services are not exposed
on public network interfaces.

Port 18196 is the one-token protocol adapter over the same native Ministral
service. Port 18195 retains native tool calls and generated reasons. The stack
launcher starts the adapter only for Ministral. Wait for 18196 `/health` after
startup, and 18210 `/health` for the EVK harness; [the one-token guide](single_token_controller.md) also covers host GPU
usage and private token-score audits. Do not send competing requests to both
controller endpoints.

## Switch between EVK and host GPU in the running demo

Start both sets of services above, plus the host one-token proxy on 18194 as
described in [the one-token guide](single_token_controller.md). With both
backends healthy:

```powershell
.\unreal\Scripts\run.ps1 -Process -ProcessControl agent -Enhancements -ProcessBackendSwitch
```

This option starts on **EVK** unless `-ProcessModelSource host_gpu` is supplied.
Press **F4** or **Xbox right-stick click** to switch both models together. The HUD
identifies the active pair: Reason2 NPU + Ministral CPU on IQ9, or both models on
the host GPU. The harness remains on the EVK across this switch. There is no
automatic fallback. Agent mode also defaults to EVK without the switch enabled;
flow mode retains its existing launch defaults.

Switching cancels outstanding vision/agent requests, queued decisions, partial
camera batches and pending readbacks; late encoder output is discarded. An
interrupted agent inspection returns a cancellation in its actual tool history.
The agent chooses whether to inspect again. Completed observations and work stay
in the conversation; physical jobs continue from their current position, and a
paused process stays paused. Explicit model holds and the per-body decision
budget remain intact. A transport hold can retry on the newly selected backend.
No recipe action or positive inspection result is supplied by the switch.

The fixed profiles use vision/agent ports **18198/18196** for EVK and
**18191/18194** for host GPU. The EVK harness reaches the GPU agent through reverse
SSH port 18214. Custom agent endpoints require `-AgentRuntime local`; vision
overrides belong to the initially selected profile. Both services must be
started separately; selecting a stopped backend produces the normal error/hold.
Runtime reports and agent traces record backend selection; it is not added to
model-facing observations or sensor PNGs.

The rendered fixture `-ProcessBackendSmoke` selects EVK/GPU/EVK on its three
bodies, using actual services and model decisions. Run it once with
`-ProcessControl agent` and once with `-ProcessControl flow`, then run
`scripts/verify_backend_switch.py <process_runtime.json> --output <audit.json>`.
The native policy fixture additionally checks interrupted inspections, late
encoder output, pending commands, paired endpoints and preservation of pause,
history, model holds and decision limits. This is integration evidence, not
new model accuracy.

## Reproduce validation

```powershell
.\unreal\Scripts\run.ps1 -ProcessPolicySmoke -Enhancements -Output artifacts\agent-policy
.\unreal\Scripts\run.ps1 -ProcessSmoke -ProcessControl agent -Enhancements -AgentRuntime local -ProcessObserverMode 3 -Output artifacts\agent-host
wsl.exe python3 scripts/verify_live_agent.py artifacts/agent-host/process_runtime.json --output artifacts/agent-host/agent-checks.json
wsl.exe python3 scripts/verify_inspection_arm.py --runtime artifacts/agent-host/process_runtime.json --output artifacts/agent-host/arm-checks.json
```

Omit `-AgentRuntime local` and use `-ProcessSmoke` for the default EVK run. Agent
smoke seeds missing/preassembled/missing bodies **only in the private simulator**,
allows at most 30 minutes and exits after three departures or an agent error/hold.
It records exact prompt/tools, choices, real tool results, source-only audits,
camera telemetry, screenshots and bounded recent request images. Normal runs
retain bounded private trace/report history without saving sensor PNG requests.
The original flow smoke remains separate and keeps its existing time limits.

Native policy checks deliberately execute an unsupported *recipe* choice and
verify that the audit flags it without a veto. They also check schema, canceled
epochs, duplicate responses, serial jobs and absence of automatic advancement.
These checks and the three-body runs are development/integration evidence,
not held-out model accuracy or industrial safety validation.

The proposed missing mounting frame and sealant-coverage/reapplication scenarios
remain future work. There is no compatible live coverage adapter or frame
inspection tool in this integration. Repeating `apply_sealant` is supported as
a whole-run illustrative job; there is no localized defect-guided repair path.


## Development evidence

The refined-prompt host run completed three bodies in 212.50 seconds: 29 Qwen
choices, 16 valid Reason2 batches, no rejected calls or independent audit failures.
Median Qwen latency was 552 ms; Reason2 was 82 ms. An earlier live prompt skipped
a required thermal inspection and was correctly audited without a veto. These
runs demonstrate why acceptance wording and evaluation still matter when recipe
checks move out of the executor. Full provenance is in [validation](validation.md).


Final live EVK run passed all three bodies in **648.77 s**, with **25 Qwen
choices**, **14/14 valid Reason2 batches**, no rejected calls and zero independent
audit failures. Qwen median/max were **15.369/52.678 s**; Reason2 **4.847/4.986 s**.
The first request followed an earlier canceled prefill, so this is not a clean
cold-start latency measurement. All three models remained resident; three memory
samples showed at least **21.49 GiB available**, with no service restarts observed.
The physical-camera audit passed **2,726 checks**. Evidence:
`artifacts/agent-live-20260918/evk-timeout-fixed/` and the private summary.json.

The EVK run needed no thermal waits: surfaces cooled during its longer agent
decisions. Both runs made extra sealant/seating inspections, including seating
on the already assembled body. Qwen remains CPU-bound on the EVK; the requested
GGUF runs, but this is much slower than host GPU control. This is prompt-refined
three-body development evidence, not new held-out accuracy. The actual EVK HUD
and welding screenshot were inspected and shown in chat.

The September 19 [speed follow-up](agent_speed.md) supersedes those live timing
figures: **11.186 s median Qwen, 1.415 s Reason2, 484.345 s total**, 22 decisions,
11 camera batches, three correct departures and no rejected calls/audit failures.
The initial full-policy prefill was 77.712 s. The prompt now explicitly separates
the existing visual acceptance conditions for initially present panels and panels
assembled here, avoiding unnecessary seating checks on preassembled bodies.
Both thermal inspections remain mandatory goals; the executor still has no
recipe veto. See the study for the earlier held attempt and development limits.

The later [Ministral comparison](ministral_controller.md) supersedes the Qwen
default and timing figures: two complete EVK runs passed six total departures
with no audit failures or rejected calls. The clean-start run took 275.569 s,
with 3.952 s median agent decisions, a 28.885 s first request and 1.416 s median
Reason2 batches. The revised prompt includes a preassembled example; two earlier
Ministral prompts missed a thermal check and are preserved as failed candidates.

The September 21 live backend-switch test validates EVK / host GPU / EVK routing,
but the agent run **does not pass the full process**: after two departures,
Ministral skipped placement on the third body and eventually chose hold. There
was one mechanical rejection and no transport error. The separate flow run
passed all three bodies with the same backend transitions. See
[the validation record](validation.md#live-backend-switching-21-september-2026)
for counts and scope. Switching backends preserves the agent's completed tool
history; it does not correct process choices or reset an explicit model hold.
