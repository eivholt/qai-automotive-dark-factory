#!/usr/bin/env bash
set -euo pipefail
export PYTHONDONTWRITEBYTECODE=1
script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "${script_dir}/../.." && pwd)"
python_bin="${DF_MODEL_PYTHON:-${HOME}/cosmos-reason2-finetune/.venv/bin/python}"
model_dir="${DF_BASE_MODEL:-${HOME}/cosmos-reason2-finetune/base/Cosmos-Reason2-2B}"
export PYTHONPATH="${repo_root}/src${PYTHONPATH:+:${PYTHONPATH}}"
export HF_HUB_OFFLINE=1
export TOKENIZERS_PARALLELISM=false
exec "${python_bin}" -m dark_factory.gates_host --model "${model_dir}" "$@"
