# Automotive Reason2 on the compiled IQ9 NPU runtime

The deployed automotive service uses compiled QAIRT vision and text graphs on
Hexagon, replacing the previous CPU-vision/llama.cpp decoder service. Both stage
adapters use FP16 vision with W8 text and FP16 text activations. The retained
older service remains an explicit fallback, on the same port when selected.

Device development validation is **191/192**: primary 119/120, weld 72/72, zero
false G and all NIR decisions correct. The only error is an unnecessary hold on
cool sealant. Median three-image batch times are **1409.49/1410.53 ms** for
primary/weld, versus about 4.85 s in the previous live EVK run. Inputs, prompts,
.98/.5 thresholds and the one-fresh-batch policy are unchanged. This is reused
development data, not new held-out accuracy.

The actual Unreal `ProcessSmoke -Enhancements` passed three correct departures
(assembly/preassembled/assembly), all six thermal acceptance checks and all
15 fresh valid model replies. HTTP reply latency was **1416.12 ms median /
1438.24 ms maximum**. Simulation time was 153.38 s. Three ambiguous replies and
thermal holds caused reinspection; no ground truth or timer overrode them.
Unreal closed after the test. Both compiled workers and Qwen remain loaded,
with about 17 GiB available RAM and no observed service restart. This verifies
the new model transport through the flow controller; it is not a new complete
Qwen-agent benchmark. Qwen decision inference remains CPU-only.

A final 27-call single-image FP16-vision probe reached **557.81 ms median**:
553.20 ms presence, 554.24 ms seating and 733.11 ms sealant. Its three serial
calls took 1671.76 ms median per aggregate and scored only 6/9. Thus single-image
speed matches the conveyor target, but the changed single-view prompts remain
offline; the deployed model uses the validated three-image request.

Evidence remains private under `artifacts/qairt-20260919`, including the
hash-bound device reports, `final-summary.json` and `process-smoke`. The actual
welding screenshot was reviewed and shown in chat. Python discovery passes 156
tests; rendered native ProcessPolicySmoke passes 135 checks.

The comparison preserves 448 x 256 lossless grayscale images, 112 visual tokens
per image, the exact three-camera prompts, independent image markers separated
by newlines, no system prompt, one output token, .98 conditional confidence and
.5 minimum full-vocabulary label mass. Original three-view prompts require
context 1024; the longest current request has 620 tokens. Shorter, single-view
prompts remain a separately versioned offline experiment with different adapter
provenance requirements.

## Build

The host uses the adjacent `qai-physics-reasoning` conversion tools read-only.
The tested environment is its `qai-cosmos-reason2` conda environment with QAI Hub
Models 0.58 and the project's custom Cosmos exporter. Qualcomm Workbench access
is required. Generated files stay in a private work directory outside Git.

1. Merge `panel-contact-shared-960-v7` into `primary/merged` and
   `panel-contact-shared-896-v6` into `weld/merged` using
   `scripts/model/merge_lora.py`. Its manifest verifies that each multimodal
   adapter changes the paired vision weights as well as text.
2. Run `bash scripts/model/build_qairt_candidate.sh primary`, then `weld`.
   `DF_QAIRT_ROOT` defaults to `$HOME/dark-factory-qairt` in WSL. Override the
   reference repository and Python paths using the documented variables at the
   top of that script. Avoid overlapping GPU quantization jobs.
3. After each export completes, run `scripts/model/package_qairt_candidate.py`
   with `--root <work-root>/<role> --reference <reference-repo> --role <role>`.
   Use the QAI Python environment. The packager checks compiler compatibility,
   full DeepStack inputs and training provenance, then records all bundle hashes.
4. Stage each private `cl1024/bundle` under the automotive EVK experiment
   directory. Do not replace the retained conveyor deployment.

Vision activation calibration uses 120 distinct original images from the
adapter's recorded **training** manifest. Each static image is duplicated into
the encoder's two-frame patch input. Stage/batch-label stratification only chooses
calibration images; it does not assign an individual camera the whole-batch label.
No validation/test pixels calibrate the model. Text matrices use W8 with FP16
activations. Inspection of the vision encodings establishes W8 weights (205
parameter encodings) and INT16 activations (934 encodings). The upstream export's
generic `w4` metadata label and `keep-vision-w4a16` option name do not describe
the actual vision precision. Retain the encoding files and `text_w8_matrices.json`.

## Native score worker

`qairt_score_worker.cpp` links the existing QAIRT plugin libraries and exposes
the protected first-token logits through a small subclass. It reads all 151,936
unmasked logits and computes G/A/R probabilities against the full vocabulary.
Its token callback stops immediately after the first sampled token, avoiding an
unused decode. Every request resets KV state; no image cache is used.

Pinned source headers:

| Component | Revision |
|---|---|
| Qualcomm GenieX v0.3.17 | `2c2bde3afabc476f31e77bd7e01559c7e8f13cfd` |
| geniex-qairt plugin | `f00a37cd50645456c8fbf0eaf179fa59640fd5bf` |
| geniex-proc | `2ec6abcac6db8473fa26ea2f882f99652723cc7b` |

Initialize only the `third-party/geniex-qairt` submodule and its
`third-party/geniex-proc` submodule. Retain their included xtensor/xtl headers,
QNN `include` and `src` directories, and GenieX `sdk/include/external`.
On the EVK, build with:

```bash
bash scripts/model/build_qairt_score_worker.sh \
  <GenieX-source-root> <matching-v0.3.17-qairt-library-directory> \
  <private-output>/qairt_score_worker
```

Use QAIRT `/opt/qairt/2.45.0.260326/lib/aarch64-oe-linux-gcc11.2` in
`LD_LIBRARY_PATH`, alongside the plugin libraries. Set `ADSP_LIBRARY_PATH` to
the same release's `lib/hexagon-v73/unsigned`. Do not mix QAIRT releases.

The worker accepts JSON lines containing only a request ID, local sensor image
paths and the prompt. It has no network listener. Source labels, temperatures,
episode identities and case names are not forwarded to it.

## Validation and interpretation

`probe_qairt_reason2.py prepare` packages an existing prepared validation manifest
using `--manifest`, `--stages`, and a new private `--output` directory. The package
retains original PNG bytes and hashes. Copy it and the Python source to the EVK.
The `run` command requires `--manifest <package>/manifest.json`,
`--provenance <bundle>/automotive_provenance.json`, `--bundle`, `--worker`,
`--qairt-lib`, and a new `--output` report path. Run primary and weld separately,
then verify simultaneous residency before attempting live integration.

The probe keeps a resident worker, records native and round-trip latency,
confidence, confusion and false G counts. `--single-view` uses three independent
requests and the experimental aggregation rules; its whole-batch labels never
become per-image ground truth. It must not be promoted under the old prompt hash.

Initial runtime control: six known conveyor images produced six correct labels
at approximately 0.50 seconds per image including full probabilities. This is
not automotive accuracy, a large benchmark, or HTTP end-to-end latency.
The startup log initially reported failure to map the final partition on DSP
protection domain 0. A verbose rerun established successful automatic retry on
domain 2 and successful loading of all five contexts; it did not fall back to CPU.
`DF_QAIRT_DEBUG=1` enables those detailed diagnostics, which should remain off
for timing measurements.

The initial automotive candidates completed on the EVK:

| Candidate | Correct batches | False G | Median complete batch |
|---|---:|---:|---:|
| Primary, calibrated W8A16 vision | 115/120 | 0 | 1.443 s |
| Weld, calibrated W8A16 vision | 71/72 | 0 | 1.440 s |

Both preserve the original three-view prompts. These candidates are **not
deployed**: their conservative errors exceed the retained 191/192 development
baseline. The former CPU-vision/NPU-decoder runtime took approximately 4.85 s
per batch in the live agent run; this is a cross-run comparison, not paired timing.

A separate 27-call automotive single-image probe reached 0.579 s median per
image (about 0.577 s NIR and 0.761 s sealant). Its whole-batch aggregation was
only 6/9 correct, and three serial requests took 1.739 s median in total.
These changed prompts need separate adapter validation/training; they are not
a valid drop-in replacement for the trained three-view classifier.

The diagnosed presence regression comes from vision quantization: normalized
pixels match HF within 1.2e-7, unquantized QAI vision preserves the answer, but
actual NPU features change it when fed into the unchanged HF text model. A
training-only recalibration keeping the last vision block's 36 activation
tensors in FLOAT16 restores the expected answer in QuantSim. However, the actual
compiled variant still scores 115/120 primary and 71/72 weld, with the same
conservative errors. Its median batch latencies are 1.425/1.449 s. The first
mixed-precision link failed with a shared constant datatype mismatch;
`split_qairt_vision_constants.py` isolates six unchanged scalar constants and
resolves that compile failure, without changing the original weights/arithmetic.

Further diagnostics confirm native vision RoPE sine/cosine inputs match the
reference within 6e-8. The compiler's bias-range warnings reflect only tiny
rounding overflows in inspected source encodings, not evidence of a large bias
loss. The compiled final features remain substantially different from QuantSim.
`make_qairt_float_vision.py` prepares an alternate fully FP16 vision checkpoint,
retaining all original text files. Both fully FP16 vision graphs compiled and
passed the device development bar above. Encoder precision, not smaller images
or relaxed confidence, recovered the lost NIR/weld classifications.

The existing 192-batch development cohort is not a new independent held-out
test. Preserve all generated reports, compiler logs, bundles and images privately.

## Deployed service

`serve_qairt_reason2.py` keeps both compiled bundles resident and selects them
only by stage. The HTTP protocol, freshness, label-mass checks and one-batch
policy remain unchanged. Startup checks the worker and bundle hashes against
complete original-prompt device validation and refuses inaccurate candidates.
The native score schema is recorded separately from the compatible HTTP
full-vocabulary-logits schema.

After validation, `start_evk_qairt.sh` expects bundles plus `validation.json`
under `~/qai-dark-factory/models/qairt-primary` and `qairt-weld`, the worker in
`runtime/qairt_score_worker`, and pinned libraries in
`runtime/geniex-v0317-qairt245/qairt`. It starts a loopback-only service on 18198,
without modifying the retained conveyor deployment. `--agent` also starts the
separate prepared Qwen CPU service if it is absent. No boot autostart is installed.

```bash
# On the prepared EVK:
bash ~/qai-dark-factory/repo/scripts/model/start_evk_qairt.sh --agent
curl http://127.0.0.1:18198/health
```

```powershell
# On the PC: persistent loopback forwarding and the existing Unreal endpoint.
wsl.exe bash scripts/model/start_evk_tunnel.sh --agent
.\unreal\Scripts\run.ps1 -Process -Enhancements -ProcessControl agent `
  -ProcessEndpoint http://127.0.0.1:18198/v1/inspect -ProcessModelSource evk_hexagon `
  -AgentEndpoint http://127.0.0.1:18195/v1/chat/completions
```

Use `-ProcessControl flow` for the deterministic comparison. To stop the NPU
service, use `systemctl --user stop dark-factory-edge-qairt` on the EVK; Qwen is
separate. For the older CPU-vision fallback, stop that unit first and use
`start_evk_stack.sh`. Never run both routers on 18198.

To reproduce the selected vision build after the initial export, run
`make_qairt_float_vision.py --source <role>/cl1024/w8-text --output <new>/checkpoint`,
export only `--components vision_encoder` with the same QAIRT/device settings,
then call `replace_qairt_vision.py` with the original bundle and new checkpoint
and export directories. It requires byte-identical text graphs, embedding and
tokenizer, and assigns a new hash-based identity. Validate all 120/72 batches
before staging the bundle and its matching report as `validation.json`.
