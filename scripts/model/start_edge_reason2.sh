#!/usr/bin/env bash
# Isolated raw inference backend. This is not the live Unreal gate endpoint.
set -euo pipefail
script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
: "${DF_EDGE_MODEL_DIR:?Set the verified primary-960 or weld-896 bundle directory}"
: "${DF_EDGE_LLAMA_BIN:?Set the pinned llama.cpp bin directory}"
verified="$(python3 "${script_dir}/verify_edge_export.py" "${DF_EDGE_MODEL_DIR}" --provenance "${DF_EDGE_PROVENANCE:-edge-provenance.json}" --details)"
mapfile -t bundle <<< "${verified}"
export LD_LIBRARY_PATH="${DF_EDGE_LLAMA_BIN}${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"
device_args=(--device none --n-gpu-layers 0 --no-mmproj-offload)
if [[ "${DF_EDGE_DEVICE:-cpu}" != cpu ]]; then
  device_args=(--device "${DF_EDGE_DEVICE}" --n-gpu-layers all)
  # F16 vision stays on CPU on Hexagon; all adapted vision weights are retained.
  [[ "${DF_EDGE_DEVICE}" != HTP* ]] || device_args+=(--no-mmproj-offload)
fi
exec "${DF_EDGE_LLAMA_BIN}/llama-server" \
  --model "${DF_EDGE_MODEL_DIR}/${bundle[1]}" \
  --mmproj "${DF_EDGE_MODEL_DIR}/${bundle[2]}" \
  --alias "${bundle[0]}" --host 127.0.0.1 --port "${DF_EDGE_PORT:-18196}" \
  --ctx-size 1024 --parallel 1 --batch-size 256 --ubatch-size 128 --threads 8 --threads-batch 8 \
  --image-min-tokens 112 --image-max-tokens 112 --flash-attn on \
  --no-cache-prompt --cache-ram 0 --slot-prompt-similarity 0 --log-verbosity 4 "${device_args[@]}" "$@"
