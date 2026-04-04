#!/usr/bin/env python3
"""
UI Coverage Verification Tool for AltirraSDL

Parses Windows Altirra's authoritative UI definitions (.rc files, menu_default.txt,
cmd*.cpp command registrations) and cross-references them against the SDL3/Dear ImGui
implementation to generate a coverage gap report.

Usage:
    python3 PORTING/verify_ui_coverage.py [--verbose] [--json]

Sources of truth (Windows side):
    - src/Altirra/res/menu_default.txt      → Menu items with command IDs
    - src/Altirra/res/Altirra.rc            → Dialogs, context menus, accelerators
    - src/Altirra/source/cmd*.cpp           → All registered command IDs

Implementation (SDL3 side):
    - src/AltirraSDL/source/ui_*.cpp        → ImGui UI implementation
    - src/AltirraSDL/source/main_sdl3.cpp   → Keyboard shortcuts, event handling
"""

import os
import re
import sys
import json
import argparse
from collections import defaultdict
from pathlib import Path

# Project root (script lives in PORTING/)
ROOT = Path(__file__).resolve().parent.parent
WIN_SRC = ROOT / "src" / "Altirra"
SDL_SRC = ROOT / "src" / "AltirraSDL"


# ─── Parsers for Windows side ─────────────────────────────────────────────

def parse_menu_default(path):
    """Extract all command IDs from menu_default.txt.

    Returns dict: command_id -> { 'label': str, 'parent_menu': str, 'line': int }
    """
    commands = {}
    current_menu = ""
    with open(path) as f:
        for lineno, line in enumerate(f, 1):
            stripped = line.rstrip()
            if not stripped:
                continue

            # Top-level menu (no leading whitespace or single tab)
            tabs = len(line) - len(line.lstrip('\t'))
            if tabs == 0 and stripped and not stripped.startswith('{'):
                current_menu = stripped.replace('&', '')

            # Extract command ID in braces
            m = re.search(r'\{(\w+\.\w+)\}', stripped)
            if m:
                cmd_id = m.group(1)
                # Get the label (everything before the command, stripped)
                label = re.sub(r'\s*\{.*\}', '', stripped).strip()
                label = label.replace('&', '').replace('%', '')
                commands[cmd_id] = {
                    'label': label,
                    'parent_menu': current_menu,
                    'line': lineno,
                    'source': 'menu_default.txt'
                }
    return commands


def parse_rc_dialogs(path):
    """Extract dialog IDs and their controls from .rc file.

    Returns dict: dialog_id -> { 'size': str, 'line': int, 'caption': str,
                                  'menu': str, 'controls': [...] }
    """
    dialogs = {}
    lines = open(path, encoding='utf-8', errors='replace').readlines()
    i = 0
    while i < len(lines):
        m = re.match(r'^(IDD_\w+)\s+DIALOGEX\s+(.+)', lines[i])
        if m:
            dlg_id = m.group(1)
            dlg = {
                'size': m.group(2).strip(),
                'line': i + 1,
                'caption': '',
                'menu': '',
                'controls': [],
            }
            # Parse dialog header and body
            j = i + 1
            in_body = False
            while j < len(lines):
                line = lines[j]
                if not in_body:
                    # Parse header lines
                    cm = re.match(r'^CAPTION\s+"([^"]*)"', line)
                    if cm:
                        dlg['caption'] = cm.group(1)
                    mm = re.match(r'^MENU\s+(\w+)', line)
                    if mm:
                        dlg['menu'] = mm.group(1)
                    if line.strip() == 'BEGIN':
                        in_body = True
                else:
                    if line.strip() == 'END':
                        break
                    # Parse controls with labels
                    # PUSHBUTTON "label", ID, ...
                    # DEFPUSHBUTTON "label", ID, ...
                    # CHECKBOX "label", ID, ...
                    # LTEXT "label", ID, ...
                    # RTEXT "label", ID, ...
                    # CTEXT "label", ID, ...
                    # GROUPBOX "label", ID, ...
                    # AUTORADIOBUTTON "label", ID, ...
                    # AUTOCHECKBOX "label", ID, ...
                    cm = re.match(
                        r'\s+(?:PUSHBUTTON|DEFPUSHBUTTON|CHECKBOX|LTEXT|RTEXT|CTEXT|'
                        r'GROUPBOX|AUTORADIOBUTTON|AUTOCHECKBOX)\s+"([^"]*)"(?:,\s*(\w+))?',
                        line)
                    if cm:
                        label = cm.group(1).replace('&', '')
                        ctrl_id = cm.group(2) or ''
                        if label and label not in ('OK', 'Cancel', '', '...', 'Static'):
                            dlg['controls'].append({
                                'label': label,
                                'id': ctrl_id,
                            })
                    # CONTROL "label", ID, "class", ...
                    cm2 = re.match(
                        r'\s+CONTROL\s+"([^"]*)",\s*(\w+),\s*"([^"]*)"', line)
                    if cm2:
                        label = cm2.group(1).replace('&', '')
                        ctrl_id = cm2.group(2)
                        ctrl_class = cm2.group(3)
                        # Include checkboxes and radio buttons from CONTROL syntax
                        if label and ctrl_class.lower() in ('button',):
                            dlg['controls'].append({
                                'label': label,
                                'id': ctrl_id,
                            })
                j += 1
            dialogs[dlg_id] = dlg
            i = j + 1
        else:
            i += 1
    return dialogs


def parse_rc_context_menus(path):
    """Extract context menu definitions and their items from .rc file.

    Returns dict: menu_id -> { 'items': [str], 'line': int }
    """
    menus = {}
    current_menu = None
    with open(path, encoding='utf-8', errors='replace') as f:
        for lineno, line in enumerate(f, 1):
            m = re.match(r'^(IDR_\w+)\s+MENU', line)
            if m:
                current_menu = m.group(1)
                menus[current_menu] = {'items': [], 'line': lineno}
                continue
            if current_menu:
                # MENUITEM "Text", ID_COMMAND
                mi = re.match(r'\s+MENUITEM\s+"([^"]*)"(?:,\s*(\w+))?', line)
                if mi:
                    text = mi.group(1)
                    cmd = mi.group(2) or ''
                    if text != 'SEPARATOR':
                        menus[current_menu]['items'].append({
                            'label': text.replace('&', ''),
                            'win_id': cmd,
                        })
                # End of menu block
                if line.strip() == 'END' and 'BEGIN' not in line:
                    # Keep parsing - nested BEGIN/END
                    pass
                # New non-menu resource starts
                if re.match(r'^\w+\s+(MENU|DIALOGEX|ACCELERATORS|ICON|BITMAP)', line) and current_menu and lineno > menus[current_menu]['line'] + 1:
                    current_menu = None
    return menus


def parse_rc_accelerators(path):
    """Extract accelerator table entries from .rc file.

    Returns dict: table_name -> [{ 'key': str, 'command': str }]
    """
    tables = {}
    current_table = None
    with open(path, encoding='utf-8', errors='replace') as f:
        for lineno, line in enumerate(f, 1):
            m = re.match(r'^(\w+)\s+ACCELERATORS', line)
            if m:
                current_table = m.group(1)
                tables[current_table] = []
                continue
            if current_table:
                # "key", ID_COMMAND, VIRTKEY [, modifiers]
                am = re.match(r'\s+(?:"(.)"|(VK_\w+)),\s*(\w+)', line)
                if am:
                    key = am.group(1) or am.group(2)
                    cmd = am.group(3)
                    tables[current_table].append({'key': key, 'command': cmd, 'line': lineno})
                if line.strip() == 'END':
                    current_table = None
    return tables


def parse_cmd_commands(src_dir):
    """Extract all registered command IDs from cmd*.cpp files.

    Returns dict: command_id -> { 'file': str, 'line': int }
    """
    commands = {}
    cmd_files = sorted(src_dir.glob("source/cmd*.cpp"))
    for fpath in cmd_files:
        with open(fpath) as f:
            for lineno, line in enumerate(f, 1):
                # Match: { "Category.Command"
                m = re.search(r'"\s*((\w+)\.(\w+))\s*"', line)
                if m:
                    cmd_id = m.group(1)
                    # Filter out non-command strings (ATMake helpers produce the same pattern)
                    if '.' in cmd_id and cmd_id[0].isupper():
                        commands[cmd_id] = {
                            'file': fpath.name,
                            'line': lineno,
                        }
    return commands


# ─── Scanner for SDL3/ImGui side ──────────────────────────────────────────

def scan_sdl_sources(src_dir):
    """Scan all SDL3 source files for references to command IDs, dialog names,
    menu items, keyboard shortcuts, and context menus.

    Returns:
        referenced_commands: set of command ID strings found in SDL sources
        source_text: dict of filename -> full text (for detailed searches)
        all_string_literals: set of all quoted string literals in SDL sources
    """
    referenced_commands = set()
    source_text = {}
    all_string_literals = set()

    source_files = list(src_dir.glob("source/*.cpp")) + list(src_dir.glob("source/*.h"))
    for fpath in source_files:
        with open(fpath, errors='replace') as f:
            text = f.read()
            source_text[fpath.name] = text

            # Find command ID references (e.g., "File.BootImage" or File.BootImage in comments)
            for m in re.finditer(r'["\s](\w+\.\w+)["\s,)]', text):
                cmd = m.group(1)
                if cmd[0].isupper() and '.' in cmd:
                    referenced_commands.add(cmd)

            # Also find partial references like category names
            for m in re.finditer(r'"([A-Z]\w+(?:\.\w+)+)"', text):
                referenced_commands.add(m.group(1))

            # Collect all string literals for label matching
            # Only match single-line strings (no newlines within quotes)
            for m in re.finditer(r'"([^"\n]{2,})"', text):
                all_string_literals.add(m.group(1))

    return referenced_commands, source_text, all_string_literals


def find_placeholder_items(source_text):
    """Find menu items marked as placeholder/TODO in ImGui code."""
    placeholders = set()
    for fname, text in source_text.items():
        # Pattern: ImGui::MenuItem("...", ...) followed by // TODO or // placeholder
        # Or: items that are greyed out with (false) enable flag and comment
        for m in re.finditer(
            r'(?:MenuItem|BeginMenu)\s*\(\s*"([^"]+)"[^)]*\)[^;]*;\s*//\s*(?:TODO|placeholder|PLACEHOLDER|stub|STUB|not implemented)',
            text, re.IGNORECASE
        ):
            placeholders.add(m.group(1).replace('###', '').strip())

        # Also find disabled items: if (false) { ... MenuItem("X") }
        for m in re.finditer(
            r'ImGui::MenuItem\s*\(\s*"([^"]+)"[^)]*false\s*\)',
            text
        ):
            # This catches MenuItem("label", shortcut, false, false) where last arg is enabled=false
            pass  # Too many false positives, skip
    return placeholders


def find_imgui_menu_items(source_text):
    """Extract all ImGui::MenuItem and BeginMenu calls."""
    items = {}
    for fname, text in source_text.items():
        for m in re.finditer(r'ImGui::MenuItem\s*\(\s*"([^"]*)"', text):
            label = m.group(1).split("###")[0].strip()
            if label and label != '---':
                items[label] = fname
        for m in re.finditer(r'ImGui::BeginMenu\s*\(\s*"([^"]*)"', text):
            label = m.group(1).split("###")[0].strip()
            if label:
                items[label] = fname
    return items


def find_keyboard_shortcuts(source_text):
    """Extract keyboard shortcut definitions from SDL sources."""
    shortcuts = {}
    for fname, text in source_text.items():
        # SDL key event handling patterns
        for m in re.finditer(
            r'(?:SDLK_|SDL_SCANCODE_)(\w+).*?//.*?(\w[\w\s+]+)',
            text
        ):
            shortcuts[m.group(1)] = {'comment': m.group(2).strip(), 'file': fname}

        # MenuItem with shortcut hint
        for m in re.finditer(
            r'MenuItem\s*\(\s*"([^"]+)"\s*,\s*"([^"]+)"',
            text
        ):
            shortcuts[m.group(2)] = {'label': m.group(1), 'file': fname}
    return shortcuts


# ─── Categorization ───────────────────────────────────────────────────────

# Commands that are Windows-only and not applicable to SDL3
WINDOWS_ONLY_COMMANDS = {
    'Options.ToggleSingleInstance',
    'Options.SetFileAssocForAll', 'Options.SetFileAssocForUser',
    'Options.UnsetFileAssocForAll', 'Options.UnsetFileAssocForUser',
    'Options.ToggleDisplayD3D9', 'Options.ToggleDisplayD3D11',
    'Help.CheckForUpdates',
}

# Commands implemented via loops/tables — the command ID itself doesn't appear
# as a string literal, but the functionality is present.
IMPLEMENTED_VIA_LOOPS = set()
# Disk attach/detach 1-8 (loop in ui_menus.cpp)
for i in range(1, 9):
    IMPLEMENTED_VIA_LOOPS.add(f'Disk.Attach{i}')
    IMPLEMENTED_VIA_LOOPS.add(f'Disk.Detach{i}')
# Memory/Watch panes 1-4 (loop in ui_menus.cpp)
for i in range(1, 5):
    IMPLEMENTED_VIA_LOOPS.add(f'Pane.Memory{i}')
    IMPLEMENTED_VIA_LOOPS.add(f'Pane.Watch{i}')
# Console device buttons (table-driven in ui_menus.cpp)
IMPLEMENTED_VIA_LOOPS.update({
    'Console.BlackBoxDumpScreen', 'Console.BlackBoxMenu',
    'Console.IDEPlus2SwitchDisks', 'Console.IDEPlus2WriteProtect',
    'Console.IDEPlus2SDX',
    'Console.IndusGTError', 'Console.IndusGTTrack', 'Console.IndusGTId',
    'Console.IndusGTBootCPM', 'Console.IndusGTChangeDensity',
    'Console.HappyToggleFastSlow', 'Console.HappyToggleWriteProtect',
    'Console.HappyToggleWriteEnable',
    'Console.ATR8000Reset', 'Console.XELCFSwap',
})
# Special cartridge types (table-driven in ui_menus.cpp)
IMPLEMENTED_VIA_LOOPS.update({
    'Cart.AttachSC3D', 'Cart.AttachMaxFlash1MB', 'Cart.AttachMaxFlash1MBMyIDE',
    'Cart.AttachMaxFlash8MB', 'Cart.AttachMaxFlash8MBBank0',
    'Cart.AttachJACart128K', 'Cart.AttachJACart256K', 'Cart.AttachJACart512K',
    'Cart.AttachJACart1024K', 'Cart.AttachDCart',
    'Cart.AttachSIC', 'Cart.AttachSIC256K', 'Cart.AttachSIC128K',
    'Cart.AttachSICPlus',
    'Cart.AttachMegaCart512K', 'Cart.AttachMegaCart4MB',
    'Cart.AttachTheCart32MB', 'Cart.AttachTheCart64MB', 'Cart.AttachTheCart128MB',
})

# Commands that are settings exposed in Configure System dialog (ui_system.cpp)
# rather than standalone menu commands.  These are "implemented differently" —
# the underlying setting is handled via direct API calls in the system config
# paged dialog, not through a command dispatch system.
# This set is populated by examining ui_system.cpp categories.
SETTINGS_IN_SYSCONFIG = {
    # System/Hardware
    'System.HardwareMode800', 'System.HardwareMode800XL', 'System.HardwareMode130XE',
    'System.HardwareMode1200XL', 'System.HardwareMode1400XL',
    'System.HardwareMode5200', 'System.HardwareModeXEGS',
    'System.CPUMode6502', 'System.CPUMode65C02', 'System.CPUMode65C816',
    'System.CPUMode65C816x2', 'System.CPUMode65C816x4', 'System.CPUMode65C816x6',
    'System.CPUMode65C816x8', 'System.CPUMode65C816x10', 'System.CPUMode65C816x12',
    'System.CPUMode65C816x23',
    'System.ToggleCPUIllegalInstructions', 'System.ToggleCPUNMIBlocking',
    'System.ToggleCPUStopOnBRK', 'System.ToggleCPUHistory',
    'System.ToggleCPUPathTracing', 'System.CPUOptionsDialog',
    # Memory
    'System.MemoryMode8K', 'System.MemoryMode16K', 'System.MemoryMode24K',
    'System.MemoryMode32K', 'System.MemoryMode40K', 'System.MemoryMode48K',
    'System.MemoryMode52K', 'System.MemoryMode64K', 'System.MemoryMode128K',
    'System.MemoryMode256K', 'System.MemoryMode320K', 'System.MemoryMode320KCompy',
    'System.MemoryMode576K', 'System.MemoryMode576KCompy', 'System.MemoryMode1088K',
    'System.HighMemoryNone', 'System.HighMemoryNA', 'System.HighMemory64K',
    'System.HighMemory192K', 'System.HighMemory960K', 'System.HighMemory4032K',
    'System.HighMemory16320K',
    'System.AxlonMemoryNone', 'System.AxlonMemory64K', 'System.AxlonMemory128K',
    'System.AxlonMemory256K', 'System.AxlonMemory512K', 'System.AxlonMemory1024K',
    'System.AxlonMemory2048K', 'System.AxlonMemory4096K',
    'System.ToggleAxlonAliasing', 'System.ToggleMapRAM', 'System.ToggleUltimate1MB',
    'System.ToggleFloatingIOBus', 'System.TogglePreserveExtRAM',
    'System.ToggleShadowROM', 'System.ToggleShadowCarts',
    'System.MemoryClearZero', 'System.MemoryClearRandom',
    'System.MemoryClearDRAM1', 'System.MemoryClearDRAM2', 'System.MemoryClearDRAM3',
    # Firmware/Kernel
    'System.KernelModeDefault', 'System.KernelModeHLE', 'System.KernelModeLLE',
    'System.KernelModeLLEXL', 'System.KernelMode5200LLE',
    'System.BasicDefault', 'System.ROMImagesDialog',
    # Speed
    'System.SpeedOptionsDialog', 'System.FrameRateModeHardware',
    'System.FrameRateModeBroadcast', 'System.FrameRateModeIntegral',
    'System.ToggleVSyncAdaptiveSpeed',
    # Video
    'Video.StandardNTSC', 'Video.StandardPAL', 'Video.StandardSECAM',
    'Video.StandardNTSC50', 'Video.StandardPAL60',
    'Video.ToggleStandardNTSCPAL',
    'Video.ArtifactingNone', 'Video.ArtifactingNTSC', 'Video.ArtifactingNTSCHi',
    'Video.ArtifactingPAL', 'Video.ArtifactingPALHi',
    'Video.ArtifactingAuto', 'Video.ArtifactingAutoHi', 'Video.ArtifactingNextMode',
    'Video.GTIADefectNone', 'Video.GTIADefectType1', 'Video.GTIADefectType2',
    'Video.ToggleCTIA', 'Video.PALPhase0', 'Video.PALPhase1',
    'Video.MonitorModeColor', 'Video.MonitorModeMonoGreen',
    'Video.MonitorModeMonoAmber', 'Video.MonitorModeMonoWhite',
    'Video.MonitorModeMonoBluishWhite', 'Video.MonitorModePERITEL',
    'Video.ToggleXEP80', 'Video.ToggleBloom', 'Video.ToggleScanlines',
    'Video.ToggleFrameBlending', 'Video.ToggleLinearFrameBlending',
    'Video.ToggleInterlace', 'Video.ToggleMonoPersistence',
    'Video.DeinterlaceModeNone', 'Video.DeinterlaceModeAdaptiveBob',
    'Video.EnhancedModeNone', 'Video.EnhancedModeCIO', 'Video.EnhancedModeHardware',
    'Video.EnhancedTextFontDialog',
    # Audio
    'Audio.ToggleStereo', 'Audio.ToggleStereoAsMono', 'Audio.ToggleMonitor',
    'Audio.ToggleMute', 'Audio.ToggleScope', 'Audio.ToggleCovox',
    'Audio.ToggleNonlinearMixing', 'Audio.ToggleSpeakerFilter',
    'Audio.ToggleSerialNoise', 'Audio.ToggleSlightSid',
    'Audio.ToggleSecondaryChannel1', 'Audio.ToggleSecondaryChannel2',
    'Audio.ToggleSecondaryChannel3', 'Audio.ToggleSecondaryChannel4',
    'Audio.OptionsDialog',
    # Acceleration / SIO / Devices
    'Devices.ToggleSIOPatch', 'Devices.ToggleSIOBurstTransfers',
    'Devices.SIOAccelModePatch', 'Devices.SIOAccelModePBI',
    'Devices.SIOAccelModeBoth',
    'Devices.ToggleCIOPatchH', 'Devices.ToggleCIOPatchP',
    'Devices.ToggleCIOPatchR', 'Devices.ToggleCIOPatchT',
    'Devices.ToggleCIOBurstTransfers',
    'Devices.CIOHookModeHw', 'Devices.CIOHookModePBI',
    'Disk.ToggleSIOPatch', 'Disk.ToggleBurstTransfers',
    'Disk.ToggleAccurateSectorTiming', 'Disk.ToggleDriveSounds',
    'Disk.ToggleSIOOverrideDetection', 'Disk.ToggleSectorCounter',
    # Cassette
    'Cassette.ToggleSIOPatch', 'Cassette.ToggleAutoRewind',
    'Cassette.ToggleAutoBasicBoot', 'Cassette.ToggleLoadDataAsAudio',
    'Cassette.ToggleRandomizeStartPosition', 'Cassette.ToggleCrosstalkReduction',
    'Cassette.ToggleVBIAvoidance', 'Cassette.ToggleTurboPrefilter',
    'Cassette.ToggleFSKSpeedCompensation', 'Cassette.TogglePolarity',
    'Cassette.PolarityNormal', 'Cassette.PolarityInverted',
    'Cassette.DirectSenseNormal', 'Cassette.DirectSenseLowSpeed',
    'Cassette.DirectSenseHighSpeed', 'Cassette.DirectSenseMaxSpeed',
    'Cassette.TurboModeNone', 'Cassette.TurboModeCommandControl',
    'Cassette.TurboModeDataControl', 'Cassette.TurboModeProceedSense',
    'Cassette.TurboModeInterruptSense', 'Cassette.TurboModeKSOTurbo2000',
    'Cassette.TurboModeTurboD', 'Cassette.TurboModeAlways',
    'Cassette.TurboDecoderPeakFilter', 'Cassette.TurboDecoderPeakHiLo',
    'Cassette.TurboDecoderPeakLoHi', 'Cassette.TurboDecoderSlopeFilter',
    'Cassette.TurboDecoderSlopeNoFilter',
    # Input settings
    'Input.KeyboardLayoutNatural', 'Input.KeyboardLayoutDirect',
    'Input.KeyboardLayoutCustom',
    'Input.KeyboardModeCooked', 'Input.KeyboardModeRaw', 'Input.KeyboardModeFullScan',
    'Input.KeyboardArrowModeDefault', 'Input.KeyboardArrowModeAutoCtrl',
    'Input.KeyboardArrowModeRaw',
    'Input.KeyboardCopyToCustomLayout', 'Input.KeyboardCustomizeLayoutDialog',
    'Input.Toggle1200XLFunctionKeys', 'Input.ToggleAllowShiftOnReset',
    'Input.ToggleAllowInputMapKeyboardOverlap',
    'Input.ToggleAllowInputMapKeyboardModifierOverlap',
    'Input.ToggleRawInputEnabled',
    'Input.ToggleImmediatePotUpdate', 'Input.TogglePotNoise',
    'Input.ToggleImmediateLightPenUpdate',
    'Input.KeyboardDialog',
    # Boot
    'Options.ToggleBootUnloadCartridges', 'Options.ToggleBootUnloadDisks',
    'Options.ToggleBootUnloadTapes',
    'Options.ToggleAutoResetBasic', 'Options.ToggleAutoResetCartridge',
    'Options.ToggleAutoResetVideoStandard',
    'System.ProgramLoadModeDefault', 'System.ProgramLoadModeDeferred',
    'System.ProgramLoadModeDiskBoot', 'System.ProgramLoadModeType3Poll',
    'System.ToggleFPPatch', 'System.ToggleFastBoot',
    'System.ToggleProgramLaunchDelayRandomization',
    'System.ToggleRTime8', 'System.PulseWarpOff',
    # Debugger settings
    'Debug.ToggleAutoLoadKernelSymbols', 'Debug.ToggleAutoLoadSystemSymbols',
    'Debug.ToggleDebugLink',
    'Debug.PreStartSymbolLoadDisabled', 'Debug.PreStartSymbolLoadEnabled',
    'Debug.PreStartSymbolLoadDeferred',
    'Debug.PostStartSymbolLoadDisabled', 'Debug.PostStartSymbolLoadEnabled',
    'Debug.PostStartSymbolLoadDeferred',
    'Debug.ScriptAutoLoadDisabled', 'Debug.ScriptAutoLoadEnabled',
    'Debug.ScriptAutoLoadAskToLoad',
    # Options/UI settings
    'Options.ToggleLaunchAutoProfile', 'Options.ToggleDirectoryPolling',
    'Options.ResetAllDialogs', 'Options.PauseDuringMenu',
    'Options.UseDarkTheme',
    'Options.ToggleDisplay16Bit', 'Options.ToggleDisplayCustomRefresh',
    'Options.MediaDefaultModeRO', 'Options.MediaDefaultModeRW',
    'Options.MediaDefaultModeVRW', 'Options.MediaDefaultModeVRWSafe',
    'Options.ErrorModeDialog', 'Options.ErrorModePause',
    'Options.ErrorModeReset', 'Options.ErrorModeDebug',
    'Options.EfficiencyModeDefault', 'Options.EfficiencyModePerformance',
    'Options.EfficiencyModeEfficiency',
    'System.DevicesDialog',
    # View settings in sysconfig
    'View.ToggleAccelScreenFX', 'View.ToggleAutoHideMenu',
    'View.ToggleAutoHidePointer', 'View.ToggleConstrainPointerFullScreen',
    'View.ToggleIndicators', 'View.TogglePadBounds', 'View.TogglePadPointers',
    'View.ToggleReaderEnabled', 'View.ToggleTargetPointer',
    'View.ToggleXEP80View', 'View.ToggleXEP80ViewAutoswitching',
    'View.VideoOutputPrev', 'View.EffectClear', 'View.EffectReload',
    # Rewind
    'System.ToggleRewindRecording',
    # Recording
    'Record.Pause', 'Record.Resume',
    # Tools
    'Tools.OptionsDialog',
}


# ─── Main report generation ──────────────────────────────────────────────

def generate_report(verbose=False, output_json=False):
    menu_path = WIN_SRC / "res" / "menu_default.txt"
    rc_path = WIN_SRC / "res" / "Altirra.rc"

    # Parse Windows sources
    menu_commands = parse_menu_default(menu_path)
    cmd_commands = parse_cmd_commands(WIN_SRC)
    rc_dialogs = parse_rc_dialogs(rc_path)
    rc_context_menus = parse_rc_context_menus(rc_path)
    rc_accelerators = parse_rc_accelerators(rc_path)

    # All known Windows commands (union of menu + cmd*.cpp)
    all_win_commands = {}
    all_win_commands.update(cmd_commands)
    for cmd_id, info in menu_commands.items():
        if cmd_id not in all_win_commands:
            all_win_commands[cmd_id] = info
        else:
            all_win_commands[cmd_id]['label'] = info.get('label', '')
            all_win_commands[cmd_id]['parent_menu'] = info.get('parent_menu', '')

    # Scan SDL3 sources
    sdl_commands, sdl_source_text, sdl_string_literals = scan_sdl_sources(SDL_SRC)
    imgui_menu_items = find_imgui_menu_items(sdl_source_text)
    placeholders = find_placeholder_items(sdl_source_text)
    all_sdl_text_combined = '\n'.join(sdl_source_text.values())

    def label_found_in_sdl(label, strict=False):
        """Check if a menu label appears in SDL sources.

        Args:
            label: The label text to search for
            strict: If True, require exact or near-exact match only.
                    If False, also allow substring matching for longer labels.
        """
        if not label:
            return False
        # Exact literal match
        if label in sdl_string_literals:
            return True
        # Case-insensitive exact match
        label_lower = label.lower()
        for lit in sdl_string_literals:
            if label_lower == lit.lower():
                return True
        # Strip trailing "..." and try again (ImGui and RC may differ on ellipsis)
        clean = label.rstrip('.').strip()
        clean_lower = clean.lower()
        for lit in sdl_string_literals:
            lit_clean = lit.rstrip('.').strip().lower()
            if clean_lower == lit_clean:
                return True

        if strict:
            return False

        # For non-strict mode: fuzzy matching for longer labels (>8 chars)
        if len(label_lower) > 8:
            for item_label in imgui_menu_items:
                if label_lower in item_label.lower() or item_label.lower() in label_lower:
                    return True
        return False

    # ── Cross-reference ──

    # All pre-classified commands
    all_known_implemented = IMPLEMENTED_VIA_LOOPS | SETTINGS_IN_SYSCONFIG | WINDOWS_ONLY_COMMANDS

    # 1. Menu command coverage
    menu_covered = set()
    menu_missing = set()
    menu_placeholder = set()
    menu_windows_only = set()

    for cmd_id, info in menu_commands.items():
        label = info.get('label', '')
        if cmd_id in WINDOWS_ONLY_COMMANDS:
            menu_windows_only.add(cmd_id)
        elif cmd_id in sdl_commands or cmd_id in IMPLEMENTED_VIA_LOOPS:
            menu_covered.add(cmd_id)
        elif label_found_in_sdl(label):
            menu_covered.add(cmd_id)
        elif label in placeholders:
            menu_placeholder.add(cmd_id)
        else:
            menu_missing.add(cmd_id)

    # 2. cmd*.cpp command coverage — classified into buckets
    cmd_covered = set()        # Found directly in SDL sources
    cmd_sysconfig = set()      # Implemented in Configure System dialog
    cmd_windows_only = set()   # N/A on SDL3
    cmd_missing = set()        # Genuinely not found
    for cmd_id in cmd_commands:
        if cmd_id in sdl_commands or cmd_id in IMPLEMENTED_VIA_LOOPS:
            cmd_covered.add(cmd_id)
        elif cmd_id in menu_covered:
            cmd_covered.add(cmd_id)
        elif cmd_id in WINDOWS_ONLY_COMMANDS:
            cmd_windows_only.add(cmd_id)
        elif cmd_id in SETTINGS_IN_SYSCONFIG:
            cmd_sysconfig.add(cmd_id)
        else:
            # Also try label-based matching from menu_commands
            info = menu_commands.get(cmd_id, {})
            label = info.get('label', '')
            if label_found_in_sdl(label):
                cmd_covered.add(cmd_id)
            else:
                cmd_name = cmd_id.split('.', 1)[1] if '.' in cmd_id else ''
                if cmd_name and cmd_name in all_sdl_text_combined:
                    cmd_covered.add(cmd_id)
                else:
                    cmd_missing.add(cmd_id)

    # 3. Dialog coverage
    dialog_covered = set()
    dialog_missing = set()
    # Map known dialog IDs to SDL3 implementation files
    dialog_to_sdl_keywords = {
        'IDD_DISK_DRIVES': ['ui_disk', 'Disk Drives'],
        'IDD_CREATE_DISK': ['ui_disk', 'Create Disk', 'New Disk'],
        'IDD_ABOUT': ['ui_main', 'About'],
        'IDD_CHANGE_LOG': ['ui_main', 'Change Log'],
        'IDD_INPUT_MAPPINGS': ['ui_input', 'Input Mappings'],
        'IDD_INPUTMAP_EDIT': ['ui_input', 'Edit Input Map'],
        'IDD_INPUTMAP_ADDMAPPING': ['ui_input', 'Add Mapping'],
        'IDD_AUDIO_OPTIONS': ['ui_system', 'Audio'],
        'IDD_TAPE_CONTROL': ['ui_cassette', 'Tape Control'],
        'IDD_CARTRIDGE_MAPPER': ['ui_cartmapper', 'Cartridge Mapper'],
        'IDD_LIGHTPEN': ['ui_lightpen', 'Light Pen'],
        'IDD_CHEATER': ['ui_cheater', 'Cheater'],
        'IDD_PROFILES': ['ui_profiles', 'Profile'],
        'IDD_FIRMWARE': ['ui_firmware', 'Firmware'],
        'IDD_FIRMWARE_EDIT': ['ui_firmware', 'Edit Firmware'],
        'IDD_ADJUST_COLORS': ['ui_display', 'Adjust Colors'],
        'IDD_ADJUST_SCREENFX': ['ui_screenfx', 'Screen Effects'],
        'IDD_CONFIGURE': ['ui_system', 'Configure System', 'System Configuration'],
        'IDD_CONFIGURE_SYSTEM': ['ui_system', 'Configure System'],
        'IDD_CONFIGURE_MEMORY': ['ui_system', 'Memory'],
        'IDD_CONFIGURE_VIDEO': ['ui_system', 'Video'],
        'IDD_CONFIGURE_AUDIO': ['ui_system', 'Audio'],
        'IDD_CONFIGURE_DISK': ['ui_system', 'Disk'],
        'IDD_CONFIGURE_CASSETTE': ['ui_system', 'Cassette'],
        'IDD_CONFIGURE_CPU': ['ui_system', 'CPU'],
        'IDD_CONFIGURE_ACCELERATION': ['ui_system', 'Acceleration'],
        'IDD_CONFIGURE_FIRMWARE': ['ui_system', 'Firmware'],
        'IDD_CONFIGURE_DEVICES': ['ui_system', 'Devices'],
        'IDD_CONFIGURE_SPEED': ['ui_system', 'Speed'],
        'IDD_CONFIGURE_KEYBOARD': ['ui_system', 'Keyboard'],
        'IDD_CONFIGURE_DISPLAY': ['ui_system', 'Display'],
        'IDD_CONFIGURE_DISPLAY2': ['ui_system', 'Display'],
        'IDD_CONFIGURE_DEBUGGER': ['ui_system', 'Debugger'],
        'IDD_CONFIGURE_EASEOFUSE': ['ui_system', 'Ease of Use'],
        'IDD_CONFIGURE_BOOT': ['ui_system', 'Boot'],
        'IDD_CONFIGURE_CAPTION': ['ui_system', 'Caption'],
        'IDD_CONFIGURE_WORKAROUNDS': ['ui_system', 'Workarounds'],
        'IDD_CONFIGURE_ACCESSIBILITY': ['ui_system', 'Accessibility'],
        'IDD_CONFIGURE_UI': ['ui_system', 'UI'],
        'IDD_CONFIGURE_INPUT': ['ui_system', 'Input'],
        'IDD_CONFIGURE_SETTINGS': ['ui_system', 'Settings'],
        'IDD_CONFIGURE_COMPATDB': ['ui_system', 'Compat'],
        'IDD_CONFIGURE_MEDIADEFAULTS': ['ui_system', 'Media Defaults'],
        'IDD_CONFIGURE_ASSESSMENT': ['ui_system', 'Assessment', 'Recommendations'],
        'IDD_CONFIGURE_OVERVIEW': ['ui_system', 'Overview'],
        'IDD_CONFIGURE_ERRORS': ['ui_system', 'Error'],
        'IDD_CONFIGURE_ENHANCEDTEXT': ['ui_system', 'Enhanced Text'],
        'IDD_CONFIGURE_FLASH': ['ui_system', 'Flash'],
        'IDD_CONFIGURE_FILETYPES': ['ui_system', 'File Types'],
        'IDD_VIDEO_RECORDING': ['ui_recording', 'Record Video'],
        'IDD_DISK_EXPLORER': ['ui_tools', 'Disk Explorer'],
        'IDD_INPUT_SETUP': ['ui_input', 'Input Setup'],
        'IDD_KEYBOARD_CUSTOMIZE': ['ui_tools', 'Keyboard'],
        'IDD_DEVICE_NEW': ['ui_system', 'Add Device'],
        'IDD_WIZARD': ['ui_tools', 'Setup Wizard', 'First Time'],
        'IDD_COMPATIBILITY': ['ui_system', 'Compatibility'],
        'IDD_ADVANCED_CONFIGURATION': ['ui_tools', 'Advanced Config'],
        'IDD_CMDLINEHELP': ['ui_main', 'Command-Line'],
        'IDD_DBG_BREAKPOINTS': ['ui_dbg_breakpoints', 'Breakpoint'],
        'IDD_DEBUG_BREAKPOINT': ['ui_dbg_breakpoints', 'Breakpoint'],
        'IDD_DBG_TARGETS': ['ui_dbg_targets', 'Targets'],
        # Dialogs that exist in SDL3 but weren't in original mapping
        'IDD_DRAGONCART': ['ui_devconfig', 'dragoncart'],
        'IDD_PCLINK': ['ui_devconfig', 'pclink', 'RenderPCLinkConfig'],
        'IDD_HDEVICE': ['ui_devconfig', 'hostfs', 'RenderHostFSConfig'],
        'IDD_CHEAT_EDIT': ['ui_cheater', 'Edit Cheat'],
        'IDD_CREATEINPUTMAP': ['ui_input', 'RenderCreateInputMap', 'Create Input Map'],
        'IDD_INPUTMAP_ADDCONTROLLER': ['ui_input', 'Add Controller'],
        'IDD_INPUTMAP_REBIND': ['ui_input', 'RenderRebindDialog', 'Rebind'],
        'IDD_REWIND': ['ui_rewind', 'Rewind'],
        'IDD_CONFIGURE_ASSESSMENT': ['ui_system', 'Recommendations'],
        # CompatDB sub-dialogs are popups inside the main CompatDB editor
        'IDD_COMPATDB_EDITOR': ['ui_tools', 'Compatibility Database'],
        'IDD_COMPATDB_ADDTAG': ['ui_tools', 'Select Tag'],
        'IDD_COMPATDB_EDITALIAS': ['ui_tools', 'Edit Alias'],
        # Setup Wizard pages are consolidated into a single multi-page dialog
        'IDD_WIZARD_EXPERIENCE': ['ui_tools', 'Setup Wizard', 'First Time Setup'],
        'IDD_WIZARD_FIRMWARE': ['ui_tools', 'Setup Wizard', 'First Time Setup'],
        'IDD_WIZARD_MESSAGE': ['ui_tools', 'Setup Wizard', 'First Time Setup'],
        'IDD_WIZARD_SELECTTYPE': ['ui_tools', 'Setup Wizard', 'First Time Setup'],
        'IDD_WIZARD_SELECTVIDEOTYPE': ['ui_tools', 'Setup Wizard', 'First Time Setup'],
        # IDD_OPTIONS is the old paged-options dialog, replaced by Configure System
        'IDD_OPTIONS': ['ui_system', 'System Configuration'],
        # IDD_EDIT_VAR is inline editing in Advanced Configuration
        'IDD_EDIT_VAR': ['ui_tools', 'Advanced Config'],
        # IDD_FILEVIEW is built into Disk Explorer as the file viewer pane
        'IDD_FILEVIEW': ['ui_tools', 'FileView'],
        # IDD_FIRMWARE_KNOWN is a sub-dialog inside Firmware manager
        'IDD_FIRMWARE_KNOWN': ['ui_firmware', 'Firmware'],
        # IDD_ROM_IMAGES is the old firmware dialog, now IDD_FIRMWARE
        'IDD_ROM_IMAGES': ['ui_firmware', 'Firmware'],
        # IDD_DEBUG_OPENSOURCEFILE uses SDL3 file dialog directly
        'IDD_DEBUG_OPENSOURCEFILE': ['ui_menus', 'OpenSourceFile', 'Source Files'],
        # IDD_DEBUG_MEMORYCTL is the address bar built into memory pane
        'IDD_DEBUG_MEMORYCTL': ['ui_dbg_memory', 'Address'],
    }

    all_sdl_text = '\n'.join(sdl_source_text.values()).lower()

    # Dialogs that are Windows-only or handled differently in ImGui
    DIALOG_NOT_APPLICABLE = {
        # Generic property controls — ImGui handles inline, no separate dialog needed
        'IDD_CFGPROP_CHECKBOX', 'IDD_CFGPROP_COLOR', 'IDD_CFGPROP_DROPDOWN',
        'IDD_CFGPROP_EDIT', 'IDD_CFGPROP_GENERIC', 'IDD_CFGPROP_PATH',
        'IDD_CFGPROP_SLIDER', 'IDD_CFGPROP_TEXT',
        # Windows file association dialog
        'IDD_FILE_ASSOC2',
        # Windows-specific display mode selection
        'IDD_OPTIONS_DISPLAY_MODE',
        # Physical disk selection (Windows raw disk access)
        'IDD_SELECT_PHYSICAL_DISK',
        # Check for updates (no update mechanism on Linux)
        'IDD_CHECKFORUPDATES',
    }

    # Dialogs that are sub-dialogs or tabs within a larger SDL3 dialog.
    # Screen Effects sub-pages are tabs inside the single "Adjust Screen Effects"
    # dialog (ui_screenfx.cpp).  IDD_ADJUST_COLORS_REFERENCE is a sub-panel of
    # Adjust Colors (ui_display.cpp).
    DIALOG_SCREENFX_SUB = {
        'IDD_ADJUST_SCREENFX_BLOOM', 'IDD_ADJUST_SCREENFX_HDR',
        'IDD_ADJUST_SCREENFX_MAIN', 'IDD_ADJUST_SCREENFX_MASK',
        'IDD_ADJUST_ARTIFACTING', 'IDD_ADJUST_COLORS_REFERENCE',
    }

    # Device config dialogs use tag-based dispatch in ui_devconfig.cpp, not RC IDs.
    # Map RC dialog IDs to the device tags that prove implementation.
    DEVICE_DIALOG_TAGS = {
        'IDD_DEVICE_1030MODEM':    ['1030', '1030full'],
        'IDD_DEVICE_1400XL':       ['1400xl'],
        'IDD_DEVICE_815':          ['diskdrive815'],
        'IDD_DEVICE_850':          ['850', '850full'],
        'IDD_DEVICE_AMDC':         ['diskdriveamdc'],
        'IDD_DEVICE_ATR8000':      ['diskdriveatr8000'],
        'IDD_DEVICE_BLACKBOX':     ['blackbox', 'blackboxfloppy'],
        'IDD_DEVICE_COMPUTEREYES': ['computereyes'],
        'IDD_DEVICE_CORVUS':       ['corvus'],
        'IDD_DEVICE_COVOX':        ['covox'],
        'IDD_DEVICE_CUSTOM':       ['custom'],
        'IDD_DEVICE_DISKDRIVEFULL':['diskdrive'],
        'IDD_DEVICE_DONGLE':       ['dongle'],
        'IDD_DEVICE_GENERIC':      ['rendergenericconfig'],
        'IDD_DEVICE_HAPPY810':     ['diskdrivehappy810'],
        'IDD_DEVICE_HARDDISK':     ['harddisk'],
        'IDD_DEVICE_HDVIRTFAT32':  ['hdvirtfat16', 'hdvirtfat32', 'hdvirtsdfs'],
        'IDD_DEVICE_KMKJZIDE':     ['kmkjzide'],
        'IDD_DEVICE_KMKJZIDEV2':   ['kmkjzide2'],
        'IDD_DEVICE_MODEM':        ['rendermodemconfig'],
        'IDD_DEVICE_MYIDE2':       ['myide2'],
        'IDD_DEVICE_NETSERIAL':    ['netserial'],
        'IDD_DEVICE_PARFILEWRITER':['parfilewriter'],
        'IDD_DEVICE_PERCOMAT':     ['diskdrivepercomat'],
        'IDD_DEVICE_PERCOMRFD':    ['diskdrivepercom'],
        'IDD_DEVICE_POCKETMODEM':  ['pocketmodem'],
        'IDD_DEVICE_SIDE3':        ['side3'],
        'IDD_DEVICE_SOUNDBOARD':   ['soundboard'],
        'IDD_DEVICE_SX212':        ['sx212'],
        'IDD_DEVICE_VBXE':         ['vbxe'],
        'IDD_DEVICE_VERONICA':     ['veronica'],
        'IDD_DEVICE_XEP80':        ['xep80'],
    }

    for dialog_id, info in rc_dialogs.items():
        if dialog_id in DIALOG_NOT_APPLICABLE:
            dialog_covered.add(dialog_id)  # Count as "handled"
            continue
        if dialog_id in DIALOG_SCREENFX_SUB:
            dialog_covered.add(dialog_id)  # Tabs inside a consolidated dialog
            continue
        # Device config dialogs — check by device tag in SDL3 source
        if dialog_id in DEVICE_DIALOG_TAGS:
            tags = DEVICE_DIALOG_TAGS[dialog_id]
            if any(t in all_sdl_text for t in tags):
                dialog_covered.add(dialog_id)
                continue
        keywords = dialog_to_sdl_keywords.get(dialog_id, [])
        found = False
        for kw in keywords:
            if kw.lower() in all_sdl_text:
                found = True
                break
        if found:
            dialog_covered.add(dialog_id)
        else:
            dialog_missing.add(dialog_id)

    # 3b. Per-control coverage for covered dialogs
    dialog_control_gaps = {}  # dialog_id -> { 'controls_found': [...], 'controls_missing': [...] }
    for dialog_id in sorted(dialog_covered):
        if dialog_id in DIALOG_NOT_APPLICABLE:
            continue
        dlg = rc_dialogs[dialog_id]
        controls = dlg.get('controls', [])
        if not controls:
            continue
        found_ctrls = []
        missing_ctrls = []
        for ctrl in controls:
            label = ctrl['label']
            if not label or len(label) < 3:
                continue
            if label_found_in_sdl(label, strict=True):
                found_ctrls.append(ctrl)
            else:
                missing_ctrls.append(ctrl)
        if missing_ctrls:  # Only record dialogs with gaps
            dialog_control_gaps[dialog_id] = {
                'controls_found': found_ctrls,
                'controls_missing': missing_ctrls,
                'total': len(found_ctrls) + len(missing_ctrls),
                'caption': dlg.get('caption', ''),
            }

    # 3c. Check associated menus for covered dialogs
    dialog_menu_gaps = {}
    for dialog_id in dialog_covered:
        dlg = rc_dialogs[dialog_id]
        menu_ref = dlg.get('menu', '')
        if menu_ref and menu_ref in rc_context_menus:
            # This dialog has an associated menu bar — check its items
            menu_info = rc_context_menus[menu_ref]
            missing_menu_items = []
            for item in menu_info['items']:
                clean = item['label'].split('\\t')[0].replace('&', '').strip()
                if clean and not label_found_in_sdl(clean, strict=True):
                    missing_menu_items.append(item)
            if missing_menu_items:
                dialog_menu_gaps[dialog_id] = {
                    'menu_id': menu_ref,
                    'missing_items': missing_menu_items,
                    'caption': dlg.get('caption', ''),
                }

    # 4. Context menu / menu bar coverage — PER-ITEM granularity
    #    For each menu, check every MENUITEM label individually.
    ctx_detail = {}  # menu_id -> { 'items_found': [...], 'items_missing': [...] }

    for menu_id, menu_info in rc_context_menus.items():
        found_items = []
        missing_items = []
        for item in menu_info['items']:
            label = item['label']
            # Clean label: remove \t shortcuts, trailing ..., ampersands
            clean = label.split('\\t')[0].replace('&', '').strip()
            if not clean:
                continue
            # Use strict matching for context menu items — short labels like
            # "View", "Open", "Delete" are too generic for substring matching
            if label_found_in_sdl(clean, strict=True):
                found_items.append(item)
            else:
                missing_items.append(item)
        ctx_detail[menu_id] = {
            'items_found': found_items,
            'items_missing': missing_items,
            'total': len(found_items) + len(missing_items),
        }

    # ── Output ──

    # Total effective coverage = covered + sysconfig + windows_only (all accounted for)
    total_cmd_accounted = len(cmd_covered) + len(cmd_sysconfig) + len(cmd_windows_only)

    report = {
        'summary': {},
        'menu_commands': {'covered': [], 'missing': [], 'placeholder': [], 'windows_only': []},
        'all_commands': {'covered': [], 'missing': [], 'sysconfig': [], 'windows_only': []},
        'dialogs': {'covered': [], 'missing': []},
        'context_menus': {'covered': [], 'missing': []},
        'accelerator_tables': {},
    }

    # Summary
    total_menu = len(menu_commands)
    total_cmd = len(cmd_commands)
    total_dialog = len(rc_dialogs)
    total_ctx = len(rc_context_menus)

    effective_menu = len(menu_covered) + len(menu_windows_only)
    effective_cmd = total_cmd_accounted

    # Context menu item-level stats
    total_ctx_items = sum(d['total'] for d in ctx_detail.values())
    found_ctx_items = sum(len(d['items_found']) for d in ctx_detail.values())
    missing_ctx_items = sum(len(d['items_missing']) for d in ctx_detail.values())

    report['summary'] = {
        'menu_commands': {
            'total': total_menu, 'covered': len(menu_covered),
            'windows_only': len(menu_windows_only),
            'missing': len(menu_missing), 'placeholder': len(menu_placeholder),
            'pct': f"{effective_menu/total_menu*100:.1f}%" if total_menu else "N/A"
        },
        'all_commands': {
            'total': total_cmd,
            'covered': len(cmd_covered),
            'in_sysconfig': len(cmd_sysconfig),
            'windows_only': len(cmd_windows_only),
            'missing': len(cmd_missing),
            'pct': f"{effective_cmd/total_cmd*100:.1f}%" if total_cmd else "N/A"
        },
        'dialogs': {
            'total': total_dialog, 'covered': len(dialog_covered),
            'missing': len(dialog_missing),
            'pct': f"{len(dialog_covered)/total_dialog*100:.1f}%" if total_dialog else "N/A"
        },
        'context_menus': {
            'total_menus': total_ctx,
            'total_items': total_ctx_items,
            'items_found': found_ctx_items,
            'items_missing': missing_ctx_items,
            'pct': f"{found_ctx_items/total_ctx_items*100:.1f}%" if total_ctx_items else "N/A"
        },
    }

    # Detailed lists
    for cmd_id in sorted(menu_covered):
        info = menu_commands.get(cmd_id, {})
        report['menu_commands']['covered'].append({
            'id': cmd_id, 'label': info.get('label', ''), 'menu': info.get('parent_menu', '')
        })
    for cmd_id in sorted(menu_missing):
        info = menu_commands.get(cmd_id, {})
        report['menu_commands']['missing'].append({
            'id': cmd_id, 'label': info.get('label', ''), 'menu': info.get('parent_menu', '')
        })
    for cmd_id in sorted(menu_placeholder):
        info = menu_commands.get(cmd_id, {})
        report['menu_commands']['placeholder'].append({
            'id': cmd_id, 'label': info.get('label', ''), 'menu': info.get('parent_menu', '')
        })

    for cmd_id in sorted(cmd_missing):
        info = cmd_commands.get(cmd_id, {})
        report['all_commands']['missing'].append({
            'id': cmd_id, 'file': info.get('file', ''),
        })
    for cmd_id in sorted(cmd_sysconfig):
        report['all_commands']['sysconfig'].append({'id': cmd_id})

    for did in sorted(dialog_missing):
        report['dialogs']['missing'].append(did)
    for did in sorted(dialog_covered):
        report['dialogs']['covered'].append(did)

    report['context_menus_detail'] = ctx_detail

    # Accelerator tables
    for table_name, entries in rc_accelerators.items():
        report['accelerator_tables'][table_name] = entries

    if output_json:
        print(json.dumps(report, indent=2))
        return report

    # ── Text report ──
    print("=" * 78)
    print("  AltirraSDL UI Coverage Report")
    print("=" * 78)
    print()

    # Summary table
    s = report['summary']
    mc = s['menu_commands']
    ac = s['all_commands']
    print("SUMMARY")
    print("-" * 78)
    print(f"  {'Category':<25} {'Total':>6} {'Direct':>7} {'SysCfg':>7} {'WinOnly':>8} {'MISSING':>8} {'Accounted':>10}")
    print(f"  {'-'*25} {'-'*6} {'-'*7} {'-'*7} {'-'*8} {'-'*8} {'-'*10}")
    print(f"  {'Menu Commands':<25} {mc['total']:>6} {mc['covered']:>7} {'—':>7} {mc['windows_only']:>8} {mc['missing']:>8} {mc['pct']:>10}")
    print(f"  {'Registered Commands':<25} {ac['total']:>6} {ac['covered']:>7} {ac['in_sysconfig']:>7} {ac['windows_only']:>8} {ac['missing']:>8} {ac['pct']:>10}")
    print(f"  {'Dialogs (RC)':<25} {s['dialogs']['total']:>6} {s['dialogs']['covered']:>7} {'—':>7} {'—':>8} {s['dialogs']['missing']:>8} {s['dialogs']['pct']:>10}")
    cm = s['context_menus']
    print(f"  {'Context Menu Items':<25} {cm['total_items']:>6} {cm['items_found']:>7} {'—':>7} {'—':>8} {cm['items_missing']:>8} {cm['pct']:>10}")
    print()
    print(f"  Direct  = found in SDL3 source code (string literal, label, or loop/table)")
    print(f"  SysCfg  = setting exposed in Configure System dialog (not a menu command)")
    print(f"  WinOnly = Windows-specific (D3D, file assoc, single instance, etc.)")
    print(f"  MISSING = genuinely not found — needs implementation or verification")
    print()

    # Missing menu commands
    if report['menu_commands']['missing']:
        print("MISSING MENU COMMANDS (in menu_default.txt but not found in SDL3)")
        print("-" * 78)
        by_menu = defaultdict(list)
        for item in report['menu_commands']['missing']:
            by_menu[item['menu']].append(item)
        for menu in sorted(by_menu):
            print(f"\n  [{menu}]")
            for item in by_menu[menu]:
                print(f"    {item['id']:<45} {item['label']}")
        print()

    # Missing commands from cmd*.cpp (genuine gaps only)
    if report['all_commands']['missing']:
        print("MISSING REGISTERED COMMANDS (genuinely not found in SDL3)")
        print("-" * 78)
        by_file = defaultdict(list)
        for item in report['all_commands']['missing']:
            by_file[item['file']].append(item)
        for f in sorted(by_file):
            print(f"\n  [{f}]")
            for item in by_file[f]:
                print(f"    {item['id']}")
        print()

    # Commands in sysconfig (not missing, just implemented differently)
    if verbose and report['all_commands']['sysconfig']:
        print("COMMANDS IN CONFIGURE SYSTEM (implemented via direct API, not command dispatch)")
        print("-" * 78)
        for item in report['all_commands']['sysconfig']:
            print(f"    {item['id']}")
        print()

    # Missing dialogs — categorized
    if report['dialogs']['missing']:
        print("MISSING DIALOGS (in Altirra.rc but no SDL3 equivalent detected)")
        print("-" * 78)

        # Categorize for readability
        categories = {
            'Device Config': [],
            'Screen Effects': [],
            'Debugger/Profiler': [],
            'Tape Editor': [],
            'Input': [],
            'Tools': [],
            'Other': [],
        }
        for did in report['dialogs']['missing']:
            if did.startswith('IDD_DEVICE_'):
                categories['Device Config'].append(did)
            elif 'SCREENFX' in did or 'ARTIFACTING' in did or 'COLORS_REF' in did:
                categories['Screen Effects'].append(did)
            elif any(x in did for x in ['TRACE', 'PROFIL', 'VERIFIER', 'DEBUG', 'DBG']):
                categories['Debugger/Profiler'].append(did)
            elif 'TAPE' in did:
                categories['Tape Editor'].append(did)
            elif 'INPUT' in did:
                categories['Input'].append(did)
            elif any(x in did for x in ['COMPAT', 'WIZARD', 'HDEVICE', 'PCLINK']):
                categories['Tools'].append(did)
            else:
                categories['Other'].append(did)

        for cat, dids in categories.items():
            if dids:
                print(f"\n  [{cat}] ({len(dids)} dialogs)")
                for did in sorted(dids):
                    print(f"    {did}")
        print()

    # Covered dialog completeness gaps (controls missing from implemented dialogs)
    if dialog_control_gaps or dialog_menu_gaps:
        print("INCOMPLETE DIALOGS (dialog exists in SDL3 but has missing controls/menus)")
        print("-" * 78)
        # Merge both gap sources
        all_gap_dialogs = set(dialog_control_gaps.keys()) | set(dialog_menu_gaps.keys())
        for did in sorted(all_gap_dialogs):
            caption = ''
            if did in dialog_control_gaps:
                g = dialog_control_gaps[did]
                caption = g['caption']
                total = g['total']
                found = len(g['controls_found'])
                pct = f"{found/total*100:.0f}%" if total else "?"
                print(f"\n  {did} \"{caption}\"  (controls: {found}/{total} = {pct})")
                for ctrl in g['controls_missing']:
                    print(f"    MISSING control: {ctrl['label']:<35} [{ctrl['id']}]")
            if did in dialog_menu_gaps:
                mg = dialog_menu_gaps[did]
                if not caption:
                    caption = mg['caption']
                    print(f"\n  {did} \"{caption}\"")
                print(f"    Associated menu {mg['menu_id']}:")
                for item in mg['missing_items']:
                    label = item['label'].split('\\t')[0].replace('&', '')
                    print(f"    MISSING menu:    {label:<35} [{item['win_id']}]")
        print()

    # Context menu per-item coverage
    menus_with_gaps = {mid: d for mid, d in ctx_detail.items()
                       if d['items_missing']}
    if menus_with_gaps:
        print("CONTEXT MENU / MENU BAR GAPS (per-item analysis)")
        print("-" * 78)
        for mid in sorted(menus_with_gaps):
            d = menus_with_gaps[mid]
            total = d['total']
            found = len(d['items_found'])
            pct = f"{found/total*100:.0f}%" if total else "N/A"
            print(f"\n  {mid}  ({found}/{total} items = {pct})")
            for item in d['items_missing']:
                label = item['label'].split('\\t')[0].replace('&', '')
                print(f"    MISSING: {label:<40} [{item['win_id']}]")
        print()

    # Verbose: show covered items too
    if verbose:
        print("COVERED MENU COMMANDS")
        print("-" * 78)
        for item in report['menu_commands']['covered']:
            print(f"  OK  {item['id']:<45} {item['label']}")
        print()

        print("COVERED DIALOGS")
        print("-" * 78)
        for did in report['dialogs']['covered']:
            print(f"  OK  {did}")
        print()

        print("COVERED CONTEXT MENUS")
        print("-" * 78)
        for mid in report['context_menus']['covered']:
            print(f"  OK  {mid}")
        print()

    return report


def main():
    parser = argparse.ArgumentParser(description="Verify SDL3 UI coverage against Windows Altirra")
    parser.add_argument('--verbose', '-v', action='store_true', help='Show covered items too')
    parser.add_argument('--json', action='store_true', help='Output as JSON')
    args = parser.parse_args()

    generate_report(verbose=args.verbose, output_json=args.json)


if __name__ == '__main__':
    main()
