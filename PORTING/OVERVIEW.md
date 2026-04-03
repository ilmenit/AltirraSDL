# Cross-Platform Porting Overview

## Goal

Enable Altirra to compile and run on Linux and macOS while preserving the
original Windows build with its native UI. The cross-platform build uses SDL3
for platform abstraction and Dear ImGui for the user interface. No other
external dependencies are introduced.

## Corrections to Initial Analysis

The first round of analysis contained several errors that this document
corrects:

1. **POSIX sockets are required.** SDL3_net's higher-level API does not expose
   `setsockopt`, `shutdown()` half-close, `getsockname`/`getpeername`, or raw
   fd access needed by the socket worker's buffered I/O and event model. The
   SDL3 build uses POSIX sockets directly (the exact equivalent of Winsock)
   with `poll()` replacing `WSAEventSelect`/`WaitForMultipleObjects`.

2. **IVDVideoDisplayMinidriver cannot be implemented for SDL3.** The interface
   has `HWND`, `HMONITOR`, `HDC`, and `RECT` baked into method signatures
   (`PreInit`, `Init`, `Paint`). A cross-platform build must bypass this
   interface entirely and provide a new `IVDVideoDisplay` implementation that
   uses SDL3 internally.

3. **IVDAudioOutput is contaminated with Win32 types.** Its `Init()` takes
   `tWAVEFORMATEX`. However, the higher-level `IATAudioOutput` interface is
   completely clean and is the correct integration point.

4. **VDCriticalSection clones the CRITICAL_SECTION memory layout directly** in
   the header (a 6-field struct). This cannot be hidden behind a source-file
   swap; the header itself needs `#ifdef` blocks.

5. **SDL3 can replace most of the system library**, not just display/audio. It
   provides threading, mutexes, semaphores, condition variables, RW locks,
   atomics, file I/O, async I/O, filesystem operations, timers, and process
   management. This significantly reduces the surface area of platform-specific
   code.

6. **The main loop is deeply Win32.** It uses `MsgWaitForMultipleObjects` for
   frame timing and Windows message dispatch as the idle mechanism. The SDL3
   build needs its own main loop, not a wrapper around the existing one.

7. **VDDisplay library is heavily Windows-dependent.** 15+ of its 31 source
   files use GDI or Direct3D 9 APIs. It is not just the minidriver interface
   that is contaminated -- the entire library (renderers, display drivers,
   font rendering) is Win32/D3D-specific. The SDL3 build does not compile or
   link VDDisplay at all; it only uses the clean interface headers
   (`display.h`, `compositor.h`, `renderer.h`) for type definitions.

8. **Dita and Riza have Windows dependencies.** `Dita/source/services.cpp`
   includes COM and shell APIs. `Riza` provides the Win32 audio output
   backends (`IVDAudioOutput`). Neither is compiled for the SDL3 build.

9. **GTIA submits frames via `PostBuffer(VDVideoDisplayFrame*)`,** not via
   `SetSource()` with a raw `VDPixmap`. `VDVideoDisplayFrame` wraps a
   `VDPixmap` plus frame metadata (frame number, screen FX settings). The
   SDL3 display implementation must handle this frame queue protocol.

10. **`ATCreateAudioOutput()` is called inside `ATSimulator::Init()`**, not
    by the frontend. The SDL3 build needs the factory to return an SDL3-based
    implementation. This is handled via conditional compilation in the factory
    function or by providing a separate `audiooutput_sdl3.cpp` that the
    build system selects instead of the Win32 version.

## Design Principles

**Minimal disruption.** The original author should remain fully comfortable
with the codebase. Changes to existing files are limited to `#ifdef` blocks in
headers where platform-specific types appear as struct members or method
parameters. No renaming, no new abstractions layered on top of existing code,
no changes to the Windows build path.

**Conditional compilation at the source level.** Where a `.cpp` file is
entirely platform-specific (e.g., `file.cpp` using `CreateFileW`), we add a
parallel `file_sdl3.cpp` using SDL3 APIs. The build system selects which to
compile. Headers remain shared.

**Two frontend projects, one emulation core.** The existing `Altirra` project
(Win32 frontend) and a new `AltirraSDL` project (SDL3 + Dear ImGui frontend)
both link against the same core libraries. The core libraries gain
cross-platform support through the system library changes. The frontend
projects are entirely separate.

**SDL3 as the sole platform layer for non-Windows.** Rather than writing
separate POSIX, CoreAudio, ALSA, etc. backends, we use SDL3 for all OS
interaction: windowing, audio, input, threading, file I/O, networking, timers.
This gives us Linux, macOS, and potentially more platforms from a single
implementation.

**No new external dependencies beyond SDL3 and Dear ImGui.** Configuration
storage uses an INI-like text file in the same format as Windows Altirra's
portable mode, persisted to `~/.config/altirra/settings.ini`. The existing
`VDRegistryProviderMemory` class is the in-memory store; `ATUILoadRegistry`
/ `ATUISaveRegistry` (from `uiregistry.cpp`) handle serialization.

## Architecture Overview

```
+---------------------+     +---------------------+
|     Altirra         |     |    AltirraSDL       |
|  (Win32 Frontend)   |     | (SDL3+ImGui Frontend|
|  - Win32 UI/Menus   |     |  - Dear ImGui UI    |
|  - D3D9 Display     |     |  - SDL3 Display     |
|  - Win32 Audio      |     |  - SDL3 Audio       |
|  - Win32 Input      |     |  - SDL3 Input       |
|  - Win32 Main Loop  |     |  - SDL3 Main Loop   |
+--------+------------+     +----------+----------+
         |                              |
         +-------------+----------------+
                       |
         +-------------v--------------+
         |     Core Emulation         |
         |  ATCPU, ATEmulation,       |
         |  ATDevices, ATIO,          |
         |  ATAudio (synthesis),      |
         |  ATDebugger, ATBasic,      |
         |  ATNetwork, ATCore         |
         +-------------+--------------+
                       |
         +-------------v--------------+
         |     System Library         |
         |  Threading, File I/O,      |
         |  Atomics, Filesystem,      |
         |  Configuration             |
         |                            |
         |  win32 impl | sdl3 impl   |
         +----------------------------+
```

The core emulation layer is already platform-agnostic (zero Windows API calls
in ATCPU, ATEmulation, ATDevices, ATIO, ATNetwork, ATCompiler, ATVM,
ATDebugger). It compiles unchanged on any platform once the system library
beneath it provides the required primitives.

**Not compiled for SDL3 build:** VDDisplay (GDI/D3D renderers), Dita (COM/
shell services), Riza (Win32 audio backends), Tessa, ATNativeUI, ATUI,
ATUIControls, AltirraShell, Asuka (build tool). These are Windows-only.
The SDL3 frontend implements equivalent functionality directly.

## What Changes in Existing Code

**Important:** While core emulation source files have no direct Win32 API
calls, their `stdafx.h` precompiled headers and transitive includes pull in
Windows-specific content. This means the header cleanup is a prerequisite
before *any* core library compiles on Linux. The changes below are small
(`#ifdef` guards) but load-bearing.

| Area | Nature of Change |
|------|-----------------|
| `src/h/vd2/system/vdtypes.h` | Add `VD_COMPILER_GCC` detection and `VD_OS_WINDOWS` / `VD_OS_LINUX` / `VD_OS_MACOS` macros |
| `src/h/vd2/system/win32/intrin.h` | Guard `#pragma push_macro`/`pop_macro` (MSVC-only) and `<intrin.h>` include; on GCC/Clang non-Windows, include `<x86intrin.h>` or `<arm_neon.h>` by architecture |
| `src/h/vd2/system/thread.h` | Guard Win32 forward declarations (lines 41-54) with `#if VD_OS_WINDOWS`; `#ifdef` for `VDCriticalSection` member layout, `VDSignalBase` members, `__stdcall`, `VDThreadID` width |
| `src/h/vd2/system/atomic.h` | Extend `VD_COMPILER_CLANG` checks to include `VD_COMPILER_GCC` (~10 one-line edits); GCC hits `#error` without this |
| `src/h/vd2/system/Error.h` | Guard `using VDExceptionPostContext = struct HWND__ *` with `#ifdef`; on non-Windows use `using VDExceptionPostContext = void *` |
| `src/h/vd2/system/file.h` | Replace `#error` with `typedef int VDFileHandle` for non-Windows |
| `src/system/h/stdafx.h` | Guard `#include <windows.h>` and `#include <process.h>` with `#if VD_OS_WINDOWS` |
| `src/ATIO/h/stdafx.h` | Guard `WINVER` define with `#if VD_OS_WINDOWS` |

Everything else is additive: new `.cpp` files, a new frontend project, a new
build system file.

## What Is New

| Component | Description |
|-----------|-------------|
| `src/system/source/*_sdl3.cpp` | SDL3 implementations of system library (thread, file, filesys, fileasync, registry) |
| `src/ATAudio/source/audiooutput_sdl3.cpp` | SDL3 audio output implementing `IATAudioOutput` |
| `src/ATNetworkSockets/source/*_sdl3.cpp` | POSIX socket implementation (socket worker, bridge, DNS, VXLAN) |
| `src/AltirraSDL/source/videowriter_sdl3.cpp` | Portable video recording (Raw/RLE/ZMBV AVI via aviwriter.cpp) |
| `src/AltirraSDL/` | New frontend project with SDL3 main loop, Dear ImGui UI, SDL3 display, SDL3 input |
| `CMakeLists.txt` | CMake build system for cross-platform compilation |

## Phased Implementation Plan

Each phase produces a testable result. Every phase must pass the gate
**"Windows build still works identically"** before proceeding.

### Phase 1: CMake Build System (Windows-only, verifies structure)

Create the CMake build alongside `.sln`. Initially targets Windows only,
compiling the same source files as the Visual Studio solution. This
validates the CMake structure and dependency graph before adding any
platform-specific code.

See [BUILD.md](BUILD.md).

**Gate:** `cmake --build` on Windows produces a working `Altirra.exe`
identical to the `.sln` build. No source files are modified.

### Phase 2: Header Cleanup (compiles on both, runs on Windows only)

Add `#ifdef` guards to system headers so they parse on non-Windows
compilers. This is the smallest, most surgical set of changes -- only
`#ifdef` additions in existing headers, no behavior changes on Windows.

- `vdtypes.h`: Add `VD_OS_*` macros
- `win32/intrin.h`: Guard MSVC pragmas; add GCC/Clang `<x86intrin.h>` path
- `thread.h`: Guard Win32 forward declarations and `VDCriticalSection`/
  `VDSignalBase` member layouts
- `Error.h`: Guard `HWND__` typedef
- `file.h`: Replace `#error` with `typedef int VDFileHandle`
- Per-project `stdafx.h`: Guard `#include <windows.h>` and `<process.h>`

See [SYSTEM.md](SYSTEM.md) header changes section.

**Gate:** Windows build works unchanged. All modified headers compile on
Linux with GCC/Clang (even if linking fails -- we're only testing headers).

### Phase 3: System Library SDL3 Implementations

Add `_sdl3.cpp` source files for the system library: threading, file I/O,
filesystem, async I/O, config storage, timers. CMake selects these on
non-Windows.

See [SYSTEM.md](SYSTEM.md).

**Gate:** All core libraries (`ATCPU`, `ATEmulation`, `ATDevices`, `ATIO`,
`ATNetwork`, `ATCore`, `ATDebugger`, `ATBasic`, `ATCompiler`, `ATVM`,
`Kasumi`, `vdjson`, `ATAudio` synthesis-only) compile and **link** on
Linux. Run unit tests (`ATTest`) if possible.

### Phase 4: Simulator Library Extraction

Extract `ATSimulator` and its non-UI dependencies from `src/Altirra/` into
a library that both frontends can link. This is a mechanical refactoring
(move files, update includes, add build target).

See [BUILD.md](BUILD.md) simulator section.

**Gate:** Windows `.sln` build still works (the Altirra project pulls in the
same files). CMake on Linux compiles the simulator library.

### Phase 5: Minimal SDL3 Frontend (first pixels on screen)

Create `AltirraSDL` with: SDL3 window, `VDVideoDisplaySDL3` connected to
GTIA, SDL3 audio output via `ATAudioOutputSDL3`, basic keyboard input.
Hardcoded to load a ROM/disk image from command line.

See [DISPLAY.md](DISPLAY.md), [AUDIO.md](AUDIO.md), [INPUT.md](INPUT.md),
[MAIN_LOOP.md](MAIN_LOOP.md).

**Gate:** Run `AltirraSDL game.xex` on Linux -- see Atari screen, hear
audio, type on keyboard. No menus, no settings UI.

### Phase 6: Dear ImGui UI

Add menus, file dialogs (SDL3 native), settings dialogs, device
configuration. Progressive -- start with the most-used dialogs.

See [UI.md](UI.md).

**Gate:** All common operations accessible via GUI. Can configure hardware,
mount disk images, change settings -- all via ImGui.

### Phase 7: Debugger UI

Port the debugger panes (disassembly, memory, registers, console,
breakpoints) to Dear ImGui with docking.

See [UI.md](UI.md) debugger section.

**Gate:** Full debugging workflow works on Linux.

### Phase 8: Network and Remaining Features

Network socket emulation is implemented using POSIX sockets with `poll()`
(see [NETWORK.md](NETWORK.md)). The `ATNetworkSockets` library compiles
and links for Linux and macOS. Port any remaining features (profiler,
etc.) as needed.

**Gate:** Feature parity with Windows build.

## File Organization

All porting-related implementation details are in separate documents:

- [SYSTEM.md](SYSTEM.md) -- System library (threading, file I/O, filesystem, config)
- [DISPLAY.md](DISPLAY.md) -- Video display and rendering
- [AUDIO.md](AUDIO.md) -- Audio output
- [INPUT.md](INPUT.md) -- Keyboard, mouse, and gamepad input
- [UI.md](UI.md) -- Dear ImGui user interface
- [NETWORK.md](NETWORK.md) -- Network socket emulation
- [BUILD.md](BUILD.md) -- CMake build system and project structure
