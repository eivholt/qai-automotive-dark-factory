#!/usr/bin/env bash
set -euo pipefail
export PYTHONDONTWRITEBYTECODE=1
if [[ $# -ne 2 ]]; then
  echo "Usage: export_lora_gguf.sh MERGED_MODEL_DIR NEW_OUTPUT_DIR" >&2
  exit 2
fi
script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
python_bin="${DF_MODEL_PYTHON:-${HOME}/cosmos-reason2-finetune/.venv/bin/python}"
llama_dir="${DF_LLAMA_SOURCE:-${HOME}/cosmos-reason2-iq9/src/llama.cpp-910196f6}"
model_dir="$1"
output="$2"
if [[ -e "${output}/provenance.json" || -e "${output}/Cosmos-Reason2-2B-Automotive-LoRA-BF16.gguf" ]]; then
  echo "Choose a new export directory; existing model exports are never overwritten" >&2
  exit 2
fi
mkdir -p "${output}"
"${python_bin}" "${llama_dir}/convert_hf_to_gguf.py" "${model_dir}" --outtype bf16 --outfile "${output}/Cosmos-Reason2-2B-Automotive-LoRA-BF16.gguf"
"${python_bin}" "${llama_dir}/convert_hf_to_gguf.py" "${model_dir}" --mmproj --outtype f16 --outfile "${output}/mmproj-Cosmos-Reason2-2B-Automotive-LoRA-F16.gguf"
"${python_bin}" "${script_dir}/record_model_provenance.py" --base "${model_dir}" --gguf "${output}" --llama-source "${llama_dir}" --automotive-adapter
