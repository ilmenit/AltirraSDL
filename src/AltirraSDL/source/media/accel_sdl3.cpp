//	AltirraSDL - SDL3 accelerator table management
//	Ported from src/Altirra/source/uikeyboard.cpp (lines 930-1148).
//	Uses VK code constants matching Windows (via inputdefs.h kATInputCode_*).

#include <stdafx.h>
#include <SDL3/SDL.h>
#include <unordered_map>
#include <string>

#include <vd2/system/vdtypes.h>
#include <vd2/system/VDString.h>
#include <vd2/system/text.h>
#include <vd2/system/registry.h>
#include <vd2/Dita/accel.h>
#include <at/atui/uicommandmanager.h>

#include "uikeyboard.h"
#include "inputdefs.h"
#include "accel_sdl3.h"
#include "ui_main.h"

extern ATUICommandManager g_ATUICommandMgr;
// =========================================================================
// Globals
// =========================================================================

static VDAccelTableDefinition g_ATUIDefaultAccelTables[kATUIAccelContextCount];
static VDAccelTableDefinition g_ATUIAccelTables[kATUIAccelContextCount];

bool g_shortcutCaptureActive = false;
ATShortcutCaptureResult g_shortcutCaptureResult = {};
VDStringA g_pendingShortcutAssign;

// Shortcut string cache: command name -> UTF-8 display string
static std::unordered_map<std::string, std::string> g_shortcutStringCache;
static bool g_shortcutCacheValid = false;

// =========================================================================
// Default accelerator tables (matches Windows uikeyboard.cpp:936-990)
// VK codes from inputdefs.h (kATInputCode_* = Windows VK_*)
// =========================================================================

// Windows VK constants not in inputdefs.h
static constexpr uint32 VK_CANCEL = 0x03;  // Ctrl+Break
static constexpr uint32 VK_BACK   = 0x08;  // Backspace

namespace {
	const auto CTRL = VDUIAccelerator::kModCtrl;
	const auto SHIFT = VDUIAccelerator::kModShift;
	const auto ALT = VDUIAccelerator::kModAlt;
	const auto UP = VDUIAccelerator::kModUp;
	const auto EXT = VDUIAccelerator::kModExtended;

	const VDAccelTableEntry kATDefaultAccelTableDisplay[] = {
		{ "System.PulseWarpOn",            0, { kATInputCode_KeyF1,  0 } },
		{ "System.PulseWarpOff",           0, { kATInputCode_KeyF1,  UP } },
		{ "Input.CycleQuickMaps",          0, { kATInputCode_KeyF1,  SHIFT } },
		{ "Console.HoldKeys",              0, { kATInputCode_KeyF1,  ALT } },
		{ "System.WarmReset",              0, { kATInputCode_KeyF5,  0 } },
		{ "System.ColdReset",              0, { kATInputCode_KeyF5,  SHIFT } },
		{ "Video.ToggleStandardNTSCPAL",   0, { kATInputCode_KeyF7,  CTRL } },
		{ "View.NextANTICVisMode",         0, { kATInputCode_KeyF8,  SHIFT } },
		{ "View.NextGTIAVisMode",          0, { kATInputCode_KeyF8,  CTRL } },
		{ "System.TogglePause",            0, { kATInputCode_KeyF9,  0 } },
		// F1 is repurposed here for the emote picker, but only while
		// netplay is lockstepping — otherwise the F1/PulseWarp bindings
		// above fire and warp behaves as normal.  The command handler
		// (CmdPulseWarpOn) routes to the picker when online and to warp
		// otherwise, so a single key works in both contexts.  No
		// separate accel entry needed; this one stays off the table.
		{ "Input.CaptureMouse",            0, { kATInputCode_KeyF12, 0 } },
		{ "View.ToggleFullScreen",         0, { kATInputCode_KeyReturn, ALT } },
		{ "System.ToggleSlowMotion",       0, { VK_BACK,             ALT } },
		{ "Audio.ToggleChannel1",          0, { kATInputCode_Key1,   ALT+SHIFT } },
		{ "Audio.ToggleChannel2",          0, { kATInputCode_Key2,   ALT+SHIFT } },
		{ "Audio.ToggleChannel3",          0, { kATInputCode_Key3,   ALT+SHIFT } },
		{ "Audio.ToggleChannel4",          0, { kATInputCode_Key4,   ALT+SHIFT } },
		{ "Edit.PasteText",                0, { kATInputCode_KeyV,   ALT+SHIFT } },
		{ "Edit.SaveFrame",                0, { kATInputCode_KeyF10, ALT } },
		{ "Edit.CopyText",                 0, { kATInputCode_KeyC,   ALT+SHIFT } },
		{ "Edit.CopyFrame",                0, { kATInputCode_KeyM,   ALT+SHIFT } },
		{ "Edit.SelectAll",                0, { kATInputCode_KeyA,   ALT+SHIFT } },
		{ "Edit.Deselect",                 0, { kATInputCode_KeyD,   ALT+SHIFT } },
		{ "Edit.ShowSuggestions",          0, { kATInputCode_KeyOemComma, ALT } },
	};

	const VDAccelTableEntry kATDefaultAccelTableGlobal[] = {
		{ "Cheat.CheatDialog",             0, { kATInputCode_KeyH,   ALT+SHIFT } },
		{ "File.BootImage",                0, { kATInputCode_KeyB,   ALT } },
		{ "File.OpenImage",                0, { kATInputCode_KeyO,   ALT } },
		{ "Debug.OpenSourceFile",          0, { kATInputCode_KeyO,   ALT+SHIFT } },
		{ "Disk.DrivesDialog",             0, { kATInputCode_KeyD,   ALT } },
		{ "Pane.Display",                  0, { kATInputCode_Key1,   ALT } },
		{ "Pane.Console",                  0, { kATInputCode_Key2,   ALT } },
		{ "Pane.Registers",                0, { kATInputCode_Key3,   ALT } },
		{ "Pane.Disassembly",              0, { kATInputCode_Key4,   ALT } },
		{ "Pane.CallStack",                0, { kATInputCode_Key5,   ALT } },
		{ "Pane.History",                  0, { kATInputCode_Key6,   ALT } },
		{ "Pane.Memory1",                  0, { kATInputCode_Key7,   ALT } },
		{ "Pane.PrinterOutput",            0, { kATInputCode_Key8,   ALT } },
		{ "Pane.ProfileView",              0, { kATInputCode_Key0,   ALT+SHIFT } },
		{ "System.Configure",              0, { kATInputCode_KeyS,   ALT } },

		{ "Debug.RunStop",                 0, { kATInputCode_KeyF8,  0 } },
		{ "Debug.StepInto",                0, { kATInputCode_KeyF11, 0 } },
		{ "Debug.StepOver",                0, { kATInputCode_KeyF10, 0 } },
		{ "Debug.StepOut",                 0, { kATInputCode_KeyF11, SHIFT } },
		{ "Debug.Break",                   0, { VK_CANCEL,           CTRL + EXT } },
	};

	const VDAccelTableEntry kATDefaultAccelTableDebugger[] = {
		{ "Debug.Run",                     0, { kATInputCode_KeyF5,  0 } },
		{ "Debug.ToggleBreakpoint",        0, { kATInputCode_KeyF9,  0 } },
		{ "Debug.NewBreakpoint",           0, { kATInputCode_KeyB,   CTRL } },
	};
}

// =========================================================================
// Accelerator table management
// =========================================================================

void ATUIInitDefaultAccelTables() {
	g_ATUIDefaultAccelTables[kATUIAccelContext_Global].AddRange(
		kATDefaultAccelTableGlobal, vdcountof(kATDefaultAccelTableGlobal));
	g_ATUIDefaultAccelTables[kATUIAccelContext_Display].AddRange(
		kATDefaultAccelTableDisplay, vdcountof(kATDefaultAccelTableDisplay));
	g_ATUIDefaultAccelTables[kATUIAccelContext_Debugger].AddRange(
		kATDefaultAccelTableDebugger, vdcountof(kATDefaultAccelTableDebugger));

	for (int i = 0; i < kATUIAccelContextCount; ++i)
		g_ATUIAccelTables[i] = g_ATUIDefaultAccelTables[i];
}

void ATUILoadAccelTables() {
	vdfastvector<VDAccelToCommandEntry> commands;
	g_ATUICommandMgr.ListCommands(commands);

	VDStringA keyName;
	for (int i = 0; i < kATUIAccelContextCount; ++i) {
		keyName.sprintf("AccelTables3\\%d", i);
		VDRegistryAppKey key(keyName.c_str(), false, false);
		if (key.isReady()) {
			try {
				g_ATUIAccelTables[i].Load(key, g_ATUIDefaultAccelTables[i],
					commands.data(), (uint32)commands.size());
			} catch (const MyError&) {
				// eat load error
			}
		}
	}

	ATUIInvalidateShortcutCache();
}

void ATUISaveAccelTables() {
	VDStringA keyName;
	for (int i = 0; i < kATUIAccelContextCount; ++i) {
		keyName.sprintf("AccelTables3\\%d", i);
		VDRegistryAppKey key(keyName.c_str());
		g_ATUIAccelTables[i].Save(key, g_ATUIDefaultAccelTables[i]);
	}
}

const VDAccelTableDefinition *ATUIGetDefaultAccelTables() {
	return g_ATUIDefaultAccelTables;
}

VDAccelTableDefinition *ATUIGetAccelTables() {
	return g_ATUIAccelTables;
}

const VDAccelTableEntry *ATUIGetAccelByCommand(ATUIAccelContext context, const char *command) {
	for (;;) {
		const VDAccelTableDefinition& table = g_ATUIAccelTables[context];
		uint32 numAccels = table.GetSize();
		for (uint32 i = 0; i < numAccels; ++i) {
			const VDAccelTableEntry& entry = table[i];
			if (!strcmp(entry.mpCommand, command))
				return &entry;
		}

		// Fall back: Display -> Global, but not further
		if (context == kATUIAccelContext_Display) {
			context = kATUIAccelContext_Global;
			continue;
		}
		break;
	}
	return nullptr;
}

const VDAccelTableEntry *ATUIFindConflictingVirtKeyMapping(
	uint32 vk, bool alt, bool ctrl, bool shift, bool ext,
	ATUIAccelContext context)
{
	uint32 mods = 0;
	if (alt)   mods |= VDUIAccelerator::kModAlt;
	if (ctrl)  mods |= VDUIAccelerator::kModCtrl;
	if (shift) mods |= VDUIAccelerator::kModShift;
	if (ext)   mods |= VDUIAccelerator::kModExtended;

	VDUIAccelerator accel;
	accel.mVirtKey = vk;
	accel.mModifiers = mods;

	for (;;) {
		const VDAccelTableEntry *entry = g_ATUIAccelTables[context](accel);
		if (entry)
			return entry;

		// Also check key-up variant
		VDUIAccelerator accelUp = accel;
		accelUp.mModifiers = mods | VDUIAccelerator::kModUp;
		entry = g_ATUIAccelTables[context](accelUp);
		if (entry)
			return entry;

		// Fall back from Display to Global (matching ATUIActivateVirtKeyMapping)
		if (context == kATUIAccelContext_Display) {
			context = kATUIAccelContext_Global;
			continue;
		}
		break;
	}
	return nullptr;
}

bool ATUIActivateVirtKeyMapping(uint32 vk, bool alt, bool ctrl, bool shift,
	bool ext, bool up, ATUIAccelContext context)
{
	uint32 mods = 0;
	if (alt)   mods |= VDUIAccelerator::kModAlt;
	if (ctrl)  mods |= VDUIAccelerator::kModCtrl;
	if (shift) mods |= VDUIAccelerator::kModShift;
	if (ext)   mods |= VDUIAccelerator::kModExtended;
	if (up)    mods |= VDUIAccelerator::kModUp;

	VDUIAccelerator accel;
	accel.mVirtKey = vk;
	accel.mModifiers = mods;

	for (;;) {
		const VDAccelTableEntry *entry = g_ATUIAccelTables[context](accel);
		if (entry) {
			// ExecuteCommandNT returns false if mpTestFn fails — in that case
			// the key should NOT be consumed so it can fall through to the
			// emulator (e.g. Pane shortcuts when debugger is closed).
			if (g_ATUICommandMgr.ExecuteCommandNT(entry->mpCommand))
				return true;
		}

		// Fall back from Display to Global context
		if (context == kATUIAccelContext_Display) {
			context = kATUIAccelContext_Global;
			continue;
		}
		break;
	}
	return false;
}

// =========================================================================
// SDL scancode → VK code translation
// =========================================================================

uint32 SDLScancodeToVK(SDL_Scancode sc) {
	switch (sc) {
	case SDL_SCANCODE_A: return kATInputCode_KeyA;
	case SDL_SCANCODE_B: return kATInputCode_KeyB;
	case SDL_SCANCODE_C: return kATInputCode_KeyC;
	case SDL_SCANCODE_D: return kATInputCode_KeyD;
	case SDL_SCANCODE_E: return kATInputCode_KeyE;
	case SDL_SCANCODE_F: return kATInputCode_KeyF;
	case SDL_SCANCODE_G: return kATInputCode_KeyG;
	case SDL_SCANCODE_H: return kATInputCode_KeyH;
	case SDL_SCANCODE_I: return kATInputCode_KeyI;
	case SDL_SCANCODE_J: return kATInputCode_KeyJ;
	case SDL_SCANCODE_K: return kATInputCode_KeyK;
	case SDL_SCANCODE_L: return kATInputCode_KeyL;
	case SDL_SCANCODE_M: return kATInputCode_KeyM;
	case SDL_SCANCODE_N: return kATInputCode_KeyN;
	case SDL_SCANCODE_O: return kATInputCode_KeyO;
	case SDL_SCANCODE_P: return kATInputCode_KeyP;
	case SDL_SCANCODE_Q: return kATInputCode_Keyq;
	case SDL_SCANCODE_R: return kATInputCode_KeyR;
	case SDL_SCANCODE_S: return kATInputCode_KeyS;
	case SDL_SCANCODE_T: return kATInputCode_KeyT;
	case SDL_SCANCODE_U: return kATInputCode_KeyU;
	case SDL_SCANCODE_V: return kATInputCode_KeyV;
	case SDL_SCANCODE_W: return kATInputCode_KeyW;
	case SDL_SCANCODE_X: return kATInputCode_KeyX;
	case SDL_SCANCODE_Y: return kATInputCode_KeyY;
	case SDL_SCANCODE_Z: return kATInputCode_KeyZ;

	case SDL_SCANCODE_0: return kATInputCode_Key0;
	case SDL_SCANCODE_1: return kATInputCode_Key1;
	case SDL_SCANCODE_2: return kATInputCode_Key2;
	case SDL_SCANCODE_3: return kATInputCode_Key3;
	case SDL_SCANCODE_4: return kATInputCode_Key4;
	case SDL_SCANCODE_5: return kATInputCode_Key5;
	case SDL_SCANCODE_6: return kATInputCode_Key6;
	case SDL_SCANCODE_7: return kATInputCode_Key7;
	case SDL_SCANCODE_8: return kATInputCode_Key8;
	case SDL_SCANCODE_9: return kATInputCode_Key9;

	case SDL_SCANCODE_LEFT:     return kATInputCode_KeyLeft;
	case SDL_SCANCODE_UP:       return kATInputCode_KeyUp;
	case SDL_SCANCODE_RIGHT:    return kATInputCode_KeyRight;
	case SDL_SCANCODE_DOWN:     return kATInputCode_KeyDown;
	case SDL_SCANCODE_HOME:     return kATInputCode_KeyHome;
	case SDL_SCANCODE_END:      return kATInputCode_KeyEnd;
	case SDL_SCANCODE_PAGEUP:   return kATInputCode_KeyPrior;
	case SDL_SCANCODE_PAGEDOWN: return kATInputCode_KeyNext;
	case SDL_SCANCODE_INSERT:   return kATInputCode_KeyInsert;
	case SDL_SCANCODE_DELETE:   return kATInputCode_KeyDelete;

	case SDL_SCANCODE_RETURN:    return kATInputCode_KeyReturn;
	case SDL_SCANCODE_ESCAPE:    return kATInputCode_KeyEscape;
	case SDL_SCANCODE_BACKSPACE: return kATInputCode_KeyBack;
	case SDL_SCANCODE_TAB:       return kATInputCode_KeyTab;
	case SDL_SCANCODE_SPACE:     return kATInputCode_KeySpace;

	case SDL_SCANCODE_F1:  return kATInputCode_KeyF1;
	case SDL_SCANCODE_F2:  return kATInputCode_KeyF2;
	case SDL_SCANCODE_F3:  return kATInputCode_KeyF3;
	case SDL_SCANCODE_F4:  return kATInputCode_KeyF4;
	case SDL_SCANCODE_F5:  return kATInputCode_KeyF5;
	case SDL_SCANCODE_F6:  return kATInputCode_KeyF6;
	case SDL_SCANCODE_F7:  return kATInputCode_KeyF7;
	case SDL_SCANCODE_F8:  return kATInputCode_KeyF8;
	case SDL_SCANCODE_F9:  return kATInputCode_KeyF9;
	case SDL_SCANCODE_F10: return kATInputCode_KeyF10;
	case SDL_SCANCODE_F11: return kATInputCode_KeyF11;
	case SDL_SCANCODE_F12: return kATInputCode_KeyF12;

	case SDL_SCANCODE_PAUSE:    return 0x13;  // VK_PAUSE
	case SDL_SCANCODE_CAPSLOCK: return 0x14;  // VK_CAPITAL

	case SDL_SCANCODE_KP_0: return kATInputCode_KeyNumpad0;
	case SDL_SCANCODE_KP_1: return kATInputCode_KeyNumpad1;
	case SDL_SCANCODE_KP_2: return kATInputCode_KeyNumpad2;
	case SDL_SCANCODE_KP_3: return kATInputCode_KeyNumpad3;
	case SDL_SCANCODE_KP_4: return kATInputCode_KeyNumpad4;
	case SDL_SCANCODE_KP_5: return kATInputCode_KeyNumpad5;
	case SDL_SCANCODE_KP_6: return kATInputCode_KeyNumpad6;
	case SDL_SCANCODE_KP_7: return kATInputCode_KeyNumpad7;
	case SDL_SCANCODE_KP_8: return kATInputCode_KeyNumpad8;
	case SDL_SCANCODE_KP_9: return kATInputCode_KeyNumpad9;
	case SDL_SCANCODE_KP_MULTIPLY: return kATInputCode_KeyMultiply;
	case SDL_SCANCODE_KP_PLUS:     return kATInputCode_KeyAdd;
	case SDL_SCANCODE_KP_MINUS:    return kATInputCode_KeySubtract;
	case SDL_SCANCODE_KP_PERIOD:   return kATInputCode_KeyDecimal;
	case SDL_SCANCODE_KP_DIVIDE:   return kATInputCode_KeyDivide;
	case SDL_SCANCODE_KP_ENTER:    return kATInputCode_KeyNumpadEnter;

	case SDL_SCANCODE_LSHIFT: return kATInputCode_KeyLShift;
	case SDL_SCANCODE_RSHIFT: return kATInputCode_KeyRShift;
	case SDL_SCANCODE_LCTRL:  return kATInputCode_KeyLControl;
	case SDL_SCANCODE_RCTRL:  return kATInputCode_KeyRControl;

	case SDL_SCANCODE_SEMICOLON:    return kATInputCode_KeyOem1;
	case SDL_SCANCODE_EQUALS:       return kATInputCode_KeyOemPlus;
	case SDL_SCANCODE_COMMA:        return kATInputCode_KeyOemComma;
	case SDL_SCANCODE_MINUS:        return kATInputCode_KeyOemMinus;
	case SDL_SCANCODE_PERIOD:       return kATInputCode_KeyOemPeriod;
	case SDL_SCANCODE_SLASH:        return kATInputCode_KeyOem2;
	case SDL_SCANCODE_GRAVE:        return kATInputCode_KeyOem3;
	case SDL_SCANCODE_LEFTBRACKET:  return kATInputCode_KeyOem4;
	case SDL_SCANCODE_BACKSLASH:    return kATInputCode_KeyOem5;
	case SDL_SCANCODE_RIGHTBRACKET: return kATInputCode_KeyOem6;
	case SDL_SCANCODE_APOSTROPHE:   return kATInputCode_KeyOem7;

	default: return kATInputCode_None;
	}
}

// Check if an SDL scancode corresponds to an extended key (right-hand side
// keys, arrow keys, etc. — matching Windows WM_KEYDOWN extended flag).
static bool IsExtendedSDLKey(SDL_Scancode sc) {
	switch (sc) {
	case SDL_SCANCODE_RCTRL:
	case SDL_SCANCODE_RALT:
	case SDL_SCANCODE_INSERT:
	case SDL_SCANCODE_DELETE:
	case SDL_SCANCODE_HOME:
	case SDL_SCANCODE_END:
	case SDL_SCANCODE_PAGEUP:
	case SDL_SCANCODE_PAGEDOWN:
	case SDL_SCANCODE_LEFT:
	case SDL_SCANCODE_RIGHT:
	case SDL_SCANCODE_UP:
	case SDL_SCANCODE_DOWN:
	case SDL_SCANCODE_KP_ENTER:
	case SDL_SCANCODE_KP_DIVIDE:
	case SDL_SCANCODE_PAUSE:
		return true;
	default:
		return false;
	}
}

// =========================================================================
// SDL event → accel table dispatch
// =========================================================================

bool ATUISDLActivateAccelKey(const SDL_KeyboardEvent& ev, bool up, ATUIAccelContext context) {
	uint32 vk = SDLScancodeToVK(ev.scancode);
	if (vk == kATInputCode_None)
		return false;

	bool alt   = (ev.mod & SDL_KMOD_ALT) != 0;
	bool ctrl  = (ev.mod & SDL_KMOD_CTRL) != 0;
	bool shift = (ev.mod & SDL_KMOD_SHIFT) != 0;
	bool ext   = IsExtendedSDLKey(ev.scancode);

	return ATUIActivateVirtKeyMapping(vk, alt, ctrl, shift, ext, up, context);
}

bool ATUIFindBoundKey(uint32 vk, SDL_Keymod sdlMod, SDL_Scancode scancode) {
	if (vk == kATInputCode_None)
		return false;

	uint32 mods = 0;
	if (sdlMod & SDL_KMOD_ALT)   mods |= VDUIAccelerator::kModAlt;
	if (sdlMod & SDL_KMOD_CTRL)  mods |= VDUIAccelerator::kModCtrl;
	if (sdlMod & SDL_KMOD_SHIFT) mods |= VDUIAccelerator::kModShift;
	if (IsExtendedSDLKey(scancode)) mods |= VDUIAccelerator::kModExtended;

	// Check all contexts for any binding with this VK+modifiers
	for (int ctx = 0; ctx < kATUIAccelContextCount; ++ctx) {
		const VDAccelTableDefinition& table = g_ATUIAccelTables[ctx];
		for (uint32 i = 0; i < table.GetSize(); ++i) {
			const VDAccelTableEntry& entry = table[i];
			if (entry.mAccel.mVirtKey == vk) {
				// Match found — check if modifiers also match (ignoring Up flag)
				uint32 entryMods = entry.mAccel.mModifiers & ~VDUIAccelerator::kModUp;
				if (entryMods == mods)
					return true;
			}
		}
	}
	return false;
}

// =========================================================================
// Shortcut string cache for menu labels
// =========================================================================

static void BuildShortcutCache() {
	g_shortcutStringCache.clear();

	for (int ctx = 0; ctx < kATUIAccelContextCount; ++ctx) {
		const VDAccelTableDefinition& table = g_ATUIAccelTables[ctx];
		for (uint32 i = 0; i < table.GetSize(); ++i) {
			const VDAccelTableEntry& entry = table[i];

			// Skip key-up entries (e.g. PulseWarpOff)
			if (entry.mAccel.mModifiers & VDUIAccelerator::kModUp)
				continue;

			std::string cmdName(entry.mpCommand);

			// Only store first binding found (highest priority context)
			if (g_shortcutStringCache.count(cmdName))
				continue;

			VDStringW ws;
			VDUIGetAcceleratorString(entry.mAccel, ws);
			if (!ws.empty()) {
				VDStringA utf8 = VDTextWToU8(ws);
				g_shortcutStringCache[cmdName] = std::string(utf8.c_str());
			}
		}
	}
	g_shortcutCacheValid = true;
}

const char *ATUIGetShortcutStringForCommand(const char *command) {
	if (!g_shortcutCacheValid)
		BuildShortcutCache();

	auto it = g_shortcutStringCache.find(command);
	if (it != g_shortcutStringCache.end())
		return it->second.c_str();
	return "";
}

void ATUIInvalidateShortcutCache() {
	g_shortcutCacheValid = false;
}

// =========================================================================
// Shortcut capture mode
// =========================================================================

void ATUIHandleShortcutCapture(const SDL_KeyboardEvent& ev) {
	// Ignore modifier-only key presses — wait for a real key
	switch (ev.scancode) {
	case SDL_SCANCODE_LSHIFT:
	case SDL_SCANCODE_RSHIFT:
	case SDL_SCANCODE_LCTRL:
	case SDL_SCANCODE_RCTRL:
	case SDL_SCANCODE_LALT:
	case SDL_SCANCODE_RALT:
		return;
	default:
		break;
	}

	// ESC cancels capture
	if (ev.scancode == SDL_SCANCODE_ESCAPE) {
		g_shortcutCaptureActive = false;
		g_shortcutCaptureResult.captured = false;
		return;
	}

	uint32 vk = SDLScancodeToVK(ev.scancode);
	if (vk == kATInputCode_None)
		return;  // Unknown key — ignore

	uint32 mods = 0;
	if (ev.mod & SDL_KMOD_CTRL)  mods |= VDUIAccelerator::kModCtrl;
	if (ev.mod & SDL_KMOD_ALT)   mods |= VDUIAccelerator::kModAlt;
	if (ev.mod & SDL_KMOD_SHIFT) mods |= VDUIAccelerator::kModShift;
	if (IsExtendedSDLKey(ev.scancode)) mods |= VDUIAccelerator::kModExtended;

	g_shortcutCaptureActive = false;
	g_shortcutCaptureResult.captured = true;
	g_shortcutCaptureResult.vk = vk;
	g_shortcutCaptureResult.modifiers = mods;
}

extern ATUIState g_uiState;

void ATUIOpenShortcutEditor(const char *command) {
	if (command)
		g_pendingShortcutAssign = command;
	else
		g_pendingShortcutAssign.clear();

	g_uiState.showKeyboardShortcuts = true;
}

void ATUISetShowDiskManager(bool v) { g_uiState.showDiskManager = v; }
void ATUISetShowSystemConfig(bool v) { g_uiState.showSystemConfig = v; }
void ATUISetShowCheater(bool v) { g_uiState.showCheater = v; }
