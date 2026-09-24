#!/usr/bin/env bash
# Run on IQ9. Owns the agent conversation; UE only reports equipment events.
set -euo pipefail
[[ "$(uname -m)" == aarch64 ]] || { echo 'Run on the EVK.' >&2; exit 2; }
repo="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
unit=dark-factory-edge-runtime
if systemctl --user is-active --quiet "$unit"; then
  echo 'EVK agent runtime already active on 18210.'
  exit 0
fi
python3 - <<'PY'
import socket
with socket.socket() as s:
    if s.connect_ex(('127.0.0.1',18210)) == 0:
        raise SystemExit('Port18210 is occupied by another listener.')
PY
args=()
[[ -z "${DF_AGENT_RUNTIME_QA_LOG:-}" ]] || args=(--qa-log "$DF_AGENT_RUNTIME_QA_LOG")
systemd-run --user --unit="$unit" --collect -p MemoryMax=256M \
  python3 -u "${repo}/scripts/agent/serve_edge_runtime.py" "${args[@]}"
echo 'EVK agent harness: loopback18210. Start the host --agent tunnel for equipment and GPU comparison.'
