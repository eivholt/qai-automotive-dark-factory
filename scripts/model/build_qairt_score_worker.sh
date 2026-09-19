#!/usr/bin/env bash
# Native ARM64 build; headers are from GenieX 2c2bde3a and its pinned submodules.
set -euo pipefail
headers="${1:?Usage: build_qairt_score_worker.sh HEADERS_ROOT QAIRT_PLUGIN_LIBS OUTPUT}"
libs="${2:?Provide the matching GenieX v0.3.17 qairt library directory}"
output="${3:?Provide a private executable output path}"
[[ "$(uname -m)" == aarch64 ]] || { echo 'Build on the EVK.' >&2; exit 2; }
source_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
source_file="${4:-${source_dir}/qairt_score_worker.cpp}"
q="${headers}/third-party/geniex-qairt"
proc="${q}/third-party/geniex-proc"
g++ -O2 -std=c++20 \
  -I"${q}/core/include" -I"${q}/models" -I"${q}/qnn-api/include" \
  -I"${q}/qnn-api/include/HTP" -I"${q}/qnn-api/include/System" \
  -I"${q}/qnn-api/src" -I"${q}/qnn-api/src/config" \
  -I"${proc}/include" -I"${proc}/third-party/xtensor/include" -I"${proc}/third-party/xtl/include" \
  -I"${headers}/sdk/include/external" "${source_file}" \
  -L"${libs}" -lgeniex_vlm -lgeniex_core -lgeniex-proc-vision -lgeniex-proc \
  -Wl,-rpath,"${libs}" -o "${output}"
