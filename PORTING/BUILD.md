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
| Android        | POSIX + SDL3  | AltirraSDL (SDL3+ImGui+Touch) | Gradle + CMake + NDK |

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
- git (for FetchContent of SDL3, Dear ImGui, librashader headers)
- SDL3 — **auto-fetched and built from source** if no system SDL3 is
  found.  A system install (distro package, vcpkg, conan, Homebrew) is
  optional and only used to speed up the first build.  If `find_package`
  picks up a broken/incompatible system SDL3, pass
  `-DALTIRRA_FETCH_SDL3=ON` (or `./build.sh --fetch-sdl3`) to force the
  source build.
- Dear ImGui (auto-fetched via FetchContent)
- librashader C headers (auto-fetched via FetchContent; runtime `.so`
  optional, see `--librashader`)

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
| `ALTIRRA_FETCH_SDL3` | OFF | Skip `find_package(SDL3)` and always fetch SDL3 from source |
| `ENABLE_LIBRASHADER` | ON | Fetch librashader C headers and enable shader preset support |
| `ALTIRRA_BRIDGE` | ON | Compile the bridge module into the `AltirraSDL` target. When ON, the frontend supports `--bridge` at runtime. When OFF, the bridge sources and the 5 surgical insertions in `main_sdl3.cpp` are omitted — the binary contains zero bridge symbols. |
| `ALTIRRA_BRIDGE_SERVER` | OFF | Also build the standalone headless `AltirraBridgeServer` target alongside `AltirraSDL`. The server reuses every bridge source file from `src/AltirraSDL/source/bridge/` plus a per-target glue layer in `src/AltirraBridgeServer/`. See the [AltirraBridge Build Path](#altirrabridge-build-path) section. |

Override on the command line:
```bash
# Force SDL3 build on Windows
cmake .. -DALTIRRA_SDL3=ON

# Build only libraries on Linux (no frontend)
cmake .. -DALTIRRA_SDL3=OFF

# Disable librashader support
cmake .. -DENABLE_LIBRASHADER=OFF
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

## AltirraBridge Build Path

AltirraBridge is a JSON-over-socket scripting interface. It has
two build surfaces: a module compiled into the `AltirraSDL`
frontend (exposed via `AltirraSDL --bridge`) and a standalone
headless `AltirraBridgeServer` target (same protocol, no UI).
Both paths share the exact same C++ source files under
`src/AltirraSDL/source/bridge/`; they differ only in `main_*.cpp`
and which platform layer they link.

### Source tree layout

```
src/AltirraSDL/source/bridge/                  (shared bridge module)
    bridge_server.{h,cpp}                       ← listen loop + frame gate
    bridge_protocol.{h,cpp}                     ← JSON framing + helpers
    bridge_transport.{h,cpp}                    ← TCP / UDS / abstract UDS
    bridge_commands_state.{h,cpp}               ← Phase 2 read commands
    bridge_commands_write.{h,cpp}               ← Phase 3 write / input
    bridge_commands_render.{h,cpp}              ← Phase 4 screenshot / rawscreen
    bridge_commands_debug.{h,cpp}               ← Phase 5a introspection
    bridge_commands_debug2.{h,cpp}              ← Phase 5b breakpoints / profiler / verifier
    bridge_main_glue.h                          ← per-target dispatch interface
    bridge_main_glue_sdl3.cpp                   ← SDL3-frontend BOOT/MOUNT/STATE_* glue

src/AltirraBridgeServer/                        (headless target)
    CMakeLists.txt
    main_bridge.cpp                             ← headless main loop
    ui_stubs.cpp                                ← no-op IATUIRenderer, null video display
    stubs/imgui.h                               ← include-chain shim (no ImGui link)

src/AltirraSDL/cmake/altirra_core_sources.cmake ← shared source-list filter
```

`src/AltirraSDL/AltirraBridge/` (outside `source/`) hosts the
user-facing tree: SDK, docs, examples, case studies, skill. The
CI workflow bundles it verbatim into release packages.

### Frontend integration (`main_sdl3.cpp`)

The bridge is wired into the SDL3 frontend via **five surgical
insertions**, each ~3 lines, all guarded by
`#if ALTIRRA_BRIDGE_ENABLED`:

1. Argv parse: recognise `--bridge` / `--bridge=<addr>` next to
   the existing `--test-mode` and `--headless` flags.
2. Init: `ATBridge::Init(addrSpec)` after `ATInitDebugger()`.
3. Per-frame poll: `ATBridge::Poll(g_sim, g_uiState)` adjacent
   to `ATTestModePollCommands`.
4. Frame-completed hook: `ATBridge::OnFrameCompleted(g_sim)` in
   the frame-completed branch so the gate counter advances.
5. Shutdown: `ATBridge::Shutdown(g_sim)` before `g_sim.Shutdown()`.

Setting `-DALTIRRA_BRIDGE=OFF` removes the bridge sources from
the `AltirraSDL` target list and the five insertions compile
out via the preprocessor guard — the resulting binary contains
zero bridge symbols. Verify with `nm AltirraSDL | grep -i bridge`
on Linux or the equivalent on other platforms.

### Headless target (`AltirraBridgeServer`)

`src/AltirraBridgeServer/CMakeLists.txt` links:

- Every file in `ALTIRRA_ALL_SOURCES` (the filtered core
  emulation source list produced by `altirra_core_sources.cmake`,
  shared with the `AltirraSDL` build).
- The same 8 bridge module files listed above.
- `bridge_main_glue_sdl3.cpp` is **replaced** by the headless
  target's own dispatch implementations inside `main_bridge.cpp`
  (boot, mount, state save/load called directly against the
  simulator instead of going through the SDL3 deferred-action
  queue).
- `main_bridge.cpp` — minimal main loop (init SDL3 audio in
  dummy mode, init simulator, init debugger, loop on
  `ATBridge::Poll` + `ATSimulator::Advance`, clean shutdown).
- `ui_stubs.cpp` — no-op `IATUIRenderer` (satisfies
  `ATDiskInterface::Init`'s hard requirement for a non-null
  renderer), null `IVDVideoDisplay` (so GTIA actually generates
  frames for `SCREENSHOT`), plus misc stubs for SDL3-frontend
  symbols the AltirraSDL `stubs/` files reference.

The headless target deliberately does NOT link:

- Dear ImGui
- librashader
- The AltirraSDL display backends (`display_sdl3.cpp`,
  `display_librashader.cpp`)
- The AltirraSDL input layer
- The AltirraSDL UI tree (menus, dialogs, debug panes)
- Embedded fonts

SDL3 is still linked transitively because `audiooutput_sdl3.cpp`
opens an SDL audio stream (with the dummy driver at startup),
but the SDL3 video/event/gamepad subsystems are never
initialised. A future v2 may replace `audiooutput_sdl3.cpp` with
a true null audio implementation to drop SDL3 entirely — until
then "headless lean SDK build" means "no UI, no display layer,
no input, no shader, no ImGui", which is still ~30% smaller
than the full `AltirraSDL` binary.

### Cross-platform notes

The bridge core compiles identically on all three desktop
platforms plus Android:

- **Transport**: Winsock on Windows (linked via `ws2_32` in the
  target's `target_link_libraries`), POSIX sockets everywhere
  else. Address forms `tcp:HOST:PORT` (all platforms),
  `unix:/path` (POSIX filesystem UDS), `unix-abstract:NAME`
  (Linux + Android only).
- **Android**: bridge runs inside the APK; reach it from a host
  via `adb forward tcp:PORT tcp:PORT`. All binary payload
  commands support inline base64 so a shared filesystem is not
  required.
- **Winsock init**: self-contained in `bridge_transport.cpp`.
  No other file in the bridge needs to know about `WSAStartup`.

### C example binaries

The SDK ships four C example programs under
`src/AltirraSDL/AltirraBridge/sdk/c/examples/` with their own
standalone CMake project (not part of the main build):

- `01_ping.c`, `02_peek_regs.c`, `03_input.c` — libc-only.
- `04_paint.c` — interactive SDL3 paint demo. Guarded by
  `find_package(SDL3 CONFIG QUIET)`; if SDL3 is missing, the
  CMake configure prints a status and skips only `04_paint`.

The CI workflow (`.github/workflows/bridge-package.yml`) builds
all four on each platform and bundles the binaries (plus the
SDL3 runtime next to `04_paint` on Linux and Windows, brew SDL3
dylib with `install_name_tool` fixup on macOS) into the release
archive.

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

Dear ImGui and librashader C headers are fetched automatically via CMake
FetchContent (no manual install needed). SDL3_net is **not** required —
`ATNetworkSockets` uses POSIX sockets (or Winsock on Windows) directly.

### librashader (Optional Runtime Dependency)

librashader enables RetroArch shader preset support (.slangp/.glslp).
The **C headers** are fetched automatically by CMake — no Rust toolchain
needed. At runtime, the shared library is loaded via `dlopen`/`LoadLibrary`.

To install the runtime library:

```bash
# Build from source (requires Rust toolchain)
git clone https://github.com/SnowflakePowered/librashader.git
cd librashader
cargo build --release -p librashader-capi
# Copy target/release/librashader.so to /usr/local/lib/

# Or install from distro packages if available
# Fedora: dnf install librashader
# Arch:   pacman -S librashader
```

If the shared library is not installed, AltirraSDL runs normally — shader
preset features are simply unavailable. The UI shows installation
instructions when librashader is not found.

When packaging (`cmake --build . --target package_altirra`), the shared
library is bundled automatically if found on the build system.

## Android Build

The Android build produces an APK containing the AltirraSDL native
library, SDL3, Dear ImGui, and the mobile touch UI. The build uses
Gradle for APK packaging and CMake (via the NDK) for native compilation.

### Quick Start

```bash
# If everything is already set up:
./build.sh --android                # debug APK
./build.sh --android --release      # release APK
./build.sh --android --clean        # clean + rebuild

# First time — auto-install SDK components (if sdkmanager is on PATH):
./build.sh --setup-android
```

The build script validates every dependency and prints platform-specific
install instructions if anything is missing. Read on for manual setup.

### Prerequisites

Three things are needed, in this order:

1. **Java JDK 17+** (must be installed first — sdkmanager needs it)
2. **Android SDK** with command-line tools
3. **SDK components:** platform, NDK, build-tools (installed via sdkmanager)

The Gradle wrapper is bundled automatically — no system Gradle needed.

### Step 1: Install Java JDK

sdkmanager and Gradle both require a JDK. Install one for your platform:

```bash
# Fedora / RHEL (latest available OpenJDK)
sudo dnf install java-latest-openjdk-devel

# Debian / Ubuntu
sudo apt install openjdk-21-jdk

# Arch
sudo pacman -S jdk-openjdk

# macOS
brew install openjdk

# Windows (winget)
winget install EclipseAdoptium.Temurin.21.JDK

# Windows (manual)
# Download from https://adoptium.net/
```

Verify: `javac -version` should print 17 or higher.

If your distro doesn't have `java-latest-openjdk-devel`, search for
what's available: `dnf search openjdk-devel` or `apt search openjdk`.
Any version >= 17 works.

### Step 2: Install Android SDK

**Option A: Android Studio** (easiest — handles everything)

Download from https://developer.android.com/studio. The SDK is installed
during first-run setup. Typical location: `~/Android/Sdk`.

**Option B: Command-line only** (no IDE needed)

```bash
# 1. Create SDK directory
mkdir -p ~/Android/Sdk/cmdline-tools
cd ~/Android/Sdk/cmdline-tools

# 2. Download command-line tools
#    Get the URL for your platform from:
#    https://developer.android.com/studio#command-line-tools-only
#
#    Linux:
wget https://dl.google.com/android/repository/commandlinetools-linux-11076708_latest.zip
#    macOS:
# curl -O https://dl.google.com/android/repository/commandlinetools-mac-11076708_latest.zip
#    Windows:
# Download commandlinetools-win-11076708_latest.zip from the URL above

# 3. Extract and rename
unzip commandlinetools-*_latest.zip
mv cmdline-tools latest

# 4. Add to shell profile (~/.bashrc, ~/.zshrc, or ~/.bash_profile)
echo 'export ANDROID_HOME=$HOME/Android/Sdk' >> ~/.bashrc
echo 'export PATH=$PATH:$ANDROID_HOME/cmdline-tools/latest/bin' >> ~/.bashrc
echo 'export PATH=$PATH:$ANDROID_HOME/platform-tools' >> ~/.bashrc
source ~/.bashrc
```

### Step 3: Install SDK Components

```bash
# Install platform, NDK, and build tools
sdkmanager --install \
    'platforms;android-36' \
    'ndk;28.2.13676358' \
    'build-tools;36.0.0'

# Accept all licenses (required on first install)
sdkmanager --licenses
```

**Finding current version numbers:** Run `sdkmanager --list` to see all
available versions. The versions above are pinned in the build script
(`scripts/build/android.sh`) — check the `REQUIRED_*` variables for the
exact versions the build expects. Newer versions generally work too
(the NDK auto-detects the latest installed).

**Or auto-install:** If sdkmanager is on your PATH, the build script
can install everything for you:

```bash
./build.sh --setup-android
```

### Step 4: Build

```bash
./build.sh --android                # debug APK
./build.sh --android --release      # release APK
./build.sh --android --clean        # clean + rebuild

# Install on connected device:
adb install -r android/app/build/outputs/apk/debug/app-debug.apk
```

Or step by step:

```bash
cd android
./setup_sdl3.sh                     # fetch SDL3 source, set up Java
./gradlew assembleDebug             # build debug APK
adb install app/build/outputs/apk/debug/app-debug.apk
```

### How It Works

```
build.sh --android
    |
    v
scripts/build/android.sh
    |
    +-- Validates ANDROID_HOME and NDK
    +-- Runs android/setup_sdl3.sh (if needed)
    |       +-- Clones SDL3 source to android/SDL3/
    |       +-- Symlinks SDL3 Java sources into project
    +-- Writes android/local.properties
    +-- Runs: gradlew assembleDebug (or assembleRelease)
            |
            v
        Gradle (android/app/build.gradle)
            |
            +-- Compiles Java (AltirraActivity + SDLActivity)
            +-- Runs CMake via externalNativeBuild
            |       +-- Builds SDL3 from android/SDL3/ as shared lib
            |       +-- Builds AltirraSDL as libmain.so
            |       +-- Links: libmain.so -> libSDL3.so + emulation libs
            +-- Packages APK (native libs + manifest + resources)
```

### Project Structure

```
android/
    build.gradle                 Top-level Gradle build
    settings.gradle              Project settings
    gradle.properties            Gradle JVM settings
    setup_sdl3.sh                SDL3 source fetcher + Java symlinker
    SDL3/                        (git-ignored) SDL3 source clone
    app/
        build.gradle             App build config (CMake path, SDK versions, ABIs)
        src/main/
            AndroidManifest.xml  Permissions, activity, file associations
            java/
                org/altirra/app/
                    AltirraActivity.java    Extends SDLActivity
                org/libsdl/               (symlink to SDL3 Java sources)
            res/
                values/strings.xml
                mipmap-hdpi/              (app icon — TODO)
```

### Build Configuration

The `app/build.gradle` passes these to CMake:

| Setting | Value | Purpose |
|---------|-------|---------|
| `ANDROID_STL` | `c++_shared` | C++20 standard library |
| `ALTIRRA_SDL3` | `ON` | Build SDL3 frontend |
| `ENABLE_LIBRASHADER` | `OFF` | No shader presets on Android (for now) |

ABI targets: `arm64-v8a` (all modern phones) and `armeabi-v7a` (older
32-bit devices). Remove `armeabi-v7a` from `abiFilters` in `build.gradle`
to reduce APK size if 32-bit support is not needed.

### Android-Specific CMake Changes

The root `CMakeLists.txt` detects `ANDROID` and:

1. Builds SDL3 from source via `add_subdirectory()` instead of
   `find_package()` — system SDL3 packages don't exist on Android
2. Defines `VD_OS_ANDROID=1` globally
3. The `AltirraSDL/CMakeLists.txt` builds a shared library (`libmain.so`)
   instead of an executable
4. Links `GLESv3` + `EGL` instead of desktop `OpenGL::GL`
5. Defines `ALTIRRA_MOBILE=1` to activate the mobile touch UI

### Signing

Debug builds are signed with the Android debug keystore automatically.
For release builds:

```bash
# Generate a keystore (once):
keytool -genkey -v -keystore altirra-release.keystore \
    -alias altirra -keyalg RSA -keysize 2048 -validity 10000

# Add to android/app/build.gradle under android.signingConfigs, or:
jarsigner -verbose -keystore altirra-release.keystore \
    app/build/outputs/apk/release/app-release-unsigned.apk altirra
zipalign -v 4 app-release-unsigned.apk altirra-release.apk
```

### Debugging

```bash
# Install and launch with logcat:
adb install -r app/build/outputs/apk/debug/app-debug.apk
adb shell am start -n org.altirra.app/.AltirraActivity
adb logcat -s SDL AltirraSDL

# Native debugging (requires Android Studio or ndk-gdb):
# Open the android/ directory in Android Studio, set breakpoints in C++
```

### Troubleshooting

| Problem | Solution |
|---------|----------|
| `JAVA_HOME is not set` | Install JDK 17+: see Step 1 above |
| `java-17-openjdk-devel` not found | Your distro uses a different name. Try `java-latest-openjdk-devel` (Fedora) or `openjdk-21-jdk` (Ubuntu) |
| `ANDROID_HOME is not set` | See Step 2 above. The build script also searches `~/Android/Sdk` automatically |
| `Missing SDK components` | Run `./build.sh --setup-android` or `sdkmanager --install 'platforms;android-36' ...` |
| `License not accepted` | Run `sdkmanager --licenses` and accept all |
| `SDL3 source not found` | Run `cd android && ./setup_sdl3.sh` |
| `NDK not found` | Install via `sdkmanager --install 'ndk;28.2.13676358'` or set `ANDROID_NDK_HOME` |
| `No matching ABIs` | Check `abiFilters` in `app/build.gradle` |
| `Java compilation errors` | Verify SDL3 Java symlink: `ls -la android/app/src/main/java/org/libsdl` |
| `OpenGL ES errors` | Device must support GLES 3.0+ (virtually all post-2014 devices) |
| `cmake version too old` | Install cmake 3.24+ via `sdkmanager --install 'cmake;3.31.6'` |
| `Gradle wrapper not found` | The build script downloads it automatically. If it fails, install Gradle: `brew install gradle` / `sdk install gradle` |
| `Could not find com.android.tools.build:gradle` | Check internet connectivity. Gradle downloads Android plugin on first build |
| `Unsupported class file major version` | Your Java is too new for the Gradle version. Update `gradle-wrapper.properties` to a Gradle that supports your JDK (e.g. Gradle 9.3+ for Java 25) |
| `Minimum supported Gradle version is X` | AGP requires a specific Gradle minimum. Update `distributionUrl` in `gradle-wrapper.properties` to the version shown in the error |
| NDK version disagrees with `android.ndkVersion` | Install the NDK version AGP expects (shown in error), or pin `ndkVersion` in `app/build.gradle` |
