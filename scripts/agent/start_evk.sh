#!/usr/bin/env bash
# Hash-pinned text controller, with bounded batches on the IQ9 CPU.
set -euo pipefail
: "${DF_EDGE_LLAMA_BIN:?Set the isolated pinned llama.cpp bin directory}"
root="${DF_EDGE_ROOT:-${HOME}/qai-dark-factory}"
case "${DF_AGENT_KIND:-ministral}" in
  ministral)
    case "${DF_AGENT_QUANT:-q4_k_m}" in
      q4_k_m)
        relative=models/ministral3-3b/Ministral-3-3B-Instruct-2512-Q4_K_M.gguf
        expected=9ed150d4367e68df0ac8e1540f6ddc65b42d0ee26378329d1ecbca60f93fc5f8 ;;
      q8_0)
        relative=models/ministral3-3b/Ministral-3-3B-Instruct-2512-Q8_0.gguf
        expected=8c2b72eb5861304fcfd5e82f1eddd6efa4115737f4239fd216a028a8852413ef ;;
      *) echo 'DF_AGENT_QUANT must be q4_k_m or q8_0.' >&2; exit 2 ;;
    esac ;;
  qwen)
    relative=models/qwen35-9b/Qwen3.5-9B-Q4_K_M.gguf
    expected=03b74727a860a56338e042c4420bb3f04b2fec5734175f4cb9fa853daf52b7e8 ;;
  *) echo 'DF_AGENT_KIND must be ministral or qwen.' >&2; exit 2 ;;
esac
model="${DF_AGENT_MODEL:-${root}/${relative}}"
[[ -x "${DF_EDGE_LLAMA_BIN}/llama-server" && -f "$model" ]] || { echo 'Prepared agent runtime/model missing.' >&2; exit 2; }
actual="$(sha256sum "${model}" | cut -d' ' -f1)"
[[ "${actual}" == "${expected}" ]] || { echo 'Agent weights differ from the selected tested export.' >&2; exit 2; }
[[ "${1:-}" != --check ]] || exit 0
export LD_LIBRARY_PATH="${DF_EDGE_LLAMA_BIN}${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"
args=(--model "${model}" --alias dark-factory-agent \
  --host 127.0.0.1 --port "${DF_AGENT_PORT:-18195}" --device none --n-gpu-layers 0 \
  --ctx-size 8192 --parallel 1 --batch-size 256 --ubatch-size 128 --threads 8 --threads-batch 8 \
  --flash-attn on --cache-ram 0 --jinja --reasoning off "$@")
if [[ -n "${DF_AGENT_CACHE_DIR:-}" ]]; then
  mkdir -p "$DF_AGENT_CACHE_DIR"
  args+=(--slot-save-path "$DF_AGENT_CACHE_DIR")
  python3 - "$DF_AGENT_CACHE_DIR" "$DF_EDGE_LLAMA_BIN" "$model" "$actual" "${args[@]}" <<'PY'
import hashlib,json,os,sys
from pathlib import Path
directory,bin_dir,model,model_hash,*arguments=sys.argv[1:]
bin_dir=Path(bin_dir)
files=[bin_dir/'llama-server',*sorted(bin_dir.glob('*.so*'))]
hashes={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in files if p.is_file()}
identity={'schema':'darkfactory-native-cache-identity-v1','model_path':model,
          'model_sha256':model_hash,'runtime_sha256':hashes,'arguments':arguments,
          'backend_environment':{k:v for k,v in os.environ.items() if k.startswith('GGML_')}}
path=Path(directory)/'identity.json'
temporary=path.with_suffix('.tmp');temporary.write_text(json.dumps(identity,indent=2)+'\n');temporary.replace(path)
PY
fi
exec "${DF_EDGE_LLAMA_BIN}/llama-server" "${args[@]}"
