#!/usr/bin/env bash
# Text-only controller for offline experiments and explicit live Unreal agent mode.
set -euo pipefail
llama_bin="${DF_AGENT_LLAMA_BIN:-${HOME}/cosmos-reason2-iq9/src/llama.cpp-910196f6/build-cuda-128/bin}"
case "${DF_AGENT_KIND:-ministral}" in
  ministral) default_model="${HOME}/models/dark-factory-ministral/Ministral-3-3B-Instruct-2512-Q4_K_M.gguf" ;;
  qwen) default_model="${HOME}/models/dark-factory-agent/Qwen3.5-9B-Q4_K_M.gguf" ;;
  *) echo 'DF_AGENT_KIND must be ministral or qwen.' >&2; exit 2 ;;
esac
model="${DF_AGENT_MODEL:-${default_model}}"
cuda_env="${DF_CUDA_ENV:-${HOME}/miniconda3/envs/pytorch-build}"
[[ -f "${model}" ]] || { echo 'Set DF_AGENT_MODEL to the local controller GGUF.' >&2; exit 1; }
export LD_LIBRARY_PATH="${llama_bin}:${cuda_env}/targets/x86_64-linux/lib${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"
exec "${llama_bin}/llama-server" --model "${model}" --alias dark-factory-agent \
    --host 127.0.0.1 --port "${DF_AGENT_PORT:-18193}" \
    --n-gpu-layers all --ctx-size 8192 --parallel 1 --flash-attn on \
    --jinja --reasoning off "$@"
