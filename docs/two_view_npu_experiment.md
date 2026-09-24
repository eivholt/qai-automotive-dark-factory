# Two-view classification and concurrent HTP inference

Follow-up: the [streaming experiment](streamed_reason2.md) now preserves the joint
three-view scores with early encoding/prefill. Its fresh rendered NIR check
rejects the proposed spot1+spot2 reduction: 36/52 versus 52/52 with three views.
The 48/48 retained-data result below is historical feasibility evidence, not a
deployment recommendation.

September 22, 2026. The user requested a test of two independent camera
classifications on the IQ9's two HTP devices, including whether Ministral could
remain on NPU between inspection jobs. This follows the
[device-placement study](npu_device_placement.md) and
[pipeline investigation](dual_htp_pipeline.md).

**Two HTPs accelerate independent requests when both images are already ready.
For this demo's one moving camera, streaming to one HTP keeps up with the
observed image arrival rate. The current adapters and views do not support
replacing every gate with two independent classifications.** No production
configuration, prompt, threshold, camera trajectory or worker was changed.

## Scope and method

All inference uses resident copies of the deployed Reason2 FP16-vision/W8-text
bundles and the original QAIRT 2.45 scoring worker. Private directories link the
unchanged model files and copy only `htp_backend_ext_config.json`, assigning
device 0 or 1. Actual worker handles confirm `/dev/fastrpc-cdsp-secure` and
`/dev/fastrpc-cdsp1-secure`. The original primary/weld services remain loaded and
idle; they are not overwritten or stopped.

The classification experiment reuses the existing **192-batch development
cohort**: 24 presence, 72 sealant, 24 seating and 72 weld batches. Each original
image is processed separately using the previously versioned
`automotive-panel-single-view-experiment-v1` prompts, producing **576 new
single-image calls**. Images are hash checked, all four stages retain the .98
conditional-confidence and .5 label-mass thresholds, and each native inference
emits one token with full-vocabulary probabilities. Labels and source metadata
are used only by the offline evaluator, never sent to the worker.

For comparison across view subsets, the offline evaluator combines predictions:
all selected G gives G; presence requires unanimous R for a missing-panel branch,
otherwise it gives A; the other stages give R if any selected view is R,
otherwise A. These are experimental aggregate labels, not additional live agent
vetoes. Whole-batch labels are never assigned to individual camera images.

All three two-view subsets are evaluated. The scheduling comparison uses
views 1+2 for primary stages and 2+3 for weld. They are explicit experimental
selections, not a validated replacement sensor recipe. Keeping all three
independent predictions provides another control.

A further **192-call joint two-image control** uses the same selected pairs.
It retains the original global inspection wording, changing only the number
and order of views and the sealant run-to-view description. Its prompts are
recorded separately and are not accepted by the live router. This distinguishes
reducing the image count from replacing a joint decision with independent ones.

A nine-batch pilot also reruns the original three-image prompt: all nine answers
are correct. Its 27 single-image answers reproduce the earlier single-view probe
with **identical winners and G/A/R probabilities**, using the same worker hash.
Thus those reproduced classification failures are not caused by relocation to
the second HTP.

The separate timing study uses one saved development sample each for presence,
sealant and weld. It includes one warmup and five measured trials per mode,
rotating/reversing mode order. There are two availability conditions:

- Both images ready at the beginning: measures concurrency on supplied inputs.
- Staggered arrival: releases inputs at the stage's observed median inter-capture
  gap from the previous live device-1 run, approximately **0.962-0.966 seconds**.
  This includes the observed settling/capture overhead; the motion itself is
  authored as 0.8 seconds.

The timers are real wall-clock releases around actual NPU calls, not an
arithmetic latency estimate. Images and temperatures are frozen saved data.
No robot job or altered two-pose motion is executed. Transport/PNG encoding and
initial arm deployment are outside these native-worker timings.

## Hardware scheduling results

Weld example, median milliseconds from availability of the first image:

| Input availability / classifier scheduling | Inspection result ready | Including next NPU agent decision |
| --- | ---: | ---: |
| Two images already ready, one HTP serially | 1,364 | 1,614 |
| Two images already ready, two HTPs concurrently | 727 | 976 |
| Two images arriving 962 ms apart, one HTP | 1,645 | 1,894 |
| Two images arriving 962 ms apart, two HTPs | 1,646 | 1,895 |
| Three staggered images, wait for batch then original joint classifier | 3,231 | 3,481 |
| Three staggered images, independent classifications streamed to one HTP | 2,607 | 2,857 |

Parallel execution reduces the already-ready pair's classification latency by
about **47%**. With the actual camera cadence, a single-image call completes
before the next image arrives, so the second accelerator adds no useful latency
reduction. Comparing the two-view and three-view rows also removes an image and
one inter-capture interval; do not attribute that entire difference to hardware
concurrency.

The same pattern holds for the other timed stages: with two images ready,
presence takes 1,106/574 ms on one/two HTPs, and sealant 1,469/763 ms. With the
recorded arrival gap, those become 1,524/1,527 ms for presence and 1,708/1,710 ms
for sealant. These small streamed differences do not establish a useful gain.

The three-view streaming row suggests roughly **624 ms** could be hidden for this
saved weld example. It changes classification semantics and is not an enabled
streaming implementation. It does not establish a whole assembly-cycle saving;
earlier live inspections can observe hotter parts and trigger more waits.

## Ministral can stay on HTP1

During the weld timing study, both Reason2 replicas and compiled Ministral remain
loaded. Reason2 executes on HTP0/1, then Ministral consumes the resulting weld
observation on HTP1. Actual device handles and loaded libraries confirm Reason2
QAIRT 2.45 and Ministral QAIRT 2.50.40 coexist on that device.

The agent benchmark uses an actual saved decision history, primed before each
timed trial, followed by the measured classifier result. Saved timestamps remain
unchanged: this is a history/timing replay, not fresh live gate evidence. If the
classifier returns a different code, the final observation is changed to that
actual code rather than inventing G. All measured weld examples return G and
Ministral selects the same holder-release action. Its 30 measured decisions use
an actual 59-token append and take approximately **244-246 ms**, without model
reloads. Full-history priming, recorded separately, is excluded from this warm
decision measurement.

There are five compiled instances resident during this test: the original two
Reason2 workers, two experimental Reason2 replicas, and Ministral. The CPU agent
fallback also remains loaded. `MemAvailable` is **4,622,260 KiB**, about 4.41 GiB,
with no swap configured. This demonstrates this particular residency, not room
for arbitrary additional models or a sustained power/thermal characterization.

No new CPU/GPU agent comparison was run: NPU sharing works without slowing the
following warm decision. The previously measured tuned CPU and EVK GPU agent
medians were about 1.04 and 3.09 seconds respectively; these are separate studies,
not matched arms of this experiment.

## Camera coverage

The coverage audit uses 18 sealant and 18 weld rows from the existing permanent-
frame captures, selecting training/development rows only and excluding the test
split. It reconstructs the 78 sealant sample positions and six weld centers in
world space, projects them into every actual physical view, and samples the
original grayscale pixels. Sealant uses the existing 3x3 footprint, welds 5x5,
and the existing 2.5 C error tolerance. Combining all three views reproduces a
passing coverage result.

| Selected views | Sealant rows failing coverage / 18 | Weld rows failing coverage / 18 |
| --- | ---: | ---: |
| 1+2 | 9 | 8 |
| 1+3 | 18 | 0 |
| 2+3 | 9 | 0 |
| 1+2+3 | 0 | 0 |

Among 273 hot sealant sample observations, pairs 1+2 and 2+3 each miss ten;
pair 1+3 misses 92. These are repeated sample observations across captures, not
independent physical defects. Ambient agreement alone is not visibility proof.

One concrete example is sample `episode-0001-case-1`, sealant sample 35: its
target is 89.06 C. View 1 samples 20 C because foreground geometry occludes it;
view 2 projects it outside the image; view 3 samples 89.02 C. Removing view 3
would remove the actual hot-contact evidence at that point. The private images
are retained in `artifacts/panel-frame-capture-v1/`.

Some two-view weld subsets cover all six centers in this limited fixture.
That does not establish all fault/occlusion coverage or that the changed model
prompts classify them correctly. A generally valid two-view recipe would need
new camera placement, coverage checks and compatible model evaluation.

## Classification results

Correct whole-batch decisions on the reused development cohort:

| Stage | Original joint three-view reference | Two independent views, selected pair | Two jointly classified views | Three independent views |
| --- | ---: | ---: | ---: | ---: |
| Presence | 24/24 | 16/24 | 24/24 | 16/24 |
| Sealant | 71/72 | 65/72 | 71/72 | 58/72 |
| Seating | 24/24 | 18/24 | 24/24 | 18/24 |
| Weld | 72/72 | 69/72 | 68/72 | 72/72 |
| Total | 191/192 | 168/192 | 187/192 | 164/192 |

The selected two-view configuration produces three false G results, all ambiguous
weld cases, and two wrong missing-panel R branches on already installed panels.
The original joint reference has no false G or wrong NIR branches. The
reference is its retained full validation plus this study's matched nine-batch
control, not a newly rerun 192-batch three-view benchmark.

Other two-view weld subsets score 69/72 for 1+2 and 71/72 for 1+3; **every pair
has at least one false G**. Keeping all three independent weld predictions gives
72/72 with zero false G on this cohort, but the other stages still regress.
Pair 1+3 for sealant also produces four false G results and lacks the second
run's proper viewpoint. A higher count for a subset must not override its
coverage failure.

The joint two-image control recovers the primary model's stage scores without
training. Median native calls are 882 ms presence, 884 ms seating, 1,061 ms
sealant and 1,067 ms weld. However, it falsely accepts four ambiguous weld cases.
Nor does its sealant score repair the demonstrated visibility gaps: a dataset
can be labeled correctly while part of a contact remains hidden. This experiment
therefore identifies **joint two-view NIR** as a more promising candidate than
independent two-view classification, not a ready replacement for all gates.

This is not new held-out accuracy. Existing adapters were trained for the joint
three-view prompt; fast single-image execution does not validate those changed
prompts. Nothing here justifies weakening the confidence or ambiguity bands.

## Implication for the next implementation

Keep the current three-view live gates. A targeted follow-up can test joint
two-view NIR presence/seating with a separately versioned capture/prompt contract,
coverage of all intended defects, physically checked R4 motion and new validation.
It does not require assuming that a new adapter is necessary; these 48 existing
development examples already match their expected labels without training.

The strongest general next candidate is
to stream **image encoding and compatible incremental prefill** while R4 moves,
retaining one final joint classification and the original prompt. That targets
idle time without substituting the failed independent-classification contract.
It still needs feature/token/output parity, cancellation/freshness tests and
actual rendered integration.

A separately validated single-view weld tool could be another experiment,
retaining three views and recording the changed prompt/adapter provenance.
Two simultaneous fixed cameras or several
independent cells would create more ready work and could benefit directly from
two Reason2 HTP workers. This demo's current camera cadence does not.

Private evidence and code are under `artifacts/two-view-htp-20260922/`, mirrored
under `~/qai-dark-factory/experiments/two-view-htp-20260922` on the EVK. Reports
retain prompt text, input hashes, full scores, device/library inventory, measured
arrival/start/end times, warmups, memory snapshots and all failures. Source
weights, SDK files and sensor images remain private. No live policy or adapters
were replaced, and no new training was performed.

Final verification checks all ten pinned source-bundle files for each adapter;
all original hashes match. All seven reports complete without transport errors,
all private workers exit, and the original 18195/18196/18198 services return
healthy HTTP 200 responses. No Unreal instance was started for these saved-input
experiments. No native-policy or rendered integration pass is claimed for a
changed two-view contract.
