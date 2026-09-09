//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2008-2026 Avery Lee
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

#include <stdafx.h>
#include <vd2/system/binary.h>
#include <vd2/VDDisplay/display.h>
#include "debugdisplay.h"
#include "antic.h"
#include "gtia.h"
#include "gtiarenderer.h"
#include "gtiatables.h"
#include "memorymanager.h"

ATDebugDisplay::ATDebugDisplay()
	: mpMemory(NULL)
	, mpAntic(NULL)
	, mpGTIA(NULL)
	, mpDisplay(NULL)
	, mMode(kMode_AnticHistory)
	, mPaletteMode(PaletteMode::ColorTrace)
	, mDLAddrOverride(-1)
	, mPFAddrOverride(-1)

{
	static_assert(vdcountof(RenderState::mColorTable) == ATGTIA::kColorCount);

	ATInitGTIAPriorityTables(mPriTables);
}

ATDebugDisplay::~ATDebugDisplay() {
}

void ATDebugDisplay::Init(ATMemoryManager *memory, ATAnticEmulator *antic, ATGTIAEmulator *gtia, IVDVideoDisplay *display) {
	mpMemory = memory;
	mpAntic = antic;
	mpDisplay = display;
	mpGTIA = gtia;

	VDPixmapLayout layout = {0};

	// The wide display covers $20-$DF (192 color clocks), but only $22-DD is visible
	// in the display. We allocate a 208 color clock wide bitmap to allow room for
	// horizontal scrolling emulation.
	layout.data = 4;
	layout.w = 376;
	layout.h = 240;
	layout.pitch = 416;
	layout.format = nsVDPixmap::kPixFormat_Pal8;
	layout.palette = 0;

	mDisplayBuffer.init(layout);
	mDisplayBuffer.palette = mPalette;

	memset(mDisplayBuffer.base(), 0, mDisplayBuffer.size());
}

void ATDebugDisplay::Shutdown() {
	if (mpDisplay)
		mpDisplay->Reset();

	mpMemory = NULL;
	mpAntic = NULL;
	mpGTIA = NULL;
	mpDisplay = NULL;

	mDisplayBuffer.clear();
}

void ATDebugDisplay::Update() {
	ATGTIARegisterState gtiaState;
	ATAnticRegisterState anticState;

	mpAntic->GetRegisterState(anticState);
	mpGTIA->GetRegisterState(gtiaState);

	const ATAnticEmulator::DLHistoryEntry *const history = mpAntic->GetDLHistory() + 8;

	bool prevvscroll = false;
	int fonthibase = -1;
	int fontlobase = -1;

	uint8 pfbak;
	uint8 pf0;
	uint8 pf1;
	uint8 pf2;
	uint8 pf3;
	uint8 pm0;
	uint8 pm1;
	uint8 pm2;
	uint8 pm3;

	const ATGTIARegisterTrace& gtiaRegisterTrace = mpGTIA->GetRegisterTrace();

	if (mPaletteMode == PaletteMode::CurrentRegisters) {
		pfbak = gtiaState.mReg[0x1A];
		pf0 = gtiaState.mReg[0x16];
		pf1 = gtiaState.mReg[0x17];
		pf2 = gtiaState.mReg[0x18];
		pf3 = gtiaState.mReg[0x19];
		pm0 = gtiaState.mReg[0x12];
		pm1 = gtiaState.mReg[0x13];
		pm2 = gtiaState.mReg[0x14];
		pm3 = gtiaState.mReg[0x15];
	} else {
		pfbak = 0x00;
		pf0 = 0x04;
		pf1 = 0x08;
		pf2 = 0x0C;
		pf3 = 0x0F;
		pm0 = 0x18;
		pm1 = 0x58;
		pm2 = 0x78;
		pm3 = 0xB8;
	}

	const uint8 gractl = gtiaState.mReg[0x1D];

	memset(mDisplayBuffer.base(), pfbak, mDisplayBuffer.size());

	ATAnticEmulator::DLHistoryEntry hval = history[0];

	if (mDLAddrOverride >= 0)
		hval.mDLAddress = (uint16)mDLAddrOverride;

	hval.mDMACTL = anticState.mDMACTL;
	hval.mCHBASE = anticState.mCHBASE >> 1;

	if (mPFAddrOverride >= 0)
		hval.mPFAddress = (uint16)mPFAddrOverride;

	uint32 dlbase = hval.mDLAddress & 0xfc00;
	uint32 dloffset = hval.mDLAddress;
	bool dlEnabled = true;

	for(int y=0; y<240; ) {
		uint8 prior = gtiaState.mReg[0x1B];

		if (mPaletteMode == PaletteMode::ColorTrace) {
			const auto& lineState = gtiaRegisterTrace.mLineStates[y];
			pm0 = lineState.mColors[0];
			pm1 = lineState.mColors[1];
			pm2 = lineState.mColors[2];
			pm3 = lineState.mColors[3];
			pf0 = lineState.mColors[4];
			pf1 = lineState.mColors[5];
			pf2 = lineState.mColors[6];
			pf3 = lineState.mColors[7];
			pfbak = lineState.mColors[8];

			prior = lineState.mPRIOR;
		}

		if (dlEnabled) {
			if (mMode == kMode_AnticHistory) {
				const ATAnticEmulator::DLHistoryEntry& hvalsrc = history[y];

				if (!hvalsrc.mbValid) {
					++y;
					continue;
				}
			
				uint16 pfaddr = hval.mPFAddress;
				hval = hvalsrc;

				if (mPFAddrOverride >= 0)
					hval.mPFAddress = pfaddr;
			} else {
				hval.mDLAddress = dlbase + (dloffset & 0x3ff);
				hval.mControl = mpMemory->DebugAnticReadByte(dlbase + (dloffset++ & 0x3ff));

				uint8 cmode = hval.mControl & 0x0f;
				if (cmode) {
					if (cmode == 1 || (hval.mControl & 0x40)) {
						uint8 lo = mpMemory->DebugAnticReadByte(dlbase + (dloffset++ & 0x3ff));
						uint8 hi = mpMemory->DebugAnticReadByte(dlbase + (dloffset++ & 0x3ff));

						uint16 addr = (uint16)(lo + ((uint32)hi << 8));

						if (cmode == 1)
							hval.mDLAddress = addr;
						else if (mPFAddrOverride < 0)
							hval.mPFAddress = addr;
					}
				}
			}
		}

		const uint8 mode = hval.mControl & 15;

		// check for jump
		if (mode == 1) {
			if (hval.mControl & 0x40)
				dlEnabled = false;
		}

		// check if vertical scrolling is enabled
		const bool vscroll = mode >= 2 && (hval.mControl & 0x20);

		// compute initial starting and ending rows
		static const uint8 kModeEndTable[14]={
			7,9,7,15,7,15,7,3,3,1,0,1,0,0
		};

		uint8 row = 0;
		uint8 rowlast = mode ? kModeEndTable[mode - 2] : (hval.mControl >> 4) & 7;

		if (vscroll != prevvscroll) {
			if (vscroll)
				row = hval.mHVScroll >> 4;
			else
				rowlast = hval.mHVScroll >> 4;

			prevvscroll = vscroll;
		}

		// check for horizontal scrolling and compute effective widths
		const uint8 displayWidth = hval.mDMACTL & 3;
		const bool hscroll = (hval.mControl & 0x10) != 0;

		uint8 fetchWidth = displayWidth;

		if (mode < 2) {
			fetchWidth = 0;
		} else if (hscroll) {
			static const uint8 kHScrollFetchTable[4] = {0,2,3,3};

			fetchWidth = kHScrollFetchTable[fetchWidth];
		}

		// compute number of bytes to fetch
		static const uint8 kFetchModeTable[16]={
			0,0,2,2,2,2,1,1,0,0,1,1,1,2,2,2
		};

		const int fetchmode = kFetchModeTable[mode];
		const int rowbytes = mode >= 2 ? (6 + (fetchWidth << 1)) << fetchmode : 0;
		uint16 pfad = hval.mPFAddress;

		hval.mPFAddress = pfad + rowbytes;

		// fill line buffer
		if (fetchWidth) {
			uint32 overflow = (pfad & 0xfff) + rowbytes;
			if (overflow >= 0x1000) {
				// Playfield address would wrap -- do split read
				overflow -= 0x1000;
				mpMemory->DebugAnticReadMemory(mAnticLineBuffer, pfad, rowbytes - overflow);
				mpMemory->DebugAnticReadMemory(mAnticLineBuffer + (rowbytes - overflow), pfad & 0xf000, overflow);
			} else {
				// Unwrapped case
				mpMemory->DebugAnticReadMemory(mAnticLineBuffer, pfad, rowbytes);
			}

			// mask off bytes that go beyond the DMA stop
			static const uint8 kFetchStartTable[2][3]={
				{ 28, 20, 12 }, { 26, 18, 10 }
			};

			int fetchstart = kFetchStartTable[mode < 8][fetchWidth - 1];
			int fetchlimit = (106 - fetchstart) >> (3 - fetchmode);

			if (rowbytes > fetchlimit)
				memset(mAnticLineBuffer + fetchlimit, 0, rowbytes - fetchlimit);

			// read in the font if we need it
			if (mode < 6) {
				int hibase = (hval.mCHBASE << 1) & 0xfc;

				if (fonthibase != hibase) {
					fonthibase = hibase;
					mpMemory->DebugAnticReadMemory(mFontHi, (uint16)hibase << 8, sizeof mFontHi);
				}
			} else if (mode < 8) {
				int lobase = hval.mCHBASE << 1;

				if (fontlobase != lobase) {
					fontlobase = lobase;
					mpMemory->DebugAnticReadMemory(mFontLo, (uint16)lobase << 8, sizeof mFontLo);
				}
			}
		}

		// render out rows
		const int fontRowInv = (anticState.mCHACTL & 4) ? 0x07 : 0x00;
		const uint8 charBlankMask = (anticState.mCHACTL & 1) ? 0x00 : 0xFF;
		const uint8 charInvMask = (anticState.mCHACTL & 2) ? 0xFF : 0x00;

		do {
			// clear ANx decode buffer
			memset(mAnticDecodeBuffer, 0, sizeof mAnticDecodeBuffer);

			if (fetchWidth) {
				// ANTIC decode line buffer into ANx data
				DecodeState decodeState;
				decodeState.mMode = mode;
				decodeState.mDctr = row;
				decodeState.mRowBytes = rowbytes;
				decodeState.mCharRowInv = fontRowInv;
				decodeState.mCharBlankMask = charBlankMask;
				decodeState.mCharInvMask = charInvMask;

				switch(mode) {
					case  2: DecodeMode2 (decodeState); break;
					case  3: DecodeMode3 (decodeState); break;
					case  4: DecodeMode45(decodeState); break;
					case  5: DecodeMode45(decodeState); break;
					case  6: DecodeMode67(decodeState); break;
					case  7: DecodeMode67(decodeState); break;
					case  8: DecodeMode8 (decodeState); break;
					case  9: DecodeMode9 (decodeState); break;
					case 10: DecodeModeA (decodeState); break;
					case 11: DecodeModeBC(decodeState); break;
					case 12: DecodeModeBC(decodeState); break;
					case 13: DecodeModeDE(decodeState); break;
					case 14: DecodeModeDE(decodeState); break;
					case 15: DecodeModeF (decodeState); break;
					default:
						break;
				}

				// shift decode output according to fetch width
				const size_t hscrollDistance = hscroll ? hval.mHVScroll & 15 : 0;

				switch(fetchWidth) {
					case 1:		// narrow ($40-BF; 128cc)
						memmove(mAnticDecodeBuffer + 32 + hscrollDistance, mAnticDecodeBuffer, 128);
						break;

					case 2:		// normal ($30-CF; 160cc)
						memmove(mAnticDecodeBuffer + 16 + hscrollDistance, mAnticDecodeBuffer, 160);
						break;

					case 3:		// wide ($20-DF; 192cc)
						if (hscrollDistance) {
							memmove(mAnticDecodeBuffer + hscrollDistance, mAnticDecodeBuffer, 192);

							// we need to clear the left area as there is no data shifted (delayed) in
							memset(mAnticDecodeBuffer, 0, hscrollDistance);
						}
						break;
				}

				// window to display width
				switch(displayWidth) {
					case 1:		// narrow ($40-BF; 128cc)
						memset(mAnticDecodeBuffer, 0, 32);
						memset(mAnticDecodeBuffer + 160, 0, 32);
						break;

					case 2:		// normal ($30-BF; 160cc)
						memset(mAnticDecodeBuffer, 0, 16);
						memset(mAnticDecodeBuffer + 176, 0, 16);
						break;

					case 3:		// wide ($2C-DD; 178cc)
						memset(mAnticDecodeBuffer, 0, 12);
						memset(mAnticDecodeBuffer + 190, 0, 2);
						break;
				}
			}

			// GTIA decode playfield priority
			const uint8 gtiaMode = prior & 0xC0;
			bool hires = false;

			memset(mAnticPriorityBuffer, 0, sizeof mAnticPriorityBuffer);

			if (gtiaMode) {
				if (gtiaMode == 0x80)
					ConvertDecodeToPriorityMode10();
			} else {
				switch(mode) {
					case 2:
					case 3:
					case 15:
						ConvertDecodeToPriorityHires();
						hires = true;
						break;

					default:
						ConvertDecodeToPriorityLores();
						break;
				}
			}

			// update P/M graphics if DMA is enabled
			if (mbPMGraphicsEnabled) {
				if ((gractl & 3) && (hval.mDMACTL & 0x0C)) {
					const bool singleLine = (hval.mDMACTL & 0x10) != 0;
					uint32 pmBase = (uint32)anticState.mPMBASE << 8;
					uint32 pmStride = singleLine ? 0x100 : 0x80;

					if (singleLine)
						pmBase = (pmBase & 0xF800) + y + 8;
					else
						pmBase = (pmBase & 0xFC00) + (y >> 1) + 4;

					// evaluate VDELAY if this is an even line (odd line always DMAs)
					uint8 dmaMask = y & 1 ? 0xFF : ~gtiaState.mReg[0x1C];

					// check for missile DMA (also forced if player DMA is on)
					if ((gractl & 1) && (hval.mDMACTL & 0x0C)) {
						static constexpr uint8 kMissileMasks[16] {
							0x00, 0x03, 0x0C, 0x0F,
							0x30, 0x33, 0x3C, 0x3F,
							0xC0, 0xC3, 0xCC, 0xCF,
							0xF0, 0xF3, 0xFC, 0xFF,
						};

						// update GRAFM
						uint8 missileDmaData = mpMemory->DebugAnticReadByte((uint16)(pmBase + pmStride*3));
						gtiaState.mReg[0x11] ^= (gtiaState.mReg[0x11] ^ missileDmaData) & kMissileMasks[dmaMask & 15];
					}

					// check for player DMA
					if ((gractl & 2) && (hval.mDMACTL & 0x08)) {
						// update GRAFP0-P3
						if (dmaMask & 0x10)
							gtiaState.mReg[0x0D] = mpMemory->DebugAnticReadByte((uint16)(pmBase + pmStride*4));

						if (dmaMask & 0x20)
							gtiaState.mReg[0x0E] = mpMemory->DebugAnticReadByte((uint16)(pmBase + pmStride*5));

						if (dmaMask & 0x40)
							gtiaState.mReg[0x0F] = mpMemory->DebugAnticReadByte((uint16)(pmBase + pmStride*6));

						if (dmaMask & 0x80)
							gtiaState.mReg[0x10] = mpMemory->DebugAnticReadByte((uint16)(pmBase + pmStride*7));
					}

				}

				// render P/M graphics into priority buffer
				const uint8 spriteSizes[8] {
					(uint8)(gtiaState.mReg[0x08] & 3),
					(uint8)(gtiaState.mReg[0x09] & 3),
					(uint8)(gtiaState.mReg[0x0A] & 3),
					(uint8)(gtiaState.mReg[0x0B] & 3),
					(uint8)((gtiaState.mReg[0x0C] >> 0) & 3),
					(uint8)((gtiaState.mReg[0x0C] >> 2) & 3),
					(uint8)((gtiaState.mReg[0x0C] >> 4) & 3),
					(uint8)((gtiaState.mReg[0x0C] >> 6) & 3)
				};

				const bool fifthPlayerEnabled = (prior & 0x10) != 0;
				const uint8 spritePris[8] {
					ATGTIA::P0,
					ATGTIA::P1,
					ATGTIA::P2,
					ATGTIA::P3,
					fifthPlayerEnabled ? ATGTIA::PF3 : ATGTIA::P0,
					fifthPlayerEnabled ? ATGTIA::PF3 : ATGTIA::P1,
					fifthPlayerEnabled ? ATGTIA::PF3 : ATGTIA::P2,
					fifthPlayerEnabled ? ATGTIA::PF3 : ATGTIA::P3,
				};

				const uint8 spriteDatas[8] {
					gtiaState.mReg[0x0D],
					gtiaState.mReg[0x0E],
					gtiaState.mReg[0x0F],
					gtiaState.mReg[0x10],
					(uint8)((gtiaState.mReg[0x11] << 6) & 0xC0),
					(uint8)((gtiaState.mReg[0x11] << 4) & 0xC0),
					(uint8)((gtiaState.mReg[0x11] << 2) & 0xC0),
					(uint8)((gtiaState.mReg[0x11] << 0) & 0xC0),
				};

				for(int i=0; i<8; ++i) {
					const uint8 spriteData = spriteDatas[i];
					if (!spriteData)
						continue;

					// Display region is color clocks 34-221. Therefore, any sprite pos <2
					// or >= 222 will never display anything.
					const uint32 spritePos = gtiaState.mReg[i];
					if (spritePos < 2 || spritePos >= 222)
						continue;

					// Render sprite into priority buffer
					uint8 *VDRESTRICT priDst = &mAnticPriorityBuffer[spritePos];
					const uint8 spritePri = spritePris[i];

					switch(spriteSizes[i]) {
						case 0:		// 1x
						case 2:		// 1xalt
							if (spriteData & 0x80) priDst[0] |= spritePri;
							if (spriteData & 0x40) priDst[1] |= spritePri;
							if (spriteData & 0x20) priDst[2] |= spritePri;
							if (spriteData & 0x10) priDst[3] |= spritePri;
							if (spriteData & 0x08) priDst[4] |= spritePri;
							if (spriteData & 0x04) priDst[5] |= spritePri;
							if (spriteData & 0x02) priDst[6] |= spritePri;
							if (spriteData & 0x01) priDst[7] |= spritePri;
							break;

						case 1:		// 2x
							if (spriteData & 0x80) { priDst[ 0] |= spritePri; priDst[ 0] |= spritePri; }
							if (spriteData & 0x40) { priDst[ 2] |= spritePri; priDst[ 3] |= spritePri; }
							if (spriteData & 0x20) { priDst[ 4] |= spritePri; priDst[ 5] |= spritePri; }
							if (spriteData & 0x10) { priDst[ 6] |= spritePri; priDst[ 7] |= spritePri; }
							if (spriteData & 0x08) { priDst[ 8] |= spritePri; priDst[ 9] |= spritePri; }
							if (spriteData & 0x04) { priDst[10] |= spritePri; priDst[11] |= spritePri; }
							if (spriteData & 0x02) { priDst[12] |= spritePri; priDst[13] |= spritePri; }
							if (spriteData & 0x01) { priDst[14] |= spritePri; priDst[15] |= spritePri; }
							break;

						case 3:		// 4x
							if (spriteData & 0x80) { priDst[ 0] |= spritePri; priDst[ 1] |= spritePri;  priDst[ 2] |= spritePri;  priDst[ 3] |= spritePri; }
							if (spriteData & 0x40) { priDst[ 4] |= spritePri; priDst[ 5] |= spritePri;  priDst[ 6] |= spritePri;  priDst[ 7] |= spritePri; }
							if (spriteData & 0x20) { priDst[ 8] |= spritePri; priDst[ 9] |= spritePri;  priDst[10] |= spritePri;  priDst[11] |= spritePri; }
							if (spriteData & 0x10) { priDst[12] |= spritePri; priDst[13] |= spritePri;  priDst[14] |= spritePri;  priDst[15] |= spritePri; }
							if (spriteData & 0x08) { priDst[16] |= spritePri; priDst[17] |= spritePri;  priDst[18] |= spritePri;  priDst[19] |= spritePri; }
							if (spriteData & 0x04) { priDst[20] |= spritePri; priDst[21] |= spritePri;  priDst[22] |= spritePri;  priDst[23] |= spritePri; }
							if (spriteData & 0x02) { priDst[24] |= spritePri; priDst[25] |= spritePri;  priDst[26] |= spritePri;  priDst[27] |= spritePri; }
							if (spriteData & 0x01) { priDst[28] |= spritePri; priDst[29] |= spritePri;  priDst[30] |= spritePri;  priDst[31] |= spritePri; }
							break;
					}
				}
			}

			// Clear color clocks to enforce 34-221 window, but allow rendering 32-223.
			mAnticPriorityBuffer[32] = 0;
			mAnticPriorityBuffer[33] = 0;
			mAnticPriorityBuffer[222] = 0;
			mAnticPriorityBuffer[223] = 0;

			// GTIA palette lookup
			// There are 2ccs of padding at the front of the scanline that we can safely overwrite for
			// hidden cc positions $20-21.
			uint8 *dst = mDisplayBuffer.GetPixelRow<uint8>(y) - 4;

			RenderState renderState;
			renderState.mpDst = dst;
			renderState.mpPriTable = mPriTables[(prior & 15) + (prior & 0x20 ? 16 : 0)];

			renderState.mColorTable[ATGTIA::kColorP0		] = pm0;
			renderState.mColorTable[ATGTIA::kColorP1		] = pm1;
			renderState.mColorTable[ATGTIA::kColorP2		] = pm2;
			renderState.mColorTable[ATGTIA::kColorP3		] = pm3;
			renderState.mColorTable[ATGTIA::kColorPF0		] = pf0;
			renderState.mColorTable[ATGTIA::kColorPF1		] = pf1;
			renderState.mColorTable[ATGTIA::kColorPF2		] = pf2;
			renderState.mColorTable[ATGTIA::kColorPF3		] = pf3;
			renderState.mColorTable[ATGTIA::kColorBAK		] = pfbak;
			renderState.mColorTable[ATGTIA::kColorBlack		] = 0;
			renderState.mColorTable[ATGTIA::kColorP0P1		] = pm0 | pm1;
			renderState.mColorTable[ATGTIA::kColorP2P3		] = pm2 | pm3;
			renderState.mColorTable[ATGTIA::kColorPF0P0		] = pf0 | pm0;
			renderState.mColorTable[ATGTIA::kColorPF0P1		] = pf0 | pm1;
			renderState.mColorTable[ATGTIA::kColorPF0P0P1	] = pf0 | pm0 | pm1;
			renderState.mColorTable[ATGTIA::kColorPF1P0		] = pf1 | pm0;
			renderState.mColorTable[ATGTIA::kColorPF1P1		] = pf1 | pm1;
			renderState.mColorTable[ATGTIA::kColorPF1P0P1	] = pf1 | pm0 | pm1;
			renderState.mColorTable[ATGTIA::kColorPF2P2		] = pf2 | pm2;
			renderState.mColorTable[ATGTIA::kColorPF2P3		] = pf2 | pm3;
			renderState.mColorTable[ATGTIA::kColorPF2P2P3	] = pf2 | pm2 | pm3;
			renderState.mColorTable[ATGTIA::kColorPF3P2		] = pf3 | pm2;
			renderState.mColorTable[ATGTIA::kColorPF3P3		] = pf3 | pm3;
			renderState.mColorTable[ATGTIA::kColorPF3P2P3	] = pf3 | pm2 | pm3;

			switch(gtiaMode) {
				case 0x00:
					if (hires)
						RenderHires(renderState);
					else
						RenderLores(renderState);
					break;
				case 0x40:
					RenderMode9(renderState);
					break;
				case 0x80:
					RenderLores(renderState);
					break;
				case 0xC0:
					RenderMode11(renderState);
					break;
			}

			++y;
		} while(row++ != rowlast && y < 240);
	}

	mpGTIA->GetPalette(mPalette);
	mpDisplay->SetSourcePersistent(true, mDisplayBuffer);
}

// text 0.5cc no descenders
void ATDebugDisplay::DecodeMode2(const DecodeState& decodeState) {
	const uint8 *VDRESTRICT fontptr = mFontHi + ((decodeState.mDctr & 7) ^ decodeState.mCharRowInv);
	const uint8 *VDRESTRICT src = mAnticLineBuffer;
	uint8 *VDRESTRICT dst = mAnticDecodeBuffer;

	const uint8 charBlankMask = decodeState.mCharBlankMask;
	const uint8 charInvMask = decodeState.mCharInvMask;
	const size_t rowBytes = decodeState.mRowBytes;
	for(size_t i = 0; i < rowBytes; ++i) {
		uint8 c = fontptr[(uint32)(src[i] & 0x7f) << 3];

		if (src[i] & 0x80) {
			c &= charBlankMask;
			c ^= charInvMask;
		}

		dst[0] = kCodePF0 + ((c >> 6) & 0x03);
		dst[1] = kCodePF0 + ((c >> 4) & 0x03);
		dst[2] = kCodePF0 + ((c >> 2) & 0x03);
		dst[3] = kCodePF0 + ((c >> 0) & 0x03);
		dst += 4;
	}
}

// text 0.5cc descenders
void ATDebugDisplay::DecodeMode3(const DecodeState& decodeState) {
	int fontRow = decodeState.mDctr & 7;
	const uint8 *VDRESTRICT fontptr = mFontHi + (fontRow ^ decodeState.mCharRowInv);
	const uint8 *VDRESTRICT src = mAnticLineBuffer;
	uint8 *VDRESTRICT dst = mAnticDecodeBuffer;

	uint8 mask[4];
	mask[0] = mask[1] = mask[2] = decodeState.mDctr < 8 ? 0xFF : 0x00;
	mask[3] = ((decodeState.mDctr - 2) & 15) < 8 ? 0xFF : 0x00;

	const uint8 charBlankMask = decodeState.mCharBlankMask;
	const uint8 charInvMask = decodeState.mCharInvMask;
	const size_t rowBytes = decodeState.mRowBytes;
	for(size_t i = 0; i < rowBytes; ++i) {
		uint8 c = src[i];
		uint8 d = fontptr[(uint32)(c & 0x7f) << 3] & mask[(c >> 5) & 3];

		if (c & 0x80) {
			d &= charBlankMask;
			d ^= charInvMask;
		}

		dst[0] = kCodePF0 + ((d >> 6) & 0x03);
		dst[1] = kCodePF0 + ((d >> 4) & 0x03);
		dst[2] = kCodePF0 + ((d >> 2) & 0x03);
		dst[3] = kCodePF0 + ((d >> 0) & 0x03);
		dst += 4;
	}
}

// text 1cc 4 color
void ATDebugDisplay::DecodeMode45(const DecodeState& decodeState) {
		int fontRow = decodeState.mDctr;

		if (decodeState.mMode & 1)
		fontRow >>= 1;

	const uint8 *VDRESTRICT fontptr = mFontHi + ((fontRow & 7) ^ decodeState.mCharRowInv);
	const uint8 *VDRESTRICT src = mAnticLineBuffer;
	uint8 *VDRESTRICT dst = mAnticDecodeBuffer;

	static constexpr uint8 palA[4] = {
		kCodeBAK,
		kCodePF0,
		kCodePF1,
		kCodePF2
	};

	static constexpr uint8 palB[4] = {
		kCodeBAK,
		kCodePF0,
		kCodePF1,
		kCodePF3
	};

	const size_t rowBytes = decodeState.mRowBytes;
	for(size_t i=0; i<rowBytes; ++i) {
		const uint8 c = src[i];
		const uint8 d = fontptr[(int)(c & 0x7f) << 3];

		const uint8 *VDRESTRICT pal = (c & 0x80) ? palB : palA;

		dst[0] = pal[(d >> 6) & 3];
		dst[1] = pal[(d >> 4) & 3];
		dst[2] = pal[(d >> 2) & 3];
		dst[3] = pal[(d >> 0) & 3];
		dst += 4;
	}
}

// text 1cc 2 color
void ATDebugDisplay::DecodeMode67(const DecodeState& decodeState) {
	int fontRow = decodeState.mDctr;

	if (decodeState.mMode & 1)
		fontRow >>= 1;

	const uint8 *VDRESTRICT fontptr = mFontLo + ((fontRow & 7) ^ decodeState.mCharRowInv);
	const uint8 *VDRESTRICT src = mAnticLineBuffer;
	uint8 *VDRESTRICT dst = mAnticDecodeBuffer;

	const size_t rowBytes = decodeState.mRowBytes;
	for(size_t i = 0; i < rowBytes; ++i) {
		const uint8 c = fontptr[(uint32)(src[i] & 0x3f) << 3];
		const uint8 fore = kCodePF0 + (src[i] >> 6);

		dst[ 0] = (c & 0x80) ? fore : kCodeBAK;
		dst[ 1] = (c & 0x40) ? fore : kCodeBAK;
		dst[ 2] = (c & 0x20) ? fore : kCodeBAK;
		dst[ 3] = (c & 0x10) ? fore : kCodeBAK;
		dst[ 4] = (c & 0x08) ? fore : kCodeBAK;
		dst[ 5] = (c & 0x04) ? fore : kCodeBAK;
		dst[ 6] = (c & 0x02) ? fore : kCodeBAK;
		dst[ 7] = (c & 0x01) ? fore : kCodeBAK;
		dst += 8;
	}
}

// graphics 4cc 4 color
void ATDebugDisplay::DecodeMode8(const DecodeState& decodeState) {
	static constexpr uint32 pal[4] = {
		kCodeBAK * 0x01010101U,
		kCodePF0 * 0x01010101U,
		kCodePF1 * 0x01010101U,
		kCodePF2 * 0x01010101U,
	};

	const uint8 *VDRESTRICT src = mAnticLineBuffer;
	uint8 *VDRESTRICT dst = mAnticDecodeBuffer;

	const size_t rowBytes = decodeState.mRowBytes;
	for(size_t i=0; i<rowBytes; ++i) {
		const uint8 c  = src[i];

		VDWriteUnalignedLEU32(dst +  0, pal[(c >> 6) & 3]);
		VDWriteUnalignedLEU32(dst +  4, pal[(c >> 4) & 3]);
		VDWriteUnalignedLEU32(dst +  8, pal[(c >> 2) & 3]);
		VDWriteUnalignedLEU32(dst + 12, pal[(c >> 0) & 3]);
		dst += 16;
	}
}

// graphics 2cc 2 color
void ATDebugDisplay::DecodeMode9(const DecodeState& decodeState) {
	constexpr uint16 back = kCodeBAK * 0x0101;
	constexpr uint16 fore = kCodePF0 * 0x0101;

	const uint8 *VDRESTRICT src = mAnticLineBuffer;
	uint8 *VDRESTRICT dst = mAnticDecodeBuffer;

	const size_t rowBytes = decodeState.mRowBytes;
	for(size_t i=0; i<rowBytes; ++i) {
		const uint8 c  = src[i];

		VDWriteUnalignedLEU16(dst +  0, (c & 0x80) ? fore : back);
		VDWriteUnalignedLEU16(dst +  2, (c & 0x40) ? fore : back);
		VDWriteUnalignedLEU16(dst +  4, (c & 0x20) ? fore : back);
		VDWriteUnalignedLEU16(dst +  6, (c & 0x10) ? fore : back);
		VDWriteUnalignedLEU16(dst +  8, (c & 0x08) ? fore : back);
		VDWriteUnalignedLEU16(dst + 10, (c & 0x04) ? fore : back);
		VDWriteUnalignedLEU16(dst + 12, (c & 0x02) ? fore : back);
		VDWriteUnalignedLEU16(dst + 14, (c & 0x01) ? fore : back);
		dst += 16;
	}
}

// graphics 2cc 4 color
void ATDebugDisplay::DecodeModeA(const DecodeState& decodeState) {
	static constexpr uint32 pal[4] = {
		(uint32)kCodeBAK * 0x01010101,
		(uint32)kCodePF0 * 0x01010101,
		(uint32)kCodePF1 * 0x01010101,
		(uint32)kCodePF2 * 0x01010101,
	};

	const uint8 *VDRESTRICT src = mAnticLineBuffer;
	uint8 *VDRESTRICT dst = mAnticDecodeBuffer;

	const size_t rowBytes = decodeState.mRowBytes;
	for(size_t i=0; i<rowBytes; ++i) {
		const uint8 c  = src[i];

		VDWriteUnalignedLEU16(dst +  0, pal[(c >> 6) & 3]);
		VDWriteUnalignedLEU16(dst +  2, pal[(c >> 4) & 3]);
		VDWriteUnalignedLEU16(dst +  4, pal[(c >> 2) & 3]);
		VDWriteUnalignedLEU16(dst +  6, pal[(c >> 0) & 3]);
		dst += 8;
	}
}

// graphics 1cc 2 color
void ATDebugDisplay::DecodeModeBC(const DecodeState& decodeState) {
	const uint8 *VDRESTRICT src = mAnticLineBuffer;
	uint8 *VDRESTRICT dst = mAnticDecodeBuffer;

	const size_t rowBytes = decodeState.mRowBytes;
	for(size_t i=0; i<rowBytes; ++i) {
		const uint8 c = src[i];

		dst[0] = (c & 0x80) ? kCodePF0 : kCodeBAK;
		dst[1] = (c & 0x40) ? kCodePF0 : kCodeBAK;
		dst[2] = (c & 0x20) ? kCodePF0 : kCodeBAK;
		dst[3] = (c & 0x10) ? kCodePF0 : kCodeBAK;
		dst[4] = (c & 0x08) ? kCodePF0 : kCodeBAK;
		dst[5] = (c & 0x04) ? kCodePF0 : kCodeBAK;
		dst[6] = (c & 0x02) ? kCodePF0 : kCodeBAK;
		dst[7] = (c & 0x01) ? kCodePF0 : kCodeBAK;
		dst += 8;
	}
}

// graphics 1cc 4 color
void ATDebugDisplay::DecodeModeDE(const DecodeState& decodeState) {
	static constexpr uint8 pal[4] = {
		kCodeBAK,
		kCodePF0,
		kCodePF1,
		kCodePF2
	};

	const uint8 *VDRESTRICT src = mAnticLineBuffer;
	uint8 *VDRESTRICT dst = mAnticDecodeBuffer;

	const size_t rowBytes = decodeState.mRowBytes;
	for(size_t i=0; i<rowBytes; ++i) {
		const uint8 c = src[i];

		dst[0] = pal[(c >> 6) & 3];
		dst[1] = pal[(c >> 4) & 3];
		dst[2] = pal[(c >> 2) & 3];
		dst[3] = pal[(c >> 0) & 3];
		dst += 4;
	}
}

// 0.5cc 1.5 color
void ATDebugDisplay::DecodeModeF(const DecodeState& decodeState) {
	const uint8 *VDRESTRICT src = mAnticLineBuffer;
	uint8 *VDRESTRICT dst = mAnticDecodeBuffer;

	const size_t rowBytes = decodeState.mRowBytes;
	for(size_t i=0; i<rowBytes; ++i) {
		const uint8 c = src[i];

		dst[0] = kCodePF0 + ((c >> 6) & 3);
		dst[1] = kCodePF0 + ((c >> 4) & 3);
		dst[2] = kCodePF0 + ((c >> 2) & 3);
		dst[3] = kCodePF0 + ((c >> 0) & 3);
		dst += 4;
	}
}

void ATDebugDisplay::ConvertDecodeToPriorityLores() {
	static constexpr uint8 kPriLookup[8] {
		0, 0, 0, 0,
		ATGTIA::PF0,
		ATGTIA::PF1,
		ATGTIA::PF2,
		ATGTIA::PF3,
	};

	const uint8 *VDRESTRICT src = mAnticDecodeBuffer;
	uint8 *VDRESTRICT dst = mAnticPriorityBuffer + 0x20;

	for(size_t i=0; i<RenderState::kRenderColorClocks; ++i) {
		dst[i] = kPriLookup[src[i]];
	}
}

void ATDebugDisplay::ConvertDecodeToPriorityMode10() {
	const uint8 *VDRESTRICT src = mAnticDecodeBuffer;
	uint8 *VDRESTRICT dst = mAnticPriorityBuffer + 0x20;

	// The first 12 color clocks are guaranteed to be BAK, so we can hardcode
	// them and avoid underflow due to the decoding 1cc delay. Two BAK codes
	// decode to P0.
	dst[0] = ATGTIA::P0;
	dst++;

	static constexpr uint8 kPriLookup[16] {
		ATGTIA::P0,
		ATGTIA::P1,
		ATGTIA::P2,
		ATGTIA::P3,
		ATGTIA::PF0,
		ATGTIA::PF1,
		ATGTIA::PF2,
		ATGTIA::PF3,
		0,
		0,
		0,
		0,
		ATGTIA::PF0,
		ATGTIA::PF1,
		ATGTIA::PF2,
		ATGTIA::PF3,
	};

	for(size_t i=0; i<RenderState::kRenderColorClocks; i+=2) {
		const uint8 code0 = src[i+0];	// mode 10 1cc delay
		const uint8 code1 = src[i+1];
		uint8 pri = kPriLookup[((code0 & 3) << 2) + (code1 & 3)];

		// If the second color clock has a background code, it will turn off
		// the playfield outputs for both color clocks. This does not affect
		// the player outputs.
		if (!(code1 & 4))
			pri &= ATGTIA::P0123;

		dst[0] = pri;
		dst[1] = pri;
		dst += 2;
	}
}

void ATDebugDisplay::ConvertDecodeToPriorityHires() {
	static constexpr uint8 kPriLookup[8] {
		0, 0, 0, 0,
		ATGTIA::PF2,
		ATGTIA::PF2,
		ATGTIA::PF2,
		ATGTIA::PF2,
	};

	const uint8 *VDRESTRICT src = mAnticDecodeBuffer;
	uint8 *VDRESTRICT dst = mAnticPriorityBuffer + 0x20;

	for(size_t i=0; i<RenderState::kRenderColorClocks; ++i) {
		dst[i] = kPriLookup[src[i]];
	}
}

void ATDebugDisplay::RenderLores(const RenderState& renderState) {
	const uint8 *VDRESTRICT priTable = renderState.mpPriTable;
	const uint8 *VDRESTRICT pri = mAnticPriorityBuffer + 0x20;
	const uint8 *VDRESTRICT colorTable = renderState.mColorTable;
	uint8 *VDRESTRICT dst = renderState.mpDst;

	for(size_t i=0; i<RenderState::kRenderColorClocks; ++i) {
		uint8 px = colorTable[priTable[pri[i]]];;
		dst[0] = px;
		dst[1] = px;
		dst += 2;
	}
}

void ATDebugDisplay::RenderHires(const RenderState& renderState) {
	const uint8 *VDRESTRICT priTable = renderState.mpPriTable;
	const uint8 *VDRESTRICT pri = mAnticPriorityBuffer + 0x20;
	const uint8 *VDRESTRICT src = mAnticDecodeBuffer;
	const uint8 *VDRESTRICT colorTable = renderState.mColorTable;
	uint8 *VDRESTRICT dst = renderState.mpDst;

	const uint8 pf1lum = renderState.mColorTable[ATGTIA::kColorPF1] & 0x0F;

	for(size_t i=0; i<RenderState::kRenderColorClocks; ++i) {
		uint8 c = colorTable[priTable[pri[i]]];

		dst[0] = src[i] & 2 ? (c & 0xf0) + pf1lum : c;
		dst[1] = src[i] & 1 ? (c & 0xf0) + pf1lum : c;
		dst += 2;
	}
}

void ATDebugDisplay::RenderMode9(const RenderState& renderState) {
	const uint8 *VDRESTRICT priTable = renderState.mpPriTable;
	const uint8 *VDRESTRICT pri = mAnticPriorityBuffer + 0x20;
	const uint8 *VDRESTRICT src = mAnticDecodeBuffer;
	const uint8 *VDRESTRICT colorTable = renderState.mColorTable;
	uint8 *VDRESTRICT dst = renderState.mpDst;

	for(size_t i=0; i<RenderState::kRenderColorClocks; i+=2) {
		uint8 pri0 = pri[i+0];
		uint8 pri1 = pri[i+1];
		uint8 c0 = colorTable[priTable[pri0]];
		uint8 c1 = colorTable[priTable[pri1]];
		uint8 lum = ((src[i+0] & 3) << 2) + (src[i+1] & 3);

		if (!(pri0 & (ATGTIA::P0123)))
			c0 = (c0 & 0xf0) + lum;

		if (!(pri1 & (ATGTIA::P0123)))
			c1 = (c1 & 0xf0) + lum;

		dst[0] = c0;
		dst[1] = c0;
		dst[2] = c1;
		dst[3] = c1;
		dst += 4;
	}
}

void ATDebugDisplay::RenderMode11(const RenderState& renderState) {
	const uint8 *VDRESTRICT priTable = renderState.mpPriTable;
	const uint8 *VDRESTRICT pri = mAnticPriorityBuffer + 0x20;
	const uint8 *VDRESTRICT src = mAnticDecodeBuffer;
	const uint8 *VDRESTRICT colorTable = renderState.mColorTable;
	uint8 *VDRESTRICT dst = renderState.mpDst;

	for(size_t i=0; i<RenderState::kRenderColorClocks; i+=2) {
		uint8 pri0 = pri[i+0];
		uint8 pri1 = pri[i+1];
		uint8 c0 = colorTable[priTable[pri0]];
		uint8 c1 = colorTable[priTable[pri1]];
		uint8 hue = ((src[i+0] & 3) << 6) + ((src[i+1] & 3) << 4);

		if (!(pri0 & (ATGTIA::P0123))) {
			if (!hue)
				c0 &= 0xf0;

			c0 |= hue;
		}

		if (!(pri1 & (ATGTIA::P0123))) {
			if (!hue)
				c1 &= 0xf0;

			c1 |= hue;
		}

		dst[0] = c0;
		dst[1] = c0;
		dst[2] = c1;
		dst[3] = c1;
		dst += 4;
	}
}
