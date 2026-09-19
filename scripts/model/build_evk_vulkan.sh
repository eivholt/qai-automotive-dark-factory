#!/usr/bin/env bash
# Isolated build; no system packages, drivers or existing binaries are replaced.
set -euo pipefail
[[ "$(uname -m)" == aarch64 ]] || { echo 'Run on the prepared EVK.' >&2; exit 2; }
root="${DF_EDGE_ROOT:-${HOME}/qai-dark-factory}/runtime"
src="${root}/llama-0eadefebd3f8f92a86d634a0e5b8fffc9dc792c0"
[[ -f "${src}/CMakeLists.txt" ]] || { echo 'Prepare the pinned source with build_evk_llama.sh first.' >&2; exit 2; }
[[ "$(git -C "$src" rev-parse HEAD)" == 0eadefebd3f8f92a86d634a0e5b8fffc9dc792c0 ]] || { echo 'Source revision differs from the tested build.' >&2; exit 2; }
deps="${root}/vulkan-build-deps"
mkdir -p "${deps}/packages" "${deps}/root"
cd "${deps}/packages"
apt-get download glslc=2023.8-1build1 libshaderc1=2023.8-1build1 \
  libvulkan-dev=1.3.275.0-1build1 spirv-headers=1.6.1+1.4.309.0-1~ubuntu0.24.04.2
for package in ./*.deb; do dpkg-deb -x "$package" "${deps}/root"; done
export LD_LIBRARY_PATH="${deps}/root/usr/lib/aarch64-linux-gnu${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"
cmake -S "$src" -B "${src}/build-vulkan" -DCMAKE_BUILD_TYPE=Release \
  -DGGML_BACKEND_DL=ON -DGGML_NATIVE=ON -DLLAMA_CURL=OFF -DLLAMA_BUILD_TESTS=OFF \
  -DGGML_VULKAN=ON -DCMAKE_PREFIX_PATH="${deps}/root/usr" \
  -DVulkan_INCLUDE_DIR="${deps}/root/usr/include" \
  -DVulkan_LIBRARY=/usr/lib/aarch64-linux-gnu/libvulkan.so.1 \
  -DVulkan_GLSLC_EXECUTABLE="${deps}/root/usr/bin/glslc"
cmake --build "${src}/build-vulkan" --target llama-server llama-bench -j "${DF_EDGE_BUILD_JOBS:-4}"
