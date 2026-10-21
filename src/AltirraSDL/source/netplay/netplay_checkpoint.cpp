// AltirraSDL - exact local checkpoint storage.
#include <stdafx.h>
#include "netplay_checkpoint.h"
#include "netplay_input.h"
#include "netplay_disk.h"
#include "disk.h"
#include <array>
#include "simulator.h"
#include "gtia.h"
#include <at/atcore/serializable.h>
#include <at/atcore/logging.h>
#include <vd2/system/error.h>
#include <at/atcore/scheduler.h>
#include <vd2/system/refcount.h>

extern ATSimulator g_sim;
extern ATLogChannel g_ATLCNetplay;

namespace ATNetplay {

struct Checkpoint::Impl {
	vdrefptr<IATSerializable> snapshot;
	uint64_t cycle = 0;
	ATNetplayInput::AppliedState inputs;
	std::array<DiskCheckpoint, 15> disks;
};

Checkpoint::Checkpoint() : mpImpl(new Impl) {}
Checkpoint::~Checkpoint() = default;

bool Checkpoint::Capture() {
	try {
		vdrefptr<IATSerializable> snapshot;
		g_sim.CreateSnapshot(~snapshot, nullptr);
		if (!snapshot) return false;
		mpImpl->inputs = ATNetplayInput::CaptureAppliedState();
		for (unsigned i = 0; i < mpImpl->disks.size(); ++i)
			mpImpl->disks[i] = CaptureDiskCheckpoint(g_sim.GetDiskInterface(i).GetDiskImage());
		mpImpl->snapshot = std::move(snapshot);
		mpImpl->cycle = g_sim.GetScheduler()->GetTick64();
		return true;
	} catch (const MyError& error) {
		g_ATLCNetplay("checkpoint: capture failed: %s", error.c_str());
		return false;
	} catch (...) {
		g_ATLCNetplay("checkpoint: capture failed (unexpected exception)");
		return false;
	}
}

bool Checkpoint::Restore() const {
	if (!mpImpl->snapshot) return false;
	const bool paused = g_sim.IsPaused();
	try {
		g_sim.GetGTIA().DiscardInFlightFrame();
		for (const auto& disk : mpImpl->disks)
			if (!RestoreDiskCheckpoint(disk)) return false;
		ATNetplayInput::RestoreAppliedState(mpImpl->inputs);
		if (!g_sim.ApplySnapshot(*mpImpl->snapshot, nullptr)) return false;
		if (paused) g_sim.Pause(); else g_sim.Resume();
		return true;
	} catch (...) {
		if (paused) g_sim.Pause(); else g_sim.Resume();
		return false;
	}
}

uint64_t Checkpoint::Cycle() const { return mpImpl->cycle; }

bool CaptureCheckpoint(std::unique_ptr<Checkpoint>& out) {
	auto checkpoint = std::make_unique<Checkpoint>();
	if (!checkpoint->Capture()) return false;
	out = std::move(checkpoint);
	return true;
}

bool RestoreCheckpoint(const Checkpoint& checkpoint) {
	return checkpoint.Restore();
}

} // namespace ATNetplay
