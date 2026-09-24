# Clearer agent history and HTP reliability

September 23 update: canonical named histories and the eight-tool menu are now
the default at the user's request. See [current prompts](current_agent_prompts.md)
and [implemented tool scope](agent_demo_scope.md). Historical measurements below
retain their original thirteen-choice scope; known policy failures are not erased.

The original opt-in `named-jobs-v3` history gave Ministral explicit job names and actual
results, removed body IDs and numerical timestamps from model input, and fixed
JSON field order. The model still returns one unmasked action token. The
application supplies tool arguments, call IDs, body identity and timing. No
Ministral adapter, action mask, recipe veto or source-truth hint is added.

This is an agent reliability experiment, **not a promotion of the NPU controller
to the default**. The canonical candidate passes 44/46 development policy cases
and 34/34 reserved cases, but still incorrectly departs a preassembled body after
only a weld-temperature check, and permits welding after placement without
sealant. These are important known failures. The unchanged CPU controller also
has policy failures; changing compute devices alone does not solve them.

## Model input

Actual completed sealant application becomes:

```json
{"job":"apply_sealant","status":"completed","holder_engaged":false,"completed_jobs":["apply_sealant"]}
```

An inspection names the requested stage and retains the actual stage, G/R/A,
validity, failure reason and holder state. The completed-job list is a literal
chronological list of jobs whose returned status was `completed`; failed,
cancelled and pending calls do not enter it. Repeated completed jobs remain
repeated. This list does not recommend actions or certify inspection quality.
Job completion never becomes a synthetic inspection result.

The transport retains body/epoch/cancellation checks, call pairing, IDs and
timestamps. Private QA logs separately retain the request body ID, and the
verifier reconstructs the model input from the actual Unreal observations and
dispatched actions. An invalid/stale G remains invalid; dropping numerical
capture age does not make it usable. All thirteen actions remain available.

The original experiment's business policy is preserved in `configs/live_agent_legacy.json`. Optional static
examples in `configs/agent_policy_examples.txt` illustrate failure recovery and
independent thermal checks. They are prompt examples, not current-body evidence
or fine-tuning data. Their text contributes to the policy hash. Updating the
policy still requires ordinary validation, not adapter retraining.

## Why canonical JSON matters

The first named-history live run held after successfully applying sealant.
Its equivalent synthetic case chose placement. The only differing observation
content was JSON member order: Unreal wrote `status` before `stage`, while the
Python fixture wrote it afterward. Three full-reset NPU calls reproduced:

| Input | Model choice |
| --- | --- |
| Actual live history, original member order | M: hold |
| Same policy and facts, only members reordered | C: place panel |
| Canonical history with the v3 format version | C: place panel |

The reordered v2 messages are exactly equal to the earlier synthetic fixture.
This is a demonstrated prompt-format sensitivity, not evidence of stale KV.
Canonicalization makes equivalent objects produce identical model input; it
does not decide what action is correct. Unit tests cover all 120 permutations
of the five-field presence result, ID/time variants, failed/cancelled jobs,
chronological completion records, and audit tampering.

## Experiment boundaries

The original policy with named v2 history scored 40/46 synthetic development
cases on both HTP1 and the EVK CPU. Full-reset HTP replay retained the six
failing cases. Several alternatives were worse: shorter policies, a second
one-token self-review, natural-language result labels, a flat event log and
additional inspection summaries. Failed experiments and the failed first live
run remain in the private evidence; they are not discarded after later passes.

Adding the static examples reached 44/46. The 34 reserved cases were evaluated
after selecting that candidate, and all passed. Canonical v3 repeated 44/46 and
34/34. The repeated reserved set is now reused evidence, not a new held-out
score. The cases are synthetic policy histories, not images, real-camera
validation or a production error-rate estimate. No Reason2 image accuracy is
established by these tests.

Follow-up alternatives also failed to improve the selected candidate:
one-token command words scored 43/46 and an explicit index of completed/missing
job and inspection results scored 40/46. A second one-token self-review scored
29/46. These were offline experiments only and are not deployed. The two
remaining canonical failures were not merely near ties: the wrong actions had
raw winning probabilities of 0.792 and 0.897. No confidence-based action filter
was introduced.

During the experiment, a rendered EVK flow run completed three bodies and 16
streamed Reason2 inspections while HTP1 policy probes ran. All 3,114 camera
checks and the stream audit passed. This supports coexistence of the workloads;
it is not a matched whole-cycle speedup measurement.

## Live canonical candidate

Three fresh runs with seeds 42, 43 and 44 completed **nine bodies**, making 84
actual one-token decisions and 42 streamed Reason2 inspections. Each run
completed assembly/preassembled/assembly, with zero process-audit failures,
rejected actions or final holds. Nine ambiguous thermal results caused actual
wait/reinspection sequences; the application did not repair the choices.
All three process, token-to-dispatch and streaming audits pass.

| Metric | Measured result |
| --- | --- |
| Three-body wall time | 178.338 / 178.520 / 178.466 s |
| Agent HTTP latency measured by Unreal, median | 269 ms |
| Warm proxy calls, median; 75 calls | 255 ms |
| New-body proxy calls, median; nine calls | 5,307 ms |
| Reason2 final HTTP latency, median | 714 ms |
| Reason2 first-image-to-answer stage medians | about 2.54–2.71 s |

Reason2's final HTTP interval excludes the early image encoding and prefill.
The new-body delay is real: without IDs, the new arrival is a shortened exact
prefix of the previous conversation. The guarded native cache deliberately
resets that case, and the examples also lengthen the policy. The earlier
ID-bearing 0.49-second new-body rewind result does not apply to this candidate.
No native rewind guard was relaxed and no previous decision was reused.

Camera QA is **not wholly clean**. Seed 43 passes all 2,726 checks. Seeds 42 and
44 each fail one authored/actual Euler-angle comparison at weld view 1:
0.000203578 and 0.000123168 degrees respectively, against 0.0001 degrees.
The physical capture/lens and exact joint checks pass. Camera motion and QA
tolerances are unchanged; these two exceptions remain recorded.

Full-reset replay also corrects the original failed histories: request 20
changes from D (premature sealant inspection) to C (placement), and request 25
changes from E (welding without placement) to C. The control placement remains
C and the later seating-defect history remains M. These are reused failure
regressions, not new accuracy samples.

Actual device handles confirm the two Reason2 workers on
`fastrpc-cdsp-secure` and Ministral on `fastrpc-cdsp1-secure`, with about 15 GiB
available and no swap. Native binaries, model weights, SDK separation and
Reason2 contracts remain unchanged. Python discovery passes 274 tests with two
optional SDK skips; native policy passes 151 checks. The demonstrated sequence
improvement does not erase the two synthetic policy failures or establish
production reliability. CPU remains the default.

The demo and temporary compiled-agent services/tunnel were stopped after the
tests. Existing streaming Reason2 and CPU fallback services remain healthy.

## Reproduce

Use the private prepared HTP1 dependencies from
[the split-HTP setup](streaming_dual_htp.md). Before starting its optional services:

```bash
export DF_AGENT_OBSERVATION_FORMAT=named
export DF_AGENT_POLICY_EXAMPLES="$HOME/qai-dark-factory/repo/configs/agent_policy_examples.txt"
bash scripts/agent/start_evk_compiled_agent.sh
```

At the time of this historical experiment the default launcher history was `json`; these flags selected the
research candidate. Existing CPU and Reason2 services are left unchanged. The
compiled proxy is 18197 and Reason2 is 18198. Then run a bounded, automatically
closing test from Windows:

```powershell
.\unreal\Scripts\run.ps1 -ProcessSmoke -ProcessControl agent -Enhancements `
  -ProcessBackendSwitch -EvkAgentNpu -Seed 42 -Width 1280 -Height 720 `
  -Output artifacts/your-fresh-agent-run
```

Offline policy probes perform no physical jobs:

```bash
python scripts/agent/probe_named_policy.py --endpoint http://127.0.0.1:18206 \
  --policy-examples configs/agent_policy_examples.txt \
  --output artifacts/your-fresh-policy-report.json
```

Use `--reset` to disable cache reuse, or `--suite reserved` for the separately
defined policy histories. `verify_single_token_agent.py` must receive the same
`--policy-examples` file when auditing this candidate. The runtime prompt hash,
actual observations, raw vocabulary winner and dispatch must all agree.

Private evidence: `artifacts/agent-named-history-20260923/`. Model weights,
rendered images and detailed request logs remain excluded from Git.
