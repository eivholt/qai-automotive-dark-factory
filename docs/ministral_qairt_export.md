# Experimental Ministral compiled HTP export

`scripts/agent/export_ministral_qairt.py` exports the pinned
Ministral-3-3B-Instruct-2512 Q4_K_M text model for IQ9 HTP v73 / SoC 77.
This is a separate comparison candidate; compilation alone does not establish
native CPU decision parity, runtime compatibility, or process accuracy.
QAIRT converts the source tensors into target encodings, so a pinned GGUF and
preserved positional behavior do not establish identical quantized arithmetic.
Keep weights, SDKs, exported binaries, prompt references and logs private.

The September 19 export completed in 27 minutes 47 seconds with a 3.417 GB
bundle. Both context binaries passed metadata inspection and all transferred
EVK files passed their SHA-256 manifest. Runtime decision and live process
evaluation is recorded separately in [the NPU experiment record](ministral_npu.md).

The first actual EVK load succeeded with Genie API 1.20.0 and the exact SDK
2.50.40 Genie, QnnHtp, HTP extensions, v73 stub and QnnSystem library hashes.
All 22 native prompt token arrays matched through the running worker. Two
full-reset probes selected the expected actions: initial arrival `A` in 3.83 s
of worker inference, and the earlier dynamic-Q4_0 failure history `H` in 5.50 s.
These are bounded development checks; they do not establish all-history logit
equivalence, live process reliability, or independent model accuracy.

The previous SDK 2.47 parser rejects architecture `mistral3`. SDK 2.50.40.260831
has an explicit vendor mapping to the Mistral graph, including Q/K inverse
permutation and the nonstandard 128-dimensional heads (3072 hidden channels,
32 query heads, eight KV heads, 26 layers). Its generated default tokenizer and
positional metadata do not fully describe this source model. The exporter
therefore preserves the official Tekken tokenizer and supplies explicit Genie
runtime YaRN. Simply renaming the architecture is insufficient.

## Pinned inputs

| Input | Version / SHA-256 |
| --- | --- |
| Source GGUF | `9ed150d4367e68df0ac8e1540f6ddc65b42d0ee26378329d1ecbca60f93fc5f8` |
| QAIRT SDK ZIP 2.50.40.260831 | `e821833cff67b0c55a413c920658cf9132a92e06309b6c26900149fd1a8809da` |
| qairt-dev 0.11.0 Linux wheel | `908e32b489f3459ab092d4ab7fb52737987e4a25ca7552167a16cce95c2a776e` |
| Official tokenizer.json | `99cf274236c60277fcfad861a5a1007518687ad06ba8938760f50b55ffa0b1ef` |
| Official config.json (audit reference) | `a9c3658d7bb45ab5be77e660d483a9d3f8a3d54bda2c69a1c576bdfefbc16be6` |

Official sources: [Qualcomm SDK archive](https://softwarecenter.qualcomm.com/api/download/software/sdks/Qualcomm_AI_Runtime_Community/All/2.50.40.260831/v2.50.40.260831.zip)
and [Mistral model files](https://huggingface.co/mistralai/Ministral-3-3B-Instruct-2512/tree/main).
Verify hashes after download; a moving model branch is not a version pin.

The isolated Linux environment uses Python 3.12, qairt-dev 0.11.0,
torch 2.6.0+cpu, numpy 1.26.4, transformers 4.57.1, gguf 0.9.1,
tokenizers 0.22.2 and rich 15.0.0. The script checks critical package versions,
SDK source hashes and the actual imported GGUF loader location.
The original environment and deployment remain separate.

The September 19 bundle was produced in phases. The private build/preflight
driver had SHA-256 `acad60fbe196576114a3f3b74408db10eab4f642ec410b3bd8b5a6812ef6c7d5`;
the later tracked finalization module had SHA-256
`356ef1fa012f6692e6fff82af1b85def001ffc7a6e87c6806fa7d638b17decc6`.
The current complete reproducer was assembled and validated during that work;
it did not drive the original compilation. `export_provenance.json` records
the separate build, additional preflight, finalization and reproducer-validation
phases. Private snapshots preserve the actual build driver and historical
tracked revisions whose bytes match their recorded hashes. The provenance
correction changed audit metadata only; finalized runtime artifact hashes stayed
unchanged.

## Build and preflight

Activate the isolated environment, then set paths to private inputs. The SDK
needs its x86 host binaries and shared libraries available before Python starts:

```bash
export QAIRT_SDK_ROOT=/private/sdks/qairt/2.50.40.260831
export PATH="$QAIRT_SDK_ROOT/bin/x86_64-linux-clang:$PATH"
export LD_LIBRARY_PATH="$QAIRT_SDK_ROOT/lib/x86_64-linux-clang${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
python scripts/agent/export_ministral_qairt.py \
  --sdk-root "$QAIRT_SDK_ROOT" \
  --gguf /private/models/Ministral-3-3B-Instruct-2512-Q4_K_M.gguf \
  --official-tokenizer /private/research/official-tokenizer.json \
  --prompt-reference /private/prompt-parity-reference.json \
  --output /private/ministral-qairt250-yarn4096
```

Add `--preflight-only` to validate without compiling. Preflight compares every
token against at least the 22 captured native decision prompts, checks exactly
one BOS, validates A–M action IDs 1065–1077 and tests query/key permutation for
32 and eight heads. The official tokenizer's reserved IDs 34/35 are renamed
to the GGUF's `[THINK]` / `[/THINK]`, preserving all 131072 source token IDs and
spellings. No labels or evaluator truth enter these checks.

The exporter makes two graph splits and places its temporary files under the
chosen output root. It writes `preflight.json`, the saved `container`, the
`genie` bundle and `provenance.json` with exported file hashes. It refuses to
replace a container or bundle. The bundle also carries `export_provenance.json`
and `SHA256SUMS`; run `sha256sum -c SHA256SUMS` after transfer. Binary metadata
inspection verifies both AR1/AR64 graph pairs, HTP build identity and external
FP16 rotary inputs. The deployed artifact is the finalized `genie`
directory: a bare re-export of `container` would lose the final runtime YaRN
correction and must not be deployed directly.

## Quantization representation

The source GGUF has 156 Q4_K tensors, 27 Q6_K tensors and 53 F32 tensors.
The SDK decodes weights into FP32, imports the source block encodings, applies
the Q/K permutation and linear-to-convolution layout changes, and serializes
target QNN tensors. Inspection of both exported AR1 DLCs found:

| Source / role | Exported representation |
| --- | --- |
| Q4_K projections | 4-bit asymmetric `BW_FLOAT_BLOCK`, block size 32 |
| Q6_K projections and the tied output head | 8-bit symmetric block encoding, block size 16 |
| Q6_K token embedding table | FP16 |
| Graph activations, logits, KV cache and rotary inputs | FP16 |

The Q4 tensors use an `SFIXED_POINT_8` storage carrier in the DLC, but their
encoding bit width is four; reading only the storage type would misreport
them as ordinary 8-bit weights. Source Q6_K negative block scales can be
rescaled by the SDK before target quantization. The input embedding and tied
output head share source values but have different target representations.
The compiler can also choose its own internal packing and kernels.
Genie's custom sampler converts logits to a float32 callback buffer; this does
not recover precision lost in the graph's FP16 output.

Thus this candidate retains the pinned Q4_K_M source and corrected model
semantics, while using QNN mixed-precision arithmetic. FP16 rounding, Q6-to-QNN
conversion and backend operation ordering prevent a claim of bitwise GGUF
execution. The private `compiled-quantization-audit.json` records the inspected
DLC types/encodings; runtime checks establish only the measured output and
decision behavior.

## Preserved positional behavior

Genie's runtime already implements `yarnrope`; MAD's graph receives external
sine/cosine tensors. The final config uses theta 1000000, full dimension 128
(64 rotary pairs), factor 16, beta-fast 32, beta-slow 1 and original context
16384. Genie's `attn-factor` is `1 / (1 + 0.1 * log(16))`, canceling its built-in
YaRN amplitude multiplier and giving the model's unit amplitude.

The model also scales query attention by
`1 + 0.1 * log(floor(position / 16384) + 1)`. This is exactly one throughout
the enforced 4096-token context. Raising this context needs a fresh architecture
audit; neither the exporter nor its runtime contract supports longer contexts.
At runtime the worker must add BOS exactly once because Genie tokenization
disables tokenizer-added special tokens.

`scripts/agent/verify_ministral_rope.cpp` compares the actual GGML CPU operation
against the Genie source formula over every position 0–4095 and all 64 pairs.
Build it against the same native reference's GGML headers/libraries:

```bash
c++ -O2 -std=c++17 scripts/agent/verify_ministral_rope.cpp \
  -I"$LLAMA_SOURCE/ggml/include" -L"$LLAMA_BUILD/bin" \
  -Wl,-rpath,"$LLAMA_BUILD/bin" -lggml-cpu -lggml-base \
  -o /private/verify-ministral-rope
/private/verify-ministral-rope > /private/rope-numeric.json
```

The September 19 preflight with native source `910196f6` measured maximum
sine/cosine absolute difference 0.000403315, RMS 0.000036666, and amplitude
error below 0.0000001. GGML uses FP32 frequency progression and Genie uses
doubles. Accounting for the compiled graph's FP16 rotary input tensors gives
a maximum input difference of 0.000586033. This is numerical agreement within
the stated tolerance, not bitwise
logit equivalence. The same preflight matched all 22 actual prompt token arrays
(maximum 1381 tokens including BOS). A compiled runtime still requires direct
output/decision comparisons and contention measurements before use.

## Worker/runtime compatibility

Use the matching 2.50.40 ARM64 Genie/QNN host libraries and v73 DSP libraries
in a separate process and directory. Do not combine them with Reason2's
libraries or the older compiled candidate's runtime. The current
`genie_single_token_worker.cpp` passes a C++17 syntax check with the 2.50.40
headers; that release exports the user-data sampler callback, token-query,
one-token limit and context-occupancy APIs it uses. Its custom sampler still
accepts `type: custom` plus `callback-name`. An ARM64 build and actual dialog
creation were subsequently verified on the EVK, as reported above. These
runtime checks are independent of the host-side header check.
