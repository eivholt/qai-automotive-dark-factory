#!/usr/bin/env bash
# Optional SDK 2.50 compiled Ministral on HTP1; leaves Reason2/CPU services alone.
set -euo pipefail
[[ "$(uname -m)" == aarch64 ]] || { echo 'Run on the prepared EVK.' >&2; exit 2; }
[[ $# -eq 0 || ( $# -eq 1 && "$1" == --check ) ]] || exit 2
: "${DF_GENIE_CONFIG:?Set the device-1 config from prepare_genie_device.py}"
: "${DF_GENIE_WORKER:?Set the validated compiled native worker}"
: "${DF_NATIVE_PROPERTIES:?Set the original native chat-template properties}"
: "${DF_QAIRT_SDK:?Set the matching QAIRT 2.50.40 SDK directory}"
history_format="${DF_AGENT_OBSERVATION_FORMAT:-named}"
[[ "$history_format" == json || "$history_format" == named ]] || { echo 'Use json or named history.' >&2; exit 2; }
examples=()
if [[ -n "${DF_AGENT_POLICY_EXAMPLES:-}" ]]; then
  [[ -f "$DF_AGENT_POLICY_EXAMPLES" && -r "$DF_AGENT_POLICY_EXAMPLES" ]] || { echo 'Missing policy examples file.' >&2; exit 2; }
  examples=(--policy-examples "$DF_AGENT_POLICY_EXAMPLES")
fi
repo="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
libs="${DF_QAIRT_SDK}/lib/aarch64-oe-linux-gcc11.2"
dsp="${DF_QAIRT_SDK}/lib/hexagon-v73/unsigned"
tokenizer=$(python3 - "$DF_GENIE_CONFIG" "$DF_GENIE_WORKER" "$DF_NATIVE_PROPERTIES" "$libs" "$dsp" <<'PY'
import json, sys
from pathlib import Path
config_path, worker, properties, libs, dsp = map(Path, sys.argv[1:])
config_path = config_path.resolve(strict=True)
config = json.loads(config_path.read_text())['dialog']
backend = config['engine']['backend']
assert backend['type'] == 'QnnHtp' and config['engine']['model']['type'] == 'binary'
extensions = json.loads((config_path.parent / backend['extensions']).read_text())
assert len(extensions['devices']) == 1
device = extensions['devices'][0]
assert device['device_id'] == 1 and [c['core_id'] for c in device['cores']] == [0], 'Require HTP1/core0'
tokenizer = (config_path.parent / config['tokenizer']['path']).resolve(strict=True)
paths = [worker, properties, libs/'libGenie.so', libs/'libQnnHtp.so', tokenizer]
contexts = config['engine']['model']['binary']['ctx-bins']
assert contexts and dsp.is_dir()
paths += [config_path.parent / p for p in contexts]
assert all(p.is_file() for p in paths), 'Missing native runtime/model dependency'
print(tokenizer)
PY
)
echo 'Prepared files target HTP1/core0; /health records actual loaded identities.'
[[ "${1:-}" != --check ]] || exit 0
python3 - <<'PY'
import socket
for port in (18206, 18197):
    with socket.socket() as s:
        if s.connect_ex(('127.0.0.1', port)) == 0:
            raise SystemExit(f'Port {port} is occupied; no existing service was changed.')
PY
systemd-run --user --unit=dark-factory-compiled-agent --collect -p MemoryMax=12G \
  env PYTHONUNBUFFERED=1 "LD_LIBRARY_PATH=$libs" "ADSP_LIBRARY_PATH=$dsp" \
  python3 "$repo/scripts/agent/serve_genie_backend.py" --worker "$DF_GENIE_WORKER" \
  --bundle-config "$DF_GENIE_CONFIG" --tokenizer "$tokenizer" --qairt-lib "$libs" \
  --template-properties "$DF_NATIVE_PROPERTIES" --port 18206 --cache-mode append_rewind
if ! python3 - <<'PY'
import json, time, urllib.request
deadline = time.monotonic() + 45
while time.monotonic() < deadline:
    try:
        with urllib.request.urlopen('http://127.0.0.1:18206/health', timeout=2) as r:
            h = json.load(r)
        assert h['status'] == 'ok' and h['cache_mode'] == 'append_rewind'
        assert h['worker']['prefix_rewind_api_supported']
        break
    except OSError:
        time.sleep(.5)
else:
    raise SystemExit('Compiled agent did not become ready; inspect its journal.')
PY
then
  systemctl --user stop dark-factory-compiled-agent
  exit 1
fi
qa=()
[[ -z "${DF_AGENT_QA_LOG_DIR:-}" ]] || qa=(--qa-log-dir "$DF_AGENT_QA_LOG_DIR")
systemd-run --user --unit=dark-factory-compiled-agent-single-token --collect -p MemoryMax=256M \
  python3 "$repo/scripts/agent/serve_single_token.py" --backend http://127.0.0.1:18206 \
  --port 18197 --config "$repo/configs/live_agent.json" --observation-format "$history_format" "${examples[@]}" "${qa[@]}"
if ! python3 - <<'PY'
import json, time, urllib.request
deadline = time.monotonic() + 15
while time.monotonic() < deadline:
    try:
        with urllib.request.urlopen('http://127.0.0.1:18197/health', timeout=2) as r:
            h = json.load(r)
        assert h['status'] == 'ok' and h['backend'] == 'http://127.0.0.1:18206'
        assert not h['benchmark_reset_cache'] and not h['next_turn_prefill']['enabled']
        break
    except OSError:
        time.sleep(.5)
else:
    raise SystemExit('One-token proxy did not become ready; inspect its journal.')
PY
then
  systemctl --user stop dark-factory-compiled-agent-single-token dark-factory-compiled-agent
  exit 1
fi
echo 'Optional HTP1 agent: native 18206, one-token proxy 18197. Inspect both /health endpoints.'
echo 'No boot autostart. CPU fallback and Reason2 remain unchanged.'
