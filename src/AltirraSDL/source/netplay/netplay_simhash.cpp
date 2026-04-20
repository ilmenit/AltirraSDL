// Altirra SDL3 netplay - per-frame simulator-state hash (impl)

#include <stdafx.h>

#include "netplay_simhash.h"

#include "simulator.h"
#include "cpu.h"
#include "savestateio.h"
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
	// Hash CPU regs + full 64 KB of raw memory.  Intentionally narrow:
	// if the sim diverges in device state without it leaking back into
	// RAM or the CPU within a few frames, we'll still miss it — but
	// the cheap hash is what we pay every frame, so we trade recall
	// against cost.  ComputeSimStateHashBreakdown is the fat catch-all
	// and runs only on desync.
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

	// NOTE (2026-04-20): The GTIA / ANTIC / POKEY `GetRegisterState`
	// accessors return a debugger-facing **mirror** (e.g.
	// POKEY::mState.mReg) which is only updated on CPU-side register
	// WRITES (ATPokeyEmulator::WriteByte, pokey.cpp:2562), NOT by
	// LoadState.  After a savestate load, the mirror still carries
	// whatever the LOADING process had written since boot — which on
	// two peers that started independently means two different mirror
	// contents even though actual emulation state (mAUDF, mAUDC,
	// mAUDCTL, polynomial counters, ANTIC DMA schedule, etc.) is
	// byte-identical.
	//
	// If we hash the mirror, the breakdown shouts "POKEY diverged!"
	// and we waste a lockstep session chasing a ghost.  We leave the
	// fields in the breakdown struct at 0 so the log format stays
	// stable, and note the truth here: the authoritative signals for
	// determinism are cpuRegs, the four RAM banks, and the scheduler
	// tick delta.  A future upgrade can hash the genuine emu-internal
	// device fields directly; for today's diagnostic that's overkill.
	out.gtiaRegs = 0;
	out.anticRegs = 0;
	out.pokeyRegs = 0;

	// Scheduler tick — progresses in fixed cycle counts per frame in a
	// deterministic sim, so a mismatch here means one peer ran more /
	// fewer cycles than the other between two "identical" frame
	// boundaries (a very loud red flag).
	if (auto *sch = sim.GetScheduler()) {
		out.schedTick = (uint32_t)(sch->GetTick64() & 0xFFFFFFFFu);
	}

	out.total = ComputeSimStateHash(sim);
}

// -------------------------------------------------------------------
// Diagnostic: cross-peer post-Load byte diff (2026-04-20)
// -------------------------------------------------------------------

bool SerializeSimToSnapshotBytes(ATSimulator& sim, vdfastvector<uint8_t>& out) {
	out.clear();
	try {
		vdrefptr<IATSerializable> snapshot;
		vdrefptr<IATSerializable> snapshotInfo;
		sim.CreateSnapshot(~snapshot, ~snapshotInfo);

		VDMemoryBufferStream mem;
		VDBufferedWriteStream bs(&mem, 4096);
		vdautoptr<IVDZipArchiveWriter> zip(VDCreateZipArchiveWriter(bs));
		{
			vdautoptr<IATSaveStateSerializer> ser(
				ATCreateSaveStateSerializer(L"savestate.json"));
			ser->Serialize(*zip, *snapshot);
		}
		{
			vdautoptr<IATSaveStateSerializer> ser(
				ATCreateSaveStateSerializer(L"savestateinfo.json"));
			ser->Serialize(*zip, *snapshotInfo);
		}
		zip->Finalize();
		bs.Flush();

		const auto& buf = mem.GetBuffer();
		out.assign(buf.data(), buf.data() + buf.size());
		return true;
	} catch (...) {
		return false;
	}
}

uint32_t HashSavestateJsonInSnapshot(const uint8_t *zipBytes, size_t zipLen) {
	if (!zipBytes || zipLen == 0) return 0;
	try {
		VDMemoryStream ms(zipBytes, (uint32_t)zipLen);
		VDZipArchive arch;
		arch.Init(&ms);
		const sint32 idx = arch.FindFile("savestate.json");
		if (idx < 0) return 0;

		vdfastvector<uint8_t> content;
		if (!arch.ReadRawStream(idx, content, /*allowLarge*/ true)) {
			// The entry was deflate-compressed; decompress into `content`.
			arch.DecompressStream(idx, content);
		}

		uint32_t h = kFnv1a32Offset;
		FnvFoldBuf(h, content.data(), content.size());
		if (h == 0) h = 1;
		return h;
	} catch (...) {
		return 0;
	}
}

} // namespace ATNetplay
