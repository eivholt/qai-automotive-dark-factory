# Historical agent-controlled assembly experiment

**Current implementation:** [Live agent control](live_agent_control.md) now has
an explicit Unreal tool connection and a non-vetoing process audit. The
prerequisite-enforcing mock executor and results described below are historical.
They do not describe the new live executor.

For the September 18 Qwen9B retest, actual IQ9 CPU trials, adapted Reason2
deployment and current startup commands, see [EVK model runtime](evk_model_runtime.md).
The results below retain the original September 16 experiment provenance.

Research and host experiments, 16 September 2026. The production Unreal demo still
uses its existing process controller. The new Python experiment runs a real local
controller LLM against **synthetic inspection replies and mock mechanical jobs**.
It has no Unreal command connection. These results are not Reason2 accuracy,
industrial validation, or IQ9 deployment evidence.

## Recommended controller

Start with **Qwen3.5-9B Q4_K_M**, text only, 8,192 context tokens, one request slot,
thinking disabled. Keep Reason2 and its two selected adapters responsible for
images. The controller does not need another vision encoder/projector.

| Candidate | Evidence and tradeoff |
|---|---|
| Qwen3.5-9B | Leading host candidate. Newer than the December 2025 Ministral baseline; publisher BFCL-v4 score 66.1. The downloaded Q4_K_M file is 5,680,522,464 bytes / 5.29 GiB. |
| Qwen3.5-4B | Smaller and faster: 2,740,937,888 bytes / 2.55 GiB. Publisher BFCL-v4 score 50.3. It made more incorrect tool requests in our first comparison. |
| Gemma 4 E4B | Another newer candidate with native function calling. Its effective parameter count is not its storage count: approximately 8B parameters including embeddings. Keep as a deployment alternative, not yet locally benchmarked. |

Sources: [Qwen model card](https://huggingface.co/Qwen/Qwen3.5-9B),
[9B quantization](https://huggingface.co/unsloth/Qwen3.5-9B-GGUF),
[4B quantization](https://huggingface.co/unsloth/Qwen3.5-4B-GGUF),
[Google model card](https://huggingface.co/google/gemma-4-E4B-it).
Publisher scores are not Q4 results on our process and do not establish superiority
over Ministral on this task. We have not run the old Ministral baseline here.

The [OR project reference](https://github.com/eivholt/or-edge-agent/blob/main/IQ9_EVK.md)
already separates LLM tool selection from action validation and runs a Ministral
controller on HTP. Its slow CPU VLM experiments should not be used as a latency
prediction for our smaller-image, next-token Reason2 classifier. The adjacent
physics project's GenieX work measured approximately 2.15 s mean TTFT with a
GELU CPU fallback on its own small probe; those inputs and labels are different.

## Agent chooses jobs; Unreal performs them

The user's requested interface is deliberately coarse. `inspect(stage)` means
deploy the camera arm, perform the physical three-view scan, submit complete
one Reason2 batch and validate the result. `act(spot_weld)` means perform the whole
authored welding job. Arm poses, tool opening, electrode clamping, current timing,
particles and withdrawal remain inside Unreal. No LLM-generated joint angles,
per-electrode commands or millisecond scheduling.

The intended live division is:

1. The controller LLM requests a job or an inspection, using only observed tool
   results and acknowledged job status.
2. An inspection broker owns camera motion, freshness, body/epoch identity, exact
   prompts, adapter routing, confidence 0.98, label mass 0.5 and one fresh three-camera
   batch. It returns a compact validated result, not a guessed next action.
3. An executor checks prerequisites and motion ownership, starts the complete
   job and reports completion. A rejected request does not start motion. Timers
   and the agent cannot override missing model permission.

Keep models resident and schedule inference serially: controller -> Reason2 tool
-> controller. Simultaneous residency does not require simultaneous computation.
This avoids competing inference kernels and repeated model loading. Begin with
shadow decisions before connecting accepted jobs to the scene. Do not replace
the controller in the production demo until asynchronous cancellation, process
holds, body turnover and actual camera results have been exercised.

The live broker must retain immutable evidence handles scoped to body, epoch,
stage and scene revision; pause/turnover invalidates pending work. Mechanical
commands need idempotent request IDs and completion acknowledgements. The offline
mock checks several of these rules but is not a production executor implementation.
No raw fault state, case name, seed, expected answer or private temperature goes
to either model. Reason2 keeps its existing image-only inputs and stage-only
adapter selection; the controller cannot rewrite an inspection prompt.

## More varied initial conditions and recovery

The `frame-sealant` scenario set implements these proposed branches in the mock
executor. It uses the separate study name `agent-frame-sealant-study-v1`; the new
stage names must not be sent to the current automotive adapter service.

| Observed condition | Agent-level jobs |
|---|---|
| Panel already installed | Existing sealant-temperature and weld-temperature checks, then depart. Skip hidden-frame/coverage inspection. |
| Panel missing, mounting frame installed | Apply sealant, inspect continuity, place panel, then the existing cool/weld/seat/cool/release sequence. |
| Panel and open mounting frame missing | Place and hold frame, weld frame without sealant, verify seating and weld cooling, release frame; then assemble the panel. |
| Discontinuous sealant | Add a pass over the same complete three-edge path, inspect again, then place the panel only after acceptance. |
| Persistent gap after two additional passes | Hold for review with the panel still unplaced. |
| Uncertain/unavailable inspection | Hold; do not turn missing evidence into acceptance. |

Panel presence comes first: an installed opaque panel hides the underlying frame
and sealant. Frame presence and bead continuity must be inspected while exposed.
Continuity and cooling are separate decisions. A top-up must reset the affected
sealant/contact-heat history in a future scene implementation; it does not inherit
an earlier cooling approval. Preserve the existing one-in-five preassembled branch.
No live probabilities for missing frames or under-dispensing have been chosen yet.

Future visual work needs a pickable copy of the source-derived open frame, a stock
location, authored holder/welder paths and frame-specific thermal fields. Keep
the raw assets private and use actual arm-mounted camera views. New Reason2 data
must distinguish frame presence, frame seating/cooling and full three-edge bead
continuity, with whole-episode splits and independent nuisance variation. Include
small gaps, shadows, glare, underfilled sections and uncertain visibility.

The previous `automotive-sealant-coverage-study-v1` base-model feasibility test
failed. There is currently **no compatible trained continuity adapter or live
reapplication gate**. A binary continuous/not-continuous/A decision avoids region
localization, but it still needs new captures, training and held-out evaluation.
Start with visible-contrast gap examples and compare NIR/locally lit views before
selecting the sensor contract; do not infer gaps solely from cooling patterns.

## IQ9 memory calculation

Use GiB = 2^30 bytes. Qualcomm specifies 36 GB LPDDR5 for the
[IQ-9075 EVK](https://www.qualcomm.com/developer/hardware/qualcomm-iq-9075-evaluation-kit-evk).
A read-only check of this user's board measured **36,902,268,928 bytes = 34.37 GiB**
visible RAM, no swap, and approximately **9.67 GiB MemAvailable**.
It also reported 23.74 GiB slab, including **23.31 GiB unreclaimable slab**.
The cause has not been diagnosed; do not attribute that memory to model weights
from process RSS. Existing EVK services were not changed.

The current host loads **two merged BF16 Reason2 copies**, not one base with two
cheap dynamically switched adapters. Each private rank-8 LoRA file is 47,789,640
bytes, containing 11,927,552 FP32 parameters. They modify both vision and decoder
layers. A decoder-only GGUF LoRA switch cannot be assumed to reproduce them.

For the first EVK estimate, allow two separately merged/exported Reason2 bundles,
including two matching vision/projector files. The adjacent physics project's
measured base export is 1,229,455,008 bytes for the Q4_0 decoder plus 819,395,424
bytes for the FP16 vision/projector, **1.908 GiB per bundle**. Merging a LoRA does
not change tensor shapes, so this is a useful size proxy, not a measured automotive
quantized export. Unmerged adapter files are not added again to merged weights.

| Allocation | Qwen9B + two Reason2 bundles | Qwen4B + two Reason2 bundles |
|---|---:|---:|
| Controller GGUF weights | 5.29 GiB | 2.55 GiB |
| Two Reason2 decoder + vision bundles | 3.82 GiB | 3.82 GiB |
| Reason2 FP16 KV, 4,096 tokens per copy | 0.875 GiB | 0.875 GiB |
| Controller KV/recurrent-state allowance | 0.313 GiB | 0.313 GiB |
| Compute buffers, allocator and runtime allowance | 2–4 GiB | 2–4 GiB |
| **Estimated model working budget** | **12.3–14.3 GiB** | **9.6–11.6 GiB** |
| OS/application planning reserve | 3–4 GiB | 3–4 GiB |
| **Dedicated-device planning total** | **15.3–18.3 GiB** | **12.6–15.6 GiB** |

These are engineering allowances, not guaranteed peak memory or measured HTP
allocations. Unified RAM is shared by Linux and accelerators; it is not extra
GPU VRAM. Both options fit the board's nominal capacity, but **the current 9.67 GiB
available does not provide adequate headroom for the 9B plan**. Investigate the
device's existing memory state before attempting co-residency there.

The cache arithmetic is reproducible:

```text
Reason2 FP16 KV/token = 2 * 28 layers * 8 KV heads * 128 head_dim * 2 bytes
                     = 114,688 bytes; at 4,096 tokens = 448 MiB per copy.
Qwen9B attention KV  = 2 * 8 full-attention layers * 4 KV heads * 256 * 2 * 8,192
                     = 256 MiB; add 64 MiB recurrent-state planning reserve.
```

Qwen uses hybrid attention; recurrent state and backend workspace require separate
allowances. The 4B column conservatively keeps the same cache reserve. The current
PyTorch Reason2 scorer actually uses `use_cache=False`; the KV rows budget a future
GGUF serving context, not extra cache on top of a measured PyTorch allocation.

Dynamic visual+text adapter support could reduce duplication later. It must first
preserve all adapted tensors and exact stage routing. Do not count that saving
in the initial deployment budget.

Capacity is not throughput. Q4_K_M on CUDA is not proof of full HTP acceleration.
The prior Reason2 Q4_0 export retains a Q6_K output tensor, which can fall back on
the pinned backend. Qwen's recurrent operations also need placement verification.
GenieX [PR #1248](https://github.com/qualcomm/GenieX/pull/1248), merged 3 August,
fixed an IQ9075 VLM projector backend-selection crash; its board measurements
used Qwen3.5-2B, not 9B. Its GPU/hybrid caveats do not establish behavior on this
Ubuntu image. Match QAIRT compiler/runtime versions and validate every deployment
with backend logs, peak memory, latency, and classification/decision parity.

## Reproduce the host tool experiment

Use a recent CUDA llama.cpp with Qwen3.5 support. The tested build is
`910196f6b3dfc6aca88fa732e2b02f270ff9b56b`, version 10123, already installed locally.
The scripts use the binary without modifying that adjacent project's installation.
Download only the text GGUF; do not load an mmproj for the controller.

| Model | Quantization repository revision | GGUF SHA-256 |
|---|---|---|
| 9B | `unsloth/Qwen3.5-9B-GGUF` / `3885219b6810b007914f3a7950a8d1b469d598a5` | `03b74727a860a56338e042c4420bb3f04b2fec5734175f4cb9fa853daf52b7e8` |
| 4B | `unsloth/Qwen3.5-4B-GGUF` / `e87f176479d0855a907a41277aca2f8ee7a09523` | `00fe7986ff5f6b463e62455821146049db6f9313603938a70800d1fb69ef11a4` |

The private downloads are in WSL `~/models/dark-factory-agent/`. From PowerShell
at the repository root, run the server in one terminal:

```powershell
wsl.exe bash scripts/agent/start_host.sh
```

Then run either study in a second terminal:

```powershell
wsl.exe python3 scripts/agent/experiment.py --repeat 2 --output artifacts/agent/panel.json
wsl.exe python3 scripts/agent/experiment.py --scenario-set frame-sealant --repeat 2 --output artifacts/agent/frame-sealant.json
```

The endpoint is loopback port 18193. `DF_AGENT_MODEL`, `DF_AGENT_LLAMA_BIN` and
`DF_AGENT_PORT` override paths/port. Ctrl+C stops the foreground server. No model
weights or traces are committed. Reason2 need not be running for the synthetic
tool experiment. An exit status of 1 means at least one case failed the strict
criterion: correct terminal outcome, no rejected tool calls, no transport error,
and a real observed exception before an expected review hold.

## Initial measured results

RTX 5090, thinking off, greedy decoding, eight development cases repeated twice.
These repetitions are not sixteen independent held-out cases. The first matched
comparison used the same prompt/tools for both models (`qwen*-replay-v3.json`).

| Controller | Clean cases | Rejected requests | HTTP decision median / p95 |
|---|---:|---:|---:|
| Qwen3.5-9B Q4_K_M | 14/16 | 2 | 372 / 733 ms |
| Qwen3.5-4B Q4_K_M | 10/16 | 66 | 224 / 247 ms |

9B reached the correct final outcome in all cases but retried an inspection after
stale evidence in both repeats; the executor rejected it and the agent then held.
4B repeatedly requested an inapplicable inspection for preassembled bodies and
exhausted the turn limit. The small test favors 9B as the next candidate, not as
a validated replacement controller. Normal missing-panel orchestration took
3.49–3.64 s total LLM time over nine job/inspection requests, excluding real
mechanical travel, cooling and Reason2 calls.

Earlier prompt iterations are retained privately: initially `hold` was confused
with maintaining the gripper; another prompt attempted assembly after one batch.
Moving two-batch collection inside the inspection tool improved the interface.
The expanded study subsequently exposed excessive context from repeated verbose
tool results. Results now expose compact validated batch summaries; full identity
and freshness checks remain inside the executor. An installed panel also skips
underlying frame inspection to respect opaque occlusion. These are development
changes, so earlier scores are not automatically scores for the revised interface.

The expanded frame/sealant study (`qwen9-frame-sealant-v2.json`) completed all
16 runs with the intended terminal outcome; **12/16 were clean**. Both repeats
of the preassembled and combined-recovery cases contained rejected requests.
Missing-frame installation, a single sealant top-up, persistent gaps escalating
after two top-ups, uncertainty and unavailable coverage all completed cleanly.
No actual image classification or scene motion occurs in these fixtures.
The earlier expanded v1 run stopped on a context-limit HTTP error; its partial
trace is retained and is not counted as a complete run.

Both selected BF16 Reason2 copies were also loaded alongside Qwen9B. Reason2
reported 8,521,694,208 CUDA-allocated bytes (7.94 GiB). Total GPU use sampled at
18,070 MiB / 17.65 GiB including desktop allocations, with 32,607 MiB total.
Twenty-four HTTP probes using four previously captured private batches had a
119.5 ms median while controller work overlapped. This establishes host
co-residency and transport operation, not fresh consensus or new accuracy.
Unreal was not running, so it does not establish full-demo FPS/headroom.

Private reports, memory observations, prompts, tool traces and server logs are
under `artifacts/agent-research-20260916/`. The new jobs and gates remain an offline
study until their scene generation, sensor views and trained adapters exist.
All 138 Python tests pass in the prepared WSL environment. No Unreal runtime or
rendering source changed, and no rendered validation is claimed for the proposal.
The temporary controller and Reason2 host services were stopped after testing;
ports 18191 and 18193 have no listeners. The EVK's services remain untouched.

## September 18 fixed one-batch follow-up

The user selected one fresh three-camera batch for all gates, without a consensus
switch, and authorized EVK reboots at will. The old conveyor model service is
stopped and disabled; its files remain intact. Reboot reclaimed the large kernel
slab leak. Qwen9B plus both adapted Q8 Reason2 bundles now load together on IQ9,
with roughly 21.7 GiB still available during the first agent cases.

The updated one-batch host suite passes **16/16 panel runs** (106 decisions,
386.55 ms median) and **16/16 frame/rework runs** (166 decisions, 371.85 ms median),
with no rejected calls. These are two deterministic repeats of eight synthetic
cases per suite. They supersede the earlier prompt results above, but remain
real-LLM/simulated-tools development tests, without actual Reason2 inspection or
Unreal jobs. Additional device/integration evidence is in
[the EVK runtime guide](evk_model_runtime.md).

Private evidence: `artifacts/evk-one-batch-20260918/`. Current image gates retain
.98/.5, eight-second evidence age and fixed prompts. The new frame/coverage stages
still have no trained adapters. A live agent integration also needs to account
for Qwen CPU decision latency after an inspection: it must not silently extend
the existing evidence-age limit while choosing the subsequent job.

The corresponding EVK standard suite now passes **8/8 cases / 53 decisions**,
with no rejected tool calls, **12.06 s median** and **53.56 s first request**.
Both adapted Reason2 services stayed loaded throughout. Total agent inference
was 707.63 s; the device expanded frame/rework suite has not been run in this
follow-up. These results still use simulated inspection/job replies.
