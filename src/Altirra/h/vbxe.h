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

#ifndef f_AT_VBXE_H
#define f_AT_VBXE_H

#include <vd2/system/vdstl.h>
#include <at/atcore/scheduler.h>

class ATConsoleOutput;
class ATMemoryManager;
class ATMemoryLayer;
class ATIRQController;
struct ATTraceContext;
class ATTraceChannelSimple;
class ATTraceChannelFormatted;
class ATPaletteCorrector;
class IATObjectState;
template<typename T> class vdrefptr;

enum class ATVBXEOverlayMode : uint8 {
	Disabled,
	LR,
	SR,
	HR,
	Text
};

struct ATVBXEXDLHistoryEntry {
	// Base (4)
	uint32 mXdlAddr : 19;

	uint32 mOverlayMode : 3;
	uint32 mbMapEnabled : 1;
	uint32 mbXdlActive : 1;

	// XDLC_OVADR (4)
	uint32 mOverlayAddr : 20;
	uint32 mOverlayStep : 12;

	// XDLC_MAPADR (4)
	uint32 mMapAddr : 20;
	uint32 mMapStep : 12;

	// XDLC_OVSCROL (2)
	uint8 mOvHScroll;
	uint8 mOvVScroll;

	// XDLC_ATT (2)
	uint8 mAttrByte;	// Playfield/overlap palette and overlay width byte
	uint8 mOvPriority;

	// XDLC_CHBASE (1)
	uint32 mChBase : 8;

	// XDLC_MAPPAR (3)
	uint32 mMapFieldWidth : 5;
	uint32 mMapFieldHeight : 5;
	uint32 mMapHScroll : 5;
	uint32 mMapVScroll : 5;

	bool operator==(const ATVBXEXDLHistoryEntry&) const = default;
};

class ATVBXEEmulator final : public IATSchedulerCallback {
	friend struct ATVBXERegressionTests;
	ATVBXEEmulator(const ATVBXEEmulator&) = delete;
	ATVBXEEmulator& operator=(const ATVBXEEmulator&) = delete;
public:
	enum : uint32 { kTypeID = "vbxe"_vdfcctypeid };

	ATVBXEEmulator();
	~ATVBXEEmulator();

	void SetSharedMemoryMode(bool sharedMemory);
	void SetMemory(void *memory);

	// VBXE requires 512K of memory.
	void Init(ATIRQController *irqcon, ATMemoryManager *memman, ATScheduler *sch);
	void Shutdown();

	void ColdReset();
	void WarmReset();

	void Set5200Mode(bool enable);
	void SetRegisterBase(uint8 page);
	uint8 GetRegisterBase() const { return mRegBase; };

	// Set or get core revision as decimal version (126 = 1.26). Version is adjusted to
	// supported values.
	uint32 GetVersion() const;
	void SetVersion(uint32 version);

	void SetAnalysisMode(bool analysisMode);
	void SetDefaultPalette(const uint32 pal[256], ATPaletteCorrector *palcorr);
	uint32 GetRawPaletteAndSEL(uint32 pal[1024]) const;

	void SetTraceContext(ATTraceContext *context);

	uint8 *GetMemoryBase() { return mpMemory; }

	sint32 TryMapCPUAddressToLocal(uint16 addr) const;

	bool IsBlitLoggingEnabled() const { return mbBlitLogging; }
	void SetBlitLoggingEnabled(bool enable, bool compact);
	void DumpStatus();
	void DumpBlitList(sint32 addrOpt, bool compact, uint32 maxCount);
	bool DumpBlitListEntry(uint32 addr, bool compact, bool autologging);
	void DumpXDL();
	void DumpXDLHistory(ATConsoleOutput& output);

	const ATVBXEXDLHistoryEntry *GetXDLHistory(uint32 y) const;

	void LoadState(const IATObjectState *state);
	vdrefptr<IATObjectState> SaveState() const;

	// ANTIC/RAM interface
	sint32 ReadControl(uint8 addrLo);
	bool WriteControl(uint8 addrLo, uint8 value);

	// GTIA interface
	void BeginFrame(int correctionMode, bool signedPalette);
	void EndFrame();
	void BeginScanline(uint32 y, uint32 *dst, const uint8 *mergeBuffer, const uint8 *anticBuffer, bool hires);
	void RenderScanline(int x2, bool pfpmrendered);
	uint32 SenseScanline(int hpx1, int hpx2, const uint8 *weights) const;
	void EndScanline();

	void AddRegisterChange(uint8 pos, uint8 addr, uint8 value);
	void SetRegisterImmediate(uint8 addr, uint8 value);

public:
	void OnScheduledEvent(uint32 id) override;

private:
	struct RegisterChange {
		uint8 mPos;
		uint8 mReg;
		uint8 mValue;
		uint8 mPad;
	};

	bool IsBlitterActive() const;
	void AssertBlitterIrq();

	static bool StaticGTIAWrite(void *thisptr, uint32 reg, uint8 value);
	static sint32 StaticReadControl(void *thisptr, uint32 reg) { return ((ATVBXEEmulator *)thisptr)->ReadControl((uint8)reg); }
	static bool StaticWriteControl(void *thisptr, uint32 reg, uint8 value) { return ((ATVBXEEmulator *)thisptr)->WriteControl((uint8)reg, value); }

	void InitMemoryMaps();
	void ShutdownMemoryMaps();
	void UpdateMemoryMaps();
	void UpdateRegisters(const RegisterChange *changes, int count);

	int RenderAttrPixels(int x1, int x2);
	void RenderAttrDefaultPixels(int x1h, int x2h);

	enum class RenderMode : uint8 {
		Lores,
		Hires,
		HiresXcolor,
		Mode9,
		Mode10,
		Mode11,
	};

	template<bool T_Blank, RenderMode T_Mode>
	void Render(int x1, int x2, bool attmap);

	template<bool T_EnableCollisions> void RenderOverlay(int x1, int x2);
	void RenderOverlayLR(uint8 *dst, int x1, int w);
	void RenderOverlaySR(uint8 *dst, int x1, int w);
	void RenderOverlayHR(uint8 *dst, int x1, int w);
	void RenderOverlay80Text(uint8 *dst, int rx1, int x1, int w);

	void RunBlitter();
	uint32 RunBlitterRow();
	
	template<uint8 T_Mode>
	uint32 RunBlitterRow();

	uint64 GetBlitTime() const;
	void LoadBlitter();
	void LoadBCB(const uint8 (&bcb)[21]);
	void SaveBCB(uint8 (&bcb)[21]) const;

	void InitPriorityTables();
	void UpdateColorTable();
	void RecorrectPalettes();

	static constexpr uint32 kLocalAddrMask = 0x7FFFF;

	uint8 *mpMemory = nullptr;
	ATIRQController *mpIRQController = nullptr;
	ATMemoryManager *mpMemMan;
	ATScheduler *mpScheduler = nullptr;

	uint8	mMemAcControl;
	uint8	mMemAcBankA;
	uint8	mMemAcBankB;
	bool	mb5200Mode;
	bool	mbSharedMemory;
	uint8	mVersion;			// MINOR_REVISION value, sans shared mem flag.
	bool	mbVersion126;
	uint8	mRegBase;

	uint32 mXdlBaseAddr;
	uint32 mXdlAddr;
	bool mbXdlActive;
	bool mbXdlEnabled;
	uint32 mXdlRepeatCounter;

	using OvMode = ATVBXEOverlayMode;

	enum OvWidth {
		kOvWidth_Narrow,
		kOvWidth_Normal,
		kOvWidth_Wide,
	};

	OvMode mOvMode;
	OvWidth mOvWidth;
	bool	mbOvTrans;
	bool	mbOvTrans15;
	uint8	mOvHscroll;
	uint8	mOvVscroll;
	uint8	mOvMainPriority;		// INVERTED priority from XDL
	uint8	mOvPriority[4];			// INVERTED priority chosen by att map

	uint8	mOvCollMask;			// Overlay collision mask (nibble swapped from canonical)
	uint8	mOvCollState;			// Overlay collision state (nibble swapped from canonical)

	uint32 mOvAddr;
	uint32 mOvStep;
	uint32 mOvTextRow;
	uint32 mChAddr;

	uint8 mPfPaletteIndex;
	uint8 mOvPaletteIndex;

	bool mbExtendedColor;
	bool mbAttrMapEnabled;
	uint32 mAttrAddr;
	uint32 mAttrStep;
	uint32 mAttrWidth;
	uint32 mAttrHeight;
	uint32 mAttrHscroll;
	uint32 mAttrVscroll;
	uint32 mAttrRow;

	// RAMDAC
	uint8 mPsel;
	uint8 mCsel;

	// IRQ
	bool mbIRQEnabled;
	bool mbIRQRequest;

	ATEvent *mpEventBlitterIrq = nullptr;

	// configuration latch
	uint8 mConfigLatch;

	uint32	mDMACyclesXDL = 0;				// VBXE cycles in scanline for XDL DMA
	uint32	mDMACyclesAttrMap = 0;			// VBXE cycles in scanline for attribute map DMA
	uint32	mDMACyclesOverlay = 0;			// VBXE cycles in scanline for overlay DMA
	uint32	mDMACyclesOverlayStart = 0;		// ANTIC X cycle at which overlay DMA starts.

	//==== blitter ====
	enum class BlitState : uint8 {
		// blitter is not running
		Stopped,

		// blitter should read next control block, or stop if end of blit list
		Reload,

		// blitter is processing a blit
		ProcessBlit,

		// blitter has finished, running out delay until reporting completion to 6502
		Stopping
	} mBlitState = BlitState::Stopped;

	bool mbBlitLogging = false;
	bool mbBlitLoggingCompact = false;
	bool mbBlitterContinue = false;		// true if start of blit list or last blit indicated another blit follows
	uint32 mBlitterStopTime = 0;
	uint32 mBlitterEndScanTime = 0;
	uint8 mBlitterMode;
	sint32 mBlitCyclesLeft;
	sint32 mBlitCyclesPerRow;
	sint32 mBlitCyclesSavedPerZero;
	uint32 mBlitListAddr;
	uint32 mBlitListFetchAddr;
	uint32 mBlitSrcAddr;
	sint32 mBlitSrcStepX;
	sint32 mBlitSrcStepY;
	uint32 mBlitDstAddr;
	sint32 mBlitDstStepX;
	sint32 mBlitDstStepY;
	uint32 mBlitWidth;
	uint32 mBlitHeight;
	uint32 mBlitHeightLeft;
	uint8 mBlitAndMask;
	uint8 mBlitXorMask;
	uint8 mBlitCollisionMask;
	uint8 mBlitActiveCollisionMask;
	uint8 mBlitPatternMode;
	uint8 mBlitCollisionCode;
	uint8 mBlitZoomX;
	uint8 mBlitZoomY;
	uint8 mBlitZoomCounterY;

	// scan out
	const uint32 *mpPfPalette;
	const uint32 *mpOvPalette;

	const uint8 *mpMergeBuffer;
	const uint8 *mpAnticBuffer;
	const uint8 *mpMergeBuffer0;
	const uint8 *mpAnticBuffer0;

	uint32 *mpDst;
	int mX;
	int mRCIndex;
	int mRCCount;

	bool mbHiresMode;
	bool mbAnalysisMode;
	uint8 mPRIOR;

	struct PriorityInfo {
		uint8 mMapColor;
		uint8 mPalColor;
		uint8 mOvPri;
		uint8 unused;
	};

	const PriorityInfo *mpPriTable;
	const PriorityInfo *mpPriTableHi;
	const uint8 *mpColorTable;

	ATMemoryLayer *mpMemLayerMEMACA;
	ATMemoryLayer *mpMemLayerMEMACB;
	ATMemoryLayer *mpMemLayerRegisters;
	ATMemoryLayer *mpMemLayerGTIAOverlay;

	typedef vdfastvector<RegisterChange> RegisterChanges;
	RegisterChanges mRegisterChanges;

	ATTraceChannelSimple *mpTraceChannelOverlay = nullptr;
	ATTraceChannelFormatted *mpTraceChannelBlit = nullptr;
	uint64	mTraceBlitStartTime = 0;

	ATVBXEXDLHistoryEntry mXDLLastHistoryEntry {};
	ATVBXEXDLHistoryEntry mXDLHistory[240] {};	// 4.8K

	int		mRenderPaletteCorrectionMode = 0;
	int		mRenderPaletteCorrectedMode = 0;
	bool	mbRenderPaletteChanged = false;
	bool	mbRenderPaletteSigned = false;
	ATPaletteCorrector *mpPaletteCorrector = nullptr;

	uint8	mColorTable[24];
	uint8	mColorTableExt[24];
	PriorityInfo	mPriorityTables[32][256] {};	// 32K
	PriorityInfo	mPriorityTablesHi[32][256] {};	// 32K

	uint32	mPalette[4][256];			// render palette (corrected version)
	uint32	mArchPalette[4][256];		// architectural palette (uncorrected version)
	uint32	mDefaultPalette[256];

	uint8	mOverlayDecode[912];		// 28MHz (640) - decoded pixels
	uint8	mOvPriDecode[456*2];		// 14MHz (320) - priority,collision pairs
	uint8	mOvTextTrans[912];			// 28MHz (640) - text translucency pixel masks

	struct AttrPixel {
		uint8 mColors[4];	// BAK/PF0/PF1/PF2
		uint8 mCtrl;
		uint8 mHiresFlag;
		uint8 mPriority;
		uint8 _mUnused;
	};

	AttrPixel	mAttrPixels[456];
	uint8	mAttrBuffer[64 * 4] {};

	uint8	mTempMergeBuffer[228];
	uint8	mTempAnticData[228];

	static const OvMode kOvModeTable[3][4];
};

class IATVBXEDevice : public IVDUnknown {
public:
	enum : uint32 { kTypeID = "vbxd"_vdfcctypeid };

	virtual void SetSharedMemory(void *mem) = 0;

	virtual bool GetSharedMemoryMode() const = 0;
	virtual void SetSharedMemoryMode(bool sharedMemory) = 0;

	virtual bool GetAltPageEnabled() const = 0;
	virtual void SetAltPageEnabled(bool enabled) = 0;
};

#endif
