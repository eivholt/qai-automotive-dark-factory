#!/usr/bin/env bash
set -euo pipefail
export PYTHONDONTWRITEBYTECODE=1
script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "${script_dir}/../.." && pwd)"
python_bin="${DF_MODEL_PYTHON:-${HOME}/cosmos-reason2-finetune/.venv/bin/python}"
model_dir="${DF_BASE_MODEL:-${HOME}/cosmos-reason2-finetune/base/Cosmos-Reason2-2B}"
llama_dir="${DF_LLAMA_SOURCE:-${HOME}/cosmos-reason2-iq9/src/llama.cpp-910196f6}"
output="${DF_GGUF_DIR:-${repo_root}/checkpoints/process-base-bf16}"
mkdir -p "${output}"
"${python_bin}" "${llama_dir}/convert_hf_to_gguf.py" "${model_dir}" --outtype bf16 --outfile "${output}/Cosmos-Reason2-2B-base-BF16.gguf"
"${python_bin}" "${llama_dir}/convert_hf_to_gguf.py" "${model_dir}" --mmproj --outtype f16 --outfile "${output}/mmproj-Cosmos-Reason2-2B-base-F16.gguf"
"${python_bin}" "${script_dir}/record_model_provenance.py" --base "${model_dir}" --gguf "${output}" --llama-source "${llama_dir}"
