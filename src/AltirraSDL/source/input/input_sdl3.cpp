//	Altirra SDL3 frontend - input handling
//
//	Keyboard input has three dispatch paths, one per independent axis of
//	"what the key means":
//
//	1. ATInputManager path (physical position):
//	     SDL_Scancode → ATInputCode (VK equivalent) → OnButtonDown/Up()
//	   Used for input mapping — arrow keys as joystick, function keys as
//	   console buttons, WASD as a gamepad, etc.  Always scancode-based so
//	   muscle memory / remapped profiles follow finger position regardless
//	   of the OS keyboard layout.  Matches Windows uivideodisplaywindow.cpp,
//	   which feeds VK codes to ATInputManager for the controller mapping.
//
//	2. POKEY direct path (layout-aware key identity):
//	     a) SDL_Keycode (ev.key)  → VK  [letters, digits, OEM punctuation]
//	     b) SDL_Scancode fallback → VK  [Enter, Esc, Tab, Space, arrows,
//	                                     F-keys, modifiers, numpad, ...]
//	     → ATUIGetScanCodeForVirtualKey() → PushKey()
//	   Layout-aware for character keys (so Ctrl+Z works on a German
//	   layout where the Z key is physically SDL_SCANCODE_Y), physical for
//	   layout-invariant keys.  This mirrors how ImGui_ImplSDL3 splits its
//	   mapping and how Windows Altirra naturally behaves — Win32 VK codes
//	   are layout-aware for letters and digits, so WM_KEYDOWN on Windows
//	   already delivers the "layout key identity" we reconstruct here.
//
//	3. Cooked character path (composed text):
//	     SDL_EVENT_TEXT_INPUT → HandleTextInput → cooked character map
//	   Handles non-ASCII characters, dead keys, AltGr compositions and
//	   IME output.  The OS has already applied the keyboard layout and
//	   compose state, so we just look the resulting character up in the
//	   Atari character map.  Path 2 intentionally returns without
//	   injecting anything when ev.key is a non-ASCII printable, leaving
//	   the SDL_EVENT_TEXT_INPUT that follows to drive this path.

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
#ifdef ALTIRRA_NETPLAY_ENABLED
#include "../netplay/netplay_input.h"
#endif

// Declared in uiaccessors_stubs.cpp — provides read-only access to the sorted
// custom key map without copying.
extern const vdfastvector<uint32>& ATUIGetCustomKeyMapRef();

// -------------------------------------------------------------------------
// SDL scancode → ATInputCode (Windows VK equivalent) for input mapping
// AND for the unified Atari keymap lookup.
//
// Atari keyboard scan codes are no longer translated here directly.
// Instead, the unified path in HandleKeyDown calls
// ATUIGetScanCodeForVirtualKey() which uses the canonical keymap built
// by ATUIInitVirtualKeyMap (see source/input/keyboard_keymap_sdl3.cpp,
// itself a port of src/Altirra/source/uikeyboard.cpp).  This eliminates
// the hand-rolled scancode table and pulls in every Atari quirk
// (Delete=Ctrl+Back=0xB4, Home=Clear=0x76, Insert=Ctrl+>=0xB7, Ctrl+\=
// Ctrl+Esc=0x9C, the cooked European character table, etc.) for free.
// -------------------------------------------------------------------------
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
// SDL_Keycode (layout-aware) → Win32 VK.
//
// Used by the POKEY direct path for letter/digit/OEM-punctuation keys whose
// identity must follow the OS keyboard layout, so that Ctrl+<letter> and
// typed characters work correctly on non-US layouts.  On a German layout,
// pressing the physical Y key (SDL_SCANCODE_Y) produces ev.key == 'z'
// (SDL_Keycode numerically equal to the ASCII codepoint of the base
// character); this function maps 'z' → VK_Z (0x5A), so the Atari keymap
// lookup fires as if the user had pressed Z on a US layout — matching
// Windows Altirra, which gets the layout-aware VK from WM_KEYDOWN directly.
//
// Mirrors the split used by ImGui_ImplSDL3_KeyEventToImGuiKey: letters,
// digits and punctuation use SDL_Keycode; everything else (Enter, Esc,
// Tab, Backspace, Space, arrows, Home/End, PgUp/PgDn, Ins/Del, F-keys,
// modifiers, numpad, CapsLock/NumLock) falls through to the physical
// SDL_Scancode path below.
//
// Important properties that keep the range checks safe:
//   • Printable ASCII SDL_Keycode values equal their character code.
//   • Non-printable SDL_Keycode values (arrows, function keys, numpad,
//     modifiers, etc.) all have SDLK_SCANCODE_MASK (0x40000000) set, so
//     they are always > 0x7F and cannot match the ASCII ranges here.
//   • SDLK_RETURN (0x0D), SDLK_BACKSPACE (0x08), SDLK_TAB (0x09),
//     SDLK_ESCAPE (0x1B) and SDLK_SPACE (0x20) are printable-ASCII
//     codepoints but intentionally excluded — they are layout-invariant
//     and handled via the scancode fallback.
//   • Non-ASCII printable characters (ü, é, ß, ...) return None and
//     are delivered via SDL_EVENT_TEXT_INPUT through the cooked
//     character map, which is the only path that can correctly handle
//     dead keys, AltGr composition and IME.
// -------------------------------------------------------------------------
static uint32 SDLKeycodeToVK_Layout(SDL_Keycode kc) {
	// Letters: normalise to uppercase, which matches the Win32 VK numbering
	// for A–Z exactly (VK_A = 0x41 .. VK_Z = 0x5A).  Accept either case so
	// the function is robust regardless of whether SDL3 reports the shifted
	// or base form in ev.key on a given platform / SDL build.
	if (kc >= 'a' && kc <= 'z') return (uint32)(kc - 'a' + 0x41);
	if (kc >= 'A' && kc <= 'Z') return (uint32)kc;

	// Digits: '0'..'9' equal VK_0..VK_9 numerically.
	if (kc >= '0' && kc <= '9') return (uint32)kc;

	// OEM punctuation — mapped to the US-layout VK assignments that the
	// Atari keymap (built in keyboard_keymap_sdl3.cpp from uikeyboard.cpp)
	// is indexed by.  When the user presses the key that prints ';' on
	// their layout, we route it through VK_OEM_1 regardless of where that
	// key physically sits, so Ctrl+; and friends behave consistently.
	switch (kc) {
	case ';':  return kATInputCode_KeyOem1;       // 0xBA
	case '=':  return kATInputCode_KeyOemPlus;    // 0xBB
	case ',':  return kATInputCode_KeyOemComma;   // 0xBC
	case '-':  return kATInputCode_KeyOemMinus;   // 0xBD
	case '.':  return kATInputCode_KeyOemPeriod;  // 0xBE
	case '/':  return kATInputCode_KeyOem2;       // 0xBF
	case '`':  return kATInputCode_KeyOem3;       // 0xC0
	case '[':  return kATInputCode_KeyOem4;       // 0xDB
	case '\\': return kATInputCode_KeyOem5;       // 0xDC
	case ']':  return kATInputCode_KeyOem6;       // 0xDD
	case '\'': return kATInputCode_KeyOem7;       // 0xDE
	default:   return kATInputCode_None;
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
// Extended-key flag — mirrors the Win32 "extended scan code" bit so that
// keymap entries flagged with kATUIKeyboardMappingModifier_Extended (arrow
// keys, navigation cluster, numpad Enter) match correctly.
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
#ifdef ALTIRRA_NETPLAY_ENABLED
	// Netplay: forward keyboard edges into the lockstep pipeline
	// instead of pushing straight to POKEY, so both peers see the
	// same Atari key events on the same emulated frame.
	if (ATNetplayInput::IsSuppressingLocalInput()) {
		if (!repeat) ATNetplayInput::OnLocalKeyDown(atariCode);
		return;
	}
#endif
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

static void HandleSpecialScanCode(uint32 scanCode, bool down, SDL_Scancode sdlSc) {
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

#ifdef ALTIRRA_NETPLAY_ENABLED
	// Netplay: forward the switch edge to the lockstep pipeline
	// rather than driving GTIA directly.
	if (ATNetplayInput::IsSuppressingLocalInput()) {
		ATNetplayInput::OnLocalConsoleSwitch(switchBit, down);
		if (down)
			g_customConsoleSwitches[sdlSc] = switchBit;
		else
			g_customConsoleSwitches.erase(sdlSc);
		return;
	}
#endif

	g_inputState.mpGTIA->SetConsoleSwitch(switchBit, down);

	if (down)
		g_customConsoleSwitches[sdlSc] = switchBit;
	else
		g_customConsoleSwitches.erase(sdlSc);
}

// -------------------------------------------------------------------------

// Note: F2/F3/F4 → Start/Select/Option console switches are now driven by
// the unified keymap (g_ATDefaultVKeyMapCommonSSO in keyboard_keymap_sdl3.cpp),
// which produces kATUIKeyScanCode_Start/Select/Option scan codes that
// HandleSpecialScanCode() routes to GTIA.  This matches Windows.

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

#ifndef ALTIRRA_INPUT_TRACE
#define ALTIRRA_INPUT_TRACE 0
#endif

// Bounded event trace: log up to kTraceBudget events per traced scancode,
// each as a single line with direction, repeat flag, delta since the previous
// event on the same scancode, and — for UP events — whether a paired KEY_DOWN
// for the same scancode is already queued immediately behind it.  A "paired"
// DOWN within 1 ms of the UP is the direct signature of X11-style autorepeat
// chatter; seeing "paired=no" on every UP rules that theory out entirely.
// Once the per-scancode budget is exhausted, nothing more is logged.
static void ATInputSDL3_TraceKeyEvent(const SDL_KeyboardEvent& ev, bool down) {
#if ALTIRRA_INPUT_TRACE
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
#else
	(void)ev;
	(void)down;
#endif
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

			// Optional one-shot diagnostics for key-map issues.
#if ALTIRRA_INPUT_TRACE
			static bool s_loggedScancode[SDL_SCANCODE_COUNT] = {};
			if (!ev.repeat && !s_loggedScancode[ev.scancode]) {
				s_loggedScancode[ev.scancode] = true;
				const char *name = SDL_GetScancodeName(ev.scancode);
				fprintf(stderr,
					"[input] first press: scancode=%d (%s) inputCode=0x%x mapped=%s overlap=%s\n",
					(int)ev.scancode, name ? name : "?", inputCode,
					mapped ? "yes" : "no",
					g_kbdOpts.mbAllowInputMapOverlap ? "yes" : "no");

				static bool s_dumpedMaps = false;
				if (!s_dumpedMaps) {
					s_dumpedMaps = true;
					ATInputManager *im = g_inputState.mpInputManager;
					const uint32 mapCount = im->GetInputMapCount();
					fprintf(stderr, "[input] active input maps (%u total):\n", mapCount);
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
#endif

			// Windows uivideodisplaywindow.cpp:2225 forwards every press to
			// OnButtonDown, including OS auto-repeats — matched here.
			g_inputState.mpInputManager->OnButtonDown(0, inputCode);
			if (mapped && !g_kbdOpts.mbAllowInputMapOverlap)
				consumedByInputMap = true;
		}
	}

	if (consumedByInputMap)
		return;

	// -----------------------------------------------------------------
	// Path 2: Unified Atari keyboard input via the shared keymap.
	//
	// Mirrors Windows ProcessKeyDown (uivideodisplaywindow.cpp:2216-2268)
	// which calls ATUIGetScanCodeForVirtualKey() for the canonical
	// Win32-vkey lookup.  The keymap is built by ATUIInitVirtualKeyMap
	// (keyboard_keymap_sdl3.cpp) from the same data as Windows
	// (g_ATDefaultVKeyMap, g_ATRawVKeyMap, common SSO/Break/FKey tables,
	// arrow-key generator, cooked character mappings).  This single
	// lookup replaces the hand-rolled SDLScancodeToAtari() table and
	// brings every Atari quirk for free: Delete=Ctrl+Back, Home=Clear,
	// Insert=Ctrl+>, Ctrl+\=Ctrl+Esc, Ctrl+[/], Shift+Enter (4C),
	// Ctrl+Enter (8C), Shift+Space (61), Ctrl+Space (A1), the Win10+
	// Ctrl+Shift+Alt+0 workaround, etc.  Custom layout mode is also
	// handled by the same code path, with ATUIInitVirtualKeyMap setting
	// the active map to the user-defined custom one.
	// -----------------------------------------------------------------
	if (!g_inputState.mpPokey)
		return;

	bool shift = (ev.mod & SDL_KMOD_SHIFT) != 0;
	bool ctrl  = (ev.mod & SDL_KMOD_CTRL) != 0;
	bool altForPokey = alt;

	// AltGr handling: SDL3 reports AltGr as SDL_KMOD_MODE on layouts that
	// have it (German, French, Spanish, ...) AND on Win32/X11 it is also
	// reported as the LCTRL+RALT physical pair the OS uses to synthesize
	// AltGr.  Either signal means the user is composing a printable
	// character via the OS layout — NOT pressing a Ctrl+Alt shortcut —
	// so suppress ctrl/alt for the POKEY scan code path and let
	// SDL_EVENT_TEXT_INPUT deliver the cooked character via the shared
	// character map.  Matches Windows behaviour where WM_CHAR delivers
	// the composed character regardless of WM_KEYDOWN.
	const bool altgr = (ev.mod & SDL_KMOD_MODE) != 0
		|| (((ev.mod & SDL_KMOD_LCTRL) != 0) && ((ev.mod & SDL_KMOD_RALT) != 0));
	if (altgr) {
		ctrl = false;
		altForPokey = false;
	}

	// Update POKEY shift/ctrl register state.  Pulse only on the genuine
	// press (no auto-repeat) so games polling SHFLOK don't see re-pulses
	// at the OS repeat rate (Windows uivideodisplaywindow.cpp:2258).
	// HandleKeyUp updates the same state on shift/ctrl release so the
	// POKEY register cannot latch past the physical key release.
	if (!ev.repeat) {
#ifdef ALTIRRA_NETPLAY_ENABLED
		if (ATNetplayInput::IsSuppressingLocalInput()) {
			ATNetplayInput::OnLocalShiftCtrlState(shift, ctrl);
		} else
#endif
		{
			g_inputState.mpPokey->SetShiftKeyState(shift, true);
			g_inputState.mpPokey->SetControlKeyState(ctrl);
		}
	}

	// Exclude Ctrl/Shift from POKEY when bound by input maps (e.g. Left
	// Ctrl as joystick fire).  Matches Windows ExcludeMappedCtrlShiftState().
	// SDL3 reports per-side modifier state directly in ev.mod via the
	// SDL_KMOD_LSHIFT/RSHIFT/LCTRL/RCTRL bits, so no SDL_GetKeyboardState
	// detour is needed.
	if (g_inputState.mpInputManager && !g_kbdOpts.mbAllowInputMapModifierOverlap) {
		if (shift) {
			const bool lShBound = g_inputState.mpInputManager->IsInputMapped(0, kATInputCode_KeyLShift);
			const bool rShBound = g_inputState.mpInputManager->IsInputMapped(0, kATInputCode_KeyRShift);
			if (lShBound || rShBound) {
				shift = (!lShBound && (ev.mod & SDL_KMOD_LSHIFT) != 0)
					 || (!rShBound && (ev.mod & SDL_KMOD_RSHIFT) != 0);
			}
		}
		if (ctrl) {
			const bool lCtBound = g_inputState.mpInputManager->IsInputMapped(0, kATInputCode_KeyLControl);
			const bool rCtBound = g_inputState.mpInputManager->IsInputMapped(0, kATInputCode_KeyRControl);
			if (lCtBound || rCtBound) {
				ctrl = (!lCtBound && (ev.mod & SDL_KMOD_LCTRL) != 0)
					|| (!rCtBound && (ev.mod & SDL_KMOD_RCTRL) != 0);
			}
		}
	}

	// Translate the SDL event to a Win32 VK number (matches the values in
	// inputdefs.h, which use Win32 VK numbering by design).
	//
	// Layout-aware first: for letters, digits and OEM punctuation we honour
	// the OS keyboard layout via ev.key (SDL_Keycode), so that e.g. Ctrl+Z
	// on a German layout (where the physical key is SDL_SCANCODE_Y) fires
	// the Atari Ctrl+Z entry in the keymap — matching Windows Altirra,
	// which receives a layout-aware VK_Z from WM_KEYDOWN directly.
	//
	// Physical fallback: for everything else (Enter, Esc, Tab, Backspace,
	// Space, arrows, Home/End, PgUp/PgDn, Ins/Del, F-keys, modifiers,
	// numpad, CapsLock/NumLock) we use ev.scancode via the existing table.
	// These keys are layout-invariant, so physical position is both
	// correct and unambiguous (notably, this keeps Numpad Enter distinct
	// from the main Enter key, which share a keycode).
	//
	// Non-ASCII printable characters (ü, é, ß, dead-key results, IME
	// output, AltGr compositions, ...) are NOT handled here at all — they
	// flow through SDL_EVENT_TEXT_INPUT → HandleTextInput → the cooked
	// character map, which is the only path that can correctly respect
	// the OS's compose / dead-key / IME state.
	//
	// Numpad note: SDL3's default SDL_HINT_KEYCODE_OPTIONS includes
	// `hide_numpad`, which makes numpad digit keys report ev.key == '0'..'9'
	// (ASCII) whenever NumLock is on.  That would cause the layout path to
	// resolve numpad digits to VK_0..VK_9 (main-row) and silently bypass
	// the numpad-specific keymap entries (VK_NUMPAD0..9 via the existing
	// scancode table).  Windows WM_KEYDOWN always delivers VK_NUMPAD* for
	// numpad keys, so we gate the layout path on the scancode not being a
	// numpad key; this forces numpad input to stay on the physical path.
	const bool isNumpadScancode =
		(ev.scancode >= SDL_SCANCODE_KP_DIVIDE &&
		 ev.scancode <= SDL_SCANCODE_KP_PERIOD) ||
		ev.scancode == SDL_SCANCODE_KP_EQUALS ||
		ev.scancode == SDL_SCANCODE_NUMLOCKCLEAR;
	uint32 vk = kATInputCode_None;
	if (!isNumpadScancode)
		vk = SDLKeycodeToVK_Layout(ev.key);
	if (vk == kATInputCode_None)
		vk = SDLScancodeToInputCode(ev.scancode);
	// Numpad Enter uses VK_RETURN with the extended flag in the Windows
	// keymap (uikeyboard.cpp:351-354 — VKEYMAP(VK_RETURN, kExtended, ...)).
	// kATInputCode_KeyNumpadEnter (0x10D) is an Altirra-internal value for
	// the input-manager only.  Rewrite it here so the keymap lookup hits
	// the extended Enter entries.
	if (vk == kATInputCode_KeyNumpadEnter)
		vk = kATInputCode_KeyReturn;
	if (vk == kATInputCode_None || vk >= 0x10000) {
		// No VK translation — let SDL_EVENT_TEXT_INPUT deliver the
		// printable character via the cooked map (HandleTextInput).
		return;
	}

	const bool ext = IsExtendedSDLKey(ev.scancode);
	uint32 scanCode = 0;
	if (!ATUIGetScanCodeForVirtualKey(vk, altForPokey, ctrl, shift, ext, scanCode)) {
		// Not in keymap — fall through to SDL_EVENT_TEXT_INPUT for the
		// cooked character path.  Mirrors Windows where ProcessKeyDown
		// returns false and WM_CHAR delivers the printable character.
		return;
	}

	if (scanCode >= kATUIKeyScanCodeFirst && scanCode <= kATUIKeyScanCodeLast) {
		// Special key (Start/Select/Option/Break).  Down only on the
		// genuine press, not auto-repeat — matches Windows
		// uivideodisplaywindow.cpp:2330.  HandleKeyUp releases via the
		// g_customConsoleSwitches / g_customBreakKeys trackers keyed
		// on the SDL scancode.
		if (!ev.repeat)
			HandleSpecialScanCode(scanCode, true, ev.scancode);
		return;
	}

	// Normal Atari scan code — already has the right shift/ctrl bits
	// baked in by the keymap, route to POKEY raw/cooked.
	ATInputSDL3_PushAtariKey(ev.scancode, (uint8)scanCode, ev.repeat);

	// Drain the immediately-following SDL_EVENT_TEXT_INPUT (if any) so we
	// don't double-push to POKEY.  SDL3 queues TEXT_INPUT right after the
	// KEY_DOWN that produced it inside the same SDL_PumpEvents pass, so
	// PeepEvents called from inside HandleKeyDown sees it ahead in the
	// queue.  This is the SDL3 equivalent of Win32 returning true from
	// WM_KEYDOWN to suppress the corresponding WM_CHAR.
	//
	// Concrete cases this fixes:
	//   • Shift+Space → keymap gives 0x61, TEXT_INPUT would give " "→0x21
	//   • Ctrl+Space  → keymap gives 0xA1, TEXT_INPUT would give " "→0x21
	//   • Ctrl+letters where the OS still emits a control-char text event
	//
	// We must only drain if TEXT_INPUT is the IMMEDIATE next event in the
	// queue — a SDL_PeepEvents call with the type filter set to just
	// TEXT_INPUT would scan past unrelated events (e.g. a mouse motion
	// queued between KEY_DOWN and a later TEXT_INPUT) and incorrectly
	// swallow text input from a different source.  So we peek with the
	// "any type" range, check the type, and only drain if it matches.
	//
	// We DO want TEXT_INPUT to fire when the keymap returned false above
	// (e.g. plain 'a' in Natural mode); that early-return path leaves the
	// TEXT_INPUT untouched.  Only the keymap-hit path drains.
	{
		SDL_Event next;
		const int peeked = SDL_PeepEvents(&next, 1, SDL_PEEKEVENT,
			SDL_EVENT_FIRST, SDL_EVENT_LAST);
		if (peeked > 0 && next.type == SDL_EVENT_TEXT_INPUT) {
			SDL_PeepEvents(&next, 1, SDL_GETEVENT,
				SDL_EVENT_TEXT_INPUT, SDL_EVENT_TEXT_INPUT);
		}
	}
}

void ATInputSDL3_HandleKeyUp(const SDL_KeyboardEvent& ev) {
	ATInputSDL3_TraceKeyEvent(ev, false);

	// Update POKEY shift/ctrl state when a modifier key itself is released.
	// SDL_GetModState() returns the live modifier state at the moment of
	// the event, so this correctly accounts for "the other shift is still
	// held".  This is the release counterpart to the SetShiftKeyState()
	// call in HandleKeyDown — without it, POKEY's SHFLOK / Control bits
	// could stay latched after the user lifts the modifier, until the
	// next non-modifier key event re-sampled ev.mod.  Matches Windows
	// uivideodisplaywindow.cpp:2316-2321 (UpdateCtrlShiftState on
	// kATUIVK_Shift release).
	if (g_inputState.mpPokey) {
		switch (ev.scancode) {
		case SDL_SCANCODE_LSHIFT:
		case SDL_SCANCODE_RSHIFT: {
			const SDL_Keymod m = SDL_GetModState();
			const bool shift = (m & SDL_KMOD_SHIFT) != 0;
#ifdef ALTIRRA_NETPLAY_ENABLED
			if (ATNetplayInput::IsSuppressingLocalInput()) {
				ATNetplayInput::OnLocalShiftCtrlState(shift,
					(m & SDL_KMOD_CTRL) != 0);
			} else
#endif
			g_inputState.mpPokey->SetShiftKeyState(shift, true);
			break;
		}
		case SDL_SCANCODE_LCTRL:
		case SDL_SCANCODE_RCTRL: {
			const SDL_Keymod m = SDL_GetModState();
			const bool ctrl = (m & SDL_KMOD_CTRL) != 0;
#ifdef ALTIRRA_NETPLAY_ENABLED
			if (ATNetplayInput::IsSuppressingLocalInput()) {
				ATNetplayInput::OnLocalShiftCtrlState(
					(m & SDL_KMOD_SHIFT) != 0, ctrl);
			} else
#endif
			g_inputState.mpPokey->SetControlKeyState(ctrl);
			break;
		}
		default:
			break;
		}
	}

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
#ifdef ALTIRRA_NETPLAY_ENABLED
			if (ATNetplayInput::IsSuppressingLocalInput()) {
				ATNetplayInput::OnLocalConsoleSwitch(it->second, false);
			} else
#endif
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

	// Note: F2/F3/F4 release is handled by the g_customConsoleSwitches
	// tracker above — populated by HandleSpecialScanCode on the press,
	// keyed on ev.scancode.  No separate hardcoded path needed.
}

void ATInputSDL3_HandleTextInput(const char *text) {
	// SDL_EVENT_TEXT_INPUT fires for printable characters after the OS
	// has applied the keyboard layout, dead-key composition, IME, and
	// AltGr.  This is the SDL3 equivalent of WM_CHAR on Win32.  We
	// route it through ATUIGetScanCodeForCharacter32() which uses the
	// shared cooked character map (built into the active keymap by
	// ATUIInitVirtualKeyMap), so the same `é`/`à`/`£`/`ñ`/`ö` etc.
	// translations Windows uses also work on SDL3.  This path is active
	// in ALL layout modes (Natural / Raw / Custom) — Windows allows the
	// same: cooked character entries are merged into every keymap.
	if (!g_inputState.mpPokey || !text || !text[0])
		return;

	// Iterate every UTF-8 codepoint in the buffer; SDL3 guarantees valid
	// UTF-8 input.  Most events deliver one character but composed input
	// or IME commit strings can be longer.
	const uint8 *p = (const uint8 *)text;
	while (*p) {
		uint32 ch = 0;
		const uint8 c0 = *p;
		if (c0 < 0x80) {
			ch = c0;
			p += 1;
		} else if (c0 >= 0xC2 && c0 < 0xE0 && (p[1] & 0xC0) == 0x80) {
			ch = ((c0 & 0x1F) << 6) | (p[1] & 0x3F);
			p += 2;
		} else if (c0 >= 0xE0 && c0 < 0xF0
			&& (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) {
			ch = ((c0 & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
			p += 3;
		} else if (c0 >= 0xF0 && c0 < 0xF5
			&& (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80 && (p[3] & 0xC0) == 0x80) {
			// 4-byte UTF-8 (BMP supplementary plane).  The cooked map only
			// has entries below U+10000, so this codepoint will not match —
			// but advance p so we don't loop forever on malformed input.
			p += 4;
			continue;
		} else {
			break;	// invalid UTF-8 — stop
		}

		if (ch == 0 || ch >= 0x10000)
			continue;

		uint32 scanCode = 0;
		if (ATUIGetScanCodeForCharacter32(ch, scanCode)) {
			if (scanCode < kATUIKeyScanCodeFirst) {
#ifdef ALTIRRA_NETPLAY_ENABLED
				if (ATNetplayInput::IsSuppressingLocalInput()) {
					ATNetplayInput::OnLocalKeyDown((uint8)scanCode);
					continue;
				}
#endif
				g_inputState.mpPokey->PushKey((uint8)scanCode, false);
			}
		}
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
