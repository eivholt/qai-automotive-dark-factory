#!/usr/bin/env bash
# Build only. Does not load a model, change services, or modify either QAIRT SDK.
set -euo pipefail
sdk="${1:?Usage: build_genie_single_token_worker.sh QAIRT_247_SDK OUTPUT [QAIRT_LIB_DIRECTORY]}"
output="${2:?Provide an output executable path}"
[[ "$(uname -m)" == aarch64 ]] || { echo 'Build on the EVK with its native ARM64 compiler.' >&2; exit 2; }
libs="${3:-${sdk}/lib/aarch64-oe-linux-gcc11.2}"
source_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
json_include="${DF_GENIE_JSON_INCLUDE:-${sdk}/examples/Genie/Genie/src/qualla/include}"
[[ -f "${sdk}/include/Genie/GenieSampler.h" && -f "${libs}/libGenie.so" && -f "${json_include}/nlohmann/json.hpp" ]] || {
  echo 'Require QAIRT 2.47 headers, matching ARM64 libraries and nlohmann/json.hpp.' >&2; exit 2;
}
g++ -O2 -std=c++17 -Wall -Wextra -Werror \
  -I"${sdk}/include" -I"${json_include}" \
  "${source_dir}/genie_single_token_worker.cpp" \
  -L"${libs}" -Wl,-rpath,"${libs}" -lGenie -lcrypto -ldl -o "${output}"
