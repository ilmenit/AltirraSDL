// Production CPU conformance runner; optional debugger services fail if used.
#include <stdafx.h>
#include <cstdio>
#include <cstdlib>
#include "cpu.h"
#include "cpuheatmap.h"
#include "verifier.h"
#include "bkptmanager.h"
#include "cpuhookmanager.h"
#include "disasm.h"
#include "cputest_runner.h"

static void Unexpected(const char *service) {
	fprintf(stderr, "Unexpected CPU test service: %s\n", service);
	std::exit(1);
}
void ATCPUHeatMap::ProcessInsn(const ATCPUEmulator&, uint8, uint16, uint16) { Unexpected("heat map"); }
void ATCPUVerifier::OnReset() { Unexpected("verifier reset"); }
void ATCPUVerifier::OnIRQEntry() { Unexpected("verifier IRQ"); }
void ATCPUVerifier::OnNMIEntry() { Unexpected("verifier NMI"); }
void ATCPUVerifier::VerifyInsn(const ATCPUEmulator&, uint8, uint16) { Unexpected("verifier instruction"); }
int ATBreakpointManager::CheckPCBreakpoints(uint32, uint32, const BreakpointIndices*) { Unexpected("breakpoint"); return 0; }
uint8 ATCPUHookManager::OnHookHit(uint16) const { Unexpected("CPU hook"); return 0; }
uint16 ATDisassembleInsn(uint16, uint8) { Unexpected("disassembler"); return 0; }
void ATConsoleWrite(const char *s) { fputs(s, stdout); }
void ATConsolePrintfImpl(const char *, ...) { Unexpected("console printf"); }
void ATConsoleTaggedPrintfImpl(const char *, ...) { Unexpected("tagged console printf"); }

// Replay from each cycle position, including pending interrupts and the
// accelerated CPU's subcycle cursor. This uses the production state API.
#include <array>
#include <algorithm>
#include <at/atcore/snapshot.h>
#include <vd2/system/refcount.h>
#include "cpumemory.h"
#include "cpustates.h"

namespace {
class SnapshotMemory final : public ATCPUEmulatorMemory {
public:
	std::array<uint8, 65536> bytes {};
	PageTable pages {};
	BankTable banks {};
	uint32 addresses[256] {};
	SnapshotMemory() {
		for (auto& page : pages) page = 1;
		for (auto& bank : banks) bank = &pages;
		mpCPUReadPageMap = mpCPUWritePageMap = &pages;
		mpCPUReadBankMap = mpCPUWriteBankMap = &banks;
		mpCPUReadAddressPageMap = addresses;
		mBusValue = 0;
	}
	uint8 CPUReadByte(uint32 address) override { return bytes[address & 65535]; }
	uint8 CPUExtReadByte(uint16 address, uint8) override { return bytes[address]; }
	sint32 CPUExtReadByteAccel(uint16 address, uint8, bool) override { return bytes[address]; }
	uint8 CPUDebugReadByte(uint16 address) const override { return bytes[address]; }
	uint8 CPUDebugExtReadByte(uint16 address, uint8) const override { return bytes[address]; }
	void CPUWriteByte(uint16 address, uint8 value) override { bytes[address] = value; }
	void CPUExtWriteByte(uint16 address, uint8, uint8 value) override { bytes[address] = value; }
	sint32 CPUExtWriteByteAccel(uint16 address, uint8, uint8 value, bool) override {
		bytes[address] = value;
		return 0;
	}
};
class SnapshotClock final : public ATCPUEmulatorCallbacks {
public:
	uint32 tick = 1000;
	uint32 CPUGetCycle() override { return tick; }
	uint32 CPUGetUnhaltedCycle() override { return tick; }
	uint32 CPUGetUnhaltedAndRDYCycle() override { return tick; }
	void CPUGetHistoryTimes(ATCPUHistoryEntry*) const override {}
};
class SnapshotCPU final : public ATCPUEmulator {
public:
	void SeedRuntime(uint32 tick) {
		mNMIIgnoreUnhaltedCycle = tick + 1;
		mbNMIForced = true;
		mInsnPC = 0x1234;
	}
	std::array<uint32, 28> Signature(uint32 tick) const {
		return {mPC, mInsnPC, mA, mX, mY, mP, mS, mAddr,
			mAddr2, mData, mData16, mIntFlags, mNMIAssertTime - tick,
			mIRQAssertTime - tick, mIRQAcknowledgeTime - tick,
			mNMIIgnoreUnhaltedCycle - tick, (uint32)mbNMIForced,
			mSubCyclesLeft, (uint32)mbForceNextCycleSlow,
			mCPUMode == kATCPUMode_65C816 ? mAH : 0U,
			mCPUMode == kATCPUMode_65C816 ? mXH : 0U,
			mCPUMode == kATCPUMode_65C816 ? mYH : 0U,
			mCPUMode == kATCPUMode_65C816 ? mSH : 0U,
			mCPUMode == kATCPUMode_65C816 ? mB : 0U,
			mCPUMode == kATCPUMode_65C816 ? mK : 0U,
			mCPUMode == kATCPUMode_65C816 ? mDP : 0U,
			(uint32)mbEmulationFlag, (uint32)(*mpNextState == AT6502States::kStateReadOpcodeNoBreak
				? AT6502States::kStateReadOpcode : *mpNextState)};
	}
};
bool SnapshotRoundtrips() {
	for (const auto mode : {kATCPUMode_6502, kATCPUMode_65C816}) {
		for (const uint32 speed : {1U, 4U}) {
			if (mode == kATCPUMode_6502 && speed != 1) continue;
			SnapshotMemory memory;
			SnapshotClock clock;
			SnapshotCPU cpu;
			cpu.Init(&memory, nullptr, &clock);
			cpu.SetCPUMode(mode, speed);
			cpu.ColdReset();
			cpu.SetPC(0x0800);
			// LDA, indexed indirect load, INC, taken branch, JMP.
			const uint8 program[] = {0xA9, 3, 0xA2, 0, 0x85, 0x10,
				0xA1, 0x20, 0xE6, 0x10, 0xD0, 0xFC, 0x4C, 0, 8};
			std::copy(std::begin(program), std::end(program), memory.bytes.begin() + 0x800);
			if (mode == kATCPUMode_65C816) {
				// (dp,X), PEI across page $00FF, and RTL all contain
				// cycle states absent from the old snapshot classifier.
				const uint8 calls[] = {0xA2, 0, 0xA1, 0x20, 0x22,
					0x10, 8, 0, 0x4C, 0, 8};
				const uint8 subroutine[] = {0xD4, 0xFF, 0x68, 0x68, 0x6B};
				std::copy(std::begin(calls), std::end(calls), memory.bytes.begin() + 0x800);
				std::copy(std::begin(subroutine), std::end(subroutine), memory.bytes.begin() + 0x810);
				memory.bytes[0xFF] = 0x55;
				memory.bytes[0x100] = 0x66;
			}
			memory.bytes[0x20] = 0;
			memory.bytes[0x21] = 0x40;
			memory.bytes[0x4000] = 7;
			memory.bytes[0xFFFA] = 0;
			memory.bytes[0xFFFB] = 9;
			memory.bytes[0x900] = 0x40; // RTI
			cpu.SeedRuntime(clock.tick);
			for (unsigned position = 0; position < 80; ++position) {
				if (position == 15) cpu.AssertNMI();
				vdrefptr<IATObjectState> saved;
				cpu.SaveState(~saved);
				const auto savedMemory = memory.bytes;
				const auto savedTick = clock.tick;
				const auto original = cpu.Signature(clock.tick);
				for (unsigned i = 0; i < 16; ++i) { cpu.Advance(); ++clock.tick; }
				const auto expected = cpu.Signature(clock.tick);
				const auto expectedMemory = memory.bytes;
				memory.bytes = savedMemory;
				clock.tick = savedTick;
				cpu.LoadState(saved);
				if (cpu.Signature(clock.tick) != original) {
					fprintf(stderr, "CPU snapshot immediate mismatch mode=%u speed=%u cycle=%u\n", mode, speed, position);
					const auto actual = cpu.Signature(clock.tick);
					for (unsigned field = 0; field < actual.size(); ++field)
						if (actual[field] != original[field])
							fprintf(stderr, "field %u: expected %08X actual %08X\n", field, original[field], actual[field]);
					return false;
				}
				for (unsigned i = 0; i < 16; ++i) { cpu.Advance(); ++clock.tick; }
				if (cpu.Signature(clock.tick) != expected || memory.bytes != expectedMemory) {
					fprintf(stderr, "CPU snapshot future mismatch mode=%u speed=%u cycle=%u\n", mode, speed, position);
					const auto actual = cpu.Signature(clock.tick);
					for (unsigned field = 0; field < actual.size(); ++field)
						if (actual[field] != expected[field])
							fprintf(stderr, "field %u: expected %08X actual %08X\n", field, expected[field], actual[field]);
					return false;
				}
				// Move the baseline by one cycle, so every instruction
				// phase is exercised rather than sampling every 16th.
				memory.bytes = savedMemory;
				clock.tick = savedTick;
				cpu.LoadState(saved);
				cpu.Advance();
				++clock.tick;
			}
		}
	}
	puts("CPU snapshot cycle replay PASS");
	return true;
}
} // namespace

int main(int argc, char **argv) {
	if (!SnapshotRoundtrips()) return 1;
	if (argc != 2) return 2;
	ATCPUTestOptions opts;
	opts.mPath = argv[1];
	opts.mCheckCycles = true;
	opts.mVerbose = true;
	return ATRunCPUTests(opts);
}
