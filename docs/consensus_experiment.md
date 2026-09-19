# One versus two inspection batches

The demo now uses **one valid, fresh, high-confidence three-image batch** at every
model gate. This is the user's September 18 choice after the comparison below.
There is no consensus switch. Presence G selects preassembled and R selects
assembly. Thermal R waits; seating R raises an exception. A, invalid replies,
stale captures and transport failures hold. Pause cancels pending work and
requires a fresh scan. Confidence .98, label mass .5 and eight-second freshness
remain unchanged.

Both `execution_policy.required_consensus_batches` and the legacy-named protocol
field `decision_policy.consecutive_green` report 1. Host/EVK servers must reload
the current configuration; older two-batch replies are rejected. Prompts, image
processing, three physical views, thermal bands and adapter training provenance
are unchanged. This controller decision does not require retraining.

```powershell
# After the compatible model host is ready:
.\unreal\Scripts\run.ps1 -Process -Enhancements
```

`ProcessPolicySmoke` exercises the fixed policy with scripted replies and no
HTTP. Actual `ProcessSmoke` requires assembly/preassembled/assembly departures,
correct branches and six thermal acceptance audits. The audits observe nominal
45/55 C and clear-ready 42/52 C boundaries without influencing model actions.

The comparison below is historical: its temporary one/two-batch launcher option
has been removed. `compare_process_consensus.py` can still analyze those saved
reports. Repeated scenarios establish integration/timing, not rare-error rates
or independent held-out accuracy. Reply-time temperature is not a per-image
truth label; the three images are captured at different times while heat cools.

## September 18 host experiment

The paired sequence uses seed 42 and `-Enhancements -ProcessObserverMode 3`, in
the order two batches, one batch, one batch, two batches. Each run starts a new
Unreal process, with the same resident Reason2 service on the RTX 5090. The selected
`panel-contact-shared-960-v7` primary and `panel-contact-shared-896-v6` weld adapter use the existing BF16
stage router and linear patch embedding. No weights or thresholds were changed.
Private reports, host provenance and screenshots are under
`artifacts/consensus-ab-20260918/`; no generated assets or images are published.

Process seconds exclude application startup and sensor asset warmup. They include
all arrival, robot motion, inspection, cooling holds and departure for three
bodies. Both policies use the same authored mechanical durations. Earlier gates
let the one-batch process reach subsequent inspections while the panel is hotter,
so some saved confirmation scans are replaced by necessary cooling rescans.

| Run | Required batches | Three-body process time | Model requests | Median HTTP latency |
| --- | ---: | ---: | ---: | ---: |
| two-a | 2 | 162.33 s | 23 | 82.76 ms |
| one-a | 1 | 137.83 s | 16 | 82.35 ms |
| one-b | 1 | 134.33 s | 15 | 82.10 ms |
| two-b | 2 | 162.31 s | 23 | 82.20 ms |

The one-batch mean is **136.08 s versus 162.32 s**, a **26.24 s / 16.16%**
reduction per three-body sequence. Requests average **15.5 versus 23**, or
**32.61% fewer**. The gain comes mainly from fewer physical camera scans; model
inference latency remains approximately 82 ms.

All **12 departures** selected the correct assembly/preassembled branch. All
**24 thermal acceptances** were below both the nominal limits and the stricter
clear-ready limits. No invalid/transport replies occurred among 77 requests.
The one-batch trials returned five uncertain thermal batches in total and four
hot-weld rejections, all held correctly; the two-batch trials returned two
uncertain batches. These are expected holds, not evidence of false approvals.
One-batch weld inspections first rejected approximately 72 C and then accepted
approximately 45.5 C after a fresh scan. Removing confirmation therefore did
not remove the cooling gate. An extra uncertain sealant batch accounts for most
of the 3.50 s difference between the two one-batch runs.

This small repeated scenario found no outcome improvement from the second
batch. These runs cannot quantify rare errors or validate untested seating faults.
The user subsequently selected fixed one-batch operation. No new adapter training
or held-out evaluation was performed.

Validation: UE 5.8 build, 138 Python tests, 41 launcher checks, 118 native policy
checks covering both counts, all four actual ProcessSmoke runs, and all four
physical-camera telemetry audits passed (14,978 camera checks total). The two
HUD screenshots were reviewed and shown in chat. All Unreal instances and the
temporary repo-specific model service were stopped afterward; the adjacent
physics service was untouched. Final machine-readable summary:
`artifacts/consensus-ab-20260918/comparison.json`.


## Fixed-policy regression

After removing the switch and updating both sides of the reply policy, the host
three-body run passed in 137.89 s: 16 valid replies, 82.49 ms median, three correct
departures, six clear-ready thermal acceptances and 3,114 camera audit checks.
No new model training or held-out accuracy is claimed. Build, 143 Python tests,
42 launcher checks and 115 rendered fixed-policy checks pass. The historical
comparison above remains separate from this final integration regression.
