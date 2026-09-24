#!/usr/bin/env bash
# Isolated diagnostic candidate only. No service installation or existing stops.
set -euo pipefail
[[ "$(uname -m)" == aarch64 ]] || { echo 'Run on the prepared ARM64 EVK.' >&2; exit 2; }
[[ $# -eq 0 || ( $# -eq 1 && "$1" == --check ) ]] || exit 2
: "${DF_HEXAGON_CANDIDATE_DIR:?Set the separately prepared private candidate directory}"
root="${DF_EDGE_ROOT:-${HOME}/qai-dark-factory}"
rev=0eadefebd3f8f92a86d634a0e5b8fffc9dc792c0
src="${root}/runtime/llama-${rev}"
bin="${src}/build/bin"
plugin="${root}/runtime/geniex-v0.6.1/llama_cpp"
loader="${root}/runtime/libggml-hexagon-loader.so"
repo="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
ctx="${DF_HEXAGON_CANDIDATE_CTX:-4096}"
[[ "$ctx" == 2048 || "$ctx" == 4096 || "$ctx" == 8192 ]] || { echo 'Context must be 2048, 4096 or 8192.' >&2; exit 2; }
for path in "$bin/llama-server" "$loader" "$plugin/libggml-hexagon.so" "$plugin/libggml-htp-v73.so"; do
  [[ -f "$path" ]] || { echo "Missing candidate dependency: $path" >&2; exit 2; }
done
verification="$(python3 "$repo/scripts/agent/prepare_hexagon_ministral.py" \
  --root "$DF_HEXAGON_CANDIDATE_DIR" --verify-only --runtime-source "$src" --runtime-archive "${src}.tar.gz")"
echo "$verification"
[[ "${1:-}" != --check ]] || exit 0
export LD_LIBRARY_PATH="$bin:$plugin"
export ADSP_LIBRARY_PATH="$plugin"
export GGML_BACKEND_PATH="$loader"
export GGML_HEXAGON_MBUF=256
model="$DF_HEXAGON_CANDIDATE_DIR/Ministral-3-3B-Instruct-2512-decoder-Q4_0-head-Q8_0.gguf"
args=(--model "$model" --alias dark-factory-agent --host 127.0.0.1 --port 18205
  --device HTP0 --n-gpu-layers all --override-tensor 'token_embd.weight=CPU' --fit off
  --ctx-size "$ctx" --parallel 1 --batch-size 256 --ubatch-size 128 --threads 8 --threads-batch 8
  --flash-attn on --cache-ram 0 --jinja --reasoning off --log-verbosity 4)
python3 - "$DF_HEXAGON_CANDIDATE_DIR" "$bin" "$plugin" "$loader" "$verification" "${args[@]}" <<'PY'
import hashlib,json,os,socket,sys,time
from pathlib import Path
directory,bin_dir,plugin,loader,verification,*arguments=sys.argv[1:]
with socket.socket() as sock:
    if sock.connect_ex(('127.0.0.1',18205)) == 0:
        raise SystemExit('Candidate port 18205 is already occupied; no process was stopped')
def digest(path):
    h=hashlib.sha256()
    with path.open('rb') as f:
        for block in iter(lambda:f.read(8*1024*1024),b''):h.update(block)
    return h.hexdigest()
files=[Path(bin_dir)/'llama-server',Path(loader),*sorted(Path(bin_dir).glob('*.so*')),
       *sorted(Path(plugin).glob('*.so*'))]
report={'schema':'darkfactory-ministral-hexagon-launch-v1','arguments':arguments,
        'verified_source':json.loads(verification)['runtime_source'],
        'backend':'GenieX v0.6.1 dynamic Hexagon v73; CPU tied head; actual offload unverified',
        'model_provenance_sha256':digest(Path(directory)/'candidate.json'),
        'runtime_sha256':{str(p):digest(p) for p in files if p.is_file()},
        'backend_environment':{k:v for k,v in os.environ.items() if k.startswith('GGML_') or k in ('LD_LIBRARY_PATH','ADSP_LIBRARY_PATH')},
        'normal_services_changed':False}
path=Path(directory)/f'launch-{time.time_ns()}.json'
path.write_text(json.dumps(report,indent=2)+'\n')
print(str(path),flush=True)
PY
exec "$bin/llama-server" "${args[@]}"
