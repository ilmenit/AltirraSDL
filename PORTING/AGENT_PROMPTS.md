# Parallel Work Stream Agent Prompts

Each prompt below is self-contained for an AI coding agent. All share these rules:

- **Read CLAUDE.md** at repo root before starting — it has mandatory coding rules.
- **Read the Windows implementation first** — the `.rc` file and Windows `ui*.cpp` source are the specification.
- **Match Windows Altirra exactly** — same options, same labels, same behavior.
- **No stubs, no placeholders** — implement fully or leave out of scope items clearly marked.
- **Split large implementations into multiple files** — e.g., a new pane in `ui_dbg_foo.cpp` with helpers in `ui_dbg_foo_helpers.cpp`, or a new tool in `ui_tool_foo.cpp` + `ui_tool_foo_data.cpp`. Aim for files under ~600 lines. Follow the existing pattern of `ui_devconfig.cpp` (dispatch) + `ui_devconfig_devices.cpp` (per-device implementations).
- **Use existing SDL3/ImGui patterns** — look at nearby files in `src/AltirraSDL/source/` for context menu, dialog, slider, and pane patterns.
- **Build and test** — run `cd build && cmake --build . -j$(nproc)` after changes. Fix all warnings.

---

## A1 — Disassembly Context Menu

**File to modify:** `src/AltirraSDL/source/ui_dbg_disassembly.cpp`
**Windows reference:** `src/Altirra/source/uidbgdisasm.cpp` (search for `ID_CONTEXT_SHOWCODEBYTES`)
**RC reference:** `src/Altirra/res/Altirra.rc` — `IDR_DISASM_CONTEXT_MENU`

**Task:** Add 14 missing context menu items to the disassembly pane.

The existing context menu (around line 336) has 4 items: Go to Source, Set Next Statement, Show Next Statement, Toggle Breakpoint. You need to add after Toggle Breakpoint:

1. **Display toggles** (each is a bool member + `ImGui::MenuItem("Label", nullptr, &mbFlag)`):
   - "Show Code Bytes" — toggle `mbShowCodeBytes`, triggers rebuild
   - "Show Labels" — toggle `mbShowLabels`, triggers rebuild
   - "Show Label Namespaces" — toggle `mbShowLabelNamespaces`, triggers rebuild (only enabled when Show Labels is on)
   - "Show Procedure Breaks" — toggle `mbShowProcedureBreaks`, triggers rebuild
   - "Show Call Previews" — toggle `mbShowCallPreviews`, triggers rebuild
   - "Show Mixed Source/Disasm" — toggle `mbShowSourceInDisasm`, triggers rebuild

2. **65C816 M/X mode submenu** (only shown when CPU mode is 65C816). Use `ImGui::BeginMenu("65C816 M/X Mode")`:
   - "Auto M/X" (radio)
   - "Current M/X Context" (radio)
   - "M8, X8" / "M8, X16" / "M16, X8" / "M16, X16" (radio)
   - "Emulation" (radio)
   Store as an enum member. When changed, triggers rebuild.

3. **"Use DP Register State"** — toggle bool, affects how direct-page addresses are resolved.

**Implementation details:**
- Each toggle needs a `bool mb*` member variable on the pane class (initialized to match Windows defaults — check `uidbgdisasm.cpp` constructor).
- The checked state shows via the 3rd param of `ImGui::MenuItem("Label", nullptr, mbFlag)`.
- After toggling, set `mbNeedsRebuild = true` to refresh the disassembly.
- The actual rendering of code bytes, labels, etc. in the instruction lines requires modifying the disassembly line rendering loop — read how Windows `RemakeView()` uses these flags to format `ATDisassembleInsn()` output.
- Use `ATDisassembleInsn()` with appropriate flags from `at/atdebugger/target.h`.

**Verify:** Open debugger, right-click in disassembly. All 18 items appear. Toggle each — display changes accordingly. 65C816 submenu only visible with 65C816 CPU.

---

## A3 — History Pane Context Menu

**File to modify:** `src/AltirraSDL/source/ui_dbg_history.cpp` (~299 lines)
**Windows reference:** `src/Altirra/source/uihistoryview.cpp` (lines 561-696)
**RC reference:** `IDR_HISTORY_CONTEXT_MENU` in Altirra.rc

**Task:** Add a full context menu to the history pane (currently has zero context menu). This is a substantial enhancement — the history pane needs 20 new context menu items that control how history entries are displayed and what columns are visible.

**Context menu items (grouped):**

1. **Go to Source** — enabled only when a valid instruction line is right-clicked. Calls `ATImGuiConsoleShowSource(addr)`.

2. **Show toggles** (8 bools, each triggers rebuild):
   - "Show PC Address" — show/hide PC column
   - "Show Global PC Address" — show full bank:addr (only enabled when Show PC Address is on)
   - "Show Registers" — show/hide A/X/Y/S/P columns
   - "Show Special Registers" — show extended regs (only enabled when Show Registers is on)
   - "Show Flags" — show/hide flag letters (NV-BDIZC)
   - "Show Code Bytes" — show raw instruction bytes
   - "Show Labels" — show symbolic labels
   - "Show Label Namespaces" — show namespace prefixes (only enabled when Show Labels is on)

3. **Collapse toggles** (3 bools):
   - "Collapse Loops" — collapse repeated instruction sequences
   - "Collapse Calls" — collapse JSR...RTS as single line
   - "Collapse Interrupts" — collapse IRQ/NMI handlers

4. **Timestamp format** (radio group — mutually exclusive enum):
   - Separator before this group
   - "Reset Timestamp Origin" — resets to default
   - "Set Timestamp Origin" — sets clicked entry as origin (enabled only when instruction line clicked)
   - Separator
   - "Show Beam Position" / "Show Microseconds" / "Show Cycles" / "Show Unhalted Cycles" / "Show Tape Position (Samples)" / "Show Tape Position (Seconds)" — radio items

5. **Copy Visible to Clipboard** — copies all visible history lines to system clipboard via `SDL_SetClipboardText()`.

**Implementation details:**
- Add bool members for all toggles with defaults matching Windows (read the constructor in `uihistoryview.cpp`).
- The table columns in the render loop must conditionally show/hide based on these flags.
- For collapse modes, you'll need to examine how `IATDebugTargetHistory::GetLineInfo()` works and skip collapsed entries.
- For timestamp modes, use the cycle count from each history entry and format according to the selected mode. Windows uses `ATGetDebugger()->GetFrameTimingBase()` for beam position conversion.
- Right-click detection: set a context address/line on right-click, then `ImGui::OpenPopup("HistCtx")`.
- This will likely grow the file significantly. Consider splitting into `ui_dbg_history.cpp` (render + context menu) and `ui_dbg_history_format.cpp` (timestamp formatting, line rendering helpers, copy-to-clipboard).

**Verify:** Open debugger, run some code, open History pane. Right-click shows full menu. Toggle display options — columns appear/disappear. Change timestamp mode — timestamps reformat. Copy Visible works.

---

## A4 — Console Context Menu: Copy

**File to modify:** `src/AltirraSDL/source/ui_dbg_console.cpp`
**Windows reference:** `IDR_DEBUGGER_MENU` in Altirra.rc

**Task:** Add "Copy" to the existing console context menu (which currently only has "Clear All").

After `if (ImGui::MenuItem("Clear All"))`:
- Add separator
- Add `if (ImGui::MenuItem("Copy"))` — copies the entire console output text buffer to system clipboard via `SDL_SetClipboardText()`.
- Optionally add "Select All" if ImGui text selection is feasible.

**Verify:** Right-click in console output → Copy works, text appears in system clipboard.

---

## A5 — Debug Display Context Menu

**File to modify:** `src/AltirraSDL/source/ui_dbg_debugdisplay.cpp`
**Windows reference:** `IDR_DEBUGDISPLAY_CONTEXT_MENU` in Altirra.rc
**Windows source:** `src/Altirra/source/uidbgdisplay.cpp`

**Task:** Add 3 missing context menu items. The pane currently has no context menu.

1. **"Force Update"** — forces a re-render of the debug display visualization regardless of emulation state. Calls the pane's rebuild/refresh method.
2. **"Current Register Values"** (radio) — palette mode showing current GTIA color register values
3. **"Analysis Palette"** (radio) — palette mode showing analysis-specific coloring

**Implementation:** Add right-click detection in the render method. Open popup. Use radio-style menu items for palette modes. Store mode as enum member.

**Verify:** Right-click in debug display. Force Update refreshes. Palette mode switches change visualization colors.

---

## A6 — Source Pane Context Menu Additions

**File to modify:** `src/AltirraSDL/source/ui_dbg_source.cpp`
**Windows reference:** `IDR_SOURCE_CONTEXT_MENU` in Altirra.rc

**Task:** Add 2 missing items to the existing source pane context menu (which has Go to Disassembly, Set Next Statement, Toggle Breakpoint, Show Next Statement).

1. **"Open in File Explorer"** — opens the parent directory of the source file in the system file manager. Use `SDL_OpenURL()` with a `file://` URL pointing to the directory, or use `xdg-open` on Linux via `system()`.
2. **"Open in Default Editor"** — opens the source file in the system default text editor. Use `SDL_OpenURL()` with a `file://` URL to the file, or `xdg-open <path>`.

Both should be enabled only when a valid file path is loaded (`!mPath.empty()`).

**Verify:** Right-click in source view. "Open in File Explorer" opens the folder. "Open in Default Editor" opens the file in the user's editor.

---

## B1 — Disk Explorer Context Menu & Options

**File to modify:** `src/AltirraSDL/source/ui_tools.cpp` (Disk Explorer section, around line 1060-1280)
**Windows reference:** `src/Altirra/source/uidiskexplorer.cpp` — search for `ID_DISKEXP_IMPORTFILE`, `ID_DISKEXP_IMPORTTEXT`, `ID_NAMECHECKING_STRICT`
**RC reference:** `IDR_DISK_EXPLORER_CONTEXT_MENU` and `IDR_DISKEXPLORER_MENU` in Altirra.rc

**Task:** Add missing context menu items and the Options menu to the Disk Explorer.

**Context menu additions** (add to existing `BeginPopupContextItem()` at line ~1189):

1. **"View"** — if file, load into viewer pane (call `LoadFileView()`); if directory, navigate into it.
2. **"New Folder"** — create directory at current location. Use `pFS->CreateDir()`. Show inline rename popup for the new name.
3. **"Import File..."** — open SDL file dialog, read host file, write to disk via `pFS->WriteFile()`. Apply 8.3 filename normalization. Use existing collision detection.
4. **"Import File as Text..."** — same as Import File but convert CR/LF (0x0D/0x0A) to Atari EOL (0x9B) during write.
5. **"Export File as Text..."** — same as existing Export but convert Atari EOL (0x9B) to host CR/LF during read.
6. **Partition items** (enabled only when `pFS->SupportsPartitions()`):
   - "Open" — navigate into partition via `pFS->OpenPartition()`
   - "Import Disk Image..." — import entire disk image into partition
   - "Export Disk Image..." — export partition contents as disk image

**Options menu** — add a menu bar to the Disk Explorer window via `ImGui::BeginMenuBar()`:

1. **"Options" menu** containing:
   - "Strict" (radio) — strict 8.3 filename validation
   - "Relaxed" (radio) — allow longer/mixed case names
   - Separator
   - "Adjust Conflicting Filenames" (checkbox toggle) — auto-rename files that collide

Store these settings in `g_diskExplorer` state struct. Apply during import operations.

**Text EOL conversion helper** — create a small helper function:
```cpp
static void ConvertHostToAtari(vdfastvector<uint8>& data); // CR/LF -> 0x9B
static void ConvertAtariToHost(vdfastvector<uint8>& data); // 0x9B -> CR/LF
```

**Code organization:** If the disk explorer section of `ui_tools.cpp` grows beyond ~600 lines, extract it into a separate `ui_tool_diskexplorer.cpp` file with its own header declaring the render function and state struct.

**Verify:** Open Disk Explorer with a disk image. Right-click shows all items with correct enable states. Import/Export as Text converts line endings correctly. New Folder creates a directory. Options menu toggles persist and affect import behavior.

---

## B2 — Disk Drives Context Menu

**File to modify:** `src/AltirraSDL/source/ui_disk.cpp`
**Windows reference:** `src/Altirra/source/uidisk.cpp` (lines 1128-1456)
**RC reference:** `IDR_DISK_CONTEXT_MENU` in Altirra.rc

**Task:** Add ~30 missing context menu items to the disk drives "+" popup (currently has 5: New Disk, Save, Save As, Revert, Eject).

**Items to add** (after existing items, with separators between groups):

1. **Disk operations:**
   - "Explore Disk..." — open Disk Explorer for this drive's mounted image. Set `state.showDiskExplorer = true` with the drive's disk image.
   - "Revert Disk" (already exists)
   - "Show Disk Image File" — open the parent directory of the disk image file in system file manager via `SDL_OpenURL("file://...")` or `xdg-open`.

2. **Drive operations:**
   - "Swap With Another Drive" → submenu listing other drives (D1-D8/D15). Calls `sim.SwapDiskDrives(fromIdx, toIdx)`.
   - "Shift To Another Drive" → submenu listing other drives. Calls `sim.RotateDiskDrive(fromIdx, toIdx)`.

3. **Change Interleave** → submenu with 14 options (table-driven):
   ```
   Default, 1:1, 12:1 (810 rev. B SD), 9:1 (SD), 9:1 (SD improved),
   5:1 (US Doubler SD fast), 4:1 (Indus GT SuperSynchromesh),
   2:1 (SD), 13:1 (ED), 12:1 (ED improved), 16:1 (815 DD),
   15:1 (XF551 DD), 9:1 (XF551 DD fast), 7:1 (US Doubler DD fast)
   ```
   Each calls `di.SetInterleave(value)` on the disk interface. Use a static table of `{label, interleave_value}` pairs.

4. **Convert To Filesystem** → submenu with 8 options (table-driven):
   ```
   DOS 1.0 (SD), DOS 2.0S/2.5 (SD/ED), DOS 2.0D (DD),
   MyDOS (SD), MyDOS (DD), SpartaDOS (SD/ED), SpartaDOS (DD), SpartaDOS (512)
   ```
   Each calls the appropriate `ATDiskFormatDOS*()` / `ATDiskFormatMyDOS()` / `ATDiskFormatSpartaDOS()` function.

5. **Virtual disk operations:**
   - "Mount Folder as Virtual DOS 2 Disk..." — SDL folder picker → `ATDiskMountVirtualFolder()` with DOS2 format.
   - "Mount Folder as Virtual SpartaDOS Disk..." — same, SDFS format.
   - "Extract Boot Sectors for Virtual DOS 2 Disk..." — save 3×128 bytes via file dialog.
   - "Recursively Expand .ARChive Files..." — `ATDiskExpandARCFiles()` on mounted image.

**Code organization:** The context menu will be large. Extract it into a helper function `RenderDiskDriveContextMenu(int driveIdx, ATDiskInterface& di, ...)` — potentially in a separate `ui_disk_context.cpp` file if `ui_disk.cpp` grows too large.

**Enable states:** Each item needs correct enable/disable. Interleave/Convert/Explore need a loaded disk. Mount folder needs an empty drive. Check Windows `uidisk.cpp` for exact conditions.

**Verify:** Mount a disk image. Click "+" on a drive. All items appear with correct enable states. Interleave changes apply. Filesystem conversion works (test with a blank disk). Swap/shift moves images between drives.

---

## C1 — Adjust Colors Dialog Completion

**File to modify:** `src/AltirraSDL/source/ui_display.cpp`
**Windows reference:** `src/Altirra/source/uicolors.cpp` — `ATAdjustColorsDialog`
**RC reference:** `IDD_ADJUST_COLORS` and `IDR_ADJUSTCOLORS_MENU` in Altirra.rc

**Task:** Complete the Adjust Colors dialog with missing controls and menu items.

**Missing controls:**
1. **Palette preview** — draw a 16x16 grid of color swatches showing the current 256-color palette. Get palette via `sim.GetGTIA().GetPalette(pal)`. Render each color as a small filled rectangle using `ImGui::GetWindowDrawList()->AddRectFilled()`.

2. **Hue Step slider** — controls the angular step between palette hue entries. Read the Windows slider range from `uicolors.cpp`.

3. **Gamma Correction slider** — separate from the existing Gamma slider. Check Windows for range (50-260, mapped to 0.50-2.60).

**Missing menu bar** (add `ImGuiWindowFlags_MenuBar` to the Begin flags, then `ImGui::BeginMenuBar()`):

1. **File menu:**
   - "Export Palette..." — save 768-byte RGB file (256 colors × 3 bytes). Get palette from GTIA, write R,G,B for each entry. Use SDL save file dialog.
   - "Palette Solver..." — opens the palette solver sub-dialog (see below).

2. **View menu:**
   - "Show Red/Green Shifts" (toggle) — shows color shift visualization
   - "Show Red/Green Relative Offsets" (toggle) — shows relative offset visualization

3. **Options menu:**
   - "Shared NTSC/PAL Settings" (radio) — single palette for both
   - "Separate NTSC and PAL Settings" (radio) — independent palettes per standard
   - "Use PAL Quirks" (checkbox) — toggles PAL quirk emulation

**Palette Solver** — this is a complex sub-dialog. Create it in a new file `ui_palette_solver.cpp`:
- "Load Reference Picture..." — load a screenshot/image with known palette
- "Lock Hue Start" / "Lock Gamma Correction" checkboxes
- "Normalize Contrast" checkbox
- "Gain" slider with "Reset Gain" button
- "Match" button — runs `ATColorPaletteSolver` (genetic algorithm in `src/Altirra/source/palettesolver.cpp`). The solver iterates until convergence, updating params progressively. Run iterations per frame (non-blocking).
- Display current error metric and progress.

**Code organization:** The Adjust Colors dialog is already in `ui_display.cpp` (~281 lines). The palette solver should be a separate file `ui_palette_solver.cpp`. If the menu bar and palette preview grow `ui_display.cpp` significantly, consider extracting `ui_adjust_colors.cpp`.

**Verify:** Open Adjust Colors. Palette preview shows 256 colors updating live as sliders change. Export Palette saves correct binary. Palette Solver loads a reference and converges to matching params.

---

## C2 — Screen Effects Bloom & Labels

**File to modify:** `src/AltirraSDL/source/ui_screenfx.cpp`
**Windows reference:** `IDD_ADJUST_SCREENFX_BLOOM` in Altirra.rc

**Task:** Add missing label controls to the Bloom tab of the Adjust Screen Effects dialog.

Currently the Bloom tab has enable checkbox and threshold. Add:

1. **"Radius" label + slider** — logarithmic slider for bloom radius. Use the existing `SliderFloatLog()` helper pattern. Range matches Windows: ~0.1 to 10.0.
2. **"Direct intensity" label + slider** — linear slider for direct bloom intensity. Range: 0.0 to 2.0.
3. **"Indirect intensity" label + slider** — linear slider for indirect bloom intensity. Range: 0.0 to 2.0.

Read the bloom params from `VDDBloomV2Params` (see `src/h/vd2/VDDisplay/display.h`). When changed, update via the backend's screen effects sync path.

Also wire `mbBloomScanlineCompensation` — add a checkbox "Scanline compensation" that sets this param. The backend already has the uniform but it's not being set from the UI.

**Verify:** Open Adjust Screen Effects → Bloom tab. All 4 controls visible. Adjusting radius/intensity changes the bloom effect visually. Scanline compensation checkbox toggles compensation.

---

## D1 — Breakpoint Editor Dialog

**New file:** `src/AltirraSDL/source/ui_dbg_breakpoint_edit.cpp`
**Windows reference:** `src/Altirra/source/uidbgbreakpoints.cpp` — `ATUIDebuggerBreakpointDialog`
**RC reference:** `IDD_DEBUG_BREAKPOINT` in Altirra.rc

**Task:** Implement the Add/Edit Breakpoint dialog. Currently the breakpoints pane lists breakpoints but has no UI for creating or editing them.

**Dialog fields:**

1. **Location section:**
   - "Break on PC address" radio — break when PC reaches address
   - "Break on memory read" radio — break on memory read at address
   - "Break on memory write" radio — break on memory write at address
   - Address input field — hex address (e.g., "4000")

2. **Condition section:**
   - "Enable only on condition" checkbox
   - Condition expression input — parsed via `ATDebuggerParseExpression()`. Show parse error inline if invalid.

3. **Action section:**
   - "Stop execution" checkbox (default on)
   - "Print a message" checkbox — enables trace message field
   - Trace message input field (ASCII only, 0x20-0x7E)

4. **OK / Cancel buttons** — OK validates, creates breakpoint via `IATDebugger`, closes dialog. Cancel discards.

**Integration with breakpoints pane:** Add "Add..." and "Edit..." buttons to `ui_dbg_breakpoints.cpp` toolbar area. Double-click on a breakpoint row opens the editor with pre-populated fields.

**Validation rules** (from Windows):
- Non-stopping breakpoints require command or trace message
- Condition-only breakpoints need an instruction breakpoint type
- Invalid expressions show inline error

**Code organization:** Keep the dialog in its own file. It's a modal popup opened from the breakpoints pane. ~200-300 lines.

**Verify:** Open Breakpoints pane. Click "Add...". Fill in PC address breakpoint at $4000. OK creates it. Run to it — execution stops. Edit it to add a condition. Condition-only breakpoint works.

---

## D2 — Verifier Dialog

**New file:** `src/AltirraSDL/source/ui_dbg_verifier.cpp`
**Windows reference:** `src/Altirra/source/uiverifier.cpp` — `ATUIVerifierDialog`
**RC reference:** `IDD_VERIFIER` in Altirra.rc

**Task:** Implement the Verifier dialog. This is a settings dialog with 11 checkboxes.

**Verification checks** (from `kVerifierFlags[]` in Windows source):
1. "Undocumented OS entry" — `kATVerifierFlag_UndocumentedKernelEntry`
2. "Recursive NMI execution" — `kATVerifierFlag_RecursiveNMI`
3. "Interrupt handler register corruption" — `kATVerifierFlag_InterruptRegs`
4. "Address indexing across 64K boundary" — `kATVerifierFlag_64KWrap`
5. "Abnormal playfield DMA" — `kATVerifierFlag_AbnormalDMA`
6. "OS calling convention violations" — `kATVerifierFlag_CallingConventionViolations`
7. "Loading over active display list" — `kATVerifierFlag_LoadingOverDisplayList`
8. "Loading absolute address zero" — `kATVerifierFlag_AddressZero`
9. "Non-canonical hardware address" — `kATVerifierFlag_NonCanonicalHardwareAddress`
10. "Stack overflow/underflow" — `kATVerifierFlag_StackWrap`
11. "65C816: Stack pointer in page zero" — `kATVerifierFlag_StackInZP816`

**Implementation:**
- Center-on-open modal dialog (`ImGuiWindowFlags_NoSavedSettings`)
- Read current flags from `ATCPUVerifier* pVerifier = sim.GetVerifier()` → `pVerifier->GetFlags()`
- 11 checkboxes, each maps to a flag bit
- "Enable Verifier" master checkbox → `sim.SetVerifierEnabled(bool)`
- OK applies flags via `pVerifier->SetFlags(uint32)`, Cancel discards
- Wire the Debug > Verifier menu item to open this dialog

**Verify:** Debug > Verifier opens dialog. Check some boxes, OK. Run code that triggers a check — debugger breaks with verifier message in console.

---

## D3 — Keyboard Customize Dialog

**New file:** `src/AltirraSDL/source/ui_keyboard_customize.cpp`
**Windows reference:** `src/Altirra/source/uikeyboard.cpp` — `ATUIDialogKeyboardCustomize`
**RC reference:** `IDD_KEYBOARD_CUSTOMIZE` in Altirra.rc

**Task:** Implement the custom keyboard layout editor. This is opened from Configure System > Keyboard > "Customize..." button (currently missing).

**Dialog layout:**
1. **Left panel — Emulated key list:** Table showing all Atari keys with their current host key bindings. Columns: Emulated Key, Host Key(s). Sortable and searchable (filter input at top).

2. **Right panel — Binding editor:** When a key is selected:
   - Shows current host key binding(s)
   - "Character" / "Virtual Key" radio — binding mode
   - "Add..." button — opens key capture (wait for next keypress, show it)
   - "Remove" button — removes selected binding
   - "Clear" button — removes all bindings for this key

3. **Toolbar:**
   - "Copy Default Layout to Custom" button — copies the default layout mapping to the custom map as a starting point (from `ATUIGetDefaultKeyMap()`)

**Key data:** The key map is stored via `ATUIInitVirtualKeyMap()` / custom layout storage. Read the Windows source for the exact data structures.

**Code organization:** Single file `ui_keyboard_customize.cpp` (~400-500 lines). State managed via a local struct, not global.

**Also needed:** Add the "Customize..." button to `ui_system.cpp` in the Keyboard category page (around line 1127). Currently missing `IDC_CUSTOMIZE` and `IDC_COPY_TO_CUSTOM`.

**Verify:** Configure System > Keyboard > Customize. Default layout loads. Select an Atari key → see host binding. Add a new binding via key capture. Save and verify the mapping works in emulation.

---

## D4 — Fullscreen Options

**File to modify:** `src/AltirraSDL/source/display_sdl3.cpp` and/or `src/AltirraSDL/source/main_sdl3.cpp`
**Windows reference:** `src/Altirra/source/uidisplay.cpp` — fullscreen mode handling

**Task:** Implement fullscreen resolution selection and borderless fullscreen.

1. **Borderless fullscreen** — `g_ATOptions.mbFullScreenBorderless` is already stored but not applied. When toggling fullscreen:
   - If borderless: use `SDL_SetWindowFullscreen(window, true)` (SDL3 borderless fullscreen)
   - If exclusive: use `SDL_SetWindowFullscreen(window, true)` with a specific display mode set via `SDL_SetWindowFullscreenMode()`

2. **Resolution selection** — add a dialog or combo in Configure System > Display Effects page:
   - Enumerate display modes via `SDL_GetFullscreenDisplayModes()`
   - Show resolution + refresh rate list
   - Store selected mode in settings
   - Apply via `SDL_SetWindowFullscreenMode()` before entering fullscreen

**Code organization:** Fullscreen logic is in `main_sdl3.cpp`. The UI control goes in `ui_system.cpp` Display Effects page. Keep changes focused.

**Verify:** Toggle fullscreen. With borderless mode, window covers screen without mode switch. With exclusive mode and a selected resolution, display switches to that resolution.

---

## D5 — Text Selection Submenu

**File to modify:** `src/AltirraSDL/source/ui_menus.cpp` + new file `src/AltirraSDL/source/ui_text_selection.cpp`
**Windows reference:** `src/Altirra/source/uidisplay.cpp` — text copy/paste, `ATTextCopyMode` enum
**Menu reference:** `menu_default.txt` lines 144-152

**Task:** Implement the View > Text Selection submenu with 7 items.

This requires two parts:

**Part 1 — Text selection system** (`ui_text_selection.cpp`):
- Text selection overlay on the emulation display
- Mouse drag to select a rectangular region of screen characters
- Convert screen memory (ANTIC Mode 2 display) → character data using the internal character set tables
- Provide `CopyText()`, `CopyEscapedText()`, `CopyHex()`, `CopyUnicode()` functions that read the selected characters and format appropriately
- `PasteText()` — read clipboard, convert to Atari key events, queue them via `ATInputManager`

**Part 2 — Menu wiring** (in `ui_menus.cpp`):
Add under View menu after "Save Frame":
```
Text Selection submenu:
  Copy Text           — ASCII representation
  Copy Escaped Text   — with escape sequences for special chars
  Copy Hex            — raw hex bytes
  Copy Unicode        — Unicode ATASCII mapping
  Paste Text          — clipboard → key events
  ---
  Select All          — select entire screen
  Deselect            — clear selection
```

**Code organization:** The text selection logic (screen reading, ATASCII conversion, selection rectangle) belongs in its own file. Menu wiring is a few lines in `ui_menus.cpp`.

**Verify:** Enter BASIC, type `LIST`. View > Text Selection > Select All. Copy Text → paste in host editor, shows the BASIC listing text correctly.

---

## E1 — Profiler View Pane

**New files:** `src/AltirraSDL/source/ui_dbg_profiler.cpp` + `src/AltirraSDL/source/ui_dbg_profiler_view.cpp`
**Windows reference:** `src/Altirra/source/profilerui.cpp`
**RC reference:** `IDR_PROFILE_MODE_MENU`, `IDR_PROFILE_OPTIONS_MENU`, `IDR_PROFILE_LIST_CONTEXT_MENU` in Altirra.rc

**Task:** Implement the Profile View debugger pane. This is a complex pane with toolbar, timeline, and statistics display.

**Components:**

1. **Toolbar** (horizontal button bar):
   - Start/Stop profiling buttons
   - Mode dropdown: Instruction sampling, Function sampling, Call graph, Basic block, BASIC line sampling
   - Options dropdown: counter modes (Cycles, Instructions, Branch taken/not taken, Page crossing, Redundant), frame trigger (VBlank, PC address), Enable Global Addresses

2. **Timeline view** — horizontal bar showing per-frame profile data. Zoom (mouse wheel) and scroll (drag). Frame numbers on X axis.

3. **Profile list view** — sortable table showing profiling results:
   - Columns: Address/Symbol, Cycles, Instructions, Cycles/Insn, % of total
   - Right-click context menu: "Copy As CSV"
   - Click to navigate to address in disassembly

4. **Source/disassembly view** — shows annotated disassembly with cycle counts per instruction (right-aligned columns).

**Code organization:** Split into:
- `ui_dbg_profiler.cpp` — pane class, toolbar, mode management (~300 lines)
- `ui_dbg_profiler_view.cpp` — timeline rendering, list table, CSV export (~400 lines)

**Backend integration:** The profiler backend (`ATCPUProfiler` in `src/Altirra/source/profiler.cpp`) already exists and compiles for SDL3. Use `IATDebugger::StartProfile()` / `StopProfile()` / `GetProfileSession()`.

**Wire menu:** Enable the "Profile View" menu item in `ui_menus.cpp` Debug menu (currently placeholder).

**Verify:** Debug > Profile View opens pane. Start profiling, run code, stop. Timeline shows frame bars. List shows per-function/instruction stats. Click navigates to disassembly. CSV export works.

---

## E2 — Trace Viewer / Performance Analyzer

**New files:** `src/AltirraSDL/source/ui_dbg_traceviewer.cpp` + `src/AltirraSDL/source/ui_dbg_traceviewer_timeline.cpp` + `src/AltirraSDL/source/ui_dbg_traceviewer_panels.cpp`
**Windows reference:** `src/Altirra/source/uitraceviewer.cpp` (~5000 lines)
**RC reference:** `IDR_PERFANALYZER_MENU`, `IDD_TRACEVIEWER*`, `IDD_TRACE_SETTINGS` in Altirra.rc

**Task:** Implement the Performance Analyzer (Trace Viewer). This is the most complex debugger tool — a multi-track timeline with CPU history, ANTIC events, POKEY events, PIA events, and multiple analysis views.

**This is a large feature. Split into 3 files:**

**File 1 — `ui_dbg_traceviewer.cpp` (~400 lines):** Pane shell, menu bar, toolbar, tab management.
- Menu bar: File (Load/Save/Import Atari800/Export Chrome), Trace (Start Native Trace)
- Tool bar: Start/Stop trace, View Memory Statistics
- Tab bar: Timeline, CPU Profile, Events, Log

**File 2 — `ui_dbg_traceviewer_timeline.cpp` (~500 lines):** The main timeline visualization.
- Multi-track lanes (CPU, ANTIC, POKEY, PIA) with event markers
- Timescale bar with zoom (mouse wheel) and pan (drag)
- Click-to-navigate: clicking a point sets the debugger to that cycle
- Selection: drag to select a time range for analysis

**File 3 — `ui_dbg_traceviewer_panels.cpp` (~400 lines):** Analysis panels in tabs.
- CPU Profile tab: per-function statistics from trace data
- Events tab: filterable event list (DMA, interrupts, register writes)
- Log tab: text log with timestamps, Copy Selected/All, timestamp mode selection

**Backend:** `ATTraceCollection` and `ATCPUTracer` in the debugger backend handle data capture. `ATTraceChannelFormatted` provides formatted output.

**Import/Export:**
- Import Atari800WinPlus monitor trace: parse text format
- Export Chrome Trace Event format: JSON output for chrome://tracing

**Wire menu:** Enable "Performance Analyzer..." in Debug menu.

**Verify:** Debug > Performance Analyzer. Start trace, run code, stop. Timeline shows events. Zoom/pan works. CPU profile tab shows statistics. Export Chrome trace, open in chrome://tracing — events display correctly.

---

## E3 — Tape Editor

**New files:** `src/AltirraSDL/source/ui_tool_tapeeditor.cpp` + `src/AltirraSDL/source/ui_tool_tapeeditor_view.cpp` + `src/AltirraSDL/source/ui_tool_tapeeditor_ops.cpp`
**Windows reference:** `src/Altirra/source/uitapeeditor.cpp` (~3000+ lines)
**RC reference:** `IDR_TAPEEDITOR_MENU`, `IDD_TAPEEDITOR` in Altirra.rc

**Task:** Implement the full Tape Editor. This is a complex tool for editing cassette tape images with waveform visualization.

**Split into 3 files:**

**File 1 — `ui_tool_tapeeditor.cpp` (~400 lines):** Main window, menu bar, toolbar.
- Menu bar with 5 menus (File, Edit, View, Data, Options) — 28 total items
- File: New, Open, Reload, Save As CAS, Save As WAV, Close
- Edit: Undo/Redo, Select All/Deselect, Cut/Copy/Paste/Delete, Convert Standard/Raw Block, Repeat Last Analysis (normal + flip)
- View: FSK Data / Turbo Data (radio), No Signal / Waveform / Spectrogram (radio), Show Frequency Guidelines
- Data: Extract C: File
- Options: Capture SIO Tape Decoding, Store Waveform On Load
- Status bar: tape position, selection range, block type

**File 2 — `ui_tool_tapeeditor_view.cpp` (~500 lines):** Waveform/spectrogram rendering.
- Waveform mode: draw audio samples as line graph using `ImGui::GetWindowDrawList()->AddLine()`
- Spectrogram mode: render frequency-domain image (FFT of audio data)
- Block markers: colored regions showing tape data blocks
- Selection: mouse drag to select time range
- Zoom (mouse wheel) and scroll (drag/scrollbar)
- Frequency guidelines: horizontal lines at standard baud rate frequencies

**File 3 — `ui_tool_tapeeditor_ops.cpp` (~300 lines):** Edit operations.
- Undo/redo stack
- Cut/copy/paste of tape blocks
- Block type conversion (standard ↔ raw)
- Analysis operations (decode FSK/turbo data)
- CAS/WAV file I/O (reuse existing `ATLoadCassetteImage` / save functions)
- Extract C: file (decode and save program data from tape)

**Backend:** `IATCassetteImage` and `ATCassetteEmulator` provide the data model. The image has blocks with raw or standard data, timestamps, and optional waveform.

**Wire menu:** Enable "Tape Editor..." in File > Cassette submenu (currently placeholder).

**Verify:** File > Cassette > Tape Editor. Load a CAS file. Waveform displays. Select a region, Copy, Paste elsewhere. Convert block types. Save As CAS/WAV. Undo/redo works. Spectrogram view shows frequency content.

---

## F1 — GL Backend: PAL Artifacting & EmTextureID

**File to modify:** `src/AltirraSDL/source/display_backend_gl33.cpp`
**Windows reference:** `src/Altirra/source/displayd3d11.cpp` — PAL blending implementation
**Shader reference:** `src/AltirraSDL/source/shaders_screenfx.inl` — `kGLSL_PALArtifacting_FS`

**Task:** Wire two incomplete shader features into the GL render pipeline.

1. **PAL Artifacting render pass:**
   - The fragment shader `kGLSL_PALArtifacting_FS` exists but isn't wired into `RenderScreenFX()`.
   - Add a render pass that applies PAL color blending when `mScreenFX.mPALBlendingOffset != 0`.
   - Create an FBO for the PAL pass, bind the emulator texture as input, render with the PAL shader, use the output as input for subsequent passes.
   - Set uniform `u_blendingOffset` from `mScreenFX.mPALBlendingOffset`.

2. **EmTextureID for ImGui:**
   - When the debugger Display pane uses `ImGui::Image()`, it needs the emulator GL texture as an `ImTextureID`.
   - The GL texture handle (`GLuint mEmulatorTex`) needs to be exposed via a method like `GetEmulatorTextureID()`.
   - Cast: `(ImTextureID)(intptr_t)mEmulatorTex` — this is the standard ImGui-OpenGL pattern.
   - Wire into `ui_debugger.cpp` Display pane so it shows the actual emulator output.

3. **macOS GL 3.2 forward-compatible context** (if targeting macOS):
   - Set `SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG` before context creation
   - Use `#version 150` GLSL prefix instead of `#version 330 core` on Apple
   - Conditional in `CreateGLContext()` or equivalent

**Code organization:** These are changes within the existing file. Keep each feature in a clearly commented section.

**Verify:** Load a PAL game. PAL artifacting visible (color bleeding between adjacent pixels). Debugger Display pane shows the emulator frame (not a black rectangle). On macOS (if applicable), GL context creates successfully.

---

## F2 — librashader Preset Browser & Parameter UI

**New file:** `src/AltirraSDL/source/ui_shader_presets.cpp`
**Existing file to modify:** `src/AltirraSDL/source/display_backend_gl33.cpp`

**Task:** Add a shader preset browser and parameter UI for the librashader integration.

1. **View menu item:** Add "Shader Preset" submenu to View menu:
   - "(None)" — disable librashader, use built-in effects
   - "Browse..." — open SDL file dialog for `.slangp` / `.glslp` files
   - Separator
   - Recent presets list (last 5 used, stored in settings)

2. **Preset parameter panel** (`ui_shader_presets.cpp`):
   - When a preset is loaded, show a collapsible ImGui panel
   - Enumerate parameters via `preset_get_runtime_params()`
   - For each parameter: label + slider (min/max from parameter metadata)
   - Changes applied in real-time

3. **Mutual exclusion:** When librashader is active:
   - Disable built-in screen effects (scanlines, bloom, mask, distortion)
   - Show info text: "Built-in effects disabled while shader preset is active"
   - When switching back to "(None)", re-enable built-in effects

4. **GL state management:** Save and restore GL state around `gl_filter_chain_frame()` calls — librashader may modify GL state that the rest of the backend depends on.

5. **Settings persistence:** Save last preset path and parameter values in `settings.ini`.

**Code organization:** The preset browser UI in its own file (~300 lines). GL integration changes in `display_backend_gl33.cpp`.

**Verify:** View > Shader Preset > Browse. Load a CRT shader. Parameters appear in panel. Adjust parameters — effect changes in real-time. Switch to (None) — built-in effects resume. Restart app — last preset auto-loads.
