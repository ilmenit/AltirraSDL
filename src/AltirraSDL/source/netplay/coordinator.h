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
		Ended,                  // clean Bye or app-initiated End
		Desynced,               // hash mismatch
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
	               const uint8_t* entryCodeHash /* 16 bytes or nullptr */);

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
	void AcknowledgeSnapshotApplied();

	// ---- lockstep frame API (valid only in Lockstepping) -----------------

	void SubmitLocalInput(const NetInput& in) { mLoop.SubmitLocalInput(in); }
	bool CanAdvance() const { return mLoop.CanAdvance(); }
	bool GetInputsForCurrentFrame(NetInput& p1, NetInput& p2) const {
		return mLoop.GetInputsForCurrentFrame(p1, p2);
	}
	void OnFrameAdvanced();

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
	void HandleBye(const NetBye& b);

	void SendHello();
	void SendWelcome();
	void SendReject(uint32_t reason, const Endpoint& to);
	void SendBye(uint32_t reason);
	void PumpSnapshotSender(uint64_t nowMs);
	void PumpLockstepSend();

	// Transport.
	Transport mTransport;
	Endpoint  mPeer;                 // set once we learn the peer's addr
	bool      mPeerKnown = false;

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

	// Host pending-welcome: a joiner Hello arrived, but
	// SubmitSnapshotForUpload() hasn't been called yet.  We hold
	// enough info to send the Welcome once bytes arrive.
	bool     mHostHasPendingJoiner = false;
	Endpoint mPendingJoiner;
	uint8_t  mPendingJoinerEntryCode[kEntryCodeHashLen] = {};

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

	// Diagnostics.
	const char* mLastError = "";
};

} // namespace ATNetplay
