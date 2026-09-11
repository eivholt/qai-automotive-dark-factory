# Camera inference bridge

The bridge accepts one fixed camera PNG from Unreal and returns a single G/A/R decision. Its default is an explicit scripted mock: no model runs and no accuracy is measured. In EVK mode it forwards the pixels and the versioned inspection prompt to your existing OpenAI-compatible IQ9 service. It neither installs a model nor changes that service.

From this repository in PowerShell:

```powershell
python -m pip install -e .
python -m dark_factory.bridge --mock-cycle G,A,R
```

Alternatively, use `$env:PYTHONPATH = 'src'` before the module command to run the standard-library bridge without installing the package. Python 3.11 or newer is required. `--mock-decision G`, `A`, or `R` gives a constant scripted result; the default is `A` (hold). A cycle advances once per valid image request, irrespective of what the image shows. It is useful for checking pass/hold/reject animations only.

After the new adapter has been trained, evaluated and deployed separately, point the bridge to that service explicitly:

```powershell
$env:DARK_FACTORY_EVK_URL = 'http://EVK-IP:PORT'
$env:DARK_FACTORY_MODEL = 'Cosmos-Reason2-2B-DarkFactory-FT'
python -m dark_factory.bridge --backend evk --timeout 15
```

Replace the address and model with the actual service inventory. No previous-project address is used automatically. `--evk-url`, `--model`, `--timeout` and `--port` override `DARK_FACTORY_EVK_URL`, `DARK_FACTORY_MODEL`, `DARK_FACTORY_TIMEOUT` and `DARK_FACTORY_PORT`; `DARK_FACTORY_BACKEND` can select the backend. If needed, an optional `DARK_FACTORY_API_KEY` is sent as a bearer token and never returned by health or logged. The bridge binds to PC loopback only, normally `127.0.0.1:18190`. Unreal and the bridge run on the same PC; the IQ9 service may be on the LAN.

## HTTP contract

`GET /health` reports `status`, `backend`, `mock`, configured `model`/`evk_url`, `contract_version`, `prompt_sha256`, `image`, and `evk_connectivity_checked: false`. This checks the bridge process and loaded contract, not EVK availability or model identity.

`POST /inspect` requires `Content-Type: application/json`, a content length, and exactly these three fields:

```json
{
  "request_id": "capture-0001",
  "episode_id": "episode-0001",
  "image_base64": "BARE_BASE64_PNG_BYTES"
}
```

Identifiers contain 1–128 ASCII letters, digits, dots, colons, underscores or hyphens and start with a letter or digit. The image is a **448 × 256, non-interlaced, 8-bit PNG**, at most 1 MiB, using RGB, RGBA, gray or gray-alpha pixels. The JSON body is limited to 1.5 MiB. PNG framing, checksums, dimensions and decompressed size are validated. The bridge preserves the submitted pixels. Scene state, labels, intended decision, viewer/controller state, variable prompts and file paths are not accepted in this request.

A successful inspection returns HTTP 200:

```json
{
  "request_id": "capture-0001",
  "episode_id": "episode-0001",
  "decision": "G",
  "source": "evk",
  "latency_ms": 421.7
}
```

| Decision | Observation | Simulation action |
| --- | --- | --- |
| G | Four modules visibly present, seated and aligned | Pass / advance |
| A | Occlusion or image quality prevents inspection | Hold |
| R | A module visibly missing, raised or misaligned | Reject |

An actual model answer `A` has **no `error` field**. Transport failures, invalid replies and validation failures return `decision: "A"` plus an `error` field and a non-200 status: 400/413/415 for invalid input, 502 for backend/parse failures, 503 for a busy bridge/backend, and 504 for an EVK timeout. Valid identifiers are echoed on errors; unreadable/invalid identifiers become empty strings. Unreal must also hold on a disconnected request, an invalid source, stale/mismatched identifiers or its own timeout. A missing response is never a pass. Mock responses always say `source: "mock"` and remain visibly labeled as scripted in the demo.

## Model request and timing

[configs/inspection_contract.json](../configs/inspection_contract.json) is shared by training, capture metadata and inference. Its fixed prompt describes the four-module battery tray. The bridge verifies the prompt SHA-256 and sends exactly one user message containing the PNG data URL and that text. It sends no system message, ground truth, request IDs or episode IDs to the model.

The generation settings follow the previous project's `evk-speed-v1` deployment: `max_completion_tokens: 1`, `temperature: 0`, `enable_think: false`, `top_k: 1`, `seed: 42`, `stream: false`. For a service requiring the older OpenAI token-budget spelling, use `--token-parameter max_tokens`; both spellings are never sent together. The model must return `choices[0].message.content` as exactly one uppercase G, A or R (surrounding whitespace is allowed). Prose, JSON decisions, reasoning tags and multiple completions are rejected. Verify the G/A/R tokenization and template on the exact checkpoint used for training before deployment.

A single persistent HTTP connection is reused after successful EVK requests. Errors/timeouts reset it; server-requested disconnects reconnect on the next request. There is one outstanding inference, with a maximum of eight local HTTP handlers and a five-second local request-read deadline. Additional EVK work is rejected as busy instead of queued. No implicit retry can return an older decision as a fresh capture. Response bodies are limited to 64 KiB. EVK requests have socket timeouts and a total network exchange deadline, including slowly delivered response bodies. Operating-system DNS resolution may exceed this deadline; use the EVK's IP address for a strict network timing budget. Keep the Unreal request timeout slightly larger than the bridge timeout and keep the assembly held while awaiting a current result.

Changing the scenario prompt requires a new contract version and prompt hash, updated capture labels and a new evaluation; it is not a runtime instruction input. LoRA alone does not promise lower latency. Measure warm end-to-end time on IQ9 with the same camera resolution, prompt and one-token output budget.

## Verification

```powershell
$env:PYTHONPATH = 'src'
python -m unittest discover -s tests -v
```

Bridge tests run real loopback HTTP exchanges against a fake EVK and check image validation, no ground-truth leakage, strict replies, meaningful model `A` versus error-hold, connection reuse, reconnects, busy responses and hard timeouts. They do not claim model accuracy or contact your EVK. The labeled-frame evaluator described in the main tutorial is the next step after a real backend is available. It checks the bridge contract and identity before and after evaluation; a changed source, prompt, configured model or endpoint invalidates the run. A present error field never earns accuracy credit, including on a labeled A frame.
