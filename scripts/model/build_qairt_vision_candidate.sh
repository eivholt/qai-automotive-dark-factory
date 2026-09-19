#!/usr/bin/env bash
# Recalibrate only the vision encoder; retain the original compiled text graphs.
set -euo pipefail
export PYTHONUNBUFFERED=1 PYTHONDONTWRITEBYTECODE=1
role="${1:?Usage: build_qairt_vision_candidate.sh primary|weld}"
[[ "$role" == primary || "$role" == weld ]] || exit 2
repo="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
reference="${DF_QAIRT_REFERENCE_REPO:-/mnt/c/Users/eivho/repos/qai-physics-reasoning}"
root="${DF_QAIRT_ROOT:-${HOME}/dark-factory-qairt}/${role}"
python="${DF_QAIRT_PYTHON:-${HOME}/miniconda3/envs/qai-cosmos-reason2/bin/python}"
work="${root}/cl1024/vision-last-fp16"
[[ ! -e "$work" ]] || { echo 'Use a fresh candidate; partial output is not reusable.' >&2; exit 2; }
mkdir -p "$work"
export PYTHONPATH="${reference}${PYTHONPATH:+:${PYTHONPATH}}"
cd "$repo"
"$python" -m qai_hub_models.models.cosmos_reason2_2b.quantize --checkpoint "${root}/merged" \
  --context-length 1024 --calibration-sequence-length 128 --skip-llm --num-samples 8 --veg-num-samples 120 \
  --image-size 256 448 --precision w4a16 --veg-paired-calibration-manifest "${root}/calibration.json" \
  --veg-fp16-last-block-activations --output-dir "${work}/vision"
"$python" - "${root}/cl1024/w8-text" "${work}/vision" "${work}/checkpoint" <<'PY'
import json,os,shutil,sys
from pathlib import Path
source,vision,dest=map(Path,sys.argv[1:])
shutil.copytree(source,dest,copy_function=os.link)
for file in vision.iterdir():
    if file.is_file():
        target=dest/file.name
        if target.exists(): target.unlink()
        shutil.copyfile(file,target)
assert json.loads((dest/'args.json').read_text())['vision_mixed_precision']['fp16_activation_blocks']==[23]
PY
# QNN 2.45 cannot share these scalar constants between integer and FLOAT16
# branches. Preserve the recalibrated source checkpoint and isolate copies.
"$python" "${repo}/scripts/model/split_qairt_vision_constants.py" \
  --source "${work}/checkpoint" --output "${work}/checkpoint-isolated"
"$python" -m qai_hub_models.models.cosmos_reason2_2b.export --checkpoint "${work}/checkpoint-isolated" \
  --runtime geniex_qairt --device 'Dragonwing IQ-9075 EVK' --device-os 1.9 --context-length 1024 \
  --image-size 256 448 --components vision_encoder --skip-profiling --output-dir "${work}/export"
"$python" "${repo}/scripts/model/replace_qairt_vision.py" --original "${root}/cl1024/bundle" \
  --reference "$reference" --export "${work}/export" --checkpoint "${work}/checkpoint-isolated" --output "${work}/bundle"
