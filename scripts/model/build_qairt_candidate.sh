#!/usr/bin/env bash
# Isolated automotive export using the retained conveyor QAIRT compiler tools.
# Never starts/stops or replaces the conveyor or automotive live deployments.
set -euo pipefail
export PYTHONDONTWRITEBYTECODE=1 PYTHONUNBUFFERED=1
role="${1:?Usage: build_qairt_candidate.sh primary|weld}"
[[ "$role" == primary || "$role" == weld ]] || exit 2
repo="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
reference="${DF_QAIRT_REFERENCE_REPO:-/mnt/c/Users/eivho/repos/qai-physics-reasoning}"
root="${DF_QAIRT_ROOT:-${HOME}/dark-factory-qairt}/${role}"
python="${DF_QAIRT_PYTHON:-${HOME}/miniconda3/envs/qai-cosmos-reason2/bin/python}"
trainer="${DF_MODEL_PYTHON:-${HOME}/cosmos-reason2-finetune/.venv/bin/python}"
ctx="${DF_QAIRT_CONTEXT:-1024}"
[[ "$ctx" == 512 || "$ctx" == 1024 ]] || exit 2
[[ -f "${root}/merged/adapter_provenance.json" ]] || { echo 'Merge this role adapter with merge_lora.py first.' >&2; exit 2; }
mkdir -p "$root"
if [[ ! -f "${root}/calibration.json" ]]; then
  if [[ "$role" == primary ]]; then
    adapter=panel-contact-shared-960-v7; training=data/panel-contact-combined/train.jsonl
    stages=(panel_presence adhesive panel_seated)
  else
    adapter=panel-contact-shared-896-v6; training=data/panel-contact-v10/shared/train.jsonl
    stages=(weld)
  fi
  "$trainer" "${repo}/scripts/model/prepare_qairt_calibration.py" --adapter "${repo}/checkpoints/${adapter}" \
    --training "${repo}/${training}" --stages "${stages[@]}" --output "${root}/calibration.json"
fi
work="${root}/cl${ctx}"
mkdir -p "$work"
# Keep the exact source revision in private provenance. Imported reference tools
# are read-only; generated model files go only under this project's work root.
revision="$(git -C "$reference" rev-parse HEAD)"
git -C "$reference" diff --quiet HEAD -- scripts qai_hub_models || { echo 'Reference converter has unrecorded source edits.' >&2; exit 2; }
if [[ -f "${work}/reference-revision.txt" && "$(cat "${work}/reference-revision.txt")" != "$revision" ]]; then
  echo 'Converter revision changed; use a fresh work root.' >&2; exit 2
fi
printf '%s\n' "$revision" > "${work}/reference-revision.txt"
export PYTHONPATH="${reference}${PYTHONPATH:+:${PYTHONPATH}}"
cd "$repo"
if [[ ! -f "${work}/quantized/args.json" ]]; then
  "$python" -m qai_hub_models.models.cosmos_reason2_2b.quantize --checkpoint "${root}/merged" \
    --context-length "$ctx" --calibration-sequence-length 128 --num-samples 8 --veg-num-samples 120 \
    --image-size 256 448 --precision w4a16 --veg-paired-calibration-manifest "${root}/calibration.json" \
    --output-dir "${work}/quantized"
fi
# args.json is written after quantization, but finalization may have been
# interrupted. This idempotent metadata/checkpoint validation must still run.
"$python" "${reference}/scripts/finalize_checkpoint.py" "${root}/merged" "${work}/quantized"
if [[ ! -f "${work}/fp16-activations/w4_fp16.json" ]]; then
  "$python" "${reference}/scripts/make_w4_fp16_checkpoint.py" --source "${work}/quantized" \
    --destination "${work}/fp16-activations" --keep-vision-w4a16
fi
if [[ ! -f "${work}/w8-text/text_w8_matrices.json" ]]; then
  "$python" "${reference}/scripts/make_text_w8_matrix_checkpoint.py" "${work}/fp16-activations" "${work}/w8-text" \
    --parts part1_of_4 part2_of_4 part3_of_4 part4_of_4 --context-length "$ctx" --image-size 256 448
fi
if [[ ! -f "${work}/export-complete.txt" ]]; then
  "$python" -m qai_hub_models.models.cosmos_reason2_2b.export --checkpoint "${work}/w8-text" \
    --runtime geniex_qairt --device 'Dragonwing IQ-9075 EVK' --device-os 1.9 --context-length "$ctx" \
    --image-size 256 448 --skip-profiling --output-dir "${work}/export"
  echo 'Export CLI completed; independently verify every context and device accuracy before serving.' > "${work}/export-complete.txt"
fi
