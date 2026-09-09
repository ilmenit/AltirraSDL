# macOS Memory Leak Diagnostic Build

This document tracks the temporary instrumentation used to investigate the
large memory growth reported on an M5 MacBook Pro running macOS Tahoe 26.5.2.
All behavioral hooks are gated by `ALTIRRA_MAC_OS_LEAK_DEBUG`; normal builds
compile to no-op inline functions and retain their existing behavior.

## What was added

- `src/AltirraSDL/source/app/macos_leak_debug.h` declares the isolated hooks
  and supplies no-op implementations for normal builds.
- `src/AltirraSDL/source/app/macos_leak_debug.cpp` reads macOS task memory and
  malloc-zone statistics and prints a `MACLEAK DATA` line every ten seconds.
- `src/AltirraSDL/source/app/main_sdl3.cpp` contains the explicitly marked
  call sites around frame preparation, upload, render, and presentation.
- `src/AltirraSDL/CMakeLists.txt` defines the macOS-only, default-OFF
  `ALTIRRA_MAC_OS_LEAK_DEBUG` option.
- `run-macos-memory-diagnostic.sh` launches a packaged app in one of four
  controlled modes and saves the Terminal output.

Each report contains process physical footprint, resident/virtual/compressed
memory, default malloc-zone used/allocated bytes, pause and window state,
emulated frame count, render count, texture upload count, and cumulative bytes
submitted to the upload API.

## Reporter instructions

Download and unzip `AltirraSDL-macos-memory-diagnostic.zip`, then in Terminal:

```sh
xattr -cr /path/to/AltirraSDL-macos-memory-diagnostic/AltirraSDL.app
cd /path/to/AltirraSDL-macos-memory-diagnostic
./run-macos-memory-diagnostic.sh baseline
```

Reproduce the issue, quit normally, and send the generated
`AltirraSDL-memory-baseline-*.log` file. Run these additional modes only when
requested:

```sh
./run-macos-memory-diagnostic.sh new-frames-only
./run-macos-memory-diagnostic.sh no-upload
./run-macos-memory-diagnostic.sh no-present
```

- `baseline` logs without changing rendering behavior.
- `new-frames-only` skips repeated uploads of an unchanged cached frame.
- `no-upload` suppresses all emulator texture uploads; UI rendering continues.
- `no-present` suppresses buffer swaps after rendering and uploading.

These modes are diagnostic interventions, not proposed production behavior.

## Build locally

```sh
cmake --preset macos-debug \
  -DALTIRRA_FETCH_SDL3=ON \
  -DALTIRRA_ENABLE_FFMPEG_RECORDING=OFF \
  -DALTIRRA_FETCH_FFMPEG=OFF \
  -DALTIRRA_MAC_OS_LEAK_DEBUG=ON
cmake --build --preset macos-debug --target AltirraSDL
```

The option deliberately fails configuration on non-Apple platforms when ON.
Build once with the option OFF to verify the normal code path.

## Distribution

The diagnostic is no longer built or published by GitHub CI. Build it locally
with the commands above when investigating a macOS memory issue. Normal macOS
release builds continue to use `.github/workflows/macos.yml`.

## Reading results

- Physical footprint growing with malloc usage flat points to graphics/driver
  allocations rather than a conventional C++ heap leak.
- Flat growth in `new-frames-only` implicates redundant cached-frame uploads.
- Growth stopping only in `no-upload` implicates texture transfer generally.
- Growth continuing in `no-upload` but stopping in `no-present` implicates the
  OpenGL/Cocoa presentation path.
- Growth in all modes points outside emulator texture upload/presentation.

## Removal after resolution

Search for `MACLEAK`, `ALTIRRA_MAC_OS_LEAK_DEBUG`, and
`ATMacLeakDebug`. Remove the two diagnostic source files, the marked call
sites, the CMake option/source entry, launcher, workflow, and this document.
Do not retain the diagnostic environment-variable behavior in normal builds.
