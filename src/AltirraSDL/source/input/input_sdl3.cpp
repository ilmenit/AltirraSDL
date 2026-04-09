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
#include <algorithm>
#include <unordered_map>
#include <SDL3/SDL.h>
#include <at/ataudio/pokey.h>
#include "inputmanager.h"
#include "inputmap.h"
#include "inputdefs.h"
#include "gtia.h"
#include "simulator.h"
#include "uiaccessors.h"
#include "uikeyboard.h"

// Declared in uiaccessors_stubs.cpp — provides read-only access to the sorted
// custom key map without copying.
extern const vdfastvector<uint32>& ATUIGetCustomKeyMapRef();

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

	// Special
	case SDL_SCANCODE_PAUSE:    return 0x13;  // VK_PAUSE
	case SDL_SCANCODE_CAPSLOCK: return 0x14;  // VK_CAPITAL

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
	if (inputMgr) {
		inputMgr->SetConsoleCallback(&g_consoleCallback);

		// Parity with Windows uivideodisplaywindow.cpp:1693 — the Win32 UI
		// clears restricted mode when the display window is created, and
		// only ever re-enables it for modal UI-navigation capture.  SDL3
		// has no modal UI-nav mode, so the flag must simply start false.
		// A header default initializer (inputmanager.h) already guarantees
		// this, but we make the intent explicit here so parity with the
		// Windows init sequence is visible and future code that adds a
		// modal capture mode has an obvious place to pair with.
		inputMgr->SetRestrictedMode(false);
	}

	// Match Windows uivideodisplaywindow.cpp:1601-1613 (OnForceKeysUp) —
	// explicitly clear shift/ctrl/raw-key/console state at startup so we
	// don't depend on POKEY's constructor defaults.
	extern ATUIKeyboardOptions g_kbdOpts;
	if (pokey) {
		pokey->SetShiftKeyState(false, !g_kbdOpts.mbFullRawKeys);
		pokey->SetControlKeyState(false);
		pokey->ReleaseAllRawKeys(!g_kbdOpts.mbFullRawKeys);
	}
	if (gtia)
		gtia->SetConsoleSwitch(0x07, false);
}

// -------------------------------------------------------------------------
// Custom key map lookup for kLM_Custom layout mode.
// Uses the same packed uint32 format and binary search as Windows.
// -------------------------------------------------------------------------

static bool IsExtendedSDLKey(SDL_Scancode sc) {
	switch (sc) {
	case SDL_SCANCODE_LEFT: case SDL_SCANCODE_RIGHT:
	case SDL_SCANCODE_UP: case SDL_SCANCODE_DOWN:
	case SDL_SCANCODE_INSERT: case SDL_SCANCODE_DELETE:
	case SDL_SCANCODE_HOME: case SDL_SCANCODE_END:
	case SDL_SCANCODE_PAGEUP: case SDL_SCANCODE_PAGEDOWN:
	case SDL_SCANCODE_KP_ENTER:
		return true;
	default:
		return false;
	}
}

static bool LookupCustomKeyMap(uint32 vk, bool shift, bool ctrl, bool alt,
	bool extended, uint32& scanCode)
{
	uint32 keyInputCode = vk << 9;
	if (shift)    keyInputCode += kATUIKeyboardMappingModifier_Shift;
	if (ctrl)     keyInputCode += kATUIKeyboardMappingModifier_Ctrl;
	if (alt)      keyInputCode += kATUIKeyboardMappingModifier_Alt;
	if (extended) keyInputCode += kATUIKeyboardMappingModifier_Extended;

	const auto& keyMap = ATUIGetCustomKeyMapRef();
	auto it = std::lower_bound(keyMap.begin(), keyMap.end(), keyInputCode);
	if (it == keyMap.end() || (*it & 0xFFFFFE00) != keyInputCode)
		return false;

	scanCode = *it & 0x1FF;
	return true;
}

// Track which SDL scancodes have activated console switches in custom mode,
// so we can release them on key-up regardless of modifier state.
// Thread safety: SDL3 event callbacks are always on the main thread, same as
// Windows WM_KEYDOWN/UP — no synchronization needed.
static std::unordered_map<SDL_Scancode, uint32> g_customConsoleSwitches;

// Custom-mapped Break key tracker — release path mirrors Windows
// uivideodisplaywindow.cpp:2382 (SetBreakKeyState false on key-up).
static std::unordered_map<SDL_Scancode, bool> g_customBreakKeys;

// Raw keyboard mode tracker (mbRawKeys) — mirrors Windows
// ATUIVideoDisplayWindow::mActiveKeys (uivideodisplaywindow.cpp:2344-2348).
// Maps SDL scancode of a held key to the Atari scan code that was pushed
// via PushRawKey, so the corresponding ReleaseRawKey can be called on
// key-up.  Without this, raw-mode keys would stick on after release.
static std::unordered_map<SDL_Scancode, uint8> g_rawActiveKeys;

// Helper: push an Atari key.  In raw keyboard mode, calls PushRawKey and
// records the scancode for later release; in cooked mode, calls PushKey
// with the OS auto-repeat flag.  Mirrors the raw/cooked branch in
// Windows ProcessVirtKey (uivideodisplaywindow.cpp:2336-2354).
static void ATInputSDL3_PushAtariKey(SDL_Scancode sdlSc, uint8 atariCode, bool repeat) {
	extern ATUIKeyboardOptions g_kbdOpts;
	if (!g_inputState.mpPokey)
		return;
	if (g_kbdOpts.mbRawKeys) {
		// Match Windows uivideodisplaywindow.cpp:2341 — raw mode does
		// nothing on auto-repeat; the key is already physically held.
		if (repeat)
			return;
		auto it = g_rawActiveKeys.find(sdlSc);
		if (it != g_rawActiveKeys.end())
			it->second = atariCode;
		else
			g_rawActiveKeys[sdlSc] = atariCode;
		g_inputState.mpPokey->PushRawKey(atariCode, !g_kbdOpts.mbFullRawKeys);
	} else {
		g_inputState.mpPokey->PushKey(atariCode, repeat);
	}
}

static void HandleCustomConsoleSwitch(uint32 scanCode, bool down, SDL_Scancode sdlSc) {
	if (!g_inputState.mpGTIA)
		return;

	uint32 switchBit = 0;
	switch (scanCode) {
	case kATUIKeyScanCode_Start:  switchBit = 0x01; break;
	case kATUIKeyScanCode_Select: switchBit = 0x02; break;
	case kATUIKeyScanCode_Option: switchBit = 0x04; break;
	case kATUIKeyScanCode_Break:
		// Match Windows uivideodisplaywindow.cpp:2382 — Break is a true
		// hold via SetBreakKeyState(state, ...), not a one-shot pulse.
		if (g_inputState.mpPokey) {
			extern ATUIKeyboardOptions g_kbdOpts;
			g_inputState.mpPokey->SetBreakKeyState(down, !g_kbdOpts.mbFullRawKeys);
		}
		if (down)
			g_customBreakKeys[sdlSc] = true;
		else
			g_customBreakKeys.erase(sdlSc);
		return;
	default:
		return;
	}

	g_inputState.mpGTIA->SetConsoleSwitch(switchBit, down);

	if (down)
		g_customConsoleSwitches[sdlSc] = switchBit;
	else
		g_customConsoleSwitches.erase(sdlSc);
}

// -------------------------------------------------------------------------

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

// Joystick-related scancodes we want accurate event traces for.  We log the
// first N events per scancode so the stream is bounded but reveals chatter.
static bool ATInputSDL3_IsTracedScancode(SDL_Scancode sc) {
	switch (sc) {
	case SDL_SCANCODE_LEFT:
	case SDL_SCANCODE_RIGHT:
	case SDL_SCANCODE_UP:
	case SDL_SCANCODE_DOWN:
	case SDL_SCANCODE_LCTRL:
	case SDL_SCANCODE_RCTRL:
	case SDL_SCANCODE_LSHIFT:
	case SDL_SCANCODE_RSHIFT:
		return true;
	default:
		return false;
	}
}

// Bounded event trace: log up to kTraceBudget events per traced scancode,
// each as a single line with direction, repeat flag, delta since the previous
// event on the same scancode, and — for UP events — whether a paired KEY_DOWN
// for the same scancode is already queued immediately behind it.  A "paired"
// DOWN within 1 ms of the UP is the direct signature of X11-style autorepeat
// chatter; seeing "paired=no" on every UP rules that theory out entirely.
// Once the per-scancode budget is exhausted, nothing more is logged.
static void ATInputSDL3_TraceKeyEvent(const SDL_KeyboardEvent& ev, bool down) {
	if (!ATInputSDL3_IsTracedScancode(ev.scancode))
		return;

	static constexpr int kTraceBudget = 12;
	static int s_logged[SDL_SCANCODE_COUNT] = {};
	static Uint64 s_lastTs[SDL_SCANCODE_COUNT] = {};

	int &logged = s_logged[ev.scancode];
	if (logged >= kTraceBudget)
		return;
	++logged;

	const Uint64 last = s_lastTs[ev.scancode];
	const Uint64 deltaUs = last ? ((ev.timestamp - last) / 1000) : 0;
	s_lastTs[ev.scancode] = ev.timestamp;

	// For UP events, peek the queue for an immediately-following KEY_DOWN on
	// the same scancode.  SDL_PeepEvents with SDL_PEEKEVENT is non-destructive.
	int pairedDeltaUs = -1;  // -1 = no paired DOWN found
	if (!down) {
		SDL_Event next;
		if (SDL_PeepEvents(&next, 1, SDL_PEEKEVENT,
				SDL_EVENT_KEY_DOWN, SDL_EVENT_KEY_DOWN) > 0) {
			if (next.key.scancode == ev.scancode) {
				const Uint64 dt = (next.key.timestamp >= ev.timestamp)
					? (next.key.timestamp - ev.timestamp)
					: 0;
				pairedDeltaUs = (int)(dt / 1000);
			}
		}
	}

	const char *name = SDL_GetScancodeName(ev.scancode);
	if (down) {
		fprintf(stderr,
			"[input-trace] DOWN sc=%d (%s) repeat=%d dt=%llu us\n",
			(int)ev.scancode, name ? name : "?",
			(int)ev.repeat,
			(unsigned long long)deltaUs);
	} else if (pairedDeltaUs >= 0) {
		fprintf(stderr,
			"[input-trace] UP   sc=%d (%s) dt=%llu us  paired-DOWN follows in %d us\n",
			(int)ev.scancode, name ? name : "?",
			(unsigned long long)deltaUs, pairedDeltaUs);
	} else {
		fprintf(stderr,
			"[input-trace] UP   sc=%d (%s) dt=%llu us  no-paired-DOWN\n",
			(int)ev.scancode, name ? name : "?",
			(unsigned long long)deltaUs);
	}

	if (logged == kTraceBudget) {
		fprintf(stderr,
			"[input-trace]   (budget exhausted for sc=%d; no further events logged for this key)\n",
			(int)ev.scancode);
	}
}

void ATInputSDL3_HandleKeyDown(const SDL_KeyboardEvent& ev) {
	ATInputSDL3_TraceKeyEvent(ev, true);

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

			// One-shot diagnostic: log the first time each scancode is seen,
			// reporting whether it is currently bound through the input map.
			// Helps diagnose user reports of "arrow keys don't work as
			// joystick" without polluting logs during normal play.
			static bool s_loggedScancode[SDL_SCANCODE_COUNT] = {};
			if (!ev.repeat && !s_loggedScancode[ev.scancode]) {
				s_loggedScancode[ev.scancode] = true;
				const char *name = SDL_GetScancodeName(ev.scancode);
				fprintf(stderr,
					"[input] first press: scancode=%d (%s) inputCode=0x%x mapped=%s overlap=%s\n",
					(int)ev.scancode, name ? name : "?", inputCode,
					mapped ? "yes" : "no",
					g_kbdOpts.mbAllowInputMapOverlap ? "yes" : "no");

				// On the very first input-mapped key of the session, also dump
				// the full set of enabled input maps and the controller(s) /
				// trigger(s) that this scancode resolves to.  This separates
				// "no map active" from "wrong map active" from "multiple maps
				// fighting each other" in user bug reports.
				static bool s_dumpedMaps = false;
				if (!s_dumpedMaps) {
					s_dumpedMaps = true;
					ATInputManager *im = g_inputState.mpInputManager;
					const uint32 mapCount = im->GetInputMapCount();
					fprintf(stderr, "[input] active input maps (%u total):\n",
						mapCount);
					for (uint32 i = 0; i < mapCount; ++i) {
						vdrefptr<ATInputMap> imap;
						if (!im->GetInputMapByIndex(i, ~imap))
							continue;
						const bool en = im->IsInputMapEnabled(imap);
						const wchar_t *nm = imap->GetName();
						char nm8[256] = "?";
						if (nm) {
							size_t j = 0;
							for (; j < sizeof(nm8) - 1 && nm[j]; ++j)
								nm8[j] = (nm[j] < 0x80) ? (char)nm[j] : '?';
							nm8[j] = 0;
						}
						fprintf(stderr,
							"[input]   [%u] %s  enabled=%s  controllers=%u  mappings=%u\n",
							i, nm8, en ? "yes" : "no",
							imap->GetControllerCount(),
							imap->GetMappingCount());
						if (!en)
							continue;
						for (uint32 c = 0; c < imap->GetControllerCount(); ++c) {
							const auto& ctl = imap->GetController(c);
							fprintf(stderr,
								"[input]       controller[%u] type=%u index=%u\n",
								c, (unsigned)ctl.mType, ctl.mIndex);
						}
						for (uint32 m = 0; m < imap->GetMappingCount(); ++m) {
							const auto& mp = imap->GetMapping(m);
							if (mp.mInputCode == inputCode) {
								fprintf(stderr,
									"[input]       MATCH: inputCode=0x%x -> controllerId=%u code=0x%x\n",
									mp.mInputCode, mp.mControllerId, mp.mCode);
							}
						}
					}
				}
			}

			// Match Windows uivideodisplaywindow.cpp:2222-2230 + 2330 —
			// input-mapped keys do NOT retrigger on OS auto-repeat.
			// Skip the OnButtonDown call on repeat events, but still
			// honor "consumed" so cooked POKEY input doesn't double-fire.
			if (!ev.repeat)
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
	// Match Windows uivideodisplaywindow.cpp:2330 — special-key down
	// transitions only fire on the initial press, not on auto-repeat.
	if (!ev.repeat && HandleConsoleSwitch(ev.scancode, true))
		return;
	if (ev.repeat) {
		// Even on repeat, F2/F3/F4 in non-function-key mode are claimed
		// by the console-switch path and must not fall through to POKEY.
		extern ATUIKeyboardOptions g_kbdOpts;
		if (!g_kbdOpts.mbEnableFunctionKeys &&
			(ev.scancode == SDL_SCANCODE_F2 ||
			 ev.scancode == SDL_SCANCODE_F3 ||
			 ev.scancode == SDL_SCANCODE_F4))
			return;
	}

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
			ATInputSDL3_PushAtariKey(ev.scancode, fkeyCode, ev.repeat);
			return;
		}
	}

	// Path 2: Direct POKEY path for Atari keyboard typing
	if (!g_inputState.mpPokey) return;

	bool shift = (ev.mod & SDL_KMOD_SHIFT) != 0;
	bool ctrl  = (ev.mod & SDL_KMOD_CTRL) != 0;

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
	// reads these independently of key presses, e.g. raw keyboard mode).
	// Match Windows uivideodisplaywindow.cpp:2258 — only on the genuine
	// press transition, NOT on OS auto-repeat events, otherwise the shift
	// state register is re-pulsed at the OS repeat rate and games that
	// poll Shift as a hold see a stream of new presses.
	if (!ev.repeat) {
		g_inputState.mpPokey->SetShiftKeyState(shift, true);
		g_inputState.mpPokey->SetControlKeyState(ctrl);
	}

	// Custom layout mode: use custom key map for lookup instead of
	// hardcoded SDLScancodeToAtari() table.
	if (g_kbdOpts.mLayoutMode == ATUIKeyboardOptions::kLM_Custom) {
		uint32 vk = SDLScancodeToInputCode(ev.scancode);
		if (vk != kATInputCode_None) {
			uint32 scanCode;
			if (LookupCustomKeyMap(vk, shift, ctrl, alt,
					IsExtendedSDLKey(ev.scancode), scanCode)) {
				if (scanCode >= kATUIKeyScanCodeFirst) {
					// Special-key down (Start/Select/Option/Break)
					// only on genuine press, not auto-repeat — matches
					// Windows uivideodisplaywindow.cpp:2330.
					if (!ev.repeat)
						HandleCustomConsoleSwitch(scanCode, true, ev.scancode);
				} else {
					// Scan code from custom map already has shift/ctrl baked in.
					// Routes through the raw/cooked helper so raw keyboard mode
					// matches Windows ProcessVirtKey (uivideodisplaywindow.cpp:2341).
					ATInputSDL3_PushAtariKey(ev.scancode, (uint8)scanCode, ev.repeat);
				}
			}
		}
		return;  // Custom mode: unmapped keys do nothing
	}

	// Handle Break (Pause/Break key) for Natural/Raw modes.
	// Ctrl+Pause is Debug.Break, handled in main_sdl3.cpp before reaching here.
	// In Custom mode, Break is routed through the custom key map above.
	// Match Windows uivideodisplaywindow.cpp:2382 — Break is held via
	// SetBreakKeyState(true), released by the key-up handler.  Repeats are
	// already filtered above.
	if (ev.scancode == SDL_SCANCODE_PAUSE) {
		g_inputState.mpPokey->SetBreakKeyState(true, !g_kbdOpts.mbFullRawKeys);
		return;
	}

	// Arrow keys — replicate the Windows default key map logic from
	// uikeyboard.cpp:580-605.  The Atari keyboard encodes arrows as
	// Ctrl+<punct> (Ctrl+- = Up/0x0E, Ctrl+= = Down/0x0F, Ctrl++ = Left/0x86,
	// Ctrl+* = Right/0x87).  The per-combo Shift/Ctrl bits depend on
	// mArrowKeyMode (Invert / Auto / Default), identical to the
	// kCtrlShiftMasks table in uikeyboard.cpp.
	//
	// The final scan code already has the Ctrl/Shift bits baked in by the
	// csmask, so we must PushKey() directly and return without letting the
	// generic "ctrl |= 0x80 / shift |= 0x40" fall-through re-apply them.
	//
	// Alt is intentionally ignored here so that PC Alt+Arrow still produces
	// the Atari arrow scan code (matches Windows, which registers an explicit
	// Alt variant for every arrow mapping).  This is what lets Alt+Arrow move
	// the cursor in BASIC on the SDL3 build, the same as the Win32 build.
	{
		uint8 arrowBase = 0xFF;
		switch (ev.scancode) {
		case SDL_SCANCODE_UP:    arrowBase = 0x0E; break;
		case SDL_SCANCODE_DOWN:  arrowBase = 0x0F; break;
		case SDL_SCANCODE_LEFT:  arrowBase = 0x06; break;
		case SDL_SCANCODE_RIGHT: arrowBase = 0x07; break;
		default: break;
		}
		if (arrowBase != 0xFF) {
			// Mirrors kCtrlShiftMasks in src/Altirra/source/uikeyboard.cpp:584.
			//                              N     S     C     C+S
			static const uint8 kCsMasks[3][4] = {
				/* kAKM_InvertCtrl  */ { 0x80, 0xC0, 0x00, 0x40 },
				/* kAKM_AutoCtrl    */ { 0x80, 0x40, 0x80, 0xC0 },
				/* kAKM_DefaultCtrl */ { 0x00, 0x40, 0x80, 0xC0 },
			};
			unsigned mode = (unsigned)g_kbdOpts.mArrowKeyMode;
			if (mode >= ATUIKeyboardOptions::kAKMCount)
				mode = ATUIKeyboardOptions::kAKM_DefaultCtrl;
			const unsigned j = (ctrl ? 2u : 0u) | (shift ? 1u : 0u);
			const uint8 atariArrow = arrowBase | kCsMasks[mode][j];
			ATInputSDL3_PushAtariKey(ev.scancode, atariArrow, ev.repeat);
			return;
		}
	}

	uint8 atariCode = SDLScancodeToAtari(ev.scancode, shift, ctrl);
	if (atariCode == 0xFF) return;

	if (ctrl)  atariCode |= 0x80;
	if (shift) atariCode |= 0x40;

	ATInputSDL3_PushAtariKey(ev.scancode, atariCode, ev.repeat);
}

void ATInputSDL3_HandleKeyUp(const SDL_KeyboardEvent& ev) {
	ATInputSDL3_TraceKeyEvent(ev, false);

	// Release through ATInputManager FIRST (matches Windows ProcessKeyUp
	// which releases input-mapped keys before handling console switches).
	if (g_inputState.mpInputManager) {
		uint32 inputCode = SDLScancodeToInputCode(ev.scancode);
		if (inputCode != kATInputCode_None)
			g_inputState.mpInputManager->OnButtonUp(0, inputCode);
	}

	// Raw keyboard mode: release the matching Atari scancode if this
	// SDL scancode is currently held.  Mirrors Windows ProcessKeyUp
	// (uivideodisplaywindow.cpp:2280-2286).
	{
		extern ATUIKeyboardOptions g_kbdOpts;
		auto it = g_rawActiveKeys.find(ev.scancode);
		if (it != g_rawActiveKeys.end()) {
			if (g_inputState.mpPokey)
				g_inputState.mpPokey->ReleaseRawKey(it->second, !g_kbdOpts.mbFullRawKeys);
			g_rawActiveKeys.erase(it);
		}
	}

	// Release any console switches activated by custom key map, regardless of
	// current mode — the user may have switched modes while a key was held.
	{
		auto it = g_customConsoleSwitches.find(ev.scancode);
		if (it != g_customConsoleSwitches.end()) {
			if (g_inputState.mpGTIA)
				g_inputState.mpGTIA->SetConsoleSwitch(it->second, false);
			g_customConsoleSwitches.erase(it);
			return;
		}
	}

	// Release custom-mapped Break key (mirrors Windows ProcessSpecialKey
	// up branch — uivideodisplaywindow.cpp:2382 with state=false).
	{
		auto it = g_customBreakKeys.find(ev.scancode);
		if (it != g_customBreakKeys.end()) {
			extern ATUIKeyboardOptions g_kbdOpts;
			if (g_inputState.mpPokey)
				g_inputState.mpPokey->SetBreakKeyState(false, !g_kbdOpts.mbFullRawKeys);
			g_customBreakKeys.erase(it);
			return;
		}
	}

	// Release Break for the standard (Natural/Raw) PAUSE path.
	if (ev.scancode == SDL_SCANCODE_PAUSE) {
		extern ATUIKeyboardOptions g_kbdOpts;
		if (g_inputState.mpPokey)
			g_inputState.mpPokey->SetBreakKeyState(false, !g_kbdOpts.mbFullRawKeys);
		return;
	}

	// Console switches (standard F2/F3/F4 path)
	HandleConsoleSwitch(ev.scancode, false);
}

void ATInputSDL3_HandleTextInput(const char *text) {
	// Handle character ("cooked") mode mappings in custom layout.
	// SDL_EVENT_TEXT_INPUT fires for printable characters after key composition.
	extern ATUIKeyboardOptions g_kbdOpts;
	if (g_kbdOpts.mLayoutMode != ATUIKeyboardOptions::kLM_Custom)
		return;

	if (!g_inputState.mpPokey || !text || !text[0])
		return;

	// Decode first UTF-8 character (SDL3 guarantees valid UTF-8)
	uint32 ch = 0;
	uint8 c0 = (uint8)text[0];
	if (c0 < 0x80)
		ch = c0;
	else if (c0 >= 0xC2 && c0 < 0xE0 && ((uint8)text[1] & 0xC0) == 0x80)
		ch = ((c0 & 0x1F) << 6) | ((uint8)text[1] & 0x3F);
	else if (c0 >= 0xE0 && c0 < 0xF0
		&& ((uint8)text[1] & 0xC0) == 0x80 && ((uint8)text[2] & 0xC0) == 0x80)
		ch = ((c0 & 0x0F) << 12) | (((uint8)text[1] & 0x3F) << 6) | ((uint8)text[2] & 0x3F);
	else
		return;

	if (ch == 0 || ch > 0xFFFF)
		return;

	// Search custom map for cooked mapping
	uint32 keyInputCode = (ch << 9) | kATUIKeyboardMappingModifier_Cooked;
	const auto& keyMap = ATUIGetCustomKeyMapRef();
	auto it = std::lower_bound(keyMap.begin(), keyMap.end(), keyInputCode);
	if (it != keyMap.end() && (*it & 0xFFFFFE00) == keyInputCode) {
		uint32 scanCode = *it & 0x1FF;
		if (scanCode < kATUIKeyScanCodeFirst)
			g_inputState.mpPokey->PushKey((uint8)scanCode, false);
	}
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
		// Also release Break in case it was held when focus was lost.
		g_inputState.mpPokey->SetBreakKeyState(false, !g_kbdOpts.mbFullRawKeys);
	}

	// Release console switches (including any custom-mapped ones)
	if (g_inputState.mpGTIA)
		g_inputState.mpGTIA->SetConsoleSwitch(0x07, false);
	g_customConsoleSwitches.clear();
	g_customBreakKeys.clear();
	g_rawActiveKeys.clear();
}
