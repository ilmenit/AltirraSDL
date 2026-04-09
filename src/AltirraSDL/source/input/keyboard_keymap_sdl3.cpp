//	Altirra SDL3 frontend - keyboard keymap port
//
//	This is a verbatim port of the keymap data and lookup functions from
//	src/Altirra/source/uikeyboard.cpp (lines 1-681 of the upstream Win32
//	file).  We cannot compile uikeyboard.cpp directly on non-Windows
//	platforms because it includes <windows.h> for VK_* constants.
//
//	IMPORTANT: keep this file in sync with uikeyboard.cpp.  The data
//	tables (g_ATDefaultVKeyMap, g_ATRawVKeyMap, kATUIDefaultCharToScanCode-
//	Mappings, the common SSO/Break/FKey tables and the arrow-key generator
//	in ATUIGetDefaultKeyMap) are the source of truth for Atari scancode
//	translation on every platform.  When Avery Lee adds or modifies an
//	entry in uikeyboard.cpp, mirror the change here.
//
//	The accelerator-table half of uikeyboard.cpp (everything from line 927
//	onward — kATDefaultAccelTable*, ATUIInitDefaultAccelTables,
//	ATUIActivateVirtKeyMapping etc.) is intentionally NOT ported here.
//	Display-context accelerators on the SDL3 build are handled by the
//	menu system in main_sdl3.cpp / ui_menus.cpp via ATUIFindBoundKey.

#include <stdafx.h>
#include <algorithm>
#include <vd2/system/vdtypes.h>
#include <vd2/system/vdstl.h>
#include "uikeyboard.h"

// -------------------------------------------------------------------------
// Win32 VK_* constants used by the keymap data tables.  These are the
// canonical Microsoft values from <WinUser.h>; the SDLScancodeToInputCode()
// helper in input_sdl3.cpp translates SDL_Scancode to the same values, so
// the keymap built here can be looked up unchanged.  ATInputCode values in
// inputdefs.h also use Win32 VK numbering, which is what makes this work.
// -------------------------------------------------------------------------
#ifndef _WIN32
	#define VK_CANCEL     0x03
	#define VK_BACK       0x08
	#define VK_TAB        0x09
	#define VK_RETURN     0x0D
	#define VK_PAUSE      0x13
	#define VK_CAPITAL    0x14
	#define VK_ESCAPE     0x1B
	#define VK_SPACE      0x20
	#define VK_PRIOR      0x21
	#define VK_NEXT       0x22
	#define VK_END        0x23
	#define VK_HOME       0x24
	#define VK_LEFT       0x25
	#define VK_UP         0x26
	#define VK_RIGHT      0x27
	#define VK_DOWN       0x28
	#define VK_INSERT     0x2D
	#define VK_DELETE     0x2E
	#define VK_F1         0x70
	#define VK_F2         0x71
	#define VK_F3         0x72
	#define VK_F4         0x73
	#define VK_F5         0x74
	#define VK_F6         0x75
	#define VK_F7         0x76
	#define VK_F8         0x77
	#define VK_F9         0x78
	#define VK_F10        0x79
	#define VK_F11        0x7A
	#define VK_F12        0x7B
	#define VK_OEM_1      0xBA	// ';:' for US
	#define VK_OEM_PLUS   0xBB	// '+' any country
	#define VK_OEM_COMMA  0xBC	// ',' any country
	#define VK_OEM_MINUS  0xBD	// '-' any country
	#define VK_OEM_PERIOD 0xBE	// '.' any country
	#define VK_OEM_2      0xBF	// '/?' for US
	#define VK_OEM_3      0xC0	// '`~' for US
	#define VK_OEM_4      0xDB	// '[{' for US
	#define VK_OEM_5      0xDC	// '\|' for US
	#define VK_OEM_6      0xDD	// ']}' for US
	#define VK_OEM_7      0xDE	// '"' for US
#endif

// -------------------------------------------------------------------------
// === Begin verbatim port from src/Altirra/source/uikeyboard.cpp ===
// Lines below are a copy of uikeyboard.cpp with non-keymap code (debug
// printf, accelerator tables, registry I/O) removed.  Do not edit logic
// here without mirroring the change in uikeyboard.cpp.
// -------------------------------------------------------------------------

static bool g_ATUICustomKeyMapEnabled;
static vdfastvector<uint32> g_ATDefaultKeyMap;
static vdfastvector<uint32> g_ATCustomKeyMap;

static constexpr uint8 kATUIDefaultCharToScanCodeMappings[][2] {
	{ 'l', 0x00 }, { 'L', 0x40 },
	{ 'j', 0x01 }, { 'J', 0x41 },
	{ ';', 0x02 }, { ':', 0x42 },
	{ 'k', 0x05 }, { 'K', 0x45 },
	{ '+', 0x06 }, { '\\', 0x46 },
	{ '*', 0x07 }, { '^', 0x47 },
	{ 'o', 0x08 }, { 'O', 0x48 },
	{ 'p', 0x0A }, { 'P', 0x4A },
	{ 'u', 0x0B }, { 'U', 0x4B },
	{ 'i', 0x0D }, { 'I', 0x4D },
	{ '-', 0x0E }, { '_', 0x4E },
	{ '=', 0x0F }, { '|', 0x4F },
	{ 'v', 0x10 }, { 'V', 0x50 },
	{ 'c', 0x12 }, { 'C', 0x52 },
	{ 'b', 0x15 }, { 'B', 0x55 },
	{ 'x', 0x16 }, { 'X', 0x56 },
	{ 'z', 0x17 }, { 'Z', 0x57 },
	{ '4', 0x18 }, { '$', 0x58 },
	{ '3', 0x1A }, { '#', 0x5A },
	{ '6', 0x1B }, { '&', 0x5B },
	{ '5', 0x1D }, { '%', 0x5D },
	{ '2', 0x1E }, { '"', 0x5E },
	{ '1', 0x1F }, { '!', 0x5F },
	{ ',', 0x20 }, { '[', 0x60 },
	{ ' ', 0x21 },
	{ '.', 0x22 }, { ']', 0x62 },
	{ 'n', 0x23 }, { 'N', 0x63 },
	{ 'm', 0x25 }, { 'M', 0x65 },
	{ '/', 0x26 }, { '?', 0x66 },
	{ 'r', 0x28 }, { 'R', 0x68 },
	{ 'e', 0x2A }, { 'E', 0x6A },
	{ 'y', 0x2B }, { 'Y', 0x6B },
	{ 't', 0x2D }, { 'T', 0x6D },
	{ 'w', 0x2E }, { 'W', 0x6E },
	{ 'q', 0x2F }, { 'Q', 0x6F },
	{ '9', 0x30 }, { '(', 0x70 },
	{ '0', 0x32 }, { ')', 0x72 },
	{ '7', 0x33 }, { '\'', 0x73 },
	{ '8', 0x35 }, { '@', 0x75 },
	{ '<', 0x36 },
	{ '>', 0x37 },
	{ 'f', 0x38 }, { 'F', 0x78 },
	{ 'h', 0x39 }, { 'H', 0x79 },
	{ 'd', 0x3A }, { 'D', 0x7A },
	{ 'g', 0x3D }, { 'G', 0x7D },
	{ 's', 0x3E }, { 'S', 0x7E },
	{ 'a', 0x3F }, { 'A', 0x7F },
	{ '`', 0x27 }, { '~', 0x67 },

	{ 0x00E1, 0xA0 },	// $00: latin small letter A with acute
	{ 0x00F9, 0xBF },	// $01: latin small letter U with grave
	{ 0x00D1, 0x95 },	// $02: latin capital letter N with tilde
	{ 0x00C9, 0x92 },	// $03: latin capital letter E with acute
	{ 0x00E7, 0xBA },	// $04: latin small letter C with cedilla
	{ 0x00F4, 0xAA },	// $05: latin small letter O with circumflex
	{ 0x00F2, 0xB8 },	// $06: latin small letter O with grave
	{ 0x00EC, 0xBD },	// $07: latin small letter I with grave
	{ 0x00A3, 0xB9 },	// $08: pound sign
	{ 0x00EF, 0x8D },	// $09: latin small letter I with diaeresis
	{ 0x00FC, 0x81 },	// $0A: latin small letter U with diaeresis
	{ 0x00E4, 0x85 },	// $0B: latin small letter A with diaeresis
	{ 0x00D6, 0x80 },	// $0C: latin capital letter O with diaeresis
	{ 0x00FA, 0xA5 },	// $0D: latin small letter U with acute
	{ 0x00F3, 0xA3 },	// $0E: latin small letter O with acute
	{ 0x00F6, 0x88 },	// $0F: latin small letter O with diaeresis
	{ 0x00DC, 0x8A },	// $10: latin capital letter U with diaeresis
	{ 0x00E2, 0xAF },	// $11: latin small letter A with circumflex
	{ 0x00FB, 0xA8 },	// $12: latin small letter U with circumflex
	{ 0x00EE, 0xBE },	// $13: latin small letter I with circumflex
	{ 0x00E9, 0xAD },	// $14: latin small letter E with acute
	{ 0x00E8, 0x8B },	// $15: latin small letter E with grave
	{ 0x00F1, 0x90 },	// $16: latin small letter N with tilde
	{ 0x00EA, 0xAE },	// $17: latin small letter E with circumflex
	{ 0x00E5, 0x96 },	// $18: latin small letter A with ring above
	{ 0x00E0, 0xAB },	// $19: latin small letter A with grave
	{ 0x00C5, 0x97 },	// $1A: latin capital letter A with ring above

	{ 0x00A1, 0xA2 },	// $60: inverted exclamation mark
	{ 0x00C4, 0x82 },	// $7B: latin capital letter A with diaeresis
};

struct ATUIDefaultCharMappings {
	uint8 mCharToScanCode[256];
	uint32 mCharKeyMappings[vdcountof(kATUIDefaultCharToScanCodeMappings)];

	static constexpr uint8 kInvalidScanCode = 0xFF;

	constexpr ATUIDefaultCharMappings()
		: mCharToScanCode{}
		, mCharKeyMappings{}
	{
		for(auto& v : mCharToScanCode)
			v = kInvalidScanCode;

		for(size_t i = 0; i < vdcountof(kATUIDefaultCharToScanCodeMappings); ++i) {
			const auto& mapping = kATUIDefaultCharToScanCodeMappings[i];

			mCharToScanCode[mapping[0]] = mapping[1];
			mCharKeyMappings[i] = ((uint32)mapping[0] << 9) + mapping[1] + kATUIKeyboardMappingModifier_Cooked;
		}
	}
};

constexpr ATUIDefaultCharMappings kATUIDefaultCharMappings;

void ATUIAddDefaultCharMappings(vdfastvector<uint32>& dst) {
	dst.insert(dst.end(), std::begin(kATUIDefaultCharMappings.mCharKeyMappings), std::end(kATUIDefaultCharMappings.mCharKeyMappings));
}

const vdfastvector<uint32>& ATUIGetCurrentKeyMap() {
	return g_ATUICustomKeyMapEnabled ? g_ATCustomKeyMap : g_ATDefaultKeyMap;
}

bool ATUIGetScanCodeForKeyInput(uint32 keyInputCode, uint32& ch) {
	const auto& keyMap = ATUIGetCurrentKeyMap();

	auto it = std::lower_bound(keyMap.begin(), keyMap.end(), keyInputCode);
	if (it == keyMap.end() || (*it & 0xFFFFFE00) != keyInputCode)
		return false;

	ch = (*it & 0x1FF);
	return true;
}

bool ATUIGetScanCodeForCharacter32(uint32 c32, uint32& ch) {
	return c32 < 0x10000 && ATUIGetScanCodeForKeyInput(ATUIPackKeyboardMapping(0, c32, kATUIKeyboardMappingModifier_Cooked), ch);
}

bool ATUIGetDefaultScanCodeForCharacter(uint32 c32, uint8& ch) {
	if (c32 < 0x100) {
		const uint8 scanCode = kATUIDefaultCharMappings.mCharToScanCode[c32];

		if (scanCode != kATUIDefaultCharMappings.kInvalidScanCode) {
			ch = scanCode;
			return true;
		}
	}

	switch(c32) {
		case 0x2665: ch = 0xA0; return true;	// Ctrl+,  heart
		case 0x251C: ch = 0xBF; return true;	// Ctrl+A  vertical tee right
		case 0x2595: ch = 0x95; return true;	// Ctrl+B  vertical bar right
		case 0x2518: ch = 0x92; return true;	// Ctrl+C  top-left elbow
		case 0x2524: ch = 0xBA; return true;	// Ctrl+D  vertical tee left
		case 0x2510: ch = 0xAA; return true;	// Ctrl+E  bottom-left elbow
		case 0x2571: ch = 0xB8; return true;	// Ctrl+F  forward diagonal
		case 0x2572: ch = 0xBD; return true;	// Ctrl+G  backwards diagonal
		case 0x25E2: ch = 0xB9; return true;	// Ctrl+H  lower right filled triangle
		case 0x2597: ch = 0x8D; return true;	// Ctrl+I  lower right quadrant
		case 0x25E3: ch = 0x81; return true;	// Ctrl+J  lower left filled triangle
		case 0x259D: ch = 0x85; return true;	// Ctrl+K  quadrant upper right
		case 0x2598: ch = 0x80; return true;	// Ctrl+L  quadrant upper left
		case 0x2594: ch = 0xA5; return true;	// Ctrl+M  top quarter
		case 0x2582: ch = 0xA3; return true;	// Ctrl+N  bottom quarter
		case 0x2596: ch = 0x88; return true;	// Ctrl+O  lower left quadrant
		case 0x2663: ch = 0x8A; return true;	// Ctrl+P  club
		case 0x250C: ch = 0xAF; return true;	// Ctrl+Q  lower-right elbow
		case 0x2500: ch = 0xA8; return true;	// Ctrl+R  horizontal bar
		case 0x253C: ch = 0xBE; return true;	// Ctrl+S  four-way
		case 0x2022: ch = 0xAD; return true;	// Ctrl+T  filled circle
		case 0x2584: ch = 0x8B; return true;	// Ctrl+U  lower half
		case 0x258E: ch = 0x90; return true;	// Ctrl+V  left quarter
		case 0x252C: ch = 0xAE; return true;	// Ctrl+W  horizontal tee down
		case 0x2534: ch = 0x96; return true;	// Ctrl+X  horizontal tee up
		case 0x258C: ch = 0xAB; return true;	// Ctrl+Y  left side
		case 0x2514: ch = 0x97; return true;	// Ctrl+Z  top-right elbow
		case 0x241B: ch = 0x1C; return true;	// Esc     escape
		case 0x2191: ch = 0x8E; return true;	// up arrow
		case 0x2193: ch = 0x8F; return true;	// down arrow
		case 0x2190: ch = 0x86; return true;	// left arrow
		case 0x2192: ch = 0x87; return true;	// right arrow
		case 0x2666: ch = 0x61; return true;	// Ctrl+.  black diamond suit
		case 0x2660: ch = 0x82; return true;	// Ctrl+;  spade
		case 0x21B0: ch = 0x76; return true;	// Clear   curved arrow up-left
		case 0x25C0: ch = 0x34; return true;	// Back    tall left arrow
		case 0x25B6: ch = 0x2C; return true;	// Tab     tall right arrow
	}

	return false;
}

namespace {
	enum : uint32 {
		kShift = kATUIKeyboardMappingModifier_Shift,
		kCtrl = kATUIKeyboardMappingModifier_Ctrl,
		kAlt = kATUIKeyboardMappingModifier_Alt,
		kExtended = kATUIKeyboardMappingModifier_Extended
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

static const uint32 g_ATDefaultVKeyMap[]={
	VKEYMAP_CSALL(VK_TAB,		0x2C),
	VKEYMAP_CSALL(VK_BACK,		0x34),
	VKEYMAP_CSALL(VK_RETURN,	0x0C),
	VKEYMAP(VK_RETURN, kExtended, 0x0C),
	VKEYMAP(VK_RETURN, kExtended + kShift, 0x4C),
	VKEYMAP(VK_RETURN, kExtended + kCtrl, 0x8C),
	VKEYMAP(VK_RETURN, kExtended + kCtrl + kShift, 0xCC),
	VKEYMAP_CSALL(VK_ESCAPE,	0x1C),
	VKEYMAP_CSALL(VK_END,		0x27),
	VKEYMAP_CSXOR(VK_F6,		0x11),
	VKEYMAP(VK_OEM_1, kCtrl,	0x82),
	VKEYMAP(VK_OEM_PLUS, kCtrl,			0x86),
	VKEYMAP(VK_OEM_4, kCtrl,			0xE0),
	VKEYMAP(VK_OEM_4, kCtrl + kShift,	0xE0),
	VKEYMAP(VK_OEM_5, kCtrl,			0x9C),
	VKEYMAP(VK_OEM_5, kCtrl + kShift,	0xDC),
	VKEYMAP(VK_OEM_6, kCtrl,			0xE2),
	VKEYMAP(VK_OEM_6, kCtrl + kShift,	0xE2),
	VKEYMAP(VK_OEM_COMMA, kCtrl,		0xA0),
	VKEYMAP(VK_OEM_PERIOD, kCtrl,		0xA2),
	VKEYMAP(VK_OEM_2, kCtrl,			0xA6),
	VKEYMAP(VK_OEM_2, kCtrl + kShift,	0xE6),
	VKEYMAP(VK_HOME,	0,				0x76),
	VKEYMAP(VK_HOME,	kShift,			0x76),
	VKEYMAP(VK_HOME,	kCtrl,			0xB6),
	VKEYMAP(VK_HOME,	kCtrl + kShift,	0xF6),
	VKEYMAP(VK_DELETE,	0,				0xB4),
	VKEYMAP(VK_DELETE,	kShift,			0x74),
	VKEYMAP(VK_DELETE,	kCtrl,			0xF4),
	VKEYMAP(VK_DELETE,	kCtrl + kShift,	0xF4),
	VKEYMAP(VK_INSERT,	0,				0xB7),
	VKEYMAP(VK_INSERT,	kShift,			0x77),
	VKEYMAP(VK_INSERT,	kCtrl,			0xF7),
	VKEYMAP(VK_INSERT,	kCtrl + kShift,	0xF7),
	VKEYMAP(VK_SPACE,	kShift,			0x61),
	VKEYMAP(VK_SPACE,	kCtrl,			0xA1),
	VKEYMAP(VK_SPACE,	kCtrl + kShift,	0xE1),

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

	VKEYMAP('0',	kCtrl + kShift + kAlt,	0xF2),	// Ctrl+Shift+Alt+0 (Win10+ workaround)
};

static const uint32 g_ATRawVKeyMap[]={
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
	VKEYMAP_CSALL(VK_SPACE,		0x21),
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

	VKEYMAP('0',	kCtrl + kShift + kAlt,	0xF2),
};

static const uint32 g_ATDefaultVKeyMapCommonSSO[]={
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

static const uint32 g_ATDefaultVKeyMapCommonBreak[]={
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

static const uint32 g_ATDefaultVKeyMapFKey[]={
	VKEYMAP_CSXOR(VK_F1, 0x03),
	VKEYMAP_CSXOR(VK_F2, 0x04),
	VKEYMAP_CSXOR(VK_F3, 0x13),
	VKEYMAP_CSXOR(VK_F4, 0x14),
};

static void ATUIRegisterVirtualKeyMappings(vdfastvector<uint32>& dst, const uint32 *mappings, uint32 n) {
	while(n--) {
		uint32 mapping = *mappings++;

		switch((mapping >> 9) & 0xFFFF) {
			case VK_INSERT:
			case VK_DELETE:
			case VK_HOME:
			case VK_END:
			case VK_NEXT:
			case VK_PRIOR:
			case VK_LEFT:
			case VK_RIGHT:
			case VK_UP:
			case VK_DOWN:
				mapping |= kExtended;
				break;
		}

		dst.push_back(mapping);
	}
}

void ATUIGetDefaultKeyMap(const ATUIKeyboardOptions& options, vdfastvector<uint32>& mappings) {
	VDASSERT(options.mLayoutMode != ATUIKeyboardOptions::kLM_Custom);

	mappings.clear();

	switch(options.mLayoutMode) {
		case ATUIKeyboardOptions::kLM_Natural:
		default:
			ATUIRegisterVirtualKeyMappings(mappings, g_ATDefaultVKeyMap, vdcountof(g_ATDefaultVKeyMap));
			break;

		case ATUIKeyboardOptions::kLM_Raw:
			ATUIRegisterVirtualKeyMappings(mappings, g_ATRawVKeyMap, vdcountof(g_ATRawVKeyMap));
			break;
	}

	ATUIRegisterVirtualKeyMappings(mappings, g_ATDefaultVKeyMapCommonBreak, vdcountof(g_ATDefaultVKeyMapCommonBreak));

	if (options.mbEnableFunctionKeys)
		ATUIRegisterVirtualKeyMappings(mappings, g_ATDefaultVKeyMapFKey, vdcountof(g_ATDefaultVKeyMapFKey));
	else
		ATUIRegisterVirtualKeyMappings(mappings, g_ATDefaultVKeyMapCommonSSO, vdcountof(g_ATDefaultVKeyMapCommonSSO));

	static const uint8 kArrowVKs[4]={ VK_UP, VK_DOWN, VK_LEFT, VK_RIGHT };
	static const uint8 kArrowKCs[4]={ 0x0E, 0x0F, 0x06, 0x07 };

	static const uint8 kCtrlShiftMasks[][4]={
		//              N     S     C     C+S
		/* invert */  { 0x80, 0xC0, 0x00, 0x40 },
		/* auto */    { 0x80, 0x40, 0x80, 0xC0 },
		/* default */ { 0x00, 0x40, 0x80, 0xC0 },
	};

	VDASSERTCT(sizeof(kCtrlShiftMasks)/sizeof(kCtrlShiftMasks[0]) == ATUIKeyboardOptions::kAKMCount);

	const uint8 *csmasks = kCtrlShiftMasks[options.mArrowKeyMode];

	for(int i=0; i<4; ++i) {
		const uint32 baseVK = kArrowVKs[i];
		const uint8 kbcode = kArrowKCs[i];

		for(int j=0; j<4; ++j) {
			uint8 kbcode2 = kbcode | csmasks[j];

			mappings.push_back(ATUIPackKeyboardMapping(kbcode2, baseVK, (j << 25) + kATUIKeyboardMappingModifier_Extended));
			mappings.push_back(ATUIPackKeyboardMapping(kbcode2, baseVK, (j << 25) + kATUIKeyboardMappingModifier_Alt + kATUIKeyboardMappingModifier_Extended));
		}
	}

	ATUIAddDefaultCharMappings(mappings);

	mappings.erase(
		std::remove_if(
			mappings.begin(),
			mappings.end(),
			[](uint32 mapping) -> bool { return !ATIsValidScanCode((uint8)mapping); }),
		mappings.end());

	std::sort(mappings.begin(), mappings.end());
}

void ATUIInitVirtualKeyMap(const ATUIKeyboardOptions& options) {
	if (options.mLayoutMode == ATUIKeyboardOptions::kLM_Custom) {
		std::sort(g_ATCustomKeyMap.begin(), g_ATCustomKeyMap.end());
		g_ATUICustomKeyMapEnabled = true;
	} else {
		g_ATDefaultKeyMap.clear();
		g_ATDefaultKeyMap.reserve(2048);
		ATUIGetDefaultKeyMap(options, g_ATDefaultKeyMap);
		g_ATUICustomKeyMapEnabled = false;
	}
}

bool ATUIGetScanCodeForVirtualKey(uint32 virtKey, bool alt, bool ctrl, bool shift, bool extended, uint32& scanCode) {
	if (virtKey >= 0x10000)
		return false;

	uint32 baseCode = virtKey << 9;

	if (alt)
		baseCode += kAlt;

	if (ctrl)
		baseCode += kCtrl;

	if (shift)
		baseCode += kShift;

	if (extended)
		baseCode += kExtended;

	return ATUIGetScanCodeForKeyInput(baseCode, scanCode);
}

void ATUIGetCustomKeyMap(vdfastvector<uint32>& mappings) {
	mappings = g_ATCustomKeyMap;
}

void ATUISetCustomKeyMap(const uint32 *mappings, size_t n) {
	g_ATCustomKeyMap.clear();
	g_ATCustomKeyMap.assign(mappings, mappings + n);

	std::sort(g_ATCustomKeyMap.begin(), g_ATCustomKeyMap.end());
}

// SDL3-specific accessor used by input_sdl3.cpp to read the custom map
// without copying.  Not present in upstream uikeyboard.cpp.
const vdfastvector<uint32>& ATUIGetCustomKeyMapRef() {
	return g_ATCustomKeyMap;
}

bool ATIsValidScanCode(uint32 c) {
	if (c >= 0x100)
		return c <= kATUIKeyScanCodeLast;

	switch(c & 0x3F) {
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
