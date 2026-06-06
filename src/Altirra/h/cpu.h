//	Altirra - Atari 800/800XL emulator
//	Copyright (C) 2008 Avery Lee
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

#ifndef AT_CPU_H
#define AT_CPU_H

#ifdef _MSC_VER
	#pragma once
#endif

#include <vd2/system/vdtypes.h>
#include <at/atcore/enumparse.h>
#include <at/atcpu/history.h>

class ATCPUEmulator;
class ATCPUHookManager;
class ATCPUProfiler;
class ATCPUVerifier;
class ATCPUHeatMap;
class ATSaveStateReader;
class ATCPUEmulatorMemory;
class ATBreakpointManager;
struct ATCPUHistoryEntry;
class ATSnapEncoder;
class IATObjectState;

enum ATDebugDisasmMode : uint8;

class ATCPUEmulatorCallbacks {
public:
	virtual uint32 CPUGetCycle() = 0;
	virtual uint32 CPUGetUnhaltedCycle() = 0;
	virtual uint32 CPUGetUnhaltedAndRDYCycle() = 0;
	virtual void CPUGetHistoryTimes(ATCPUHistoryEntry * VDRESTRICT he) const = 0;
};

enum ATCPUStepResult : uint8 {
	kATCPUStepResult_Continue,
	kATCPUStepResult_SkipCall,
	kATCPUStepResult_Stop
};

typedef ATCPUStepResult (*ATCPUStepCallback)(ATCPUEmulator *cpu, uint32 pc, bool call, void *data);

struct ATCPUStepCondition;

struct ATCPUStepConditionLink {
	ATCPUStepCondition *mpNext = nullptr;
	bool mbRegistered = false;

	ATCPUStepConditionLink();
	ATCPUStepConditionLink(const ATCPUStepConditionLink&);
	~ATCPUStepConditionLink();

	ATCPUStepConditionLink& operator=(const ATCPUStepConditionLink&);

	bool IsRegistered() const { return mbRegistered; }
};

struct ATCPUStepCondition : public ATCPUStepConditionLink {
	// Ignore calls/interrupts below the stack level where the step condition
	// is triggered.
	bool mbStepOver = false;

	// Only break on an RTS instruction.
	bool mbStepOut = false;

	// Disable this step condition until an NMI has occurred, after which this
	// flag is cleared.
	bool mbWaitForNMI = false;

	// S register range for valid condition. Only valid if: (u8)(s - start) >= length.
	uint8 mIgnoreStackStart = 0;
	uint8 mIgnoreStackLength = 0;

	// PC range to ignore. The step condition is ignored if
	// (u16)(pc - start) >= length. Length = 0 disables the range condition.
	// Only the 16-bit PC is used.
	uint16 mIgnorePCStart = 0;
	uint16 mIgnorePCLength = 0;

	// Step callback called when the condition is met. If the callback is not set,
	// the 
	ATCPUStepCallback mpCallback = nullptr;
	void *mpCallbackData = nullptr;

	static ATCPUStepCondition CreateSingleStep();
	static ATCPUStepCondition CreateAtStackLevel(uint8 s);
	static ATCPUStepCondition CreateNMI();
};

enum ATCPUMode : uint8 {
	kATCPUMode_6502,
	kATCPUMode_65C02,
	kATCPUMode_65C816,
	kATCPUModeCount
};

AT_DECLARE_ENUM_TABLE(ATCPUMode);

enum ATCPUSubMode : uint8 {
	kATCPUSubMode_6502,
	kATCPUSubMode_65C02,
	kATCPUSubMode_65C816_Emulation,
	kATCPUSubMode_65C816_NativeM16X16,
	kATCPUSubMode_65C816_NativeM16X8,
	kATCPUSubMode_65C816_NativeM8X16,
	kATCPUSubMode_65C816_NativeM8X8,
	kATCPUSubModeCount
};

enum ATCPUAdvanceMode {
	kATCPUAdvanceMode_6502,
	kATCPUAdvanceMode_65816,
	kATCPUAdvanceMode_65816HiSpeed
};

namespace AT6502 {
	enum {
		kFlagN = 0x80,
		kFlagV = 0x40,
		kFlagM = 0x20,		// 65C816 native mode only
		kFlagX = 0x10,		// 65C816 native mode only
		kFlagB = 0x10,
		kFlagD = 0x08,
		kFlagI = 0x04,
		kFlagZ = 0x02,
		kFlagC = 0x01
	};
}

class ATCPUEmulator {
	ATCPUEmulator(const ATCPUEmulator&) = delete;
	ATCPUEmulator& operator=(const ATCPUEmulator&) = delete;
public:
	ATCPUEmulator();
	~ATCPUEmulator();

	bool	Init(ATCPUEmulatorMemory *mem, ATCPUHookManager *hookmgr, ATCPUEmulatorCallbacks *callbacks);

	ATCPUEmulatorMemory *GetMemory() const { return mpMemory; }
	ATCPUHookManager *GetHookManager() const { return mpHookMgr; }

	void	SetBreakpointManager(ATBreakpointManager *bkptmanager);

	void	ColdReset();
	void	WarmReset();

	bool	GetUnusedCycle() {
		bool b = mbUnusedCycle;
		mbUnusedCycle = false;
		return b;
	}

	bool	IsAtInsnStep() const;
	bool	IsInstructionInProgress() const;
	bool	IsNextCycleWrite() const;
	uint8	GetHeldCycleValue();

	void	ForceNextCycleSlow() {
		mSubCyclesLeft = 1;
		mbForceNextCycleSlow = true;
	}

	bool	GetEmulationFlag() const { return mbEmulationFlag; }
	uint16	GetInsnPC() const { return mInsnPC; }
	uint16	GetPC() const { return mPC; }
	uint32	GetXPC() const;
	uint8	GetP() const { return mP; }
	uint8	GetS() const { return mS; }
	uint16	GetS16() const { return mS + ((uint32)mSH << 8); }
	uint8	GetA() const { return mA; }
	uint8	GetX() const { return mX; }
	uint8	GetY() const { return mY; }

	void	SetEmulationFlag(bool emu);
	void	SetPC(uint16 pc);
	void	SetP(uint8 p);
	void	SetA(uint8 a) { mA = a; }
	void	SetX(uint8 x) { mX = x; }
	void	SetY(uint8 y) { mY = y; }
	void	SetS(uint8 s) { mS = s; }
	void	SetAH(uint8 a);
	void	SetXH(uint8 x);
	void	SetYH(uint8 y);
	void	SetSH(uint8 s);
	void	SetD(uint16 dp);
	void	SetK(uint8 k);
	void	SetB(uint8 b);

	uint8	GetAH() const { return mAH; }
	uint8	GetXH() const { return mXH; }
	uint8	GetYH() const { return mYH; }
	uint8	GetSH() const { return mSH; }
	uint8	GetB() const { return mB; }
	uint8	GetK() const { return mK; }
	uint16	GetD() const { return mDP; }

	void	SetFlagC() { mP |= AT6502::kFlagC; }
	void	ClearFlagC() { mP &= ~AT6502::kFlagC; }

	void	SetHook(uint16 pc, bool enable);

	bool	IsStepConditionSatisfied(const ATCPUStepCondition& condition) const;
	void	AddStepCondition(ATCPUStepCondition& condition);
	void	RemoveStepCondition(ATCPUStepCondition& condition);

	void	SetTrace(bool trace) {
		mbTrace = trace;
		if (trace)
			mDebugFlags |= kDebugFlag_Trace;
		else
			mDebugFlags &= ~kDebugFlag_Trace;
	}

	void	SetCPUMode(ATCPUMode mode, uint32 subCycles);
	ATCPUMode GetCPUMode() const { return mCPUMode; }
	ATDebugDisasmMode GetDisasmMode() const { return (ATDebugDisasmMode)mCPUMode; }
	uint32 GetSubCycles() const { return mSubCycles; }
	ATCPUSubMode GetCPUSubMode() const { return mCPUSubMode; }
	ATCPUAdvanceMode GetAdvanceMode() const { return mAdvanceMode; }

	bool	IsHistoryEnabled() const { return mbHistoryEnabled; }
	void	SetHistoryEnabled(bool enable);

	void	SetTracingEnabled(bool enable);

	bool	IsPathfindingEnabled() const { return mbPathfindingEnabled; }
	void	SetPathfindingEnabled(bool enable);

	bool	IsPathBreakEnabled() const { return mbPathBreakEnabled; }
	void	SetPathBreakEnabled(bool enable) { mbPathBreakEnabled = enable; }

	void	SetProfiler(ATCPUProfiler *profiler);
	void	SetVerifier(ATCPUVerifier *verifier);

	ATCPUHeatMap *GetHeatMap() const { return mpHeatMap; }
	void	SetHeatMap(ATCPUHeatMap *heatmap);

	bool	AreIllegalInsnsEnabled() const { return mbIllegalInsnsEnabled; }
	void	SetIllegalInsnsEnabled(bool enable);

	bool	GetStopOnBRK() const { return mbStopOnBRK; }
	void	SetStopOnBRK(bool en);

	bool	IsNMIBlockingEnabled() const { return mbAllowBlockedNMIs; }
	void	SetNMIBlockingEnabled(bool enable);

	uint32	GetBreakpointCount() const;
	bool	IsBreakpointSet(uint16 addr) const;
	sint32	GetNextBreakpoint(sint32 last) const;
	void	SetBreakpoint(uint16 addr);
	void	ClearBreakpoint(uint16 addr);
	void	SetAllBreakpoints();
	void	ClearAllBreakpoints();

	void	ResetAllPaths();
	sint32	GetNextPathInstruction(sint32 addr) const;
	bool	IsPathStart(uint16 addr) const;
	bool	IsInPath(uint16 addr) const;

	const ATCPUHistoryEntry& GetHistory(uint32 i) const {
		const ATCPUHistoryEntry *base = mHistory.begin();
		ptrdiff_t pos = (const char *)mpHistoryNext - (const char *)base;
		ptrdiff_t offset = ((i + 1) & (kHistoryLength - 1)) * sizeof(ATCPUHistoryEntry);

		pos -= offset;
		if (pos < 0)
			pos += kHistoryLength * sizeof(ATCPUHistoryEntry);

		return *(const ATCPUHistoryEntry *)((const char *)base + pos);
	}

	int GetHistoryLength() const { return kHistoryLength; }
	uint32	GetHistoryCounter() const { return mHistoryBase + (uint32)(mpHistoryNext - mHistory.begin()); }

	void	DumpStatus(bool extended = false);

	void	BeginLoadState(ATSaveStateReader& reader);
	void	LoadState6502(ATSaveStateReader& reader);
	void	LoadState65C816(ATSaveStateReader& reader);
	void	LoadStatePrivate(ATSaveStateReader& reader);
	void	LoadStateResetPrivate(ATSaveStateReader& reader);
	void	EndLoadState(ATSaveStateReader& reader);

	void	SaveState(IATObjectState **result);
	void	LoadState(const IATObjectState *state);
	void	LoadState(const class ATSaveStateCPU& state);

	void	InjectOpcode(uint8 op);
	void	Push(uint8 v);
	void	PushWord(uint16 v);
	uint8	Pop();
	void	Jump(uint16 addr);
	void	Ldy(uint8 v);

	void	AssertIRQ(int cycleOffset);
	void	NegateIRQ();
	void	AssertNMI();
	void	AssertABORT();
	void	AssertRDY();
	void	NegateRDY();

	// Low-priority call needed to clean up timers to avoid time base wrapping. This needs
	// to be called no more than every 2^30 cycles, so not critical.
	void	PeriodicCleanup();

	int		Advance();
	int		Advance6502();
	int		Advance65816();
	int		Advance65816HiSpeed(bool dma);

protected:
	friend class ATSaveStateCPU;

	VDNOINLINE uint8 ProcessDebugging();
	VDNOINLINE void ProcessStepOver();
	VDNOINLINE uint8 ProcessHook();
	VDNOINLINE uint8 ProcessHook816();

	template<bool T_Accel>
	bool	ProcessInterrupts();

	template<bool is816, bool subCycles>
	void	AddHistoryEntry(bool slowFlag);

	void	UpdatePendingIRQState();
	void	RecomputeStepConditions();

	void	RedecodeInsnWithoutBreak();
	void	Update65816DecodeTable();
	void	RebuildDecodeTables();
	void	RebuildDecodeTables6502(bool cmos);
	void	RebuildDecodeTables65816();
	bool	Decode6502(uint8 opcode);
	bool	Decode6502Ill(uint8 opcode);
	bool	Decode65C02(uint8 opcode);
	bool	Decode65C816(uint8 opcode, bool unalignedDP, bool emu, bool mode16, bool index16);
	void	DecodeReadImm();
	void	DecodeReadZp();
	void	DecodeReadZpX();
	void	DecodeReadZpY();
	void	DecodeReadAbs();
	void	DecodeReadAbsX();
	void	DecodeWriteAddrAbsX();
	void	DecodeReadAbsY();
	void	DecodeWriteAddrAbsY();
	void	DecodeReadIndX();
	void	DecodeReadIndY();
	void	DecodeWriteAddrIndY();
	void	DecodeReadInd();

	void	Decode65816AddrDp(bool unalignedDP);
	void	Decode65816AddrDpX(bool unalignedDP, bool emu);
	void	Decode65816AddrDpY(bool unalignedDP, bool emu);
	void	Decode65816AddrDpInd(bool unalignedDP, bool emu);
	void	Decode65816AddrDpIndX(bool unalignedDP, bool emu);
	void	Decode65816AddrDpIndY(bool unalignedDP, bool emu, bool forceCycle);
	void	Decode65816AddrDpLongInd(bool unalignedDP);
	void	Decode65816AddrDpLongIndY(bool unalignedDP, bool emu);
	void	Decode65816AddrAbs();
	void	Decode65816AddrAbsX(bool forceCycle);
	void	Decode65816AddrAbsY(bool forceCycle);
	void	Decode65816AddrAbsLong();
	void	Decode65816AddrAbsLongX();
	void	Decode65816AddrStackRel();
	void	Decode65816AddrStackRelInd();

	const uint8 *mpNextState;

	// The register arrangement here is set up to allow DWORD copies into
	// history entries.
	uint8	mA;
	uint8	mX;
	uint8	mY;
	uint8	mS;

	bool	mbHistoryActive;
	uint8	mP;
	uint16	mInsnPC;
	uint16	mPC;
	uint16	mAddr;
	uint16	mAddr2;
	uint8	mRelOffset;
	uint8	mData;
	uint8	mOpcode;
	uint16	mData16;
	uint8	mAddrBank;

	uint8	mB;		// data bank register
	uint8	mK;		// program bank register
	uint8	mSH;
	uint8	mAH;
	uint8	mXH;
	uint8	mYH;
	uint16	mDP;

	// These are in bitfields so the insn fetch code can check them all at once.
	enum : uint8 {
		kIntFlag_IRQReleasePending = 0x01,
		kIntFlag_IRQSetPending = 0x02,
		kIntFlag_IRQActive = 0x04,
		kIntFlag_IRQPending = 0x08,
		kIntFlag_NMIPending = 0x10
	};

	uint8	mIntFlags;

	bool	mbNMIForced;

	bool	mbTrace;			// must also affect mDebugFlags
	bool	mbStep;				// must also affect mDebugFlags
	bool	mbStepOver;			// set if we want to avoid subroutines

	enum {
		kDebugFlag_Step = 0x01,		// mbStep is set
		kDebugFlag_SBrk = 0x02,		// mSBrk is active
		kDebugFlag_Trace = 0x04,	// mbTrace is set
		kDebugFlag_BP = 0x08,		// Breakpoints are set
		kDebugFlag_StepNMI = 0x10	// NMI step is pending
	};

	uint8	mDebugFlags;

	uint16	mStepIgnorePCStart = 0;
	uint16	mStepIgnorePCLength = 0;
	uint8	mStepIgnoreStackStart = 0;
	uint8	mStepIgnoreStackLength = 0;

	uint32	mSubCyclesLeft;
	bool	mbForceNextCycleSlow;
	bool	mbUnusedCycle;
	bool	mbEmulationFlag;
	uint32	mNMIIgnoreUnhaltedCycle;
	uint32	mNMIAssertTime;
	uint32	mIRQAssertTime;
	uint32	mIRQAcknowledgeTime;
	uint32	mSBrk;				// must also affect mDebugFlags
	ATCPUMode	mCPUMode;
	uint32	mSubCycles;
	ATCPUSubMode	mCPUSubMode;
	uint8	mDecodeTableMode816;
	ATCPUAdvanceMode	mAdvanceMode;

	ATCPUEmulatorMemory	*mpMemory;
	ATCPUHookManager *mpHookMgr;
	ATCPUEmulatorCallbacks	*mpCallbacks;
	ATBreakpointManager *mpBkptManager;

	ATCPUProfiler	*mpProfiler;
	ATCPUVerifier	*mpVerifier;
	ATCPUHeatMap	*mpHeatMap;

	uint8 *mpDstState;
	uint8	mStates[16];
	uint16	mStateRelocOffset = 0;

	ATCPUHistoryEntry *mpHistoryNext = nullptr;
	uint32	mHistoryBase = 0;

	static constexpr uint32 kHistoryLength = 131072;
	vdblock<ATCPUHistoryEntry> mHistory;

	enum HistoryEnableFlags : uint8 {
		kHistoryEnableFlag_None = 0x00,
		kHistoryEnableFlag_Direct = 0x01,
		kHistoryEnableFlag_Profiler = 0x02,
		kHistoryEnableFlag_Tracer = 0x04
	};

	HistoryEnableFlags	mHistoryEnableFlags;
	bool	mbHistoryEnabled;
	bool	mbPathfindingEnabled;
	bool	mbPathBreakEnabled;
	bool	mbIllegalInsnsEnabled;
	bool	mbStopOnBRK;
	bool	mbMarkHistoryIRQ;
	bool	mbMarkHistoryNMI;
	bool	mbAllowBlockedNMIs;

	uint32	mBreakpointCount;

	ATCPUStepCondition *mpStepConditions = nullptr;
	ATCPUStepCondition *mpStepConditionsIterator = nullptr;

	enum {
		kInsnFlagBreakPt		= 0x01,
		kInsnFlagHook			= 0x08,
		kInsnFlagPathStart		= 0x10,
		kInsnFlagPathExecuted	= 0x20
	};

	enum class ExtOpcode : uint16 {
		Irq = 256,
		Nmi,
		Reset,
		Abort,
	};

	static constexpr uint32 kNumExtOpcodes = 260;

	uint16	mDecodePtrs[kNumExtOpcodes];
	uint16	mDecodePtrs816[10][kNumExtOpcodes];
	uint8	mDecodeHeap[0x5000];
	uint8	mInsnFlags[65536];

	struct ExtOpcodeAnchor {
		uint16 mOffset;
		uint16 mExtOpcode;
	};

	struct ExtOpcodeAnchorPred;

	uint32 mNumExtOpcodeAnchors;
	ExtOpcodeAnchor	mExtOpcodeAnchors[10 * kNumExtOpcodes];
};

#endif
