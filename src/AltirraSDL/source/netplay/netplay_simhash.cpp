// Altirra SDL3 netplay - per-frame simulator-state hash (impl)

#include <stdafx.h>

#include "netplay_simhash.h"
#include "netplay_disk.h"

#include "simulator.h"
#include "cpu.h"
#include "antic.h"
#include "disk.h"
#include "savestateio.h"
#include <at/ataudio/pokey.h>
#include <at/atcore/scheduler.h>
#include <at/atcore/serializable.h>
#include <vd2/system/file.h>
#include <vd2/system/zip.h>

#include <cstdint>
#include <cstring>

namespace ATNetplay {

namespace {

// FNV-1a 32.  Chosen over CRC32 because we already use the FNV family
// in lockstep.cpp for the rolling input hash and don't want to pull in
// a table-driven CRC for an off-hot-path diagnostic.
constexpr uint32_t kFnv1a32Offset = 0x811c9dc5u;
constexpr uint32_t kFnv1a32Prime  = 0x01000193u;

inline void FnvFold(uint32_t& h, uint8_t b) {
	h ^= (uint32_t)b;
	h *= kFnv1a32Prime;
}

inline void FnvFoldU16(uint32_t& h, uint16_t v) {
	FnvFold(h, (uint8_t)(v & 0xFF));
	FnvFold(h, (uint8_t)((v >> 8) & 0xFF));
}

inline void FnvFoldU32(uint32_t& h, uint32_t v) {
	for (int i = 0; i < 4; ++i) FnvFold(h, (uint8_t)((v >> (8*i)) & 0xFF));
}

inline void FnvFoldU64(uint32_t& h, uint64_t v) {
	for (int i = 0; i < 8; ++i) FnvFold(h, (uint8_t)((v >> (8*i)) & 0xFF));
}

inline void FnvFoldBuf(uint32_t& h, const uint8_t *p, size_t n) {
	for (size_t i = 0; i < n; ++i) FnvFold(h, p[i]);
}

uint32_t HashCpu(ATSimulator& sim) {
	const auto& cpu = sim.GetCPU();
	uint32_t h = kFnv1a32Offset;
	FnvFoldU16(h, cpu.GetPC());
	FnvFoldU16(h, cpu.GetInsnPC());
	FnvFold(h,   cpu.GetA());
	FnvFold(h,   cpu.GetX());
	FnvFold(h,   cpu.GetY());
	FnvFold(h,   cpu.GetS());
	FnvFold(h,   cpu.GetP());
	return h;
}

uint32_t HashRamBank(const uint8_t *mem, size_t base, size_t len) {
	uint32_t h = kFnv1a32Offset;
	FnvFoldBuf(h, mem + base, len);
	return h;
}

} // anonymous

uint32_t ComputeSimStateHash(ATSimulator& sim) {
	// Hash CPU regs + full 64 KB of raw memory + scheduler tick + per-
	// subsystem device fingerprints (POKEY / ANTIC / disk drives).
	//
	// Including device-internal state lets the lockstep desync detector
	// catch divergences on the same frame they occur, instead of having
	// to wait for them to leak into RAM via a hardware register read
	// (which can take 20-30 frames during boot — exactly the World
	// Karate Championship frame-29 desync signature).  The extra cost
	// is sub-microsecond per frame compared to the ~50 µs the 64 KB
	// RAM hash already pays.
	uint32_t h = kFnv1a32Offset;

	// CPU regs
	const auto& cpu = sim.GetCPU();
	FnvFoldU16(h, cpu.GetPC());
	FnvFoldU16(h, cpu.GetInsnPC());
	FnvFold(h,   cpu.GetA());
	FnvFold(h,   cpu.GetX());
	FnvFold(h,   cpu.GetY());
	FnvFold(h,   cpu.GetS());
	FnvFold(h,   cpu.GetP());

	// 64 KB RAM (direct pointer; no page-table indirection).
	const uint8_t *mem = sim.GetRawMemory();
	if (mem) {
		FnvFoldBuf(h, mem, 0x10000);
	}

	// Scheduler tick (low 32 bits of GetTick64()).  After the lockstep-
	// entry rebase + ColdReset both peers' absolute schedulers are
	// aligned, so this advances identically frame-by-frame.  Folding
	// it into the hash catches any future regression where the two
	// peers' scheduler clocks drift apart by even one cycle per frame.
	if (auto *sch = sim.GetScheduler())
		FnvFoldU32(h, (uint32_t)(sch->GetTick64() & 0xFFFFFFFFu));

	// POKEY internal state — poly counters, 15/64 kHz clock phase,
	// timer counters, AUDF/AUDC/CTL, IRQ regs, SKCTL/SKSTAT, serial
	// shift state, pot scan progress.  This is the most likely source
	// of "looks identical but boots differently" divergence in
	// .atr-driven boots, since OS SIO traffic exercises POKEY serial
	// timing heavily before the boot loader has copied data into RAM.
	FnvFoldU32(h, sim.GetPokey().GetNetplayDeterminismFingerprint());

	// ANTIC internal state — beam position, frame anchor delta, NMIST,
	// DMACTL.  Catches any beam-position / DMA-pattern drift before
	// it manifests as a video glitch.
	FnvFoldU32(h, sim.GetAntic().GetNetplayDeterminismFingerprint());

	// Per-drive disk emulator state (rotational counter, active SIO
	// command).  Only enabled drives contribute non-zero values, so
	// a non-disk session pays only the function-call cost per drive.
	for (int i = 0; i < 15; ++i) {
		FnvFoldU32(h, sim.GetDiskDrive(i).GetNetplayDeterminismFingerprint());
		FnvFoldU64(h, HashRollbackDisk(sim.GetDiskInterface(i).GetDiskImage()));
	}

	// FNV-1a never produces the 0 value from non-empty input in a way
	// that matters here, but the on-wire protocol treats hashLow32==0
	// as "no hash yet".  Force a non-zero encoding to avoid that edge.
	if (h == 0) h = 1;
	return h;
}

void ComputeSimStateHashBreakdown(ATSimulator& sim, SimHashBreakdown& out) {
	out = SimHashBreakdown{};

	out.cpuRegs = HashCpu(sim);

	const uint8_t *mem = sim.GetRawMemory();
	if (mem) {
		out.ramBank0 = HashRamBank(mem, 0x0000, 0x4000);
		out.ramBank1 = HashRamBank(mem, 0x4000, 0x4000);
		out.ramBank2 = HashRamBank(mem, 0x8000, 0x4000);
		out.ramBank3 = HashRamBank(mem, 0xC000, 0x4000);
	}

	// Device fingerprints.  Earlier versions hashed the debugger
	// register MIRROR (ATPokeyRegisterState etc.), which is updated
	// only on CPU-side register writes — that produced false positives
	// after savestate apply because the mirror carried per-process
	// pre-session writes even when emulation state was bit-identical.
	// We now route these through new GetNetplayDeterminismFingerprint()
	// methods that hash the genuine emu-internal fields (poly counters,
	// timer state, beam position, etc.) and fold tick-domain fields as
	// deltas, so the values are stable across absolute-tick rebases
	// and meaningful for divergence diagnosis.
	//
	// gtiaRegs is currently unused on the wire — leave at zero until
	// a GTIA fingerprint method exists.  anticRegs and pokeyRegs carry
	// the new device state.
	out.gtiaRegs  = 0;
	out.anticRegs = sim.GetAntic().GetNetplayDeterminismFingerprint();
	out.pokeyRegs = sim.GetPokey().GetNetplayDeterminismFingerprint();

	// Scheduler tick — progresses in fixed cycle counts per frame in a
	// deterministic sim, so a mismatch here means one peer ran more /
	// fewer cycles than the other between two "identical" frame
	// boundaries (a very loud red flag).
	if (auto *sch = sim.GetScheduler()) {
		out.schedTick = (uint32_t)(sch->GetTick64() & 0xFFFFFFFFu);
	}

	out.total = ComputeSimStateHash(sim);
}

} // namespace ATNetplay
