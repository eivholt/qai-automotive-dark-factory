#!/usr/bin/env bash
# Experimental exact-weight GPU controller; start under a render-capable service.
set -euo pipefail
root="${DF_EDGE_ROOT:-${HOME}/qai-dark-factory}"
repo="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
bin="${root}/runtime/llama-0eadefebd3f8f92a86d634a0e5b8fffc9dc792c0/build-vulkan/bin"
DF_AGENT_KIND=ministral DF_AGENT_QUANT=q4_k_m DF_AGENT_MODEL="${root}/models/ministral3-3b/Ministral-3-3B-Instruct-2512-Q4_K_M.gguf" DF_EDGE_ROOT="$root" DF_EDGE_LLAMA_BIN="$bin" bash "${repo}/scripts/agent/start_evk.sh" --check
export LD_LIBRARY_PATH="${bin}${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"
exec "${bin}/llama-server" --model "${root}/models/ministral3-3b/Ministral-3-3B-Instruct-2512-Q4_K_M.gguf" \
  --alias dark-factory-agent --host 127.0.0.1 --port "${DF_AGENT_PORT:-18204}" \
  --device Vulkan0 --n-gpu-layers all --ctx-size 8192 --parallel 1 \
  --batch-size 256 --ubatch-size 128 --threads 4 --threads-batch 4 \
  --flash-attn on --cache-ram 0 --jinja --reasoning off "$@"
