//	Altirra SDL3 frontend - input handling
//
//	Keyboard input has two paths:
//
//	1. POKEY direct path: SDL scancode → Atari KBCODE → PushKey()
//	   This handles actual Atari keyboard typing (letters, numbers, etc.)
//
//	2. ATInputManager path: SDL scancode → ATInputCode (VK equivalent)
//	   → ATInputManager::OnButtonDown/Up()
//	   This handles input mapping — arrow keys as joystick, function keys
//	   as console buttons, etc.  The input map system routes these to
//	   emulated Atari controllers (joystick, paddle, 5200, etc.)
//
//	The Windows version (uivideodisplaywindow.cpp) sends VK codes through
//	ATInputManager for the controller mapping path.  We match that by
//	translating SDL scancodes to the same VK code values (ATInputCode).

#include <stdafx.h>
#include <SDL3/SDL.h>
#include <at/ataudio/pokey.h>
#include "inputmanager.h"
#include "inputdefs.h"
#include "gtia.h"
#include "simulator.h"
#include "uiaccessors.h"
#include "uikeyboard.h"

// -------------------------------------------------------------------------
// SDL scancode → Atari KBCODE mapping (POKEY direct path)
// Reference: Atari 800 Hardware Reference Manual, Chapter 3
// -------------------------------------------------------------------------

// Returns 0xFF if not mapped
static uint8 SDLScancodeToAtari(SDL_Scancode sc, bool shift, bool ctrl) {
	switch (sc) {
	// Row 0: digits
	case SDL_SCANCODE_1:       return 0x1F;
	case SDL_SCANCODE_2:       return 0x1E;
	case SDL_SCANCODE_3:       return 0x1A;
	case SDL_SCANCODE_4:       return 0x18;
	case SDL_SCANCODE_5:       return 0x1D;
	case SDL_SCANCODE_6:       return 0x1B;
	case SDL_SCANCODE_7:       return 0x33;
	case SDL_SCANCODE_8:       return 0x35;
	case SDL_SCANCODE_9:       return 0x30;
	case SDL_SCANCODE_0:       return 0x32;
	case SDL_SCANCODE_MINUS:   return 0x0E;
	case SDL_SCANCODE_EQUALS:  return 0x0F;

	// Row 1: QWERTYUIOP
	case SDL_SCANCODE_Q:       return 0x2F;
	case SDL_SCANCODE_W:       return 0x2E;
	case SDL_SCANCODE_E:       return 0x2A;
	case SDL_SCANCODE_R:       return 0x28;
	case SDL_SCANCODE_T:       return 0x2D;
	case SDL_SCANCODE_Y:       return 0x2B;
	case SDL_SCANCODE_U:       return 0x0B;
	case SDL_SCANCODE_I:       return 0x0D;
	case SDL_SCANCODE_O:       return 0x08;
	case SDL_SCANCODE_P:       return 0x0A;
	case SDL_SCANCODE_LEFTBRACKET:  return 0x0E;  // maps to Atari - key position
	case SDL_SCANCODE_RIGHTBRACKET: return 0x0F;  // maps to Atari = key position

	// Row 2: ASDFGHJKL
	case SDL_SCANCODE_A:       return 0x3F;
	case SDL_SCANCODE_S:       return 0x3E;
	case SDL_SCANCODE_D:       return 0x3A;
	case SDL_SCANCODE_F:       return 0x38;
	case SDL_SCANCODE_G:       return 0x3D;
	case SDL_SCANCODE_H:       return 0x39;
	case SDL_SCANCODE_J:       return 0x01;
	case SDL_SCANCODE_K:       return 0x05;
	case SDL_SCANCODE_L:       return 0x00;
	case SDL_SCANCODE_SEMICOLON:   return 0x02;
	case SDL_SCANCODE_APOSTROPHE:  return 0x06;  // maps to Atari + key position

	// Row 3: ZXCVBNM
	case SDL_SCANCODE_Z:       return 0x17;
	case SDL_SCANCODE_X:       return 0x16;
	case SDL_SCANCODE_C:       return 0x12;
	case SDL_SCANCODE_V:       return 0x10;
	case SDL_SCANCODE_B:       return 0x15;
	case SDL_SCANCODE_N:       return 0x23;
	case SDL_SCANCODE_M:       return 0x25;
	case SDL_SCANCODE_COMMA:   return 0x20;
	case SDL_SCANCODE_PERIOD:  return 0x22;
	case SDL_SCANCODE_SLASH:   return 0x26;

	// Special keys
	case SDL_SCANCODE_SPACE:   return 0x21;
	case SDL_SCANCODE_RETURN:  return 0x0C;
	case SDL_SCANCODE_BACKSPACE: return 0x34;
	case SDL_SCANCODE_TAB:     return 0x2C;
	case SDL_SCANCODE_ESCAPE:  return 0x1C;

	// F6 = Atari Help key (XL/XE) — always available
	case SDL_SCANCODE_F6:      return 0x11;
	case SDL_SCANCODE_DELETE:  return 0x34;

	case SDL_SCANCODE_CAPSLOCK:  return 0x3C;
	case SDL_SCANCODE_BACKSLASH: return 0x07;  // maps to Atari * key position
	case SDL_SCANCODE_GRAVE:    return 0x27;   // maps to Atari Inverse Video / Fuji key
	case SDL_SCANCODE_END:      return 0x27;   // alternate mapping for Fuji key (matches Windows)

	default: return 0xFF;
	}
}

// -------------------------------------------------------------------------
// SDL scancode → ATInputCode (Windows VK equivalent) for input mapping
//
// ATInputCode values are Windows VK codes.  This table maps SDL3 physical
// scancodes to the same VK values so that Altirra's input maps (which
// reference VK codes) work identically on SDL3.
// -------------------------------------------------------------------------

static uint32 SDLScancodeToInputCode(SDL_Scancode sc) {
	switch (sc) {
	// Letters
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

	// Digits
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

	// Navigation
	case SDL_SCANCODE_LEFT:  return kATInputCode_KeyLeft;
	case SDL_SCANCODE_UP:    return kATInputCode_KeyUp;
	case SDL_SCANCODE_RIGHT: return kATInputCode_KeyRight;
	case SDL_SCANCODE_DOWN:  return kATInputCode_KeyDown;
	case SDL_SCANCODE_HOME:  return kATInputCode_KeyHome;
	case SDL_SCANCODE_END:   return kATInputCode_KeyEnd;
	case SDL_SCANCODE_PAGEUP:   return kATInputCode_KeyPrior;
	case SDL_SCANCODE_PAGEDOWN: return kATInputCode_KeyNext;
	case SDL_SCANCODE_INSERT: return kATInputCode_KeyInsert;
	case SDL_SCANCODE_DELETE: return kATInputCode_KeyDelete;

	// Common keys
	case SDL_SCANCODE_RETURN:    return kATInputCode_KeyReturn;
	case SDL_SCANCODE_ESCAPE:    return kATInputCode_KeyEscape;
	case SDL_SCANCODE_BACKSPACE: return kATInputCode_KeyBack;
	case SDL_SCANCODE_TAB:       return kATInputCode_KeyTab;
	case SDL_SCANCODE_SPACE:     return kATInputCode_KeySpace;

	// Function keys
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

	// Numpad
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

	// Modifiers
	case SDL_SCANCODE_LSHIFT: return kATInputCode_KeyLShift;
	case SDL_SCANCODE_RSHIFT: return kATInputCode_KeyRShift;
	case SDL_SCANCODE_LCTRL:  return kATInputCode_KeyLControl;
	case SDL_SCANCODE_RCTRL:  return kATInputCode_KeyRControl;

	// Punctuation (OEM keys)
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

// -------------------------------------------------------------------------
// Input state
// -------------------------------------------------------------------------

// -------------------------------------------------------------------------
// Console callback — routes input-mapped console triggers (Start, Select,
// Option from gamepad) to GTIA console switches.
// Without this, gamepad buttons mapped to kATInputTrigger_Start etc.
// would have no effect.
// -------------------------------------------------------------------------

extern ATSimulator g_sim;

class ATInputConsoleCallbackSDL3 : public IATInputConsoleCallback {
public:
	void SetConsoleTrigger(uint32 id, bool state) override {
		switch (id) {
		case kATInputTrigger_Start:
			g_sim.GetGTIA().SetConsoleSwitch(0x01, state);
			break;
		case kATInputTrigger_Select:
			g_sim.GetGTIA().SetConsoleSwitch(0x02, state);
			break;
		case kATInputTrigger_Option:
			g_sim.GetGTIA().SetConsoleSwitch(0x04, state);
			break;
		case kATInputTrigger_ColdReset:
			if (state) {
				extern ATUIKeyboardOptions g_kbdOpts;
				g_sim.ColdReset();
				g_sim.Resume();
				if (!g_kbdOpts.mbAllowShiftOnColdReset)
					g_sim.GetPokey().SetShiftKeyState(false, true);
			}
			break;
		case kATInputTrigger_WarmReset:
			if (state) { g_sim.WarmReset(); g_sim.Resume(); }
			break;
		case kATInputTrigger_Turbo:
			ATUISetTurboPulse(state);
			break;
		default:
			break;
		}
	}
};

static ATInputConsoleCallbackSDL3 g_consoleCallback;

struct ATInputStateSDL3 {
	ATPokeyEmulator *mpPokey = nullptr;
	ATInputManager *mpInputManager = nullptr;
	ATGTIAEmulator *mpGTIA = nullptr;
};

static ATInputStateSDL3 g_inputState;

void ATInputSDL3_Init(ATPokeyEmulator *pokey, ATInputManager *inputMgr, ATGTIAEmulator *gtia) {
	g_inputState.mpPokey = pokey;
	g_inputState.mpInputManager = inputMgr;
	g_inputState.mpGTIA = gtia;

	// Register console callback so gamepad Start/Select/Option work
	if (inputMgr)
		inputMgr->SetConsoleCallback(&g_consoleCallback);
}

// Handle console switches (Start/Select/Option) — these are PIA/GTIA
// buttons, not keyboard keys.  They must be held (down on press, up on
// release) for proper behavior.
//
// F2 = Start, F3 = Select, F4 = Option — but only when function keys
// are NOT enabled (mbEnableFunctionKeys).  When function keys are enabled,
// F1-F4 map to Atari 1200XL function keys instead (handled via POKEY path).
static bool HandleConsoleSwitch(SDL_Scancode sc, bool down) {
	if (!g_inputState.mpGTIA)
		return false;

	// When 1200XL function keys are enabled, F2/F3/F4 are Atari keys,
	// not console switches.  Let them fall through to the POKEY path.
	extern ATUIKeyboardOptions g_kbdOpts;
	if (g_kbdOpts.mbEnableFunctionKeys)
		return false;

	switch (sc) {
	case SDL_SCANCODE_F2:
		g_inputState.mpGTIA->SetConsoleSwitch(0x01, down);
		return true;
	case SDL_SCANCODE_F3:
		g_inputState.mpGTIA->SetConsoleSwitch(0x02, down);
		return true;
	case SDL_SCANCODE_F4:
		g_inputState.mpGTIA->SetConsoleSwitch(0x04, down);
		return true;
	default:
		return false;
	}
}

void ATInputSDL3_HandleKeyDown(const SDL_KeyboardEvent& ev) {
	extern ATUIKeyboardOptions g_kbdOpts;
	const bool alt = (ev.mod & SDL_KMOD_ALT) != 0;

	// Path 1: Route through ATInputManager for input mapping FIRST.
	// Windows only sends to input manager when Alt is NOT held (line 2222)
	// to prevent Alt+key shortcuts from triggering input mappings.
	bool consumedByInputMap = false;
	if (g_inputState.mpInputManager && !alt) {
		uint32 inputCode = SDLScancodeToInputCode(ev.scancode);
		if (inputCode != kATInputCode_None) {
			// Always send to input manager (it may or may not have a mapping).
			// If the key IS mapped and overlap is disabled, consume it —
			// don't send to POKEY or console switches (matches Windows).
			bool mapped = g_inputState.mpInputManager->IsInputMapped(0, inputCode);
			g_inputState.mpInputManager->OnButtonDown(0, inputCode);
			if (mapped && !g_kbdOpts.mbAllowInputMapOverlap)
				consumedByInputMap = true;
		}
	}

	if (consumedByInputMap)
		return;

	// Console switches (F2=Start, F3=Select, F4=Option)
	// When mbEnableFunctionKeys is on, HandleConsoleSwitch returns false
	// and F2/F3/F4 fall through to the function key POKEY path below.
	if (HandleConsoleSwitch(ev.scancode, true))
		return;

	// 1200XL function keys (F1-F4) — only when mbEnableFunctionKeys is ON.
	if (g_kbdOpts.mbEnableFunctionKeys && g_inputState.mpPokey) {
		uint8 fkeyCode = 0xFF;
		switch (ev.scancode) {
		case SDL_SCANCODE_F1: fkeyCode = 0x03; break;
		case SDL_SCANCODE_F2: fkeyCode = 0x04; break;
		case SDL_SCANCODE_F3: fkeyCode = 0x13; break;
		case SDL_SCANCODE_F4: fkeyCode = 0x14; break;
		default: break;
		}
		if (fkeyCode != 0xFF) {
			bool shift = (ev.mod & SDL_KMOD_SHIFT) != 0;
			bool ctrl  = (ev.mod & SDL_KMOD_CTRL) != 0;
			if (ctrl)  fkeyCode |= 0x80;
			if (shift) fkeyCode |= 0x40;
			g_inputState.mpPokey->PushKey(fkeyCode, ev.repeat);
			return;
		}
	}

	// Path 2: Direct POKEY path for Atari keyboard typing
	if (!g_inputState.mpPokey) return;

	bool shift = (ev.mod & SDL_KMOD_SHIFT) != 0;
	bool ctrl  = (ev.mod & SDL_KMOD_CTRL) != 0;

	// Handle Break (Pause/Break key — Ctrl+Pause is Debug.Break in main_sdl3.cpp)
	if (ev.scancode == SDL_SCANCODE_PAUSE) {
		g_inputState.mpPokey->PushBreak();
		return;
	}

	// Exclude Ctrl/Shift from POKEY path when they're input-mapped.
	// This prevents e.g. Left Ctrl (joystick fire) from also activating
	// Atari Ctrl.  Matches Windows ExcludeMappedCtrlShiftState().
	if (g_inputState.mpInputManager && !g_kbdOpts.mbAllowInputMapModifierOverlap) {
		if (shift) {
			bool lshiftBound = g_inputState.mpInputManager->IsInputMapped(0, kATInputCode_KeyLShift);
			bool rshiftBound = g_inputState.mpInputManager->IsInputMapped(0, kATInputCode_KeyRShift);
			if (lshiftBound || rshiftBound) {
				// Shift is only considered held if an UN-mapped shift key is down.
				// SDL3 doesn't tell us which shift key is held in the mod flags,
				// so check individual scancodes via SDL_GetKeyboardState.
				const bool *keys = SDL_GetKeyboardState(nullptr);
				shift = (!lshiftBound && keys[SDL_SCANCODE_LSHIFT])
					 || (!rshiftBound && keys[SDL_SCANCODE_RSHIFT]);
			}
		}
		if (ctrl) {
			bool lctrlBound = g_inputState.mpInputManager->IsInputMapped(0, kATInputCode_KeyLControl);
			bool rctrlBound = g_inputState.mpInputManager->IsInputMapped(0, kATInputCode_KeyRControl);
			if (lctrlBound || rctrlBound) {
				const bool *keys = SDL_GetKeyboardState(nullptr);
				ctrl = (!lctrlBound && keys[SDL_SCANCODE_LCTRL])
					|| (!rctrlBound && keys[SDL_SCANCODE_RCTRL]);
			}
		}
	}

	// Update POKEY shift/ctrl register state (important for software that
	// reads these independently of key presses, e.g. raw keyboard mode)
	g_inputState.mpPokey->SetShiftKeyState(shift, true);
	g_inputState.mpPokey->SetControlKeyState(ctrl);

	uint8 atariCode = SDLScancodeToAtari(ev.scancode, shift, ctrl);
	if (atariCode == 0xFF) return;

	if (ctrl)  atariCode |= 0x80;
	if (shift) atariCode |= 0x40;

	g_inputState.mpPokey->PushKey(atariCode, ev.repeat);
}

void ATInputSDL3_HandleKeyUp(const SDL_KeyboardEvent& ev) {
	// Release through ATInputManager FIRST (matches Windows ProcessKeyUp
	// which releases input-mapped keys before handling console switches).
	if (g_inputState.mpInputManager) {
		uint32 inputCode = SDLScancodeToInputCode(ev.scancode);
		if (inputCode != kATInputCode_None)
			g_inputState.mpInputManager->OnButtonUp(0, inputCode);
	}

	// Console switches
	HandleConsoleSwitch(ev.scancode, false);
}

void ATInputSDL3_ReleaseAllKeys() {
	// Release all keyboard buttons in ATInputManager to prevent stuck
	// joystick directions when ImGui captures keyboard or window loses focus.
	// Matches Windows OnDeactivate: im->ReleaseButtons(0, kATInputCode_JoyClass-1)
	if (g_inputState.mpInputManager)
		g_inputState.mpInputManager->ReleaseButtons(0, kATInputCode_JoyClass - 1);

	// Release shift/ctrl/raw keys in POKEY
	// ReleaseAllRawKeys ensures raw keyboard keys don't stick on focus loss
	// (matches Windows OnForceKeysUp)
	extern ATUIKeyboardOptions g_kbdOpts;
	if (g_inputState.mpPokey) {
		g_inputState.mpPokey->SetShiftKeyState(false, !g_kbdOpts.mbFullRawKeys);
		g_inputState.mpPokey->SetControlKeyState(false);
		g_inputState.mpPokey->ReleaseAllRawKeys(!g_kbdOpts.mbFullRawKeys);
	}

	// Release console switches
	if (g_inputState.mpGTIA)
		g_inputState.mpGTIA->SetConsoleSwitch(0x07, false);
}
