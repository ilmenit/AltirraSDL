//	AltirraSDL - Paste Text (Edit > Paste Text, MCP type_text).
//
//	Port of Windows main.cpp Paste(): turns text into Atari key presses
//	queued on POKEY, including the {token} syntax for special keys. UI-free,
//	so both the SDL3 frontend and the headless AltirraBridgeServer build it.
//	(Moved unchanged from ui/menus/ui_menus.cpp.)

#include <stdafx.h>
#include <vd2/system/vdtypes.h>
#include <vd2/system/vdstl.h>
#include <at/ataudio/pokey.h>
#include "simulator.h"
#include "uikeyboard.h"

#include <cwchar>

extern ATSimulator g_sim;

// =========================================================================
// Paste Text — port of Windows main.cpp Paste() function
// =========================================================================

// Scancode name table for {token} parsing in paste text — matches Windows
// main.cpp g_scancodeNameTable exactly.
static const struct { uint8 scancode; const wchar_t *name; } kScancodeNames[] = {
	{ 0x2C, L"tab" },
	{ 0x34, L"back" },
	{ 0x34, L"backspace" },
	{ 0x34, L"bksp" },
	{ 0x0C, L"enter" },
	{ 0x0C, L"return" },
	{ 0x1C, L"esc" },
	{ 0x1C, L"escape" },
	{ 0x27, L"fuji" },
	{ 0x27, L"inv" },
	{ 0x27, L"invert" },
	{ 0x11, L"help" },
	{ 0x76, L"clear" },
	{ 0xB4, L"del" },
	{ 0xB4, L"delete" },
	{ 0xB7, L"ins" },
	{ 0xB7, L"insert" },
	{ 0x3C, L"caps" },
	{ 0x86, L"left" },
	{ 0x87, L"right" },
	{ 0x8E, L"up" },
	{ 0x8F, L"down" },
};

// Case-insensitive wchar_t comparison for scancode name lookup.
static bool WcsiEqual(const wchar_t *a, size_t alen, const wchar_t *b) {
	size_t blen = wcslen(b);
	if (alen != blen) return false;
	for (size_t i = 0; i < alen; ++i) {
		wchar_t ca = a[i], cb = b[i];
		if (ca >= L'A' && ca <= L'Z') ca += L'a' - L'A';
		if (cb >= L'A' && cb <= L'Z') cb += L'a' - L'A';
		if (ca != cb) return false;
	}
	return true;
}

void ATUIPasteTextDirect(const wchar_t *text, size_t textLen) {
	auto& pokey = g_sim.GetPokey();
	const wchar_t *s = text;
	size_t len = textLen;
	vdfastvector<wchar_t> pasteChars;

	while (len--) {
		wchar_t c = *s++;
		if (!c) continue;

		int repeat = 1;
		switch (c) {
			case L'\u200B': case L'\u200C': case L'\u200D':
			case L'\u200E': case L'\u200F': continue;
			case L'\u2010': case L'\u2011': case L'\u2012':
			case L'\u2013': case L'\u2014': case L'\u2015': c = L'-'; break;
			case L'\u2018': case L'\u2019': c = L'\''; break;
			case L'\u201C': case L'\u201D': c = L'"'; break;
			case L'\u2026': c = L'.'; repeat = 3; break;
			case L'\uFEFF': continue;
		}
		while (repeat--) pasteChars.push_back(c);
	}

	pasteChars.push_back(0);
	wchar_t skipLT = 0;
	uint8 scancodeModifier = 0;
	const wchar_t *t = pasteChars.data();

	while (wchar_t c = *t++) {
		if (c == skipLT) { skipLT = 0; continue; }
		skipLT = 0;

		// {token} parsing — matches Windows main.cpp Paste() exactly.
		// Supports: {enter}, {tab}, {esc}, {left}, {right}, {up}, {down},
		// {fuji}, {help}, {clear}, {del}, {ins}, {caps}, {back},
		// and modifier prefixes: {shift-X}, {ctrl-X}, {+X}, {^X}.
		if (c == L'{') {
			const wchar_t *start = t;
			while (*t && *t != L'}')
				++t;
			if (*t != L'}')
				break;

			const wchar_t *nameStart = start;
			size_t nameLen = (size_t)(t - start);
			++t; // skip '}'

			// Parse modifier prefixes
			while (nameLen > 0) {
				if (nameStart[0] == L'+') {
					scancodeModifier |= 0x40;
					++nameStart; --nameLen;
					continue;
				}
				if (nameLen >= 6 && (WcsiEqual(nameStart, 6, L"shift-") || WcsiEqual(nameStart, 6, L"shift+"))) {
					scancodeModifier |= 0x40;
					nameStart += 6; nameLen -= 6;
					continue;
				}
				if (nameStart[0] == L'^') {
					scancodeModifier |= 0x80;
					++nameStart; --nameLen;
					continue;
				}
				if (nameLen >= 5 && (WcsiEqual(nameStart, 5, L"ctrl-") || WcsiEqual(nameStart, 5, L"ctrl+"))) {
					scancodeModifier |= 0x80;
					nameStart += 5; nameLen -= 5;
					continue;
				}
				break;
			}

			// Look up scancode name
			bool found = false;
			for (const auto& entry : kScancodeNames) {
				if (WcsiEqual(nameStart, nameLen, entry.name)) {
					uint8 scancode = entry.scancode;
					if (scancodeModifier)
						scancode = (scancode & 0x3F) | scancodeModifier;
					pokey.PushKey(scancode, false, true, false, true);
					found = true;
					break;
				}
			}

			scancodeModifier = 0;
			continue;
		}

		const uint8 kInvalidScancode = 0xFF;
		uint8 scancode = kInvalidScancode;

		switch (c) {
			case L'\r': case L'\n':
				skipLT = c ^ (L'\r' ^ L'\n');
				scancode = 0x0C; break;
			case L'\t': scancode = 0x2C; break;
			case L'\x001B': scancode = 0x1C; break;
			default:
				if (ATUIGetDefaultScanCodeForCharacter(c, scancode)) {
					// For control characters, inject an ESC prefix so they
					// display as visible characters — matches Windows exactly.
					switch (scancode) {
						case 0x1C: case 0x8E: case 0x8F: case 0x86:
						case 0x87: case 0x82: case 0x76: case 0x34: case 0x2C:
							pokey.PushKey(0x1C, false, true, false, true);
							break;
					}
				} else scancode = kInvalidScancode;
				break;
		}

		if (scancode != kInvalidScancode)
			pokey.PushKey(scancode | scancodeModifier, false, true, false, true);

		scancodeModifier = 0;
	}
}
