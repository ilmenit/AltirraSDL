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

	// One-shot guard so the DESYNC-detected log only fires once per
	// lockstep session rather than every tick until the resync handler
	// clears the flag.
	bool mDesyncLogged = false;

	// Diagnostics.
	const char* mLastError = "";

public:
	// Queries for the UI banner.
	uint32_t ResyncTotalChunks() const { return mSnapTx.TotalChunks(); }
	uint32_t ResyncAckedChunks() const { return mSnapTx.AcknowledgedChunks(); }
	uint32_t ResyncReceivedChunks() const { return mSnapRx.ReceivedChunks(); }
	uint32_t ResyncExpectedChunks() const { return mSnapRx.ExpectedChunks(); }
	uint32_t ResyncEpoch() const { return mResyncEpoch; }
};

} // namespace ATNetplay
