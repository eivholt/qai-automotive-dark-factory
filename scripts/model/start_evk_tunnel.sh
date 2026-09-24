#!/usr/bin/env bash
# Run in host WSL. Maintains loopback-only EVK model connections.
set -euo pipefail
host="${DF_EVK_SSH_HOST:-ubuntu@192.168.1.158}"
ports=(18198)
if [[ $# -gt 0 ]]; then
  [[ $# -eq 1 && "$1" == --agent ]] || { echo "Usage: $0 [--agent]" >&2; exit 2; }
  ports+=(18195 18196)
fi
for port in "${ports[@]}"; do
  unit=dark-factory-edge-tunnel
  [[ "$port" != 18195 ]] || unit=dark-factory-edge-agent-tunnel
  [[ "$port" != 18196 ]] || unit=dark-factory-edge-agent-token-tunnel
  if ! systemctl --user is-active --quiet "${unit}"; then
    systemd-run --user --unit="${unit}" --collect -p Restart=on-failure -p RestartSec=3 \
      ssh -N -o BatchMode=yes -o ExitOnForwardFailure=yes -o ServerAliveInterval=15 \
      -o ServerAliveCountMax=3 -L "127.0.0.1:${port}:127.0.0.1:${port}" "${host}"
  fi
  echo "WSL tunnel service: ${unit}; health: http://127.0.0.1:${port}/health"
  echo "Stop with: systemctl --user stop ${unit}"
done
if [[ $# -eq 1 ]]; then
  unit=dark-factory-edge-runtime-tunnel
  if ! systemctl --user is-active --quiet "$unit"; then
    systemd-run --user --unit="$unit" --collect -p Restart=on-failure -p RestartSec=3 \
      ssh -N -o BatchMode=yes -o ExitOnForwardFailure=yes -o ServerAliveInterval=15 \
      -o ServerAliveCountMax=3 \
      -L 127.0.0.1:18210:127.0.0.1:18210 \
      -R 127.0.0.1:18214:127.0.0.1:18194 "$host"
  fi
  echo 'EVK harness: http://127.0.0.1:18210/health; GPU comparison uses reverse loopback18214.'
  echo "Stop with: systemctl --user stop ${unit}"
fi
