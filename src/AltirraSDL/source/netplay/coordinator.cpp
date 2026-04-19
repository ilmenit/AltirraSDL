// Altirra SDL3 netplay - session coordinator (impl)

#include <stdafx.h>

#include "coordinator.h"

#include "protocol.h"

#include <cstring>

namespace ATNetplay {

namespace {

bool EntryCodeMatches(const uint8_t* a, const uint8_t* b) {
	// Constant-time compare would be nice against malicious peers,
	// but the code isn't a secret — it's a convenience gate, not a
	// crypto token.  Plain memcmp is fine.
	return std::memcmp(a, b, kEntryCodeHashLen) == 0;
}

bool IsZeroEntryCode(const uint8_t* h) {
	for (size_t i = 0; i < kEntryCodeHashLen; ++i)
		if (h[i] != 0) return false;
	return true;
}

} // anonymous

Coordinator::Coordinator() = default;
Coordinator::~Coordinator() {
	if (mPhase == Phase::Lockstepping ||
	    mPhase == Phase::SendingSnapshot ||
	    mPhase == Phase::ReceivingSnapshot ||
	    mPhase == Phase::SnapshotReady) {
		// Best-effort farewell if the app forgot to End() us.
		SendBye(kByeCleanExit);
	}
}

// ---------------------------------------------------------------------------
// Host entrypoint
// ---------------------------------------------------------------------------

bool Coordinator::BeginHost(uint16_t localPort,
                            const char* playerHandle,
                            const char* cartName,
                            uint64_t osRomHash,
                            uint64_t basicRomHash,
                            uint64_t settingsHash,
                            uint16_t inputDelayFrames,
                            const uint8_t* entryCodeHash) {
	if (!mTransport.Listen(localPort)) {
		FailWith("failed to bind UDP socket");
		return false;
	}

	// Record our configuration to populate NetWelcome later.
	HandleFromString(playerHandle, reinterpret_cast<uint8_t*>(mPlayerHandle));
	CartFromString(cartName, reinterpret_cast<uint8_t*>(mCartName));
	mOsRomHash = osRomHash;
	mBasicRomHash = basicRomHash;
	mSettingsHash = settingsHash;
	mInputDelay = inputDelayFrames;
	if (entryCodeHash) {
		std::memcpy(mEntryCodeHash, entryCodeHash, kEntryCodeHashLen);
		mHasEntryCode = !IsZeroEntryCode(mEntryCodeHash);
	} else {
		std::memset(mEntryCodeHash, 0, kEntryCodeHashLen);
		mHasEntryCode = false;
	}

	mRole = Role::Host;
	mPhase = Phase::WaitingForJoiner;
	mLastError = "";
	return true;
}

// ---------------------------------------------------------------------------
// Joiner entrypoint
// ---------------------------------------------------------------------------

bool Coordinator::BeginJoin(const char* hostAddress,
                            const char* playerHandle,
                            uint64_t osRomHash,
                            uint64_t basicRomHash,
                            bool acceptTos,
                            const uint8_t* entryCodeHash) {
	// Bind ephemeral local port.
	if (!mTransport.Listen(0)) {
		FailWith("failed to bind UDP socket");
		return false;
	}
	if (!Transport::Resolve(hostAddress, mPeer)) {
		FailWith("failed to resolve host address");
		mTransport.Close();
		return false;
	}
	mPeerKnown = true;

	HandleFromString(playerHandle, reinterpret_cast<uint8_t*>(mPlayerHandle));
	mOsRomHash = osRomHash;
	mBasicRomHash = basicRomHash;
	mAcceptTos = acceptTos;
	if (entryCodeHash) {
		std::memcpy(mEntryCodeHash, entryCodeHash, kEntryCodeHashLen);
		mHasEntryCode = !IsZeroEntryCode(mEntryCodeHash);
	} else {
		std::memset(mEntryCodeHash, 0, kEntryCodeHashLen);
		mHasEntryCode = false;
	}

	mRole = Role::Joiner;
	mPhase = Phase::Handshaking;
	mLastError = "";

	SendHello();
	return true;
}

// ---------------------------------------------------------------------------
// Poll
// ---------------------------------------------------------------------------

void Coordinator::Poll(uint64_t nowMs) {
	if (mPhase == Phase::Idle ||
	    mPhase == Phase::Ended ||
	    mPhase == Phase::Failed ||
	    mPhase == Phase::Desynced) {
		return;
	}

	// Drain the socket.
	for (int drained = 0; drained < 64; ++drained) {
		size_t n = 0;
		Endpoint from;
		RecvResult r = mTransport.RecvFrom(mRxBuf, sizeof mRxBuf, n, from);
		if (r == RecvResult::WouldBlock) break;
		if (r == RecvResult::Error)      break;

		uint32_t magic = 0;
		if (!PeekMagic(mRxBuf, n, magic)) continue;

		switch (magic) {
			case kMagicHello: {
				NetHello h;
				if (DecodeHello(mRxBuf, n, h) != DecodeResult::Ok) continue;
				HandleHelloFromJoiner(h, from, nowMs);
				break;
			}
			case kMagicWelcome: {
				NetWelcome w;
				if (DecodeWelcome(mRxBuf, n, w) != DecodeResult::Ok) continue;
				HandleWelcomeFromHost(w, nowMs);
				break;
			}
			case kMagicReject: {
				NetReject rj;
				if (DecodeReject(mRxBuf, n, rj) != DecodeResult::Ok) continue;
				HandleRejectFromHost(rj);
				break;
			}
			case kMagicInput: {
				NetInputPacket ip;
				if (DecodeInputPacket(mRxBuf, n, ip) != DecodeResult::Ok) continue;
				HandleInputPacket(ip, nowMs);
				break;
			}
			case kMagicChunk: {
				NetSnapChunk c;
				if (DecodeSnapChunk(mRxBuf, n, c) != DecodeResult::Ok) continue;
				HandleSnapChunk(c, nowMs);
				break;
			}
			case kMagicAck: {
				NetSnapAck a;
				if (DecodeSnapAck(mRxBuf, n, a) != DecodeResult::Ok) continue;
				HandleSnapAck(a);
				break;
			}
			case kMagicBye: {
				NetBye b;
				if (DecodeBye(mRxBuf, n, b) != DecodeResult::Ok) continue;
				HandleBye(b);
				break;
			}
			default:
				// Unknown magic — silently ignore (could be from a
				// different version or a stray packet).
				break;
		}
	}

	// Outgoing pumps.
	if (mPhase == Phase::SendingSnapshot) {
		PumpSnapshotSender(nowMs);
	}
	if (mPhase == Phase::Lockstepping && mWantsOutgoingInput) {
		PumpLockstepSend();
		mWantsOutgoingInput = false;
	}
	// Lockstep: detect desync transition.
	if (mPhase == Phase::Lockstepping && mLoop.IsDesynced()) {
		SendBye(kByeDesyncDetected);
		mPhase = Phase::Desynced;
	}
}

// ---------------------------------------------------------------------------
// Host: handle joiner Hello
// ---------------------------------------------------------------------------

void Coordinator::HandleHelloFromJoiner(const NetHello& hello,
                                        const Endpoint& from,
                                        uint64_t /*nowMs*/) {
	if (mPhase != Phase::WaitingForJoiner) {
		// Already handshaking with someone — ignore duplicate Hellos
		// from strangers.  A future v1.x could send a "host full"
		// reject; for v1.0 silence is fine.
		return;
	}

	if (hello.protocolVersion != kProtocolVersion) {
		SendReject(kRejectVersionSkew, from);
		return;
	}
	if (hello.osRomHash != mOsRomHash) {
		SendReject(kRejectOsMismatch, from);
		return;
	}
	if (hello.basicRomHash != mBasicRomHash) {
		SendReject(kRejectBasicMismatch, from);
		return;
	}
	if ((hello.acceptTos & 1u) == 0) {
		SendReject(kRejectTosNotAccept, from);
		return;
	}
	if (mHasEntryCode) {
		if (!EntryCodeMatches(hello.entryCodeHash, mEntryCodeHash)) {
			SendReject(kRejectBadEntryCode, from);
			return;
		}
	}

	// Hello accepted.  Lock in the peer.
	mPeer = from;
	mPeerKnown = true;

	if (mSnapTxBuffer.empty()) {
		// The app hasn't uploaded the snapshot yet.  Defer sending
		// Welcome until SubmitSnapshotForUpload() is called.
		mHostHasPendingJoiner = true;
		mPendingJoiner = from;
		// Advance phase so SubmitSnapshotForUpload knows we're
		// expecting it.
		mPhase = Phase::SendingSnapshot;
		return;
	}

	// Snapshot already submitted (app was proactive).  Welcome now.
	mPhase = Phase::SendingSnapshot;
	SendWelcome();
	mSnapTx.Begin(mSnapTxBuffer.data(), mSnapTxBuffer.size());
}

// ---------------------------------------------------------------------------
// SubmitSnapshotForUpload
// ---------------------------------------------------------------------------

void Coordinator::SubmitSnapshotForUpload(const uint8_t* data, size_t len) {
	if (mRole != Role::Host) return;
	mSnapTxBuffer.assign(data, data + len);

	if (mHostHasPendingJoiner) {
		mHostHasPendingJoiner = false;
		mPhase = Phase::SendingSnapshot;
		SendWelcome();
		mSnapTx.Begin(mSnapTxBuffer.data(), mSnapTxBuffer.size());
	}
	// If no joiner yet, bytes sit in mSnapTxBuffer until one arrives.
}

// ---------------------------------------------------------------------------
// Joiner: handle Welcome
// ---------------------------------------------------------------------------

void Coordinator::HandleWelcomeFromHost(const NetWelcome& w, uint64_t /*nowMs*/) {
	if (mPhase != Phase::Handshaking) return;

	// Adopt host-chosen parameters.
	mInputDelay = w.inputDelayFrames;
	std::memcpy(mCartName, w.cartName, kCartLen);
	mSettingsHash = w.settingsHash;

	// Prepare receiver.
	mSnapRx.Begin(w.snapshotChunks, w.snapshotBytes);
	mPhase = Phase::ReceivingSnapshot;

	// If the snapshot is zero-length (edge case: nothing to ship),
	// move straight to SnapshotReady.
	if (w.snapshotChunks == 0) {
		mPhase = Phase::SnapshotReady;
	}
}

void Coordinator::HandleRejectFromHost(const NetReject& r) {
	if (mPhase != Phase::Handshaking) return;
	mPhase = Phase::Failed;
	switch (r.reason) {
		case kRejectOsMismatch:     mLastError = "host rejected: OS ROM mismatch"; break;
		case kRejectBasicMismatch:  mLastError = "host rejected: BASIC ROM mismatch"; break;
		case kRejectVersionSkew:    mLastError = "host rejected: protocol version mismatch"; break;
		case kRejectTosNotAccept:   mLastError = "host rejected: terms of service not accepted"; break;
		case kRejectHostFull:       mLastError = "host rejected: host full"; break;
		case kRejectHostNotReady:   mLastError = "host rejected: host not ready"; break;
		case kRejectBadEntryCode:   mLastError = "host rejected: wrong entry code"; break;
		case kRejectHostRejected:   mLastError = "host rejected: manual decline"; break;
		default:                    mLastError = "host rejected: unknown reason"; break;
	}
}

// ---------------------------------------------------------------------------
// Snapshot chunking (bidirectional)
// ---------------------------------------------------------------------------

void Coordinator::HandleSnapChunk(const NetSnapChunk& c, uint64_t /*nowMs*/) {
	if (mPhase != Phase::ReceivingSnapshot) return;

	if (mSnapRx.OnChunk(c)) {
		NetSnapAck ack { kMagicAck, c.chunkIdx };
		size_t n = EncodeSnapAck(ack, mTxBuf, sizeof mTxBuf);
		if (n && mPeerKnown) mTransport.SendTo(mTxBuf, n, mPeer);
	}

	if (mSnapRx.IsComplete()) {
		mPhase = Phase::SnapshotReady;
	}
}

void Coordinator::HandleSnapAck(const NetSnapAck& a) {
	if (mPhase != Phase::SendingSnapshot) return;
	mSnapTx.OnAckReceived(a.chunkIdx);
	if (mSnapTx.GetStatus() == SnapshotSender::Status::Done) {
		// Host is ready for lockstep immediately; joiner transitions
		// to Lockstepping when it calls AcknowledgeSnapshotApplied.
		mLoop.Begin(Slot::Host, mInputDelay);
		mPhase = Phase::Lockstepping;
	} else if (mSnapTx.GetStatus() == SnapshotSender::Status::Failed) {
		FailWith("snapshot upload failed (retry budget exhausted)");
	}
}

void Coordinator::PumpSnapshotSender(uint64_t nowMs) {
	if (!mPeerKnown) return;
	NetSnapChunk c;
	while (mSnapTx.NextOutgoing(c, nowMs)) {
		size_t n = EncodeSnapChunk(c, mTxBuf, sizeof mTxBuf);
		if (n == 0) break;
		mTransport.SendTo(mTxBuf, n, mPeer);
		// Emit ONE packet per Poll() to avoid flooding — the retry
		// timer (500 ms) paces subsequent re-sends naturally.  We
		// need to break after the first emission to match that
		// pacing intent; without this, NextOutgoing would re-fire
		// for the same chunk on the very next iteration if nowMs
		// increments within the loop's view (it doesn't, but the
		// intent is clearer with an explicit break).
		break;
	}
}

// ---------------------------------------------------------------------------
// AcknowledgeSnapshotApplied (joiner)
// ---------------------------------------------------------------------------

void Coordinator::AcknowledgeSnapshotApplied() {
	if (mPhase != Phase::SnapshotReady) return;
	mLoop.Begin(Slot::Joiner, mInputDelay);
	mPhase = Phase::Lockstepping;
}

// ---------------------------------------------------------------------------
// Lockstep hot path
// ---------------------------------------------------------------------------

void Coordinator::HandleInputPacket(const NetInputPacket& pkt, uint64_t nowMs) {
	if (mPhase != Phase::Lockstepping) return;
	mLoop.OnPeerInputPacket(pkt, nowMs);
}

void Coordinator::OnFrameAdvanced() {
	if (mPhase != Phase::Lockstepping) return;
	mLoop.OnFrameAdvanced();
	mWantsOutgoingInput = true;  // send our state to the peer this Poll
}

void Coordinator::PumpLockstepSend() {
	if (!mPeerKnown) return;
	NetInputPacket pkt;
	mLoop.BuildOutgoingInputPacket(pkt);
	size_t n = EncodeInputPacket(pkt, mTxBuf, sizeof mTxBuf);
	if (n) mTransport.SendTo(mTxBuf, n, mPeer);
}

// ---------------------------------------------------------------------------
// Bye / termination
// ---------------------------------------------------------------------------

void Coordinator::HandleBye(const NetBye& b) {
	// Peer said goodbye; treat as clean exit unless they flagged
	// desync.
	if (b.reason == kByeDesyncDetected && mPhase == Phase::Lockstepping) {
		mPhase = Phase::Desynced;
		mLastError = "peer reported desync";
	} else {
		mPhase = Phase::Ended;
	}
}

void Coordinator::End(uint32_t byeReason) {
	if (mPhase == Phase::Idle || mPhase == Phase::Ended || mPhase == Phase::Failed)
		return;
	SendBye(byeReason);
	mPhase = Phase::Ended;
}

void Coordinator::SendBye(uint32_t reason) {
	if (!mPeerKnown) return;
	NetBye b { kMagicBye, reason };
	size_t n = EncodeBye(b, mTxBuf, sizeof mTxBuf);
	if (n) mTransport.SendTo(mTxBuf, n, mPeer);
}

// ---------------------------------------------------------------------------
// Packet senders
// ---------------------------------------------------------------------------

void Coordinator::SendHello() {
	NetHello h;
	h.magic = kMagicHello;
	h.protocolVersion = kProtocolVersion;
	h.flags = 0;
	// sessionNonce: all zero for v1.0 — the field is reserved for
	// future control-packet auth.  Leaving it zero costs nothing now
	// and mirrors the Go PoC.
	h.osRomHash = mOsRomHash;
	h.basicRomHash = mBasicRomHash;
	std::memcpy(h.playerHandle, mPlayerHandle, kHandleLen);
	h.acceptTos = mAcceptTos ? 1u : 0u;
	std::memcpy(h.entryCodeHash, mEntryCodeHash, kEntryCodeHashLen);

	size_t n = EncodeHello(h, mTxBuf, sizeof mTxBuf);
	if (n && mPeerKnown) mTransport.SendTo(mTxBuf, n, mPeer);
}

void Coordinator::SendWelcome() {
	NetWelcome w;
	w.magic = kMagicWelcome;
	w.inputDelayFrames = mInputDelay;
	w.playerSlot = 2;    // first joiner is always P2
	std::memcpy(w.cartName, mCartName, kCartLen);
	w.snapshotBytes = (uint32_t)mSnapTxBuffer.size();
	w.snapshotChunks =
		(uint32_t)((mSnapTxBuffer.size() + kSnapshotChunkSize - 1)
			/ kSnapshotChunkSize);
	w.settingsHash = mSettingsHash;

	size_t n = EncodeWelcome(w, mTxBuf, sizeof mTxBuf);
	if (n && mPeerKnown) mTransport.SendTo(mTxBuf, n, mPeer);
}

void Coordinator::SendReject(uint32_t reason, const Endpoint& to) {
	NetReject r { kMagicReject, reason };
	size_t n = EncodeReject(r, mTxBuf, sizeof mTxBuf);
	if (n) mTransport.SendTo(mTxBuf, n, to);
}

// ---------------------------------------------------------------------------
// Misc
// ---------------------------------------------------------------------------

void Coordinator::FailWith(const char* msg) {
	mLastError = msg;
	mPhase = Phase::Failed;
}

} // namespace ATNetplay
