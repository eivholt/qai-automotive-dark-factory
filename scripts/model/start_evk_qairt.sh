#!/usr/bin/env bash
# Run on the prepared EVK after both hash-bound development validations pass.
set -euo pipefail
[[ "$(uname -m)" == aarch64 ]] || { echo 'Run on the EVK.' >&2; exit 2; }
with_agent=false
mode=auto
for arg in "$@"; do
  case "$arg" in
    --agent) with_agent=true ;;
    --streaming|--baseline)
      [[ "$mode" == auto ]] || { echo 'Select only one execution mode.' >&2; exit 2; }
      mode="${arg#--}" ;;
    *) echo "Usage: $0 [--agent] [--streaming|--baseline]" >&2; exit 2 ;;
  esac
done
root="${DF_EDGE_ROOT:-${HOME}/qai-dark-factory}"
repo="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
runtime="${root}/runtime/geniex-v0317-qairt245/qairt"
sdk=/opt/qairt/2.45.0.260326
start_token_proxy() {
  # Only Ministral has been tested with the fixed-action prompt/token protocol.
  if [[ "${DF_AGENT_KIND:-ministral}" != ministral ]]; then
    systemctl --user stop dark-factory-edge-agent-single-token 2>/dev/null || true
    return 0
  fi
  if systemctl --user is-active --quiet dark-factory-edge-agent-single-token; then
    echo 'Existing one-token Ministral adapter remains active on 18196.'
    bash "${repo}/scripts/agent/start_edge_runtime.sh"
    return 0
  fi
  python3 - <<'PY'
import socket, time, urllib.request
with socket.socket() as s:
    if s.connect_ex(('127.0.0.1',18196)) == 0:
        raise SystemExit('Port18196 is occupied by another listener.')
deadline = time.monotonic() + 60
while True:
    try:
        with urllib.request.urlopen('http://127.0.0.1:18195/health', timeout=2) as r:
            if r.status == 200:
                break
    except OSError:
        pass
    if time.monotonic() >= deadline:
        raise SystemExit('Controller did not become ready; retry after checking its journal.')
    time.sleep(.5)
PY
  systemd-run --user --unit=dark-factory-edge-agent-single-token --collect -p MemoryMax=256M \
    env DF_SINGLE_TOKEN_BACKEND=http://127.0.0.1:18195 DF_SINGLE_TOKEN_PORT=18196 \
    "DF_AGENT_CACHE_DIR=${root}/cache/ministral-policy" \
    bash "${repo}/scripts/agent/start_single_token.sh" --prefill-next-turn
  bash "${repo}/scripts/agent/start_edge_runtime.sh"
}
start_agent() {
  ${with_agent} || return 0
  local kind="${DF_AGENT_KIND:-ministral}"
  case "$kind" in ministral|qwen) ;; *) echo 'DF_AGENT_KIND must be ministral or qwen.' >&2; return 2 ;; esac
  if [[ "$kind" == ministral && "${DF_AGENT_QUANT:-q4_k_m}" != q4_k_m ]]; then
    echo 'The validated stack requires Q4_K_M; use an isolated start_evk.sh service for Q8 experiments.' >&2
    return 2
  fi
  if systemctl --user is-active --quiet dark-factory-edge-agent; then
    if systemctl --user show dark-factory-edge-agent -p ExecStart --value | grep -Fq "DF_AGENT_KIND=${kind}"; then
      echo "Existing ${kind} controller remains active."; start_token_proxy; return 0
    fi
    echo "A different/legacy controller is active. Stop dark-factory-edge-agent before selecting ${kind}." >&2
    return 2
  fi
  local bin="${root}/runtime/llama-0eadefebd3f8f92a86d634a0e5b8fffc9dc792c0/build/bin"
  DF_AGENT_KIND="$kind" DF_EDGE_LLAMA_BIN="$bin" bash "${repo}/scripts/agent/start_evk.sh" --check
  python3 - <<'PY'
import socket
with socket.socket() as s:
    if s.connect_ex(('127.0.0.1',18195)) == 0:
        raise SystemExit('Port18195 is occupied by another listener.')
PY
  local cache_env=()
  [[ "$kind" != ministral ]] || cache_env=("DF_AGENT_CACHE_DIR=${root}/cache/ministral-policy")
  systemd-run --user --unit=dark-factory-edge-agent --collect -p MemoryMax=9G -p OOMScoreAdjust=500 \
    env "DF_EDGE_ROOT=${root}" "DF_EDGE_LLAMA_BIN=${bin}" "DF_AGENT_KIND=${kind}" "${cache_env[@]}" bash "${repo}/scripts/agent/start_evk.sh"
  start_token_proxy
}
if systemctl --user is-active --quiet dark-factory-edge-qairt; then
  if [[ "$mode" != auto ]]; then
    active=$(systemctl --user show dark-factory-edge-qairt -p ExecStart --value)
    if { [[ "$mode" == streaming && "$active" != *serve_streamed_reason2.py* ]]; } ||
       { [[ "$mode" == baseline && "$active" == *serve_streamed_reason2.py* ]]; }; then
      echo 'Stop dark-factory-edge-qairt before changing execution mode.' >&2; exit 2
    fi
  fi
  echo 'Automotive QAIRT service is already running.'; start_agent; exit 0
fi
if [[ "$mode" == auto ]]; then
  mode=baseline
  if [[ -f "${root}/runtime/qairt_stream_worker" || -f "${root}/models/qairt-primary/streaming-validation.json" ||
        -f "${root}/models/qairt-weld/streaming-validation.json" ]]; then mode=streaming; fi
fi
server=serve_qairt_reason2.py
worker=qairt_score_worker
validation=validation.json
if [[ "$mode" == streaming ]]; then
  server=serve_streamed_reason2.py; worker=qairt_stream_worker; validation=streaming-validation.json
fi
for role in primary weld; do
  for name in automotive_provenance.json "$validation"; do
    [[ -f "${root}/models/qairt-${role}/${name}" ]] || { echo "Missing ${role}/${name}" >&2; exit 2; }
  done
done
[[ -f "${root}/runtime/${worker}" && -f "${runtime}/libgeniex_vlm.so" && -f "${sdk}/lib/aarch64-oe-linux-gcc11.2/libQnnHtp.so" ]] || exit 2
python3 - <<'PY'
import socket
with socket.socket() as s:
    if s.connect_ex(('127.0.0.1',18198)) == 0:
        raise SystemExit('Port18198 is occupied; stop the known old automotive router before changing runtime.')
PY
mkdir -p "${root}/logs/qairt"
systemd-run --user --unit=dark-factory-edge-qairt --collect -p MemoryMax=16G -p OOMScoreAdjust=500 \
  env PYTHONUNBUFFERED=1 "LD_LIBRARY_PATH=${runtime}:${sdk}/lib/aarch64-oe-linux-gcc11.2" \
  "ADSP_LIBRARY_PATH=${sdk}/lib/hexagon-v73/unsigned" \
  python3 "${repo}/scripts/model/${server}" \
  --primary "${root}/models/qairt-primary" --weld "${root}/models/qairt-weld" \
  --primary-validation "${root}/models/qairt-primary/${validation}" \
  --weld-validation "${root}/models/qairt-weld/${validation}" \
  --worker "${root}/runtime/${worker}" --qairt-lib "${sdk}/lib/aarch64-oe-linux-gcc11.2" \
  --logs "${root}/logs/qairt" --port 18198
echo "Starting validated ${mode} NPU vision and text on loopback18198; inspect /health before launching Unreal."
start_agent
echo 'The text controller remains a separate CPU service; --agent starts the selected model if absent.'
