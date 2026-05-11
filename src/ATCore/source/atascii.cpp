//	Altirra - Atari 800/800XL/5200 emulator
//	Core library - generic bus signal implementation
//	Copyright (C) 2023 Avery Lee
//
//	This program is free software; you can redistribute it and/or modify
//	it under the terms of the GNU General Public License as published by
//	the Free Software Foundation; either version 2 of the License, or
//	(at your option) any later version.
//
//	This program is distributed in the hope that it will be useful,
//	but WITHOUT ANY WARRANTY; without even the implied warranty of
//	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//	GNU General Public License for more details.
//
//	You should have received a copy of the GNU General Public License along
//	with this program. If not, see <http://www.gnu.org/licenses/>.
//
//	As a special exception, this library can also be redistributed and/or
//	modified under an alternate license. See COPYING.RMT in the same source
//	archive for details.

#include <stdafx.h>
#include <at/atcore/atascii.h>

namespace ATATASCIIAuxTables {
	static constexpr wchar_t kIntlLowTab[] {
		0x00E1,		// $00: latin small letter A with acute
		0x00F9,		// $01: latin small letter U with grave
		0x00D1,		// $02: latin capital letter N with tilde
		0x00C9,		// $03: latin capital letter E with acute
		0x00E7,		// $04: latin small letter C with cedilla
		0x00F4,		// $05: latin small letter O with circumflex
		0x00F2,		// $06: latin small letter O with grave
		0x00EC,		// $07: latin small letter I with grave
		0x00A3,		// $08: pound sign
		0x00EF,		// $09: latin small letter I with diaeresis
		0x00FC,		// $0A: latin small letter U with diaeresis
		0x00E4,		// $0B: latin small letter A with diaeresis
		0x00D6,		// $0C: latin capital letter O with diaeresis
		0x00FA,		// $0D: latin small letter U with acute
		0x00F3,		// $0E: latin small letter O with acute
		0x00F6,		// $0F: latin small letter O with diaeresis
		0x00DC,		// $10: latin capital letter U with diaeresis
		0x00E2,		// $11: latin small letter A with circumflex
		0x00FB,		// $12: latin small letter U with circumflex
		0x00EE,		// $13: latin small letter I with circumflex
		0x00E9,		// $14: latin small letter E with acute
		0x00E8,		// $15: latin small letter E with grave
		0x00F1,		// $16: latin small letter N with tilde
		0x00EA,		// $17: latin small letter E with circumflex
		0x00E5,		// $18: latin small letter A with ring above
		0x00E0,		// $19: latin small letter A with grave
		0x00C5,		// $1A: latin capital letter A with ring above
	};
	static_assert(vdcountof(kIntlLowTab) == 27);

	static constexpr uint16 kUnicodeLowTable[]={
		0x2665,	// heart
		0x251C,	// vertical tee right
		0x2595,	// vertical bar right
		0x2518,	// top-left elbow
		0x2524,	// vertical tee left
		0x2510,	// bottom-left elbow
		0x2571,	// forward diagonal
		0x2572,	// backwards diagonal
		0x25E2,	// lower right filled triangle
		0x2597,	// lower right quadrant
		0x25E3,	// lower left filled triangle
		0x259D,	// quadrant upper right
		0x2598,	// quadrant upper left
		0x2594,	// top quarter
		0x2582,	// bottom quarter
		0x2596,	// lower left quadrant
					
		0x2663,	// club
		0x250C,	// lower-right elbow
		0x2500,	// horizontal bar
		0x253C,	// four-way
		0x2022,	// filled circle
		0x2584,	// lower half
		0x258E,	// left quarter
		0x252C,	// horizontal tee down
		0x2534,	// horizontal tee up
		0x258C,	// left side
		0x2514,	// top-right elbow
		0x241B,	// escape
		0x2191,	// up arrow
		0x2193,	// down arrow
		0x2190,	// left arrow
		0x2192,	// right arrow
	};

	static constexpr uint16 kUnicodeHighTable[]={
		0x2660,	// spade
		'|',	// vertical bar (leave this alone so as to not invite font issues)
		0x21B0,	// curved arrow up-left
		0x25C0,	// tall left arrow
		0x25B6,	// tall right arrow
	};

	static constexpr VDCxPair<uint16, uint8> kExtraUnicodeMappings[] {
		{ 0x2010, (uint8)'-' },	// hyphen
		{ 0x2011, (uint8)'-' },	// non-breaking hyphen
		{ 0x2012, (uint8)'-' },	// figure dash
		{ 0x2013, (uint8)'-' },	// en dash
		{ 0x2014, (uint8)'-' },	// em dash
		{ 0x2015, (uint8)'-' },	// horizontal bar

		{ 0x2018, (uint8)'\'' },	// left single quotation mark
		{ 0x2019, (uint8)'\'' },	// right single quotation mark

		{ 0x201C, (uint8)'"' },	// left double quotation mark
		{ 0x201D, (uint8)'"' },	// right double quotation mark
	};
}

constexpr ATATASCIITables kATATASCIITables = []() constexpr -> ATATASCIITables {
	using namespace ATATASCIIAuxTables;

	ATATASCIITables tbl {};

	// basic
	for(int i=0; i<32; ++i)
		tbl.mATASCIIToUnicode[0][i] = kUnicodeLowTable[i];

	for(int i=32; i<123; ++i)
		tbl.mATASCIIToUnicode[0][i] = (uint8)i;

	for(int i=0; i<5; ++i)
		tbl.mATASCIIToUnicode[0][123 + i] = kUnicodeHighTable[i];

	for(int i=0; i<27; ++i)
		tbl.mATASCIIToUnicode[1][i] = kIntlLowTab[i];

	for(int i=27; i<32; ++i)
		tbl.mATASCIIToUnicode[1][i] = kUnicodeLowTable[i];

	for(int i=32; i<128; ++i)
		tbl.mATASCIIToUnicode[1][i] = tbl.mATASCIIToUnicode[0][i];

	tbl.mATASCIIToUnicode[0][96] = 0x2666;	// U+2666 black diamond suit

	tbl.mATASCIIToUnicode[1][96] = 0xA1;	// $60: inverted exclamation mark
	tbl.mATASCIIToUnicode[1][123] = 0xC4;	// $7B: latin capital letter A with diaeresis

	using EncodeEntry = VDCxPair<uint16, uint8>;
	EncodeEntry e[167] {};

	int n = 0;

	for(int i=0; i<128; ++i) {
		e[n++] = EncodeEntry { tbl.mATASCIIToUnicode[0][i], (uint8)i };

		if (tbl.mATASCIIToUnicode[0][i] != tbl.mATASCIIToUnicode[1][i])
			e[n++] = EncodeEntry { tbl.mATASCIIToUnicode[1][i], (uint8)i };
	}

	for(int i=0; i<(int)vdcountof(kExtraUnicodeMappings); ++i)
		e[n++] = kExtraUnicodeMappings[i];

	if (n < 167)
		throw n;

	if (n > 167)
		throw n;

	tbl.mUnicodeToATASCII.init(e);

	return tbl;
}();

////////////////////////////////////////////////////////////////////////////////

static_assert(ATConvertInternalToATASCII(0x00) == 0x20);
static_assert(ATConvertInternalToATASCII(0x21) == 0x41);
static_assert(ATConvertInternalToATASCII(0x45) == 0x05);
static_assert(ATConvertInternalToATASCII(0x7F) == 0x7F);
static_assert(ATConvertInternalToATASCII(0x80) == 0xA0);
static_assert(ATConvertInternalToATASCII(0xA1) == 0xC1);
static_assert(ATConvertInternalToATASCII(0xC5) == 0x85);
static_assert(ATConvertInternalToATASCII(0xFF) == 0xFF);
