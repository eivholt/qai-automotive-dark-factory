#!/usr/bin/env bash
# Cross-build an opt-in Turnip ICD on an Ubuntu 24.04 x86_64 host. No sudo,
# package installation, device access, or system driver replacement.
set -euo pipefail

build_root="${TURNIP_BUILD_ROOT:-/home/eivho/dark-factory-ministral/turnip-26.1.8}"
jobs="${JOBS:-16}"
mkdir -p "$build_root"/{downloads,host,deps,logs,package/lib,package/share/vulkan/icd.d}
build_root="$(realpath "$build_root")"
exec > >(tee -a "$build_root/logs/build.log") 2>&1

for command_name in aarch64-linux-gnu-gcc-13 aarch64-linux-gnu-g++-13 \
  aarch64-linux-gnu-ar aarch64-linux-gnu-readelf ninja pkg-config curl \
  dpkg-deb uv; do
  command -v "$command_name" >/dev/null || {
    printf 'Missing host prerequisite: %s\n' "$command_name" >&2; exit 1;
  }
done

fetch() {
  local filename="$1" url="$2" digest="$3"
  if [[ ! -f "$build_root/downloads/$filename" ]]; then
    curl --fail --location --retry 3 "$url" -o "$build_root/downloads/$filename.part"
    mv "$build_root/downloads/$filename.part" "$build_root/downloads/$filename"
  fi
  printf '%s  %s\n' "$digest" "$build_root/downloads/$filename" | sha256sum -c -
}

fetch mesa-26.1.8.tar.xz https://archive.mesa3d.org/mesa-26.1.8.tar.xz \
  b320f65874fd9653ac6c0bd1616605387344e1247411a50c797b5f3fb9dc0b55
fetch libdrm-2.4.123.tar.xz https://dri.freedesktop.org/libdrm/libdrm-2.4.123.tar.xz \
  a2b98567a149a74b0f50e91e825f9c0315d86e7be9b74394dae8b298caadb79e
fetch zlib-1.3.1.tar.gz https://zlib.net/fossils/zlib-1.3.1.tar.gz \
  9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23

# Trusted Ubuntu archive packages are only extracted under build_root/host.
# SHA256 values are recorded from Ubuntu's signed apt package indexes.
fetch bison.deb https://archive.ubuntu.com/ubuntu/pool/main/b/bison/bison_3.8.2+dfsg-1ubuntu0.24.04.1_amd64.deb \
  9d75084d1293f68f3c1a2ee677dea5baa4e010bac2d4c455d8e23177e97aa353
fetch flex.deb https://archive.ubuntu.com/ubuntu/pool/main/f/flex/flex_2.6.4-8.2build1_amd64.deb \
  c5604f55827b50e3f568b4731b7799f86b147454c867be02bc141d9d8ce561ff
fetch m4.deb https://archive.ubuntu.com/ubuntu/pool/main/m/m4/m4_1.4.19-4build1_amd64.deb \
  30ea715845a1863abd55d11e47ab91513130bf347852d4cdeae69e9d98fa795c
fetch glslang-tools.deb https://archive.ubuntu.com/ubuntu/pool/universe/g/glslang/glslang-tools_15.1.0-2~ubuntu0.24.04.2_amd64.deb \
  8767bba0e4cc781a5de0da3e662f656664cc3b8de2399aef6755bac61137727e
fetch spirv-tools.deb https://archive.ubuntu.com/ubuntu/pool/universe/s/spirv-tools/spirv-tools_2025.1~rc1-1~ubuntu0.24.04.2_amd64.deb \
  529e396c337d15beb8ed0f03fcbeb2a066e5a818e032e197b6ef2d3328dc7ae9
for name in bison flex m4 glslang-tools spirv-tools; do
  dpkg-deb -x "$build_root/downloads/$name.deb" "$build_root/host"
done

if [[ ! -x "$build_root/venv/bin/python" ]]; then
  uv venv --python /usr/bin/python3 "$build_root/venv"
fi
uv pip install --python "$build_root/venv/bin/python" \
  meson==1.9.1 Mako==1.3.10 MarkupSafe==3.0.3 PyYAML==6.0.3 packaging==25.0 ply==3.11
export PATH="$build_root/venv/bin:$build_root/host/usr/bin:$PATH"
export BISON_PKGDATADIR="$build_root/host/usr/share/bison"
export M4="$build_root/host/usr/bin/m4"

for archive in mesa-26.1.8.tar.xz libdrm-2.4.123.tar.xz zlib-1.3.1.tar.gz; do
  source_dir="${archive%.tar.*}"
  [[ -d "$build_root/$source_dir" ]] || tar -xf "$build_root/downloads/$archive" -C "$build_root"
done

cat > "$build_root/aarch64.ini" <<EOF
[binaries]
c = '/usr/bin/aarch64-linux-gnu-gcc-13'
cpp = '/usr/bin/aarch64-linux-gnu-g++-13'
ar = '/usr/bin/aarch64-linux-gnu-ar'
strip = '/usr/bin/aarch64-linux-gnu-strip'
pkg-config = '/usr/bin/pkg-config'
[host_machine]
system = 'linux'
cpu_family = 'aarch64'
cpu = 'aarch64'
endian = 'little'
[properties]
needs_exe_wrapper = true
pkg_config_libdir = ['$build_root/deps/lib/pkgconfig']
EOF

if [[ ! -f "$build_root/libdrm-build/build.ninja" ]]; then
  meson setup "$build_root/libdrm-build" "$build_root/libdrm-2.4.123" \
    --cross-file "$build_root/aarch64.ini" --prefix "$build_root/deps" \
    --libdir lib --buildtype release --default-library static \
    -Dintel=disabled -Dradeon=disabled -Damdgpu=disabled -Dnouveau=disabled \
    -Dvmwgfx=disabled -Domap=disabled -Dexynos=disabled -Dfreedreno=disabled \
    -Dtegra=disabled -Dvc4=disabled -Detnaviv=disabled -Dtests=false \
    -Dman-pages=disabled -Dvalgrind=disabled -Dcairo-tests=disabled
fi
meson compile -C "$build_root/libdrm-build" -j "$jobs"
meson install -C "$build_root/libdrm-build"

if [[ ! -f "$build_root/deps/lib/libz.a" ]]; then
  (
    cd "$build_root/zlib-1.3.1"
    CC=aarch64-linux-gnu-gcc-13 AR=aarch64-linux-gnu-ar \
      RANLIB=aarch64-linux-gnu-ranlib CFLAGS='-O2 -fPIC' \
      ./configure --static --prefix="$build_root/deps"
    make -j "$jobs"
    make install
  )
fi

if [[ ! -f "$build_root/mesa-build/build.ninja" ]]; then
  meson setup "$build_root/mesa-build" "$build_root/mesa-26.1.8" \
    --cross-file "$build_root/aarch64.ini" --prefix /opt/private-turnip \
    --libdir lib --buildtype release --wrap-mode nofallback \
    -Dplatforms=[] -Dgallium-drivers=[] -Dvulkan-drivers=freedreno \
    -Dfreedreno-kmds=msm -Dglx=disabled -Degl=disabled -Dgbm=disabled \
    -Dopengl=false -Dgles1=disabled -Dgles2=disabled -Dllvm=disabled \
    -Dexpat=disabled -Dxmlconfig=disabled -Dzstd=disabled -Dzlib=enabled \
    -Dshader-cache=enabled -Dbuild-tests=false -Dvideo-codecs=[] \
    -Dlibunwind=disabled -Dlmsensors=disabled -Dvalgrind=disabled \
    -Ddisplay-info=disabled -Dperfetto=false
fi
meson compile -C "$build_root/mesa-build" -j "$jobs"

install -m 755 "$build_root/mesa-build/src/freedreno/vulkan/libvulkan_freedreno.so" \
  "$build_root/package/lib/libvulkan_freedreno.so"
cat > "$build_root/package/share/vulkan/icd.d/freedreno_icd.aarch64.json" <<'EOF'
{
  "file_format_version": "1.0.0",
  "ICD": {
    "library_path": "../../../lib/libvulkan_freedreno.so",
    "api_version": "1.4.0"
  }
}
EOF
cat > "$build_root/package/with-turnip" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
private_turnip_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
export VK_DRIVER_FILES="$private_turnip_root/share/vulkan/icd.d/freedreno_icd.aarch64.json"
export LD_LIBRARY_PATH="$private_turnip_root/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
exec "$@"
EOF
chmod 755 "$build_root/package/with-turnip"

aarch64-linux-gnu-readelf -h -d -V "$build_root/package/lib/libvulkan_freedreno.so" \
  > "$build_root/package/elf-audit.txt"
cp "$build_root/mesa-build/meson-info/intro-buildoptions.json" "$build_root/package/build-options.json"
cp "$build_root/mesa-26.1.8/docs/license.rst" "$build_root/package/mesa-license.rst"
{
  printf 'Mesa 26.1.8, libdrm 2.4.123 (static), zlib 1.3.1 (static)\n'
  printf 'Host-only cross-build; no device test is implied.\n'
  aarch64-linux-gnu-gcc-13 --version | head -1
  meson --version
  sha256sum "$build_root/downloads/mesa-26.1.8.tar.xz" \
    "$build_root/downloads/libdrm-2.4.123.tar.xz" \
    "$build_root/downloads/zlib-1.3.1.tar.gz"
  aarch64-linux-gnu-readelf -d "$build_root/package/lib/libvulkan_freedreno.so" | grep NEEDED
} > "$build_root/package/provenance.txt"
(
  cd "$build_root/package"
  sha256sum lib/libvulkan_freedreno.so share/vulkan/icd.d/freedreno_icd.aarch64.json \
    with-turnip elf-audit.txt build-options.json mesa-license.rst provenance.txt > SHA256SUMS
)
tar -C "$build_root" -czf "$build_root/private-turnip-26.1.8-aarch64.tar.gz" package
sha256sum "$build_root/private-turnip-26.1.8-aarch64.tar.gz"
printf 'Private driver package: %s/package\n' "$build_root"
