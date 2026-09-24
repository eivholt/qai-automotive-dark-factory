#!/usr/bin/env bash
# Opt-in host cross-build of a pinned IQ9 CPU backend. No device access,
# model changes, package installation or system-runtime replacement.
# Variants: omp-control (default), noomp, tile16, noomp-tile16.
set -euo pipefail

[[ "$(uname -m)" == x86_64 ]] || { echo 'Run on the x86_64 Ubuntu build host.' >&2; exit 2; }
build_root="${DF_CPU_BUILD_ROOT:-${HOME}/dark-factory-ministral/cpu-private}"
variant="${DF_CPU_VARIANT:-omp-control}"
jobs="${DF_CPU_BUILD_JOBS:-8}"
revision=0eadefebd3f8f92a86d634a0e5b8fffc9dc792c0
source_digest=19e38c290e4e97784c77e393e4b60593c84ff7c9c5c376d16dea6d459172e0fd
case "$variant" in omp-control|noomp|tile16|noomp-tile16) ;; *) echo 'DF_CPU_VARIANT must be omp-control, noomp, tile16 or noomp-tile16.' >&2; exit 2;; esac
[[ "$jobs" =~ ^[1-9][0-9]*$ ]] || { echo 'DF_CPU_BUILD_JOBS must be a positive integer.' >&2; exit 2; }
for command_name in cmake ninja curl tar patch python3 sha256sum \
  aarch64-linux-gnu-gcc-13 aarch64-linux-gnu-g++-13 aarch64-linux-gnu-readelf; do
  command -v "$command_name" >/dev/null || { echo "Missing host prerequisite: $command_name" >&2; exit 2; }
done
mkdir -p "$build_root"/{downloads,logs}
build_root="$(realpath "$build_root")"
exec > >(tee -a "$build_root/logs/build-$variant.log") 2>&1

archive="$build_root/downloads/llama-$revision.tar.gz"
if [[ ! -f "$archive" ]]; then
  if [[ -n "${DF_CPU_SOURCE_ARCHIVE:-}" ]]; then
    # Optional existing archive avoids a repeated download; the same digest is required.
    cp -- "$DF_CPU_SOURCE_ARCHIVE" "$archive.part"
  else
    curl --fail --location --retry 3 \
      "https://codeload.github.com/ggml-org/llama.cpp/tar.gz/$revision" -o "$archive.part"
  fi
  printf '%s  %s\n' "$source_digest" "$archive.part" | sha256sum -c -
  mv -- "$archive.part" "$archive"
fi
printf '%s  %s\n' "$source_digest" "$archive" | sha256sum -c -

cat > "$build_root/fa-query-tile16.patch" <<'EOF'
--- a/ggml/src/ggml-cpu/common.h
+++ b/ggml/src/ggml-cpu/common.h
@@ -6,7 +6,7 @@
 #include "ggml-impl.h"
 #include "simd-mappings.h"
 
-#define GGML_FA_TILE_Q  64
+#define GGML_FA_TILE_Q  16
 #define GGML_FA_TILE_KV 64
 
 #ifdef __cplusplus
EOF
patch_digest="$(sha256sum "$build_root/fa-query-tile16.patch" | cut -d' ' -f1)"
source_id="$revision-$variant-dl"
source_dir="$build_root/source-$source_id"
source_manifest="$build_root/source-$source_id.sha256"
if [[ ! -f "$source_manifest" ]]; then
  [[ ! -e "$source_dir" ]] || { echo "Incomplete source tree exists: $source_dir; choose another DF_CPU_BUILD_ROOT." >&2; exit 2; }
  mkdir "$source_dir"
  tar -xf "$archive" --strip-components=1 -C "$source_dir"
  if [[ "$variant" == *tile16 ]]; then
    patch --directory "$source_dir" --strip=1 --batch < "$build_root/fa-query-tile16.patch"
  fi
  (cd "$source_dir" && find . -type f -print0 | sort -z | xargs -0 sha256sum) > "$source_manifest"
fi
(cd "$source_dir" && sha256sum --quiet -c "$source_manifest")

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
# Configure only ggml, so server Web UI provisioning and unrelated tools never run.
mkdir -p "$build_root/wrapper-$source_id"
cat > "$build_root/wrapper-$source_id/CMakeLists.txt" <<EOF
cmake_minimum_required(VERSION 3.19)
project(private_iq9_cpu C CXX ASM)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "\${CMAKE_BINARY_DIR}/bin")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "\${CMAKE_BINARY_DIR}/bin")
add_subdirectory("$source_dir/ggml" ggml)
EOF
openmp=ON
[[ "$variant" != noomp* ]] || openmp=OFF
arch='-mcpu=cortex-a78c+crc+crypto+nossbs+dotprod+noi8mm+nosve'
cmake -S "$build_root/wrapper-$source_id" -B "$build_root/build-$source_id" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$build_root/aarch64.cmake" -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_FLAGS="$arch" -DCMAKE_CXX_FLAGS="$arch" -DBUILD_SHARED_LIBS=ON \
  -DGGML_NATIVE=OFF -DGGML_OPENMP="$openmp" -DGGML_CPU=ON \
  -DGGML_CPU_REPACK=ON -DGGML_LLAMAFILE=ON -DGGML_CPU_KLEIDIAI=OFF \
  -DGGML_CPU_ALL_VARIANTS=OFF -DGGML_BACKEND_DL=ON \
  -DGGML_VULKAN=OFF -DGGML_CUDA=OFF -DGGML_METAL=OFF -DGGML_BLAS=OFF \
  -DGGML_BUILD_TESTS=OFF -DGGML_BUILD_EXAMPLES=OFF
cmake --build "$build_root/build-$source_id" -j "$jobs" --target ggml-cpu

package_dir="$build_root/cpu-dl-$variant-package"
mkdir -p "$package_dir"
cp -a "$build_root/build-$source_id/bin/libggml-cpu.so" "$package_dir/"
# The original server discovers CPU backends dynamically. A linked-only library
# builds successfully but cannot register with that loader.
aarch64-linux-gnu-readelf --dyn-syms --wide "$package_dir/libggml-cpu.so" | \
  grep -E '[[:space:]]GLOBAL[[:space:]]+DEFAULT[[:space:]]+[0-9]+[[:space:]]+ggml_backend_init$' >/dev/null || {
    echo 'CPU library is missing its dynamic ggml_backend_init entry point.' >&2; exit 2;
  }
cp "$build_root/build-$source_id/CMakeCache.txt" "$build_root/aarch64.cmake" "$package_dir/"
cp "$source_manifest" "$package_dir/source.sha256"
cp "$build_root/fa-query-tile16.patch" "$package_dir/"
cp -- "${BASH_SOURCE[0]}" "$package_dir/build_private_cpu.sh"
cat > "$package_dir/PROVENANCE.txt" <<EOF
llama.cpp revision: $revision
Source archive SHA256: $source_digest
Variant: $variant
Source patch SHA256: $patch_digest (applied to tile16 and noomp-tile16)
Cross compiler: $(aarch64-linux-gnu-g++-13 --version | head -n1)
Architecture flags: $arch
GGML_BACKEND_DL=ON; GGML_OPENMP=$openmp; CPU REPACK/LLAMAFILE enabled; all accelerators disabled.
Only the CPU shared library is packaged. Model files and prompt are not inputs to this build.
Build log: $build_root/logs/build-$variant.log
EOF
cat > "$package_dir/USAGE.txt" <<'EOF'
Opt-in diagnostic dynamic CPU plugin for the same pinned llama.cpp revision only.
Create an isolated copy of the original server and runtime libraries, then replace
only libggml-cpu.so from this package. Keep a single CPU plugin in that directory.
The package exports ggml_backend_init for the original loader. Do not change the
original service or its runtime directory. Set LD_LIBRARY_PATH to the isolated bin.
Do not use GGML_BACKEND_PATH as a replacement mechanism; it is additive.
The OpenMP variants need the existing libgomp.so.1; noomp/noomp-tile16 do not.
Preserve weights, prompt, sampler/global-vocabulary argmax, KV types and server
arguments during comparison. Check runtime readiness, exact model-visible histories,
raw action winners and numerical backend cases before any deployment decision.
tile16/noomp-tile16 change CPU flash-attention query tiles 64 -> 16; KV tiles remain 64.
For 16-63 query rows it enables the existing tiled F32 query/accumulation path instead
of the row-wise F16 query/F16 value accumulator when KV uses F16. This is a numerical
path change, not only scheduling. Planner and kernel are rebuilt together from the
same header. It retains all keys, values and masks; no context pruning/action masking.
The performance benefit is unproven until measured on the target workload. Smaller
tiles can increase K/V packing overhead on longer prefills. Build success is not
numerical correctness, process-policy correctness, or measured model acceleration.
EOF
aarch64-linux-gnu-readelf -h -d "$package_dir/libggml-cpu.so" > "$package_dir/elf-audit.txt"
(cd "$package_dir" && find . -maxdepth 1 -type f ! -name SHA256SUMS -print0 | sort -z | xargs -0 sha256sum > SHA256SUMS)
tar -C "$build_root" -czf "$build_root/cpu-dl-$variant-aarch64.tar.gz" "cpu-dl-$variant-package"
sha256sum "$build_root/cpu-dl-$variant-aarch64.tar.gz"
