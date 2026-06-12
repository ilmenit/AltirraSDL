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

#include <stdafx.h>
#include <vd2/system/binary.h>
#include <at/atcore/enumparseimpl.h>
#include <at/atcore/serialization.h>
#include <at/atcore/snapshotimpl.h>
#include <at/atdebugger/target.h>
#include "cpu.h"
#include "cpustates.h"
#include "console.h"
#include "disasm.h"
#include "profiler.h"
#include "savestate.h"
#include "simulator.h"
#include "verifier.h"
#include "bkptmanager.h"
#include "cpuhookmanager.h"
#include "cpuheatmap.h"

using namespace AT6502;

AT_DEFINE_ENUM_TABLE_BEGIN(ATCPUMode)
	{ kATCPUMode_6502, "6502" },
	{ kATCPUMode_65C02, "65c02" },
	{ kATCPUMode_65C816, "65c816" },
AT_DEFINE_ENUM_TABLE_END(ATCPUMode, kATCPUMode_6502)

// #define PROFILE_OPCODES

#ifdef PROFILE_OPCODES
	class ATCPUOpcodeProfiler {
	public:
		~ATCPUOpcodeProfiler() {
			double scores[256] {};

			for(const auto& e : mPCInsnPairs) {
				scores[e.first & 0xFF] += log((double)e.second);
			}

			uint8 order[256];

			for(uint32 i=0; i<256; ++i) {
				order[i] = (uint8)i;
			}

			std::stable_sort(std::begin(order), std::end(order), [&](uint8 i, uint8 j) { return scores[i] > scores[j]; });

			for(uint32 i=0; i<256; i += 16) {
				VDDEBUG2("0x%02X,0x%02X,0x%02X,0x%02X,0x%02X,0x%02X,0x%02X,0x%02X,0x%02X,0x%02X,0x%02X,0x%02X,0x%02X,0x%02X,0x%02X,0x%02X,\n",
					order[i+ 0], order[i+ 1], order[i+ 2], order[i+ 3],
					order[i+ 4], order[i+ 5], order[i+ 6], order[i+ 7],
					order[i+ 8], order[i+ 9], order[i+10], order[i+11],
					order[i+12], order[i+13], order[i+14], order[i+15]
				);
			}
		}

		void OnOpcode(uint16 pc, uint8 opcode) {
			++mPCInsnPairs[((uint32)pc << 16) + ((uint32)mLastOpcode << 8) + opcode];
			mLastOpcode = opcode;
		}

	private:
		uint8 mLastOpcode = 0;

		vdhashmap<uint32, uint32> mPCInsnPairs;
	} g_ATCPUOpcodeProfiler;

	#define ATCPU_PROFILE_OPCODE(pc, opcode) g_ATCPUOpcodeProfiler.OnOpcode(pc, opcode)
#else
	#define ATCPU_PROFILE_OPCODE(pc, opcode) ((void)0)
#endif

////////////////////////////////////////////////////////////////////////////////

ATCPUStepConditionLink::ATCPUStepConditionLink() {
}

ATCPUStepConditionLink::ATCPUStepConditionLink(const ATCPUStepConditionLink&) {
}

ATCPUStepConditionLink::~ATCPUStepConditionLink() {
}

ATCPUStepConditionLink& ATCPUStepConditionLink::operator=(const ATCPUStepConditionLink&) {
	if (mbRegistered) {
		[[unlikely]]
		VDRaiseInternalFailure();
	}

	return *this;
}

////////////////////////////////////////////////////////////////////////////////

ATCPUStepCondition ATCPUStepCondition::CreateSingleStep() {
	return {};
}

ATCPUStepCondition ATCPUStepCondition::CreateAtStackLevel(uint8 s) {
	ATCPUStepCondition c{};

	c.mIgnoreStackStart = s ^ 0x80;
	c.mIgnoreStackLength = 0x80;

	return c;
}

ATCPUStepCondition ATCPUStepCondition::CreateNMI() {
	ATCPUStepCondition c{};
	c.mbWaitForNMI = true;
	return c;
}

////////////////////////////////////////////////////////////////////////////////

struct ATCPUEmulator::ExtOpcodeAnchorPred {
	bool operator()(const ExtOpcodeAnchor& x, const ExtOpcodeAnchor& y) const {
		return x.mOffset < y.mOffset;
	}

	bool operator()(const ExtOpcodeAnchor& x, uint16 y) const {
		return x.mOffset < y;
	}

	bool operator()(uint16 x, const ExtOpcodeAnchor& y) const {
		return x < y.mOffset;
	}
};

ATCPUEmulator::ATCPUEmulator()
	: mpMemory(NULL)
	, mpHookMgr(NULL)
	, mpProfiler(NULL)
	, mpVerifier(NULL)
	, mpHeatMap(NULL)
	, mpBkptManager(NULL)
{
	static_assert((kHistoryLength & (kHistoryLength - 1)) == 0, "History size must be power of two");

	mHistory.resize(kHistoryLength);

	mSBrk = 0x100;
	mbTrace = false;
	mbStep = false;
	mDebugFlags = 0;
	mpNextState = mStates;
	mStates[0] = kStateReadOpcode;
	mCPUMode = kATCPUMode_6502;
	mSubCycles = 1;
	mSubCyclesLeft = 1;
	mbForceNextCycleSlow = false;
	mCPUSubMode = kATCPUSubMode_6502;
	mAdvanceMode = kATCPUAdvanceMode_6502;

	memset(mInsnFlags, 0, sizeof mInsnFlags);

	mbHistoryActive = false;
	mHistoryEnableFlags = kHistoryEnableFlag_None;
	mbPathfindingEnabled = false;
	mbPathBreakEnabled = false;
	mbIllegalInsnsEnabled = true;
	mbStopOnBRK = false;
	mbAllowBlockedNMIs = false;
	mBreakpointCount = 0;

	RebuildDecodeTables();
	
	VDASSERTCT(kStateCount < 256);
	
	VDASSERTCT((int)kATCPUMode_6502 == (int)kATDebugDisasmMode_6502);
	VDASSERTCT((int)kATCPUMode_65C02 == (int)kATDebugDisasmMode_65C02);
	VDASSERTCT((int)kATCPUMode_65C816 == (int)kATDebugDisasmMode_65C816);
}

ATCPUEmulator::~ATCPUEmulator() {
}

bool ATCPUEmulator::Init(ATCPUEmulatorMemory *mem, ATCPUHookManager *hookmgr, ATCPUEmulatorCallbacks *callbacks) {
	mpMemory = mem;
	mpHookMgr = hookmgr;
	mpCallbacks = callbacks;

	mbStep = false;
	mbTrace = false;
	mDebugFlags &= ~(kDebugFlag_Step | kDebugFlag_StepNMI | kDebugFlag_Trace);

	ColdReset();

	std::fill(mHistory.begin(), mHistory.end(), ATCPUHistoryEntry{});
	mHistoryBase = 0;
	mpHistoryNext = mHistory.begin();

	return true;
}

void ATCPUEmulator::SetBreakpointManager(ATBreakpointManager *bkptmanager) {
	mpBkptManager = bkptmanager;
}

void ATCPUEmulator::ColdReset() {
	WarmReset();
	mNMIIgnoreUnhaltedCycle = 0xFFFFFFFFU;
}

void ATCPUEmulator::WarmReset() {
	mpNextState = mDecodeHeap + mDecodePtrs[(uint16)ExtOpcode::Reset];
	mIntFlags = 0;
	mbNMIForced = false;
	mbUnusedCycle = false;
	mbMarkHistoryIRQ = false;
	mbMarkHistoryNMI = false;
	mInsnPC = 0xFFFC;
	mPC = 0xFFFC;
	mP = 0x30 | kFlagI;
	mbEmulationFlag = true;

	// 65C816 initialization
	if (mCPUMode == kATCPUMode_65C816) {
		mSH = 0x01;
		mDP = 0x0000;
		mXH = 0;
		mYH = 0;
		mB = 0;
		mK = 0;

		// Set MXIE. Clear D. NVZC are preserved.
		mP = (mP & (kFlagN | kFlagV | kFlagZ | kFlagC)) | (kFlagM | kFlagX | kFlagI);

		// force reinit
		mDecodeTableMode816 = 0xFF;
		Update65816DecodeTable();
	}

	if (mpVerifier)
		mpVerifier->OnReset();
}

bool ATCPUEmulator::IsAtInsnStep() const {
	uint8 state = *mpNextState;

	if (state == kStateUpdateHeatMap)
		state = mpNextState[1];

	return state == kStateReadOpcodeNoBreak;
}

bool ATCPUEmulator::IsInstructionInProgress() const {
	uint8 state = *mpNextState;

	if (state == kStateUpdateHeatMap)
		state = mpNextState[1];

	return state != kStateReadOpcode && state != kStateReadOpcodeNoBreak;
}

bool ATCPUEmulator::IsNextCycleWrite() const {
	const uint8 *p = mpNextState;
	for(;;) {
		const uint8 state = *p++;

		switch(state) {
			case kStateReadOpcode:
			case kStateReadOpcodeNoBreak:
			case kStateReadDummyOpcode:
			case kStateReadImm:
			case kStateReadAddrL:
			case kStateReadAddrH:
			case kStateReadAddrHX:
			case kStateReadAddrHY:
			case kStateReadAddrHX_SHY:
			case kStateRead:
			case kStateReadAddX:
			case kStateReadAddY:
			case kStateReadCarry:
			case kStateReadCarryForced:
			case kStateReadAbsIndAddr:
			case kStateReadAbsIndAddrBroken:
			case kStateReadIndAddr:
			case kStateReadIndYAddr:
			case kStateReadIndYAddr_SHA:
			case kStatePop:
			case kStatePopPCL:
			case kStatePopPCH:
			case kStatePopPCHP1:
			case kStateReadRel:
			case kStateReadImmL16:
			case kStateReadImmH16:
			case kStateReadAddrDp:
			case kStateReadAddrDpX:
			case kStateReadAddrDpY:
			case kStateReadIndAddrDp:
			case kStateReadIndAddrDpY:
			case kStateReadIndAddrDpLongH:
			case kStateReadIndAddrDpLongB:
			case kState816ReadIndAddrDpLongHInPage:
			case kState816ReadIndAddrDpLongBInPage:
			case kStateReadAddrAddY:
			case kState816ReadAddrL:
			case kState816ReadAddrH:
			case kState816ReadAddrHX:
			case kState816ReadAddrAbsXSpec:
			case kState816ReadAddrAbsXAlways:
			case kState816ReadAddrAbsYSpec:
			case kState816ReadAddrAbsYAlways:
			case kStateRead816AddrAbsLongL:
			case kStateRead816AddrAbsLongH:
			case kStateRead816AddrAbsLongB:
			case kStateReadAddrB:
			case kStateReadAddrBX:
			case kStateReadAddrSO:
			case kState816ReadAddrSO_AddY:
			case kState816ReadByte:
			case kStateReadL16:
			case kStateReadH16:
			case kStateReadH16_DpBank:
			case kStatePopNative:
			case kStatePopL16:
			case kStatePopH16:
			case kStatePopPBKNative:
			case kStatePopPCLNative:
			case kStatePopPCHNative:
			case kStatePopPCHP1Native:
			case kState816_MoveRead:
			case kStateWait:
				return false;

			case kStateWrite:
			case kStatePush:
			case kStatePushPCL:
			case kStatePushPCH:
			case kStatePushPCLM1:
			case kStatePushPCHM1:
			case kState816WriteByte:
			case kStateWriteL16:
			case kStateWriteH16:
			case kStateWriteH16_DpBank:
			case kStatePushNative:
			case kStatePushL16:
			case kStatePushH16:
			case kStatePushPBKNative:
			case kStatePushPCLNative:
			case kStatePushPCHNative:
			case kStatePushPCLM1Native:
			case kStatePushPCHM1Native:
			case kState816_MoveWriteP:
			case kState816_MoveWriteN:
				return true;
		}
	}
}

uint32 ATCPUEmulator::GetXPC() const {
	return mpMemory->mpCPUReadAddressPageMap[(uint8)(mPC >> 8)] + mPC;
}

void ATCPUEmulator::SetEmulationFlag(bool emu) {
	if (mCPUMode != kATCPUMode_65C816)
		return;

	if (mbEmulationFlag == emu)
		return;

	mbEmulationFlag = emu;

	Update65816DecodeTable();
}

void ATCPUEmulator::SetPC(uint16 pc) {
	mPC = pc;
	mInsnPC = pc;
	mpDstState = mStates;
	mpNextState = mStates;
	*mpDstState++ = kStateReadOpcodeNoBreak;
}

void ATCPUEmulator::SetP(uint8 p) {
	if (mCPUMode != kATCPUMode_65C816 || mCPUSubMode == kATCPUSubMode_65C816_Emulation)
		p |= 0x30;

	if (mP != p) {
		mP = p;

		if (!(p & AT6502::kFlagX)) {
			mXH = 0;
			mYH = 0;
		}
	}
}

void ATCPUEmulator::SetAH(uint8 a) {
	if (!(mP & AT6502::kFlagM))
		mAH = a;
}

void ATCPUEmulator::SetXH(uint8 x) {
	if (!(mP & AT6502::kFlagX))
		mXH = x;
}

void ATCPUEmulator::SetYH(uint8 y) {
	if (!(mP & AT6502::kFlagX))
		mYH = y;
}

void ATCPUEmulator::SetSH(uint8 s) {
	if (!mbEmulationFlag)
		mSH = s;
}

void ATCPUEmulator::SetD(uint16 dp) {
	if (mCPUMode == kATCPUMode_65C816)
		mDP = dp;
}

void ATCPUEmulator::SetK(uint8 k) {
	if (mCPUMode == kATCPUMode_65C816)
		mK = k;
}

void ATCPUEmulator::SetB(uint8 b) {
	if (mCPUMode == kATCPUMode_65C816)
		mB = b;
}

void ATCPUEmulator::SetHook(uint16 pc, bool enable) {
	if (enable)
		mInsnFlags[pc] |= kInsnFlagHook;
	else
		mInsnFlags[pc] &= ~kInsnFlagHook;
}

bool ATCPUEmulator::IsStepConditionSatisfied(const ATCPUStepCondition& condition) const {
	if (!condition.mbRegistered)
		return false;

	if (condition.mbWaitForNMI)
		return false;

	if ((uint32)(mPC - condition.mIgnorePCStart) < condition.mIgnorePCLength)
		return false;

	if ((uint32)(mS - condition.mIgnoreStackStart) < condition.mIgnoreStackLength)
		return false;

	return true;
}

void ATCPUEmulator::AddStepCondition(ATCPUStepCondition& condition) {
	if (condition.mbRegistered) {
		[[unlikely]]
		VDRaiseInternalFailure();
	}

	condition.mpNext = mpStepConditions;
	condition.mbRegistered = true;

	mpStepConditions = &condition;

	RecomputeStepConditions();
}

void ATCPUEmulator::RemoveStepCondition(ATCPUStepCondition& condition) {
	if (!condition.mbRegistered)
		return;

	ATCPUStepCondition **prev = &mpStepConditions;
	while(ATCPUStepCondition *p = *prev) {
		if (p == &condition) {
			*prev = p->mpNext;

			if (mpStepConditionsIterator == p)
				mpStepConditionsIterator = p->mpNext;

			condition.mbRegistered = false;
			condition.mpNext = nullptr;

			RecomputeStepConditions();
			return;
		}
	}

	VDRaiseInternalFailure();
}

sint32 ATCPUEmulator::GetNextBreakpoint(sint32 last) const {
	if ((uint32)last >= 0x10000)
		last = -1;

	for(++last; last < 0x10000; ++last) {
		if (mInsnFlags[last] & kInsnFlagBreakPt)
			return last;
	}

	return -1;
}

uint8 ATCPUEmulator::GetHeldCycleValue() {
	if (mCPUMode == kATCPUMode_65C816)
		return mpMemory->ExtReadByte(mPC, mK);
	else
		return mpMemory->ReadByte(mPC);
}

void ATCPUEmulator::SetCPUMode(ATCPUMode mode, uint32 subCycles) {
	if (subCycles < 1 || mode != kATCPUMode_65C816)
		subCycles = 1;

	if (subCycles > 24)
		subCycles = 24;

	if (mCPUMode == mode && mSubCycles == subCycles)
		return;

	const bool resetRequired = (mCPUMode != mode);

	mSubCycles = subCycles;
	mSubCyclesLeft = 1;
	mbForceNextCycleSlow = true;

	// ensure sane 65C816 state if we are changing CPU mode
	if (mCPUMode != mode) {
		mCPUMode = mode;

		mbEmulationFlag = true;
		mP |= 0x30;
		mSH = 1;
		mXH = 0;
		mYH = 0;
		mAH = 0;
		mDP = 0;
		mK = 0;
		mB = 0;
	}

	switch(mode) {
		case kATCPUMode_65C816:
			mCPUSubMode = kATCPUSubMode_65C816_Emulation;
			mAdvanceMode = subCycles > 1 ? kATCPUAdvanceMode_65816HiSpeed : kATCPUAdvanceMode_65816;
			break;
		case kATCPUMode_65C02:
			mCPUSubMode = kATCPUSubMode_65C02;
			mAdvanceMode = kATCPUAdvanceMode_6502;
			break;
		case kATCPUMode_6502:
			mCPUSubMode = kATCPUSubMode_6502;
			mAdvanceMode = kATCPUAdvanceMode_6502;
			break;
	}

	RebuildDecodeTables();

	// if we changed the CPU mode, force a warmstart
	if (resetRequired)
		WarmReset();
}

void ATCPUEmulator::SetHistoryEnabled(bool enable) {
	if (mbHistoryEnabled != enable) {
		mbHistoryEnabled = enable;

		if (enable)
			mHistoryEnableFlags = (HistoryEnableFlags)(mHistoryEnableFlags | kHistoryEnableFlag_Direct);
		else
			mHistoryEnableFlags = (HistoryEnableFlags)(mHistoryEnableFlags & ~kHistoryEnableFlag_Direct);

		mbHistoryActive = (mHistoryEnableFlags != kHistoryEnableFlag_None);

		RebuildDecodeTables();
		mbMarkHistoryIRQ = false;
		mbMarkHistoryNMI = false;
	}
}

void ATCPUEmulator::SetTracingEnabled(bool enable) {
	if (enable)
		mHistoryEnableFlags = (HistoryEnableFlags)(mHistoryEnableFlags | kHistoryEnableFlag_Tracer);
	else
		mHistoryEnableFlags = (HistoryEnableFlags)(mHistoryEnableFlags & ~kHistoryEnableFlag_Tracer);

	bool active = (mHistoryEnableFlags != kHistoryEnableFlag_None);

	if (mbHistoryActive != active) {
		mbHistoryActive = active;

		RebuildDecodeTables();
		mbMarkHistoryIRQ = false;
		mbMarkHistoryNMI = false;
	}
}

void ATCPUEmulator::SetPathfindingEnabled(bool enable) {
	if (mbPathfindingEnabled != enable) {
		mbPathfindingEnabled = enable;
		RebuildDecodeTables();
	}
}

void ATCPUEmulator::SetProfiler(ATCPUProfiler *profiler) {
	if (mpProfiler != profiler) {
		mpProfiler = profiler;

		if (profiler)
			mHistoryEnableFlags = (HistoryEnableFlags)(mHistoryEnableFlags | kHistoryEnableFlag_Profiler);
		else
			mHistoryEnableFlags = (HistoryEnableFlags)(mHistoryEnableFlags & ~kHistoryEnableFlag_Profiler);

		mbHistoryActive = (mHistoryEnableFlags != kHistoryEnableFlag_None);
		RebuildDecodeTables();
	}
}

void ATCPUEmulator::SetVerifier(ATCPUVerifier *verifier) {
	if (mpVerifier != verifier) {
		mpVerifier = verifier;

		RebuildDecodeTables();
	}
}

void ATCPUEmulator::SetHeatMap(ATCPUHeatMap *heatmap) {
	if (mpHeatMap != heatmap) {
		mpHeatMap = heatmap;

		RebuildDecodeTables();
	}
}

void ATCPUEmulator::SetIllegalInsnsEnabled(bool enable) {
	if (mbIllegalInsnsEnabled != enable) {
		mbIllegalInsnsEnabled = enable;
		RebuildDecodeTables();
	}
}

void ATCPUEmulator::SetStopOnBRK(bool en) {
	if (mbStopOnBRK != en) {
		mbStopOnBRK = en;
		RebuildDecodeTables();
	}
}

void ATCPUEmulator::SetNMIBlockingEnabled(bool enable) {
	if (mbAllowBlockedNMIs != enable) {
		mbAllowBlockedNMIs = enable;
		RebuildDecodeTables();
	}
}

uint32 ATCPUEmulator::GetBreakpointCount() const {
	return (mDebugFlags & kDebugFlag_BP) && mBreakpointCount > 0;
}

bool ATCPUEmulator::IsBreakpointSet(uint16 addr) const {
	return 0 != (mInsnFlags[addr] & kInsnFlagBreakPt);
}

void ATCPUEmulator::SetBreakpoint(uint16 addr) {
	if (!(mInsnFlags[addr] & kInsnFlagBreakPt)) {
		mInsnFlags[addr] |= kInsnFlagBreakPt;

		if (!mBreakpointCount++)
			mDebugFlags |= kDebugFlag_BP;
	}
}

void ATCPUEmulator::ClearBreakpoint(uint16 addr) {
	if (mInsnFlags[addr] & kInsnFlagBreakPt) {
		mInsnFlags[addr] &= ~kInsnFlagBreakPt;

		if (!--mBreakpointCount)
			mDebugFlags &= ~kDebugFlag_BP;
	}
}

void ATCPUEmulator::SetAllBreakpoints() {
	if (mBreakpointCount < 0x10000) {
		mBreakpointCount = 0;
		mDebugFlags |= kDebugFlag_BP;

		for(auto& insnFlags : mInsnFlags)
			insnFlags |= kInsnFlagBreakPt;
	}
}

void ATCPUEmulator::ClearAllBreakpoints() {
	if (mBreakpointCount) {
		mBreakpointCount = 0;
		mDebugFlags &= ~kDebugFlag_BP;

		for(auto& insnFlags : mInsnFlags)
			insnFlags &= kInsnFlagBreakPt;
	}
}

void ATCPUEmulator::ResetAllPaths() {
	for(uint32 i=0; i<65536; ++i)
		mInsnFlags[i] &= ~(kInsnFlagPathStart | kInsnFlagPathExecuted);
}

sint32 ATCPUEmulator::GetNextPathInstruction(sint32 last) const {
	if ((uint32)last >= 0x10000)
		last = -1;

	for(++last; last < 0x10000; ++last) {
		if (mInsnFlags[last] & (kInsnFlagPathStart | kInsnFlagPathExecuted))
			return last;
	}

	return -1;
}

bool ATCPUEmulator::IsPathStart(uint16 addr) const {
	return 0 != (mInsnFlags[addr] & kInsnFlagPathStart);
}

bool ATCPUEmulator::IsInPath(uint16 addr) const {
	return 0 != (mInsnFlags[addr] & (kInsnFlagPathStart | kInsnFlagPathExecuted));
}

void ATCPUEmulator::DumpStatus(bool extended) {
	if (mCPUMode == kATCPUMode_65C816) {
		if (mbEmulationFlag) {
			ATConsoleTaggedPrintf("C=%02X%02X X=%02X Y=%02X S=%02X P=%02X (%c%c%c%c%c%c)  "
				, mAH
				, mA
				, mX
				, mY
				, mS
				, mP
				, mP & 0x80 ? 'N' : ' '
				, mP & 0x40 ? 'V' : ' '
				, mP & 0x08 ? 'D' : ' '
				, mP & 0x04 ? 'I' : ' '
				, mP & 0x02 ? 'Z' : ' '
				, mP & 0x01 ? 'C' : ' '
				);
		} else {
			if (!(mP & kFlagX)) {
				ATConsoleTaggedPrintf("%c=%02X%02X X=%02X%02X Y=%02X%02X S=%02X%02X P=%02X (%c%c%c %c%c%c%c)  "
					, (mP & kFlagM) ? 'C' : 'A'
					, mAH
					, mA
					, mXH
					, mX
					, mYH
					, mY
					, mSH
					, mS
					, mP
					, mP & 0x80 ? 'N' : ' '
					, mP & 0x40 ? 'V' : ' '
					, mP & 0x20 ? 'M' : ' '
					, mP & 0x08 ? 'D' : ' '
					, mP & 0x04 ? 'I' : ' '
					, mP & 0x02 ? 'Z' : ' '
					, mP & 0x01 ? 'C' : ' '
					);
			} else {
				ATConsoleTaggedPrintf("%c=%02X%02X X=--%02X Y=--%02X S=%02X%02X P=%02X (%c%c%cX%c%c%c%c)  "
					, (mP & kFlagM) ? 'C' : 'A'
					, mAH
					, mA
					, mX
					, mY
					, mSH
					, mS
					, mP
					, mP & 0x80 ? 'N' : ' '
					, mP & 0x40 ? 'V' : ' '
					, mP & 0x20 ? 'M' : ' '
					, mP & 0x08 ? 'D' : ' '
					, mP & 0x04 ? 'I' : ' '
					, mP & 0x02 ? 'Z' : ' '
					, mP & 0x01 ? 'C' : ' '
					);
			}
		}
	} else {
		ATConsoleTaggedPrintf("A=%02X X=%02X Y=%02X S=%02X P=%02X (%c%c%c%c%c%c)  "
			, mA
			, mX
			, mY
			, mS
			, mP
			, mP & 0x80 ? 'N' : ' '
			, mP & 0x40 ? 'V' : ' '
			, mP & 0x08 ? 'D' : ' '
			, mP & 0x04 ? 'I' : ' '
			, mP & 0x02 ? 'Z' : ' '
			, mP & 0x01 ? 'C' : ' '
			);
	}

	ATDisassembleInsn(mInsnPC, mK);

	if (extended && mCPUMode == kATCPUMode_65C816)
		ATConsolePrintf("              B=%02X D=%04X\n", mB, mDP);
}

void ATCPUEmulator::BeginLoadState(ATSaveStateReader& reader) {
	reader.RegisterHandlerMethod(kATSaveStateSection_Arch, VDMAKEFOURCC('6', '5', '0', '2'), this, &ATCPUEmulator::LoadState6502);
	reader.RegisterHandlerMethod(kATSaveStateSection_Arch, VDMAKEFOURCC('C', '8', '1', '6'), this, &ATCPUEmulator::LoadState65C816);
	reader.RegisterHandlerMethod(kATSaveStateSection_Private, VDMAKEFOURCC('6', '5', '0', '2'), this, &ATCPUEmulator::LoadStatePrivate);
	reader.RegisterHandlerMethod(kATSaveStateSection_ResetPrivate, 0, this, &ATCPUEmulator::LoadStateResetPrivate);
	reader.RegisterHandlerMethod(kATSaveStateSection_End, 0, this, &ATCPUEmulator::EndLoadState);
}

void ATCPUEmulator::LoadState6502(ATSaveStateReader& reader) {
	SetPC(reader.ReadUint16());
	mA		= reader.ReadUint8();
	mX		= reader.ReadUint8();
	mY		= reader.ReadUint8();
	mS		= reader.ReadUint8();
	mP		= reader.ReadUint8();
}

void ATCPUEmulator::LoadState65C816(ATSaveStateReader& reader) {
	mDP		= reader.ReadUint16();
	mAH		= reader.ReadUint8();
	mXH		= reader.ReadUint8();
	mYH		= reader.ReadUint8();
	mSH		= reader.ReadUint8();
	mbEmulationFlag	= (reader.ReadUint8() & 1) != 0;
	mB		= reader.ReadUint8();
	mK		= reader.ReadUint8();
}

void ATCPUEmulator::LoadStatePrivate(ATSaveStateReader& reader) {
	const uint32 t = mpCallbacks->CPUGetUnhaltedAndRDYCycle();

	mbUnusedCycle = reader.ReadBool();
	mIntFlags = reader.ReadUint8();
	mIRQAssertTime = reader.ReadUint32() + t;
	mIRQAcknowledgeTime = reader.ReadUint32() + t;
	mNMIAssertTime = reader.ReadUint32() + t;
}

void ATCPUEmulator::LoadStateResetPrivate(ATSaveStateReader& reader) {
	mIRQAssertTime = mpCallbacks->CPUGetUnhaltedAndRDYCycle() - 10;
	mIRQAcknowledgeTime = mIRQAssertTime;
	mbUnusedCycle	= false;
}

void ATCPUEmulator::EndLoadState(ATSaveStateReader& reader) {
}

class ATSaveStateCPU816 final : public ATSnapExchangeObject<ATSaveStateCPU816, "ATSaveStateCPU816"> {
public:
	template<typename T>
	void Exchange(T& rw) {
		rw.Transfer("ah", &mAH);
		rw.Transfer("xh", &mXH);
		rw.Transfer("yh", &mYH);
		rw.Transfer("sh", &mSH);
		rw.Transfer("b", &mB);
		rw.Transfer("d", &mDP);
		rw.Transfer("k", &mK);
		rw.Transfer("e", &mbEmulationFlag);
	}

	uint8 mAH = 0;
	uint8 mXH = 0;
	uint8 mYH = 0;
	uint8 mSH = 0;
	uint16 mDP = 0;
	uint8 mB = 0;
	uint8 mK = 0;
	bool mbEmulationFlag = false;
};

class ATSaveStateCPU final : public ATSnapExchangeObject<ATSaveStateCPU, "ATSaveStateCPU"> {
public:
	template<typename T>
	void Exchange(T& rw) {
		rw.Transfer("pc", &mPC);
		rw.Transfer("insn_pc", &mInsnPC);
		rw.Transfer("a", &mA);
		rw.Transfer("x", &mX);
		rw.Transfer("y", &mY);
		rw.Transfer("p", &mP);
		rw.Transfer("s", &mS);
		rw.Transfer("ext816", &mp816);
		rw.Transfer("unused_cycle", &mbUnusedCycle);
		rw.Transfer("irq_assert_time", &mIrqAssertTime);
		rw.Transfer("irq_acknowledge_time", &mIrqAcknowledgeTime);
		rw.Transfer("nmi_assert_time", &mNmiAssertTime);
		rw.Transfer("current_ext_opcode", &mCurrentExtOpcode);
		rw.Transfer("current_ext_opcode_phase", &mCurrentExtOpcodePhase);
		rw.Transfer("int_addr", &mAddr);
		rw.Transfer("int_addr2", &mAddr2);
		rw.Transfer("int_addr_bank", &mAddrBank);
		rw.Transfer("int_rel_offset", &mRelOffset);
		rw.Transfer("int_data", &mData);
		rw.Transfer("int_data16", &mData16);

		if constexpr (!T::IsWriter) {
			if (mCurrentExtOpcode >= ATCPUEmulator::kNumExtOpcodes)
				throw ATInvalidSaveStateException();
		}
	}

	uint8 mA = 0;
	uint8 mX = 0;
	uint8 mY = 0;
	uint8 mP = 0;
	uint8 mS = 0;
	uint16 mPC = 0;
	uint16 mInsnPC = 0;
	uint8 mAH = 0;
	uint8 mXH = 0;
	uint8 mYH = 0;
	uint8 mSH = 0;
	uint16 mDP = 0;
	uint8 mB = 0;
	uint8 mK = 0;
	vdrefptr<ATSaveStateCPU816> mp816;
	bool mbUnusedCycle = false;
	uint8 mIntFlags = 0;
	uint32 mIrqAssertTime = 0;
	uint32 mIrqAcknowledgeTime = 0;
	uint32 mNmiAssertTime = 0;
	uint16 mCurrentExtOpcode = 0;
	uint8 mCurrentExtOpcodePhase = 0;
	uint16 mAddr = 0;
	uint16 mAddr2 = 0;
	uint8 mRelOffset = 0;
	uint8 mData = 0;
	uint16 mData16 = 0;
	uint8 mAddrBank = 0;
};

namespace {
	bool IsCycleEndingState(ATCPUState state) {
		switch(state) {
			case kStateNop:
				return false;

			case kStateReadOpcode:
			case kStateReadOpcodeNoBreak:
			case kStateReadImm:
			case kStateReadAddrL:
			case kStateReadAddrH:
			case kStateRead:
			case kStateReadSetSZToA:
			case kStateReadDummyOpcode:
			case kStateReadAddrHX:
			case kStateReadAddrHX_SHY:
			case kStateReadAddrHY_SHA:
			case kStateReadAddrHY_SHX:
			case kStateReadAddrHY:
			case kStateReadAddX:
			case kStateReadAddY:
			case kStateReadCarry:
			case kStateReadCarryForced:
			case kStateWrite:
			case kStateWriteA:
			case kStateReadAbsIndAddr:
			case kStateReadAbsIndAddrBroken:
			case kStateReadIndAddr:
			case kStateReadIndYAddr:
			case kStateReadIndYAddr_SHA:
			case kStateWait:
				return true;

			case kStateAtoD:
			case kStateXtoD:
			case kStateYtoD:
			case kStateStoD:
			case kStatePtoD:
			case kStatePtoD_B0:
			case kStatePtoD_B1:
			case kState0toD:
			case kStateDtoA:
			case kStateDtoX:
			case kStateDtoY:
			case kStateDtoS:
			case kStateDtoP:
			case kStateDtoP_noICheck:
			case kStateDSetSZ:
			case kStateDSetSZToA:
			case kStateDSetSZToX:
			case kStateDSetSZToY:
			case kStateAddrToPC:
			case kStateCheckNMIBlocked:
			case kStateNMIVecToPC:
			case kStateRESVecToPC:
			case kStateIRQVecToPC:
			case kStateNMIOrIRQVecToPC:
			case kStateNMIOrIRQVecToPCBlockable:
			case kStateDelayInterrupts:
				return false;

			case kStatePush:
			case kStatePushPCL:
			case kStatePushPCH:
			case kStatePushPCLM1:
			case kStatePushPCHM1:
			case kStatePop:
			case kStatePopPCL:
			case kStatePopPCH:
			case kStatePopPCHP1:
				return true;

			case kStateAdc:
			case kStateSbc:
			case kStateCmp:
			case kStateCmpX:
			case kStateCmpY:
			case kStateInc:
				return false;

			case kStateIncXWait:
				return true;

			case kStateDec:
				return false;

			case kStateDecXWait:
				return true;

			case kStateDecC:
			case kStateAnd:
			case kStateAnd_SAX:
			case kStateAnc:
			case kStateXaa:
			case kStateLas:
			case kStateSbx:
			case kStateArr:
			case kStateXas:
			case kStateOr:
			case kStateXor:
			case kStateAsl:
			case kStateLsr:
			case kStateRol:
			case kStateRor:
			case kStateBitSetSV:
			case kStateSEI:
			case kStateCLI:
			case kStateSEC:
			case kStateCLC:
			case kStateSED:
			case kStateCLD:
			case kStateCLV:
				return false;

			case kStateJs:
			case kStateJns:
			case kStateJc:
			case kStateJnc:
			case kStateJz:
			case kStateJnz:
			case kStateJo:
			case kStateJno:
			case kStateJsAddToPath:
			case kStateJnsAddToPath:
			case kStateJcAddToPath:
			case kStateJncAddToPath:
			case kStateJzAddToPath:
			case kStateJnzAddToPath:
			case kStateJoAddToPath:
			case kStateJnoAddToPath:
			case kStateJccFalseRead:
				return true;

			case kStateStepOver:
			case kStateResetBit:
			case kStateSetBit:
				return false;

			case kStateReadRel:
			case kStateJ0:
			case kStateJ1:
			case kStateJ0AddToPath:
			case kStateJ1AddToPath:
				return true;

			case kStateWaitForInterrupt:
			case kStateStop:
			case kStateJ:
			case kStateJAddToPath:
				return true;

			case kStateTrb:
			case kStateTsb:
			case kStateC02_Adc:
			case kStateC02_Sbc:
			case kStateAddEAToHistory:
			case kStateAddAsPathStart:
			case kStateAddToPath:
				return false;

			case kStateBreakOnUnsupportedOpcode:
				return true;

			case kStateReadImmL16:
			case kStateReadImmH16:
			case kStateReadAddrDp:
			case kStateReadAddrDpX:
			case kStateReadAddrDpXInPage:
			case kStateReadAddrDpY:
			case kStateReadAddrDpYInPage:
			case kState816ReadIndAddrDpInPage:
			case kStateReadIndAddrDp:
			case kStateReadIndAddrDpY:
			case kStateReadIndAddrDpLongH:
			case kStateReadIndAddrDpLongB:
			case kState816ReadIndAddrDpLongHInPage:
			case kState816ReadIndAddrDpLongBInPage:
			case kStateReadAddrAddY:
			case kState816ReadAddrL:
			case kState816ReadAddrH:
			case kState816ReadAddrHX:
			case kState816ReadAddrAbsXSpec:
			case kState816ReadAddrAbsXAlways:
			case kState816ReadAddrAbsYSpec:
			case kState816ReadAddrAbsYAlways:
			case kState816ReadAddrAbsInd:
			case kStateRead816AddrAbsLongL:
			case kStateRead816AddrAbsLongH:
			case kStateRead816AddrAbsLongB:
			case kStateReadAddrB:
			case kStateReadAddrBX:
			case kStateReadAddrSO:
			case kState816ReadAddrSO_AddY:
				return true;

			case kStateBtoD:
			case kStateKtoD:
			case kState0toD16:
			case kStateAtoD16:
			case kStateXtoD16:
			case kStateYtoD16:
			case kStateStoD16:
			case kStateDPtoD16:
			case kStateDtoB:
			case kStateDtoA16:
			case kStateDtoX16:
			case kStateDtoY16:
			case kStateDtoPNative:
			case kStateDtoPNative_noICheck:
			case kStateDtoS16:
			case kStateDtoDP16:
			case kStateDSetSZ16:
			case kStateBitSetSV16:
				return false;

			case kState816WriteByte:
			case kStateWriteL16:
			case kStateWriteH16:
			case kStateWriteH16_DpBank:
			case kState816ReadByte:
			case kStateReadL16:
			case kStateReadH16:
			case kStateReadH16_DpBank:
				return true;

			case kStateAnd16:
			case kStateOr16:
			case kStateXor16:
			case kStateAdc16:
			case kStateSbc16:
			case kStateCmp16:
			case kStateInc16:
			case kStateDec16:
			case kStateRol16:
			case kStateRor16:
			case kStateAsl16:
			case kStateLsr16:
			case kStateTrb16:
			case kStateTsb16:
			case kStateCmpX16:
			case kStateCmpY16:
			case kStateXba:
			case kStateXce:
				return false;

			case kStatePushNative:
			case kStatePushL16:
			case kStatePushH16:
			case kStatePushPBKNative:
			case kStatePushPCLNative:
			case kStatePushPCHNative:
			case kStatePushPCLM1Native:
			case kStatePushPCHM1Native:
			case kStatePopNative:
			case kStatePopL16:
			case kStatePopH16:
			case kStatePopPCLNative:
			case kStatePopPCHNative:
			case kStatePopPCHP1Native:
			case kStatePopPBKNative:
			case kStateRep:
			case kStateSep:
			case kStateJ16:
			case kStateJ16AddToPath:
				return true;

			case kState816_NatCOPVecToPC:
			case kState816_EmuCOPVecToPC:
			case kState816_NatNMIVecToPC:
			case kState816_NatIRQVecToPC:
			case kState816_NatBRKVecToPC:
			case kState816_ABORT:
			case kState816_SetI_ClearD:
			case kState816_LongAddrToPC:
				return false;

			case kState816_MoveRead:
			case kState816_MoveWriteP:
			case kState816_MoveWriteN:
				return true;

			case kState816_Per:
			case kState816_SetBank0:
			case kState816_SetBankPBR:
			case kStateUpdateHeatMap:
			case kStateVerifyInsn:
			case kStateVerifyIRQEntry:
			case kStateVerifyNMIEntry:
				return false;

			default:
				VDFAIL("Invalid state found.");
				return false;
		}
	}
}

void ATCPUEmulator::SaveState(IATObjectState **result) {
	const uint32 t = mpCallbacks->CPUGetUnhaltedAndRDYCycle();

	vdrefptr<ATSaveStateCPU> obj(new ATSaveStateCPU);

	obj->mA = mA;
	obj->mX = mX;
	obj->mY = mY;
	obj->mP = mP;
	obj->mS = mS;

	// If we're at an opcode fetch, InsnPC may not have been updated yet and should be
	// overridden with the current PC. Un-overriding happens below.
	obj->mPC = mPC;
	obj->mInsnPC = mPC;
	
	if (mCPUMode == kATCPUMode_65C816) {
		obj->mp816 = new ATSaveStateCPU816();
		obj->mp816->mAH = mAH;
		obj->mp816->mXH = mXH;
		obj->mp816->mYH = mYH;
		obj->mp816->mSH = mSH;
		obj->mp816->mDP = mDP;
		obj->mp816->mB = mB;
		obj->mp816->mK = mK;
		obj->mp816->mbEmulationFlag = mbEmulationFlag;
	}

	obj->mbUnusedCycle = mbUnusedCycle;
	obj->mIntFlags = mIntFlags;
	obj->mIrqAssertTime = mIRQAssertTime - t;
	obj->mIrqAcknowledgeTime = mIRQAcknowledgeTime - t;
	obj->mNmiAssertTime = mNMIAssertTime - t;

	obj->mAddr = mAddr;
	obj->mAddr2 = mAddr2;
	obj->mRelOffset = mRelOffset;
	obj->mData = mData;
	obj->mData16 = mData16;
	obj->mAddrBank = mAddrBank;

	obj->mCurrentExtOpcode = 0;
	obj->mCurrentExtOpcodePhase = 0;

	if (*mpNextState != kStateReadOpcode && *mpNextState != kStateReadOpcodeNoBreak) {
		uint16 insnOffset = 0;

		if ((uintptr)mpNextState - (uintptr)mDecodeHeap < sizeof(mDecodeHeap))
			insnOffset = (uint16)(mpNextState - mDecodeHeap);
		else
			insnOffset = (uint16)(mStateRelocOffset + (mpNextState - mStates));

		auto it = std::upper_bound(mExtOpcodeAnchors, mExtOpcodeAnchors + mNumExtOpcodeAnchors, insnOffset, ExtOpcodeAnchorPred());
		VDASSERT(it != mExtOpcodeAnchors);

		obj->mInsnPC = mInsnPC;
		obj->mCurrentExtOpcode = it[-1].mExtOpcode;

		uint32 insnStartOffset = it[-1].mOffset;
		for(uint32 i = insnStartOffset; i < insnOffset; ++i) {
			const auto state = mDecodeHeap[i];

			if (IsCycleEndingState((ATCPUState)state))
				++obj->mCurrentExtOpcodePhase;
		}

		++obj->mCurrentExtOpcodePhase;
	}

	*result = obj.release();
}

void ATCPUEmulator::LoadState(const IATObjectState *state) {
	if (state)
		return LoadState(atser_cast<const ATSaveStateCPU&>(*state));
	else
		return LoadState(ATSaveStateCPU{});
}

void ATCPUEmulator::LoadState(const ATSaveStateCPU& cpustate) {
	const uint32 t = mpCallbacks->CPUGetUnhaltedAndRDYCycle();

	SetPC(cpustate.mPC);			// also resets next state
	mInsnPC = cpustate.mInsnPC;
	mA = cpustate.mA;
	mX = cpustate.mX;
	mY = cpustate.mY;
	mP = cpustate.mP;
	mS = cpustate.mS;
	mbUnusedCycle = cpustate.mbUnusedCycle;
	mIntFlags = cpustate.mIntFlags;
	mIRQAssertTime = cpustate.mIrqAssertTime + t;
	mIRQAcknowledgeTime = cpustate.mIrqAcknowledgeTime + t;
	mNMIAssertTime = cpustate.mNmiAssertTime + t;

	mOpcode = (uint8)cpustate.mCurrentExtOpcode;

	mAddr = cpustate.mAddr;
	mAddr2 = cpustate.mAddr2;
	mRelOffset = cpustate.mRelOffset;
	mData = cpustate.mData;
	mData16 = cpustate.mData16;
	mAddrBank = cpustate.mAddrBank;


	if (mCPUMode == kATCPUMode_65C816 && cpustate.mp816) {
		const ATSaveStateCPU816& cpustate816 = *cpustate.mp816;
		mAH = cpustate816.mAH;
		mXH = cpustate816.mXH;
		mYH = cpustate816.mYH;
		mSH = cpustate816.mSH;
		mDP = cpustate816.mDP;
		mB = cpustate816.mB;
		mK = cpustate816.mK;
		mbEmulationFlag = cpustate816.mbEmulationFlag;
	} else {
		mbEmulationFlag = true;
		mB = 0;
		mK = 0;
	}

	if (mbEmulationFlag) {
		mSH = 1;
		mP |= 0x30;
	}

	mStates[0] = kStateReadOpcode;
	mpNextState = mStates;
	mStateRelocOffset = 0;

	// 0 - read opcode
	// 1 - first uop of instruction (after 1st cycle)
	// 2 - first uop of instruction after 2nd cycle
	if (cpustate.mCurrentExtOpcodePhase) {
		if (mCPUMode == kATCPUMode_65C816)
			Update65816DecodeTable();

		const uint8 *insnStates = mDecodeHeap + mDecodePtrs[cpustate.mCurrentExtOpcode];

		for(uint32 i=1; i<cpustate.mCurrentExtOpcodePhase; ++i) {
			for(;;) {
				const auto state = (ATCPUState)*insnStates;

				if (state == kStateReadOpcode || state == kStateReadOpcodeNoBreak)
					break;

				++insnStates;
				if (IsCycleEndingState(state))
					break;
			}
		}

		mpNextState = insnStates;
	}
}

void ATCPUEmulator::InjectOpcode(uint8 op) {
	mOpcode = op;
	mpNextState = mDecodeHeap + mDecodePtrs[mOpcode];
}

void ATCPUEmulator::Push(uint8 v) {
	mpMemory->WriteByte(0x100 + mS, v);
	--mS;
}

void ATCPUEmulator::PushWord(uint16 v) {
	Push((uint8)(v >> 8));
	Push((uint8)v);
}

uint8 ATCPUEmulator::Pop() {
	return mpMemory->ReadByte(0x100 + (uint8)++mS);
}

void ATCPUEmulator::Jump(uint16 pc) {
	mPC = pc;
	mpDstState = mStates;
	mpNextState = mStates;
	*mpDstState++ = kStateReadOpcode;
}

void ATCPUEmulator::Ldy(uint8 v) {
	mY = v;

	mP &= ~kFlagN & ~kFlagZ;
	mP |= (v & 0x80);

	if (!v)
		mP |= kFlagZ;
}

void ATCPUEmulator::AssertIRQ(int cycleOffset) {
	if (!(mIntFlags & kIntFlag_IRQActive)) {
		if (mIntFlags & kIntFlag_IRQPending)
			UpdatePendingIRQState();

		mIntFlags |= kIntFlag_IRQActive;
		mIRQAssertTime = mpCallbacks->CPUGetUnhaltedAndRDYCycle() + cycleOffset;
	}
}

void ATCPUEmulator::NegateIRQ() {
	if (mIntFlags & kIntFlag_IRQActive) {
		if (!(mIntFlags & kIntFlag_IRQPending))
			UpdatePendingIRQState();

		mIntFlags &= ~kIntFlag_IRQActive;
	}
}

void ATCPUEmulator::AssertNMI() {
	if (!(mIntFlags & kIntFlag_NMIPending)) {
		mIntFlags |= kIntFlag_NMIPending;
		mNMIAssertTime = mpCallbacks->CPUGetUnhaltedAndRDYCycle();
	}
}

void ATCPUEmulator::AssertABORT() {
	if (mDecodePtrs[(uint32)ExtOpcode::Abort])
		mpNextState = mDecodeHeap + mDecodePtrs[(uint32)ExtOpcode::Abort];
}

void ATCPUEmulator::AssertRDY() {
	// If the next opcode is opdecode, check if we should take the interrupt next
	// cycle, and if so, lock in the cycle. We need to do this because this check
	// actually happens in the end of the last instruction, and that can happen
	// before execution is suspended after an INC WSYNC.
	switch(*mpNextState) {
		case AT6502States::kStateReadOpcode:
		case AT6502States::kStateReadOpcodeNoBreak:
			if (mSubCycles > 1)
				ProcessInterrupts<true>();
			else
				ProcessInterrupts<false>();
			break;
	}
}

void ATCPUEmulator::NegateRDY() {
	// If an NMI is pending, we must not allow the NMI to be taken on the next
	// instruction. This is because the 6502 only checks for NMIs at the end of
	// an instruction and the CPU has already started the next instruction when
	// it is halted by RDY being pulled low by ANTIC. Therefore, that instruction
	// will always execute before the NMI is taken.
	//
	// It's possible that this may be off by a cycle or so, but this doesn't matter
	// in the Atari where RDY is released relatively far from when the NMI can
	// occur.

	if (mIntFlags & kIntFlag_NMIPending)
		mNMIAssertTime = mpCallbacks->CPUGetUnhaltedAndRDYCycle() - 1;
}

void ATCPUEmulator::PeriodicCleanup() {
	// check if we need to bump the interrupt assert times forward to avoid wrapping
	const uint32 t = mpCallbacks->CPUGetUnhaltedAndRDYCycle();

	if (((t - mNMIAssertTime) >> 30) == 2)
		mNMIAssertTime += 0x40000000;

	if (((t - mNMIIgnoreUnhaltedCycle) >> 30) == 2)
		mNMIIgnoreUnhaltedCycle += 0x40000000;

	if (((t - mIRQAssertTime) >> 30) == 2)
		mIRQAssertTime += 0x40000000;
	
	if (((t - mIRQAcknowledgeTime) >> 30) == 2)
		mIRQAcknowledgeTime += 0x40000000;
}

#ifdef VD_COMPILER_MSVC
#pragma runtime_checks("scu", off)
#endif

int ATCPUEmulator::Advance() {
	if (mCPUMode == kATCPUMode_65C816) {
		if (mSubCycles > 1)
			return Advance65816HiSpeed(false);
		else
			return Advance65816();
	} else
		return Advance6502();
}

#include "cpumachine.inl"

#define AT_CPU_MACHINE_65C816
#include "cpumachine.inl"
#undef AT_CPU_MACHINE_65C816

#define AT_CPU_MACHINE_65C816
#define AT_CPU_MACHINE_65C816_HISPEED
#include "cpumachine.inl"
#undef AT_CPU_MACHINE_65C816_HISPEED
#undef AT_CPU_MACHINE_65C816

#ifdef VD_COMPILER_MSVC
#pragma runtime_checks("scu", restore)
#endif

uint8 ATCPUEmulator::ProcessDebugging() {
	uint8 iflags = mInsnFlags[mPC];

	if (iflags & kInsnFlagBreakPt) {
		ATSimulatorEvent event = (ATSimulatorEvent)mpBkptManager->TestPCBreakpoint(0, (uint32)mPC + ((uint32)mK << 16));
		if (event) {
			mbUnusedCycle = true;
			RedecodeInsnWithoutBreak();
			return event;
		}
	}

	if (mbStep && (uint16)(mPC - mStepIgnorePCStart) >= mStepIgnorePCLength) {
		if ((uint8)(mS - mStepIgnoreStackStart) >= mStepIgnoreStackLength) {
			bool needRecompute = true;
			bool shouldStop = false;

			mpStepConditionsIterator = mpStepConditions;
			while(mpStepConditionsIterator) {
				ATCPUStepCondition& stepCond = *mpStepConditionsIterator;
				mpStepConditionsIterator = stepCond.mpNext;

				if (!IsStepConditionSatisfied(stepCond))
					continue;

				ATCPUStepResult stepResult = kATCPUStepResult_Stop;

				if (stepCond.mpCallback) {
					// We need to do this to ensure that a save state invoked from the callback
					// is correct. This is because we've already read past the ReadOpcode state,
					// and are depending on continuation to the ReadOpcodeNoBreak state if we
					// don't stop. But we haven't stopped yet, and if we try to save state from
					// the callback without this, it'll misinterpret the next state as the
					// state point to save, which is wrong.
					RedecodeInsnWithoutBreak();

					stepResult = stepCond.mpCallback(this, mPC, false, stepCond.mpCallbackData);
				}

				if (stepResult == kATCPUStepResult_SkipCall) {
					VDASSERT(stepCond.mbRegistered);
					stepCond.mIgnoreStackStart = mS ^ 0x80;
					stepCond.mIgnoreStackLength = 0x80;
					needRecompute = true;
				} else if (stepResult == kATCPUStepResult_Stop) {
					shouldStop = true;
				}
			}

			if (needRecompute)
				RecomputeStepConditions();

			if (shouldStop) {
				mbUnusedCycle = true;
				RedecodeInsnWithoutBreak();
				return kATSimEvent_CPUSingleStep;
			}
		}
	}

	if (mS >= mSBrk) {
		mSBrk = 0x100;
		mbUnusedCycle = true;
		RedecodeInsnWithoutBreak();
		return kATSimEvent_CPUStackBreakpoint;
	}

	if (mbTrace)
		DumpStatus();

	return 0;
}

void ATCPUEmulator::ProcessStepOver() {
	// We apply a two-byte adjustment here to do this test approximately
	// at the level of the parent (we can't do it exactly).
	if ((uint8)(mS - mStepIgnoreStackStart + 2) < mStepIgnoreStackLength)
		return;

	mpStepConditionsIterator = mpStepConditions;

	bool needRecompute = false;

	while(mpStepConditionsIterator) {
		ATCPUStepCondition& stepCond = *mpStepConditionsIterator;
		mpStepConditionsIterator = stepCond.mpNext;

		if (!stepCond.mbStepOver || !IsStepConditionSatisfied(stepCond))
			continue;

		ATCPUStepResult stepResult = kATCPUStepResult_Stop;

		if (stepCond.mpCallback) {
			// This needs to use PC instead of insnPC, because we are
			// in the middle of a JSR or interrupt dispatch.
			stepResult = stepCond.mpCallback(this, mPC, true, stepCond.mpCallbackData);
		}

		switch(stepResult) {
			case kATCPUStepResult_SkipCall:
				VDASSERT(stepCond.mbRegistered);

				// We need a slight adjustment because the stack has already been
				// adjusted by the JSR -- so we want to ignore the current stack
				// frame too.
				stepCond.mIgnoreStackStart = (uint8)(mS - 0x7F);
				stepCond.mIgnoreStackLength = 0x80;
				needRecompute = true;
				break;
			case kATCPUStepResult_Stop:
				VDASSERT(stepCond.mbRegistered);
				stepCond.mbStepOver = false;
				stepCond.mbStepOut = false;
				stepCond.mbWaitForNMI = false;
				stepCond.mIgnorePCStart = 0;
				stepCond.mIgnorePCLength = 0;
				stepCond.mIgnoreStackStart = 0;
				stepCond.mIgnoreStackLength = 0;
				needRecompute = true;
				break;

			case kATCPUStepResult_Continue:
			default:
				break;
		}
	}

	if (needRecompute)
		RecomputeStepConditions();
}

template<bool T_Accel>
bool ATCPUEmulator::ProcessInterrupts() {
	if (mIntFlags & kIntFlag_NMIPending) {
		uint32 t = mpCallbacks->CPUGetUnhaltedAndRDYCycle();

		if (T_Accel || t - mNMIAssertTime >= (t == mNMIIgnoreUnhaltedCycle ? 3U : 2U)) {
			if (mbTrace)
				ATConsoleWrite("CPU: Jumping to NMI vector\n");

			mIntFlags &= ~kIntFlag_NMIPending;
			mbMarkHistoryNMI = true;

			mpNextState = mDecodeHeap + mDecodePtrs[(uint32)ExtOpcode::Nmi];

			if (mDebugFlags & kDebugFlag_StepNMI) {
				bool recomputeNeeded = false;

				for(ATCPUStepCondition *p = mpStepConditions; p; p = p->mpNext) {
					if (p->mbWaitForNMI) {
						p->mbWaitForNMI = false;

						recomputeNeeded = true;
					}
				}

				if (recomputeNeeded)
					RecomputeStepConditions();
			}
			return true;
		}
	}

	if (mIntFlags & kIntFlag_IRQReleasePending) {
		mIntFlags &= ~kIntFlag_IRQReleasePending;
	} else if (mIntFlags & (kIntFlag_IRQActive | kIntFlag_IRQPending)) {
		if (!(mP & kFlagI) || (mIntFlags & kIntFlag_IRQSetPending)) {
			if (!T_Accel && mNMIIgnoreUnhaltedCycle == mIRQAssertTime + 1) {
				++mIRQAssertTime;
			} else {
				switch(mIntFlags & (kIntFlag_IRQPending | kIntFlag_IRQActive)) {
					case kIntFlag_IRQPending:
					case kIntFlag_IRQActive:
						UpdatePendingIRQState();
						break;
				}

				// If an IRQ is pending, check if it was just acknowledged this cycle. If so,
				// bypass it as it's too late to interrupt this instruction.
				if ((mIntFlags & kIntFlag_IRQPending) && mpCallbacks->CPUGetUnhaltedAndRDYCycle() - mIRQAcknowledgeTime > 0) {
					if (mbTrace)
						ATConsoleWrite("CPU: Jumping to IRQ vector\n");

					mbMarkHistoryIRQ = true;

					mpNextState = mDecodeHeap + mDecodePtrs[(uint32)ExtOpcode::Irq];
					return true;
				}
			}
		}
	}

	mIntFlags &= ~kIntFlag_IRQSetPending;

	return false;
}

uint8 ATCPUEmulator::ProcessHook() {
	uint8 op = mpHookMgr->OnHookHit(mPC);

	if (op) {
		// mark HLE entry
		mbMarkHistoryIRQ = true;
		mbMarkHistoryNMI = true;

		if (op != 0x4C) {
			++mPC;
			mOpcode = op;
			mpNextState = mDecodeHeap + mDecodePtrs[mOpcode];
		}
	}

	return op;
}

uint8 ATCPUEmulator::ProcessHook816() {
	// We block hooks if any of the following are true:
	// - the CPU is in native mode
	// - PBK != 0
	// - DBK != 0
	// - D != 0

	if (!mbEmulationFlag || mDP || mK || mB)
		return 0;

	return ProcessHook();
}

template<bool is816, bool hispeed>
void ATCPUEmulator::AddHistoryEntry(bool slowFlag) {
	if (mpHistoryNext == mHistory.end()) {
		mpHistoryNext = mHistory.begin();
		mHistoryBase += kHistoryLength;
	}

	ATCPUHistoryEntry * VDRESTRICT he = mpHistoryNext++;

	ATCPU_PROFILE_OPCODE(mPC-1, mOpcode);

	mpCallbacks->CPUGetHistoryTimes(he);
	he->mEA = 0xFFFFFFFFUL;
	he->mPC = mPC - 1;
	he->mP = mP;

	// WORKAROUND FOR 15.3 CODEGEN BUG
	//
	// The 15.3.x compiler allocates stack space for offsetof() even within a
	// static_assert (!).

	struct Check {
		// Argh, the compiler should merge these... but it doesn't.

		void test() {
			static_assert(
				!(offsetof(ATCPUHistoryEntry, mA) & 3)
				&& offsetof(ATCPUHistoryEntry, mX) == offsetof(ATCPUHistoryEntry, mA)+1
				&& offsetof(ATCPUHistoryEntry, mY) == offsetof(ATCPUHistoryEntry, mA)+2
				&& offsetof(ATCPUHistoryEntry, mS) == offsetof(ATCPUHistoryEntry, mA)+3
				&& !(offsetof(ATCPUEmulator, mA) & 3)
				&& offsetof(ATCPUEmulator, mX) == offsetof(ATCPUEmulator, mA)+1
				&& offsetof(ATCPUEmulator, mY) == offsetof(ATCPUEmulator, mA)+2
				&& offsetof(ATCPUEmulator, mS) == offsetof(ATCPUEmulator, mA)+3
				, "copy optimization invalidated");
		}
	};

	// he->mA = mA;
	// he->mX = mX;
	// he->mY = mY;
	// he->mS = mS;
	*(uint32_t *)&he->mA = *(const uint32_t *)&mA;

	he->mOpcode[0] = mOpcode;

	he->mbIRQ = mbMarkHistoryIRQ;
	he->mbNMI = mbMarkHistoryNMI;

	if constexpr (is816) {
		he->mSubCycle = 0;

		if constexpr(hispeed) {
			if (!slowFlag)
				he->mSubCycle = mSubCycles - mSubCyclesLeft;
		}

		he->mbEmulation = mbEmulationFlag;
		he->mExt.mSH = mSH;
		he->mExt.mAH = mAH;
		he->mExt.mXH = mXH;
		he->mExt.mYH = mYH;
		he->mB = mB;
		he->mK = mK;
		he->mD = mDP;
	} else {
		he->mbEmulation = true;
		he->mSubCycle = 0;
		he->mB = 0;
		he->mK = 0;
		he->mD = 0;
	}

	mbMarkHistoryIRQ = false;
	mbMarkHistoryNMI = false;

	const uint16 pc = mPC;
	const uint8 k = mK;
	const ATCPUEmulatorMemory * VDRESTRICT mem = mpMemory;
	if constexpr (is816) {
		for(int i=0; i<3; ++i)
			he->mOpcode[i+1] = mem->DebugExtReadByte(pc+i, k);
	} else {
		for(int i=0; i<2; ++i)
			he->mOpcode[i+1] = mem->DebugReadByte(pc+i);

		he->mGlobalPCBase = mem->mpCPUReadAddressPageMap[(mPC - 1) >> 8];
	}
}

void ATCPUEmulator::UpdatePendingIRQState() {
	VDASSERT(((mIntFlags & kIntFlag_IRQActive) != 0) != ((mIntFlags & kIntFlag_IRQPending) != 0));

	const uint32 cycle = mpCallbacks->CPUGetUnhaltedAndRDYCycle();

	if (mIntFlags & kIntFlag_IRQActive) {
		// IRQ line is pulled down, but no IRQ acknowledge has happened. Check if the IRQ
		// line has been down for a minimum of three cycles; if so, set the pending
		// IRQ flag.

		if (cycle - mIRQAssertTime >= 3) {
			mIntFlags |= kIntFlag_IRQPending;
			mIRQAcknowledgeTime = mIRQAssertTime + 3;
		}
	} else {
		// IRQ line is pulled up, but an IRQ acknowledge is pending. Check if more than
		// one cycle has passed by. If so, kill the acknowledged IRQ.

		if (cycle - mIRQAcknowledgeTime >= 2)
			mIntFlags &= ~kIntFlag_IRQPending;
	}
}

void ATCPUEmulator::RecomputeStepConditions() {
	mbStep = false;

	mStepIgnorePCStart = 0;
	mStepIgnorePCLength = 0;
	mStepIgnoreStackStart = 0;
	mStepIgnoreStackLength = 0;

	mDebugFlags &= ~(kDebugFlag_Step | kDebugFlag_StepNMI);

	bool stepOver = false;
	bool ignorePCRangeValid = false;
	bool ignoreStackRangeValid = false;

	for(ATCPUStepCondition *p = mpStepConditions; p; p = p->mpNext) {
		if (p->mbWaitForNMI) {
			mDebugFlags |= kDebugFlag_StepNMI;
			continue;
		}

		if (ignorePCRangeValid) {
			if (mStepIgnorePCLength > 0) {
				if (p->mIgnorePCLength == 0) {
					mStepIgnorePCLength = 0;
					mStepIgnorePCStart = 0;
				} else {
					const uint32 start = std::max<uint32>(mStepIgnorePCStart, p->mIgnorePCStart);
					const uint32 end = std::min<uint32>(mStepIgnorePCStart + mStepIgnorePCLength, p->mIgnorePCStart + p->mIgnorePCLength);

					if (start < end) {
						mStepIgnorePCStart = start;
						mStepIgnorePCLength = end - start;
					} else {
						mStepIgnorePCStart = 0;
						mStepIgnorePCLength = 0;
					}
				}
			}
		} else {
			mStepIgnorePCStart = p->mIgnorePCStart;
			mStepIgnorePCLength = p->mIgnorePCLength;
			ignorePCRangeValid = true;
		}

		if (ignoreStackRangeValid) {
			if ((uint8)(mStepIgnoreStackStart - p->mIgnoreStackStart) < p->mIgnoreStackLength) {
				mStepIgnoreStackLength = std::min<uint8>(mStepIgnoreStackLength, p->mIgnoreStackLength - (mStepIgnoreStackStart - p->mIgnoreStackStart));
				mStepIgnoreStackStart = p->mIgnoreStackStart;
			} else if ((uint8)(p->mIgnoreStackStart - mStepIgnoreStackStart) < mStepIgnoreStackLength) {
				mStepIgnoreStackLength = std::min<uint8>(p->mIgnoreStackLength, mStepIgnoreStackLength - (p->mIgnoreStackStart - mStepIgnoreStackStart));
			} else {
				// ignore ranges disjoint -- can't ignore anything
				mStepIgnoreStackLength = 0;
			}
		} else {
			mStepIgnoreStackStart = p->mIgnoreStackStart;
			mStepIgnoreStackLength = p->mIgnoreStackLength;
			ignoreStackRangeValid = true;
		}

		mbStep = true;
		stepOver |= p->mbStepOver;
	}

	if (mbStep)
		mDebugFlags |= kDebugFlag_Step;

	if (mbStepOver != stepOver) {
		mbStepOver = stepOver;

		RebuildDecodeTables();
	}
}

void ATCPUEmulator::RedecodeInsnWithoutBreak() {
	mpNextState = mStates;
	mStates[0] = kStateReadOpcodeNoBreak;
}

void ATCPUEmulator::Update65816DecodeTable() {
	ATCPUSubMode subMode = kATCPUSubMode_65C816_Emulation;

	if (!mbEmulationFlag)
		subMode = (ATCPUSubMode)(kATCPUSubMode_65C816_NativeM16X16 + ((mP >> 4) & 3));

	if (mCPUSubMode != subMode) {
		mCPUSubMode = subMode;

		if (mbEmulationFlag) {
			mSH = 0x01;
			mXH = 0;
			mYH = 0;
		} else {
			if (mP & kFlagX) {
				mXH = 0;
				mYH = 0;
			}
		}
	}

	uint8 decMode = (uint8)(subMode + ((mDP & 0xff) ? 5 : 0) - kATCPUSubMode_65C816_Emulation);

	if (mDecodeTableMode816 != decMode) {
		mDecodeTableMode816 = decMode;
		memcpy(mDecodePtrs, mDecodePtrs816[decMode], sizeof mDecodePtrs);
	}
}

void ATCPUEmulator::RebuildDecodeTables() {
	if (((uintptr)mpNextState - (uintptr)mDecodeHeap) < sizeof(mDecodeHeap)) {
		mStateRelocOffset = mpNextState - mDecodeHeap;

		uint8 *dst = mStates;
		for(int i=0; i<15; ++i) {
			if (mpNextState[i] == kStateReadOpcode)
				break;

			*dst++ = mpNextState[i];
		}

		*dst = kStateReadOpcode;
		mpNextState = mStates;
	}

	switch(mCPUMode) {
		case kATCPUMode_6502:
			RebuildDecodeTables6502(false);
			break;

		case kATCPUMode_65C02:
			RebuildDecodeTables6502(true);
			break;

		case kATCPUMode_65C816:
			RebuildDecodeTables65816();

			// force reinit
			mDecodeTableMode816 = 0xFF;
			Update65816DecodeTable();
			break;
	}

	VDASSERT(mpDstState <= mDecodeHeap + vdcountof(mDecodeHeap));
}

void ATCPUEmulator::RebuildDecodeTables6502(bool cmos) {
	// Microcode storage order for better cache utilization. Determined by PROFILE_OPCODES.
	static constexpr uint8 kStorageOrder[] = {
		0x85,0xA5,0x8D,0xA9,0xD0,0xBD,0x20,0x18,0xF0,0xAD,0x10,0xE8,0x9D,0x90,0x4C,0x4A,
		0x60,0x65,0x38,0xC9,0x29,0xB9,0xB1,0x8E,0xA2,0xB0,0xCA,0xA0,0xE5,0x69,0x30,0xB5,
		0xC8,0x88,0xAA,0xA6,0x49,0xA8,0xA4,0xEA,0x91,0x99,0xC6,0xE6,0x0A,0x95,0x7D,0x8A,
		0x6D,0x48,0x98,0x86,0x68,0xE0,0xE9,0x2C,0xC5,0xAE,0xCE,0x84,0x24,0x26,0xAC,0xC0,
		0xBC,0xC4,0x66,0x06,0x09,0x8C,0x6A,0xEE,0x2A,0xCD,0xED,0xB4,0x05,0x45,0x75,0x0D,
		0x25,0xBE,0xDD,0xF5,0xE4,0x6C,0x40,0x79,0xD9,0x3D,0xD8,0x08,0xDE,0x28,0xD5,0x36,
		0x2D,0x46,0xF9,0xF8,0x7E,0x1D,0x15,0x59,0x19,0xD1,0xEC,0x71,0xD6,0xFD,0x58,0x31,
		0x4D,0xF1,0x50,0x94,0x0E,0xAF,0xB6,0x2E,0x5D,0xBA,0x1E,0x4E,0x70,0x35,0xFE,0xCC,
		0x3E,0x78,0x9A,0x76,0x51,0x39,0xBF,0xA1,0x11,0x6E,0xF6,0x00,0x01,0x02,0x03,0x04,
		0x07,0x0B,0x0C,0x0F,0x12,0x13,0x14,0x16,0x17,0x1A,0x1B,0x1C,0x1F,0x21,0x22,0x23,
		0x27,0x2B,0x2F,0x32,0x33,0x34,0x37,0x3A,0x3B,0x3C,0x3F,0x41,0x42,0x43,0x44,0x47,
		0x4B,0x4F,0x52,0x53,0x54,0x55,0x56,0x57,0x5A,0x5B,0x5C,0x5E,0x5F,0x61,0x62,0x63,
		0x64,0x67,0x6B,0x6F,0x72,0x73,0x74,0x77,0x7A,0x7B,0x7C,0x7F,0x80,0x81,0x82,0x83,
		0x87,0x89,0x8B,0x8F,0x92,0x93,0x96,0x97,0x9B,0x9C,0x9E,0x9F,0xA3,0xA7,0xAB,0xB2,
		0xB3,0xB7,0xB8,0xBB,0xC1,0xC2,0xC3,0xC7,0xCB,0xCF,0xD2,0xD3,0xD4,0xD7,0xDA,0xDB,
		0xDC,0xDF,0xE1,0xE2,0xE3,0xE7,0xEB,0xEF,0xF2,0xF3,0xF4,0xF7,0xFA,0xFB,0xFC,0xFF,
	};

	static_assert(std::size(kStorageOrder) == 256);

	mpDstState = mDecodeHeap;
	for(int i=0; i<256; ++i) {
		const uint8 c = kStorageOrder[i];

		mDecodePtrs[c] = (uint16)(mpDstState - mDecodeHeap);

		if (mbPathfindingEnabled)
			*mpDstState++ = kStateAddToPath;

		if (cmos) {
			if (!Decode65C02(c) && !Decode6502(c))
				*mpDstState++ = kStateBreakOnUnsupportedOpcode;
		} else {
			if (!Decode6502(c)) {
				if (!mbIllegalInsnsEnabled || !Decode6502Ill(c))
					*mpDstState++ = kStateBreakOnUnsupportedOpcode;
			}
		}

		if (mpHeatMap)
			*mpDstState++ = kStateUpdateHeatMap;

		*mpDstState++ = kStateReadOpcode;
	}

	// predecode NMI sequence
	const uint16 nmiOffset = (uint16)(mpDstState - mDecodeHeap);
	mDecodePtrs[(uint32)ExtOpcode::Nmi] = nmiOffset;

	*mpDstState++ = kStateReadDummyOpcode;
	*mpDstState++ = kStateReadDummyOpcode;

	*mpDstState++ = kStatePushPCH;
	*mpDstState++ = kStatePushPCL;
	*mpDstState++ = kStatePtoD_B0;

	if (mbStepOver)
		*mpDstState++ = kStateStepOver;

	if (mCPUMode == kATCPUMode_65C02)
		*mpDstState++ = kStateCLD;

	*mpDstState++ = kStatePush;

	if (mpVerifier)
		*mpDstState++ = kStateVerifyNMIEntry;

	*mpDstState++ = kStateSEI;
	*mpDstState++ = kStateNMIVecToPC;
	*mpDstState++ = kStateReadAddrL;
	*mpDstState++ = kStateReadAddrH;
	*mpDstState++ = kStateAddrToPC;

	if (mbPathfindingEnabled)
		*mpDstState++ = kStateAddAsPathStart;

	*mpDstState++ = kStateReadOpcode;

	// predecode IRQ sequence
	const uint16 irqOffset = (uint16)(mpDstState - mDecodeHeap);
	mDecodePtrs[(uint32)ExtOpcode::Irq] = irqOffset;
	*mpDstState++ = kStateReadDummyOpcode;
	*mpDstState++ = kStateReadDummyOpcode;

	*mpDstState++ = kStatePushPCH;
	*mpDstState++ = kStatePushPCL;
	*mpDstState++ = kStatePtoD_B0;

	if (mbStepOver)
		*mpDstState++ = kStateStepOver;

	if (mCPUMode == kATCPUMode_65C02)
		*mpDstState++ = kStateCLD;

	if (mCPUMode == kATCPUMode_6502 && mbAllowBlockedNMIs)
		*mpDstState++ = kStateCheckNMIBlocked;

	*mpDstState++ = kStatePush;

	if (mpVerifier)
		*mpDstState++ = kStateVerifyIRQEntry;

	*mpDstState++ = kStateSEI;

	if (mCPUMode == kATCPUMode_6502 && mbAllowBlockedNMIs)
		*mpDstState++ = kStateNMIOrIRQVecToPCBlockable;
	else
		*mpDstState++ = kStateNMIOrIRQVecToPC;

	*mpDstState++ = kStateReadAddrL;
	*mpDstState++ = kStateDelayInterrupts;
	*mpDstState++ = kStateReadAddrH;
	*mpDstState++ = kStateAddrToPC;

	if (mbPathfindingEnabled)
		*mpDstState++ = kStateAddAsPathStart;

	*mpDstState++ = kStateReadOpcode;

	mDecodePtrs[(uint32)ExtOpcode::Reset] = (uint16)(mpDstState - mDecodeHeap);

	*mpDstState++ = kStateRESVecToPC;
	*mpDstState++ = kStateReadAddrL;
	*mpDstState++ = kStateReadAddrH;
	*mpDstState++ = kStateAddrToPC;
	*mpDstState++ = kStateReadOpcode;

	mDecodePtrs[(uint32)ExtOpcode::Abort] = 0;

	for(uint32 i=0; i<(uint32)ExtOpcode::Abort; ++i) {
		mExtOpcodeAnchors[i] = ExtOpcodeAnchor { mDecodePtrs[i], (uint16)i };
	}

	mNumExtOpcodeAnchors = (uint32)ExtOpcode::Abort;
	std::sort(mExtOpcodeAnchors, &mExtOpcodeAnchors[mNumExtOpcodeAnchors], ExtOpcodeAnchorPred());
}

void ATCPUEmulator::RebuildDecodeTables65816() {
	mpDstState = mDecodeHeap;

	static const struct ModeInfo {
		bool mbUnalignedDP;
		bool mbEmulationMode;
		bool mbMode16;
		bool mbIndex16;
	} kModeInfo[10]={
		{ false, true,  false, false },
		{ false, false, true,  true  },
		{ false, false, true,  false },
		{ false, false, false, true  },
		{ false, false, false, false },

		{ true,  true,  false, false },
		{ true,  false, true,  true  },
		{ true,  false, true,  false },
		{ true,  false, false, true  },
		{ true,  false, false, false },
	};

	for(int i=0; i<10; ++i) {
		const bool unalignedDP = kModeInfo[i].mbUnalignedDP;
		const bool emulationMode = kModeInfo[i].mbEmulationMode;
		const bool mode16 = kModeInfo[i].mbMode16;
		const bool index16 = kModeInfo[i].mbIndex16;

		for(int j=0; j<256; ++j) {
			mDecodePtrs816[i][j] = (uint16)(mpDstState - mDecodeHeap);

			if (mbPathfindingEnabled)
				*mpDstState++ = kStateAddToPath;

			uint8 c = (uint8)j;

			if (!Decode65C816(c, unalignedDP, emulationMode, mode16, index16))
				*mpDstState++ = kStateBreakOnUnsupportedOpcode;

			if (mpHeatMap)
				*mpDstState++ = kStateUpdateHeatMap;

			*mpDstState++ = kStateReadOpcode;
		}

		// predecode NMI sequence
		mDecodePtrs816[i][(uint32)ExtOpcode::Nmi] = (uint16)(mpDstState - mDecodeHeap);
		*mpDstState++ = kStateReadDummyOpcode;
		*mpDstState++ = kStateReadDummyOpcode;

		if (emulationMode) {
			*mpDstState++ = kStatePushPCH;
			*mpDstState++ = kStatePushPCL;
			*mpDstState++ = kStatePtoD_B0;
			*mpDstState++ = kStatePush;
		} else {
			*mpDstState++ = kStatePushPBKNative;
			*mpDstState++ = kStatePushPCHNative;
			*mpDstState++ = kStatePushPCLNative;
			*mpDstState++ = kStatePtoD;
			*mpDstState++ = kStatePushNative;
		}

		if (mbStepOver)
			*mpDstState++ = kStateStepOver;

		*mpDstState++ = kState816_SetI_ClearD;

		if (mpVerifier)
			*mpDstState++ = kStateVerifyNMIEntry;

		if (emulationMode)
			*mpDstState++ = kStateNMIVecToPC;
		else
			*mpDstState++ = kState816_NatNMIVecToPC;

		*mpDstState++ = kStateReadAddrL;
		*mpDstState++ = kStateReadAddrH;
		*mpDstState++ = kStateAddrToPC;

		if (mbPathfindingEnabled)
			*mpDstState++ = kStateAddAsPathStart;

		*mpDstState++ = kStateReadOpcode;

		// predecode IRQ sequence
		mDecodePtrs816[i][(uint32)ExtOpcode::Irq] = (uint16)(mpDstState - mDecodeHeap);
		*mpDstState++ = kStateReadDummyOpcode;
		*mpDstState++ = kStateReadDummyOpcode;

		if (emulationMode) {
			*mpDstState++ = kStatePushPCH;
			*mpDstState++ = kStatePushPCL;
			*mpDstState++ = kStatePtoD_B0;
			*mpDstState++ = kStatePush;
		} else {
			*mpDstState++ = kStatePushPBKNative;
			*mpDstState++ = kStatePushPCHNative;
			*mpDstState++ = kStatePushPCLNative;
			*mpDstState++ = kStatePtoD;
			*mpDstState++ = kStatePushNative;
		}

		if (mbStepOver)
			*mpDstState++ = kStateStepOver;

		*mpDstState++ = kState816_SetI_ClearD;

		if (mpVerifier)
			*mpDstState++ = kStateVerifyIRQEntry;

		if (emulationMode)
			*mpDstState++ = kStateIRQVecToPC;
		else
			*mpDstState++ = kState816_NatIRQVecToPC;

		*mpDstState++ = kStateReadAddrL;
		*mpDstState++ = kStateReadAddrH;
		*mpDstState++ = kStateAddrToPC;

		if (mbPathfindingEnabled)
			*mpDstState++ = kStateAddAsPathStart;

		*mpDstState++ = kStateReadOpcode;

		// predecode RESET sequence
		if (i == 0) {
			mDecodePtrs816[0][(uint32)ExtOpcode::Reset] = (uint16)(mpDstState - mDecodeHeap);

			*mpDstState++ = kStateRESVecToPC;
			*mpDstState++ = kStateReadAddrL;
			*mpDstState++ = kStateReadAddrH;
			*mpDstState++ = kStateAddrToPC;
			*mpDstState++ = kStateReadOpcode;
		} else {
			mDecodePtrs816[i][(uint32)ExtOpcode::Reset] = mDecodePtrs816[0][(uint32)ExtOpcode::Reset];
		}

		// predecode ABORT sequence
		mDecodePtrs816[i][(uint32)ExtOpcode::Abort] = (uint16)(mpDstState - mDecodeHeap);
		*mpDstState++ = kStateReadDummyOpcode;
		*mpDstState++ = kStateReadDummyOpcode;

		// unlike the other interrupt types, for ABORT we must use the faulting PC.
		*mpDstState++ = kState816_ABORT;

		if (emulationMode) {
			*mpDstState++ = kStatePushH16;
			*mpDstState++ = kStatePushL16;
			*mpDstState++ = kStatePtoD_B0;
			*mpDstState++ = kStatePush;
		} else {
			*mpDstState++ = kStatePushPBKNative;
			*mpDstState++ = kStatePushH16;
			*mpDstState++ = kStatePushL16;
			*mpDstState++ = kStatePtoD;
			*mpDstState++ = kStatePushNative;
		}

		if (mbStepOver)
			*mpDstState++ = kStateStepOver;
		
		*mpDstState++ = kState816_SetI_ClearD;

		*mpDstState++ = kStateReadAddrL;
		*mpDstState++ = kStateReadAddrH;
		*mpDstState++ = kStateAddrToPC;

		if (mbPathfindingEnabled)
			*mpDstState++ = kStateAddAsPathStart;

		*mpDstState++ = kStateReadOpcode;
	}

	uint32 numExtOpcodeAnchors = 0;
	for(uint32 i=0; i<10; ++i) {
		for(uint32 j=0; j<kNumExtOpcodes; ++j)
			mExtOpcodeAnchors[numExtOpcodeAnchors++] = ExtOpcodeAnchor { mDecodePtrs816[i][j], (uint16)j };
	}

	mNumExtOpcodeAnchors = numExtOpcodeAnchors;
	std::sort(mExtOpcodeAnchors, &mExtOpcodeAnchors[mNumExtOpcodeAnchors], ExtOpcodeAnchorPred());
}

void ATCPUEmulator::DecodeReadZp() {
	*mpDstState++ = kStateReadAddrL;
	*mpDstState++ = kStateRead;
}

void ATCPUEmulator::DecodeReadZpX() {
	*mpDstState++ = kStateReadAddrL;		// 2
	*mpDstState++ = kStateReadAddX;			// 3
	*mpDstState++ = kStateRead;				// 4
}

void ATCPUEmulator::DecodeReadZpY() {
	*mpDstState++ = kStateReadAddrL;		// 2
	*mpDstState++ = kStateReadAddY;			// 3
	*mpDstState++ = kStateRead;				// 4
}

void ATCPUEmulator::DecodeReadAbs() {
	*mpDstState++ = kStateReadAddrL;		// 2
	*mpDstState++ = kStateReadAddrH;		// 3
	*mpDstState++ = kStateRead;				// 4
}

void ATCPUEmulator::DecodeReadAbsX() {
	*mpDstState++ = kStateReadAddrL;		// 2
	*mpDstState++ = kStateReadAddrHX;		// 3
	*mpDstState++ = kStateReadCarry;		// 4
	*mpDstState++ = kStateRead;				// (5)
}

void ATCPUEmulator::DecodeWriteAddrAbsX() {
	*mpDstState++ = kStateReadAddrL;		// 2
	*mpDstState++ = kStateReadAddrHX;		// 3
	*mpDstState++ = kStateReadCarryForced;	// 4
}

void ATCPUEmulator::DecodeReadAbsY() {
	*mpDstState++ = kStateReadAddrL;		// 2
	*mpDstState++ = kStateReadAddrHY;		// 3
	*mpDstState++ = kStateReadCarry;		// 4
	*mpDstState++ = kStateRead;				// (5)
}

void ATCPUEmulator::DecodeWriteAddrAbsY() {
	*mpDstState++ = kStateReadAddrL;		// 2
	*mpDstState++ = kStateReadAddrHY;		// 3
	*mpDstState++ = kStateReadCarryForced;	// 4
}

void ATCPUEmulator::DecodeReadIndX() {
	*mpDstState++ = kStateReadAddrL;		// 2
	*mpDstState++ = kStateReadAddX;			// 3
	*mpDstState++ = kStateRead;				// 4
	*mpDstState++ = kStateReadIndAddr;		// 5
	*mpDstState++ = kStateRead;				// 6

	if (mbHistoryEnabled)
		*mpDstState++ = kStateAddEAToHistory;
}

void ATCPUEmulator::DecodeReadIndY() {
	*mpDstState++ = kStateReadAddrL;		// 2
	*mpDstState++ = kStateRead;				// 3
	*mpDstState++ = kStateReadIndYAddr;		// 4
	*mpDstState++ = kStateReadCarry;		// 5
	*mpDstState++ = kStateRead;				// (6)

	if (mbHistoryEnabled)
		*mpDstState++ = kStateAddEAToHistory;
}

void ATCPUEmulator::DecodeWriteAddrIndY() {
	*mpDstState++ = kStateReadAddrL;		// 2
	*mpDstState++ = kStateRead;				// 3
	*mpDstState++ = kStateReadIndYAddr;		// 4
	*mpDstState++ = kStateReadCarryForced;	// 5
}

void ATCPUEmulator::DecodeReadInd() {
	*mpDstState++ = kStateReadAddrL;		// 2
	*mpDstState++ = kStateRead;				// 3
	*mpDstState++ = kStateReadIndAddr;		// 4
	*mpDstState++ = kStateRead;				// 5

	if (mbHistoryEnabled)
		*mpDstState++ = kStateAddEAToHistory;
}

#undef ATCPU_PROFILE_OPCODE
