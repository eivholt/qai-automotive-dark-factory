# Agent demonstration scope

On September 23 the user clarified the main objective: demonstrate an LLM
choosing tools to sense and act in the simulated factory. A simpler agentic task
is acceptable when it makes that demonstration clearer and more repeatable.

September 23 implementation: this eight-tool profile is now the default for
`-ProcessControl agent`. Flow mode remains the default comparison. Both normal
agent services use canonical named histories (named-jobs-v4), without model-visible
IDs or numeric timestamps, and with a fixed field order including nested thermal
results. The original menu is retained in `configs/live_agent_legacy.json` for
explicit comparison using `-AgentRuntime local -AgentProfile legacy` and a proxy started with that
config. See [exact current prompts](current_agent_prompts.md).

The [agent harness](edge_agent_runtime.md) now runs on the EVK by default.
Its autonomous loop owns the conversation and tool selection; Unreal reports
actual equipment events and executes the selected tools. F4 selects inference
devices while preserving this EVK orchestration. The prompt and eight-tool
semantics are unchanged by the move.

| Current choice | Work performed by the tool |
| --- | --- |
| `inspect_panel` | Actual NIR presence inspection using Reason2. |
| `prepare_panel` | Apply sealant, then pick, place and hold the panel. |
| `inspect_temperatures` | Actual sealant and weld thermal inspections; return both results and their validity. |
| `weld_panel` | Complete the authored six-spot pinch-welding job. |
| `inspect_seating` | Actual NIR panel-seating inspection using Reason2. |
| `finish_body` | Retract an engaged holder, then move the body out. |
| `wait` | Wait exactly five seconds. |
| `hold` | Stop the episode for operator attention. |

All eight choices remain available to the agent on every turn. This is a
smaller fixed capability set, not a state-dependent list of permitted actions.
The application would continue to supply IDs, timestamps and tool parameters.
One unmasked A-H action token is translated into the selected tool call; code
supplies its reason and call ID. No controller fine-tuning was performed.

The LLM retains the decisions that make the demonstration agentic:

- Inspect the arriving body and choose whether assembly is needed.
- Request temperature measurements, interpret the observations and decide
  whether to weld or wait and inspect again.
- Request seating and temperature checks before choosing departure.
- Retry an inconclusive inspection or hold on a defect or tool failure.

The missing-panel and preassembled branches remain useful visible examples of
different behavior under the same task instruction. Authored arm motion,
extrusion, pickup and carriage movement belong inside the actuator tools.
Their internal choreography should be visible in the tutorial and execution
trace. Partial failures must stop that tool and report what actually completed;
they must not trigger an undisclosed recovery sequence or claim success.

`inspect_temperatures` obtains two real Reason2 stage results. Each retains
its existing three-camera capture, prompt, adapter routing, thresholds and
freshness validation. The combined tool distinguishes absent/invalid results
from valid classifications. It does not automatically wait, re-inspect, start
welding or dispatch the body. The LLM chooses subsequent tool calls.

Keep the independent evaluator outside the agent's observations. Retain schema,
identity, cancellation and mechanical motion checks. Record any additional
deterministic decisions openly if later experiments need them; do not describe
them as LLM choices.

Validation must use the new tool contract, cover both arrival branches,
hot/unclear/invalid observations and partial actuator failures, and compare
actual model choices with actual execution in rendered runs. Earlier 44/46
policy results belong to the original thirteen-choice interface. Bundling tools
does not retroactively turn its failed cases into passes, and any reliability or
latency benefit must be measured. Preserve the original profile as a comparison.

The executor records each actual actuator substep. `prepare_panel` checks the
supported single-pickup capability before depositing sealant, then runs application
and placement. `finish_body` checks only whether the holder is mechanically engaged
to choose retraction before carriage movement. Every substep is independently
audited, with no quality veto. Paused mechanical macros resume where they stopped.
A cancelled thermal macro returns only the observations already received, with a
cancellation status; a new call starts both checks afresh. Field names `completed`
and `completed_jobs` describe work, not accepted quality. Nested `valid` and
`decision` fields retain their independent meanings.

## September 23 validation

- UE 5.8 build passed; native ProcessPolicySmoke passed all 164 checks, including
  macro sequencing, duplicate pickup rejection, pause/resume, partial thermal
  cancellation and execution without recipe vetoes.
- Python discovery passed 280 tests (two skipped).
- Separate rendered flow-mode ProcessSmoke passed all three bodies and six
  thermal acceptance audits, using 15 actual host-GPU Reason2 batches.
- Actual rendered agent runs passed on host GPU and IQ9 CPU-agent/HTP-Reason2:
  three bodies each, assembly/preassembled/assembly, zero independent action-audit
  failures and zero rejected calls. Each run made 23 LLM choices and 21 actual
  Reason2 batches. Host raw next-token logs matched all 23 physical dispatches.
- IQ9 streaming validation passed all 21 batches, preserving early preparation,
  three actual images per inspection, stage identity and the eight-second receipt
  freshness limit. Neither inspection in the thermal bundle uses synthetic data.

| Run | Three-body wall time | Agent HTTP median | Reason2 final HTTP median |
| --- | --- | --- | --- |
| Host GPU | 179.433 s | 36.4 ms | 84.1 ms |
| IQ9 CPU agent + HTP Reason2 | 216.844 s | 1,335.7 ms | 764.2 ms |

Reason2's final HTTP duration excludes earlier streamed preparation and camera
motion. IQ9 capture-to-reply medians were approximately 3.16-3.35 seconds by
stage. These runs are development integration evidence, not held-out model
accuracy or a controlled speed comparison against the original tool menu.
Bundled temperature checks perform more sensing work than a single stage call.

Both devices selected the same actions in all 42 synthetic planning probes:
37 matched the expected choices. Three extra waits were conservative: the model
waited on an adverse pre-welding weld-temperature result even though the sealant
result already allowed welding. Two choices were unsafe and remain unresolved:

1. It chose welding using thermal G evidence obtained before prepare_panel.
2. After an additional weld pass, it chose finish_body using seating and thermal
   evidence from before that new weld pass.

These failures remain in the reports; neither a prompt retest nor a deterministic
repair has hidden them. The user explicitly selected this default while retaining
failures for follow-up. Reproduce with `scripts/agent/probe_simple_policy.py`;
expected choices are never included in model messages.

Private evidence is under `artifacts/agent-simple-20260923/`: gpu-policy.json,
evk-cpu-policy.json, gpu-live-ready/, gpu-live-logs/, gpu-token-audit.json,
evk-live/, evk-stream-audit.json and the corresponding live audit reports.
The initial gpu-live/ attempt reached a transport hold while its proxy was still
starting; gpu-live-ready/ is the completed integration run. No failed attempt is
counted as a successful run. Assets, captures and model logs remain ignored.
