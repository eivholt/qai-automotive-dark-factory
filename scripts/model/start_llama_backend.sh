#!/usr/bin/env bash
set -euo pipefail
script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "${script_dir}/../.." && pwd)"
llama_bin="${DF_LLAMA_BIN:-${HOME}/cosmos-reason2-iq9/src/llama.cpp-910196f6/build-cuda-128/bin}"
model_dir="${DF_GGUF_DIR:-${repo_root}/checkpoints/process-base-bf16}"
model_file="${DF_GGUF_MODEL_FILE:-Cosmos-Reason2-2B-base-BF16.gguf}"
projector_file="${DF_GGUF_PROJECTOR_FILE:-mmproj-Cosmos-Reason2-2B-base-F16.gguf}"
python_bin="${DF_MODEL_PYTHON:-${HOME}/cosmos-reason2-finetune/.venv/bin/python}"
model_alias="$("${python_bin}" "${script_dir}/verify_model_export.py" --directory "${model_dir}" --text "${model_file}" --projector "${projector_file}" --alias-only)"
cuda_env="${DF_CUDA_ENV:-${HOME}/miniconda3/envs/pytorch-build}"
export LD_LIBRARY_PATH="${llama_bin}:${cuda_env}/targets/x86_64-linux/lib${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"
exec "${llama_bin}/llama-server" -m "${model_dir}/${model_file}" \
  --mmproj "${model_dir}/${projector_file}" \
  --alias "${model_alias}" -ngl all -c 1024 \
  --host 127.0.0.1 --port "${DF_LLAMA_PORT:-18192}" --parallel 1 --image-min-tokens 112 --image-max-tokens 112 \
  --flash-attn on --no-cache-prompt --slot-prompt-similarity 0 "$@"
