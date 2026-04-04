# UI Testing Plan

Automated verification of every interactive widget in the SDL3+ImGui frontend
using the test mode framework (`--test-mode`).

## Test Framework

Tests use the Unix domain socket IPC protocol described in `BUILD.md`.
Each test connects to `/tmp/altirra-test-<pid>.sock` and issues commands.
The ImGui test engine hooks automatically track every widget each frame.

### Core Command Sequences

Every test follows this pattern:

1. **Setup** -- bring the UI to the right state (`open_dialog`, `boot_image`, etc.)
2. **Verify** -- `list_items` to confirm all expected widgets exist with correct labels
3. **Interact** -- `click` widgets, change state
4. **Assert** -- `query_state`, `list_items`, or `screenshot` to verify result
5. **Cleanup** -- `close_dialog`, restore defaults

### Widget Verification Checklist

For each widget in each dialog:
- [ ] Widget exists (appears in `list_items`)
- [ ] Label matches Windows Altirra exactly
- [ ] Type is correct (button, checkbox, treenode, input)
- [ ] Initial state is correct (checked/unchecked, enabled/disabled)
- [ ] Interaction works (click toggles checkbox, opens menu, etc.)
- [ ] State change is reflected in `query_state` or subsequent `list_items`

---

## 1. Menu Bar

**How to test:** The main menu bar is always visible. Use `list_items` with
the relevant window filter (e.g., `"##MainMenuBar"`). Menus must be clicked
open before their items become visible.

### 1.1 File Menu

| # | Widget | Type | Test |
|---|--------|------|------|
| 1 | Boot Image... | MenuItem | Click opens file dialog |
| 2 | Open Image... | MenuItem | Click opens file dialog |
| 3 | Recently Booted | SubMenu | Opens, shows MRU entries (up to 10) |
| 4 | Recently Booted > Clear List | MenuItem | Clears MRU; verify list empty after |
| 5 | Disk Drives... | MenuItem | Click sets `showDiskManager=true` |
| 6 | Attach Disk > Rotate Down | MenuItem | Rotates disk assignment |
| 7 | Attach Disk > Rotate Up | MenuItem | Rotates disk assignment |
| 8 | Attach Disk > Drive 1..8 | MenuItem x8 | Each opens file dialog |
| 9 | Detach Disk > All | MenuItem | Detaches all drives |
| 10 | Detach Disk > Drive 1..8 | MenuItem x8 | Disabled when no disk; enabled+detaches when loaded |
| 11 | Cassette > Tape Control... | MenuItem | Click sets `showCassetteControl=true` |
| 12 | Cassette > Tape Editor... | MenuItem | Placeholder -- verify disabled |
| 13 | Cassette > New Tape | MenuItem | Creates empty tape |
| 14 | Cassette > Load... | MenuItem | Opens file dialog |
| 15 | Cassette > Unload | MenuItem | Disabled when no tape; enabled+unloads when loaded |
| 16 | Cassette > Save... | MenuItem | Disabled when no tape |
| 17 | Cassette > Export Audio Tape... | MenuItem | Disabled when no tape |
| 18 | Load State... | MenuItem | Opens file dialog |
| 19 | Save State... | MenuItem | Opens file dialog |
| 20 | Quick Load State | MenuItem | Disabled when no quick save exists |
| 21 | Quick Save State | MenuItem | Creates quick save |
| 22 | Attach Special Cartridge > (20 items) | MenuItem x20 | Each attaches the named special cartridge |
| 23 | Secondary Cartridge > Attach... | MenuItem | Opens file dialog |
| 24 | Secondary Cartridge > Detach | MenuItem | Disabled when no cart; enabled+detaches |
| 25 | Attach Cartridge... | MenuItem | Opens file dialog |
| 26 | Detach Cartridge | MenuItem | Disabled when no cart |
| 27 | Save Firmware > (5 items) | MenuItem x5 | Each conditional on device presence |
| 28 | Exit | MenuItem | Sets `requestExit=true` |

### 1.2 View Menu

| # | Widget | Type | Test |
|---|--------|------|------|
| 1 | Full Screen | MenuItem | Toggles fullscreen mode |
| 2 | Filter Mode > Next Mode + 5 modes | MenuItem x6 | Next Mode is action; Point/Bilinear/Sharp Bilinear/Bicubic/Default are radio-like |
| 3 | Filter Sharpness > (5 items) | MenuItem x5 | Radio-like: one checked |
| 4 | Video Frame > (5 mode items) | MenuItem x5 | Radio-like: one checked |
| 5 | Video Frame > Pan/Zoom Tool | MenuItem | Placeholder -- verify disabled |
| 6 | Video Frame > Reset Pan and Zoom | MenuItem | Resets pan/zoom |
| 7 | Video Frame > Reset Panning | MenuItem | Resets pan only |
| 8 | Video Frame > Reset Zoom | MenuItem | Resets zoom only |
| 9 | Overscan Mode > (5 mode items) | MenuItem x5 | Radio-like: one checked |
| 10 | Overscan > Vertical Override > (5 items) | MenuItem x5 | Radio-like: one checked |
| 11 | Overscan > Extended PAL Height | MenuItem | Checkable toggle |
| 12 | Overscan > Indicator Margin | MenuItem | Checkable toggle |
| 13 | Vertical Sync | MenuItem | Checkable toggle |
| 14 | Show FPS | MenuItem | Checkable toggle |
| 15 | Video Outputs > 1 Computer Output | MenuItem | Radio-like |
| 16 | Video Outputs > Next Output | MenuItem | Conditional on alt output |
| 17 | Video Outputs > Auto-Switch Video Output | MenuItem | Checkable toggle |
| 18 | Adjust Colors... | MenuItem | Sets `showAdjustColors=true` |
| 19 | Adjust Screen Effects... | MenuItem | Placeholder -- disabled |
| 20 | Customize HUD... | MenuItem | Placeholder -- disabled |
| 21 | Calibrate... | MenuItem | Placeholder -- disabled |
| 22 | Display | MenuItem | Placeholder -- disabled |
| 23 | Printer Output | MenuItem | Placeholder -- disabled |
| 24 | Copy Frame to Clipboard | MenuItem | Copies current frame |
| 25 | Copy Frame to Clipboard (True Aspect) | MenuItem | Placeholder -- disabled |
| 26 | Save Frame... | MenuItem | Opens file dialog |
| 27 | Save Frame (True Aspect)... | MenuItem | Placeholder -- disabled |
| 28 | Text Selection > (7 items) | MenuItem x7 | All placeholder -- disabled |

### 1.3 System Menu

| # | Widget | Type | Test |
|---|--------|------|------|
| 1 | Profiles > Edit Profiles... | MenuItem | Sets `showProfiles=true` |
| 2 | Profiles > Temporary Profile | MenuItem | Checkable toggle |
| 3 | Profiles > [dynamic profile list] | MenuItem xN | Radio-like: active profile checked |
| 4 | Configure System... | MenuItem | Sets `showSystemConfig=true` |
| 5 | Warm Reset | MenuItem | Triggers warm reset |
| 6 | Cold Reset | MenuItem | Triggers cold reset |
| 7 | Cold Reset (Computer Only) | MenuItem | Computer-only cold reset |
| 8 | Pause | MenuItem | Checkable toggle; verify via `query_state.sim.paused` |
| 9 | Warp Speed | MenuItem | Checkable toggle; verify via `query_state.sim.turbo` |
| 10 | Pause When Inactive | MenuItem | Checkable toggle |
| 11 | Rewind > Quick Rewind | MenuItem | Placeholder -- disabled |
| 12 | Rewind > Rewind... | MenuItem | Placeholder -- disabled |
| 13 | Power-On Delay > (5 items) | MenuItem x5 | Radio-like: one checked |
| 14 | Hold Keys For Reset | MenuItem | Activates hold |
| 15 | Internal BASIC | MenuItem | Checkable toggle |
| 16 | Auto-Boot Tape (Hold Start) | MenuItem | Checkable toggle |
| 17 | Console Switches > (all items) | MenuItem xN | Each checkable or action; dynamic list |

### 1.4 Input Menu

| # | Widget | Type | Test |
|---|--------|------|------|
| 1 | Input Mappings... | MenuItem | Sets `showInputMappings=true` |
| 2 | Input Setup... | MenuItem | Sets `showInputSetup=true` |
| 3 | Cycle Quick Maps | MenuItem | Cycles to next quick map |
| 4 | Capture Mouse | MenuItem | Conditional: disabled if mouse not mapped |
| 5 | Auto-Capture Mouse | MenuItem | Checkable toggle |
| 6 | Light Pen/Gun... | MenuItem | Opens Light Pen dialog (gun/pen offsets + noise mode) |
| 7 | Recalibrate Light Pen/Gun | MenuItem | Calls ATLightPenPort::Recalibrate() |
| 8 | Port 1..4 > None | MenuItem x4 | Sets port to no controller |
| 9 | Port 1..4 > [device entries] | MenuItem xN | Dynamic per-port device list |

### 1.5 Cheat Menu

| # | Widget | Type | Test |
|---|--------|------|------|
| 1 | Cheater... | MenuItem | Opens Cheater dialog (memory search + cheat management) |
| 2 | Disable P/M Collisions | MenuItem | Checkable toggle |
| 3 | Disable Playfield Collisions | MenuItem | Checkable toggle |

### 1.6 Debug Menu

| # | Widget | Type | Test |
|---|--------|------|------|
| 1 | Enable Debugger | MenuItem | Checkable toggle; verify debugger windows appear |
| 2 | Open Source File... | MenuItem | Placeholder -- disabled |
| 3 | Source File List... | MenuItem | Placeholder -- disabled |
| 4 | Window > Console | MenuItem | Opens console pane |
| 5 | Window > Registers | MenuItem | Opens registers pane |
| 6 | Window > Disassembly | MenuItem | Opens disassembly pane |
| 7 | Window > Call Stack | MenuItem | Opens call stack pane |
| 8 | Window > History | MenuItem | Opens history pane |
| 9 | Window > Memory > Memory 1..4 | MenuItem x4 | Opens memory pane |
| 10 | Window > Watch > Watch 1..4 | MenuItem x4 | Placeholder -- disabled |
| 11 | Window > Breakpoints | MenuItem | Opens breakpoints pane |
| 12 | Window > Targets | MenuItem | Placeholder -- disabled |
| 13 | Window > Debug Display | MenuItem | Placeholder -- disabled |
| 14 | Visualization > Cycle GTIA | MenuItem | Cycles visualization mode |
| 15 | Visualization > Cycle ANTIC | MenuItem | Placeholder -- disabled |
| 16 | Options > Break at EXE Run Address | MenuItem | Checkable; conditional |
| 17 | Options > Auto-Reload ROMs | MenuItem | Placeholder -- disabled |
| 18 | Options > Randomize Memory On EXE Load | MenuItem | Placeholder -- disabled |
| 19 | Options > Change Font... | MenuItem | Placeholder -- disabled |
| 20 | Run/Break | MenuItem | Conditional on debugger state |
| 21 | Break | MenuItem | Conditional on debugger + running |
| 22 | Step Into | MenuItem | Conditional on debugger + stopped |
| 23 | Step Over | MenuItem | Conditional on debugger + stopped |
| 24 | Step Out | MenuItem | Conditional on debugger + stopped |
| 25 | Profile > Profile View | MenuItem | Placeholder -- disabled |
| 26 | Verifier... | MenuItem | Placeholder -- disabled |
| 27 | Performance Analyzer... | MenuItem | Placeholder -- disabled |

### 1.7 Record Menu

| # | Widget | Type | Test |
|---|--------|------|------|
| 1 | Record Raw Audio... | MenuItem | Disabled during recording |
| 2 | Record Audio... | MenuItem | Disabled during recording |
| 3 | Record Video... | MenuItem | Opens recording dialog |
| 4 | Record SAP Type R... | MenuItem | Disabled during recording |
| 5 | Record VGM... | MenuItem | Opens save dialog, starts VGM recording (UTF-16LE fixed) |
| 6 | Stop Recording | MenuItem | Disabled when not recording |
| 7 | Pause/Resume Recording | MenuItem | Disabled when not video recording |

### 1.8 Tools Menu

| # | Widget | Type | Test |
|---|--------|------|------|
| 1 | Disk Explorer... | MenuItem | Sets `showDiskExplorer=true` |
| 2 | Convert SAP to EXE... | MenuItem | Opens conversion tool |
| 3 | Export ROM set... | MenuItem | Opens export dialog |
| 4 | Analyze tape decoding... | MenuItem | Opens tape analysis |
| 5 | First Time Setup... | MenuItem | Sets `showSetupWizard=true` |
| 6 | Keyboard Shortcuts... | MenuItem | Sets `showKeyboardShortcuts=true` |
| 7 | Compatibility Database... | MenuItem | Sets `showCompatDB=true` |
| 8 | Advanced Configuration... | MenuItem | Sets `showAdvancedConfig=true` |

### 1.9 Window Menu

| # | Widget | Type | Test |
|---|--------|------|------|
| 1 | Close | MenuItem | Placeholder -- disabled |
| 2 | Undock | MenuItem | Placeholder -- disabled |
| 3 | Next Pane | MenuItem | Placeholder -- disabled |
| 4 | Previous Pane | MenuItem | Placeholder -- disabled |
| 5 | Adjust Window Size > [size entries] | MenuItem xN | Resizes window |
| 6 | Reset Window Layout | MenuItem | Resets all window positions |

### 1.10 Help Menu

| # | Widget | Type | Test |
|---|--------|------|------|
| 1 | Contents | MenuItem | Placeholder -- disabled |
| 2 | About | MenuItem | Sets `showAboutDialog=true` |
| 3 | Change Log | MenuItem | Sets `showChangeLog=true` |
| 4 | Command-Line Help | MenuItem | Sets `showCommandLineHelp=true` |
| 5 | Export Debugger Help... | MenuItem | Placeholder -- disabled |
| 6 | Check For Updates | MenuItem | Placeholder -- disabled |
| 7 | Altirra Home... | MenuItem | Placeholder -- disabled |

---

## 2. Configure System Dialog

**Setup:** `open_dialog SystemConfig`

This is the largest dialog. Each category page is selected by clicking the
sidebar tree. Use `list_items "Configure System"` after navigating to each page.

### 2.1 Sidebar Navigation

| # | Widget | Type | Test |
|---|--------|------|------|
| 1 | Overview | Selectable | Select and verify page content appears |
| 2 | Recommendations | Selectable | Select and verify content |
| 3 | Computer (tree) | TreeNode | Expand to show sub-items |
| 4 | System | Selectable | Navigate to System page |
| 5 | CPU | Selectable | Navigate to CPU page |
| 6 | Firmware | Selectable | Navigate to Firmware page |
| 7 | Memory | Selectable | Navigate to Memory page |
| 8 | Acceleration | Selectable | Navigate to Acceleration page |
| 9 | Speed | Selectable | Navigate to Speed page |
| 10 | Boot | Selectable | Navigate to Boot page |
| 11 | Outputs (tree) | TreeNode | Expand |
| 12 | Video | Selectable | Navigate to Video page |
| 13 | Enhanced Text | Selectable | Navigate to Enhanced Text page |
| 14 | Audio | Selectable | Navigate to Audio page |
| 15 | Peripherals (tree) | TreeNode | Expand |
| 16 | Devices | Selectable | Navigate to Devices page |
| 17 | Keyboard | Selectable | Navigate to Keyboard page |
| 18 | Media (tree) | TreeNode | Expand |
| 19 | Defaults | Selectable | Navigate to Media Defaults page |
| 20 | Disk | Selectable | Navigate to Disk page |
| 21 | Cassette | Selectable | Navigate to Cassette page |
| 22 | Flash | Selectable | Navigate to Flash page |
| 23 | Emulator (tree) | TreeNode | Expand |
| 24 | Compat DB | Selectable | Navigate to Compat DB page |
| 25 | Display | Selectable | Navigate to Display (emulator) page |
| 26 | Ease of Use | Selectable | Navigate to Ease of Use page |
| 27 | Error Handling | Selectable | Navigate to Error Handling page |
| 28 | Input | Selectable | Navigate to Input page |
| 29 | Window Caption | Selectable | Navigate to Window Caption page |
| 30 | Workarounds | Selectable | Navigate to Workarounds page |

### 2.2 Overview Page

| # | Widget | Type | Test |
|---|--------|------|------|
| 1 | Copy to Clipboard | Button | Copies system info text |

### 2.3 Recommendations Page

| # | Widget | Type | Test |
|---|--------|------|------|
| 1 | ##AssessTarget | Combo | Options: Compatibility, Accuracy, Emulator Performance |

### 2.4 System (Hardware) Page

| # | Widget | Type | Test |
|---|--------|------|------|
| 1 | ##HardwareType | Combo | 7 hardware types; verify `query_state.sim.hardwareMode` changes |
| 2 | ##VideoStandard | Combo | NTSC/PAL/SECAM/NTSC50/PAL60; disabled in 5200 mode |
| 3 | Toggle NTSC/PAL | Button | Swaps video standard; disabled in 5200 mode |
| 4 | CTIA mode | Checkbox | Toggle CTIA vs GTIA |
| 5 | ##DefectMode | Combo | None / Type 1 / Type 2 |

### 2.5 CPU Page

| # | Widget | Type | Test |
|---|--------|------|------|
| 1 | 6502 / 6502C | RadioButton | CPU type selection (group of 8) |
| 2 | 65C02 | RadioButton | |
| 3 | 65C816 (1.79MHz) | RadioButton | |
| 4 | 65C816 (3.58MHz) | RadioButton | |
| 5 | 65C816 (7.14MHz) | RadioButton | |
| 6 | 65C816 (10.74MHz) | RadioButton | |
| 7 | 65C816 (14.28MHz) | RadioButton | |
| 8 | 65C816 (17.90MHz) | RadioButton | |
| 9 | Enable illegal instructions | Checkbox | Toggle |
| 10 | Allow BRK/IRQ to block NMI | Checkbox | Toggle |
| 11 | Stop on BRK instruction | Checkbox | Toggle |
| 12 | Record instruction history | Checkbox | Toggle |
| 13 | Track code paths | Checkbox | Toggle |
| 14 | Shadow ROMs in fast RAM | Checkbox | Toggle |
| 15 | Shadow cartridges in fast RAM | Checkbox | Toggle |

### 2.6 Firmware Page

| # | Widget | Type | Test |
|---|--------|------|------|
| 1 | Firmware Manager... | Button | Opens Firmware Manager dialog |

### 2.7 Memory Page

| # | Widget | Type | Test |
|---|--------|------|------|
| 1 | Memory Size | Combo | 13 options: 8K through 1088K |
| 2 | Memory Clear Pattern | Combo | Random/DRAM1/DRAM2/Atari OS/Custom |
| 3 | Enable MapRAM | Checkbox | XL/XE only |
| 4 | Enable Ultimate1MB | Checkbox | Toggle |
| 5 | Enable bank register aliasing | Checkbox | Toggle |
| 6 | Enable floating I/O bus | Checkbox | 800 only |
| 7 | Axlon banks | Combo | 8 size options |
| 8 | High memory banks | Combo | 5 options |
| 9 | Preserve extended memory on cold reset | Checkbox | Toggle |

### 2.8 Acceleration Page

| # | Widget | Type | Test |
|---|--------|------|------|
| 1 | Fast boot | Checkbox | Toggle |
| 2 | Fast floating-point math | Checkbox | Toggle |
| 3 | SIO Patch | Checkbox | Toggle |
| 4 | D: patch (Disk SIO) | Checkbox | Toggle |
| 5 | C: patch (Cassette SIO) | Checkbox | Toggle |
| 6 | PRT: patch (Other SIO) | Checkbox | Toggle |
| 7 | H: (Host device CIO) | Checkbox | Toggle |
| 8 | P: (Printer CIO) | Checkbox | Toggle |
| 9 | R: (RS-232 CIO) | Checkbox | Toggle |
| 10 | T: (1030 Serial CIO) | Checkbox | Toggle |
| 11 | D: burst I/O | Checkbox | Toggle |
| 12 | PRT: burst I/O | Checkbox | Toggle |
| 13 | CIO burst transfers | Checkbox | Toggle |
| 14 | Software patch##SIO | RadioButton | SIO patch mode |
| 15 | PBI patch##SIO | RadioButton | SIO patch mode |
| 16 | Both##SIO | RadioButton | SIO patch mode |
| 17 | Hardware##CIO | RadioButton | CIO patch mode |
| 18 | PBI##CIO | RadioButton | CIO patch mode |
| 19 | SIO override detection | Checkbox | Toggle |

### 2.9 Speed Page

| # | Widget | Type | Test |
|---|--------|------|------|
| 1 | Run as fast as possible (warp) | Checkbox | Toggle; verify `query_state.sim.turbo` |
| 2 | Slow Motion | Checkbox | Toggle |
| 3 | Speed | Combo | 4 multiplier options |
| 4 | Base frame rate | Combo | NTSC/PAL/mixed |
| 5 | Lock speed to display refresh rate | Checkbox | Toggle |
| 6 | Pause when emulator window is inactive | Checkbox | Toggle |

### 2.10 Boot Page

| # | Widget | Type | Test |
|---|--------|------|------|
| 1 | ##ProgramLoadMode | Combo | 4 load behavior options |
| 2 | Randomize Memory on EXE Load | Checkbox | Toggle |
| 3 | Randomize program load timing | Checkbox | Toggle |
| 4 | Unload cartridges when booting new image | Checkbox | Toggle |
| 5 | Unload disks when booting new image | Checkbox | Toggle |
| 6 | Unload tapes when booting new image | Checkbox | Toggle |
| 7 | Power-on delay | Combo | 5 options |
| 8 | ##SeedInput | InputText | Decimal seed value |
| 9 | Use Specific Seed | Button | Sets seed |
| 10 | Auto | Button | Clears specific seed |

### 2.11 Video Page

| # | Widget | Type | Test |
|---|--------|------|------|
| 1 | Artifacting | Combo | 7 options |
| 2 | Monitor Mode | Combo | 6 options |
| 3 | Frame Blending | Checkbox | Toggle |
| 4 | Linear Frame Blending | Checkbox | Toggle |
| 5 | Mono Persistence | Checkbox | Toggle |
| 6 | Interlace | Checkbox | Toggle |
| 7 | Scanlines | Checkbox | Toggle |
| 8 | PAL Phase | Combo | Standard / Delayed |
| 9 | Extended PAL Height | Checkbox | Toggle |

### 2.12 Audio Page

| # | Widget | Type | Test |
|---|--------|------|------|
| 1 | Mute All | Checkbox | Toggle |
| 2 | Stereo | Checkbox | Toggle |
| 3 | Downmix stereo to mono | Checkbox | Toggle |
| 4 | Non-linear mixing | Checkbox | Toggle |
| 5 | Serial noise | Checkbox | Toggle |
| 6 | Simulate console speaker | Checkbox | Toggle |
| 7 | [Channel] (per POKEY channel) | Checkbox xN | Channel muting toggles |
| 8 | Drive Sounds | Checkbox | Toggle |
| 9 | Audio monitor | Checkbox | Toggle |
| 10 | Audio scope | Checkbox | Toggle |
| 11 | Host audio options... | Button | Opens audio options sub-dialog |

### 2.13 Enhanced Text Page

| # | Widget | Type | Test |
|---|--------|------|------|
| 1 | Mode | Combo | None / Hardware / Software |

### 2.14 Devices Page

| # | Widget | Type | Test |
|---|--------|------|------|
| 1 | Add Device... | Button | Opens device selector popup |
| 2 | [Device name] entries | Selectable xN | Dynamic device list |
| 3 | Remove | Button | Conditional: enabled if device selected |
| 4 | Settings... | Button | Conditional: enabled if device has settings |

### 2.15 Keyboard Page

| # | Widget | Type | Test |
|---|--------|------|------|
| 1 | Allow SHIFT key to be detected on cold reset | Checkbox | Toggle |
| 2 | Enable F1-F4 as 1200XL function keys | Checkbox | Toggle |
| 3 | Share modifier host keys | Checkbox | Toggle |
| 4 | Share non-modifier host keys | Checkbox | Toggle |

### 2.16 Disk Page

| # | Widget | Type | Test |
|---|--------|------|------|
| 1 | Accurate sector timing | Checkbox | Toggle |
| 2 | Play drive sounds | Checkbox | Toggle |
| 3 | Show sector counter | Checkbox | Toggle |

### 2.17 Cassette Page

| # | Widget | Type | Test |
|---|--------|------|------|
| 1 | Auto-boot on startup | Checkbox | Toggle |
| 2 | Auto-boot BASIC on startup | Checkbox | Toggle |
| 3 | Auto-rewind on startup | Checkbox | Toggle |
| 4 | Load data as audio | Checkbox | Toggle |
| 5 | Randomize starting position | Checkbox | Toggle |
| 6 | Turbo mode | Combo | 8 options |
| 7 | Turbo decoder | Combo | 5 options |
| 8 | Invert turbo data | Checkbox | Toggle |
| 9 | ##DirectReadFilter | Combo | 4 options |
| 10 | Avoid OS C: random VBI-related errors | Checkbox | Toggle |
| 11 | Enable FSK speed compensation | Checkbox | Toggle |
| 12 | Enable crosstalk reduction | Checkbox | Toggle |

### 2.18 Display (Emulator) Page

| # | Widget | Type | Test |
|---|--------|------|------|
| 1 | Auto-hide mouse pointer after short delay | Checkbox | Toggle |
| 2 | Constrain mouse pointer in full-screen mode | Checkbox | Toggle |
| 3 | Hide target pointer for abs mouse input | Checkbox | Toggle |
| 4 | Show indicators | Checkbox | Toggle |
| 5 | Pad bottom margin for indicators | Checkbox | Toggle |
| 6 | Show tablet/pad bounds | Checkbox | Toggle |
| 7 | Show tablet/pad pointers | Checkbox | Toggle |

### 2.19 Input (Emulator) Page

| # | Widget | Type | Test |
|---|--------|------|------|
| 1 | Enable paddle potentiometer noise | Checkbox | Toggle |
| 2 | Use immediate analog updates | Checkbox | Toggle |
| 3 | Use immediate light pen updates | Checkbox | Toggle |

### 2.20 Ease of Use Page

| # | Widget | Type | Test |
|---|--------|------|------|
| 1 | Reset when changing cartridges | Checkbox | Toggle |
| 2 | Reset when changing video standard | Checkbox | Toggle |
| 3 | Reset when toggling internal BASIC | Checkbox | Toggle |

### 2.21 Workarounds Page

| # | Widget | Type | Test |
|---|--------|------|------|
| 1 | Poll directories for changes (H: device) | Checkbox | Toggle |

### 2.22 Compat DB Page

| # | Widget | Type | Test |
|---|--------|------|------|
| 1 | Show compatibility warnings | Checkbox | Toggle |
| 2 | Use internal database | Checkbox | Toggle |
| 3 | Use external database | Checkbox | Toggle |
| 4 | ##CompatPath | InputText | Path input |
| 5 | Browse... | Button | Opens folder dialog |
| 6 | Unmute all warnings | Button | Resets muted warnings |

### 2.23 Error Handling Page

| # | Widget | Type | Test |
|---|--------|------|------|
| 1 | Error mode | Combo | Options based on error mode count |

### 2.24 Media Defaults Page

| # | Widget | Type | Test |
|---|--------|------|------|
| 1 | Write mode | Combo | Read Only / Virtual R/W (Safe) / Virtual R/W / Read/Write |

### 2.25 Window Caption Page

| # | Widget | Type | Test |
|---|--------|------|------|
| 1 | Template | InputText | Window caption template with variable substitution |

### 2.26 Dialog Controls

| # | Widget | Type | Test |
|---|--------|------|------|
| 1 | OK | Button | Closes dialog, applies settings |

---

## 3. Disk Manager Dialog

**Setup:** `open_dialog DiskManager`

| # | Widget | Type | Test |
|---|--------|------|------|
| 1 | Drives 1-8 | RadioButton | Tab selector |
| 2 | Drives 9-15 | RadioButton | Tab selector |
| 3 | ... (per-drive browse) | Button x8 | Opens file dialog per drive |
| 4 | Eject (per drive) | Button x8 | Disabled when empty; ejects when loaded |
| 5 | + (per-drive context) | Button x8 | Opens context menu |
| 6 | > New Disk... | MenuItem | Opens Create Disk sub-dialog |
| 7 | > Save Disk | MenuItem | Conditional on dirty state |
| 8 | > Save Disk As... | MenuItem | Conditional on loaded |
| 9 | > Revert | MenuItem | Conditional |
| 10 | > Eject | MenuItem | Conditional on loaded |
| 11 | ##wm (per-drive write mode) | Combo x8 | Off/R-O/VRWSafe/VRW/R-W; disabled when empty |
| 12 | Emulation level | Combo | 12 drive emulation options |
| 13 | OK | Button | Closes dialog |

### 3.1 Create Disk Sub-Dialog

| # | Widget | Type | Test |
|---|--------|------|------|
| 1 | Format | Combo | 10 presets + Custom |
| 2 | Sector Count | InputInt | Enabled only in Custom mode |
| 3 | 128 / 256 / 512 (sector size) | RadioButton x3 | Enabled only in Custom mode |
| 4 | Boot Sectors | InputInt | Editable |
| 5 | Create | Button | Creates disk |
| 6 | Cancel | Button | Closes sub-dialog |

---

## 4. Cassette Control Dialog

**Setup:** `open_dialog CassetteControl`

| # | Widget | Type | Test |
|---|--------|------|------|
| 1 | ##TapePos (position slider) | SliderFloat | Enabled when tape loaded; shows M:SS.d |
| 2 | Stop | Button | Green highlight when active; enabled when loaded |
| 3 | Pause | Button | Green highlight when paused |
| 4 | Play | Button | Green highlight when playing |
| 5 | \|< (seek start) | Button | Seeks to beginning |
| 6 | >\| (seek end) | Button | Seeks to end |
| 7 | Rec | Button | Red highlight when recording |

---

## 5. Adjust Colors Dialog

**Setup:** `open_dialog AdjustColors`

| # | Widget | Type | Test |
|---|--------|------|------|
| 1 | Preset | Combo | (Custom) + dynamic preset list |
| 2 | Share NTSC/PAL settings | Checkbox | Toggle |
| 3 | Luma Ramp | Combo | Linear / XL |
| 4 | Color Matching | Combo | None / sRGB / Adobe RGB / Gamma 2.2 / 2.4 |
| 5 | PAL quirks | Checkbox | Toggle |
| 6 | Hue (header) | CollapsingHeader | Expand/collapse |
| 7 | Hue Start | SliderFloat | -120 to 360 deg |
| 8 | Hue Range | SliderFloat | 0 to 540 deg |
| 9 | Brightness / Contrast (header) | CollapsingHeader | Expand/collapse |
| 10 | Brightness | SliderFloat | -50% to +50% |
| 11 | Contrast | SliderFloat | 0% to 200% |
| 12 | Saturation | SliderFloat | 0% to 100% |
| 13 | Gamma | SliderFloat | 0.50 to 2.60 |
| 14 | Intensity Scale | SliderFloat | 50% to 200% |
| 15 | Artifacting (header) | CollapsingHeader | Expand/collapse |
| 16 | Phase | SliderFloat | -60 to 360 deg |
| 17 | Saturation##Art | SliderFloat | 0% to 400% |
| 18 | Sharpness | SliderFloat | 0% to 100% |
| 19 | Color Matrix (header) | CollapsingHeader | Expand/collapse |
| 20-25 | R-Y/G-Y/B-Y Shift+Scale | SliderFloat x6 | Per-channel adjustments |
| 26 | Reset to Defaults | Button | Resets all color settings |

---

## 6. Firmware Manager Dialog

**Setup:** Navigate via System Config > Firmware > Firmware Manager...

| # | Widget | Type | Test |
|---|--------|------|------|
| 1 | All Types (type filter) | Selectable | Filters firmware list |
| 2 | [Category] entries | Selectable xN | Category filter |
| 3 | [Type] entries | Selectable xN | Type filter |
| 4 | [Firmware name] entries | Selectable xN | Select firmware in list |
| 5 | Add... | Button | Opens file dialog |
| 6 | Remove | Button | Conditional: custom firmware selected |
| 7 | Settings... | Button | Opens Edit Firmware dialog |
| 8 | Scan... | Button | Opens folder dialog for scan |
| 9 | Set as Default | Button | Conditional: firmware selected |
| 10 | Use for... | Button | Opens use-for popup |
| 11 | Audit... | Button | Opens firmware audit dialog |
| 12 | Clear | Button | Clears all firmware |

### 6.1 Edit Firmware Dialog

| # | Widget | Type | Test |
|---|--------|------|------|
| 1 | Name | InputText | Firmware name |
| 2 | Type | Combo | Category-grouped type selector |
| 3 | CRC32 | InputText | Read-only |
| 4 | OPTION key inverted | Checkbox | Toggle |
| 5 | OK | Button | Saves and closes |
| 6 | Cancel | Button | Discards and closes |

---

## 7. Input Mappings Dialog

**Setup:** `open_dialog InputMappings`

| # | Widget | Type | Test |
|---|--------|------|------|
| 1 | Add Controller | Button | Opens controller template selector |
| 2 | Add Mapping | Button | Conditional: controller selected |
| 3 | Delete | Button | Conditional: controller selected |
| 4 | Rebind | Button | Opens rebind capture dialog |
| 5 | [Controller name] | TreeNode xN | Collapsible controller list |
| 6 | [Mapping name] | TreeNode xN | Nested mapping entries |

---

## 8. Profiles Dialog

**Setup:** `open_dialog Profiles`

| # | Widget | Type | Test |
|---|--------|------|------|
| 1 | Temporary Profile | Checkbox | Toggle temporary mode |
| 2 | [Global Profile] | Selectable | Select global profile |
| 3 | [Profile name] entries | Selectable xN | Dynamic list; double-click to rename |
| 4 | Add Profile | Button | Creates new profile |
| 5 | OK | Button | Closes dialog |

---

## 9. Cartridge Mapper Dialog

**Setup:** `open_dialog CartridgeMapper`

| # | Widget | Type | Test |
|---|--------|------|------|
| 1 | [Mapper type] entries | Selectable xN | Dynamic mapper list |
| 2 | Show All | Checkbox | Shows all mapper types |
| 3 | Show Details | Checkbox | Shows detail info |
| 4 | OK | Button | Conditional: valid selection |
| 5 | Cancel | Button | Closes without applying |

---

## 10. Recording Dialogs

### 10.1 Record Video Dialog

| # | Widget | Type | Test |
|---|--------|------|------|
| 1 | Video Codec | Combo | 3 codec options |
| 2 | [Frame rate] x3 | RadioButton x3 | Frame rate selection |
| 3 | Record at half frame rate | Checkbox | Toggle |
| 4 | Encode duplicate frames as full | Checkbox | Toggle |
| 5 | Scaling | Combo | 5 scaling options |
| 6 | Aspect Ratio | Combo | 3 options |
| 7 | Resampling | Combo | 3 options |
| 8 | Record | Button | Starts recording |
| 9 | Cancel | Button | Closes dialog |

---

## 11. Tools Dialogs

### 11.1 Disk Explorer

**Setup:** `open_dialog DiskExplorer`

| # | Widget | Type | Test |
|---|--------|------|------|
| 1 | File > Open Disk Image... | MenuItem | Opens file dialog |
| 2 | File > Open Mounted Drive > [drive] | MenuItem xN | Opens mounted drive |
| 3 | File > Close | MenuItem | Closes explorer |
| 4 | View > Text / Hex / Executable / MAC-65 | MenuItem x5 | View mode radio |
| 5 | Write | Checkbox | Toggle write mode |
| 6 | [filename] entries | Selectable xN | File browser; context menu |
| 7 | > Export... | MenuItem | Conditional: not directory |
| 8 | > Delete | MenuItem | Conditional: writable |
| 9 | > Rename... | MenuItem | Conditional: writable |

### 11.2 First Time Setup Wizard

**Setup:** `open_dialog SetupWizard`

| # | Widget | Type | Test |
|---|--------|------|------|
| 1 | Computer (XL/XE) | RadioButton | Computer type |
| 2 | Atari 5200 | RadioButton | Computer type |
| 3 | NTSC (60 Hz) | RadioButton | Video standard |
| 4 | PAL (50 Hz) | RadioButton | Video standard |
| 5 | Authentic | RadioButton | Emulation mode |
| 6 | Convenient | RadioButton | Emulation mode |
| 7 | < Prev | Button | Navigate back |
| 8 | Next > | Button | Navigate forward |
| 9 | Finish | Button | Complete setup |
| 10 | Close | Button | Close without finishing |

### 11.3 Compatibility Database Editor

**Setup:** `open_dialog CompatDB`

| # | Widget | Type | Test |
|---|--------|------|------|
| 1 | File menu items (New/Load/Save/Compile) | MenuItem x6 | File operations |
| 2 | ##search | InputText | Title search filter |
| 3 | [Title name] entries | Selectable xN | Dynamic title list |
| 4 | Add Title | Button | Creates new title |
| 5 | Delete Title | Button | Conditional: title selected |

### 11.4 Advanced Configuration

**Setup:** `open_dialog AdvancedConfig`

| # | Widget | Type | Test |
|---|--------|------|------|
| 1 | ##filter | InputText | Variable search filter |
| 2 | [Variable name] entries | Selectable xN | Dynamic variable list |
| 3 | > Unset | MenuItem | Reset to default |
| 4 | ##editbool / ##editval | Checkbox/InputText | Per-variable editor |
| 5 | Close | Button | Closes dialog |

---

## 12. Display Settings Dialog

**Setup:** `open_dialog DisplaySettings`

| # | Widget | Type | Test |
|---|--------|------|------|
| 1 | Filter Mode | Combo | Point (Nearest) / Bilinear / Sharp Bilinear / Bicubic / Default (Any Suitable) |
| 2 | Stretch Mode | Combo | Fit to Window / Preserve Aspect Ratio / Square Pixels / Integer Scale / Integer + Aspect Ratio |
| 3 | Overscan Mode | Combo | Normal / Extended / Full / OS Screen Only / Widescreen |
| 4 | Show FPS | Checkbox | Toggle |
| 5 | Show Indicators | Checkbox | Toggle |
| 6 | Auto-Hide Mouse Pointer | Checkbox | Toggle |
| 7 | OK | Button | Closes dialog |

---

## 13. Audio Options Dialog

**Setup:** `open_dialog AudioOptions`

| # | Widget | Type | Test |
|---|--------|------|------|
| 1 | Volume | SliderInt | 0-200% |
| 2 | Drive volume | SliderInt | 0-200% |
| 3 | Covox volume | SliderInt | 0-200% |
| 4 | Latency | SliderInt | 1-50 |
| 5 | Extra buffer | SliderInt | 2-50 |
| 6 | Show debug info | Checkbox | Toggle |
| 7 | OK | Button | Closes dialog |

---

## 14. About / Change Log / Command-Line Help Dialogs

Simple read-only dialogs with an OK button.

| Dialog | Setup | Verify |
|--------|-------|--------|
| About | `open_dialog About` | Window "About AltirraSDL" visible; OK button exists |
| Change Log | `open_dialog ChangeLog` | Window "Change Log" visible; OK button exists |
| Command-Line Help | `open_dialog CommandLineHelp` | Window "Command-Line Help" visible; OK button exists |

---

## 15. Compatibility Warning Dialog

**Setup:** `open_dialog CompatWarning` (or triggered by loading a title with known issues)

| # | Widget | Type | Test |
|---|--------|------|------|
| 1 | Continue | Button | Resumes emulation |
| 2 | Adjust Settings | Button | Leaves paused for manual adjustment |
| 3 | Apply and Continue | Button | Applies recommended fixes and resumes |

---

## 16. Keyboard Shortcuts Dialog

**Setup:** `open_dialog KeyboardShortcuts`

Verify dialog opens and contains shortcut listings with an OK/Close button.

---

## 17. Debugger Panes

**Setup:** Enable debugger via Debug > Enable Debugger menu item, then open panes.

### 12.1 Console Pane

| # | Widget | Type | Test |
|---|--------|------|------|
| 1 | ##input | InputText | Command input with completion |

### 12.2 Disassembly Pane

| # | Widget | Type | Test |
|---|--------|------|------|
| 1 | ##addr | InputText | Hex address input |
| 2 | Go | Button | Navigate to address |
| 3 | Follow PC | Button | Follow program counter |
| 4 | [Instruction line] entries | Selectable xN | Click to toggle breakpoint |

### 12.3 Memory Pane

| # | Widget | Type | Test |
|---|--------|------|------|
| 1 | [Address space] selector | Selectable xN | Memory space |
| 2 | ##addr | InputText | Hex address input |
| 3 | Go | Button | Navigate to address |
| 4 | ##vmode | Combo | 4 view modes |
| 5 | ##imode | Combo | 3 input modes |
| 6 | [Memory line] entries | Selectable xN | Context menu: Toggle R/W breakpoints |

### 12.4 Breakpoints Pane

| # | Widget | Type | Test |
|---|--------|------|------|
| 1 | [Breakpoint entries] | Selectable xN | Context menu: Delete |

### 12.5 History Pane

| # | Widget | Type | Test |
|---|--------|------|------|
| 1 | Enable CPU History | Button | Conditional: shown when history not enabled |

### 12.6 Call Stack Pane

| # | Widget | Type | Test |
|---|--------|------|------|
| 1 | [Stack frame] entries | Selectable xN | Click to navigate |

---

## 18. Cross-Cutting Tests

Tests that verify behavior across multiple dialogs or interaction patterns.

### 18.1 Dialog Open/Close Cycle

For every dialog in `kDialogMap`:

```
open_dialog <Name>
wait_frames 2
query_state  # verify dialog flag is true
list_items   # verify widgets exist
close_dialog <Name>
wait_frames 2
query_state  # verify dialog flag is false
```

### 18.2 Menu Radio Group Exclusivity

For each radio-like menu group (filter mode, overscan, video frame, etc.):

```
# Click each option in sequence
# After each click, verify only that option is checked
# and all others are unchecked
```

### 18.3 Checkbox Persistence

For each checkbox in System Config:

```
open_dialog SystemConfig
# Navigate to page
# Record initial state
click "Configure System" "<checkbox label>"
wait_frames 2
# Verify state toggled
close_dialog SystemConfig
open_dialog SystemConfig
# Navigate back to same page
# Verify state persisted
```

### 18.4 Disabled Widget Verification

Verify widgets are disabled in the correct conditions:

| Condition | Disabled widgets |
|-----------|-----------------|
| No disk loaded | Detach Disk > Drive N, Eject button, write mode combo |
| No tape loaded | Cassette > Unload/Save/Export, tape transport buttons |
| No cartridge | Detach Cartridge, Secondary > Detach |
| Not recording | Stop Recording, Pause/Resume Recording |
| Debugger off | Window submenu, Run/Break/Step items |
| 5200 hardware | Video Standard combo, Toggle NTSC/PAL button |

### 18.5 State Consistency After Reset

```
cold_reset
wait_frames 30
query_state  # verify sim.running=true, sim.paused=false
```

### 18.6 Screenshot Verification

For visual regression testing of each dialog:

```
open_dialog <Name>
wait_frames 5
screenshot /tmp/test_<name>.bmp
close_dialog <Name>
```

Compare against reference screenshots.

---

## Test Execution Strategy

### Phase 1: Smoke Tests

1. Verify application starts in test mode
2. `ping` returns `{"ok":true}`
3. `query_state` returns valid JSON with all dialog flags
4. `list_dialogs` returns all 19 dialog names
5. Open and close each dialog (Section 13.1)

### Phase 2: Menu Completeness

For each top-level menu:
1. Click menu to open
2. `list_items` with menu window filter
3. Verify every item from Section 1 exists with correct label
4. Verify placeholder items are disabled

### Phase 3: Dialog Widget Audit

For each dialog (Sections 2-12):
1. Open dialog
2. `list_items` with dialog window name
3. Verify every widget from the section table exists
4. Verify types (checkbox, button, combo, etc.)
5. Verify initial state (checked/unchecked, enabled/disabled)

### Phase 4: Interaction Tests

For each interactive widget:
1. Click and verify state change
2. For checkboxes: verify toggle behavior
3. For combos: verify option cycling
4. For radio groups: verify exclusivity
5. For buttons: verify action effect

### Phase 5: Integration Tests

1. Hardware mode change -> verify video standard restrictions
2. Boot image -> verify MRU list updates
3. Attach/detach disk -> verify drive status updates
4. Profile switch -> verify settings change
5. Recording start/stop -> verify menu state changes
6. Debugger enable -> verify pane availability

### Phase 6: Visual Regression

Screenshot every dialog and compare against reference images
from Windows Altirra to verify layout matches.
