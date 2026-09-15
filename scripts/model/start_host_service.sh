#!/usr/bin/env bash
# Start the repo-specific model host under the WSL user service manager.
set -euo pipefail
script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "${script_dir}/../.." && pwd)"
unit="dark-factory-panel-model.service"
python_bin="${DF_MODEL_PYTHON:-${HOME}/cosmos-reason2-finetune/.venv/bin/python}"
model_dir="${DF_BASE_MODEL:-${HOME}/cosmos-reason2-finetune/base/Cosmos-Reason2-2B}"
port=18191
args=("$@")
for ((i=0; i<${#args[@]}; i++)); do
    case "${args[i]}" in
        --port)
            if ((i + 1 >= ${#args[@]})); then
                echo "--port requires a TCP port number." >&2
                exit 2
            fi
            i=$((i + 1))
            port="${args[i]}"
            ;;
        --port=*) port="${args[i]#--port=}" ;;
        --help|-h)
            echo "Usage: bash scripts/model/start_host_service.sh [start_host.sh model arguments]"
            echo "Starts ${unit}; defaults to loopback port 18191. Reuse a healthy existing service."
            echo "For model arguments: bash scripts/model/start_host.sh --help"
            exit 0
            ;;
    esac
done

for tool in systemctl systemd-run; do
    command -v "${tool}" >/dev/null || { echo "${tool} is required; use start_host.sh in a foreground WSL terminal otherwise." >&2; exit 1; }
done
if ! command -v "${python_bin}" >/dev/null; then
    echo "Model Python not found: ${python_bin}. Set DF_MODEL_PYTHON to the prepared environment." >&2
    exit 1
fi
# Fail before any model loading if the user manager or endpoint is unavailable.
if ! systemctl --user show-environment >/dev/null; then
    echo "A running systemd user manager is required. Use start_host.sh in a foreground WSL terminal otherwise." >&2
    exit 1
fi
load_state="$(systemctl --user show "${unit}" --property=LoadState --value 2>/dev/null || true)"
if [[ "${load_state}" != "not-found" ]]; then
    echo "${unit} already exists or could not be inspected (LoadState=${load_state:-unknown})." >&2
    echo "Inspect it with: systemctl --user status ${unit}" >&2
    echo "Reuse it if healthy; stop it explicitly before starting a replacement. No service was changed." >&2
    exit 1
fi
"${python_bin}" - "${port}" <<'PY'
import socket
import sys

try:
    port = int(sys.argv[1])
    if not 1 <= port <= 65535:
        raise ValueError
except ValueError:
    raise SystemExit("--port must be an integer from 1 to 65535.")
try:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as probe:
        probe.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        probe.bind(("127.0.0.1", port))
except OSError as exc:
    raise SystemExit(f"Cannot reserve loopback port {port}: {exc}. Reuse or explicitly stop its owner; no service was changed.")
PY

# Arrays preserve spaces and literal shell characters in model arguments and env.
# The foreground launcher adds repo/src to this inherited PYTHONPATH itself.
systemd-run --user --unit="${unit}" --collect --expand-environment=no \
    --property=Type=exec --property=Restart=on-failure --property=RestartSec=5 \
    --property=StartLimitIntervalSec=120 --property=StartLimitBurst=3 \
    --working-directory="${repo_root}" \
    --setenv="DF_MODEL_PYTHON=${python_bin}" \
    --setenv="DF_BASE_MODEL=${model_dir}" \
    --setenv="PYTHONPATH=${PYTHONPATH-}" \
    /usr/bin/bash "${script_dir}/start_host.sh" "${args[@]}"

echo "STARTING ${unit}; model loading continues in the background. This does not mean ready."
echo "Ready check: curl --fail http://127.0.0.1:${port}/health"
echo "Status: systemctl --user status ${unit}"
echo "Logs:   journalctl --user -u ${unit} -n 80 --no-pager"
echo "Stop:   systemctl --user stop ${unit}"
echo "The service survives this shell/Codex exit for the current WSL session; it is not installed for reboot startup."
