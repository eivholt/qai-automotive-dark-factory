# Ministral NPU candidates and Reason2 contention

September 19, 2026 development record. The dynamic Hexagon candidate executes
Ministral decoder work on the IQ9 NPU while retaining its tied embedding/output
head on CPU. This is a new quantization and backend, not the existing Q4_K_M CPU
model with identical numerical behavior. Both actual three-body runs failed
the integration audit. A holder-clarified model prompt passed 41 offline checks
but produced four audit failures in the subsequent live run. Neither the
dynamic candidate nor that prompt change is promoted. No result here establishes
held-out accuracy or industrial process safety.

A separate SDK 2.50 compiled candidate now builds and runs on the EVK. It
passes the bounded reset/incremental checks and one actual three-body live run
recorded below. Full-prefill and warm append contention are measured. The CPU
controller remains the default; the compiled pass does not rehabilitate the
rejected dynamic candidate or establish new held-out accuracy.

Reason2 remains the actual three-camera inspector, with the unchanged
automotive-panel-process-v3 sensor/reply contract, shared960 primary/shared896
weld routing, confidence 0.98 and label mass 0.5. The agent has no recipe veto,
ground-truth access or automatic action repair. Mechanical, syntax, epoch and
freshness checks still apply. See [live agent control](live_agent_control.md).

## Dynamic candidate provenance

The exact current Q4_K_M contains Q4_K/Q6_K matrices unsupported by the pinned
Hexagon matrix path. Instead, the publisher's BF16 GGUF was downloaded and
directly quantized to a Q4_0 decoder with a Q8_0 tied embedding/head. The helper's
explicit Q8_0-source fallback would be a separately recorded requantization; it
was not used for this candidate.

| Input or output | Pinned identity |
| --- | --- |
| Publisher GGUF repository | `mistralai/Ministral-3-3B-Instruct-2512-GGUF` |
| Publisher revision | `eb599d408350ea2bb60452cb86be7c7b2fc28227` |
| BF16 source | `Ministral-3-3B-Instruct-2512-BF16.gguf`, 6,866,745,504 bytes |
| BF16 SHA-256 | `17ef932bea952e007f9dad63151da5699132ec513d1033d618df7382e24aa3ee` |
| Quantizer llama.cpp revision | `910196f6b3dfc6aca88fa732e2b02f270ff9b56b` |
| Candidate file | `Ministral-3-3B-Instruct-2512-decoder-Q4_0-head-Q8_0.gguf`, 2,139,109,536 bytes |
| Candidate SHA-256 | `c7da75b7ef88ab2e83cb5065b92d8e2c01aa2a8453738fb86140e970da17c014` |
| Original Q4_K_M CPU comparison SHA-256 | `9ed150d4367e68df0ac8e1540f6ddc65b42d0ee26378329d1ecbca60f93fc5f8` |
| Dynamic runtime llama.cpp revision | `0eadefebd3f8f92a86d634a0e5b8fffc9dc792c0` |
| Runtime source archive SHA-256 | `19e38c290e4e97784c77e393e4b60593c84ff7c9c5c376d16dea6d459172e0fd` |
| GenieX runtime | `v0.6.1`, ARM64 Linux / Hexagon v73 |
| GenieX archive SHA-256 | `985c13348990b5c18fed9434ba2ab267aa5a9bb6c9e88bcfa1de94429ca6767a` |

The private `candidate.json` is the authoritative quantizer revision and binary
hash record. It also records every tensor shape/type, source/output hashes and
architecture/tokenizer metadata fingerprints. The output retains all 236
tensors: 182 Q4_0, one Q8_0 and 53 F32. The original tokenizer and architecture
metadata are checked unchanged. Preparation's `inference_validated: false`
describes the preparation step; runtime evidence is recorded separately.

Prepare on the Linux host with a clean checkout of the recorded quantizer
source and its built `llama-quantize` binary. These commands create private
weights and provenance without inference:

```bash
python3 scripts/agent/prepare_hexagon_ministral.py \
  --root /private/models/ministral-hexagon-bf16-q40-20260919 \
  --download-source --source-kind bf16 \
  --quantizer /private/llama-quantizer/build/bin/llama-quantize \
  --quantizer-source /private/llama-quantizer --threads 8
```

The recorded quantizer invocation uses `--pure --token-embedding-type q8_0
--output-tensor-type q8_0` and output type `Q4_0`; BF16 is not requantized from
the deployed Q4_K_M. The helper verifies the downloaded immutable source hash,
refuses incomplete/existing unproven output, and writes `quantize.log` and
`candidate.json`. Generated weights, runtime binaries and logs stay out of Git.

## EVK launch and placement

Build the isolated runtime with `scripts/model/build_evk_llama.sh`, using
`DF_GENIEX_061_DIR` for the matching GenieX directory. Keep it under
`~/qai-dark-factory/runtime`. The candidate launcher verifies either clean
pinned Git source or the pinned official archive plus every extracted source
member; the EVK's archive installation was verified across 3,515 members.
Generated build files do not substitute for the source check.

On the prepared ARM64 EVK, use an otherwise free candidate port:

```bash
export DF_HEXAGON_CANDIDATE_DIR="$HOME/qai-dark-factory/models/ministral-hexagon-bf16-q40-20260919"
export DF_HEXAGON_CANDIDATE_CTX=4096
bash scripts/agent/start_hexagon_candidate.sh --check
bash scripts/agent/start_hexagon_candidate.sh
```

`--check` only verifies files, hashes and headers. The second command starts a
foreground native server on `127.0.0.1:18205`; it does not replace normal
services. It uses HTP0, all eligible layers, `token_embd.weight=CPU`, one slot,
batch 256/ubatch 128, eight CPU threads, flash attention, `GGML_HEXAGON_MBUF=256`
and no RAM slot-cache pool. Each launch records its arguments, source check and
runtime library hashes. The diagnostic context is 4,096 tokens; the measured
real prompts are below 2,000 tokens. A larger context needs separate validation.

The actual journal records an HTP0 v73 session, 1,624 MiB of HTP0 model buffers,
416 MiB of HTP0 KV buffers, 19.5 MiB of HTP0 compute buffers and three graph
splits. CPU and CPU_REPACK each report a 408 MiB model buffer. The tied
131,072-row embedding/head exceeds this backend's supported quantized matrix
row limit and is explicitly placed on CPU. The aggregate loader message
`offloaded 27/27 layers` is not evidence that every operation or the output
projection runs on NPU. No per-kernel utilization or overlap profile was
collected. Logs report real HTP allocations and execution; they do not establish
an entirely NPU-resident graph.

The pinned source audit confirms that `mistral3.cpp` duplicates the tied output
tensor under the embedding's name, so the `token_embd.weight=CPU` override also
applies to the output projection. The aggregate layer counter ignores those
individual tensor overrides. The buffers and three splits are consistent with
CPU embedding, HTP decoder work and CPU output projection; there is no per-op
trace establishing every remaining operator's placement or absence of CPU
fallback. In particular, this evidence does not locate YaRN execution.

The observed device snapshot retained about 17 GiB free with the CPU backup
and both NPU roles resident. This is one measured resident configuration, not
a maximum concurrency or memory guarantee.

## Prompt versions and protocol adapter

The base `configs/live_agent.json` bytes remain pinned at SHA-256
`c47aa7776b32f7001fced0c7b524e92fec74047b1ecf9dfd182e252864eb477a`.
The one-token adapter translates the actual tool conversation into a native
prompt and the chosen A–M token back into one tool call. Every action remains
available. It does not insert expected results, select from a filtered menu,
invent a future observation or run physical jobs itself.

| Version | One-token prompt SHA-256 | Evidence |
| --- | --- | --- |
| Original/reference | `a6bb5d377bec78f47c6264e2b25b779e6ce39a23b557d467e58a80bb12ebdf3c` | Contention measurements and failed first live run below |
| Holder clarification, rejected experiment | `64e0053428241a7643a773fb0108e953ef1884fc1f1f0f1976aa7f24cf66fa37` | 41/41 offline checks; subsequent live run failed with four audit failures |

The clarification experiment added an instruction only in `single_token.make_prompt`:
before `send_forward`, the latest completed tool result must report the holder
released; waiting or a cool inspection does not retract it. It is not a
host-side repair rule. It is retained as a failed experimental record, not
promoted to the default. The original base configuration and incoming request
contract are unchanged. The 41 checks comprise assembly 9, preassembled 4,
faults 4, departure 16 and varied holder-release histories 8. They are
development checks, not held-out accuracy or a substitute for live integration.

The historical measurements must explicitly use the saved original prompt;
rerunning them with the clarification would be a different experiment. Extract
and hash-check it from the private original probe artifact without changing the
base config:

```bash
python3 - <<'PY'
import hashlib, json
from pathlib import Path
assert hashlib.sha256(Path('configs/live_agent.json').read_bytes()).hexdigest() == \
    'c47aa7776b32f7001fced0c7b524e92fec74047b1ecf9dfd182e252864eb477a'
root = Path('artifacts/ministral-npu-contention-20260919')
prompt = json.loads((root / 'dynamic-hexagon-assembly.json').read_text())['prompt']
assert hashlib.sha256(prompt.encode()).hexdigest() == \
    'a6bb5d377bec78f47c6264e2b25b779e6ce39a23b557d467e58a80bb12ebdf3c'
(root / 'original-single-token-prompt.txt').write_bytes(prompt.encode())
PY
python3 scripts/agent/serve_single_token.py \
  --backend http://127.0.0.1:18205 --port 18197 \
  --config configs/live_agent.json \
  --prompt artifacts/ministral-npu-contention-20260919/original-single-token-prompt.txt \
  --benchmark-reset-cache
```

Use a free port or explicitly stop only the preceding candidate before starting
another adapter. `--benchmark-reset-cache` is offline measurement mode: it sends
`cache_prompt=false` for every decision and is incompatible with saved policy
cache and next-turn prefill. Omit it for live integration. Check `/health` for
the prompt/config hashes and cache mode before interpreting any report.

## Saved-input contention measurements

The input envelopes retain actual final-body history from the private
`agent-prefill-20260919/evk-overlap-process` run; classifier request bytes are
copied verbatim. Replay retains those bytes, including identifiers/timestamps,
and supplies no expected labels to either model. It produces no new physical
actions or fresh process evidence. The sequential mode first requires the
agent to choose the matching inspection, then sends that saved inspection.
The simultaneous mode intentionally submits two independent saved requests as
a stress/throughput experiment, not an actual causal agent-to-inspector flow.

With the original-prompt full-prefill proxy on 18197 and the existing Reason2
router available on 18198, run from the repository's configured Python
environment, using loopback tunnels if benchmarking on the host:

```bash
PYTHONPATH=src python3 scripts/agent/benchmark_npu_contention.py \
  --agent-endpoint http://127.0.0.1:18197 \
  --classifier-endpoint http://127.0.0.1:18198 \
  --agent-payload artifacts/ministral-npu-contention-20260919/panel_presence-agent-request.json \
  --classifier-payload artifacts/ministral-npu-contention-20260919/panel_presence-classifier-request.json \
  --repeats 5 --warmup 1 --timeout 60 \
  --output /private/new-npu-full-prefill-panel_presence.json
```

Use the corresponding `weld-*` input files for the weld replay. The benchmark
keeps at most one outstanding request per endpoint, bounds response size/time,
checks response errors and action/result codes, and stops on transport failure.
It records per-call latency, p50/p95, pair makespan, slowdown and actual Reason2
decision stability. One warmup trial per mode is reported separately; each
table entry below summarizes five measured trials. Percentiles use linear
interpolation and are descriptive for this small sample.

Dynamic NPU agent full-prefill results, milliseconds (p50 / p95):

| Saved stage / mode | Agent HTTP | Reason2 HTTP | Pair makespan |
| --- | ---: | ---: | ---: |
| Presence, agent only | 2192.6 / 2196.7 | — | — |
| Presence, classifier only | — | 1228.9 / 1232.7 | — |
| Presence, sequential | 2181.1 / 2184.0 | 1225.6 / 1238.6 | 3407.4 / 3422.0 |
| Presence, simultaneous stress | 2183.5 / 2189.9 | 3262.5 / 3272.9 | 3263.0 / 3273.7 |
| Weld, agent only | 2926.5 / 2946.3 | — | — |
| Weld, classifier only | — | 1408.4 / 1411.1 | — |
| Weld, sequential | 2930.3 / 2947.4 | 1402.0 / 1409.4 | 4332.3 / 4356.7 |
| Weld, simultaneous stress | 2923.9 / 2926.9 | 4181.4 / 4208.5 | 4182.0 / 4209.1 |

The native timing records show `cache_n=0` and `prompt_n=1021` for presence,
and `cache_n=0` / `prompt_n=1291` for weld. Under simultaneous HTTP submission,
Reason2 median latency increases 2.655× and 2.969× relative to its respective
classifier-only baseline. Agent median latency remains about 1.00×. Median
pair makespan improves only 1.044× / 1.036× versus sequential replay. This is
evidence of contention at the measured workload; HTTP overlap does not establish
simultaneous accelerator-kernel execution.

For comparison, the existing CPU-agent/NPU-Reason2 replay retained its normal
prompt-cache behavior. It used identical input hashes but a different agent
quantization, backend and cache regime:

| Saved stage | CPU agent-only p50 / p95 | Reason2-only p50 / p95 | Sequential pair p50 / p95 | Simultaneous pair p50 / p95 | Simultaneous Reason2 p50 slowdown |
| --- | ---: | ---: | ---: | ---: | ---: |
| Presence | 284.1 / 286.5 | 1202.7 / 1211.9 | 1288.9 / 1335.3 | 1187.5 / 1195.1 | 0.987× |
| Weld | 405.9 / 446.0 | 1333.0 / 1347.7 | 1508.1 / 1517.5 | 1364.9 / 1370.7 | 1.023× |

Repeated identical prompts warm the CPU cache: its agent medians decrease to
98.6/145.0 ms during sequential presence/weld replay. Those times must not be
presented as a CPU-versus-NPU full-prefill speedup. The NPU table deliberately
forces full prefill; neither table measures matched growing-prefix cache reuse.
The CPU agent-only warmups were 1510.4 ms and 5183.1 ms; the corresponding NPU
warmups were 2184.3 ms and 2934.1 ms. All per-mode warmups remain in the reports.

All four reports completed without errors. Reason2 stayed R on presence and G
on weld across classifier-only, sequential and simultaneous modes: 15 measured
calls plus three separate warmups per report. This is decision stability on
repeated saved inputs, not new accuracy evidence. Reports are private:
`npu-full-prefill-panel_presence.json`, `npu-full-prefill-weld.json`,
`cpu-npu-presence-baseline.json` and `cpu-npu-weld-baseline.json` beneath
`artifacts/ministral-npu-contention-20260919/`.

## Actual integration and pending validation

The original-prompt dynamic run lasted 184.904 seconds, with 31 agent choices
and 15 real Reason2 batches. Median agent latency was 269.1 ms and median
Reason2 latency 1414.8 ms. On the first assembly body, the model selected
`send_forward` before releasing the holder. The mechanical check rejected that
action; the model then chose `release_holder` and retried departure. The run
therefore **failed** its audit despite recovering and completing the sequence.
Its record is `dynamic-live-audit.json`, with actual history and captures in
the private `dynamic-live/` directory. Do not describe it as a passed live test.

The clarified prompt also **failed** actual three-body integration, despite
41/41 offline checks. Its final `dynamic-clarified-live/process_runtime.json`
records 185.689 seconds, 31 choices, 16 Reason2 batches, zero mechanically
rejected calls and four independent audit failures. All three bodies departed,
but that does not satisfy the process audit. The final
`dynamic-clarified-live-audit.json` reports median agent/Reason2 latency of
269.0/1415.2 ms and `passed: false`; `dynamic-clarified-live-health.json` pins
the clarified prompt and unchanged base config. On the first assembly body:

- The model inspected seating before welding. Welding invalidated that evidence.
- It then executed `release_holder` at an independently recorded apparent weld
  temperature of 194.368°C, without post-weld seating or cool-weld confirmation.
  The audit recorded `missing_seating_confirmation`,
  `missing_cool_weld_observation` and `release_while_weld_hot`.
- After later weld inspections returned R then G, it executed `send_forward`
  without obtaining a post-weld seating inspection. The fourth failure was
  `missing_seating_confirmation`.

These are audit findings about actual executed choices, not mechanical
rejections or an evaluator that repaired the flow. The independent evaluator's
temperature/source state was not sent to the agent. The failed clarified prompt
is not promoted; the original base configuration remains unchanged.

## Compiled SDK 2.50 candidate

The separate [compiled HTP exporter](ministral_qairt_export.md) completed with
SDK 2.50.40.260831, superseding the earlier SDK 2.47 parser failure on
`mistral3`. It starts from the pinned original Q4_K_M source, preserves the
official tokenizer and explicitly handles Q/K permutation and 4,096-token YaRN
behavior. Export took 27 minutes 47 seconds and produced a 3.417 GB finalized
bundle with two HTP context binaries, each containing AR1 and AR64 graphs. Graph
metadata/build identities were inspected and every transferred bundle file
passed its SHA-256 manifest. Actual EVK dialog creation and inference succeeded
with Genie API 1.20.0 and matching SDK 2.50.40 libraries.

This also has distinct numerical behavior from native Q4_K_M. The exporter
converts source Q4_K projections to QNN four-bit block encodings, source Q6_K
projections/tied output to eight-bit encodings, and the input embedding to
FP16. Activations, logits, KV and rotary inputs are FP16. A float32 callback
buffer does not restore precision lost in the graph. The exporter guide records
the detailed encoding audit; neither source provenance nor prompt-token parity
establishes bitwise GGUF execution.

The running worker matched all 22 captured native prompt token arrays, including
exactly one BOS and A–M IDs 1065–1077. It computes the actual unmasked global
vocabulary winner and full-vocabulary action probabilities, then requires the
native emission to agree. It does not apply an action mask, grammar, logit bias,
recipe veto, expected label or fallback action. The one-token prompt remains the
original `a6bb5d...`, with base config `c47aa777...`; the failed holder
clarification is absent.

Completed runtime checks:

| Check | Measured result |
| --- | --- |
| Reset assembly history | 9/9 expected choices; median HTTP 4593.2 ms |
| Reset preassembled history | 4/4; median HTTP 4234.8 ms |
| Reset fault probes | 4/4; median HTTP 4789.6 ms |
| Reset departure probes | 16/16; median HTTP 4451.9 ms |
| Same saved growing assembly history in append mode | 9/9 choices agree with reset; first call resets, eight calls append |
| Eight actual append calls | Median HTTP 254.2 ms; submitted suffixes 30–60 tokens |
| Reset-versus-append action probability difference | Maximum absolute difference 0.0134879 |

The reset suites total 33/33. The same-history reset median is 4593.2 ms across
nine decisions; the append median excludes its initial full-reset call. This
bounded cache comparison is distinct from identical-prompt contention replay.
It compares returned action probabilities and top-128 scores, not complete
logit-vector equality or a hardware cache trace. All nine choices agree, but
the probability difference prevents a bitwise equivalence claim.

The earlier dynamic candidate's actual premature-departure history now selects
`H` (`release_holder`) in full-reset compiled inference, with probability
0.8956 versus the native CPU reference's 0.9004. The initial arrival selects
`A`. Correcting this one saved failure case is a measured decision comparison,
not proof that all future histories are correct.

Append mode retains context only when the new full input exactly extends the
previous evaluated input and its first new token is the previous sampled
winner. It submits the full new suffix, including that unevaluated winner, and
checks native context occupancy before and after inference. New bodies,
changed prefixes and repeated identical prompts reset. No future observation
or selected action is precomputed. `reused_tokens_measured` remains null because
retained occupancy is not direct hardware evidence of saved computation.

The compiled resident snapshot recorded 16,248,012,800 bytes available
(about 15.1 GiB) with the CPU fallback and Reason2 still resident. This is a
separate snapshot from the dynamic candidate's approximately 17 GiB result.
The HTP v73 library and compiled contexts loaded successfully; process RSS alone
does not account for all shared or DSP allocations.

Private evidence beneath `artifacts/ministral-npu-contention-20260919/`:
`genie-first-check.json`, `genie-reset-{assembly,preassembled,faults,departure}.json`,
`genie-cache-parity.json`, the reset/append health snapshots and
`genie-placement-and-memory.txt`. These are development checks, not new held-out
accuracy. Fake-ABI protocol and shutdown tests independently cover transport
behavior; they do not substitute for these actual runtime checks.

### Reproduce the isolated compiled runtime

Use the finalized `genie` bundle from the exporter, not a bare container
re-export. Keep matching SDK 2.50.40 ARM64 Genie/QNN host libraries and unsigned
v73 DSP libraries separate from Reason2's SDK and from GenieX. The EVK requires
the native compiler, OpenSSL development headers, nlohmann headers and Python
Jinja2. From the repository directory, with paths adapted to the private files:

```bash
DF_QAIRT_SDK=/private/sdks/qairt/2.50.40.260831
DF_GENIE_LIB="$DF_QAIRT_SDK/lib/aarch64-oe-linux-gcc11.2"
DF_GENIE_BUNDLE=/private/models/ministral-qairt250-yarn4096/genie
DF_GENIE_WORKER=/private/build/genie_single_token_worker
DF_NATIVE_PROPERTIES=/private/references/native-props.json
mkdir -p /private/build
bash scripts/agent/build_genie_single_token_worker.sh \
  "$DF_QAIRT_SDK" "$DF_GENIE_WORKER" "$DF_GENIE_LIB"
(cd "$DF_GENIE_BUNDLE" && sha256sum -c SHA256SUMS)
DF_GENIE_TOKENIZER="$(python3 - "$DF_GENIE_BUNDLE" <<'PY'
import json, sys
from pathlib import Path
bundle = Path(sys.argv[1]).resolve()
config = json.loads((bundle / 'genie_config.json').read_text())
tokenizer = (bundle / config['dialog']['tokenizer']['path']).resolve(strict=True)
assert tokenizer.is_relative_to(bundle)
print(tokenizer)
PY
)"
env LD_LIBRARY_PATH="$DF_GENIE_LIB" \
  ADSP_LIBRARY_PATH="$DF_QAIRT_SDK/lib/hexagon-v73/unsigned" \
  python3 scripts/agent/serve_genie_backend.py \
  --worker "$DF_GENIE_WORKER" \
  --bundle-config "$DF_GENIE_BUNDLE/genie_config.json" \
  --tokenizer "$DF_GENIE_TOKENIZER" --qairt-lib "$DF_GENIE_LIB" \
  --template-properties "$DF_NATIVE_PROPERTIES" \
  --port 18206 --cache-mode append
```

`native-props.json` must be the captured source GGUF's native server properties,
including its exact chat template and boundary tokens. The HTTP server starts
and owns the native worker; do not launch another worker against the same test
at the same time. `--cache-mode reset` selects the full-reset comparison instead
of the explicitly requested experimental append mode. Health records the loaded
config, effective config, tokenizer, context/worker/library identities and cache
mode. SIGTERM and fatal native protocol errors stop and reap the child.

In a separate terminal, expose the one-token tool adapter on a free candidate
port, using the hash-checked original prompt extracted earlier in this document:

```bash
python3 scripts/agent/serve_single_token.py \
  --backend http://127.0.0.1:18206 --port 18197 \
  --config configs/live_agent.json \
  --prompt artifacts/ministral-npu-contention-20260919/original-single-token-prompt.txt
```

The native Genie HTTP API is on 18206; the OpenAI-shaped tool adapter is on
18197. This does not install or replace normal services. Ensure the previous
dynamic proxy is not occupying 18197. Do not enable the CPU-specific persisted
policy cache or next-turn prefill for this candidate. Keep ordinary QA request
logging disabled except during an explicitly bounded private fixture.

### Completed compiled live integration

The actual rendered assembly/preassembled/assembly run **passed** in 185.940 s,
with 28 agent choices and 14 real Reason2 batches. No calls were mechanically
rejected and the independent evaluator recorded no audit failures. The original
prompt `a6bb5d...`, unchanged base configuration and actual tool observations
were used throughout; there was no recipe veto or output repair.

| Measurement | Compiled candidate, append enabled | Earlier selected CPU, cache + overlap |
| --- | ---: | ---: |
| Three-body wall time | 185.940 s | 170.429 s |
| Median agent latency | 269.146 ms | 1416 ms |
| First agent decision | 3852.963 ms | 1703 ms |
| Median Reason2 latency | 1332.105 ms | 1415 ms |
| Agent choices / inspection batches | 28 / 14 | 22 / 11 |
| Rejected calls / independent audit failures | 0 / 0 | 0 / 0 |

The maximum compiled-agent latency was 3853.599 ms. Faster median decisions did
not shorten the full cycle. The compiled run
performed three extra cooling inspections with intervening waits: a weld retry
on the first assembly body and sealant/weld retries on the third. Decision time
affects when the cameras see the evolving thermal state, so model latency and
whole-cycle throughput must be reported separately. These runs also use
different cache implementations and are not an isolated matched-kernel test.

All 28 selected unmasked vocabulary winners matched the actual model-visible
history and dispatched tool. The physical-camera audit passed 2,726 telemetry
checks over 42 recorded captures; that audit did not read PNG pixels and does
not certify all intermediate motion or occlusion. The evidence is
`genie-live-audit.json`, `genie-live-token-audit.json`,
`genie-live-camera-audit.json` and the actual private `genie-live/` runtime and
captures under the experiment directory. This is one three-body development
integration, not new held-out accuracy or a guarantee across future histories.

### Compiled full-prefill contention

The two compiled full-prefill reports completed with five measured trials and
one separate warmup per mode, using the same saved presence/weld requests as
the earlier comparisons. Genie ran in reset mode. Every measured agent call's
server timings report `cache_mode=reset`, `retained_context_tokens=0` and a
submitted prompt equal to the full 1021/1291-token input. The replay has the same
bounded, one-request-per-endpoint discipline described above and makes no
physical actions. Simultaneous submission remains an independent stress test,
not a causal agent-inspection sequence.

Milliseconds, p50 / p95:

| Saved stage / mode | Compiled agent HTTP | Reason2 HTTP | Pair makespan |
| --- | ---: | ---: | ---: |
| Presence, agent only | 3824.9 / 3837.9 | — | — |
| Presence, classifier only | — | 1156.6 / 1177.7 | — |
| Presence, sequential | 3824.3 / 3841.4 | 1156.2 / 1161.8 | 4980.5 / 5002.9 |
| Presence, simultaneous stress | 4786.1 / 4815.4 | 3262.1 / 3283.3 | 4786.7 / 4816.1 |
| Weld, agent only | 5023.2 / 5077.9 | — | — |
| Weld, classifier only | — | 1313.8 / 1341.0 | — |
| Weld, sequential | 5016.8 / 5068.2 | 1315.6 / 1337.4 | 6345.9 / 6393.3 |
| Weld, simultaneous stress | 6138.5 / 6216.7 | 3907.6 / 3956.3 | 6139.2 / 6217.5 |

Both participants slow under simultaneous submission. The compiled agent's
median latency rises 25.1% for presence and 22.2% for weld; Reason2 rises
2.820× and 2.974×. This differs from the dynamic candidate, whose agent timing
was approximately unchanged while Reason2 slowed. Sequential/concurrent median
pair makespan ratios are only 1.0405× and 1.0337×, or 4.05% and 3.37% pair
speedup. HTTP overlap is measured; the reports do not establish simultaneous
kernel execution or a benefit to the actual causal process flow.

Reason2 remains R for presence and G for weld across all 15 measured classifier
calls per report, with three separately reported warmup decisions also stable.
There are no failed calls or aborted trials. This is saved-input stability,
not accuracy. Agent-only warmups were 3858.5/5053.3 ms; the reports retain all
other warmups. Private evidence is `genie-full-prefill-panel_presence.json` and
`genie-full-prefill-weld.json` beneath the same experiment directory.

### Compiled warm append contention

A separate bounded private replay measured the saved weld decision after
preparing its actual preceding history. Each trial first ran an untimed prime
ending before the saved final assistant action and required the model to choose
that same action before appending its real recorded result. It did not invent
a future observation. Sixteen primes covered three measured trials and one
excluded warmup for each of the four modes, including classifier-only trials.
The measured agent and Reason2 request bytes remained exactly unchanged.
There were no physical actions or fresh process evidence.

Every measured agent call reports native `cache_mode=append`, 1,231 retained
tokens, 60 submitted suffix tokens and final occupancy/full prompt length of
1,291. This establishes the requested runtime append behavior rather than an
identical-prompt reset. The cost of the 16 full-history primes is excluded from
the timed calls, so this table is conditional on an already prepared history,
not an end-to-end cold-workload throughput measurement.

Weld replay milliseconds, p50 / p95, three measured trials per mode:

| Mode | Compiled agent HTTP | Reason2 HTTP | Pair makespan |
| --- | ---: | ---: | ---: |
| Agent only | 252.14 / 294.25 | — | — |
| Classifier only | — | 1316.15 / 1319.90 | — |
| Sequential | 253.57 / 294.17 | 1316.69 / 1322.36 | 1570.28 / 1616.54 |
| Simultaneous independent stress | 364.73 / 372.71 | 1480.55 / 1498.50 | 1481.55 / 1499.17 |

Simultaneous submission raises agent median latency 44.65% and Reason2 latency
12.49%. The sequential/concurrent pair ratio is 1.0599×, corresponding to only
5.65% less median pair wall time. All nine measured agent choices remain
`inspect(weld)` and all nine measured Reason2 results remain G. The three
separately reported classifier warmups also remain G. No trial failed or
aborted. With only three measured trials per mode, interpolated p95 values are
descriptive, not a reliable estimate of tail behavior.

A checksum audit ran at 18:57:40–18:57:43 UTC on September 19. It overlapped only
the excluded first warmup and its untimed prime. The first measured action
completed at 18:57:48 UTC; all measured calls occurred after the audit. This
timing distinction is retained in `warm-run-prime-utc-journal.txt`; all three
measured trials per mode are retained in the summaries. The private
`genie-warm-append-weld/report.json` records source/request/runner hashes, all
primes, warmups and measured calls.

Keep dependent NPU work sequential: the agent chooses an inspection, Reason2
performs that inspection, and its actual result becomes available for the next
choice. Warm independent sharing offers a modest pair-time gain while slowing
both calls; heavy full prefill substantially delays Reason2. These HTTP
measurements do not establish simultaneous kernels or justify inventing or
speculating on future inspection results.

At the end of the NPU experiment, compiled native endpoint 18206 and tool proxy
18197 were ready with normal append behavior, forced benchmark reset disabled
and QA request logging off. They were subsequently stopped for the
[GPU placement investigation](ministral_gpu.md). CPU default endpoint 18196
remains selected alongside Reason2 on the NPU. The rejected dynamic Q4_0
experiment does not replace either selected service.

## Guarded prefix rewind across bodies — later September 19 experiment

`serve_genie_backend.py --cache-mode append_rewind` is a new opt-in candidate.
Exact append remains the first choice. When a new body changes the middle of
the prompt, the worker may submit the full actual token array using Genie's
REWIND API, retaining the common policy prefix. It permits only an interior
divergence in both old and new evaluated inputs. Identical/shorter prompts and
boundary cases reset; no saved output token can serve as a new decision.
Every accepted response requires one fresh full-vocabulary sampler callback,
matching emitted winner, END, and exact final context occupancy. The normal
reset/append defaults are unchanged. Eligibility requires API 1.20.0; actual
loaded QAIRT 2.50.40 library hashes, worker and bridge source identity are also
recorded. The API version alone does not certify another runtime build.

The device experiment used 21 exact model-visible histories from the GPU run,
then independently recomputed each with a full reset. All 21 token arrays
matched native llama tokenization. All 21 action winners matched between the
NPU cache experiment and NPU reset, with a maximum absolute change of 0.005653
among the thirteen full-vocabulary action probabilities. This is not bitwise
score equality, nor comparison of every vocabulary probability.

| Actual new-body history | Full reset | Guarded rewind |
| --- | ---: | ---: |
| Body 2 arrival, 1,016 tokens | 3,823 ms | **481 ms** |
| Body 3 arrival, 1,022 tokens | 3,827 ms | **482 ms** |

Both rewind calls shared 948 prefix tokens, with 68/74 new tokens, and retained
the same initial-inspection choice A. This removes about 87% of these two
new-body request latencies. The first cold request still took 3,821 ms;
ordinary successful append calls were about 242–244 ms. These are direct worker
round trips, not HTTP/HUD latency, and only two new-body transitions were tested.
Expected prefix reuse is reported separately from measured runtime occupancy;
there is no hardware counter establishing saved work per token. No Reason2
inference ran concurrently and no new contention benefit is claimed.

September 21 correction: this historical direct-worker fixture tokenized the
rendered text with `add_special=False` and submitted it without the leading BOS
that the live HTTP bridge adds. Its cached/reset comparisons used identical
inputs internally, but they do **not** establish parity with live HTTP inputs.
The [device-placement follow-up](npu_device_placement.md) repeats the histories
through the actual HTTP template/tokenization path and keeps the old evidence
separate. Two premature-sealant choices below were absent in that corrected
replay; the missing-sealant departure remained.

There is a policy limitation: both modes selected premature sealant inspection
after application but before placement in two saved histories, and both
reproduced the missing-sealant departure. These observations broaden the known
policy weaknesses; they do not implicate cached output reuse. Saved histories
were replayed without executing any actions or attaching them to a newly
generated conversation. The optimization is retained as experimental, not a
validated live controller or a new accuracy result. No NPU agent service is
selected by the normal launcher.

The native worker was rebuilt against the same deployed 2.50.40 SDK in a
separate output path. Sixteen fake-ABI protocol tests and sixteen bridge tests
cover the additional mode, missing-END rejection and source provenance. The
actual device completed all 42 score calls with fresh callbacks and full-input
occupancy. Private evidence is `artifacts/ministral-cpu-tuning-20260919/rewind-report.json`.

One further export opportunity remains untested: the AR64 graph currently
produces 64 × 131,072 FP16 logits, although only its final row is consumed.
The SDK supports prefill variants without the output head followed by a smaller
final-logit graph. Investigate that separately: it requires another graph
export, may change scheduler behavior, and has no measured gain yet.

## Output-head pruning investigation — September 20

This experiment reached a verified in-memory graph reduction but **did not
produce a compiled candidate**. The original model, runtime and deployment
remain unchanged; there is no new latency or accuracy result for a pruned head.

The proposed layout keeps the original AR1 full-vocabulary head and makes the
AR64 final split produce only its 26 KV outputs. Reachability from those outputs
removes 535 of 8,196 operators, including the unused head and last-layer work
after its KV producers. It retains every required KV output. Source DLC bytes,
static weights and quantization metadata were retained for verification.

The installed QAIRT 2.50.40 APIs blocked reserialization:

- Changing a tensor's type directly did not update the graph's output registry.
  The graph-level update method did; ordinary disconnected-node removal then
  produced the expected 7,661-operator graph.
- `DlcModule.save()` saved its original updater container and silently retained
  the unmodified graph. Reload verification rejected that output.
- The SDK's direct `IrDlcSerializer` saved an **unmodified** loaded graph
  successfully. Both manual reachability pruning and SDK-native disconnected
  node removal caused serialization to exit with code 1 before completion.
  No Python exception explained the failure. Eager weight loading did not fix it.

The helper and private graph audits record this limitation. Compilation is not
authorized by a graph-count check alone. A subsequent attempt should generate
the desired graph from the original GGUF/source export before serialization,
rather than assume loaded-DLC mutation is supported. This was not attempted
in the bounded follow-up.

There is also an independent runtime tradeoff. In this SDK, AR64 without logits
plus AR1 disables the inference scheduler (`qualla/engine.cpp`). The legacy
`NspEngine::rewindKVCacheToPrefixMatch` resets KV rather than retaining a policy
prefix. Therefore the previous 0.48-second new-body rewind results **cannot**
carry over. A true AR2 or larger logits variant could retain scheduler
eligibility, but would require new graphs in both splits and separate tests.
The legacy strategy builder also has a reference-lifetime risk at remainder-one
boundaries such as 65/129 fresh tokens; no candidate device failure was observed
because the candidate never compiled.

A fresh **original-model** control completed 42 history calls and 14 recorded
reset/append boundary results at lengths 1, 2, 63, 64, 65, 128 and 129, with
separate preparation calls for the append cases. Its known policy errors remain.
These checks validate the control and harness only, not the uncompiled legacy
candidate. Evidence is private under `artifacts/model-speed-20260920/npu-original.json`
and `artifacts/npu-head-export-20260920/`.
