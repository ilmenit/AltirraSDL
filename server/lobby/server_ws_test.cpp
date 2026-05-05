// WS bridge integration tests.
//
// Spawn the bridge on a random localhost port with stub callbacks
// (auth always succeeds, UDP forward always fails) so we can exercise
// the WS-WS path in isolation.  Uses mongoose's own WS client to drive
// the protocol — the same library the bridge uses on the server side.
//
// Tests aim to lock in the slice-1 contract:
//   - subprotocol-bound handshake (version + session + role + token)
//   - bidirectional binary frame delivery between two peers in the
//     same session, with the role envelope byte
//   - reject upgrade for missing/malformed subprotocol
//   - role-conflict upgrade rejection (409)
//   - oversized frame rejection (close 1009)
//   - synthetic NetBye on graceful close

#include "ws_bridge.h"
#include "vendor/mongoose/mongoose.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {

using ATLobby::WsAuthResult;

// ---------- Stub callbacks ----------------------------------------------------

// Token map: validator accepts any session+token combination where the
// 32-char hex token equals the canonical string we feed at handshake.
// Real Store-backed validation is exercised by server_test.cpp.
constexpr const char* kFakeToken = "0123456789abcdef0123456789abcdef";

WsAuthResult StubValidate(const uint8_t /*sid*/[16], uint8_t role,
                          const char* tokenHex, void* /*ctx*/) {
	// Mirror production semantics (server.cpp::WsValidateSession):
	// host MUST present the canonical token; joiner is auth-free.
	if (role == 0 /* host */) {
		if (!tokenHex) return WsAuthResult::kForbidden;
		if (std::strcmp(tokenHex, kFakeToken) == 0)
			return WsAuthResult::kOk;
		return WsAuthResult::kForbidden;
	}
	return WsAuthResult::kOk;
}

bool StubUdpForward(const uint8_t /*sid*/[16], uint8_t /*role*/,
                    const uint8_t* /*inner*/, size_t /*len*/, void*) {
	return false;  // no UDP peer in tests
}

// ---------- Test scaffolding --------------------------------------------------

struct ClientCtx {
	std::vector<uint8_t> lastBinary;
	bool                 connected = false;
	bool                 closed    = false;
	int                  httpStatus = 0;
	uint16_t             wsCloseCode = 0;
};

void ClientFn(struct mg_connection* c, int ev, void* ev_data) {
	auto* cc = static_cast<ClientCtx*>(c->fn_data);
	if (ev == MG_EV_WS_OPEN) {
		cc->connected = true;
	} else if (ev == MG_EV_WS_MSG) {
		auto* wm = static_cast<struct mg_ws_message*>(ev_data);
		uint8_t opcode = wm->flags & 0x0F;
		if (opcode == WEBSOCKET_OP_BINARY) {
			cc->lastBinary.assign(
				(const uint8_t*)wm->data.buf,
				(const uint8_t*)wm->data.buf + wm->data.len);
		}
	} else if (ev == MG_EV_WS_CTL) {
		auto* wm = static_cast<struct mg_ws_message*>(ev_data);
		uint8_t opcode = wm->flags & 0x0F;
		if (opcode == WEBSOCKET_OP_CLOSE && wm->data.len >= 2) {
			cc->wsCloseCode =
				(uint16_t)(((uint8_t)wm->data.buf[0] << 8) |
				            (uint8_t)wm->data.buf[1]);
		}
	} else if (ev == MG_EV_HTTP_MSG) {
		auto* hm = static_cast<struct mg_http_message*>(ev_data);
		cc->httpStatus = mg_http_status(hm);
	} else if (ev == MG_EV_CLOSE) {
		cc->closed = true;
	}
}

void Pump(struct mg_mgr& mgr, int ms) {
	auto deadline = std::chrono::steady_clock::now() +
		std::chrono::milliseconds(ms);
	while (std::chrono::steady_clock::now() < deadline) {
		mg_mgr_poll(&mgr, 5);
	}
}

bool WaitFor(struct mg_mgr& mgr, std::function<bool()> pred,
             int timeoutMs) {
	auto deadline = std::chrono::steady_clock::now() +
		std::chrono::milliseconds(timeoutMs);
	while (std::chrono::steady_clock::now() < deadline) {
		mg_mgr_poll(&mgr, 5);
		if (pred()) return true;
	}
	return false;
}

// Pick a free TCP port by binding port 0 on a temporary socket.
int PickFreePort() {
	int s = ::socket(AF_INET, SOCK_STREAM, 0);
	if (s < 0) return -1;
	sockaddr_in a{};
	a.sin_family = AF_INET;
	a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	a.sin_port = 0;
	if (::bind(s, (const sockaddr*)&a, sizeof a) != 0) {
		::close(s); return -1;
	}
	socklen_t sl = sizeof a;
	if (::getsockname(s, (sockaddr*)&a, &sl) != 0) {
		::close(s); return -1;
	}
	int port = ntohs(a.sin_port);
	::close(s);
	return port;
}

struct Bridge {
	uint16_t port = 0;
	ATLobby::WsBridgeStats        stats;
	ATLobby::ReflectorSocketHandle dummyFd;
	ATLobby::WsBridgeContext      ctx;
	std::atomic<bool>             stop{false};
	std::thread                   th;

	void Start() {
		port = (uint16_t)PickFreePort();
		ATLobby::WsBridgeConfig wcfg{};
		wcfg.port            = port;
		wcfg.listenAddr      = "127.0.0.1";
		wcfg.maxMessageBytes = 64;     // small cap for the oversized test
		wcfg.pingIntervalMs  = 60000;  // long: tests don't need keepalive
		wcfg.pongTimeoutMs   = 60000;
		wcfg.idleTimeoutMs   = 60000;
		th = std::thread([this, wcfg]() mutable {
			ATLobby::RunWsBridge(
				wcfg, stats, dummyFd,
				StubValidate, nullptr,
				StubUdpForward, nullptr,
				ctx, stop);
		});
		// Give the bridge a moment to bind.
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}

	void Stop() {
		stop.store(true);
		if (th.joinable()) th.join();
	}
};

std::string BuildSubprotocol(const char* sessionHex32, const char* role,
                             const char* tokenHex) {
	std::string s = "altirra-netplay.v1, session.";
	s.append(sessionHex32);
	s.append(", role.");
	s.append(role);
	s.append(", token.");
	s.append(tokenHex);
	return s;
}

const char* kFakeSession = "00112233445566778899aabbccddeeff";

// ---------- Tests -------------------------------------------------------------

bool TestUpgradeMissingSubprotocol(Bridge& b) {
	std::printf("test: upgrade rejected when subprotocol missing... ");
	struct mg_mgr mgr;
	mg_mgr_init(&mgr);
	ClientCtx cc;
	char url[64];
	std::snprintf(url, sizeof url,
		"ws://127.0.0.1:%u/netplay", (unsigned)b.port);
	struct mg_connection* c = mg_ws_connect(
		&mgr, url, ClientFn, &cc, NULL);
	if (!c) { std::printf("FAIL (connect)\n"); mg_mgr_free(&mgr); return false; }
	bool ok = WaitFor(mgr, [&]{ return cc.closed; }, 1000);
	mg_mgr_free(&mgr);
	if (!ok || cc.connected) {
		std::printf("FAIL (got connected=%d closed=%d)\n",
			(int)cc.connected, (int)cc.closed);
		return false;
	}
	std::printf("OK\n");
	return true;
}

bool TestUpgradeBadToken(Bridge& b) {
	std::printf("test: host upgrade rejected on bad token (close 4003)... ");
	struct mg_mgr mgr;
	mg_mgr_init(&mgr);
	ClientCtx cc;
	char url[64];
	std::snprintf(url, sizeof url,
		"ws://127.0.0.1:%u/netplay", (unsigned)b.port);
	std::string subp = BuildSubprotocol(
		kFakeSession, "host",
		"deadbeefdeadbeefdeadbeefdeadbeef");  // wrong token
	std::string hdr = "Sec-WebSocket-Protocol: " + subp + "\r\n";
	struct mg_connection* c = mg_ws_connect(
		&mgr, url, ClientFn, &cc, "%s", hdr.c_str());
	if (!c) { std::printf("FAIL\n"); mg_mgr_free(&mgr); return false; }
	// Bridge upgrades the WS (so the browser can read the close code)
	// then immediately sends a Close frame with policy code 4003.
	bool ok = WaitFor(mgr, [&]{ return cc.closed; }, 1000);
	mg_mgr_free(&mgr);
	if (!ok || cc.wsCloseCode != 4003) {
		std::printf("FAIL (closed=%d code=%u)\n",
			(int)cc.closed, (unsigned)cc.wsCloseCode);
		return false;
	}
	std::printf("OK\n");
	return true;
}

// Joiners do NOT need the host's token — the WS bridge accepts any
// joiner upgrade and lets the inner Altirra protocol handle auth
// (entry codes, ROM hash mismatch, etc.).  Mirrors UDP relay
// semantics where ASGR carries no auth.
bool TestUpgradeJoinerNoToken(Bridge& b) {
	std::printf("test: joiner upgrade accepted with no token... ");
	struct mg_mgr mgr;
	mg_mgr_init(&mgr);
	ClientCtx cc;
	char url[64];
	std::snprintf(url, sizeof url,
		"ws://127.0.0.1:%u/netplay", (unsigned)b.port);
	// No "token." subprotocol entry.  Use a fresh session id so it
	// doesn't collide with TestPairExchange's slot.
	const char* sid = "1111111122222222333333334444444a";
	std::string subp = "altirra-netplay.v1,session.";
	subp += sid;
	subp += ",role.joiner";
	std::string hdr = "Sec-WebSocket-Protocol: " + subp + "\r\n";
	struct mg_connection* c = mg_ws_connect(
		&mgr, url, ClientFn, &cc, "%s", hdr.c_str());
	(void)c;
	bool ok = WaitFor(mgr, [&]{ return cc.connected; }, 1000);
	mg_mgr_free(&mgr);
	if (!ok) { std::printf("FAIL\n"); return false; }
	std::printf("OK\n");
	return true;
}

bool TestPairExchange(Bridge& b) {
	std::printf("test: two peers exchange a binary frame... ");
	struct mg_mgr mgr;
	mg_mgr_init(&mgr);
	ClientCtx host, joiner;
	char url[64];
	std::snprintf(url, sizeof url,
		"ws://127.0.0.1:%u/netplay", (unsigned)b.port);

	std::string hHdr = "Sec-WebSocket-Protocol: " +
		BuildSubprotocol(kFakeSession, "host", kFakeToken) + "\r\n";
	std::string jHdr = "Sec-WebSocket-Protocol: " +
		BuildSubprotocol(kFakeSession, "joiner", kFakeToken) + "\r\n";

	struct mg_connection* hc = mg_ws_connect(
		&mgr, url, ClientFn, &host, "%s", hHdr.c_str());
	struct mg_connection* jc = mg_ws_connect(
		&mgr, url, ClientFn, &joiner, "%s", jHdr.c_str());
	if (!hc || !jc) { std::printf("FAIL (connect)\n");
		mg_mgr_free(&mgr); return false; }

	bool ok = WaitFor(mgr,
		[&]{ return host.connected && joiner.connected; }, 2000);
	if (!ok) {
		std::printf("FAIL (handshake h=%d j=%d)\n",
			(int)host.connected, (int)joiner.connected);
		mg_mgr_free(&mgr); return false;
	}

	// Host sends [role=0][0xAA, 0xBB, 0xCC, 0xDD] — minimum 5 bytes
	// (1 envelope + 4 inner magic).
	uint8_t framed[5] = {0, 0xAA, 0xBB, 0xCC, 0xDD};
	mg_ws_send(hc, framed, sizeof framed, WEBSOCKET_OP_BINARY);

	// Joiner should see the inner bytes prepended with recipient_role=1.
	ok = WaitFor(mgr, [&]{ return !joiner.lastBinary.empty(); }, 2000);
	if (!ok || joiner.lastBinary.size() != 5 ||
	    joiner.lastBinary[0] != 1 ||
	    joiner.lastBinary[1] != 0xAA ||
	    joiner.lastBinary[4] != 0xDD) {
		std::printf("FAIL (joiner saw %zu bytes)\n",
			joiner.lastBinary.size());
		mg_mgr_free(&mgr); return false;
	}

	// Joiner replies; host should see role=0 prepended.
	uint8_t reply[5] = {1, 0x11, 0x22, 0x33, 0x44};
	mg_ws_send(jc, reply, sizeof reply, WEBSOCKET_OP_BINARY);
	ok = WaitFor(mgr, [&]{ return !host.lastBinary.empty(); }, 2000);
	if (!ok || host.lastBinary.size() != 5 ||
	    host.lastBinary[0] != 0 ||
	    host.lastBinary[1] != 0x11) {
		std::printf("FAIL (host saw %zu bytes)\n",
			host.lastBinary.size());
		mg_mgr_free(&mgr); return false;
	}

	mg_mgr_free(&mgr);
	std::printf("OK\n");
	return true;
}

bool TestRoleConflict(Bridge& b) {
	std::printf("test: second host upgrade rejected (close 4009)... ");
	struct mg_mgr mgr;
	mg_mgr_init(&mgr);
	ClientCtx h1, h2;
	char url[64];
	std::snprintf(url, sizeof url,
		"ws://127.0.0.1:%u/netplay", (unsigned)b.port);
	// Use a different session than TestPairExchange for isolation.
	const char* sid = "ffeeddccbbaa99887766554433221100";
	std::string hdr = "Sec-WebSocket-Protocol: " +
		BuildSubprotocol(sid, "host", kFakeToken) + "\r\n";
	struct mg_connection* h1c = mg_ws_connect(
		&mgr, url, ClientFn, &h1, "%s", hdr.c_str());
	(void)h1c;
	WaitFor(mgr, [&]{ return h1.connected; }, 1000);
	if (!h1.connected) { std::printf("FAIL (first host)\n");
		mg_mgr_free(&mgr); return false; }
	struct mg_connection* h2c = mg_ws_connect(
		&mgr, url, ClientFn, &h2, "%s", hdr.c_str());
	(void)h2c;
	// Bridge completes the upgrade for h2 (briefly raising connected)
	// so the close frame's 4009 reaches the browser.  We assert on the
	// close code, not on connected==false.
	bool ok = WaitFor(mgr, [&]{ return h2.closed; }, 1000);
	mg_mgr_free(&mgr);
	if (!ok || h2.wsCloseCode != 4009) {
		std::printf("FAIL (h2 closed=%d code=%u)\n",
			(int)h2.closed, (unsigned)h2.wsCloseCode);
		return false;
	}
	std::printf("OK\n");
	return true;
}

bool TestOversizedFrame(Bridge& b) {
	std::printf("test: oversized frame rejected... ");
	struct mg_mgr mgr;
	mg_mgr_init(&mgr);
	ClientCtx cc;
	char url[64];
	std::snprintf(url, sizeof url,
		"ws://127.0.0.1:%u/netplay", (unsigned)b.port);
	const char* sid = "1234567890abcdef1234567890abcdef";
	std::string hdr = "Sec-WebSocket-Protocol: " +
		BuildSubprotocol(sid, "host", kFakeToken) + "\r\n";
	struct mg_connection* c = mg_ws_connect(
		&mgr, url, ClientFn, &cc, "%s", hdr.c_str());
	(void)c;
	bool ok = WaitFor(mgr, [&]{ return cc.connected; }, 1000);
	if (!ok) { std::printf("FAIL (handshake)\n");
		mg_mgr_free(&mgr); return false; }
	std::vector<uint8_t> big(128, 0xAA);
	big[0] = 0;  // role byte
	mg_ws_send(c, big.data(), big.size(), WEBSOCKET_OP_BINARY);
	ok = WaitFor(mgr, [&]{ return cc.closed; }, 2000);
	mg_mgr_free(&mgr);
	if (!ok) { std::printf("FAIL (no close)\n"); return false; }
	std::printf("OK\n");
	return true;
}

bool TestSyntheticByeOnClose(Bridge& b) {
	std::printf("test: surviving peer receives synthetic Bye on close... ");
	struct mg_mgr mgr;
	mg_mgr_init(&mgr);
	ClientCtx host, joiner;
	char url[64];
	std::snprintf(url, sizeof url,
		"ws://127.0.0.1:%u/netplay", (unsigned)b.port);
	const char* sid = "aaaaaaaaaaaaaaaabbbbbbbbbbbbbbbb";
	std::string hHdr = "Sec-WebSocket-Protocol: " +
		BuildSubprotocol(sid, "host", kFakeToken) + "\r\n";
	std::string jHdr = "Sec-WebSocket-Protocol: " +
		BuildSubprotocol(sid, "joiner", kFakeToken) + "\r\n";
	struct mg_connection* hc = mg_ws_connect(
		&mgr, url, ClientFn, &host, "%s", hHdr.c_str());
	struct mg_connection* jc = mg_ws_connect(
		&mgr, url, ClientFn, &joiner, "%s", jHdr.c_str());
	(void)jc;
	bool ok = WaitFor(mgr,
		[&]{ return host.connected && joiner.connected; }, 2000);
	if (!ok) { std::printf("FAIL (handshake)\n");
		mg_mgr_free(&mgr); return false; }

	// Close the host gracefully.
	hc->is_draining = 1;

	// Joiner should receive a NetBye-shaped binary frame from the
	// bridge: 9 bytes (1 envelope + 8 NetBye), envelope byte = 1
	// (joiner side), magic 'A','N','P','B' little-endian = 41 4E 50 42.
	ok = WaitFor(mgr,
		[&]{ return joiner.lastBinary.size() == 9; }, 2000);
	mg_mgr_free(&mgr);
	if (!ok) {
		std::printf("FAIL (joiner saw %zu bytes)\n",
			joiner.lastBinary.size());
		return false;
	}
	const auto& b9 = joiner.lastBinary;
	if (b9[0] != 1 || b9[1] != 'A' || b9[2] != 'N' ||
	    b9[3] != 'P' || b9[4] != 'B') {
		std::printf("FAIL (bad bye contents: %02x %02x %02x %02x %02x)\n",
			b9[0], b9[1], b9[2], b9[3], b9[4]);
		return false;
	}
	std::printf("OK\n");
	return true;
}

}  // namespace

int main() {
	Bridge b;
	b.Start();
	int fails = 0;
	if (!TestUpgradeMissingSubprotocol(b))   ++fails;
	if (!TestUpgradeBadToken(b))             ++fails;
	if (!TestUpgradeJoinerNoToken(b))        ++fails;
	if (!TestPairExchange(b))                ++fails;
	if (!TestRoleConflict(b))                ++fails;
	if (!TestOversizedFrame(b))              ++fails;
	if (!TestSyntheticByeOnClose(b))         ++fails;
	b.Stop();
	if (fails > 0) {
		std::printf("FAILED: %d test(s)\n", fails);
		return 1;
	}
	std::printf("ws bridge: all tests passed\n");
	return 0;
}
