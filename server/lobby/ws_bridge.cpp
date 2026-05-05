// Altirra SDL netplay lobby — WebSocket bridge implementation.
//
// Single-threaded mongoose event loop on `localhost:<port>`.  The
// public TLS endpoint (Caddy) reverse-proxies plain WS to us, so we
// never see the TLS layer.  Auth is by `Sec-WebSocket-Protocol`
// subprotocol header — browsers cannot set arbitrary HTTP headers on
// the WebSocket constructor, only subprotocols, and the URL path is
// logged by Caddy so query-string tokens are unsafe.
//
// All mongoose state (the manager, listening conn, accepted conns)
// belongs to this thread.  The reflector thread (in server.cpp) reaches
// us via two thread-safe primitives:
//
//   1. WsRegistry::Other()  — looks up "is the other side of this
//      session a WS peer?"  Mutex-guarded.
//   2. mg_wakeup(mgr, id, …) — queues a WS send to a specific accepted
//      connection.  Mongoose's socketpair-based wakeup is thread-safe.
//
// We send to UDP peers via the reflector socket FD (shared in
// ReflectorSocketHandle).  sendto() on the same FD is thread-safe
// under the kernel's per-socket lock; the reflector's recvfrom()
// continues unaffected.  Sending from a fresh ephemeral socket would
// break Coordinator::PeerIsLobby() on the native peer, since that
// check matches `from.IP:8081` exactly.

#include "ws_bridge.h"

// Pull in the constants the bridge needs to identify Altirra packets
// at the inner level.  The bridge is otherwise transport-agnostic; it
// only constructs synthetic NetBye on close (8 bytes).
#include "lobby_protocol.h"

#include "vendor/mongoose/mongoose.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <arpa/inet.h>

namespace ATLobby {

// ---------------------------------------------------------------------
// Constants shared with server.cpp's reflector
// ---------------------------------------------------------------------
//
// We can't include packets.h (the lobby builds without the SDL3 client
// tree), so duplicate the four bytes that matter for ASDF wrapping and
// the NetBye magic.  Drift caught by build (server.cpp uses identical
// string literals).
constexpr uint8_t  kAsdfMagic[4]   = {'A', 'S', 'D', 'F'};
constexpr size_t   kAsdfHeaderSize = 4 + 16 + 1 + 3;   // magic + sid + role + 3 pad
constexpr uint32_t kNetByeMagicLE  = 0x42504E41u;       // 'ANPB' in little-endian wire order
constexpr size_t   kNetByeSize     = 8;                 // magic + reason

constexpr uint8_t kRelayRoleHost   = 0;
constexpr uint8_t kRelayRoleJoiner = 1;

// ---------------------------------------------------------------------
// Hex parsing helpers (slice-1-private; not pulled from json_cursor.h
// to keep the bridge independent of cpp-httplib's pull-in chain).
// ---------------------------------------------------------------------

static bool HexNibble(char c, uint8_t& out) {
	if (c >= '0' && c <= '9') { out = (uint8_t)(c - '0');      return true; }
	if (c >= 'a' && c <= 'f') { out = (uint8_t)(10 + c - 'a'); return true; }
	if (c >= 'A' && c <= 'F') { out = (uint8_t)(10 + c - 'A'); return true; }
	return false;
}

// Decode `len` hex chars at `s` into `len/2` bytes at `out`.  Returns
// false if any non-hex char or odd length.
static bool HexDecode(const char* s, size_t len, uint8_t* out) {
	if ((len & 1u) != 0) return false;
	for (size_t i = 0; i < len; i += 2) {
		uint8_t hi, lo;
		if (!HexNibble(s[i], hi))     return false;
		if (!HexNibble(s[i + 1], lo)) return false;
		out[i / 2] = (uint8_t)((hi << 4) | lo);
	}
	return true;
}

// Parse the UUID-formatted session id (`8-4-4-4-12` lowercase) and
// also accept the dashless 32-char form, into 16 raw bytes.
static bool ParseSessionId(const char* s, size_t len, uint8_t out[16]) {
	if (len == 32) return HexDecode(s, 32, out);
	if (len != 36) return false;
	if (s[8] != '-' || s[13] != '-' || s[18] != '-' || s[23] != '-')
		return false;
	char compact[32];
	std::memcpy(compact + 0,  s + 0,  8);
	std::memcpy(compact + 8,  s + 9,  4);
	std::memcpy(compact + 12, s + 14, 4);
	std::memcpy(compact + 16, s + 19, 4);
	std::memcpy(compact + 20, s + 24, 12);
	return HexDecode(compact, 32, out);
}

// ---------------------------------------------------------------------
// WsRegistry — cross-thread mapping of (sid, role) → mongoose conn id
// ---------------------------------------------------------------------
//
// Stored value is the connection id, not the pointer, because the
// reflector thread cannot safely dereference an mg_connection* — the
// bridge thread may free it between lookup and use.  The id is stable
// for the connection's lifetime; mg_wakeup(mgr, id, …) handles the
// stale-id case by searching the active conn list and silently
// dropping if absent.

struct WsRegistryHandle {
	mutable std::mutex mu;
	struct Slot {
		unsigned long hostConnId   = 0;   // 0 = vacant
		unsigned long joinerConnId = 0;
	};
	std::unordered_map<std::string, Slot> bySid;   // key = 16 raw bytes
	struct mg_mgr* mgr = nullptr;     // set by RunWsBridge

	static std::string Key(const uint8_t sid[16]) {
		return std::string(reinterpret_cast<const char*>(sid), 16);
	}

	// Returns true if (sid, role) was vacant and we registered.  False
	// if the slot was already taken — caller should reject the upgrade
	// with 409 Conflict.
	bool Register(const uint8_t sid[16], uint8_t role, unsigned long id) {
		std::lock_guard<std::mutex> lk(mu);
		auto& slot = bySid[Key(sid)];
		unsigned long& target = (role == kRelayRoleHost)
			? slot.hostConnId : slot.joinerConnId;
		if (target != 0) return false;
		target = id;
		return true;
	}

	// Idempotent unregister by id.  Erases the slot entry entirely if
	// both halves are vacant after the operation.
	void Unregister(const uint8_t sid[16], unsigned long id) {
		std::lock_guard<std::mutex> lk(mu);
		auto it = bySid.find(Key(sid));
		if (it == bySid.end()) return;
		auto& slot = it->second;
		if (slot.hostConnId   == id) slot.hostConnId   = 0;
		if (slot.joinerConnId == id) slot.joinerConnId = 0;
		if (slot.hostConnId == 0 && slot.joinerConnId == 0)
			bySid.erase(it);
	}

	// Returns the connection id of the OTHER side, or 0 if no peer.
	unsigned long Other(const uint8_t sid[16], uint8_t senderRole) const {
		std::lock_guard<std::mutex> lk(mu);
		auto it = bySid.find(Key(sid));
		if (it == bySid.end()) return 0;
		const auto& slot = it->second;
		return (senderRole == kRelayRoleHost)
			? slot.joinerConnId : slot.hostConnId;
	}
};

// ---------------------------------------------------------------------
// Per-connection state (bridge-thread-only; no mutex)
// ---------------------------------------------------------------------

namespace {

struct PerConn {
	uint8_t sid[16]    = {};
	uint8_t role       = 0;
	bool    registered = false;     // true between WS_OPEN and CLOSE
	int64_t lastRecvMs = 0;         // updated on each WS msg AND pong
	int64_t lastPingMs = 0;         // last time we sent a ping
};

// Per-IP upgrade rate limit (slice 6 — production-readiness).
// Each entry tracks the most recent fixed-window count of UPGRADE
// attempts from a given source IP.  The bridge thread is single-
// threaded (mongoose event loop) so no lock is needed.
struct UpgradeRateBucket {
	int64_t windowStartMs = 0;
	int     count          = 0;
};

// Side-table indexed by mg_connection->id.  Only the bridge thread
// reads/writes this; entries created on WS_OPEN, removed on CLOSE.
struct BridgeState {
	WsBridgeConfig            cfg;
	WsBridgeStats*            stats         = nullptr;
	ReflectorSocketHandle*    reflectorFd   = nullptr;
	WsSessionValidatorFn      validateFn    = nullptr;
	void*                     validateCtx   = nullptr;
	WsUdpForwarderFn          udpForwardFn  = nullptr;
	void*                     udpForwardCtx = nullptr;
	WsRegistryHandle*         reg           = nullptr;

	std::unordered_map<unsigned long, PerConn> conns;

	// Per-IP upgrade rate limit, fixed-window 60 s, cap kUpgradesPerMin.
	// Caddy fronts us so the immediate TCP peer is loopback; we read
	// X-Forwarded-For when present and fall back to the TCP peer.
	std::unordered_map<std::string, UpgradeRateBucket> upgradeRate;
};

int64_t NowMs() {
	using Clock = std::chrono::steady_clock;
	return std::chrono::duration_cast<std::chrono::milliseconds>(
		Clock::now().time_since_epoch()).count();
}

// ---------------------------------------------------------------------
// Subprotocol parsing
// ---------------------------------------------------------------------
//
// Browser sends `Sec-WebSocket-Protocol: altirra-netplay.v1, session.<hex>,
// role.<host|joiner>, token.<hex>`.  We accept this exact list shape
// (the four tokens may appear in any order, separated by commas with
// optional whitespace).  The marker `altirra-netplay.v1` is required;
// session must be 32 hex chars or UUID-with-dashes; role must be `host`
// or `joiner`; token must be 32 hex chars (matches NewToken() output).

struct ParsedAuth {
	uint8_t sid[16] = {};
	uint8_t role    = 0;
	std::string tokenHex;     // null-separated null-terminated copy
	bool versionOk  = false;
	bool sidOk      = false;
	bool roleOk     = false;
	bool tokenOk    = false;
};

// Split on comma; strip leading whitespace.  Accepts mg_str input.
static void SplitCsv(struct mg_str s,
                     std::vector<struct mg_str>& out) {
	out.clear();
	while (s.len > 0) {
		while (s.len > 0 && (s.buf[0] == ' ' || s.buf[0] == '\t')) {
			s.buf++; s.len--;
		}
		char* p = s.buf;
		size_t i = 0;
		while (i < s.len && s.buf[i] != ',') ++i;
		// Trim trailing whitespace on this token.
		size_t end = i;
		while (end > 0 && (p[end - 1] == ' ' || p[end - 1] == '\t')) --end;
		struct mg_str tok;
		tok.buf = p;
		tok.len = end;
		out.push_back(tok);
		if (i == s.len) break;
		s.buf += i + 1;
		s.len -= i + 1;
	}
}

// Match `prefix` and copy the rest into `outValue`.  Returns true on
// match.
static bool TokenStartsWith(struct mg_str t, const char* prefix,
                            struct mg_str& outValue) {
	size_t plen = std::strlen(prefix);
	if (t.len < plen) return false;
	if (std::memcmp(t.buf, prefix, plen) != 0) return false;
	outValue.buf = t.buf + plen;
	outValue.len = t.len - plen;
	return true;
}

static bool ParseSubprotocol(struct mg_str hdr, ParsedAuth& out) {
	std::vector<struct mg_str> tokens;
	SplitCsv(hdr, tokens);
	for (auto t : tokens) {
		struct mg_str v;
		if (t.len == 18 &&
		    std::memcmp(t.buf, "altirra-netplay.v1", 18) == 0) {
			out.versionOk = true;
		} else if (TokenStartsWith(t, "session.", v)) {
			if (ParseSessionId(v.buf, v.len, out.sid))
				out.sidOk = true;
		} else if (TokenStartsWith(t, "role.", v)) {
			if (v.len == 4 && std::memcmp(v.buf, "host", 4) == 0) {
				out.role = kRelayRoleHost; out.roleOk = true;
			} else if (v.len == 6 && std::memcmp(v.buf, "joiner", 6) == 0) {
				out.role = kRelayRoleJoiner; out.roleOk = true;
			}
		} else if (TokenStartsWith(t, "token.", v)) {
			if (v.len == 32) {
				// Validate hex shape; token validation happens in the
				// callback against the Store.
				bool ok = true;
				for (size_t i = 0; i < v.len; ++i) {
					char c = v.buf[i];
					if (!((c >= '0' && c <= '9') ||
					      (c >= 'a' && c <= 'f') ||
					      (c >= 'A' && c <= 'F'))) { ok = false; break; }
				}
				if (ok) {
					out.tokenHex.assign(v.buf, v.len);
					out.tokenOk = true;
				}
			}
		}
		// Unknown tokens are tolerated (forward-compat).
	}
	// Token is required for role=host (proves we are the legitimate
	// host of this session — same secret as DELETE /v1/session/{id}).
	// For role=joiner, token is OPTIONAL: anyone can ask the lobby to
	// bridge them into a session, exactly as anyone can ASGR a UDP
	// session id today.  The actual auth happens at the inner Altirra
	// protocol level via NetHello + entry codes; the WS bridge is just
	// a transport.
	if (!out.versionOk || !out.sidOk || !out.roleOk) return false;
	if (out.role == kRelayRoleHost && !out.tokenOk) return false;
	return true;
}

// ---------------------------------------------------------------------
// Outbound paths
// ---------------------------------------------------------------------

// Forward a WS-received inner packet to a UDP peer registered in the
// caller's RelayTable.  The forwarder callback in server.cpp does the
// table lookup; we just hand it the raw bytes plus session+role.
static bool ForwardToUdpPeer(BridgeState& st,
                             const uint8_t sid[16], uint8_t senderRole,
                             const uint8_t* inner, size_t innerLen) {
	if (!st.udpForwardFn) return false;
	bool ok = st.udpForwardFn(sid, senderRole, inner, innerLen,
		st.udpForwardCtx);
	if (ok) {
		st.stats->forwardsCrossTransport.fetch_add(
			1, std::memory_order_relaxed);
	} else {
		st.stats->droppedNoPeer.fetch_add(1, std::memory_order_relaxed);
	}
	return ok;
}

// Forward an inner packet to another WS peer in the same session.  The
// payload sent on the wire is `[recipient_role:u8][inner…]` so the
// browser can dispatch by role exactly as the bridge does inbound.
static bool ForwardToWsPeer(BridgeState& st, unsigned long otherConnId,
                            uint8_t senderRole,
                            const uint8_t* inner, size_t innerLen) {
	if (otherConnId == 0) return false;
	uint8_t recipientRole = (senderRole == kRelayRoleHost)
		? kRelayRoleJoiner : kRelayRoleHost;

	// We're already on the bridge thread (this is called from the
	// MG_EV_WS_MSG handler), so a direct mg_ws_send on the other conn
	// is safe.  Find the other conn by id.
	struct mg_connection* other = nullptr;
	for (struct mg_connection* c = st.reg->mgr->conns; c; c = c->next) {
		if (c->id == otherConnId && c->is_websocket && !c->is_closing) {
			other = c; break;
		}
	}
	if (!other) {
		st.stats->droppedNoPeer.fetch_add(1, std::memory_order_relaxed);
		return false;
	}

	std::vector<uint8_t> framed(1 + innerLen);
	framed[0] = recipientRole;
	if (innerLen > 0) std::memcpy(framed.data() + 1, inner, innerLen);
	size_t sent = mg_ws_send(other, framed.data(), framed.size(),
		WEBSOCKET_OP_BINARY);
	(void)sent;  // mg_ws_send returns the framed-byte count it queued

	st.stats->messagesOut.fetch_add(1, std::memory_order_relaxed);
	st.stats->bytesOut.fetch_add((uint64_t)framed.size(),
		std::memory_order_relaxed);
	return true;
}

}  // namespace

// ---------------------------------------------------------------------
// Cross-thread entry point: reflector → bridge
// ---------------------------------------------------------------------
//
// Called from the reflector thread when an ASDF UDP frame arrives and
// the OTHER side of the session is on WS.  Queues a wakeup on the WS
// conn; bridge handler does the actual mg_ws_send.

bool WsBridgeForwardToWs(WsRegistryHandle* reg,
                         const uint8_t sidRaw[16], uint8_t senderRole,
                         const uint8_t* inner, size_t innerLen,
                         WsBridgeStats& stats) {
	if (!reg || !reg->mgr) return false;
	unsigned long otherId = reg->Other(sidRaw, senderRole);
	if (otherId == 0) return false;

	uint8_t recipientRole = (senderRole == kRelayRoleHost)
		? kRelayRoleJoiner : kRelayRoleHost;

	// mg_wakeup payload: [recipient_role:u8][inner…].  The wakeup
	// handler on the bridge thread dispatches mg_ws_send for us.
	std::vector<uint8_t> payload(1 + innerLen);
	payload[0] = recipientRole;
	if (innerLen > 0) std::memcpy(payload.data() + 1, inner, innerLen);

	bool ok = mg_wakeup(reg->mgr, otherId,
		payload.data(), payload.size());
	if (ok) {
		stats.forwardsCrossTransport.fetch_add(
			1, std::memory_order_relaxed);
	} else {
		stats.droppedNoPeer.fetch_add(1, std::memory_order_relaxed);
	}
	return ok;
}

// ---------------------------------------------------------------------
// Mongoose event handler
// ---------------------------------------------------------------------

namespace {

// Build an HTTP error reply with the right code; mongoose's mg_http_reply
// expects a status code, headers, and body.  For upgrade rejection we
// keep the body minimal (browsers don't surface it usefully anyway).
static void RejectUpgrade(struct mg_connection* c, int code,
                          const char* reason) {
	mg_http_reply(c, code,
		"Content-Type: text/plain\r\n", "%s\n", reason);
}

// Suppress mongoose's automatic Sec-WebSocket-Protocol echo.
//
// vendor/mongoose/mongoose.c ws_handshake() unconditionally echoes the
// request's Sec-WebSocket-Protocol value back into the 101 response,
// AS-IS — i.e. the entire offered list (`altirra-netplay.v1, session.<hex>,
// role.<r>, token.<hex>`).  Combined with our own explicit single-protocol
// header (`Sec-WebSocket-Protocol: altirra-netplay.v1`), this produces TWO
// Sec-WebSocket-Protocol headers in the response — one valid, one whose
// value is a comma-joined list.  Browsers (Chromium, Firefox) reject this
// and abort the WS connection with code 1006; emscripten then surfaces it
// as the misleading "could not reach lobby (DNS / TLS / CORS / mixed-
// content)" error.  curl ignores the duplicate, which is why CLI tests
// look fine while real WASM clients fail.
//
// We can't suppress the echo without modifying mongoose, but we can make
// mongoose's internal mg_http_get_header() lookup miss the header.  The
// header bytes live in mongoose's recv buffer (mg_str.buf is char*, not
// const) and we already finished parsing the value above, so it is safe
// to mangle the name byte-for-byte without changing its length.  Keeping
// the length intact preserves iteration over hm->headers[] for any later
// code that walks the array.
static void StripRequestSubprotocolHeader(struct mg_http_message* hm) {
	static const char kName[] = "Sec-WebSocket-Protocol";
	constexpr size_t kNameLen = sizeof(kName) - 1;
	const size_t maxH = sizeof(hm->headers) / sizeof(hm->headers[0]);
	for (size_t i = 0; i < maxH && hm->headers[i].name.len > 0; ++i) {
		struct mg_str& k = hm->headers[i].name;
		if (k.len != kNameLen) continue;
		bool eq = true;
		for (size_t j = 0; j < kNameLen; ++j) {
			char a = k.buf[j], b = kName[j];
			if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
			if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
			if (a != b) { eq = false; break; }
		}
		if (!eq) continue;
		// Break the name without changing length.  '_' is not in the
		// target name, so the case-insensitive compare in
		// mg_http_get_header() now fails on byte 0.
		k.buf[0] = '_';
		return;
	}
}

// Application-policy WS close codes mirroring our WsAuthResult enum.
// Browsers can read these in `event.code` on the WebSocket close event,
// whereas a pre-upgrade HTTP 4xx collapses to 1006 (no detail).
constexpr uint16_t kWsCloseBadRequest = 4000;
constexpr uint16_t kWsCloseForbidden  = 4003;
constexpr uint16_t kWsCloseRoleTaken  = 4009;
constexpr uint16_t kWsCloseSessionGone = 4010;

// Reject AFTER completing the WS handshake: complete the upgrade with
// the agreed subprotocol echo, then immediately send a Close frame with
// an application-policy code and a human reason.  Browsers expose both
// to JS, so the WASM joiner can show "session full" / "role taken" /
// "session ended" instead of the generic 1006 "WSS blocked".
static void RejectAfterUpgrade(struct mg_connection* c,
                               struct mg_http_message* hm,
                               uint16_t wsCloseCode,
                               const char* reason) {
	StripRequestSubprotocolHeader(hm);
	mg_ws_upgrade(c, hm,
		"Sec-WebSocket-Protocol: altirra-netplay.v1\r\n");
	char buf[125];   // 125 = WS control-frame payload limit
	buf[0] = (char)((wsCloseCode >> 8) & 0xFF);
	buf[1] = (char)(wsCloseCode & 0xFF);
	size_t reasonLen = std::strlen(reason);
	if (reasonLen > sizeof(buf) - 2) reasonLen = sizeof(buf) - 2;
	std::memcpy(buf + 2, reason, reasonLen);
	mg_ws_send(c, buf, 2 + reasonLen, WEBSOCKET_OP_CLOSE);
	c->is_draining = 1;
}

// Find or create per-conn state for an accepted WS connection.
static PerConn& GetPerConn(BridgeState& st, struct mg_connection* c) {
	return st.conns[c->id];
}

static void DropPerConn(BridgeState& st, struct mg_connection* c) {
	st.conns.erase(c->id);
}

// Synthesise an inbound NetBye to deliver to the surviving peer when
// our side disconnects mid-session.  Without this, the surviving peer
// waits its multi-minute peer-silence timer.  reason field stays 0
// (kReasonOther).
static void SendSyntheticBye(BridgeState& st, const PerConn& pc) {
	uint8_t bye[kNetByeSize];
	bye[0] = (uint8_t)(kNetByeMagicLE       & 0xFF);
	bye[1] = (uint8_t)((kNetByeMagicLE >> 8) & 0xFF);
	bye[2] = (uint8_t)((kNetByeMagicLE >> 16) & 0xFF);
	bye[3] = (uint8_t)((kNetByeMagicLE >> 24) & 0xFF);
	bye[4] = 0; bye[5] = 0; bye[6] = 0; bye[7] = 0;

	// Try WS first (other peer is on WS in this same bridge), then UDP.
	unsigned long otherId = st.reg->Other(pc.sid, pc.role);
	if (otherId != 0) {
		ForwardToWsPeer(st, otherId, pc.role, bye, kNetByeSize);
		return;
	}
	ForwardToUdpPeer(st, pc.sid, pc.role, bye, kNetByeSize);
}

// Cap on per-IP WS upgrade attempts per 60 s window.  Caddy in front
// of us already rate-limits TLS handshakes, but a single misbehaving
// client can still hammer the upgrade endpoint over a kept-alive
// HTTP/2 stream — this is a per-source-IP backstop.  Generous enough
// that legitimate session retries (refresh, reconnect after short
// dropout) never trip; tight enough that a 20-Hz reconnect loop
// gets shut out.
constexpr int kUpgradesPerMin = 60;
constexpr int64_t kUpgradeWindowMs = 60 * 1000;

// Identify the source IP of an incoming HTTP request.  Caddy is
// configured to overwrite X-Forwarded-For with `{remote_host}` so the
// header (when present) is the attested client IP.  When the bridge
// is hit directly (loopback testing, or a misconfigured deployment),
// we fall back to the TCP peer address from c->rem.
static std::string ClientIpForBridge(struct mg_connection* c,
                                     struct mg_http_message* hm) {
	const struct mg_str* xff = mg_http_get_header(hm,
		"X-Forwarded-For");
	if (xff && xff->len > 0) {
		// Last entry only (Caddy overwrites with a single value, but
		// be defensive: a leftmost-trusted reader is the wrong choice
		// here because the real client value is appended by the
		// proxy nearest to us).
		struct mg_str s = *xff;
		size_t i = s.len;
		while (i > 0 && (s.buf[i - 1] == ' ' || s.buf[i - 1] == '\t'))
			--i;
		size_t end = i;
		size_t start = end;
		while (start > 0 && s.buf[start - 1] != ',') --start;
		while (start < end &&
		       (s.buf[start] == ' ' || s.buf[start] == '\t')) ++start;
		if (end > start) return std::string(s.buf + start, end - start);
	}
	// mg_addr → "ip:port" string; we want just the IP.
	char buf[64];
	mg_snprintf(buf, sizeof buf, "%M", mg_print_ip, &c->rem);
	return std::string(buf);
}

static bool RateLimitAllowUpgrade(BridgeState& st, const std::string& ip,
                                  int64_t nowMs) {
	auto& bk = st.upgradeRate[ip];
	if (bk.windowStartMs == 0 ||
	    (nowMs - bk.windowStartMs) > kUpgradeWindowMs) {
		bk.windowStartMs = nowMs;
		bk.count         = 1;
		return true;
	}
	if (bk.count < kUpgradesPerMin) {
		bk.count += 1;
		return true;
	}
	return false;
}

static void HandleHttpMsg(BridgeState& st, struct mg_connection* c,
                          struct mg_http_message* hm) {
	struct mg_str path = hm->uri;

	// Health probe — useful for verifying the bridge is alive
	// independently of the main HTTP API on :8080.
	if (mg_match(path, mg_str("/healthz"), NULL)) {
		mg_http_reply(c, 200, "Content-Type: text/plain\r\n", "ok\n");
		return;
	}

	if (!mg_match(path, mg_str("/netplay"), NULL)) {
		mg_http_reply(c, 404, "Content-Type: text/plain\r\n",
			"not found\n");
		return;
	}

	// Per-IP rate limit on upgrade attempts.
	std::string clientIp = ClientIpForBridge(c, hm);
	if (!RateLimitAllowUpgrade(st, clientIp, NowMs())) {
		st.stats->droppedRateLimit.fetch_add(
			1, std::memory_order_relaxed);
		mg_http_reply(c, 429, "Content-Type: text/plain\r\n",
			"too many WS upgrade attempts\n");
		c->is_draining = 1;
		return;
	}

	struct mg_str* hSubp = mg_http_get_header(hm, "Sec-WebSocket-Protocol");
	if (!hSubp) {
		st.stats->droppedAuth.fetch_add(1, std::memory_order_relaxed);
		st.stats->upgradesRejectedAuth.fetch_add(1,
			std::memory_order_relaxed);
		RejectUpgrade(c, 400,
			"missing Sec-WebSocket-Protocol");
		c->is_draining = 1;
		return;
	}

	ParsedAuth auth;
	if (!ParseSubprotocol(*hSubp, auth)) {
		st.stats->droppedAuth.fetch_add(1, std::memory_order_relaxed);
		st.stats->upgradesRejectedAuth.fetch_add(1,
			std::memory_order_relaxed);
		RejectUpgrade(c, 400,
			"malformed Sec-WebSocket-Protocol");
		c->is_draining = 1;
		return;
	}

	// Validate session+token via the caller-supplied callback.  The
	// callback returns an HTTP-shaped code so we can distinguish
	// 403 (bad token) from 410 (gone).  Use the upgrade-then-close
	// path so the browser can read the close code + reason.
	WsAuthResult vr = st.validateFn(auth.sid, auth.role,
		auth.tokenHex.c_str(), st.validateCtx);
	if (vr != WsAuthResult::kOk) {
		st.stats->droppedAuth.fetch_add(1, std::memory_order_relaxed);
		st.stats->upgradesRejectedAuth.fetch_add(1,
			std::memory_order_relaxed);
		const char* msg = "rejected";
		uint16_t    wsCode = kWsCloseBadRequest;
		switch (vr) {
			case WsAuthResult::kBadRequest:
				msg = "bad request";  wsCode = kWsCloseBadRequest;   break;
			case WsAuthResult::kForbidden:
				msg = "forbidden";    wsCode = kWsCloseForbidden;    break;
			case WsAuthResult::kGone:
				msg = "session gone"; wsCode = kWsCloseSessionGone;  break;
			case WsAuthResult::kConflict:
				msg = "role taken";   wsCode = kWsCloseRoleTaken;    break;
			default:
				msg = "rejected";     wsCode = kWsCloseBadRequest;   break;
		}
		RejectAfterUpgrade(c, hm, wsCode, msg);
		return;
	}

	// Conflict check: don't allow a second WS to claim the same role.
	// (We can't see UDP-side conflicts here; that's a defense-in-depth
	// task for slice 6 which unifies the registries.)  Like the
	// validate path above, upgrade-then-close so the browser sees a
	// real close code (4009) instead of an opaque 1006.
	if (!st.reg->Register(auth.sid, auth.role, c->id)) {
		st.stats->upgradesRejectedConflict.fetch_add(1,
			std::memory_order_relaxed);
		RejectAfterUpgrade(c, hm, kWsCloseRoleTaken,
			"role already in session");
		return;
	}

	// Stash per-conn state.  WsRegistry registration above uses the
	// raw sid; per-conn state here mirrors it for fast path on close.
	PerConn& pc = GetPerConn(st, c);
	std::memcpy(pc.sid, auth.sid, 16);
	pc.role       = auth.role;
	pc.registered = true;
	pc.lastRecvMs = NowMs();
	pc.lastPingMs = pc.lastRecvMs;

	// Respond with the version marker (NOT the token — never reflect
	// secrets in response headers).  Browsers verify the echoed
	// subprotocol and reject if they don't see their requested one.
	// Strip the request-side Sec-WebSocket-Protocol first so mongoose's
	// internal echo (which would re-emit the entire offered list as a
	// second response header) doesn't fire — see comment on
	// StripRequestSubprotocolHeader for the failure mode.
	StripRequestSubprotocolHeader(hm);
	mg_ws_upgrade(c, hm,
		"Sec-WebSocket-Protocol: altirra-netplay.v1\r\n");

	st.stats->connectionsTotal.fetch_add(1, std::memory_order_relaxed);
}

static void HandleWsMsg(BridgeState& st, struct mg_connection* c,
                        struct mg_ws_message* wm) {
	uint8_t opcode = wm->flags & 0x0F;
	if (opcode != WEBSOCKET_OP_BINARY) {
		// We only carry binary inner packets.  Text frames are user
		// error or hostile; close the connection gracefully.
		c->is_draining = 1;
		return;
	}
	if (wm->data.len > st.cfg.maxMessageBytes) {
		st.stats->droppedOversized.fetch_add(1,
			std::memory_order_relaxed);
		mg_ws_send(c, "", 0, WEBSOCKET_OP_CLOSE);
		c->is_draining = 1;
		return;
	}
	if (wm->data.len < 1 + 4) {
		// 1 envelope role byte + at least 4 bytes inner magic.
		st.stats->droppedAuth.fetch_add(1, std::memory_order_relaxed);
		return;
	}
	auto it = st.conns.find(c->id);
	if (it == st.conns.end() || !it->second.registered) {
		st.stats->droppedAuth.fetch_add(1, std::memory_order_relaxed);
		return;
	}
	PerConn& pc = it->second;

	uint8_t envelopeRole = (uint8_t)wm->data.buf[0];
	if (envelopeRole != pc.role) {
		// Defense in depth: the WS handshake bound the role; the
		// envelope byte must match.  A mismatch is a buggy or hostile
		// client.
		st.stats->droppedAuth.fetch_add(1, std::memory_order_relaxed);
		return;
	}
	pc.lastRecvMs = NowMs();
	st.stats->messagesIn.fetch_add(1, std::memory_order_relaxed);
	st.stats->bytesIn.fetch_add((uint64_t)wm->data.len,
		std::memory_order_relaxed);

	const uint8_t* inner    = (const uint8_t*)wm->data.buf + 1;
	size_t         innerLen = wm->data.len - 1;

	// Try WS-WS first; fall back to UDP relay table.
	unsigned long otherId = st.reg->Other(pc.sid, pc.role);
	if (otherId != 0) {
		ForwardToWsPeer(st, otherId, pc.role, inner, innerLen);
		return;
	}
	ForwardToUdpPeer(st, pc.sid, pc.role, inner, innerLen);
}

static void HandleClose(BridgeState& st, struct mg_connection* c) {
	auto it = st.conns.find(c->id);
	if (it == st.conns.end()) return;
	PerConn pc = it->second;       // copy before erase
	if (pc.registered) {
		// Send synthetic Bye to the surviving peer (if any) so it
		// transitions to ended state immediately.
		SendSyntheticBye(st, pc);
		st.reg->Unregister(pc.sid, c->id);
	}
	DropPerConn(st, c);
}

// Per-connection wakeup payload from the reflector thread:
// `[recipient_role:u8][inner…]`.  The recipient_role byte was set by
// the cross-thread sender so the browser can dispatch as if it had
// come from the other peer directly.
static void HandleWakeup(BridgeState& st, struct mg_connection* c,
                         struct mg_str* data) {
	if (!c->is_websocket || c->is_closing) return;
	if (!data || data->len == 0) return;

	mg_ws_send(c, data->buf, data->len, WEBSOCKET_OP_BINARY);
	st.stats->messagesOut.fetch_add(1, std::memory_order_relaxed);
	st.stats->bytesOut.fetch_add((uint64_t)data->len,
		std::memory_order_relaxed);
}

// Periodic timer (fired by mongoose's MG_EV_POLL — once per loop
// iteration).  Walks per-conn state to send keepalive pings and prune
// idle WS peers.  Bridge thread, no lock needed.
static void TickPoll(BridgeState& st) {
	int64_t now = NowMs();
	for (struct mg_connection* c = st.reg->mgr->conns; c; ) {
		struct mg_connection* next = c->next;
		if (!c->is_websocket || c->is_closing) { c = next; continue; }
		auto it = st.conns.find(c->id);
		if (it == st.conns.end()) { c = next; continue; }
		PerConn& pc = it->second;

		// App-idle: no inbound netplay frame in 60 s → close.
		if (now - pc.lastRecvMs > (int64_t)st.cfg.idleTimeoutMs) {
			mg_ws_send(c, "", 0, WEBSOCKET_OP_CLOSE);
			c->is_draining = 1;
			c = next; continue;
		}

		// Send keepalive ping every kPingIntervalMs.  Pong updates
		// lastRecvMs (handled in HandleWsCtl below); if pong doesn't
		// arrive within kPongTimeoutMs of the LAST ping, close.
		bool needPing = (now - pc.lastPingMs) >
			(int64_t)st.cfg.pingIntervalMs;
		if (needPing) {
			mg_ws_send(c, "", 0, WEBSOCKET_OP_PING);
			pc.lastPingMs = now;
		}
		// Pong-timeout: if we sent a ping pingIntervalMs ago AND
		// nothing inbound has arrived in (pingIntervalMs+pongTimeoutMs)
		// then the connection is dead.
		int64_t deadlineMs = pc.lastPingMs + (int64_t)st.cfg.pongTimeoutMs;
		if (now > deadlineMs && pc.lastRecvMs < pc.lastPingMs) {
			mg_ws_send(c, "", 0, WEBSOCKET_OP_CLOSE);
			c->is_draining = 1;
		}
		c = next;
	}
}

static void HandleWsCtl(BridgeState& st, struct mg_connection* c,
                        struct mg_ws_message* wm) {
	uint8_t opcode = wm->flags & 0x0F;
	if (opcode == WEBSOCKET_OP_PONG || opcode == WEBSOCKET_OP_PING) {
		auto it = st.conns.find(c->id);
		if (it != st.conns.end()) it->second.lastRecvMs = NowMs();
	}
	(void)st;
}

// Mongoose dispatches all events through this single function pointer.
// We thread BridgeState* via fn_data of the listening connection (and
// inherit it on accepted conns).
static void EventHandler(struct mg_connection* c, int ev, void* ev_data) {
	BridgeState* st = static_cast<BridgeState*>(c->fn_data);
	if (!st) return;

	switch (ev) {
		case MG_EV_HTTP_MSG:
			HandleHttpMsg(*st, c,
				static_cast<struct mg_http_message*>(ev_data));
			break;
		case MG_EV_WS_MSG:
			HandleWsMsg(*st, c,
				static_cast<struct mg_ws_message*>(ev_data));
			break;
		case MG_EV_WS_CTL:
			HandleWsCtl(*st, c,
				static_cast<struct mg_ws_message*>(ev_data));
			break;
		case MG_EV_WAKEUP:
			HandleWakeup(*st, c,
				static_cast<struct mg_str*>(ev_data));
			break;
		case MG_EV_CLOSE:
			HandleClose(*st, c);
			break;
		case MG_EV_POLL:
			// MG_EV_POLL fires for every connection on every loop tick.
			// Run the per-conn maintenance once per tick by piggy-
			// backing on the listener's POLL.
			if (c->is_listening) TickPoll(*st);
			break;
		default:
			break;
	}
}

}  // namespace

// ---------------------------------------------------------------------
// RunWsBridge — top-level entry point
// ---------------------------------------------------------------------

void RunWsBridge(const WsBridgeConfig& cfg,
                 WsBridgeStats& stats,
                 ReflectorSocketHandle& reflectorFd,
                 WsSessionValidatorFn validateFn, void* validateCtx,
                 WsUdpForwarderFn udpForwardFn, void* udpForwardCtx,
                 WsBridgeContext& outCtx,
                 std::atomic<bool>& stop) {
	struct mg_mgr mgr;
	mg_mgr_init(&mgr);
	if (!mg_wakeup_init(&mgr)) {
		std::fprintf(stderr, "ws-bridge: mg_wakeup_init failed\n");
		mg_mgr_free(&mgr);
		return;
	}

	WsRegistryHandle reg;
	reg.mgr = &mgr;

	BridgeState st;
	st.cfg           = cfg;
	st.stats         = &stats;
	st.reflectorFd   = &reflectorFd;
	st.validateFn    = validateFn;
	st.validateCtx   = validateCtx;
	st.udpForwardFn  = udpForwardFn;
	st.udpForwardCtx = udpForwardCtx;
	st.reg           = &reg;

	// Publish the registry pointer so the reflector thread (in
	// server.cpp) can probe for WS peers before falling back to UDP.
	outCtx.reg = &reg;

	char url[64];
	std::snprintf(url, sizeof url, "http://%s:%u",
		cfg.listenAddr, (unsigned)cfg.port);
	struct mg_connection* listener = mg_http_listen(
		&mgr, url, EventHandler, &st);
	if (!listener) {
		std::fprintf(stderr,
			"ws-bridge: mg_http_listen(%s) failed\n", url);
		mg_mgr_free(&mgr);
		return;
	}

	std::fprintf(stdout,
		"ws-bridge listening on %s/netplay (max=%zu B, idle=%llums)\n",
		url, cfg.maxMessageBytes,
		(unsigned long long)cfg.idleTimeoutMs);
	std::fflush(stdout);

	// Poll loop.  100 ms cadence: we want timely pruning + prompt
	// shutdown response without burning CPU when idle.
	while (!stop.load(std::memory_order_relaxed)) {
		mg_mgr_poll(&mgr, 100);
	}

	// Drop the registry pointer so any in-flight reflector lookups
	// fail closed instead of touching freed memory after this returns.
	outCtx.reg = nullptr;

	mg_mgr_free(&mgr);
}

}  // namespace ATLobby
