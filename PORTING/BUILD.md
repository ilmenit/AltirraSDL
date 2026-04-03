# Build System

## Current State

The project uses Visual Studio solution files (`.sln` / `.vcxproj`) with
MSBuild property sheets in `src/Build/`. There is no cross-platform build
system. The build requires Windows, Visual Studio 2022, and the Windows SDK.

## Strategy

Add a CMake build system alongside the existing Visual Studio solution. The
`.sln` files remain the primary build system for Windows. CMake is used for
Linux and macOS, and optionally for Windows if developers prefer it.

**Phase 1 starts on Windows:** The CMake build is first validated on Windows,
compiling the exact same source files as the `.sln`. This proves the CMake
dependency graph is correct before any cross-platform code is added. Only
after the CMake Windows build matches the `.sln` output do we proceed to
header cleanup and Linux compilation.

CMake was chosen over Meson, Premake, or other alternatives because:

- SDL3 itself uses CMake and provides first-class `find_package` support
- Dear ImGui integrates trivially with CMake (add sources to target)
- CMake is the most widely supported cross-platform build system
- IDE integration: CLion, VS Code, Visual Studio all support CMake natively

## Project Structure

```
CMakeLists.txt                  (root)
src/
    CMakeLists.txt              (adds subdirectories)
    system/CMakeLists.txt       (system library)
    ATCore/CMakeLists.txt
    ATCPU/CMakeLists.txt
    ATEmulation/CMakeLists.txt
    ATDevices/CMakeLists.txt
    ATIO/CMakeLists.txt
    ATAudio/CMakeLists.txt
    ATNetwork/CMakeLists.txt
    ATNetworkSockets/CMakeLists.txt
    ATDebugger/CMakeLists.txt
    ATBasic/CMakeLists.txt
    ATCompiler/CMakeLists.txt
    ATVM/CMakeLists.txt
    Kasumi/CMakeLists.txt
    vdjson/CMakeLists.txt
    AltirraSDL/CMakeLists.txt   (SDL3 frontend)
    thirdparty/
        imgui/                  (Dear ImGui sources)
        CMakeLists.txt
```

### Root CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.24)
project(Altirra VERSION 4.40 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Platform detection
if(WIN32)
    add_compile_definitions(VD_OS_WINDOWS=1)
elseif(APPLE)
    add_compile_definitions(VD_OS_MACOS=1)
elseif(UNIX)
    add_compile_definitions(VD_OS_LINUX=1)
endif()

# Find SDL3 (SDL3_net is NOT used; ATNetworkSockets uses POSIX sockets)
find_package(SDL3 REQUIRED)

# Shared include paths
set(ALTIRRA_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/src/h)

add_subdirectory(src)
```

### Library CMakeLists.txt Pattern

Each library follows the same pattern. Example for `system`:

```cmake
# src/system/CMakeLists.txt

set(SYSTEM_COMMON_SOURCES
    source/atomic.cpp
    source/cache.cpp
    source/cmdline.cpp
    source/cpuaccel.cpp
    source/date.cpp
    source/error.cpp
    source/event.cpp
    source/hash.cpp
    source/math.cpp
    source/memory.cpp
    source/text.cpp
    source/vectors.cpp
    source/vdstl.cpp
    source/zip.cpp
    # ... other platform-agnostic files
)

if(WIN32)
    set(SYSTEM_PLATFORM_SOURCES
        source/file.cpp
        source/filesys.cpp
        source/fileasync.cpp
        source/thread.cpp
        source/registry.cpp
        source/w32assist.cpp
        source/error_win32.cpp
        source/process.cpp
    )
else()
    set(SYSTEM_PLATFORM_SOURCES
        source/date_sdl3.cpp
        source/debug_sdl3.cpp
        source/error_sdl3.cpp
        source/file_sdl3.cpp
        source/filesys_sdl3.cpp
        source/registry_sdl3.cpp
        source/text_sdl3.cpp
        source/time_sdl3.cpp
    )
endif()

add_library(system STATIC ${SYSTEM_COMMON_SOURCES} ${SYSTEM_PLATFORM_SOURCES})
target_include_directories(system PUBLIC ${ALTIRRA_INCLUDE_DIR})

if(NOT WIN32)
    target_link_libraries(system PRIVATE SDL3::SDL3)
endif()
```

### Frontend CMakeLists.txt

```cmake
# src/AltirraSDL/CMakeLists.txt

add_executable(AltirraSDL
    source/main_sdl3.cpp
    source/display_sdl3.cpp
    source/input_sdl3.cpp
    source/joystick_sdl3.cpp
    source/ui_main.cpp
    source/ui_system.cpp
    source/ui_disk.cpp
    source/ui_cassette.cpp
    # Stubs for Win32-only symbols
    stubs/uiaccessors_stubs.cpp
    stubs/oshelper_stubs.cpp
    stubs/console_stubs.cpp
    stubs/uirender_stubs.cpp
    stubs/win32_stubs.cpp
    stubs/device_stubs.cpp
    # Audio output (SDL3 implementation)
    ${CMAKE_SOURCE_DIR}/src/ATAudio/source/audiooutput_sdl3.cpp
    # Settings persistence (shared with Windows portable mode)
    ${CMAKE_SOURCE_DIR}/src/Altirra/source/uiregistry.cpp
    # All emulation core files (filtered from Altirra/source)
    ${ALTIRRA_ALL_SOURCES}
)

target_link_libraries(AltirraSDL PRIVATE
    # Core emulation
    ATCPU
    ATEmulation
    ATDevices
    ATIO
    ATAudio
    ATNetwork
    ATNetworkSockets
    ATDebugger
    ATBasic
    ATCompiler
    ATVM
    ATCore

    # Support libraries
    system
    Kasumi
    vdjson

    # External
    SDL3::SDL3
    imgui
)

target_include_directories(AltirraSDL PRIVATE
    ${ALTIRRA_INCLUDE_DIR}
    ${CMAKE_SOURCE_DIR}/src/Altirra/h  # for simulator.h, inputmanager.h, etc.
    ${CMAKE_SOURCE_DIR}/src/thirdparty/imgui
)
```

### Dear ImGui Integration

Dear ImGui is header/source-only (no library to link). Include it as a
static library built from source:

```cmake
# src/thirdparty/CMakeLists.txt

add_library(imgui STATIC
    imgui/imgui.cpp
    imgui/imgui_demo.cpp
    imgui/imgui_draw.cpp
    imgui/imgui_tables.cpp
    imgui/imgui_widgets.cpp
    imgui/backends/imgui_impl_sdl3.cpp
    imgui/backends/imgui_impl_sdlrenderer3.cpp
)

target_include_directories(imgui PUBLIC imgui imgui/backends)
target_link_libraries(imgui PRIVATE SDL3::SDL3)
```

## Libraries Excluded from SDL3 Build

The following libraries are **not compiled** for the SDL3 build because they
depend heavily on Win32 / Direct3D / COM APIs:

| Library | Reason | Replaced By |
|---------|--------|-------------|
| **VDDisplay** | 15+ files use GDI/D3D9 (renderers, display drivers, font rendering) | SDL3 display in AltirraSDL |
| **Dita** | COM/shell APIs (`services.cpp`) | Not needed; SDL3 file dialogs |
| **Riza** | Win32 audio backends (`IVDAudioOutput`) | SDL3 audio in AltirraSDL |
| **Tessa** | Win32-specific | Not needed |
| **ATNativeUI** | Pure Win32 (HWND, dialogs, menus) | Dear ImGui |
| **ATUI / ATUIControls** | Depends on VDDisplay renderer | Dear ImGui overlays |
| **AltirraShell** | Win32 shell integration | Not applicable on Linux/macOS |
| **Asuka** | Win32 build tool | Not needed (CMake handles build) |

**Header-only dependencies:** The SDL3 build uses interface headers from
`VDDisplay` (`display.h`, `compositor.h`, `renderer.h`) and `Kasumi`
(`pixmap.h`) for type definitions. These headers are clean (no Win32 types
in the interfaces themselves). The headers are included via
`${ALTIRRA_INCLUDE_DIR}` but no VDDisplay source files are compiled.

## Conditional Compilation Strategy

The build system selects platform-specific source files. Within source files,
use `#if VD_OS_WINDOWS` only when a single file must handle both platforms
(rare). Prefer separate files.

### Files Selected by Platform

| Component | Windows | Linux/macOS |
|-----------|---------|-------------|
| system/thread | `thread.cpp` | `thread.cpp` (cross-platform via `#ifdef`) |
| system/file | `file.cpp` | `file_sdl3.cpp` |
| system/filesys | `filesys.cpp` | `filesys_sdl3.cpp` |
| system/fileasync | `fileasync.cpp` | `fileasync_sdl3.cpp` (sync buffered I/O for AVI writing) |
| system/registry | `registry.cpp` | `registry_sdl3.cpp` |
| system/text | `text.cpp` | `text_sdl3.cpp` (wchar_t encoding) |
| system/date | `date.cpp` | `date_sdl3.cpp` |
| system/time | `time.cpp` | `time_sdl3.cpp` |
| system/error | `error_win32.cpp` | `error_sdl3.cpp` |
| system/debug | `debug.cpp` | `debug_sdl3.cpp` |
| ATAudio output | `audiooutput.cpp`, `audiooutwaveout.cpp`, etc. | `audiooutput_sdl3.cpp` |
| ATNetworkSockets | `worker.cpp`, `socketworker.cpp`, etc. | `worker_sdl3.cpp`, `socketworker_sdl3.cpp`, etc. (POSIX sockets) |
| ATCore timer | `timerserviceimpl_win32.h` | `timerserviceimpl_sdl3.h` |
| Frontend | `Altirra` project (Win32) | `AltirraSDL` project |

### Files Compiled on All Platforms (unchanged)

All of: `ATCPU/*`, `ATEmulation/*`, `ATDevices/*`, `ATIO/*`, `ATNetwork/*`,
`ATCompiler/*`, `ATVM/*`, `ATDebugger/*`, `vdjson/*`, `Kasumi/*`,
`ATAudio/source/pokey*.cpp`,
`ATAudio/source/audio{filters,sampleplayer,samplepool,samplebuffer,convolutionplayer}.cpp`

**Note:** `ATBasic` contains only 6502 assembly (`.s` files) built by the
MADS assembler, not C++ source. It is not compiled by CMake. The pre-built
kernel binary includes the BASIC ROM (see Kernel ROM section below).

## Handling the Simulator

The `ATSimulator` class lives in `src/Altirra/` which also contains all the
Win32 UI code. For the SDL3 build, the simulator and its dependencies must be
compilable without the Win32 UI files.

Options:

**Option A: Extract simulator into a library.** Move `simulator.cpp`,
`simulator.h`, and its non-UI dependencies into a new `ATSimulator` library
project. Both `Altirra` (Win32) and `AltirraSDL` link against it.

**Option B: Include simulator sources directly in AltirraSDL.** Add
`simulator.cpp` and selected non-UI files from `src/Altirra/source/` to the
`AltirraSDL` build target. More pragmatic, less clean.

**Option C: Compile the Altirra project as a library with UI files excluded.**
Use CMake to build a static library from the Altirra source directory,
excluding `ui*.cpp`, `main.cpp`, and other Win32-only files.

**Recommended: Option A.** It is the cleanest separation and makes the
architecture explicit. The original `Altirra.vcxproj` continues to build
everything together as before.

**Complexity note:** `simulator.cpp` currently includes ~80 headers,
some of which are UI-related (`debugger.h`, `uirender.h`, `profiler.h`).
Extracting the simulator library requires resolving these dependencies --
typically by replacing UI-layer includes with forward declarations or
moving them behind interfaces. This is not purely mechanical; it
requires case-by-case analysis of each dependency. Budget accordingly.

### What Goes in ATSimulator Library

From `src/Altirra/source/`, the non-UI files needed by any frontend:

- `simulator.cpp` -- core simulator
- `inputmanager.cpp` -- input routing
- `inputcontroller.cpp` -- port controllers
- `cassette*.cpp` -- cassette emulation
- `disk*.cpp` -- disk drive emulation
- `cartridge*.cpp` -- cartridge handling
- `kerneldb.cpp` -- kernel ROM database
- `firmware*.cpp` -- firmware management
- `savestate*.cpp` -- save/load state
- `debugger*.cpp` (non-UI) -- debugger backend
- Other emulation-support files

Exclude: All `ui*.cpp`, `main.cpp`, `uidbg*.cpp`, `oshelper.cpp`,
`joystick.cpp` (Win32 joystick), `videowriter.cpp` (replaced by
`videowriter_sdl3.cpp`), `console.cpp`. Note: `aviwriter.cpp` is
included explicitly — it is platform-portable (uses `IVDFileAsync`).

## Kernel ROM

The kernel ROM (6502 assembly) is built by `src/Kernel/Makefile` using the
MADS assembler. MADS is a Windows tool. For cross-platform builds:

1. **Pre-built kernel**: Include the compiled kernel binary in the
   repository. The kernel changes rarely.
2. **MADS via Wine**: Run MADS under Wine on Linux. Add a CMake option to
   enable/disable kernel compilation.
3. **Cross-compile MADS**: If MADS source is available, compile it for the
   host platform.

Recommended: Option 1 for simplicity. Commit the kernel binary and only
rebuild it on Windows when kernel source changes.

## Build Commands

```bash
# Linux/macOS
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(nproc)

# Run
./src/AltirraSDL/AltirraSDL

# Windows (CMake, optional -- .sln is primary)
cmake .. -G "Visual Studio 17 2022" -A x64
cmake --build . --config Release
```

## Dependencies

Build dependencies on Linux:

```bash
# Debian/Ubuntu
apt install cmake build-essential libsdl3-dev

# Fedora
dnf install cmake gcc-c++ SDL3-devel
```

On macOS:

```bash
brew install cmake sdl3
```

Dear ImGui is vendored in the repository (source-only, no system package
needed). SDL3_net is **not** required — `ATNetworkSockets` uses POSIX
sockets directly.
