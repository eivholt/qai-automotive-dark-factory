# Candidate native Genie one-token worker

This is an experimental transport and scoring implementation, not evidence that
the current Ministral export loads or that its decisions match the CPU model.
The worker was compiled against both QAIRT 2.47 and 2.50.40's Genie APIs. Runtime
version and exact loaded library hashes are reported; each SDK still requires
its own model/runtime and score-parity checks. It uses separate host libraries from Reason2's QAIRT
2.45 process. The caller must supply the matching ADSP library search path.

Build on the EVK with matching SDK headers and libraries:

```bash
bash scripts/agent/build_genie_single_token_worker.sh SDK_ROOT PRIVATE_OUTPUT
```

The optional third argument selects the QAIRT library directory.
`DF_GENIE_JSON_INCLUDE` can select an existing nlohmann header directory.
OpenSSL development headers and libcrypto provide streaming SHA256. The helper
only builds an executable; it does not load models or change services.

Run with `CONFIG_JSON TOKENIZER_JSON QAIRT_LIB_DIRECTORY`. Relative bundle paths
resolve against the config directory. The explicit tokenizer must be the one
named in that config. The worker checks a basic binary QnnHtp dialog, excludes
configured LoRA and grammar, and derives an in-memory config with a custom sampler and one
output token. It verifies A-M as distinct, exact-roundtrip single tokens.

The initial stdout line is health. Subsequent input and output are JSON lines
with a string `request_id`. Vendor stdout is redirected to stderr. Supported
operations are:

- `{"request_id":"h","op":"health"}`
- `{"request_id":"t","op":"tokenize","content":"A","add_special":false}`
- `{"request_id":"d","op":"decode","tokens":[1065]}`
- `{"request_id":"c","op":"completion","tokens":[1,1065],"cache_mode":"reset"}`

The example token IDs are illustrative inputs, not a valid model conversation.
Completion also accepts a rendered `prompt` instead of `tokens`. The caller
owns the exact native template and BOS handling. Genie receives tokenQuery END
after reset, which adds no instruction text or implicit BOS/EOS. Inputs must fit
the compiled context with room for one output token. No truncation is allowed.

Scoring uses the actual custom-sampler float buffer. Its size is bytes in Genie
2.47. The worker checks the complete vocabulary size and finite logits, computes
global argmax and full-vocabulary softmax, and returns top-128 log probabilities
plus all thirteen exact action probabilities. The returned token must equal the
sampler winner. A winner outside A-M is retained for the protocol adapter to
reject. No mask, bias, penalty, fine tuning, or fallback action is applied.
Genie suppresses emission of configured EOS/EOT winners. When exactly one valid
sampler callback selects such an ID and the runtime sends a zero-token END,
the response retains that raw winner and scores, reports
`native_emitted_token_count=0` and `native_eos_suppressed=true`, and invalidates
append state. Unexplained missing output, a missing END, or an abort still fail.
`native_sampler_callback_count` records the actual callback count.
`generation_settings.samplers=["temperature"]` describes equivalent greedy
semantics for the existing validator; `native_sampler` identifies the actual
registered argmax callback.

`cache_mode=reset` is the default. Explicit experimental `append` can retain the
previous evaluated input only when the new full tokens extend that exact prefix
and the first new token equals the previously sampled winner. The full prompt
must still fit the context. Runtime occupancy must equal the previous full input
length. The worker sends the entire new suffix, including the sampled token,
through END; the sampled token was not evaluated when the previous one-token
query stopped. Mismatched prefixes, changed action tokens and repeated prompts
fall back to reset. Completion errors reset and invalidate the retained prefix.
The worker verifies final runtime occupancy against the new full token count.

Reports distinguish full prompt count/hash, submitted suffix count/hash, actual
mode, retained context count and runtime occupancy. `reused_tokens_measured` is
null: retained occupancy is not a hardware measurement of saved computation.
Compare reset and append winners, returned top-128 log probabilities and all
thirteen action probabilities on the real runtime before using append in a live
controller. The remaining vocabulary scores are not serialized, so these fields
cannot establish equality of the entire score vector. No persisted cache or
tool-time preparation is implemented.

Unrestricted REWIND is rejected. QAIRT 2.47's raw tokenQuery path does not forward
its output callback for this sentence code. Its boundary handling can also emit a
saved token before fresh sampling, and HTP checkpoints include the unevaluated
final sample. QAIRT 2.50.40 fixes callback routing but retains boundary behavior.

The explicit `append_rewind` experiment requires the reviewed Genie API 1.20.0
and preserves exact append first. Otherwise, rewind is permitted only if the old
evaluated tokens and the complete actual new input share a positive prefix and
diverge strictly before either input ends. This excludes repeated requests,
shortened prefixes and the saved unevaluated winner. Those cases fall back to a
full reset. Occupancy must equal the previous evaluated input length; EOS and
errors invalidate reuse. The runtime receives the full real input for REWIND,
and the response still requires exactly one fresh global-vocabulary sampler
callback, its matching emitted winner, an END callback and exact final occupancy. A saved token
without fresh scores fails the request and resets state.

Use `serve_genie_backend.py --cache-mode append_rewind` only for this experiment;
the default and selected service configuration are unchanged. Diagnostics report
the expected common prefix and fresh suffix separately from the full submitted
array. Expected reuse is not measured accelerator work. Compare reset, append
and rewind scores and latency on the real compiled model before promotion;
the fake ABI tests establish protocol guards only. No persistent prefix cache
or preparation based on future observations is added.
HTTP health records the backend source hash and selected cache mode alongside
the worker and loaded runtime identities. API eligibility alone does not certify
an untested library build; record the actual library hashes in each experiment.

Health includes SHA256 identities for the config, effective config, tokenizer,
context binaries, backend extension config, worker and loaded Genie/QNN host
libraries. It records environment paths and the dynamic Genie API version.
Source weight provenance belongs to the export manifest and is explicitly not
inferred from a compiled context filename.

The pure scoring test requires only g++. The optional protocol fixture compiles
the real worker against a test-only fake Genie ABI, with no model or accelerator:

```bash
DF_GENIE_TEST_SDK=SDK_ROOT python3 -m unittest discover -s tests -p 'test_genie_single_token*.py' -v
```

These tests establish protocol checks and score arithmetic, not model behavior,
native cache equivalence, accelerator use, or performance.
