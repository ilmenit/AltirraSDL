# AltirraSDL Debugger Implementation Tracker

This tracker records debugger work relative to native Windows Altirra. The
goal is Windows parity first; SDL-only additions can stay when they are
intentional and documented.

## Completed in current pass

- Wired `ATUIDebuggerInit()` into SDL startup after `ATInitDebugger()`.
- Registered the debugger script auto-load confirmation callback in SDL,
  matching the native Windows `ATConsoleConfirmScriptLoad()` behavior as far
  as the current SDL generic dialog layer supports it.
- Added `ATUIShowGenericDialogAutoCenter()` in the SDL generic dialog shim so
  code can use the same API entry point as Windows.
- Enabled the Debugger > Window > Targets and Debug Display menu items; both
  panes already had SDL pane creation functions.
- Registered command IDs for the already implemented pane targets:
  `Pane.Memory2`, `Pane.Memory3`, `Pane.Memory4`, `Pane.Watch1`,
  `Pane.Watch2`, `Pane.Watch3`, `Pane.Watch4`, `Pane.Breakpoints`,
  `Pane.Targets`, and `Pane.DebugDisplay`.
- Registered debugger command IDs for `Debug.OpenSourceFileList`,
  `Debug.ToggleDebugger`, `Debug.VerifierDialog`, and
  `Debug.ShowTraceViewer`.
- Wired `ATUIGetActivePaneId()` to the ImGui debugger focused-pane tracker.
- Added an ImGui debugger pane command hook and routed
  `Debug.ToggleBreakpoint` through the focused source/disassembly pane instead
  of toggling the current frame PC unconditionally.
- Changed source-pane single-click behavior to select the active line, with
  breakpoint toggling through the pane command/context-menu path, matching the
  native text editor model more closely.
- Split `Debug.Run` from `Debug.RunStop`; `Debug.Run` now only starts
  execution and can delegate source-mode run to the focused source pane.
- Replaced the debugger Display pane's direct raw-texture `ImGui::Image()` path
  with an ImGui draw callback that calls the active display backend's
  `RenderFrameClipped()` path. The pane also uses the main display's
  stretch-mode and pixel-aspect fit rules plus the same global zoom, pan, and
  pixel-snapping destination math as the normal display, so OpenGL
  orientation/effects are handled by the same backend pipeline as the normal
  display while output is constrained to the docked pane viewport.
- Added a typed SDL `IATDisplayPane` adapter for
  `ATGetUIPaneAs(kATUIPaneId_Display, IATDisplayPane::kTypeID)` and
  `ATUIGetDisplayPane()`. The adapter forwards mouse capture/release, display
  reset, text select/deselect/copy, frame copy/save/capture, paste, filter
  refresh, rendered-frame callbacks, and auto-suggest control to existing SDL
  display/UI systems.
- Implemented native-style `IATDisplayPane::SetVideoWriter()` handling for
  the SDL Display pane. SDL video recording now sets/clears the writer on
  start/stop like native `uifrontend.cpp`, and the debugger Display pane draws
  the `recording.video.show_motion_vectors` overlay from
  `ATVideoRecordingDebugInfo`.
- Registered debugger option/mode command IDs backed by existing portable
  state: `Debug.ToggleBreakAtExeRun`, `Debug.ToggleAutoReloadRoms`,
  `Debug.ToggleAutoLoadKernelSymbols`, `Debug.ToggleAutoLoadSystemSymbols`,
  `Debug.ToggleDebugLink`, all pre/post-start symbol-load mode commands, and
  all script auto-load mode commands.
- Fixed source-pane navigation actions so "Show Next Statement" fallback and
  "Go to Disassembly" move the disassembly pane with an SDL
  `SetPosition()` equivalent instead of mutating debugger frame PC state.
- Improved the Debug Display pane toward native parity:
  added DL history/history-start mode selection, Auto PF reset, filter mode
  menu entries, native-style 16-bit address validation via `ResolveSymbol()`,
  DL mode switching on manual DL address entry, and OpenGL UV correction for
  the debug-display texture.
- Fixed Debug Display Bicubic filtering so the Bicubic menu item now produces
  a bicubic-resampled texture instead of using the same linear texture sampling
  as Bilinear. Debug Display also now marks itself dirty on every debugger
  system-state update, matching native unconditional deferred updates.
- Improved source-window reuse to match native lookup more closely:
  SDL now uses `VDFileIsPathEqual()` for exact path comparison and then falls
  back to filename-only comparison before opening another source pane.
- Added a safe native-interface bridge for ImGui debugger panes:
  `ATUIGetActivePaneAs(IATUIDebuggerPane::kTypeID)` and
  `ATGetUIPaneAs(..., IATUIDebuggerPane::kTypeID)` now return the focused/target
  ImGui debugger pane interface, and disassembly exposes
  `IATUIDebuggerDisassemblyPane` without pretending that ImGui panes are
  `ATUIPane*`.
- Routed Step Into, Step Over, and Step Out through the focused pane's
  `OnPaneCommand()` first, matching native command dispatch. Source panes now
  handle step into/over through their source-range stepping helper and step out
  through source mode.
- Split Profile View from Performance Analyzer routing. `Pane.ProfileView`
  now opens an SDL Profile View pane backed by the real `ATCPUProfiler` /
  `ATProfileSession` path, while `Debug.ShowTraceViewer` opens the SDL
  Performance Analyzer under an SDL-only pane ID.
- Improved Profile View timeline parity. SDL now draws a native-style frame
  timeline with per-frame cycle-height bars, the same trimmed-mean vertical
  scale used by native `ATUIProfilerPane::LoadProfile()`, native-style initial
  zoom selection, hover highlighting, mouse-drag frame range selection wired to
  `MergeFrames()`, wheel and `+`/`-` zoom, and a horizontal scroll control.
- Improved Profile View call-graph presentation parity. In Call Graph mode,
  SDL now renders a tree instead of the flat table, matching native
  `ATUIProfileView::RemakeView()` structure: four root contexts, parent/child
  relationships from `ATProfileSession::mContexts`, zero-inclusive-instruction
  child filtering, inclusive-cycle sort order, and node labels with calls plus
  inclusive cycle/CPU-cycle/instruction percentages.
- Added the first native-equivalent Profile View source drill-down path.
  Double-clicking a flat profile row or a call-graph tree node now opens an
  `Altirra Profiler - Detailed View` ImGui window. It keeps a collapsed
  address-sorted copy of the current profile records, renders disassembly
  around the selected address through the same portable disassembly helpers as
  native `ATUIProfilerSourceTextView`, and shows the native Cycles, Insns, CPI,
  CCPI, DMA%, and Text columns.
- Completed the source-detail scroll/header parity slice. The detailed view now
  has a hexadecimal address field, Up/Down/Page Up/Page Down controls,
  mouse-wheel and keyboard scrolling over the detail table, dynamic visible-line
  sizing based on the window height, and resizable metric/text columns through
  ImGui table headers.
- Improved Profile View flat-table selection/status parity. SDL profile rows
  now use stable record IDs across sorting, support single-select, Ctrl-toggle,
  and Shift-range selection, and display the same selected-count/cycles/insns
  status summary computed by native `ATUIProfileView::OnItemSelectionChanged()`.
- Registered `Debug.ChangeFontDialog` and enabled Debug > Options > Change Font.
  The SDL command opens Configure System > Fonts, where the Monospace Font
  setting controls debugger panes.
- Registered `IATDebugger::SetOnRequestFile()` for SDL builds. Native desktop
  debugger commands that use `?` for a file path now open an SDL native file
  dialog synchronously, convert the selected UTF-8 path to `VDStringW`, and
  return an empty path on cancel/error to match the native abort contract.
  WebAssembly reports unsupported synchronous requests and returns the same
  empty-path abort result until an async/VFS-specific picker flow exists.
- Made `Pane.PrinterOutput` reachable while the debugger is closed. SDL opens
  the debugger dockspace first because the current Printer Output UI is an
  ImGui debugger pane, then activates the pane.
- Improved source pane parity:
  `ATImGuiConsoleShowSource()` now requires exact source-line mappings like
  native `ATConsoleShowSource()`, source-window lookup checks aliases, source
  opens search relative to the module path and already-open source window
  directories, resolved paths preserve the original symbol path as an alias, and
  Source File List now filters zero-line entries, sorts paths, shows the native
  empty placeholder, and opens as a centered modal popup.
- Added native-style listing-derived address mapping to the SDL source pane.
  The parser accepts the same MADS/native listing row formats as Windows,
  stores listing-derived maps separately from debugger symbol maps, and merges
  both so listing-only files can provide address-to-line and line-to-address
  mappings.
- Aligned source-pane address lookup and breakpoint behavior for listing-only
  files with native Windows. PC/frame highlighting now uses the native
  nearest-previous address rule within 64 bytes instead of requiring an exact
  address hit, and listing-only panes toggle raw address breakpoints while
  symbol-bound panes continue toggling source breakpoints by file/line.
- Refactored source-step range calculation into a shared helper used by both
  the real Step Into/Over path and test-mode coverage. This keeps
  listing-derived source-step behavior tied to the native algorithm:
  adjacent listing addresses produce a step range from `pc + 1` to the next
  mapped row, while rows already at the boundary, final rows, and gaps of 100+
  bytes fall back to normal source-mode stepping.
- Added a native-desktop missing-source browse prompt to
  `ATOpenSourceWindow()`. When automatic module/open-source-directory search
  fails, SDL shows a synchronous native SDL open-file dialog, opens the selected
  file with the original symbol path preserved as an alias, and returns no pane
  on cancel like Windows.
- Added source file watcher reload using the portable `VDFileWatcher`.
  Source panes poll watchers through the debugger pane `OnFrame()` hook, so
  hidden source panes also notice disk edits while the debugger is open. Reloads
  rebuild source text and symbol/listing maps while preserving the SDL source
  alias and selected line where possible.
- Fixed two Memory pane context-menu parity gaps: Add to Watch now formats the
  full context address with native `%04X` minimum-width semantics instead of
  truncating to 16 bits, and Track On-Screen now shows the native "Too many
  watches" error when the debugger watch-slot limit is reached.
- Aligned Memory pane cancel-edit semantics with native `ATMemoryWindow`:
  `CancelEdit()` now preserves the selected/highlighted address and only clears
  the in-progress edit value, matching Escape/cancel behavior in
  `uidbgmemory.cpp`.
- Improved Watch pane editing/update parity. Printable ASCII input queued by
  ImGui now starts inline editing, so expressions can begin with `$`, digits,
  `@`, and other punctuation like native `WM_CHAR` label editing. Watch panes
  also mark entries for refresh on every debugger system-state update, matching
  native update timing.
- Exposed the native `IATUIDebuggerWatchPane` interface from SDL Watch panes
  through `ATGetUIPaneAs()` / `ATUIDebuggerGetPaneAs()`, matching native
  `ATWatchWindow::AsInterface()` so shared pane code can call `AddWatch()`
  through the typed interface.
- Routed SDL's `ATUIDebuggerAddToWatch()` helper through the native-equivalent
  typed interface path: activate Watch 1, resolve
  `IATUIDebuggerWatchPane` via `ATGetUIPaneAs()`, then call `AddWatch()`. This
  aligns Memory pane Add to Watch with native `uidbgmemory.cpp`.
- Fixed Breakpoints pane validation and selection parity. Enabled conditions
  are now parsed even when the condition field is empty, matching native
  validation, and the pane tracks a selected row so Delete removes the selected
  breakpoint like the native list view. Existing breakpoint commands are loaded
  into the command field verbatim, matching native behavior and avoiding
  corruption of arbitrary `.printf` commands on edit/save.
- Refactored Breakpoints dialog submission through a shared native-equivalent
  validation helper used by both the ImGui modal and test mode. This covers the
  native requirements that condition-only breakpoints require a condition and
  non-stopping breakpoints require a command or trace message.
- Refactored Breakpoints row description formatting into a shared helper used
  by the pane and test mode. Focused coverage now verifies the SDL annotation
  additions for one-shot, trace/non-stopping, and clear-on-reset text.
- Refactored Breakpoints dialog trace-message formatting into a shared helper
  used by both the UI and test mode. This preserves the native conversion to a
  `.printf` command, including quote escaping and percent doubling.
- Fixed Breakpoints group ordering. Native
  `ATUIDebuggerPaneBreakpoints::UpdateBreakpoints()` calls
  `std::sort(..., vdstringpredi())`, but `vdstringpredi` is an equality
  predicate; SDL now uses a strict case-insensitive comparator with a
  case-sensitive tie-break for deterministic ordering.
- Replaced fixed debugger pane activation switch with an SDL ImGui pane factory
  registry. Fixed panes are registered during `ATUIDebuggerInit()`, default
  debugger pane creation uses the same path as `ATActivateUIPane()`, and
  registered native pane IDs without SDL factories now log a diagnostic.
- Improved debugger layout lifecycle parity. SDL now flushes ImGui layout
  settings immediately before entering debugger mode and before leaving it,
  matching native's save-on-transition behavior more closely. The saved
  debugger dockspace is then restored by ImGui's normal `.ini` persistence;
  the SDL default layout is only rebuilt when no saved dock children exist.
- Aligned the Call Stack pane with native Windows behavior: it now requests 16
  frames, formats rows as `>SP: *PC (symbol)` without symbol offsets, uses the
  native low-16-bit current-frame marker check, and preserves row selection
  independently from the current-frame marker.
- Aligned the Registers pane with native Windows behavior: labels now use the
  native spacing from `uidbgregisters.cpp`, and the text is rendered with a
  read-only multiline input widget so users can select/copy register text like
  the native read-only EDIT control.
- Aligned the Targets pane selection model with native Windows behavior. The
  current target is indicated only by the arrow marker, while list row selection
  remains independent and user-driven; ordinary debugger state updates no
  longer force a target-list rebuild.
- Aligned Targets pane keyboard activation with native Windows behavior.
  Native `uidbgtargets.cpp` enables list activation on Enter; SDL now uses the
  same selected-row activation helper for Enter and double-click.
- Aligned History pane "Go to Source" failure handling with native Windows
  behavior. When no exact source line exists for the selected history address,
  SDL now shows the native-style error dialog instead of silently ignoring the
  failed source lookup. The SDL common-dialog stubs now also implement the
  native `ATUIShowError()` entry points used by shared debugger UI code.
- Aligned History pane keyboard navigation with native `ATUIHistoryView`
  behavior for reviewed paths: `Ctrl+F` now focuses the search field,
  PageUp/PageDown move by the measured visible page size instead of a fixed
  20 lines, and End skips back to a visible line when search filtering hides
  the tree back node. Runtime review found that the SDL handler was still
  gated on the current ImGui window after rendering the child scroll area,
  so focused History keyboard shortcuts could be missed. It now uses the
  pane-level focus flag, matching the debugger focus model used by the other
  SDL panes.
- Exposed the native `IATUIDebuggerHistoryPane` interface from the SDL History
  pane through `ATGetUIPaneAs()`, matching
  `ATHistoryWindow::AsInterface()`. The SDL beam-position ping path now
  preserves native `console.cpp` behavior: return if the History pane interface
  is absent, otherwise activate History only when an existing pane cannot jump.
- Aligned two Disassembly pane behaviors with native `ATDisassemblyWindow`:
  printable characters typed in the disassembly view now focus the Console and
  seed its command input, and context-menu "Go to Source" now shows the
  native-style missing-source error when no exact source line exists.
- Fixed Disassembly breakpoint toggling after view rebuilds/run-stop cycles.
  Native `ATDisassemblyWindow::RemakeView()` and
  `OnDebuggerSystemStateUpdate()` move the text-editor cursor to the frame PC,
  PC, or focus line; SDL rebuilt the line list and scrolled to that line but
  left `mSelectedLine` at `-1`, making `Debug.ToggleBreakpoint`/F9 a no-op
  until the user clicked a line. SDL now selects the same line it scrolls to.
- Added native-style Disassembly target navigation for reviewed jump/call
  operand cases. SDL now retains decoded `kBM_Expand*` targets, renders target
  lines as navigable links, supports double-click and context-menu "Go to
  Target", and adds Back/Forward toolbar buttons backed by the same 64-entry
  circular history behavior as native `PushAndJump()` / `GoPrev()` /
  `GoNext()`.
- Added Disassembly call-preview expansion. SDL now toggles `[expand]` /
  `[contract]`, inserts nested disassembly rows from the decoded target, stops
  at a procedure-ending instruction, includes mixed source rows inside the
  nested block like native `Disassemble()`, and removes deeper nested rows on
  contract.
- Implemented the native Help > Export Debugger Help path for SDL. Re-review
  found that Windows `uidbghelp.cpp` was not covered in the initial analysis:
  SDL now exports an HTML page from the embedded debugger help resource,
  registers `Help.ExportDebuggerHelp`, and enables the ImGui/macOS Help menu
  item.
- Aligned debugger command-manager predicates with native `cmddebug.cpp` for
  reviewed commands. `Debug.Run` and Step Into/Over/Out now require the
  simulator to be not running, `Debug.Break` requires running or queued
  debugger commands, and New/Toggle Breakpoint require an open debugger.
  Open Source File and Source File List are no longer command-gated by debugger
  visibility; SDL opens the debugger dockspace first so source panes are
  visible. The macOS menu path now also enables Targets and Debug Display.
- Aligned Debug > Options menu ordering in the ImGui and macOS menu paths with
  native `menu_default.txt`: Auto-Reload ROMs, Randomize Memory On EXE Load,
  Break at EXE Run Address, separator, Change Font.

Verification:

- `cmake --build --preset linux-release -j$(nproc)` completed successfully.
- Scripted UI smoke test opened the debugger, waited for the docked Display
  pane to render through the backend callback, queried visible windows, and
  exited cleanly.
- Scripted UI smoke test invoked `Pane.PrinterOutput` while the debugger was
  closed; the debugger dockspace opened and the `Printer Output` pane was
  visible and focused.
- Scripted UI smoke test opened the debugger and invoked
  `Debug.OpenSourceFileList`; the centered `Source Files` modal appeared and
  focused successfully after the listing-map, missing-source prompt, and
  file-watcher changes.
- Listing parsing and listing-only address mapping have compile/build
  verification and focused runtime coverage through
  `source_query_mapping <hex_addr> <line_index> <utf8_path>`. Test-mode
  verification opened a MADS-style listing without symbols, confirmed exact
  `$2000` mapped to line 1, nearest `$2001` also mapped to line 1 by the native
  64-byte previous-line rule, line 2 mapped back to `$2002`, and out-of-range
  `$2100` returned no line.
- Listing-derived source-step range computation has compile/build verification
  and focused runtime coverage through
  `source_query_step_range <hex_pc> <utf8_path>`. Test-mode verification
  confirmed `$2000` produces range `$2001` length 1, `$2001` produces no range
  because `pc + 1` reaches the next mapped row, `$2002` produces range `$2003`
  length 66, `$2045` produces no range on the final row, and a `$3000` to
  `$3100` listing gap produces no range because it fails the native `< 100`
  byte cutoff.
- The missing-source prompt path now has non-OS runtime coverage through
  `source_missing_file <symbol_path> [resolved_path]`. Test-mode verification
  covered cancel/no-pane, selected-file opening, original symbol-path alias
  preservation, and UTF-8 alias round trips.
- Source file watcher reload has compile/build verification and focused
  runtime coverage through `source_open_file <utf8_path>`,
  `source_query_file <utf8_path>`, and `source_reload <selected_line|-1>
  <utf8_path>`. Test-mode verification opened a two-line source file, edited
  it externally to three lines, triggered the same `OnFileUpdated()` / watcher
  reload path, and confirmed the same pane reloaded from line count 2 to 3,
  changed the last line from `two` to `three reloaded`, and preserved selected
  line 1.
- Memory pane Add to Watch / Track On-Screen fixes have compile/build
  verification and focused non-OS runtime coverage. `memory_add_to_watch_expr`
  verified CPU byte formatting as `db $2000`, ANTIC/non-CPU byte formatting as
  `db $10002000`, and VBXE/non-CPU word formatting as `dw $20012345`, using
  the same expression builder as the context-menu UI. `memory_track_on_screen`
  verified that an empty core watch table adds at slot 0, `watch_fill` fills all
  eight core slots, a ninth Track On-Screen request returns
  `added:false, watch_index:-1, overflow:true`, and the watch count remains 8.
  The real native SDL message box for "Too many watches" is not opened during
  automation, but the UI path calls the same helper with dialog display enabled.
- Memory `CancelEdit()` selection-preservation parity has source-level
  verification against `uidbgmemory.cpp`, compile/build verification, and
  focused runtime coverage through `memory_hex_cancel`. Test-mode verification
  at CPU `$2000` started from `$5A`, staged `$A5`, canceled the edit, read back
  `$5A`, and reported `selected:"$2000",selection_enabled:true`.
- Memory edit entry points now enforce the native selected-address guard from
  `ATMemoryWindow`: `BeginEdit()`, `CommitEdit()`, and `CancelEdit()` return
  without state changes unless a selected highlighted address exists.
- Memory hex byte commit now has focused runtime coverage through
  `memory_hex_edit`: verification at CPU `$2000` wrote `$5A` through
  `BeginEdit()` / `CommitEdit()`, read back `$5A`, and then restored the byte
  to `$00`.
- Memory word/decimal/text commit semantics now have focused runtime coverage
  through `memory_value_edit` and `memory_text_edit`: verification wrote and read
  back hex word `$1234`, decimal word `4660` as `$1234`, decimal byte `200` as
  `$C8`, ATASCII `65` as `$41`, and Internal text `65` as `$21`, then restored
  all touched CPU bytes to `$00`.
- Memory highlight visibility is now wrap-aware for fixed-size address spaces.
  `memory_ensure_visible view=0x1000FFF0 highlight=0x10000000 columns=16
  rows=2` verified that an ANTIC-space wrapped visible address keeps
  `view_start:"$1000FFF0"` instead of scrolling to `$10000000`; non-visible
  highlights still scroll to `$10000010` or `$10000FF0` as appropriate. The CPU
  view was verified separately not to use this 64K wrap assumption because
  `kATAddressSpace_CPU` is a 24-bit CPU view.
- Memory keyboard navigation now routes through shared movement helpers, and
  PageUp top-clamp behavior matches native `ATMemoryWindow::OnViewScroll()` by
  preserving the intra-row address offset. Focused `memory_navigation` coverage
  verified PageUp from view `$00000003` keeps `$00000003`, PageUp/PageDown by a
  visible page, Ctrl+Up/Ctrl+Down one-row view scrolling, selected-address
  Left/Right/Up/Down movement, and Tab value/interpretation-column toggling.
- Memory typed edit auto-advance now routes through the same native-style
  selected-address movement helper as Enter and arrow navigation instead of
  assigning `mHighlightedAddress` directly. The real ATASCII/Internal keyboard
  path now stages `mEditValue` and calls `CommitEdit()` like native `WM_CHAR`
  handling instead of writing directly to target memory. Focused runtime
  coverage through `memory_hex_auto_advance` and `memory_text_auto_advance`
  verified hex byte `$5A` advances `$2000 -> $2001`, hex word `$1234` advances
  `$2000 -> $2002`, ATASCII `65` advances `$2000 -> $2001`, row-crossing byte
  edit `$200F -> $2010` keeps the two-row view at `$2000`, page-crossing byte
  edit `$201F -> $2020` scrolls the two-row view to `$2010`, and an ANTIC
  boundary edit at `$1000FFFF` keeps selection at `$1000FFFF` rather than
  advancing past the address-space limit. The ANTIC run is selection-boundary
  coverage; its readback byte is hardware/backend dependent.
- Scripted debugger-open smoke test still shows Memory 1 and Watch 1 panes
  after the memory context-menu changes.
- Watch pane printable-input, selected-row editing, Delete removal, and
  update-timing fixes have compile/build verification and focused pane-level
  runtime coverage. `watch_describe` verified the initial `<blank>` row state;
  `watch_edit 123` with no selected row returned `edited:false`, matching the
  native F2 requirement for a selected list row; `watch_printable_edit $ D40B`
  created `$D40B|<blank>`; `watch_printable_edit 1 23` created
  `123|<blank>`; `watch_edit 456` changed the selected row to
  `456|<blank>`; `watch_delete_selected` removed the selected row back to
  `<blank>`; and Delete on the trailing blank row returned `deleted:false`.
  The lower-level ImGui mouse/table click-to-focus path now also has focused
  runtime coverage: a real `click WatchTable ##sel` selected `$D40B`, `key
  delete` removed it through pane-level selected-row handling, and
  `watch_describe` returned `<blank>`.
- Watch pane typed interface lookup has compile/build verification and focused
  runtime coverage through `watch_interface [expr]`. Test-mode verification
  found Watch 1 through `IATUIDebuggerWatchPane::kTypeID` and added `$D40B`
  through `IATUIDebuggerWatchPane::AddWatch()`. The same runtime path was rerun
  after routing `ATUIDebuggerAddToWatch()` through the typed interface.
- Scripted debugger-open smoke test still shows Watch 1 after the watch editing
  changes.
- Breakpoints pane condition-validation and selected-row Delete behavior have
  compile/build verification. The shared submit helper now has focused runtime
  coverage through `breakpoint_submit`, `breakpoint_describe`,
  `breakpoint_delete`, `breakpoint_delete_selected`, and `breakpoint_count`:
  condition-only without a condition rejected with the native message, invalid
  conditions rejected, non-stopping breakpoints without command/trace rejected,
  condition-only breakpoints submitted and described as `Any insn when $01`,
  command+trace breakpoints round-tripped as `.printf "hit"; .echo_ok
  [trace]`, selected-row deletion through the Breakpoints pane removed user
  breakpoint ID 0 and left ID 1 intact, the keyboard Delete event deleted the
  focused pane's selected breakpoint ID 0 after `breakpoint_select 0`, and
  cleanup reduced the breakpoint count back to zero. Trace-message formatting has
  focused runtime coverage through `breakpoint_format_trace <trace_text>`:
  normal text produced `.printf "hello"`, percent characters were doubled,
  quotes were escaped, and empty trace text produced no command. Breakpoint row
  description formatting has focused runtime coverage through
  `breakpoint_format_description [oneshot=1] [clear=1] [trace=1]`, which
  produced `$2000 [one-shot] [trace] [clear-on-reset]` through the same
  formatter used by the pane. Breakpoint group sorting has source-level
  verification against native `uidbgbreakpoints.cpp`; native uses
  `std::sort(..., vdstringpredi())`, but `vdstringpredi` is an equality
  predicate, not a strict ordering predicate. SDL now intentionally uses a
  strict case-insensitive comparator for deterministic cross-platform order.
  Runtime coverage through `debugger_console bp -g zulu $2000`,
  `debugger_console bp -g Bravo $2001`, `debugger_console bp -g alpha $2002`,
  and `breakpoint_pane_order` verified `alpha.0|Bravo.0|zulu.0`, with cleanup
  returning the pane order to empty. Actual mouse context-menu Delete activation
  now has focused UI runtime coverage through a persistent test-mode connection:
  `right_click BPTable ##row` exposed the popup items `New Breakpoint...` and
  `Delete`, `click Popup Delete` removed breakpoint ID 0, `breakpoint_describe
  0` returned not found, and `breakpoint_count` returned zero. Real
  one-shot/clear-on-reset flag creation remains a console/core-command feature
  rather than a control in the native breakpoint dialog or SDL modal.
- Export Debugger Help has compile/build verification and non-OS runtime
  coverage through `export_debugger_help [utf8_path]`. Test-mode verification
  covered extensionless paths appending `.html`, explicit `.html` paths staying
  unchanged, empty/cancel producing no export, nonempty generated HTML, expected
  debugger-help markers, and identical output for extensionless and explicit
  paths resolving to the same generated page. Native SDL save dialogs, built-in
  fallback dialogs, and WebAssembly picker behavior still need manual/runtime
  validation.
- Debugger command predicates/source command reachability have source-level
  verification against `cmddebug.cpp`, compile/build verification, and a
  test-mode smoke confirming `Debug.OpenSourceFileList` opens the debugger
  dockspace and focuses the `Source Files` modal when invoked while the debugger
  is initially closed.
- A source-backed command inventory check found no missing native command IDs:
  all 28 `Debug.*` commands from native `cmddebug.cpp` and all 19 debugger
  `Pane.*` IDs exposed by native `menu_default.txt` are represented in the SDL
  command/menu implementation.
- Debug > Options menu ordering has source-level verification against
  `menu_default.txt` and compile/build verification.
- Source-mode Run, Step Into, Step Over, Step Out, and Toggle Breakpoint were
  re-reviewed against `ATSourceWindow::OnPaneCommand()` in `uidbgsource.cpp`.
  SDL's source pane now matches the reviewed native behavior: source-mode Run,
  source-range Step Into/Over using pane and symbol lookup maps, source-mode
  Step Out, and selected-line breakpoint toggling.
- Source-pane Toggle Breakpoint now has focused runtime coverage through
  `source_toggle_breakpoint <line_index> <utf8_path>`. Test-mode verification
  used a small MADS listing fixture, confirmed line 1 maps to `$2000`, invoked
  the real `OnPaneCommand(kATUIPaneCommandId_DebugToggleBreakpoint)` path, and
  observed breakpoint count changing 0 -> 1 and then 1 -> 0 on the second
  toggle.
- Added `query_command <command>` to SDL UI test mode. It reports whether a
  command exists, whether its `mpTestFn` currently enables execution, and its
  checked/radio state, so native-equivalent command predicates can be tested
  without executing the command.
- Runtime smoke with `--test-mode` verified the native-equivalent debugger
  command predicate matrix for `Debug.Run`, `Debug.Break`, Step Into/Over/Out,
  New Breakpoint, Toggle Breakpoint, Open Source File, and Source File List.
  Running/closed and running/open states enable only Break, New/Toggle
  Breakpoint when the debugger is open, and both source-file commands; paused
  states enable Run and all step commands, disable Break, enable source-file
  commands, and enable New/Toggle Breakpoint only when the debugger is open.
- SDL ImGui pane factory registration has compile/build verification and
  test-mode smoke coverage for debugger open, fixed panes (`Breakpoints`,
  `Targets`, `Debug Display`, `Profile View`, `Printer Output`), and indexed
  `Memory 4`/`Watch 4` activation. It still needs any future explicit
  class-style registration for indexed pane families if that dispatch is
  refactored.
- Added `click_label` / `right_click_label` and `query_window_label` to UI
  test mode so menu items and windows with spaces in their labels can be driven
  directly. Real ImGui menu-path coverage now verifies all Debug > Window pane
  entries: Console, Registers, Disassembly, Call Stack, History, Memory 1-4,
  Watch 1-4, Breakpoints, Targets, and Debug Display. Debug > Profile >
  Profile View is also covered. Each path opened the expected pane/window
  through menu clicks rather than command IDs.
- Profile View timeline parity has source-level verification against
  `ATUIProfilerTimelineView` / `ATUIProfilerPane::LoadProfile()` in native
  `profilerui.cpp`, compile/build verification, and a rebuilt test-mode smoke
  confirming `Pane.ProfileView` opens a focused `Profile View` pane through the
  command path. Focused interaction coverage is still pending for timeline drag
  selection, zoom, scroll, and profile-session data rendering.
- Profile View call-graph tree parity has source-level verification against
  native `ATUIProfileView::RemakeView()` and
  `ATUIProfileViewTreeSource::GetText()` in `profilerui.cpp`, plus compile/build
  verification. Focused runtime coverage is still pending for real call-graph
  sessions, tree expansion order, and source drill-down behavior.
- Profile View source drill-down has source-level verification against native
  `ATUIProfileView::OnItemDoubleClicked()`,
  `ATUIProfileView::OnTreeItemDoubleClicked()`, and
  `ATUIProfilerSourceTextView` in `profilerui.cpp`, plus compile/build
  verification. Test-mode smoke still only covers opening Profile View and the
  profiler toolbar path; focused runtime coverage is pending for row
  double-click, call-graph-node double-click, detailed disassembly contents,
  and detailed-view scrolling/header behavior.
- Profile View source-detail scrolling/header behavior has source-level
  verification against native `ATUIProfilerSourceTextView::OnVScroll()`,
  `ScrollLines()`, `OnSize()`, and `ATUIProfilerSourcePane` header handling,
  plus compile/build verification. Focused runtime coverage is still pending.
- Profile View flat-table selection/status parity has source-level verification
  against native `ATUIProfileView::OnItemSelectionChanged()` in
  `profilerui.cpp`, plus compile/build verification. Focused runtime coverage
  is still pending for click, Ctrl-toggle, Shift-range selection, and status
  text contents on real profile data.
- Debugger layout transition flushing has compile/build verification and a
  test-mode open/close/reopen smoke. The smoke verified that the debugger
  dockspace disappears on close, returns on reopen with Display visible, and
  reuses the saved dock layout instead of rebuilding the default layout.
- Call Stack native formatting/frame-count/selection parity has compile/build
  verification and a debugger-open smoke verifying the Call Stack pane still
  renders.
- Registers native text formatting and read-only selection/copy parity has
  compile/build verification and a debugger-open smoke verifying the Registers
  pane renders with the read-only multiline text widget.
- Targets selection/current-marker and Enter/double-click activation parity have
  compile/build verification. Test-mode coverage through `targets_activate 0`
  verified selected-row activation on the current target
  (`activated:true,current:0,count:1`), and `targets_activate 999` verified
  out-of-range handling. This run only had one registered debug target, so a
  multi-target fixture is still needed to prove switching from target 0 to a
  different device target.
- History missing-source feedback has source-level parity verification against
  `uidbghistory.cpp` / `uihistoryview.cpp`, compile/build verification, and
  focused runtime coverage. Test mode now records blocking SDL/native message
  boxes through `query_message_box`; `history_context go_source` on an
  instruction without source mapping returned `applied:false` and recorded
  `kind:"error"`, `title:"Altirra Error"`, and the native-style message
  `There is no source line associated with the address: $EB22.` without
  hanging the automation socket.
- History `Ctrl+F`, PageUp/PageDown, and End-key changes have source-level
  parity verification against `uihistoryview.cpp`, compile/build verification,
  debugger-open smoke coverage, and focused runtime coverage through
  `history_describe` plus expanded test-mode key injection. Runtime
  verification focused History, confirmed `PageUp` moved the selected history
  index by the measured page size, confirmed `Ctrl+F` accepted text into the
  search box and Enter activated `search_active=1 search_text="lda"`, then
  confirmed filtered `End` selected the last visible matching line and
  filtered `PageUp` moved back by visible lines.
- History context-menu action behavior now has shared helper coverage through
  `history_context` and `history_select`. The ImGui popup and the test seam use
  the same selected-line capture and action helpers for Go to Source,
  timestamp mode changes, Set Timestamp Origin, and Reset Timestamp Origin.
  Runtime verification selected a real instruction, changed to cycle mode,
  set the timestamp origin and observed the selected line become `T+0`, changed
  to microseconds mode, and reset the origin back to the default base.
- History Go to Source success now has focused runtime coverage using a real
  debugger source mapping. The test selected a real History instruction,
  generated a temporary source file plus a minimal MADS-style listing mapping
  that selected address, loaded it through the real `.loadsym` debugger command,
  invoked `history_context go_source`, and observed `applied:true`. A
  subsequent `source_query_file` confirmed the Source pane opened the mapped
  file with three lines and focused zero-based `selected_line:1`, matching
  native `ATConsoleShowSource()` line-2-to-index-1 behavior. The message-box
  recorder remained empty, proving the success path did not fall through to
  the missing-source error dialog.
- History real popup activation now has focused runtime coverage through
  `history_open_context`. Runtime verification selected a real instruction,
  opened the same `HistCtx` ImGui popup path used by right-click, observed an
  active popup window containing native History menu items including
  `Go to Source`, display toggles, collapse toggles, `Reset Timestamp Origin`,
  and `Set Timestamp Origin`, and clicked `Set Timestamp Origin` through the
  popup item path.
- History horizontal scrolling now has focused runtime coverage through
  `history_hscroll` plus `history_describe`. The check verifies the actual
  ImGui child scrollbar state used by the rendered History pane: a positive
  request clamps to the measured right edge, a negative request clamps to zero,
  and a very large request clamps back to the same measured right edge. This
  covers SDL's equivalent of native `ATUIHistoryView::HScrollToPixel()`
  clamping behavior for the current ImGui implementation.
- History typed-interface lookup has source-level parity verification against
  native `ATHistoryWindow::AsInterface()`, compile/build verification, and
  focused runtime coverage through `history_interface`, which found History via
  `IATUIDebuggerHistoryPane::kTypeID`.
- History beam-position ping routing has source-level verification against
  native `ATConsolePingBeamPosition()` in `console.cpp`: the SDL path now uses
  `IATUIDebuggerHistoryPane` directly and preserves the native absent-pane
  early return.
- Disassembly printable-key forwarding and source feedback have source-level
  parity verification against `uidbgdisasm.cpp`, compile/build verification,
  and focused test-mode regressions. The harness now exposes `send_text
  <utf8>`, `query_debugger_focus`, and `query_console_input`; with Disassembly
  focused (`pane_id: 5`), `send_text abc` moved focus to Console (`pane_id: 2`)
  and `query_console_input` returned `"abc"`. During review, these commands
  also exposed and fixed a Disassembly focus-condition bug where the keyboard
  handler used the current child window focus instead of pane-level focus. SDL
  now handles printable text at the `SDL_EVENT_TEXT_INPUT` layer for focused
  Disassembly, which better matches native `WM_CHAR` forwarding than relying
  only on ImGui's per-frame character queue.
- Disassembly context-menu Go to Source behavior now has focused runtime
  coverage through `disasm_context go_source`. The test seam uses the same
  selected-line address and shared helper as the real popup path. With no
  source mapping, it returned `applied:false` for `$0000` and
  `query_message_box` recorded the native-style `Altirra Error` message. After
  loading a temporary source file plus MADS-style listing for `$0000` through
  the real `.loadsym` command, the same command returned `applied:true`,
  `source_query_file` confirmed the Source pane opened with `selected_line:1`,
  and the message-box recorder remained empty.
- Disassembly selected-line breakpoint toggling after rebuild has source-level
  parity verification against native `ATDisassemblyWindow::RemakeView()` /
  `OnDebuggerSystemStateUpdate()` cursor movement, compile/build verification,
  and focused runtime coverage through
  `disasm_selected_breakpoint [query|toggle]` and
  `disasm_breakpoint_runstop_regression`. Test-mode verification paused the
  emulator, opened the debugger/disassembly pane, confirmed a valid selected
  line at `$0000`, toggled it on through the pane command path, simulated a
  running then stopped system-state update at `$0000`, confirmed the rebuilt
  pane still selected line 0 with `has_breakpoint:true`, toggled it off through
  the same command path, and confirmed `breakpoint_count` returned 0 afterward.
- Disassembly target navigation and Back/Forward history have source-level
  parity verification against native `uidbgdisasm.cpp`, build verification,
  and focused runtime coverage through `disasm_target_nav`. The test seeds a
  deterministic `JSR $2010` at `$2000`, follows the decoded target through the
  same `PushAndJump()` path used by double-click and context-menu "Go to
  Target", then exercises the same `GoPrev()` / `GoNext()` methods used by the
  Back/Forward toolbar buttons. Runtime verification observed `$2000 -> $2010`,
  Back returning to `$2000`, Forward returning to `$2010`, and history counters
  changing from back=1/forward=0 to back=0/forward=1 and back=1/forward=0.
- Disassembly `[expand]` / `[contract]` call-preview insertion has source-level
  parity verification against native `uidbgdisasm.cpp`, build verification, and
  focused runtime coverage through `disasm_preview`. The test seeds a
  deterministic `JSR $2010` at `$2000`; without symbols, expansion reports two
  nested instruction rows, zero nested source rows, `[contract]` after expand,
  and the original line count after contract. After loading a temporary
  MADS-style listing for `$2010` and opening the mapped source file, the same
  command reports two nested instruction rows plus one nested source row,
  confirming that expanded preview blocks now share native mixed-source
  insertion behavior.
- The SDL `IATDisplayPane` adapter has compile/build verification. A test-mode
  smoke run also verified that opening the debugger still shows the docked
  Display pane after the adapter was added. It still needs focused runtime
  coverage for text selection copy, frame capture/save, auto-suggest trigger,
  and rendered-frame callback behavior.
- The debugger Display pane global zoom/pan destination math has compile/build
  verification and a test-mode smoke run showing the docked Display pane still
  opens. Backend-level clipped rendering also has compile/build verification and
  a test-mode smoke run showing the docked Display pane still opens through the
  clipped backend callback. The Display pane still needs visual regression
  coverage for zoomed/panned asymmetric frame content.
- Display-pane video-writer attachment and motion-vector overlay have
  source-level verification against native `uifrontend.cpp` and
  `uivideodisplaywindow.cpp`, plus build verification. Focused runtime coverage
  is still pending for the overlay because it requires a ZMBV recording with
  `recording.video.show_motion_vectors` enabled.
- Debug Display Bicubic filtering and unconditional system-state refresh have
  source-level verification against native `uidbgdebugdisplay.cpp`, build
  verification, and focused runtime state coverage through `debug_display`.
  Test-mode verification covered the default state
  `mode=0,palette=0,filter=2,dl=-1,pf=-1`; valid DL `0x2000` switching to
  history-start mode and setting `dl=8192`; valid PF `0x2400` setting
  `pf=9216`; invalid DL/PF `0x10000` returning `applied:false` while preserving
  state; filter `3` selecting Bicubic; palette `1` selecting Analysis; and
  invalid filter/palette values preserving state. Focused visual coverage is
  still pending for point, bilinear, and bicubic comparison against the native
  display child window.

## Parity-critical remaining work

- Complete the remaining Display debugger pane parity: visual regression
  coverage with asymmetric zoomed/panned frame content, and enhanced-text
  replacement once SDL has an equivalent backing system. The pane now uses the
  backend render path, applies global zoom/pan destination math, clips output
  to the docked viewport, and exposes a typed display-pane adapter for
  implemented SDL-equivalent behavior.
- WebAssembly debugger file requests now register the same callback and return
  the native abort/empty-path contract with an explicit log when no synchronous
  picker is available. Real browser file selection still needs a separate
  async/VFS-aware command flow if it is required.
- Debugger file requests now have a non-OS test seam:
  `debugger_request_file <open|save> [utf8_path]` invokes the same callback with
  an override path. Test-mode verification covered open, save, empty/cancel,
  and UTF-8 path round trips; the native desktop OS dialog still needs manual
  validation.
- Finish Debug Display's child display-stack parity and focused visual/runtime
  coverage. The pane now exposes native mode/filter controls, validates
  addresses, and has a real Bicubic resampling path, but it still uses an ImGui
  texture conversion path instead of a native-style child display window.
- Replace the remaining centralized indexed Memory/Watch dispatch with explicit
  SDL class-style pane factories if it becomes a source of drift. Raw native
  `ATUIPane*` factories remain incompatible with ImGui panes without an adapter.
- Add debugger close/reopen layout persistence coverage. SDL now explicitly
  flushes ImGui layout settings on debugger transitions, but exact native
  `ATSavePaneLayout("Standard"/"Debugger")` serialization is not implemented
  because ImGui dock persistence already keys debugger layout by dockspace and
  window IDs.
- Active-pane command dispatch now covers the native-equivalent overrides found
  in code: Source handles Run, Toggle Breakpoint, and source-mode stepping, and
  Disassembly handles Toggle Breakpoint. Native Memory does not override
  `OnPaneCommand()`, so its remaining work is edit/scroll/watch behavior
  validation rather than active-pane command dispatch.
- Complete focused Memory pane validation for edit/scroll/wrap behavior.
  Non-CPU Add-to-Watch expression formatting, Track On-Screen overflow, edit
  method selection guards, hex byte commit/cancel behavior, word/decimal/text
  commit behavior, selected-address movement, native-style page-scroll offset
  preservation, typed edit auto-advance including row/page crossing, and
  wrap-aware visible-highlight handling now have source-level and/or non-OS
  runtime coverage; real key-event/focus behavior and the complete
  address-space boundary matrix still need focused validation.
- Breakpoints pane parity-critical validation is covered for condition
  validation, condition-only breakpoints, non-stopping validation,
  command/trace round-tripping, selected-row deletion through the pane,
  Delete-key deletion, context-menu Delete, mixed-case group ordering, basic
  delete/count, trace-message formatting, and annotation description formatting.
- Add native save-dialog coverage for `Help.ExportDebuggerHelp`.
- Command-manager predicate coverage is now complete for the reviewed debugger
  command matrix across running/stopped and debugger-open/debugger-closed states.
  Menu-path UI coverage now covers every Debug > Window pane entry and Debug >
  Profile > Profile View; remaining command work is coverage for other
  debugger-related menu entries outside Debug > Window/Profile and any unreviewed
  command surface discovered by later native comparison.
- Complete native source parity: async WebAssembly/forced built-in dialog
  handling for user-driven missing-source prompts, focused source-mode command
  execution coverage for symbol-derived mappings, and any remaining
  native-equivalent external open/show integration differences remain
  incomplete. Listing parsing,
  module/open-source-directory search, path alias persistence, native-desktop
  missing-source prompts, file watcher reload, exact source-line lookup, and
  Source File List filtering/sorting are now implemented. Source Run,
  Step Into, Step Over, and Step Out now have listing-derived runtime
  command-path coverage through `source_command`; Toggle Breakpoint has
  listing-derived runtime command-path coverage through
  `source_toggle_breakpoint`.
- Add focused source-mode execution tests for Run, Step Into, Step Over, and
  Step Out on symbol-derived mappings.
- Add a multi-target Targets pane fixture with at least one device debug target
  so `targets_activate <row>` can prove a real current-target index change, not
  only selected-row activation on the sole default target.
- Complete Profile View parity beyond the newly functional pane, timeline,
  call-graph tree, flat-table selection/status, and source-detail view by adding
  focused timeline / call-graph / profile-selection / source-detail interaction
  tests.
- Implement real pane pointer/adaptor support for APIs returning `ATUIPane*`
  (`ATGetUIPane`, `ATUIGetActivePane`) if needed by remaining shared code.
  Interface lookup through `ATGetUIPaneAs`/`ATUIGetActivePaneAs` now works for
  debugger pane interfaces that SDL exposes, but raw `ATUIPane*` remains
  intentionally null because ImGui panes do not derive from native panes.

## Intentional SDL additions to preserve

- Default debugger layout opens Memory 1, Watch 1, and Call Stack in addition
  to the native default panes. This is an SDL quality-of-life addition and is
  documented in `ATUIDebuggerOpen()`.
- SDL command registration includes command-manager paths for menu, shortcut,
  command-palette, and custom-device VM dispatch even where the ImGui menu
  can call functionality directly.
- SDL keeps the Performance Analyzer/trace viewer as an SDL debugger tool pane
  using `kATUIPaneId_PerformanceAnalyzerSDL`; this preserves the useful SDL
  analyzer while restoring native `Pane.ProfileView` semantics.
- SDL routes the native Change Font command to the existing Configure System >
  Fonts page instead of duplicating a Windows-style modal chooser. The setting
  still controls the debugger monospace font.
- History includes SDL UI conveniences that are not native behavior: an
  in-pane "Enable CPU History" button when tracking is disabled, and an
  always-visible ImGui search row instead of a transient Win32 search edit.
- `Pane.Display` remains debugger-gated in SDL because it only focuses the
  dockable Display pane inside the ImGui debugger dockspace. The normal emulator
  display outside debugger mode is not a native-style pane.
- `Pane.PrinterOutput` opens the debugger dockspace when invoked while closed,
  preserving command/menu reachability until SDL has native-style normal pane
  windows.
