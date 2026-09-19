#!/usr/bin/env bash
# Run on the prepared EVK. Starts only this repository's isolated user services.
set -euo pipefail
[[ "$(uname -m)" == aarch64 ]] || { echo "Run on the ARM64 EVK" >&2; exit 2; }
with_agent=false
[[ $# -eq 0 ]] || { [[ $# -eq 1 && "$1" == --agent ]] && with_agent=true; } || exit 2
root="${DF_EDGE_ROOT:-${HOME}/qai-dark-factory}"
repo="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
bin="${root}/runtime/llama-0eadefebd3f8f92a86d634a0e5b8fffc9dc792c0/build/bin"
plugin="${root}/runtime/geniex-v0.6.1/llama_cpp"
units=(dark-factory-edge-primary dark-factory-edge-weld dark-factory-edge-router)
${with_agent} && units+=(dark-factory-edge-agent)
all_running=true
for unit in "${units[@]}"; do
  systemctl --user is-active --quiet "${unit}" || all_running=false
done
if ${all_running}; then echo "Requested edge services are already running."; exit 0; fi
# Conservative startup allowance includes file mappings, DSP allocations and OS
# reserve. A degraded device must be repaired, not pushed into another OOM.
available_kib="$(awk '/MemAvailable:/ {print $2}' /proc/meminfo)"
minimum_kib=$((7 * 1024 * 1024))
${with_agent} && minimum_kib=$((18 * 1024 * 1024))
if ((available_kib < minimum_kib)); then
  echo "Insufficient available RAM: ${available_kib} KiB; need ${minimum_kib} KiB before startup." >&2
  echo "Check existing test services and kernel slab usage. This script never reboots or stops another project." >&2
  exit 2
fi
for path in "${bin}/llama-server" "${root}/runtime/libggml-hexagon-loader.so" "${plugin}/libggml-htp-v73.so"; do
  [[ -f "${path}" ]] || { echo "Missing prepared runtime: ${path}" >&2; exit 2; }
done
for name in primary-960 weld-896; do
  python3 "${repo}/scripts/model/verify_edge_export.py" "${root}/models/${name}" --provenance edge-q8-provenance.json
done
# Check ports before any mutation; never take over a foreign listener.
ports="18196,18197,18198"
${with_agent} && ports="${ports},18195"
python3 - "${ports}" <<'PY'
import socket,sys
for port in map(int,sys.argv[1].split(',')):
    with socket.socket() as sock:
        if sock.connect_ex(('127.0.0.1',port)) == 0:
            raise SystemExit(f'Port {port} is already in use. Stop only known old test units first.')
PY
for spec in primary:primary-960:18196 weld:weld-896:18197; do
  IFS=: read -r role model port <<< "${spec}"
  systemd-run --user --unit="dark-factory-edge-${role}" --collect -p MemoryMax=4G -p OOMScoreAdjust=500 \
    env "DF_EDGE_LLAMA_BIN=${bin}" "DF_EDGE_MODEL_DIR=${root}/models/${model}" \
    DF_EDGE_PROVENANCE=edge-q8-provenance.json "DF_EDGE_PORT=${port}" DF_EDGE_DEVICE=HTP0 \
    "LD_LIBRARY_PATH=${bin}:${plugin}" "ADSP_LIBRARY_PATH=${plugin}" \
    "GGML_BACKEND_PATH=${root}/runtime/libggml-hexagon-loader.so" \
    bash "${repo}/scripts/model/start_edge_reason2.sh"
done
for port in 18196 18197; do
  ready=false
  for attempt in {1..30}; do
    if curl --fail --silent --max-time 2 "http://127.0.0.1:${port}/health" >/dev/null; then ready=true; break; fi
    sleep 1
  done
  ${ready} || { echo "Backend ${port} did not become ready; inspect its user journal." >&2; exit 1; }
done
systemd-run --user --unit=dark-factory-edge-router --collect -p MemoryMax=512M -p OOMScoreAdjust=500 \
  python3 "${repo}/scripts/model/serve_edge_reason2.py" \
  --primary-provenance "${root}/models/primary-960/edge-q8-provenance.json" \
  --weld-provenance "${root}/models/weld-896/edge-q8-provenance.json"
if ${with_agent}; then
  systemd-run --user --unit=dark-factory-edge-agent --collect -p MemoryMax=9G -p OOMScoreAdjust=500 \
    env DF_AGENT_KIND=qwen "DF_EDGE_LLAMA_BIN=${bin}" "DF_AGENT_MODEL=${root}/models/qwen35-9b/Qwen3.5-9B-Q4_K_M.gguf" \
    bash "${repo}/scripts/agent/start_evk.sh"
fi
echo "Router warms all four stages, then listens on loopback 18198. Check /health."
echo "Stop: systemctl --user stop ${units[*]}"
