#!/usr/bin/env bash
# Opt-in Adreno 663 candidate; CPU remains the normal controller default.
# Requires private Turnip + the Q6 scale32 backend described in ministral_gpu.md.
set -euo pipefail
root="${DF_EDGE_ROOT:-${HOME}/qai-dark-factory}"
repo="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
bin="${DF_GPU_LLAMA_BIN:-${root}/runtime/vulkan-ops-test/q6-server-bin}"
turnip="${DF_GPU_TURNIP_ROOT:-${root}/runtime/private-turnip-26.1.8/package}"
[[ -f "${bin}/libggml-vulkan.so" && -x "${turnip}/with-turnip" ]] || {
  echo 'Prepare the isolated Q6 scale32 runtime and private Turnip first; see docs/ministral_gpu.md.' >&2
  exit 2
}
# An additional backend could silently select the old, numerically failing GPU.
[[ -z "${GGML_BACKEND_PATH:-}" ]] || { echo 'Use only the Vulkan plugin in the isolated bin directory; unset GGML_BACKEND_PATH.' >&2; exit 2; }
DF_AGENT_KIND=ministral DF_AGENT_QUANT=q4_k_m DF_AGENT_MODEL="${root}/models/ministral3-3b/Ministral-3-3B-Instruct-2512-Q4_K_M.gguf" DF_EDGE_ROOT="$root" DF_EDGE_LLAMA_BIN="$bin" bash "${repo}/scripts/agent/start_evk.sh" --check
[[ "${1:-}" != --check ]] || exit 0
export LD_LIBRARY_PATH="${bin}${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"
export tu_restrict_subgroup_size_64=true
exec "${turnip}/with-turnip" "${bin}/llama-server" --model "${root}/models/ministral3-3b/Ministral-3-3B-Instruct-2512-Q4_K_M.gguf" \
  --alias dark-factory-agent --host 127.0.0.1 --port "${DF_AGENT_PORT:-18204}" \
  --device Vulkan0 --n-gpu-layers all --ctx-size 4096 --parallel 1 \
  --batch-size 64 --ubatch-size 64 --threads 4 --threads-batch 4 \
  --flash-attn on --fit off --no-warmup --no-context-shift \
  --cache-ram 0 --jinja --reasoning off "$@"
