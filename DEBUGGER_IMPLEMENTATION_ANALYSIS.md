# AltirraSDL Debugger Implementation Analysis

Date: 2026-07-06

This document compares the AltirraSDL debugger UI against native Windows
Altirra. Windows remains the reference for debugger behavior, emulator state
semantics, and command routing. Exact visual parity is not required when the SDL
frontend has a useful improvement, but SDL-specific additions should be listed
explicitly so they can be reviewed as intentional behavior instead of accidental
drift.

## Scope

Reviewed areas:

- Native debugger integration:
  - `src/Altirra/source/console.cpp`
  - `src/Altirra/source/cmddebug.cpp`
  - `src/Altirra/source/cmds.cpp`
  - `src/Altirra/source/uidbg*.cpp`
  - `src/Altirra/h/uidbg*.h`
  - `src/Altirra/res/menu_default.txt`
- SDL debugger integration:
  - `src/AltirraSDL/source/ui/debugger/*.cpp`
  - `src/AltirraSDL/source/ui/debugger/*.h`
  - `src/AltirraSDL/stubs/console_stubs.cpp`
  - `src/AltirraSDL/source/commands_sdl3.cpp`
  - `src/AltirraSDL/source/ui/menus/ui_menus_debug.cpp`
  - `src/AltirraSDL/source/app/main_sdl3.cpp`
  - `src/AltirraSDL/source/display/*`

The review focuses on the debugger UI and its integration with the existing
debugger core. It does not attempt to prove correctness of the emulator core.

This file has been re-audited against the code after the initial pass. Claims
below are written against code paths that were inspected. Where a point is a
risk rather than an observed runtime failure, it is labelled as a risk.

## Implementation Status, 2026-07-06

Follow-up implementation has started from this analysis. The live tracker is
`DEBUGGER_IMPROVEMENTS.md`.

Verified fixes now present in the code:

- `src/AltirraSDL/source/app/main_sdl3.cpp` calls `ATUIDebuggerInit()` after
  `ATInitDebugger()`.
- `src/AltirraSDL/source/ui/debugger/ui_debugger.cpp` registers an SDL
  equivalent of the native debugger script auto-load confirmation callback.
- `src/AltirraSDL/stubs/win32_stubs.cpp` implements
  `ATUIShowGenericDialogAutoCenter()` as the SDL shim for the native generic
  dialog API.
- `src/AltirraSDL/source/ui/menus/ui_menus_debug.cpp` no longer disables the
  already implemented Targets and Debug Display panes.
- `src/AltirraSDL/source/commands_sdl3.cpp` registers command-manager entries
  for the additional Memory, Watch, Breakpoints, Targets, Debug Display,
  debugger toggle, source list, verifier, and trace viewer commands.
- `src/AltirraSDL/stubs/console_stubs.cpp` now reports the focused ImGui
  debugger pane ID through `ATUIGetActivePaneId()`.
- `src/AltirraSDL/source/ui/debugger/ui_debugger.h` and the source/disassembly
  panes now implement a pane-command hook for active-pane debugger commands.
  `Debug.ToggleBreakpoint` is routed through the focused pane instead of
  toggling `GetFramePC()` unconditionally.
- `Debug.Run` is separated from `Debug.RunStop`, and source panes can handle
  `kATUIPaneCommandId_DebugRun` as a source-mode run, matching the native
  command split more closely.
- The debugger Display pane now renders through the active display backend's
  `RenderFrameClipped()` path using an ImGui draw callback, instead of drawing
  `GetImGuiTextureID()` directly. Its pane-local fit calculation follows the
  same stretch-mode and pixel-aspect rules as the main display, applies global
  zoom/pan destination math, and clips output to the pane viewport.
- Debugger option/mode command IDs for break-at-EXE-run, auto-load toggles,
  DebugLink, symbol-load modes, and script auto-load modes are now registered
  against existing portable debugger/simulator state.
- Source-pane "Show Next Statement" fallback and "Go to Disassembly" now route
  through `ATUIDebuggerSetDisassemblyPosition()` instead of calling
  `SetFramePC()`. `Set Next Statement` remains the context action that mutates
  PC, matching native behavior.
- Debug Display now exposes DL history/history-start mode selection, Auto PF
  reset, filter mode entries, 16-bit address validation through
  `ResolveSymbol()`, DL mode switching on explicit DL address entry, and GL UV
  correction for its generated texture.
- Source-window lookup now uses `VDFileIsPathEqual()` and native-style
  filename-only fallback matching instead of exact `wcscmp()` only.
- Source-window lookup now also checks `GetPathAlias()`, and
  `ATOpenSourceWindow(const ATDebuggerSourceFileInfo&, bool)` preserves the
  native symbol-path alias when a source file is resolved through the module
  directory or an already-open source window directory.
- `ATImGuiConsoleShowSource()` now uses exact `LookupLine(addr, false, ...)`
  semantics like native `ATConsoleShowSource()`, instead of opening source for a
  nearest/previous line.
- SDL source panes now parse MADS/native listing rows using the same accepted
  line formats as `uidbgsource.cpp`, keep listing-derived maps separate from
  debugger symbol maps, and merge both so source line navigation and breakpoints
  can work when listing contents provide the address mapping.
- The SDL Source File List dialog now treats `EnumSourceFiles()`'s second
  argument as a line count, filters zero-line entries, sorts paths, shows the
  native empty placeholder text, and uses a centered modal popup rather than a
  saved-position tool window.
- `ATGetUIPaneAs()` and `ATUIGetActivePaneAs()` now bridge native debugger pane
  interface IDs to ImGui debugger panes for `IATUIDebuggerPane` and
  `IATUIDebuggerDisassemblyPane`. Raw `ATUIPane*` lookups remain null because
  SDL panes do not derive from native pane windows.
- `ATGetUIPaneAs(kATUIPaneId_Display, IATDisplayPane::kTypeID)` and
  `ATUIGetDisplayPane()` now return an SDL display-pane adapter. It forwards
  mouse capture, display reset, text selection/copy, frame capture/copy/save,
  paste, filter refresh, rendered-frame callbacks, and auto-suggest hooks to
  the existing SDL display/UI systems.
- Step Into, Step Over, and Step Out now delegate to the focused pane through
  `OnPaneCommand()` before falling back to debugger-core stepping, matching the
  native command-routing pattern.
- Profile View and Performance Analyzer are no longer conflated:
  `Pane.ProfileView` opens a new SDL `ui_dbg_profileview.cpp` pane using
  `kATUIPaneId_Profiler`, while `Debug.ShowTraceViewer` opens the SDL
  Performance Analyzer through the SDL-only
  `kATUIPaneId_PerformanceAnalyzerSDL`.
- `Debug.ChangeFontDialog` is registered and the Debug > Options > Change Font
  menu item now opens Configure System > Fonts, which controls the SDL
  debugger monospace font.
- SDL builds now register `IATDebugger::SetOnRequestFile()`. Native desktop
  builds return a synchronous path for debugger commands that use `?` as their
  file argument through SDL native file dialogs; WebAssembly builds report the
  unsupported synchronous request and return an empty path. Cancel/error still
  follows the native empty-path abort contract.
- The History pane now shows the native-style error dialog when "Go to Source"
  is used on an instruction address with no exact source-line mapping. The SDL
  generic dialog stubs also expose the native `ATUIShowError()` entry points
  used by shared debugger UI code.
- The History pane now exposes `IATUIDebuggerHistoryPane` through
  `ATGetUIPaneAs()`, matching native `ATHistoryWindow::AsInterface()`, and the
  SDL beam-position jump path now preserves the native `console.cpp` behavior:
  return if the History pane interface is absent, otherwise activate History
  only when an existing pane cannot jump.
- Re-review found that native `uidbghelp.cpp` was not covered in the initial
  inventory. SDL now implements `ATUIExportDebugHelp()`, registers
  `Help.ExportDebuggerHelp`, and enables the Help > Export Debugger Help menu
  item in the ImGui and macOS menu paths.
- Re-review of native `menu_default.txt` found the SDL Debug > Options menu
  order had drifted. The ImGui and macOS menu paths now match the native order:
  Auto-Reload ROMs, Randomize Memory On EXE Load, Break at EXE Run Address,
  separator, Change Font.
- Re-review of native `cmddebug.cpp` found command-manager predicate drift.
  SDL now gives `Debug.Run`, `Debug.Break`, Step Into/Over/Out,
  New Breakpoint, and Toggle Breakpoint native-equivalent command predicates,
  and keeps Open Source File / Source File List reachable while the debugger is
  closed by opening the SDL debugger dockspace first. The macOS native menu path
  was also brought back in sync for Targets and Debug Display activation.

Verification: `cmake --build --preset linux-release -j$(nproc)` completed
successfully after these changes.

Known caveat: real WebAssembly debugger file selection still needs a separate
async/VFS-aware command flow because the native debugger request-file callback
is synchronous.

## Executive Summary

The AltirraSDL debugger is substantial and many panes are implemented, but it is
not yet equivalent to the Windows debugger in several behavior-critical areas.
The most visible originally reported issue was the debugger Display pane. That
pane now uses the backend render path instead of drawing the raw emulator
texture directly, which restores the normal filter/effects/orientation pipeline.
It also applies the same zoom/pan destination math as the main SDL display and
clips backend output to the docked pane viewport. Remaining Display-pane work is
around native pane semantics and verification: visual parity tests, plus
enhanced-text replacement, whose native backing system is still absent in SDL.

The next major class of issues is integration. Native Altirra uses pane
interfaces, active-pane command dispatch, registered pane types, debugger
callbacks, a broad command vocabulary, and saved normal/debugger layouts. SDL
now covers much of the command, callback, active-pane, and fixed-pane activation
surface through an SDL ImGui pane factory registry, but still lacks separate
normal/debugger layout save/restore and raw native `ATUIPane*` adaptor support.

The source/disassembly path is closer to native after the command-dispatch,
navigation, exact line lookup, source-list, listing parsing, source-search, and
native-desktop missing-file browse prompt and file-watcher reload fixes. SDL
source panes still lack WebAssembly/builtin-dialog handling for missing source
prompts and some pane-specific command parity that needs comparison.

The History pane comparison against `uidbghistory.cpp` and `uihistoryview.cpp`
shows that its core tree update, selection, jump-to-cycle, keyboard navigation,
typed pane interface, beam-position ping routing, and context-menu timestamp
behavior now follow the native model closely. Concrete mismatches were fixed:
"Go to Source" now reports the native missing-source error instead of silently
doing nothing when no exact source line exists, `Ctrl+F` focuses the search
field, PageUp/PageDown use the visible page size, End avoids selecting a hidden
filtered line, and `IATUIDebuggerHistoryPane` plus the beam-position ping path
now use the same interface lookup contract as native. Re-review also found an
SDL-specific focus bug: the keyboard handler queried the current ImGui window
after rendering the child scroll area, so a focused History pane could miss
shortcuts. It now uses pane-level focus, and runtime coverage verifies
`Ctrl+F` search activation plus filtered End/PageUp selection.

The Disassembly pane comparison against `uidbgdisasm.cpp` found concrete
behavior gaps and aligned the SDL implementation: printable SDL text input
while Disassembly is focused now follows the native `WM_CHAR` behavior by
focusing the Console and seeding its input line, "Go to Source" now reports the
native missing-source error instead of silently ignoring a failed source lookup,
and rebuild/run-stop cycles now select the same frame-PC/PC/focus line that the
native text editor cursor would select. Build coverage passes, and test-mode
coverage now verifies printable forwarding and selected-line breakpoint
toggling through the pane command path.

The Memory pane comparison against `uidbgmemory.cpp` found a concrete edit
semantics mismatch and fixed it: SDL `CancelEdit()` now cancels only the
in-progress value, preserving the selected/highlighted address like native
`ATMemoryWindow::CancelEdit()`.

## Severity Key

- Critical: directly explains broken core debugger behavior or corrupted user
  view/state.
- High: missing native behavior that affects normal debugger workflows.
- Medium: important parity gap, edge case, or incomplete pane behavior.
- Low: mostly visual or polish unless combined with another issue.

## Findings

### 1. High: Debugger Display pane display pipeline parity is incomplete

Status: partially fixed.

Native Windows Altirra treats Display as a real debugger pane backed by the
normal video display path. The pane is registered by the native UI pane system
and participates in the debugger layout.

Original AltirraSDL behavior differed:

- `src/AltirraSDL/source/app/main_sdl3.cpp` uploads the emulator frame and
  applies filter/screen effects, but skips the backend `RenderFrame()` call when
  the debugger is open.
- `src/AltirraSDL/source/ui/debugger/ui_debugger.cpp` renders the Display pane
  by calling `ATUIGetDisplayBackend()->GetImGuiTextureID()` and drawing that
  texture with `ImGui::Image()`.
- `src/AltirraSDL/source/display/display_backend.h` documents `RenderFrame()` as
  the path that renders the active effects to a destination rectangle.
- `src/AltirraSDL/source/display/display_backend_gl33.cpp` and
  `src/AltirraSDL/source/display/shaders_common.inl` use the GL shader path to
  handle fullscreen triangle UV orientation. The raw uploaded texture is not the
  same as the final rendered output.

Current code state:

- `RenderDisplayPane()` now reserves an ImGui item for pane input and submits an
  ImGui draw callback that calls `IDisplayBackend::RenderFrameClipped()` for
  the pane rectangle and visible viewport.
- `ImDrawCallback_ResetRenderState` is submitted after the backend render so
  the OpenGL/SDL ImGui backend restores its own render state before drawing
  overlays and later widgets.
- The pane-local fit calculation now follows the same stretch-mode and
  pixel-aspect rules as `ComputeDisplayRect()` for the main display.
- The pane-local destination rectangle now applies the same global display
  zoom, pan, and pixel-snapping math as the normal SDL display path.
- A typed `IATDisplayPane` adapter is now exposed through
  `ATGetUIPaneAs(kATUIPaneId_Display, IATDisplayPane::kTypeID)` and
  `ATUIGetDisplayPane()`. It implements SDL-equivalent behavior for mouse
  capture/release, display reset, text select/deselect/copy, frame
  copy/save/capture, paste, filter refresh, rendered-frame callbacks from the
  current emulator frame, and auto-suggest control.
- The SDL adapter now stores `IATVideoWriter` through `SetVideoWriter()`, and
  SDL video recording start/stop calls mirror native `uifrontend.cpp` by
  setting/clearing the display-pane writer. The debugger Display pane draws the
  same motion-vector debug overlay when
  `recording.video.show_motion_vectors` is enabled and the writer returns
  `ATVideoRecordingDebugInfo`.

Remaining impact:

- The raw-texture/effects/orientation bypass is fixed for the exercised OpenGL
  path, but this still needs a visual regression test with asymmetric content.
- Global display zoom/pan destination positioning is implemented, and backend
  output is clipped to the docked pane viewport while preserving the full
  destination-to-source mapping.
- Native-style `IATVideoWriter` attachment is now implemented for the reviewed
  display-window use: encoder motion-vector debug visualization. Enhanced-text
  replacement is still not implemented in the SDL display path.

Recommended fix:

- Add visual regression coverage for zoomed/panned display clipping inside the
  docked pane.
- Complete enhanced-text replacement when SDL has an equivalent enhanced-text
  display pipeline.
- Add a regression test with an asymmetric image so vertical flips and
  crop/stretch mistakes are immediately visible.

### 2. Medium: Debugger file-request callback is native-desktop only

Status: fixed for native SDL desktop builds; still absent for WebAssembly.

Native `ATInitUIPanes()` in `src/Altirra/source/console.cpp` does more than
register panes. It also connects debugger callbacks:

- `ATDebugger::SetScriptAutoLoadConfirmFn(ATConsoleConfirmScriptLoad)`
- `ATDebugger::SetOnRequestFile(ATConsoleRequestFile)`

Original SDL state:

- `ATUIDebuggerInit()` in `src/AltirraSDL/source/ui/debugger/ui_debugger.cpp`
  is empty.
- No SDL equivalent registers the script auto-load confirmation callback.
- No SDL equivalent registers the debugger file request callback.

Current code state:

- `ATUIDebuggerInit()` is called after `ATInitDebugger()`.
- SDL registers a script auto-load confirmation callback using
  `ATUIShowGenericDialogAutoCenter()`.
- SDL builds register `SetOnRequestFile(ATConsoleRequestFileSDL)`.
  `ATConsoleRequestFileSDL()` calls `SDL_ShowOpenFileDialog()` or
  `SDL_ShowSaveFileDialog()`, keeps the callback state alive until completion,
  pumps SDL events while waiting, and writes the selected UTF-8 path back to
  `ATDebuggerRequestFileEvent::mPath` as UTF-16 on native desktop builds.
- `src/Altirra/source/debugger.cpp` uses this callback from
  `ATDebugger::RequestFile()` when a debugger command receives `?` for a file
  path. If the callback returns an empty path, the native parser treats it as a
  user abort; SDL now follows that same empty-path contract for cancel/error.
- WebAssembly builds also register the callback, but without a synchronous
  browser picker it logs the unsupported synchronous file request and returns an
  empty path, preserving the native abort contract instead of leaving the
  debugger without a request callback.
- A deterministic test seam now invokes the same callback with an override path
  without opening an OS dialog. Test-mode command
  `debugger_request_file <open|save> [utf8_path]` verifies open/save/empty
  paths and UTF-8 path round trips.

Remaining impact:

- Native desktop debugger commands that request a file through `?` now have a
  path.
- WebAssembly debugger commands that request a file through `?` still return an
  empty path unless a synchronous test override is active; this is explicitly
  handled and documented in code because the current browser picker path is
  asynchronous and VFS-specific.
- The native-desktop OS dialog itself still needs manual validation, but the
  callback's open/save/cancel path contract and UTF-8 conversion are covered by
  the non-OS test seam.

Recommended fix:

- If WebAssembly debugger `?` prompts need real user file selection, add a
  separate async/VFS-aware command flow; the native synchronous debugger
  callback cannot block on a browser picker.
- Keep the non-OS test seam in smoke coverage for open, save, cancel, and UTF-8
  path conversion.

### 3. Medium: Fixed pane activation now uses an SDL registry

Native Altirra registers pane classes and creates panes through the UI pane
system. SDL has partial equivalents:

- `ATRegisterUIPaneType()`
- `ATRegisterUIPaneClass()`
- `ATActivateUIPane()`

Fixed for SDL ImGui panes: `src/AltirraSDL/source/ui/debugger/ui_debugger.cpp`
now has an SDL ImGui pane factory registry, registers fixed debugger panes in
`ATUIDebuggerInit()`, and makes `ATActivateUIPane()`/default debugger layout
creation use that registry. Native `ATRegisterUIPaneType()` and
`ATRegisterUIPaneClass()` storage remains for compatibility and diagnostics,
but native creators return `ATUIPane*` and cannot directly instantiate ImGui
panes.

Impact:

- New fixed SDL debugger panes can now be added through the SDL ImGui registry
  without editing a central activation switch.
- If shared/native code registers a pane ID but SDL has no ImGui factory,
  activation now logs that the native pane is registered but lacks an SDL
  factory.
- Full native extensibility is still not possible for raw `ATUIPane*` creators
  because ImGui panes are not native pane objects.

Remaining fix:

- Register indexed Memory/Watch/Source pane families as explicit SDL
  class-style factories if the current centralized indexed dispatch becomes a
  source of drift.
- Implement raw native `ATUIPane*` adaptor support only if remaining shared
  code requires it.

### 4. Medium: Implemented pane command paths are mostly wired

Status: mostly fixed for the implemented debugger surface.

Verified current state:

- `src/AltirraSDL/source/ui/menus/ui_menus_debug.cpp` enables Targets and Debug
  Display menu entries.
- `src/AltirraSDL/source/commands_sdl3.cpp` registers command paths for Memory
  2-4, Watch 1-4, Breakpoints, Targets, Debug Display, debugger toggle, source
  list, verifier, trace viewer, Change Font, DebugLink, symbol-load modes,
  script auto-load modes, and debugger auto-load option commands where they map
  to implemented SDL functionality.
- Source-backed command inventory check: all 28 native `Debug.*` command IDs
  registered in `src/Altirra/source/cmddebug.cpp` are present in
  `src/AltirraSDL/source/commands_sdl3.cpp`, and the 19 native debugger
  `Pane.*` IDs exposed through `src/Altirra/res/menu_default.txt` are also
  present in SDL's command/menu implementation.
- Re-review against native `cmddebug.cpp` found that several registered SDL
  commands were executable through accelerators/test mode even when the native
  command table would reject them through `mpTestFn`. Fixed command predicates:
  `Debug.Run` and Step Into/Over/Out require the simulator to be not running,
  `Debug.Break` requires running or queued debugger commands, and New/Toggle
  Breakpoint require the debugger to be open. `Debug.OpenSourceFile` and
  `Debug.OpenSourceFileList` now match the native no-predicate command
  reachability; because SDL source panes render inside the debugger dockspace,
  these commands open the debugger first when it is closed.
- Test-mode `query_command` coverage now verifies the reviewed native-equivalent
  debugger command matrix across running/closed, paused/closed, paused/open, and
  running/open states. The covered commands are `Debug.Run`, `Debug.Break`,
  Step Into/Over/Out, New Breakpoint, Toggle Breakpoint, Open Source File, and
  Source File List. The observed matrix matches native predicates: Run and step
  commands are enabled only while stopped, Break is enabled only while running,
  New/Toggle Breakpoint are enabled only when the debugger is open, and both
  source-file commands stay enabled regardless of debugger visibility/running
  state.
- Debug > Options ordering in both SDL menu implementations now follows native
  `menu_default.txt`.
- `Pane.PrinterOutput` is now reachable while the debugger is closed. Because
  SDL currently renders Printer Output as an ImGui debugger pane, the command
  opens the debugger dockspace first and then activates the pane.
- `Debug.ChangeFontDialog` routes to Configure System > Fonts, where the SDL
  Monospace Font setting controls debugger panes.

Intentional command-path difference:

One registered command also has different enable semantics: Windows registers
`Pane.Display` without the debugger-enabled predicate, while SDL registers
`Pane.Display` with `TestDebuggerOpen`. This is intentional in SDL because
`CmdPaneDisplay()` focuses the ImGui debugger Display window, and that window is
only submitted while the debugger dockspace is open.

Impact:

- `Pane.Display` has a documented SDL-specific enable predicate.
- `Pane.PrinterOutput` differs from native Windows because it opens the debugger
  dockspace when invoked while the debugger is closed; this preserves
  reachability until SDL has native-style non-debugger pane windows.
- `Debug.OpenSourceFile` and `Debug.OpenSourceFileList` similarly open the SDL
  debugger dockspace first when it is closed, preserving the native command
  reachability while using SDL's docked source-pane model.
- Menu-path coverage now covers every Debug > Window pane entry through real
  ImGui clicks: Console, Registers, Disassembly, Call Stack, History,
  Memory 1-4, Watch 1-4, Breakpoints, Targets, and Debug Display. Debug >
  Profile > Profile View is also covered.

Recommended fix:

- Keep expanding UI menu-path tests for debugger-related menu entries outside
  Debug > Window/Profile. Current coverage includes command-ID smoke for
  debugger open, fixed panes Breakpoints/Targets/Debug Display/Profile View/
  Printer Output, indexed Memory 4/Watch 4, real Debug > Window clicks for all
  pane entries, and Debug > Profile > Profile View.
- Keep the command-manager predicate matrix in test-mode coverage; it now covers
  running/closed, paused/closed, paused/open, and running/open states for the
  reviewed debugger commands.
- Keep the `Pane.Display` debugger gate documented as an SDL-specific command
  behavior.

### 5. Medium: Active-pane debugger command dispatch has focused-pane coverage

Status: fixed for native-equivalent pane command overrides identified in code.
SDL now exposes `IATUIDebuggerPane` through
`ATGetUIPaneAs()` / `ATUIGetActivePaneAs()` for ImGui debugger panes and routes
Run, Toggle Breakpoint, Step Into, Step Over, and Step Out through the focused
pane before falling back to debugger-core behavior.

Native commands such as Toggle Breakpoint, Step Into, Step Over, and Step Out
first offer the command to the active debugger pane through
`IATUIDebuggerPane::OnPaneCommand()`. Verified native overrides are Source and
Disassembly: `ATSourceWindow::OnPaneCommand()` handles Run, Toggle Breakpoint,
and source-mode stepping, while `ATDisassemblyWindow::OnPaneCommand()` handles
Toggle Breakpoint. The base `ATUIDebuggerPane::OnPaneCommand()` returns false,
and native Memory does not override it.

Original SDL gaps were:

- `ATUIDebuggerToggleBreakpoint()` toggles at `dbg->GetFramePC()`.
- The Disassembly pane has a local F9 handler for its selected line.
- Menu/command activation does not generally dispatch to the focused pane's
  implementation the way native Altirra does.
- `src/AltirraSDL/stubs/console_stubs.cpp` had active-pane lookup functions
  returning null/0, so shared command handlers could not find the focused pane.

Current code state:

- `ATUIDebuggerToggleBreakpoint()`, Step Into, Step Over, Step Out, and Run
  dispatch to the focused pane's `OnPaneCommand()` before using debugger-core
  fallback behavior.
- Source and disassembly panes implement the relevant command handling. Source
  Run, Step Into, Step Over, Step Out, and Toggle Breakpoint were rechecked
  against native `ATSourceWindow::OnPaneCommand()`; SDL uses source-mode Run,
  the same source-line step-range narrowing from pane/symbol maps, source-mode
  Step Out, and selected-line breakpoint toggling.
- `ATUIGetActivePaneId()` returns the focused ImGui pane ID.
- `ATUIGetActivePaneAs()` and `ATGetUIPaneAs()` return exposed ImGui debugger
  pane interfaces. Raw `ATUIGetActivePane()` / `ATGetUIPane()` still return
  null because ImGui panes are not native `ATUIPane` instances.

Impact:

- Current source/disassembly command behavior matches the reviewed native
  pane-command paths; focused runtime tests are still needed.
- Memory-pane edit/navigation behavior is still a validation area, but not an
  active-pane `OnPaneCommand()` parity gap in the native implementation.
- Remaining raw-pane risk is in any shared code that requires raw `ATUIPane*`.

Recommended fix:

- Implement raw `ATUIPane*` adapters only if a remaining shared code path truly
  requires them; otherwise keep the interface bridge documented.

### 6. Low/Medium: Source navigation mutation bug is fixed; tests remain

Status: fixed for the identified source context-menu paths. "Show Next
Statement" fallback and "Go to Disassembly" now call
`ATUIDebuggerSetDisassemblyPosition()` and do not mutate frame PC. "Set Next
Statement" still calls `SetPC()`, matching native behavior.

Native Source windows are navigation views. Commands like "Go to Disassembly"
move the disassembly pane to an address; they do not alter the debugger frame PC.

Original SDL source behavior differed:

- `src/AltirraSDL/source/ui/debugger/ui_dbg_source.cpp` uses
  `dbg->SetFramePC(...)` for some navigation paths, including "Go to
  Disassembly" and fallback "Show Next Statement" behavior.
- Native `src/Altirra/source/uidbgsource.cpp` activates the disassembly pane and
  calls the disassembly pane's `SetPosition()` instead.

Original impact:

- A pure navigation action can change the debugger's selected frame PC.
- Follow-up commands can operate on the wrong frame/location.
- This can produce confusing debugger state when browsing source around a paused
  program.

Remaining validation:

- Add focused UI tests for "Show Next Statement", "Go to Disassembly", and
  "Set Next Statement" to lock the fixed command split in place.

### 7. Medium: Source pane native resolution behavior is mostly implemented

Native source panes support more than simple symbol-to-line mapping:

- Listing file parsing for MADS/native listing formats.
- Address-to-line mapping built from listing contents when symbol lookup is not
  enough.
- Path matching through `VDFileIsPathEqual()` and loose basename matching.
- File watcher reload.
- Native "open externally" and "show in folder" integrations.

SDL currently:

- Loads source text, symbol line mappings, and MADS/native listing-derived
  address mappings. Listing-derived maps are retained separately from symbol
  maps and merged after symbol rebinding, matching the native structure.
- Source PC/frame highlighting now uses the native nearest-previous address
  rule from `ATSourceWindow::GetLineForAddress()`: an address maps to the
  previous source/listing row when it is within 64 bytes, instead of requiring
  an exact address hit.
- Listing-only source panes now toggle raw address breakpoints like native
  `ATSourceWindow::ToggleBreakpoint()` when no symbol module path is bound;
  symbol-bound source panes still toggle source breakpoints by file/line.
- Uses `VDFileIsPathEqual()`, path aliases, and filename-only fallback in
  source-window lookup.
- `ATConsoleShowSource()` and `ATImGuiConsoleShowSource()` now both use exact
  `LookupLine(addr, false, ...)` semantics.
- `ATOpenSourceWindow(const ATDebuggerSourceFileInfo&, bool)` now searches
  relative to the module path and already-open source window directories, and
  records the original symbol path as an alias when it resolves a different
  actual file path.
- Native desktop builds now prompt the user to locate a missing source file
  when automatic search fails. The selected path is opened with the original
  symbol path preserved as an alias, and cancel returns no pane, matching the
  native `ATOpenSourceWindow()` contract.
- WebAssembly builds now explicitly report that synchronous missing-source
  browse prompts are unsupported and return no pane, preserving the native
  cancel/no-pane contract instead of silently compiling out the prompt path.
- A deterministic test seam now invokes the same missing-source resolver with a
  prompt override. Test-mode command
  `source_missing_file <symbol_path> [resolved_path]` covers cancel/no-pane,
  selected-file opening, original-symbol-path alias preservation, and UTF-8
  alias round trips without driving an OS dialog.
- Source panes now use the portable `VDFileWatcher`, polled through a per-frame
  debugger pane hook, to reload file contents and rebuild source mappings after
  disk edits. This also polls hidden source panes while the debugger is open.
- Test-mode commands `source_open_file <utf8_path>` and
  `source_query_file <utf8_path>` now expose a non-OS regression path for live
  source-pane contents. Runtime verification opened a two-line source file,
  edited it externally to three lines, and confirmed the same pane reloaded to
  the new first/last lines after the watcher polled.
- Test-mode command `source_reload <selected_line|-1> <utf8_path>` now
  deterministically invokes the same `OnFileUpdated()` / watcher reload path
  after an external disk edit. Runtime verification opened a two-line source
  file, edited it externally to three lines, selected line 1, triggered reload,
  and observed line count changing 2 -> 3, last line changing from `two` to
  `three reloaded`, and selected line 1 preserved.
- Test-mode command
  `source_query_mapping <hex_addr> <line_index> <utf8_path>` covers
  listing-only address mappings. Runtime verification opened a MADS-style
  listing with no symbol table, confirmed exact address `$2000` mapped to line
  1, nearest address `$2001` also mapped to line 1 by the native 64-byte rule,
  line 2 mapped back to `$2002`, and an out-of-range address returned no line.
- Source stepping now shares the same `ComputeSourceStepRange()` helper between
  the actual Step Into/Over command path and test-mode queries. Test-mode
  command `source_query_step_range <hex_pc> <utf8_path>` covers the native
  listing-derived range algorithm without executing the emulator step.
  Runtime verification confirmed `$2000` produces range `$2001` length 1,
  `$2001` produces no range because `pc + 1` is already at the next listing
  row, `$2002` produces range `$2003` length 66, `$2045` produces no range on
  the final row, and a `$3000` to `$3100` listing gap produces no range because
  it fails the native `< 100` byte cutoff.
- Source-pane Toggle Breakpoint now has focused runtime coverage through
  `source_toggle_breakpoint <line_index> <utf8_path>`, which opens/focuses a
  source pane, selects the requested line, and invokes the same
  `OnPaneCommand(kATUIPaneCommandId_DebugToggleBreakpoint)` path used by
  menu/shortcut dispatch. Runtime verification used a small MADS listing
  fixture, confirmed line 1 maps to `$2000`, toggled once from breakpoint count
  0 to 1, then toggled again from 1 back to 0.
- Source-pane Run, Step Into, Step Over, and Step Out now have focused runtime
  coverage through `source_command` with command, PC override, and source path
  arguments. It opens/focuses a source pane and invokes the same
  `OnPaneCommand()` implementation used by active-pane menu/shortcut dispatch.
  Runtime verification used a small MADS listing fixture at PC `$0000`:
  Step Into and Step Over both returned `handled:true`, used range `$0001`
  length 1, started execution, and were stopped by test cleanup; Step Out and
  Run also returned `handled:true`, started execution, and cleaned back to a
  stopped debugger state.
- The Source File List dialog now matches the native filtering/sorting contract
  for reviewed behavior: it treats the second `EnumSourceFiles()` argument as a
  line count, filters out entries with zero lines, sorts paths, shows the native
  empty placeholder, and opens as a centered modal popup.

Remaining impact:

- Real WebAssembly/forced built-in dialog source opens still need an async
  picker flow if user-driven missing-source browse is required.
- Focused source-mode Run, Step Into, Step Over, and Step Out now have
  listing-derived command-path coverage; symbol-derived mapping fixtures still
  need the same execution coverage.

Recommended fix:

- Add WebAssembly/forced built-in dialog async picker support for
  missing-source browse prompts if user-driven source lookup is required there.
- Keep the `source_missing_file` regression path in test-mode coverage for
  cancel, selected-file, alias, and UTF-8 path behavior.
- Keep the `source_open_file`/`source_query_file` regression path in test-mode
  coverage for file-watcher reload after disk edits.
- Keep the `source_query_mapping` regression path in test-mode coverage for
  listing-only exact, nearest-previous, reverse line-to-address, and
  out-of-range mappings.
- Keep the `source_query_step_range` regression path in test-mode coverage for
  listing-derived source-step range boundaries and the native `< 100` byte
  cutoff.
- Keep the `source_command` regression path in test-mode coverage for
  listing-derived source-mode Run, Step Into, Step Over, and Step Out execution.
- Add matching focused source-mode command execution coverage for symbol-derived
  mappings.

### 8. Medium: Debug Display pane still lacks display-stack parity

Status: partially fixed. The pane is reachable from the menu and command table.
SDL now exposes DL history/history-start mode, Auto PF reset, palette mode,
filter mode entries, 16-bit address validation, DL mode switching on manual DL
address entry, GL UV correction for its generated texture, a real bicubic
resampling path for the Bicubic filter menu item, and native-style refresh on
all debugger system-state updates. Remaining gaps are primarily display-stack
parity: the ImGui texture path does not provide the native child display
window.

Native Debug Display is an ANTIC/GTIA visualization pane, separate from the main
Display pane. It supports:

- Display list address modes including automatic history-based modes.
- Playfield address override/auto mode.
- Force Update.
- Palette modes.
- Filter modes: point, bilinear, bicubic.
- A display child window using the native display stack.
- Default bilinear filtering through `mpDisplay->SetFilterMode(kFilterBilinear)`.
- 16-bit address validation for DL/PF overrides; invalid addresses beep instead
  of being applied.
- DL address entry switches the debug display mode to
  `kMode_AnticHistoryStart` before applying the override.

Original SDL Debug Display gaps were, with current status:

- Fixed: the Debug menu now enables it.
- Fixed: the command table now registers `Pane.DebugDisplay`.
- Fixed: DL history/history-start controls, Auto PF reset, filter menu entries,
  16-bit address validation, and DL mode switching on manual DL address entry
  are implemented.
- Fixed for OpenGL orientation: the generated texture uses corrected UVs.
- Fixed: Bicubic mode now uses a dedicated bicubic-resampled texture instead of
  falling through to linear texture sampling.
- Fixed: debugger system-state updates now mark the pane dirty regardless of
  whether the simulator is running, matching native `OnDebuggerSystemStateUpdate`
  deferring an update unconditionally.
- Fixed/tested: Debug Display now has focused runtime coverage through
  `debug_display`. Test-mode verification covered the native default state
  `mode=0,palette=0,filter=2,dl=-1,pf=-1`; valid DL entry `0x2000` switching
  to history-start mode and setting `dl=8192`; valid PF entry `0x2400` setting
  `pf=9216`; invalid DL/PF `0x10000` returning `applied:false` and preserving
  state; filter `3` selecting Bicubic; palette `1` selecting Analysis; and
  invalid filter/palette inputs preserving state. This covers the reviewed
  native state contract for menu/filter/palette and 16-bit address validation.
- Remaining: it uses an ImGui texture conversion path rather than the native
  display child/display stack.

Recommended fix:

- Continue replacing the remaining native child-display stack behavior or
  explicitly document any SDL-specific replacement.

### 9. Low/Medium: Debugger layout lifecycle is partially matched

Native debugger open/close behavior in `console.cpp`:

- Saves current non-debugger pane layout.
- Enables debugger mode.
- Restores the saved debugger layout or loads the native default debugger layout.
- On close, saves debugger layout and restores the normal layout.
- Focuses Display after closing the debugger.

SDL behavior:

Current SDL behavior:

- Opens debugger into an ImGui dockspace.
- Flushes ImGui layout settings immediately before entering debugger mode and
  before leaving debugger mode, matching native's save-on-transition behavior.
- Restores saved debugger docking through ImGui's normal `.ini` persistence:
  `ApplyDefaultDockLayout()` only builds the default layout when the debugger
  dockspace has no saved children.
- Closes by removing debugger clients, disabling the debugger, and clearing the
  focused debugger pane.
- Default SDL layout opens extra panes compared to native default.

Impact:

- Users can rely on the debugger dock layout being flushed on debugger
  transitions instead of waiting for ImGui's delayed autosave.
- Exact Windows `ATSavePaneLayout("Standard"/"Debugger")` parity is still not
  present because SDL uses one ImGui settings file and ImGui pane IDs instead
  of native `ATUIPane` frame trees.
- Extra default panes may be useful, but they should be treated as an intentional
  SDL addition.

Recommended fix:

- Add focused runtime coverage for debugger close/reopen layout persistence.
- Implement a native-style named layout serializer only if ImGui's built-in
  per-window/per-dockspace persistence proves insufficient.
- Keep useful SDL default additions only if they are listed as intentional.

### 10. Medium: Profile View exists; full visual parity remains incomplete

Status: partially fixed after implementation pass.

Native menu entries distinguish:

- Profile / Profile View: `Pane.ProfileView` activates `kATUIPaneId_Profiler`,
  registered by `ATInitProfilerUI()` in `profilerui.cpp` as native
  `ATUIProfilerPane`.
- Performance Analyzer: `Debug.ShowTraceViewer` calls `ATUIOpenTraceViewer()`
  in `cmddebug.cpp`, opening the trace viewer from `uitraceviewer.cpp`.

Original finding: SDL used `kATUIPaneId_Profiler` for
`ATImGuiTraceViewerPane` in `ui_dbg_traceviewer.cpp`, titled "Performance
Analyzer", so both "Profile View" and "Performance Analyzer..." activated that
same pane ID.

Current code state:

- `src/AltirraSDL/source/ui/debugger/ui_dbg_profileview.cpp` implements an SDL
  Profile View pane under `kATUIPaneId_Profiler`.
- `src/AltirraSDL/source/ui/debugger/ui_dbg_traceviewer.cpp` now uses
  `kATUIPaneId_PerformanceAnalyzerSDL` for the SDL Performance Analyzer.
- `src/AltirraSDL/source/commands_sdl3.cpp` maps `Pane.ProfileView` to
  `kATUIPaneId_Profiler` and `Debug.ShowTraceViewer` to
  `kATUIPaneId_PerformanceAnalyzerSDL`.
- SDL Profile View now has a native-style frame timeline: it draws per-frame
  cycle-height bars from `ATProfileSession::mpFrames`, uses the same trimmed
  mean vertical range calculation as native `ATUIProfilerPane::LoadProfile()`,
  auto-selects an initial pixels-per-frame zoom like
  `ATUIProfilerTimelineView::SetSession()`, supports hover highlighting,
  mouse-drag frame-range selection wired to `MergeFrames()`, wheel and `+`/`-`
  zoom, and a horizontal scroll control.
- Call-graph mode now follows native `ATUIProfileView::RemakeView()` more
  closely: SDL renders a tree instead of the flat profile table, uses the same
  four root contexts, builds parent/child relationships from
  `ATProfileSession::mContexts`, filters zero-inclusive-instruction child
  contexts, sorts roots and children by inclusive cycles, and labels nodes with
  calls plus inclusive cycle/CPU-cycle/instruction percentages.
- SDL Profile View now has the first native-equivalent source drill-down path:
  double-clicking a flat profile row or call-graph tree node opens a separate
  "Altirra Profiler - Detailed View" ImGui window. The window keeps a collapsed
  address-sorted copy of the current profile records, generates disassembly
  lines around the selected address using the same `DebugGlobalReadByte()`,
  `ATGetOpcodeLength()`, `ATDisassembleCaptureInsnContext()`, and
  `ATDisassembleInsn()` path as native `ATUIProfilerSourceTextView`, and shows
  native columns for Cycles, Insns, CPI, CCPI, DMA%, and Text.
- SDL source-detail navigation now covers the native scroll/header behavior:
  the detailed view has a hexadecimal address field, Up/Down/Page Up/Page Down
  controls, mouse-wheel and keyboard scrolling over the detail table, dynamic
  visible-line calculation from the window height, and resizable metric/text
  columns through ImGui table headers.
- SDL flat profile rows now track selection with stable record IDs, including
  single-select, Ctrl-toggle, and Shift-range selection across the current
  sorted order. The pane displays the native status text computed by
  `ATUIProfileView::OnItemSelectionChanged()`: selected count, cycles and cycle
  percentage, instructions and instruction percentage.

Remaining impact:

- The command and pane-ID meanings now match Windows.
- The SDL Profile View uses the real `ATCPUProfiler`, `ATProfileSession`, frame
  merging, counter options, and frame-trigger options, but it is not a full port
  of native `ATUIProfileView` and `ATUIProfilerTimelineView`.
- Native's graphical timeline core is now represented in SDL, while the numeric
  frame-range controls remain as an SDL precision addition.
- Native call-graph tree presentation and source-detail navigation are now
  represented in SDL. Remaining Profile View work is focused runtime coverage
  for timeline, call-graph, profile selection, and source-detail interactions.

Recommended fix:

- Continue filling in native Profile View parity with focused timeline /
  call-graph / profile-selection / source-detail interaction tests.
- Keep the SDL Performance Analyzer as an intentional SDL addition, separately
  routed from `Pane.ProfileView`.

### 11. Medium: Watch pane has keyboard and update differences

Native Watch panes support list editing behavior through the native list view:

- F2 edits.
- Delete removes.
- Typing printable characters starts editing.
- Expressions update through debugger state notifications.

SDL Watch panes are functional but differ:

- Fixed: typing-to-edit now consumes printable ImGui input characters, so
  expressions can start with `$`, digits, `@`, and other ASCII punctuation
  instead of only A-Z keys.
- Fixed: watch values are marked for update on every debugger system-state
  notification, matching native `ATWatchWindow::OnDebuggerSystemStateUpdate()`
  instead of only when the debugger is not running.
- Fixed/tested: SDL Watch panes now expose the native
  `IATUIDebuggerWatchPane` interface through `ATGetUIPaneAs()` /
  `ATUIDebuggerGetPaneAs()`, matching native `ATWatchWindow::AsInterface()`.
  Test-mode command `watch_interface [expr]` verifies that Watch 1 can be
  resolved through the typed pane lookup and can add an expression through
  `IATUIDebuggerWatchPane::AddWatch()`.
- Fixed: SDL's `ATUIDebuggerAddToWatch()` helper now follows the native memory
  pane pattern: activate Watch 1, resolve `IATUIDebuggerWatchPane` through
  `ATGetUIPaneAs()`, then call `AddWatch()`. This keeps the Memory pane's Add
  to Watch route on the same typed-interface path used by native
  `uidbgmemory.cpp`.
- Fixed/tested: pane-level edit/delete semantics now have focused test-mode
  coverage. `watch_describe` verifies the initial `<blank>` row state;
  `watch_printable_edit $ D40B` verifies printable `$` starts editing and
  creates `$D40B|<blank>`; `watch_printable_edit 1 23` verifies numeric
  printable starts; `watch_edit 456` verifies selected-row F2-style editing;
  and `watch_delete_selected` verifies selected-row Delete removal and returns
  `deleted:false` when the selected row is only the trailing blank row.
- Fixed/tested: low-level mouse/table focus activation now handles Delete/F2 at
  the focused pane/table level using the selected row, matching native
  `ATWatchWindow::ListViewWndProc()` instead of relying only on per-row
  `IsItemFocused()` during row rendering. Runtime coverage added `$D40B`,
  clicked the first `WatchTable` row through a real ImGui mouse action, sent
  Delete, and verified `watch_describe` returned `<blank>`.

Impact:

- Common native watch editing workflows now cover printable expression starts,
  selected-row F2-style editing, Delete removal, mouse row selection, typed pane
  lookup, AddWatch dispatch, and native-style update timing.

Recommended fix:

- Keep the `watch_interface` regression path in test-mode coverage for typed
  pane lookup and AddWatch dispatch.
- Keep the `watch_printable_edit`, `watch_edit`, `watch_delete_selected`, and
  `watch_describe` regression paths in test-mode coverage for pane-level edit
  behavior.

### 12. Medium: Memory pane is broad but needs edge-case parity validation

SDL Memory panes are one of the more complete debugger areas. They implement
address spaces, value modes, interpretation modes, editing, context menus,
changed-byte highlighting, bitmap views, and screen tracking.

Remaining concerns:

- Fixed: Add-to-Watch expression formatting now uses the full context address
  with `%04X`, matching native's minimum-width formatting instead of masking to
  16 bits. Runtime coverage through `memory_add_to_watch_expr` verified
  `db $2000`, `db $10002000`, and `dw $20012345` through the same formatter used
  by the context-menu UI.
- Fixed: Track-on-screen overflow now shows the native "Too many watches"
  message when `AddWatch()` returns a negative result. Runtime coverage through
  `memory_track_on_screen`, `watch_fill`, and `watch_count` verified adding to
  an empty watch table at slot 0, filling all eight core watch slots, and a
  ninth Track On-Screen request returning `added:false`,
  `watch_index:-1`, and `overflow:true` while the watch count remained 8. The
  automated test does not open the blocking SDL message box; the context-menu UI
  calls the same helper with dialog display enabled.
- Fixed: `CancelEdit()` now preserves the selected/highlighted address and only
  clears the in-progress edit value, matching native
  `ATMemoryWindow::CancelEdit()` in `uidbgmemory.cpp`.
- Fixed: `BeginEdit()`, `CommitEdit()`, and `CancelEdit()` now enforce the same
  selected-address guard as native `ATMemoryWindow`, so method-level edit
  behavior cannot mutate pane state without `mbSelectionEnabled`.
- Fixed/tested: hex byte edit commit and cancel semantics now have focused
  runtime coverage through `memory_hex_edit` and `memory_hex_cancel`.
  Verification at CPU `$2000` wrote `$5A` via `BeginEdit()` / `CommitEdit()`
  and read back `$5A`; the cancel path started from `$5A`, staged `$A5`, called
  `CancelEdit()`, read back `$5A`, and preserved selected address `$2000` with
  selection enabled. The test then restored `$2000` to `$00`.
- Tested: additional value-mode and interpretation-column commit semantics now
  have focused runtime coverage through `memory_value_edit` and
  `memory_text_edit`. Verification wrote and read back hex word `$1234`,
  decimal word `4660` as `$1234`, decimal byte `200` as `$C8`, ATASCII `65` as
  `$41`, and Internal text `65` as `$21`, matching the native reverse
  ATASCII-to-internal transform. The test restored all touched CPU bytes to
  `$00`.
- Fixed/tested: `EnsureHighlightVisible()` is now address-space-wrap aware.
  Before this fix, a 64K wrapped view such as ANTIC `$1000FFF0` with two
  16-byte rows already contained `$10000000`, but the linear comparison treated
  the wrapped address as before the view and scrolled to `$10000000`. Runtime
  coverage through `memory_ensure_visible` verified that
  `view=0x1000FFF0 highlight=0x10000000 columns=16 rows=2` keeps
  `view_start:"$1000FFF0"`, while non-visible highlights still scroll to
  `$10000010` or `$10000FF0` as appropriate. CPU `$0000FFF0` to `$00010000`
  was intentionally not treated as a 64K wrap because `kATAddressSpace_CPU` is
  a 24-bit CPU view in `address.h`.
- Fixed/tested: Memory page scrolling now preserves the intra-row address offset
  when clamping near the top, matching native `ATMemoryWindow::OnViewScroll()`.
  Before this fix, SDL PageUp from view `$00000003` with 16-byte rows clamped to
  `$00000000`; native preserves the column offset and lands on `$00000003`.
  Runtime coverage through `memory_navigation` now verifies PageUp top-clamp
  offset preservation, PageUp/PageDown by `columns * rows`, Ctrl+Up/Ctrl+Down
  one-row view scrolling, Left/Right/Up/Down selected-address movement, and Tab
  toggling between value and interpretation columns.
- Fixed/tested: typed edit auto-advance now uses the same selection movement
  helper as Enter/arrow navigation instead of directly assigning
  `mHighlightedAddress`. This keeps commit-and-advance behavior tied to
  native-style selection reset and `EnsureHighlightVisible()` handling. The
  actual ATASCII/Internal keyboard path now stages `mEditValue` and calls
  `CommitEdit()` like native `WM_CHAR` handling instead of writing directly to
  the target memory.
  Focused runtime coverage through `memory_hex_auto_advance` and
  `memory_text_auto_advance` verified hex byte `$5A` advances from `$2000` to
  `$2001`, hex word `$1234` advances from `$2000` to `$2002`, ATASCII `65`
  advances from `$2000` to `$2001`, row-crossing byte edit `$200F -> $2010`
  keeps the same two-row view at `$2000`, page-crossing byte edit
  `$201F -> $2020` scrolls the two-row view to `$2010`, and an ANTIC-space edit
  at `$1000FFFF` keeps selection at `$1000FFFF` instead of advancing past the
  address-space limit. The ANTIC case is treated as selection-boundary coverage
  because the readback value is hardware/backend dependent.
- Remaining scroll/edit behavior is hand-reimplemented and should still be
  tested against native for real key-event delivery/focus behavior and the
  complete address-space boundary matrix.

Recommended validation:

- Memory 1-4 creation and persistence.
- CPU/ANTIC/VBXE/Ext RAM/RAM/ROM/Cart/PORTB addressing.
- Editing across row/page boundaries.
- Context-menu read/write access breakpoint creation.
- Keep non-CPU Add-to-Watch formatting and Track On-Screen overflow regression
  coverage in test mode.

### 13. Medium: Breakpoints pane mostly works but differs from native UI

SDL implements breakpoint list rendering, context menu operations, and an
add/edit dialog. Noted differences:

- SDL adds annotations such as one-shot/trace/clear-on-reset in the list. These
  are useful, but they are an SDL addition.
- Fixed: SDL now matches native condition validation. If the condition checkbox
  is enabled, it calls `ATDebuggerParseExpression()` even for an empty string,
  matching `ATUIDebuggerBreakpointDialog::OnDataExchange()`.
- Fixed/tested: SDL Breakpoints dialog submission now goes through a shared
  validation helper used by both the ImGui modal and test mode. Runtime
  coverage verifies the native rules that condition-only breakpoints require a
  condition and non-stopping breakpoints require a command or trace message.
- Fixed: SDL now tracks a selected list row and the Delete key deletes that
  selected breakpoint, matching native list-view selection behavior.
- Fixed: SDL now loads an existing breakpoint command into the command field
  verbatim like native Windows. The earlier SDL parser tried to split commands
  beginning with `.printf "..."` back into the trace-message UI, which could
  rewrite valid debugger commands when the dialog was reopened and saved.
- Fixed/tested: SDL now uses one shared formatter for the trace-message action
  in the breakpoint dialog and test-mode command
  `breakpoint_format_trace <trace_text>`. Runtime verification covered normal
  text, percent escaping (`%` becomes `%%`), quote escaping (`"` becomes
  `\"`), and empty trace text producing no command, matching native
  `ATUIDebuggerBreakpointDialog::OnDataExchange()`.
- Tested: `breakpoint_submit`, `breakpoint_describe`, `breakpoint_delete`, and
  `breakpoint_count` cover non-UI breakpoint creation/deletion through the same
  `ATDebuggerBreakpointInfo` path used by the dialog. Verification covered
  invalid condition-only, invalid expression, invalid non-stopping, valid
  condition-only description (`Any insn when $01`), valid command+trace
  description (`.printf "hit"; .echo_ok [trace]`), and deleting the created
  breakpoint back to a zero count.
- Tested: `breakpoint_delete_selected <id>` activates the Breakpoints pane,
  selects the row by user breakpoint ID, and calls the same
  `DeleteSelectedBreakpoint()` helper used by the pane's context-menu Delete
  and Delete-key paths. Runtime verification created IDs 0 and 1, deleted ID 0
  through the selected-row path, confirmed ID 0 was no longer describable, left
  one breakpoint remaining, then deleted ID 1 and returned the count to zero.
- Tested: the keyboard Delete event path now has focused runtime coverage.
  `breakpoint_select 0` selected user breakpoint ID 0 and focused the
  Breakpoints pane, `key delete` injected an ImGui Delete key press, ID 0 was no
  longer describable afterward, one breakpoint remained, and cleanup returned
  the count to zero.
- Tested: Breakpoint row description formatting now uses one shared helper for
  the pane and test mode. `breakpoint_format_description oneshot=1 clear=1
  trace=1` produced `$2000 [one-shot] [trace] [clear-on-reset]`, verifying the
  SDL annotation additions through the pane's formatter.
- Fixed/tested: native `ATUIDebuggerPaneBreakpoints::UpdateBreakpoints()` sorts
  groups with `std::sort(..., vdstringpredi())`, but `vdstringpredi` is an
  equality predicate (`comparei() == 0`) rather than a strict ordering
  predicate. SDL now intentionally uses a strict case-insensitive less-than
  comparator with a case-sensitive tie-break, giving deterministic
  cross-platform order. Runtime verification created grouped breakpoints in
  unsorted mixed-case order (`zulu`, `Bravo`, `alpha`) through
  `debugger_console bp -g ...` and `breakpoint_pane_order` returned
  `alpha.0|Bravo.0|zulu.0`; cleanup returned the order to empty.
- Tested: actual mouse context-menu Delete activation now has focused runtime
  coverage. With a persistent test-mode socket, `right_click BPTable ##row`
  exposed the popup items `New Breakpoint...` and `Delete`; `click Popup Delete`
  removed breakpoint ID 0, `breakpoint_describe 0` returned not found, and
  `breakpoint_count` returned zero.
- The native Breakpoints pane resource is only a list view; New/Delete are
  exposed through the context menu and command handling. SDL also exposes
  New/Delete through context menus, so the absence of visible buttons is not a
  parity issue.

Remaining fix/validation:

- Keep useful annotations, but list them as intentional SDL additions.
- Keep the `breakpoint_format_trace` regression path in test-mode coverage for
  native trace-message command formatting.
- No Breakpoints-specific parity-critical fix is currently outstanding in this
  review pass. Real one-shot/clear-on-reset flag creation is available through
  core console commands, not through controls in the native breakpoint dialog or
  SDL modal; SDL currently verifies their display annotation formatting.

### 14. Low/Medium: Registers and Call Stack are functional but not exact

Registers:

- Fixed: SDL now uses the native register label spacing across supported
  disassembly modes instead of padded SDL-only labels.
- Fixed: SDL now renders the register text through a read-only multiline ImGui
  input widget, restoring normal text selection/copy behavior analogous to the
  native read-only EDIT control.

Call Stack:

- Fixed: SDL now matches native's 16-frame `GetCallStack()` limit, uses the
  native `>SP: *PC (symbol)` display format without symbol offsets, applies the
  same low-16-bit current-frame marker test, and preserves row selection
  independently from the current-frame marker.

Registers now match the reviewed native text formatting and selectable/copyable
read-only text behavior. Remaining risk is focused runtime coverage for less
common CPU targets.

### 15. Low/Medium: Raw pane lookup remains a compatibility limitation

`src/AltirraSDL/stubs/console_stubs.cpp` bridges many console functions to the
ImGui debugger. Current state is mixed:

- `ATGetUIPaneAs()` delegates to `ATUIDebuggerGetPaneAs()`.
- `ATUIGetActivePaneAs()` delegates to the focused ImGui debugger pane.
- `ATUIGetActivePaneId()` returns `ATUIDebuggerGetFocusedPaneId()`.
- `ATGetUIPane()` and `ATUIGetActivePane()` still return null because SDL
  ImGui panes are not native `ATUIPane` instances.

Impact:

- Verified with `rg` over SDL/core compiled trees: current SDL-compiled code
  only defines these raw lookup stubs; active debugger integrations use typed
  lookup or direct SDL pane APIs.
- Native Windows sources still have many raw `ATGetUIPane()` callers, so this
  remains a limitation if more native UI/debugger source is compiled into SDL.
- Typed interfaces are available only for panes that implement
  `AsPaneInterface()`.

Recommended fix:

- Keep adding typed `AsPaneInterface()` bridges for pane interfaces that are
  needed by shared code.
- Implement raw `ATUIPane*` adapters only if a future shared-code import truly
  requires pane identity or frame-window relationships.

### 16. Low/Medium: Printer Output is represented, but raster parity needs tests

Native Printer Output in `uidbgprinteroutput.cpp` supports:

- Text printer output through a text editor control.
- Graphical printer output through `ATUIPrinterGraphicalOutputWindow`.
- Output selection, clear, auto-attach, and output add/remove handling.
- Graphical panning/zooming and print-head tracking.
- Save As PNG at 96/300 DPI, PDF, and SVG.

SDL Printer Output in `ui_dbg_printer*.cpp` implements the same broad feature
set:

- Text and graphical output selection.
- Clear button.
- Output add/remove callbacks and auto-attach behavior.
- Graphical pan/zoom state, print-head tracking, and invalidation.
- Save As PNG 96 DPI, PNG 300 DPI, PDF, and SVG.

The main remaining risk is not missing UI surface, but rendering/export parity:
SDL uses a separate software rasterizer and separate PNG/PDF/SVG writers from
the Windows implementation. These should be compared with known graphical
printer output to catch color, coverage, bounds, and DPI differences.

### 17. Low/Medium: History pane source-error/interface parity is fixed; UI tests remain

Native `ATHistoryWindow` in `src/Altirra/source/uidbghistory.cpp` delegates the
tree control behavior to `ATUIHistoryView` in
`src/Altirra/source/uihistoryview.cpp`. The reviewed native behavior includes:

- `JumpToCycle()` lower-bounds the current history buffer and calls
  `SelectInsn()`; SDL `ATImGuiHistoryPaneImpl::JumpToCycle()` uses the same
  lower-bound and index selection model.
- Native `Ctrl+F` focuses the search edit control, PageUp/PageDown move by the
  current page item count, and End backs up to the previous visible line when
  the back node is hidden by search filtering.
- `OnDebuggerSystemStateUpdate()` avoids work while running and updates opcodes
  when stopped; SDL marks the pane for update on stopped states and updates
  during render.
- The context menu exposes the same history visibility toggles, collapse
  toggles, timestamp modes, timestamp-origin commands, and visible-copy command.
- Native `JumpToSource()` calls `ATConsoleShowSource()` and shows
  `ATUIShowError()` when no exact source mapping exists.
- Native `ATHistoryWindow::AsInterface()` returns
  `IATUIDebuggerHistoryPane` for `IATUIDebuggerHistoryPane::kTypeID`, and
  shared code in `console.cpp` uses that interface for beam-position history
  jumps.

Original SDL mismatch:

- `src/AltirraSDL/source/ui/debugger/ui_dbg_history.cpp` called
  `ATImGuiConsoleShowSource(mContextAddr)` from "Go to Source" and ignored a
  false result, so a missing source mapping silently failed.
- SDL did not handle `Ctrl+F`, used a fixed 20-line PageUp/PageDown jump, and
  selected the tree back node directly on End without the native visibility
  check.
- `src/AltirraSDL/stubs/win32_stubs.cpp` exposed `ATUIShowAlertError()` but did
  not implement the native `ATUIShowError()` overloads declared by
  `uicommondialogs.h`.
- SDL did not expose `IATUIDebuggerHistoryPane` from the History pane. Its
  beam-position jump helper used a concrete `ATImGuiHistoryPaneImpl` cast
  instead of the native typed-pane lookup contract.
- SDL `ATConsolePingBeamPosition()` could not distinguish "History pane is not
  present" from "History pane is present but cannot jump"; it activated History
  in both cases. Native `console.cpp` returns immediately when
  `IATUIDebuggerHistoryPane` is absent and only activates History after a
  failed jump on an existing pane.

Current code state:

- History "Go to Source" now reports the same native-style message when
  `ATImGuiConsoleShowSource()` fails:
  `There is no source line associated with the address: ...`.
- History keyboard navigation now handles `Ctrl+F`, uses the visible child
  height to determine PageUp/PageDown distance, and mirrors native End-key
  visibility handling. Runtime coverage uses `history_describe` and synthetic
  key injection to confirm focused History receives PageUp/End, `Ctrl+F`
  focuses search, text entry activates `search_active=1 search_text="lda"`,
  and filtered navigation stays on visible matches.
- History context-menu side effects are factored into shared helpers used by
  both the ImGui popup and test mode. Runtime coverage through
  `history_select` / `history_context` confirms timestamp mode changes,
  Set Timestamp Origin, and Reset Timestamp Origin against a real selected
  instruction.
- Test mode now records blocking SDL/native message boxes through
  `query_message_box` and suppresses the OS modal only while `--test-mode` is
  active. Runtime coverage for History source failure clears the recorder,
  invokes `history_context go_source` on an instruction without source mapping,
  observes `applied:false`, and verifies a recorded error dialog titled
  `Altirra Error` with `There is no source line associated with the address:
  $EB22.`.
- History source-success navigation has focused runtime coverage with a real
  debugger source mapping. The test selects a real History instruction, writes
  a temporary source file and minimal MADS-style listing that maps the selected
  instruction address, loads the listing through the real `.loadsym` debugger
  command, invokes `history_context go_source`, and observes `applied:true`.
  `source_query_file` then confirms that the Source pane opened the mapped file
  and focused zero-based `selected_line:1` for source line 2, while
  `query_message_box` remains empty.
- History real popup activation has focused runtime coverage through
  `history_open_context`: the same `HistCtx` popup path used by right-click
  opens an active ImGui popup window with native History menu items, and a
  popup item click is routed through the real menu item path.
- History horizontal scrolling now has focused runtime coverage through
  `history_hscroll` and `history_describe`. The rendered ImGui child reported
  a non-zero horizontal scroll range; requesting 200 pixels clamped to the
  measured right edge, requesting a negative value clamped to zero, and
  requesting a very large value clamped to the same right edge. This verifies
  SDL's current scrollbar range/state path and its equivalent of native
  `ATUIHistoryView::HScrollToPixel()` clamping.
- SDL stubs now implement `ATUIShowInfo()`, `ATUIShowError2()`, and the
  `ATUIShowError()` overloads through SDL message boxes.
- `ATImGuiHistoryPaneImpl` now derives from `IATUIDebuggerHistoryPane`,
  returns it from `AsPaneInterface()`, and
  `ATUIDebuggerHistoryPaneJumpToCycle()` resolves it through
  `ATGetUIPaneAs(kATUIPaneId_History, IATUIDebuggerHistoryPane::kTypeID)`.
- SDL `ATConsolePingBeamPosition()` now resolves
  `IATUIDebuggerHistoryPane` directly and mirrors native `console.cpp` control
  flow: absent pane returns; failed jump on an existing pane activates History.

Remaining impact:

- Build coverage passed after this change, and test-mode smoke confirmed the
  debugger dockspace opens with the History pane visible.
- Test mode exposes `history_interface`, which activates History and verifies
  that the native `IATUIDebuggerHistoryPane` interface is reachable through
  `ATGetUIPaneAs()`.
- Search/filter keyboard selection, real popup activation, timestamp-origin
  context actions, source-success navigation, source-failure dialog lifecycle,
  and horizontal scrolling now have focused runtime coverage.

Recommended fix:

- Continue comparing less common history behavior such as mouse-wheel and
  horizontal mouse-wheel input against `uihistoryview.cpp`.

### 18. Low/Medium: Disassembly navigation parity is improved; tests remain

Native `ATDisassemblyWindow` in `src/Altirra/source/uidbgdisasm.cpp` has these
reviewed behaviors that were missing in the SDL pane:

- Its message filter handles printable `WM_CHAR`/`WM_SYSCHAR` input by
  activating the Console pane and forwarding the character to the focused
  console control.
- Its `ID_CONTEXT_GOTOSOURCE` command calls `ATConsoleShowSource()` and shows
  `ATUIShowError()` with `There is no source line associated with the
  address: ...` when no exact source mapping exists.
- Its disassembler stores a target extended address for expandable jump/call
  operands using the `kBM_Expand*` break-map encodings, marks the operand as a
  hyperlink, and calls `PushAndJump()` when the link is selected.
- `PushAndJump()`, `GoPrev()`, and `GoNext()` maintain a 64-entry circular
  navigation history behind the native Back/Forward toolbar buttons.
- Its call-preview `[expand]` link changes to `[contract]`, inserts a nested
  disassembly range from the target address, stops at a procedure-ending
  instruction, and removes deeper nested rows when contracted.
- `RemakeView()` and `OnDebuggerSystemStateUpdate()` move the text-editor
  cursor to the frame PC, PC, or focus line. Native
  `Debug.ToggleBreakpoint`/F9 then toggles the breakpoint at that cursor line.

Original SDL mismatch:

- `src/AltirraSDL/source/ui/debugger/ui_dbg_disassembly.cpp` only handled
  F9/PageUp/PageDown/Escape keyboard shortcuts while focused. Printable text
  typed in Disassembly did not seed the Console input line.
- The Disassembly context-menu "Go to Source" action called
  `ATImGuiConsoleShowSource(mContextAddr)` and ignored a false return.
- SDL appended `;[expand]` for call-preview candidates, but it did not retain
  the decoded operand target address and did not provide a way to follow target
  links or navigate back/forward.
- SDL did not implement the native `[expand]` / `[contract]` call-preview
  insertion behavior.
- SDL rebuilt the line list and scrolled to frame PC/PC/focus, but left
  `mSelectedLine` at `-1`. After stopping on a breakpoint or otherwise
  rebuilding the pane, `Debug.ToggleBreakpoint`/F9 could become a no-op until
  the user clicked another disassembly line.

Current code state:

- SDL Disassembly now forwards printable ImGui input characters to
  `ATUIDebuggerFocusConsoleWithText()`, which activates the Console pane and
  appends the UTF-8 text to the Console command input.
- SDL Disassembly now shows the same native-style missing-source error dialog
  when "Go to Source" cannot find an exact source line.
- The real popup path and test path now share `GoToSourceForAddress()`, so
  source-success and source-failure behavior is not duplicated.
- SDL Disassembly now computes and stores the same reviewed target-address
  cases as native for `kBM_ExpandAbs16`, `kBM_ExpandAbs16BE`,
  `kBM_ExpandAbs24`, `kBM_ExpandRel8`, and `kBM_ExpandRel16BE`.
- Target rows are rendered with link coloring, double-click follows the target
  through `PushAndJump()`, and the context menu exposes "Go to Target" for
  target-bearing lines.
- The toolbar now includes Back/Forward buttons backed by the same 64-entry
  circular history behavior used by native `PushAndJump()` / `GoPrev()` /
  `GoNext()`.
- SDL Disassembly now implements call-preview expansion and contraction:
  double-clicking the `[expand]` / `[contract]` token toggles nested rows, the
  context menu exposes the same operation, and contraction removes rows whose
  nesting level is deeper than the parent line.
- Expanded SDL call-preview blocks now reuse the same mixed-source insertion
  helper as the top-level disassembly view. This matches native
  `ATDisassemblyWindow::OnLinkSelected(kSelCode_Expand)`, which calls
  `Disassemble()` with `nestingLevel + 1` and therefore emits source lines
  inside the nested preview block.
- SDL Disassembly now sets `mSelectedLine` to the same line it scrolls to after
  a rebuild: frame PC first, then PC, then the navigated focus line. This
  matches the native text-editor cursor target used by breakpoint commands.

Remaining impact:

- Build coverage passed after this change, and test-mode smoke confirmed that
  the debugger opens and the Disassembly pane can be activated and focused.
- Test-mode commands now exist to send text input (`send_text <utf8>`), query
  the focused debugger pane (`query_debugger_focus`), and query the Console
  input buffer (`query_console_input`). These exposed one SDL focus-condition
  bug in Disassembly: the keyboard handler was checking the current child
  window focus instead of the pane-level root/child focus state. SDL now uses
  `mbHasFocus` for that block.
- SDL now also handles printable text at the `SDL_EVENT_TEXT_INPUT` event layer
  for the focused Disassembly pane, matching native `WM_CHAR` more directly
  than relying on ImGui's per-frame character queue. Runtime test-mode evidence:
  after `Pane.Disassembly`, `query_debugger_focus` returned pane `5`
  (`kATUIPaneId_Disassembly`), `query_console_input` returned `""`,
  `send_text abc` plus `wait_frames 3` moved focus to pane `2`
  (`kATUIPaneId_Console`) and `query_console_input` returned `"abc"`.
- Test mode now exposes `disasm_selected_breakpoint [query|toggle]`, which
  exercises the same pane-command toggle path as `Debug.ToggleBreakpoint`.
  Runtime verification paused the emulator, opened Disassembly, confirmed a
  valid selected line at `$0000`, toggled the breakpoint on and observed
  `has_breakpoint:true`, then toggled it off and observed
  `has_breakpoint:false`.
- Test mode now also exposes `disasm_breakpoint_runstop_regression`, which
  specifically covers the reported stale-selection failure mode after a
  run/stop-style rebuild. Runtime verification paused the emulator, selected
  `$0000`, toggled the selected-line breakpoint on, simulated
  `OnDebuggerSystemStateUpdate()` running then stopped at `$0000`, confirmed the
  rebuilt pane still selected line 0 with `has_breakpoint:true`, toggled through
  the same pane command again, observed `after_second_toggle:false`, and
  confirmed `breakpoint_count` returned 0 afterward.
- Test mode now exposes `disasm_context go_source`, which invokes the same
  selected-line source action as the real popup. Runtime verification without a
  source mapping returned `applied:false` and recorded the native-style
  `Altirra Error` through `query_message_box`. Runtime verification after
  loading a temporary MADS-style listing for the selected address through
  `.loadsym` returned `applied:true`, opened the Source pane, focused
  zero-based `selected_line:1`, and left the message-box recorder empty.
- Test mode now exposes `disasm_target_nav`, which seeds a deterministic
  `JSR $2010` at `$2000`, follows the decoded target through the same
  `PushAndJump()` path used by double-click and context-menu "Go to Target",
  then exercises the same `GoPrev()` / `GoNext()` methods used by the
  Back/Forward toolbar buttons. Runtime verification observed `$2000 -> $2010`,
  Back returning to `$2000`, Forward returning to `$2010`, and the native-style
  history counters changing from back=1/forward=0 to back=0/forward=1 and
  back=1/forward=0.
- Test mode now exposes `disasm_preview`, which exercises the same
  `ToggleCallPreview()` path used by double-clicking the `[expand]` /
  `[contract]` token and by the context menu. Runtime verification without
  symbols reported two nested instruction rows, zero nested source rows,
  `[contract]` after expand, and the original line count after contract.
  Runtime verification after loading a temporary MADS-style listing for `$2010`
  and opening the mapped source file reported two nested instruction rows plus
  one nested source row, proving that nested preview blocks now include native
  mixed-source insertion.

Recommended fix:

- Keep the `send_text`/`query_debugger_focus`/`query_console_input` regression
  path in test-mode coverage so printable Disassembly forwarding remains
  guarded.
- Keep the `disasm_preview` regression path in test-mode coverage so
  `[expand]` / `[contract]` preview insertion and nested mixed-source rows stay
  aligned with native behavior.

### 19. Low: Export Debugger Help was missing from SDL menus

Native Windows implements Help > Export Debugger Help in
`src/Altirra/source/uidbghelp.cpp` and registers it as
`Help.ExportDebuggerHelp` in `cmds.cpp`. The command loads the debugger help
resource, inserts it into the debugger help HTML template, and saves the
resulting page through a save-file dialog.

Original SDL mismatch:

- `src/AltirraSDL/source/ui/menus/ui_menus.cpp` and
  `src/AltirraSDL/source/ui/menus/macos_menubar.mm` displayed "Export Debugger
  Help..." as a disabled placeholder.
- `src/AltirraSDL/source/commands_sdl3.cpp` did not register
  `Help.ExportDebuggerHelp`.
- SDL already embedded `IDR_DEBUG_HELP` for the `.help` console command through
  `src/AltirraSDL/source/os/oshelper_sdl3.cpp`, but there was no UI export
  path.

Current code state:

- `src/AltirraSDL/source/ui/debugger/ui_dbg_help_export.cpp` implements
  `ATUIExportDebugHelp()`, opens an SDL save dialog, writes an HTML page using
  the same debugger help text resource, inserts `AT_VERSION`, escapes help
  content as HTML, and appends `.html` when the selected path has no extension.
- `Help.ExportDebuggerHelp` is registered in the SDL command table.
- The ImGui and macOS Help menus now enable and call the export command.
- Test-mode command `export_debugger_help [utf8_path]` covers the non-OS export
  path. Runtime verification covered extensionless paths appending `.html`,
  explicit `.html` paths staying unchanged, empty/cancel producing no export,
  nonempty generated HTML, expected debugger-help markers, and identical output
  for extensionless and explicit paths resolving to the same generated page.

Remaining impact:

- The exported page follows the native template behavior and data source, but
  the SDL implementation currently carries the template in code instead of as a
  separate embedded `IDR_DEBUG_HELP_TEMPLATE` resource.
- Manual/runtime coverage is still needed for the native SDL save dialog,
  built-in fallback dialogs if enabled, and WebAssembly picker behavior.

## Native-Only Or Still Incomplete In SDL

These are Windows debugger behaviors or tools verified in the native code that
are absent from SDL, only partially represented, or intentionally routed
differently.

- Native Profile View (`profilerui.cpp` / `ATUIProfilerPane`) now has a
  separate SDL pane, a native-style frame timeline, and call-graph tree
  presentation. Source drill-down now opens a detailed disassembly/count view,
  source-detail scroll/header behavior is implemented, and flat-table
  selection/status behavior is implemented. Focused runtime tests are still
  incomplete.
- Native debugger callback setup in `ATInitUIPanes()` is now replicated for SDL
  for script auto-load confirmation and debugger file requests. WebAssembly
  registers the same request callback, but currently returns an explicit
  unsupported empty-path result unless a synchronous test override is active.
- Native active-pane interface lookup and command dispatch are only partially
  represented: `ATUIGetActivePaneAs()` and `ATGetUIPaneAs()` now bridge exposed
  debugger interfaces, but raw `ATUIGetActivePane()` and `ATGetUIPane()` still
  return null because SDL ImGui panes do not derive from native `ATUIPane`.
- Native named `ATSavePaneLayout("Standard"/"Debugger")` serialization is not
  implemented in SDL, but debugger transitions now explicitly flush ImGui layout
  settings and the debugger dockspace is restored through ImGui's saved dock
  layout.
- Native source window file watching is now represented in the SDL source pane
  through the portable `VDFileWatcher` polling path. Listing parsing, path alias
  matching, loose basename matching, exact source-line lookup,
  module/open-source-dir search, and native-desktop missing-source browse
  prompts are also represented. WebAssembly and forced built-in dialogs still
  need a missing-source prompt strategy.
- Native Debug Display controls are mostly represented, but native display-stack
  behavior, true bicubic filtering, and display-child semantics remain
  incomplete.
- Native Change Font command now routes to SDL's Fonts configuration page
  instead of a Windows-style modal font picker.
- Native Export Debugger Help is now represented in SDL, but the SDL template
  is compiled into the exporter source rather than loaded as
  `IDR_DEBUG_HELP_TEMPLATE`.

## Intentional SDL Additions

These are SDL behaviors that appear to be improvements or frontend-specific
choices rather than bugs. They can stay if we agree they are intentional, but
they should remain documented here so parity reviews do not keep rediscovering
them.

- Default debugger layout opens Memory 1, Watch 1, and Call Stack in addition to
  the Windows default Display, Console, Registers, Disassembly, and History.
- Call Stack formatting/frame-count/selection behavior now matches the native
  pane; the SDL default layout still opens it by default as an intentional SDL
  addition.
- Breakpoints list includes extra annotations for one-shot, trace, and
  clear-on-reset state.
- Breakpoints group ordering intentionally uses a strict case-insensitive
  comparator with a case-sensitive tie-break. Native Windows currently calls
  `std::sort(..., vdstringpredi())`, but `vdstringpredi` is an equality
  predicate and produces undefined/platform-dependent sort behavior.
- History pane includes additional ImGui-era conveniences: when CPU history is
  disabled, SDL shows an "Enable CPU History" button instead of only the native
  explanatory text, and its search controls are integrated directly in the
  ImGui pane header.
- Some panes use modern ImGui layouts instead of direct Win32 control clones.
- Memory panes expose rich inline controls for address spaces, value modes, and
  bitmap viewing.
- `Pane.Display` is command-gated while the debugger is closed because the SDL
  dockable Display pane exists only inside the ImGui debugger dockspace; outside
  the debugger, the main emulator display is not an `ATUIPane` window.
- `Pane.PrinterOutput` opens the debugger dockspace when invoked while closed,
  because the current SDL Printer Output implementation is also an ImGui
  debugger pane rather than a native-style normal pane.
- `Debug.OpenSourceFile` and `Debug.OpenSourceFileList` open the debugger
  dockspace when invoked while closed so the resulting SDL source panes are
  visible; native Windows does not need this because native panes are managed by
  the frame system.

Rules for additions:

- Additions must not change debugger/emulator state differently from native for
  the same command.
- Additions must not hide or disable native functionality.
- Additions should be reachable through the same menu/command model when they
  correspond to native panes or tools.
- Additions should be listed in this section when introduced.

## Pane-by-Pane Status

| Area | Native reference | SDL reference | Status |
| --- | --- | --- | --- |
| Debugger lifecycle | `console.cpp` | `ui_debugger.cpp`, `console_stubs.cpp` | Native desktop callbacks are registered; debugger transitions now explicitly flush ImGui layout settings; exact native named layout serializer remains incomplete. |
| Main Display pane | display pane registration/native display stack | `RenderDisplayPane()` | Uses backend `RenderFrameClipped()` callback, global zoom/pan destination math, viewport clipping, typed `IATDisplayPane` adapter, and native-style video-writer motion-vector overlay now; remaining gaps are visual tests and enhanced-text replacement. |
| Console | `uidbgconsole.cpp` | `ui_dbg_console.cpp`, `console_stubs.cpp` | Functional bridge; typed pane lookup works for exposed interfaces, raw `ATUIPane*` lookup remains null. |
| Registers | `uidbgregisters.cpp` | `ui_dbg_registers.cpp` | Native text formatting and read-only selection/copy behavior are implemented; less common CPU target formats still need focused runtime coverage. |
| Disassembly | `uidbgdisasm.cpp` | `ui_dbg_disassembly.cpp` | Functional with typed `SetPosition()` bridge; printable-key forwarding, source success/failure feedback, selected-line breakpoint toggling after a run/stop-style rebuild, reviewed target-address decoding, target following, Back/Forward history, and `[expand]` / `[contract]` nested call-preview rows now match reviewed native paths, including mixed-source rows inside expanded preview blocks. Focused runtime coverage now verifies source feedback, selected-line breakpoint toggling, target navigation, Back/Forward history, preview expand/contract, and nested preview source insertion. |
| Source | `uidbgsource.cpp` | `ui_dbg_source.cpp` | Improved listing parsing, path alias/search, exact source lookup, source-list behavior, native-desktop missing-source prompt, file watcher reload, and focused reload coverage; significant remaining gap is WebAssembly/builtin-dialog prompt handling plus symbol-derived source-command runtime coverage. |
| History | `uidbghistory.cpp`, `uihistoryview.cpp` | `ui_dbg_history.cpp`, `ui_dbg_history_format.cpp` | Core tree update, jump-to-cycle, typed `IATUIDebuggerHistoryPane` lookup, context-menu options, missing-source error behavior, Ctrl+F search focus, page-size keyboard navigation, and End-key filtered-line handling have been compared and aligned where mismatches were found. Focused runtime coverage now verifies Ctrl+F search activation, filtered End/PageUp selection, real popup activation, timestamp-origin context actions, source-success navigation through loaded symbols, source-failure dialog recording, and horizontal scroll clamping. |
| Memory 1-4 | `uidbgmemory.cpp` | `ui_dbg_memory*.cpp` | Broad coverage; Add-to-Watch formatting including non-CPU extended addresses, Track On-Screen overflow, edit method selection guards, hex/decimal byte/word commit behavior, text interpretation writes through `CommitEdit()`, typed edit auto-advance including row/page crossing, CancelEdit selection preservation, native-style page-scroll offset preservation, selected-address movement, and wrap-aware highlight visibility now match reviewed native paths with focused coverage for the non-OS cases. Real key-event/focus behavior and all address-space boundaries still need focused validation. |
| Watch 1-4 | `uidbgwatch.cpp` | `ui_dbg_watch.cpp` | Functional with native-style printable editing/update timing improvements; printable-start editing, selected-row F2-style editing, Delete removal, typed interface lookup, AddWatch dispatch, update timing, and low-level mouse/table row selection now have focused coverage. |
| Breakpoints | `uidbgbreakpoints.cpp` | `ui_dbg_breakpoints.cpp` | Mostly implemented; condition validation, condition-only/non-stopping validation, command+trace round-trip, selected-row Delete behavior through the pane helper, keyboard Delete deletion, context-menu Delete, deterministic mixed-case group ordering, basic delete/count, and annotation description formatting now match or intentionally improve reviewed paths with focused test-mode coverage. |
| Call Stack | `uidbgcallstack.cpp` | `ui_dbg_callstack.cpp` | Functional; frame count, formatting, current marker, and selection behavior now match native. The default SDL layout still opens it by default as an intentional addition. |
| Targets | `uidbgtargets.cpp` | `ui_dbg_targets.cpp` | Implemented and reachable; selection is separate from the current-target arrow like native, and selected-row activation now works through both double-click and Enter like native list activation. Focused coverage through `targets_activate` verifies selected-row activation and invalid-row handling on the default one-target setup; a multi-target fixture is still needed to prove switching to another device target. |
| Debug Display | `uidbgdebugdisplay.cpp`, `debugdisplay.cpp` | `ui_dbg_debugdisplay.cpp` | Reachable with native controls added; Bicubic now performs real bicubic resampling, system-state updates mark the pane dirty like native, and focused `debug_display` runtime coverage verifies DL/PF validation, DL mode switching, filter, and palette state. Remaining gap is native child display-stack parity and visual comparison for rendered output. |
| Printer Output | `uidbgprinteroutput.cpp` | `ui_dbg_printer*.cpp` | Broad feature coverage; now reachable while debugger is closed by opening the SDL debugger dockspace first; raster/export parity should be tested. |
| Profiler/Profile View | `profilerui.cpp` | `ui_dbg_profileview.cpp` | Implemented with real profiler session data, native-style frame timeline, call-graph tree presentation, flat-table selection/status, and source-detail drill-down/scroll/header behavior; focused interaction tests remain incomplete. |
| Trace Viewer/Analyzer | `uitraceviewer.cpp` | `ui_dbg_traceviewer*.cpp` | SDL implementation exists as an intentional separate SDL pane under `kATUIPaneId_PerformanceAnalyzerSDL`. |
| Verifier | `uiverifier.cpp` | `ui_dbg_verifier.cpp` | Same 11 verifier flags are present; mostly equivalent at reviewed level. |
| Export Debugger Help | `uidbghelp.cpp` | `ui_dbg_help_export.cpp` | Implemented and reachable from the Help menu/command table; non-OS export tests cover extension handling, cancel/no-export, and generated content markers. Native save-dialog coverage is still needed. |

## Recommended Remediation Order

1. Add debugger Display pane visual regression coverage and complete the
   enhanced-text replacement path once SDL has an equivalent backing system.
2. Add focused memory/source behavior validation for edit, scroll, watch,
   navigation, and source-step runtime coverage.
3. Add debugger close/reopen layout persistence coverage and implement raw
   `ATUIPane*` adapters only if remaining shared code paths prove
   they require native pane pointers instead of typed interface lookup.
4. Complete WebAssembly/builtin-dialog handling for source missing-source
   prompts.
5. Add focused source/listing regression tests for symbol-only and listing-only
   address mappings.
6. Complete Debug Display child display-stack parity and focused visual/runtime
   coverage.
7. Add focused Profile View interaction tests now that timeline selection,
   call-graph presentation, flat-table selection/status, source drill-down, and
   Performance Analyzer routing are separated.
8. Add a WebAssembly async/VFS-aware debugger file-selection flow if real
   browser-backed `?` prompts are required; the current synchronous callback
   reports unsupported and returns the native empty-path abort result.
9. Add UI tests for all debugger panes and menu/command activation.
10. Add pane-specific parity tests for watch editing, memory edit/wrap behavior,
    breakpoints, and source line mapping.
11. Add native save-dialog coverage for Help > Export Debugger Help.

## Suggested Tests

Display:

- Render an asymmetric Atari frame in normal view and debugger Display pane.
- Compare orientation, aspect, crop, and stretch.
- Repeat with OpenGL and SDL renderer backends.
- Repeat with shader/screen effects enabled.
- Verify mouse selection and coordinate mapping against visible pixels.

Pane reachability:

- Open every debugger pane from Debug > Window. Current real-click coverage
  includes all Debug > Window pane entries.
- Open every debugger pane by command ID.
- Verify menu enabled state matches actual availability.

Command dispatch:

- `query_command` coverage now verifies native-equivalent predicates for
  `Debug.Run`, `Debug.Break`, Step Into/Over/Out, New Breakpoint, Toggle
  Breakpoint, Open Source File, and Source File List across running/closed,
  paused/closed, paused/open, and running/open states.
- Focus Source, select a mapped line, run Toggle Breakpoint from menu.
- Focus History, use Go to Source/Disassembly.
- Verify breakpoint placement matches native behavior.

Source:

- Open normal source file with debug symbols.
- Keep test-mode coverage for listing-only mappings: open a MADS/native listing
  file with address records but no symbol line table, then verify exact
  address-to-line, native nearest-previous address-to-line, line-to-address,
  and out-of-range lookup behavior with `source_query_mapping`.
- Keep test-mode coverage for listing-derived source-step range computation:
  verify adjacent-row ranges, no range when `pc + 1` reaches the next row, no
  range on the final row, and no range when the next row fails the native
  `< 100` byte cutoff with `source_query_step_range`.
- Test equivalent absolute/relative/path-alias source paths.
- Keep test-mode coverage for source file reload after disk edit: open a source
  file with `source_open_file`, edit it externally, then verify the same pane's
  first/last lines through `source_query_file`.

Callbacks:

- Set script auto-load mode to ask and verify the SDL UI prompts.
- On native desktop builds, manually validate the OS dialog path for debugger
  commands that request a file with `?`.
- Keep the non-OS `debugger_request_file` seam in test-mode coverage for open,
  save, cancel/empty, and UTF-8 path conversion.
- On WebAssembly builds, verify unsupported synchronous file requests report
  clearly, or replace them with a VFS picker flow.

Memory/Watch/Breakpoints:

- Add watches starting with `$`, digits, and symbols.
- Edit memory in each address space.
- Create read/write access breakpoints from Memory context menus.
- Add/edit conditional, trace, one-shot, and clear-on-reset breakpoints.

Help:

- Keep the `export_debugger_help` regression path in test-mode coverage for
  extensionless paths, explicit `.html` paths, cancel/no-export, nonempty
  generated HTML, and debugger-help content markers.
- Manually validate the native SDL save dialog path for Help > Export Debugger
  Help, including selecting a path without an extension.

## Current Conclusion

The SDL debugger is well underway but should not be treated as a completed port.
The Display pane no longer bypasses the backend render path and now exposes a
typed display-pane interface for implemented SDL-equivalent operations,
including native-style `IATVideoWriter` attachment for the recording
motion-vector debug overlay. It still needs visual regression coverage and an
SDL backing system for enhanced-text replacement. The largest architectural
problem is now that SDL does not yet reproduce the native debugger's pane
lifecycle, separate layout save/restore, and full pane-specific command
behavior.

Useful SDL additions can stay, but they should be documented in the
"Intentional SDL Additions" section and must not replace or disable native
debugger functionality.
