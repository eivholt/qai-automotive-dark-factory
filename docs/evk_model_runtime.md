# IQ9 model experiments — 18 September 2026

For current Ministral/Reason2 startup, see [live agent control](live_agent_control.md)
and the [Ministral comparison](ministral_controller.md). This page retains the
earlier Qwen deployment experiments. Co-residency and earlier mock-tool
results below remain distinct from live agent-control validation.


The controller in these earlier experiments was **Qwen3.5-9B Q4_K_M**, with thinking disabled and complete-job
tools. Reason2 retains the four existing image gates, fixed prompts, three
448 × 256 grayscale inputs, one output token, unmasked probability bounds,
0.98 conditional confidence, 0.5 label mass and one fresh three-camera batch.
The one-batch policy is fixed; there is no consensus command-line option.
New frame-presence and sealant-coverage gates remain synthetic experiments;
there are no compatible trained adapters for those proposed inspections.

## Controller results

| Execution | Scope | Clean outcomes | Median HTTP decision |
|---|---|---:|---:|
| RTX 5090 | Eight existing process cases, two repeats | 16/16 | 305 ms |
| RTX 5090 | Eight frame/rework cases, two repeats | 16/16 | 343 ms |
| IQ9 CPU, initial two-batch study | Missing-panel and preassembled cases | 2/2 | 14.84 s |
| IQ9 CPU, fixed one batch, both Reason2 bundles resident | All eight standard cases | 8/8 | 12.06 s |

The initial EVK test completed 13 decisions; its third case was interrupted to
release memory and is not a result. After reboot, the complete one-batch suite
passed all **eight cases / 53 decisions**, with no rejected calls while both
Reason2 backends stayed resident. Median Qwen decision time was **12.06 s**; the
first request took **53.56 s**, and all decisions totalled **707.63 s**. The suite
includes assembly, preassembled, repeated thermal waits, committed presence,
seating exception, ambiguity, unavailable inspection and stale evidence.
These tests use a real controller model and **synthetic inspection/job replies**,
not autonomous control of Unreal or measured Reason2 accuracy. They are development
fixtures, not independent held-out trials.

An earlier run with less explicit observations scored 10/16. Exposing the actual
cooling-wait flag, requiring a tool call, and clarifying the complete preassembled
and frame-release paths removed the observed errors. The executor still rejects
unauthorized jobs; the prompt does not bypass an interlock.

## Adapted Reason2 exports

Both LoRAs include visual parameters. Export each **merged decoder AND its matching
adapted vision encoder**. A decoder-only adapter switch would lose part of the
fine-tuning. Stage routing is fixed: shared960 for presence, sealant and seating;
shared896 for weld. `record_edge_provenance.py` records both hashes, original
adapter training provenance, prompts, export revision and quantizer hash.
`verify_edge_export.py` checks both files before every backend start.

Matched host comparison on the existing 96-batch `rear-roof-capture64-v1`
development validation cohort:

| Decoder / vision | Correct | False G | Median HTTP |
|---|---:|---:|---:|
| BF16 / F16 | 96/96 | 0 | 121 ms |
| Q4_0 / F16 | 89/96 | 7 | 131 ms |
| Q8_0 / F16 | 96/96 | 0 | 135 ms |

**Q4_0 is rejected for live gates.** Its seven errors turned ambiguous examples
into G: four presence, two sealant and one weld batch. The edge router accepts
only Q8_0. Four current-scene image
replays also produced R/G/G/G for presence/sealant/seating/weld, matching the host
reference. Replayed captures establish transport/classification behavior, never
fresh consensus. The 96-batch cohort predates the permanent frame; this is not a
new held-out score for the final scene.

Each Q8 decoder is 1,834,427,360 bytes and each F16 vision encoder is 819,394,752
bytes: about 2.47 GiB per adapted bundle. Qwen's exact requested GGUF is
5,680,522,464 bytes, SHA-256
`03b74727a860a56338e042c4420bb3f04b2fec5734175f4cb9fa853daf52b7e8`.
The three models' weight files alone total about 10.23 GiB, before working memory.

The complete EVK comparison also scored **96/96**, with zero transport errors
and zero false G, at **5.52 s median / 6.24 s maximum** using six CPU threads for
vision. Both adapter backends were resident, requests were serial, RAM prompt
caching was disabled, and free memory stayed around 1.8–2.0 GiB. The deployed
configuration uses all eight Cortex-A78C cores: four current-scene probes retained
their decisions at 4.66 s median. A separate vision-on-HTP experiment aborted on
its first image request with a DSP queue error; CPU vision remains mandatory in
the provided launcher. The decoder startup log confirms 29/29 layers offloaded
to HTP0, with some other operations on CPU; this is not an all-NPU pipeline.

The actual eight-thread EVK **ProcessSmoke passed**: assembly / preassembled /
assembly, three correct departures in **272.97 s**, 23/23 valid fresh replies,
and all six thermal acceptance audits passed. Median request time was **4.85 s**.
Acceptance temperatures were 20.00–25.04 C, below both nominal and clear-ready
boundaries. One uncertain sealant result held and was reinspected. No thresholds,
consensus rules or evidence-age limits were loosened. The 4,472-check physical
camera audit also passed. This is current-scene integration evidence, not a new
accuracy cohort or live Qwen control of Unreal.

Validation also includes the UE 5.8 build, 144 Python tests, 44 launcher checks,
118 rendered policy checks, and an invalid edge-request probe that correctly
returned A/invalid with the `evk_hexagon` source. Screenshots were reviewed and
shown in chat. The Unreal test instance and temporary host GPU model services
are closed. That initial run used two batches and left Qwen unloaded. The subsequent
fixed-one-batch/co-residency test below supersedes that service state.

## Fixed one-batch run with all models resident

After reboot and the completed agent suite, the actual Unreal EVK ProcessSmoke
passed **assembly / preassembled / assembly**, with **three departures in
181.50 s** and **12/12 valid fresh replies**. Reason2 HTTP latency was **4.868 s
median / 5.016 s maximum**. All six thermal acceptances were clear-ready,
at 20.10–35.15 C. One uncertain sealant result held before a fresh successful
inspection. The physical-camera audit passed **2,338 checks**. Qwen stayed loaded
but idle during image inference; these are serial computations with simultaneous
model residency, not live Qwen control of the scene.

The corresponding host one-batch run passed in 137.89 s, with 16/16 valid replies
at 82.49 ms median, six clear-ready acceptances and 3,114 camera checks. Different
inference delays change thermal rescan timing, so request counts differ. These
runs establish exercised integration behavior, not new held-out classification
accuracy or independent error rates.

Across 74 memory samples covering Qwen decisions and actual Reason2 inspections,
**available RAM stayed at or above 21.49 GiB**. Unreclaimable slab peaked at
146.34 MiB after reboot, compared with approximately 24.8 GiB before it. No
current-boot OOM event was found. Per-service charged peaks were 1.630/1.570 GiB
for primary/weld, 7.355 GiB for Qwen, and 0.026 GiB for the router. These exclude
some shared/device allocations and must not be summed as the complete footprint.

UE 5.8 build, 143 Python tests, 42 launcher checks and 115 rendered policy checks
pass. Actual host and EVK screenshots were reviewed and shown in chat. Private
reports, traces, memory samples and `summary.json` are under
`artifacts/evk-one-batch-20260918/`.

Unreal, temporary host GPU services and the test-only agent SSH tunnel are closed.
The EVK primary, weld, router and Qwen services remain running; the persistent WSL
18198 router tunnel remains active. The old conveyor service remains stopped and
disabled. User authorization permits future EVK reboots without further approval.

## Isolated runtime

The reachable device was `ubuntu@192.168.1.158`, accessed through WSL SSH. The
saved `iq9075-evk` alias still points to an old address; global SSH configuration
was not changed. Everything new is under `~/qai-dark-factory/` on the EVK.
The old `qai-conveyor-geniex.service` on port 18183 is stopped and disabled
at the user's request; its model and deployment files remain intact.

The runtime uses native llama.cpp revision
`0eadefebd3f8f92a86d634a0e5b8fffc9dc792c0` and the matching Hexagon libraries
from [Qualcomm GenieX v0.6.1](https://github.com/qualcomm/GenieX/releases/tag/v0.6.1).
The ARM64 release archive SHA-256 is
`985c13348990b5c18fed9434ba2ab267aa5a9bb6c9e88bcfa1de94429ca6767a`.
The build helper can create a small dynamic-registration wrapper for that
release's Hexagon backend. Keep native ggml libraries first in `LD_LIBRARY_PATH`;
loading two copies causes an initialization assertion.

GenieX itself loaded Qwen successfully, but its API differs from llama.cpp and
does not expose the probability contract required here. The isolated native
server preserves the same tool-call interface and Reason2 log-probability scorer
as the host. No global runtime was replaced. The decoder selects HTP0 explicitly;
the adapted F16 image encoder currently runs on CPU. Q8 kernels are present in
this pinned Hexagon implementation; actual device testing is required even though
[GenieX's general precision guidance](https://github.com/qualcomm/GenieX/blob/v0.6.1/docs/en/models/supported.mdx)
recommends Q4_0 for NPU use. Qwen Q4_K_M currently uses CPU. OpenCL has no configured
vendor ICD, and the SSH user lacks access to the Vulkan render node; no GPU driver
or permission changes were made.

Disable the server RAM prompt cache with `--cache-ram 0` in addition to
`--no-cache-prompt` for Reason2. The native server otherwise defaults to an 8 GiB
RAM cache, despite its 1024-token context. An initial two-backend run was killed
by the global OOM killer after 31 successful thermal classifications; five
subsequent requests failed. That failed run is retained as evidence, not a pass.

The EVK has 34.37 GiB RAM. Before reboot, approximately 24.8 GiB was trapped in
unreclaimable kernel `skbuff` slabs. The user authorized EVK reboots at will and
retired the conveyor service for now. After disabling it and rebooting, available
RAM was **33.27 GiB**, up from 1.81 GiB with Reason2 loaded before reboot.
Qwen plus both adapted Reason2 bundles now load together; active measurements
leave at least 21.49 GiB available. The agent service cap is 9 GiB, each Reason2
backend 4 GiB, and the Python router 512 MiB. Cgroup figures alone omit some
shared/device allocations; use system-wide available memory as well.

The launcher requires 18 GiB available before combined startup, or 7 GiB for
Reason2 only. These are startup guards, not claimed model footprints. Schedule
controller and image inference serially while keeping all models resident.

## Commands on the prepared device

The current selected backend is the [compiled NPU vision/text service](evk_qairt_experiment.md):
`bash ~/qai-dark-factory/repo/scripts/model/start_evk_qairt.sh --agent` on the EVK.
It uses the same 18198 endpoint and keeps Qwen on 18195. The commands below retain
the older CPU-vision baseline for explicit comparisons; stop
`dark-factory-edge-qairt` before starting that older stack.

Builds and exports remain private. Copy the repository's code/configuration to
`~/qai-dark-factory/repo`, and place verified bundles in
`~/qai-dark-factory/models/{primary-960,weld-896}`. Each directory contains the
Q8_0 text GGUF, matching F16 mmproj and `edge-q8-provenance.json`.

```bash
# On the EVK, after staging the verified GenieX v0.6.1 archive:
DF_GENIEX_061_DIR=$HOME/qai-dark-factory/runtime/geniex-v0.6.1 \
  bash ~/qai-dark-factory/repo/scripts/model/build_evk_llama.sh

# Start only this project's Reason2 services. No boot autostart is installed.
bash ~/qai-dark-factory/repo/scripts/model/start_evk_stack.sh
curl http://127.0.0.1:18198/health

# For all three models, use this instead on a stopped stack:
# bash ~/qai-dark-factory/repo/scripts/model/start_evk_stack.sh --agent
```

The router warms all four stages before reporting ready. Stop this project's
services with:

```bash
systemctl --user stop dark-factory-edge-primary dark-factory-edge-weld dark-factory-edge-router
# If the optional agent was started:
# systemctl --user stop dark-factory-edge-agent
```

The prepared PC now has a WSL user service maintaining the router tunnel. Start
it after a WSL restart with:

```powershell
wsl.exe bash scripts/model/start_evk_tunnel.sh
```

`DF_EVK_SSH_HOST` overrides the SSH destination. Stop the tunnel with
`wsl.exe systemctl --user stop dark-factory-edge-tunnel`. Alternatively, when that
service is stopped, forward it manually and leave the terminal open:

```powershell
wsl.exe ssh -N -o ExitOnForwardFailure=yes -L 127.0.0.1:18198:127.0.0.1:18198 ubuntu@192.168.1.158
```

To repeat the real Qwen / synthetic-tool suite while the EVK agent unit is loaded,
forward its separate loopback port in another terminal:

```powershell
wsl.exe ssh -N -o ExitOnForwardFailure=yes -L 127.0.0.1:18195:127.0.0.1:18195 ubuntu@192.168.1.158
```

Then run from the repo (use a fresh private output path):

```powershell
wsl.exe python3 scripts/agent/experiment.py --endpoint http://127.0.0.1:18195/v1 --output artifacts/evk-agent-retest.json --timeout 120
```

This timeout applies to controller benchmarking only. It does not change Reason2
capture freshness. Keep this suite separate from an active Unreal EVK model run
so CPU Qwen does not compete with Reason2's CPU vision encoder.

The explicit Unreal backend selection is:

```powershell
.\unreal\Scripts\run.ps1 -Process -Enhancements `
  -ProcessEndpoint http://127.0.0.1:18198/v1/inspect -ProcessModelSource evk_hexagon
```

Host GPU remains the launcher default. Device selection changes neither sensor
encoding nor gate thresholds. The overall EVK `ProcessSmoke` budget is 480 s
instead of 240 s; each request still has the original **8 s capture-age limit**.
Late, invalid or unavailable results hold. A slower device must not be made to
pass by extending evidence freshness or lowering confidence.

Use `probe_edge_reason2.py` for independent offline device comparisons. It accepts
only loopback endpoints, sends only images and the versioned stage prompt, keeps
labels on the client, records the backend alias and hashes, and evaluates only
the explicit validation split. Its longer transport timeout is an offline
measurement facility, not permission to use stale evidence in the demo.

Private evidence is under `artifacts/agent-evk-20260918/`. No models, captures,
runtime binaries or purchased geometry are published.

## Single-image acceleration experiment — 19 September 2026

`probe_edge_reason2.py --single-view` splits an existing three-camera batch into
three serial one-image, one-output-token requests. It uses explicit single-view
prompts in `src/dark_factory/gates_single_view.py`, with separate prompt hashes
and experiment version `automotive-panel-single-view-experiment-v1`. The existing
adapter provenance is checked as the source of the weights, not as proof that
these changed prompts or single-image inputs are compatible. Only images and the
new stage prompt enter the model; source labels remain on the offline evaluator.

Presence needs three agreeing G or three agreeing R results; disagreement holds.
Other stages require all G to return G, while any confident R returns R. Missing,
misordered or failed views hold. Batch labels are never copied into individual
view ground truth. This is an offline experiment, not a new live permission path.

Actual IQ9 tests of four saved stage requests produced:

| Path | Median request | Median complete three-view inspection |
|---|---:|---:|
| Existing HTP decoder / CPU vision | 4.804 s | 4.804 s |
| Split one-image calls, HTP decoder / CPU vision | 1.767 s | 5.356 s |
| Split one-image calls, HTP decoder / HTP vision | Failed | No completed inspection |

These small latency probes are not held-out accuracy measurements. Sealant,
presence and weld labels agreed between the two CPU-vision paths. Seating changed
from G in the original batch to R in all three single-image calls. Do not promote
the changed prompts/inputs using the earlier adapter accuracy claims.

The isolated HTP-vision server selected `CLIP using HTP0 backend` and aborted in
`clip_encode` on its first single-image request: FastRPC `0x8000040d`,
`dspqueue_read failed: 0x0000002e`. Repeating with flash attention disabled also
failed; that variant additionally reported excessive dummy-warmup allocation and
unsupported backend operators. This does not identify the exact failing kernel.
Splitting HTTP requests does not fix the accelerator fault: the original
three-view backend already encodes the images sequentially. Both temporary
servers were removed; the deployed CPU-vision services remain available.

To reproduce the split CPU-vision comparison on the prepared EVK:

```bash
python3 ~/qai-dark-factory/repo/scripts/model/probe_edge_reason2.py \
  --request-dir /path/to/private/requests --single-view --stop-on-error \
  --primary-provenance ~/qai-dark-factory/models/primary-960/edge-q8-provenance.json \
  --weld-provenance ~/qai-dark-factory/models/weld-896/edge-q8-provenance.json \
  --device-description IQ9-HTP0-single-image-CPU8-vision \
  --output /path/to/private/single-view.json
```

Omit `--single-view` for the unchanged baseline on exactly the same images.
Use separate isolated raw servers for acceleration experiments; do not alter the
live router. The tested HTP variant appended `--mmproj-offload --mmproj-device
HTP0` to `start_edge_reason2.sh` on private port 18199. The second variant also
appended `--flash-attn off`. Keep the paired adapted F16 projector and Q8 decoder,
112 image tokens, original grayscale PNGs and probability thresholds unchanged.
Private reports and failure logs: `artifacts/single-view-20260919/`.

The newer [compiled QAIRT runtime](evk_qairt_experiment.md) moves both vision
and text to the NPU and preserves full-vocabulary confidence. Initial W8A16
vision candidates failed the development accuracy bar. FP16 vision recovers
191/192 with no false G or wrong NIR decisions at 1.41 s median per three-view
inspection. This version is now selected; the earlier 4.85 s CPU-vision service
remains an explicit fallback. See the linked guide for measured limitations.
