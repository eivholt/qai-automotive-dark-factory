# Private Turnip driver for IQ9 Vulkan experiments

`scripts/model/build_private_turnip.sh` cross-builds a headless Mesa 26.1.8
Turnip driver for Linux AArch64 with the MSM kernel backend. It runs entirely
on the Ubuntu 24.04 x86_64 WSL host. It neither contacts the EVK nor installs
packages or drivers into the host or target system. The output is selected
per process using `VK_DRIVER_FILES` and a private `LD_LIBRARY_PATH`.

Mesa 26.0.1's official release notes list a Turnip crash in llama.cpp's
`test-backend-ops` and a register-pressure correction in `ir3/ra`. Mesa
26.1.8 is a released bug-fix version containing the intervening changes.
This motivates an isolated driver comparison; it does not establish that
the observed Ministral crash has the same cause or that inference is correct.
Sources: [Mesa 26.0.1 notes](https://docs.mesa3d.org/relnotes/26.0.1.html),
[Mesa 26.1.8 notes and checksums](https://docs.mesa3d.org/relnotes/26.1.8.html).

## Pinned inputs and build

| Source archive | SHA256 |
| --- | --- |
| `mesa-26.1.8.tar.xz` | `b320f65874fd9653ac6c0bd1616605387344e1247411a50c797b5f3fb9dc0b55` |
| `libdrm-2.4.123.tar.xz` | `a2b98567a149a74b0f50e91e825f9c0315d86e7be9b74394dae8b298caadb79e` |
| `zlib-1.3.1.tar.gz` | `9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23` |

The Mesa checksum comes from its official release notes. The libdrm and zlib
checksums match the dependency wrap files in that verified Mesa archive.
Host-only bison, flex, m4, glslang and SPIR-V tools are extracted from pinned,
SHA256-checked Ubuntu archive packages. Meson and Python generator modules
use an isolated virtual environment with exact package versions.

The host must already provide GCC/G++ 13's AArch64 cross compiler, its
Ubuntu target C/C++ runtime and headers, cross binutils, native GCC/G++,
Ninja, make, pkg-config, curl, tar, dpkg-deb and uv. The script reports a
missing prerequisite instead of installing system packages. The compiler
names are `aarch64-linux-gnu-gcc-13` and `aarch64-linux-gnu-g++-13`.

From the repository's PowerShell terminal:

```powershell
wsl.exe -d Ubuntu -- bash -lc 'bash /mnt/c/Users/eivho/repos/qai-automotive-dark-factory/scripts/model/build_private_turnip.sh'
```

The default private build directory is
`/home/eivho/dark-factory-ministral/turnip-26.1.8`. Override
`TURNIP_BUILD_ROOT` or `JOBS` when needed. All downloaded sources, generated
files, logs and binaries remain there, outside Git. `logs/build.log` records
the build; the package contains Meson options, ELF requirements, source
provenance and a checksum manifest. Run with a new build directory when
changing compiler or configuration; existing build directories are reused.

Only Turnip/MSM is enabled. Gallium, OpenGL, EGL, GLX, display platforms,
LLVM, Perfetto and XML configuration support are disabled. Shader caching
remains enabled using private static zlib. Private static libdrm supplies
MSM interaction. No shared Mesa, libdrm or zlib dependency is shipped or
replaced. This is an inference experiment package, not a desktop graphics
driver distribution.

## Host build evidence, 19 September 2026

The host cross-build completed all 683 Mesa targets with GCC 13.3.0 and
Meson 1.9.1. The driver is an unstripped AArch64 ELF shared object with
build ID `c6656563e3ac1816e28a4d487f12ab3944f17f88` and SHA256
`f35be725604bf72fec16615493e8e8b3943d11bc111fea817c0722d2da92d446`.
The approximately 3.3 MiB archive is:

```text
/home/eivho/dark-factory-ministral/turnip-26.1.8/private-turnip-26.1.8-aarch64.tar.gz
SHA256 e5f289071b37bfbf82fb26e667254efa88540f35aae0092dde023316f375a192
```

Its direct ELF runtime dependencies are exactly:

| Library | Required symbol versions, upper bound |
| --- | --- |
| `libc.so.6` | `GLIBC_2.38` |
| `libm.so.6` | `GLIBC_2.29` |
| `libstdc++.so.6` | `GLIBCXX_3.4.29`, `CXXABI_1.3.9` |
| `libgcc_s.so.1` | No versioned requirement recorded |
| `ld-linux-aarch64.so.1` | `GLIBC_2.17` |

Ubuntu 24.04's ordinary AArch64 runtime satisfies these versions. The
application still needs a Vulkan loader; it is not bundled. The target
also needs its existing MSM kernel interface and GPU access. All package
checksums, the wrapper's environment selection, and shell syntax were
checked on the host. Host checks do not validate hardware execution.

## Per-process deployment and comparison

Transfer the archive privately into an experiment directory under
`~/qai-dark-factory` using the existing device access workflow. Extracting
creates a `package/` directory. On the target, the experiment owner can use:

```bash
mkdir -p ~/qai-dark-factory/turnip-26.1.8
tar -xzf private-turnip-26.1.8-aarch64.tar.gz \
  -C ~/qai-dark-factory/turnip-26.1.8
cd ~/qai-dark-factory/turnip-26.1.8/package
sha256sum -c SHA256SUMS
ldd lib/libvulkan_freedreno.so
./with-turnip vulkaninfo --summary
./with-turnip /path/to/llama-cli [existing experiment arguments]
```

`with-turnip` resolves its own directory, selects the bundled ICD, prepends
its private `lib` directory and execs the requested command. The ICD's
`../../../lib/libvulkan_freedreno.so` path is relative to the manifest, so
the package can move without editing it. This follows the
[Khronos loader's driver interface](https://github.com/KhronosGroup/Vulkan-Loader/blob/main/docs/LoaderDriverInterface.md).
Use ordinary user execution so the Vulkan loader honors the environment
override. The system ICD files, Mesa packages and library paths stay intact;
running the original command without `with-turnip` selects the original
environment again.

Record the detected driver name/version before comparing the same model,
batch, context, offload and decoding configuration. Device testing remains
separate from this host build. A successful source build or device listing
is not evidence that batched prefill, generated output, or repeated service
requests are correct.
