//	Altirra - Atari 800/800XL emulator
//	Copyright (C) 2008-2010 Avery Lee
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
//	You should have received a copy of the GNU General Public License
//	along with this program; if not, write to the Free Software
//	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

#include <stdafx.h>
#include <at/atcore/consoleoutput.h>
#include <at/atcore/deviceimpl.h>
#include <at/atcore/devicesnapshot.h>
#include <at/atcore/deviceu1mb.h>
#include <at/atcore/propertyset.h>
#include <at/atcore/savestate.h>
#include <at/atcore/wraptime.h>
#include "vbxe.h"
#include "vbxestate.h"
#include "gtiarenderer.h"
#include "gtiatables.h"
#include "console.h"
#include "memorymanager.h"
#include "irqcontroller.h"
#include "trace.h"
#include "artifacting.h"

using namespace ATGTIA;

#define VBXE_FETCH(addr) (mpMemory[(addr) & 0x7FFFF])
#define VBXE_FETCH_NOWRAP(addr) (mpMemory[(addr)])
#define VBXE_WRITE(addr, value) ((void)(mpMemory[(addr) & 0x7FFFF] = (value)))

namespace {
	constexpr uint8 ConvertPriorityToNative(uint8 pri) {
		pri = ~pri;

		pri = (pri << 4) + (pri >> 4);
		return pri;
	}

	constexpr uint8 ConvertPriorityFromNative(uint8 pri) {
		pri = ~pri;

		pri = (pri << 4) + (pri >> 4);
		return pri;
	}
	
	// Core version 1.26 changes the overlay priority mapping so that PF2 and PF3 share
	// a priority bit and the PF3 priority bit is repurposed for BAK. To accommodate this,
	// we translate the raw priority to produce PF23 and BAK bits. Note that our 'native
	// priority' fields have PF0-3 and P0-3 swapped, so we must also swap those here.
	struct PriorityTranslation {
		uint8 v[256] {};

		constexpr PriorityTranslation() {
			for(uint32 i=0; i<256; ++i) {
				v[i] = (i & 0xF3) + (i & 0x0C ? 0x04 : 0x00) + (!i ? 0x08 : 0x00);
			}
		};
	};
	
	static constexpr PriorityTranslation kPriorityTranslation;

	struct CollisionLookup {
		uint8 v[256] {};

		constexpr CollisionLookup() {
			for(uint32 i=0; i<256; ++i) {
				v[i] = (i & 0xF3) + (i & 0x0C ? 0x04 : 0x00);
			}
		};
	};
	
	static constexpr CollisionLookup kCollisionLookup;
}

// XDLC_TMON, XDLC_GMON, XDLC_HR, XDLC_LR
const ATVBXEOverlayMode ATVBXEEmulator::kOvModeTable[3][4]={
	// GTLH
	/* 0100 */ ATVBXEOverlayMode::Text,
	/* 0101 */ ATVBXEOverlayMode::Text,
	/* 0110 */ ATVBXEOverlayMode::Text,
	/* 0111 */ ATVBXEOverlayMode::Text,
	/* 1000 */ ATVBXEOverlayMode::SR,
	/* 1001 */ ATVBXEOverlayMode::HR,
	/* 1010 */ ATVBXEOverlayMode::LR,
	/* 1011 */ ATVBXEOverlayMode::Disabled,
	/* 1100 */ ATVBXEOverlayMode::Disabled,
	/* 1101 */ ATVBXEOverlayMode::Disabled,
	/* 1110 */ ATVBXEOverlayMode::Disabled,
	/* 1111 */ ATVBXEOverlayMode::Disabled,
};

ATVBXEEmulator::ATVBXEEmulator()
	: mpMemory(NULL)
	, mpMemMan(NULL)
	, mMemAcControl(0)
	, mMemAcBankA(0)
	, mMemAcBankB(0)
	, mb5200Mode(false)
	, mbSharedMemory(false)
	, mVersion(0x26)
	, mbVersion126(true)
	, mRegBase(0)
	, mXdlBaseAddr(0)
	, mXdlAddr(0)
	, mbXdlActive(false)
	, mbXdlEnabled(false)
	, mXdlRepeatCounter(0)
	, mOvMode(ATVBXEOverlayMode::Disabled)
	, mOvWidth(kOvWidth_Narrow)
	, mbOvTrans(false)
	, mbOvTrans15(false)
	, mOvHscroll(0)
	, mOvVscroll(0)
	, mOvAddr(0)
	, mOvStep(0)
	, mOvTextRow(0)
	, mChAddr(0)
	, mPfPaletteIndex(0)
	, mOvPaletteIndex(0)
	, mbExtendedColor(false)
	, mbAttrMapEnabled(false)
	, mAttrAddr(0)
	, mAttrStep(0)
	, mAttrWidth(0)
	, mAttrHeight(0)
	, mAttrHscroll(0)
	, mAttrVscroll(0)
	, mAttrRow(0)
	, mPsel(0)
	, mCsel(0)
	, mbIRQEnabled(false)
	, mbIRQRequest(false)
	, mConfigLatch(0)
	, mbBlitterContinue(false)
	, mBlitterMode(0)
	, mBlitCyclesLeft(0)
	, mBlitCyclesPerRow(0)
	, mBlitListAddr(0)
	, mBlitListFetchAddr(0)
	, mBlitSrcAddr(0)
	, mBlitSrcStepX(0)
	, mBlitSrcStepY(0)
	, mBlitDstAddr(0)
	, mBlitDstStepX(0)
	, mBlitDstStepY(0)
	, mBlitWidth(0)
	, mBlitHeight(0)
	, mBlitAndMask(0)
	, mBlitXorMask(0)
	, mBlitCollisionMask(0)
	, mBlitPatternMode(0)
	, mBlitCollisionCode(0)
	, mBlitZoomX(0)
	, mBlitZoomY(0)
	, mBlitZoomCounterY(0)
	, mpPfPalette(0)
	, mpOvPalette(0)
	, mpMergeBuffer(0)
	, mpAnticBuffer(0)
	, mpMergeBuffer0(0)
	, mpAnticBuffer0(0)
	, mpDst(0)
	, mX(0)
	, mRCIndex(0)
	, mRCCount(0)
	, mbHiresMode(false)
	, mbAnalysisMode(false)
	, mPRIOR(0)
	, mpPriTable(0)
	, mpPriTableHi(0)
	, mpColorTable(0)
	, mpMemLayerMEMACA(NULL)
	, mpMemLayerMEMACB(NULL)
	, mpMemLayerRegisters(NULL)
	, mpMemLayerGTIAOverlay(NULL)
{
	memset(mColorTable, 0, sizeof mColorTable);
	memset(mColorTableExt, 0, sizeof mColorTableExt);

	InitPriorityTables();

	mpColorTable = mColorTable;
	mpPriTable = mPriorityTables[0];
	mpPriTableHi = mPriorityTablesHi[0];
}

ATVBXEEmulator::~ATVBXEEmulator() {
}

void ATVBXEEmulator::SetSharedMemoryMode(bool sharedMemory) {
	mbSharedMemory = sharedMemory;
}

void ATVBXEEmulator::SetMemory(void *memory) {
	mpMemory = (uint8 *)memory;
}

void ATVBXEEmulator::Init(ATIRQController *irqController, ATMemoryManager *memman, ATScheduler *sch) {
	mpIRQController = irqController;
	mpMemMan = memman;
	mpScheduler = sch;

	ColdReset();
}

void ATVBXEEmulator::Shutdown() {
	if (mpScheduler) {
		mpScheduler->UnsetEvent(mpEventBlitterIrq);
		mpScheduler = nullptr;
	}

	ShutdownMemoryMaps();
}

void ATVBXEEmulator::ColdReset() {
	memcpy(mArchPalette[0], mDefaultPalette, sizeof mPalette[0]);
	memset(mArchPalette[1], 0, sizeof(uint32)*256*3);

	mbRenderPaletteChanged = false;

	RecorrectPalettes();

	mPsel			= 0;
	mCsel			= 0;
	mMemAcBankA		= 0;
	mMemAcBankB		= 0;
	mMemAcControl	= 0;
	mXdlAddr		= 0;
	mXdlBaseAddr	= 0;
	mbXdlEnabled	= false;
	mbXdlActive		= false;
	mOvMode			= ATVBXEOverlayMode::Disabled;
	mOvWidth		= kOvWidth_Normal;
	mOvMainPriority	= 0;

	// P0-P3 register contents are not guaranteed on power-up, but reports
	// are that the hardware typically powers up $00 -- which is significant
	// as it puts the overlay below everything else.
	for(auto& v : mOvPriority)
		v = ConvertPriorityToNative(0x00);

	mOvCollMask		= 0;
	mOvCollState	= 0;
	mOvAddr			= 0;
	mOvStep			= 0;
	mbOvTrans		= true;
	mbOvTrans15		= false;
	mOvHscroll		= 0;
	mOvVscroll		= 0;
	mBlitListAddr	= 0;
	mBlitListFetchAddr = 0;
	mpPfPalette		= mPalette[0];
	mpOvPalette	= mPalette[1];
	mPfPaletteIndex = 0;
	mOvPaletteIndex = 1;
	mAttrWidth		= 8;
	mAttrHeight		= 8;
	mAttrHscroll	= 0;
	mAttrRow		= 0;
	mChAddr			= 0;

	mConfigLatch	= 0;

	mbExtendedColor	= false;
	mbAttrMapEnabled = false;

	UpdateColorTable();
	InitMemoryMaps();
	WarmReset();
}

void ATVBXEEmulator::WarmReset() {
	// VIDEO_CONTROL: set to 0
	mbXdlEnabled	= false;
	mbExtendedColor = false;
	mbOvTrans		= true;
	mbOvTrans15		= false;

	// MEMAC_CONTROL: MCE and MAE set to 0
	mMemAcControl &= 0xf3;

	// MEMAC_BANK_SEL: MGE set to 0
	mMemAcBankA &= 0x7f;

	// MEMAC_B_CONTROL: MBCE and MBAE set to 0
	mMemAcBankB &= 0x3f;

	// BLITTER_START: set to 0
	// BLITTER_BUSY: set to 0
	mBlitState = BlitState::Stopped;
	mBlitCollisionCode = 0;
	mBlitCyclesLeft = 0;

	mpScheduler->UnsetEvent(mpEventBlitterIrq);

	// IRQ_CONTROL: set to 0
	// IRQ_STATUS: set to 0
	mbXdlActive		= false;
	mbIRQEnabled	= false;
	mbIRQRequest	= false;

	// Nuke current XDL processing.
	mOvWidth = kOvWidth_Normal;
	mOvMode = ATVBXEOverlayMode::Disabled;

	UpdateMemoryMaps();
}

void ATVBXEEmulator::Set5200Mode(bool enable) {
	mb5200Mode = enable;

	if (mpMemMan) {
		InitMemoryMaps();
		UpdateMemoryMaps();
	}
}

void ATVBXEEmulator::SetRegisterBase(uint8 page) {
	if (mRegBase == page)
		return;

	mRegBase = page;

	if (mpMemMan) {
		InitMemoryMaps();
		UpdateMemoryMaps();
	}
}

uint32 ATVBXEEmulator::GetVersion() const {
	// Convert MINOR_REVISION back from BCD to 1.xx format.
	return 100 + ((mVersion >> 4) * 10) + (mVersion & 15);
}

void ATVBXEEmulator::SetVersion(uint32 version) {
	// We only support specific versions, so clamp as appropriate. Also, we need
	// to convert to BCD.
	if (version >= 126)
		mVersion = 0x26;
	else if (version >= 124)
		mVersion = 0x24;
	else
		mVersion = 0x20;

	mbVersion126 = (mVersion >= 0x26);

	InitPriorityTables();
}

void ATVBXEEmulator::SetAnalysisMode(bool enable) {
	if (mbAnalysisMode != enable) {
		mbAnalysisMode = enable;

		UpdateColorTable();
	}
}

void ATVBXEEmulator::SetDefaultPalette(const uint32 pal[256], ATPaletteCorrector *palcorr) {
	if (pal) {
		memcpy(mDefaultPalette, pal, sizeof mDefaultPalette);

		if (!mbRenderPaletteChanged)
			memcpy(mArchPalette[0], mDefaultPalette, sizeof mArchPalette[0]);
	}

	mpPaletteCorrector = palcorr;

	RecorrectPalettes();
}

uint32 ATVBXEEmulator::GetRawPaletteAndSEL(uint32 pal[1024]) const {
	memcpy(pal, mArchPalette, 1024 * 4);

	return mPsel * 4 + mCsel;
}

void ATVBXEEmulator::SetTraceContext(ATTraceContext *context) {
	if (context) {
		ATTraceCollection *coll = context->mpCollection;

		const uint64 baseTime = context->mBaseTime;
		double invCycleRate = context->mBaseTickScale;

		ATTraceGroup *group = coll->AddGroup(L"VBXE");
		mpTraceChannelOverlay = group->AddSimpleChannel(baseTime, invCycleRate, L"Overlay");
		mpTraceChannelBlit = group->AddFormattedChannel(baseTime, invCycleRate, L"Blitter");
	} else {
		mpTraceChannelOverlay = nullptr;
		mpTraceChannelBlit = nullptr;
	}
}

sint32 ATVBXEEmulator::TryMapCPUAddressToLocal(uint16 addr) const {
	static constexpr uint8 kBankAMask[4]={
		0x7F,
		0x7E,
		0x7C,
		0x78,
	};

	// check MEMAC A (has priority over MEMAC B)
	const bool memacAEnabled = (mMemAcBankA & 0x80) && (mMemAcControl & 0x08);

	if (mb5200Mode) {
		if (memacAEnabled && addr >= 0xD800 && addr < 0xE800) {
			return (uint32)((mMemAcBankA & 0xF8) << 12) + (addr - 0xD800);
		}

		// no MEMAC B on 5200
		return -1;
	}

	if (memacAEnabled) {
		const uint32 winABase = (mMemAcControl & 0xF0) << 8;
		const uint32 winALimit = std::min<uint32>(0x10000, winABase + (0x1000 << (mMemAcControl & 3)));

		if (addr >= winABase && addr < winALimit)
			return ((uint32)(mMemAcBankA & kBankAMask[mMemAcControl & 3]) << 12) + (addr - winABase);
	}

	// check MEMAC B
	if (mMemAcBankB & 0x80) {
		if (addr >= 0x4000 && addr < 0x8000)
			return (((uint32)mMemAcBankB & 0x1F) << 14) + (addr - 0x4000);
	}

	// not in either window
	return -1;
}

void ATVBXEEmulator::SetBlitLoggingEnabled(bool enable, bool compact) {
	mbBlitLogging = enable;
	mbBlitLoggingCompact = compact;
}

void ATVBXEEmulator::DumpStatus() {
	ATConsolePrintf("XDL enabled:       %s\n", mbXdlEnabled ? "Yes" : "No");
	ATConsolePrintf("XDL active:        %s\n", mbXdlActive ? "Yes" : "No");
	ATConsolePrintf("XDL base address:  $%05X\n", mXdlBaseAddr & 0x7FFFF);
	ATConsolePrintf("XDL fetch address: $%05X\n", mXdlAddr & 0x7FFFF);

	static const char *const kWidthNames[]={
		"Narrow",
		"Normal",
		"Wide"
	};
	ATConsolePrintf("Overlay width:     %s\n", kWidthNames[mOvWidth]);

	static const char *const kModeNames[]={
		"Disabled",
		"Low resolution",
		"Standard resolution",
		"High resolution",
		"80-column text",
	};
	ATConsolePrintf("Overlay mode:      %s\n", kModeNames[(int)mOvMode]);
	ATConsolePrintf("Overlay address:   $%05X\n", mOvAddr & 0x7FFFF);
	ATConsolePrintf("Overlay step:      $%03X\n", mOvStep);
	ATConsolePrintf("Overlay priority:  $%02X | %02X %02X %02X %02X\n"
		, ConvertPriorityFromNative(mOvMainPriority)
		, ConvertPriorityFromNative(mOvPriority[0])
		, ConvertPriorityFromNative(mOvPriority[1])
		, ConvertPriorityFromNative(mOvPriority[2])
		, ConvertPriorityFromNative(mOvPriority[3])
		);

	static const char *const kBankModes[4]={
		"Disabled",
		"Antic only",
		"CPU only",
		"CPU and Antic",
	};

	uint32 winABase = (mMemAcControl & 0xF0) << 8;
	uint32 winALimit = std::min<uint32>(0x10000, winABase + (0x1000 << (mMemAcControl & 3)));

	static const uint8 kBankAMask[4]={
		0x7F,
		0x7E,
		0x7C,
		0x78,
	};

	if (mb5200Mode) {
		ATConsolePrintf("MEMAC Window A:    $%02X | $D800-$E7FF -> $%05X\n", mMemAcBankA, (uint32)(mMemAcBankA & 0xF8) << 12);
	} else {
		ATConsolePrintf("MEMAC Window A:    $%02X | $%04X-$%04X -> $%05X - %s\n", mMemAcBankA, winABase, winALimit - 1, (uint32)(mMemAcBankA & kBankAMask[mMemAcControl & 3]) << 12,
			kBankModes[mMemAcBankA & 0x80 ? (mMemAcControl >> 2) & 3 : 0]);
		ATConsolePrintf("MEMAC Window B:    $%02X | $%05X - %s\n", mMemAcBankB, ((uint32)mMemAcBankB & 0x1F) << 14, kBankModes[mMemAcBankB >> 6]);
	}

	ATConsolePrintf("Blitter IRQ:       %s, %s\n"
		, mbIRQEnabled ? "enabled" : "disabled"
		, mbIRQRequest ? "asserted" : "negated");

	if (IsBlitterActive()) {
		if (mBlitState == BlitState::Stopping)
			ATConsolePrintf("Blitter status:    active (%u rows left) (stopping in %d cycles)\n", mBlitHeightLeft, (sint32)(mBlitterStopTime - ATSCHEDULER_GETTIME(mpScheduler)));
		else
			ATConsolePrintf("Blitter status:    active (%u rows left) (%d cycle delta)\n", mBlitHeightLeft, mBlitCyclesLeft);
	} else
		ATConsolePrintf("Blitter status:    %s\n"
			, mBlitState == BlitState::Reload ? "reloading" : "disabled");
	ATConsolePrintf("Blitter list addr: $%05X\n", mBlitListAddr);
	ATConsolePrintf("Blitter list cur.: $%05X\n", mBlitListFetchAddr);
}

void ATVBXEEmulator::DumpXDL() {
	uint32 addr = mXdlBaseAddr;
	uint32 scanLines = 0;

	for(;;) {
		if (scanLines >= 240) {
			ATConsoleWrite("Aborting dump - XDL too long (exceeded 240 scanlines).\n");
			break;
		}

		uint8 xdl1 = VBXE_FETCH(addr++);
		uint8 xdl2 = VBXE_FETCH(addr++);

		ATConsolePrintf("%05X: %02X %02X      ", (addr - 2) & 0x7FFFF, xdl1, xdl2);

		if (xdl1 & 4) {
			ATConsoleWrite("; mode off\n");
		} else if (xdl1 & 3) {
			OvMode ovMode = kOvModeTable[(xdl1 & 3) - 1][(xdl2 >> 4) & 3];

			static const char *const kOvModeNames[]={
				"off",
				"lr",
				"sr",
				"hr",
				"text"
			};

			ATConsolePrintf("; mode %s\n", kOvModeNames[(int)ovMode]);
		} else
			ATConsoleWrite("; mode same\n");

		if (xdl1 & 0x10)
			ATConsoleWrite("                  ; map_off\n");
		else if (xdl1 & 0x08)
			ATConsoleWrite("                  ; map_on\n");

		// XDLC_RPTL (1 byte)
		uint32 repeat = 1;
		if (xdl1 & 0x20) {
			repeat = VBXE_FETCH(addr++) + 1;
			ATConsolePrintf("  %02X              ; repeat      %u\n"
				, repeat - 1
				, repeat);
		}

		scanLines += repeat;

		// XDLC_OVADR (5 byte)
		if (xdl1 & 0x40) {
			uint8 ov1 = VBXE_FETCH(addr++);
			uint8 ov2 = VBXE_FETCH(addr++);
			uint8 ov3 = VBXE_FETCH(addr++);
			uint8 step1 = VBXE_FETCH(addr++);
			uint8 step2 = VBXE_FETCH(addr++);

			uint32 ovaddr = (uint32)ov1 + ((uint32)ov2 << 8) + ((uint32)ov3 << 16);
			uint32 step = ((uint32)step1 + ((uint32)step2 << 8)) & 0xFFF;

			ATConsolePrintf("  %02X %02X %02X %02X %02X  ; load_ovl    $%05X, $%03X\n"
				, ov1
				, ov2
				, ov3
				, step1
				, step2
				, ovaddr
				, step);
		}

		// XDLC_OVSCRL (2 byte)
		if (xdl1 & 0x80) {
			// skip hscroll, vscroll
			uint8 hscroll = VBXE_FETCH(addr++);
			uint8 vscroll = VBXE_FETCH(addr++);

			ATConsolePrintf("  %02X %02X           ; ovscroll %d, %d\n"
				, hscroll
				, vscroll
				, hscroll & 7
				, vscroll & 7
				);
		}

		// XDLC_CHBASE (1 byte)
		if (xdl2 & 0x01) {
			uint8 chbase = VBXE_FETCH(addr++);

			ATConsolePrintf("  %02X              ; load_chbase $%05X\n", chbase, (uint32)chbase << 11);
		}

		// XDLC_MAPADR (5 byte)
		if (xdl2 & 0x02) {
			uint8 ad1 = VBXE_FETCH(addr++);
			uint8 ad2 = VBXE_FETCH(addr++);
			uint8 ad3 = VBXE_FETCH(addr++);
			uint8 step1 = VBXE_FETCH(addr++);
			uint8 step2 = VBXE_FETCH(addr++);

			uint32 mapaddr = (uint32)ad1 + ((uint32)ad2 << 8) + ((uint32)ad3 << 16);
			uint32 step = ((uint32)step1 + ((uint32)step2 << 8)) & 0xFFF;

			ATConsolePrintf("  %02X %02X %02X %02X %02X  ; load_map    $%05X, $%03X\n"
				, ad1
				, ad2
				, ad3
				, step1
				, step2
				, mapaddr, step);
		}

		// XDLC_MAPPAR (4 byte)
		if (xdl2 & 0x04) {
			uint8 hscroll = VBXE_FETCH(addr++);
			uint8 vscroll = VBXE_FETCH(addr++);
			uint8 w = VBXE_FETCH(addr++);
			uint8 h = VBXE_FETCH(addr++);
			ATConsolePrintf("  %02X %02X %02X %02X     ; map_attr    %d, %d, %dx%d\n"
				, hscroll
				, vscroll
				, w
				, h
				, hscroll & 31
				, vscroll & 31
				, (w & 31) + 1
				, (h & 31) + 1
				);
		}

		// XDLC_OVATT (2 byte)
		if (xdl2 & 0x08) {
			uint8 ctl = VBXE_FETCH(addr++);
			uint8 pri = VBXE_FETCH(addr++);

			static const char *const kWidthNames[4]={
				"narrow",
				"normal",
				"wide",
				"wide"
			};

			ATConsolePrintf("  %02X %02X           ; ovatt ovwidth=%s, ovpal=%d, pfpal=%d, pri=$%02X\n"
				, ctl
				, pri
				, kWidthNames[ctl & 3]
				, (ctl >> 4) & 3
				, (ctl >> 6) & 3
				, pri);
		}

		// XDLC_END
		if (xdl2 & 0x80) {
			ATConsolePrintf("; end\n");
			break;
		}
	}
}

void ATVBXEEmulator::DumpXDLHistory(ATConsoleOutput& output) {
	output <<= "                     Overlay                Text       Attribute map";
	output <<= "VPos     XDL    PF   Addr      Wid Md Pl Pr Char  Sc   Addr      Field Scroll";
	//          100-101  00000  P0   00000+000 Nrw SR P0 FF 00000 00   00000+000 32x32 31,31
	output <<= "----------------------------------------------------------------------------";

	uint32 xdlLast = ~(uint32)0;

	VDStringA s;
	for(uint32 idx = 0; idx < vdcountof(mXDLHistory); ) {
		const ATVBXEXDLHistoryEntry& xdlhe = mXDLHistory[idx];
		if (!xdlhe.mbXdlActive) {
			++idx;
			continue;
		}

		uint32 idx2 = idx;

		do {
			++idx2;
		} while(idx2 < vdcountof(mXDLHistory) && mXDLHistory[idx2] == xdlhe);

		if (idx2 - idx > 1)
			s.sprintf("%3u-%-3u", idx + 8, idx2 + 7);
		else
			s.sprintf("%3u    ", idx + 8);

		if (xdlLast != xdlhe.mXdlAddr) {
			s.append_sprintf(
				"  %05X"
				, xdlhe.mXdlAddr
			);

			xdlLast = xdlhe.mXdlAddr;
		} else {
			s += "  -    ";
		}

		s.append_sprintf(
			"  P%u"
			, xdlhe.mAttrByte >> 6
		);

		if (xdlhe.mOverlayMode != (uint8)ATVBXEOverlayMode::Disabled) {
			static constexpr const char *kOvWidths[] {
				"Nrw",
				"Nor",
				"Wid",
				"Nrw",
			};

			static constexpr const char *kOvModes[] {
				"LR",
				"SR",
				"HR",
				"Tx",
			};

			s.append_sprintf(
				"   %05X+%03X %s %s P%u %02X"
				, xdlhe.mOverlayAddr
				, xdlhe.mOverlayStep
				, kOvWidths[xdlhe.mAttrByte & 3]
				, kOvModes[xdlhe.mOverlayMode - 1]
				, (xdlhe.mAttrByte >> 4) & 3
				, xdlhe.mOvPriority
			);

			if (xdlhe.mOverlayMode == (uint8)ATVBXEOverlayMode::Text) {
				s.append_sprintf(
					" %05X %u%u"
					, (uint32)xdlhe.mChBase << 11
					, xdlhe.mOvHScroll
					, xdlhe.mOvVScroll
				);
			} else {
			//     00000 00
			s += " -     - ";
			}
		} else {
			//       00000+000 Nrw SR P0 FF 00000 00
			s += "   -         -   -  -  -  -     - ";
		}

		if (xdlhe.mbMapEnabled) {
			s.append_sprintf(
				"   %05X+%03X %2ux%-2u %2u,%-2u"
				, xdlhe.mMapAddr
				, xdlhe.mMapStep
				, xdlhe.mMapFieldWidth + 1
				, xdlhe.mMapFieldHeight + 1
				, xdlhe.mMapHScroll
				, xdlhe.mMapVScroll
			);
		} else {
			//       00000+000 31x31 31,31
			s += "   -     -    -     -";
		}

		output <<= s.c_str();

		idx = idx2;
	}
}

const ATVBXEXDLHistoryEntry *ATVBXEEmulator::GetXDLHistory(uint32 y) const {
	if (y >= 8 && y < 248)
		return &mXDLHistory[y - 8];
	else
		return nullptr;
}

void ATVBXEEmulator::DumpBlitList(sint32 addrOpt, bool compact, uint32 maxCount) {
	uint32 addr = addrOpt >= 0 ? (uint32)addrOpt : mBlitListAddr;
	uint32 count = 0;

	for(;;) {
		if (++count > maxCount) {
			ATConsoleWrite("Blit list exceeds max entries -- ending dump.\n");
			break;
		}
		
		if (!compact)
			ATConsolePrintf("$%05X:\n", addr);

		bool more = DumpBlitListEntry(addr, compact, false);

		if (!more)
			break;

		addr += 21;
	}
}

bool ATVBXEEmulator::DumpBlitListEntry(uint32 addr, bool compact, bool autologging) {
	const uint32 srcAddr = (uint32)VBXE_FETCH(addr) + ((uint32)VBXE_FETCH(addr + 1) << 8) + ((uint32)(VBXE_FETCH(addr + 2) & 0x7) << 16);
	const sint32 srcXInc = (sint8)VBXE_FETCH(addr + 5);
	const sint32 srcYInc = ((((uint32)VBXE_FETCH(addr + 3) + ((uint32)VBXE_FETCH(addr + 4) << 8)) & 0x1FFF) + 0xFFFFF000) ^ 0xFFFFF000;

	const uint32 dstAddr = (uint32)VBXE_FETCH(addr + 6) + ((uint32)VBXE_FETCH(addr + 7) << 8) + ((uint32)(VBXE_FETCH(addr + 8) & 0x7) << 16);
	const sint32 dstXInc = (sint8)VBXE_FETCH(addr + 11);
	const sint32 dstYInc = ((((uint32)VBXE_FETCH(addr + 9) + ((uint32)VBXE_FETCH(addr + 10) << 8)) & 0x1FFF) + 0xFFFFF000) ^ 0xFFFFF000;

	const uint32 sizeX = (uint32)VBXE_FETCH(addr + 12) + ((uint32)(VBXE_FETCH(addr + 13) & 0x01) << 8) + 1;
	const uint32 sizeY = (uint32)VBXE_FETCH(addr + 14) + 1;

	const uint8 andMask = VBXE_FETCH(addr + 15);
	const uint8 xorMask = VBXE_FETCH(addr + 16);
	const uint8 collMask = VBXE_FETCH(addr + 17);
	const uint8 zoomByte = VBXE_FETCH(addr + 18);
	const uint8 patternByte = VBXE_FETCH(addr + 19);
	const uint8 controlByte = VBXE_FETCH(addr + 20);

	const bool next = (controlByte & 0x08) != 0;

	static constexpr const char *kModeNames[]={
		"Copy",
		"Overlay",
		"Add",
		"Or",
		"And",
		"Xor",
		"HR Overlay",
		"Reserved"
	};

	if (compact) {
		VDStringA s;

		if (autologging)
			s = "VBXE: Starting new blit at ";

		s.append_sprintf("$%05X: %10s  %3dx%-3d ", addr, kModeNames[controlByte & 7], sizeX, sizeY);
		
		// check if we have a 1D operation
		const bool hasZoom = (zoomByte & 0x77) != 0;
		const bool hasPattern = (patternByte & 0x80) != 0;
		const bool hasConst = (andMask == 0);
		const bool linear = !hasZoom && !hasPattern && (hasConst || (srcYInc == srcXInc * sizeX && dstXInc == srcXInc)) && dstYInc == dstXInc * sizeX && (dstXInc == 1 || dstXInc == -1);

		// check if the source is used
		if (andMask) {
			if (sizeX == 1 && sizeY == 1)
				s.append_sprintf(" [$%05X]           ", srcAddr);
			else if (linear)
				s.append_sprintf(" [$%05X-$%05X]    ", srcAddr, srcAddr + srcYInc * sizeY - 1);
			else
				s.append_sprintf(" [$%05X,%+4d,%+5d]", srcAddr, srcXInc, srcYInc);

			if (andMask != 0xFF)
				s.append_sprintf(" &$%02X", andMask);
			else
				s += "     ";

			if (xorMask)
				s.append_sprintf("^$%02X", xorMask);
			else
				s += "    ";

			if (hasPattern)
				s.append_sprintf(" pattern:%2d", patternByte & 0x7F);
		} else {
			s.append_sprintf("                 $%02X         ", xorMask);
		}

		if (hasZoom)
			s.append_sprintf(" zoom:%dx%d", (zoomByte & 7) + 1, ((zoomByte >> 4) & 7) + 1);

		s += " -> ";

		if (sizeX == 1 && sizeY == 1 && !hasZoom)
			s.append_sprintf(" [$%05X]", dstAddr);
		else if (linear)
			s.append_sprintf(" [$%05X-$%05X]", dstAddr, dstAddr + dstYInc * sizeY - 1);
		else
			s.append_sprintf(" [$%05X,%+4d,%+5d]", dstAddr, dstXInc, dstYInc);

		// check for collision detection
		if (collMask)
			s.append_sprintf(" [coll $%02X]", collMask);

		s += '\n';

		if (autologging)
			ATConsoleTaggedPrintf("%s", s.c_str());
		else
			ATConsoleWrite(s.c_str());
	} else {
		if (autologging)
			ATConsoleTaggedPrintf("VBXE: Starting new blit at $%05X:\n", addr);

		ATConsolePrintf("  Source: $%05X Xinc=%+d Yinc=%+d\n", srcAddr, srcXInc, srcYInc);
		ATConsolePrintf("  Dest:   $%05X Xinc=%+d Yinc=%+d\n", dstAddr, dstXInc, dstYInc);
		ATConsolePrintf("  Size:   %u x %u\n", sizeX, sizeY);
		ATConsolePrintf("  Masks:  AND=$%02X, XOR=$%02X, COLL=$%02X\n"
			, andMask
			, xorMask
			, collMask
			);

		ATConsolePrintf("  Zoom:   %d x %d\n", (zoomByte & 7) + 1, ((zoomByte >> 4) & 7) + 1);

		if (patternByte & 0x80)
			ATConsolePrintf("  Patt:   repeat every %d\n", (patternByte & 0x3F) + 1);
		else
			ATConsolePrintf("  Patt:   disabled\n");

		ATConsolePrintf("  Mode:   %d (%s)\n", controlByte & 7, kModeNames[controlByte & 7]);
	}

	return next;
}

void ATVBXEEmulator::LoadState(const IATObjectState *state) {
	if (!state) {
		const ATSaveStateVbxe defaultState;
		return LoadState(&defaultState);
	}

	const ATSaveStateVbxe& vstate = atser_cast<const ATSaveStateVbxe&>(*state);

	if (mpMemory && !mbSharedMemory) {
		memset(mpMemory, 0, 524288);

		if (vstate.mpMemory) {
			const auto& readBuffer = vstate.mpMemory->GetReadBuffer();
			memcpy(mpMemory, readBuffer.data(), std::min<size_t>(524288, readBuffer.size()));
		}
	}

	WriteControl(0x40, vstate.mVideoControl);
	mXdlBaseAddr = vstate.mXdlAddr & kLocalAddrMask;
	mXdlAddr = vstate.mXdlAddrActive & kLocalAddrMask;
	WriteControl(0x44, vstate.mCsel);
	WriteControl(0x45, vstate.mPsel);
	WriteControl(0x49, vstate.mColMask);

	// need to restore IRQ_CONTROL directly as we don't want to whack the IRQ request state
	mbIRQEnabled = (vstate.mIrqControl & 0x01) != 0;

	WriteControl(0x55, vstate.mPri[0]);
	WriteControl(0x56, vstate.mPri[1]);
	WriteControl(0x57, vstate.mPri[2]);
	WriteControl(0x58, vstate.mPri[3]);
	WriteControl(0x5D, vstate.mMemacBControl);
	WriteControl(0x5E, vstate.mMemacControl);
	WriteControl(0x5F, vstate.mMemacBankSel);

	mBlitListAddr = vstate.mBlitListAddr & kLocalAddrMask;

	mOvCollState = (vstate.mArchColDetect << 4) + (vstate.mArchColDetect >> 4);
	mBlitCollisionCode = vstate.mArchBltCollisionCode;
	mbIRQRequest = (vstate.mArchIrqStatus & 0x01) != 0;

	// restore VBXE palette
	for(uint32 i=0; i<4; ++i) {
		for(uint32 j=0; j<256; ++j) {
			uint32 c = vstate.mPalette[i*256 + j];
			mArchPalette[i][j] = (c & 0xFEFEFE) + ((c & 0x808080) >> 7);
		}
	}

	mbRenderPaletteChanged = true;
	RecorrectPalettes();

	// restore XDL state
	mXdlRepeatCounter = (uint32)vstate.mXdlRepeat + 1;

	mPfPaletteIndex = vstate.mIntPfPalette & 3;
	mpPfPalette = mPalette[mPfPaletteIndex];

	mOvPaletteIndex = vstate.mIntOvPalette & 3;
	mpOvPalette = mPalette[mOvPaletteIndex];

	// reconstruct overlay mode
	if (vstate.mIntOvMode & 0x01)
		mOvMode = ATVBXEOverlayMode::Text;
	else if (!(vstate.mIntOvMode & 0x02))
		mOvMode = ATVBXEOverlayMode::Disabled;
	else if (vstate.mIntOvMode & 0x10)
		mOvMode = ATVBXEOverlayMode::HR;
	else if (vstate.mIntOvMode & 0x20)
		mOvMode = ATVBXEOverlayMode::LR;
	else
		mOvMode = ATVBXEOverlayMode::SR;

	if (vstate.mIntOvWidth == 2)
		mOvWidth = kOvWidth_Wide;
	else if (vstate.mIntOvWidth == 1)
		mOvWidth = kOvWidth_Normal;
	else
		mOvWidth = kOvWidth_Narrow;

	mOvAddr = vstate.mIntOvAddr & kLocalAddrMask;
	mOvStep = vstate.mIntOvStep & 0xFFF;
	mOvHscroll = vstate.mIntOvHscroll & 7;
	mOvVscroll = vstate.mIntOvVscroll & 7;
	mOvTextRow = vstate.mIntOvTextRow & 7;
	mChAddr = (uint32)vstate.mIntOvChbase << 11;
	mOvMainPriority = ConvertPriorityToNative(vstate.mIntOvPriority);
	mbAttrMapEnabled = vstate.mbIntMapEnabled;
	mAttrAddr = vstate.mIntMapAddr & kLocalAddrMask;
	mAttrStep = vstate.mIntMapStep & 0xFFF;
	mAttrHscroll = vstate.mIntMapHscroll & 0x1F;
	mAttrVscroll = vstate.mIntMapVscroll & 0x1F;
	mAttrWidth = (vstate.mIntMapWidth & 0x1F) + 1;
	mAttrHeight = (vstate.mIntMapHeight & 0x1F) + 1;
	mAttrRow = vstate.mIntMapRow & 0x1F;

	// restore blitter internal state
	mBlitState = BlitState::Stopped;
	mpScheduler->UnsetEvent(mpEventBlitterIrq);

	if (vstate.mArchBlitterBusy != 0) {
		mBlitListFetchAddr = vstate.mIntBlitListAddr & kLocalAddrMask;
		mBlitCyclesLeft = vstate.mIntBlitCyclesPending;

		LoadBCB(vstate.mIntBlitState);

		mBlitActiveCollisionMask = vstate.mbIntBlitCollisionLatched ? 0 : mBlitCollisionMask;

		mBlitHeightLeft = std::min<uint32>(vstate.mIntBlitHeightLeft, mBlitHeight);
		mBlitZoomCounterY = vstate.mIntBlitYZoomCounter & 7;

		// blit running
		if (vstate.mIntBlitStopDelay > 0) {
			mBlitState = BlitState::Stopping;
			mpScheduler->SetEvent(vstate.mIntBlitStopDelay, this, 1, mpEventBlitterIrq);
		} else if (mBlitHeightLeft > 0)
			mBlitState = BlitState::ProcessBlit;
		else
			mBlitState = BlitState::Reload;
	}

	// restore GTIA tracking state
	RegisterChange rcinit[10];

	for(int i=0; i<9; ++i)
		rcinit[i] = RegisterChange { .mReg = (uint8)(0x12 + i), .mValue = vstate.mGtiaPal[i] };

	rcinit[9] = RegisterChange { .mReg = 0x1B, .mValue = vstate.mGtiaPrior };

	UpdateRegisters(rcinit, 10);

	// restore GTIA register change queue
	mRCIndex = 0;
	mRCCount = vstate.mGtiaRegisterChanges.size() / 3;
	mRegisterChanges.resize(mRCCount);

	for(int i=0; i<mRCCount; ++i) {
		const uint8 *src = &vstate.mGtiaRegisterChanges[i*3];
		auto& dst = mRegisterChanges[i];

		// note that the serializer has already validated ascending position
		dst.mPos = src[0];
		dst.mReg = src[1];
		dst.mValue = src[2];
	}

	// update IRQ state
	if (mbIRQEnabled && mbIRQRequest)
		mpIRQController->Assert(kATIRQSource_VBXE, true);
	else
		mpIRQController->Negate(kATIRQSource_VBXE, true);
}

vdrefptr<IATObjectState> ATVBXEEmulator::SaveState() const {
	const vdrefptr state { new ATSaveStateVbxe };

	if (mpMemory && !mbSharedMemory) {
		state->mpMemory = new ATSaveStateMemoryBuffer;
		state->mpMemory->mpDirectName = L"vbxe-mem.bin";
		state->mpMemory->GetWriteBuffer().assign(mpMemory, mpMemory + 524288);
	}

	state->mVideoControl
		= (mbXdlEnabled ? 0x01 : 0x00)
		+ (mbExtendedColor ? 0x02 : 0x00)
		+ (mbOvTrans ? 0x00 : 0x04)
		+ (mbOvTrans15 ? 0x08 : 0x00);

	state->mXdlAddr = mXdlBaseAddr;
	state->mXdlAddrActive = mXdlAddr;

	state->mCsel = mCsel;
	state->mPsel = mPsel;

	state->mColMask = mOvCollMask;
	state->mBlitListAddr = mBlitListAddr;
	
	for(uint32 i=0; i<4; ++i)
		state->mPri[i] = ConvertPriorityFromNative(mOvPriority[i]);

	state->mIrqControl = mbIRQEnabled ? 0x01 : 0x00;

	switch(mBlitState) {
		case BlitState::Stopped:
		default:
			state->mArchBlitterBusy = 0x00;
			break;

		case BlitState::Reload:
			state->mArchBlitterBusy = 0x01;
			break;

		case BlitState::ProcessBlit:
		case BlitState::Stopping:
			state->mArchBlitterBusy = 0x02;
			break;
	}

	state->mMemacBControl = mMemAcBankB;
	state->mMemacControl = mMemAcControl;
	state->mMemacBankSel = mMemAcBankA;

	for(uint32 i=0; i<4; ++i) {
		for(uint32 j=0; j<256; ++j) {
			state->mPalette[i * 256 + j] = mArchPalette[i][j] & 0xFEFEFE;
		}
	}

	// read registers
	state->mArchColDetect = (mOvCollState << 4) + (mOvCollState >> 4);
	state->mArchBltCollisionCode = mBlitCollisionCode;
	state->mArchIrqStatus = mbIRQRequest ? 0x01 : 0x00;
	
	// internal state
	state->mXdlRepeat = (uint8)(mXdlRepeatCounter - 1);
	state->mIntPfPalette = mPfPaletteIndex;
	state->mIntOvPalette = mOvPaletteIndex;

	// convert overlay mode back to XDLC_* mode bits
	switch(mOvMode) {
		case ATVBXEOverlayMode::Disabled:
		default:
			state->mIntOvMode = 0x00;
			break;

		case ATVBXEOverlayMode::LR:
			state->mIntOvMode = 0x22;
			break;
		case ATVBXEOverlayMode::SR:
			state->mIntOvMode = 0x02;
			break;
		case ATVBXEOverlayMode::HR:
			state->mIntOvMode = 0x12;
			break;
		case ATVBXEOverlayMode::Text:
			state->mIntOvMode = 0x01;
			break;
	}

	state->mIntOvWidth = (uint8)mOvWidth;
	state->mIntOvAddr = mOvAddr;
	state->mIntOvStep = mOvStep;
	state->mIntOvHscroll = mOvHscroll;
	state->mIntOvVscroll = mOvVscroll;
	state->mIntOvChbase = (uint8)(mChAddr >> 11);
	state->mIntOvPriority = ConvertPriorityFromNative(mOvMainPriority);
	state->mIntOvTextRow = mOvTextRow;
	state->mbIntMapEnabled = mbAttrMapEnabled;
	state->mIntMapAddr = mAttrAddr;
	state->mIntMapStep = mAttrStep;
	state->mIntMapHscroll = mAttrHscroll;
	state->mIntMapVscroll = mAttrVscroll;
	state->mIntMapWidth = mAttrWidth - 1;
	state->mIntMapHeight = mAttrHeight - 1;
	state->mIntMapRow = mAttrRow;

	// save blitter internal state
	if (mBlitState != BlitState::Stopped) {
		state->mIntBlitListAddr = mBlitListFetchAddr;
		state->mIntBlitCyclesPending = mBlitCyclesLeft;
		state->mIntBlitYZoomCounter = mBlitZoomCounterY;
		SaveBCB(state->mIntBlitState);

		state->mbIntBlitCollisionLatched = (mBlitActiveCollisionMask != mBlitCollisionMask);

		state->mIntBlitHeightLeft = 0;

		if (mpEventBlitterIrq)
			state->mIntBlitStopDelay = mpScheduler->GetTicksToEvent(mpEventBlitterIrq);
		else if (mBlitState == BlitState::ProcessBlit)
			state->mIntBlitHeightLeft = mBlitHeightLeft;
	}

	// save GTIA tracking state
	state->mGtiaPal[0] = mColorTableExt[kColorP0];
	state->mGtiaPal[1] = mColorTableExt[kColorP1];
	state->mGtiaPal[2] = mColorTableExt[kColorP2];
	state->mGtiaPal[3] = mColorTableExt[kColorP3];
	state->mGtiaPal[4] = mColorTableExt[kColorPF0];
	state->mGtiaPal[5] = mColorTableExt[kColorPF1];
	state->mGtiaPal[6] = mColorTableExt[kColorPF2];
	state->mGtiaPal[7] = mColorTableExt[kColorPF3];
	state->mGtiaPal[8] = mColorTableExt[kColorBAK];
	state->mGtiaPrior = mPRIOR;

	// save register change queue
	const int numRC = mRCCount - mRCIndex;
	state->mGtiaRegisterChanges.resize(numRC * 3);
	uint8 *rcdst = state->mGtiaRegisterChanges.data();

	for(int i=0; i<numRC; ++i) {
		const RegisterChange& rc = mRegisterChanges[i];

		rcdst[0] = rc.mPos;
		rcdst[1] = rc.mReg;
		rcdst[2] = rc.mValue;
		rcdst += 3;
	}

	return state;
}

sint32 ATVBXEEmulator::ReadControl(uint8 addrLo) {
	switch(addrLo) {
		case 0x40:	// CORE_VERSION
			return 0x10;

		case 0x41:	// MINOR_REVISION
			// Only 1.21 and up indicate shared memory in bit 7.
			return (mbSharedMemory && mVersion >= 0x21 ? 0x80 : 0x00) | mVersion;

		case 0x4A:	// COLDETECT
			// convert native collision back to defined order
			return (mOvCollState << 4) + (mOvCollState >> 4);

		case 0x50:	// BLT_COLLISION_CODE
			return mBlitCollisionCode;

		case 0x53:	// BLITTER_BUSY
			// D7-D2: RAZ
			// D1: BUSY (1 = busy)
			// D0: BCB_LOAD (1 = loading from blit list)
			return (IsBlitterActive() ? 0x02 : 0x00) | (mBlitState == BlitState::Reload ? 0x01 : 0x00);

		case 0x54:	// IRQ_STATUS
			return mbIRQRequest ? 0x01 : 0x00;

		case 0x5E:	// MEMAC_CONTROL
			return mMemAcControl;

		case 0x5F:	// MEMAC_BANK_SEL
			return mMemAcBankA;

		default:
			if (addrLo >= 0xC0) {
				// D0 = DATA0
				// D1 = DCLK
				// D2 = /CONFIG
				// D7 = CONF_DONE
				return mConfigLatch;
			}

			break;
	}

	return -1;
}

bool ATVBXEEmulator::WriteControl(uint8 addrLo, uint8 value) {
	switch(addrLo) {
		case 0x40:	// VIDEO_CONTROL
			mbXdlEnabled = (value & 0x01) != 0;
			mbExtendedColor = (value & 0x02) != 0;
			mbOvTrans = (value & 0x04) == 0;
			mbOvTrans15 = (value & 0x08) != 0;
			UpdateColorTable();
			break;

		case 0x41:	// XDL_ADR0
			mXdlBaseAddr = (mXdlBaseAddr & 0x7FF00) + ((uint32)value << 0);
			break;

		case 0x42:	// XDL_ADR1
			mXdlBaseAddr = (mXdlBaseAddr & 0x700FF) + ((uint32)value << 8);
			break;

		case 0x43:	// XDL_ADR2
			mXdlBaseAddr = (mXdlBaseAddr & 0x0FFFF) + ((uint32)(value & 0x07) << 16);
			break;

		case 0x44:	// CSEL
			mCsel = value;
			break;

		case 0x45:	// PSEL
			mPsel = value & 0x03;
			break;

		case 0x46:	// CR
			mArchPalette[mPsel][mCsel] = (mArchPalette[mPsel][mCsel] & 0x00FFFF) + ((uint32)(value & 0xFE) << 16) + (((uint32)(value & 0x80) << 9));
			mPalette[mPsel][mCsel] = mpPaletteCorrector->CorrectSingleColor(mArchPalette[mPsel][mCsel], mRenderPaletteCorrectionMode, mbRenderPaletteSigned);
			mbRenderPaletteChanged = true;
			break;

		case 0x47:	// CG
			mArchPalette[mPsel][mCsel] = (mArchPalette[mPsel][mCsel] & 0xFF00FF) + ((uint32)(value & 0xFE) << 8) + (((uint32)(value & 0x80) << 1));
			mPalette[mPsel][mCsel] = mpPaletteCorrector->CorrectSingleColor(mArchPalette[mPsel][mCsel], mRenderPaletteCorrectionMode, mbRenderPaletteSigned);
			mbRenderPaletteChanged = true;
			break;

		case 0x48:	// CB
			mArchPalette[mPsel][mCsel] = (mArchPalette[mPsel][mCsel] & 0xFFFF00) + (value & 0xFE) + (value >> 7);
			mPalette[mPsel][mCsel] = mpPaletteCorrector->CorrectSingleColor(mArchPalette[mPsel][mCsel], mRenderPaletteCorrectionMode, mbRenderPaletteSigned);
			mbRenderPaletteChanged = true;
			++mCsel;
			break;

		case 0x49:	// COLMASK
			mOvCollMask = value;
			break;

		case 0x4A:	// COLCLR
			mOvCollState = 0;
			break;

		case 0x50:	// BL_ADR0
			mBlitListAddr = (mBlitListAddr & 0x7FF00) + ((uint32)value << 0);
			break;

		case 0x51:	// BL_ADR1
			mBlitListAddr = (mBlitListAddr & 0x700FF) + ((uint32)value << 8);
			break;

		case 0x52:	// BL_ADR2
			mBlitListAddr = (mBlitListAddr & 0x0FFFF) + ((uint32)(value & 0x07) << 16);
			break;

		case 0x53:	// BLITTER_START
			// D0: 1 = START, 0 = STOP
			if (value & 1) {
				// Enable the blitter.
				//
				// Note that if the blitter is already running, trying to set this bit again
				// does nothing.

				if (!IsBlitterActive()) {
					mBlitState = BlitState::Reload;
					mbBlitterContinue = true;
					mBlitListFetchAddr = mBlitListAddr;

					// Correct the amount of time that the blitter has left, since we're
					// likely starting mid-scan.
					uint32 maxBlitTime = (mBlitterEndScanTime - ATSCHEDULER_GETTIME(mpScheduler)) * 8;
					if (maxBlitTime >= (UINT32_C(1) << 31))
						maxBlitTime = 0;

					if (mBlitCyclesLeft > (sint32)maxBlitTime)
						mBlitCyclesLeft = (sint32)maxBlitTime;

					// We have to load the first entry immediately because some demos are a
					// bit creative and overwrite the first entry without checking blitter
					// status...
					LoadBlitter();
					RunBlitter();
				}
			} else {
				// Stop the blitter.
				mBlitState = BlitState::Stopped;

				mpScheduler->UnsetEvent(mpEventBlitterIrq);
			}
			break;

		case 0x54:	// IRQ_CONTROL
			// acknowledge blitter interrupt
			if (mbIRQRequest) {
				mbIRQRequest = false;

				if (mbIRQEnabled)
					mpIRQController->Negate(kATIRQSource_VBXE, true);
			}

			// modify blitter IRQ enabled setting
			mbIRQEnabled = (value & 0x01) != 0;
			break;

		case 0x55:	// P0
			mOvPriority[0] = ConvertPriorityToNative(value);
			break;

		case 0x56:	// P1
			mOvPriority[1] = ConvertPriorityToNative(value);
			break;

		case 0x57:	// P2
			mOvPriority[2] = ConvertPriorityToNative(value);
			break;

		case 0x58:	// P3
			mOvPriority[3] = ConvertPriorityToNative(value);
			break;

		case 0x5D:	// MEMAC_B_CONTROL
			if (mMemAcBankB != value) {
				mMemAcBankB = value;
				UpdateMemoryMaps();
			}
			break;

		case 0x5E:	// MEMAC_CONTROL
			if (mMemAcControl != value) {
				mMemAcControl = value;
				UpdateMemoryMaps();
			}
			break;

		case 0x5F:	// MEMAC_BANK_SEL
			if (mMemAcBankA != value) {
				mMemAcBankA = value;
				UpdateMemoryMaps();
			}
			break;

		default:
			if (addrLo >= 0xC0) {
				// D0 = DATA0
				// D1 = DCLK
				// D2 = /CONFIG
				mConfigLatch = value & 7;
			}

			break;
	}

	return false;
}

bool ATVBXEEmulator::StaticGTIAWrite(void *thisptr, uint32 reg, uint8 value) {
	if ((uint8)reg >= 0x80)
		((ATVBXEEmulator *)thisptr)->WarmReset();

	return false;
}

void ATVBXEEmulator::InitMemoryMaps() {
	ShutdownMemoryMaps();

	// Window A has priority over window B
	uint8 *dummyRam = &mPriorityTables[0][0].mMapColor;
	mpMemLayerMEMACA = mpMemMan->CreateLayer(kATMemoryPri_Extsel+1, dummyRam, 0xD8, 0x10, false);
	mpMemMan->SetLayerName(mpMemLayerMEMACA, "VBXE MEMAC A");
	mpMemLayerMEMACB = mpMemMan->CreateLayer(kATMemoryPri_Extsel, dummyRam, 0x40, 0x40, false);
	mpMemMan->SetLayerName(mpMemLayerMEMACB, "VBXE MEMAC B");

	ATMemoryHandlerTable handler;
	handler.mbPassReads			= true;
	handler.mbPassAnticReads	= true;
	handler.mbPassWrites		= true;
	handler.mpThis				= this;
	handler.mpDebugReadHandler	= NULL;
	handler.mpReadHandler		= NULL;
	handler.mpWriteHandler		= StaticGTIAWrite;

	if (mb5200Mode)
		mpMemLayerGTIAOverlay = mpMemMan->CreateLayer(kATMemoryPri_HardwareOverlay, handler, 0xC0, 0x10);
	else
		mpMemLayerGTIAOverlay = mpMemMan->CreateLayer(kATMemoryPri_HardwareOverlay, handler, 0xD0, 0x01);

	mpMemMan->SetLayerName(mpMemLayerGTIAOverlay, "VBXE GTIA Overlay");
	mpMemMan->EnableLayer(mpMemLayerGTIAOverlay, kATMemoryAccessMode_CPUWrite, true);

	if (mRegBase) {
		handler.mbPassReads			= true;
		handler.mbPassAnticReads	= true;
		handler.mbPassWrites		= true;
		handler.mpThis				= this;
		handler.mpDebugReadHandler	= StaticReadControl;
		handler.mpReadHandler		= StaticReadControl;
		handler.mpWriteHandler		= StaticWriteControl;
		mpMemLayerRegisters = mpMemMan->CreateLayer(kATMemoryPri_Hardware + 1, handler, mRegBase, 0x01);
		mpMemMan->SetLayerName(mpMemLayerRegisters, "VBXE Control Registers");
		mpMemMan->EnableLayer(mpMemLayerRegisters, true);
	}
}

void ATVBXEEmulator::ShutdownMemoryMaps() {
	if (mpMemLayerGTIAOverlay) {
		mpMemMan->DeleteLayer(mpMemLayerGTIAOverlay);
		mpMemLayerGTIAOverlay = NULL;
	}

	if (mpMemLayerRegisters) {
		mpMemMan->DeleteLayer(mpMemLayerRegisters);
		mpMemLayerRegisters = NULL;
	}

	if (mpMemLayerMEMACA) {
		mpMemMan->DeleteLayer(mpMemLayerMEMACA);
		mpMemLayerMEMACA = NULL;
	}

	if (mpMemLayerMEMACB) {
		mpMemMan->DeleteLayer(mpMemLayerMEMACB);
		mpMemLayerMEMACB = NULL;
	}
}

void ATVBXEEmulator::UpdateMemoryMaps() {
	if (mb5200Mode) {
		// Window A ($D800-E7FF)
		uint8 *winA = mpMemory + (((uint32)mMemAcBankA << 12) & 0x7F000);
		mpMemMan->SetLayerMemory(mpMemLayerMEMACA, winA, 0xD8, 0x10);
		mpMemMan->EnableLayer(mpMemLayerMEMACA, true);
	} else {
		// Window B ($4000-7FFF)
		if (mMemAcBankB & 0xC0) {
			uint8 *winB = mpMemory + ((mMemAcBankB & 0x1F) << 14);
			mpMemMan->SetLayerMemory(mpMemLayerMEMACB, winB, 0x40, 0x40);

			// MEMAC-B access - CPU
			mpMemMan->EnableLayer(mpMemLayerMEMACB, kATMemoryAccessMode_CPURead, (mMemAcBankB & 0x80) != 0);
			mpMemMan->EnableLayer(mpMemLayerMEMACB, kATMemoryAccessMode_CPUWrite, (mMemAcBankB & 0x80) != 0);

			// MEMAC-B access - ANTIC
			mpMemMan->EnableLayer(mpMemLayerMEMACB, kATMemoryAccessMode_AnticRead, (mMemAcBankB & 0x40) != 0);
		} else {
			mpMemMan->EnableLayer(mpMemLayerMEMACB, false);
		}

		if ((mMemAcBankA & 0x80) && (mMemAcControl & 0x0C)) {
			static const int kPageCount[4]={
				16,		// 00 - 4K window
				32,		// 01 - 8K window
				64,		// 10 - 16K window
				128		// 11 - 32K window
			};

			int numPages = kPageCount[mMemAcControl & 3];
			int pageBase = mMemAcControl & 0xF0;

			// check for overflow -- window is truncated in this case (does not wrap to $0000)
			if (pageBase + numPages > 0x100)
				numPages = 0x100 - pageBase;

			static const uint32 kAddrMask[4]={
				0x7F000,
				0x7E000,
				0x7C000,
				0x78000
			};

			uint8 *winA = mpMemory + (((uint32)mMemAcBankA << 12) & kAddrMask[mMemAcControl & 3]);
			mpMemMan->SetLayerMemory(mpMemLayerMEMACA, winA, pageBase, numPages);

			// MEMAC-A access - CPU
			mpMemMan->EnableLayer(mpMemLayerMEMACA, kATMemoryAccessMode_CPURead, (mMemAcControl & 0x08) != 0);
			mpMemMan->EnableLayer(mpMemLayerMEMACA, kATMemoryAccessMode_CPUWrite, (mMemAcControl & 0x08) != 0);

			// MEMAC-A access - ANTIC
			mpMemMan->EnableLayer(mpMemLayerMEMACA, kATMemoryAccessMode_AnticRead, (mMemAcControl & 0x04) != 0);
		} else {
			mpMemMan->EnableLayer(mpMemLayerMEMACA, false);
		}
	}
}

void ATVBXEEmulator::BeginFrame(int correctionMode, bool signedPalette) {
	if (mpTraceChannelOverlay)
		mpTraceChannelOverlay->TruncateLastEvent(mpScheduler->GetTick64());

	mRenderPaletteCorrectionMode = correctionMode;
	if (mRenderPaletteCorrectedMode != correctionMode || mbRenderPaletteSigned != signedPalette) {
		mbRenderPaletteSigned = signedPalette;
		RecorrectPalettes();
	}

	mbXdlActive = mbXdlEnabled;
	mXdlAddr = mXdlBaseAddr;
	mXdlRepeatCounter = 1;
	mOvWidth = kOvWidth_Normal;
	mOvMode = ATVBXEOverlayMode::Disabled;

	mpPfPalette = mPalette[0];
	mpOvPalette = mPalette[1];
	mPfPaletteIndex = 0;
	mOvPaletteIndex = 1;

	mbAttrMapEnabled = false;
	mAttrWidth = 8;
	mAttrHeight = 8;
	mAttrHscroll = 0;
	mAttrVscroll = 0;
	mDMACyclesXDL = 0;
	mDMACyclesAttrMap = 0;
	mDMACyclesOverlay = 0;

	mOvHscroll = 0;
	mOvVscroll = 0;
	mOvMainPriority = 0;		// native equiv. to $FF default

	// Not reset by hardware:
	// - overlay address, step
	// - attribute map address, step
	// - character base

	mXDLLastHistoryEntry = {};
	mXDLLastHistoryEntry.mMapFieldWidth = 7;
	mXDLLastHistoryEntry.mMapFieldHeight = 7;
	mXDLLastHistoryEntry.mAttrByte = 0x11;
	mXDLLastHistoryEntry.mOvPriority = 0xFF;
	mXDLLastHistoryEntry.mChBase = (uint8)(mChAddr >> 11);
	mXDLLastHistoryEntry.mOverlayStep = mOvStep;
	mXDLLastHistoryEntry.mMapStep = mOvStep;
}

void ATVBXEEmulator::EndFrame() {
	if (mpTraceChannelOverlay)
		mpTraceChannelOverlay->TruncateLastEvent(mpScheduler->GetTick64());

	mbXdlActive = false;
	mXdlRepeatCounter = 1;
}

void ATVBXEEmulator::BeginScanline(uint32 y, uint32 *dst, const uint8 *mergeBuffer, const uint8 *anticBuffer, bool hires) {
	mpDst = dst;

	mbHiresMode = hires;
	mX = 0;
	mpMergeBuffer = mergeBuffer;
	mpMergeBuffer0 = mergeBuffer;
	mpAnticBuffer = anticBuffer;
	mpAnticBuffer0 = anticBuffer;
	mDMACyclesXDL = 0;
	mDMACyclesAttrMap = 0;
	mDMACyclesOverlay = 0;
	mDMACyclesOverlayStart = 0;
	mBlitterEndScanTime = ATSCHEDULER_GETTIME(mpScheduler) + 114;

	if (dst)
		VDMemset32(dst, mpPfPalette[mpColorTable[kColorBAK]], 68*2);

	bool reloadAttrMap = false;

	if (--mXdlRepeatCounter) {
		mOvTextRow = (mOvTextRow + 1) & 7;
	} else {
		if (!mbXdlActive) {
			mXdlRepeatCounter = 0xFFFFFFFF;
			mbAttrMapEnabled = false;
			mOvMode = ATVBXEOverlayMode::Disabled;
		} else {
			mXDLLastHistoryEntry.mXdlAddr = mXdlAddr;

			uint32 xdlStart = mXdlAddr;
			uint8 xdl1 = VBXE_FETCH(mXdlAddr++);
			uint8 xdl2 = VBXE_FETCH(mXdlAddr++);

			if (xdl1 & 4)
				mOvMode = ATVBXEOverlayMode::Disabled;
			else if (xdl1 & 3)
				mOvMode = kOvModeTable[(xdl1 & 3) - 1][(xdl2 >> 4) & 3];

			if (xdl1 & 0x10)
				mbAttrMapEnabled = false;
			else if (xdl1 & 0x08) {
				mbAttrMapEnabled = true;
				reloadAttrMap = true;
			}

			mXDLLastHistoryEntry.mOverlayMode = (uint8)mOvMode;
			mXDLLastHistoryEntry.mbMapEnabled = mbAttrMapEnabled;

			// XDLC_RPTL (1 byte)
			if (xdl1 & 0x20)
				mXdlRepeatCounter = VBXE_FETCH(mXdlAddr++);

			++mXdlRepeatCounter;

			// XDLC_OVADR (5 byte)
			if (xdl1 & 0x40) {
				uint8 ov1 = VBXE_FETCH(mXdlAddr++);
				uint8 ov2 = VBXE_FETCH(mXdlAddr++);
				uint8 ov3 = VBXE_FETCH(mXdlAddr++);
				uint8 step1 = VBXE_FETCH(mXdlAddr++);
				uint8 step2 = VBXE_FETCH(mXdlAddr++);

				mOvAddr = (uint32)ov1 + ((uint32)ov2 << 8) + ((uint32)ov3 << 16);
				mOvStep = ((uint32)step1 + ((uint32)step2 << 8)) & 0xFFF;

				// overlay addr is always updated below regardless of XDL update
				mXDLLastHistoryEntry.mOverlayStep = mOvStep;
			}

			// XDLC_OVSCRL (2 byte)
			if (xdl1 & 0x80) {
				mOvHscroll = VBXE_FETCH(mXdlAddr++) & 7;
				mOvVscroll = VBXE_FETCH(mXdlAddr++) & 7;

				mXDLLastHistoryEntry.mOvHScroll = mOvHscroll;
				mXDLLastHistoryEntry.mOvVScroll = mOvVscroll;
			}

			// XDLC_CHBASE (1 byte)
			if (xdl2 & 0x01) {
				uint8 chbase = VBXE_FETCH(mXdlAddr++);

				mChAddr = (uint32)chbase << 11;
				mXDLLastHistoryEntry.mChBase = chbase;
			}

			// XDLC_MAPADR (5 byte)
			if (xdl2 & 0x02) {
				uint8 ad1 = VBXE_FETCH(mXdlAddr++);
				uint8 ad2 = VBXE_FETCH(mXdlAddr++);
				uint8 ad3 = VBXE_FETCH(mXdlAddr++);
				uint8 step1 = VBXE_FETCH(mXdlAddr++);
				uint8 step2 = VBXE_FETCH(mXdlAddr++);

				mAttrAddr = (uint32)ad1 + ((uint32)ad2 << 8) + ((uint32)ad3 << 16);
				mAttrStep = ((uint32)step1 + ((uint32)step2 << 8)) & 0xFFF;

				reloadAttrMap = true;

				// map addr is always updated below regardless of XDL update
				mXDLLastHistoryEntry.mMapStep = mAttrStep;
			}

			// XDLC_MAPPAR (4 byte)
			if (xdl2 & 0x04) {
				mAttrHscroll = VBXE_FETCH(mXdlAddr++) & 0x1F;
				mAttrVscroll = VBXE_FETCH(mXdlAddr++) & 0x1F;
				const uint8 widthCode = VBXE_FETCH(mXdlAddr++) & 0x1F;
				const uint8 heightCode = VBXE_FETCH(mXdlAddr++) & 0x1F;

				mAttrWidth = widthCode + 1;
				mAttrHeight = heightCode + 1;

				mXDLLastHistoryEntry.mMapHScroll = mAttrHscroll;
				mXDLLastHistoryEntry.mMapHScroll = mAttrVscroll;
				mXDLLastHistoryEntry.mMapFieldWidth = widthCode;
				mXDLLastHistoryEntry.mMapFieldHeight = heightCode;
			}

			// XDLC_ATT (2 byte)
			if (xdl2 & 0x08) {
				uint8 ctl = VBXE_FETCH(mXdlAddr++);
				uint8 pri = VBXE_FETCH(mXdlAddr++);

				mOvWidth = ((ctl & 3) == 3) ? kOvWidth_Narrow : (OvWidth)(ctl & 3);
				mPfPaletteIndex = ctl >> 6;
				mOvPaletteIndex = (ctl >> 4) & 3;
				mpPfPalette = mPalette[mPfPaletteIndex];
				mpOvPalette = mPalette[mOvPaletteIndex];

				mOvMainPriority = ConvertPriorityToNative(pri);

				mXDLLastHistoryEntry.mAttrByte = ctl;
				mXDLLastHistoryEntry.mOvPriority = pri;
			}

			// XDLC_END
			if (xdl2 & 0x80)
				mbXdlActive = false;

			mOvTextRow = mOvVscroll & 7;

			if (reloadAttrMap)
				mAttrRow = mAttrVscroll;

			// deduct XDL cycles
			mDMACyclesXDL = (mXdlAddr - xdlStart);

			if (mpTraceChannelOverlay && mOvMode != ATVBXEOverlayMode::Disabled) {
				static constexpr const wchar_t *kTraceOvModes[4][3]={
					{ L"LR128", L"LR160", L"LR168" },
					{ L"SR256", L"SR320", L"SR336" },
					{ L"HR512", L"HR640", L"HR672" },
					{ L"Narrow Text", L"Normal Text", L"Wide Text" },
				};

				const uint64 t = mpScheduler->GetTick64();
				mpTraceChannelOverlay->AddTickEvent(t, t + 114 * mXdlRepeatCounter, kTraceOvModes[(int)mOvMode - 1][mOvWidth], kATTraceColor_Default);
			}
		}
	}

	// update XDL history
	if (y >= 8 && y < 248) {
		mXDLLastHistoryEntry.mbXdlActive = mbXdlActive;
		mXDLLastHistoryEntry.mOverlayAddr = mOvAddr;
		mXDLLastHistoryEntry.mMapAddr = mAttrAddr;
		mXDLHistory[y - 8] = mXDLLastHistoryEntry;
	}

	// deduct attribute map cycles
	if (mbAttrMapEnabled && (reloadAttrMap || mAttrRow == 0)) {
		static const uint32 kAttrMapWidth[3]={
			256,
			320,
			336
		};

		// VBXE always reads 43 attribute map cells regardless of cell width
		const uint32 toRead = 43 * 4;
		mDMACyclesAttrMap += toRead;

		// read the attribute map cells
		const uint32 offset = mAttrAddr & 0x7FFFF;
		size_t split = 0x80000 - offset;

		if (split < toRead) {
			memcpy(mAttrBuffer, &mpMemory[offset], split);
			memcpy(mAttrBuffer + split, &mpMemory[0], toRead - split);
		} else {
			memcpy(mAttrBuffer, &mpMemory[offset], toRead);
		}
	}

	// deduct overlay map cycles
	static constexpr uint32 kOvCyclesPerMode[5][3]={
		{ 0, 0, 0 },
		{ 128, 160, 168 },
		{ 256, 320, 336 },
		{ 256, 320, 336 },
		{ 195, 243, 255 },
	};

	static constexpr uint32 kOvDMAStart[3]={
		10, 18, 26
	};

	mDMACyclesOverlay = kOvCyclesPerMode[(int)mOvMode][mOvWidth];
	mDMACyclesOverlayStart = kOvDMAStart[mOvWidth];

	VDASSERT(mDMACyclesXDL + mDMACyclesOverlay + mDMACyclesAttrMap < 114 * 8);

	// Credit remaining cycles to the blitter and run it now so we can figure out when the
	// blitter ends.
	mBlitCyclesLeft += (8 * 114) - (mDMACyclesXDL + mDMACyclesAttrMap + mDMACyclesOverlay);

	if (mBlitCyclesLeft > 8*114)
		mBlitCyclesLeft = 8*114;

	RunBlitter();
}

void ATVBXEEmulator::RenderScanline(int xend, bool pfpmrendered) {
	int x1 = mX;

	if (x1 >= xend)
		return;

	// render spans and process register changes
	do {
		int x2 = xend;

		if (mRCIndex < mRCCount) {
			const RegisterChange *rc0 = &mRegisterChanges[mRCIndex];
			const RegisterChange *rc = rc0;
			do {
				int xchg = rc->mPos;
				if (xchg > x1) {
					if (x2 > xchg)
						x2 = xchg;
					break;
				}

				++rc;
			} while(++mRCIndex < mRCCount);

			UpdateRegisters(rc0, (int)(rc - rc0));
		}

		// render out attpixels
		int x1h = x1 * 2;
		int x2h = x2 * 2;

		while(x1h < x2h) {
			int xth = x2h;

			if (mbAttrMapEnabled)
				xth = RenderAttrPixels(x1h, x2h);
			else
				RenderAttrDefaultPixels(x1h, x2h);

			VDASSERT(xth > x1h);

			// 40 column mode is set by ANTIC during horizontal blank if ANTIC modes 2, 3, or
			// F are used. 40 column mode has the following effects:
			//
			//	* The priority logic always sees PF2.
			//	* The collision logic sees either BAK or PF2. Adjacent bits are ORed each color
			//	  clock to determine this (PF2C in schematic).
			//	* The playfield bits are used instead to substitute the luminance of PF1 on top
			//	  of the priority logic output. This happens even if players have priority.
			//
			// The flip-flip in the GTIA that controls 40 column mode can only be set by the
			// horizontal sync command, but can be reset at any time whenever either of the
			// top two bits of PRIOR are set. If this happens, the GTIA will begin interpreting
			// AN0-AN2 in lores mode, but ANTIC will continue sending in hires mode. The result
			// is that the bit pair patterns 00-11 produce PF0-PF3 instead of BAK + PF0-PF2 as
			// usual.

			bool hiresMode = mbHiresMode;
			bool revMode = false;

			if (mAttrPixels[x1h].mCtrl & 0x04) {
				revMode = true;
				hiresMode = !hiresMode;

				const int x1l = x1h >> 1;
				const int xtl = (xth + 1) >> 1;

				if (hiresMode) {

					static const uint8 kPriTable[8]={
						0,		// BAK
						1,		// PF0
						2,		// PF1
						2,		// PF01
						3,		// PF2
						3,		// PF02
						3,		// PF12
						3,		// PF012
					};

					for(int x = x1l; x < xtl; ++x)
						mTempAnticData[x] = kPriTable[mpMergeBuffer0[x] & 7];

					for(int x = x1l; x < xtl; ++x)
						mTempMergeBuffer[x] = (mpMergeBuffer0[x] & (P0|P1|P2|P3)) | PF2;

					mpAnticBuffer = mTempAnticData;
					mpMergeBuffer = mTempMergeBuffer;
				} else {
					for(int x = x1l; x < xtl; ++x) {
						uint8 d = mpMergeBuffer0[x];

						if (d & PF2) {
							uint8 c = mpAnticBuffer0[x];
							d = (d & ~PF) | (1 << c);
						}

						// MERGE NOTE: pixels without PF2 still need their original
						// background/player bits; never reuse an earlier span's data.
						mTempMergeBuffer[x] = d;
					}

					mpMergeBuffer = mTempMergeBuffer;
				}
			}

			// If we're starting on an odd pixel, back off one pixel and save off the previous
			// pixel, then restore it afterward. This simplifies the rendering code by allowing
			// it to always render pixel pairs.
			uint32 saveColor0 = 0;
			uint32 saveColor1 = 0;
			uint8 savePri0 = 0;
			uint8 savePri1 = 0;
			bool oddRestore = false;

			if (x1h & 1) {
				--x1h;

				saveColor0 = mpDst[x1h*2 + 0];
				saveColor1 = mpDst[x1h*2 + 1];
				savePri0 = mOvPriDecode[x1h*2 + 0];
				savePri1 = mOvPriDecode[x1h*2 + 1];

				oddRestore = true;
			}

			switch(mPRIOR & 0xc0) {
				case 0x00:
					if (hiresMode) {
						if (mbExtendedColor)
							Render<false, RenderMode::HiresXcolor>(x1h, xth, mbAttrMapEnabled);
						else
							Render<false, RenderMode::Hires>(x1h, xth, mbAttrMapEnabled);
					} else if (pfpmrendered)
						Render<false, RenderMode::Lores>(x1h, xth, mbAttrMapEnabled);
					else
						Render<true, RenderMode::Lores>(x1h, xth, mbAttrMapEnabled);
					break;

				case 0x40:
					Render<false, RenderMode::Mode9>(x1h, xth, mbAttrMapEnabled);
					break;

				case 0x80:
					Render<false, RenderMode::Mode10>(x1h, xth, mbAttrMapEnabled);
					break;

				case 0xC0:
					Render<false, RenderMode::Mode11>(x1h, xth, mbAttrMapEnabled);
					break;
			}

			if (oddRestore) {
				mpDst[x1h*2 + 0] = saveColor0;
				mpDst[x1h*2 + 1] = saveColor1;
				mOvPriDecode[x1h*2 + 0] = savePri0;
				mOvPriDecode[x1h*2 + 1] = savePri1;
			}

			if (revMode) {
				mpAnticBuffer = mpAnticBuffer0;
				mpMergeBuffer = mpMergeBuffer0;
			}

			x1h = xth;
		}

		if (mOvCollMask)
			RenderOverlay<true>(x1, x2);
		else
			RenderOverlay<false>(x1, x2);

		x1 = x2;
	} while(x1 < xend);

	mX = x1;
}

uint32 ATVBXEEmulator::SenseScanline(int hpx1, int hpx2, const uint8 *weights) const {
	if (!mpDst)
		return 0;

	// clip to visible range
	int chpx1 = std::max<int>(hpx1, 68);
	int chpx2 = std::min<int>(hpx2, 444);

	if (chpx1 >= chpx2)
		return 0;

	weights += (chpx1 - hpx1);

	// compute dot product against luma of pixel pairs
	const uint32 *src = &mpDst[hpx1 * 2];
	size_t n = (size_t)(chpx2 - chpx1);
	uint32 sum = 0;

	for(size_t i=0; i<n; ++i) {
		uint32 luma = ((src[0] >> 24) + (src[1] >> 24) + 1) >> 1;
		src += 2;

		sum += luma * luma * (*weights++);
	}

	return sum;
}

void ATVBXEEmulator::EndScanline() {
	if (mpDst) {
		VDMemset32(mpDst + 444*2, mpPfPalette[mpColorTable[8]], (456 - 444) * 2);
		mpDst = NULL;
	}

	// commit any outstanding register changes
	if (mRCIndex < mRCCount)
		UpdateRegisters(&mRegisterChanges[mRCIndex], mRCCount - mRCIndex);

	mRCCount = 0;
	mRCIndex = 0;
	mRegisterChanges.clear();

	// update overlay address
	if (mOvMode != ATVBXEOverlayMode::Disabled) {
		if (mOvMode != ATVBXEOverlayMode::Text || mXdlRepeatCounter == 1 || mOvTextRow == 7)
			mOvAddr += mOvStep;
	}

	if (mbAttrMapEnabled) {
		mAttrRow = (mAttrRow + 1) & 31;

		if (mAttrRow == mAttrHeight) {
			mAttrRow = 0;
			mAttrAddr += mAttrStep;
		}
	}
}

void ATVBXEEmulator::AddRegisterChange(uint8 pos, uint8 addr, uint8 value) {
	RegisterChanges::iterator it(mRegisterChanges.end()), itBegin(mRegisterChanges.begin());

	while(it != itBegin && it[-1].mPos > pos)
		--it;

	RegisterChange change;
	change.mPos = pos;
	change.mReg = addr;
	change.mValue = value;
	change.mPad = 0;
	mRegisterChanges.insert(it, change);

	++mRCCount;
}

void ATVBXEEmulator::SetRegisterImmediate(uint8 addr, uint8 value) {
	RegisterChange change;
	change.mPos = 0;
	change.mReg = addr;
	change.mValue = value;
	change.mPad = 0;

	UpdateRegisters(&change, 1);
}

void ATVBXEEmulator::OnScheduledEvent(uint32 id) {
	mpEventBlitterIrq = nullptr;

	AssertBlitterIrq();
}

bool ATVBXEEmulator::IsBlitterActive() const {
	if (mBlitState == BlitState::Stopped)
		return false;

	if (mBlitState == BlitState::Stopping)
		return ATWrapTime{ATSCHEDULER_GETTIME(mpScheduler)} < mBlitterStopTime;

	// blitter is only 'active' if it is actually running a bit, not while
	// reading in a new BCB
	return mBlitState == BlitState::ProcessBlit;
}

void ATVBXEEmulator::AssertBlitterIrq() {
	mBlitState = BlitState::Stopped;

	if (!mbIRQRequest) {
		mbIRQRequest = true;

		if (mbIRQEnabled)
			mpIRQController->Assert(kATIRQSource_VBXE, true);
	}
}

void ATVBXEEmulator::UpdateRegisters(const RegisterChange *rc, int count) {
	while(count--) {
		// process register change
		uint8 value = rc->mValue;

		switch(rc->mReg) {
		case 0x12:
			mColorTableExt[kColorP0] = value;
			mColorTableExt[kColorP0P1] = value | mColorTableExt[kColorP1];

			value &= 0xfe;
			mColorTable[kColorP0] = value;
			mColorTable[kColorP0P1] = value | mColorTable[kColorP1];
			break;
		case 0x13:
			mColorTableExt[kColorP1] = value;
			mColorTableExt[kColorP0P1] = mColorTableExt[kColorP0] | value;

			value &= 0xfe;
			mColorTable[kColorP1] = value;
			mColorTable[kColorP0P1] = mColorTable[kColorP0] | value;
			break;
		case 0x14:
			mColorTableExt[kColorP2] = value;
			mColorTableExt[kColorP2P3] = value | mColorTableExt[kColorP3];
			mColorTableExt[kColorPF3P2] = mColorTableExt[kColorPF3] | value;
			mColorTableExt[kColorPF3P2P3] = mColorTableExt[kColorPF3P3] | value;

			value &= 0xfe;
			mColorTable[kColorP2] = value;
			mColorTable[kColorP2P3] = value | mColorTable[kColorP3];
			mColorTable[kColorPF3P2] = mColorTable[kColorPF3] | value;
			mColorTable[kColorPF3P2P3] = mColorTable[kColorPF3P3] | value;
			break;
		case 0x15:
			mColorTableExt[kColorP3] = value;
			mColorTableExt[kColorP2P3] = mColorTableExt[kColorP2] | value;
			mColorTableExt[kColorPF3P3] = mColorTableExt[kColorPF3] | value;
			mColorTableExt[kColorPF3P2P3] = mColorTableExt[kColorPF3P2] | value;

			value &= 0xfe;
			mColorTable[kColorP3] = value;
			mColorTable[kColorP2P3] = mColorTable[kColorP2] | value;
			mColorTable[kColorPF3P3] = mColorTable[kColorPF3] | value;
			mColorTable[kColorPF3P2P3] = mColorTable[kColorPF3P2] | value;
			break;
		case 0x16:
			mColorTableExt[kColorPF0] = value;
			value &= 0xfe;
			mColorTable[kColorPF0] = value;
			break;
		case 0x17:
			mColorTableExt[kColorPF1] = value;
			value &= 0xfe;
			mColorTable[kColorPF1] = value;
			break;
		case 0x18:
			mColorTableExt[kColorPF2] = value;
			value &= 0xfe;
			mColorTable[kColorPF2] = value;
			break;
		case 0x19:
			mColorTableExt[kColorPF3] = value;
			mColorTableExt[kColorPF3P2] = value | mColorTableExt[kColorP2];
			mColorTableExt[kColorPF3P3] = value | mColorTableExt[kColorP3];
			mColorTableExt[kColorPF3P2P3] = value | mColorTableExt[kColorP2P3];

			value &= 0xfe;
			mColorTable[kColorPF3] = value;
			mColorTable[kColorPF3P2] = value | mColorTable[kColorP2];
			mColorTable[kColorPF3P3] = value | mColorTable[kColorP3];
			mColorTable[kColorPF3P2P3] = value | mColorTable[kColorP2P3];
			break;
		case 0x1A:
			mColorTableExt[kColorBAK] = value;

			value &= 0xfe;
			mColorTable[kColorBAK] = value;
			break;
		case 0x1B:
			mPRIOR = value;
			mpPriTable = mPriorityTables[(value & 15) + (value&32 ? 16 : 0)];
			mpPriTableHi = mPriorityTablesHi[(value & 15) + (value&32 ? 16 : 0)];

			if (value & 0xC0)
				mbHiresMode = false;
			break;
		}

		++rc;
	}
}

int ATVBXEEmulator::RenderAttrPixels(int x1h, int x2h) {
	// x1 and x2 are measured in color clocks.
	static const int kBounds[3][2]={
		// Narrow: $40-BF
		{ 64*2, 192*2 },

		// Normal: $30-CF
		{ 48*2, 208*2 },

		// Wide: $2C-D4 (NOTE: This is different from ANTIC!)
		{ 44*2, 212*2 },
	};

	const int x1h0 = x1h;
	const int x2h0 = x2h;
	int xlh = kBounds[mOvWidth][0];
	int xrh = kBounds[mOvWidth][1];

	if (x1h < xlh) {
		if (x2h <= xlh) {
			RenderAttrDefaultPixels(x1h, x2h);
			return x2h;
		}

		RenderAttrDefaultPixels(x1h, xlh);
		x1h = xlh;
	}

	// If the hscroll value is >= cell width, then it wraps around the whole
	// 5-bit field, similarly to the ANTIC delta counter. Thus, we need to
	// detect this case and replace it with a negative offset.
	const int effectiveHscroll = (int)mAttrHscroll + (mAttrHscroll >= mAttrWidth ? -32 : 0);

	// attribute map fetch is constrained to 172 bytes (43 cells)
	if (x2h > xrh) {
		if (x1h >= xrh) {
			RenderAttrDefaultPixels(x1h, x2h);
			return x2h;
		}

		x2h = xrh;
	}

	if (x2h <= x1h)
		return x1h;

	// Compute horizontal offset into the attribute map.
	const int attrOffset = (x1h - xlh) + effectiveHscroll;
	uint32 attrIndex = 0;

	int offset = attrOffset;

	if (offset >= 0) {
		offset %= mAttrWidth;
		attrIndex = attrOffset / mAttrWidth;
	}

	// The hires bitmap is 1 bit/px for widths 1-8, 2 bit/px for 9-16, and 4 bit/px for 17-32.
	const int hiresShift = mAttrWidth > 16 ? 2 : mAttrWidth > 8 ? 1 : 0;

	const uint8 colorMask = mbExtendedColor ? 0xFF : 0xFE;

	AttrPixel px;
	px.mColors[0] = 0;

	const uint8 *attrSrc = &mAttrBuffer[(attrIndex++ & 63)*4];
	uint8 hrMaskOrPF0 = attrSrc[0];
	px.mColors[1] = hrMaskOrPF0 & colorMask;
	px.mColors[2] = attrSrc[1] & colorMask;
	px.mColors[3] = attrSrc[2] & colorMask;
	px.mCtrl = attrSrc[3];
	px.mPriority = mOvPriority[px.mCtrl & 3];

	const uint8 resBit = px.mCtrl;

	// if we have already rendered a default pixel, stop immediately at the left
	// border if the attribute map starts with a RES swap
	if (x1h > x1h0 && resBit)
		return x1h;

	do {
		px.mHiresFlag = (sint8)(hrMaskOrPF0 << (((unsigned)offset >> hiresShift) & 7)) >> 7;
		mAttrPixels[x1h] = px;

		if (++offset >= (int)mAttrWidth) {
			attrSrc = &mAttrBuffer[(attrIndex++ & 63)*4];
			hrMaskOrPF0 = attrSrc[0];
			px.mColors[1] = hrMaskOrPF0 & colorMask;
			px.mColors[2] = attrSrc[1] & colorMask;
			px.mColors[3] = attrSrc[2] & colorMask;
			px.mCtrl = attrSrc[3];
			px.mPriority = mOvPriority[px.mCtrl & 3];
			offset = 0;

			// force break in span rendering if RES bit is set
			if ((px.mCtrl ^ resBit) & 0x04)
				return x1h + 1;
		}
	} while(++x1h < x2h);

	if (x2h < x2h0) {
		// If the last in-range pixel had the resolution swap bit set, we must
		// force a stop at the border, same as if the transition had happened
		// within visible range.
		if (resBit)
			return x2h;

		RenderAttrDefaultPixels(x2h, x2h0);
		x2h = x2h0;
	}

	return x2h;
}

void ATVBXEEmulator::RenderAttrDefaultPixels(int x1h, int x2h) {
	const AttrPixel px = {
		0,
		mpColorTable[kColorPF0],
		mpColorTable[kColorPF1],
		mpColorTable[kColorPF2],
		(uint8)((mPfPaletteIndex << 6) + (mOvPaletteIndex << 4)),
		0,
		mOvMainPriority
	};

	for(int x = x1h; x < x2h; ++x)
		mAttrPixels[x] = px;
}

////////////////////////////////////////////////////////////////////////////////

template<bool T_Blank, ATVBXEEmulator::RenderMode T_Mode>
void ATVBXEEmulator::Render(int _x1h, int _x2h, bool _attrMapEnabled) {
	// MERGE NOTE: use the portable attribute spelling for GCC/Clang.
	auto run = [this](uint32 *VDRESTRICT dst, int x1h, int x2h, auto v126, auto attrMapEnabledConst) VDNOINLINE {
		constexpr bool T_AttrMapEnabled = attrMapEnabledConst;
		constexpr bool T_Hires = T_Mode == RenderMode::Hires
			|| T_Mode == RenderMode::HiresXcolor;
		constexpr bool T_Version126 = v126;

		const uint8 *VDRESTRICT colorTable = mpColorTable;
		// MERGE NOTE: preserve the hires PF0 handling from RenderMode8.
		const auto *VDRESTRICT priTable = T_Hires ? mpPriTableHi : mpPriTable;

		uint8 *VDRESTRICT priDst = mOvPriDecode + x1h * 2;
		const uint8 *VDRESTRICT src = mpMergeBuffer + (x1h >> 1);
		const uint8 *VDRESTRICT lumasrc = &mpAnticBuffer[x1h >> 1];

		const AttrPixel *VDRESTRICT apx = &mAttrPixels[x1h];

		const auto& VDRESTRICT palette = mPalette;

		uint8 i0 = 0;

		[[maybe_unused]] AttrPixel apx0[2];
		if constexpr (!T_AttrMapEnabled) {
			apx0[0] = apx[0];
			apx0[1] = apx[0];

			apx = apx0;
		}

		int w = (x2h - x1h + 1) >> 1;
		[[maybe_unused]] int x1 = x1h >> 1;

		do {
			if constexpr (!T_Blank) {
				i0 = *src++;
			}

			// MERGE NOTE: the old mode 9/11 renderers masked PF0-PF2
			// before both color and collision lookup. Preserve that gating.
			if constexpr (T_Mode == RenderMode::Mode9 || T_Mode == RenderMode::Mode11)
				i0 &= P0 | P1 | P2 | P3 | PF3;

			// mode 10 (9 colors)
			//
			// This mode works by using AN0-AN1 to trigger either the playfield or the player/missle
			// bits going into the priority logic. This means that when player colors are used, the
			// playfield takes the same priority as that player. Playfield collisions are triggered
			// only for PF0-PF3; P0-P3 colors coming from the playfield do not trigger collisions.
			if constexpr (T_Mode == RenderMode::Mode10) {
				// MERGE NOTE: keep lambda-local tables compatible with GCC 12.
				constexpr uint8 kMode10Lookup[16]={
					P0,		P1,		P2,		P3,
					PF0,	PF1,	PF2,	PF3,
					0,		0,		0,		0,
					PF0,	PF1,	PF2,	PF3
				};

				const uint8 *VDRESTRICT l2src = &mpAnticBuffer[(x1++ - 1) & ~1];
				const uint8 code = l2src[0]*4 + l2src[1];

				i0 = kMode10Lookup[code] | (i0 & 0xf8);
			}

			uint8 i1 = i0;
			uint8 ic0 = i0;
			uint8 ic1 = i1;

			[[maybe_unused]] uint8 lb;
			if constexpr (T_Hires) {
				// For V1.26+, use PF1 priority for set pixels.
				// For V1.25-, use PF2/PF3 priority for all pixels.
				lb = *lumasrc++;

				if constexpr (T_Mode == RenderMode::HiresXcolor) {
					// if hires bit is set, switch collision layer from PF2 to PF1
					if (lb & 2)
						ic0 -= (ic0 & PF2) >> 1;

					if (lb & 1)
						ic1 -= (ic1 & PF2) >> 1;

					// for non-set pixels, switch PF2 to PF3 based on hires mask
					ic0 += (ic0 & PF2) & apx[0].mHiresFlag;
					ic1 += (ic1 & PF2) & apx[1].mHiresFlag;

					if (T_Version126) {
						// for V1.26+, both collision and display use PF1/PF2/PF3
						i0 = ic0;
						i1 = ic1;
					} else {
						// for V1.25-, collision uses PF2/PF3 but not PF1
						i0 += (i0 & PF2) & apx[0].mHiresFlag;
						i1 += (i1 & PF2) & apx[1].mHiresFlag;
					}
				} else {
					i0 += (i0 & PF2) & apx[0].mHiresFlag;
					i1 += (i1 & PF2) & apx[1].mHiresFlag;
					// MERGE NOTE: color lookup must see the same PF2/PF3 swap.
					ic0 = i0;
					ic1 = i1;
				}
			}

			uint8 a0 = priTable[ic0].mMapColor;
			uint8 b0 = priTable[ic0].mPalColor;
			uint8 c0 = colorTable[b0];

			uint8 d0 = apx[0].mColors[a0] | c0;
			uint8 d1;
			
			if constexpr (T_Hires) {
				uint8 a1 = priTable[ic1].mMapColor;
				uint8 b1 = priTable[ic1].mPalColor;
				uint8 c1 = colorTable[b1];

				d1 = apx[1].mColors[a1] | c1;
			} else {
				d1 = apx[1].mColors[a0] | c0;
			}

			// if hires non-xcolor is active, apply PF1 luma to color when
			// hires pixels are active
			if constexpr (T_Mode == RenderMode::Hires) {
				if (lb & 2)
					d0 = (d0 & 0xf0) + (apx[0].mColors[2] & 0x0f);

				if (lb & 1)
					d1 = (d1 & 0xf0) + (apx[1].mColors[2] & 0x0f);
			}

			// mode 9 (1 color / 16 luma)
			//
			// In this mode, PF0-PF3 are forced off, so no playfield collisions ever register
			// and the playfield always registers as the background color. Luminance is
			// ORed in after the priority logic, but its substitution is gated by all P/M bits
			// and so it does not affect players or missiles. It does, however, affect PF3 if
			// the fifth player is enabled.
			if constexpr (T_Mode == RenderMode::Mode9) {
				constexpr uint8 kPlayerMaskLookup[16]={0xff};

				const uint8 *VDRESTRICT l2src = &mpAnticBuffer[x1++ & ~1];
				const uint8 luma = ((l2src[0] << 2) + l2src[1]) & kPlayerMaskLookup[i0 >> 4];

				d0 |= luma;
				d1 |= luma;
			}

			// 16 colors / 1 luma
			//
			// In this mode, PF0-PF3 are forced off, so no playfield collisions ever register
			// and the playfield always registers as the background color. Chroma is
			// ORed in after the priority logic, but its substitution is gated by all P/M bits
			// and so it does not affect players or missiles. It does, however, affect PF3 if
			// the fifth player is enabled.
			if constexpr (T_Mode == RenderMode::Mode11) {
				static const uint8 kMode11Lookup[16][2][2]={
					{{0xff,0xff},{0xff,0xf0}},
					{{0x00,0xff},{0x00,0xff}},
					{{0x00,0xff},{0x00,0xff}},
					{{0x00,0xff},{0x00,0xff}},
					{{0x00,0xff},{0x00,0xff}},
					{{0x00,0xff},{0x00,0xff}},
					{{0x00,0xff},{0x00,0xff}},
					{{0x00,0xff},{0x00,0xff}},
					{{0x00,0xff},{0x00,0xff}},
					{{0x00,0xff},{0x00,0xff}},
					{{0x00,0xff},{0x00,0xff}},
					{{0x00,0xff},{0x00,0xff}},
					{{0x00,0xff},{0x00,0xff}},
					{{0x00,0xff},{0x00,0xff}},
					{{0x00,0xff},{0x00,0xff}},
					{{0x00,0xff},{0x00,0xff}}
				};

				const uint8 *VDRESTRICT l2src = &mpAnticBuffer[x1++ & ~1];
				uint8 l0 = (l2src[0] << 6) + (l2src[1] << 4);

				const uint8 (&colorInfo)[2] = kMode11Lookup[i0 >> 4][l0 == 0 && T_Version126];

				d0 = (d0 | (l0 & colorInfo[0])) & colorInfo[1];
				d1 = (d1 | (l0 & colorInfo[0])) & colorInfo[1];
			}

			dst[0] = dst[1] = palette[apx[0].mCtrl >> 6][d0];
			dst[2] = dst[3] = palette[apx[1].mCtrl >> 6][d1];

			priDst[0] = apx[0].mPriority & priTable[i0].mOvPri;
			priDst[2] = apx[1].mPriority & priTable[i1].mOvPri;

			if constexpr (T_Hires) {
				if (T_Version126) {
					priDst[1] = (kPriorityTranslation.v[i0] & 0xF7) | (apx[0].mCtrl & 0x08);
					priDst[3] = (kPriorityTranslation.v[i1] & 0xF7) | (apx[1].mCtrl & 0x08);
				} else {
					priDst[1] = kCollisionLookup.v[i0] | (apx[0].mCtrl & 0x08);
					priDst[3] = kCollisionLookup.v[i1] | (apx[1].mCtrl & 0x08);
				}
			} else {
				if (T_Version126) {
					const uint8 pri = kPriorityTranslation.v[i0];
					priDst[1] = (pri & 0xF7) | (apx[0].mCtrl & 0x08);
					priDst[3] = (pri & 0xF7) | (apx[1].mCtrl & 0x08);
				} else {
					const uint8 coll = kCollisionLookup.v[i0];
					priDst[1] = coll | (apx[0].mCtrl & 0x08);
					priDst[3] = coll | (apx[1].mCtrl & 0x08);
				}
			}

			if constexpr (T_AttrMapEnabled) {
				apx += 2;
			}

			dst += 4;
			priDst += 4;
		} while(--w);
	};

	uint32 *dst = mpDst + _x1h*2;

	if (mbVersion126) {
		if (_attrMapEnabled)
			run(dst, _x1h, _x2h, std::true_type(), std::true_type());
		else
			run(dst, _x1h, _x2h, std::true_type(), std::false_type());
	} else {
		if (_attrMapEnabled)
			run(dst, _x1h, _x2h, std::false_type(), std::true_type());
		else
			run(dst, _x1h, _x2h, std::false_type(), std::false_type());
	}
}

template<bool T_EnableCollisions>
void ATVBXEEmulator::RenderOverlay(int x1, int x2) {
	// x1 and x2 are measured in color clocks.
	static const int kBounds[3][2]={
		// Narrow: $40-BF
		{ 64, 192 },

		// Normal: $30-CF
		{ 48, 208 },

		// Wide: $2C-D4 (NOTE: This is different from ANTIC!)
		{ 44, 212 },
	};

	uint32 hscroll = mOvMode == ATVBXEOverlayMode::Text ? mOvHscroll : 0;
	int xl = kBounds[mOvWidth][0];
	int xr = kBounds[mOvWidth][1];

	// Note that we need to fetch and process an additional 8 HR pixels
	// (2 color clocks) for scrolled text modes. This includes extending
	// the right fetch border and fetching two color clocks ahead. Bitmap
	// modes don't scroll and don't need this.
	int xr2 = xr;

	int x1f = x1;
	int x2f = x2;

	if (hscroll) {
		xr2 += 2;
		x1f += 2;
		x2f += 2;
	}

	if (x1f < xl)
		x1f = xl;

	if (x2f > xr2)
		x2f = xr2;

	if (x2f > x1f) {
		switch(mOvMode) {
			case ATVBXEOverlayMode::Disabled:
				return;

			case ATVBXEOverlayMode::LR:
				RenderOverlayLR(mOverlayDecode + x1f*4, x1f - xl, x2f - x1f);
				break;

			case ATVBXEOverlayMode::SR:
				RenderOverlaySR(mOverlayDecode + x1f*4, x1f - xl, x2f - x1f);
				break;

			case ATVBXEOverlayMode::HR:
				RenderOverlayHR(mOverlayDecode + x1f*4, x1f - xl, x2f - x1f);
				break;

			case ATVBXEOverlayMode::Text:
				RenderOverlay80Text(mOverlayDecode + x1f*4, xl, x1f - xl, x2f - x1f);
				break;
		}
	}

	int x1h = x1;
	int x2h = x2;

	if (x1h < xl)
		x1h = xl;

	if (x2h > xr)
		x2h = xr;

	x1h += x1h;
	x2h += x2h;

	const uint8 collMask = mOvCollMask;
	uint8 collState = mOvCollState;

	const uint8 *VDRESTRICT dec = &mOverlayDecode[x1h*2 + hscroll];
	uint32 *VDRESTRICT dst = mpDst + x1h * 2;
	const AttrPixel *VDRESTRICT apx = &mAttrPixels[x1h];
	const uint8 *VDRESTRICT prisrc = &mOvPriDecode[x1h * 2];
	if (mbOvTrans) {
		if (mOvMode == ATVBXEOverlayMode::Text) {
			const uint8 *ovpri = &mOvTextTrans[x1h * 2 + hscroll];

			if (mbOvTrans15) {
				for(int xh = x1h; xh < x2h; ++xh) {
					const uint8 pri = prisrc[0];

					if (!pri) {
						uint8 v0 = dec[0];
						uint8 v1 = dec[1];

						if (ovpri[0] && (v0 & 15) != 15) {
							dst[0] = mPalette[(apx[0].mCtrl >> 4) & 3][v0];

							if constexpr (T_EnableCollisions) {
								if (collMask & (1 << (v0 >> 5)))
									collState |= prisrc[1];
							}
						}

						if (ovpri[1] && (v1 & 15) != 15) {
							dst[1] = mPalette[(apx[0].mCtrl >> 4) & 3][v1];

							if constexpr (T_EnableCollisions) {
								if (collMask & (1 << (v1 >> 5)))
									collState |= prisrc[1];
							}
						}
					}

					prisrc += 2;
					dec += 2;
					++apx;
					dst += 2;
					ovpri += 2;
				}
			} else {
				for(int xh = x1h; xh < x2h; ++xh) {
					const uint8 pri = prisrc[0];

					if (!pri) {
						uint8 v0 = dec[0];
						uint8 v1 = dec[1];

						if (ovpri[0]) {
							dst[0] = mPalette[(apx[0].mCtrl >> 4) & 3][v0];

							if constexpr (T_EnableCollisions) {
								if (collMask & (1 << (v0 >> 5)))
									collState |= prisrc[1];
							}
						}

						if (ovpri[1]) {
							dst[1] = mPalette[(apx[0].mCtrl >> 4) & 3][v1];

							if constexpr (T_EnableCollisions) {
								if (collMask & (1 << (v1 >> 5)))
									collState |= prisrc[1];
							}
						}
					}

					prisrc += 2;
					dec += 2;
					++apx;
					dst += 2;
					ovpri += 2;
				}
			}
		} else {
			if (mbOvTrans15) {
				for(int xh = x1h; xh < x2h; ++xh) {
					const uint8 pri = prisrc[0];

					if (!pri) {
						uint8 v0 = dec[0];
						uint8 v1 = dec[1];

						if (v0 && (v0 & 15) != 15) {
							dst[0] = mPalette[(apx[0].mCtrl >> 4) & 3][v0];

							if constexpr (T_EnableCollisions) {
								if (collMask & (1 << (v0 >> 5)))
									collState |= prisrc[1];
							}
						}

						if (v1 && (v1 & 15) != 15) {
							dst[1] = mPalette[(apx[0].mCtrl >> 4) & 3][v1];

							if constexpr (T_EnableCollisions) {
								if (collMask & (1 << (v1 >> 5)))
									collState |= prisrc[1];
							}
						}
					}

					prisrc += 2;
					dec += 2;
					++apx;
					dst += 2;
				}
			} else {
				for(int xh = x1h; xh < x2h; ++xh) {
					const uint8 pri = prisrc[0];

					if (!pri) {
						uint8 v0 = dec[0];
						uint8 v1 = dec[1];

						if (v0) {
							dst[0] = mPalette[(apx[0].mCtrl >> 4) & 3][v0];

							if constexpr (T_EnableCollisions) {
								if (collMask & (1 << (v0 >> 5)))
									collState |= prisrc[1];
							}
						}

						if (v1) {
							dst[1] = mPalette[(apx[0].mCtrl >> 4) & 3][v1];

							if constexpr (T_EnableCollisions) {
								if (collMask & (1 << (v1 >> 5)))
									collState |= prisrc[1];
							}
						}
					}

					prisrc += 2;
					dec += 2;
					++apx;
					dst += 2;
				}
			}
		}
	} else {
		for(int xh = x1h; xh < x2h; ++xh) {
			const uint8 pri = prisrc[0];
			prisrc += 2;

			if (T_EnableCollisions) {
				const uint8 collBit0 = 1 << (dec[0] >> 5);
				const uint8 collBit1 = 1 << (dec[1] >> 5);

				if (collMask & (collBit0 | collBit1))
					collState |= prisrc[1];
			}

			if (!pri) {
				uint8 v0 = dec[0];
				uint8 v1 = dec[1];

				dst[0] = mPalette[(apx[0].mCtrl >> 4) & 3][v0];
				dst[1] = mPalette[(apx[0].mCtrl >> 4) & 3][v1];
			}

			dec += 2;
			++apx;
			dst += 2;
		}
	}

	mOvCollState = collState;
}

void ATVBXEEmulator::RenderOverlayLR(uint8 *dst, int x1, int w) {
	do {
		uint8 px = VBXE_FETCH(mOvAddr + x1);

		dst[0] = dst[1] = dst[2] = dst[3] = px;
		dst += 4;
		++x1;
	} while(--w);
}

void ATVBXEEmulator::RenderOverlaySR(uint8 *dst, int x1, int w) {
	x1 += x1;

	do {
		uint8 b0 = VBXE_FETCH(mOvAddr + x1);
		uint8 b1 = VBXE_FETCH(mOvAddr + x1 + 1);

		dst[0] = dst[1] = b0;
		dst[2] = dst[3] = b1;
		dst += 4;
		x1 += 2;
	} while(--w);
}

void ATVBXEEmulator::RenderOverlayHR(uint8 *dst, int x1, int w) {
	x1 += x1;

	do {
		uint8 b0 = VBXE_FETCH(mOvAddr + x1);
		uint8 b1 = VBXE_FETCH(mOvAddr + x1 + 1);

		dst[0] = b0 >> 4;
		dst[1] = b0 & 15;
		dst[2] = b1 >> 4;
		dst[3] = b1 & 15;
		dst += 4;
		x1 += 2;
	} while(--w);
}

void ATVBXEEmulator::RenderOverlay80Text(uint8 *dst, int rx1, int x1, int w) {
	static const uint32 kExpand4[16]={
		0x00000000,
		0xFF000000,
		0x00FF0000,
		0xFFFF0000,
		0x0000FF00,
		0xFF00FF00,
		0x00FFFF00,
		0xFFFFFF00,
		0x000000FF,
		0xFF0000FF,
		0x00FF00FF,
		0xFFFF00FF,
		0x0000FFFF,
		0xFF00FFFF,
		0x00FFFFFF,
		0xFFFFFFFF,
	};

	x1 += x1;

	// Character sets are always aligned on a 2K boundary (11 bits), so the character
	// data fetch never wraps around the memory base.
	const uint8 *chbase = &mpMemory[mChAddr + mOvTextRow];

	if (mbOvTrans) {
		uint8 *transDst = &mOvTextTrans[rx1*4];

		do {
			const uint32 fetchAddr = (mOvAddr + ((x1 >> 1) & ~1)) & 0x7FFFF;
			uint8 ch = VBXE_FETCH_NOWRAP(fetchAddr);
			uint8 attr = VBXE_FETCH_NOWRAP(fetchAddr + 1);
			uint8 data = chbase[(uint32)ch << 3];

			uint32 baseColor = (uint32)(attr & 0x7f) * 0x01010101;
			uint32 mask = kExpand4[x1 & 2 ? data & 15 : data >> 4];
			uint32 result;

			if (attr & 0x80) {
				result = (~mask & 0x80808080) + baseColor;
				*(uint32 *)transDst = 0xFFFFFFFF;
			} else {
				result = mask & baseColor;
				*(uint32 *)transDst = mask;
			}

			*(uint32 *)dst = result;

			dst += 4;
			transDst += 4;
			x1 += 2;
		} while(--w);
	} else {
		do {
			const uint32 fetchAddr = (mOvAddr + ((x1 >> 1) & ~1)) & 0x7FFFF;
			uint8 ch = VBXE_FETCH_NOWRAP(fetchAddr);
			uint8 attr = VBXE_FETCH_NOWRAP(fetchAddr + 1);
			uint8 data = chbase[(uint32)ch << 3];

			uint32 baseColor = (uint32)(attr & 0x7f) * 0x01010101;
			uint32 mask = kExpand4[x1 & 2 ? data & 15 : data >> 4];
			uint32 result;

			if (attr & 0x80)
				result = (~mask & 0x80808080) + baseColor;
			else
				result = (mask & baseColor) + (~mask & 0x80808080);

			*(uint32 *)dst = result;

			dst += 4;
			x1 += 2;
		} while(--w);
	}
}

void ATVBXEEmulator::RunBlitter() {
	if (mBlitState == BlitState::Stopped || mBlitState == BlitState::Stopping)
		return;

	while(mBlitCyclesLeft > 0) {
		if (mBlitState == BlitState::Reload) {
			if (!mbBlitterContinue) {
				if (mbBlitLogging)
					ATConsoleTaggedPrintf("VBXE: Blit list completed\n");

				mBlitState = BlitState::Stopping;

				// Determine when the blitter should stop. For now we just assume all DMA cycles are
				// at the beginning of the scanline; this is particularly incorrect for the overlay
				// but we just ignore that for now.

				mBlitterStopTime = mBlitterEndScanTime - (mBlitCyclesLeft >> 3);

				mpScheduler->UnsetEvent(mpEventBlitterIrq);

				const uint32 t = ATSCHEDULER_GETTIME(mpScheduler);
				if (ATWrapTime{t} >= mBlitterStopTime)
					AssertBlitterIrq();
				else
					mpEventBlitterIrq = mpScheduler->AddEvent(mBlitterStopTime - t, this, 1);
				break;
			}

			LoadBlitter();
			
			if (mBlitCyclesLeft <= 0)
				break;
		}

		uint32 zeroSourceBytes = RunBlitterRow();

		// Check how many cycles we should credit based on $00 source bytes.
		//
		//	Mode 0: None (no optimization)
		//	Mode 1: 1/dest if coldetect off, 2/dest if coldetect on
		//	Modes 2-6: 2/dest
		//
		// Note that there is a complication if X-zoom or constant mode are active, as
		// we can't go below 1/dest.

		if (mBlitterMode != 0 && mBlitAndMask != 0)
			mBlitCyclesLeft += mBlitCyclesSavedPerZero * zeroSourceBytes;

		mBlitCyclesLeft -= mBlitCyclesPerRow;

		mBlitDstAddr += mBlitDstStepY;

		if (++mBlitZoomCounterY >= mBlitZoomY) {
			mBlitZoomCounterY = 0;
			mBlitSrcAddr += mBlitSrcStepY;

			if (!--mBlitHeightLeft) {
				mBlitState = BlitState::Reload;

				if (mpTraceChannelBlit) {
					mpTraceChannelBlit->AddTickEventF(mTraceBlitStartTime, GetBlitTime(), kATTraceColor_Default, L"%ux%u", mBlitWidth, mBlitHeight);
				}
			}
		}
	}
}

uint32 ATVBXEEmulator::RunBlitterRow() {
	switch(mBlitterMode) {
		default:
		case 0:	return RunBlitterRow<0>(); break;
		case 1:	return RunBlitterRow<1>(); break;
		case 2:	return RunBlitterRow<2>(); break;
		case 3:	return RunBlitterRow<3>(); break;
		case 4:	return RunBlitterRow<4>(); break;
		case 5:	return RunBlitterRow<5>(); break;
		case 6:	return RunBlitterRow<6>(); break;
	}
}

template<uint8 T_Mode>
uint32 ATVBXEEmulator::RunBlitterRow() {
	// Process one row.
	//
	// We may have to adjust the cycle timing depending on the content of the blit.
	// Timings are as follows (r = source read cycle, x = dest read / execute cycle, w = write cycle):
	//
	//	Mode 0 (fill/copy):			rw
	//	Mode 1 (stencil):			r ($00)
	//								rw (non-$00, collision off)
	//								rxw (non-$00, collision on)
	//	Mode 2/3/5/6(add/or/xor/hr)	r for $00
	//								rxw for non-$00
	//	Mode 4 (and)				rw for $00
	//								rxw for non-$00
	//
	// If the AND mask is $00 or X-zoom is active, the blitter can skip subsequent
	// source read cycles and reuse the previously fetched result. In cases where
	// the x-w cycles are also skipped, the blitter runs source read cycles. The
	// fastest possible blit rate is one cycle per destination byte.
	//
	// There is no optimization for Y-zoom.
	//
	uint32 srcRowAddr = mBlitSrcAddr;
	uint32 dstRowAddr = mBlitDstAddr;
	uint32 patWidth = mBlitPatternMode & 0x80 ? (mBlitPatternMode & 0x3F) + 1 : 0xfffff;
	uint32 patCounter = patWidth;
	uint32 dstStepXZoomed = mBlitDstStepX * mBlitZoomX;
	uint32 zeroSourceBytes = 0;

	const uint8 andMask = mBlitAndMask;
	const uint8 xorMask = mBlitXorMask;
	const uint8 zoomX = mBlitZoomX;
	const uint32 width = mBlitWidth;
	const sint32 srcStepX = mBlitSrcStepX;
	const sint32 dstStepX = mBlitDstStepX;

	if (zoomX == 0) {
		VDNEVERHERE;
	}

	if (width == 0) {
		VDNEVERHERE;
	}

	if constexpr (T_Mode == 0) {
		if (zoomX == 1 && !(mBlitPatternMode & 0x80)) {
			if (andMask == 0) {
				for(uint32 x=0; x<width; ++x) {
					VBXE_WRITE(dstRowAddr, xorMask);
					dstRowAddr += dstStepX;
				}

				srcRowAddr += srcStepX * width;

			} else {
				for(uint32 x=0; x<width; ++x) {
					uint8 c = VBXE_FETCH(srcRowAddr);

					c &= andMask;
					c ^= xorMask;

					VBXE_WRITE(dstRowAddr, c);
					dstRowAddr += dstStepX;
						
					srcRowAddr += srcStepX;
				}
			}
		} else {
			for(uint32 x=0; x<width; ++x) {
				uint8 c = VBXE_FETCH(srcRowAddr);

				c &= andMask;
				c ^= xorMask;

				for(uint8 i=0; i<zoomX; ++i) {
					VBXE_WRITE(dstRowAddr, c);
					dstRowAddr += dstStepX;
				}

				srcRowAddr += srcStepX;

				if (!--patCounter) {
					patCounter = patWidth;
					srcRowAddr = mBlitSrcAddr;
				}
			}
		}

		return 0;
	}

	for(uint32 x=0; x<width; ++x) {
		uint8 c = VBXE_FETCH(srcRowAddr);

		c &= andMask;
		c ^= xorMask;

		if (c) {
			for(uint8 i=0; i<zoomX; ++i) {
				uint8 d = VBXE_FETCH(dstRowAddr);

				if constexpr (T_Mode == 6) {
					const uint8 cl = c & 0x0f;
					const uint8 ch = c & 0xf0;
					uint8 dl = d & 0x0f;
					uint8 dh = d & 0xf0;

					if (cl) {
						if (dl) {
							if ((1 << ((d >> 1) & 7)) & mBlitActiveCollisionMask) {
								mBlitCollisionCode = (mBlitCollisionCode & 0xf0) + dl;
								mBlitActiveCollisionMask = 0;
							}
						}

						dl = cl;
					}

					if (ch) {
						if (dh) {
							if ((1 << ((d >> 5) & 7)) & mBlitActiveCollisionMask) {
								mBlitCollisionCode = (mBlitCollisionCode & 0x0f) + dh;
								mBlitActiveCollisionMask = 0;
							}
						}

						dh = ch;
					}

					VBXE_WRITE(dstRowAddr, dl + dh);
				} else {
					if (d && ((1 << (d >> 5)) & mBlitActiveCollisionMask)) {
						mBlitCollisionCode = d;
						mBlitActiveCollisionMask = 0;
					}

					if constexpr (T_Mode == 1)
						VBXE_WRITE(dstRowAddr, c);
					else if constexpr (T_Mode == 2)
						VBXE_WRITE(dstRowAddr, c + d);
					else if constexpr (T_Mode == 3)
						VBXE_WRITE(dstRowAddr, c | d);
					else if constexpr (T_Mode == 4)
						VBXE_WRITE(dstRowAddr, c & d);
					else if constexpr (T_Mode == 5)
						VBXE_WRITE(dstRowAddr, c ^ d);
				}

				dstRowAddr += dstStepX;
			}
		} else {
			++zeroSourceBytes;

			if constexpr (T_Mode == 4) {
				for(uint8 i=0; i<zoomX; ++i) {
					VBXE_WRITE(dstRowAddr, 0);
					dstRowAddr += dstStepX;
				}
			} else {
				dstRowAddr += dstStepXZoomed;
			}
		}

		srcRowAddr += srcStepX;

		if (!--patCounter) {
			patCounter = patWidth;
			srcRowAddr = mBlitSrcAddr;
		}

	}

	return zeroSourceBytes;
}

uint64 ATVBXEEmulator::GetBlitTime() const {
	// compute blit time assuming high 32 bits match
	uint64 t = mpScheduler->GetTick64();
	uint32 blitTime32 = mBlitterEndScanTime - (mBlitCyclesLeft >> 3);
	uint64 blitTime64 = (t - (uint32)t) + blitTime32;

	// if computed 64-bit time is >+/-2^31 off, correct by 2^32
	uint64 deltaCheck = (blitTime64 - t);

	if (deltaCheck + UINT64_C(0x80000000) >= UINT64_C(0x100000000)) {
		if (deltaCheck > UINT64_C(0x8000'0000'0000'0000))
			blitTime64 += UINT64_C(0x100000000);
		else
			blitTime64 -= UINT64_C(0x100000000);
	}

	return blitTime64;
}

void ATVBXEEmulator::LoadBlitter() {
	if (mbBlitLogging) {
		DumpBlitListEntry(mBlitListFetchAddr, mbBlitLoggingCompact, true);
	}

	mTraceBlitStartTime = GetBlitTime();

	// read in next BCB from local memory
	uint8 bcb[21];
	for(int i=0; i<21; ++i)
		bcb[i] = VBXE_FETCH(mBlitListFetchAddr + i);

	mBlitListFetchAddr += 21;

	// decode BCB to internal state
	LoadBCB(bcb);

	// reset parameters for new blit
	mBlitState = BlitState::ProcessBlit;
	mBlitHeightLeft = mBlitHeight;
	mBlitZoomCounterY = 0;
	mBlitCollisionCode = 0;

	// deduct cycles for blit list
	mBlitCyclesLeft -= 21;

	// deduct a single cycle for constant read, since the source read cycle cannot initially
	// be skipped
	if (mBlitAndMask == 0)
		--mBlitCyclesLeft;
}

// Load blitter state from a blit control block as it appears in VBXE local memory.
void ATVBXEEmulator::LoadBCB(const uint8 (&bcb)[21]) {
	const uint8 rawSrcAddr0		= bcb[ 0];
	const uint8 rawSrcAddr1		= bcb[ 1];
	const uint8 rawSrcAddr2		= bcb[ 2];
	const uint8 rawSrcStepY0	= bcb[ 3];
	const uint8 rawSrcStepY1	= bcb[ 4];
	const uint8 rawSrcStepX		= bcb[ 5];
	const uint8 rawDstAddr0		= bcb[ 6];
	const uint8 rawDstAddr1		= bcb[ 7];
	const uint8 rawDstAddr2		= bcb[ 8];
	const uint8 rawDstStepY0	= bcb[ 9];
	const uint8 rawDstStepY1	= bcb[10];
	const uint8 rawDstStepX		= bcb[11];
	const uint8 rawBltWidth0	= bcb[12];
	const uint8 rawBltWidth1	= bcb[13];
	const uint8 rawBltHeight	= bcb[14];
	const uint8 rawBltAndMask	= bcb[15];
	const uint8 rawBltXorMask	= bcb[16];
	const uint8 rawBltCollisionMask = bcb[17];
	const uint8 rawBltZoom		= bcb[18];
	const uint8 rawPatternMode	= bcb[19];
	const uint8 rawBltControl	= bcb[20];

	mBlitSrcAddr = (uint32)rawSrcAddr0 + ((uint32)rawSrcAddr1 << 8) + ((uint32)rawSrcAddr2 << 16);
	mBlitSrcStepX = (sint8)rawSrcStepX;
	mBlitSrcStepY = ((((uint32)rawSrcStepY0 + ((uint32)rawSrcStepY1 << 8)) & 0x1FFF) + 0xFFFFF000) ^ 0xFFFFF000;
	mBlitDstAddr = (uint32)rawDstAddr0 + ((uint32)rawDstAddr1 << 8) + ((uint32)rawDstAddr2 << 16);
	mBlitDstStepX = (sint8)rawDstStepX;
	mBlitDstStepY = ((((uint32)rawDstStepY0 + ((uint32)rawDstStepY1 << 8)) & 0x1FFF) + 0xFFFFF000) ^ 0xFFFFF000;
	mBlitWidth = (uint32)rawBltWidth0 + (uint32)((rawBltWidth1 & 0x01) << 8) + 1;
	mBlitHeight = (uint32)rawBltHeight + 1;
	mBlitAndMask = rawBltAndMask;
	mBlitXorMask = rawBltXorMask;
	mBlitCollisionMask = rawBltCollisionMask;
	mBlitActiveCollisionMask = rawBltCollisionMask;
	mBlitPatternMode = rawPatternMode;

	mbBlitterContinue = (rawBltControl & 0x08) != 0;

	mBlitterMode = rawBltControl & 7;

	mBlitZoomX = (rawBltZoom & 7) + 1;
	mBlitZoomY = ((rawBltZoom >> 4) & 7) + 1;

	// recompute derived parameters that are constant for the entire blit
	// Compute memory cycles per row blitted.
	const uint32 srcBytesPerRow = mBlitWidth;
	const uint32 dstBytesPerRow = mBlitWidth * mBlitZoomX;

	mBlitCyclesPerRow = dstBytesPerRow;
	mBlitCyclesSavedPerZero = 0;

	if (mBlitAndMask)
		mBlitCyclesPerRow += srcBytesPerRow;

	if (mBlitAndMask || mBlitXorMask) {
		switch(mBlitterMode) {
			// Mode 0 is read-write.
			case 0:
				break;

			// Mode 1 is read-modify-write if collision detection is enabled.
			case 1:
				if (mBlitCollisionMask) {
					mBlitCyclesPerRow += dstBytesPerRow;

					if (mBlitAndMask)
						mBlitCyclesSavedPerZero = mBlitZoomX*2;
					else
						mBlitCyclesSavedPerZero = mBlitZoomX;
				} else {
					if (mBlitAndMask)
						mBlitCyclesSavedPerZero = mBlitZoomX;
					else
						mBlitCyclesSavedPerZero = 0;
				}
				break;

			// Modes 2-6 are always read-modify-write.
			default:
				mBlitCyclesPerRow += dstBytesPerRow;

				if (mBlitAndMask)
					mBlitCyclesSavedPerZero = mBlitZoomX*2;
				else
					mBlitCyclesSavedPerZero = mBlitZoomX;
				break;
		}
	}
}

// Save blitter state to a blit control block as it appears in VBXE local memory.
// This isn't an operation that VBXE ever does, but we use it in save state processing
// so that the save state matches the same form as the blit list.
void ATVBXEEmulator::SaveBCB(uint8 (&bcb)[21]) const {
	bcb[ 0] = (uint8)(mBlitSrcAddr >>  0);
	bcb[ 1] = (uint8)(mBlitSrcAddr >>  8);
	bcb[ 2] = (uint8)(mBlitSrcAddr >> 16);
	bcb[ 3] = (uint8)(mBlitSrcStepY >> 0);
	bcb[ 4] = (uint8)(mBlitSrcStepY >> 8);
	bcb[ 5] = (uint8)(mBlitSrcStepX     );
	bcb[ 6] = (uint8)(mBlitDstAddr >>  0);
	bcb[ 7] = (uint8)(mBlitDstAddr >>  8);
	bcb[ 8] = (uint8)(mBlitDstAddr >> 16);
	bcb[ 9] = (uint8)(mBlitDstStepY >> 0);
	bcb[10] = (uint8)(mBlitDstStepY >> 8);
	bcb[11] = (uint8)(mBlitDstStepX     );
	bcb[12] = (uint8)((mBlitWidth - 1) >> 0);
	bcb[13] = (uint8)((mBlitWidth - 1) >> 8);
	bcb[14] = (uint8)(mBlitHeight - 1);
	bcb[15] = mBlitAndMask;
	bcb[16] = mBlitXorMask;
	bcb[17] = mBlitCollisionMask;
	bcb[18] = (mBlitZoomX - 1) + ((mBlitZoomY - 1) << 4);
	bcb[19] = mBlitPatternMode;
	bcb[20] = (mbBlitterContinue ? 0x08 : 0x00) + mBlitterMode;
}

void ATVBXEEmulator::InitPriorityTables() {
	uint8 tab[32][256];

	ATInitGTIAPriorityTables(tab);

	// We need to rewrite the tables to split out playfield and player colors, since
	// the former can change so often with VBXE.
	for(int table=0; table<32; ++table) {
		const uint8 *src = tab[table];

		for(int idx=0; idx<256; ++idx) {
			auto& dst = mPriorityTables[table][idx];
			auto& dst2 = mPriorityTablesHi[table][idx];

			// The first value is the index in the attribute cell (0-3); the
			// second value is from the color table. PF0-PF2 must come from
			// the attribute cell in CCR modes; in hires modes only PF1 and
			// PF2 come from there since the PF0 cell is used for the PF2/PF3
			// selector instead.

			uint8 ovpri = 0;

			switch(src[idx]) {
				case kColorP0:
					dst.mMapColor = 0;
					dst.mPalColor = kColorP0;
					dst2.mMapColor = 0;
					dst2.mPalColor = kColorP0;
					ovpri = 0x10;
					break;
				case kColorP1:
					dst.mMapColor = 0;
					dst.mPalColor = kColorP1;
					dst2.mMapColor = 0;
					dst2.mPalColor = kColorP1;
					ovpri = 0x20;
					break;
				case kColorP2:
					dst.mMapColor = 0;
					dst.mPalColor = kColorP2;
					dst2.mMapColor = 0;
					dst2.mPalColor = kColorP2;
					ovpri = 0x40;
					break;
				case kColorP3:
					dst.mMapColor = 0;
					dst.mPalColor = kColorP3;
					dst2.mMapColor = 0;
					dst2.mPalColor = kColorP3;
					ovpri = 0x80;
					break;
				case kColorPF0:
					dst.mMapColor = 1;
					dst.mPalColor = kColorBlack;
					dst2.mMapColor = 0;
					dst2.mPalColor = kColorPF0;
					ovpri = 0x01;
					break;
				case kColorPF1:
					dst.mMapColor = 2;
					dst.mPalColor = kColorBlack;
					dst2.mMapColor = 2;
					dst2.mPalColor = kColorBlack;
					ovpri = 0x02;
					break;
				case kColorPF2:
					dst.mMapColor = 3;
					dst.mPalColor = kColorBlack;
					dst2.mMapColor = 3;
					dst2.mPalColor = kColorBlack;
					ovpri = 0x04;
					break;
				case kColorPF3:
					dst.mMapColor = 0;
					dst.mPalColor = kColorPF3;
					dst2.mMapColor = 0;
					dst2.mPalColor = kColorPF3;
					ovpri = mbVersion126 ? 0x04 : 0x08;
					break;
				case kColorBAK:
					dst.mMapColor = 0;
					dst.mPalColor = kColorBAK;
					dst2.mMapColor = 0;
					dst2.mPalColor = kColorBAK;
					ovpri = mbVersion126 ? 0x08 : 0x00;
					break;
				case kColorBlack:
					dst.mMapColor = 0;
					dst.mPalColor = kColorBlack;
					dst2.mMapColor = 0;
					dst2.mPalColor = kColorBlack;
					ovpri = 0x00;
					break;
				case kColorP0P1:
					dst.mMapColor = 0;
					dst.mPalColor = kColorP0P1;
					dst2.mMapColor = 0;
					dst2.mPalColor = kColorP0P1;
					ovpri = 0x30;
					break;
				case kColorP2P3:
					dst.mMapColor = 0;
					dst.mPalColor = kColorP2P3;
					dst2.mMapColor = 0;
					dst2.mPalColor = kColorP2P3;
					ovpri = 0xC0;
					break;
				case kColorPF0P0:
					dst.mMapColor = 1;
					dst.mPalColor = kColorP0;
					dst2.mMapColor = 0;
					dst2.mPalColor = kColorPF0P0;
					ovpri = 0x11;
					break;
				case kColorPF0P1:
					dst.mMapColor = 1;
					dst.mPalColor = kColorP1;
					dst2.mMapColor = 0;
					dst2.mPalColor = kColorPF0P1;
					ovpri = 0x21;
					break;
				case kColorPF0P0P1:
					dst.mMapColor = 1;
					dst.mPalColor = kColorP0P1;
					dst2.mMapColor = 0;
					dst2.mPalColor = kColorPF0P0P1;
					ovpri = 0x31;
					break;
				case kColorPF1P0:
					dst.mMapColor = 2;
					dst.mPalColor = kColorP0;
					dst2.mMapColor = 2;
					dst2.mPalColor = kColorP0;
					ovpri = 0x12;
					break;
				case kColorPF1P1:
					dst.mMapColor = 2;
					dst.mPalColor = kColorP1;
					dst2.mMapColor = 2;
					dst2.mPalColor = kColorP1;
					ovpri = 0x22;
					break;
				case kColorPF1P0P1:
					dst.mMapColor = 2;
					dst.mPalColor = kColorP0P1;
					dst2.mMapColor = 2;
					dst2.mPalColor = kColorP0P1;
					ovpri = 0x32;
					break;
				case kColorPF2P2:
					dst.mMapColor = 3;
					dst.mPalColor = kColorP2;
					dst2.mMapColor = 3;
					dst2.mPalColor = kColorP2;
					// MERGE NOTE: test19 used P1 instead of P2 here.
					ovpri = PF2 | P2;
					break;
				case kColorPF2P3:
					dst.mMapColor = 3;
					dst.mPalColor = kColorP3;
					dst2.mMapColor = 3;
					dst2.mPalColor = kColorP3;
					// MERGE NOTE: test19 used P2 instead of P3 here.
					ovpri = PF2 | P3;
					break;
				case kColorPF2P2P3:
					dst.mMapColor = 3;
					dst.mPalColor = kColorP2P3;
					dst2.mMapColor = 3;
					dst2.mPalColor = kColorP2P3;
					ovpri = PF2 | P2 | P3;
					break;
				case kColorPF3P2:
					dst.mMapColor = 0;
					dst.mPalColor = kColorPF3P2;
					dst2.mMapColor = 0;
					dst2.mPalColor = kColorPF3P2;
					ovpri = mbVersion126 ? 0x44 : 0x48;
					break;
				case kColorPF3P3:
					dst.mMapColor = 0;
					dst.mPalColor = kColorPF3P3;
					dst2.mMapColor = 0;
					dst2.mPalColor = kColorPF3P3;
					ovpri = mbVersion126 ? 0x84 : 0x88;
					break;
				case kColorPF3P2P3:
					dst.mMapColor = 0;
					dst.mPalColor = kColorPF3P2P3;
					dst2.mMapColor = 0;
					dst2.mPalColor = kColorPF3P2P3;
					ovpri = mbVersion126 ? 0xC4 : 0xC8;
					break;
			}

			dst.mOvPri = ovpri;
			dst2.mOvPri = ovpri;
		}
	}
}

void ATVBXEEmulator::UpdateColorTable() {
	mpColorTable = mbAnalysisMode ? kATAnalysisColorTable : mbExtendedColor ? mColorTableExt : mColorTable;
}

void ATVBXEEmulator::RecorrectPalettes() {
	if (!mpPaletteCorrector)
		return;

	mRenderPaletteCorrectedMode = mRenderPaletteCorrectionMode;

	for(int i=0; i<4; ++i) {
		for(int j=0; j<256; ++j)
			mPalette[i][j] = mpPaletteCorrector->CorrectSingleColor(mArchPalette[i][j], mRenderPaletteCorrectionMode, mbRenderPaletteSigned);
	}
}

///////////////////////////////////////////////////////////////////////////

void ATCreateDeviceVBXE(const ATPropertySet& pset, IATDevice **dev);

extern const ATDeviceDefinition g_ATDeviceDefVBXE = { "vbxe", "vbxe", L"VideoBoard XE", ATCreateDeviceVBXE };

class ATVBXEDevice final
	: public ATDevice
	, public IATVBXEDevice
	, public IATDeviceScheduling
	, public IATDeviceMemMap
	, public IATDeviceIRQSource
	, public IATDeviceU1MBControllable
	, public IATDeviceSnapshot
{
public:
	ATVBXEDevice();

	void *AsInterface(uint32 iid) override;

	void GetDeviceInfo(ATDeviceInfo& info) override;
	void WarmReset() override;
	void ColdReset() override;
	void GetSettingsBlurb(VDStringW& buf) override;
	void GetSettings(ATPropertySet& settings) override;
	bool SetSettings(const ATPropertySet& settings) override;
	void Init() override;
	void Shutdown() override;
	void SetTraceContext(ATTraceContext *context) override;

public:
	void SetSharedMemory(void *mem) override;

	bool GetSharedMemoryMode() const override;
	void SetSharedMemoryMode(bool sharedMemory) override;

	virtual bool GetAltPageEnabled() const override;
	virtual void SetAltPageEnabled(bool enabled) override;

public:	// IATDeviceScheduling
	void InitScheduling(ATScheduler *sch, ATScheduler *slowsch) override;

public:	// IATDeviceMemMap
	void InitMemMap(ATMemoryManager *memmap) override;
	bool GetMappedRange(uint32 index, uint32& lo, uint32& hi) const override;

public:	// IATDeviceIRQSource
	void InitIRQSource(ATIRQController *irqc) override;

public:	// IATDeviceU1MBControllable
	void SetU1MBControl(ATU1MBControl control, sint32 value) override;

public:	// IATDeviceSnapshot
	void LoadState(const IATObjectState *state, ATSnapshotContext& ctx) override;
	vdrefptr<IATObjectState> SaveState(ATSnapshotContext& ctx) const override;

private:
	void UpdateMemoryMapping();
	void AllocVBXEMemory();
	void FreeVBXEMemory();

	ATMemoryManager *mpMemMan = nullptr;
	ATIRQController *mpIrqController = nullptr;
	ATScheduler *mpScheduler = nullptr;

	bool mbSharedMemoryEnabled = false;
	void *mpSharedMemory = nullptr;
	void *mpVBXEMemory = nullptr;
	uint8 mRegBase = 0xD6;
	sint32 mMemBaseOverride = -1;

	ATVBXEEmulator mEmulator;
};

ATVBXEDevice::ATVBXEDevice() {
	mEmulator.SetRegisterBase(0xD6);
}

void *ATVBXEDevice::AsInterface(uint32 iid) {
	switch(iid) {
		case IATVBXEDevice::kTypeID: return static_cast<IATVBXEDevice *>(this);
		case IATDeviceScheduling::kTypeID: return static_cast<IATDeviceScheduling *>(this);
		case IATDeviceMemMap::kTypeID: return static_cast<IATDeviceMemMap *>(this);
		case IATDeviceIRQSource::kTypeID: return static_cast<IATDeviceIRQSource *>(this);
		case IATDeviceU1MBControllable::kTypeID: return static_cast<IATDeviceU1MBControllable *>(this);
		case IATDeviceSnapshot::kTypeID: return static_cast<IATDeviceSnapshot *>(this);
		case ATVBXEEmulator::kTypeID:	return &mEmulator;
		default:	return ATDevice::AsInterface(iid);
	}
}

void ATVBXEDevice::GetDeviceInfo(ATDeviceInfo& info) {
	info.mpDef = &g_ATDeviceDefVBXE;
}

void ATVBXEDevice::WarmReset() {
	mEmulator.WarmReset();
}

void ATVBXEDevice::ColdReset() {
	mEmulator.ColdReset();
}

void ATVBXEDevice::GetSettingsBlurb(VDStringW& buf) {
	buf.append_sprintf(L"FX1.%u at $%02X00", mEmulator.GetVersion() % 100, mRegBase);
	if (mbSharedMemoryEnabled)
		buf += L", shared mem";
}

void ATVBXEDevice::GetSettings(ATPropertySet& settings) {
	settings.SetBool("shared_mem", mbSharedMemoryEnabled);
	settings.SetBool("alt_page", GetAltPageEnabled());
	settings.SetUint32("version", mEmulator.GetVersion());
}

bool ATVBXEDevice::SetSettings(const ATPropertySet& settings) {
	SetSharedMemoryMode(settings.GetBool("shared_mem", false));
	SetAltPageEnabled(settings.GetBool("alt_page", false));
	mEmulator.SetVersion(settings.GetUint32("version"));

	return true;
}

void ATVBXEDevice::Init() {
	UpdateMemoryMapping();
	mEmulator.Init(mpIrqController, mpMemMan, mpScheduler);
}

void ATVBXEDevice::Shutdown() {
	mEmulator.Shutdown();

	FreeVBXEMemory();

	mpMemMan = nullptr;
	mpIrqController = nullptr;
}

void ATVBXEDevice::SetTraceContext(ATTraceContext *context) {
	mEmulator.SetTraceContext(context);
}

void ATVBXEDevice::SetSharedMemory(void *mem) {
	mpSharedMemory = mem;

	UpdateMemoryMapping();
}

bool ATVBXEDevice::GetSharedMemoryMode() const {
	return mbSharedMemoryEnabled;
}

void ATVBXEDevice::SetSharedMemoryMode(bool enabled) {
	if (mbSharedMemoryEnabled != enabled) {
		mbSharedMemoryEnabled = enabled;

		mEmulator.SetSharedMemoryMode(enabled);

		UpdateMemoryMapping();

		if (enabled)
			FreeVBXEMemory();
	}
}

bool ATVBXEDevice::GetAltPageEnabled() const {
	return mRegBase != 0xD6;
}

void ATVBXEDevice::SetAltPageEnabled(bool enabled) {
	mRegBase = enabled ? 0xD7 : 0xD6;

	if (mMemBaseOverride < 0)
		mEmulator.SetRegisterBase(mRegBase);
}

void ATVBXEDevice::InitScheduling(ATScheduler *sch, ATScheduler *slowsch) {
	mpScheduler = sch;
}

void ATVBXEDevice::InitMemMap(ATMemoryManager *memman) {
	mpMemMan = memman;
}

bool ATVBXEDevice::GetMappedRange(uint32 index, uint32& lo, uint32& hi) const {
	if (index == 0) {
		lo = (uint32)mEmulator.GetRegisterBase() << 8;
		hi = lo + 0x100;
		return true;
	} else {
		return false;
	}
}

void ATVBXEDevice::InitIRQSource(ATIRQController *irqc) {
	mpIrqController = irqc;
}

void ATVBXEDevice::SetU1MBControl(ATU1MBControl control, sint32 value) {
	if (control == kATU1MBControl_VBXEBase) {
		if (mMemBaseOverride != value) {
			mMemBaseOverride = value;

			mEmulator.SetRegisterBase(mMemBaseOverride < 0 ? mRegBase : (uint8)mMemBaseOverride);
		}
	}
}

void ATVBXEDevice::LoadState(const IATObjectState *state, ATSnapshotContext& ctx) {
	mEmulator.LoadState(state);
}

vdrefptr<IATObjectState> ATVBXEDevice::SaveState(ATSnapshotContext& ctx) const {
	return mEmulator.SaveState();
}

void ATVBXEDevice::UpdateMemoryMapping() {
	if (!mbSharedMemoryEnabled)
		AllocVBXEMemory();

	mEmulator.SetMemory(mbSharedMemoryEnabled ? mpSharedMemory : mpVBXEMemory);
}

void ATVBXEDevice::AllocVBXEMemory() {
	if (!mpVBXEMemory) {
		mpVBXEMemory = VDAlignedMalloc(524288, 16);
		memset(mpVBXEMemory, 0, 524288);
	}
}

void ATVBXEDevice::FreeVBXEMemory() {
	if (mpVBXEMemory) {
		VDAlignedFree(mpVBXEMemory);
		mpVBXEMemory = NULL;
	}
}

///////////////////////////////////////////////////////////////////////////

void ATCreateDeviceVBXE(const ATPropertySet& pset, IATDevice **dev) {
	vdrefptr<ATVBXEDevice> p(new ATVBXEDevice);

	*dev = p.release();
}
