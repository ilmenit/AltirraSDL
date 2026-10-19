// bridge_screentext.cpp — portable ANTIC text-mode screen reader.
//
// ReadTextLine() is a verbatim port of the static ReadText() that used
// to live in source/ui/dialogs/ui_textselection.cpp (mouse click-drag
// text selection / copy). It is moved here, unchanged apart from
// threading an explicit ATSimulator& through instead of reaching for
// the global g_sim, so it can be shared between the GUI bridge target
// (AltirraSDL --bridge) and the headless target (AltirraBridgeServer),
// which does not link Dear ImGui or SDL3.
//
// ReadScreenText() is new: a stateless walk over the whole screen that
// does not touch ATTextSelectionState (that state is GUI-only and
// drives the live mouse-selection highlight overlay — reusing it here
// would have side effects on a supposedly read-only query).

#include <stdafx.h>

#include "bridge_screentext.h"

#include <at/atcore/atascii.h>
#include <vd2/system/text.h>
#include "simulator.h"
#include "antic.h"
#include "memorymanager.h"

namespace ATBridge {

int ReadTextLine(ATSimulator& sim, uint8 *dst, int yc, int startChar, int numChars, bool& intl) {
	ATAnticEmulator& antic = sim.GetAntic();
	const ATAnticEmulator::DLHistoryEntry *dlhist = antic.GetDLHistory();
	const ATAnticEmulator::DLHistoryEntry& dle = dlhist[yc];

	intl = false;

	if (!dle.mbValid)
		return 0;

	switch (dle.mControl & 15) {
		case 2: case 3: case 6: case 7: break;
		default: return 0;
	}

	// Compute width
	static const int kWidthLookup[2][4] = {
		{ 0, 16, 20, 24 },	// no horizontal scrolling
		{ 0, 20, 24, 24 },	// horizontal scrolling
	};

	int len = (dle.mControl & 4 ? 1 : 2)
		* kWidthLookup[(dle.mControl & 0x10) != 0][dle.mDMACTL & 3];

	// Clip
	if (numChars <= 0 || startChar >= len)
		return 0;
	if (startChar < 0) {
		numChars += startChar;
		startChar = 0;
	}
	if (numChars > len - startChar)
		numChars = len - startChar;
	if (numChars <= 0)
		return 0;

	// Read raw bytes via ANTIC memory
	uint8 data[48];
	sim.GetMemoryManager()->DebugAnticReadMemory(data, dle.mPFAddress + startChar, numChars);

	// Internal-to-ATASCII conversion
	static const uint8 kInternalToATASCIIXorTab[4] = { 0x20, 0x60, 0x40, 0x00 };

	uint8 mask = (dle.mControl & 4) ? 0x3f : 0xff;
	uint8 xorval = (dle.mControl & 4) && (dle.mCHBASE & 1) ? 0x40 : 0x00;

	if (!(dle.mControl & 4) && dle.mCHBASE == (0xCC >> 1))
		intl = true;

	for (int i = 0; i < numChars; ++i) {
		uint8 c = data[i];
		c &= mask;
		c ^= xorval;
		c ^= kInternalToATASCIIXorTab[(c & 0x60) >> 5];
		*dst++ = c;
	}

	return numChars;
}

void ReadScreenText(ATSimulator& sim, vdvector<VDStringA>& outLines) {
	static const int kMaxLineChars = 48;	// matches ReadTextLine's internal data[48]

	ATAnticEmulator& antic = sim.GetAntic();
	const ATAnticEmulator::DLHistoryEntry *dlhist = antic.GetDLHistory();

	uint8 buf[kMaxLineChars];

	for (int yc = 8; yc <= 247; ++yc) {
		const ATAnticEmulator::DLHistoryEntry& dle = dlhist[yc];

		if (!dle.mbValid)
			continue;

		switch (dle.mControl & 15) {
			case 2: case 3: case 6: case 7: break;
			default: continue;
		}

		bool intl = false;
		int actual = ReadTextLine(sim, buf, yc, 0, kMaxLineChars, intl);
		if (!actual)
			continue;

		const auto& decodeTab = kATATASCIITables.mATASCIIToUnicode[intl];

		// Decode first, then trim — matches ATUITextCopy(ASCII): trimming
		// must run on the decoded bytes, not the raw ATASCII ones, so an
		// inverse-video space (cursor block, $A0) — which decodes to a
		// plain space — gets trimmed the same as a normal $20 space.
		for (int i = 0; i < actual; ++i) {
			uint16 wc = decodeTab[buf[i] & 0x7f];
			buf[i] = wc < 0x100 ? (uint8)wc : 0x20;
		}

		int base = 0;
		int end = actual;

		while (base < end && buf[base] == 0x20)
			++base;
		while (end > base && buf[end - 1] == 0x20)
			--end;

		if (base >= end)
			continue;	// blank line — skip, don't append an empty string

		VDStringW s;
		for (int i = base; i < end; ++i)
			s += (wchar_t)buf[i];

		outLines.push_back(VDTextWToU8(s));
	}
}

} // namespace ATBridge
