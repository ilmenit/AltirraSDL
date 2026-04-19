// Altirra SDL3 netplay - main-loop glue (impl)

#include <stdafx.h>

#include "netplay_glue.h"

#include "coordinator.h"

#include <memory>

namespace ATNetplayGlue {

namespace {

std::unique_ptr<ATNetplay::Coordinator> g_coord;

bool IsTerminalPhase(ATNetplay::Coordinator::Phase p) {
	using P = ATNetplay::Coordinator::Phase;
	return p == P::Ended || p == P::Failed || p == P::Desynced || p == P::Idle;
}

void EnsureCoordinator() {
	if (!g_coord) g_coord = std::make_unique<ATNetplay::Coordinator>();
}

} // anonymous

bool IsActive() {
	return g_coord && !IsTerminalPhase(g_coord->GetPhase());
}

bool IsLockstepping() {
	return g_coord &&
	       g_coord->GetPhase() == ATNetplay::Coordinator::Phase::Lockstepping;
}

void Poll(uint64_t nowMs) {
	if (!g_coord) return;
	g_coord->Poll(nowMs);

	// Opportunistic teardown: once the session has ended (cleanly or
	// not), free the coordinator so the singleton slot is ready for
	// a fresh StartHost/StartJoin.  We don't tear down on the same
	// tick that flagged the transition — give the caller at least
	// one IsActive() observation to surface the ended state to the
	// UI / HUD.  A simple "was-terminal, now-terminal" edge detector
	// does the job.
	static int s_terminalTicks = 0;
	if (IsTerminalPhase(g_coord->GetPhase())) {
		if (++s_terminalTicks >= 2) {
			g_coord.reset();
			s_terminalTicks = 0;
		}
	} else {
		s_terminalTicks = 0;
	}
}

bool CanAdvanceThisTick() {
	if (!g_coord) return true;
	if (g_coord->GetPhase() != ATNetplay::Coordinator::Phase::Lockstepping)
		return true;  // handshake/snapshot: sim continues normally
	return g_coord->CanAdvance();
}

void OnFrameAdvanced() {
	if (!g_coord) return;
	if (g_coord->GetPhase() != ATNetplay::Coordinator::Phase::Lockstepping)
		return;
	g_coord->OnFrameAdvanced();
}

void Shutdown() {
	if (!g_coord) return;
	g_coord->End();
	g_coord.reset();
}

// ---- UI entry points ------------------------------------------------------

bool StartHost(uint16_t localPort,
               const char* playerHandle,
               const char* cartName,
               uint64_t osRomHash,
               uint64_t basicRomHash,
               uint64_t settingsHash,
               uint16_t inputDelayFrames,
               const uint8_t* entryCodeHash) {
	EnsureCoordinator();
	return g_coord->BeginHost(localPort, playerHandle, cartName,
		osRomHash, basicRomHash, settingsHash, inputDelayFrames,
		entryCodeHash);
}

bool StartJoin(const char* hostAddress,
               const char* playerHandle,
               uint64_t osRomHash,
               uint64_t basicRomHash,
               bool acceptTos,
               const uint8_t* entryCodeHash) {
	EnsureCoordinator();
	return g_coord->BeginJoin(hostAddress, playerHandle, osRomHash,
		basicRomHash, acceptTos, entryCodeHash);
}

void SubmitSnapshotBytes(const uint8_t* data, size_t len) {
	if (!g_coord) return;
	g_coord->SubmitSnapshotForUpload(data, len);
}

const char* LastError() {
	return g_coord ? g_coord->LastError() : "";
}

uint16_t BoundPort() {
	return g_coord ? g_coord->BoundPort() : 0;
}

uint32_t CurrentFrame() {
	if (!g_coord) return 0;
	if (g_coord->GetPhase() != ATNetplay::Coordinator::Phase::Lockstepping)
		return 0;
	return g_coord->Loop().CurrentFrame();
}

} // namespace ATNetplayGlue
