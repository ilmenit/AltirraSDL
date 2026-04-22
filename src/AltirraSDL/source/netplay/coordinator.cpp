// Altirra SDL3 netplay - session coordinator (impl)

#include <stdafx.h>

#include "coordinator.h"

#include "protocol.h"

#include <at/atcore/logging.h>

#include <cstring>

// Single shared channel for every netplay subsystem.  Default ON
// (unlike most Altirra log channels) so users can copy diagnostics
// straight from the terminal when a session misbehaves.  Kept
// deliberately sparse: one line per state transition, none per frame.
ATLogChannel g_ATLCNetplay(true, true, "NETPLAY", "Netplay session");

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
                            const uint8_t* entryCodeHash,
                            const NetBootConfig& bootConfig) {
	mBootConfig = bootConfig;
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
	g_ATLCNetplay("host: listening on UDP port %u (cart=\"%s\" private=%s delay=%u)",
		(unsigned)mTransport.BoundPort(),
		cartName ? cartName : "(none)",
		mHasEntryCode ? "yes" : "no",
		(unsigned)mInputDelay);
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

	g_ATLCNetplay("joiner: resolved \"%s\", sending Hello", hostAddress);
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
				HandleBye(b, from);
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
		g_ATLCNetplay("DESYNC detected at frame %lld (role=%s, delay=%u)",
			(long long)mLoop.DesyncFrame(),
			mRole == Role::Host ? "host" : "joiner",
			(unsigned)mInputDelay);
		SendBye(kByeDesyncDetected);
		mPhase = Phase::Desynced;
	}

	// Lockstep: peer-silence timeout.  With the lockstep design the
	// game naturally pauses on both sides when peer packets stop
	// arriving (our gate won't open), so there's no crash — but if
	// the peer genuinely died or dropped off the network we'd otherwise
	// sit frozen forever.  The UI now shows an interactive "peer
	// unresponsive" prompt at ~3 s; this long backstop only fires if
	// the user dismissed the prompt and then walked away.  Auto-
	// termination without user consent is explicitly unwanted.
	if (mPhase == Phase::Lockstepping) {
		constexpr uint64_t kPeerTimeoutMs = 600000;  // 10 minutes
		if (mLoop.PeerTimedOut(nowMs, kPeerTimeoutMs)) {
			g_ATLCNetplay("%s: peer silent for >%llu ms — timing out "
				"(last packet %llu ms ago)",
				mRole == Role::Host ? "host" : "joiner",
				(unsigned long long)kPeerTimeoutMs,
				(unsigned long long)(nowMs - mLoop.LastPeerRecvMs()));
			SendBye(kByeTimeout);
			mPhase = Phase::Ended;
			mLastError = "peer timed out";
		}
	}
}

// ---------------------------------------------------------------------------
// Host: handle joiner Hello
// ---------------------------------------------------------------------------

void Coordinator::HandleHelloFromJoiner(const NetHello& hello,
                                        const Endpoint& from,
                                        uint64_t nowMs) {
	if (mPhase != Phase::WaitingForJoiner) {
		// Host is already past the listening phase (handshake in
		// progress, snapshot streaming, or in lockstep).  Tell the
		// joiner we're full so they get a clear message instead of
		// hanging in "Connecting…" until their handshake timeout.
		SendReject(kRejectHostFull, from);
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

	// Hello passed validation.  If prompt-accept is enabled, enqueue
	// the joiner and wait for the UI to call AcceptPendingJoiner(i)
	// or RejectPendingJoiner(i).  Duplicate Hellos from the same
	// endpoint (the joiner retransmits every ~500 ms while waiting)
	// refresh the arrival timestamp and nothing else.  We stay in
	// WaitingForJoiner until an Accept picks one entry, so a Reject on
	// any single row can return to listening cleanly.
	if (mPromptAccept) {
		for (auto& e : mPendingDecisions) {
			if (e.peer.Equals(from)) {
				// Refresh arrivedMs (we keep the oldest for the UI
				// "Requested Xs ago" display) — actually keep the
				// first arrival time; retransmits are not new requests.
				return;
			}
		}
		if (mPendingDecisions.size() >= kMaxPendingDecisions) {
			// Queue is full.  Bounce the joiner with HostFull so they
			// get a clear error instead of a silent timeout.
			SendReject(kRejectHostFull, from);
			return;
		}
		PendingDecision pd;
		pd.peer = from;
		std::memset(pd.handle, 0, sizeof pd.handle);
		std::memcpy(pd.handle, hello.playerHandle, kHandleLen);
		pd.arrivedMs = nowMs;
		mPendingDecisions.push_back(pd);
		char ep[32] = {};
		from.Format(ep, sizeof ep);
		g_ATLCNetplay("host: incoming join from \"%s\" at %s — "
			"awaiting host decision (queue depth %zu)",
			pd.handle, ep, mPendingDecisions.size());
		return;
	}

	// Hello accepted.  Lock in the peer.
	mPeer = from;
	mPeerKnown = true;
	{
		char handle[kHandleLen + 1] = {};
		std::memcpy(handle, hello.playerHandle, kHandleLen);
		char ep[32] = {};
		from.Format(ep, sizeof ep);
		g_ATLCNetplay("host: accepted joiner \"%s\" from %s",
			handle, ep);
	}

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
// Prompt-accept gate
// ---------------------------------------------------------------------------

const char* Coordinator::PendingJoinerHandle(size_t i) const {
	if (i >= mPendingDecisions.size()) return "";
	return mPendingDecisions[i].handle;
}

uint64_t Coordinator::PendingArrivedMs(size_t i) const {
	if (i >= mPendingDecisions.size()) return 0;
	return mPendingDecisions[i].arrivedMs;
}

void Coordinator::AcceptPendingJoiner(size_t i) {
	if (i >= mPendingDecisions.size()) return;
	if (mPhase != Phase::WaitingForJoiner) {
		// Phase moved out from under us (timeout, End, etc.).  Drop
		// the queue silently.
		mPendingDecisions.clear();
		return;
	}

	// Tell every other queued joiner that the host chose someone else
	// so they stop waiting.  Use kRejectHostFull to match the message
	// they would have got if they'd arrived after a silent auto-accept.
	for (size_t k = 0; k < mPendingDecisions.size(); ++k) {
		if (k == i) continue;
		SendReject(kRejectHostFull, mPendingDecisions[k].peer);
	}

	PendingDecision chosen = mPendingDecisions[i];
	mPendingDecisions.clear();

	// Adopt the accepted peer and run the same path the auto-accept
	// branch used to (see HandleHelloFromJoiner).
	mPeer      = chosen.peer;
	mPeerKnown = true;

	char ep[32] = {};
	mPeer.Format(ep, sizeof ep);
	g_ATLCNetplay("host: accepted joiner \"%s\" from %s (manual approval)",
		chosen.handle, ep);

	if (mSnapTxBuffer.empty()) {
		mHostHasPendingJoiner = true;
		mPendingJoiner = mPeer;
		mPhase = Phase::SendingSnapshot;
		return;
	}

	mPhase = Phase::SendingSnapshot;
	SendWelcome();
	mSnapTx.Begin(mSnapTxBuffer.data(), mSnapTxBuffer.size());
}

void Coordinator::RejectPendingJoiner(size_t i) {
	if (i >= mPendingDecisions.size()) return;
	const PendingDecision& pd = mPendingDecisions[i];
	g_ATLCNetplay("host: rejected joiner \"%s\" (manual decline)",
		pd.handle);
	SendReject(kRejectHostRejected, pd.peer);
	mPendingDecisions.erase(mPendingDecisions.begin() + i);
	// Stay in WaitingForJoiner — another entry may still be queued and
	// new peers can still arrive.
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
	mBootConfig = w.boot;

	// Prepare receiver.
	mSnapRx.Begin(w.snapshotChunks, w.snapshotBytes);
	mPhase = Phase::ReceivingSnapshot;
	g_ATLCNetplay("joiner: Welcome accepted (snapshot=%u bytes / %u chunks, delay=%u)",
		(unsigned)w.snapshotBytes, (unsigned)w.snapshotChunks,
		(unsigned)w.inputDelayFrames);

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
		case kRejectOsMismatch:     mLastError = "OS ROM does not match the host's. Install the matching firmware and try again."; break;
		case kRejectBasicMismatch:  mLastError = "BASIC ROM does not match the host's. Install the matching firmware and try again."; break;
		case kRejectVersionSkew:    mLastError = "Protocol version mismatch — your Altirra build is incompatible with the host's."; break;
		case kRejectTosNotAccept:   mLastError = "Host requires terms-of-service acceptance."; break;
		case kRejectHostFull:       mLastError = "The host chose another player, or is handling too many requests right now."; break;
		case kRejectHostNotReady:   mLastError = "Host is not ready to accept joiners yet."; break;
		case kRejectBadEntryCode:   mLastError = "Incorrect join code. Ask the host for the right code and try again."; break;
		case kRejectHostRejected:   mLastError = "The host declined your request to join."; break;
		default:                    mLastError = "Host rejected the connection (unknown reason)."; break;
	}
	g_ATLCNetplay("joiner: %s (reason code %u)", mLastError, (unsigned)r.reason);
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
		g_ATLCNetplay("joiner: snapshot download complete, applying…");
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
		g_ATLCNetplay("host: snapshot delivered, entering Lockstepping (delay=%u frames)",
			(unsigned)mInputDelay);
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
	g_ATLCNetplay("joiner: snapshot applied, entering Lockstepping (delay=%u frames)",
		(unsigned)mInputDelay);
}

// ---------------------------------------------------------------------------
// Lockstep hot path
// ---------------------------------------------------------------------------

void Coordinator::HandleInputPacket(const NetInputPacket& pkt, uint64_t nowMs) {
	if (mPhase != Phase::Lockstepping) return;
	mLoop.OnPeerInputPacket(pkt, nowMs);
}

void Coordinator::OnFrameAdvanced(uint32_t simStateHash) {
	if (mPhase != Phase::Lockstepping) return;
	mLoop.OnFrameAdvanced(simStateHash);
	// Send the outgoing packet IMMEDIATELY rather than deferring it
	// to the next Poll().  The peer is likely already stalled waiting
	// for this frame's input — deferring would cost them a full render
	// + vsync cycle (~16 ms) before our packet is transmitted, which
	// halves effective lockstep throughput on localhost and high-RTT
	// links alike.  PumpLockstepSend is cheap (one encode + sendto).
	PumpLockstepSend();
	mWantsOutgoingInput = false;
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

void Coordinator::HandleBye(const NetBye& b, const Endpoint& from) {
	// Host-side special case: a Bye from a joiner that's only *queued*
	// (not yet the locked-in peer) means that joiner cancelled before
	// we got around to accepting them.  Just drop them from the queue —
	// do not end the listening phase.  Without this the host's whole
	// offer would collapse the moment any queued peer gave up waiting.
	if (mRole == Role::Host && mPhase == Phase::WaitingForJoiner) {
		for (size_t k = 0; k < mPendingDecisions.size(); ++k) {
			if (mPendingDecisions[k].peer.Equals(from)) {
				char ep[32] = {};
				from.Format(ep, sizeof ep);
				g_ATLCNetplay("host: queued joiner \"%s\" at %s cancelled "
					"before decision",
					mPendingDecisions[k].handle, ep);
				mPendingDecisions.erase(mPendingDecisions.begin() + k);
				return;
			}
		}
		// Bye from a stranger while listening — ignore.
		return;
	}

	// Peer said goodbye; treat as clean exit unless they flagged
	// desync.
	const char *reasonStr = "clean";
	switch (b.reason) {
		case kByeCleanExit:       reasonStr = "clean";         break;
		case kByeDesyncDetected:  reasonStr = "desync";        break;
		case kByeTimeout:         reasonStr = "timeout";       break;
		case kByeVersionMismatch: reasonStr = "ver-mismatch";  break;
		default:                  reasonStr = "unknown";       break;
	}
	g_ATLCNetplay("%s: peer said Bye (%s)",
		mRole == Role::Host ? "host" : "joiner", reasonStr);
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
	// Tell every still-queued joiner that we're not coming — without
	// this they sit in "Connecting…" until their own handshake timeout
	// fires.  Use kRejectHostFull; the session is ending from under
	// them, not a per-peer rejection.
	for (const auto& pd : mPendingDecisions) {
		SendReject(kRejectHostFull, pd.peer);
	}
	mPendingDecisions.clear();
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
	w.boot = mBootConfig;

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
	g_ATLCNetplay("%s session failed: %s",
		mRole == Role::Host ? "host" :
		mRole == Role::Joiner ? "joiner" : "idle",
		msg ? msg : "(no reason)");
}

} // namespace ATNetplay
