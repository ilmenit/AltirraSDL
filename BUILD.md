# Building AltirraSDL

Altirra supports three independent build paths and two independent
binary targets:

| Build Path | Platform | Frontend | Build System |
|------------|----------|----------|--------------|
| **Visual Studio** | Windows | Native Win32 UI (Altirra.exe) | `.sln` |
| **CMake** | Linux, macOS, Windows | SDL3 + Dear ImGui (AltirraSDL) | CMake + `build.sh` |
| **Android** | Android | SDL3 + Dear ImGui + Touch | Gradle + CMake + NDK via `build.sh --android` |

| Target | Binary | Dependencies | Use case |
|--------|--------|--------------|----------|
| `AltirraSDL` | GUI emulator | SDL3 + Dear ImGui + librashader (optional) | End-user desktop emulator. Optional scripting via `--bridge`. |
| `AltirraBridgeServer` | Headless scripting server | SDL3 (audio only, dummy driver) | Automation, CI, AI agents, RE tooling. Same bridge protocol as `AltirraSDL --bridge`, ~30% smaller binary, no UI. Opt-in via `-DALTIRRA_BRIDGE_SERVER=ON`. See [AltirraBridge/README.md](src/AltirraSDL/AltirraBridge/README.md). |

All three build paths coexist in the same repository and do not
conflict (different output directories: `.sln` uses `out/`, CMake
uses `build/`).

---

## Quick Start (build.sh)

The `build.sh` script automates the CMake workflow on all platforms.

```bash
# Build release for current platform
./build.sh

# Build debug
./build.sh --debug

# Build + create distributable archive
./build.sh --package

# Build + binary archive + source archive
./build.sh --package --source

# Clean rebuild
./build.sh --clean --package
```

**On Windows**, run from **Git Bash**, **MSYS2**, or **WSL**.

### Output

| File | Contents |
|------|----------|
| `build/<preset>/src/AltirraSDL/AltirraSDL` | Executable |
| `build/<preset>/AltirraSDL-<ver>-<platform>.zip` | Binary distribution (with `--package`) |
| `build/<preset>/AltirraSDL-<ver>-src.tar.gz` | Source archive (with `--source`) |

The binary archive follows Altirra's distribution convention:
```
AltirraSDL-4.40-linux.zip
    AltirraSDL          (executable)
    Copying             (GPL v2+ license)
    extras/
        customeffects/  (shader/effect presets)
        sampledevices/  (custom device examples)
        deviceserver/   (Python scripts)
        readme.txt
```

### All Options

| Option | Description |
|--------|-------------|
| `--release` | Release build (default) |
| `--debug` | Debug build |
| `--package` | Create distributable archive after build |
| `--source` | Also create source archive (requires `--package`) |
| `--clean` | Remove build directory before configuring |
| `--native` | Windows only: build core libraries for use with Visual Studio `.sln` |
| `--jobs N` or `-jN` | Override parallel job count (default: all cores) |
| `--cmake "ARGS"` | Pass extra arguments to CMake configure |
| `--librashader` | Build librashader from source (requires Rust, see below) |
| `--cmake "-DALTIRRA_BRIDGE_SERVER=ON"` | Also build the headless `AltirraBridgeServer` target. `build.sh` does not wire this up as a dedicated flag — pass it through via `--cmake`. See the [AltirraBridge section](#altirrabridge-optional--scripting--automation) below. |
| `--help` | Show help |

---

## AltirraBridge (optional — scripting / automation)

AltirraBridge is a JSON-over-socket scripting interface for
AltirraSDL plus a headless lean build (`AltirraBridgeServer`)
intended for automation, CI testing, and AI-driven workflows.
Two SDKs ship with it: **Python** (stdlib only — `altirra_bridge`
package) and **C** (single-file `altirra_bridge.h` / `.c`).

See [`src/AltirraSDL/AltirraBridge/README.md`](src/AltirraSDL/AltirraBridge/README.md)
for the full overview, [`docs/PROTOCOL.md`](src/AltirraSDL/AltirraBridge/docs/PROTOCOL.md)
for the wire contract, and [`docs/COMMANDS.md`](src/AltirraSDL/AltirraBridge/docs/COMMANDS.md)
for the per-command reference.

### Downloading a prebuilt package (recommended)

If you just want to use the bridge, **don't build it yourself**.
Every push to `main` produces cross-platform packages for Linux
x86_64, macOS arm64, and Windows x86_64 on the
[`nightly-bridge`](../../releases/tag/nightly-bridge) release.
Each archive is self-contained: headless server binary, both SDKs,
docs, prebuilt C example binaries, RE case studies, and the
Claude Code skill. No compilation, no install.

```sh
# Linux / macOS
tar xzf AltirraBridge-*-linux-x86_64.tar.gz
cd AltirraBridge-*/
./AltirraBridgeServer --bridge=tcp:127.0.0.1:0

# Windows
Expand-Archive AltirraBridge-*-windows-x86_64.zip
cd AltirraBridge-*/
.\AltirraBridgeServer.exe --bridge=tcp:127.0.0.1:0
```

### Building the bridge from source

The bridge is a plain CMake option. Configure with
`-DALTIRRA_BRIDGE_SERVER=ON` and build the `AltirraBridgeServer`
target:

```bash
# Linux / macOS
cmake -S . -B build -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DALTIRRA_BRIDGE_SERVER=ON
cmake --build build --target AltirraBridgeServer -j$(nproc)
./build/src/AltirraBridgeServer/AltirraBridgeServer --bridge=tcp:127.0.0.1:0
```

```pwsh
# Windows (from Developer PowerShell for VS)
cmake -S . -B build -G Ninja `
      -DCMAKE_BUILD_TYPE=Release `
      -DALTIRRA_BRIDGE_SERVER=ON
cmake --build build --target AltirraBridgeServer
.\build\src\AltirraBridgeServer\AltirraBridgeServer.exe --bridge=tcp:127.0.0.1:0
```

The `AltirraBridge` module inside `AltirraSDL` is always built —
it's how `AltirraSDL --bridge` exposes the same protocol. Opt out
with `-DALTIRRA_BRIDGE=OFF` if you want a bridge-free
`AltirraSDL` binary; the bridge code is then excluded from the
link entirely.

### Building the C example binaries

The C examples have their own standalone CMake project so users
who only download the SDK can build them in isolation. Three of
the four examples are libc-only; `04_paint` requires SDL3 for the
interactive window. If SDL3 is not found, the CMake configure
prints a status and skips `04_paint` — the others still build.

```bash
cd src/AltirraSDL/AltirraBridge/sdk/c/examples
mkdir build && cd build

# With system SDL3 (brew install sdl3, libsdl3-dev, etc.)
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build .

# Or point at the bridge build's FetchContent'd SDL3
cmake .. -DCMAKE_BUILD_TYPE=Release \
         -DCMAKE_PREFIX_PATH=../../../../../../build/_deps/sdl3-build
cmake --build .
```

Binaries land next to the build directory (`01_ping`,
`02_peek_regs`, `03_input`, `04_paint`). On Linux the 04_paint
binary has `RUNPATH=$ORIGIN`, so dropping `libSDL3.so.0` next to
it is enough to run without a system SDL3 install. On macOS the
prebuilt release bundles SDL3 with `install_name_tool`. On
Windows, `SDL3.dll` goes next to `04_paint.exe`.

### Using the Python SDK

The Python SDK is pure stdlib (`socket` + `json`). There is no
`pip install` step — just put `sdk/python/` on `PYTHONPATH`:

```sh
export PYTHONPATH=src/AltirraSDL/AltirraBridge/sdk/python
python3 -c "
from altirra_bridge import AltirraBridge
with AltirraBridge.from_token_file('/tmp/altirra-bridge-12345.token') as a:
    a.boot('/path/to/game.xex'); a.frame(120)
    open('frame.png','wb').write(a.screenshot())
"
```

The package also ships three higher-level modules for reverse
engineering: `altirra_bridge.loader` (XEX parser),
`altirra_bridge.project` (persistent RE project state), and
`altirra_bridge.asm_writer` (MADS source exporter). See
[`docs/api/python-client.md`](src/AltirraSDL/AltirraBridge/docs/api/python-client.md)
for the full reference.

### Installing the Claude Code skill

If you drive the bridge from Claude Code, the bundled
`altirra-bridge` skill teaches the agent how to use the SDK:

```sh
python -m altirra_bridge.install_skills           # ./.claude/skills/
python -m altirra_bridge.install_skills --user    # ~/.claude/skills/
```

---

## Librashader (optional — shader presets)

[librashader](https://github.com/SnowflakePowered/librashader) provides
RetroArch-compatible shader preset support (CRT effects, scanlines, etc.).
It is **optional** — the emulator works without it, but shader presets
in the Screen Effects menu will be unavailable.

### Building librashader

```bash
# Build AltirraSDL with librashader support
./build.sh --librashader
```

This clones librashader, builds it from source with Rust, and places the
shared library next to the executable. Only the **OpenGL and Vulkan**
backends are compiled — no extra system DLLs are required.

**Prerequisite:** A working [Rust toolchain](https://rustup.rs/) (`cargo`
must be on PATH). The first build takes a few minutes; subsequent builds
are cached.

### Using a pre-built librashader

If you prefer not to build from source, download a release from the
[librashader releases page](https://github.com/SnowflakePowered/librashader/releases)
and place the shared library next to the AltirraSDL executable:

| Platform | File to place next to executable |
|----------|----------------------------------|
| Linux | `librashader.so` |
| macOS | `librashader.dylib` |
| Windows | `librashader.dll` |

**Note:** Pre-built releases from upstream include all backends (D3D9,
D3D11, D3D12, GL, Vulkan) and may require additional system libraries
on Windows (`D3DX9_43.dll` from the legacy DirectX End-User Runtime,
`dxcompiler.dll` from the Windows SDK). Building from source with
`--librashader` avoids this by compiling only GL + Vulkan backends.

---

## Prerequisites

### Linux (Debian/Ubuntu)

```bash
sudo apt install cmake build-essential libsdl3-dev
```

### Linux (Fedora)

```bash
sudo dnf install cmake gcc-c++ SDL3-devel
```

### macOS

```bash
brew install cmake sdl3
```

### Windows (for CMake/SDL3 build)

```
vcpkg install sdl3:x64-windows
```

Or install SDL3 development libraries manually and ensure they are on
`CMAKE_PREFIX_PATH`.

### Windows (for Visual Studio native build)

- Visual Studio 2022 v17.14+ (v143 toolset)
- Windows 11 SDK (10.0.26100.0+)
- MADS 2.1.0+ (6502 assembler, for kernel ROM — optional)

Dear ImGui is fetched automatically via CMake FetchContent (no manual
install needed).

---

## CMake Build (manual)

If you prefer not to use `build.sh`, use CMake presets directly:

```bash
# Linux
cmake --preset linux-release
cmake --build build/linux-release -j$(nproc)
./build/linux-release/src/AltirraSDL/AltirraSDL

# macOS
cmake --preset macos-release
cmake --build build/macos-release -j$(sysctl -n hw.ncpu)

# Windows SDL (from Developer Command Prompt)
cmake --preset windows-sdl-release
cmake --build build/windows-sdl-release --config Release
```

### Available Presets

| Preset | Platform | Type |
|--------|----------|------|
| `linux-debug` | Linux | Debug |
| `linux-release` | Linux | Release |
| `macos-debug` | macOS | Debug |
| `macos-release` | macOS | Release |
| `windows-sdl-debug` | Windows | Debug (SDL3) |
| `windows-sdl-release` | Windows | Release (SDL3) |
| `windows-libs-only` | Windows | Core libraries only (no frontend) |

### Package Target

To create a distributable folder:

```bash
cmake --build build/linux-release --target package_altirra
# Creates: build/linux-release/AltirraSDL-4.40/
```

### Install Target

For system-wide installation (FHS layout):

```bash
cmake --install build/linux-release --prefix /usr/local
# Installs: /usr/local/bin/AltirraSDL
#           /usr/local/share/altirra/extras/
```

---

## Android Build

### Prerequisites

1. **Java JDK 17+** — required by sdkmanager and Gradle
2. **Android SDK** with command-line tools
3. **SDK components** installed via sdkmanager

```bash
# 1. Install Java
# Fedora:
sudo dnf install java-latest-openjdk-devel
# Debian/Ubuntu:
sudo apt install openjdk-21-jdk
# macOS:
brew install openjdk

# 2. Install Android SDK (if not using Android Studio)
mkdir -p ~/Android/Sdk/cmdline-tools
cd ~/Android/Sdk/cmdline-tools
# Download from: https://developer.android.com/studio#command-line-tools-only
unzip commandlinetools-*_latest.zip
mv cmdline-tools latest

# 3. Set environment (add to ~/.bashrc or ~/.zshrc)
export ANDROID_HOME=$HOME/Android/Sdk
export PATH=$PATH:$ANDROID_HOME/cmdline-tools/latest/bin:$ANDROID_HOME/platform-tools

# 4. Install SDK components
sdkmanager --install \
    'platforms;android-36' \
    'ndk;28.2.13676358' \
    'build-tools;36.0.0'
sdkmanager --licenses
```

### Build

```bash
./build.sh --android                # debug APK
./build.sh --android --release      # release APK
./build.sh --android --release --sign  # release APK, signed with debug keystore
./build.sh --android --clean        # clean + rebuild

# Or auto-install SDK components + build:
./build.sh --setup-android
```

### Output

```
android/app/build/outputs/apk/debug/app-debug.apk
android/app/build/outputs/apk/release/app-release-unsigned.apk
```

### Install

**Via USB (ADB):**

```bash
adb install -r <path-to-apk>
```

**Via file transfer (release APK):**

The release APK is unsigned and must be signed before Android will
install it. The easiest way is to use the `--sign` flag:

```bash
./build.sh --android --release --sign
```

This automatically creates a debug keystore (at `~/.altirra-debug.jks`)
on first use and signs the APK. Then copy the signed APK to your phone
and open it. You will need to
enable **Install from unknown sources** in Android settings (Settings →
Security, or Settings → Apps → Special access → Install unknown apps).

The debug APK (`app-debug.apk`) is already signed with the Android debug
key and can be installed directly.

### Troubleshooting

The build script validates all dependencies and prints install
instructions if anything is missing. Common issues:

- **Java package not found** — package names vary by distro. Search
  with `dnf search openjdk-devel` or `apt search openjdk`. Any JDK >= 17 works.
- **`Unsupported class file major version`** — Gradle is too old for
  your Java. Update `distributionUrl` in
  `android/gradle/wrapper/gradle-wrapper.properties`.
- **NDK version mismatch** — install the NDK version shown in the error
  via `sdkmanager --install 'ndk;<version>'`.

See [PORTING/BUILD.md](PORTING/BUILD.md) for detailed internals.

---

## Visual Studio Native Build (Windows)

The native Win32 build produces the traditional `Altirra.exe` with full
Win32 UI, Direct3D display, and WASAPI audio. This is the primary build
path on Windows and does not use CMake.

### Steps

1. Open `src/Altirra.sln` in Visual Studio 2022
2. Set startup project to **Altirra**
3. **First build must be Release x64** — this compiles the Asuka build tool
   used by other configurations
4. Then build any configuration:
   - **Debug** — unoptimized, full debug info
   - **Profile** — optimized with debug info
   - **Release** — fully optimized with LTCG

Output goes to `out/`. Intermediates to `obj/`.

### Solution Files

| Solution | Contents |
|----------|----------|
| `src/Altirra.sln` | Main emulator (32 projects) |
| `src/AltirraRMT.sln` | Raster Music Tracker plugins |
| `src/ATHelpFile.sln` | Help file (requires .NET 4.8, HTML Help 1.4) |

### Local Overrides

Place `.props` files in `localconfig/active/` to override build settings
without modifying tracked files. See `localconfig/example/` for templates.

MADS assembler path can be overridden via the `ATMadsPath` property in
`localconfig/active/Altirra.local.props`.

### Release Packaging (Windows native)

```bash
py release.py    # From VS Developer Command Prompt
```

Requires Python 3.10+, 7-zip, AdvanceCOMP. Produces:
- `Altirra-<ver>.zip` — Binary distribution
- `Altirra-<ver>-src.7z` — Source archive

---

## Using Both Build Paths

The Visual Studio `.sln` and CMake builds are fully independent:

| | Visual Studio | CMake |
|---|---|---|
| **Source directory** | `src/Altirra.sln` | `CMakeLists.txt` (root) |
| **Output** | `out/` | `build/<preset>/` |
| **Intermediates** | `obj/`, `lib/` | `build/<preset>/` |
| **Frontend** | Native Win32 | SDL3 + Dear ImGui |
| **Emulation core** | Same source files | Same source files |

On Windows, you can build both:
- Native `Altirra.exe` via Visual Studio
- SDL3 `AltirraSDL.exe` via `./build.sh` or CMake

The `windows-libs-only` preset builds just the core emulation libraries
via CMake, which can be useful for testing that the core compiles with
different compilers (GCC/Clang on Windows).

---

## Detailed Build Documentation

For internals (conditional compilation, SIMD selection, compatibility
shims, test mode), see [PORTING/BUILD.md](PORTING/BUILD.md).
