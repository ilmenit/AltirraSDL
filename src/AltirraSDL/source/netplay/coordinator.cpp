// Altirra SDL3 netplay - session coordinator (impl)

#include <stdafx.h>

#include "coordinator.h"

#include "protocol.h"
#include "netplay_profile.h"

#include <at/atcore/logging.h>

#include <cstdio>
#include <cstring>
#include <random>

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
	mPhaseStartMs = 0;  // set on first Poll
	mPunchTargets.clear();
	mHostHasSeenHint = false;
	mDirectRescueCandidates.clear();
	mPeerPath = PeerPath::Direct;
	mRelayRegistered = false;
	mPunchProbesReceived = 0;
	mRelayFramesReceived = 0;
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
	mHaveLastRejectReason = false;
	mLastRejectReason = 0;
	mPhaseStartMs = 0;
	mPeerPath = PeerPath::Direct;
	mRelayRegistered = false;
	mCandidateStats.clear();
	mPunchProbesReceived = 0;
	mRelayFramesReceived = 0;
	{
		CandidateStat cs;
		cs.ep = mPeer;
		cs.display = hostAddress ? hostAddress : "";
		mCandidateStats.push_back(std::move(cs));
	}

	// Fresh nonce for this attempt — even the single-candidate path
	// uses it so the host's new dedupe logic doesn't have to special-
	// case legacy callers within our own binary.
	GenerateSessionNonce();

	g_ATLCNetplay("joiner: resolved \"%s\", sending Hello", hostAddress);
	SendHello();
	return true;
}

bool Coordinator::BeginJoinMulti(const char* hostCandidatesSemicolonList,
                                 uint16_t localPort,
                                 const char* playerHandle,
                                 uint64_t osRomHash,
                                 uint64_t basicRomHash,
                                 bool acceptTos,
                                 const uint8_t* entryCodeHash) {
	if (!mTransport.Listen(localPort)) {
		FailWith("failed to bind UDP socket");
		return false;
	}

	// Parse semicolon-separated candidates, resolve each.  Skip any
	// that don't parse / resolve — a joiner only needs ONE working
	// candidate for the hole-punch spray to succeed.
	mHelloCandidates.clear();
	mCandidateStats.clear();
	const char* s = hostCandidatesSemicolonList ? hostCandidatesSemicolonList : "";
	std::string cur;
	auto flush = [&](const std::string& tok) {
		if (tok.empty()) return;
		Endpoint ep;
		if (Transport::Resolve(tok.c_str(), ep)) {
			// Deduplicate: mobile/loopback + lan often coincide when the
			// probe can't reach the outside world.
			for (const auto& existing : mHelloCandidates) {
				if (existing.Equals(ep)) return;
			}
			mHelloCandidates.push_back(ep);
			CandidateStat cs;
			cs.ep = ep;
			cs.display = tok;
			mCandidateStats.push_back(std::move(cs));
			char fmt[64];
			ep.Format(fmt, sizeof fmt);
			g_ATLCNetplay("joiner candidate: %s -> %s",
				tok.c_str(), fmt);
		} else {
			g_ATLCNetplay("joiner candidate unresolved: \"%s\"",
				tok.c_str());
		}
	};
	for (const char* p = s; *p; ++p) {
		if (*p == ';' || *p == ',') {
			flush(cur);
			cur.clear();
		} else if (*p != ' ' && *p != '\t') {
			cur.push_back(*p);
		}
	}
	flush(cur);

	if (mHelloCandidates.empty()) {
		FailWith("no reachable host candidates");
		mTransport.Close();
		return false;
	}

	// Seed the persistent direct-rescue candidate set with the host's
	// announced lobby endpoints.  These are the addresses the host is
	// listening on for direct UDP — the same set the Hello-spray uses
	// during handshake.  If the punch initially fails and the session
	// settles on Relay (mPeer = lobby), MaybeProbeDirectFromRelay will
	// keep poking these addresses; if any one becomes reachable later
	// (router reboot, NAT mapping change, transient block lifted), the
	// host's kMagicPunch handler echoes back direct and both sides
	// flip to Direct.
	mDirectRescueCandidates.clear();
	for (const auto& ep : mHelloCandidates) {
		mDirectRescueCandidates.push_back(ep);
	}

	// Pick the first candidate as the initial mPeer so the rest of the
	// code (which uses mPeer for snapshot retries, etc.) has a usable
	// default.  The ACTUAL peer is locked when the first NetWelcome
	// arrives (HandleWelcomeFromHost sets mPeer = from).
	mPeer      = mHelloCandidates.front();
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
	mHelloStartMs = 0;  // set on first Poll tick
	mLastHelloMs  = 0;
	mHaveLastRejectReason = false;
	mLastRejectReason = 0;
	mPhaseStartMs = 0;
	mPeerPath = PeerPath::Direct;
	mRelayRegistered = false;
	mPunchProbesReceived = 0;
	mRelayFramesReceived = 0;

	// One nonce for the whole spray — every candidate gets the same
	// bytes so the host collapses them into one pending decision.
	GenerateSessionNonce();

	g_ATLCNetplay("joiner: multi-candidate (%u), spraying Hello",
		(unsigned)mHelloCandidates.size());

	// Initial spray to every candidate immediately.  Note: the relay-
	// path Hello is NOT fired here because mLobbyRelayKnown / the
	// relay context is set by the UI AFTER BeginJoinMulti returns
	// (SetRelayContext is called once Create / Join lobby state is
	// resolved).  The first Poll tick will both register with the
	// lobby (MaybePrearmRelay, now T=0) and send a wrapped Hello in
	// the retry-spray loop, so the relay-first behavior kicks in
	// within one frame (~16 ms) of relay context becoming available.
	NetHello h;
	FillHello(h);
	size_t n = EncodeHello(h, mTxBuf, sizeof mTxBuf);
	if (n) {
		for (const auto& ep : mHelloCandidates) {
			mTransport.SendTo(mTxBuf, n, ep);
		}
	}
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

	if (mPhaseStartMs == 0) mPhaseStartMs = nowMs;

	// Drain the socket.
	for (int drained = 0; drained < 64; ++drained) {
		size_t n = 0;
		Endpoint from;
		RecvResult r = mTransport.RecvFrom(mRxBuf, sizeof mRxBuf, n, from);
		if (r == RecvResult::WouldBlock) break;
		if (r == RecvResult::Error)      break;

		uint32_t magic = 0;
		if (!PeekMagic(mRxBuf, n, magic)) continue;

		// Track whether this packet arrived via the lobby (ASDF
		// unwrap) or directly off the wire.  Used by the passive
		// direct-rescue path below to detect "peer is sending us
		// direct traffic — we should be on Direct too" without
		// having to wait for our own probe to round-trip.
		bool unwrappedFromRelay = false;

		// v4: relay-frame unwrap.  The server forwards inner bytes
		// directly, but if somehow we see an ASDF on our own socket
		// (the other side is wrapping and hitting us directly, or a
		// future design change), unwrap in place and re-dispatch.
		if (magic == kMagicRelayData) {
			unwrappedFromRelay = true;
			NetRelayDataHeader hdr;
			const uint8_t* inner = nullptr;
			size_t innerLen = 0;
			if (DecodeRelayFrame(mRxBuf, n, hdr, inner, innerLen)
			    != DecodeResult::Ok) continue;
			++mRelayFramesReceived;
			// Copy inner over the receive buffer (overlapping
			// memmove) so the downstream switch can use the
			// existing mRxBuf/n path.
			std::memmove(mRxBuf, inner, innerLen);
			n = innerLen;
			if (!PeekMagic(mRxBuf, n, magic)) continue;
			// Peer is visible through the lobby — if we were not
			// already in Relay mode, switch now so our responses
			// travel back the same path.  Also register with the
			// relay so the lobby will deliver our outgoing ASDF
			// frames; without this, WrapAndSend wraps the packet
			// but the lobby drops it because we have no entry in
			// its RelayTable.  Observed 2026-04-29 on the same-
			// machine two-instance test: joiner pre-armed and
			// switched to relay-only at T+6 s; host received the
			// wrapped Hello, but its snapshot chunks all dropped
			// at the lobby because it never registered, leaving
			// the session frozen in SendingSnapshot.
			//
			// IMPORTANT: gate the entire block on the FIRST
			// transition.  This branch runs for every incoming
			// ASDF frame, so without this gate the auto-flip log
			// would spam once per peer packet AND
			// OnPeerPathFlippedToRelay would reset the direct-
			// probe schedule every 16 ms (peer input packet rate
			// during Lockstepping), preventing the rescue probes
			// from ever firing.
			// Grace window: suppress flip-to-Relay for a short period
			// after we just flipped to Direct.  In-flight ASDF
			// frames the peer sent before its own rescue completed
			// would otherwise flap us straight back, spamming
			// toasts / logs while the lobby's queue drains.  We
			// still PROCESS the unwrapped packet (lockstep input,
			// snap chunks, etc.) — we just keep the path on Direct.
			const bool inFlipGrace =
				mLastFlipToDirectMs != 0 &&
				nowMs >= mLastFlipToDirectMs &&
				(nowMs - mLastFlipToDirectMs) < kFlipToRelayGraceMs;

			if (mPeerPath != PeerPath::Relay && !inFlipGrace) {
				g_ATLCNetplay(
					"%s: peer ASDF detected — flipping to relay",
					mRole == Role::Host ? "host" :
					mRole == Role::Joiner ? "joiner" : "idle");
				mPeerPath = PeerPath::Relay;
				OnPeerPathFlippedToRelay(nowMs);
				SendRelayRegister();
			}
		}

		BumpCandidateRx(from, magic == kMagicReject);

		// Passive direct-rescue flip: if a non-ASDF, non-NetPunch
		// packet arrives from our known peer address while we're on
		// Relay during Lockstepping, the peer is already on Direct
		// and sending us raw traffic — flip back so our outbound
		// stops paying the relay tax.  Covers the asymmetric scenario
		// where our local probe reached the peer (peer flipped) but
		// the peer's echo to us was lost.
		// NetPunch is excluded here and handled by the dedicated
		// rescue branch in the kMagicPunch handler below — that path
		// also echoes back, which is essential when both peers are on
		// Relay and lockstep traffic is stalled (no other packets to
		// drive the convergence).  Without the exclusion, this rule
		// would flip us first and the NetPunch handler's echo gate
		// (`mPeerPath == Relay`) would fail, leaving the peer stuck
		// on Relay.
		// Authentication: from.Equals(mPeer) — we trust the locked-in
		// peer endpoint that was authenticated during handshake.  We
		// also reset mRelayRegistered so a future Direct→Relay
		// transition can re-register with the lobby (the RelayTable
		// entry times out after 30 s of idle, so the in-memory
		// "already registered" flag would otherwise block the
		// re-registration we'd actually need).
		// Critical guard: PeerIsLobby() — when relay was engaged
		// before any direct-path traffic ever reached us, mPeer is
		// the lobby endpoint itself.  The lobby strips ASDF and
		// forwards inner bytes, so every relay-routed peer packet
		// arrives with the inner magic intact, from the lobby IP,
		// with unwrappedFromRelay=false.  Without this gate, the
		// FIRST lockstep input frame after entering Lockstepping
		// would flip us to Direct against the lobby — outbound
		// would then go raw to the lobby (dropped, no ASDF wrap)
		// and the peer would go silent until Bye.  Observed
		// 2026-04-30: gen=4 World Karate session, T+18 s into
		// Lockstepping → "peer direct traffic detected" with
		// peer=158.180.27.70:8081, immediate Bye.
		if (magic != kMagicPunch
		    && !unwrappedFromRelay
		    && mPhase == Phase::Lockstepping
		    && mPeerPath == PeerPath::Relay
		    && mPeerKnown
		    && !mDirectGiveUp
		    && !PeerIsLobby()
		    && from.Equals(mPeer))
		{
			const uint64_t outageMs =
				(mRelayEngagedAtMs && nowMs >= mRelayEngagedAtMs)
				? (nowMs - mRelayEngagedAtMs) : 0;
			char ep[32] = {};
			from.Format(ep, sizeof ep);
			g_ATLCNetplay("%s: peer direct traffic detected — flipping "
				"back to direct (was on relay %llu ms, peer=%s)",
				mRole == Role::Host ? "host" : "joiner",
				(unsigned long long)outageMs, ep);
			mPeerPath = PeerPath::Direct;
			mRelayRegistered = false;
			mLastFlipToDirectMs = nowMs;
		}

		switch (magic) {
			case kMagicPunch: {
				NetPunch pp;
				if (DecodePunch(mRxBuf, n, pp) != DecodeResult::Ok) continue;

				// Mid-session direct rescue: a NetPunch arriving on the
				// direct socket while we're stuck on Relay during
				// Lockstepping is the signal that the network has
				// healed.  Authenticate via session nonce, flip back
				// to Direct, and echo so the OTHER side flips too on
				// the same exchange (one round trip total — without
				// the echo, only one side knows direct works).  Both
				// sides also keep their independent probe schedules
				// running as a backstop in case the echo is lost.
				// Defensive: only fire when the punch arrived
				// directly (not via ASDF unwrap).  A relay-routed
				// punch's `from` is the lobby endpoint, not the peer
				// — assigning that to mPeer would corrupt our direct
				// fallback target.  Our own probe sender uses raw
				// SendTo, so this branch only matches the intended
				// direct-path flow.  Also gated by mDirectGiveUp:
				// once we've decided to stay on relay for this
				// session (flap suppression), don't recover even if
				// the peer's probe arrives.
				// Reject punches that arrived via the lobby's
				// strip-and-forward path (from == lobby).  A real
				// direct rescue, by definition, must come from a
				// non-lobby address — and only then is "set mPeer =
				// from" the right thing to do.  This is the gate
				// that allows recovery from a relay-only-since-start
				// session: in that state PeerIsLobby() is true and
				// mPeer is unusable, but if the peer manages to
				// punch us directly (e.g. its NAT mapping changed
				// and one of our candidates now reaches it), the
				// punch arrives from the peer's true address and we
				// adopt it as mPeer.
				const bool fromLobby =
					mLobbyRelayKnown && from.Equals(mLobbyRelayEndpoint);
				if (mPhase == Phase::Lockstepping &&
				    mPeerPath == PeerPath::Relay &&
				    !unwrappedFromRelay &&
				    !mDirectGiveUp &&
				    !fromLobby &&
				    std::memcmp(pp.sessionNonce, mSessionNonce,
				                kSessionNonceLen) == 0)
				{
					const uint64_t outageMs =
						(mRelayEngagedAtMs && nowMs >= mRelayEngagedAtMs)
						? (nowMs - mRelayEngagedAtMs) : 0;
					mPeerPath = PeerPath::Direct;
					// Reset the relay-registered flag so a future
					// Direct → Relay engagement can re-register
					// with the lobby — the RelayTable entry times
					// out after 30 s of idle while we're on Direct.
					mRelayRegistered = false;
					mLastFlipToDirectMs = nowMs;
					// Refresh mPeer to the address we just heard from
					// — covers the NAT-eviction-and-recreate case where
					// the peer's external IP/port changed during the
					// relay window.  WrapAndSend now targets this in
					// Direct mode.
					mPeer = from;
					// Echo a single NetPunch back via direct so the
					// other side flips too.  Re-use the inbound nonce
					// (== mSessionNonce by construction since we just
					// authenticated it).  Idempotent if the other side
					// already flipped on its own probe response.
					{
						NetPunch echo;
						std::memcpy(echo.sessionNonce, mSessionNonce,
							sizeof echo.sessionNonce);
						uint8_t buf[kWirePunchSize];
						size_t en = EncodePunch(echo, buf, sizeof buf);
						if (en) mTransport.SendTo(buf, en, from);
					}
					char ep[32] = {};
					from.Format(ep, sizeof ep);
					g_ATLCNetplay("%s: direct connection restored at "
						"frame %lld (was on relay %llu ms, peer=%s)",
						mRole == Role::Host ? "host" : "joiner",
						(long long)mLoop.CurrentFrame(),
						(unsigned long long)outageMs, ep);
					break;
				}

				if (mRole == Role::Joiner &&
				    mPhase == Phase::Handshaking) {
					if (std::memcmp(pp.sessionNonce, mSessionNonce,
					    kSessionNonceLen) == 0) {
						++mPunchProbesReceived;
						if (mPunchProbesReceived == 1) {
							char fmt[64];
							from.Format(fmt, sizeof fmt);
							g_ATLCNetplay("joiner: punch-assisted — host "
								"probe arrived from %s", fmt);
						}
					}
				}
				// Host side outside the Lockstepping+Relay rescue
				// case: a punch arriving here is almost certainly our
				// own probe looping back through a weird NAT path;
				// drop silently.
				break;
			}
			case kMagicHello: {
				NetHello h;
				if (DecodeHello(mRxBuf, n, h) != DecodeResult::Ok) continue;
				HandleHelloFromJoiner(h, from, nowMs);
				break;
			}
			case kMagicWelcome: {
				NetWelcome w;
				if (DecodeWelcome(mRxBuf, n, w) != DecodeResult::Ok) continue;
				// Multi-candidate: lock mPeer onto the responder and
				// stop spraying to the other candidates.  The first
				// host to Welcome us wins.
				if (!mHelloCandidates.empty() &&
				    mPhase == Phase::Handshaking) {
					mPeer = from;
					mPeerKnown = true;
					// Path determination: same logic as the host-
					// side accept paths.  If Welcome arrived via
					// the lobby (from == lobby endpoint), the
					// host can only be reached through the
					// lobby's strip-and-forward path.  Flip
					// mPeerPath = Relay so all subsequent
					// outbound (Hello retransmits, snapshot
					// chunk acks, lockstep input) gets ASDF-
					// wrapped.  Direct rescue probing in
					// MaybeProbeDirectFromRelay continues against
					// the original mHelloCandidates set in case
					// the network heals later.
					const bool acceptedViaRelay =
						mLobbyRelayKnown &&
						from.Equals(mLobbyRelayEndpoint);
					if (acceptedViaRelay &&
					    mPeerPath != PeerPath::Relay) {
						mPeerPath = PeerPath::Relay;
						OnPeerPathFlippedToRelay(nowMs);
					}
					char fmt[64];
					from.Format(fmt, sizeof fmt);
					g_ATLCNetplay("joiner: locked onto responding host %s "
						"(had %u candidates, %s)",
						fmt, (unsigned)mHelloCandidates.size(),
						acceptedViaRelay ? "via relay" : "direct");
					mHelloCandidates.clear();
				}
				HandleWelcomeFromHost(w, nowMs);
				break;
			}
			case kMagicReject: {
				NetReject rj;
				if (DecodeReject(mRxBuf, n, rj) != DecodeResult::Ok) continue;
				// Only accept a Reject from the host we asked — an
				// arbitrary bystander sending a Reject to our
				// ephemeral port mustn't be able to abort the join.
				// For the v3 candidate spray, a Reject is
				// **per-path**, not terminal: when the host runs
				// our new dedupe logic they won't send us a Reject
				// on alt-paths for our own join at all, but older
				// hosts (and any path-specific rejects) must not
				// short-circuit a Welcome that's still in flight on
				// another candidate.  Remove just the rejecting
				// endpoint from the spray set; only when the last
				// candidate rejects do we transition to Failed.
				if (!mHelloCandidates.empty() &&
				    mPhase == Phase::Handshaking) {
					bool matched = false;
					for (auto it = mHelloCandidates.begin();
					     it != mHelloCandidates.end(); ++it) {
						if (it->Equals(from)) {
							mHelloCandidates.erase(it);
							matched = true;
							break;
						}
					}
					mLastRejectReason = rj.reason;
					mHaveLastRejectReason = true;

					char fmt[64];
					from.Format(fmt, sizeof fmt);
					if (!mHelloCandidates.empty()) {
						// Still candidates in flight — keep waiting
						// for a Welcome on any of them.
						g_ATLCNetplay("joiner: Reject from %s (reason %u); "
							"%u candidate(s) still pending",
							fmt, (unsigned)rj.reason,
							(unsigned)mHelloCandidates.size());
						(void)matched;
						break;
					}
					// Exhausted: every sprayed candidate rejected.
					// Lock mPeer on the last rejecter so the error
					// surfaces with the host-supplied reason.
					mPeer = from;
					mPeerKnown = true;
				}
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
			case kMagicSnapSkip: {
				// Joiner has the game locally — skip the chunked
				// transfer.  Validates session nonce and the
				// claimed CRC32 against what we advertised in
				// Welcome (mBootConfig.gameFileCRC32) before
				// short-circuiting the SnapshotSender.  Anti-
				// spoof: a stale skip from a different session
				// would carry a non-matching nonce; a different
				// game's CRC would fail the second check.
				NetSnapSkip s;
				if (DecodeSnapSkip(mRxBuf, n, s) != DecodeResult::Ok) continue;
				if (mRole != Role::Host) break;
				if (mPhase != Phase::SendingSnapshot) break;
				if (std::memcmp(s.sessionNonce, mSessionNonce,
				                kSessionNonceLen) != 0) break;
				if (s.gameFileCRC32 != mBootConfig.gameFileCRC32) break;
				if (mSnapTx.GetStatus() != SnapshotSender::Status::Sending)
					break;
				mSnapTx.ForceDone();
				g_ATLCNetplay(
					"host: joiner has the game locally (CRC32=%08X) — "
					"skipping snapshot transfer",
					(unsigned)s.gameFileCRC32);
				// PumpSnapshotSender on the next Poll tick will see
				// Status==Done and run the existing "snapshot
				// delivered → Lockstepping" path.
				break;
			}
			case kMagicBye: {
				NetBye b;
				if (DecodeBye(mRxBuf, n, b) != DecodeResult::Ok) continue;
				HandleBye(b, from);
				break;
			}
			case kMagicResyncStart: {
				NetResyncStart s;
				if (DecodeResyncStart(mRxBuf, n, s) != DecodeResult::Ok) continue;
				HandleResyncStart(s, nowMs);
				break;
			}
			case kMagicResyncDone: {
				NetResyncDone d;
				if (DecodeResyncDone(mRxBuf, n, d) != DecodeResult::Ok) continue;
				HandleResyncDone(d, nowMs);
				break;
			}
			case kMagicEmote: {
				NetEmote e;
				if (DecodeEmote(mRxBuf, n, e) != DecodeResult::Ok) continue;
				HandleEmote(e);
				break;
			}
			case kMagicSimHashDiag: {
				NetSimHashDiag d;
				if (DecodeSimHashDiag(mRxBuf, n, d) != DecodeResult::Ok) continue;
				HandleSimHashDiag(d);
				break;
			}
			default:
				// Unknown magic — silently ignore (could be from a
				// different version or a stray packet).
				break;
		}
	}

	// Relay-first: register with the lobby's relay table immediately
	// on the first Poll tick after relay context is set, BEFORE the
	// Hello-spray below consults mRelayRegistered.  Without this
	// ordering, the very first spray after BeginJoinMulti would
	// always skip the wrapped-Hello branch and we'd lose one retry
	// interval (~250 ms) of relay-path latency on every join.
	MaybePrearmRelay(nowMs);

	// v3 NAT traversal: joiner retransmits NetHello to every candidate
	// while we're still in Handshaking.  Stops as soon as any host
	// replies (Welcome / Reject clears mHelloCandidates) or the hard
	// timeout elapses.  v4 extends the hard timeout to kRelayFailTimeoutMs
	// once relay mode has engaged, so the relay handshake has its own
	// budget beyond the original direct-punch window.
	if (mPhase == Phase::Handshaking && !mHelloCandidates.empty()) {
		if (mHelloStartMs == 0) mHelloStartMs = nowMs;
		uint64_t timeoutMs = (mPeerPath == PeerPath::Relay)
			? kRelayFailTimeoutMs : kHelloTimeoutMs;
		if (nowMs - mHelloStartMs >= timeoutMs) {
			g_ATLCNetplay("joiner: handshake timeout after %ums, "
				"no host responded (candidates tried: %u, "
				"last-seen reject reason: %s)",
				(unsigned)(nowMs - mHelloStartMs),
				(unsigned)mHelloCandidates.size(),
				mHaveLastRejectReason ? "yes" : "none");
			if (mHaveLastRejectReason) {
				NetReject synth{};
				synth.reason = mLastRejectReason;
				HandleRejectFromHost(synth);
			} else {
				// Structured diagnostics.  List each candidate with
				// whether we saw ANY return traffic, plus whether a
				// punch probe arrived, plus relay status.  Stored in
				// mLastErrorOwned so mLastError (const char*) keeps
				// pointing at valid memory.
				char line[512];
				std::string msg;
				msg = "Could not reach host — please check the host is "
					"online and try again.";
				if (!mCandidateStats.empty()) {
					msg += "\nCandidates tried:";
					for (const auto& c : mCandidateStats) {
						std::snprintf(line, sizeof line,
							"\n  %s — %s%s",
							c.display.c_str(),
							c.rxPackets == 0
								? "no reply"
								: "some reply",
							c.rxRejects ? " (rejected)" : "");
						msg += line;
					}
				}
				std::snprintf(line, sizeof line,
					"\nPunch probes received: %d\nLobby relay: %s",
					mPunchProbesReceived,
					mPeerPath == PeerPath::Relay
						? (mRelayFramesReceived
							? "reachable" : "registered, no traffic")
						: "not tried");
				msg += line;
				mLastErrorOwned = std::move(msg);
				FailWith(mLastErrorOwned.c_str());
			}
			mHelloCandidates.clear();
		} else if (nowMs - mLastHelloMs >= kHelloRetryMs) {
			mLastHelloMs = nowMs;
			NetHello h;
			FillHello(h);
			size_t nn = EncodeHello(h, mTxBuf, sizeof mTxBuf);
			if (nn) {
				for (const auto& ep : mHelloCandidates) {
					mTransport.SendTo(mTxBuf, nn, ep);
				}
				// Relay-first: ALSO send a wrapped Hello through the
				// lobby on every retry, in parallel with the direct
				// candidates.  Was previously gated on
				// mPeerPath == Relay (i.e. only fired after the
				// T+6s engage), which contributed up to a 30 s
				// "host sees joiner" delay because the host's
				// relay registration depended on the lobby
				// heartbeat delivering the joiner's peer-hint.
				// The new flow registers both peers proactively
				// in MaybePrearmRelay at T=0, so a single lobby
				// roundtrip (~100-300 ms) is enough for the host
				// to see this Hello.  Direct punching still runs
				// alongside; whichever path completes the
				// handshake first determines mPeerPath.
				if (mLobbyRelayKnown && mRelayRegistered) {
					SendWrappedViaLobby(mTxBuf, nn);
				}
			}
		}
	}

	// v4: drive punch probes and relay fallback (two-stage:
	// pre-arm at T+kRelayPrearmAfterMs then engage at
	// T+kRelayFallbackAfterMs; mid-session rescue is independent).
	PumpPunchProbes(nowMs);
	MaybePrearmRelay(nowMs);
	MaybeEngageRelay(nowMs);
	MaybeRescueRelayMidSession(nowMs);
	MaybeProbeDirectFromRelay(nowMs);

	// Outgoing pumps.  Snapshot sender is shared between the session-
	// start upload (Phase::SendingSnapshot) and the mid-session resync
	// (Phase::Resyncing) — it doesn't care which phase it's serving.
	if (mPhase == Phase::SendingSnapshot || mPhase == Phase::Resyncing) {
		PumpSnapshotSender(nowMs);
	}
	if (mPhase == Phase::Lockstepping && mWantsOutgoingInput) {
		PumpLockstepSend();
		mWantsOutgoingInput = false;
	}
	// Lockstep: detect desync transition.  Host: initiate a resync by
	// capturing its sim state and streaming it to the joiner.  Joiner:
	// stay in Lockstepping with mLoop.IsDesynced() gating CanAdvance —
	// the sim naturally pauses, and the host will detect within ~1 RTT
	// (via retro-check on the next peer input packet) and drive the
	// resync from its side.
	if (mPhase == Phase::Lockstepping && mLoop.IsDesynced()) {
		if (!mDesyncLogged) {
			g_ATLCNetplay("DESYNC detected at frame %lld (role=%s, delay=%u)",
				(long long)mLoop.DesyncFrame(),
				mRole == Role::Host ? "host" : "joiner",
				(unsigned)mInputDelay);
			mDesyncLogged = true;
		}
		if (mRole == Role::Host) {
			BeginHostResync(nowMs);
		}
		// joiner: nothing to do here; wait for NetResyncStart.
	}

	// Host resync: retransmit NetResyncStart until the joiner acks at
	// least one chunk.  Absorbs a one-packet loss of the initial start
	// without blowing the SnapTx retry budget trying to push chunks the
	// joiner is dropping.
	if (mPhase == Phase::Resyncing && mRole == Role::Host &&
	    mSnapTx.AcknowledgedChunks() == 0 && mPeerKnown &&
	    nowMs - mResyncStartLastSentMs >= kResyncStartRetryMs) {
		NetResyncStart s;
		s.magic        = kMagicResyncStart;
		s.epoch        = mResyncEpoch;
		s.stateBytes   = (uint32_t)mResyncTxBuffer.size();
		s.stateChunks  = mSnapTx.TotalChunks();
		s.resumeFrame  = mResyncResumeFrame;
		s.seedHash     = mResyncSeedHash;
		size_t nn = EncodeResyncStart(s, mTxBuf, sizeof mTxBuf);
		if (nn) {
			WrapAndSend(mTxBuf, nn, mPeer);
			mResyncStartLastSentMs = nowMs;
			g_ATLCNetplay("host: re-sent ResyncStart (epoch %u, no chunks acked yet)",
				(unsigned)mResyncEpoch);
		}
	}

	// Joiner resync: retransmit NetResyncDone until the host's first
	// post-resync input packet arrives (which flips
	// mAwaitingHostResyncAck off in HandleInputPacket).  Fires only
	// after AcknowledgeResyncApplied has put us back in Lockstepping
	// — the one emit inline there is not guaranteed to survive UDP.
	if (mAwaitingHostResyncAck && mRole == Role::Joiner &&
	    mPhase == Phase::Lockstepping && mPeerKnown &&
	    nowMs - mResyncDoneLastSentMs >= kResyncDoneRetryMs) {
		NetResyncDone d;
		d.magic       = kMagicResyncDone;
		d.epoch       = mResyncEpoch;
		d.resumeFrame = mResyncResumeFrame;
		size_t nn = EncodeResyncDone(d, mTxBuf, sizeof mTxBuf);
		if (nn) {
			WrapAndSend(mTxBuf, nn, mPeer);
			mResyncDoneLastSentMs = nowMs;
			g_ATLCNetplay("joiner: re-sent ResyncDone (epoch %u, awaiting host input)",
				(unsigned)mResyncEpoch);
		}
	}

	// Overall Phase::Resyncing deadline.  If we're stuck here past the
	// budget — chunks dropping, Done packets all lost, or the app
	// never called SubmitResyncCapture / AcknowledgeResyncApplied —
	// bail so the UI can surface the failure instead of showing a
	// frozen progress bar forever.  The 10-minute peer-silence
	// watchdog below only runs in Lockstepping and cannot rescue
	// this case.
	if (mPhase == Phase::Resyncing && mResyncStartMs != 0 &&
	    nowMs - mResyncStartMs >= kResyncOverallTimeoutMs) {
		g_ATLCNetplay("%s: resync timed out after %llu ms (epoch %u) — terminating",
			mRole == Role::Host ? "host" : "joiner",
			(unsigned long long)(nowMs - mResyncStartMs),
			(unsigned)mResyncEpoch);
		SendBye(kByeDesyncDetected);
		mPhase = Phase::Desynced;
		mLastError = "resync timed out";
	}

	// Joiner-side ReceivingSnapshot deadline.  Without this, a host
	// that crashes / loses network after sending Welcome but before
	// the snapshot finishes leaves us stuck in
	// "Downloading game from host…" indefinitely — the handshake
	// timeout already passed (we're in ReceivingSnapshot, not
	// Handshaking) and the Lockstepping peer-silence backstop only
	// runs in Lockstepping.  Pattern matches the resync timeout
	// above.  No SendBye: the host has stopped responding by
	// definition; FailWith surfaces the error to the UI.
	if (mPhase == Phase::ReceivingSnapshot && mLastChunkRecvMs != 0 &&
	    nowMs - mLastChunkRecvMs >= kSnapshotReceiveTimeoutMs) {
		g_ATLCNetplay(
			"joiner: snapshot transfer stalled (no chunk in %llu ms) — terminating",
			(unsigned long long)(nowMs - mLastChunkRecvMs));
		FailWith("snapshot transfer stalled (host unresponsive)");
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
	// Note: cross-Altirra-release canonical-profile mismatches
	// (kRejectCanonicalProfileMismatch) are detected on the JOINER
	// side, in HandleWelcomeFromHost, by comparing the host's
	// advertised canonicalProfileVersion against the local
	// ATNetplayProfile::kCanonicalProfileVersion.  The joiner's
	// Hello does not carry the field, so there is nothing for the
	// host to compare here.
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
	// or RejectPendingJoiner(i).  We stay in WaitingForJoiner until
	// an Accept picks one entry, so a Reject on any single row can
	// return to listening cleanly.
	//
	// Dedupe key: the joiner's per-attempt session nonce, when
	// present.  A single joiner's v3 candidate spray reaches us on
	// multiple source endpoints (LAN NIC / loopback / NAT hairpin),
	// each with its own (ip,port) but all carrying the same nonce —
	// we must collapse them into one pending row, not three.  v1.0
	// clients send a zero nonce; we fall back to handle-based match
	// for those.  Endpoint-based match is used only as a final
	// fallback for simple retransmits when neither nonce nor handle
	// matches anything.
	if (mPromptAccept) {
		bool helloNonceNonZero = false;
		for (size_t k = 0; k < kSessionNonceLen; ++k) {
			if (hello.sessionNonce[k] != 0) { helloNonceNonZero = true; break; }
		}

		for (auto& e : mPendingDecisions) {
			bool sameJoiner = false;
			if (helloNonceNonZero && e.hasSessionNonce) {
				sameJoiner = (std::memcmp(e.sessionNonce,
					hello.sessionNonce, kSessionNonceLen) == 0);
			} else {
				// Legacy path: match on handle.  False positive
				// risk (two users with identical nicknames) is
				// bounded — both get merged into one row but the
				// host still reaches a valid joiner.  New clients
				// always carry a nonce, so this only fires for
				// v1.0 peers.
				sameJoiner = (std::memcmp(e.handle, hello.playerHandle,
					kHandleLen) == 0);
			}
			if (!sameJoiner) continue;

			// Same joiner.  If this is a new source endpoint, add
			// it to altPeers so we remember not to Reject it on
			// accept.  Don't touch arrivedMs — "requested Xs ago"
			// stays anchored to the first arrival.
			if (e.peer.Equals(from)) return;
			for (const auto& alt : e.altPeers) {
				if (alt.Equals(from)) return;
			}
			e.altPeers.push_back(from);

			char ep[32] = {};
			from.Format(ep, sizeof ep);
			g_ATLCNetplay("host: additional path for \"%s\" at %s "
				"(now %zu path(s); queue depth unchanged)",
				e.handle, ep, 1 + e.altPeers.size());
			return;
		}

		if (mPendingDecisions.size() >= kMaxPendingDecisions) {
			// Queue is full.  Bounce the joiner with HostFull so they
			// get a clear error instead of a silent timeout.
			SendReject(kRejectHostFull, from);
			return;
		}
		PendingDecision pd;
		pd.peer = from;
		std::memcpy(pd.sessionNonce, hello.sessionNonce, kSessionNonceLen);
		pd.hasSessionNonce = helloNonceNonZero;
		std::memset(pd.handle, 0, sizeof pd.handle);
		std::memcpy(pd.handle, hello.playerHandle, kHandleLen);
		pd.arrivedMs = nowMs;
		mPendingDecisions.push_back(std::move(pd));
		char ep[32] = {};
		from.Format(ep, sizeof ep);
		g_ATLCNetplay("host: incoming join from \"%s\" at %s — "
			"awaiting host decision (queue depth %zu)",
			mPendingDecisions.back().handle, ep, mPendingDecisions.size());
		return;
	}

	// Hello accepted.  Lock in the peer.
	mPeer = from;
	mPeerKnown = true;
	// Path determination: if this Hello arrived via the lobby's
	// strip-and-forward path (from == lobby endpoint), we cannot
	// reach the peer directly — every reply we send must be
	// ASDF-wrapped so the lobby forwards it.  Flip mPeerPath
	// accordingly so SendWelcome / snapshot chunks travel via
	// WrapAndSend through the lobby.  Direct rescue probing in
	// MaybeProbeDirectFromRelay will keep trying to acquire a
	// real direct endpoint via the candidate list.
	if (mLobbyRelayKnown && from.Equals(mLobbyRelayEndpoint)) {
		if (mPeerPath != PeerPath::Relay) {
			mPeerPath = PeerPath::Relay;
			OnPeerPathFlippedToRelay(nowMs);
		}
	}
	{
		char handle[kHandleLen + 1] = {};
		std::memcpy(handle, hello.playerHandle, kHandleLen);
		char ep[32] = {};
		from.Format(ep, sizeof ep);
		g_ATLCNetplay("host: accepted joiner \"%s\" from %s%s",
			handle, ep,
			(mLobbyRelayKnown && from.Equals(mLobbyRelayEndpoint))
				? " (via relay)" : " (direct)");
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
	mSnapProgressMilestone = 0;
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
	// Important: Reject reaches each peer on **every** path we observed
	// them on, otherwise their v3 spray would simply keep retransmitting
	// on the unrejected candidates and time out instead of failing
	// promptly.  We do *not* Reject the chosen row's altPeers — those
	// are the chosen joiner themselves, and Welcome on chosen.peer is
	// all they need (their own spray stops when Welcome arrives).
	for (size_t k = 0; k < mPendingDecisions.size(); ++k) {
		if (k == i) continue;
		SendReject(kRejectHostFull, mPendingDecisions[k].peer);
		for (const auto& alt : mPendingDecisions[k].altPeers) {
			SendReject(kRejectHostFull, alt);
		}
	}

	PendingDecision chosen = std::move(mPendingDecisions[i]);
	mPendingDecisions.clear();

	// Adopt the accepted peer and run the same path the auto-accept
	// branch used to (see HandleHelloFromJoiner).
	mPeer      = chosen.peer;
	mPeerKnown = true;

	// Path determination — same logic as the auto-accept branch in
	// HandleHelloFromJoiner.  If the chosen endpoint is the lobby,
	// outbound responses must be ASDF-wrapped so the lobby forwards
	// them to the peer.
	const bool acceptedViaRelay =
		mLobbyRelayKnown && mPeer.Equals(mLobbyRelayEndpoint);
	if (acceptedViaRelay && mPeerPath != PeerPath::Relay) {
		// We don't have nowMs here — use 0 as a sentinel.  The
		// MaybeRescueRelayMidSession path is already gated on
		// mPhase == Lockstepping so it won't observe this 0 stamp
		// before it's overwritten.  The direct-rescue probe schedule
		// resets on the next genuine flip-to-relay if any.
		mPeerPath = PeerPath::Relay;
		OnPeerPathFlippedToRelay(0);
	}

	char ep[32] = {};
	mPeer.Format(ep, sizeof ep);
	g_ATLCNetplay("host: accepted joiner \"%s\" from %s (manual approval, %s)",
		chosen.handle, ep, acceptedViaRelay ? "via relay" : "direct");

	if (mSnapTxBuffer.empty()) {
		mHostHasPendingJoiner = true;
		mPendingJoiner = mPeer;
		mPhase = Phase::SendingSnapshot;
		return;
	}

	mPhase = Phase::SendingSnapshot;
	SendWelcome();
	mSnapTx.Begin(mSnapTxBuffer.data(), mSnapTxBuffer.size());
	mSnapProgressMilestone = 0;
}

void Coordinator::RejectPendingJoiner(size_t i) {
	if (i >= mPendingDecisions.size()) return;
	const PendingDecision& pd = mPendingDecisions[i];
	g_ATLCNetplay("host: rejected joiner \"%s\" (manual decline)",
		pd.handle);
	// Reject on every path we saw this joiner on, for the same reason
	// the accept branch does: otherwise the joiner's v3 spray keeps
	// hammering the unrejected candidates until the hard timeout.
	SendReject(kRejectHostRejected, pd.peer);
	for (const auto& alt : pd.altPeers) {
		SendReject(kRejectHostRejected, alt);
	}
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
	mSnapProgressMilestone = 0;
	}
	// If no joiner yet, bytes sit in mSnapTxBuffer until one arrives.
}

// ---------------------------------------------------------------------------
// Joiner: handle Welcome
// ---------------------------------------------------------------------------

void Coordinator::HandleWelcomeFromHost(const NetWelcome& w, uint64_t nowMs) {
	if (mPhase != Phase::Handshaking) return;

	// v4: reject if the host's canonical-profile version differs
	// from ours.  Same rationale as the symmetric check on the host
	// side (HandleHelloFromJoiner): cold-booting to a different
	// canonical baseline guarantees a frame-0 desync.  We surface
	// this through Phase::Failed + a cached LastError so the user
	// sees a clear explanation instead of mysterious silence.
	if (w.boot.canonicalProfileVersion !=
	    ATNetplayProfile::kCanonicalProfileVersion) {
		mPhase = Phase::Failed;
		mLastError = "Host is running a different Online Play "
			"profile version. Both peers must run a compatible "
			"Altirra release.";
		g_ATLCNetplay("joiner: canonical profile mismatch "
			"(host=v%u, local=v%u)",
			(unsigned)w.boot.canonicalProfileVersion,
			(unsigned)ATNetplayProfile::kCanonicalProfileVersion);
		return;
	}

	// Adopt host-chosen parameters.
	mInputDelay = w.inputDelayFrames;
	std::memcpy(mCartName, w.cartName, kCartLen);
	mSettingsHash = w.settingsHash;
	mBootConfig = w.boot;

	// Prepare receiver.
	mSnapRx.Begin(w.snapshotChunks, w.snapshotBytes);
	mSnapProgressMilestone = 0;
	mPhase = Phase::ReceivingSnapshot;
	// Seed the snapshot-receive watchdog with nowMs (or 1 if nowMs
	// happens to be 0 — same sentinel pattern used elsewhere).  The
	// Poll-side watchdog fails the session if no chunk arrives
	// within kSnapshotReceiveTimeoutMs.
	mLastChunkRecvMs = nowMs ? nowMs : 1;
	g_ATLCNetplay("joiner: Welcome accepted (snapshot=%u bytes / %u chunks, delay=%u)",
		(unsigned)w.snapshotBytes, (unsigned)w.snapshotChunks,
		(unsigned)w.inputDelayFrames);

	// If the snapshot is zero-length (edge case: nothing to ship),
	// move straight to SnapshotReady.
	if (w.snapshotChunks == 0) {
		mPhase = Phase::SnapshotReady;
		return;
	}

	// Joiner-side cache lookup.  If the UI/glue layer installed a hook
	// AND the host advertised a real CRC32 + extension, ask whether
	// we already have these bytes locally (library or netplay cache).
	// On hit, adopt the bytes as the fully-received snapshot, send
	// NetSnapSkip so the host short-circuits its sender, and move
	// straight to SnapshotReady — saving the entire chunked transfer.
	mUsedLocalSnapshot = false;
	if (mCacheLookup &&
	    w.boot.gameFileCRC32 != 0 &&
	    w.boot.gameExtension[0] != 0) {
		std::vector<uint8_t> localBytes;
		const bool hit = mCacheLookup(
			w.boot.gameFileCRC32,
			(uint64_t)w.snapshotBytes,
			w.boot.gameExtension,
			localBytes);
		if (hit && (uint64_t)localBytes.size() == (uint64_t)w.snapshotBytes) {
			if (mSnapRx.AdoptBytes(localBytes.data(), localBytes.size())) {
				// Tell the host we don't need the chunks.  Carries
				// the CRC the host advertised so the host can
				// validate before short-circuiting its sender.
				NetSnapSkip skip;
				std::memcpy(skip.sessionNonce, mSessionNonce,
				            sizeof skip.sessionNonce);
				skip.gameFileCRC32 = w.boot.gameFileCRC32;
				uint8_t buf[kWireSnapSkipSize];
				size_t n = EncodeSnapSkip(skip, buf, sizeof buf);
				if (n && mPeerKnown) WrapAndSend(buf, n, mPeer);

				mUsedLocalSnapshot = true;
				mPhase = Phase::SnapshotReady;
				g_ATLCNetplay("joiner: snapshot satisfied locally "
					"(CRC32=%08X, %zu bytes) — sent SnapSkip",
					(unsigned)w.boot.gameFileCRC32, localBytes.size());
				return;
			}
		}
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
		case kRejectCanonicalProfileMismatch:
			mLastError = "Host is running a different Online Play "
				"profile version. Both peers must run a compatible "
				"Altirra release.";
			break;
		default:                    mLastError = "Host rejected the connection (unknown reason)."; break;
	}
	g_ATLCNetplay("joiner: %s (reason code %u)", mLastError, (unsigned)r.reason);
}

// ---------------------------------------------------------------------------
// Snapshot chunking (bidirectional)
// ---------------------------------------------------------------------------

void Coordinator::HandleSnapChunk(const NetSnapChunk& c, uint64_t nowMs) {
	const bool sessionStart = (mPhase == Phase::ReceivingSnapshot);
	const bool midResync    = (mPhase == Phase::Resyncing && mRole == Role::Joiner);
	if (!sessionStart && !midResync) return;

	if (mSnapRx.OnChunk(c)) {
		// Refresh the ReceivingSnapshot watchdog on every accepted
		// chunk (including duplicates — they prove the host is alive
		// and trying).  Only stamps during sessionStart; mid-session
		// resyncs use kResyncOverallTimeoutMs which has its own
		// independent deadline.
		if (sessionStart) mLastChunkRecvMs = nowMs ? nowMs : 1;
		NetSnapAck ack { kMagicAck, c.chunkIdx };
		size_t n = EncodeSnapAck(ack, mTxBuf, sizeof mTxBuf);
		if (n && mPeerKnown) WrapAndSend(mTxBuf, n, mPeer);
	}

	// Symmetric to the host's HandleSnapAck milestones — emit one
	// log line at 25 / 50 / 75 % so a slow / stalled download is
	// visible in the joiner's log.  100 % is covered by the
	// "snapshot download complete, applying…" line below.
	{
		const uint32_t total = mSnapRx.ExpectedChunks();
		const uint32_t recv  = mSnapRx.ReceivedChunks();
		if (total > 0 && recv < total) {
			const uint8_t pct100 = (uint8_t)((uint64_t)recv * 100u / total);
			const uint8_t bucket =
				pct100 >= 75 ? 3 :
				pct100 >= 50 ? 2 :
				pct100 >= 25 ? 1 : 0;
			if (bucket > mSnapProgressMilestone) {
				mSnapProgressMilestone = bucket;
				g_ATLCNetplay(
					"joiner: snapshot %u%% (%u/%u chunks)",
					(unsigned)(bucket * 25u),
					(unsigned)recv, (unsigned)total);
			}
		}
	}

	if (mSnapRx.IsComplete()) {
		if (sessionStart) {
			mPhase = Phase::SnapshotReady;
			g_ATLCNetplay("joiner: snapshot download complete, applying…");
			// Persist the just-downloaded bytes to the netplay cache
			// so the next session for the same game can skip the
			// transfer.  Suppressed when we adopted bytes locally
			// (mUsedLocalSnapshot=true) — those came FROM the cache /
			// library, no need to write them back.  The store hook
			// validates CRC and writes atomically; failure logs but
			// does not abort the session.
			if (!mUsedLocalSnapshot && mCacheStore &&
			    mBootConfig.gameFileCRC32 != 0 &&
			    mBootConfig.gameExtension[0] != 0 &&
			    !mSnapRx.Data().empty()) {
				mCacheStore(mBootConfig.gameFileCRC32,
				             mBootConfig.gameExtension,
				             mSnapRx.Data().data(),
				             mSnapRx.Data().size());
			}
		} else {
			// Mid-session resync: flag the app to call ApplySavestate +
			// AcknowledgeResyncApplied.  Phase stays Resyncing until
			// the app acks.
			mNeedsResyncApply = true;
			g_ATLCNetplay("joiner: resync payload complete (epoch %u), awaiting apply",
				(unsigned)mResyncEpoch);
		}
	}
}

void Coordinator::HandleSnapAck(const NetSnapAck& a) {
	const bool sessionStart = (mPhase == Phase::SendingSnapshot);
	const bool midResync    = (mPhase == Phase::Resyncing && mRole == Role::Host);
	if (!sessionStart && !midResync) return;

	mSnapTx.OnAckReceived(a.chunkIdx);

	// Progress milestones at 25 / 50 / 75 % so a slow / stalled
	// transfer is visible in the log without per-chunk noise.  100% is
	// covered by the existing "snapshot delivered" line below — skip
	// the milestone on the final ack so we don't emit a confusing
	// "snapshot 75% (N/N chunks)" right before the "delivered" line.
	{
		const uint32_t total = mSnapTx.TotalChunks();
		const uint32_t acked = mSnapTx.AcknowledgedChunks();
		if (total > 0 && acked < total) {
			const uint8_t pct100 = (uint8_t)((uint64_t)acked * 100u / total);
			const uint8_t bucket =
				pct100 >= 75 ? 3 :
				pct100 >= 50 ? 2 :
				pct100 >= 25 ? 1 : 0;
			if (bucket > mSnapProgressMilestone) {
				mSnapProgressMilestone = bucket;
				g_ATLCNetplay("%s: snapshot %u%% (%u/%u chunks)",
					mRole == Role::Host ? "host" : "joiner",
					(unsigned)(bucket * 25u),
					(unsigned)acked, (unsigned)total);
			}
		}
	}

	if (mSnapTx.GetStatus() == SnapshotSender::Status::Done) {
		if (sessionStart) {
			// Host is ready for lockstep immediately; joiner transitions
			// to Lockstepping when it calls AcknowledgeSnapshotApplied.
			mLoop.Begin(Slot::Host, mInputDelay);
			mPhase = Phase::Lockstepping;
			g_ATLCNetplay("host: snapshot delivered, entering Lockstepping (delay=%u frames)",
				(unsigned)mInputDelay);
			if (mPeerPath == PeerPath::Relay && PeerIsLobby()) {
				g_ATLCNetplay("host: relay-only session — mid-session "
					"direct rescue will probe %zu peer candidate%s "
					"from the lobby heartbeat",
					mDirectRescueCandidates.size(),
					mDirectRescueCandidates.size() == 1 ? "" : "s");
			}
		} else {
			// Resync: all chunks acknowledged.  Now wait for ResyncDone
			// from the joiner confirming successful apply — only then
			// does the host resume lockstep.
			g_ATLCNetplay("host: resync payload delivered (epoch %u), awaiting ResyncDone",
				(unsigned)mResyncEpoch);
		}
	} else if (mSnapTx.GetStatus() == SnapshotSender::Status::Failed) {
		if (sessionStart) {
			FailWith("snapshot upload failed (retry budget exhausted)");
		} else {
			// Resync transfer exhausted retries — treat as unrecoverable.
			SendBye(kByeDesyncDetected);
			mPhase = Phase::Desynced;
			mLastError = "resync payload upload failed";
			g_ATLCNetplay("host: resync transfer failed — terminating session");
		}
	}
}

void Coordinator::PumpSnapshotSender(uint64_t nowMs) {
	if (!mPeerKnown) return;
	NetSnapChunk c;
	// Fill the SnapshotSender's sliding window each Poll tick.  The
	// sender returns false once the window is full (enough chunks
	// in flight, none yet eligible for retry) — that is our natural
	// stop signal.  Up to kWindowSize chunks per tick is bounded
	// by the window, so a runaway loop is impossible.  This is the
	// change that turns 220 ms × N-chunk stop-and-wait into a
	// 2-3 RTT batched transfer over relay.
	while (mSnapTx.NextOutgoing(c, nowMs)) {
		size_t n = EncodeSnapChunk(c, mTxBuf, sizeof mTxBuf);
		if (n == 0) break;
		WrapAndSend(mTxBuf, n, mPeer);
	}

	// Surface SnapTx retry-budget exhaustion HERE, not only in
	// HandleSnapAck.  NextOutgoing() flips the status to Failed once
	// kMaxAttemptsPerChunk re-sends elapse without an ack — but if
	// zero acks ever arrived (the path one-way, e.g. host direct +
	// joiner relay-only), HandleSnapAck never fires and the session
	// hangs in SendingSnapshot indefinitely.  Detecting the failed
	// status here drives a clean FailWith so the UI surfaces an error
	// and the user isn't left wondering why nothing happened.
	if (mSnapTx.GetStatus() == SnapshotSender::Status::Failed) {
		const bool sessionStart = (mPhase == Phase::SendingSnapshot);
		const bool midResync    = (mPhase == Phase::Resyncing &&
		                            mRole == Role::Host);
		if (sessionStart) {
			FailWith("snapshot upload failed (no chunk acks received)");
		} else if (midResync) {
			SendBye(kByeDesyncDetected);
			mPhase = Phase::Desynced;
			mLastError = "resync payload upload failed (no acks)";
			g_ATLCNetplay("host: resync transfer failed (no acks) — terminating session");
		}
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
	if (mPeerPath == PeerPath::Relay && PeerIsLobby()) {
		g_ATLCNetplay("joiner: relay-only session — mid-session "
			"direct rescue will probe %zu host candidate%s",
			mDirectRescueCandidates.size(),
			mDirectRescueCandidates.size() == 1 ? "" : "s");
	}
}

// ---------------------------------------------------------------------------
// Lockstep hot path
// ---------------------------------------------------------------------------

void Coordinator::HandleInputPacket(const NetInputPacket& pkt, uint64_t nowMs) {
	if (mPhase != Phase::Lockstepping) return;
	// Post-resync handshake: a peer input packet from the post-resync
	// timeline (baseFrame at or past the agreed resume point) proves
	// the host got our Done and returned to Lockstepping.  Stop
	// retransmitting.  Guard on baseFrame so a stale pre-resync
	// packet that happened to arrive after we resumed doesn't wrongly
	// clear the flag while the host is still stuck.
	if (mAwaitingHostResyncAck && pkt.baseFrame >= mResyncResumeFrame) {
		mAwaitingHostResyncAck = false;
	}
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
	if (n) WrapAndSend(mTxBuf, n, mPeer);
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
	// A desync Bye from the peer is terminal regardless of our current
	// phase — Lockstepping (their apply-time check fired before ours)
	// or Resyncing (they hit their flap limit / apply error mid-
	// recovery).  Without handling the Resyncing case, the UI would
	// show "clean exit" for an unrecoverable desync.
	if (b.reason == kByeDesyncDetected &&
	    (mPhase == Phase::Lockstepping || mPhase == Phase::Resyncing)) {
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
		for (const auto& alt : pd.altPeers) {
			SendReject(kRejectHostFull, alt);
		}
	}
	mPendingDecisions.clear();
	SendBye(byeReason);
	mPhase = Phase::Ended;
	// Log the locally-initiated end so a session that disappears from
	// the user's perspective leaves a clear trail.  HandleBye logs
	// peer-initiated ends; this covers our side (Shutdown, StopHost,
	// app-level disconnect, manual Leave).  reason==kByeCleanExit (0)
	// is the common case; non-zero values surface explicit reasons.
	g_ATLCNetplay("%s: session ended locally (reason=%u)",
		mRole == Role::Host ? "host" :
		mRole == Role::Joiner ? "joiner" : "idle",
		(unsigned)byeReason);
}

void Coordinator::SendBye(uint32_t reason) {
	if (!mPeerKnown) return;
	NetBye b { kMagicBye, reason };
	size_t n = EncodeBye(b, mTxBuf, sizeof mTxBuf);
	if (n) WrapAndSend(mTxBuf, n, mPeer);
}

bool Coordinator::SendEmote(uint8_t iconId) {
	if (mPhase != Phase::Lockstepping) return false;
	if (!mPeerKnown) return false;
	NetEmote e;
	e.iconId = iconId;
	size_t n = EncodeEmote(e, mTxBuf, sizeof mTxBuf);
	if (!n) return false;
	return WrapAndSend(mTxBuf, n, mPeer);
}

// ---------------------------------------------------------------------------
// Packet senders
// ---------------------------------------------------------------------------

void Coordinator::FillHello(NetHello& h) const {
	h.magic = kMagicHello;
	h.protocolVersion = kProtocolVersion;
	h.flags = 0;
	// Per-attempt session nonce — lets the host recognise Hellos from
	// the v3 candidate spray (LAN / srflx / loopback / hairpin) as the
	// same joiner rather than N distinct join requests.  Hosts running
	// v1.0 ignore it; new hosts dedupe on it and fall back to handle
	// when a peer left it zero.
	std::memcpy(h.sessionNonce, mSessionNonce, kSessionNonceLen);
	h.osRomHash = mOsRomHash;
	h.basicRomHash = mBasicRomHash;
	std::memcpy(h.playerHandle, mPlayerHandle, kHandleLen);
	h.acceptTos = mAcceptTos ? 1u : 0u;
	std::memcpy(h.entryCodeHash, mEntryCodeHash, kEntryCodeHashLen);
}

void Coordinator::SendHello() {
	NetHello h;
	FillHello(h);
	size_t n = EncodeHello(h, mTxBuf, sizeof mTxBuf);
	if (n && mPeerKnown) WrapAndSend(mTxBuf, n, mPeer);
}

void Coordinator::GenerateSessionNonce() {
	// std::random_device seeds a 32-bit engine; draw 4x to fill 16 B.
	// Not cryptographically strong, but the nonce only needs to be
	// collision-resistant within a host's pending-decision window
	// (<= kMaxPendingDecisions * a few seconds) — 128 bits is ample.
	std::random_device rd;
	std::mt19937 eng(rd());
	for (size_t i = 0; i < kSessionNonceLen; i += 4) {
		uint32_t v = eng();
		std::memcpy(mSessionNonce + i, &v, 4);
	}
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
	if (n && mPeerKnown) WrapAndSend(mTxBuf, n, mPeer);
}

void Coordinator::SendReject(uint32_t reason, const Endpoint& to) {
	NetReject r { kMagicReject, reason };
	size_t n = EncodeReject(r, mTxBuf, sizeof mTxBuf);
	if (n) mTransport.SendTo(mTxBuf, n, to);
}

// ---------------------------------------------------------------------------
// Misc
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Resync
// ---------------------------------------------------------------------------

void Coordinator::BeginHostResync(uint64_t nowMs) {
	if (mRole != Role::Host) return;
	if (mPhase != Phase::Lockstepping) return;

	// Flap limiter.  A resync that itself leads to another immediate
	// desync signals a deterministic bug no amount of state-copying
	// will fix; fall back to the legacy "Bye + Desynced" behaviour.
	if (mResyncCountInWindow == 0 ||
	    nowMs - mResyncWindowStartMs > kResyncFlapWindowMs) {
		mResyncWindowStartMs = nowMs;
		mResyncCountInWindow = 1;
	} else {
		++mResyncCountInWindow;
	}
	if (mResyncCountInWindow > kMaxResyncsPerWindow) {
		g_ATLCNetplay("host: resync flap limit reached (%d in %llu ms) — "
			"declaring unrecoverable desync",
			mResyncCountInWindow,
			(unsigned long long)(nowMs - mResyncWindowStartMs));
		SendBye(kByeDesyncDetected);
		mPhase = Phase::Desynced;
		mLastError = "repeated desync — session unrecoverable";
		return;
	}

	// resumeFrame = next-frame-to-apply.  The savestate the glue layer
	// is about to capture represents the state AFTER applying
	// (resumeFrame - 1) and BEFORE frame resumeFrame.  The seed hash
	// we seed into the joiner's lockstep is our local hash at
	// (resumeFrame - 1) — that's the hash the app layer computed on
	// the very state it's about to serialise.
	++mResyncEpoch;
	mResyncResumeFrame = mLoop.CurrentFrame();
	// Seed hash = our local hash of the last-applied frame, which is
	// exactly the hash the joiner will compute post-apply if the
	// savestate round-tripped cleanly.  Falls back to 0 if the slot
	// has already been recycled (shouldn't happen in practice — ring
	// is 256 frames, the last-applied slot is always fresh).  A zero
	// seed is treated as "no hash yet" by OnPeerInputPacket, which
	// just suppresses the retro-check for one frame — safe either way.
	mResyncSeedHash = 0;
	if (mResyncResumeFrame > 0) {
		mLoop.GetLocalHashAt(mResyncResumeFrame - 1, mResyncSeedHash);
	}

	// Clear the lockstep desync flag so CanAdvance() unblocks once we
	// transition back to Lockstepping.  The ResumeAt() call on resume
	// will wipe rings; we could clear it there, but clearing here is
	// harmless and keeps the invariant "mPhase != Lockstepping implies
	// CanAdvance is irrelevant".
	mLoop.ClearDesync();

	mSnapTxBuffer.clear();
	mResyncTxBuffer.clear();
	mNeedsResyncCapture = true;
	mResyncStartMs = nowMs;
	mPhase = Phase::Resyncing;

	g_ATLCNetplay("host: initiating resync epoch=%u resumeFrame=%u",
		(unsigned)mResyncEpoch, (unsigned)mResyncResumeFrame);
}

void Coordinator::SubmitResyncCapture(const uint8_t* data, size_t len) {
	if (mPhase != Phase::Resyncing) return;
	if (mRole != Role::Host) return;
	if (!mNeedsResyncCapture) return;
	mNeedsResyncCapture = false;

	if (!data || len == 0) {
		SendBye(kByeDesyncDetected);
		mPhase = Phase::Desynced;
		mLastError = "host failed to capture savestate";
		g_ATLCNetplay("host: SubmitResyncCapture got empty payload");
		return;
	}

	// Keep host's copy alive for the duration of the SnapTx transfer.
	mResyncTxBuffer.assign(data, data + len);

	const uint32_t chunks = (uint32_t)((mResyncTxBuffer.size() +
		kSnapshotChunkSize - 1) / kSnapshotChunkSize);

	// Send NetResyncStart — the joiner needs this BEFORE chunks so it
	// can arm its receiver for this epoch's chunk count.
	NetResyncStart s;
	s.magic        = kMagicResyncStart;
	s.epoch        = mResyncEpoch;
	s.stateBytes   = (uint32_t)mResyncTxBuffer.size();
	s.stateChunks  = chunks;
	s.resumeFrame  = mResyncResumeFrame;
	s.seedHash     = mResyncSeedHash;
	size_t n = EncodeResyncStart(s, mTxBuf, sizeof mTxBuf);
	if (n && mPeerKnown) {
		WrapAndSend(mTxBuf, n, mPeer);
		// Record the send so the retransmit watchdog in Poll() doesn't
		// fire for the next kResyncStartRetryMs.
		// (nowMs would be cleaner but SubmitResyncCapture isn't given
		// a clock; leave it at 0 — the watchdog treats that as "long
		// ago" and will issue one extra retransmit on the next tick,
		// which is harmless.)
	}

	// Kick off chunk transfer (uses existing SnapTx machinery).
	mSnapTx.Begin(mResyncTxBuffer.data(), mResyncTxBuffer.size());
	mSnapProgressMilestone = 0;
	g_ATLCNetplay("host: resync upload starting (%u bytes / %u chunks, epoch=%u)",
		(unsigned)mResyncTxBuffer.size(), (unsigned)chunks, (unsigned)mResyncEpoch);
}

void Coordinator::HandleResyncStart(const NetResyncStart& s, uint64_t nowMs) {
	if (mRole != Role::Joiner) return;
	// Accept from Lockstepping (normal trigger) or a duplicate while
	// already Resyncing (host retransmit before we ACKed first chunk).
	if (mPhase != Phase::Lockstepping && mPhase != Phase::Resyncing) return;

	if (mPhase == Phase::Resyncing && s.epoch == mResyncEpoch) {
		// Retransmit of the same start — nothing to do, receiver is
		// already armed.
		return;
	}

	BeginJoinerResync(s, nowMs);
}

void Coordinator::BeginJoinerResync(const NetResyncStart& s, uint64_t nowMs) {
	mResyncEpoch       = s.epoch;
	mResyncResumeFrame = s.resumeFrame;
	mResyncSeedHash    = s.seedHash;

	// Clear pre-resync lockstep state so the (now meaningless) input
	// queue can't race with the new timeline.
	mLoop.ClearDesync();

	mSnapRx.Begin(s.stateChunks, s.stateBytes);
	mSnapProgressMilestone = 0;
	mNeedsResyncApply = false;
	mResyncStartMs = nowMs;
	mPhase = Phase::Resyncing;

	g_ATLCNetplay("joiner: resync incoming (epoch=%u, %u bytes / %u chunks, "
		"resumeFrame=%u)",
		(unsigned)s.epoch, (unsigned)s.stateBytes,
		(unsigned)s.stateChunks, (unsigned)s.resumeFrame);

	// Edge case: zero-byte payload (host captured nothing) — treat as
	// immediate apply-ready so the app can fast-fail the session.
	if (s.stateChunks == 0) mNeedsResyncApply = true;
}

void Coordinator::AcknowledgeResyncApplied() {
	if (mPhase != Phase::Resyncing) return;
	if (mRole != Role::Joiner) return;
	if (!mNeedsResyncApply) return;
	mNeedsResyncApply = false;

	// Resume LockstepLoop at the agreed frame with ring state wiped.
	// Host will do the same on its side when ResyncDone arrives.
	mLoop.ResumeAt(mResyncResumeFrame, mResyncSeedHash);
	mDesyncLogged = false;

	NetResyncDone d;
	d.magic       = kMagicResyncDone;
	d.epoch       = mResyncEpoch;
	d.resumeFrame = mResyncResumeFrame;
	size_t n = EncodeResyncDone(d, mTxBuf, sizeof mTxBuf);
	if (n && mPeerKnown) WrapAndSend(mTxBuf, n, mPeer);

	// Arm the Poll-driven retransmit.  If this first Done is lost the
	// host is stuck in Resyncing with no recovery signal — joiner
	// input packets get dropped there on the phase check — so we keep
	// re-sending every kResyncDoneRetryMs until we see host input.
	// mResyncDoneLastSentMs left at 0 so the very next Poll fires a
	// duplicate Done immediately; after that, pacing takes over.
	mAwaitingHostResyncAck = true;
	mResyncDoneLastSentMs  = 0;

	mPhase = Phase::Lockstepping;
	g_ATLCNetplay("joiner: resync applied, resumed at frame %u (epoch %u)",
		(unsigned)mResyncResumeFrame, (unsigned)mResyncEpoch);
}

void Coordinator::HandleResyncDone(const NetResyncDone& d, uint64_t /*nowMs*/) {
	if (mRole != Role::Host) return;
	if (mPhase != Phase::Resyncing) return;
	if (d.epoch != mResyncEpoch) {
		// Stale ack for a previous resync — ignore.
		return;
	}

	// Host now resumes its own lockstep at the same frame the joiner
	// jumped to.  Ring state wiped; hash ring re-seeded from our local
	// post-capture hash.
	mLoop.ResumeAt(mResyncResumeFrame, mResyncSeedHash);
	mDesyncLogged = false;
	mPhase = Phase::Lockstepping;
	mResyncTxBuffer.clear();    // free memory
	g_ATLCNetplay("host: resync complete, resumed at frame %u (epoch %u)",
		(unsigned)mResyncResumeFrame, (unsigned)mResyncEpoch);
}

} // namespace ATNetplay

// Forward the decoded emote to the frontend.  Declared in
// ui/emotes/emote_netplay.h but we do not include that here so the
// netplay module stays independent of the UI module.  Symbol is
// defined in emote_netplay.cpp.
namespace ATEmoteNetplay {
void OnReceivedFromPeer(uint8_t iconId);
}

namespace ATNetplay {

void Coordinator::HandleEmote(const NetEmote& e) {
	if (mPhase != Phase::Lockstepping) return;
	ATEmoteNetplay::OnReceivedFromPeer(e.iconId);
}

// ---------------------------------------------------------------------------
// SimHashDiag (per-subsystem hash exchange on desync / session start)
// ---------------------------------------------------------------------------

void Coordinator::SubmitLocalSimHashDiag(const NetSimHashDiag& d) {
	// Cache locally first so a peer packet that already arrived (race
	// across the wire) can be matched against us without waiting for
	// another Submit call.
	mLocalDiag    = d;
	mLocalDiagSet = true;

	// Emit to the peer.  Unreliable one-shot — if it drops, the peer
	// falls back to its own local-only breakdown log and the DIFF line
	// simply doesn't appear on that peer.  Safer than adding retry
	// state for a purely diagnostic channel.
	if (mPeerKnown) {
		size_t n = EncodeSimHashDiag(d, mTxBuf, sizeof mTxBuf);
		if (n) WrapAndSend(mTxBuf, n, mPeer);
	}

	MaybeLogSimHashDiff();
}

void Coordinator::HandleSimHashDiag(const NetSimHashDiag& d) {
	mPeerDiag    = d;
	mPeerDiagSet = true;
	MaybeLogSimHashDiff();
}

void Coordinator::MaybeLogSimHashDiff() {
	if (!mLocalDiagSet || !mPeerDiagSet) return;
	if (mLocalDiag.frame != mPeerDiag.frame) {
		// Out-of-order breakdown pair (e.g. our desync-frame log
		// crossed the peer's session-start frame-0 log in flight).
		// Nothing actionable to log until both sides agree on the
		// frame in question.
		return;
	}
	if ((int64_t)mLocalDiag.frame == mLastDiffLoggedFrame) return;
	mLastDiffLoggedFrame = (int64_t)mLocalDiag.frame;

	// Field-by-field diff.  Label each line with "MATCH" or "DIFF" so
	// a grep on DIFF localizes the first diverging subsystem at a
	// glance.  Intentionally verbose — this is what users will copy-
	// paste into bug reports, and a single-line summary hides which
	// subsystem tipped over.
	const char* role =
		mRole == Role::Host   ? "host" :
		mRole == Role::Joiner ? "joiner" : "idle";
	g_ATLCNetplay("simhash diff @frame %u (%s vs peer):",
		(unsigned)mLocalDiag.frame, role);

	auto cmp = [](const char* label, uint32_t ours, uint32_t theirs) {
		g_ATLCNetplay("  %-9s %s  ours=%08x  theirs=%08x",
			label,
			ours == theirs ? "MATCH" : "DIFF ",
			ours, theirs);
	};
	cmp("total",     mLocalDiag.total,     mPeerDiag.total);
	cmp("cpu",       mLocalDiag.cpuRegs,   mPeerDiag.cpuRegs);
	cmp("ram0",      mLocalDiag.ramBank0,  mPeerDiag.ramBank0);
	cmp("ram1",      mLocalDiag.ramBank1,  mPeerDiag.ramBank1);
	cmp("ram2",      mLocalDiag.ramBank2,  mPeerDiag.ramBank2);
	cmp("ram3",      mLocalDiag.ramBank3,  mPeerDiag.ramBank3);
	cmp("gtia",      mLocalDiag.gtiaRegs,  mPeerDiag.gtiaRegs);
	cmp("antic",     mLocalDiag.anticRegs, mPeerDiag.anticRegs);
	cmp("pokey",     mLocalDiag.pokeyRegs, mPeerDiag.pokeyRegs);
	cmp("schedTick", mLocalDiag.schedTick, mPeerDiag.schedTick);
}

void Coordinator::FailWith(const char* msg) {
	mLastError = msg;
	mPhase = Phase::Failed;
	g_ATLCNetplay("%s session failed: %s",
		mRole == Role::Host ? "host" :
		mRole == Role::Joiner ? "joiner" : "idle",
		msg ? msg : "(no reason)");
}

// ---------------------------------------------------------------------------
// v4 NAT traversal — two-sided punch + relay fallback
// ---------------------------------------------------------------------------

void Coordinator::SetRelayContext(const uint8_t sessionIdBytes16[16],
                                  const char* lobbyHostPort) {
	// IDEMPOTENT — safe to call every Poll tick.  Caller (the action
	// loop in ui_netplay_actions.cpp) does call this per-tick after
	// the relay-first hoist, so any work done here multiplies by the
	// frame rate.  Cheap fast path: if the new args match the cached
	// state, do nothing — no Resolve(), no log line.
	const bool emptyInput =
		(!sessionIdBytes16 || !lobbyHostPort || !*lobbyHostPort);
	if (emptyInput) {
		// Empty input means "tear down".  Only act if we currently
		// have something to tear down — otherwise the per-tick caller
		// would generate spurious state churn.
		if (mHasRelaySessionId || mLobbyRelayKnown) {
			mHasRelaySessionId = false;
			mLobbyRelayKnown   = false;
		}
		return;
	}

	const bool sessionIdMatches =
		mHasRelaySessionId &&
		std::memcmp(mRelaySessionId, sessionIdBytes16, 16) == 0;
	const bool lobbyStrMatches =
		mLobbyRelayKnown &&
		mLobbyHostPortCached == lobbyHostPort;

	if (sessionIdMatches && lobbyStrMatches) {
		// No change — nothing to do, nothing to log.
		return;
	}

	// At least one input differs from the cached state.  Re-resolve
	// the lobby endpoint (a numeric "ip:port" form skips DNS, but
	// hostnames may not — we cache the result either way) and
	// update the flags atomically (single-threaded coord, no race).
	std::memcpy(mRelaySessionId, sessionIdBytes16, 16);
	mHasRelaySessionId = true;

	Endpoint ep;
	if (Transport::Resolve(lobbyHostPort, ep)) {
		mLobbyRelayEndpoint = ep;
		mLobbyRelayKnown = true;
		mLobbyHostPortCached = lobbyHostPort;
		char fmt[64];
		ep.Format(fmt, sizeof fmt);
		g_ATLCNetplay("relay context set: lobby=%s (auto-fallback armed)", fmt);
	} else {
		mLobbyRelayKnown = false;
		mLobbyHostPortCached.clear();
		g_ATLCNetplay("relay context: failed to resolve \"%s\"", lobbyHostPort);
	}
}

void Coordinator::GetSessionNonce(uint8_t out[kSessionNonceLen]) const {
	std::memcpy(out, mSessionNonce, kSessionNonceLen);
}

void Coordinator::IngestPeerHint(const uint8_t nonce16[kSessionNonceLen],
                                 const char* candidatesSemicolonList) {
	if (mRole != Role::Host) return;
	if (!candidatesSemicolonList || !*candidatesSemicolonList) return;
	// During the handshake (WaitingForJoiner) we DO add to mPunchTargets
	// for the immediate punch-burst.  After handshake we only refresh
	// mDirectRescueCandidates so the mid-session direct-rescue prober
	// has up-to-date addresses (the joiner's NAT mapping may have
	// changed since the original engagement, which is exactly the
	// scenario where rescue can succeed).
	const bool armPunchTargets = (mPhase == Phase::WaitingForJoiner);

	// Parse the semicolon/comma list; tolerate whitespace.
	const char* s = candidatesSemicolonList;
	std::string cur;
	bool anyAdded = false;
	auto flush = [&](const std::string& tok) {
		if (tok.empty()) return;
		Endpoint ep;
		if (!Transport::Resolve(tok.c_str(), ep)) {
			g_ATLCNetplay("host: hint candidate unresolved: \"%s\"", tok.c_str());
			return;
		}
		// Always accumulate into the persistent direct-rescue set
		// (deduped on endpoint only — nonce-keyed bookkeeping is
		// reserved for mPunchTargets, the handshake-phase prober).
		bool addedToRescueSet = false;
		{
			bool dup = false;
			for (const auto& e : mDirectRescueCandidates) {
				if (e.Equals(ep)) { dup = true; break; }
			}
			if (!dup) {
				mDirectRescueCandidates.push_back(ep);
				addedToRescueSet = true;
			}
		}
		if (!armPunchTargets) {
			// Lockstepping phase: only the rescue set matters.  Log
			// the first time we learn each candidate and stay quiet
			// thereafter so the heartbeat refresh doesn't spam.
			if (addedToRescueSet) {
				char fmt[64];
				ep.Format(fmt, sizeof fmt);
				g_ATLCNetplay("host: rescue candidate %s (%s) — "
					"queued for direct-rescue probes",
					tok.c_str(), fmt);
			}
			return;
		}
		// Dedupe on (nonce, endpoint) for the handshake punch list.
		for (auto& t : mPunchTargets) {
			if (std::memcmp(t.nonce, nonce16, kSessionNonceLen) == 0 &&
			    t.ep.Equals(ep)) {
				return;   // already queued
			}
		}
		PunchTarget t;
		std::memcpy(t.nonce, nonce16, kSessionNonceLen);
		t.ep          = ep;
		t.firstSentMs = 0;   // sentinel: PumpPunchProbes will initialise
		t.lastSentMs  = 0;
		t.sentCount   = 0;
		mPunchTargets.push_back(std::move(t));
		anyAdded = true;
		char fmt[64];
		ep.Format(fmt, sizeof fmt);
		g_ATLCNetplay("host: peer-hint endpoint %s (%s) — will probe",
			tok.c_str(), fmt);
	};
	for (const char* p = s; *p; ++p) {
		if (*p == ';' || *p == ',') { flush(cur); cur.clear(); }
		else if (*p != ' ' && *p != '\t') cur.push_back(*p);
	}
	flush(cur);

	if (anyAdded) mHostHasSeenHint = true;
	// Don't send probes here — we don't have a monotonic clock.  The
	// next Poll() (one frame away at 60 Hz) will drive PumpPunchProbes
	// with a real nowMs, do the initial burst, and start the sustain
	// timer with correct bookkeeping.
}

void Coordinator::PumpPunchProbes(uint64_t nowMs) {
	if (mPunchTargets.empty()) return;
	// Drop targets that are past the sustain window.  Skip entries
	// whose firstSentMs is still the 0-sentinel (they haven't been
	// probed yet, so nowMs-0 is an artefact of the sentinel, not a
	// real elapsed-time measurement).
	for (auto it = mPunchTargets.begin(); it != mPunchTargets.end(); ) {
		if (it->firstSentMs != 0 &&
		    nowMs - it->firstSentMs > kPunchSustainDurationMs) {
			it = mPunchTargets.erase(it);
		} else ++it;
	}
	for (auto& t : mPunchTargets) {
		NetPunch probe;
		std::memcpy(probe.sessionNonce, t.nonce, kSessionNonceLen);
		uint8_t buf[kWirePunchSize];
		size_t n = EncodePunch(probe, buf, sizeof buf);
		if (!n) break;
		if (t.firstSentMs == 0) {
			// First pump after IngestPeerHint: fire the initial
			// burst (5 probes back-to-back) and seed the sustain
			// timer at nowMs.  Guard against nowMs==0 (extremely
			// early Poll tick) by biasing up so the sentinel check
			// still works — the subtraction later won't underflow
			// because kPunchSustainDurationMs > 1.
			uint64_t seed = nowMs ? nowMs : 1;
			for (int i = 0; i < kPunchBurstInitialCount; ++i) {
				mTransport.SendTo(buf, n, t.ep);
			}
			t.sentCount   = kPunchBurstInitialCount;
			t.firstSentMs = seed;
			t.lastSentMs  = seed;
			continue;
		}
		if (nowMs - t.lastSentMs < kPunchSustainIntervalMs) continue;
		mTransport.SendTo(buf, n, t.ep);
		t.lastSentMs = nowMs;
		++t.sentCount;
	}
}

bool Coordinator::WrapAndSend(const uint8_t* bytes, size_t n,
                              const Endpoint& peer) {
	if (mPeerPath != PeerPath::Relay ||
	    !mLobbyRelayKnown || !mHasRelaySessionId) {
		return mTransport.SendTo(bytes, n, peer);
	}
	return SendWrappedViaLobby(bytes, n);
}

bool Coordinator::SendWrappedViaLobby(const uint8_t* bytes, size_t n) {
	if (!mLobbyRelayKnown || !mHasRelaySessionId) return false;
	// Wrap in ASDF frame targeted at the lobby.  The server strips the
	// header and delivers inner bytes to the other side's registered
	// srflx.  Inner max is kSnapshotChunkSize + header = ~1216 bytes,
	// so we can reuse a small stack buffer.
	uint8_t buf[kMaxDatagramSize + kWireRelayHeaderSize];
	NetRelayDataHeader h;
	std::memcpy(h.sessionId, mRelaySessionId, 16);
	h.role = (mRole == Role::Host) ? kRelayRoleHost : kRelayRoleJoiner;
	size_t wn = EncodeRelayFrame(h, bytes, n, buf, sizeof buf);
	if (!wn) return false;
	return mTransport.SendTo(buf, wn, mLobbyRelayEndpoint);
}

void Coordinator::SendRelayRegister() {
	if (mRelayRegistered) return;
	if (!mLobbyRelayKnown || !mHasRelaySessionId) return;
	NetRelayRegister r;
	std::memcpy(r.sessionId, mRelaySessionId, 16);
	r.role = (mRole == Role::Host) ? kRelayRoleHost : kRelayRoleJoiner;
	uint8_t buf[kWireRelayRegisterSize];
	size_t n = EncodeRelayRegister(r, buf, sizeof buf);
	if (!n) return;
	mTransport.SendTo(buf, n, mLobbyRelayEndpoint);
	mRelayRegistered = true;
	char fmt[64];
	mLobbyRelayEndpoint.Format(fmt, sizeof fmt);
	g_ATLCNetplay("%s: auto-relay engaged (lobby=%s, role=%s)",
		mRole == Role::Host ? "host" : "joiner", fmt,
		mRole == Role::Host ? "host" : "joiner");
}

void Coordinator::MaybePrearmRelay(uint64_t nowMs) {
	// Relay-first handshake: register with the lobby's relay table
	// the moment the relay context becomes available.  No T+3s wait,
	// no waiting for a peer-hint to arrive on the heartbeat (which
	// could be 30 seconds out).  The lobby will start forwarding
	// inbound ASDF frames the instant both peers are registered, so
	// the joiner's wrapped Hello can land within one lobby round-
	// trip (~100-300 ms) instead of 30+ seconds.
	// Direct punching keeps running in parallel; if a direct
	// candidate replies first, mPeer gets set to that address and
	// mPeerPath stays Direct.  If the relay path wins, mPeer ends
	// up being the lobby and the receive-loop / Hello-handler flip
	// mPeerPath to Relay (see the from.Equals(mLobbyRelayEndpoint)
	// branches there).  Either way, the user-visible "host sees
	// joiner" event fires as soon as ONE path delivers the Hello.
	if (mRelayRegistered) return;
	if (mPeerPath == PeerPath::Relay) return;
	if (!mHasRelaySessionId || !mLobbyRelayKnown) return;

	if (mRole == Role::Joiner && mPhase == Phase::Handshaking) {
		SendRelayRegister();
		g_ATLCNetplay("joiner: relay-first registration sent "
			"(direct punching continues in parallel)");
		return;
	}
	if (mRole == Role::Host &&
	    (mPhase == Phase::WaitingForJoiner ||
	     mPhase == Phase::Handshaking ||
	     mPhase == Phase::SendingSnapshot)) {
		// No mHostHasSeenHint gate — relay-first means we register
		// PROACTIVELY so joiner-sent ASDF frames have somewhere to
		// go from the very first poll tick.  Saves the up-to-30s
		// heartbeat wait that previously delayed the host's relay
		// registration to "after we learn a joiner exists."
		SendRelayRegister();
		g_ATLCNetplay("host: relay-first registration sent "
			"(awaiting joiners; direct punching ready)");
	}
}

void Coordinator::MaybeEngageRelay(uint64_t nowMs) {
	if (mPeerPath == PeerPath::Relay) return;
	if (!mHasRelaySessionId || !mLobbyRelayKnown) return;

	// Joiner-side: direct punch has had long enough; start relaying.
	if (mRole == Role::Joiner && mPhase == Phase::Handshaking) {
		if (mPhaseStartMs == 0) return;
		if (nowMs - mPhaseStartMs < kRelayFallbackAfterMs) return;
		SendRelayRegister();   // idempotent if pre-arm already fired
		mPeerPath = PeerPath::Relay;
		OnPeerPathFlippedToRelay(nowMs);
		g_ATLCNetplay("joiner: punch failed, switching to relay at T+%llu ms",
			(unsigned long long)(nowMs - mPhaseStartMs));
		// Fire a wrapped Hello through the lobby so the joiner's
		// spray immediately reaches the host once the host also
		// registers.  Subsequent retransmits follow the same path
		// via WrapAndSend from the Poll retransmit loop.
		NetHello h;
		FillHello(h);
		uint8_t hb[kWireHelloSize];
		size_t n = EncodeHello(h, hb, sizeof hb);
		if (n) WrapAndSend(hb, n, mLobbyRelayEndpoint);
		return;
	}
	// Host-side: if a hint has been received and we still have no
	// peer after the fallback window, register with the lobby too so
	// joiner-sent ASDF frames reach us.  Without both sides being
	// registered the lobby drops ASDF silently.  Extended to cover
	// Handshaking and SendingSnapshot for the same reason as the
	// pre-arm case above — joiner-side fallback runs on a 6 s timer
	// from the joiner's mPhaseStartMs and is independent of how
	// quickly the host accepts the join.
	if (mRole == Role::Host &&
	    (mPhase == Phase::WaitingForJoiner ||
	     mPhase == Phase::Handshaking ||
	     mPhase == Phase::SendingSnapshot)) {
		if (mPhaseStartMs == 0) return;
		if (nowMs - mPhaseStartMs < kRelayFallbackAfterMs) return;
		if (!mHostHasSeenHint) return;  // no joiner known yet
		SendRelayRegister();   // idempotent if pre-arm already fired
		mPeerPath = PeerPath::Relay;
		OnPeerPathFlippedToRelay(nowMs);
		g_ATLCNetplay("host: punch failed, switching to relay at T+%llu ms",
			(unsigned long long)(nowMs - mPhaseStartMs));
	}
}

void Coordinator::MaybeRescueRelayMidSession(uint64_t nowMs) {
	// Mid-session resilience: a router reboot, a transient NAT
	// eviction, or a brief WAN outage can stall a Direct path during
	// Lockstepping.  Today such an outage just freezes the session
	// until the 10-minute peer-silence backstop fires.  Re-arming the
	// relay (one extra ASGR + flip to Relay) keeps the session alive
	// because the lobby's RelayTable has a 30 s idle window — long
	// enough to cover typical hiccups but short enough that crashed
	// peers don't keep slots forever.
	if (mPhase != Phase::Lockstepping) return;
	if (mPeerPath == PeerPath::Relay) return;
	if (!mHasRelaySessionId || !mLobbyRelayKnown) return;

	// Use the lockstep peer-age (last received peer packet) as the
	// silence signal — same source the HUD uses for the "(500 ms old)"
	// indicator.  PeerTimedOut returns true once the lockstep loop's
	// peer-input gate has been quiet for the threshold.
	if (!mLoop.PeerTimedOut(nowMs, kMidSessionRelayRescueAfterMs))
		return;

	SendRelayRegister();
	mPeerPath = PeerPath::Relay;
	OnPeerPathFlippedToRelay(nowMs);
	g_ATLCNetplay("%s: peer silent %llu ms in Lockstepping — re-arming relay",
		mRole == Role::Host ? "host" : "joiner",
		(unsigned long long)kMidSessionRelayRescueAfterMs);
}

void Coordinator::OnPeerPathFlippedToRelay(uint64_t nowMs) {
	// Stamp the engagement time and reset the direct-probe schedule so
	// a fresh round of probes starts on every relay flip.  The probe
	// pump (MaybeProbeDirectFromRelay) compares mLastDirectProbeMs to
	// nowMs; seeding it at engagement time means the first probe waits
	// kDirectProbeFastIntervalMs (1 s) before firing — long enough to
	// let the relay path stabilise but short enough to recover well
	// inside the 10 s budget if the network heals immediately.
	mRelayEngagedAtMs    = nowMs;
	mLastDirectProbeMs   = nowMs;
	mDirectProbeAttempts = 0;

	// Flap suppression: count engagements and give up on direct
	// rescue once we've flapped too many times.  Stops the
	// "Direct lost" / "Direct restored" toast spiral on connections
	// where direct works just well enough to recover then fail again.
	++mRelayEngageCount;
	if (!mDirectGiveUp &&
	    mRelayEngageCount >= kMaxRelayEngagesBeforeGivingUp) {
		mDirectGiveUp = true;
		g_ATLCNetplay("%s: direct path unstable (engaged relay %u "
			"times) — staying on relay for the rest of the session",
			mRole == Role::Host ? "host" : "joiner",
			(unsigned)mRelayEngageCount);
	}
}

void Coordinator::MaybeProbeDirectFromRelay(uint64_t nowMs) {
	// Only probe in Lockstepping (the only phase where direct rescue
	// is meaningful — in handshake phases the existing Hello-spray
	// already covers direct probing).
	if (mPhase != Phase::Lockstepping) return;
	if (mPeerPath != PeerPath::Relay) return;
	if (!mPeerKnown) return;
	if (mDirectGiveUp) return;  // flap-suppression: stay on relay forever

	// Choose the probe targets:
	//   - Normal case (mPeer is the real direct peer endpoint, e.g.
	//     after a clean direct handshake that later flapped to relay):
	//     probe just mPeer.  One packet per probe interval.
	//   - Relay-only case (PeerIsLobby — initial direct never worked,
	//     so mPeer was set to the lobby endpoint at Hello time): probe
	//     every persistent rescue candidate we know about (host: peer-
	//     hint endpoints from the lobby heartbeat; joiner: the host's
	//     announced lobby endpoints from BeginJoinMulti).  This is the
	//     scenario that lets a session that started on relay recover
	//     direct mid-game once the network heals.  Bandwidth is bounded
	//     — a typical candidate count is 2-3, probes are 24 bytes
	//     apiece, and the throttle is 1 s for the first 10 attempts
	//     then 5 s; even at 5 candidates that is < 150 B/s peak and
	//     ~24 B/s steady state.
	const bool useCandidateList = PeerIsLobby();
	if (useCandidateList && mDirectRescueCandidates.empty()) {
		// Nothing to probe and mPeer is unusable — stay on relay until
		// the lobby heartbeat (host) or some future refresh (joiner)
		// gives us a real address.
		return;
	}

	// Throttle: 1 s between probes for the first kDirectProbeFastAttempts,
	// then 5 s.
	const uint64_t interval =
		(mDirectProbeAttempts < kDirectProbeFastAttempts)
			? kDirectProbeFastIntervalMs
			: kDirectProbeSlowIntervalMs;
	if (nowMs - mLastDirectProbeMs < interval) return;

	// Build the probe.  Use the same NetPunch packet the initial-
	// handshake probing path uses — the receiver already has a
	// kMagicPunch handler we can extend with a Lockstepping+Relay
	// rescue branch.  Carry mSessionNonce so the receiver can
	// authenticate the probe (otherwise any random datagram with the
	// right magic would trigger a bogus flip).
	NetPunch p;
	std::memcpy(p.sessionNonce, mSessionNonce, sizeof p.sessionNonce);
	uint8_t buf[kWirePunchSize];
	size_t n = EncodePunch(p, buf, sizeof buf);
	if (!n) return;

	// Send DIRECT — bypass WrapAndSend so the packet is NOT wrapped in
	// ASDF and routed through the relay.  The whole point is to test
	// whether direct UDP works again.
	if (useCandidateList) {
		for (const auto& ep : mDirectRescueCandidates) {
			// Defensive: never punch the lobby itself even if it
			// somehow ended up in the candidate list.
			if (mLobbyRelayKnown && ep.Equals(mLobbyRelayEndpoint)) continue;
			mTransport.SendTo(buf, n, ep);
		}
	} else {
		mTransport.SendTo(buf, n, mPeer);
	}
	mLastDirectProbeMs = nowMs;
	++mDirectProbeAttempts;

	// Log only the FIRST probe of a given relay engagement so the log
	// stays calm.  Subsequent probes are silent; the success/failure
	// outcome is what matters and that gets logged separately.
	if (mDirectProbeAttempts == 1) {
		if (useCandidateList) {
			g_ATLCNetplay("%s: probing direct from relay "
				"(relay-only session, %zu candidate%s)",
				mRole == Role::Host ? "host" : "joiner",
				mDirectRescueCandidates.size(),
				mDirectRescueCandidates.size() == 1 ? "" : "s");
		} else {
			char ep[32] = {};
			mPeer.Format(ep, sizeof ep);
			g_ATLCNetplay("%s: probing direct from relay (peer=%s)",
				mRole == Role::Host ? "host" : "joiner", ep);
		}
	}
}

void Coordinator::BumpCandidateRx(const Endpoint& from, bool isReject) {
	for (auto& c : mCandidateStats) {
		if (c.ep.Equals(from)) {
			++c.rxPackets;
			if (isReject) ++c.rxRejects;
			return;
		}
	}
}

} // namespace ATNetplay
