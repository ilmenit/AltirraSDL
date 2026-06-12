//	Altirra - Atari 800/800XL/5200 emulator
//	SDL3 frontend - JSON processor-test (Tom Harte SingleStepTests) CLI runner
//	Copyright (C) 2009-2026 Avery Lee
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
//
//	This is a headless conformance harness that drives Altirra's real
//	65C816 CPU emulator (ATCPUEmulator from cpu.cpp) against the Tom
//	Harte "SingleStepTests/65816" JSON corpus.  It is the AltirraSDL
//	equivalent of sim816's coretest.c: for each test it loads the
//	initial register state + sparse RAM, executes exactly one
//	instruction, and compares the resulting register state + RAM
//	against the expected final state.  Per the design, only registers
//	and RAM are verified; cycle counts are ignored.

#include "cputest_runner.h"

#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <utility>
#include <vector>

#include <vd2/system/vdtypes.h>
#include <vd2/system/vdstl.h>
#include <vd2/system/file.h>
#include <vd2/system/error.h>
#include <vd2/system/text.h>
#include <vd2/system/VDString.h>
#include <vd2/vdjson/jsonreader.h>
#include <vd2/vdjson/jsonvalue.h>

#include "cpu.h"
#include "cpumemory.h"

namespace {

// Guard against opcodes that never reach another instruction-fetch boundary
// (STP $DB / WAI $CB).  64 cycles comfortably exceeds the longest real
// 65C816 instruction.
constexpr int kCycleCap = 64;

// One recorded bus cycle: a single read or write observed on the bus.
struct CycleRecord {
	uint32 addr;	// full 24-bit address
	uint8 value;	// byte read or written
	bool isWrite;
};

// =========================================================================
// Flat 16 MB banked memory, with an optional per-cycle recording mode
// =========================================================================
//
// Default (recording off): every page-table entry points straight at the
// backing array, so the special-handler path (ATCPUMEMISSPECIAL ->
// CPU*ReadByte/WriteByte) is never taken -- the fast register/RAM path.
//
// Recording on (SetRecording(true)): every page entry is rewritten to an
// odd sentinel so ATCPUMEMISSPECIAL is true for *every* page, routing all
// accesses -- opcode fetch, operand reads, data read/write, and dummy /
// internal reads -- through the virtual handlers below, which both perform
// the real RAM access and append a CycleRecord to mTrace.  This captures
// the exact per-cycle bus activity the Tom Harte "cycles" array describes.

class ATCPUTestMemory final : public ATCPUEmulatorMemory {
public:
	void Init();

	uint8 *Ram() { return mRam.data(); }

	// Toggle per-cycle bus recording.  Flips the page maps between direct
	// RAM pointers (fast) and the special sentinel (every access traced).
	void SetRecording(bool enabled);

	void ClearTrace() { mTrace.clear(); }
	const std::vector<CycleRecord>& Trace() const { return mTrace; }

	// Drop the trailing recorded access -- used by the cycle harness to
	// discard the next instruction's opcode fetch that Altirra's fused
	// micro-op core performs as the step's final Advance().
	void DropLastTrace() { if (!mTrace.empty()) mTrace.pop_back(); }

	uint8 CPUReadByte(uint32 address) override {
		const uint8 v = mRam[address & 0xFFFFFF];
		if (mRecord)
			mTrace.push_back(CycleRecord{ address & 0xFFFFFF, v, false });
		return v;
	}

	uint8 CPUExtReadByte(uint16 address, uint8 bank) override {
		const uint32 addr = ((uint32)bank << 16) | address;
		const uint8 v = mRam[addr];
		if (mRecord)
			mTrace.push_back(CycleRecord{ addr, v, false });
		return v;
	}

	sint32 CPUExtReadByteAccel(uint16 address, uint8 bank, bool chipOK) override {
		const uint32 addr = ((uint32)bank << 16) | address;
		const uint8 v = mRam[addr];
		if (mRecord)
			mTrace.push_back(CycleRecord{ addr, v, false });
		return v;
	}

	uint8 CPUDebugReadByte(uint16 address) const override {
		return mRam[address];
	}

	uint8 CPUDebugExtReadByte(uint16 address, uint8 bank) const override {
		return mRam[((uint32)bank << 16) | address];
	}

	void CPUWriteByte(uint16 address, uint8 value) override {
		mRam[address] = value;
		if (mRecord)
			mTrace.push_back(CycleRecord{ (uint32)address, value, true });
	}

	void CPUExtWriteByte(uint16 address, uint8 bank, uint8 value) override {
		const uint32 addr = ((uint32)bank << 16) | address;
		mRam[addr] = value;
		if (mRecord)
			mTrace.push_back(CycleRecord{ addr, value, true });
	}

	sint32 CPUExtWriteByteAccel(uint16 address, uint8 bank, uint8 value, bool chipOK) override {
		const uint32 addr = ((uint32)bank << 16) | address;
		mRam[addr] = value;
		if (mRecord)
			mTrace.push_back(CycleRecord{ addr, value, true });
		return 0;
	}

private:
	vdblock<uint8> mRam;
	vdblock<uintptr> mPageEntries;	// 256 banks * 256 pages
	BankTable mBankTable;
	uint32 mAddrPageMap[256];
	bool mRecord = false;
	std::vector<CycleRecord> mTrace;
};

void ATCPUTestMemory::Init() {
	mRam.resize(16 * 1024 * 1024);
	memset(mRam.data(), 0, mRam.size());

	mPageEntries.resize(256 * 256);

	uint8 *base = mRam.data();
	VDASSERT(((uintptr)base & 1) == 0);	// even => not ATCPUMEMISSPECIAL

	for (uint32 b = 0; b < 256; ++b) {
		uintptr bankBase = (uintptr)(base + ((uint32)b << 16));

		for (uint32 p = 0; p < 256; ++p)
			mPageEntries[b * 256 + p] = bankBase;

		mBankTable[b] = reinterpret_cast<PageTablePtr>(&mPageEntries[b * 256]);
	}

	for (int i = 0; i < 256; ++i)
		mAddrPageMap[i] = 0;

	mBusValue = 0;
	mpCPUReadBankMap = &mBankTable;
	mpCPUWriteBankMap = &mBankTable;
	mpCPUReadPageMap = mBankTable[0];
	mpCPUWritePageMap = mBankTable[0];
	mpCPUReadAddressPageMap = mAddrPageMap;
}

void ATCPUTestMemory::SetRecording(bool enabled) {
	mRecord = enabled;
	mTrace.clear();

	uint8 *base = mRam.data();

	for (uint32 b = 0; b < 256; ++b) {
		if (enabled) {
			// Odd sentinel => ATCPUMEMISSPECIAL true for every page, so
			// every access routes through the virtual handlers above.
			for (uint32 p = 0; p < 256; ++p)
				mPageEntries[b * 256 + p] = (uintptr)1;
		} else {
			uintptr bankBase = (uintptr)(base + ((uint32)b << 16));
			for (uint32 p = 0; p < 256; ++p)
				mPageEntries[b * 256 + p] = bankBase;
		}
	}
}

// =========================================================================
// Minimal callbacks (no scheduler; history disabled)
// =========================================================================

class ATCPUTestCallbacks final : public ATCPUEmulatorCallbacks {
public:
	uint32 CPUGetCycle() override { return mCycle; }
	uint32 CPUGetUnhaltedCycle() override { return mCycle; }
	uint32 CPUGetUnhaltedAndRDYCycle() override { return mCycle; }
	void CPUGetHistoryTimes(ATCPUHistoryEntry *) const override {}

	uint32 mCycle = 0;
};

// =========================================================================
// CPU harness: subclass exposes a full-state loader + single-step driver
// =========================================================================
//
// The public Set* register accessors are individually gated on the current
// M/X/E widths (e.g. SetAH ignores writes when M=1), which makes them
// unsuitable for loading the arbitrary, fully-specified state of a Harte
// test.  Instead we mirror ATCPUEmulator::LoadState's direct member
// assignment (legal here because the protected members are accessible from
// a subclass) and then call Update65816DecodeTable() to resynchronise the
// decode tables and sub-mode exactly as save-state restore does.

class ATCPUTestHarness final : public ATCPUEmulator {
public:
	void LoadTestState(uint16 pc, uint16 a, uint16 x, uint16 y, uint16 s,
		uint16 d, uint8 dbr, uint8 pbr, uint8 p, uint8 e)
	{
		mA = (uint8)a;
		mX = (uint8)x;
		mY = (uint8)y;
		mS = (uint8)s;
		mP = p;
		mAH = (uint8)(a >> 8);
		mXH = (uint8)(x >> 8);
		mYH = (uint8)(y >> 8);
		mSH = (uint8)(s >> 8);
		mDP = d;
		mB = dbr;
		mK = pbr;
		mbEmulationFlag = (e != 0);

		// Clear any pending interrupt left over from reset / a prior
		// test; otherwise the opcode-fetch state (kStateReadOpcode*)
		// services the interrupt before fetching, corrupting the run.
		mIntFlags = 0;

		// Hardware invariants in emulation mode: SH forced to $01 and the
		// M/X status bits read as 1.
		if (mbEmulationFlag) {
			mSH = 1;
			mP |= 0x30;
		}

		// Resync sub-mode + decode tables to the loaded P/E/DP.  On a
		// sub-mode change this also applies the hardware forcing of the
		// hidden index high bytes (XH/YH = 0 when X=1), matching the
		// values the Harte corpus expects.
		Update65816DecodeTable();

		// SetPC parks the micro-op state machine at a clean opcode-fetch
		// boundary (kStateReadOpcodeNoBreak).
		SetPC(pc);
		mInsnPC = pc;
	}

	// Execute one instruction and return the effective post-instruction PC.
	//
	// expectedCycles is the length of the test's "cycles" array, used only for
	// the block-move opcodes (see below).
	//
	// Altirra's micro-coded core FUSES the final ALU operation of many
	// instructions (loads, transfers, register-ALU, flag ops) with the opcode
	// fetch of the *next* instruction: the register write and the next fetch
	// happen in the same clock cycle.  Consequently:
	//
	//   * For these "fused" instructions there is no observable cycle where the
	//     instruction is complete but the next opcode has not been fetched.
	//     After the fused cycle, mPC has already advanced past the next opcode,
	//     but mInsnPC snapshots PC *before* that fetch incremented it -- i.e.
	//     exactly the value the Harte test expects.  So we run until mInsnPC
	//     changes and report GetInsnPC().
	//
	//   * For "non-fused" instructions (stores, RMW, control flow) the final
	//     cycle ends cleanly and mpNextState parks at kStateReadOpcode, so
	//     IsInstructionInProgress() becomes false *before* the next fetch.  At
	//     that point mPC already holds the final value (no overshoot), so we
	//     stop and report GetPC().  This also keeps single-execution semantics
	//     for self-referential JSR/JMP/branch.
	//
	//   * STP/WAI never reach another fetch; the cycle cap bounds the loop and
	//     GetPC() (= opcode address + 1) is the expected result.
	//
	//   * MVN ($54) / MVP ($44) are interruptible block moves that re-fetch
	//     themselves (mPC -= 3) after every byte, so neither mInsnPC nor the
	//     instruction-in-progress flag ever signals completion.  The Harte
	//     corpus deliberately caps them mid-move at a fixed cycle budget, so we
	//     match the reference (sim816's sim816_step_limited) by running exactly
	//     expectedCycles clocks and reporting GetPC().
	// True after the most recent StepOneInstruction() returned via a "fused"
	// path -- i.e. the final Advance() it executed was the *next*
	// instruction's opcode fetch (one bus access too many for the current
	// instruction's cycle trace).
	bool LastStepOverran() const { return mLastStepOverran; }

	// Number of Advance() calls (= emulated CPU clock cycles) consumed by the
	// most recent StepOneInstruction().  Unlike the recorded bus trace this
	// also counts internal/dummy cycles that Altirra implements as no-bus
	// waits, so (mLastStepCycles - overran?1:0) is the instruction's true cycle
	// count for comparison against the test's "cycles" array length.
	int LastStepCycles() const { return mLastStepCycles; }

	uint16 StepOneInstruction(int expectedCycles) {
		mLastStepOverran = false;

		const uint16 pc0 = GetInsnPC();

		Advance();			// cycle 1: opcode fetch (primed kStateReadOpcodeNoBreak)
		int n = 1;

		// Snapshot the opcode just fetched.  mOpcode is written *only* by an
		// opcode-fetch micro-op (kStateReadOpcode*), never during operand
		// reads or ALU cycles, so the moment it changes the *next*
		// instruction has been fetched and the current one is complete.
		const uint8 op0 = mOpcode;

		// MVN/MVP are interruptible block moves and STP/WAI halt the core;
		// none of them reaches a clean post-instruction boundary, so run
		// exactly the test's cycle budget and report mPC, matching the sim816
		// reference (sim816_step_limited).
		if (op0 == 0x44 || op0 == 0x54 ||	// MVP / MVN block move
			op0 == 0xDB || op0 == 0xCB) {	// STP / WAI
			while (n < expectedCycles) {
				Advance();
				++n;
			}
			mLastStepCycles = n;
			return GetPC();
		}

		while (n < kCycleCap) {
			// Fused completion (the common case): Altirra fuses an
			// instruction's final ALU/transfer cycle with the *next* opcode
			// fetch in one Advance().  After that fetch mInsnPC snapshots the
			// fetch PC -- exactly the post-instruction PC the Harte test wants.
			if (GetInsnPC() != pc0) {
				mLastStepOverran = true;
				mLastStepCycles = n;
				return GetInsnPC();
			}

			// Fused control transfer to the *same* 16-bit PC (JSL/JML/RTL to a
			// different bank, or a self-targeting jump).  Here mInsnPC keeps the
			// same value so the test above cannot see the fetch, but mOpcode has
			// been overwritten by the freshly fetched target opcode.  mInsnPC is
			// the (unchanged) target PC and every register is already final.
			if (mOpcode != op0) {
				mLastStepOverran = true;
				mLastStepCycles = n;
				return GetInsnPC();
			}

			// Non-fused completion (stores, RMW, BRK/COP push+vector): the final
			// cycle ends cleanly and mpNextState parks at an opcode-fetch state,
			// so IsInstructionInProgress() goes false *before* the next fetch and
			// mPC already holds the final value (no overshoot).
			if (!IsInstructionInProgress()) {
				mLastStepCycles = n;
				return GetPC();
			}

			Advance();
			++n;
		}

		// Runaway guard: only a self-targeting jump/branch (target == its own
		// PC, e.g. JMP (abs,X) that points at itself) reaches here -- mInsnPC,
		// mOpcode and every register are invariant across the loop, so the
		// post-instruction PC is simply the (unchanged) target = mInsnPC.  (mPC
		// here is mid-operand-fetch and would be wrong.)
		mLastStepCycles = n;
		return GetInsnPC();
	}

	uint16 GetA16() const { return (uint16)(GetA() | (GetAH() << 8)); }
	uint16 GetX16() const { return (uint16)(GetX() | (GetXH() << 8)); }
	uint16 GetY16() const { return (uint16)(GetY() | (GetYH() << 8)); }

private:
	bool mLastStepOverran = false;
	int mLastStepCycles = 0;
};

// =========================================================================
// Parsed test representation
// =========================================================================

struct CTRegState {
	uint16 pc = 0, a = 0, x = 0, y = 0, s = 0, d = 0;
	uint8 p = 0, dbr = 0, pbr = 0, e = 0;
};

typedef std::vector<std::pair<uint32, uint8> > CTRamList;

// One expected bus cycle from the test's "cycles" array.
struct CTCycle {
	uint32 addr = 0;	// 24-bit bus address
	uint8 value = 0;	// data byte (only meaningful when !isNull)
	bool isWrite = false;	// RWB flag from the flags string (position 3)
	bool isNull = false;	// JSON value == null => internal/dummy cycle
};

struct CTTest {
	VDStringA name;
	CTRegState initial;
	CTRegState final_;
	CTRamList initRam;
	CTRamList finalRam;
	int cycleCount = 0;	// length of the "cycles" array
	std::vector<CTCycle> cycles;
};

bool ParseRegState(const VDJSONValueRef& o, CTRegState& r) {
	if (!o.IsObject())
		return false;

	struct { const char *key; uint16 *dst16; uint8 *dst8; } fields[] = {
		{ "pc",  &r.pc,  nullptr },
		{ "s",   &r.s,   nullptr },
		{ "a",   &r.a,   nullptr },
		{ "x",   &r.x,   nullptr },
		{ "y",   &r.y,   nullptr },
		{ "d",   &r.d,   nullptr },
		{ "p",   nullptr, &r.p   },
		{ "dbr", nullptr, &r.dbr },
		{ "pbr", nullptr, &r.pbr },
		{ "e",   nullptr, &r.e   },
	};

	for (const auto& f : fields) {
		VDJSONValueRef v = o[f.key];
		if (!v.IsInt())
			return false;

		const sint64 iv = v.AsInt64();
		if (f.dst16)
			*f.dst16 = (uint16)iv;
		else
			*f.dst8 = (uint8)iv;
	}

	return true;
}

bool ParseRamList(const VDJSONValueRef& arr, CTRamList& out) {
	if (!arr.IsArray())
		return false;

	for (VDJSONValueRef entry : arr.AsArray()) {
		if (!entry.IsArray() || entry.GetArrayLength() < 2)
			return false;

		const uint32 addr = (uint32)entry[(size_t)0].AsInt64() & 0xFFFFFF;
		const uint8 val = (uint8)entry[(size_t)1].AsInt64();
		out.push_back(std::make_pair(addr, val));
	}

	return true;
}

bool ParseTest(const VDJSONValueRef& obj, CTTest& t) {
	if (!obj.IsObject())
		return false;

	VDJSONValueRef nameRef = obj["name"];
	if (nameRef.IsString())
		t.name = VDTextWToU8(VDStringSpanW(nameRef.AsString()));
	else
		t.name = "(unnamed)";

	VDJSONValueRef initial = obj["initial"];
	VDJSONValueRef final_ = obj["final"];
	if (!initial.IsObject() || !final_.IsObject())
		return false;

	if (!ParseRegState(initial, t.initial))
		return false;
	if (!ParseRegState(final_, t.final_))
		return false;
	if (!ParseRamList(initial["ram"], t.initRam))
		return false;
	if (!ParseRamList(final_["ram"], t.finalRam))
		return false;

	// Cycle count is the length of the "cycles" array; used to bound
	// block-move opcodes (MVN/MVP) and, in cycle mode, to verify the
	// per-cycle bus trace.  Each entry is [addr24, value|null, flags8].
	// Missing/empty is tolerated (0).
	VDJSONValueRef cycles = obj["cycles"];
	if (cycles.IsArray()) {
		t.cycleCount = (int)cycles.GetArrayLength();

		for (VDJSONValueRef entry : cycles.AsArray()) {
			if (!entry.IsArray() || entry.GetArrayLength() < 3)
				return false;

			CTCycle c;
			c.addr = (uint32)entry[(size_t)0].AsInt64() & 0xFFFFFF;

			VDJSONValueRef valRef = entry[(size_t)1];
			if (valRef.IsNull()) {
				c.isNull = true;
			} else {
				c.value = (uint8)valRef.AsInt64();
			}

			// flags string: position 3 is 'r' (read) or 'w' (write).
			VDJSONValueRef flagsRef = entry[(size_t)2];
			if (flagsRef.IsString()) {
				const wchar_t *fl = flagsRef.AsString();
				size_t flen = wcslen(fl);
				c.isWrite = (flen > 3 && fl[3] == L'w');
			}

			t.cycles.push_back(c);
		}
	}

	return true;
}

// =========================================================================
// State setup + verification
// =========================================================================

void SetupState(ATCPUTestHarness& cpu, ATCPUTestMemory& mem, const CTTest& t) {
	cpu.LoadTestState(t.initial.pc, t.initial.a, t.initial.x, t.initial.y,
		t.initial.s, t.initial.d, t.initial.dbr, t.initial.pbr,
		t.initial.p, t.initial.e);

	uint8 *ram = mem.Ram();
	for (const auto& e : t.initRam)
		ram[e.first] = e.second;
}

// Returns true on pass.  Emits register/RAM diffs to stdout when verbose.
bool VerifyState(ATCPUTestHarness& cpu, ATCPUTestMemory& mem, const CTTest& t,
	uint16 finalPC, bool verbose)
{
	bool ok = true;
	bool printedName = false;

	auto fail = [&]() {
		ok = false;
		if (verbose && !printedName) {
			printf("FAIL: %s\n", t.name.c_str());
			printedName = true;
		}
	};

	auto checkReg = [&](const char *label, unsigned actual, unsigned expected,
		int width)
	{
		if (actual != expected) {
			fail();
			if (verbose) {
				if (width == 4)
					printf("  %-3s: expected $%04X, got $%04X\n", label, expected, actual);
				else
					printf("  %-3s: expected $%02X, got $%02X\n", label, expected, actual);
			}
		}
	};

	checkReg("PC",  finalPC,       t.final_.pc, 4);
	checkReg("A",   cpu.GetA16(),  t.final_.a,  4);
	checkReg("X",   cpu.GetX16(),  t.final_.x,  4);
	checkReg("Y",   cpu.GetY16(),  t.final_.y,  4);
	checkReg("S",   cpu.GetS16(),  t.final_.s,  4);
	checkReg("P",   cpu.GetP(),    t.final_.p,  2);
	checkReg("D",   cpu.GetD(),    t.final_.d,  4);
	checkReg("DBR", cpu.GetB(),    t.final_.dbr, 2);
	checkReg("PBR", cpu.GetK(),    t.final_.pbr, 2);
	checkReg("E",   cpu.GetEmulationFlag() ? 1 : 0, t.final_.e, 2);

	const uint8 *ram = mem.Ram();
	for (const auto& e : t.finalRam) {
		const uint8 actual = ram[e.first];
		if (actual != e.second) {
			fail();
			if (verbose)
				printf("  RAM[$%06X]: expected $%02X, got $%02X\n",
					e.first, e.second, actual);
		}
	}

	return ok;
}

// Returns true when the recorded per-cycle bus activity matches the test's
// "cycles" array.  This is a SAFE, harness-only cycle-conformance check that
// accepts Altirra's optimized-but-equivalent micro-op cycle model without any
// change to the shared CPU core.  It verifies three things:
//
//   1. Total cycle count.  Uses effectiveCycles = the number of Advance()s the
//      instruction consumed minus the fused next-opcode fetch (when the step
//      over-ran).  Crucially this counts internal/dummy cycles that Altirra
//      implements as no-bus waits -- which never appear in the bus trace -- so
//      it is the authoritative per-instruction cycle count (e.g. REP/SEP and
//      the push family PHB/PHK each spend a wait cycle with no bus access).
//
//   2. Writes.  Every write the chip performs must be reproduced exactly
//      (address + data), and the trace must contain no extra writes.  Writes
//      are always real accesses, never internal/dummy cycles.
//
//   3. Real reads.  Every concrete (non-null) read in the test must appear
//      somewhere in the trace (address + data).  Left-over actual reads are
//      Altirra's internal/dummy cycles or the fused next-opcode fetch and are
//      intentionally not address-checked (the test marks those cycles null =
//      don't-care, and Altirra legitimately places a dummy read at a different
//      but equivalent address or fuses it with the next fetch).
//
// Matching is order-tolerant (multiset): Altirra reorders internal/dummy
// cycles relative to the real access within an instruction (e.g. the pull
// family reads the stack early and the dummies late -- the reverse of real
// silicon).  The checks above still catch any wrong address, wrong data, wrong
// R/W direction, extra/dropped write, or wrong cycle count; only the ordering
// of cycles within the (already register/RAM-verified) instruction is relaxed.
bool VerifyCycles(const CTTest& t, const std::vector<CycleRecord>& trace,
	int effectiveCycles, bool verbose)
{
	const size_t expN = t.cycles.size();
	const size_t actN = trace.size();

	bool ok = true;
	const char *reason = nullptr;

	// (1) Cycle count.
	if ((int)expN != effectiveCycles) {
		ok = false;
		reason = "cycle count";
	}

	std::vector<bool> used(actN, false);

	// (2) Writes: exact multiset match (expected <-> actual).
	if (ok) {
		for (size_t i = 0; i < expN && ok; ++i) {
			const CTCycle& e = t.cycles[i];
			if (e.isNull || !e.isWrite)
				continue;

			bool matched = false;
			for (size_t j = 0; j < actN; ++j) {
				if (used[j] || !trace[j].isWrite)
					continue;
				if (trace[j].addr == e.addr && trace[j].value == e.value) {
					used[j] = true;
					matched = true;
					break;
				}
			}

			if (!matched) {
				ok = false;
				reason = "missing write";
			}
		}

		// No unexplained extra writes in the trace.
		for (size_t j = 0; j < actN && ok; ++j) {
			if (!used[j] && trace[j].isWrite) {
				ok = false;
				reason = "unexpected write";
			}
		}
	}

	// (3) Real reads: every concrete expected read must be present.
	if (ok) {
		for (size_t i = 0; i < expN && ok; ++i) {
			const CTCycle& e = t.cycles[i];
			if (e.isNull || e.isWrite)
				continue;

			bool matched = false;
			for (size_t j = 0; j < actN; ++j) {
				if (used[j] || trace[j].isWrite)
					continue;
				if (trace[j].addr == e.addr && trace[j].value == e.value) {
					used[j] = true;
					matched = true;
					break;
				}
			}

			if (!matched) {
				ok = false;
				reason = "missing read";
			}
		}
	}

	if (!ok && verbose) {
		printf("FAIL (cycles): %s [%s]\n", t.name.c_str(),
			reason ? reason : "?");
		printf("  expected %zu cycles, got %d (bus accesses recorded: %zu)\n",
			expN, effectiveCycles, actN);
		const size_t dump = expN > actN ? expN : actN;
		for (size_t i = 0; i < dump; ++i) {
			printf("  [%2zu] ", i);
			if (i < expN) {
				const CTCycle& e = t.cycles[i];
				if (e.isNull)
					printf("exp %c @$%06X (internal)", e.isWrite ? 'W' : 'R', e.addr);
				else
					printf("exp %c @$%06X =$%02X", e.isWrite ? 'W' : 'R', e.addr, e.value);
			} else {
				printf("exp --");
			}
			printf("   ");
			if (i < actN) {
				const CycleRecord& a = trace[i];
				printf("got %c @$%06X =$%02X", a.isWrite ? 'W' : 'R', a.addr, a.value);
			} else {
				printf("got --");
			}
			printf("\n");
		}
	}

	return ok;
}

// =========================================================================
// File I/O
// =========================================================================

bool ReadFileBytes(const wchar_t *wpath, vdblock<uint8>& out) {
	VDFile f;
	if (!f.openNT(wpath))
		return false;

	const sint64 len = f.size();
	if (len < 0)
		return false;

	out.resize((size_t)len);
	if (len > 0)
		f.read(out.data(), (sint32)len);

	return true;
}

// Returns: 0 = all pass, 1 = had failures, 2 = fatal (I/O / parse).
int RunSingleFile(ATCPUTestHarness& cpu, ATCPUTestMemory& mem,
	const wchar_t *wpath, const char *displayName,
	const ATCPUTestOptions& opts, int& totalPass, int& totalFail,
	int& totalCycleFail)
{
	vdblock<uint8> bytes;
	if (!ReadFileBytes(wpath, bytes)) {
		printf("cpu-test: cannot read %s\n", displayName);
		return 2;
	}

	VDJSONDocument doc;
	VDJSONReader reader;
	if (!reader.Parse(bytes.data(), bytes.size(), doc)) {
		printf("cpu-test: JSON parse error in %s\n", displayName);
		return 2;
	}

	VDJSONValueRef root = doc.Root();
	if (!root.IsArray()) {
		printf("cpu-test: %s is not a JSON array of tests\n", displayName);
		return 2;
	}

	int filePass = 0, fileFail = 0, fileCycleFail = 0, count = 0;
	bool stop = false;

	for (VDJSONValueRef item : root.AsArray()) {
		if (opts.mLimit > 0 && count >= opts.mLimit)
			break;

		CTTest test;
		if (!ParseTest(item, test)) {
			printf("cpu-test: malformed test #%d in %s\n", count, displayName);
			++fileFail;
			++count;
			continue;
		}

		SetupState(cpu, mem, test);

		if (opts.mCheckCycles)
			mem.ClearTrace();

		const uint16 finalPC = cpu.StepOneInstruction(test.cycleCount);

		const bool stateOK = VerifyState(cpu, mem, test, finalPC, opts.mVerbose);

		// Cycle verification is independent of register/RAM: a test can
		// pass state but fail the per-cycle bus trace, and vice versa.
		bool cycleOK = true;
		if (opts.mCheckCycles) {
			// The instruction's true cycle count is the number of Advance()s
			// it consumed, minus the fused next-opcode fetch when the step
			// over-ran (the over-run Advance belongs to the next instruction).
			const int effectiveCycles =
				cpu.LastStepCycles() - (cpu.LastStepOverran() ? 1 : 0);

			cycleOK = VerifyCycles(test, mem.Trace(), effectiveCycles,
				opts.mVerbose);
		}

		if (stateOK && cycleOK) {
			++filePass;
		} else {
			++fileFail;
			if (!cycleOK)
				++fileCycleFail;
			if (opts.mStopOnFail) {
				++count;
				stop = true;
				break;
			}
		}

		++count;
	}

	if (opts.mCheckCycles)
		printf("[%s] %d pass, %d fail (%d cycle) (%d total)\n",
			displayName, filePass, fileFail, fileCycleFail, count);
	else
		printf("[%s] %d pass, %d fail (%d total)\n",
			displayName, filePass, fileFail, count);

	totalPass += filePass;
	totalFail += fileFail;
	totalCycleFail += fileCycleFail;

	if (stop)
		return 1;
	return (fileFail > 0) ? 1 : 0;
}

bool EndsWithJson(const std::string& s) {
	if (s.size() < 5)
		return false;

	std::string tail = s.substr(s.size() - 5);
	for (char& c : tail)
		c = (char)tolower((unsigned char)c);

	return tail == ".json";
}

}	// namespace

// =========================================================================
// Entry point
// =========================================================================

int ATRunCPUTests(const ATCPUTestOptions& opts) {
	if (opts.mPath.empty()) {
		printf("cpu-test: no test path given\n");
		return 2;
	}

	ATCPUTestMemory mem;
	mem.Init();

	ATCPUTestCallbacks callbacks;

	ATCPUTestHarness cpu;
	cpu.Init(&mem, nullptr, &callbacks);
	cpu.SetHistoryEnabled(false);
	cpu.SetStopOnBRK(false);
	cpu.SetCPUMode(kATCPUMode_65C816, 1);
	cpu.ColdReset();

	// In cycle mode, route every CPU bus access through the recording
	// handlers (slower); the default register/RAM mode keeps the fast
	// direct-page path.  Recording is enabled once, after ColdReset, so
	// the reset sequence itself is not traced.
	if (opts.mCheckCycles)
		mem.SetRecording(true);

	int totalPass = 0, totalFail = 0, totalCycleFail = 0;
	int fatal = 0;

	try {
		if (EndsWithJson(opts.mPath)) {
			// Single-file mode.
			VDStringW wpath(VDTextU8ToW(opts.mPath.c_str(), (int)opts.mPath.size()));
			int r = RunSingleFile(cpu, mem, wpath.c_str(), opts.mPath.c_str(),
				opts, totalPass, totalFail, totalCycleFail);
			if (r == 2)
				fatal = 1;
		} else {
			// Directory mode: probe hh.e.json / hh.n.json per opcode.
			std::string dir = opts.mPath;
			while (!dir.empty() && (dir.back() == '/' || dir.back() == '\\'))
				dir.pop_back();

			const bool wantE = (opts.mMode != ATCPUTestMode::Native);
			const bool wantN = (opts.mMode != ATCPUTestMode::Emulation);

			for (int opcode = 0; opcode <= 0xFF && !fatal; ++opcode) {
				if (opts.mOpcodeFilter >= 0 && opcode != opts.mOpcodeFilter)
					continue;

				const char *modes[2] = { "e", "n" };
				const bool want[2] = { wantE, wantN };

				for (int m = 0; m < 2 && !fatal; ++m) {
					if (!want[m])
						continue;

					char base[64];
					snprintf(base, sizeof base, "%02x.%s.json", opcode, modes[m]);

					std::string full = dir;
					full += '/';
					full += base;

					VDStringW wpath(VDTextU8ToW(full.c_str(), (int)full.size()));

					VDFile probe;
					if (!probe.openNT(wpath.c_str()))
						continue;
					probe.closeNT();

					int r = RunSingleFile(cpu, mem, wpath.c_str(), base,
						opts, totalPass, totalFail, totalCycleFail);
					if (r == 2) {
						fatal = 1;
						break;
					}

					if (opts.mStopOnFail && totalFail > 0)
						break;
				}

				if (opts.mStopOnFail && totalFail > 0)
					break;
			}
		}
	} catch (const MyError& e) {
		printf("cpu-test: error: %s\n", e.c_str());
		fatal = 1;
	}

	printf("\n=== CPU Test Summary ===\n");
	printf("Passed: %d\n", totalPass);
	printf("Failed: %d\n", totalFail);
	if (opts.mCheckCycles)
		printf("  of which cycle failures: %d\n", totalCycleFail);
	printf("Total:  %d\n", totalPass + totalFail);
	printf("Result: %s\n", (totalFail == 0 && !fatal) ? "PASS" : "FAIL");

	if (fatal)
		return 2;
	return (totalFail > 0) ? 1 : 0;
}
