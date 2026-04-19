// Altirra SDL3 netplay - main-loop glue
//
// Owns a collection of Coordinator instances — one per hosted offer
// plus (optionally) one joiner — and exposes a tiny surface the main
// loop in main_sdl3.cpp can call without pulling in the full
// coordinator header.
//
// Why multiple coordinators?  The UI lets the user advertise several
// games in the lobby at once ("My Hosted Games").  Each offer holds
// its own UDP port + Coordinator so a peer connecting on port N ends
// up in the right handshake for that offer.  Only one coordinator can
// reach `Lockstepping` at any time — the activity state machine in
// ui_netplay_actions.cpp keeps the rest Suspended.
//
// Lifecycle for hosts:
//   1. The UI calls `StartHost(gameId, ...)`.  A Coordinator is
//      constructed, `Listen(0)` assigns an ephemeral port, the
//      caller reads it back via `BoundPort(gameId)`.
//   2. The main loop calls `Poll()` each tick — the glue iterates
//      every coordinator (hosts + joiner) and drives them.
//   3. The UI calls `StopHost(gameId)` to tear down one offer, or
//      `Shutdown()` to tear down everything.
//
// Lifecycle for joiners is unchanged from v1 (single-join only).
//
// When no hostedGames exist and no join is in progress every entry point
// here is O(1) — a single empty-vector check — so the hook is free to
// have compiled in.

#pragma once

#include <cstddef>
#include <cstdint>

namespace ATNetplay { enum class CoordPhase; }

namespace ATNetplayGlue {

// Phase mirror of ATNetplay::Coordinator::Phase so the UI can consume
// it without pulling the full coordinator header.  Values match 1:1.
enum class Phase : uint8_t {
	None = 0,            // no coordinator exists for this id
	Idle,
	WaitingForJoiner,
	Handshaking,
	SendingSnapshot,
	ReceivingSnapshot,
	SnapshotReady,
	Lockstepping,
	Ended,
	Desynced,
	Failed,
};

// True iff any coordinator (host or joiner) is in a non-terminal phase.
bool IsActive();

// True iff some coordinator is in the lockstep phase.  At most one
// coordinator can be in this phase per the activity state machine.
bool IsLockstepping();

// Drive all coordinators' Poll().  Must be called once per main-loop
// iteration from main_sdl3.cpp.
void Poll(uint64_t nowMs);

// Returns false when the main loop should SKIP ATSimulator::Advance
// this tick.  Only the currently-lockstepping coordinator (if any)
// can gate emulation.
bool CanAdvanceThisTick();

// Signal that the simulator just advanced one frame.  Drives the
// active-lockstep coordinator's hash + frame counter.  No-op when no
// coordinator is lockstepping.
void OnFrameAdvanced();

// Submit a single frame's local input to whichever coordinator is
// currently lockstepping.  Polls SDL via ATNetplayInput::PollLocal.
// No-op when no coordinator is lockstepping.
void SubmitLocalInput();

// Apply the current lockstep frame's inputs to the simulator by
// driving the netplay-owned controller ports with (host, joiner)
// NetInputs.  Called immediately before g_sim.Advance() when
// CanAdvanceThisTick() returned true.  No-op when no coordinator is
// lockstepping.
void ApplyFrameInputsToSim();

// Tear down every host offer AND the joiner.  Safe to call
// unconditionally on shutdown.
void Shutdown();

// --- Host hostedGames (multi) ---------------------------------------------------
//
// Each call identifies its offer by an opaque string id (the one stored
// in `HostedGame::id`).  The glue stores a coordinator per id.  Calls
// to an unknown id are no-ops except `StartHost` itself, which creates
// the entry.

bool StartHost(const char* gameId,
               uint16_t localPort,            // 0 = ephemeral
               const char* playerHandle,
               const char* cartName,
               uint64_t osRomHash,
               uint64_t basicRomHash,
               uint64_t settingsHash,
               uint16_t inputDelayFrames,
               const uint8_t* entryCodeHash);

// Tear down one host offer.  Safe to call on an unknown id.
void StopHost(const char* gameId);

// Is this offer currently running a coordinator?
bool HostExists(const char* gameId);

// Phase of a single offer; `Phase::None` when unknown.
Phase HostPhase(const char* gameId);

// Port the offer's coordinator bound; 0 when unknown or not listening.
uint16_t HostBoundPort(const char* gameId);

// Per-offer error string; empty when OK or unknown.
const char* HostLastError(const char* gameId);

// Hand the serialised simulator state to a specific offer's
// coordinator so it can stream it to the joiner.  No-op for unknown id.
void SubmitHostSnapshot(const char* gameId, const uint8_t* data, size_t len);

// --- Joiner (single) -------------------------------------------------------

bool StartJoin(const char* hostAddress,
               const char* playerHandle,
               uint64_t osRomHash,
               uint64_t basicRomHash,
               bool acceptTos,
               const uint8_t* entryCodeHash);

void StopJoin();
bool JoinExists();
Phase JoinPhase();
const char* JoinLastError();

// Joiner received snapshot bytes — returns pointer + length for the
// UI/caller to Apply into the simulator.  Out params nullable.
void GetReceivedSnapshot(const uint8_t** data, size_t* len);

// Signal that we've applied the snapshot to the simulator so the
// coordinator can advance into Lockstepping.
void AcknowledgeSnapshotApplied();

// --- Introspection / legacy shims ------------------------------------------

// Which gameId is currently lockstepping, or empty if none.
const char* LockstepOfferId();

// Frame counter of the currently-lockstepping coordinator.
uint32_t CurrentFrame();

// Input-delay the currently-lockstepping coordinator is using, in
// frames.  Returns 0 if no coordinator is lockstepping.
uint32_t CurrentInputDelay();

// True iff the currently-lockstepping coordinator has flagged a
// desync.  Out param, when non-null, receives the frame number at
// which the desync was detected.
bool IsDesynced(int64_t* outFrame);

// ms since we last received a packet from the peer on the currently-
// lockstepping coordinator.  Returns a large number (UINT64_MAX/2) if
// no coordinator is lockstepping or no packet has arrived yet.
uint64_t MsSinceLastPeerPacket(uint64_t nowMs);

// Tear down every active host coord + any joiner coord.  Used by the
// in-session HUD's Disconnect button.  Safe to call when nothing is
// running.
void DisconnectActive();

} // namespace ATNetplayGlue
