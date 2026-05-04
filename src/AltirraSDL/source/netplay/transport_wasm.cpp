// Altirra SDL3 netplay - WasmTransport (WSS over emscripten_websocket).
//
// Single-threaded WASM build: all the callbacks below fire on the
// browser's main thread (same thread that drives the emulator main
// loop).  No locks needed — std::deque insertion in OnMessage is
// safe because the main loop is paused while the JS event-loop runs
// the callback, and a new RecvFrom call won't preempt mid-callback.
//
// Inner protocol bytes are byte-identical to UDP (Hello/Welcome/Input
// /etc.); the lobby's WS bridge unwraps the 1-byte role envelope on
// our outbound and prepends a recipient-role byte on our inbound.

#include <stdafx.h>

#include "transport_wasm.h"

#include "packets.h"   // for kMaxDatagramSize sanity comment

#include <at/atcore/logging.h>

#include <cstdio>
#include <cstring>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>   // emscripten_get_now
#endif

extern ATLogChannel g_ATLCNetplay;

namespace ATNetplay {

namespace {

// Bound on the inbound queue.  256 frames @ ~1.2 KB/frame ≈ 300 KB
// worst case; fits comfortably even when the browser tab is
// backgrounded and the emulator's main loop falls behind.  Drop-
// oldest on overflow keeps the most-recent state available.
constexpr size_t kInboundCap = 256;

// Bound on the outbound queue.  Pre-handshake the Coordinator may
// emit a few Hello probes before the WS opens; cap is plenty.
constexpr size_t kOutboundCap = 64;

// Subprotocol header construction.  The browser's WebSocket
// constructor accepts a list of subprotocols; we use this to ferry
// session+role+token without exposing them in the URL (Caddy logs
// URIs, never headers/subprotocols).  Format:
//
//   altirra-netplay.v1, session.<32hex>, role.<host|joiner>, token.<32hex>
//
// The lobby (server/lobby/ws_bridge.cpp) parses the same list and
// echoes back `altirra-netplay.v1` as the agreed subprotocol.

std::string BuildSubprotocolList(const std::string& sidHex,
                                 uint8_t role,
                                 const std::string& tokenHex) {
	std::string s = "altirra-netplay.v1,session.";
	s += sidHex;
	s += ",role.";
	s += (role == 0 ? "host" : "joiner");
	// Token is mandatory for host (proves we own the session record),
	// optional for joiner (the lobby bridges anyone in; auth happens
	// at the Altirra protocol layer).  Avoid emitting an empty token
	// stub since the bridge would parse it as malformed.
	if (!tokenHex.empty()) {
		s += ",token.";
		s += tokenHex;
	}
	return s;
}

// Sentinel "this packet came from THE peer" endpoint.  WasmTransport
// is single-peer — the Coordinator's Endpoint comparison only matches
// against this constant.  Format() returns "wss-relay" so logs read
// reasonably.
Endpoint MakeWsPeerEndpoint() {
	Endpoint e;
	// rawLen=1 + raw[0]=0xFF marks "WS sentinel".  Any WasmTransport
	// recv will yield this exact value, so Endpoint::Equals matches.
	e.rawLen = 1;
	e.raw[0] = 0xFF;
	return e;
}

}  // namespace

WasmTransport::WasmTransport(std::string lobbyHost,
                             std::string sidHex32,
                             uint8_t     role,
                             std::string tokenHex)
	: mLobbyHost(std::move(lobbyHost))
	, mSidHex(std::move(sidHex32))
	, mTokenHex(std::move(tokenHex))
	, mRole(role)
{
}

WasmTransport::~WasmTransport() {
	Close();
}

bool WasmTransport::Listen(uint16_t /*port*/) {
#if defined(__EMSCRIPTEN__)
	if (mWs != 0) return true;   // already opened

	mHandshakeStartMs = emscripten_get_now();

	std::string url = "wss://";
	url += mLobbyHost;
	url += "/netplay";

	std::string subp = BuildSubprotocolList(mSidHex, mRole, mTokenHex);

	EmscriptenWebSocketCreateAttributes attrs{};
	attrs.url             = url.c_str();
	attrs.protocols       = subp.c_str();
	attrs.createOnMainThread = EM_TRUE;

	EMSCRIPTEN_WEBSOCKET_T ws = emscripten_websocket_new(&attrs);
	if (ws <= 0) {
		g_ATLCNetplay("WasmTransport: emscripten_websocket_new failed: %d",
			(int)ws);
		return false;
	}
	mWs = (int32_t)ws;

	emscripten_websocket_set_onopen_callback(ws, this,
		&WasmTransport::OnOpenCb);
	emscripten_websocket_set_onmessage_callback(ws, this,
		&WasmTransport::OnMessageCb);
	emscripten_websocket_set_onerror_callback(ws, this,
		&WasmTransport::OnErrorCb);
	emscripten_websocket_set_onclose_callback(ws, this,
		&WasmTransport::OnCloseCb);
	return true;
#else
	(void)mWs; (void)mOpen; (void)mClosed;
	(void)mLobbyHost; (void)mSidHex; (void)mTokenHex; (void)mRole;
	return false;
#endif
}

bool WasmTransport::IsOpen() const {
	return mOpen && !mClosed;
}

bool WasmTransport::HasFailed() const {
#if defined(__EMSCRIPTEN__)
	// Fast path: already-failed transports stay failed.
	if (mFailed) return true;
	// Handshake timeout: only fires while we're waiting for the
	// browser's onopen — once mOpen flips true the deadline is moot.
	// mClosed implies Close() was called explicitly (Coordinator tear-
	// down) or onclose already fired; either way, no synthetic
	// failure on top.
	if (mOpen || mClosed) return false;
	if (mHandshakeStartMs <= 0.0) return false;
	const double elapsed = emscripten_get_now() - mHandshakeStartMs;
	if (elapsed < kHandshakeTimeoutMs) return false;
	// Synthesise a close-equivalent failure record.  Code 4998 is
	// outside the IANA-assigned range AND the lobby's documented 4000-
	// 4099 application-policy band, so it can never collide with a
	// real lobby status.  The text mirrors the OnCloseCb formatting
	// so callers don't need a separate code path.
	mFailed         = true;
	mCloseCode      = 4998;
	mCloseReason    = "handshake timeout";
	char buf[200];
	std::snprintf(buf, sizeof buf,
		"could not reach lobby — no response in %.0f s "
		"(DNS / TLS / Caddy route / firewall) (code 4998)",
		kHandshakeTimeoutMs / 1000.0);
	mFailureSummary.assign(buf);
	g_ATLCNetplay("WasmTransport: handshake timeout after %.1f s — %s",
		elapsed / 1000.0, mFailureSummary.c_str());
	return true;
#else
	return mFailed;
#endif
}

bool WasmTransport::SendTo(const uint8_t* bytes, size_t n,
                           const Endpoint& /*to*/) {
#if defined(__EMSCRIPTEN__)
	if (mClosed) return false;
	if (n == 0)  return true;

	// Frame: [role:u8][inner...].  Owns its own buffer; the lobby
	// peels the role byte before forwarding to the OTHER side.
	std::vector<uint8_t> framed(1 + n);
	framed[0] = mRole;
	std::memcpy(framed.data() + 1, bytes, n);

	if (!mOpen) {
		// Pre-handshake: queue.  Cap is small; over-cap = caller is
		// retrying Hello faster than the browser can complete the
		// handshake — drop the oldest, keep the newest (the next
		// Hello has the same payload as the older one anyway).
		if (mOutbound.size() >= kOutboundCap)
			mOutbound.pop_front();
		mOutbound.push_back(std::move(framed));
		return true;
	}

	EMSCRIPTEN_RESULT r = emscripten_websocket_send_binary(
		(EMSCRIPTEN_WEBSOCKET_T)mWs,
		framed.data(), (uint32_t)framed.size());
	return r == EMSCRIPTEN_RESULT_SUCCESS;
#else
	(void)bytes; (void)n;
	return false;
#endif
}

RecvResult WasmTransport::RecvFrom(uint8_t* buf, size_t bufSize,
                                   size_t& outLen, Endpoint& from) {
	if (mInbound.empty()) {
		outLen = 0;
		return RecvResult::WouldBlock;
	}
	auto& msg = mInbound.front();
	if (msg.size() > bufSize) {
		// Caller provided a buffer smaller than our packet — same
		// silent-truncate behaviour Linux gives for UDP.
		std::memcpy(buf, msg.data(), bufSize);
		outLen = bufSize;
	} else {
		std::memcpy(buf, msg.data(), msg.size());
		outLen = msg.size();
	}
	from = MakeWsPeerEndpoint();
	mInbound.pop_front();
	return RecvResult::Ok;
}

void WasmTransport::Close() {
#if defined(__EMSCRIPTEN__)
	if (mWs != 0 && !mClosed) {
		mClosed = true;
		emscripten_websocket_close((EMSCRIPTEN_WEBSOCKET_T)mWs,
			1000, "bye");
		emscripten_websocket_delete((EMSCRIPTEN_WEBSOCKET_T)mWs);
		mWs = 0;
	}
#else
	mClosed = true;
	mWs = 0;
#endif
	mOpen = false;
}

void WasmTransport::DrainOutboundLocked() {
#if defined(__EMSCRIPTEN__)
	while (!mOutbound.empty()) {
		auto& framed = mOutbound.front();
		emscripten_websocket_send_binary(
			(EMSCRIPTEN_WEBSOCKET_T)mWs,
			framed.data(), (uint32_t)framed.size());
		mOutbound.pop_front();
	}
#endif
}

#if defined(__EMSCRIPTEN__)

bool WasmTransport::OnOpenCb(int /*eventType*/,
                             const EmscriptenWebSocketOpenEvent* _Nonnull /*evt*/,
                             void* userData) {
	auto* self = static_cast<WasmTransport*>(userData);
	self->mOpen = true;
	self->DrainOutboundLocked();
	g_ATLCNetplay("WasmTransport: WS open (sid=%.8s..., role=%u)",
		self->mSidHex.c_str(), (unsigned)self->mRole);
	return true;
}

bool WasmTransport::OnMessageCb(int /*eventType*/,
                                const EmscriptenWebSocketMessageEvent* _Nonnull m,
                                void* userData) {
	auto* self = static_cast<WasmTransport*>(userData);
	if (m->isText) {
		// We never send/expect text frames; ignore.
		return true;
	}
	if (m->numBytes < 1) return true;

	// Strip the recipient_role envelope byte the lobby prepends.
	const uint8_t* body = m->data + 1;
	uint32_t       blen = m->numBytes - 1;
	if (blen == 0) return true;

	if (self->mInbound.size() >= kInboundCap) {
		self->mInbound.pop_front();
		if (!self->mWarnedInboundOverflow) {
			self->mWarnedInboundOverflow = true;
			g_ATLCNetplay(
				"WasmTransport: inbound queue overflow (cap=%zu) — "
				"emulator main loop falling behind, dropping oldest",
				kInboundCap);
		}
	}
	self->mInbound.emplace_back(body, body + blen);
	return true;
}

bool WasmTransport::OnErrorCb(int /*eventType*/,
                              const EmscriptenWebSocketErrorEvent* _Nonnull /*evt*/,
                              void* userData) {
	auto* self = static_cast<WasmTransport*>(userData);
	// The browser doesn't expose a useful error reason on the
	// onerror event (CORS / mixed-content / refused TLS all surface
	// as a generic event).  The follow-up onclose event carries the
	// real close code, so log a placeholder here and capture the
	// detail there.
	g_ATLCNetplay("WasmTransport: WS error (browser will close shortly)");
	self->mOpen   = false;
	self->mFailed = true;
	return true;
}

bool WasmTransport::OnCloseCb(int /*eventType*/,
                              const EmscriptenWebSocketCloseEvent* _Nonnull c,
                              void* userData) {
	auto* self = static_cast<WasmTransport*>(userData);
	self->mOpen          = false;
	self->mClosed        = true;
	self->mCloseCode     = c ? c->code : 0;
	self->mCloseWasClean = c ? c->wasClean : false;
	if (c && c->reason[0]) self->mCloseReason.assign(c->reason);
	// Treat any non-1000 close BEFORE we ever opened as a failure.
	// 1006 (abnormal closure) usually means mixed-content/CORS/TLS;
	// 1008/1011 are application-policy rejects from the lobby.
	if (!self->mFailed && self->mCloseCode != 0 && self->mCloseCode != 1000)
		self->mFailed = true;
	// Build a one-liner the joiner UI can show instead of "Waiting for
	// host…" forever.  Map the most common close codes to friendly
	// language; fall back to a generic "code N" for anything else.
	if (self->mFailed) {
		char buf[160];
		const char *human = "lobby connection lost";
		switch (self->mCloseCode) {
			case 1006: human = "could not reach lobby (DNS / TLS / CORS / mixed-content)"; break;
			case 1008: human = "lobby refused the connection (policy)"; break;
			case 1011: human = "lobby internal error"; break;
			case 4000: human = "lobby refused: bad request"; break;
			case 4001: human = "lobby refused: authentication"; break;
			case 4003: human = "lobby refused: forbidden (bad token?)"; break;
			case 4009: human = "lobby refused: role already taken"; break;
			case 4010: human = "lobby refused: session is gone"; break;
		}
		if (!self->mCloseReason.empty()) {
			std::snprintf(buf, sizeof buf,
				"%s (code %u: %s)", human,
				(unsigned)self->mCloseCode, self->mCloseReason.c_str());
		} else {
			std::snprintf(buf, sizeof buf,
				"%s (code %u)", human,
				(unsigned)self->mCloseCode);
		}
		self->mFailureSummary.assign(buf);
	}
	g_ATLCNetplay("WasmTransport: WS closed code=%u clean=%d reason=\"%s\"%s%s",
		(unsigned)self->mCloseCode,
		(int)self->mCloseWasClean,
		self->mCloseReason.c_str(),
		self->mFailed ? " — " : "",
		self->mFailed ? self->mFailureSummary.c_str() : "");
	return true;
}

#endif  // __EMSCRIPTEN__

} // namespace ATNetplay
