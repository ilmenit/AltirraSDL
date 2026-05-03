// Altirra SDL3 netplay - session coordinator
//
// Packages the four netplay modules (transport, protocol, snapshot
// channel, lockstep loop) behind a single state machine that the
// main loop can drive in a handful of calls per frame.
//
//   Phase lifecycle (host side):
//     Idle -> BeginHost() -> WaitingForJoiner
//         [NetHello arrives, validates]
//         -> SendingSnapshot (needs SubmitSnapshotForUpload)
//         [all chunks ACKed]
//         -> Lockstepping
//         [End() or desync]
//         -> Ended / Desynced
//
//   Phase lifecycle (joiner side):
//     Idle -> BeginJoin()
//         -> Handshaking (NetHello sent, awaiting NetWelcome)
//         [NetWelcome arrives]
//         -> ReceivingSnapshot
//         [all chunks received]
//         -> SnapshotReady (caller must consume via GetReceivedSnapshot)
//         -> Lockstepping
//         [End() or desync]
//         -> Ended / Desynced
//
// The coordinator does NOT call into the simulator directly.  Two
// I/O boundaries are exposed to the main application:
//
//   Host: after BeginHost transitions to SendingSnapshot, the main
//   app serialises ATSimulator state (via IATSerializable) and
//   passes the bytes to SubmitSnapshotForUpload().
//
//   Joiner: after Phase::SnapshotReady, the main app retrieves bytes
//   via GetReceivedSnapshot() and calls ATSimulator::ApplySnapshot;
//   on success it calls AcknowledgeSnapshotApplied() to arm the
//   lockstep phase.
//
// Local input capture / remote input injection are also exposed as
// plain calls — the coordinator doesn't know about ATInputManager.

#pragma once

#include "lockstep.h"
#include "snapshot_channel.h"
#include "transport.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace ATNetplay {

class Coordinator {
public:
	enum class Phase : uint8_t {
		Idle = 0,
		WaitingForJoiner,       // host only
		Handshaking,            // joiner only (Hello sent, awaiting Welcome)
		SendingSnapshot,        // host, snapshot upload in flight
		ReceivingSnapshot,      // joiner, snapshot download in flight
		SnapshotReady,          // joiner, waiting for app to apply & ack
		Lockstepping,           // normal play
		Resyncing,              // mid-session state transfer to recover from a hash mismatch
		Ended,                  // clean Bye or app-initiated End
		Desynced,               // hash mismatch — unrecoverable (flap limit hit)
		Failed,                 // transport / handshake / snapshot failure
	};

	Coordinator();
	~Coordinator();

	Coordinator(const Coordinator&) = delete;
	Coordinator& operator=(const Coordinator&) = delete;

	// ---- host entrypoint --------------------------------------------------

	// Open a UDP listener on `localPort` (0 = ephemeral).  The rest of
	// the arguments populate NetWelcome when a joiner connects.  Returns
	// false if the socket can't be bound.
	//
	// entryCodeHash may be nullptr for public sessions; for private
	// sessions pass a 16-byte truncated SHA-256 of the agreed code.
	// The joiner's NetHello must carry the same bytes or we reject
	// with kRejectBadEntryCode.
	bool BeginHost(uint16_t localPort,
	               const char* playerHandle,
	               const char* cartName,
	               uint64_t osRomHash,
	               uint64_t basicRomHash,
	               uint64_t settingsHash,
	               uint16_t inputDelayFrames,
	               const uint8_t* entryCodeHash /* 16 bytes or nullptr */,
	               const NetBootConfig& bootConfig);

	// ---- joiner entrypoint ------------------------------------------------

	// Resolve hostAddress ("host:port") and send NetHello.  The reply
	// (NetWelcome or NetReject) arrives asynchronously — Poll() drives
	// the handshake.  Returns false on resolve/socket failure.
	bool BeginJoin(const char* hostAddress,
	               const char* playerHandle,
	               uint64_t osRomHash,
	               uint64_t basicRomHash,
	               bool acceptTos,
	               const uint8_t* entryCodeHash /* 16 bytes or nullptr */);

	// Multi-candidate join (v3 NAT traversal).  Accepts a list of host
	// endpoints ("host:port;host:port;...").  Binds a UDP socket on
	// localPort (0 = ephemeral), resolves every candidate, and begins
	// spraying NetHello to all resolved endpoints every kHelloRetryMs
	// until one replies with NetWelcome or kHelloTimeoutMs elapses.
	// Same identity params as BeginJoin; returns false on socket bind
	// failure or when zero candidates resolved.
	bool BeginJoinMulti(const char* hostCandidatesSemicolonList,
	                    uint16_t localPort,
	                    const char* playerHandle,
	                    uint64_t osRomHash,
	                    uint64_t basicRomHash,
	                    bool acceptTos,
	                    const uint8_t* entryCodeHash /* 16 bytes or nullptr */);

	// ---- main-loop driver -------------------------------------------------

	// Call once per main-loop iteration with a monotonic ms clock.
	// Drains the UDP socket, dispatches packets to the right sub-
	// state machine, and emits outgoing packets (input packets,
	// snapshot chunks, retries).
	void Poll(uint64_t nowMs);

	// ---- snapshot I/O boundary -------------------------------------------

	// Host: hand off the serialised simulator state.  Must be called
	// exactly once while in Phase::SendingSnapshot, immediately after
	// BeginHost accepts a joiner.  The coordinator fills in the
	// snapshot-bytes/chunks fields of the NetWelcome and begins
	// chunk transfer.
	//
	// Note the ordering: we send a NetWelcome first, then stream
	// chunks.  The joiner uses NetWelcome's chunk count to dimension
	// its receive buffer.  This means the caller must stash the
	// snapshot bytes here BEFORE we can respond to the incoming
	// Hello.  If the handshake fires (Hello arrives) before the app
	// has stashed the bytes, the coordinator holds the joiner in a
	// pending-welcome slot until the bytes arrive — which in the
	// integrated main loop is "zero ticks later".
	void SubmitSnapshotForUpload(const uint8_t* data, size_t len);

	// Joiner: accessible once Phase == SnapshotReady.  Caller
	// deserialises via ATSimulator::ApplySnapshot and then calls
	// AcknowledgeSnapshotApplied() to move to Lockstepping.
	const std::vector<uint8_t>& GetReceivedSnapshot() const { return mSnapRx.Data(); }

	// ---- resync I/O boundary --------------------------------------------
	// True iff the host just transitioned into Phase::Resyncing and is
	// awaiting a captured savestate buffer from the application layer.
	// The app should call ATNetplay::CaptureSavestate() and pass the
	// bytes to SubmitResyncCapture().  Clears after SubmitResyncCapture.
	bool NeedsResyncCapture() const { return mNeedsResyncCapture; }

	// Host: hand over the captured savestate bytes.  Triggers the
	// ResyncStart + chunk stream.  No-op if called outside Resyncing.
	void SubmitResyncCapture(const uint8_t* data, size_t len);

	// True iff the joiner has fully received the resync payload and is
	// awaiting the application layer to apply it to the live simulator.
	bool NeedsResyncApply() const { return mNeedsResyncApply; }

	// Joiner: the received resync payload.  Valid once NeedsResyncApply
	// returns true.
	const std::vector<uint8_t>& GetReceivedResyncData() const { return mSnapRx.Data(); }

	// Joiner: signal that the savestate was applied cleanly.  Sends
	// NetResyncDone, re-seeds LockstepLoop at the agreed resume frame,
	// and returns to Phase::Lockstepping.  Call after ApplySavestate
	// succeeds; if it fails, the app should End() the session.
	void AcknowledgeResyncApplied();
	// Joiner: BootConfig parsed from the last NetWelcome.  Valid once
	// Phase >= ReceivingSnapshot.  Host-side returns the config we
	// will ship.
	const NetBootConfig& GetBootConfig() const { return mBootConfig; }
	void AcknowledgeSnapshotApplied();

	// ---- joiner-side cache hooks (Item 4d/4e) ---------------------------------
	//
	// The coordinator is library/configdir-agnostic.  The UI/glue layer
	// installs callbacks that:
	//   - look up game bytes by (CRC32, expected size, extension)
	//     before the chunked download starts; if the lookup yields
	//     bytes whose CRC32 matches, the joiner adopts them locally
	//     and sends NetSnapSkip so the host can fast-finish too;
	//   - persist the downloaded bytes after a successful chunk-by-
	//     chunk transfer so the next session for the same game hits
	//     the cache.
	// Returning false / no-op for either is fine — the chunked
	// transfer is the always-correct fallback.
	using CacheLookupFn = std::function<bool(
		uint32_t crc32, uint64_t expectedSize, const char ext[8],
		std::vector<uint8_t>& outBytes)>;
	using CacheStoreFn  = std::function<void(
		uint32_t crc32, const char ext[8],
		const uint8_t* data, size_t len)>;
	void SetCacheLookupHook(CacheLookupFn fn) { mCacheLookup = std::move(fn); }
	void SetCacheStoreHook (CacheStoreFn  fn) { mCacheStore  = std::move(fn); }

	// Test-only accessor.  Used by coordinator_selftest's lossy
	// regressions to call mTransport.SetTestDropRate(...).  Not
	// surfaced by netplay_glue, so production callers cannot reach
	// the underlying socket.
	Transport& TestGetTransport() { return mTransport; }

	// ---- lockstep frame API (valid only in Lockstepping) -----------------

	void SubmitLocalInput(const NetInput& in) { mLoop.SubmitLocalInput(in); }
	bool CanAdvance() const { return mLoop.CanAdvance(); }
	bool GetInputsForCurrentFrame(NetInput& p1, NetInput& p2) const {
		return mLoop.GetInputsForCurrentFrame(p1, p2);
	}
	void OnFrameAdvanced(uint32_t simStateHash);

	// ---- prompt-accept gate ----------------------------------------------
	//
	// When enabled (host only), an arriving Hello that passes all the
	// usual validation (protocol, CRCs, TOS, entry code) does NOT
	// auto-progress to SendingSnapshot.  Instead the coordinator keeps
	// a FIFO queue of pending joiners; the host UI polls it, shows an
	// Allow / Deny card per entry, and calls AcceptPendingJoiner(i) or
	// RejectPendingJoiner(i) to resume the handshake.  Off by default.
	void SetPromptAccept(bool enable) { mPromptAccept = enable; }

	// How many joiners are currently queued awaiting a decision.
	size_t PendingDecisionCount() const { return mPendingDecisions.size(); }

	// Convenience (back-compat): true iff at least one entry is queued.
	bool HasPendingDecision() const { return !mPendingDecisions.empty(); }

	// NUL-terminated joiner handle for the i-th queued pending Hello.
	// Returns "" when i is out of range.  Buffer is owned by the
	// coordinator; copy if you need to keep it.
	const char* PendingJoinerHandle(size_t i = 0) const;

	// Host-local wall-clock time (milliseconds, from the nowMs passed
	// to Poll) when the i-th entry first arrived.  0 when i is out of
	// range.  The UI uses this to render a "Requested Xs ago" timer.
	uint64_t PendingArrivedMs(size_t i = 0) const;

	// Resolve the i-th queued Hello.  No-op when the index is out of
	// range or nothing is pending.
	//
	// Accept(i) adopts that peer, rejects every other queued entry
	// with kRejectHostFull, and proceeds to SendingSnapshot (or holds
	// for the upload bytes).  Reject(i) sends kRejectHostRejected to
	// that one peer and removes it from the queue; the host stays in
	// WaitingForJoiner so the next queued entry can be decided.
	void AcceptPendingJoiner(size_t i = 0);
	void RejectPendingJoiner(size_t i = 0);

	// ---- emote (fire-and-forget UI reactions) -----------------------------

	// Send a NetEmote to the peer.  No-op unless Phase::Lockstepping
	// (only phase where mPeer is guaranteed live and stable).  Returns
	// true iff the packet was actually handed to the socket.
	bool SendEmote(uint8_t iconId);

	// ---- desync diagnostics ---------------------------------------------
	//
	// Hand the coordinator a freshly-computed per-subsystem state hash
	// breakdown for a specific frame.  The coordinator forwards it to
	// the peer (so the peer can log a precise DIFF on receipt) and
	// caches it locally (so when the peer's own breakdown arrives for
	// the same frame, we log a DIFF here too).  Called by the glue on
	// desync detection and on the first lockstep frame of a session.
	// No-op when no peer is known.  Both sides may call this for any
	// frame; packets carrying the same frame are idempotent.
	void SubmitLocalSimHashDiag(const NetSimHashDiag& d);

	// ---- v4 NAT traversal --------------------------------------------
	//
	// Configure the lobby's relay endpoint and this session's 16-byte
	// identifier (first 16 bytes of the lobby sessionId UUID, hex-
	// decoded, no dashes).  Needed before the auto-relay fallback can
	// engage; passing an empty/invalid sessionId disables relay.  The
	// UI calls this once — on the host immediately after
	// LobbyClient::Create returns, on the joiner before BeginJoinMulti.
	// `lobbyHostPort` is "host:port" (typically kReflectorPortDefault,
	// 8081) and resolved once here.  Safe to call multiple times.
	void SetRelayContext(const uint8_t sessionIdBytes16[16],
	                     const char* lobbyHostPort);

	// Host-side: called by the heartbeat thread each time the lobby
	// returns a peer-hint.  `nonce16` is the joiner's sessionNonce
	// (may be all-zero for pre-v4 joiners), `candidatesSemicolonList`
	// is the joiner's advertised "ip:port;ip:port;..." candidate set.
	// Queues NetPunch targets for each candidate; the initial burst
	// and subsequent sustain probes are driven from Poll() so the
	// caller doesn't need a monotonic clock.  Safe to call repeatedly
	// with the same nonce — we dedupe on nonce+endpoint.
	void IngestPeerHint(const uint8_t nonce16[kSessionNonceLen],
	                    const char* candidatesSemicolonList);

	// Connection mode for the UI.  Direct = UDP punch held; Relay =
	// packets routed through the lobby reflector after fallback.
	// The full enum lives in the private section near other v4
	// relay state — duplicated here as a public alias so the UI can
	// take the dependency without including any private types.
	enum class PeerPath : uint8_t { Direct, Relay };

	// True once relay fallback has taken over the UDP path for this
	// session.  UI can surface a "Relay active" badge after this flips.
	bool IsRelayActive() const { return mPeerPath == PeerPath::Relay; }

	// Edge-detected by ATNetplayUI_Poll to fire the connection-quality
	// toast and update the in-session HUD pip.  Const, lock-free.
	PeerPath GetPeerPath() const noexcept { return mPeerPath; }

	// Copy out our own per-attempt sessionNonce (16 bytes) so the UI
	// can include it in the peer-hint POST before BeginJoinMulti.
	// Valid after BeginJoin/BeginJoinMulti and until End().
	void GetSessionNonce(uint8_t out[kSessionNonceLen]) const;

	// ---- termination ------------------------------------------------------

	// Send a NetBye and transition to Ended.  Idempotent.
	void End(uint32_t byeReason = kByeCleanExit);

	// ---- queries ----------------------------------------------------------

	Phase GetPhase() const { return mPhase; }

	// True in phases where the main loop should suppress normal SDL
	// input routing (to avoid double-applying inputs that the
	// coordinator will also inject).
	bool OwnsLocalInput() const { return mPhase == Phase::Lockstepping; }

	// True iff the main loop should call Advance() this tick.
	bool IsLockstepping() const { return mPhase == Phase::Lockstepping; }

	// Lockstep accessors for HUD / diagnostics.
	const LockstepLoop& Loop() const { return mLoop; }
	LockstepLoop& Loop() { return mLoop; }

	uint16_t BoundPort() const { return mTransport.BoundPort(); }

	// Last error message (one-shot; cleared on next successful
	// transition).  Empty when no error.
	const char* LastError() const { return mLastError; }

private:
	enum class Role : uint8_t {
		None, Host, Joiner,
	};

	void FailWith(const char* msg);
	void HandleHelloFromJoiner(const NetHello& hello, const Endpoint& from, uint64_t nowMs);
	void HandleWelcomeFromHost(const NetWelcome& w, uint64_t nowMs);
	void HandleRejectFromHost(const NetReject& r);
	void HandleInputPacket(const NetInputPacket& pkt, uint64_t nowMs);
	void HandleSnapChunk(const NetSnapChunk& c, uint64_t nowMs);
	void HandleSnapAck(const NetSnapAck& a);
	void HandleBye(const NetBye& b, const Endpoint& from);
	void HandleResyncStart(const NetResyncStart& s, uint64_t nowMs);
	void HandleResyncDone(const NetResyncDone& d, uint64_t nowMs);
	void HandleEmote(const NetEmote& e);
	void HandleSimHashDiag(const NetSimHashDiag& d);
	// If we have both a local and a peer diag for the same frame, log
	// a DIFF line naming every subsystem whose hash disagrees.  Fires
	// once per (frame) pair; a second call with identical frame is a
	// no-op.  Called from SubmitLocalSimHashDiag and HandleSimHashDiag.
	void MaybeLogSimHashDiff();

	// Resync: begin a host-initiated mid-session savestate transfer.
	// Called from Poll() when the host detects a local desync or when
	// the host receives a (future) ResyncRequest from a joiner that
	// detected first.  If the flap limit is reached, transitions to
	// Phase::Desynced (terminal) instead.
	void BeginHostResync(uint64_t nowMs);

	// Resync: joiner-side response to a ResyncStart.  Halts lockstep,
	// re-arms the existing SnapshotReceiver for the new epoch, and
	// waits for chunks to arrive.
	void BeginJoinerResync(const NetResyncStart& s, uint64_t nowMs);

	void SendHello();
	void SendWelcome();
	void SendReject(uint32_t reason, const Endpoint& to);
	void SendBye(uint32_t reason);
	void PumpSnapshotSender(uint64_t nowMs);
	void PumpLockstepSend();

	// Populate a NetHello with all current joiner-side state
	// (protocol version, ROM hashes, handle, entry code, **and the
	// per-attempt session nonce**) so the four call sites that emit
	// a Hello — BeginJoin, BeginJoinMulti's initial spray, the Poll
	// retransmit spray, and plain SendHello — stay in sync.
	void FillHello(NetHello& h) const;

	// Seed mSessionNonce from a cryptographic-quality source.  Called
	// once per join attempt from BeginJoin / BeginJoinMulti.
	void GenerateSessionNonce();

	// Transport.
	Transport mTransport;
	Endpoint  mPeer;                 // set once we learn the peer's addr
	bool      mPeerKnown = false;

	// Multi-candidate join (v3 NAT traversal).  When the joiner starts
	// a join the caller may pass several candidate host endpoints; the
	// coordinator sprays NetHello to all of them at kHelloRetryMs
	// intervals until one replies with NetWelcome.  On first Welcome
	// we lock mPeer to the responder and clear this list.
	std::vector<Endpoint> mHelloCandidates;
	uint64_t              mLastHelloMs   = 0;
	uint64_t              mHelloStartMs  = 0;
	static constexpr uint64_t kHelloRetryMs    = 250;
	static constexpr uint64_t kHelloTimeoutMs  = 15000;

	// Per-attempt random nonce (16 bytes) that rides in every Hello we
	// emit during a single StartJoin.  The host uses it to recognise
	// Hellos arriving from multiple network paths (LAN / srflx / NAT
	// hairpin / loopback) as belonging to the *same* joiner, instead of
	// treating each source endpoint as a distinct join attempt.
	// Generated once per join; unchanged across retransmits and across
	// all sprayed candidates.  Filled via GenerateSessionNonce().
	uint8_t mSessionNonce[kSessionNonceLen] = {};

	// Remembered reason from the last NetReject we received while the
	// multi-candidate spray still had other candidates in flight.  We
	// only transition to Phase::Failed when *every* candidate has
	// rejected (or timed out), and we use this reason so the user sees
	// the host-supplied cause rather than a generic "timeout".
	uint32_t mLastRejectReason = 0;
	bool     mHaveLastRejectReason = false;

	// Phase + role.
	Phase mPhase = Phase::Idle;
	Role  mRole  = Role::None;

	// Handshake state.
	char     mPlayerHandle[kHandleLen] = {};
	char     mCartName[kCartLen]       = {};
	uint64_t mOsRomHash    = 0;
	uint64_t mBasicRomHash = 0;
	uint64_t mSettingsHash = 0;
	uint16_t mInputDelay   = 3;
	uint8_t  mEntryCodeHash[kEntryCodeHashLen] = {};
	bool     mHasEntryCode = false;
	bool     mAcceptTos    = false;

	// v3 BootConfig — filled on host by BeginHost, on joiner by
	// HandleWelcomeFromHost.
	NetBootConfig mBootConfig{};

	// Joiner-side cache hooks (Item 4d).  Optional — null is fine,
	// and means the joiner always falls back to the chunked download.
	CacheLookupFn mCacheLookup;
	CacheStoreFn  mCacheStore;
	// Set when the joiner short-circuited the download via
	// NetSnapSkip; suppresses the post-download cache-store call so
	// we don't re-write bytes that just came from the cache.
	bool          mUsedLocalSnapshot = false;

	// Host pending-welcome: a joiner Hello arrived, but
	// SubmitSnapshotForUpload() hasn't been called yet.  We hold
	// enough info to send the Welcome once bytes arrive.
	bool     mHostHasPendingJoiner = false;
	Endpoint mPendingJoiner;
	uint8_t  mPendingJoinerEntryCode[kEntryCodeHashLen] = {};

	// Prompt-accept gate (host only).  When mPromptAccept is true and
	// a valid Hello arrives, we enqueue it and wait for the UI to call
	// AcceptPendingJoiner(i) / RejectPendingJoiner(i).  Cap to a small
	// number so a misbehaving peer can't balloon our memory or drown
	// the host's decision list.
	struct PendingDecision {
		// Primary source endpoint — the first path on which this
		// joiner's Hello arrived.  The Welcome reply goes here.
		Endpoint peer;

		// Additional source endpoints observed for the *same* joiner
		// (same sessionNonce, or same handle for legacy clients).
		// The v3 spray has the joiner fire Hellos to multiple host
		// candidates in parallel; each arrives on a different source
		// endpoint on our side (LAN NIC / loopback / NAT hairpin).
		// We coalesce them into one UI row and remember the rest so
		// we can avoid sending a spurious Reject to ourselves when
		// the user accepts, and so the UI shows one joiner, not N.
		std::vector<Endpoint> altPeers;

		// Joiner-generated per-attempt nonce (zero for v1.0 clients
		// that don't populate it; we then dedupe on handle instead).
		uint8_t  sessionNonce[kSessionNonceLen] = {};
		bool     hasSessionNonce = false;

		char     handle[kHandleLen + 1] = {};
		uint64_t arrivedMs = 0;   // host-local clock from Poll(nowMs)
	};
	static constexpr size_t kMaxPendingDecisions = 8;
	bool     mPromptAccept       = false;
	std::vector<PendingDecision> mPendingDecisions;

	// Snapshot channels (only one is active at a time).
	std::vector<uint8_t> mSnapTxBuffer;   // host's copy of what we're uploading
	SnapshotSender       mSnapTx;
	SnapshotReceiver     mSnapRx;

	// Lockstep.
	LockstepLoop mLoop;

	// Buffers.
	uint8_t  mRxBuf[kMaxDatagramSize];
	uint8_t  mTxBuf[kMaxDatagramSize];

	// Pacing for outgoing input packets (Lockstepping phase).  We
	// emit one per Poll() call — the main loop ticks at 60 Hz, so
	// that's the rate the peer expects.
	bool mWantsOutgoingInput = false;

	// ---- Resync state ----------------------------------------------------
	//
	// Host captures its sim state at mResyncFrame-1 (the last frame it
	// applied before the desync) and ships it so the joiner can jump to
	// mResyncFrame with matching state.  The SnapshotSender / Receiver
	// carry the bytes; the NetResyncStart header disambiguates "session
	// start" chunks (which are cleared out long before Lockstepping
	// begins) from mid-session resync chunks via the epoch counter.
	uint32_t mResyncEpoch = 0;             // incremented per resync
	uint32_t mResyncResumeFrame = 0;       // frame both peers jump to
	uint32_t mResyncSeedHash = 0;          // post-apply hash for ack slot
	std::vector<uint8_t> mResyncTxBuffer;  // host: capture destination
	bool     mNeedsResyncCapture = false;  // host: app must call SubmitResyncCapture
	bool     mNeedsResyncApply   = false;  // joiner: app must call AcknowledgeResyncApplied

	// Flap limiter: if the session keeps resyncing we're looking at a
	// deterministic bug that resyncing won't fix.  Give up after kMaxResyncsPerWindow
	// starts within kResyncFlapWindowMs.
	static constexpr int      kMaxResyncsPerWindow = 3;
	static constexpr uint64_t kResyncFlapWindowMs = 60000;
	int      mResyncCountInWindow = 0;
	uint64_t mResyncWindowStartMs = 0;

	// ResyncStart retransmit.  If the initial start packet is lost, the
	// joiner stays in Lockstepping and drops every chunk we send, so
	// SnapTx eventually burns its retry budget.  Re-emit the start
	// every kResyncStartRetryMs until the joiner acks at least one
	// chunk (from which point we know they have the header).
	static constexpr uint64_t kResyncStartRetryMs = 500;
	uint64_t mResyncStartLastSentMs = 0;

	// ResyncDone retransmit (joiner side).  AcknowledgeResyncApplied
	// flips mAwaitingHostResyncAck; Poll() re-emits NetResyncDone every
	// kResyncDoneRetryMs until we see a peer input packet (proof the
	// host received the Done and returned to Lockstepping).  Without
	// this, a single lost UDP packet strands the host in Resyncing
	// forever: the joiner is already advancing and sending input, but
	// the host drops those packets at HandleInputPacket's phase check
	// so no recovery signal reaches it.
	static constexpr uint64_t kResyncDoneRetryMs = 250;
	bool     mAwaitingHostResyncAck = false;
	uint64_t mResyncDoneLastSentMs  = 0;

	// Overall Phase::Resyncing deadline.  Stamped by BeginHostResync
	// and BeginJoinerResync; Poll() aborts the session if the phase
	// persists past this budget.  The 10-minute peer-silence timeout
	// at the bottom of Poll() only runs in Lockstepping so can't
	// rescue a stuck resync; this deadline fills that gap.  Sized for
	// a multi-MB savestate over a slow link plus a few retransmit
	// cycles.
	static constexpr uint64_t kResyncOverallTimeoutMs = 15000;
	uint64_t mResyncStartMs = 0;

	// Joiner-side ReceivingSnapshot watchdog.  Without this, a host
	// that crashes / loses network AFTER sending Welcome but BEFORE
	// the snapshot finishes leaves the joiner stuck in
	// "Downloading game from host…" indefinitely — the 25 s
	// handshake timeout no longer applies (we're past it), the
	// 15 s resync timeout doesn't cover the session-start
	// snapshot, and the 10-min Lockstepping peer-silence backstop
	// is far too long to be useful here.
	//
	// 30 s is comfortably longer than any plausible chunk
	// round-trip even on relay (typical: 200-400 ms; worst-case
	// retry budget per chunk after Item 2 bump: 5 s) but short
	// enough that a stalled host surfaces a clear error
	// promptly.  Stamped on ReceivingSnapshot entry and on each
	// successfully-accepted chunk.
	static constexpr uint64_t kSnapshotReceiveTimeoutMs = 30000;
	uint64_t mLastChunkRecvMs = 0;

	// One-shot guard so the DESYNC-detected log only fires once per
	// lockstep session rather than every tick until the resync handler
	// clears the flag.
	bool mDesyncLogged = false;

	// Cached per-subsystem hash breakdowns for the most recent frame
	// the glue submitted (local) and the most recent one we received
	// from the peer.  MaybeLogSimHashDiff() logs a DIFF only when both
	// are for the same frame; a second call with the same frame is
	// suppressed by mLastDiffLoggedFrame so a retransmit or a
	// re-submission of the same breakdown doesn't spam the log.
	NetSimHashDiag mLocalDiag{};
	NetSimHashDiag mPeerDiag{};
	bool     mLocalDiagSet = false;
	bool     mPeerDiagSet  = false;
	int64_t  mLastDiffLoggedFrame = -1;

	// v4 two-sided punch + relay fallback state ---------------------

	// (PeerPath enum is declared in the public section above.)
	PeerPath mPeerPath = PeerPath::Direct;

	bool    mHasRelaySessionId = false;
	uint8_t mRelaySessionId[16] = {};
	bool    mLobbyRelayKnown = false;
	Endpoint mLobbyRelayEndpoint;
	// Cached "host:port" string we last successfully resolved, used
	// by SetRelayContext to short-circuit per-tick re-entry without
	// re-running Transport::Resolve() or re-emitting a log line.
	std::string mLobbyHostPortCached;
	// Timestamp (Poll's nowMs) of the last successful ASGR send to the
	// lobby's RelayTable.  0 = never sent.  SendRelayRegister gates on
	// (now - mRelayRegisteredMs) >= kRelayRegisterIntervalMs so a
	// periodic refresh keeps the slot alive against the server's 30 s
	// idle-prune (kRelayPeerIdleMs).  Reset to 0 on every Direct↔Relay
	// transition, on Begin*, and on coordinator teardown.
	uint64_t mRelayRegisteredMs = 0;

	// Direct-rescue probe state.  All three fields are reset at every
	// site that flips mPeerPath to Relay (4 sites: MaybeEngageRelay
	// host+joiner, the auto-flip in Poll's recv loop, and
	// MaybeRescueRelayMidSession).  mRelayEngagedAtMs doubles as the
	// "was on relay X ms" timestamp for the recovery-success log.
	uint64_t mRelayEngagedAtMs    = 0;
	uint64_t mLastDirectProbeMs   = 0;
	uint32_t mDirectProbeAttempts = 0;
	// Stamped at every Relay → Direct flip; used by the auto-flip
	// block to suppress immediate flip-back on in-flight ASDF frames
	// the peer sent before its own rescue completed.
	uint64_t mLastFlipToDirectMs  = 0;

	// Flap counter: incremented at every Direct → Relay engagement.
	// When >= kMaxRelayEngagesBeforeGivingUp, mDirectGiveUp is set
	// and all Relay → Direct rescue paths (probe sender, NetPunch
	// rescue branch, passive-flip rule) are disabled for the
	// remainder of this Coordinator's lifetime.
	uint32_t mRelayEngageCount = 0;
	bool     mDirectGiveUp     = false;

	// Snapshot upload progress: last logged percentile milestone (0/1/2/3
	// = 0%/25%/50%/75%).  100% is covered by the existing "snapshot
	// delivered" log.  Reset at each snapshot-begin path.
	uint8_t  mSnapProgressMilestone = 0;

	// Monotonic wall-clock (Poll's nowMs) when the current handshake
	// phase started.  Used by the relay-fallback timer.  Set by
	// BeginJoin / BeginJoinMulti / BeginHost.  0 until first Poll.
	uint64_t mPhaseStartMs = 0;

	// Host-side: NetPunch targets learned from peer-hints.  We burst
	// a few probes immediately and then sustain one per target every
	// kPunchSustainIntervalMs for kPunchSustainDurationMs.  Deduped
	// on (nonce, endpoint); a repeated hint with the same nonce just
	// refreshes the entries.
	struct PunchTarget {
		uint8_t  nonce[kSessionNonceLen] = {};
		Endpoint ep;
		uint64_t firstSentMs = 0;
		uint64_t lastSentMs  = 0;
		int      sentCount   = 0;
	};
	std::vector<PunchTarget> mPunchTargets;
	// Persistent flag: at least one peer-hint has ever been delivered
	// during this WaitingForJoiner phase.  The relay-fallback gate
	// checks this instead of mPunchTargets-non-empty because punch
	// targets are pruned after the 4 s sustain window; without a
	// persistent flag the host would miss the 10 s relay trigger if
	// the hint happened to arrive slightly earlier than that.
	bool mHostHasSeenHint = false;

	// Persistent direct-rescue candidates — endpoint addresses we
	// believe the peer is reachable at directly.  Populated from:
	//   - Host: every peer-hint the lobby delivers (across all
	//     phases, not just WaitingForJoiner — the joiner's heartbeat
	//     keeps refreshing these and a NAT mapping that changed
	//     after the relay engaged is exactly what we need to retry).
	//   - Joiner: copied from mHelloCandidates at BeginJoinMulti
	//     (the host's announced lobby endpoints).
	// Used by MaybeProbeDirectFromRelay when PeerIsLobby() — i.e.,
	// in a relay-only session where mPeer points at the lobby and
	// is therefore not a viable probe target.  Never expires; we
	// dedupe on endpoint only (nonce is supplied by mSessionNonce
	// at probe time).
	std::vector<Endpoint> mDirectRescueCandidates;
	static constexpr int      kPunchBurstInitialCount = 5;
	static constexpr uint64_t kPunchSustainIntervalMs = 500;
	static constexpr uint64_t kPunchSustainDurationMs = 4000;
	// Two-stage relay-fallback timer (kept in sync with the canonical
	// definitions in lobby_protocol.h):
	//   T+kRelayPrearmAfterMs   — send one ASGR to prime the lobby
	//                             RelayTable while still spraying direct
	//                             probes.  No peer-path change.
	//   T+kRelayFallbackAfterMs — declare punch lost; flip mPeerPath to
	//                             Relay and start sending ASDF frames.
	// The pre-arm avoids paying an extra round-trip for ASGR at fallback
	// time — the table is already populated, so the first ASDF lands
	// immediately.  See lobby_protocol.h for the full rationale.
	static constexpr uint64_t kRelayPrearmAfterMs     = 3000;
	static constexpr uint64_t kRelayFallbackAfterMs   = 6000;
	static constexpr uint64_t kRelayFailTimeoutMs     = 25000;
	// Mid-session resilience: if a peer goes silent during Lockstepping
	// while we're still on a Direct path, the next router reboot or
	// transient NAT eviction will kill the session.  Re-arming the relay
	// (one extra ASGR + flip to Relay) at this threshold keeps the
	// session alive: the RelayTable's 30 s idle window covers a typical
	// connectivity hiccup.  Triggers ONLY in Lockstepping.
	static constexpr uint64_t kMidSessionRelayRescueAfterMs = 5000;

	// Mid-session direct rescue: while on Relay during Lockstepping,
	// periodically test whether direct UDP works again.  Schedule:
	// 10 fast probes at 1-second intervals after the relay flip, then
	// drop to 1 probe / 5 s.  Designed so total recovery time from
	// outage start to direct restored stays well under 10 s (5 s
	// peer-silence detection + 1 s first probe + ~1 s round-trip).
	static constexpr uint64_t kDirectProbeFastIntervalMs = 1000;
	static constexpr uint64_t kDirectProbeSlowIntervalMs = 5000;
	static constexpr uint32_t kDirectProbeFastAttempts   = 10;

	// Grace window after a Relay → Direct flip during which the auto-
	// flip-to-Relay path in the recv loop is suppressed.  Without this,
	// in-flight ASDF frames the peer sent before *its* own flip would
	// flap us straight back to Relay (and the peer's in-flight ASDF
	// from before our flip would do the same in reverse), spamming
	// "Direct connection restored" / "lost" toasts.  2 s is a safe
	// upper bound for lobby + UDP path latency on a normal home
	// connection; if peer is genuinely on Relay (e.g. asymmetric
	// direct path), legitimate new ASDF after 2 s correctly flips us
	// back.
	static constexpr uint64_t kFlipToRelayGraceMs = 2000;

	// Flap suppression: a marginal direct path with high packet loss
	// will repeatedly stall lockstep at 5 s (peer-silence threshold),
	// flip to Relay, recover to Direct, stall again — user sees a
	// "Direct lost" / "Direct restored" toast pair every 10–12 s
	// indefinitely.  After N Direct → Relay engagements in a session,
	// stop trying to recover to Direct: stay on Relay so gameplay is
	// stable, even if it costs a few ms of latency.  Operator can
	// manually reconnect to reset (new Coordinator → new counter).
	// 3 = tolerate 2 mid-session flaps after the initial engagement
	// (which on a session that fell back during handshake consumes
	// engagement #1).  Each Direct-Relay-Direct cycle takes ~12-14 s,
	// so the flap window is bounded at ~30 s before we settle into
	// stable relay mode for the remainder of the session.
	static constexpr uint32_t kMaxRelayEngagesBeforeGivingUp = 3;

	// Diagnostics: per-candidate inbound counters populated by Poll()
	// when drained packets are from one of our candidates (joiner
	// side).  Rendered in the structured FailWith message.
	struct CandidateStat {
		Endpoint ep;
		std::string display;     // as passed on the command line
		int rxPackets = 0;
		int rxRejects = 0;
	};
	std::vector<CandidateStat> mCandidateStats;
	int mPunchProbesReceived = 0;
	int mRelayFramesReceived = 0;

	// Stats book-keeping — increment helpers.
	void BumpCandidateRx(const Endpoint& from, bool isReject);

	// Send a packet to `peer`.  If the session has transitioned to
	// relay mode, the bytes are wrapped in a 24-byte 'ASDF' header
	// and dispatched to the lobby's reflector endpoint instead.  The
	// server strips the header and forwards inner bytes to the other
	// peer's registered srflx.
	bool WrapAndSend(const uint8_t* bytes, size_t n, const Endpoint& peer);
	// Send `bytes` to the lobby relay UNCONDITIONALLY wrapped in an
	// ASDF frame, regardless of mPeerPath.  Used by the relay-first
	// handshake path so the joiner can spray Hellos via the lobby
	// in parallel with the direct candidates from T=0, instead of
	// only doing so after MaybeEngageRelay flips mPeerPath at T+6s.
	bool SendWrappedViaLobby(const uint8_t* bytes, size_t n);

	// Role-appropriate relay register.  kRelayRoleHost for hosts,
	// kRelayRoleJoiner for joiners.  Periodic: each call sends a
	// fresh ASGR if the previous send was more than
	// kRelayRegisterIntervalMs ago (or this is the first send for
	// this Coordinator).  Otherwise no-op.  The periodic refresh
	// keeps the lobby's RelayTable slot alive across the 30 s idle
	// prune even when the host is waiting for joiners with no other
	// relay traffic to flow.
	void SendRelayRegister(uint64_t nowMs);

	// Send NetPunch probes to each target that is still inside its
	// sustain window.  Called from Poll().
	void PumpPunchProbes(uint64_t nowMs);

	// Two-stage relay fallback evaluator — called from Poll().
	//
	// MaybePrearmRelay: register with the lobby's RelayTable as soon as
	//   relay context is set.  Does NOT change mPeerPath — direct
	//   probes continue.  Re-sends every kRelayRegisterIntervalMs while
	//   in WaitingForJoiner / Handshaking / SendingSnapshot so the slot
	//   doesn't expire under the server's 30 s idle prune.  Skipped
	//   when mPeerPath == Relay (ordinary ASDF traffic refreshes the
	//   slot).
	// MaybeEngageRelay: at T+kRelayFallbackAfterMs, flip mPeerPath to
	//   Relay and fire the first wrapped Hello via lobby.  Calls
	//   SendRelayRegister defensively in case prearm was skipped.
	// MaybeRescueRelayMidSession: in Lockstepping, if mPeerPath==Direct
	//   and the peer has been silent past kMidSessionRelayRescueAfterMs,
	//   re-arm relay so the session survives a transient direct-path
	//   outage (router reboot, NAT eviction).
	// MaybeProbeDirectFromRelay: in Lockstepping, if mPeerPath==Relay,
	//   periodically send a NetPunch direct (NOT through the relay) to
	//   the peer's last-known address.  If the peer's matching kMagicPunch
	//   handler authenticates and echoes it back, both sides flip to
	//   Direct via the receive-loop branch.  See kDirectProbe* for the
	//   schedule.
	void MaybePrearmRelay(uint64_t nowMs);
	void MaybeEngageRelay(uint64_t nowMs);
	void MaybeRescueRelayMidSession(uint64_t nowMs);
	void MaybeProbeDirectFromRelay(uint64_t nowMs);

	// Stamp mRelayEngagedAtMs and reset the direct-rescue probe
	// counters.  Called from every site that flips mPeerPath to
	// Relay so a fresh probe schedule starts on each engagement.
	void OnPeerPathFlippedToRelay(uint64_t nowMs);

	// True when the only address we have for the peer IS the lobby
	// itself.  Happens when initial NAT punch failed entirely and
	// the joiner's Hello arrived via the lobby's ASDF-strip-and-
	// forward path, so HandleHelloOnHost set mPeer = from = lobby.
	// In that state every relay-routed peer packet looks like a
	// raw direct lockstep packet from the lobby IP, which would
	// trip the passive-flip rule, the kMagicPunch rescue, and the
	// direct-probe sender into "rescuing" against the lobby — a
	// no-op at best, broken (outbound dropped at lobby) at worst.
	// All three paths must therefore short-circuit when this is
	// true; the session stays on Relay for its lifetime.
	bool PeerIsLobby() const {
		return mLobbyRelayKnown && mPeerKnown
		    && mPeer.Equals(mLobbyRelayEndpoint);
	}

	// Diagnostics.
	const char* mLastError = "";
	std::string mLastErrorOwned;  // backing store for structured msgs

public:
	// Queries for the UI banner.
	uint32_t ResyncTotalChunks() const { return mSnapTx.TotalChunks(); }
	uint32_t ResyncAckedChunks() const { return mSnapTx.AcknowledgedChunks(); }
	uint32_t ResyncReceivedChunks() const { return mSnapRx.ReceivedChunks(); }
	uint32_t ResyncExpectedChunks() const { return mSnapRx.ExpectedChunks(); }
	uint32_t ResyncEpoch() const { return mResyncEpoch; }
};

} // namespace ATNetplay
