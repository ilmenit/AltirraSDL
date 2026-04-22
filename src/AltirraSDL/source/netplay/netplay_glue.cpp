// Altirra SDL3 netplay - main-loop glue (impl)

#include <stdafx.h>

#include "netplay_glue.h"

#include "coordinator.h"
#include "packets.h"
#include "netplay_input.h"
#include "netplay_simhash.h"

#include "simulator.h"
#include "cpu.h"
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

// After the joiner coordinator self-tears-down (two-tick terminal
// grace), the UI still needs to show WHY the session ended — otherwise
// a rejected joiner's Waiting screen drops back to the generic default
// label and the reject message is lost.  Cache the last terminal phase
// and last error string at teardown; cleared on the next BeginJoin.
Phase                                    g_lastJoinPhase  = Phase::None;
std::string                              g_lastJoinError;

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
	// Joiner.  Before the two-tick teardown nukes the coordinator,
	// snapshot its terminal phase + last error so the Waiting screen
	// can keep surfacing "you were rejected" after the reset.
	if (g_joiner && IsTerminal(g_joiner->GetPhase())) {
		g_lastJoinPhase = ToGluePhase(g_joiner->GetPhase());
		const char *err = g_joiner->LastError();
		g_lastJoinError = err ? err : "";
	}
	PollCoord(g_joiner, g_joinerTerminalTicks, nowMs);

	// Edge-drive the input injector's lifecycle off the aggregate
	// lockstep state.  BeginSession allocates controller ports and
	// puts the input manager in restricted mode; EndSession reverses.
	//
	// On the host, ALSO guarantee the simulator is Resume()d at this
	// edge.  The host's sim was paused by kATDeferred_NetplayHostBoot
	// so snapshot capture got a frozen frame; there's a separate
	// Resume path in ReconcileHostedGames at the Lockstepping edge,
	// but field evidence (2026-04-20) is that it races against the
	// lockstep-entry notification — the sim can still be paused when
	// the first frames need to apply, so Advance() returns Stopped,
	// no frames are produced, no local hash is computed, and the
	// joiner (whose sim IS running) sees its frame-0 hash diverge
	// against nothing and flags a false-positive desync at frame 0.
	// Resuming here — the canonical "lockstep is live" transition —
	// is belt-and-braces and cheap (Resume is idempotent).
	const bool lock = IsLockstepping();
	if (lock && !ATNetplayInput::IsActive()) {
		if (g_sim.IsPaused()) {
			g_ATLCNetplay("lockstep entry: sim was paused, resuming");
			g_sim.Resume();
		}

		// Normalize RNG state across peers.  The PIA floating-input
		// RNG seed (and the per-subsystem seeds derived from
		// mRandomSeed) are NOT round-tripped through the savestate,
		// so each peer otherwise carries the rand()-derived seed its
		// process happened to pick at startup (main.cpp:3893).  That
		// invisible divergence is the source of the drift we saw in
		// the 2026-04-20 post-Load byte-diff investigation: memory
		// and CPU regs were bit-identical after Load, yet the PIA
		// floating-input LFSR produced different bits for
		// non-driven port bits on the two machines, which leaks into
		// PIA reads and from there into game state within a few
		// frames.  Using a constant here (not a per-session value)
		// is deliberate: both peers reach this line once lockstep
		// engages, and we want the same seed on both.
		constexpr uint32_t kNetplayMasterSeed = 0xA7C0BEEFu;
		g_sim.ReseedNetplayRandomState(kNetplayMasterSeed);
		g_ATLCNetplay("lockstep entry: reseeded RNG (master=0x%08X)",
			kNetplayMasterSeed);

		// Normalize CPU registers across peers.  ATCPUEmulator::ColdReset
		// (cpu.cpp:217-253 → WarmReset) only resets PC/InsnPC/P/pipeline
		// state — A/X/Y/S are deliberately left at whatever the previous
		// program had in flight, mirroring real Atari power-on (random
		// register state).  For netplay that's a desync source: the host
		// peer's registers carry forward whatever the user was running
		// locally before the joiner connected, while the joiner peer's
		// registers carry forward whatever they had before joining.
		// netplay_simhash.cpp:50-61 hashes A/X/Y/S directly so the very
		// first frame's hash diverges and Lockstep flags a frame-0 desync
		// (observed 2026-04-21: host cpu=80329d0a vs joiner cpu=c391ee09
		// with byte-identical RAM).  Forcing a constant on both peers is
		// the same trick we use for the PIA RNG above.
		ATCPUEmulator &cpu = g_sim.GetCPU();
		cpu.SetA(0);
		cpu.SetX(0);
		cpu.SetY(0);
		cpu.SetS(0xFF);
		g_ATLCNetplay("lockstep entry: normalized CPU regs "
			"(A=X=Y=0, S=FF)");

		ATNetplayInput::BeginSession();
		g_ATLCNetplay("input: BeginSession (sim running=%d paused=%d)",
			g_sim.IsRunning() ? 1 : 0, g_sim.IsPaused() ? 1 : 0);
	} else if (!lock && ATNetplayInput::IsActive()) {
		ATNetplayInput::EndSession();
		g_ATLCNetplay("input: EndSession");
	}

	// (Lockstep heartbeat log removed — too noisy for normal runs.)
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

namespace {

// Dump a per-subsystem state-hash breakdown on first desync detection.
// Both peers log their own breakdown; comparing the logs pinpoints the
// first-diverging subsystem (CPU, RAM bank, GTIA/ANTIC/POKEY registers,
// or scheduler tick count).  This is the payoff for running the cheap
// per-frame sim-state hash in the first place.
void LogDesyncBreakdownOnce(ATNetplay::Coordinator *c) {
	if (!c) return;
	static void *s_lastCoord = nullptr;
	static int64_t s_lastFrame = -1;
	if (!c->Loop().IsDesynced()) {
		if ((void*)c == s_lastCoord) { s_lastCoord = nullptr; s_lastFrame = -1; }
		return;
	}
	const int64_t f = c->Loop().DesyncFrame();
	if ((void*)c == s_lastCoord && s_lastFrame == f) return;
	s_lastCoord = (void*)c;
	s_lastFrame = f;

	ATNetplay::SimHashBreakdown br{};
	ATNetplay::ComputeSimStateHashBreakdown(g_sim, br);
	g_ATLCNetplay("desync breakdown @frame %lld: "
		"total=%08x cpu=%08x "
		"ram0=%08x ram1=%08x ram2=%08x ram3=%08x "
		"gtia=%08x antic=%08x pokey=%08x schedTick=%08x",
		(long long)f,
		br.total, br.cpuRegs,
		br.ramBank0, br.ramBank1, br.ramBank2, br.ramBank3,
		br.gtiaRegs, br.anticRegs, br.pokeyRegs, br.schedTick);
	g_ATLCNetplay("  (peer should log a matching line; compare "
		"subsystem-by-subsystem to localize the first divergence)");
}

} // anonymous

void OnFrameAdvanced() {
	ATNetplay::Coordinator *c = nullptr;
	for (auto& s : g_hosts) {
		if (s.coord && s.coord->GetPhase() == CoordPhase::Lockstepping) {
			c = s.coord.get(); break;
		}
	}
	if (!c && g_joiner &&
	    g_joiner->GetPhase() == CoordPhase::Lockstepping) {
		c = g_joiner.get();
	}
	if (!c) return;

	// Hash the sim state we just committed.  Must run BEFORE any
	// further sim mutation and AFTER a full emu frame boundary —
	// main_sdl3.cpp gates this call on hadFrame exactly for that.
	const uint32_t h = ATNetplay::ComputeSimStateHash(g_sim);

	// Snapshot-round-trip diagnostic: log a full breakdown on BOTH
	// peers for the first lockstep frame of every session.  Without
	// this, the side that detects desync logs its breakdown (via
	// LogDesyncBreakdownOnce below) and sends Bye before the other
	// side computes its own breakdown, so we never see both halves
	// of the comparison.  Printing unconditionally on frame 0 means
	// both peers always log — a side-by-side diff of the breakdowns
	// localizes the first diverging subsystem immediately.
	const uint32_t curFrame = c->Loop().CurrentFrame();
	if (curFrame == 0) {
		ATNetplay::SimHashBreakdown br{};
		ATNetplay::ComputeSimStateHashBreakdown(g_sim, br);
		g_ATLCNetplay("frame0 breakdown (role=%s): "
			"total=%08x cpu=%08x "
			"ram0=%08x ram1=%08x ram2=%08x ram3=%08x "
			"gtia=%08x antic=%08x pokey=%08x schedTick=%08x",
			c == g_joiner.get() ? "joiner" : "host",
			br.total, br.cpuRegs,
			br.ramBank0, br.ramBank1, br.ramBank2, br.ramBank3,
			br.gtiaRegs, br.anticRegs, br.pokeyRegs, br.schedTick);
	}

	c->OnFrameAdvanced(h);
	LogDesyncBreakdownOnce(c);
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
               const uint8_t* entryCodeHash,
               const ATNetplay::NetBootConfig& bootConfig) {
	if (!gameId || !*gameId) return false;

	// If the id already has a coordinator, tear the old one down first.
	StopHost(gameId);

	HostSlot slot;
	slot.gameId = gameId;
	slot.coord   = std::make_unique<ATNetplay::Coordinator>();
	bool ok = slot.coord->BeginHost(localPort, playerHandle, cartName,
		osRomHash, basicRomHash, settingsHash, inputDelayFrames,
		entryCodeHash, bootConfig);
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

void HostSetPromptAccept(const char* gameId, bool enable) {
	HostSlot* h = FindHost(gameId);
	if (!h || !h->coord) return;
	h->coord->SetPromptAccept(enable);
}

size_t HostPendingCount(const char* gameId) {
	HostSlot* h = FindHost(gameId);
	if (!h || !h->coord) return 0;
	return h->coord->PendingDecisionCount();
}

bool HostHasPendingDecision(const char* gameId) {
	return HostPendingCount(gameId) != 0;
}

bool HostPendingAt(const char* gameId, size_t i,
                   char* outHandle, size_t outHandleSize,
                   uint64_t* outArrivedMs) {
	if (outHandle && outHandleSize) outHandle[0] = 0;
	if (outArrivedMs) *outArrivedMs = 0;
	HostSlot* h = FindHost(gameId);
	if (!h || !h->coord) return false;
	if (i >= h->coord->PendingDecisionCount()) return false;
	const char* src = h->coord->PendingJoinerHandle(i);
	if (outHandle && outHandleSize) {
		size_t n = 0;
		while (src[n] && n + 1 < outHandleSize) { outHandle[n] = src[n]; ++n; }
		outHandle[n] = 0;
	}
	if (outArrivedMs) *outArrivedMs = h->coord->PendingArrivedMs(i);
	return true;
}

bool HostPendingJoinerHandle(const char* gameId,
                             char* outBuf, size_t outBufSize) {
	return HostPendingAt(gameId, 0, outBuf, outBufSize, nullptr);
}

void HostAcceptPending(const char* gameId, size_t i) {
	HostSlot* h = FindHost(gameId);
	if (!h || !h->coord) return;
	h->coord->AcceptPendingJoiner(i);
}

void HostRejectPending(const char* gameId, size_t i) {
	HostSlot* h = FindHost(gameId);
	if (!h || !h->coord) return;
	h->coord->RejectPendingJoiner(i);
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
	// Fresh attempt: clear any cached "last failed" state from a
	// prior join so the Waiting screen doesn't flash a stale error.
	g_lastJoinPhase = Phase::None;
	g_lastJoinError.clear();
	g_joinerTerminalTicks = 0;
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
	// Explicit user dismiss: wipe both the live coordinator AND the
	// cached failure state so the Waiting screen closes cleanly.
	if (g_joiner) {
		g_joiner->End();
		g_joiner.reset();
	}
	g_joinerTerminalTicks = 0;
	g_lastJoinPhase = Phase::None;
	g_lastJoinError.clear();
}

bool JoinExists()         { return g_joiner != nullptr; }
Phase JoinPhase() {
	if (g_joiner) return ToGluePhase(g_joiner->GetPhase());
	return g_lastJoinPhase;
}
const char* JoinLastError() {
	if (g_joiner) {
		const char *err = g_joiner->LastError();
		if (err && *err) return err;
	}
	return g_lastJoinError.c_str();
}

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

ATNetplay::NetBootConfig JoinBootConfig() {
	if (!g_joiner) return ATNetplay::NetBootConfig{};
	return g_joiner->GetBootConfig();
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
	// Treat this as an explicit user dismiss on the joiner side — we
	// don't want the cached rejection to reappear on the Waiting screen
	// after the user chose to leave.
	g_lastJoinPhase = Phase::None;
	g_lastJoinError.clear();
}

} // namespace ATNetplayGlue
