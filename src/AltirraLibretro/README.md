# Altirra Libretro Core

This directory contains the RetroArch/libretro adapter for Altirra.

The target builds a shared library named `altirra_libretro` with no platform
library prefix:

- Linux: `altirra_libretro.so`
- Windows: `altirra_libretro.dll`
- macOS: `altirra_libretro.dylib`

Build it with:

```sh
cmake -S . -B build-libretro -DALTIRRA_LIBRETRO=ON -DALTIRRA_SDL3=OFF -DALTIRRA_LIBRETRO_NO_SDL3=ON
cmake --build build-libretro --target AltirraLibretro
```

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
