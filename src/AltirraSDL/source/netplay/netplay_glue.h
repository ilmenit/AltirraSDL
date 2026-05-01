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
#include <functional>
#include <vector>

namespace ATNetplay { enum class CoordPhase; struct NetBootConfig; }

#ifdef ALTIRRA_NETPLAY_ENABLED

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
	Resyncing,
};

// PeerPath mirror of ATNetplay::Coordinator::PeerPath.  Surfaced to the
// UI so the connecting screen and in-session HUD can show whether the
// session is on the direct UDP path or has fallen back to relay
// (higher latency, slightly less bandwidth headroom).
//   None    — no coordinator exists / not yet relevant
//   Direct  — UDP punch holds; both peers are talking peer-to-peer
//   Relay   — packets are routed through the lobby reflector
// Edge transitions are detected by ATNetplayUI_Poll to fire toasts
// and update the persistent connection-mode pip in the in-session HUD.
enum class PeerPath : uint8_t {
	None = 0,
	Direct,
	Relay,
};

// True iff any coordinator (host or joiner) is in a non-terminal phase.
bool IsActive();

// True iff a peer interaction is in flight — i.e. some coordinator is
// past WaitingForJoiner (Handshaking onward, excluding terminal phases).
// Use this — NOT IsActive() — to gate user actions that would disrupt
// an in-progress connection (e.g. Boot/Open Image on the host side).
// A host that has only published an offer and is still in
// WaitingForJoiner returns false here, so the user can keep playing
// (or switch images) until somebody actually starts connecting.  Once
// a joiner sends Hello, the host's coord transitions to Handshaking
// and this predicate flips to true; from that point the snapshot path
// (kATDeferred_NetplayHostBoot) takes over the loaded image anyway.
bool IsSessionEngaged();

// True iff some coordinator is in the lockstep phase.  At most one
// coordinator can be in this phase per the activity state machine.
bool IsLockstepping();

// Session-end cleanup hook.  Registered by ui_netplay so the
// canonical netplay profile (see ATNetplayProfile in
// netplay/netplay_profile.h) is torn down when the lower glue layer
// dismantles a session via DisconnectActive() or Shutdown() — paths
// that don't otherwise reach the activity-edge restore in
// ReconcileHostedGames.  The hook is invoked before coordinators are
// torn down so the sim can be returned to its pre-session state
// cleanly.  Pass nullptr to clear.
using SessionEndCleanupFn = void(*)();
void SetSessionEndCleanupHook(SessionEndCleanupFn fn);

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

// --- Joiner-side cache hooks (Item 4d/4e) ---------------------------------
// Install the user's Game Library lookup callback.  StartJoin will compose
// it with the file-based netplay_cache so the coordinator's cache hook
// answers "do you have this game locally?" by checking the cache
// directory FIRST and the library SECOND.  Pass nullptr to disable
// library lookup (cache directory still works).
//
// Lifetime: call once at app startup.  The callback must outlive every
// StartJoin → StopJoin cycle.
using LibraryLookupFn = std::function<bool(
    uint32_t crc32, uint64_t expectedSize, const char ext[8],
    std::vector<uint8_t>& outBytes)>;
void SetLibraryLookupHook(LibraryLookupFn fn);

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
               const uint8_t* entryCodeHash,
               const ATNetplay::NetBootConfig& bootConfig);

// Joiner-side view of the BootConfig shipped in NetWelcome.  Returns
// a zero-initialised struct if no joiner coordinator exists yet.
ATNetplay::NetBootConfig JoinBootConfig();

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

// --- Prompt-accept (host) -------------------------------------------------
//
// When enabled, an arriving Hello passes validation and then waits for
// the host UI to call HostAcceptPending(i) / HostRejectPending(i).  The
// coordinator keeps a FIFO queue of pending joiners so a second peer
// can line up while the host is deliberating on the first.  The UI
// iterates HostPendingCount() each tick and renders a row per entry.

void HostSetPromptAccept(const char* gameId, bool enable);

// Number of joiners currently queued awaiting the host's decision.
// 0 when no offer is registered under gameId.
size_t HostPendingCount(const char* gameId);

// Convenience: HostPendingCount(gameId) != 0.
bool   HostHasPendingDecision(const char* gameId);

// Read the i-th queued pending entry.  outHandle (NUL-terminated,
// capped at outHandleSize) receives the joiner's handle.  outArrivedMs
// (may be null) receives the host-local clock ms at which the entry
// was first enqueued — callers use it to render a "Requested Xs ago"
// timer.  Returns false (and writes "") if the index is out of range
// or the offer is unknown.
bool HostPendingAt(const char* gameId, size_t i,
                   char* outHandle, size_t outHandleSize,
                   uint64_t* outArrivedMs);

// Back-compat single-slot accessor — equivalent to HostPendingAt(0,…)
// without the timestamp.  New callers should use HostPendingAt.
bool HostPendingJoinerHandle(const char* gameId,
                             char* outBuf, size_t outBufSize);

// Accept the i-th queued entry (default 0): coordinator rejects every
// other queued peer with kRejectHostFull and proceeds to Snapshot send.
void HostAcceptPending(const char* gameId, size_t i = 0);

// Reject the i-th queued entry (default 0): coordinator sends
// kRejectHostRejected to just that peer and leaves the rest queued.
void HostRejectPending(const char* gameId, size_t i = 0);

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

// Connection mode for the joiner coordinator (Direct vs Relay).
// Returns PeerPath::None when no joiner exists.  Read in the
// connecting screen (RenderWaiting) and the in-session HUD (HUD pip).
PeerPath JoinerPeerPath();

// Connection mode for the host offer identified by `gameId`
// (PeerPath::None when no such offer exists).
PeerPath HostPeerPath(const char* gameId);

// Active-coordinator convenience: returns the PeerPath of whichever
// coordinator is currently lockstepping (host or joiner) so the
// in-session HUD has a single source of truth without caring which
// side the local user is on.  PeerPath::None when none is locksteping.
PeerPath ActivePeerPath();

// True iff any coordinator is currently running a mid-session state
// transfer (Phase::Resyncing).  When this returns true, the HUD
// should render a "Resynchronizing…" overlay and swallow user input
// to the sim — the simulator is paused for the duration and a held
// key would mis-land on the first post-resume frame.
// Out params: populated with chunk progress when non-null.
//   *outReceivedChunks  — joiner side: chunks received so far
//   *outExpectedChunks  — joiner side: total chunks
//   *outAckedChunks     — host side: chunks acknowledged so far
//   *outTotalChunks     — host side: total chunks
// Missing-side counters are left at zero.
bool IsResyncing(uint32_t* outReceivedChunks = nullptr,
                 uint32_t* outExpectedChunks = nullptr,
                 uint32_t* outAckedChunks    = nullptr,
                 uint32_t* outTotalChunks    = nullptr);

// Tear down every active host coord + any joiner coord.  Used by the
// in-session HUD's Disconnect button.  Safe to call when nothing is
// running.
void DisconnectActive();

// Send a single communication icon ("emote") to the peer.  No-op
// unless some coordinator is currently in Phase::Lockstepping.
// Returns true iff the packet was handed to the socket.
bool SendEmote(uint8_t iconId);

// --- v4 NAT traversal ------------------------------------------------------

// Configure the coordinator for a given host offer with its lobby
// session id + lobby host:port so the auto-relay fallback can engage
// if the direct punch doesn't land.  Safe to call on unknown gameId
// (no-op).  `sessionIdHex` is the lobby sessionId (UUID string);
// `lobbyHostPort` is typically "lobby.example.com:8081".
void HostSetRelayContext(const char* gameId,
                         const char* sessionIdHex,
                         const char* lobbyHostPort);

// Joiner version.  Must be called after StartJoin so the coordinator
// already exists.  Same field semantics.
void JoinerSetRelayContext(const char* sessionIdHex,
                           const char* lobbyHostPort);

// Write the joiner coordinator's per-attempt sessionNonce as 32 hex
// chars + NUL into `out33`.  Returns false if no joiner exists (in
// which case out33 is left untouched).  Used by the UI to POST the
// nonce to the lobby so the host can match up the subsequent hello
// spray with the hint.
bool JoinerGetSessionNonceHex(char out33[33]);

// The joiner's bound UDP port, or 0 if no joiner exists.  Used to
// build the candidate string for the peer-hint POST.
uint16_t JoinerBoundPort();

// Enumerate the joiner's own LAN IPv4 addresses and format a
// semicolon-separated "ip:port;ip:port;..." candidate string into
// `out` of size `outSize`.  Appends loopback at the end so the host
// can still punch on same-box tests.  Returns the number of chars
// written (not counting NUL).  Writes "" and returns 0 when no
// joiner exists or enumeration fails.
size_t JoinerBuildLocalCandidates(char* out, size_t outSize);

// Feed a peer-hint received via heartbeat to the host coordinator.
// `nonceHex` is 32 hex chars (joiner's sessionNonce); `candidates`
// is the semicolon-separated "ip:port;ip:port;..." list.  No-op if
// gameId is unknown.
void HostIngestPeerHint(const char* gameId,
                        const char* nonceHex,
                        const char* candidates);

} // namespace ATNetplayGlue

#else // !ALTIRRA_NETPLAY_ENABLED

// -----------------------------------------------------------------------
// Inline no-op stubs.
//
// When the netplay module is compiled out (ALTIRRA_NETPLAY=OFF — the
// WASM build forces this, since the browser has no UDP sockets and no
// COOP/COEP setup for pthread-backed coordinators), main_sdl3.cpp and
// a handful of UI files still reference ATNetplayGlue:: symbols in
// code paths that are naturally unreachable when the feature is off.
//
// Rather than litter those call sites with `#ifdef ALTIRRA_NETPLAY_ENABLED`
// guards, the stubs below give every entry point a constant-folding
// "netplay not active" answer.  Netplay-aware code paths become dead
// branches the compiler eliminates; the one-line cost is negligible
// and keeps main_sdl3.cpp readable across all build flavours.
//
// None of these stubs has side effects or touches global state.
// -----------------------------------------------------------------------

namespace ATNetplayGlue {
    inline bool     IsActive()                      { return false; }
    inline bool     IsSessionEngaged()              { return false; }
    inline bool     IsLockstepping()                { return false; }
    inline void     Poll(uint64_t)                  {}
    inline bool     CanAdvanceThisTick()            { return true; }
    inline void     OnFrameAdvanced()               {}
    inline void     SubmitLocalInput()              {}
    inline void     ApplyFrameInputsToSim()         {}
    inline void     Shutdown()                      {}
    inline void     DisconnectActive()              {}
    inline bool     SendEmote(uint8_t)              { return false; }
    inline bool     IsDesynced(int64_t* = nullptr)  { return false; }
    inline bool     IsResyncing(uint32_t* = nullptr, uint32_t* = nullptr,
                                uint32_t* = nullptr, uint32_t* = nullptr) { return false; }
    inline uint32_t CurrentFrame()                  { return 0; }
    inline uint32_t CurrentInputDelay()             { return 0; }
    inline uint64_t MsSinceLastPeerPacket(uint64_t) { return UINT64_MAX / 2; }
    enum class PeerPath : uint8_t { None = 0, Direct, Relay };
    inline PeerPath JoinerPeerPath()                { return PeerPath::None; }
    inline PeerPath HostPeerPath(const char*)       { return PeerPath::None; }
    inline PeerPath ActivePeerPath()                { return PeerPath::None; }
    inline const char* LockstepOfferId()            { return ""; }
    using SessionEndCleanupFn = void(*)();
    inline void SetSessionEndCleanupHook(SessionEndCleanupFn) {}
} // namespace ATNetplayGlue

#endif // ALTIRRA_NETPLAY_ENABLED
