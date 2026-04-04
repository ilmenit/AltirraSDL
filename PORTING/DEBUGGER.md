# Debugger (Dear ImGui)

## Scope

The Windows Altirra debugger is ~48,000 lines across 70+ files: a command
console, 13 dockable panes, profiler, trace viewer, and verifier.  The SDL3
port replicates all of this using Dear ImGui dockable windows.

## Architecture

### Three Layers

```
Windows                              SDL3 (ImGui)
-------                              ----
debugger.cpp (ATDebugger engine)  →  Same file, compiled via stub headers
console.cpp  (Win32 pane mgmt)   →  ui_debugger.cpp (ImGui pane manager)
uidbg*.cpp   (Win32 HWND panes)  →  ui_dbg_*.cpp (ImGui panes)
```

**Layer 1 — Engine (`debugger.cpp`, 14,149 lines):** The `ATDebugger` singleton
implementing `IATDebugger`.  Contains all command processing, breakpoint
management, expression evaluation, symbol loading, stepping logic.  This file
compiles unchanged for SDL3 — its only Win32 dependency was an unnecessary
`#include <at/atnativeui/uiframe.h>` which is satisfied by a stub header.

**Layer 2 — Pane Manager (`ui_debugger.cpp`):** Replaces Win32
`ATUIPane`/`ATContainerWindow`/`ATFrameWindow` docking system with a simple
registry of ImGui panes.  Provides real implementations of:
- `ATActivateUIPane()` — open/focus a debugger pane
- `ATRegisterUIPaneType()` / `ATRegisterUIPaneClass()` — pane registration
- `ATUIDebuggerOpen()` / `ATUIDebuggerClose()` — debugger enable/disable
- `ATUIDebuggerTick()` — called each frame to process queued commands
- `ATUIDebuggerRenderPanes()` — renders dockspace + Display pane + all
  debugger panes each frame

**Layer 3 — ImGui Panes (`ui_dbg_*.cpp`):** Each debugger pane is an
`ATImGuiDebuggerPane` subclass that:
- Implements `IATDebuggerClient` for state update callbacks
- Caches state in `OnDebuggerSystemStateUpdate()`
- Renders from cache each frame via `Render()`
- Docks into the shared dockspace (layout persisted via `imgui.ini`)

### Initialization Sequence

The SDL3 startup mirrors the Windows initialization order:

```
g_sim.Init()           → Simulator ready
ATInitDebugger()       → Breakpoint manager, event callbacks, debug targets
ATLoadSettings(...)    → Load debugger settings from registry
...main loop...
ATShutdownDebugger()   → Cleanup before simulator shutdown
g_sim.Shutdown()
```

`ATInitDebugger()` calls `g_debugger.Init()` which is critical — without
it, breakpoints don't work and the debugger won't receive simulator events.

### Docking Layout

When the debugger opens, a full-window ImGui DockSpace is created below the
menu bar.  The emulation display becomes a dockable "Display" ImGui window
(using `ImGui::Image()` with the SDL texture) instead of rendering as a
background.  The default layout replicates Windows `ATLoadDefaultPaneLayout`
(console.cpp:921):

```
┌──────────────────────┬────────────────────┐
│                      │  Registers         │
│    Display           │  Disassembly (tab) │
│                      │  History (tab)     │
├──────────────────────┴────────────────────┤
│              Console (focused)             │
└───────────────────────────────────────────┘
```

The layout is built programmatically via `ImGui::DockBuilder*()` on first
open.  After that, ImGui's own `imgui.ini` persistence handles layout
changes the user makes (resizing, rearranging, undocking).

When the debugger is closed, the Display pane disappears and the emulation
texture reverts to direct SDL rendering as a background.

### Console Output Routing

```
ATConsoleWrite(s)  →  console_stubs.cpp  →  g_pConsoleWindow->Write(s)
                                             + g_pConsoleWindow->ShowEnd()
                                         →  (fallback: shared buffer g_consoleText)
```

Console output functions (`ATConsoleWrite`, `ATConsolePrintfImpl`,
`ATConsoleTaggedPrintfImpl`) are in `console_stubs.cpp`.  When the console
pane is open, they route to `IATUIDebuggerConsoleWindow::Write()` followed
by `ShowEnd()` (matching Windows behavior).  Before the pane exists, text
accumulates in a shared `std::string` buffer (`g_consoleText`) protected
by a mutex.

### Command Execution Flow

Commands are **queued**, not executed directly:

```
User presses Enter  →  ATConsoleQueueCommand(s)
                    →  g_debugger.QueueCommand(s, true)
                    →  mCommandQueue (deque)

ATDebugger::Tick()  →  if (!g_sim.IsRunning())
                    →  dequeue + ATConsoleExecuteCommand()
```

`Tick()` only processes commands when the simulator is stopped.  This
prevents command execution during emulation, matching Windows behavior.

### Console Run State

The console input is disabled (grayed out) when the simulator is running.
When the debugger breaks, the input is re-enabled and auto-focused.
This matches Windows `OnRunStateChanged` behavior in `uidbgconsole.cpp`.

Opening the debugger automatically breaks execution so the console is
immediately usable.

### Stub Headers

Two stub headers allow cross-platform compilation of files that include
Win32-dependent headers:

| Stub Header | Satisfies | Used By |
|-------------|-----------|---------|
| `stubs/at/atnativeui/uiframe.h` | `<at/atnativeui/uiframe.h>` | `debugger.cpp` |
| `stubs/at/atui/uicommandmanager.h` | `<at/atui/uicommandmanager.h>` | `debuggerautotest.cpp` |

These provide only the declarations needed (forward declarations, enums,
function signatures) without pulling in `<windows.h>`.  The stubs directory
is configured with `BEFORE PRIVATE` in CMake so it takes precedence.

### Keyboard Shortcuts

When the debugger is open, these keys are intercepted before emulation:

| Key | Action |
|-----|--------|
| F5 | Run/Break (toggle) |
| F10 | Step Over |
| F11 | Step Into |
| Shift+F11 | Step Out |

When the debugger is closed, F5/F9 retain their emulation functions
(Warm Reset / Pause).

## Key Files

### New SDL3 files (`src/AltirraSDL/`)

| File | Lines | Purpose |
|------|-------|---------|
| `source/ui_debugger.h` | ~90 | Pane manager API, ATImGuiDebuggerPane base class |
| `source/ui_debugger.cpp` | ~430 | Pane registry, dockspace, Display pane, open/close, tick, stepping |
| `source/ui_dbg_console.cpp` | ~280 | Console pane (command I/O, history) |
| `source/ui_dbg_registers.cpp` | ~210 | Registers pane (all CPU modes) |
| `source/ui_dbg_disassembly.cpp` | ~230 | Disassembly pane (live around PC) |
| `source/ui_dbg_history.cpp` | ~250 | CPU history pane (past instructions) |
| `stubs/at/atnativeui/uiframe.h` | ~45 | Stub: forward decls, no windows.h |
| `stubs/at/atui/uicommandmanager.h` | ~130 | Stub: ATUICommandManager full header |

### Modified SDL3 files

| File | Change |
|------|--------|
| `CMakeLists.txt` | Removed exclusions for debugger.cpp, debuggerautotest.cpp, cpuheatmap.cpp; added new source files |
| `stubs/console_stubs.cpp` | Console output routes to ImGui pane; ATOpenConsole wired to debugger |
| `stubs/win32_stubs.cpp` | Removed ATGetDebugger stub; added ATUISaveFrame stub |
| `stubs/uiaccessors_stubs.cpp` | Removed duplicate enum tables; added ATUIGetCommandManager |
| `stubs/device_stubs.cpp` | Removed ATCPUHeatMap stubs (real cpuheatmap.cpp now compiled) |
| `source/main_sdl3.cpp` | Added ATInitDebugger/ATShutdownDebugger, ATUIDebuggerTick(), keyboard shortcuts; display rendering conditional on debugger state |
| `source/ui_menus.cpp` | Debug menu items wired to real debugger functions |

### Reused from Windows build (compiled unchanged)

| File | Lines | Purpose |
|------|-------|---------|
| `debugger.cpp` | 14,149 | ATDebugger engine (commands, breakpoints, symbols) |
| `debuggerautotest.cpp` | 684 | Automated test commands |
| `debuggerexp.cpp` | 3,233 | Expression evaluator |
| `debuggersettings.cpp` | 161 | Settings persistence |
| `debuggerlog.cpp` | 79 | Debug logging |
| `cpuheatmap.cpp` | ~500 | Memory access tracking |
| `bkptmanager.cpp` | ~200 | Breakpoint management |
| `verifier.cpp` | 545 | CPU verification checks |
| `profiler.cpp` | 1,046 | Performance profiler backend |
| `cputracer.cpp` | 472 | CPU trace collection |
| `disasm.cpp` | 2,600+ | Disassembly engine (all CPU modes) |

### ATDebugger library (already compiled)

The `ATDebugger` static library (`src/ATDebugger/`) compiles and links
unchanged.  It provides: symbol I/O, history tree, expression nodes,
breakpoint implementation, debug targets, default symbols.

## Pane Implementation Status

| Pane | Status | File | Context Menu |
|------|--------|------|--------------|
| Display | Done | `ui_debugger.cpp` (inline) | N/A |
| Console | Done | `ui_dbg_console.cpp` | Missing: Copy |
| Registers | Done | `ui_dbg_registers.cpp` | N/A |
| Disassembly | Done | `ui_dbg_disassembly.cpp` | 4/18 items (14 missing) |
| History | Done | `ui_dbg_history.cpp` | 1/21 items (20 missing) |
| Memory (x4) | Done | `ui_dbg_memory.h`, `_memory.cpp`, `_memory_hexdump.cpp`, `_memory_bitmap.cpp` | Done |
| Watch (x4) | Done | `ui_dbg_watch.cpp` | N/A |
| Call Stack | Done | `ui_dbg_callstack.cpp` | N/A |
| Breakpoints | Done | `ui_dbg_breakpoints.cpp` | N/A |
| Targets | Done | `ui_dbg_targets.cpp` | N/A |
| Source | Done | `ui_dbg_source.cpp` | 4/6 items (2 missing) |
| Debug Display | Done | `ui_dbg_debugdisplay.cpp` | 3/6 items (3 missing) |
| Printer Output | Done | `ui_dbg_printer.cpp` | 1/7 items (6 missing) |
| Profiler | TODO | — | — |
| Trace Viewer | TODO | — | — |

## Dialog Implementation Status

| Dialog | Status | Notes |
|--------|--------|-------|
| Verifier | TODO | 11 verification checks |
| New/Edit Breakpoint | TODO | Condition editor, command-on-hit, location type |
| Debug Font | TODO | Font selection for console |
| Source File List | Done | ImGui dialog |
| Trace Settings | TODO | Trace recording configuration |
| Profiler Boundary Rule | TODO | Profiler configuration |

## Debug Menu Status

| Item | Status |
|------|--------|
| Enable Debugger | Done |
| Run/Break (F5) | Done |
| Break | Done |
| Step Into (F11) | Done |
| Step Over (F10) | Done |
| Step Out (Shift+F11) | Done |
| Break at EXE Run Address | Done |
| Window > All 13 panes | Done |
| Open Source File | Done (SDL3 file dialog) |
| Source File List | Done |
| Visualization (GTIA + ANTIC) | Done |
| Auto-Reload ROMs | Done |
| Randomize Memory | Done |
| Change Font | TODO |
| Profile View | TODO |
| Verifier | TODO |
| Performance Analyzer | TODO |

## Design Decisions

1. **debugger.cpp compiles unchanged** — only the `#include` is satisfied
   by a stub header.  No `#ifdef` in the source file.

2. **ImGui docking for pane layout** — a full-window `ImGui::DockSpace()`
   is created when the debugger is open.  The emulation display becomes
   a dockable "Display" pane.  Layout is initialized via `DockBuilder`
   and persists via `imgui.ini`.  Config/modal dialogs use centering +
   `NoSavedSettings` (per CLAUDE.md rules).

3. **Console output buffering** — text is accumulated in a `std::string`
   protected by a mutex.  This allows output from debugger commands
   (main thread) and trace logging (potentially other threads) to
   coexist safely.

4. **Shared ATUICommandManager** — `uicommandmanager.cpp` from the ATUI
   library is compiled directly into the SDL3 build (no Win32 deps).
   This provides the real command manager needed by `debuggerautotest.cpp`.

5. **Break on debugger open** — unlike Windows (which opens the debugger
   without breaking), the SDL3 build breaks execution when the debugger
   is enabled.  Since all panes share the same SDL window, the user
   expects the console to be immediately usable.

6. **Display pane vs background rendering** — when the debugger is closed,
   the backend renders the emulation frame directly to the window.  When
   the debugger opens, the Display pane calls
   `ATUIGetDisplayBackend()->GetImGuiTextureID()` and renders it via
   `ImGui::Image()` inside a dockable window, so it participates in the
   layout alongside other panes.  This works with both the SDL\_Renderer
   and OpenGL backends.

7. **vdrefcounted pane lifecycle** — panes are allocated with `new`
   (refcount 0) and stored in `vdrefptr` (refcount 1).  The creator must
   NOT call `Release()` after registration — `vdrefcounted` starts at
   refcount 0, not 1.
