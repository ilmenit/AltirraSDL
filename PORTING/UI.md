# User Interface (Dear ImGui)

## Scope

The Win32 UI is the **single largest porting effort** in this project:
~80,000 lines across 130 `ui*.cpp` files, plus 25 framework files in
ATNativeUI (~19,000 lines), 9 files in ATUI (~3,600 lines), and 7 files
in ATUIControls (~2,100 lines). There are 149 dialog templates in the
Windows resource file.

This document catalogs every major UI subsystem, assesses complexity, and
defines the implementation order.

## Current Architecture (Windows)

### Three-Tier UI Framework

```
                    Altirra UI Code (130 ui*.cpp files, ~80K lines)
                        |
        +---------------+------------------+
        |               |                  |
  ATNativeUI        ATUI/ATUIControls    Direct Win32
  (Win32 dialogs,   (Custom widget       (WndProc, GDI
   docking, menus)   system, overlays)    painting)
        |               |                  |
        v               v                  v
     Win32 API      IVDDisplayRenderer    GDI / D3D9
```

1. **ATNativeUI** (25 files, ~19K lines) -- Win32 abstraction layer:
   dialog base classes (`VDDialogFrameW32`), docking pane framework
   (`ATUIPaneWindow`), proxy wrappers for native controls (18 classes:
   ListBox, ComboBox, TreeView, TabControl, etc.), drag-drop (OLE),
   theming, canvas.

2. **ATUI / ATUIControls** (16 files, ~5,700 lines) -- Custom widget
   system for on-screen overlays (settings, indicators). Uses
   `ATUIWidget` base class with `Paint(IVDDisplayRenderer&)` methods.
   Provides: button, label, text edit, slider, image, list view.

3. **Direct Win32** -- Some UI files use WndProc/GDI directly, bypassing
   both frameworks (debugger panes, trace viewer, tape editor).

### Key Dependencies (all replaced by Dear ImGui)

| Win32 Dependency | Dear ImGui Replacement |
|-----------------|----------------------|
| `VDDialogFrameW32` | `ImGui::Begin()` / `ImGui::End()` windows |
| `ATUIPaneWindow` (docking) | `ImGui::DockSpace()` |
| `VDUIProxyListBox` | `ImGui::ListBox()` / `ImGui::Selectable()` |
| `VDUIProxyComboBox` | `ImGui::Combo()` |
| `VDUIProxyTabControl` | `ImGui::TabBar()` / `ImGui::TabItem()` |
| `VDUIProxyTreeView` | `ImGui::TreeNode()` |
| `VDUIProxyTrackbar` | `ImGui::SliderInt()` / `ImGui::SliderFloat()` |
| `VDUIProxyEditControl` | `ImGui::InputText()` |
| `VDUIProxyHotKeyControl` | Custom ImGui widget (key capture) |
| `VDUIProxyRichEditControl` | `ImGui::TextUnformatted()` or custom |
| `IVDDisplayRenderer` (UI drawing) | `ImDrawList` API |
| `IVDDisplayFont` / `VDDisplayTextRenderer` | ImGui font system |
| `ATPropertySet` (device config) | Unchanged (data layer, not UI) |
| `VDRegistryAppKey` (persistence) | Unchanged (uses `IVDRegistryProvider`) |

## Dear ImGui Architecture

```
SDL3 Window + Renderer
    |
    v
Dear ImGui (SDL3 backend)
    |
    +-- Menu Bar (File, System, Input, Display, Debug, Tools)
    +-- Emulator Viewport (SDL texture)
    +-- Settings windows
    +-- Device configuration windows
    +-- Debugger windows (docked panes)
    +-- Tool windows (profiler, trace, tape editor, disk explorer)
    +-- Status overlay (FPS, drive activity)
```

### Integration Model

```cpp
void RenderUI(SDL_Renderer *renderer, ATSimulator &sim, ATUIState &state) {
    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    RenderMainMenu(sim, window, state);

    // Modeless windows -- each has a bool controlling visibility
    if (state.showSystemConfig)    ATUIRenderSystemConfig(sim, state);
    if (state.showDiskManager)     ATUIRenderDiskManager(sim, state, window);
    if (state.showCassetteControl) ATUIRenderCassetteControl(sim, state, window);

    RenderStatusOverlay(sim);

    ImGui::Render();
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
}
```

### Dialog Behavior Patterns (matching Windows Altirra)

The Windows version uses three dialog patterns.  The Dear ImGui port
must match each one:

| Pattern | Window Type | Buttons | Apply | Examples |
|---------|------------|---------|-------|----------|
| **Live settings** | Modeless | OK (dismiss) | Immediately | Disk drives, Configure System |
| **Transport/tool** | Modeless | None | Immediately | Cassette Tape Control |
| **Data input** | Modal | OK + Cancel | On OK | Create Disk, Audio Options |

**Key rule:** Settings apply immediately in all management dialogs.  The
OK button is a *dismiss* button, not a *confirm* button.  There is no
Cancel because there is nothing to undo.

Configuration settings (Audio, Display, Video, Disk, Cassette, etc.) all
live inside the **Configure System** paged dialog — they are sidebar
categories, **not** separate dialog windows.

### Emulator Screen as ImGui Texture

```cpp
void RenderEmulatorScreen(ATSimulator &sim) {
    ImGui::Begin("Screen", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize);

    SDL_Texture *frameTex = display->GetFrameTexture();
    ImVec2 size = CalculateDisplaySize();
    ImGui::Image((ImTextureID)frameTex, size);

    ImGui::End();
}
```

In fullscreen, the texture fills the window. ImGui menus/overlays appear
on top when activated (Escape or hotkey).

### Input Routing

Dear ImGui consumes input when its widgets have focus. When the emulator
viewport has focus, input goes to `ATInputManager`:

```cpp
if (!ImGui::GetIO().WantCaptureKeyboard) {
    ATInputCode code = TranslateSDL3Key(event.key.scancode);
    inputManager.OnKeyDown(code);
}
```

### File Dialogs

Use SDL3 native dialogs (`SDL_ShowOpenFileDialog()` /
`SDL_ShowSaveFileDialog()`), not an ImGui file browser widget. This gives
platform-native behavior on all OSes.

### Font Configuration

- **General UI:** Built-in Dear ImGui font (ProggyClean) or embedded
  proportional font
- **Debugger:** Monospace font (embedded TTF, loaded at startup via
  `ImGui::GetIO().Fonts->AddFontFromFileTTF()`)
- **No SDL3_ttf needed** -- ImGui handles font rasterization internally

### Theme

`ImGui::StyleColorsDark()` as default. The emulator aesthetic benefits
from a dark theme that doesn't distract from the Atari display.

### Docking

Enable via `ImGuiConfigFlags_DockingEnable`. Users can arrange debugger
panes, tool windows, and the emulator viewport freely.

## Complete UI Subsystem Catalog

### 1. Main Window and Menus

**Win32 files:** `uimainwindow.cpp`, `uimenu.cpp`, `uiportmenus.cpp`,
`uicommandmanager.cpp`

**What it does:** Top-level window, menu bar (File, System, Input,
Display, Debug, Tools, Help), port device menus (controller type per
joystick port), command/accelerator dispatch.

**ImGui approach:** `ImGui::BeginMainMenuBar()` with nested
`ImGui::BeginMenu()`. Commands dispatch to `ATSimulator` methods.

```cpp
if (ImGui::BeginMainMenuBar()) {
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Boot Image...", "Ctrl+O"))
            OpenFileDialog();
        if (ImGui::MenuItem("Quick Load State", "F7"))
            sim.QuickLoadState();
        ImGui::Separator();
        if (ImGui::MenuItem("Exit"))
            requestExit = true;
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("System")) {
        if (ImGui::MenuItem("Cold Reset", "Shift+F5"))
            sim.ColdReset();
        if (ImGui::MenuItem("Warm Reset", "F5"))
            sim.WarmReset();
        ImGui::Separator();
        if (ImGui::MenuItem("Pause", "F9", sim.IsPaused()))
            sim.IsPaused() ? sim.Resume() : sim.Pause();
        ImGui::EndMenu();
    }
    // Display, Input, Debug, Tools, Help...
    ImGui::EndMainMenuBar();
}
```

**Complexity:** LOW. Menu structure is data-driven.

### 2. Settings / System Configuration

**Win32 files:** `uisettingswindow.cpp` (~1,244 lines),
`uisettingsmain.cpp` (~732 lines)

**What it does:** Hierarchical settings using a stacked-screen navigation
model (not tabs). Each screen is a list of typed setting items:
`ATUIBoolSetting`, `ATUIIntSetting`, `ATUIEnumSetting`,
`ATUISubScreenSetting`, `ATUIActionSetting`. Sub-screens stack on top
with back navigation.

**Categories:** Hardware model, memory, CPU type, OS ROM, video standard,
BASIC, SIO patch, boot options, firmware paths, and many more.

**ImGui approach:** Two options:

*Option A (preserve stacked navigation):* Use `ImGui::BeginChild()` for
the settings list. Each `ATUISubScreenSetting` pushes a new view. Back
button returns to parent.

*Option B (tree sidebar):* Use `ImGui::TreeNode()` in a left panel for
category navigation, with settings in a right panel. More conventional
for desktop apps.

```cpp
void RenderSystemConfig(ATSimulator &sim) {
    ImGui::Begin("System Configuration", &showSystemConfig);

    // Left panel: category tree
    ImGui::BeginChild("Categories", ImVec2(200, 0), true);
    if (ImGui::Selectable("Hardware", category == kHardware))
        category = kHardware;
    if (ImGui::Selectable("Memory", category == kMemory))
        category = kMemory;
    // ...
    ImGui::EndChild();

    ImGui::SameLine();

    // Right panel: settings for selected category
    ImGui::BeginChild("Settings");
    switch (category) {
    case kHardware:
        RenderHardwareSettings(sim);
        break;
    // ...
    }
    ImGui::EndChild();

    ImGui::End();
}
```

**Complexity:** MEDIUM. The setting types map directly to ImGui widgets.
The data callbacks (`getter`/`setter`/`immediateSetter`) can be reused.

### 3. Device Configuration Dialogs (50+ files)

**Win32 files:** `uiconf*.cpp` (~50 files)

**Devices covered:**
- **Disk drives:** Happy 810, 1050 (with Duplicator/Speedy/Turbo
  variants), ATR8000, Indus GT, XF551, AMDC, Percom, 810 Archiver
- **Hard drives:** IDE (KMK/JZ v1/v2, MyIDE/MyIDE-II, SIDE/SIDE2/SIDE3),
  Corvus, Black Box, CompactFlash variants
- **Audio:** Covox, SoundBoard, SlightSID
- **Video:** VBXE, XEP80
- **Serial:** 850 Interface, 1030 Modem, Pocket Modem, NetSerial,
  PipeSerial
- **Other:** Veronica (65C816 coprocessor), Dragoncart, ParFileWriter,
  ComputerEyes, RapidUS, custom devices

**Pattern:** Every config dialog follows the same structure:

```cpp
class ATUIDialogDeviceXxx : public VDDialogFrameW32 {
    ATPropertySet& mPropSet;   // device properties (data layer)

    void OnLoaded() {
        // Populate controls from mPropSet
    }

    void OnDataExchange(bool write) {
        if (write) {
            // Read controls → mPropSet
        } else {
            // mPropSet → controls
        }
    }

    void UpdateEnables() {
        // Enable/disable controls based on current selections
    }
};
```

**ImGui approach:** Same pattern -- read from `ATPropertySet`, render
ImGui controls, write back on Apply/OK:

```cpp
void RenderDeviceConfig(ATPropertySet& props) {
    int version = props.GetInt("version", 0);
    if (ImGui::Combo("Version", &version, "V1\0V2\0V3\0"))
        props.SetInt("version", version);

    int baseAddr = props.GetInt("base_addr", 0xD600);
    if (ImGui::InputInt("Base Address", &baseAddr, 0x100, 0x100,
            ImGuiInputTextFlags_CharsHexadecimal))
        props.SetInt("base_addr", baseAddr);

    bool autoSpeed = props.GetBool("auto_speed", true);
    if (ImGui::Checkbox("Auto Speed", &autoSpeed))
        props.SetBool("auto_speed", autoSpeed);
}
```

**Complexity:** LOW per dialog (10-50 lines of ImGui each), but HIGH
total volume (50+ dialogs). Once the first few are done, the rest are
mechanical.

**Status:** IMPLEMENTED in `ui_devconfig.cpp` + `ui_devconfig_devices.cpp`
(~2,500 lines). All ~41 registered device config tags have dedicated ImGui
dialogs matching Windows property encodings. Generic fallback via
`EnumProperties()` for unknown devices. Property values verified against
Windows: 1-based XEP80 port, MyIDE2 cpldver=2, 815 bit-shifted id, Covox
best-match range, BlackBox Floppy/Printer HLE enum strings, 850Full
per-port baud rates, 1400XL simplified modem (no speed/SIO/check_rate),
1030 modem (SIO level but no connect_rate/check_rate), ATR8000 with
serial signals (not Percom), BlackBox ramsize as K values (8/32/64).
DragonCart has full networking dialog (access mode, VXLAN, NAT forwarding).
Modem dialogs support custom terminal types. File/folder browse buttons
on all path fields (Hard Disk, Virtual FAT, PCLink, HostFS, Custom Device,
Video Still Image, Parallel File Writer). Device list shows hierarchy with
child devices, settings blurbs, double-click to edit, right-click context
menu with XCmd extended commands, sorted Add Device menu with help tooltips.
Dongle validates 16-hex-digit mapping. Label accuracy verified against
Windows (port notes, revision names, DIP switch text).

### 4. Disk Management

**Win32 files:** `uidisk.cpp` (~1,920 lines), `uidiskexplorer.cpp`
(~2,075 lines)

**What it does:**
- Mount/unmount/create disk images per drive (D1:-D8:)
- Disk image format selection (ATR, XFD, DCM, etc.)
- Write mode control (read-only, virtual R/W, R/W)
- Disk explorer: browse filesystem inside mounted images, view file
  contents in multiple formats (text, hex, GR.0 screen, executable dump,
  MAC/65 disassembly)

**ImGui approach:**
- Drive list with mount/unmount buttons, drag-drop from OS file manager
- Explorer as a separate window with tree (directory) + list (files) +
  preview pane
- File viewer modes via `ImGui::TabBar()`

**Complexity:** MEDIUM. The filesystem parsing (`ATDiskFS*`) is in ATIO
and reusable. The viewer formats (hex, text, executable) need rendering
code but are straightforward with ImGui.

### 5. Input Configuration

**Win32 files:** `uiinput.cpp` (~2,875 lines), `uikeyboard.cpp`
(~1,148 lines), `uikeyboardcustomize.cpp` (~1,038 lines)

**What it does:**
- Input map creation: assign host inputs → Atari controller actions
- Controller type per port (joystick, paddle, trackball, etc.)
- ~80+ virtual input types (directions, buttons, analog axes, triggers)
- Keyboard layout customization with hot-key capture
- PC scancode → Atari scancode mapping table
- Import/export of custom key maps (JSON)

**ImGui approach:**
- Controller port configuration via combo boxes
- Input binding: click a row, press a key/button to bind
- Hot-key capture widget: `ImGui::Button("Press key...")` then enter
  capture mode, read next SDL event
- Keyboard layout as visual grid (optional, not essential for MVP)

**Status:** IMPLEMENTED in `ui_input.cpp`. Full editor with list dialog,
create-from-template wizard, per-map edit (controller tree + bindings),
add/edit/delete controllers and mappings, rebind with keyboard + gamepad
capture, and Input Setup dialog (dead zones, analog power curves, live
gamepad visualization). Keyboard layout customization not yet ported.

### 6. Display Settings

**Win32 files:** `uidisplay.cpp` (~2,109 lines),
`uivideodisplaywindow.cpp` (~3,509 lines), `uirender.cpp` (~3,301 lines)

**What it does:**
- Filter mode (nearest/bilinear/sharp bilinear)
- Aspect ratio correction and stretch modes
- Overscan adjustment
- Screen effects (scanlines, bloom, etc.)
- Fullscreen control
- Enhanced text mode rendering
- Custom rendering pipeline integration

**ImGui approach:**
- Settings are simple combo/slider/checkbox controls
- `uivideodisplaywindow.cpp` and `uirender.cpp` are *rendering* code, not
  dialog code -- they manage the display pipeline. For SDL3, this is
  replaced by `VDVideoDisplaySDL3` (see DISPLAY.md), not by ImGui
- Only the *settings UI* needs ImGui implementation

**Complexity:** LOW for the settings dialog. The rendering code is
replaced by the SDL3 display layer, not by ImGui.

### 7. Audio Settings

**Win32 files:** `uiaudio*.cpp`, portions of settings screens

**What it does:** Volume, latency, audio API selection, mixer levels.

**ImGui approach:** Sliders and combos. On SDL3, API selection is
removed (SDL3 picks automatically).

**Complexity:** LOW.

### 8. Firmware Manager

**Win32 files:** `uifirmware.cpp` (~1,452 lines)

**What it does:** Add/remove/configure OS ROMs and firmware images.
Lists available firmware by type (OS-A, OS-B, XL, BASIC, etc.), shows
file paths, allows re-scanning.

**Status:** IMPLEMENTED in `ui_system.cpp` (firmware manager section,
~600 lines). Full dialog matching Windows `IDD_FIRMWARE`:
- Firmware list table with Name, Type, Use for, Default (*) columns
- Type category filter (Computer/Printers/Disk Drives/Hardware)
- "No firmware" placeholder rows for unrepresented types (All Types view)
- Internal firmware grayed out (non-editable, non-removable)
- Add (SDL3 file dialog + `ATFirmwareAutodetect`, thread-safe callback)
- Blank firmware warning (all-same-byte ROM detection, matches Windows)
- Remove, Settings (edit name/type/CRC32/path, OPTION key flag)
- Scan (SDL3 folder picker + `VDDirectoryIterator`, skips hidden/system)
- Audit (background thread CRC scan, incremental progress, matches Windows)
- Right-click context menu (Set as Default, Edit...)
- Set as Default, Use for... (specific firmware assignment toggle menu)
- Clear (with confirmation), OK button
- Drag-and-drop file import (routes SDL_EVENT_DROP_FILE when manager open)
- Firmware type long names match Windows exactly (all 56 types)

### 9. Debugger (14 files, ~10,000 lines)

**Win32 files:** `uidbg*.cpp`

This is the most complex UI subsystem. Each pane is a dockable window
inheriting from `ATUIDebuggerPaneWindow`. They communicate through the
`ATDebugger` backend (platform-agnostic).

#### 9a. Disassembly View (`uidbgdisasm.cpp`, ~1,333 lines)

- CPU instruction stream with address, hex bytes, mnemonic, operand
- Current PC highlight, breakpoint markers (red dots)
- Click-to-set-breakpoint, right-click context menu
- Source-level annotation (shows labels from symbol files)
- Supports 6502/65C02/65C816 instruction sets

**ImGui:** Custom rendering with `ImDrawList` for the gutter (breakpoint
markers) + `ImGui::Selectable()` or `ImGui::Text()` per line with
monospace font. `ImGui::BeginPopupContextItem()` for right-click menu.

#### 9b. Memory Viewer (`uidbgmemory.cpp`, ~1,696 lines)

- Hex dump with ASCII sidebar (classic hex editor layout)
- Address expression evaluation (type "ANTIC+2" to jump)
- Multiple view modes (byte, word, float)
- Address history (back/forward navigation)
- Memory coloring by type (ROM, RAM, hardware registers)

**ImGui:** Use the well-known `imgui_memory_editor` pattern (many open
source implementations exist) or custom `ImDrawList` rendering.

#### 9c. Console (`uidbgconsole.cpp`, ~669 lines)

- Command input line with history (up/down arrows)
- Rich output display (colored text, links)
- Auto-scroll, context menu for copy

**ImGui:** `ImGui::InputText()` with callback for history +
`ImGui::TextUnformatted()` in a scrolling region. Color via
`ImGui::PushStyleColor()`.

#### 9d. Register Display (`uidbgregisters.cpp`, ~245 lines)

- CPU registers: A, X, Y, S, PC, P (flags broken out: N V - B D I Z C)
- Cycle counter, scanline position
- Color-coded changed values

**ImGui:** Simple `ImGui::Text()` layout with color for changed values.

#### 9e. Watch Window (`uidbgwatch.cpp`, ~278 lines)

- User-defined expressions evaluated each step
- Editable expression list
- Value display with type formatting

**ImGui:** `ImGui::InputText()` for expression + `ImGui::Text()` for
value, in a table layout.

#### 9f. Breakpoint List (`uidbgbreakpoints.cpp`, ~420 lines)

- List all breakpoints with enable/disable toggle
- Address, condition, hit count
- Add/remove/edit

**ImGui:** `ImGui::BeginTable()` with columns, `ImGui::Checkbox()` for
enable, `ImGui::Selectable()` for selection.

#### 9g. Call Stack (`uidbgcallstack.cpp`, ~129 lines)

- Stack frames with address and symbol name
- Click to navigate

**ImGui:** Simple list with `ImGui::Selectable()`.

#### 9h. Source View (`uidbgsource.cpp`, ~808 lines)

- Source file display with line numbers
- Current-line highlight, breakpoint markers
- Symbol file integration

**ImGui:** Monospace text with line numbers, similar to disassembly.

#### 9i. Execution History (`uidbghistory.cpp`, ~383 lines)

- Recent instructions executed (ring buffer)
- Timestamp, address, instruction

**ImGui:** Scrolling table.

#### 9j. Printer Output (`uidbgprinteroutput.cpp`, ~3,000 lines)

- Graphical printer/plotter output visualization
- Custom rendering of print head movement and character output
- This is the largest single debugger file

**ImGui:** `ImDrawList` for custom 2D rendering. The print logic
(character placement, line feeds) stays the same; only the rendering
backend changes from GDI to ImDrawList.

#### 9k. Debug Display (`uidbgdebugdisplay.cpp`, ~361 lines)

- Real-time on-screen debug overlay (ANTIC/GTIA state visualization)

**ImGui:** Overlay window with `ImGui::GetForegroundDrawList()`.

#### Debugger Layout

```cpp
void RenderDebugger(ATSimulator &sim) {
    ImGuiID dockspace = ImGui::DockSpaceOverViewport(
        ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);

    // Each pane is a separate ImGui window, dockable
    if (ImGui::Begin("Disassembly"))
        RenderDisassemblyPane(sim);
    ImGui::End();

    if (ImGui::Begin("Memory"))
        RenderMemoryPane(sim);
    ImGui::End();

    if (ImGui::Begin("Registers"))
        RenderRegistersPane(sim);
    ImGui::End();

    if (ImGui::Begin("Console"))
        RenderConsolePane(sim);
    ImGui::End();

    // Watch, Breakpoints, Call Stack, History, Source...
}
```

**Overall debugger complexity:** HIGH. But the `ATDebugger` backend does
all the heavy lifting (disassembly, expression evaluation, symbol
resolution, breakpoint management). The ImGui code is presentation only.

### 10. Trace Viewer (`uitraceviewer.cpp`, ~4,551 lines)

**What it does:** Multi-view trace analysis tool:
- CPU execution history timeline
- Channel/signal timeline visualization (ANTIC, GTIA, POKEY state over
  time, drawn as colored bars/waveforms)
- CPU profiling statistics (time per function/region)
- Event log with filtering
- Timescale ruler
- Custom tooltips for hover inspection

**Components:** 7 internal view classes (`ATUITraceViewerCPUHistoryView`,
`ATUITraceViewerChannelView`, `ATUITraceViewerEventView`, etc.)

**ImGui approach:** This is a specialized visualization tool. Use
`ImDrawList` for the timeline rendering (colored rectangles, waveforms)
with `ImGui::BeginChild()` for each view panel. The trace data
structures (`ATTraceCollection`) are reusable.

**Complexity:** HIGH. Custom rendering for channel timelines and
waveforms. This is Phase 7+ work -- not needed for basic emulator
functionality.

### 11. Tape Editor (`uitapeeditor.cpp`, ~3,610 lines)

**What it does:** Cassette tape waveform editor:
- Waveform display (audio sample visualization)
- Spectrogram mode
- Cassette image manipulation (cut, copy, insert silence)
- Playback position indicator
- Audio analysis tools

**ImGui approach:** `ImDrawList` for waveform/spectrogram rendering.
`ImGui::PlotLines()` for simple waveform display, or custom rendering
for full spectrogram.

**Complexity:** MEDIUM-HIGH. Waveform rendering is well-suited to
ImDrawList, but spectrogram needs pixel-level rendering (SDL texture).

### 12. Profiler (`uiprofiler.cpp`)

**What it does:** Real-time performance visualization:
- Region-based profiling (time per code region)
- Frame-based recording
- Timeline with colored region rectangles
- Text labels for region names

**ImGui approach:** `ImDrawList` rectangles + text. Similar to a simple
flame graph.

**Complexity:** LOW-MEDIUM.

### 13. Compatibility Database (`uicompatdb.cpp`, ~1,154 lines)

**What it does:** Browse game/software compatibility information, known
issues, required settings.

**ImGui approach:** Filterable list/table.

**Complexity:** LOW.

### 14. Cheat System (`uicheater.cpp`)

**What it does:** Memory value search and modification.

**ImGui approach:** Input fields + result table.

**Complexity:** LOW.

### 15. Setup Wizard (`uisetupwizard.cpp`, ~546 lines)

**What it does:** First-run wizard for initial configuration (firmware
paths, hardware model).

**ImGui approach:** Multi-step wizard with Next/Back/Finish buttons.

**Complexity:** LOW.

### 16. Status Overlay

**Win32 files:** Parts of `uirender.cpp`, ATUI indicator widgets

**What it does:** FPS counter, drive activity LEDs, recording indicator,
speed percentage, turbo mode indicator.

```cpp
void RenderStatusOverlay(ATSimulator &sim) {
    ImGui::SetNextWindowPos(
        ImVec2(io.DisplaySize.x - 10, io.DisplaySize.y - 10),
        ImGuiCond_Always, ImVec2(1.0f, 1.0f));
    ImGui::SetNextWindowBgAlpha(0.5f);

    if (ImGui::Begin("Status", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoNav)) {
        ImGui::Text("%.1f FPS", currentFps);

        // Drive activity LEDs
        for (int i = 0; i < 4; i++) {
            if (sim.IsDriveActive(i)) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0,1,0,1), "D%d", i+1);
            }
        }
    }
    ImGui::End();
}
```

**Complexity:** LOW.

## Implementation Priority

Not all 130 files need porting. Many are niche features. Implement in
order of how essential they are to a usable emulator:

### Tier 1: Minimum Viable Emulator (Phase 5-6)

These are required for basic use:

| Component | Files | Est. Lines | Priority |
|-----------|-------|-----------|----------|
| Main menu bar | 1 | ~200 | Must have |
| File open (SDL3 native dialog) | 1 | ~50 | Must have |
| Status overlay | 1 | ~100 | Must have |
| System config (hardware, memory, CPU) | 1 | ~300 | Must have |
| Display settings (filter, aspect) | 1 | ~150 | Must have |
| Audio settings (volume, latency) | 1 | ~100 | Must have |
| Disk drive management (mount/unmount) | 1 | ~300 | Must have |
| Firmware manager (ROM paths) | 1 | ~400 | **DONE** |
| **Tier 1 total** | **~8** | **~1,400** | |

### Tier 2: Comfortable Usage (Phase 6)

| Component | Files | Est. Lines | Priority |
|-----------|-------|-----------|----------|
| Input configuration | 1 | ~2700 | **DONE** |
| Keyboard customization | 1 | ~300 | High |
| Device configs (~41 dialogs) | 2 | ~2500 | **DONE** |
| Setup wizard | 1 | ~200 | Medium |
| Cheat system | 1 | ~100 | Medium |
| **Tier 2 total** | **~15** | **~1,600** | |

### Tier 3: Debugger (Phase 7)

| Component | Files | Est. Lines | Priority |
|-----------|-------|-----------|----------|
| Disassembly | 1 | ~400 | High |
| Memory viewer | 1 | ~500 | High |
| Console | 1 | ~250 | High |
| Registers | 1 | ~100 | High |
| Breakpoints | 1 | ~200 | High |
| Watch | 1 | ~150 | Medium |
| Call stack | 1 | ~80 | Medium |
| Source view | 1 | ~300 | Medium |
| History | 1 | ~150 | Low |
| Debug display | 1 | ~150 | Low |
| Printer output | 1 | ~800 | Low |
| **Tier 3 total** | **~11** | **~3,080** | |

### Tier 4: Advanced Tools (Phase 7-8)

| Component | Files | Est. Lines | Priority |
|-----------|-------|-----------|----------|
| ~~Remaining device configs~~ | -- | -- | **DONE** (ui_devconfig.cpp + ui_devconfig_devices.cpp) |
| ~~Tools menu (all 8 items)~~ | 1 | ~2,500 | **DONE** (ui_tools.cpp) |
| Trace viewer | 1 | ~1,500 | Low |
| Profiler | 1 | ~300 | Low |
| Tape editor | 1 | ~800 | Low |
| **Tier 4 total** | **~45** | **~5,400** | |

### Summary

| Tier | ImGui lines | Replaces Win32 lines |
|------|------------|---------------------|
| Tier 1 (MVP) | ~1,400 | ~8,000 |
| Tier 2 (comfortable) | ~1,600 | ~5,000 |
| Tier 3 (debugger) | ~3,080 | ~10,000 |
| Tier 4 (advanced) | ~5,400 | ~15,000 |
| **Total** | **~11,500** | **~38,000** |

The ImGui code is significantly smaller than the Win32 code it replaces
because:
- No framework boilerplate (ATNativeUI, proxy controls, message dispatch)
- No resource file dialog templates
- No manual layout calculations (ImGui auto-layouts)
- No manual event wiring (immediate-mode handles this)

The remaining ~42,000 lines of Win32 UI code consists of framework
infrastructure (ATNativeUI, ATUI, ATUIControls) and rendering code
(`uirender.cpp`, `uivideodisplaywindow.cpp`) that is replaced by SDL3 +
ImGui at the architectural level, not file-by-file.

## Source Organization

```
src/AltirraSDL/
    source/
        ui_main.cpp          -- Menu bar, deferred actions, status overlay,
                                recording, about/changelog/cmdline dialogs
        ui_main.h            -- ATUIState, deferred action types, declarations
        ui_system.cpp        -- Configure System (20+ category pages),
                                Firmware Manager (list/edit/scan/audit)
        ui_display.cpp       -- Display Settings, Adjust Colors
        ui_disk.cpp          -- Disk Drives dialog
        ui_cassette.cpp      -- Cassette Tape Control
        ui_input.cpp         -- Input Mappings dialog
        ui_profiles.cpp      -- Profiles dialog
        ui_cartmapper.cpp    -- Cartridge mapper selection
        ui_devconfig.cpp     -- Per-device config dialogs (~40 types)
```

## Dependencies

- Dear ImGui (source vendored in project)
- `imgui_impl_sdl3.cpp` + `imgui_impl_sdlrenderer3.cpp` (official)
- SDL3 (windowing, input, file dialogs)
- ATSimulator API (emulator state)
- ATDebugger API (debugger functionality)
- ATPropertySet (device configuration data)
- IVDRegistryProvider (settings persistence)

Does **not** depend on ATNativeUI, ATUI, ATUIControls, VDDisplay, Dita,
or any Win32 headers.

## SDL3/ImGui Implementation Architecture

This section describes the concrete architecture chosen for the ImGui
port, based on analysis of the existing Windows codebase.

### Shared vs. Separate Layers

The Windows UI has a clean three-layer separation:

```
menu_default.txt   →   ATUICommandManager   →   cmd*.cpp (logic)
    (layout)              (dispatch)              (handlers)
```

**Key finding:** The `cmd*.cpp` files (13 files) contain all command
logic and have zero direct Win32 API calls. However, they **cannot be
compiled as-is** on Linux because they `#include` Altirra UI headers
(`uiaccessors.h`, `uiconfirm.h`, `uidisplay.h`) that transitively pull
in Win32 dependencies. They also reference globals (`g_sim`,
`g_xepViewEnabled`, etc.) and UI accessor functions that are
Windows-specific.

The `ATUICommandManager` class (in `ATUI/source/uicommandmanager.cpp`)
is also not compiled for Linux — ATUI has no `CMakeLists.txt` and all
its sources are excluded from the build.

**Consequence:** The ImGui UI implements its own command handling,
calling `ATSimulator` methods directly. The menu structure mirrors the
Windows version by following `menu_default.txt`, but command dispatch
is done inline in the ImGui rendering code rather than through
`ATUICommandManager`.

**Future opportunity:** Once the Win32 header dependencies in the
`cmd*.cpp` files are resolved (via stubs or `#ifdef` guards), the
command system could be shared. This is a Phase 7+ optimization.

### Menu Structure: Mirror Windows Exactly

The ImGui menu replicates the Windows menu bar:

**File | View | System | Input | Cheat | Debug | Record | Tools | Window | Help**

Rationale:
- Users switching between platforms get the same experience
- Documentation, tutorials, and forum help apply to both builds
- The menu structure is well-organized after years of refinement
- `menu_default.txt` serves as the canonical reference

Platform-irrelevant items are omitted on SDL3:
- `Options.SetFileAssocForUser/ForAll` (Windows shell integration)
- `Options.ToggleDisplayD3D9/D3D11` (Windows renderer selection)
- `Help.CheckForUpdates` (Windows-specific updater)
- `Window.Undock` (replaced by ImGui native docking)

### Settings: Tree Sidebar

The Windows version uses a stacked-screen navigation model (push/pop
sub-screens). For the ImGui port, a **tree sidebar** layout is used:

```
┌─ System Configuration ──────────────────────┐
│ ┌──────────┐ ┌────────────────────────────┐  │
│ │ Hardware │ │ Hardware Model: [800XL  ▾] │  │
│ │ Memory   │ │ Video Standard: [NTSC   ▾] │  │
│ │ CPU      │ │ Internal BASIC: [☑]        │  │
│ │ Firmware │ │ ...                        │  │
│ │ Display  │ │                            │  │
│ │ Audio    │ │                            │  │
│ │ Input    │ │                            │  │
│ └──────────┘ └────────────────────────────┘  │
└──────────────────────────────────────────────┘
```

Rationale:
- All categories visible at once (more discoverable)
- Standard desktop UX convention
- `ImGui::BeginChild()` panels make this trivial
- The `ATUIBoolSetting`/`ATUIEnumSetting` data types can still be used
  for the setting definitions; only the rendering changes

### Debugger: ImGui Built-in Docking

The Windows debugger uses a custom `ATContainerDockingPane` framework
(~2,000 lines). ImGui's built-in docking replaces this with zero custom
code:

```
┌─ Menu Bar ──────────────────────────────────────────┐
├─────────────────────────┬───────────────────────────┤
│  Disassembly            │  Registers                │
│  $8000  LDA #$00       │  A=00  X=FF  Y=00        │
│  $8002  STA $D40A      │  S=FF  PC=$8000          │
│                        ├───────────────────────────┤
│                        │  Watch                    │
│                        │  COLPF0 = $28             │
├─────────────────────────┴───────────────────────────┤
│  Console                                            │
│  > x pc                                             │
│  > _                                                │
└─────────────────────────────────────────────────────┘
```

- Same pane types as Windows: Console, Registers, Disassembly,
  Memory (x4), Watch (x4), Breakpoints, Call Stack, History, Targets
- `ImGuiConfigFlags_DockingEnable` + `DockSpaceOverViewport()`
- Layout persists via `imgui.ini` (ImGui's built-in save/restore)
- Each pane is an `ImGui::Begin()` window with free drag-and-drop

### Rendering Pipeline

With ImGui integrated, the main loop rendering changes from:

```
Old:  display.Present()  →  SDL_RenderClear + RenderTexture + RenderPresent
```

To:

```
New:  display.PrepareFrame()          // upload pixels to SDL texture
      SDL_SetRenderDrawColor(black)
      SDL_RenderClear()
      SDL_RenderTexture(display)      // draw emulator frame
      ImGui_NewFrame()                // begin ImGui frame
      RenderUI()                      // menu bar, overlay, windows
      ImGui::Render()                 // finalize ImGui draw data
      ImGui_RenderDrawData()          // draw ImGui on top
      SDL_RenderPresent()             // flip
```

ImGui event routing ensures keyboard/mouse input goes to ImGui when
its widgets have focus, and to the emulator otherwise:

```cpp
ImGui_ImplSDL3_ProcessEvent(&event);          // always
if (!ImGui::GetIO().WantCaptureKeyboard)      // then check
    ATInputSDL3_HandleKeyDown(event.key);
```

### Dependency Management

**Dear ImGui** is fetched automatically via CMake `FetchContent` from
the official repository (docking branch). No manual download or
vendoring required. The build creates a static `imgui` library target
with the SDL3 + SDLRenderer3 backends.

**SDL3** remains a system-level dependency installed via package manager.

### What Changes vs. Windows

| Aspect | Windows | SDL3/ImGui |
|--------|---------|------------|
| Menu structure | Identical | Identical |
| Command dispatch | ATUICommandManager | Direct ATSimulator calls |
| Settings layout | Stacked screens | Tree sidebar |
| Docking framework | Custom ATContainerDockingPane | ImGui built-in |
| File dialogs | Win32 GetOpenFileName | SDL_ShowOpenFileDialog |
| Context menus | Win32 TrackPopupMenu | ImGui::BeginPopupContextItem |
| Theme | Custom Win32 dark theme | ImGui::StyleColorsDark |
| Window management | Win32 HWND | ImGui docking tabs |

### Implementation Order

| Step | Component | Est. Lines | Notes |
|------|-----------|-----------|-------|
| 1 | ImGui integration + menu bar | ~300 | FetchContent, init, basic menu |
| 2 | Status overlay | ~50 | FPS, drive LEDs |
| 3 | File open dialog | ~50 | SDL_ShowOpenFileDialog |
| 4 | System settings | ~300 | Tree sidebar, hardware/memory/CPU |
| 5 | Disk management | ~200 | Mount/unmount D1:-D8: |
| 6 | Audio output (real) | ~200 | Replace stub with SDL3 audio |
| 7 | Firmware manager | ~150 | ROM path config |
| 8 | Debugger panes | ~1,500 | Console, registers, disassembly, history (done); memory, watch, call stack, breakpoints, targets, profiler, trace (TODO) |

Steps 1-3 produce a usable emulator. Steps 4-7 produce a comfortable
one. Step 8 targets power users. The debugger core panes (Console,
Registers, Disassembly, History) are implemented with an ImGui DockBuilder
layout matching Windows. The Display pane renders the emulation texture
inside the dockspace when the debugger is open.
