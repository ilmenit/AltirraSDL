# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Altirra is an Atari 800/800XL/5200 emulator, authored by Avery Lee. Licensed under GNU GPL v2+. The codebase is ~520K lines of C++ with 6502 assembly for the kernel ROM.

The project supports two build paths:
- **Windows (primary):** Native Win32 UI, Direct3D display, built via Visual Studio `.sln`
- **Cross-platform (in progress):** SDL3 + Dear ImGui frontend, built via CMake, targeting Linux and macOS

See `PORTING/` for the full cross-platform implementation plan.

## Build System

### Windows (Visual Studio — primary)

**Requirements:** Windows 10 x64+, Visual Studio 2022 v17.14+ (v143 toolset), Windows 11 SDK (10.0.26100.0+), MADS 2.1.0+ (6502 assembler).

**Solution files:**
- `src/Altirra.sln` — Main emulator (32 projects)
- `src/AltirraRMT.sln` — Raster Music Tracker plugins
- `src/ATHelpFile.sln` — Help file (requires .NET 4.8, C++/CLI, HTML Help 1.4)

**Build steps:**
1. Open `src/Altirra.sln`, set startup project to `Altirra`
2. First build must be **Release x64** (or Release ARM64 on ARM) to compile build tools used by other configurations
3. Then build any configuration: Debug (unoptimized), Profile (optimized), Release (LTCG)

**Output:** `out/` directory. Intermediates in `obj/`, libraries in `lib/` (all deletable).

**Kernel ROM:** Built via `src/Kernel/Makefile` using MADS assembler. The kernel is 6502 assembly.

**Release builds:** `py release.py` from a VS Developer Command Prompt. Requires Python 3.10+, 7-zip, AdvanceCOMP.

**Local overrides:** Place `.props` files in `localconfig/active/` (see `localconfig/example/` for templates). MADS path can be overridden via `ATMadsPath` property in `localconfig/active/Altirra.local.props`.

### Cross-Platform (CMake — in progress)

**Requirements:** CMake 3.24+, C++17 compiler (GCC or Clang), SDL3, Dear ImGui (vendored).

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(nproc)
./src/AltirraSDL/AltirraSDL
```

The CMake build exists alongside the `.sln` — it does not replace it. See `PORTING/BUILD.md` for details.

## Code Style

- **Indentation:** Tabs for C++/headers/shaders (`.cpp`, `.h`, `.inl`, `.fx`, `.fxh`), spaces for other file types
- **Guideline width:** 80 columns
- See `src/.editorconfig` for per-filetype settings

## Architecture

The project is organized as ~32 Visual Studio projects under `src/`, each a distinct module:

### Core Emulation (platform-agnostic — zero Win32 API calls)
- **ATCPU** — 6502/65C02/65C816 CPU emulation (cycle-accurate)
- **ATEmulation** — Hardware emulation (ANTIC video, GTIA graphics, POKEY audio/IO, PIA)
- **ATDevices** — Peripheral device emulation (disk drives, cassette, serial, etc.)
- **ATIO** — Disk/tape image I/O and format handling
- **ATNetwork** — Emulated Atari network stack (IP, TCP, UDP)
- **ATDebugger** — Debugger backend (symbols, targets, breakpoints)
- **ATCompiler / ATVM** — Compiler infrastructure and virtual machine for BASIC
- **ATCore** — Core utilities, base classes, scheduler
- **Kasumi** — Image processing and blitting
- **vdjson** — JSON parser

### Platform-Dependent (Win32 on Windows, SDL3 replacements on other platforms)
- **ATAudio** — Audio synthesis (cross-platform) + output backends (platform-specific)
- **ATNetworkSockets** — OS socket adapter (Winsock on Windows, POSIX sockets on others)
- **system** — System abstraction layer (threading, file I/O, filesystem, config)

### Windows-Only (not compiled for SDL3 build)
- **Altirra** — Main Win32 application (~225+ source files, includes UI)
- **ATNativeUI** — Windows native UI (Win32 HWND, dialogs, menus)
- **ATUI / ATUIControls** — Custom widget system (depends on VDDisplay)
- **VDDisplay** — Display rendering (GDI/D3D9 — 15+ Win32-dependent files)
- **Riza** — Win32 audio backends (WaveOut, DirectSound, XAudio2, WASAPI)
- **Dita** — Dialog toolkit (COM/shell APIs)
- **Tessa** — Win32-specific
- **ATAppBase** — Win32 application base framework
- **AltirraShell** — Shell integration (file associations)
- **Asuka** — Build tool (compiled first, used by other projects)

### SDL3 Frontend (new, cross-platform)
- **AltirraSDL** — SDL3 + Dear ImGui frontend (display, audio, input, UI)

### Software Emulation
- **Kernel** — Atari OS kernel ROM implementation in 6502 assembly (~97 .s/.xasm files)
- **ATBasic** — Atari BASIC ROM in 6502 assembly (.s files, built by MADS)

### Headers
Shared headers live in `src/h/` with two namespaces:
- `src/h/at/` — Altirra-specific headers (atcore, atcpu, atemulation, etc.)
- `src/h/vd2/` — Legacy VirtualDub2 library headers (Kasumi, Dita, Riza, Tessa, VDDisplay, system)

### Build Support
- **Build/** — MSBuild property sheets (`.props`/`.targets`)
- **Shared/** — Shared resources including `altirra.natvis` for Visual Studio debug visualization

## Cross-Platform Porting

Detailed documentation lives in `PORTING/`:

| Document | Contents |
|----------|----------|
| `OVERVIEW.md` | Architecture, design principles, 8-phase plan |
| `SYSTEM.md` | System library (threading, file I/O, filesystem, VDStringW/wchar_t, config) |
| `BUILD.md` | CMake structure, excluded libraries, simulator extraction |
| `DISPLAY.md` | SDL3 video display, frame submission protocol |
| `AUDIO.md` | SDL3 audio output via IATAudioOutput |
| `INPUT.md` | SDL3 event to ATInputCode translation |
| `UI.md` | Dear ImGui replacing ATNativeUI and ATUI |
| `NETWORK.md` | POSIX sockets replacing Winsock in ATNetworkSockets |
| `MAIN_LOOP.md` | SDL3 main loop replacing Win32 message pump |

**Key design principles:**
- Minimal changes to existing files (`#ifdef` guards only where platform types appear in headers)
- New `_sdl3.cpp` files alongside existing Win32 `.cpp` files; build system selects which to compile
- The Windows `.sln` build is always preserved and must never break
- SDL3 is the primary platform layer for non-Windows (exception: ATNetworkSockets uses POSIX sockets because SDL3_net lacks the low-level socket access required)
- Only two external dependencies: SDL3 and Dear ImGui
- No stubs, no placeholders. When implementing, do full implementation of functionality, aim to make implementation complete and error free.
- We need full equivalent implementaiton to Windows Altirra. Don't ommit anything.
- After implementation, look for errors, bugs, mistakes. Fix if spotted. If some were identified, repeat, because maybe we didn't cover everything properly.

## Testing

- **ATTest** — Internal C++ test framework (~1,600 lines)
- **AltirraTest** — Test harness project
- **ATBasic/tests/** — BASIC interpreter test suite (~25 `.txt` test files)
- **UI Test Mode** — Automated ImGui UI testing via `--test-mode` flag.
  Launches a Unix domain socket at `/tmp/altirra-test-<pid>.sock` that
  accepts text commands and returns JSON responses. Uses ImGui's built-in
  `IMGUI_ENABLE_TEST_ENGINE` hooks to automatically track all widgets
  (buttons, checkboxes, menu items, etc.) with their labels, types, and
  positions each frame. Commands: `ping`, `query_state`, `list_items`,
  `open_dialog`/`close_dialog`, `click <window> <label>`, `wait_frames`,
  `screenshot`, `boot_image`, `pause`/`resume`, `cold_reset`/`warm_reset`.
  See `PORTING/BUILD.md` for full command reference.
  Files: `src/AltirraSDL/source/ui_testmode.h`, `ui_testmode.cpp`.

## Key Design Notes

- Do not consider anything "lower priority" like Debugger or Tools. We aim for 100% coverage of Windows implementation, so we don't have low priority items.
- The Windows build uses Win32 API throughout with no external dependencies (statically linked, no redistributables needed).
- The VD2/VirtualDub2 libraries (Kasumi, Dita, Riza, Tessa, system) are legacy foundations; their headers use the `vd2/` namespace.
- The `system` library uses `VDStringW` (wchar_t) for all file paths. On non-Windows, `_sdl3.cpp` files convert to UTF-8 at the OS boundary using `VDTextWToU8()` / `VDTextU8ToW()` from `text.h`.
- Link errors during build usually mean an upstream project failed — always look for the first error.
- Core emulation libraries (ATCPU, ATEmulation, ATDevices, ATIO, ATNetwork, ATDebugger, ATCompiler, ATVM, Kasumi, vdjson) contain zero Win32 API calls and compile unchanged on any platform once system library headers are cleaned up.
- **Settings persistence:** The SDL3 build stores settings in `~/.config/altirra/settings.ini` using the same INI format as Windows Altirra's portable mode. `ATRegistryLoadFromDisk()` loads on startup, `ATRegistryFlushToDisk()` saves on exit. The `VDRegistryProviderMemory` provider is the in-memory backing store; `ATUILoadRegistry`/`ATUISaveRegistry` (from `uiregistry.cpp`) handle serialization.
- Do not claim anything as NICHE, rare use-case. Do not depreciate any functionality implemented in the emulator. The whole emulator is a rare use-case and we aim for 100% compatibiltiy with Windows version.

## SDL3 Porting Pitfalls

### Member default values assume Windows settings will override them

The emulation core classes have member defaults in their headers (e.g.
`mFoo = SomeEnum::NonObviousValue`).  On Windows, `settings.cpp` and the
UI command handlers load user preferences from the registry and call the
appropriate setters, so the defaults rarely matter.  The SDL3 build now
**includes** `settings.cpp` (and calls `ATLoadSettings`/`ATSaveSettings`)
but still **excludes** the `cmd*.cpp` files, so any setting that is only
changed by a command handler (and not by `settings.cpp`) may still use its
member initialiser default.

**Rule:** When the SDL3 frontend creates or initialises an emulation
object, explicitly set every configuration property to match the Windows
defaults rather than relying on the member initialiser.  The canonical
reference for "what Windows sets" is `settings.cpp` (load path) and the
`cmd*.cpp` command handlers (UI defaults).

### Debugging rendering or emulation mismatches

When the SDL3 output differs from Windows Altirra, the problem is almost
never in the core rendering code — it is correct and battle-tested.
Follow this checklist:

1. **Check which code path is actually executing.**  Add a one-shot
   `fprintf(stderr, ...)` at each dispatch branch (not in tight loops)
   to confirm the expected function is called.  A wrong mode, flag, or
   enum silently routes to a different renderer.
2. **Compare configuration state, not just output pixels.**  Dump the
   relevant flags/enums (`mPRIOR`, `mDefectMode`, `mbHiresMode`,
   `mArtifactMode`, frame properties, etc.) and compare with what
   Windows would have after loading default settings.
3. **Trace from the display back to the renderer.**  Start from the
   displayed frame data, identify which render function wrote those
   bytes, then check why that function was chosen.
4. **Force-recompile touched files.**  The CMake incremental build
   sometimes misses timestamp changes — use `touch <file>` before
   `cmake --build` to be sure.

### Dear ImGui UI must match Windows Altirra exactly

The SDL3 Dear ImGui UI must replicate Windows Altirra's menus and
dialogs faithfully — same options, same layout, same location.  Do not
invent new dialogs, rearrange options for "convenience", or add controls
that don't exist in the Windows version, until explicitly asked to do it.

**Rule:** Before implementing or modifying any UI element, check what the
Windows version shows in that exact context.  The canonical references
are `menu_default.txt` (menu structure), the `cmd*.cpp` command handlers
(what each item does), and the `ui*.cpp` dialog implementations (dialog
layout).

**Key structural facts:**
- Windows has one large **Configure System** paged dialog (28+ category
  pages in a tree sidebar).  The SDL3 version replicates this as a
  single `ATUIRenderSystemConfig()` with a sidebar — **not** as multiple
  separate dialog windows.
- Audio, Display, Disk, Cassette, and Input settings that appear inside
  Configure System in Windows must appear as categories in the SDL3
  System Configuration sidebar, not as standalone dialogs.
- The main **menu bar** provides quick-access toggles (like Filter Mode,
  Overscan, Speed, BASIC) *in addition to* Configure System.  Both
  paths exist in Windows; replicate both.
- **Tape Control** is a separate dialog (transport + position).
  Cassette *settings* (SIO patch, turbo defaults) are in Configure
  System → Cassette page.
- **Disk Drives** is a separate dialog (mount/eject/emulation mode).
  Disk *settings* (SIO patch, timing) are in Configure System →
  Acceleration page.

**Checklist for new UI work:**
1. Find the Windows equivalent (menu item, dialog, or page)
2. Verify every option and its location
3. Do not add options that the Windows version doesn't show in that
   context, do it only on explicit request. 
4. Do not omit options that the Windows version does show

### ImGui window positioning: config dialogs vs. debugger panes

Dear ImGui draws everything onto the single SDL window canvas — there
are no separate OS windows like Win32 `HWND` dialogs.  Saved positions
in `imgui.ini` are absolute screen coordinates, so a dialog positioned
in fullscreen can end up outside the visible area after restarting in a
smaller window.

Windows Altirra avoids this because most dialogs (including Configure
System) do **not** save their position — they open at the OS default
(centered on parent) every time.  Only a few tool windows (Firmware
Manager, File Viewer) persist placement.

The SDL3 build follows the same split:

**Config/modal dialogs** — center on the main window every time they
open, no position persistence:
```cpp
ImGui::SetNextWindowSize(ImVec2(w, h), ImGuiCond_Appearing);
ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
    ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
ImGui::Begin("Title", &open,
    flags | ImGuiWindowFlags_NoSavedSettings);
```
- `ImGuiCond_Appearing` re-centers each time the dialog goes from
  closed → open.
- `ImGuiWindowFlags_NoSavedSettings` prevents `imgui.ini` from storing
  the position (and from overriding the centering on subsequent opens).

**Debugger/tool panes** — use normal `ImGui::Begin()` without
`NoSavedSettings`, so `imgui.ini` persists their docked layout across
sessions.

**Main SDL window** — size, position, and fullscreen state are saved to
`settings.ini` (under `Window Placement` key) on exit and restored on
startup.  The windowed-mode geometry is cached on every resize/move
event so it is preserved even when the user closes from fullscreen.

**Rule:** Every new `ImGui::Begin()` call must follow one of these two
patterns.  Config dialogs get centering + `NoSavedSettings`; persistent
tool windows get plain `Begin()`.

### Understand API state machines before calling methods

The simulator has independent state axes (running/paused, hardware
state, media state).  Methods like `ColdReset()` change hardware state
but do **not** change running state — an emulator that was paused stays
paused after reset.  Calling a method without understanding which state
axes it touches leads to inconsistent states.

**Rule:** Before calling any simulator or hardware API, understand what
state it changes and what it leaves unchanged.  If the intended user
action requires multiple state changes, make all of them explicitly.

### Assume all callbacks are called from arbitrary threads

Any API that accepts a callback — file dialogs, audio streams, network
I/O, timers — may invoke it from a thread other than the caller's.
The emulator core is single-threaded and not safe for concurrent
access.

**Rule:** Callbacks from external APIs must never directly mutate
shared state.  Capture only plain data (paths, indices, flags) and
hand it to the main thread via a thread-safe mechanism.  Process the
results synchronously in the main loop.

### Every acquired state must have a release path for all exit scenarios

If code acquires stateful resources on an event (key press → joystick
direction, mouse capture, console switch hold), the corresponding
release must fire on **every** exit path: normal release, focus loss,
UI overlay capture, error, shutdown.  Missing any one path causes
stuck state.

**Rule:** When implementing any press/release, capture/release, or
acquire/release pattern, enumerate all the ways the release event can
be lost and add explicit cleanup for each.

### Never guess hardware-specific constants

Atari hardware encodings (keyboard scancodes, register bit layouts,
PIA port assignments) are non-obvious and don't follow intuitive
patterns.  Values guessed from documentation, memory, or apparent
patterns are wrong more often than right.

**Rule:** Every hardware-facing constant must be traced to an
authoritative source — the existing codebase, a hardware reference
manual, or a verified test.  If two sources disagree, test on real
hardware or use the value from the battle-tested Windows implementation.

### Keep UI representation and behaviour in sync

When the UI displays information to the user (shortcut hints, status
indicators, enabled/disabled states), it creates a contract.  If the
displayed information doesn't match actual behaviour, users will report
bugs even when the underlying functionality is correct.

**Rule:** Any user-visible text or state that implies functionality
(keyboard shortcut labels, checkbox states, enabled menu items) must be
backed by working code.  When changing behaviour, update the display.
When changing the display, update behaviour.
