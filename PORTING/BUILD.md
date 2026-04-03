# Build System

## Overview

The project supports two independent build paths that coexist in the repository:

- **Visual Studio `.sln`** — Native Win32 build (primary on Windows)
- **CMake** — Cross-platform build (Linux, macOS, Windows SDL, future: Android)

The CMake build never generates or modifies `.sln`/`.vcxproj` files. Both
build paths compile the same emulation core; they differ only in the
frontend (Win32 native UI vs. SDL3+ImGui).

## Build Matrix

| Platform       | OS APIs       | Frontend         | Build System |
|----------------|---------------|------------------|--------------|
| Windows native | Win32         | Altirra.exe      | `.sln`       |
| Windows SDL    | Win32         | AltirraSDL (SDL3+ImGui) | CMake |
| Linux          | POSIX + SDL3  | AltirraSDL (SDL3+ImGui) | CMake |
| macOS          | POSIX + SDL3  | AltirraSDL (SDL3+ImGui) | CMake |
| Android        | POSIX + SDL3  | AltirraSDL (SDL3+ImGui) | CMake + NDK |

Three independent axes control what gets compiled:

1. **Host OS** — `WIN32` / `APPLE` / `ANDROID` / `UNIX` (CMake built-ins).
   Selects system-level sources (file I/O, sockets, threading).
2. **Frontend** — `ALTIRRA_SDL3` option. Controls whether the SDL3+ImGui
   frontend is built.
3. **CPU architecture** — `CMAKE_SYSTEM_PROCESSOR`. Selects SIMD sources
   (SSE2/AVX2 vs NEON).

These axes are orthogonal: Windows+SDL3 uses Win32 OS APIs but the
SDL3+ImGui frontend.

## CMake Build

### Requirements

- CMake 3.24+
- C++20 compiler (GCC, Clang, or MSVC)
- SDL3 (system-installed or via vcpkg/conan)
- Dear ImGui (fetched automatically via FetchContent)

### Quick Start

```bash
# Linux
cmake --preset linux-release
cmake --build build/linux-release -j$(nproc)
./build/linux-release/src/AltirraSDL/AltirraSDL

# macOS
cmake --preset macos-release
cmake --build build/macos-release -j$(nproc)

# Windows SDL (from Developer Command Prompt or with MSVC on PATH)
cmake --preset windows-sdl-release
cmake --build build/windows-sdl-release --config Release
```

### CMake Options

| Option | Default | Description |
|--------|---------|-------------|
| `ALTIRRA_SDL3` | ON (non-Windows), OFF (Windows) | Build the SDL3+ImGui frontend |

Override on the command line:
```bash
# Force SDL3 build on Windows
cmake .. -DALTIRRA_SDL3=ON

# Build only libraries on Linux (no frontend)
cmake .. -DALTIRRA_SDL3=OFF
```

### CMake Presets

`CMakePresets.json` provides named configurations:

| Preset | Platform | Description |
|--------|----------|-------------|
| `linux-debug` | Linux | Debug build with SDL3 |
| `linux-release` | Linux | Release build with SDL3 |
| `macos-debug` | macOS | Debug build with SDL3 |
| `macos-release` | macOS | Release build with SDL3 |
| `windows-sdl-debug` | Windows | Debug SDL3 build |
| `windows-sdl-release` | Windows | Release SDL3 build |
| `windows-libs-only` | Windows | Libraries only (use .sln for native frontend) |

### Project Structure

```
CMakeLists.txt                  (root — platform detection, ALTIRRA_SDL3 option)
CMakePresets.json               (named build configurations)
src/
    CMakeLists.txt              (adds subdirectories)
    system/CMakeLists.txt       (system library — platform sources selected by OS)
    ATCore/CMakeLists.txt
    ATCPU/CMakeLists.txt
    ATEmulation/CMakeLists.txt
    ATDevices/CMakeLists.txt
    ATIO/CMakeLists.txt
    ATAudio/CMakeLists.txt
    ATNetwork/CMakeLists.txt
    ATNetworkSockets/CMakeLists.txt
    ATDebugger/CMakeLists.txt
    ATVM/CMakeLists.txt
    Kasumi/CMakeLists.txt
    vdjson/CMakeLists.txt
    AltirraSDL/CMakeLists.txt   (SDL3 frontend — only when ALTIRRA_SDL3=ON)
    compat/                     (shim headers for non-MSVC: intrin.h, tchar.h)
```

### Compatibility Shims

Non-MSVC compilers (GCC, Clang, Android NDK) need shim headers for
MSVC-specific includes used throughout the codebase:

- `src/compat/intrin.h` — Maps MSVC `__cpuid()` to GCC/Clang equivalents;
  includes `<x86intrin.h>` on x86 or `<arm_neon.h>` on ARM64.
- `src/compat/tchar.h` — Provides `TCHAR`, `_T()`, and string functions
  as narrow-char aliases.

These shims are injected via `ALTIRRA_COMPAT_DIR` (set only when `NOT MSVC`).
On Windows with MSVC, the native headers are used regardless of whether the
SDL3 frontend is being built.

## Test Mode (Automated UI Testing)

The SDL3 build includes a test automation framework that allows external
agents (LLM coding assistants, scripts) to interact with the ImGui UI
programmatically via a Unix domain socket.

### Activation

```bash
./AltirraSDL --test-mode
```

This creates a socket at `/tmp/altirra-test-<pid>.sock`. When `--test-mode`
is not passed, all test infrastructure is skipped (zero overhead).

### Protocol

Newline-delimited text commands in, single-line JSON responses out.
Connect with `socat` or Python `socket.AF_UNIX`.

### Commands

| Command | Description |
|---------|-------------|
| `ping` | Liveness check |
| `query_state` | Full UI state: dialog flags, simulator state, visible windows |
| `list_items [window]` | List all widgets with labels, types, positions |
| `list_dialogs` | List available dialog names |
| `open_dialog <name>` | Open a named dialog (e.g., `SystemConfig`) |
| `close_dialog <name>` | Close a named dialog |
| `click <window> <label>` | Click a widget by label (fire-and-forget, 3-frame sequence) |
| `wait_frames [n]` | Block until N frames have rendered |
| `screenshot <path>` | Save next frame as BMP |
| `cold_reset` / `warm_reset` | Reset emulator |
| `pause` / `resume` | Control emulation |
| `boot_image <path>` | Boot a disk/cart/tape image |
| `attach_disk <drive> <path>` | Mount disk image on drive N |
| `load_state` / `save_state <path>` | State management |

### Architecture

The framework uses ImGui's built-in `IMGUI_ENABLE_TEST_ENGINE` hooks.
When enabled, every widget (`Button`, `Checkbox`, `MenuItem`, etc.)
automatically calls `ImGuiTestEngineHook_ItemAdd` and
`ImGuiTestEngineHook_ItemInfo`. Our `ui_testmode.cpp` provides
implementations of these hooks that build a per-frame item registry.
No changes to UI code are required — widget tracking is automatic.

### Files

- `src/AltirraSDL/source/ui_testmode.h` — Public API
- `src/AltirraSDL/source/ui_testmode.cpp` — Socket IPC, hook implementations,
  item registry, command dispatcher

## Windows Native Build (Visual Studio)

### Requirements

- Windows 10 x64+
- Visual Studio 2022 v17.14+ (v143 toolset)
- Windows 11 SDK (10.0.26100.0+)
- MADS 2.1.0+ (6502 assembler, for kernel ROM)

### Solution Files

- `src/Altirra.sln` — Main emulator (32 projects)
- `src/AltirraRMT.sln` — Raster Music Tracker plugins
- `src/ATHelpFile.sln` — Help file

### Build Steps

1. Open `src/Altirra.sln`, set startup project to `Altirra`
2. First build must be **Release x64** (compiles build tools used by other configs)
3. Then build any configuration: Debug, Profile, Release (LTCG)

Output goes to `out/`. Local overrides in `localconfig/active/`.

## Libraries Excluded from SDL3 Build

These libraries are Windows-only and not compiled by CMake:

| Library | Reason | Replaced By |
|---------|--------|-------------|
| **VDDisplay** | GDI/D3D9 renderers | SDL3 display in AltirraSDL |
| **Dita** | COM/shell APIs | SDL3 file dialogs |
| **Riza** | Win32 audio backends | SDL3 audio in AltirraSDL |
| **Tessa** | Win32-specific | Not needed |
| **ATNativeUI** | Win32 HWND/dialogs/menus | Dear ImGui |
| **ATUI / ATUIControls** | Depends on VDDisplay | Dear ImGui |
| **AltirraShell** | Win32 shell integration | Not applicable |
| **Asuka** | Win32 build tool | Not needed (CMake handles build) |
| **ATAppBase** | Win32 application framework | SDL3 main loop |

## Conditional Compilation Strategy

### File Selection by Platform

The build system selects platform-specific source files at the CMake level.
Within source files, `#if VD_OS_WINDOWS` is used only when a single file
must handle both platforms (rare). Prefer separate `_sdl3.cpp` files.

| Component | Windows | Non-Windows |
|-----------|---------|-------------|
| system/file | `file.cpp` | `file_sdl3.cpp` |
| system/filesys | `filesys.cpp` | `filesys_sdl3.cpp` |
| system/fileasync | `fileasync.cpp` | `fileasync_sdl3.cpp` |
| system/registry | `registry.cpp` | `registry_sdl3.cpp` |
| system/text | `text.cpp` | `text_sdl3.cpp` |
| system/date | `date.cpp` | `date_sdl3.cpp` |
| system/time | `time.cpp` | `time_sdl3.cpp` |
| system/error | `error_win32.cpp` | `error_sdl3.cpp` |
| system/debug | `debug.cpp` | `debug_sdl3.cpp` |
| ATAudio output | `audiooutput.cpp` + WASAPI/WaveOut/XAudio2 | `audiooutput_sdl3.cpp` |
| ATNetworkSockets | `worker.cpp`, Winsock | `worker_sdl3.cpp`, POSIX sockets |
| Frontend | `Altirra` (.sln) | `AltirraSDL` (CMake) |

### Files Compiled on All Platforms (unchanged)

`ATCPU/*`, `ATEmulation/*`, `ATDevices/*`, `ATIO/*`, `ATNetwork/*`,
`ATVM/*`, `ATDebugger/*`, `vdjson/*`, `Kasumi/*`,
`ATAudio/source/pokey*.cpp`, `ATAudio/source/audio{filters,sample*,convolution*}.cpp`

### Architecture-Specific SIMD

| Module | x86_64 | ARM64 |
|--------|--------|-------|
| ATCore | `fft_sse2.cpp`, `fft_avx2.cpp` | `checksum_arm64.cpp`, `fft_neon.cpp` |
| Kasumi | `region_sse2.cpp`, `resample_stages_x64.cpp` | `blt_spanutils_arm64.cpp`, `region_neon.cpp`, `resample_stages_arm64.cpp` |
| ATIO | `audioreaderflac_x86.cpp` | `audioreaderflac_arm64.cpp` |

MSVC-only x86 files (reference MASM `.asm` externals) are guarded by `if(MSVC)`.

## Kernel ROM

The kernel ROM (6502 assembly) is built by `src/Kernel/Makefile` using MADS.
Pre-built kernel binaries are committed to `src/Altirra/autogen/`. The kernel
changes rarely.

## Platform Dependencies

```bash
# Debian/Ubuntu
apt install cmake build-essential libsdl3-dev

# Fedora
dnf install cmake gcc-c++ SDL3-devel

# macOS
brew install cmake sdl3

# Windows (vcpkg)
vcpkg install sdl3:x64-windows
```

Dear ImGui is fetched automatically via CMake FetchContent (no manual
install needed). SDL3_net is **not** required — `ATNetworkSockets` uses
POSIX sockets (or Winsock on Windows) directly.

## Future: Android

The build system is prepared for Android via CMake + NDK toolchain:

- `ANDROID` CMake variable triggers `VD_OS_ANDROID` definition
- System libraries use POSIX/SDL3 sources (same as Linux)
- SDL3 has first-class Android support
- NDK provides ARM NEON headers natively
- Compat shims (`intrin.h`, `tchar.h`) are applied for non-MSVC (NDK uses Clang)
- Frontend: AltirraSDL with SDL3+ImGui (SDL3 handles Android lifecycle)

To build, use the NDK toolchain with CMake:
```bash
cmake .. -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
         -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-26
```
