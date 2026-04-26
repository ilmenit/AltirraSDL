// Altirra SDL3 netplay - main-loop glue (impl)

#include <stdafx.h>

#include "netplay_glue.h"

#include "coordinator.h"
#include "packets.h"
#include "protocol.h"
#include "netplay_input.h"
#include "netplay_savestate.h"
#include "netplay_simhash.h"

#include "simulator.h"
#include "cpu.h"
#include <at/atcore/logging.h>

extern ATLogChannel g_ATLCNetplay;
extern ATSimulator g_sim;

// Speed-control accessors.  When a lockstep session goes live we force
// all warp-related state off so a held F1 at session start can't carry
// through into the session.  (The accessors themselves also refuse new
// "enable warp" requests while IsActive()==true — see
// stubs/uiaccessors_stubs.cpp — so once the session is up, no UI path
// can re-engage it.)
void ATUISetTurbo(bool);
void ATUISetTurboPulse(bool);
void ATUISetSlowMotion(bool);
void ATUISetSpeedModifier(float);

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
		case P::Resyncing:         return Phase::Resyncing;
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
// Drive the resync I/O boundary for the given coordinator.  This is
// where the netplay module reaches into g_sim: host side captures its
// running state when the coordinator flags NeedsResyncCapture, joiner
// side applies the received payload when NeedsResyncApply flips.
// Both operations pause the sim first so the capture / apply sees a
// stable frame — the simulator's Pause is idempotent and the resume
// happens at the Lockstepping edge in the main Poll() body below.
// True iff WE (DriveResyncIO) own the current pause state.  Tracked so
// our backstop resume doesn't accidentally clear a user-initiated
// pre-session pause that happens to be lingering when a Bye arrives.
bool g_resyncOwnsPause = false;

void DriveResyncIO(ATNetplay::Coordinator& coord) {
	if (coord.GetPhase() != ATNetplay::Coordinator::Phase::Resyncing)
		return;

	// Unconditionally pause the sim for as long as any coord is in
	// Resyncing.  We exit Lockstepping on the phase transition, which
	// means CanAdvanceThisTick() stops gating advance and the main loop
	// would otherwise keep calling g_sim.Advance() with stale state for
	// every tick until the chunks finish arriving.  On the joiner that
	// means hundreds of ms of garbage frames between BeginJoinerResync
	// and the last chunk.  Pause() is idempotent; Resume() happens on
	// the phase transition back to Lockstepping (see below).
	if (!g_sim.IsPaused()) {
		g_sim.Pause();
		g_resyncOwnsPause = true;
	}

	if (coord.NeedsResyncCapture()) {
		std::vector<uint8_t> buf;
		if (ATNetplay::CaptureSavestate(buf)) {
			coord.SubmitResyncCapture(buf.data(), buf.size());
		} else {
			// Capture failed — submit empty payload so coordinator
			// transitions to Desynced rather than stalling forever.
			coord.SubmitResyncCapture(nullptr, 0);
		}
	}
	if (coord.NeedsResyncApply()) {
		const auto& data = coord.GetReceivedResyncData();
		if (!data.empty() &&
		    ATNetplay::ApplySavestate(data.data(), data.size())) {
			coord.AcknowledgeResyncApplied();
		} else {
			// Apply failed — end the session; a half-applied state is
			// worse than a clean drop.
			coord.End(ATNetplay::kByeDesyncDetected);
		}
	}
}

bool PollCoord(std::unique_ptr<ATNetplay::Coordinator>& coord,
               int& terminalTicks,
               uint64_t nowMs) {
	if (!coord) return false;
	coord->Poll(nowMs);
	DriveResyncIO(*coord);
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

// Session-end cleanup hook.  Set by the UI layer (ui_netplay_actions)
// during init so that DisconnectActive() / Shutdown() can drive the
// pre-session-state restore without a backwards include from glue
// into the UI module.  Invoked at most once per teardown.
namespace {
SessionEndCleanupFn g_sessionEndCleanupHook = nullptr;
}

void SetSessionEndCleanupHook(SessionEndCleanupFn fn) {
	g_sessionEndCleanupHook = fn;
}

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

		// Force-clear any warp/slow-motion/speed-modifier state that
		// survived into the session (e.g. user held F1 as handshake
		// completed).  The accessors allow "off" requests
		// unconditionally so these calls are always effective.
		ATUISetTurbo(false);
		ATUISetTurboPulse(false);
		ATUISetSlowMotion(false);
		ATUISetSpeedModifier(0.0f);
		g_ATLCNetplay("lockstep entry: cleared warp/slow/speed-mod");
	} else if (!lock && ATNetplayInput::IsActive()) {
		ATNetplayInput::EndSession();
		g_ATLCNetplay("input: EndSession");
	}

	// Post-resync resume: the sim was paused inside DriveResyncIO() so
	// CaptureSavestate / ApplySavestate saw a frozen frame.  When the
	// coordinator transitions back to Lockstepping, resume here — the
	// lockstep-entry edge above doesn't fire because
	// ATNetplayInput::IsActive() stayed true across the resync.
	// If someone else resumed the sim out from under us (initial
	// lockstep-entry block above, user action, etc.), drop ownership —
	// the flag should never claim to own a non-paused sim.
	if (!g_sim.IsPaused()) g_resyncOwnsPause = false;

	if (lock && g_sim.IsPaused() && g_resyncOwnsPause) {
		g_ATLCNetplay("post-resync: sim was paused, resuming");
		g_sim.Resume();
		g_resyncOwnsPause = false;
	}

	// Session-end resume backstop: if a resync was in flight and the
	// session then hit a terminal phase (user Disconnect, peer Bye,
	// resync transfer failed, flap limit tripped), nobody would
	// otherwise clear the Pause() we set in DriveResyncIO.  The
	// ownership flag means we never touch a pause we didn't set.
	if (!IsActive() && g_sim.IsPaused() && g_resyncOwnsPause) {
		g_ATLCNetplay("session ended with sim paused (resync interrupted), resuming");
		g_sim.Resume();
		g_resyncOwnsPause = false;
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

// Convert a SimHashBreakdown (internal diagnostic type) into a
// NetSimHashDiag (wire type).  Kept local so the netplay module's
// packet layer doesn't depend on netplay_simhash.h.
ATNetplay::NetSimHashDiag BreakdownToDiag(uint32_t frame,
                                          const ATNetplay::SimHashBreakdown& br) {
	ATNetplay::NetSimHashDiag d;
	d.frame     = frame;
	d.total     = br.total;
	d.cpuRegs   = br.cpuRegs;
	d.ramBank0  = br.ramBank0;
	d.ramBank1  = br.ramBank1;
	d.ramBank2  = br.ramBank2;
	d.ramBank3  = br.ramBank3;
	d.gtiaRegs  = br.gtiaRegs;
	d.anticRegs = br.anticRegs;
	d.pokeyRegs = br.pokeyRegs;
	d.schedTick = br.schedTick;
	return d;
}

// Dump a per-subsystem state-hash breakdown on first desync detection
// AND ship it to the peer.  Each peer logs its own breakdown locally;
// the peer-exchange path in Coordinator then logs a field-by-field
// DIFF line when both sides' breakdowns for the same frame are in
// hand, so a single log now pinpoints the first-diverging subsystem
// without requiring the user to collect and diff two logs.
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

	// Ship to peer.  When their HandleSimHashDiag fires it triggers a
	// "simhash diff @frame N" log with a DIFF/MATCH line per
	// subsystem, on both sides.
	c->SubmitLocalSimHashDiag(BreakdownToDiag((uint32_t)f, br));
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
		// Ship to peer so the coordinator can log a DIFF line naming
		// the first-diverging subsystem if the cold-boot state
		// already disagrees on frame 0.  That case is the
		// "immediately starts in desynchronized state" symptom: two
		// peers start at the same frame with different RAM / scheduler
		// tick / PIA-LFSR state that the current normalization hacks
		// don't cover.
		c->SubmitLocalSimHashDiag(BreakdownToDiag(curFrame, br));
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
	// End coords before invoking the cleanup hook — same ordering as
	// DisconnectActive so ApplySnapshot doesn't race a live coord.
	for (auto& s : g_hosts) {
		if (s.coord) s.coord->End();
	}
	g_hosts.clear();
	if (g_joiner) {
		g_joiner->End();
		g_joiner.reset();
	}
	g_joinerTerminalTicks = 0;

	// Best-effort restore: if a session is alive at app shutdown the
	// user likely just wants to exit, but running the hook keeps
	// behaviour consistent with the runtime DisconnectActive path.
	// Safe to call whether or not a session was active.
	if (g_sessionEndCleanupHook) g_sessionEndCleanupHook();

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
	// Route through BeginJoinMulti so both single-endpoint ("host:port")
	// and multi-candidate ("host:port;host:port;...") strings are
	// handled uniformly.  A single candidate just means "spray to
	// exactly one endpoint with retries" — which is strictly better
	// than the old one-shot send under packet loss.
	bool ok = g_joiner->BeginJoinMulti(hostAddress, 0, playerHandle,
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

bool IsResyncing(uint32_t* outReceived, uint32_t* outExpected,
                 uint32_t* outAcked,    uint32_t* outTotal) {
	if (outReceived) *outReceived = 0;
	if (outExpected) *outExpected = 0;
	if (outAcked)    *outAcked = 0;
	if (outTotal)    *outTotal = 0;

	auto collect = [&](ATNetplay::Coordinator* c) {
		if (!c) return false;
		if (c->GetPhase() != ATNetplay::Coordinator::Phase::Resyncing)
			return false;
		if (outReceived) *outReceived = c->ResyncReceivedChunks();
		if (outExpected) *outExpected = c->ResyncExpectedChunks();
		if (outAcked)    *outAcked    = c->ResyncAckedChunks();
		if (outTotal)    *outTotal    = c->ResyncTotalChunks();
		return true;
	};

	if (collect(g_joiner.get())) return true;
	for (auto& s : g_hosts) if (collect(s.coord.get())) return true;
	return false;
}

uint64_t MsSinceLastPeerPacket(uint64_t nowMs) {
	ATNetplay::Coordinator *c = ActiveLockstep();
	if (!c) return UINT64_MAX / 2;
	uint64_t last = c->Loop().LastPeerRecvMs();
	if (last == 0) return UINT64_MAX / 2;
	return nowMs > last ? (nowMs - last) : 0;
}

void DisconnectActive() {
	// End every host coord + joiner coord first so subsequent ApplySnapshot
	// in the cleanup hook doesn't race against an active lockstep coord
	// reading sim state for hash computation.  The two-tick terminal
	// teardown in Poll() will finish coord cleanup; we just need them
	// out of an "actively producing frames" phase.
	for (auto& s : g_hosts) {
		if (s.coord) s.coord->End();
	}
	if (g_joiner) g_joiner->End();
	// Treat this as an explicit user dismiss on the joiner side — we
	// don't want the cached rejection to reappear on the Waiting screen
	// after the user chose to leave.
	g_lastJoinPhase = Phase::None;
	g_lastJoinError.clear();

	// Synchronously drive the pre-session restore so the user is back
	// on their own emulator state by the time this returns, rather than
	// racing the next Poll tick to catch the activity edge.  Mirrors
	// the order used by ReconcileHostedGames (coords reach terminal
	// phases first, then the activity edge fires the restore).
	// Hook is a no-op if no snapshot was taken.
	if (g_sessionEndCleanupHook) g_sessionEndCleanupHook();
}

bool SendEmote(uint8_t iconId) {
	ATNetplay::Coordinator *c = ActiveLockstep();
	if (!c) return false;
	return c->SendEmote(iconId);
}

// --- v4 NAT traversal -----------------------------------------------------

void HostSetRelayContext(const char* gameId,
                         const char* sessionIdHex,
                         const char* lobbyHostPort) {
	HostSlot* h = FindHost(gameId);
	if (!h || !h->coord) return;
	uint8_t sid[16] = {};
	if (!sessionIdHex ||
	    !ATNetplay::UuidHexToBytes16(sessionIdHex, sid)) return;
	h->coord->SetRelayContext(sid, lobbyHostPort);
}

void JoinerSetRelayContext(const char* sessionIdHex,
                           const char* lobbyHostPort) {
	if (!g_joiner) return;
	uint8_t sid[16] = {};
	if (!sessionIdHex ||
	    !ATNetplay::UuidHexToBytes16(sessionIdHex, sid)) return;
	g_joiner->SetRelayContext(sid, lobbyHostPort);
}

bool JoinerGetSessionNonceHex(char out33[33]) {
	if (!g_joiner) return false;
	uint8_t n[ATNetplay::kSessionNonceLen];
	g_joiner->GetSessionNonce(n);
	static const char kHex[] = "0123456789abcdef";
	for (size_t i = 0; i < ATNetplay::kSessionNonceLen; ++i) {
		out33[i*2]   = kHex[(n[i] >> 4) & 0xF];
		out33[i*2+1] = kHex[ n[i]       & 0xF];
	}
	out33[32] = '\0';
	return true;
}

uint16_t JoinerBoundPort() {
	return g_joiner ? g_joiner->BoundPort() : 0;
}

size_t JoinerBuildLocalCandidates(char* out, size_t outSize) {
	if (!out || outSize == 0) return 0;
	out[0] = '\0';
	uint16_t port = JoinerBoundPort();
	if (!port) return 0;
	std::vector<std::string> ips;
	ATNetplay::Transport::EnumerateLocalIPv4s(ips);
	std::string built;
	for (const auto& ip : ips) {
		if (!built.empty()) built.push_back(';');
		char ep[64];
		std::snprintf(ep, sizeof ep, "%s:%u", ip.c_str(), (unsigned)port);
		built += ep;
	}
	// Always append loopback last — same-box tests otherwise lose the
	// only candidate the host's punch can actually reach.
	char loop[32];
	std::snprintf(loop, sizeof loop, "127.0.0.1:%u", (unsigned)port);
	if (!built.empty()) built.push_back(';');
	built += loop;
	if (built.size() + 1 > outSize) built.resize(outSize - 1);
	std::memcpy(out, built.data(), built.size());
	out[built.size()] = '\0';
	return built.size();
}

void HostIngestPeerHint(const char* gameId,
                        const char* nonceHex,
                        const char* candidates) {
	HostSlot* h = FindHost(gameId);
	if (!h || !h->coord) return;
	uint8_t nonce[ATNetplay::kSessionNonceLen] = {};
	if (nonceHex && *nonceHex) {
		// Parse exactly 32 hex chars (16 bytes).  Short / malformed
		// nonces fall back to all-zero so the legacy handle-based
		// host-side dedupe path still works.
		size_t parsed = 0;
		uint8_t acc = 0;
		for (const char* p = nonceHex; *p && parsed < 32; ++p) {
			char c = *p;
			int v;
			if      (c >= '0' && c <= '9') v = c - '0';
			else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
			else if (c >= 'A' && c <= 'F') v = c - 'A' + 10;
			else break;
			if ((parsed & 1) == 0) acc = (uint8_t)(v << 4);
			else nonce[parsed / 2] = (uint8_t)(acc | (uint8_t)v);
			++parsed;
		}
	}
	// The coordinator defers the initial burst + sustain timer
	// bookkeeping to its next Poll() (one frame away at 60 Hz), so
	// we don't need to supply a monotonic clock here.
	h->coord->IngestPeerHint(nonce, candidates);
}

} // namespace ATNetplayGlue
