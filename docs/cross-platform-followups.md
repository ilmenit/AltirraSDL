# AltirraSDL — Cross-platform follow-ups

Status snapshot of the long-running effort to bring AltirraSDL to full
parity with native Windows Altirra. Companion to `CLAUDE.md` (project
rules) — this file tracks **what's left to do** and the verified state
of each engine subsystem.

Last updated: April 2026, after closing Tier-1 (broken accessors) and
the bulk of Tier-2 (Win32-only engine source files).

## Status by tier

| Tier | Scope | State |
|---|---|---|
| 1 | Broken accessor stubs (drive sounds, vsync-adaptive, enhanced text mode, kernel switch, light pen, speed/resize) | **Done** |
| 2a | Engine sources excluded from CMake glob — fully portable | **Done** (custom device, CS8900A, uiqueue) |
| 2b | Engine sources excluded — needed POSIX backend | **Done** (modemtcp, idephysdisk, midimate, pipeserial) |
| 3 | Light Pen recalibration end-to-end | **Done** |
| 4 | Console history pane jump-to-cycle | **Done** |
| 5 | `commands_sdl3.cpp` ↔ `cmds.cpp` audit | **Partial** — 51% of Windows commands mirrored |
| 6 | Profiler + trace-viewer parity | **Pending** |
| 7 | Per-device configuration dialog audit | **Pending** |
| 8 | UX polish (out of emulation scope) | **Pending** |

## Sessions completed

Each session is one focused PR-sized change. Files marked **(read)** are
canonical Windows references that should not be touched from SDL3 work.

### Session 1 — Custom Device VM
Re-enabled the 5K-line scriptable device VM. The files were misnamed
"win32" but use only `at/atnetworksockets/nativesockets.h` (portable).
- Touched: `src/AltirraSDL/cmake/altirra_core_sources.cmake`,
  `src/AltirraSDL/stubs/device_stubs.cpp`,
  `src/AltirraSDL/stubs/win32_stubs.cpp`,
  `src/AltirraSDL/source/app/main_sdl3.cpp` (added bounded
  `ATUIGetQueue().Run()` drain),
  `src/system/source/filewatcher_sdl3.cpp` **(new — portable
  `VDFileWatcher`)**,
  `src/AltirraBridgeServer/ui_stubs.cpp`.
- Reference **(read)**: `src/Altirra/source/customdevice.cpp`,
  `customdevicevmtypes.cpp`, `customdevice_win32.cpp`,
  `src/Altirra/source/uiqueue.cpp`.
- Side fix: `src/Altirra/source/customdevicevmtypes.cpp:1662` —
  replaced `static_assert(sizeof(wchar_t)==2)` with `if constexpr`
  branch covering both UTF-16 and UTF-32 wchar_t.

### Session 2 — CS8900A Ethernet emulator
File had no Win32 includes — exclusion was misdiagnosed. Just
un-excluded.
- Touched: `altirra_core_sources.cmake`, `device_stubs.cpp`.
- DragonCart device factory (already enabled) now routes through
  the real emulator instead of the stub.

### Session 3 — TCP modem driver
- New: `src/AltirraSDL/source/os/modemtcp_sdl3.cpp` (~960 lines).
  POSIX sockets + `poll()` + self-pipe replacing Winsock2 +
  `WSAEvent`. Telnet protocol state machine ported verbatim.
- Replaces stub: `device_stubs.cpp:ATCreateModemDriverTCP`.

### Session 4 — Light pen recalibration
- Implemented in `src/AltirraSDL/stubs/uiaccessors_stubs.cpp`
  (`ATUIRecalibrateLightPen` body). Two-stage state machine on
  `ATLightPenPort::OnTriggerCorrectionEvent`. Status prompts via
  `IATUIRenderer::SetMessage(Prompt)`.
- Existing wiring: SDL3 mouse clicks already raise
  `TriggerCorrection` via `ATLightPenController::SetDigitalTrigger`
  (`inputcontroller.cpp:1386`).

### Session 5 — Debugger history JumpToCycle
- Added to `src/AltirraSDL/source/ui/debugger/ui_dbg_history.{h,cpp}`:
  `ATImGuiHistoryPaneImpl::JumpToCycle(uint32)` and `SelectInsn`
  (verbatim port of Win32 `uihistoryview.cpp:349`).
- New free helper: `ATUIDebuggerHistoryPaneJumpToCycle(uint32)`.
- `console_stubs.cpp:ATConsolePingBeamPosition` now decodes
  cycle-from-(frame,vpos,hpos) and dispatches.

### Session 6 — IDE physical disk passthrough
- New: `src/AltirraSDL/source/os/idephysdisk_sdl3.cpp` (~210 lines).
  `open(O_RDONLY|O_CLOEXEC)` + `pread` + Linux `BLKGETSIZE64` /
  macOS `DKIOCGETBLOCKCOUNT`. Read-only (matches Win32 semantics).
- Replaces stub: `device_stubs.cpp:ATIDEPhysicalDisk` + factory.
- Path convention: any `/dev/*` path is treated as a physical disk.

### Session 7 — MidiMate device
- New: `src/AltirraSDL/source/os/midimate_sdl3.cpp` (~440 lines).
  Verbatim port of MIDI parser + per-platform sink:
  - Linux → ALSA seq client (creates "Altirra/MidiMate Out" port).
  - macOS → CoreMIDI virtual source.
  - Windows-SDL3 → winmm `midiOut*`.
  - WASM/Android → no-op sink (parser still validates traffic).
- CMake: added `find_package(ALSA)` Linux, `-framework CoreMIDI`
  macOS.
- Replaces stub: `device_stubs.cpp:g_ATDeviceDefMidiMate`.

### Session 8 — PipeSerial device
- New: `src/AltirraSDL/source/os/pipeserial_sdl3.cpp` (~480 lines).
  AF_UNIX SOCK_STREAM listener (POSIX equivalent of Win32 named
  pipes). `poll()` + self-pipe instead of `WaitForMultipleObjects`.
- Path convention: `$XDG_RUNTIME_DIR/altirra-{name}.sock` or
  `/tmp/altirra-{name}.sock`.
- Replaces stub: `device_stubs.cpp:g_ATDeviceDefPipeSerial`.
- After this session, **all device factories in `device_stubs.cpp`
  are real implementations** — zero remaining device stubs.

### Session 9 — Command registry expansion
- Added 136 commands to `src/AltirraSDL/source/commands_sdl3.cpp`
  in a `kSDL3CommandsExtra[]` array.
- Coverage: 47 → 183 total registered, 97 mirroring Windows IDs
  (51% Windows-parity, up from 13%).
- Categories ported in full: Audio, Cassette settings,
  View filter/stretch/indicator toggles, Input keyboard
  layout/mode, System core toggles + memory/hardware/video
  shortcuts, Cart detach.

## Remaining work

### Outstanding command-registry gap (259 commands)

Run `comm -23 win_cmds.txt sdl3_cmds.txt | awk -F. '{print $1}' |
sort | uniq -c` to see the live breakdown. Current state:

| Category | Missing | Notes |
|---|---|---|
| System.* | 72 | Profile dialogs, dynamic kernel/basic switchers driven by Win32 menu rebuild, accelerator commands, command-line dialog. Most route to Win32 dialogs that don't exist in SDL3 (handled by Configure System ImGui pages instead — port if scripting needs them). |
| Video.* | 38 | GTIA defect modes, artifacting modes, palette controls. Many use `cmds.cpp`'s `SimEnumTest<>` template helpers. Worth porting (engine APIs all available). |
| Disk.* | 25 | Disk drive commands; mostly UI dialog wrappers around the Disks dialog. |
| Cart.* | 25 | `Cart.AttachXxx` shortcuts that pre-select a cartridge mapper. Useful but niche; route through file dialog with mapper hint. |
| View.* | 19 | Effect customization, calibration screen, video output cycling. |
| Console.* | 15 | Device-button helpers (BlackBox, IDEPlus2, IndusGT, Happy, ATR8000, XELCF, SX212). Each needs `ATUIGetDeviceButtonSupported`/`ATUIDepressDeviceButton` wiring to the specific device. |
| Devices.* | 12 | Device-specific commands (e.g. AddDevice quick-toggles). |
| Pane.* | 10 | Win32 panes that don't exist in SDL3 (Memory2-N, Watch, Breakpoints panel beyond Memory1). |
| Tools.* | 9 | Profile editor, ROM image set export, batch tools. Mostly Win32 dialogs. |
| Options.* | 6 | Auto-reset/boot-unload toggles. Need `ATUIResetFlag_*` mask infrastructure (currently bypassed in SDL3). |
| Help.* | 6 | Most are Win32-only (CommandLine, ChangeLog viewer). |
| Input.* | 5 | Customize-layout dialog, copy-to-custom layout. |
| File.* | 5 | QuickLoad/SaveState, LoadState, SaveState — exist as ImGui menu items but not as commands. |
| Edit.* | 5 | UI dialogs (Find/Goto Address, etc.). |
| Window.* | 4 | Win32 dock/undock — skip (no SDL3 equivalent). |

**To advance:** read each missing command in `cmds.cpp`, port the body
(usually a one-liner calling `g_sim.*` or `ATUI*`). Skip Win32-only
dialogs and Win32-only panes. Most of the 259 are 1-2 lines each.

### Session 10 — Profiler + trace-viewer parity (pending)

Windows `uiprofiler.cpp` shows a full timeline + per-symbol breakdown
+ source-line annotation. SDL3 has only a basic panel.

- Files to extend: `src/AltirraSDL/source/ui/debugger/ui_dbg_profiler.cpp`,
  `ui_dbg_traceviewer*.cpp`.
- Reference **(read)**: `src/Altirra/source/uiprofiler.cpp`,
  `uitraceviewer.cpp`.
- Verify by loading a known program with a profile recording and
  comparing tabs/columns/legend against a Windows screenshot at the
  same trace point.

### Session 11 — Per-device configuration dialog audit (pending)

Windows has ~40 `uiconfdev*.cpp` dialog files. SDL3 has migrated most
into Configure System pages but coverage is uneven.

- Audit step: `ls src/Altirra/source/uiconfdev*.cpp` and
  cross-reference against
  `src/AltirraSDL/source/ui/sysconfig/ui_system_pages_*.cpp`.
- For each Windows dialog confirmed missing in SDL3, add a sidebar
  page or extend the existing one with the same controls.

### Session 12 — UX polish (out of emulation scope)

Listed for completeness; do not block on these.
- `ATAsyncDownloadUrl` (update checker) — currently no-op. Implement
  via libcurl or platform-native HTTP.
- `ATSetProcessEfficiencyMode` (P/E core scheduling) — currently
  no-op in `device_stubs.cpp`. Per-platform: Linux `sched_setaffinity`,
  macOS `pthread_set_qos_class_self_np`, Windows `SetProcessAffinityMask`.
- `ATUIShowGenericDialog` "don't show again" checkbox — needs custom
  ImGui modal (SDL3 message boxes lack the affordance).
- `ATUITemporarilyMountVHDImageW32` — Win32 admin-elevation path;
  leave stubbed.
- `ATUpdateVerifyFeedSignature` — feed signature crypto.
- `ATConsoleSetFont` — wire to ImGui font selection.

### Smaller follow-ups (not session-sized)

- **`uiconfirm.cpp` confirmation dialogs** — System-change-reset
  prompts (`ATUIConfirmSystemChangeReset`,
  `ATUIConfirmBasicChangeReset`) are not stubbed in SDL3, which means
  some `cmd*.cpp` bodies that wrap their changes in confirm prompts
  silently bypass the prompt when ported. Either port the dialog as
  an ImGui modal or stub the functions to "always yes" and rely on
  the caller's UX context to be unsurprising.
- **VDFileWatcher polling** — current `filewatcher_sdl3.cpp` polls
  via stat each call. Custom Device hot-reload works but is
  polling-based. A future enhancement could use inotify on Linux,
  FSEvents on macOS, but the polling cadence (1× per consumer poll)
  is fine for the use case.
- **Light pen ImGui prompt overlay** — current implementation uses
  the status-message bar (`SetMessage(Prompt)`). Adequate, but a
  modal overlay with crosshair guides would match the Windows
  experience more closely.

## Tier-2 implementation reference

Quick lookup table for each Win32-only engine source that has a SDL3
counterpart. Use this when investigating a device-specific bug to
confirm which file is actually being compiled.

| Win32 source (read-only) | SDL3 implementation | Linked into bridge? |
|---|---|---|
| `customdevice_win32.cpp` | itself (re-included; no Win32 deps) | yes |
| `customdevice.cpp` | itself (un-excluded) | yes |
| `customdevicevmtypes.cpp` | itself (un-excluded) | yes |
| `cs8900a.cpp` | itself (un-excluded) | yes |
| `idephysdisk.cpp` | `source/os/idephysdisk_sdl3.cpp` | yes |
| `midimate.cpp` | `source/os/midimate_sdl3.cpp` | yes |
| `modemtcp.cpp` | `source/os/modemtcp_sdl3.cpp` | yes |
| `pipeserial_win32.cpp` | `source/os/pipeserial_sdl3.cpp` | yes |
| `directorywatcher.cpp` | `source/os/directorywatcher_sdl3.cpp` | yes |
| `uiqueue.cpp` | itself (re-included via explicit list) | yes |
| `system/source/filewatcher.cpp` | `system/source/filewatcher_sdl3.cpp` | n/a |

**Win32-only sources still excluded** (these stay Win32-only; no SDL3
equivalent planned):
- `about.cpp` (richedit.h)
- `allocator.cpp` (MSVC debug heap)
- `cmd*.cpp` (Win32 menu/accelerator framework)
- `profilerui.cpp` (`w32assist.h`)
- `startuplogger.cpp`
- `texteditor.cpp` (Win32 HWND text widget)
- All `ui*.cpp` files (replaced by SDL3 ImGui equivalents)

## Verification recipe

For any change to engine code or stubs, run all three target builds:

```sh
cmake --build build/linux-release  -j$(nproc)
cmake --build build/wasm-release   -j$(nproc)
cmake --build build/bridge-test    -j$(nproc)
```

Smoke-launch:
```sh
build/linux-release/src/AltirraSDL/AltirraSDL --test-mode &
SOCK=$(ls -t /tmp/altirra-test-*.sock | head -1)
echo ping | nc -U "$SOCK"   # expect: {"ok":true}
```

Symbol check (confirm a port resolves to its real implementation):
```sh
nm -C build/linux-release/src/AltirraSDL/AltirraSDL | grep <symbol>
```

Native Windows `.sln` build is the gold reference and must never be
broken by SDL3 work. Verify by opening `src/Altirra.sln` in Visual
Studio and rebuilding Release|x64.
