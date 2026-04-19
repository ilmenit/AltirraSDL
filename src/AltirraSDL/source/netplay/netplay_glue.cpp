// Altirra SDL3 netplay - main-loop glue (impl)

#include <stdafx.h>

#include "netplay_glue.h"

#include "coordinator.h"
#include "netplay_input.h"

#include "simulator.h"
#include <at/atcore/logging.h>

extern ATLogChannel g_ATLCNetplay;
extern ATSimulator g_sim;

#include <algorithm>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace ATNetplayGlue {

namespace {

using CoordPhase = ATNetplay::Coordinator::Phase;

Phase ToGluePhase(CoordPhase p) {
	using P = CoordPhase;
	switch (p) {
		case P::Idle:              return Phase::Idle;
		case P::WaitingForJoiner:  return Phase::WaitingForJoiner;
		case P::Handshaking:       return Phase::Handshaking;
		case P::SendingSnapshot:   return Phase::SendingSnapshot;
		case P::ReceivingSnapshot: return Phase::ReceivingSnapshot;
		case P::SnapshotReady:     return Phase::SnapshotReady;
		case P::Lockstepping:      return Phase::Lockstepping;
		case P::Ended:             return Phase::Ended;
		case P::Desynced:          return Phase::Desynced;
		case P::Failed:            return Phase::Failed;
	}
	return Phase::None;
}

bool IsTerminal(CoordPhase p) {
	return p == CoordPhase::Ended || p == CoordPhase::Failed
	    || p == CoordPhase::Desynced || p == CoordPhase::Idle;
}

// One host offer's runtime state: coordinator + the id that keyed it.
struct HostSlot {
	std::string                              gameId;
	std::unique_ptr<ATNetplay::Coordinator>  coord;
	// ms tick counter used for opportunistic teardown (same pattern as
	// the legacy single-coordinator glue had).
	int terminalTicks = 0;
};

std::vector<HostSlot>                    g_hosts;
std::unique_ptr<ATNetplay::Coordinator>  g_joiner;
int                                      g_joinerTerminalTicks = 0;

// Find by id.  Returns nullptr if not present.
HostSlot* FindHost(const char* gameId) {
	if (!gameId || !*gameId) return nullptr;
	for (auto& s : g_hosts) {
		if (s.gameId == gameId) return &s;
	}
	return nullptr;
}

// Poll one coordinator and apply the two-tick terminal-phase teardown.
// Returns true if the slot should be removed from its container.
bool PollCoord(std::unique_ptr<ATNetplay::Coordinator>& coord,
               int& terminalTicks,
               uint64_t nowMs) {
	if (!coord) return false;
	coord->Poll(nowMs);
	if (IsTerminal(coord->GetPhase())) {
		if (++terminalTicks >= 2) {
			coord.reset();
			terminalTicks = 0;
			return true;
		}
	} else {
		terminalTicks = 0;
	}
	return false;
}

} // anonymous

// -------------------------------------------------------------------
// Aggregate queries
// -------------------------------------------------------------------

bool IsActive() {
	if (g_joiner && !IsTerminal(g_joiner->GetPhase())) return true;
	for (auto& s : g_hosts) {
		if (s.coord && !IsTerminal(s.coord->GetPhase())) return true;
	}
	return false;
}

bool IsLockstepping() {
	if (g_joiner &&
	    g_joiner->GetPhase() == CoordPhase::Lockstepping) return true;
	for (auto& s : g_hosts) {
		if (s.coord && s.coord->GetPhase() == CoordPhase::Lockstepping)
			return true;
	}
	return false;
}

const char* LockstepOfferId() {
	for (auto& s : g_hosts) {
		if (s.coord && s.coord->GetPhase() == CoordPhase::Lockstepping)
			return s.gameId.c_str();
	}
	return "";
}

void Poll(uint64_t nowMs) {
	// Hosts.
	for (auto it = g_hosts.begin(); it != g_hosts.end(); ) {
		if (PollCoord(it->coord, it->terminalTicks, nowMs)) {
			it = g_hosts.erase(it);
		} else {
			++it;
		}
	}
	// Joiner.
	PollCoord(g_joiner, g_joinerTerminalTicks, nowMs);

	// Edge-drive the input injector's lifecycle off the aggregate
	// lockstep state.  BeginSession allocates controller ports and
	// puts the input manager in restricted mode; EndSession reverses.
	const bool lock = IsLockstepping();
	if (lock && !ATNetplayInput::IsActive()) {
		ATNetplayInput::BeginSession();
		g_ATLCNetplay("input: BeginSession (sim running=%d paused=%d)",
			g_sim.IsRunning() ? 1 : 0, g_sim.IsPaused() ? 1 : 0);
	} else if (!lock && ATNetplayInput::IsActive()) {
		ATNetplayInput::EndSession();
		g_ATLCNetplay("input: EndSession");
	}

	// Once-per-second lockstep heartbeat: sim running state, local
	// frame counter and lockstep gate state.  This is deliberately
	// rate-limited so the log stays short enough to paste into a
	// bug report.
	static uint64_t s_lastHeartbeatMs = 0;
	if (lock && nowMs - s_lastHeartbeatMs >= 1000) {
		s_lastHeartbeatMs = nowMs;
		ATNetplay::Coordinator *c = nullptr;
		for (auto& s : g_hosts) {
			if (s.coord && s.coord->GetPhase() == CoordPhase::Lockstepping) {
				c = s.coord.get(); break;
			}
		}
		if (!c && g_joiner
		      && g_joiner->GetPhase() == CoordPhase::Lockstepping)
			c = g_joiner.get();
		if (c) {
			uint64_t last = c->Loop().LastPeerRecvMs();
			g_ATLCNetplay("heartbeat: frame=%u gate=%s "
				"sim running=%d paused=%d peerAgeMs=%llu",
				(unsigned)c->Loop().CurrentFrame(),
				c->CanAdvance() ? "open" : "closed",
				g_sim.IsRunning() ? 1 : 0,
				g_sim.IsPaused()  ? 1 : 0,
				(unsigned long long)(last == 0 ? 0 : nowMs - last));
		}
	}
	if (!lock) s_lastHeartbeatMs = 0;
}

bool CanAdvanceThisTick() {
	// If any coordinator is lockstepping, only it dictates advance;
	// otherwise emulation runs normally.
	for (auto& s : g_hosts) {
		if (s.coord && s.coord->GetPhase() == CoordPhase::Lockstepping) {
			return s.coord->CanAdvance();
		}
	}
	if (g_joiner && g_joiner->GetPhase() == CoordPhase::Lockstepping) {
		return g_joiner->CanAdvance();
	}
	return true;
}

void OnFrameAdvanced() {
	for (auto& s : g_hosts) {
		if (s.coord && s.coord->GetPhase() == CoordPhase::Lockstepping) {
			s.coord->OnFrameAdvanced();
			return;
		}
	}
	if (g_joiner && g_joiner->GetPhase() == CoordPhase::Lockstepping) {
		g_joiner->OnFrameAdvanced();
	}
}

namespace {

// Return the coordinator that's currently in Lockstepping, or nullptr
// if none.  At most one coordinator can be in this phase per the
// activity state machine.
ATNetplay::Coordinator *ActiveLockstep() {
	for (auto& s : g_hosts) {
		if (s.coord && s.coord->GetPhase() == CoordPhase::Lockstepping)
			return s.coord.get();
	}
	if (g_joiner && g_joiner->GetPhase() == CoordPhase::Lockstepping)
		return g_joiner.get();
	return nullptr;
}

} // anonymous

void SubmitLocalInput() {
	ATNetplay::Coordinator *c = ActiveLockstep();
	if (!c) return;
	ATNetplay::NetInput in = ATNetplayInput::PollLocal();
	c->SubmitLocalInput(in);
}

void ApplyFrameInputsToSim() {
	ATNetplay::Coordinator *c = ActiveLockstep();
	if (!c) return;
	ATNetplay::NetInput p1{}, p2{};
	if (c->GetInputsForCurrentFrame(p1, p2)) {
		ATNetplayInput::ApplyFrameInputs(p1, p2);
	}
}

void Shutdown() {
	for (auto& s : g_hosts) {
		if (s.coord) s.coord->End();
	}
	g_hosts.clear();
	if (g_joiner) {
		g_joiner->End();
		g_joiner.reset();
	}
	g_joinerTerminalTicks = 0;
	if (ATNetplayInput::IsActive()) ATNetplayInput::EndSession();
}

// -------------------------------------------------------------------
// Host hostedGames
// -------------------------------------------------------------------

bool StartHost(const char* gameId,
               uint16_t localPort,
               const char* playerHandle,
               const char* cartName,
               uint64_t osRomHash,
               uint64_t basicRomHash,
               uint64_t settingsHash,
               uint16_t inputDelayFrames,
               const uint8_t* entryCodeHash) {
	if (!gameId || !*gameId) return false;

	// If the id already has a coordinator, tear the old one down first.
	StopHost(gameId);

	HostSlot slot;
	slot.gameId = gameId;
	slot.coord   = std::make_unique<ATNetplay::Coordinator>();
	bool ok = slot.coord->BeginHost(localPort, playerHandle, cartName,
		osRomHash, basicRomHash, settingsHash, inputDelayFrames,
		entryCodeHash);
	if (!ok) return false;

	g_hosts.push_back(std::move(slot));
	return true;
}

void StopHost(const char* gameId) {
	HostSlot* h = FindHost(gameId);
	if (!h) return;
	if (h->coord) h->coord->End();
	// Remove by id (pointer may dangle after erase).
	std::string id = gameId;
	g_hosts.erase(std::remove_if(g_hosts.begin(), g_hosts.end(),
		[&](const HostSlot& s) { return s.gameId == id; }),
		g_hosts.end());
}

bool HostExists(const char* gameId) {
	return FindHost(gameId) != nullptr;
}

Phase HostPhase(const char* gameId) {
	HostSlot* h = FindHost(gameId);
	if (!h || !h->coord) return Phase::None;
	return ToGluePhase(h->coord->GetPhase());
}

uint16_t HostBoundPort(const char* gameId) {
	HostSlot* h = FindHost(gameId);
	if (!h || !h->coord) return 0;
	return h->coord->BoundPort();
}

const char* HostLastError(const char* gameId) {
	HostSlot* h = FindHost(gameId);
	if (!h || !h->coord) return "";
	return h->coord->LastError();
}

void SubmitHostSnapshot(const char* gameId, const uint8_t* data, size_t len) {
	HostSlot* h = FindHost(gameId);
	if (!h || !h->coord) return;
	h->coord->SubmitSnapshotForUpload(data, len);
}

// -------------------------------------------------------------------
// Joiner
// -------------------------------------------------------------------

bool StartJoin(const char* hostAddress,
               const char* playerHandle,
               uint64_t osRomHash,
               uint64_t basicRomHash,
               bool acceptTos,
               const uint8_t* entryCodeHash) {
	if (g_joiner) {
		g_joiner->End();
		g_joiner.reset();
	}
	g_joiner = std::make_unique<ATNetplay::Coordinator>();
	bool ok = g_joiner->BeginJoin(hostAddress, playerHandle,
		osRomHash, basicRomHash, acceptTos, entryCodeHash);
	if (!ok) {
		// Keep the failed coordinator around so callers can read
		// JoinLastError().  It's in Phase::Failed now.
	}
	return ok;
}

void StopJoin() {
	if (!g_joiner) return;
	g_joiner->End();
	g_joiner.reset();
	g_joinerTerminalTicks = 0;
}

bool JoinExists()         { return g_joiner != nullptr; }
Phase JoinPhase()         { return g_joiner ? ToGluePhase(g_joiner->GetPhase()) : Phase::None; }
const char* JoinLastError() { return g_joiner ? g_joiner->LastError() : ""; }

void GetReceivedSnapshot(const uint8_t** data, size_t* len) {
	if (!g_joiner) { if (data) *data = nullptr; if (len) *len = 0; return; }
	const auto& buf = g_joiner->GetReceivedSnapshot();
	if (data) *data = buf.data();
	if (len)  *len  = buf.size();
}

void AcknowledgeSnapshotApplied() {
	if (!g_joiner) return;
	g_joiner->AcknowledgeSnapshotApplied();
}

uint32_t CurrentFrame() {
	ATNetplay::Coordinator *c = ActiveLockstep();
	return c ? c->Loop().CurrentFrame() : 0;
}

uint32_t CurrentInputDelay() {
	ATNetplay::Coordinator *c = ActiveLockstep();
	return c ? c->Loop().InputDelay() : 0;
}

bool IsDesynced(int64_t* outFrame) {
	ATNetplay::Coordinator *c = ActiveLockstep();
	if (!c) { if (outFrame) *outFrame = -1; return false; }
	const auto& loop = c->Loop();
	if (outFrame) *outFrame = loop.DesyncFrame();
	return loop.IsDesynced();
}

uint64_t MsSinceLastPeerPacket(uint64_t nowMs) {
	ATNetplay::Coordinator *c = ActiveLockstep();
	if (!c) return UINT64_MAX / 2;
	uint64_t last = c->Loop().LastPeerRecvMs();
	if (last == 0) return UINT64_MAX / 2;
	return nowMs > last ? (nowMs - last) : 0;
}

void DisconnectActive() {
	// End every host coord + joiner coord.  The two-tick terminal
	// teardown in Poll() will finish cleanup.
	for (auto& s : g_hosts) {
		if (s.coord) s.coord->End();
	}
	if (g_joiner) g_joiner->End();
}

} // namespace ATNetplayGlue
