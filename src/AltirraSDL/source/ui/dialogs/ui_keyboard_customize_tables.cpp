//	AltirraSDL - Keyboard Customize dialog data tables
//	Scan code name table, VKey default mappings, ImGuiKey → VK table,
//	VKey name lookup, and mapping-to-string formatter. Split out of
//	ui_keyboard_customize.cpp (Phase 2g).

#include <stdafx.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <imgui.h>
#include <vd2/system/vdtypes.h>
#include <vd2/system/VDString.h>
#include <vd2/system/vdstl.h>

#include "uikeyboard.h"
#include "inputdefs.h"
#include "ui_keyboard_customize_internal.h"

// =========================================================================
// Scan code utility functions (ported from uikeyboard.cpp)
// =========================================================================

bool ATIsValidScanCodeLocal(uint32 c) {
	if (c >= 0x100)
		return c <= kATUIKeyScanCodeLast;

	switch (c & 0x3F) {
	case 0x09:
	case 0x19:
	case 0x24:
	case 0x29:
	case 0x31:
	case 0x3B:
		return false;
	default:
		return true;
	}
}

// Returns UTF-8 name for an Atari scan code (ported from uikeyboard.cpp).
const char *GetNameForKeyCode(uint32 c) {
	switch (c) {
	case kATUIKeyScanCode_Start:  return "Start";
	case kATUIKeyScanCode_Select: return "Select";
	case kATUIKeyScanCode_Option: return "Option";
	case kATUIKeyScanCode_Break:  return "Break";

	case 0x3F: return "A";
	case 0x15: return "B";
	case 0x12: return "C";
	case 0x3A: return "D";
	case 0x2A: return "E";
	case 0x38: return "F";
	case 0x3D: return "G";
	case 0x39: return "H";
	case 0x0D: return "I";
	case 0x01: return "J";
	case 0x05: return "K";
	case 0x00: return "L";
	case 0x25: return "M";
	case 0x23: return "N";
	case 0x08: return "O";
	case 0x0A: return "P";
	case 0x2F: return "Q";
	case 0x28: return "R";
	case 0x3E: return "S";
	case 0x2D: return "T";
	case 0x0B: return "U";
	case 0x10: return "V";
	case 0x2E: return "W";
	case 0x16: return "X";
	case 0x2B: return "Y";
	case 0x17: return "Z";
	case 0x1F: return "1";
	case 0x1E: return "2";
	case 0x1A: return "3";
	case 0x18: return "4";
	case 0x1D: return "5";
	case 0x1B: return "6";
	case 0x33: return "7";
	case 0x35: return "8";
	case 0x30: return "9";
	case 0x32: return "0";
	case 0x03: return "F1";
	case 0x04: return "F2";
	case 0x13: return "F3";
	case 0x14: return "F4";
	case 0x22: return ".";
	case 0x20: return ",";
	case 0x02: return ";";
	case 0x06: return "+";
	case 0x07: return "*";
	case 0x0E: return "-";
	case 0x0F: return "=";
	case 0x26: return "/";
	case 0x36: return "<";
	case 0x37: return ">";
	case 0x21: return "Space";
	case 0x0C: return "Enter";
	case 0x34: return "Backspace";
	case 0x1C: return "Esc";
	case 0x2C: return "Tab";
	case 0x27: return "Invert (Fuji)";
	case 0x11: return "Help";
	case 0x3C: return "Caps";
	case 0x7F: return "Shift+A";
	case 0x55: return "Shift+B";
	case 0x52: return "Shift+C";
	case 0x7A: return "Shift+D";
	case 0x6A: return "Shift+E";
	case 0x78: return "Shift+F";
	case 0x7D: return "Shift+G";
	case 0x79: return "Shift+H";
	case 0x4D: return "Shift+I";
	case 0x41: return "Shift+J";
	case 0x45: return "Shift+K";
	case 0x40: return "Shift+L";
	case 0x65: return "Shift+M";
	case 0x63: return "Shift+N";
	case 0x48: return "Shift+O";
	case 0x4A: return "Shift+P";
	case 0x6F: return "Shift+Q";
	case 0x68: return "Shift+R";
	case 0x7E: return "Shift+S";
	case 0x6D: return "Shift+T";
	case 0x4B: return "Shift+U";
	case 0x50: return "Shift+V";
	case 0x6E: return "Shift+W";
	case 0x56: return "Shift+X";
	case 0x6B: return "Shift+Y";
	case 0x57: return "Shift+Z";
	case 0x5F: return "Shift+1 (!)";
	case 0x5E: return "Shift+2 (\")";
	case 0x5A: return "Shift+3 (#)";
	case 0x58: return "Shift+4 ($)";
	case 0x5D: return "Shift+5 (%)";
	case 0x5B: return "Shift+6 (&)";
	case 0x73: return "Shift+7 (')";
	case 0x75: return "Shift+8 (@)";
	case 0x70: return "Shift+9 (()";
	case 0x72: return "Shift+0 ())";
	case 0x43: return "Shift+F1";
	case 0x44: return "Shift+F2";
	case 0x53: return "Shift+F3";
	case 0x54: return "Shift+F4";
	case 0x60: return "Shift+, ([)";
	case 0x62: return "Shift+. (])";
	case 0x42: return "Shift+; (:)";
	case 0x46: return "Shift++ (\\)";
	case 0x47: return "Shift+* (^)";
	case 0x4E: return "Shift+- (_)";
	case 0x4F: return "Shift+= (|)";
	case 0x66: return "Shift+/ (?)";
	case 0x76: return "Shift+< (Clear)";
	case 0x77: return "Shift+> (Insert Line)";
	case 0x61: return "Shift+Space";
	case 0x4C: return "Shift+Enter";
	case 0x74: return "Shift+Back (Delete Line)";
	case 0x5C: return "Shift+Esc";
	case 0x6C: return "Shift+Tab";
	case 0x67: return "Shift+Invert (Fuji)";
	case 0x51: return "Shift+Help";
	case 0x7C: return "Shift+Caps";
	case 0xBF: return "Ctrl+A";
	case 0x95: return "Ctrl+B";
	case 0x92: return "Ctrl+C";
	case 0xBA: return "Ctrl+D";
	case 0xAA: return "Ctrl+E";
	case 0xB8: return "Ctrl+F";
	case 0xBD: return "Ctrl+G";
	case 0xB9: return "Ctrl+H";
	case 0x8D: return "Ctrl+I";
	case 0x81: return "Ctrl+J";
	case 0x85: return "Ctrl+K";
	case 0x80: return "Ctrl+L";
	case 0xA5: return "Ctrl+M";
	case 0xA3: return "Ctrl+N";
	case 0x88: return "Ctrl+O";
	case 0x8A: return "Ctrl+P";
	case 0xAF: return "Ctrl+Q";
	case 0xA8: return "Ctrl+R";
	case 0xBE: return "Ctrl+S";
	case 0xAD: return "Ctrl+T";
	case 0x8B: return "Ctrl+U";
	case 0x90: return "Ctrl+V";
	case 0xAE: return "Ctrl+W";
	case 0x96: return "Ctrl+X";
	case 0xAB: return "Ctrl+Y";
	case 0x97: return "Ctrl+Z";
	case 0x9F: return "Ctrl+1";
	case 0x9E: return "Ctrl+2";
	case 0x9A: return "Ctrl+3";
	case 0x98: return "Ctrl+4";
	case 0x9D: return "Ctrl+5";
	case 0x9B: return "Ctrl+6";
	case 0xB3: return "Ctrl+7";
	case 0xB5: return "Ctrl+8";
	case 0xB0: return "Ctrl+9";
	case 0xB2: return "Ctrl+0";
	case 0x83: return "Ctrl+F1";
	case 0x84: return "Ctrl+F2";
	case 0x93: return "Ctrl+F3";
	case 0x94: return "Ctrl+F4";
	case 0xA0: return "Ctrl+,";
	case 0xA2: return "Ctrl+.";
	case 0x82: return "Ctrl+;";
	case 0x86: return "Ctrl++ (Left)";
	case 0x87: return "Ctrl+* (Right)";
	case 0x8E: return "Ctrl+- (Up)";
	case 0x8F: return "Ctrl+= (Down)";
	case 0xA6: return "Ctrl+/";
	case 0xB6: return "Ctrl+<";
	case 0xB7: return "Ctrl+> (Insert Char)";
	case 0xA1: return "Ctrl+Space";
	case 0x8C: return "Ctrl+Enter";
	case 0xB4: return "Ctrl+Back (Delete Char)";
	case 0x9C: return "Ctrl+Esc";
	case 0xAC: return "Ctrl+Tab";
	case 0xA7: return "Ctrl+Invert (Fuji)";
	case 0x91: return "Ctrl+Help";
	case 0xBC: return "Ctrl+Caps";
	case 0xFF: return "Ctrl+Shift+A";
	case 0xD5: return "Ctrl+Shift+B";
	case 0xD2: return "Ctrl+Shift+C";
	case 0xFA: return "Ctrl+Shift+D";
	case 0xEA: return "Ctrl+Shift+E";
	case 0xF8: return "Ctrl+Shift+F";
	case 0xFD: return "Ctrl+Shift+G";
	case 0xF9: return "Ctrl+Shift+H";
	case 0xCD: return "Ctrl+Shift+I";
	case 0xC1: return "Ctrl+Shift+J";
	case 0xC5: return "Ctrl+Shift+K";
	case 0xC0: return "Ctrl+Shift+L";
	case 0xE5: return "Ctrl+Shift+M";
	case 0xE3: return "Ctrl+Shift+N";
	case 0xC8: return "Ctrl+Shift+O";
	case 0xCA: return "Ctrl+Shift+P";
	case 0xEF: return "Ctrl+Shift+Q";
	case 0xE8: return "Ctrl+Shift+R";
	case 0xFE: return "Ctrl+Shift+S";
	case 0xED: return "Ctrl+Shift+T";
	case 0xCB: return "Ctrl+Shift+U";
	case 0xD0: return "Ctrl+Shift+V";
	case 0xEE: return "Ctrl+Shift+W";
	case 0xD6: return "Ctrl+Shift+X";
	case 0xEB: return "Ctrl+Shift+Y";
	case 0xD7: return "Ctrl+Shift+Z";
	case 0xDF: return "Ctrl+Shift+1";
	case 0xDE: return "Ctrl+Shift+2";
	case 0xDA: return "Ctrl+Shift+3";
	case 0xD8: return "Ctrl+Shift+4";
	case 0xDD: return "Ctrl+Shift+5";
	case 0xDB: return "Ctrl+Shift+6";
	case 0xF3: return "Ctrl+Shift+7";
	case 0xF5: return "Ctrl+Shift+8";
	case 0xF0: return "Ctrl+Shift+9";
	case 0xF2: return "Ctrl+Shift+0";
	case 0xC3: return "Ctrl+Shift+F1";
	case 0xC4: return "Ctrl+Shift+F2";
	case 0xD3: return "Ctrl+Shift+F3";
	case 0xD4: return "Ctrl+Shift+F4";
	case 0xE0: return "Ctrl+Shift+,";
	case 0xE2: return "Ctrl+Shift+.";
	case 0xC2: return "Ctrl+Shift+;";
	case 0xC6: return "Ctrl+Shift++";
	case 0xC7: return "Ctrl+Shift+*";
	case 0xCE: return "Ctrl+Shift+-";
	case 0xCF: return "Ctrl+Shift+=";
	case 0xE6: return "Ctrl+Shift+/";
	case 0xF6: return "Ctrl+Shift+<";
	case 0xF7: return "Ctrl+Shift+>";
	case 0xE1: return "Ctrl+Shift+Space";
	case 0xCC: return "Ctrl+Shift+Enter";
	case 0xF4: return "Ctrl+Shift+Backspace";
	case 0xDC: return "Ctrl+Shift+Esc";
	case 0xEC: return "Ctrl+Shift+Tab";
	case 0xE7: return "Ctrl+Shift+Invert (Fuji)";
	case 0xD1: return "Ctrl+Shift+Help";
	case 0xFC: return "Ctrl+Shift+Caps";
	default: return nullptr;
	}
}

// =========================================================================
// Scan code table (from uikeyboardcustomize.cpp)
// =========================================================================

const uint32 kScanCodeTable[] = {
	kATUIKeyScanCode_Start,
	kATUIKeyScanCode_Select,
	kATUIKeyScanCode_Option,
	kATUIKeyScanCode_Break,

	0x3F, 0x15, 0x12, 0x3A, 0x2A, 0x38, 0x3D, 0x39,  // A-H
	0x0D, 0x01, 0x05, 0x00, 0x25, 0x23, 0x08, 0x0A,  // I-P
	0x2F, 0x28, 0x3E, 0x2D, 0x0B, 0x10, 0x2E, 0x16,  // Q-X
	0x2B, 0x17,                                        // Y-Z
	0x1F, 0x1E, 0x1A, 0x18, 0x1D, 0x1B, 0x33, 0x35,  // 1-8
	0x30, 0x32,                                        // 9-0
	0x03, 0x04, 0x13, 0x14,                            // F1-F4
	0x22, 0x20, 0x02, 0x06, 0x07, 0x0E, 0x0F, 0x26,  // . , ; + * - = /
	0x36, 0x37,                                        // < >
	0x21, 0x0C, 0x34, 0x1C, 0x2C, 0x27, 0x11, 0x3C,  // Space Enter Back Esc Tab Fuji Help Caps

	// Shift variants
	0x7F, 0x55, 0x52, 0x7A, 0x6A, 0x78, 0x7D, 0x79,  // Shift+A-H
	0x4D, 0x41, 0x45, 0x40, 0x65, 0x63, 0x48, 0x4A,  // Shift+I-P
	0x6F, 0x68, 0x7E, 0x6D, 0x4B, 0x50, 0x6E, 0x56,  // Shift+Q-X
	0x6B, 0x57,                                        // Shift+Y-Z
	0x5F, 0x5E, 0x5A, 0x58, 0x5D, 0x5B, 0x73, 0x75,  // Shift+1-8
	0x70, 0x72,                                        // Shift+9-0
	0x43, 0x44, 0x53, 0x54,                            // Shift+F1-F4
	0x60, 0x62, 0x42, 0x46, 0x47, 0x4E, 0x4F, 0x66,  // Shift+. , ; + * - = /
	0x76, 0x77,                                        // Shift+< >
	0x61, 0x4C, 0x74, 0x5C, 0x6C, 0x67, 0x51, 0x7C,  // Shift+Space..Caps

	// Ctrl variants
	0xBF, 0x95, 0x92, 0xBA, 0xAA, 0xB8, 0xBD, 0xB9,  // Ctrl+A-H
	0x8D, 0x81, 0x85, 0x80, 0xA5, 0xA3, 0x88, 0x8A,  // Ctrl+I-P
	0xAF, 0xA8, 0xBE, 0xAD, 0x8B, 0x90, 0xAE, 0x96,  // Ctrl+Q-X
	0xAB, 0x97,                                        // Ctrl+Y-Z
	0x9F, 0x9E, 0x9A, 0x98, 0x9D, 0x9B, 0xB3, 0xB5,  // Ctrl+1-8
	0xB0, 0xB2,                                        // Ctrl+9-0
	0x83, 0x84, 0x93, 0x94,                            // Ctrl+F1-F4
	0xA0, 0xA2, 0x82, 0x86, 0x87, 0x8E, 0x8F, 0xA6,  // Ctrl+. , ; + * - = /
	0xB6, 0xB7,                                        // Ctrl+< >
	0xA1, 0x8C, 0xB4, 0x9C, 0xAC, 0xA7, 0x91, 0xBC,  // Ctrl+Space..Caps

	// Ctrl+Shift variants
	0xFF, 0xD5, 0xD2, 0xFA, 0xEA, 0xF8, 0xFD, 0xF9,  // Ctrl+Shift+A-H
	0xCD, 0xC1, 0xC5, 0xC0, 0xE5, 0xE3, 0xC8, 0xCA,  // Ctrl+Shift+I-P
	0xEF, 0xE8, 0xFE, 0xED, 0xCB, 0xD0, 0xEE, 0xD6,  // Ctrl+Shift+Q-X
	0xEB, 0xD7,                                        // Ctrl+Shift+Y-Z
	0xDF, 0xDE, 0xDA, 0xD8, 0xDD, 0xDB, 0xF3, 0xF5,  // Ctrl+Shift+1-8
	0xF0, 0xF2,                                        // Ctrl+Shift+9-0
	0xC3, 0xC4, 0xD3, 0xD4,                            // Ctrl+Shift+F1-F4
	0xE0, 0xE2, 0xC2, 0xC6, 0xC7, 0xCE, 0xCF, 0xE6,  // Ctrl+Shift+. , ; + * - = /
	0xF6, 0xF7,                                        // Ctrl+Shift+< >
	0xE1, 0xCC, 0xF4, 0xDC, 0xEC, 0xE7, 0xD1, 0xFC,  // Ctrl+Shift+Space..Caps
};

const int kScanCodeTableSize = (int)(sizeof(kScanCodeTable) / sizeof(kScanCodeTable[0]));

// =========================================================================
// Default key map generation — provided by source/input/keyboard_keymap_sdl3.cpp
// (the canonical port of uikeyboard.cpp lines 1-681).  The local copy that
// previously lived here has been removed in favor of a single source of
// truth shared with input_sdl3.cpp at runtime.
// =========================================================================

#if 0  // legacy block kept commented for reference
namespace {
	enum : uint32 {
		kShift = kATUIKeyboardMappingModifier_Shift,
		kCtrl = kATUIKeyboardMappingModifier_Ctrl,
		kAlt = kATUIKeyboardMappingModifier_Alt,
		kExtended = kATUIKeyboardMappingModifier_Extended
	};

	// VK constants matching Windows (same as kATInputCode_Key* values)
	enum : uint32 {
		VK_BACK      = 0x08,
		VK_TAB       = 0x09,
		VK_RETURN    = 0x0D,
		VK_PAUSE     = 0x13,
		VK_CAPITAL   = 0x14,
		VK_ESCAPE    = 0x1B,
		VK_SPACE     = 0x20,
		VK_PRIOR     = 0x21,
		VK_NEXT      = 0x22,
		VK_END       = 0x23,
		VK_HOME      = 0x24,
		VK_LEFT      = 0x25,
		VK_UP        = 0x26,
		VK_RIGHT     = 0x27,
		VK_DOWN      = 0x28,
		VK_INSERT    = 0x2D,
		VK_DELETE    = 0x2E,
		VK_CANCEL    = 0x03,
		VK_F1        = 0x70,
		VK_F2        = 0x71,
		VK_F3        = 0x72,
		VK_F4        = 0x73,
		VK_F5        = 0x74,
		VK_F6        = 0x75,
		VK_F7        = 0x76,
		VK_OEM_1     = 0xBA,
		VK_OEM_PLUS  = 0xBB,
		VK_OEM_COMMA = 0xBC,
		VK_OEM_MINUS = 0xBD,
		VK_OEM_PERIOD= 0xBE,
		VK_OEM_2     = 0xBF,
		VK_OEM_3     = 0xC0,
		VK_OEM_4     = 0xDB,
		VK_OEM_5     = 0xDC,
		VK_OEM_6     = 0xDD,
		VK_OEM_7     = 0xDE,
	};
}

#define VKEYMAP(vkey, mods, sc) (((vkey) << 9) + (mods) + (sc))
#define VKEYMAP_CSALL(vkey, sc) \
	VKEYMAP((vkey), 0, (sc)),	\
	VKEYMAP((vkey), kShift, (sc) + 0x40),	\
	VKEYMAP((vkey), kCtrl, (sc) + 0x80),	\
	VKEYMAP((vkey), kCtrl + kShift, (sc) + 0xC0)

#define VKEYMAP_CSXOR(vkey, sc) \
	VKEYMAP((vkey), 0, (sc)),	\
	VKEYMAP((vkey), kShift, (sc) + 0x40),	\
	VKEYMAP((vkey), kCtrl, (sc) + 0x80)

#define VKEYMAP_C(vkey, sc) \
	VKEYMAP((vkey), kCtrl, (sc) + 0x80)

#define VKEYMAP_C_SALL(vkey, sc) \
	VKEYMAP((vkey), kCtrl, (sc) + 0x80),	\
	VKEYMAP((vkey), kCtrl + kShift, (sc) + 0xC0)

static const uint32 g_ATDefaultVKeyMap[] = {
	VKEYMAP_CSALL(VK_TAB,      0x2C),
	VKEYMAP_CSALL(VK_BACK,     0x34),
	VKEYMAP_CSALL(VK_RETURN,   0x0C),
	VKEYMAP(VK_RETURN, kExtended, 0x0C),
	VKEYMAP(VK_RETURN, kExtended + kShift, 0x4C),
	VKEYMAP(VK_RETURN, kExtended + kCtrl, 0x8C),
	VKEYMAP(VK_RETURN, kExtended + kCtrl + kShift, 0xCC),
	VKEYMAP_CSALL(VK_ESCAPE,   0x1C),
	VKEYMAP_CSALL(VK_END,      0x27),    // Fuji
	VKEYMAP_CSXOR(VK_F6,       0x11),    // Help
	VKEYMAP(VK_OEM_1, kCtrl,   0x82),    // ;:
	VKEYMAP(VK_OEM_PLUS, kCtrl, 0x86),   // +
	VKEYMAP(VK_OEM_4, kCtrl,           0xE0),
	VKEYMAP(VK_OEM_4, kCtrl + kShift,  0xE0),
	VKEYMAP(VK_OEM_5, kCtrl,           0x9C),
	VKEYMAP(VK_OEM_5, kCtrl + kShift,  0xDC),
	VKEYMAP(VK_OEM_6, kCtrl,           0xE2),
	VKEYMAP(VK_OEM_6, kCtrl + kShift,  0xE2),
	VKEYMAP(VK_OEM_COMMA, kCtrl,       0xA0),
	VKEYMAP(VK_OEM_PERIOD, kCtrl,      0xA2),
	VKEYMAP(VK_OEM_2, kCtrl,           0xA6),
	VKEYMAP(VK_OEM_2, kCtrl + kShift,  0xE6),
	VKEYMAP(VK_HOME,  0,               0x76),
	VKEYMAP(VK_HOME,  kShift,          0x76),
	VKEYMAP(VK_HOME,  kCtrl,           0xB6),
	VKEYMAP(VK_HOME,  kCtrl + kShift,  0xF6),
	VKEYMAP(VK_DELETE, 0,              0xB4),
	VKEYMAP(VK_DELETE, kShift,         0x74),
	VKEYMAP(VK_DELETE, kCtrl,          0xF4),
	VKEYMAP(VK_DELETE, kCtrl + kShift, 0xF4),
	VKEYMAP(VK_INSERT, 0,             0xB7),
	VKEYMAP(VK_INSERT, kShift,        0x77),
	VKEYMAP(VK_INSERT, kCtrl,         0xF7),
	VKEYMAP(VK_INSERT, kCtrl + kShift,0xF7),
	VKEYMAP(VK_SPACE, kShift,         0x61),
	VKEYMAP(VK_SPACE, kCtrl,          0xA1),
	VKEYMAP(VK_SPACE, kCtrl + kShift, 0xE1),

	VKEYMAP_C_SALL('A', 0x3F),
	VKEYMAP_C     ('B', 0x15),
	VKEYMAP_C     ('C', 0x12),
	VKEYMAP_C_SALL('D', 0x3A),
	VKEYMAP_C_SALL('E', 0x2A),
	VKEYMAP_C_SALL('F', 0x38),
	VKEYMAP_C_SALL('G', 0x3D),
	VKEYMAP_C_SALL('H', 0x39),
	VKEYMAP_C_SALL('I', 0x0D),
	VKEYMAP_C     ('J', 0x01),
	VKEYMAP_C     ('K', 0x05),
	VKEYMAP_C     ('L', 0x00),
	VKEYMAP_C_SALL('M', 0x25),
	VKEYMAP_C_SALL('N', 0x23),
	VKEYMAP_C_SALL('O', 0x08),
	VKEYMAP_C_SALL('P', 0x0A),
	VKEYMAP_C_SALL('Q', 0x2F),
	VKEYMAP_C_SALL('R', 0x28),
	VKEYMAP_C_SALL('S', 0x3E),
	VKEYMAP_C_SALL('T', 0x2D),
	VKEYMAP_C_SALL('U', 0x0B),
	VKEYMAP_C     ('V', 0x10),
	VKEYMAP_C_SALL('W', 0x2E),
	VKEYMAP_C     ('X', 0x16),
	VKEYMAP_C_SALL('Y', 0x2B),
	VKEYMAP_C     ('Z', 0x17),
	VKEYMAP_C_SALL('0', 0x32),
	VKEYMAP_C_SALL('1', 0x1F),
	VKEYMAP_C_SALL('2', 0x1E),
	VKEYMAP_C_SALL('3', 0x1A),
	VKEYMAP_C_SALL('4', 0x18),
	VKEYMAP_C_SALL('5', 0x1D),
	VKEYMAP_C_SALL('6', 0x1B),
	VKEYMAP_C_SALL('7', 0x33),
	VKEYMAP_C_SALL('8', 0x35),
	VKEYMAP_C_SALL('9', 0x30),
	VKEYMAP_CSALL(VK_CAPITAL, 0x3C),

	VKEYMAP('0', kCtrl + kShift + kAlt, 0xF2),
};

static const uint32 g_ATRawVKeyMap[] = {
	VKEYMAP_CSXOR('L', 0x00),
	VKEYMAP_CSXOR('J', 0x01),
	VKEYMAP_CSXOR(VK_OEM_1, 0x02),
	VKEYMAP_CSXOR('K', 0x05),
	VKEYMAP_CSXOR(VK_OEM_7, 0x06),
	VKEYMAP_CSXOR(VK_OEM_5, 0x07),
	VKEYMAP_CSALL('O', 0x08),
	VKEYMAP_CSALL('P', 0x0A),
	VKEYMAP_CSALL('U', 0x0B),
	VKEYMAP_CSALL(VK_RETURN, 0x0C),
	VKEYMAP(VK_RETURN, kExtended, 0x0C),
	VKEYMAP(VK_RETURN, kExtended + kShift, 0x4C),
	VKEYMAP(VK_RETURN, kExtended + kCtrl, 0x8C),
	VKEYMAP(VK_RETURN, kExtended + kCtrl + kShift, 0xCC),
	VKEYMAP_CSALL('I', 0x0D),
	VKEYMAP_CSALL(VK_OEM_4, 0x0E),
	VKEYMAP_CSALL(VK_OEM_6, 0x0F),
	VKEYMAP_CSXOR('V', 0x10),
	VKEYMAP_CSXOR(VK_F6, 0x11),
	VKEYMAP_CSXOR('C', 0x12),
	VKEYMAP_CSXOR('B', 0x15),
	VKEYMAP_CSXOR('X', 0x16),
	VKEYMAP_CSXOR('Z', 0x17),
	VKEYMAP_CSALL('4', 0x18),
	VKEYMAP_CSALL('3', 0x1A),
	VKEYMAP_CSALL('6', 0x1B),
	VKEYMAP_CSALL(VK_ESCAPE, 0x1C),
	VKEYMAP_CSALL('5', 0x1D),
	VKEYMAP_CSALL('2', 0x1E),
	VKEYMAP_CSALL('1', 0x1F),
	VKEYMAP_CSALL(VK_OEM_COMMA, 0x20),
	VKEYMAP_CSALL(VK_SPACE, 0x21),
	VKEYMAP_CSALL(VK_OEM_PERIOD, 0x22),
	VKEYMAP_CSALL('N', 0x23),
	VKEYMAP_CSALL('M', 0x25),
	VKEYMAP_CSALL(VK_OEM_2, 0x26),
	VKEYMAP_CSALL(VK_END, 0x27),
	VKEYMAP_CSALL('R', 0x28),
	VKEYMAP_CSALL('E', 0x2A),
	VKEYMAP_CSALL('Y', 0x2B),
	VKEYMAP_CSALL(VK_TAB, 0x2C),
	VKEYMAP_CSALL('T', 0x2D),
	VKEYMAP_CSALL('W', 0x2E),
	VKEYMAP_CSALL('Q', 0x2F),
	VKEYMAP_CSALL('9', 0x30),
	VKEYMAP_CSALL('0', 0x32),
	VKEYMAP_CSALL('7', 0x33),
	VKEYMAP_CSALL(VK_BACK, 0x34),
	VKEYMAP_CSALL('8', 0x35),
	VKEYMAP_CSALL(VK_OEM_MINUS, 0x36),
	VKEYMAP_CSALL(VK_OEM_PLUS, 0x37),
	VKEYMAP_CSALL('F', 0x38),
	VKEYMAP_CSALL('H', 0x39),
	VKEYMAP_CSALL('D', 0x3A),
	VKEYMAP_CSALL(VK_CAPITAL, 0x3C),
	VKEYMAP_CSALL('G', 0x3D),
	VKEYMAP_CSALL('S', 0x3E),
	VKEYMAP_CSALL('A', 0x3F),
	VKEYMAP('0', kCtrl + kShift + kAlt, 0xF2),
};

static const uint32 g_ATDefaultVKeyMapCommonSSO[] = {
	VKEYMAP(VK_F2, 0, kATUIKeyScanCode_Start),
	VKEYMAP(VK_F2, kShift, kATUIKeyScanCode_Start),
	VKEYMAP(VK_F2, kCtrl, kATUIKeyScanCode_Start),
	VKEYMAP(VK_F2, kCtrl + kShift, kATUIKeyScanCode_Start),
	VKEYMAP(VK_F3, 0, kATUIKeyScanCode_Select),
	VKEYMAP(VK_F3, kShift, kATUIKeyScanCode_Select),
	VKEYMAP(VK_F3, kCtrl, kATUIKeyScanCode_Select),
	VKEYMAP(VK_F3, kCtrl + kShift, kATUIKeyScanCode_Select),
	VKEYMAP(VK_F4, 0, kATUIKeyScanCode_Option),
	VKEYMAP(VK_F4, kShift, kATUIKeyScanCode_Option),
	VKEYMAP(VK_F4, kCtrl, kATUIKeyScanCode_Option),
	VKEYMAP(VK_F4, kCtrl + kShift, kATUIKeyScanCode_Option),
};

static const uint32 g_ATDefaultVKeyMapCommonBreak[] = {
	VKEYMAP(VK_F7, 0, kATUIKeyScanCode_Break),
	VKEYMAP(VK_F7, kShift, kATUIKeyScanCode_Break),
	VKEYMAP(VK_F7, kCtrl, kATUIKeyScanCode_Break),
	VKEYMAP(VK_F7, kCtrl + kShift, kATUIKeyScanCode_Break),
	VKEYMAP(VK_PAUSE, 0, kATUIKeyScanCode_Break),
	VKEYMAP(VK_PAUSE, kShift, kATUIKeyScanCode_Break),
	VKEYMAP(VK_PAUSE, kCtrl, kATUIKeyScanCode_Break),
	VKEYMAP(VK_PAUSE, kCtrl + kShift, kATUIKeyScanCode_Break),
	VKEYMAP(VK_CANCEL, 0, kATUIKeyScanCode_Break),
	VKEYMAP(VK_CANCEL, kShift, kATUIKeyScanCode_Break),
	VKEYMAP(VK_CANCEL, kCtrl, kATUIKeyScanCode_Break),
	VKEYMAP(VK_CANCEL, kCtrl + kShift, kATUIKeyScanCode_Break),
};

static const uint32 g_ATDefaultVKeyMapFKey[] = {
	VKEYMAP_CSXOR(VK_F1, 0x03),
	VKEYMAP_CSXOR(VK_F2, 0x04),
	VKEYMAP_CSXOR(VK_F3, 0x13),
	VKEYMAP_CSXOR(VK_F4, 0x14),
};

// Character → scan code table for default "cooked" mappings
static constexpr uint8 kATUIDefaultCharToScanCodeMappings[][2] = {
	{ 'l', 0x00 }, { 'L', 0x40 }, { 'j', 0x01 }, { 'J', 0x41 },
	{ ';', 0x02 }, { ':', 0x42 }, { 'k', 0x05 }, { 'K', 0x45 },
	{ '+', 0x06 }, { '\\', 0x46 }, { '*', 0x07 }, { '^', 0x47 },
	{ 'o', 0x08 }, { 'O', 0x48 }, { 'p', 0x0A }, { 'P', 0x4A },
	{ 'u', 0x0B }, { 'U', 0x4B }, { 'i', 0x0D }, { 'I', 0x4D },
	{ '-', 0x0E }, { '_', 0x4E }, { '=', 0x0F }, { '|', 0x4F },
	{ 'v', 0x10 }, { 'V', 0x50 }, { 'c', 0x12 }, { 'C', 0x52 },
	{ 'b', 0x15 }, { 'B', 0x55 }, { 'x', 0x16 }, { 'X', 0x56 },
	{ 'z', 0x17 }, { 'Z', 0x57 },
	{ '4', 0x18 }, { '$', 0x58 }, { '3', 0x1A }, { '#', 0x5A },
	{ '6', 0x1B }, { '&', 0x5B }, { '5', 0x1D }, { '%', 0x5D },
	{ '2', 0x1E }, { '"', 0x5E }, { '1', 0x1F }, { '!', 0x5F },
	{ ',', 0x20 }, { '[', 0x60 }, { ' ', 0x21 },
	{ '.', 0x22 }, { ']', 0x62 }, { 'n', 0x23 }, { 'N', 0x63 },
	{ 'm', 0x25 }, { 'M', 0x65 }, { '/', 0x26 }, { '?', 0x66 },
	{ 'r', 0x28 }, { 'R', 0x68 }, { 'e', 0x2A }, { 'E', 0x6A },
	{ 'y', 0x2B }, { 'Y', 0x6B }, { 't', 0x2D }, { 'T', 0x6D },
	{ 'w', 0x2E }, { 'W', 0x6E }, { 'q', 0x2F }, { 'Q', 0x6F },
	{ '9', 0x30 }, { '(', 0x70 }, { '0', 0x32 }, { ')', 0x72 },
	{ '7', 0x33 }, { '\'', 0x73 }, { '8', 0x35 }, { '@', 0x75 },
	{ '<', 0x36 }, { '>', 0x37 },
	{ 'f', 0x38 }, { 'F', 0x78 }, { 'h', 0x39 }, { 'H', 0x79 },
	{ 'd', 0x3A }, { 'D', 0x7A }, { 'g', 0x3D }, { 'G', 0x7D },
	{ 's', 0x3E }, { 'S', 0x7E }, { 'a', 0x3F }, { 'A', 0x7F },
	{ '`', 0x27 }, { '~', 0x67 },
	// European characters (Latin-1 range, all fit in uint8)
	{ 0xE1, 0xA0 },  // latin small letter A with acute
	{ 0xF9, 0xBF },  // latin small letter U with grave
	{ 0xD1, 0x95 },  // latin capital letter N with tilde
	{ 0xC9, 0x92 },  // latin capital letter E with acute
	{ 0xE7, 0xBA },  // latin small letter C with cedilla
	{ 0xF4, 0xAA },  // latin small letter O with circumflex
	{ 0xF2, 0xB8 },  // latin small letter O with grave
	{ 0xEC, 0xBD },  // latin small letter I with grave
	{ 0xA3, 0xB9 },  // pound sign
	{ 0xEF, 0x8D },  // latin small letter I with diaeresis
	{ 0xFC, 0x81 },  // latin small letter U with diaeresis
	{ 0xE4, 0x85 },  // latin small letter A with diaeresis
	{ 0xD6, 0x80 },  // latin capital letter O with diaeresis
	{ 0xFA, 0xA5 },  // latin small letter U with acute
	{ 0xF3, 0xA3 },  // latin small letter O with acute
	{ 0xF6, 0x88 },  // latin small letter O with diaeresis
	{ 0xDC, 0x8A },  // latin capital letter U with diaeresis
	{ 0xE2, 0xAF },  // latin small letter A with circumflex
	{ 0xFB, 0xA8 },  // latin small letter U with circumflex
	{ 0xEE, 0xBE },  // latin small letter I with circumflex
	{ 0xE9, 0xAD },  // latin small letter E with acute
	{ 0xE8, 0x8B },  // latin small letter E with grave
	{ 0xF1, 0x90 },  // latin small letter N with tilde
	{ 0xEA, 0xAE },  // latin small letter E with circumflex
	{ 0xE5, 0x96 },  // latin small letter A with ring above
	{ 0xE0, 0xAB },  // latin small letter A with grave
	{ 0xC5, 0x97 },  // latin capital letter A with ring above
	{ 0xA1, 0xA2 },  // inverted exclamation mark
	{ 0xC4, 0x82 },  // latin capital letter A with diaeresis
};

static void RegisterVKeyMappings(vdfastvector<uint32>& dst, const uint32 *mappings, uint32 n) {
	while (n--) {
		uint32 mapping = *mappings++;

		// Force extended flag for navigation keys
		switch ((mapping >> 9) & 0xFFFF) {
		case VK_INSERT: case VK_DELETE: case VK_HOME: case VK_END:
		case VK_NEXT: case VK_PRIOR:
		case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN:
			mapping |= kExtended;
			break;
		}

		dst.push_back(mapping);
	}
}

static void AddDefaultCharMappings(vdfastvector<uint32>& dst) {
	for (const auto& m : kATUIDefaultCharToScanCodeMappings) {
		dst.push_back(((uint32)m[0] << 9) + m[1] + kATUIKeyboardMappingModifier_Cooked);
	}
}

void ATUIGetDefaultKeyMap(const ATUIKeyboardOptions& options, vdfastvector<uint32>& mappings) {
	mappings.clear();

	switch (options.mLayoutMode) {
	case ATUIKeyboardOptions::kLM_Natural:
	default:
		RegisterVKeyMappings(mappings, g_ATDefaultVKeyMap, (uint32)(sizeof(g_ATDefaultVKeyMap) / sizeof(g_ATDefaultVKeyMap[0])));
		break;
	case ATUIKeyboardOptions::kLM_Raw:
		RegisterVKeyMappings(mappings, g_ATRawVKeyMap, (uint32)(sizeof(g_ATRawVKeyMap) / sizeof(g_ATRawVKeyMap[0])));
		break;
	}

	RegisterVKeyMappings(mappings, g_ATDefaultVKeyMapCommonBreak, (uint32)(sizeof(g_ATDefaultVKeyMapCommonBreak) / sizeof(g_ATDefaultVKeyMapCommonBreak[0])));

	if (options.mbEnableFunctionKeys)
		RegisterVKeyMappings(mappings, g_ATDefaultVKeyMapFKey, (uint32)(sizeof(g_ATDefaultVKeyMapFKey) / sizeof(g_ATDefaultVKeyMapFKey[0])));
	else
		RegisterVKeyMappings(mappings, g_ATDefaultVKeyMapCommonSSO, (uint32)(sizeof(g_ATDefaultVKeyMapCommonSSO) / sizeof(g_ATDefaultVKeyMapCommonSSO[0])));

	// Arrow keys
	static const uint8 kArrowVKs[4] = { VK_UP, VK_DOWN, VK_LEFT, VK_RIGHT };
	static const uint8 kArrowKCs[4] = { 0x0E, 0x0F, 0x06, 0x07 };
	static const uint8 kCtrlShiftMasks[][4] = {
		{ 0x80, 0xC0, 0x00, 0x40 }, // invert
		{ 0x80, 0x40, 0x80, 0xC0 }, // auto
		{ 0x00, 0x40, 0x80, 0xC0 }, // default
	};

	const uint8 *csmasks = kCtrlShiftMasks[options.mArrowKeyMode];
	for (int i = 0; i < 4; ++i) {
		const uint32 baseVK = kArrowVKs[i];
		const uint8 kbcode = kArrowKCs[i];
		for (int j = 0; j < 4; ++j) {
			uint8 kbcode2 = kbcode | csmasks[j];
			mappings.push_back(ATUIPackKeyboardMapping(kbcode2, baseVK, (j << 25) + kATUIKeyboardMappingModifier_Extended));
			mappings.push_back(ATUIPackKeyboardMapping(kbcode2, baseVK, (j << 25) + kATUIKeyboardMappingModifier_Alt + kATUIKeyboardMappingModifier_Extended));
		}
	}

	AddDefaultCharMappings(mappings);

	// Strip invalid scan codes
	mappings.erase(
		std::remove_if(mappings.begin(), mappings.end(),
			[](uint32 mapping) -> bool { return !ATIsValidScanCodeLocal((uint8)mapping); }),
		mappings.end());

	std::sort(mappings.begin(), mappings.end());
}
#endif  // legacy block

// =========================================================================
// ImGuiKey → ATInputCode mapping table (shared with ui_input.cpp pattern)
// ImGuiKeyToVK is defined in ui_keyboard_customize_internal.h.
// =========================================================================

const ImGuiKeyToVK kImGuiKeyMap[] = {
	{ ImGuiKey_LeftCtrl, kATInputCode_KeyLControl },
	{ ImGuiKey_RightCtrl, kATInputCode_KeyRControl },
	{ ImGuiKey_LeftArrow, kATInputCode_KeyLeft },
	{ ImGuiKey_UpArrow, kATInputCode_KeyUp },
	{ ImGuiKey_RightArrow, kATInputCode_KeyRight },
	{ ImGuiKey_DownArrow, kATInputCode_KeyDown },
	{ ImGuiKey_0, kATInputCode_Key0 }, { ImGuiKey_1, kATInputCode_Key1 },
	{ ImGuiKey_2, kATInputCode_Key2 }, { ImGuiKey_3, kATInputCode_Key3 },
	{ ImGuiKey_4, kATInputCode_Key4 }, { ImGuiKey_5, kATInputCode_Key5 },
	{ ImGuiKey_6, kATInputCode_Key6 }, { ImGuiKey_7, kATInputCode_Key7 },
	{ ImGuiKey_8, kATInputCode_Key8 }, { ImGuiKey_9, kATInputCode_Key9 },
	{ ImGuiKey_A, kATInputCode_KeyA+0 }, { ImGuiKey_B, kATInputCode_KeyA+1 },
	{ ImGuiKey_C, kATInputCode_KeyA+2 }, { ImGuiKey_D, kATInputCode_KeyA+3 },
	{ ImGuiKey_E, kATInputCode_KeyA+4 }, { ImGuiKey_F, kATInputCode_KeyA+5 },
	{ ImGuiKey_G, kATInputCode_KeyA+6 }, { ImGuiKey_H, kATInputCode_KeyA+7 },
	{ ImGuiKey_I, kATInputCode_KeyA+8 }, { ImGuiKey_J, kATInputCode_KeyA+9 },
	{ ImGuiKey_K, kATInputCode_KeyA+10 }, { ImGuiKey_L, kATInputCode_KeyA+11 },
	{ ImGuiKey_M, kATInputCode_KeyA+12 }, { ImGuiKey_N, kATInputCode_KeyA+13 },
	{ ImGuiKey_O, kATInputCode_KeyA+14 }, { ImGuiKey_P, kATInputCode_KeyA+15 },
	{ ImGuiKey_Q, kATInputCode_KeyA+16 }, { ImGuiKey_R, kATInputCode_KeyA+17 },
	{ ImGuiKey_S, kATInputCode_KeyA+18 }, { ImGuiKey_T, kATInputCode_KeyA+19 },
	{ ImGuiKey_U, kATInputCode_KeyA+20 }, { ImGuiKey_V, kATInputCode_KeyA+21 },
	{ ImGuiKey_W, kATInputCode_KeyA+22 }, { ImGuiKey_X, kATInputCode_KeyA+23 },
	{ ImGuiKey_Y, kATInputCode_KeyA+24 }, { ImGuiKey_Z, kATInputCode_KeyA+25 },
	{ ImGuiKey_Backspace, kATInputCode_KeyBack },
	{ ImGuiKey_Tab, kATInputCode_KeyTab },
	{ ImGuiKey_Enter, kATInputCode_KeyReturn },
	{ ImGuiKey_Escape, kATInputCode_KeyEscape },
	{ ImGuiKey_Space, kATInputCode_KeySpace },
	{ ImGuiKey_PageUp, kATInputCode_KeyPrior },
	{ ImGuiKey_PageDown, kATInputCode_KeyNext },
	{ ImGuiKey_End, kATInputCode_KeyEnd },
	{ ImGuiKey_Home, kATInputCode_KeyHome },
	{ ImGuiKey_Insert, kATInputCode_KeyInsert },
	{ ImGuiKey_Delete, kATInputCode_KeyDelete },
	{ ImGuiKey_Keypad0, kATInputCode_KeyNumpad0 },
	{ ImGuiKey_Keypad1, kATInputCode_KeyNumpad1 },
	{ ImGuiKey_Keypad2, kATInputCode_KeyNumpad2 },
	{ ImGuiKey_Keypad3, kATInputCode_KeyNumpad3 },
	{ ImGuiKey_Keypad4, kATInputCode_KeyNumpad4 },
	{ ImGuiKey_Keypad5, kATInputCode_KeyNumpad5 },
	{ ImGuiKey_Keypad6, kATInputCode_KeyNumpad6 },
	{ ImGuiKey_Keypad7, kATInputCode_KeyNumpad7 },
	{ ImGuiKey_Keypad8, kATInputCode_KeyNumpad8 },
	{ ImGuiKey_Keypad9, kATInputCode_KeyNumpad9 },
	{ ImGuiKey_KeypadEnter, kATInputCode_KeyNumpadEnter },
	{ ImGuiKey_KeypadMultiply, kATInputCode_KeyMultiply },
	{ ImGuiKey_KeypadAdd, kATInputCode_KeyAdd },
	{ ImGuiKey_KeypadSubtract, kATInputCode_KeySubtract },
	{ ImGuiKey_KeypadDecimal, kATInputCode_KeyDecimal },
	{ ImGuiKey_KeypadDivide, kATInputCode_KeyDivide },
	{ ImGuiKey_F1, kATInputCode_KeyF1 }, { ImGuiKey_F2, kATInputCode_KeyF2 },
	{ ImGuiKey_F3, kATInputCode_KeyF3 }, { ImGuiKey_F4, kATInputCode_KeyF4 },
	{ ImGuiKey_F5, kATInputCode_KeyF5 }, { ImGuiKey_F6, kATInputCode_KeyF6 },
	{ ImGuiKey_F7, kATInputCode_KeyF7 }, { ImGuiKey_F8, kATInputCode_KeyF8 },
	{ ImGuiKey_F9, kATInputCode_KeyF9 }, { ImGuiKey_F10, kATInputCode_KeyF10 },
	{ ImGuiKey_F11, kATInputCode_KeyF11 }, { ImGuiKey_F12, kATInputCode_KeyF12 },
	{ ImGuiKey_Semicolon, kATInputCode_KeyOem1 },
	{ ImGuiKey_Equal, kATInputCode_KeyOemPlus },
	{ ImGuiKey_Comma, kATInputCode_KeyOemComma },
	{ ImGuiKey_Minus, kATInputCode_KeyOemMinus },
	{ ImGuiKey_Period, kATInputCode_KeyOemPeriod },
	{ ImGuiKey_Slash, kATInputCode_KeyOem2 },
	{ ImGuiKey_GraveAccent, kATInputCode_KeyOem3 },
	{ ImGuiKey_LeftBracket, kATInputCode_KeyOem4 },
	{ ImGuiKey_Backslash, kATInputCode_KeyOem5 },
	{ ImGuiKey_RightBracket, kATInputCode_KeyOem6 },
	{ ImGuiKey_Apostrophe, kATInputCode_KeyOem7 },
	{ ImGuiKey_CapsLock, 0x14 },  // VK_CAPITAL
	{ ImGuiKey_Pause, 0x13 },     // VK_PAUSE
};

bool IsExtendedVK(uint32 vk) {
	switch (vk) {
	case kATInputCode_KeyLeft: case kATInputCode_KeyRight:
	case kATInputCode_KeyUp: case kATInputCode_KeyDown:
	case kATInputCode_KeyInsert: case kATInputCode_KeyDelete:
	case kATInputCode_KeyHome: case kATInputCode_KeyEnd:
	case kATInputCode_KeyPrior: case kATInputCode_KeyNext:
	case kATInputCode_KeyNumpadEnter:
		return true;
	default:
		return false;
	}
}

// =========================================================================
// Host key name display
// =========================================================================

const char *GetVKName(uint32 vk) {
	// Single-char names for alphanumeric VK codes.
	// Only one return value is used at a time (called from FormatMappingHostKey
	// which copies the result immediately via snprintf).
	static char s_charBuf[2];
	if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) {
		s_charBuf[0] = (char)vk;
		s_charBuf[1] = 0;
		return s_charBuf;
	}
	switch (vk) {
	case kATInputCode_KeyBack:    return "Backspace";
	case kATInputCode_KeyTab:     return "Tab";
	case kATInputCode_KeyReturn:  return "Enter";
	case kATInputCode_KeyEscape:  return "Escape";
	case kATInputCode_KeySpace:   return "Space";
	case kATInputCode_KeyPrior:   return "Page Up";
	case kATInputCode_KeyNext:    return "Page Down";
	case kATInputCode_KeyEnd:     return "End";
	case kATInputCode_KeyHome:    return "Home";
	case kATInputCode_KeyLeft:    return "Left";
	case kATInputCode_KeyUp:      return "Up";
	case kATInputCode_KeyRight:   return "Right";
	case kATInputCode_KeyDown:    return "Down";
	case kATInputCode_KeyInsert:  return "Insert";
	case kATInputCode_KeyDelete:  return "Delete";
	case kATInputCode_KeyNumpad0: return "Numpad 0";
	case kATInputCode_KeyNumpad1: return "Numpad 1";
	case kATInputCode_KeyNumpad2: return "Numpad 2";
	case kATInputCode_KeyNumpad3: return "Numpad 3";
	case kATInputCode_KeyNumpad4: return "Numpad 4";
	case kATInputCode_KeyNumpad5: return "Numpad 5";
	case kATInputCode_KeyNumpad6: return "Numpad 6";
	case kATInputCode_KeyNumpad7: return "Numpad 7";
	case kATInputCode_KeyNumpad8: return "Numpad 8";
	case kATInputCode_KeyNumpad9: return "Numpad 9";
	case kATInputCode_KeyNumpadEnter: return "Numpad Enter";
	case kATInputCode_KeyMultiply:  return "Numpad *";
	case kATInputCode_KeyAdd:       return "Numpad +";
	case kATInputCode_KeySubtract:  return "Numpad -";
	case kATInputCode_KeyDecimal:   return "Numpad .";
	case kATInputCode_KeyDivide:    return "Numpad /";
	case kATInputCode_KeyF1:  return "F1";  case kATInputCode_KeyF2:  return "F2";
	case kATInputCode_KeyF3:  return "F3";  case kATInputCode_KeyF4:  return "F4";
	case kATInputCode_KeyF5:  return "F5";  case kATInputCode_KeyF6:  return "F6";
	case kATInputCode_KeyF7:  return "F7";  case kATInputCode_KeyF8:  return "F8";
	case kATInputCode_KeyF9:  return "F9";  case kATInputCode_KeyF10: return "F10";
	case kATInputCode_KeyF11: return "F11"; case kATInputCode_KeyF12: return "F12";
	case kATInputCode_KeyOem1:      return ";";
	case kATInputCode_KeyOemPlus:   return "=";
	case kATInputCode_KeyOemComma:  return ",";
	case kATInputCode_KeyOemMinus:  return "-";
	case kATInputCode_KeyOemPeriod: return ".";
	case kATInputCode_KeyOem2:      return "/";
	case kATInputCode_KeyOem3:      return "`";
	case kATInputCode_KeyOem4:      return "[";
	case kATInputCode_KeyOem5:      return "\\";
	case kATInputCode_KeyOem6:      return "]";
	case kATInputCode_KeyOem7:      return "'";
	case 0x14:                      return "Caps Lock";
	case 0x13:                      return "Pause";
	case 0x03:                      return "Cancel";
	default: return "?";
	}
}

void FormatMappingHostKey(uint32 mapping, char *buf, size_t bufSize) {
	buf[0] = 0;

	if (mapping & kATUIKeyboardMappingModifier_Cooked) {
		// Character mode
		uint32 ch = (mapping >> 9) & 0xFFFF;
		if (ch >= 0x20 && ch < 0x7F)
			snprintf(buf, bufSize, "Char '%c'", (char)ch);
		else
			snprintf(buf, bufSize, "Char U+%04X", ch);
		return;
	}

	// VK mode with modifiers
	char prefix[64] = {};
	if (mapping & kATUIKeyboardMappingModifier_Alt)
		strcat(prefix, "Alt+");
	if (mapping & kATUIKeyboardMappingModifier_Ctrl)
		strcat(prefix, "Ctrl+");
	if (mapping & kATUIKeyboardMappingModifier_Shift)
		strcat(prefix, "Shift+");

	uint32 vk = (mapping >> 9) & 0xFFFF;
	const char *name = GetVKName(vk);
	snprintf(buf, bufSize, "%s%s", prefix, name);
}

const int kImGuiKeyMapSize = (int)(sizeof(kImGuiKeyMap) / sizeof(kImGuiKeyMap[0]));
