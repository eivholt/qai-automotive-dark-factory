#!/usr/bin/env bash
# Run ON the EVK. Never replaces system runtimes or another project's services.
set -euo pipefail
[[ "$(uname -m)" == aarch64 ]] || { echo "Run this build on the ARM64 EVK" >&2; exit 2; }
root="${DF_EDGE_RUNTIME_ROOT:-${HOME}/qai-dark-factory/runtime}"
rev=0eadefebd3f8f92a86d634a0e5b8fffc9dc792c0
src="${root}/llama-${rev}"
mkdir -p "${root}"
if [[ ! -d "${src}" ]]; then
  curl --fail -L "https://github.com/ggml-org/llama.cpp/archive/${rev}.tar.gz" -o "${src}.tar.gz"
  mkdir "${src}"
  tar -xzf "${src}.tar.gz" --strip-components=1 -C "${src}"
fi
cmake -S "${src}" -B "${src}/build" -DCMAKE_BUILD_TYPE=Release \
  -DGGML_BACKEND_DL=ON -DGGML_NATIVE=ON -DLLAMA_CURL=OFF -DLLAMA_BUILD_TESTS=OFF
cmake --build "${src}/build" --target llama-server llama-cli llama-bench -j "${DF_EDGE_BUILD_JOBS:-6}"
# Optional adapter for GenieX v0.6.1's static-registration Hexagon backend.
# Its bundled llama.cpp revision must be the exact revision above. This loader
# is experimental: actual offload and decisions still require device validation.
if [[ -n "${DF_GENIEX_061_DIR:-}" ]]; then
  plugin="${DF_GENIEX_061_DIR}/llama_cpp"
  cat > "${root}/hexagon-loader.cpp" <<'EOF'
#include "ggml-backend.h"
extern "C" ggml_backend_reg_t ggml_backend_hexagon_reg(void);
extern "C" ggml_backend_reg_t ggml_backend_init(void) { return ggml_backend_hexagon_reg(); }
EOF
  g++ -shared -fPIC -I "${src}/ggml/include" "${root}/hexagon-loader.cpp" \
    -L "${plugin}" -lggml-hexagon -o "${root}/libggml-hexagon-loader.so"
  echo "Use LD_LIBRARY_PATH=${src}/build/bin:${plugin} (native ggml first)."
  echo "Use GGML_BACKEND_PATH=${root}/libggml-hexagon-loader.so and ADSP_LIBRARY_PATH=${plugin}."
fi
echo "DF_EDGE_LLAMA_BIN=${src}/build/bin"
