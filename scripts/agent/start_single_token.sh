#!/usr/bin/env bash
# Protocol adapter only; start the hash-pinned Ministral backend separately.
set -euo pipefail
repo="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
cache_args=()
[[ -z "${DF_AGENT_CACHE_DIR:-}" ]] || cache_args=(--policy-cache "$DF_AGENT_CACHE_DIR")
exec python3 -u "${repo}/scripts/agent/serve_single_token.py" \
  --backend "${DF_SINGLE_TOKEN_BACKEND:-http://127.0.0.1:18195}" \
  --port "${DF_SINGLE_TOKEN_PORT:-18196}" --config "${repo}/configs/live_agent.json" "${cache_args[@]}" "$@"
