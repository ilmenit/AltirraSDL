# Altirra Libretro Core

This directory contains the RetroArch/libretro adapter for Altirra.

The target builds a shared library named `altirra_libretro` with no platform
library prefix:

- Linux: `altirra_libretro.so`
- Windows: `altirra_libretro.dll`
- macOS: `altirra_libretro.dylib`

Build it with:

```sh
./build.sh --libretro
```

For the Flathub/Flatpak RetroArch build, do not distribute a core built on the
host system. Build inside the matching Flatpak SDK so the shared library links
against the same glibc/libstdc++ ABI that RetroArch will load:

```sh
flatpak install flathub org.kde.Sdk//6.10
./build.sh --libretro-flatpak
```

Use `./build.sh --libretro-flatpak --package` to create a package named with a
`flatpak-kde610` suffix. This artifact is intended for RetroArch Flatpak users;
keep it separate from the generic host-native Linux libretro package.

Use the build that matches the RetroArch distribution:

- Flathub/Flatpak RetroArch: `./build.sh --libretro-flatpak --package`
- Locally installed RetroArch on the same or newer Linux distribution:
  `./build.sh --libretro --package`
- Cross-distro Linux distribution: build in the oldest Linux runtime/sysroot
  you intend to support, then package that output separately.

Before shipping a Linux core, check the required symbol versions:

```sh
readelf --version-info altirra_libretro.so | grep -E 'GLIBC_|GLIBCXX_'
```

The Flatpak build script rejects outputs that require a glibc newer than the
RetroArch Flatpak runtime. A host-native Linux build does not, because it is
intended for local/native use and may legitimately depend on the host ABI.

GitHub Actions packages the standalone core for:

- Linux x86_64
- Linux aarch64
- Linux ARMv7 hard-float (`armv7hf`)
- Android ARM64 (`arm64-v8a`)
- macOS arm64 and x86_64
- Windows x86_64 and ARM64

Local cross-build presets are also available:

```sh
# Requires gcc-arm-linux-gnueabihf / g++-arm-linux-gnueabihf.
cmake --preset linux-libretro-armv7hf
cmake --build --preset linux-libretro-armv7hf

# Requires ANDROID_NDK_HOME to point at an Android NDK.
cmake --preset android-libretro-arm64
cmake --build --preset android-libretro-arm64
```

The build output directory contains both files RetroArch needs:

- `altirra_libretro.so` / `altirra_libretro.dll` / `altirra_libretro.dylib`
- `altirra_libretro.info`

Install the shared library into RetroArch's cores directory and install
`altirra_libretro.info` into RetroArch's Core Info directory. If the `.info`
file is missing or in the wrong directory, RetroArch can still load the shared
library directly, but it will show the filename instead of `Atari - 8-bit /
5200 (Altirra)` and its content browser may not filter for Atari extensions.

RetroArch's `Install or Restore a Core` action may import only the shared
library. It does not reliably install a sidecar `.info` file from the same
directory. For packaged builds, run:

```sh
./install-retroarch.sh
```

On the Flatpak RetroArch build, check the active paths in
`~/.var/app/org.libretro.RetroArch/config/retroarch/retroarch.cfg`:

```ini
libretro_directory = "..."
libretro_info_path = "..."
```

Logs are written under the configured `logs` directory only when RetroArch
logging is enabled.

`libretro/libretro.h` is a vendored subset of the canonical libretro ABI header
covering only the interfaces used by this adapter:

https://github.com/libretro/libretro-common/blob/master/include/libretro.h

The subset was checked against the upstream `master` header during this
implementation pass on 2026-06-23. The local browser/API path did not expose a
verifiable commit SHA, so no SHA is recorded here. When refreshing this file,
fetch the canonical header from upstream, record the exact commit SHA in this
README, copy only the required ABI declarations/constants, and keep the MIT
license notice in the header comment.

After refreshing `libretro.h`, rebuild the standalone core, run the smoke test,
and verify the exported symbol list remains limited to `retro_*`.
