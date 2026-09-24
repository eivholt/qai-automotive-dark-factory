#!/usr/bin/env bash
# Opt-in host cross-build of the pinned Vulkan plugin, with Q6_K signed scale
# loads expressed in int32. No device access, sudo or package installation.
# Usage (Ubuntu 24.04 x86_64 / WSL): bash scripts/model/build_private_q6_vulkan.sh
# Set DF_Q6_VARIANT=baseline for an unmodified control using the same compiler.
set -euo pipefail

[[ "$(uname -m)" == x86_64 ]] || { echo 'Run on the x86_64 Ubuntu build host.' >&2; exit 2; }
script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
build_root="${DF_Q6_BUILD_ROOT:-${HOME}/dark-factory-ministral/q6-vulkan}"
variant="${DF_Q6_VARIANT:-scale32}"
jobs="${DF_Q6_BUILD_JOBS:-16}"
revision=0eadefebd3f8f92a86d634a0e5b8fffc9dc792c0
source_digest=19e38c290e4e97784c77e393e4b60593c84ff7c9c5c376d16dea6d459172e0fd
case "$variant" in scale32|baseline) ;; *) echo 'DF_Q6_VARIANT must be scale32 or baseline.' >&2; exit 2;; esac
for command_name in cmake ninja curl patch dpkg-deb python3 sha256sum \
  aarch64-linux-gnu-gcc-13 aarch64-linux-gnu-g++-13 aarch64-linux-gnu-readelf; do
  command -v "$command_name" >/dev/null || { echo "Missing host prerequisite: $command_name" >&2; exit 2; }
done
mkdir -p "$build_root"/{downloads,host,target,logs}
build_root="$(realpath "$build_root")"
exec > >(tee -a "$build_root/logs/build-$variant.log") 2>&1

fetch() {
  local filename="$1" url="$2" digest="$3"
  if [[ ! -f "$build_root/downloads/$filename" ]]; then
    curl --fail --location --retry 3 "$url" -o "$build_root/downloads/$filename.part"
    mv "$build_root/downloads/$filename.part" "$build_root/downloads/$filename"
  fi
  printf '%s  %s\n' "$digest" "$build_root/downloads/$filename" | sha256sum -c -
}
fetch llama.tar.gz "https://codeload.github.com/ggml-org/llama.cpp/tar.gz/$revision" "$source_digest"
# Ubuntu package hashes are pinned; these packages are extracted privately.
fetch glslc.deb https://archive.ubuntu.com/ubuntu/pool/universe/s/shaderc/glslc_2023.8-1build1_amd64.deb \
  ee5f737e570b4b1713b4db9c8f9ead8258726af457391af00264350e59810bd4
fetch libshaderc1.deb https://archive.ubuntu.com/ubuntu/pool/universe/s/shaderc/libshaderc1_2023.8-1build1_amd64.deb \
  ebd699ab3feaf00676a7da0644dd74290f60cd1b34057256db90f60e126655b1
fetch libvulkan-dev.deb https://archive.ubuntu.com/ubuntu/pool/main/v/vulkan-loader/libvulkan-dev_1.3.275.0-1build1_amd64.deb \
  9577a2c5379372ca8231e700728390fa7d4991a0d8c6484ad83da849c728e76e
fetch spirv-headers.deb https://archive.ubuntu.com/ubuntu/pool/universe/s/spirv-headers/spirv-headers_1.6.1+1.4.309.0-1~ubuntu0.24.04.2_all.deb \
  6b5ed445af49a980584a0b294b0b9af60b7d9d5778896a2a3c6b24be0fd639ca
fetch libvulkan1-arm64.deb https://ports.ubuntu.com/ubuntu-ports/pool/main/v/vulkan-loader/libvulkan1_1.3.275.0-1build1_arm64.deb \
  9459488a53ead304d00587c874f9b31d56d59c9aeae5c46b6f78801a89d53e63
for name in glslc libshaderc1 libvulkan-dev spirv-headers; do
  dpkg-deb -x "$build_root/downloads/$name.deb" "$build_root/host"
done
dpkg-deb -x "$build_root/downloads/libvulkan1-arm64.deb" "$build_root/target"

patch_digest="$(sha256sum "$script_dir/llama-q6-scale32.patch" | cut -d' ' -f1)"
source_id="$revision-$variant"
[[ "$variant" == baseline ]] || source_id="$source_id-${patch_digest:0:16}"
source_dir="$build_root/source-$source_id"
if [[ ! -f "$build_root/source-$source_id.sha256" ]]; then
  [[ ! -e "$source_dir" ]] || { echo "Incomplete source tree exists: $source_dir; choose another DF_Q6_BUILD_ROOT." >&2; exit 2; }
  mkdir "$source_dir"
  tar -xf "$build_root/downloads/llama.tar.gz" --strip-components=1 -C "$source_dir"
  if [[ "$variant" == scale32 ]]; then
    patch --directory "$source_dir" --strip=1 --batch < "$script_dir/llama-q6-scale32.patch"
  fi
  (cd "$source_dir"; find . -type f -print0 | sort -z | xargs -0 sha256sum) > "$build_root/source-$source_id.sha256"
fi
(cd "$source_dir"; sha256sum --quiet -c "$build_root/source-$source_id.sha256")

cat > "$build_root/aarch64.cmake" <<'EOF'
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CMAKE_C_COMPILER /usr/bin/aarch64-linux-gnu-gcc-13)
set(CMAKE_CXX_COMPILER /usr/bin/aarch64-linux-gnu-g++-13)
set(CMAKE_FIND_ROOT_PATH /usr/aarch64-linux-gnu)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
EOF
mkdir -p "$build_root/wrapper-$source_id"
cat > "$build_root/wrapper-$source_id/CMakeLists.txt" <<EOF
cmake_minimum_required(VERSION 3.19)
project(private_q6_vulkan C CXX ASM)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "\${CMAKE_BINARY_DIR}/bin")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "\${CMAKE_BINARY_DIR}/bin")
add_subdirectory("$source_dir/ggml" ggml)
EOF
export LD_LIBRARY_PATH="$build_root/host/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
cmake -S "$build_root/wrapper-$source_id" -B "$build_root/build-$source_id" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$build_root/aarch64.cmake" -DCMAKE_BUILD_TYPE=Release \
  -DGGML_NATIVE=OFF -DGGML_OPENMP=OFF -DGGML_CPU=ON \
  -DGGML_BACKEND_DL=ON -DGGML_VULKAN=ON -DGGML_BUILD_TESTS=OFF -DGGML_BUILD_EXAMPLES=OFF \
  -DVulkan_INCLUDE_DIR="$build_root/host/usr/include" \
  -DVulkan_LIBRARY="$build_root/target/usr/lib/aarch64-linux-gnu/libvulkan.so.1" \
  -DVulkan_GLSLC_EXECUTABLE="$build_root/host/usr/bin/glslc" \
  -DSPIRV-Headers_DIR="$build_root/host/usr/share/cmake/SPIRV-Headers"
cmake --build "$build_root/build-$source_id" -j "$jobs" --target ggml-vulkan
package_dir="$build_root/q6-$variant-package"
mkdir -p "$package_dir"
cp "$build_root/build-$source_id/bin/libggml-vulkan.so" "$package_dir/"
cp "$script_dir/llama-q6-scale32.patch" "$package_dir/"
cp "$build_root/source-$source_id.sha256" "$package_dir/source.sha256"
cat > "$package_dir/PROVENANCE.txt" <<EOF
llama.cpp revision: $revision
Source archive SHA256: $source_digest
Variant: $variant
Patch SHA256: $patch_digest (applied only to scale32 variant)
GLSL compiler: Ubuntu glslc 2023.8-1build1
Cross compiler: $(aarch64-linux-gnu-g++-13 --version | head -n1)
Original GGUF format and all tensor values are unchanged.
EOF
cat > "$package_dir/USAGE.txt" <<'EOF'
Opt-in diagnostic plugin for the same pinned llama.cpp runtime only.
Dependencies: libggml-base.so.0 from that runtime, libvulkan.so.1, libstdc++.so.6,
libm.so.6, libgcc_s.so.1, libc.so.6 and ld-linux-aarch64.so.1.
No system package, service or launcher is modified by this build.
GGML_BACKEND_PATH is additive: use it only when normal discovery cannot load
another Vulkan backend (as with the isolated backend-op tester). For a model
server, copy the pinned executable into a private bin directory with its
runtime libraries and only this Vulkan plugin; do not symlink the executable
back into a directory containing the original Vulkan plugin.
Keep that isolated bin directory in LD_LIBRARY_PATH and select the private
Turnip ICD with its with-turnip wrapper (VK_DRIVER_FILES and LD_LIBRARY_PATH).
On the Adreno 663 experiment also set tu_restrict_subgroup_size_64=true and
verify startup actually reports warp size 64. This switch is independent of
the Q6 fix. Run numerical backend-op tests and actual prompts before promotion.
DF_Q6_VARIANT=baseline builds the unmodified source using the same toolchain.
EOF
(cd "$package_dir"; sha256sum libggml-vulkan.so llama-q6-scale32.patch source.sha256 PROVENANCE.txt USAGE.txt > SHA256SUMS)
aarch64-linux-gnu-readelf -h -d "$package_dir/libggml-vulkan.so" > "$package_dir/elf-audit.txt"
tar -C "$build_root" -czf "$build_root/q6-$variant-aarch64.tar.gz" "q6-$variant-package"
sha256sum "$build_root/q6-$variant-aarch64.tar.gz"
