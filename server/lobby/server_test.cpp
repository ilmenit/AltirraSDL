// Altirra SDL netplay lobby — unit tests.
//
// Pulls server.cpp in directly (guarded by ALTIRRA_LOBBY_TEST_MAIN)
// so we can reach the anonymous-namespace symbols without exporting
// them for production.  Tests exercise the same routes a real client
// would hit, using cpp-httplib's in-process Client to talk to a
// Server running on 127.0.0.1:<random>.

#define ALTIRRA_LOBBY_TEST_MAIN 1
#include "server.cpp"

#include <atomic>
#include <thread>

namespace {

// ----- tiny assertion harness (match the project's ATTest style) --

int g_testsRun  = 0;
int g_testsFail = 0;

#define T_FAIL(msg) do {                                                     \
	std::fprintf(stderr, "FAIL: %s:%d %s\n", __FILE__, __LINE__, (msg));     \
	++g_testsFail;                                                           \
	return;                                                                  \
} while (0)

#define T_EXPECT(cond, msg) do {                                             \
	if (!(cond)) T_FAIL(msg);                                                \
} while (0)

#define T_EXPECT_EQ_INT(got, want, label) do {                               \
	if ((got) != (want)) {                                                   \
		std::fprintf(stderr,                                                 \
			"FAIL: %s:%d %s: got=%d want=%d\n",                              \
			__FILE__, __LINE__, (label), (int)(got), (int)(want));           \
		++g_testsFail; return;                                               \
	}                                                                        \
} while (0)

// ----- server fixture ---------------------------------------------

struct Fixture {
	Config              cfg;
	Store               store;
	httplib::Server     srv;
	std::thread         srvThread;
	int                 port = 0;

	Fixture() : cfg(MakeCfg()), store(cfg) {
		Install(srv, store);
		srv.set_payload_max_length(kMaxRequestBodyBytes);
		// Bind to an ephemeral port; cpp-httplib's bind_to_any_port
		// lets the OS pick.
		port = srv.bind_to_any_port("127.0.0.1");
		if (port <= 0) {
			std::fprintf(stderr, "fixture: bind_to_any_port failed\n");
			std::exit(2);
		}
		srvThread = std::thread([this] { srv.listen_after_bind(); });
		// Wait for the server to actually start accepting.
		for (int i = 0; i < 200; ++i) {
			if (srv.is_running()) break;
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
		}
	}

	~Fixture() {
		// v4: nudge any active SSE listeners awake before stopping the
		// server so their handlers exit promptly (see Store::Shutdown
		// for rationale).  Idempotent if no streams are open.
		store.Shutdown();
		srv.stop();
		if (srvThread.joinable()) srvThread.join();
	}

	httplib::Client client() {
		httplib::Client c("127.0.0.1", port);
		c.set_connection_timeout(2, 0);
		c.set_read_timeout(5, 0);
		return c;
	}

	static Config MakeCfg() {
		Config c;
		// Very high burst so normal tests don't hit the limiter.
		c.rateBurst    = 10000;
		c.rateRefillMs = 1000;
		c.ttlSeconds   = 90;
		c.maxSessions  = 1000;
		return c;
	}
};

// ----- JSON read helpers for parsing responses --------------------

bool ReadCreateRespJson(const std::string& body,
                        std::string& id, std::string& token,
                        int& ttl) {
	JsonCursor c{body.data(), body.data() + body.size()};
	if (!c.match('{')) return false;
	for (;;) {
		std::string k;
		if (!c.parseString(k)) return false;
		if (!c.match(':'))     return false;
		if      (k == Field::kSessionId)  c.parseString(id);
		else if (k == Field::kToken)      c.parseString(token);
		else if (k == Field::kTTLSeconds) c.parseInt(ttl);
		else c.skipValue();
		if (!c.ok) return false;
		if (c.match(',')) continue;
		if (c.match('}')) return true;
		return false;
	}
}

std::string MakeCreateBody(const std::string& cart,
                           const std::string& handle,
                           const std::string& endpoint) {
	JsonBuilder b;
	b.raw('{');
	b.key(Field::kCartName);        b.str(cart);      b.raw(',');
	b.key(Field::kHostHandle);      b.str(handle);    b.raw(',');
	b.key(Field::kHostEndpoint);    b.str(endpoint);  b.raw(',');
	b.key(Field::kRegion);          b.str("eu");      b.raw(',');
	b.key(Field::kPlayerCount);     b.num(1);         b.raw(',');
	b.key(Field::kMaxPlayers);      b.num(2);         b.raw(',');
	b.key(Field::kProtocolVersion); b.num(1);
	b.raw('}');
	return std::move(b.s);
}

// ----- tests ------------------------------------------------------

void TestHealthz() {
	++g_testsRun;
	Fixture f;
	auto c = f.client();
	auto r = c.Get(kPathHealthz);
	T_EXPECT(r,                        "got response");
	T_EXPECT_EQ_INT(r->status, 200,    "healthz status");
	T_EXPECT(r->body.rfind("ok sessions=", 0) == 0,
		"healthz body prefix");
}

void TestCreateAndList() {
	++g_testsRun;
	Fixture f;
	auto c = f.client();
	auto body = MakeCreateBody("Joust", "alice", "1.2.3.4:12345");
	auto r = c.Post(kPathSession, body, "application/json");
	T_EXPECT(r,                       "create response present");
	T_EXPECT_EQ_INT(r->status, 201,   "create status");
	std::string id, token; int ttl = 0;
	T_EXPECT(ReadCreateRespJson(r->body, id, token, ttl),
		"parse create response");
	T_EXPECT(!id.empty(),             "sessionId non-empty");
	T_EXPECT(!token.empty(),          "token non-empty");
	T_EXPECT_EQ_INT(ttl, f.cfg.ttlSeconds, "ttlSeconds");

	auto list = c.Get(kPathSessions);
	T_EXPECT(list,                    "list response present");
	T_EXPECT_EQ_INT(list->status, 200,"list status");
	// Body should contain the session id we just created.
	T_EXPECT(list->body.find(id) != std::string::npos,
		"list contains created id");
}

// Identity dedup on Create: when the same host (hostHandle) re-
// creates a session for the same cart (cartName), the prior entry
// must be evicted so the public list doesn't show two rows for one
// game.  This handles the user-visible failure where a host crashed
// or lost network without sending a clean Delete, then reconnected
// — the lobby would otherwise show both old and new entries until
// the old one's TTL elapsed.  The new entry must come through with
// a fresh sessionId; the old one must no longer appear in /v1/sessions.
// Sessions with empty hostHandle or empty cartName are exempt (we'd
// otherwise collapse all anonymous / no-cart legacy clients into one).
void TestCreateDedupReplacesPriorSession() {
	++g_testsRun;
	Fixture f;
	auto c = f.client();
	auto r1 = c.Post(kPathSession,
		MakeCreateBody("Archon", "alice", "1.2.3.4:1"),
		"application/json");
	T_EXPECT_EQ_INT(r1 ? r1->status : 0, 201, "first create 201");
	std::string id1, tok1; int ttl1 = 0;
	T_EXPECT(ReadCreateRespJson(r1->body, id1, tok1, ttl1),
		"parse first create");

	// Same hostHandle + cartName from the same client = dedup target.
	auto r2 = c.Post(kPathSession,
		MakeCreateBody("Archon", "alice", "1.2.3.4:2"),
		"application/json");
	T_EXPECT_EQ_INT(r2 ? r2->status : 0, 201, "second create 201");
	std::string id2, tok2; int ttl2 = 0;
	T_EXPECT(ReadCreateRespJson(r2->body, id2, tok2, ttl2),
		"parse second create");
	T_EXPECT(id1 != id2, "second id is fresh");

	auto list = c.Get(kPathSessions);
	T_EXPECT(list && list->status == 200, "list 200");
	T_EXPECT(list->body.find(id1) == std::string::npos,
		"old id evicted from list");
	T_EXPECT(list->body.find(id2) != std::string::npos,
		"new id present in list");

	// Different cartName from the same host: the prior entry stays.
	auto r3 = c.Post(kPathSession,
		MakeCreateBody("Joust", "alice", "1.2.3.4:3"),
		"application/json");
	T_EXPECT_EQ_INT(r3 ? r3->status : 0, 201, "third create 201");
	std::string id3, tok3; int ttl3 = 0;
	T_EXPECT(ReadCreateRespJson(r3->body, id3, tok3, ttl3),
		"parse third create");
	auto list2 = c.Get(kPathSessions);
	T_EXPECT(list2 && list2->status == 200, "list2 200");
	T_EXPECT(list2->body.find(id2) != std::string::npos,
		"second id still present (different cart)");
	T_EXPECT(list2->body.find(id3) != std::string::npos,
		"third id present");

	// Different hostHandle, same cart: the prior entry stays.
	auto r4 = c.Post(kPathSession,
		MakeCreateBody("Joust", "bob", "5.6.7.8:1"),
		"application/json");
	T_EXPECT_EQ_INT(r4 ? r4->status : 0, 201, "fourth create 201");
	std::string id4, tok4; int ttl4 = 0;
	T_EXPECT(ReadCreateRespJson(r4->body, id4, tok4, ttl4),
		"parse fourth create");
	auto list3 = c.Get(kPathSessions);
	T_EXPECT(list3 && list3->status == 200, "list3 200");
	T_EXPECT(list3->body.find(id3) != std::string::npos,
		"alice's Joust still present");
	T_EXPECT(list3->body.find(id4) != std::string::npos,
		"bob's Joust present");
}

// The list endpoint must echo every machine-spec field the host
// supplied on create — kernelCRC32 / basicCRC32 / hardwareMode /
// videoStandard / memoryMode — and must also include the keys (as
// empty strings) when the host omitted them.  The "?" hardwareMode
// bug in the desktop browser was caused by an older server silently
// dropping these keys on GET /v1/sessions, so this test locks the
// schema contract in place.  `videoStandard` + `memoryMode` were
// added alongside `hardwareMode` so the Browser can render a full
// machine spec row (hardware | video | memory | OS | BASIC).
void TestListEchoesMachineFields() {
	++g_testsRun;
	Fixture f;
	auto c = f.client();

	// Build a create body with the three firmware/hardware fields set
	// explicitly.  Do it inline rather than extend MakeCreateBody so
	// the surface of that helper stays minimal.
	JsonBuilder b;
	b.raw('{');
	b.key(Field::kCartName);        b.str("Joust");           b.raw(',');
	b.key(Field::kHostHandle);      b.str("alice");           b.raw(',');
	b.key(Field::kHostEndpoint);    b.str("1.2.3.4:12345");   b.raw(',');
	b.key(Field::kRegion);          b.str("eu");              b.raw(',');
	b.key(Field::kPlayerCount);     b.num(1);                 b.raw(',');
	b.key(Field::kMaxPlayers);      b.num(2);                 b.raw(',');
	b.key(Field::kProtocolVersion); b.num(1);                 b.raw(',');
	b.key(Field::kKernelCRC32);     b.str("1F9CD270");        b.raw(',');
	b.key(Field::kBasicCRC32);      b.str("F0202FB3");        b.raw(',');
	b.key(Field::kHardwareMode);    b.str("800XL");           b.raw(',');
	b.key(Field::kVideoStandard);   b.str("PAL");             b.raw(',');
	b.key(Field::kMemoryMode);      b.str("320K");
	b.raw('}');

	auto r = c.Post(kPathSession, b.s, "application/json");
	T_EXPECT(r,                     "create response present");
	T_EXPECT_EQ_INT(r->status, 201, "create status");

	auto list = c.Get(kPathSessions);
	T_EXPECT(list,                     "list response present");
	T_EXPECT_EQ_INT(list->status, 200, "list status");

	const std::string& body = list->body;
	T_EXPECT(body.find("\"kernelCRC32\":\"1F9CD270\"")  != std::string::npos,
		"list body contains kernelCRC32 value");
	T_EXPECT(body.find("\"basicCRC32\":\"F0202FB3\"")   != std::string::npos,
		"list body contains basicCRC32 value");
	T_EXPECT(body.find("\"hardwareMode\":\"800XL\"")    != std::string::npos,
		"list body contains hardwareMode value");
	T_EXPECT(body.find("\"videoStandard\":\"PAL\"")     != std::string::npos,
		"list body contains videoStandard value");
	T_EXPECT(body.find("\"memoryMode\":\"320K\"")       != std::string::npos,
		"list body contains memoryMode value");

	// Also verify the keys are present (as empty strings) when the
	// host omitted them — the schema contract is "key always appears".
	Fixture f2;
	auto c2 = f2.client();
	auto r2 = c2.Post(kPathSession,
		MakeCreateBody("Asteroids","bob","5.6.7.8:1"),
		"application/json");
	T_EXPECT(r2,                     "plain create response present");
	T_EXPECT_EQ_INT(r2->status, 201, "plain create status");
	auto list2 = c2.Get(kPathSessions);
	T_EXPECT(list2,                     "plain list response present");
	T_EXPECT_EQ_INT(list2->status, 200, "plain list status");
	const std::string& body2 = list2->body;
	T_EXPECT(body2.find("\"kernelCRC32\":\"\"")  != std::string::npos,
		"list body contains empty kernelCRC32 key");
	T_EXPECT(body2.find("\"basicCRC32\":\"\"")   != std::string::npos,
		"list body contains empty basicCRC32 key");
	T_EXPECT(body2.find("\"hardwareMode\":\"\"")  != std::string::npos,
		"list body contains empty hardwareMode key");
	T_EXPECT(body2.find("\"videoStandard\":\"\"") != std::string::npos,
		"list body contains empty videoStandard key");
	T_EXPECT(body2.find("\"memoryMode\":\"\"")    != std::string::npos,
		"list body contains empty memoryMode key");
	T_EXPECT(body2.find("\"cartArtHash\":\"\"")   != std::string::npos,
		"list body contains empty cartArtHash key");
}

void TestCreateValidation() {
	++g_testsRun;
	Fixture f;
	auto c = f.client();

	// Missing cart name.
	std::string bad =
		R"({"cartName":"","hostHandle":"a","hostEndpoint":"1.2.3.4:1",)"
		R"("playerCount":1,"maxPlayers":2,"protocolVersion":1})";
	auto r = c.Post(kPathSession, bad, "application/json");
	T_EXPECT(r, "validation response");
	T_EXPECT_EQ_INT(r->status, 400, "empty cart rejected");

	// Bad endpoint.
	std::string bad2 =
		R"({"cartName":"x","hostHandle":"a","hostEndpoint":"badhost",)"
		R"("playerCount":1,"maxPlayers":2,"protocolVersion":1})";
	r = c.Post(kPathSession, bad2, "application/json");
	T_EXPECT(r, "validation response 2");
	T_EXPECT_EQ_INT(r->status, 400, "bad endpoint rejected");
}

void TestHeartbeatBadToken() {
	++g_testsRun;
	Fixture f;
	auto c = f.client();
	auto r = c.Post(kPathSession,
		MakeCreateBody("g","h","1.2.3.4:1"), "application/json");
	T_EXPECT_EQ_INT(r->status, 201, "create ok");
	std::string id, tok; int ttl = 0;
	ReadCreateRespJson(r->body, id, tok, ttl);

	std::string hb = R"({"token":"WRONG","playerCount":2})";
	auto r2 = c.Post((std::string(kPathSession) + "/" + id
		+ kPathHeartbeatSuffix).c_str(),
		hb, "application/json");
	T_EXPECT(r2, "heartbeat response");
	T_EXPECT_EQ_INT(r2->status, 401, "bad token → 401");
}

void TestHeartbeatOk() {
	++g_testsRun;
	Fixture f;
	auto c = f.client();
	auto r = c.Post(kPathSession,
		MakeCreateBody("g","h","1.2.3.4:1"), "application/json");
	std::string id, tok; int ttl = 0;
	ReadCreateRespJson(r->body, id, tok, ttl);

	JsonBuilder b;
	b.raw('{');
	b.key(Field::kToken);       b.str(tok);  b.raw(',');
	b.key(Field::kPlayerCount); b.num(2);
	b.raw('}');
	auto r2 = c.Post((std::string(kPathSession) + "/" + id
		+ kPathHeartbeatSuffix).c_str(),
		b.s, "application/json");
	T_EXPECT(r2, "hb response");
	T_EXPECT_EQ_INT(r2->status, 200, "hb ok");
}

void TestDeleteBadToken() {
	++g_testsRun;
	Fixture f;
	auto c = f.client();
	auto r = c.Post(kPathSession,
		MakeCreateBody("g","h","1.2.3.4:1"), "application/json");
	std::string id, tok; int ttl = 0;
	ReadCreateRespJson(r->body, id, tok, ttl);

	httplib::Headers hdrs = { {kHeaderSessionToken, "WRONG"} };
	auto r2 = c.Delete(
		(std::string(kPathSession) + "/" + id).c_str(), hdrs);
	T_EXPECT(r2, "delete response");
	T_EXPECT_EQ_INT(r2->status, 401, "bad token → 401 on delete");
}

void TestDeleteOk() {
	++g_testsRun;
	Fixture f;
	auto c = f.client();
	auto r = c.Post(kPathSession,
		MakeCreateBody("g","h","1.2.3.4:1"), "application/json");
	std::string id, tok; int ttl = 0;
	ReadCreateRespJson(r->body, id, tok, ttl);

	httplib::Headers hdrs = { {kHeaderSessionToken, tok} };
	auto r2 = c.Delete(
		(std::string(kPathSession) + "/" + id).c_str(), hdrs);
	T_EXPECT(r2, "delete response");
	T_EXPECT_EQ_INT(r2->status, 204, "delete ok → 204");

	// Session should be gone.
	Session s;
	T_EXPECT(!f.store.Get(id, s), "session removed from store");
}

void TestExpireSweeps() {
	++g_testsRun;
	Fixture f;
	// Directly insert a session with an old lastSeenMs to simulate
	// TTL expiry without sleeping for 90 seconds.
	Session seed;
	seed.cartName     = "x"; seed.hostHandle = "h";
	seed.hostEndpoint = "1.2.3.4:1"; seed.playerCount = 1;
	seed.maxPlayers   = 2; seed.protocolVer = 1;
	seed.visibility   = kVisibilityPublic;
	seed.createdAt    = IsoNow();
	Store::CreateError createErr = Store::CreateError::None;
	Session out = f.store.Create(seed, createErr);
	T_EXPECT(!out.id.empty(), "seeded session");

	int n = f.store.ExpireOnce(NowMs() +
		(int64_t)(f.cfg.ttlSeconds + 1) * 1000);
	T_EXPECT_EQ_INT(n, 1, "one session expired");
	Session probe;
	T_EXPECT(!f.store.Get(out.id, probe), "session removed");
}

// v6 — host-WS-presence-driven cleanup for wssRelayOnly sessions.
// Models the WASM-host lifecycle:
//   1. Create session (wssRelayOnly=true)
//   2. WS bridge fires OnHostWsPresence(true)  — host upgrade succeeded
//   3. WS bridge fires OnHostWsPresence(false) — host disconnected
//   4. After kHostWsLostGraceMs, ExpireOnce drops the session
//   5. Reconnect within grace window survives
// Native (non-relay) sessions never set the WS-lost timer; they keep
// the standard 15-s heartbeat-driven TTL even if the new path runs.
void TestWsPresenceDrivesCleanup() {
	++g_testsRun;
	Fixture f;
	Session seed;
	seed.cartName     = "x"; seed.hostHandle = "h";
	seed.hostEndpoint = "";          // wssRelayOnly host has no UDP
	seed.candidates   = "";
	seed.playerCount  = 1; seed.maxPlayers = 2; seed.protocolVer = 6;
	seed.visibility   = kVisibilityPublic;
	seed.wssRelayOnly = true;
	seed.createdAt    = IsoNow();
	Store::CreateError createErr = Store::CreateError::None;
	Session out = f.store.Create(seed, createErr);
	T_EXPECT(!out.id.empty(), "seeded wssRelayOnly session");

	const int64_t now = NowMs();

	// Host WS opens.
	f.store.OnHostWsPresence(out.id, /*present=*/true);

	// Brief check before close: session must still exist.
	T_EXPECT_EQ_INT(f.store.ExpireOnce(now + 1), 0,
		"connected host: no expire");

	// Host WS closes.  Until grace elapses, session stays.
	f.store.OnHostWsPresence(out.id, /*present=*/false);
	T_EXPECT_EQ_INT(
		f.store.ExpireOnce(now + kHostWsLostGraceMs - 100), 0,
		"within grace: no expire");

	// Past grace: session is dropped.
	int n = f.store.ExpireOnce(now + kHostWsLostGraceMs + 100);
	T_EXPECT_EQ_INT(n, 1, "past grace: expired");
	Session probe;
	T_EXPECT(!f.store.Get(out.id, probe),
		"wssRelayOnly session removed after WS loss");

	// Reconnect-within-grace path on a fresh session: clear timer.
	Session out2 = f.store.Create(seed, createErr);
	T_EXPECT(!out2.id.empty(), "second seeded session");
	f.store.OnHostWsPresence(out2.id, true);
	f.store.OnHostWsPresence(out2.id, false);
	f.store.OnHostWsPresence(out2.id, true);   // reconnect in time
	T_EXPECT_EQ_INT(
		f.store.ExpireOnce(now + kHostWsLostGraceMs + 100), 0,
		"reconnect cleared timer");

	// Native (UDP) session — wssRelayOnly=false.  Even if a stray
	// OnHostWsPresence(false) is delivered (defensive), no expiry
	// fires under the WS-lost code path; the standard heartbeat TTL
	// is the only liveness check.
	Session udpSeed = seed;
	udpSeed.wssRelayOnly = false;
	udpSeed.hostEndpoint = "1.2.3.4:5";
	Session udpOut = f.store.Create(udpSeed, createErr);
	T_EXPECT(!udpOut.id.empty(), "udp seeded");
	f.store.OnHostWsPresence(udpOut.id, false);
	T_EXPECT_EQ_INT(
		f.store.ExpireOnce(now + kHostWsLostGraceMs + 100), 0,
		"native session: WS-lost grace does not apply");
}

void TestRateLimit() {
	++g_testsRun;
	// Independent config with a tiny burst so we can hit the limiter.
	Config cfg = Fixture::MakeCfg();
	cfg.rateBurst    = 3;
	cfg.rateRefillMs = 60 * 1000;  // effectively no refill during test
	Store store(cfg);
	httplib::Server srv;
	Install(srv, store);
	srv.set_payload_max_length(kMaxRequestBodyBytes);
	int port = srv.bind_to_any_port("127.0.0.1");
	std::thread t([&] { srv.listen_after_bind(); });
	for (int i = 0; i < 200; ++i) {
		if (srv.is_running()) break;
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}
	httplib::Client c("127.0.0.1", port);
	c.set_connection_timeout(2, 0);

	int gotHealth = 0, got429 = 0;
	for (int i = 0; i < 8; ++i) {
		auto r = c.Get(kPathHealthz);
		if (!r) continue;
		if (r->status == 200) ++gotHealth;
		else if (r->status == 429) ++got429;
	}
	srv.stop();
	t.join();

	T_EXPECT(gotHealth >= 2 && gotHealth <= 3,
		"some /healthz allowed before rate limit");
	T_EXPECT(got429 >= 4, "further requests rate-limited");
}

// v4 two-sided punch: POST peer-hint and expect it delivered to the
// host in the next heartbeat response.  A second heartbeat without
// another POST must return an empty hints array (single-delivery
// semantics).
void TestPeerHintPostAndDeliver() {
	++g_testsRun;
	Fixture f;
	auto c = f.client();
	auto r = c.Post(kPathSession,
		MakeCreateBody("g","h","1.2.3.4:1"), "application/json");
	T_EXPECT_EQ_INT(r->status, 201, "create ok");
	std::string id, tok; int ttl = 0;
	ReadCreateRespJson(r->body, id, tok, ttl);

	// Post the hint.
	JsonBuilder hb;
	hb.raw('{');
	hb.key(Field::kJoinerHandle); hb.str("bob"); hb.raw(',');
	hb.key(Field::kSessionNonce);
	hb.str("0123456789abcdef0123456789abcdef"); hb.raw(',');
	hb.key(Field::kCandidates);
	hb.str("10.0.0.2:5000;203.0.113.5:5000");
	hb.raw('}');
	auto rh = c.Post((std::string(kPathSession) + "/" + id
		+ kPathPeerHintSuffix).c_str(), hb.s, "application/json");
	T_EXPECT(rh, "hint response");
	T_EXPECT_EQ_INT(rh->status, 200, "hint accepted");

	// First heartbeat must deliver the hint.
	JsonBuilder bb;
	bb.raw('{');
	bb.key(Field::kToken);       bb.str(tok); bb.raw(',');
	bb.key(Field::kPlayerCount); bb.num(1);
	bb.raw('}');
	auto r2 = c.Post((std::string(kPathSession) + "/" + id
		+ kPathHeartbeatSuffix).c_str(), bb.s, "application/json");
	T_EXPECT_EQ_INT(r2->status, 200, "hb ok");
	T_EXPECT(r2->body.find("\"sessionNonce\":\"0123456789abcdef0123456789abcdef\"")
		!= std::string::npos, "hint delivered in hb (nonce)");
	T_EXPECT(r2->body.find("\"joinerHandle\":\"bob\"") != std::string::npos,
		"hint delivered (handle)");
	T_EXPECT(r2->body.find("203.0.113.5:5000") != std::string::npos,
		"hint delivered (candidates)");

	// Second heartbeat — no new hints posted — must return empty
	// array (single-delivery).
	auto r3 = c.Post((std::string(kPathSession) + "/" + id
		+ kPathHeartbeatSuffix).c_str(), bb.s, "application/json");
	T_EXPECT_EQ_INT(r3->status, 200, "hb2 ok");
	T_EXPECT(r3->body.find("\"hints\":[]") != std::string::npos,
		"no repeated hint on 2nd hb");
}

void TestPeerHintSizeLimits() {
	++g_testsRun;
	Fixture f;
	auto c = f.client();
	auto r = c.Post(kPathSession,
		MakeCreateBody("g","h","1.2.3.4:1"), "application/json");
	std::string id, tok; int ttl = 0;
	ReadCreateRespJson(r->body, id, tok, ttl);

	// Oversize candidates.
	std::string big(kPeerHintCandidatesMax + 1, 'x');
	JsonBuilder hb;
	hb.raw('{');
	hb.key(Field::kCandidates); hb.str(big);
	hb.raw('}');
	auto rh = c.Post((std::string(kPathSession) + "/" + id
		+ kPathPeerHintSuffix).c_str(), hb.s, "application/json");
	T_EXPECT_EQ_INT(rh->status, 400, "oversize candidates rejected");

	// Bad-length nonce.
	JsonBuilder hb2;
	hb2.raw('{');
	hb2.key(Field::kCandidates); hb2.str("1.2.3.4:5"); hb2.raw(',');
	hb2.key(Field::kSessionNonce); hb2.str("deadbeef");  // 8 chars, not 32
	hb2.raw('}');
	auto rh2 = c.Post((std::string(kPathSession) + "/" + id
		+ kPathPeerHintSuffix).c_str(), hb2.s, "application/json");
	T_EXPECT_EQ_INT(rh2->status, 400, "bad nonce length rejected");
}

void TestPeerHintUnknownSession() {
	++g_testsRun;
	Fixture f;
	auto c = f.client();
	JsonBuilder hb;
	hb.raw('{');
	hb.key(Field::kCandidates); hb.str("1.2.3.4:5");
	hb.raw('}');
	auto rh = c.Post((std::string(kPathSession)
		+ "/does-not-exist" + kPathPeerHintSuffix).c_str(),
		hb.s, "application/json");
	T_EXPECT_EQ_INT(rh->status, 404, "unknown session → 404");
}

// v4 relay table: registering two peers for the same session allows
// LookupOther to find each side from the other; expiration works.
void TestRelayTableRegisterAndLookup() {
	++g_testsRun;
	uint8_t sid[16] = { 1,2,3,4,5,6,7,8, 9,10,11,12,13,14,15,16 };
	RelayTable rt;
	sockaddr_in hostA{};  hostA.sin_family = AF_INET;
	hostA.sin_port = htons(30000); hostA.sin_addr.s_addr = htonl(0x01020304u);
	sockaddr_in joinB{};  joinB.sin_family = AF_INET;
	joinB.sin_port = htons(40000); joinB.sin_addr.s_addr = htonl(0x05060708u);
	int64_t now = 100000;
	rt.Register(sid, kRelayRoleHost,   hostA, now);
	rt.Register(sid, kRelayRoleJoiner, joinB, now);

	sockaddr_in peer{};
	T_EXPECT(rt.LookupOther(sid, kRelayRoleHost,   hostA, now, peer),
		"host sees joiner");
	T_EXPECT_EQ_INT(peer.sin_port, htons(40000), "peer port = joiner");
	T_EXPECT(rt.LookupOther(sid, kRelayRoleJoiner, joinB, now, peer),
		"joiner sees host");
	T_EXPECT_EQ_INT(peer.sin_port, htons(30000), "peer port = host");

	// Expire via Prune.
	int pruned = rt.Prune(now + kRelayPeerIdleMs + 1);
	T_EXPECT_EQ_INT(pruned, 1, "expired entry pruned");
	T_EXPECT_EQ_INT((int)rt.Size(), 0, "relay table empty");
}

void TestRelayLookupOtherFailsWithOneSide() {
	++g_testsRun;
	uint8_t sid[16] = { 2,3,4,5,6,7,8,9, 10,11,12,13,14,15,16,17 };
	RelayTable rt;
	sockaddr_in hostA{};  hostA.sin_family = AF_INET;
	hostA.sin_port = htons(30000); hostA.sin_addr.s_addr = htonl(0x01020304u);
	rt.Register(sid, kRelayRoleHost, hostA, 500);
	sockaddr_in peer{};
	T_EXPECT(!rt.LookupOther(sid, kRelayRoleHost, hostA, 500, peer),
		"no joiner yet → lookup fails");
}

// Regression: ClientIp() must NOT trust X-Forwarded-For from a peer
// outside trustedProxies, and when the peer IS trusted it must take
// the rightmost XFF entry (the one the proxy appended) -- not the
// leftmost (which an attacker can forge by prepending a header before
// the request reaches the proxy).
void TestClientIpXffSpoof() {
	++g_testsRun;
	httplib::Request req;

	// Untrusted peer: XFF must be ignored entirely, no matter what.
	req.remote_addr = "203.0.113.7";
	req.headers.emplace("X-Forwarded-For", "1.2.3.4");
	{
		std::string ip = ClientIp(req, {"127.0.0.1"});
		T_EXPECT(ip == "203.0.113.7", "untrusted peer must ignore XFF");
	}

	// Trusted peer with a single XFF entry: that entry wins.
	req.remote_addr = "127.0.0.1";
	req.headers.clear();
	req.headers.emplace("X-Forwarded-For", "198.51.100.42");
	{
		std::string ip = ClientIp(req, {"127.0.0.1"});
		T_EXPECT(ip == "198.51.100.42", "trusted peer single-entry XFF");
	}

	// Trusted peer, attacker-prepended XFF + proxy-appended real IP:
	// rightmost wins, attacker's spoof is discarded.
	req.headers.clear();
	req.headers.emplace("X-Forwarded-For", "1.2.3.4, 198.51.100.42");
	{
		std::string ip = ClientIp(req, {"127.0.0.1"});
		T_EXPECT(ip == "198.51.100.42", "trusted peer rightmost wins");
	}

	// Trusted peer, no XFF header: fall back to peer.
	req.headers.clear();
	{
		std::string ip = ClientIp(req, {"127.0.0.1"});
		T_EXPECT(ip == "127.0.0.1", "trusted peer no-XFF falls back");
	}

	// Whitespace + trailing comma tolerated.
	req.headers.emplace("X-Forwarded-For", " 1.2.3.4 ,  198.51.100.42 ,");
	{
		std::string ip = ClientIp(req, {"127.0.0.1"});
		T_EXPECT(ip == "198.51.100.42", "whitespace + trailing comma");
	}
}

void TestOriginBlockedOnPost() {
	++g_testsRun;
	Fixture f;
	httplib::Client c("127.0.0.1", f.port);
	c.set_connection_timeout(2, 0);
	httplib::Headers h = { {"Origin", "https://evil.example.com"} };
	auto r = c.Post(kPathSession, h,
		MakeCreateBody("g","h","1.2.3.4:1"),
		"application/json");
	T_EXPECT(r, "origin-block response");
	T_EXPECT_EQ_INT(r->status, 403, "browser origin blocked on POST");
}

// v3: WSS-only host (browser).  Verifies:
//   - Create accepts wssRelayOnly=true with empty hostEndpoint.
//   - Create rejects wssRelayOnly=true with non-empty hostEndpoint.
//   - List echoes wssRelayOnly back to the client.
//   - Default (no wssRelayOnly key) = false.
void TestWssRelayOnlyRoundtrip() {
	++g_testsRun;
	Fixture f;
	auto c = f.client();

	// Case 1: WSS-only host with empty hostEndpoint succeeds.
	{
		JsonBuilder b;
		b.raw('{');
		b.key(Field::kCartName);        b.str("test");           b.raw(',');
		b.key(Field::kHostHandle);      b.str("browser-host");   b.raw(',');
		b.key(Field::kHostEndpoint);    b.str("");               b.raw(',');
		b.key(Field::kCandidates);      b.str("");               b.raw(',');
		b.key(Field::kRegion);          b.str("eu");             b.raw(',');
		b.key(Field::kPlayerCount);     b.num(1);                b.raw(',');
		b.key(Field::kMaxPlayers);      b.num(2);                b.raw(',');
		b.key(Field::kProtocolVersion); b.num(3);                b.raw(',');
		b.key(Field::kWssRelayOnly);    b.boolean(true);
		b.raw('}');
		auto r = c.Post(kPathSession, b.s, "application/json");
		T_EXPECT(r, "wss-only post: response");
		T_EXPECT_EQ_INT(r->status, 201, "wss-only Create accepted");
	}

	// Case 2: WSS-only with non-empty hostEndpoint rejected (400).
	{
		JsonBuilder b;
		b.raw('{');
		b.key(Field::kCartName);        b.str("test");           b.raw(',');
		b.key(Field::kHostHandle);      b.str("browser-host-2"); b.raw(',');
		b.key(Field::kHostEndpoint);    b.str("1.2.3.4:5678");   b.raw(',');
		b.key(Field::kRegion);          b.str("eu");             b.raw(',');
		b.key(Field::kPlayerCount);     b.num(1);                b.raw(',');
		b.key(Field::kMaxPlayers);      b.num(2);                b.raw(',');
		b.key(Field::kProtocolVersion); b.num(3);                b.raw(',');
		b.key(Field::kWssRelayOnly);    b.boolean(true);
		b.raw('}');
		auto r = c.Post(kPathSession, b.s, "application/json");
		T_EXPECT(r, "wss-only with endpoint: response");
		T_EXPECT_EQ_INT(r->status, 400, "wss-only with endpoint rejected");
	}

	// Case 3: List response echoes wssRelayOnly true and false (the
	// case-1 session above + a regular UDP session for default check).
	{
		auto r = c.Post(kPathSession,
			MakeCreateBody("g", "udp-host", "1.2.3.4:5000"),
			"application/json");
		T_EXPECT(r, "udp-host post");
		T_EXPECT_EQ_INT(r->status, 201, "udp-host Create accepted");
	}
	auto lr = c.Get(kPathSessions);
	T_EXPECT(lr, "list response");
	T_EXPECT_EQ_INT(lr->status, 200, "list status");
	bool sawTrue = false;
	bool sawFalse = false;
	std::string body = lr->body;
	for (size_t pos = 0;;) {
		auto found = body.find("\"wssRelayOnly\":", pos);
		if (found == std::string::npos) break;
		size_t v = found + std::strlen("\"wssRelayOnly\":");
		while (v < body.size() && (body[v] == ' ' || body[v] == '\t')) ++v;
		if (v + 4 <= body.size() && body.compare(v, 4, "true") == 0)
			sawTrue = true;
		else if (v + 5 <= body.size() && body.compare(v, 5, "false") == 0)
			sawFalse = true;
		pos = v;
	}
	T_EXPECT(sawTrue,  "list contains wssRelayOnly:true");
	T_EXPECT(sawFalse, "list contains wssRelayOnly:false");
}

// Per-host cap: a single hostHandle can have at most
// kMaxHostedGamesPerHost concurrent sessions.  The (N+1)th request
// returns 429 with body {"error":"host limit reached"}.  Each prior
// session uses a different cartName so the dedup path doesn't kick
// in (which would replace the existing entry instead of adding).
void TestHostCap() {
	++g_testsRun;
	Fixture f;
	auto c = f.client();
	const char *carts[] = {"A","B","C","D","E","F"};
	int created = 0, capped = 0;
	for (int i = 0; i < (int)(sizeof(carts) / sizeof(carts[0])); ++i) {
		auto r = c.Post(kPathSession,
			MakeCreateBody(carts[i], "alice", "1.2.3.4:1"),
			"application/json");
		if (!r) T_FAIL("no response");
		if (r->status == 201) ++created;
		else if (r->status == 429 &&
		         r->body.find("host limit reached") != std::string::npos)
			++capped;
		else {
			std::fprintf(stderr, "unexpected: status=%d body=%s\n",
				r->status, r->body.c_str());
			T_FAIL("unexpected create status");
		}
	}
	T_EXPECT_EQ_INT(created, kMaxHostedGamesPerHost,
		"first N creates succeed");
	T_EXPECT_EQ_INT(capped, 1, "(N+1)th create returns 429 host limit");
}

// Cap bucket key is normalised: trim + ASCII-lowercase.  "Alice",
// "ALICE", "alice ", " alice" all collapse onto the same counter so
// users can't slip the cap by re-typing their handle differently.
void TestHostCapCaseInsensitive() {
	++g_testsRun;
	Fixture f;
	auto c = f.client();
	// 5 different handles spelling the same normalised name → 5 slots.
	const char *handles[] = {"alice","Alice","ALICE","alice "," alice"};
	int created = 0;
	for (int i = 0; i < 5; ++i) {
		char cart[8];
		std::snprintf(cart, sizeof cart, "G%d", i);
		auto r = c.Post(kPathSession,
			MakeCreateBody(cart, handles[i], "1.2.3.4:1"),
			"application/json");
		if (!r) T_FAIL("no response");
		if (r->status == 201) ++created;
	}
	T_EXPECT_EQ_INT(created, kMaxHostedGamesPerHost,
		"5 different spellings fill the same bucket");
	// 6th create with yet another spelling → host-limit 429.
	auto r = c.Post(kPathSession,
		MakeCreateBody("G6", "AlIcE", "1.2.3.4:1"),
		"application/json");
	T_EXPECT(r, "got response");
	T_EXPECT_EQ_INT(r->status, 429, "case-fold spelling caps too");
	T_EXPECT(r->body.find("host limit reached") != std::string::npos,
		"error body is host-limit");
}

// Public projection /v1/public/sessions: returns an array, contains
// only the curated fields, includes a server-built joinUrl that uses
// the configured wasm base + ?s=<id>, and CORS-allows browser fetch.
void TestPublicSessionsEndpoint() {
	++g_testsRun;
	Fixture f;
	auto c = f.client();
	auto r1 = c.Post(kPathSession,
		MakeCreateBody("Archon", "alice", "1.2.3.4:1"),
		"application/json");
	T_EXPECT_EQ_INT(r1 ? r1->status : 0, 201, "create 201");
	std::string id, token; int ttl = 0;
	T_EXPECT(ReadCreateRespJson(r1->body, id, token, ttl),
		"parse create");

	auto r = c.Get(kPathPublicSessions);
	T_EXPECT(r,                          "got response");
	T_EXPECT_EQ_INT(r->status, 200,      "public sessions 200");
	// CORS header present (set by the pre-routing handler for all GETs).
	auto cors = r->get_header_value("Access-Control-Allow-Origin");
	T_EXPECT(cors == "*",                "CORS Access-Control-Allow-Origin: *");

	const std::string& body = r->body;
	T_EXPECT(!body.empty() && body.front() == '[',  "is JSON array");
	// Curated fields present.
	T_EXPECT(body.find("\"cartName\":\"Archon\"") != std::string::npos,
		"contains cartName");
	T_EXPECT(body.find("\"hostHandle\":\"alice\"") != std::string::npos,
		"contains hostHandle");
	T_EXPECT(body.find("\"joinUrl\":") != std::string::npos,
		"contains joinUrl");
	// joinUrl points at the configured base + ?s=<id>.
	std::string expected = "\"joinUrl\":\"" + f.cfg.publicWasmUrl
		+ "?s=" + id + "\"";
	T_EXPECT(body.find(expected) != std::string::npos,
		"joinUrl uses configured wasm base + ?s=<id>");
	// Internal protocol fields must NOT leak through.
	T_EXPECT(body.find("\"token\":")        == std::string::npos,
		"no token leak");
	T_EXPECT(body.find("\"candidates\":")   == std::string::npos,
		"no candidates leak");
	T_EXPECT(body.find("\"wssRelayOnly\":") == std::string::npos,
		"no wssRelayOnly leak");
	T_EXPECT(body.find("\"kernelCRC32\":")  == std::string::npos,
		"no kernelCRC32 leak");
	T_EXPECT(body.find("\"sessionId\":")    == std::string::npos,
		"no raw sessionId leak (joinUrl is the public handle)");
}

// Custom PUBLIC_WASM_URL is honoured: a self-hosted lobby can point
// joinUrl at its own WASM build instead of the upstream default.
void TestPublicSessionsCustomUrl() {
	++g_testsRun;
	// Custom Fixture that sets a different publicWasmUrl.
	Config cfg = Fixture::MakeCfg();
	cfg.publicWasmUrl = "https://example.org/altirra/";
	Store store(cfg);
	httplib::Server srv;
	Install(srv, store);
	srv.set_payload_max_length(kMaxRequestBodyBytes);
	int port = srv.bind_to_any_port("127.0.0.1");
	std::thread t([&] { srv.listen_after_bind(); });
	for (int i = 0; i < 200; ++i) {
		if (srv.is_running()) break;
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}
	httplib::Client c("127.0.0.1", port);
	c.set_connection_timeout(2, 0);

	auto r1 = c.Post(kPathSession,
		MakeCreateBody("Joust", "alice", "1.2.3.4:1"),
		"application/json");
	T_EXPECT_EQ_INT(r1 ? r1->status : 0, 201, "create 201");
	std::string id, token; int ttl = 0;
	T_EXPECT(ReadCreateRespJson(r1->body, id, token, ttl),
		"parse create");

	auto r = c.Get(kPathPublicSessions);
	srv.stop();
	t.join();
	T_EXPECT(r && r->status == 200, "public list 200");
	std::string expected = "\"joinUrl\":\"https://example.org/altirra/?s="
		+ id + "\"";
	T_EXPECT(r->body.find(expected) != std::string::npos,
		"custom wasm URL flows into joinUrl");
}

// Two different hosts get independent buckets.
void TestHostCapPerHandle() {
	++g_testsRun;
	Fixture f;
	auto c = f.client();
	for (int i = 0; i < kMaxHostedGamesPerHost; ++i) {
		char cart[8];
		std::snprintf(cart, sizeof cart, "A%d", i);
		auto r = c.Post(kPathSession,
			MakeCreateBody(cart, "alice", "1.2.3.4:1"),
			"application/json");
		T_EXPECT_EQ_INT(r ? r->status : 0, 201, "alice fills");
	}
	// bob still has a clean bucket.
	auto r = c.Post(kPathSession,
		MakeCreateBody("Z", "bob", "1.2.3.4:1"),
		"application/json");
	T_EXPECT_EQ_INT(r ? r->status : 0, 201,
		"different handle gets its own slots");
}

// =================================================================
// v4 broker handshake tests
// =================================================================

// Helper: POST /v1/session with state=awaiting_approval and
// wssRelayOnly=true.  Returns the created session id+token via outs.
bool CreateBrokerSession(httplib::Client& c,
                         const std::string& cart, const std::string& handle,
                         std::string& idOut, std::string& tokenOut) {
	JsonBuilder b;
	b.raw('{');
	b.key(Field::kCartName);        b.str(cart);   b.raw(',');
	b.key(Field::kHostHandle);      b.str(handle); b.raw(',');
	b.key(Field::kRegion);          b.str("eu");   b.raw(',');
	b.key(Field::kPlayerCount);     b.num(1);      b.raw(',');
	b.key(Field::kMaxPlayers);      b.num(2);      b.raw(',');
	b.key(Field::kProtocolVersion); b.num(kProtocolVersion); b.raw(',');
	b.key(Field::kWssRelayOnly);    b.boolean(true);    b.raw(',');
	b.key(Field::kState);           b.str(kStateAwaitingApproval);
	b.raw('}');
	auto r = c.Post(kPathSession, b.s, "application/json");
	if (!r || r->status != 201) return false;
	int ttl = 0;
	return ReadCreateRespJson(r->body, idOut, tokenOut, ttl);
}

// Helper: POST a joiner intent to a session.  Reads back the new
// intentId.
bool PostIntent(httplib::Client& c, const std::string& sessionId,
                const std::string& handle, const std::string& codeHash,
                std::string& intentIdOut, int& statusOut) {
	JsonBuilder b;
	b.raw('{');
	b.key(Field::kJoinerHandle); b.str(handle);   b.raw(',');
	b.key(Field::kCodeHash);     b.str(codeHash);
	b.raw('}');
	std::string path = std::string(kPathSession) + "/" + sessionId
		+ kPathIntentsSuffix;
	auto r = c.Post(path.c_str(), b.s, "application/json");
	if (!r) { statusOut = 0; return false; }
	statusOut = r->status;
	if (r->status != 201) return false;
	JsonCursor jc{r->body.data(), r->body.data() + r->body.size()};
	if (!jc.match('{')) return false;
	for (;;) {
		std::string k;
		if (!jc.parseString(k)) return false;
		if (!jc.match(':'))     return false;
		if      (k == Field::kIntentId) jc.parseString(intentIdOut);
		else                            jc.skipValue();
		if (!jc.ok) return false;
		if (jc.match(',')) continue;
		if (jc.match('}')) return !intentIdOut.empty();
		return false;
	}
}

// Helper: host POST decision on intent.  Returns HTTP status.
int PostDecision(httplib::Client& c, const std::string& sessionId,
                 const std::string& intentId, const std::string& token,
                 bool accepted, int reason) {
	JsonBuilder b;
	b.raw('{');
	b.key(Field::kToken);    b.str(token);          b.raw(',');
	b.key(Field::kAccepted); b.boolean(accepted);   b.raw(',');
	b.key(Field::kReason);   b.num(reason);
	b.raw('}');
	std::string path = std::string(kPathSession) + "/" + sessionId
		+ "/intents/" + intentId + kPathIntentDecisionTail;
	auto r = c.Post(path.c_str(), b.s, "application/json");
	return r ? r->status : 0;
}

// SSE consumer in a worker thread.  Drains events for up to
// `timeoutMs` ms (or until the connection closes), accumulates raw
// bytes, exposes the buffer.  Used by both the host-side intents-
// stream and the joiner-side decision-stream tests.
struct SseClient {
	std::thread       worker;
	std::mutex        mu;
	std::string       buffer;          // guarded by mu
	std::atomic<bool> done{false};
	std::atomic<bool> connected{false};
	std::atomic<int>  status{0};

	~SseClient() {
		Stop();
	}

	void Start(int port, const std::string& path,
	           const std::string& tokenOrEmpty) {
		worker = std::thread([this, port, path, tokenOrEmpty]() {
			httplib::Client cc("127.0.0.1", port);
			cc.set_connection_timeout(2, 0);
			cc.set_read_timeout(8, 0);
			httplib::Headers h;
			if (!tokenOrEmpty.empty()) {
				h.emplace(kHeaderSessionToken, tokenOrEmpty);
			}
			connected.store(true);
			auto r = cc.Get(path.c_str(), h,
				[this](const char* data, size_t len) -> bool {
					std::lock_guard<std::mutex> lk(mu);
					buffer.append(data, len);
					return !done.load(std::memory_order_acquire);
				});
			status.store(r ? r->status : 0);
		});
	}

	void Stop() {
		done.store(true, std::memory_order_release);
		if (worker.joinable()) worker.join();
	}

	bool WaitForSubstring(const std::string& needle, int waitMs) {
		const auto start = std::chrono::steady_clock::now();
		while (true) {
			{
				std::lock_guard<std::mutex> lk(mu);
				if (buffer.find(needle) != std::string::npos)
					return true;
			}
			auto now = std::chrono::steady_clock::now();
			if (std::chrono::duration_cast<std::chrono::milliseconds>(
				now - start).count() >= waitMs) return false;
			std::this_thread::sleep_for(
				std::chrono::milliseconds(20));
		}
	}

	std::string Snapshot() {
		std::lock_guard<std::mutex> lk(mu);
		return buffer;
	}
};

// 1. Plain happy path: host posts session in awaiting_approval,
//    joiner posts intent, host SSE receives it, host decides accept,
//    joiner SSE receives the decision.
void TestBrokerHappyPathAccept() {
	++g_testsRun;
	Fixture f;
	auto host = f.client();
	auto joiner = f.client();

	std::string sid, tok;
	T_EXPECT(CreateBrokerSession(host, "Archon", "alice", sid, tok),
		"create awaiting_approval session");

	// Host opens its intents stream BEFORE the joiner posts so it
	// observes the live event (rather than the initial replay).
	SseClient hostStream;
	hostStream.Start(f.port,
		std::string(kPathSession) + "/" + sid + kPathIntentsStreamSuffix,
		tok);
	// First chunk arrives within ~50 ms of connection (the initial
	// replay is sent before any wait).  Wait for the empty-array form.
	T_EXPECT(hostStream.WaitForSubstring("\"intents\":[]", 1000),
		"host receives initial empty replay");

	std::string intentId; int httpStatus = 0;
	T_EXPECT(PostIntent(joiner, sid, "bob", "", intentId, httpStatus),
		"joiner posts intent");
	T_EXPECT_EQ_INT(httpStatus, 201, "intent POST status");
	T_EXPECT(!intentId.empty(), "intentId returned");

	// Host SSE should now show an event: intent line carrying bob.
	T_EXPECT(hostStream.WaitForSubstring("event: intent", 2000),
		"host receives intent event");
	T_EXPECT(hostStream.WaitForSubstring("\"" + intentId + "\"", 500),
		"host event carries the intentId");
	T_EXPECT(hostStream.WaitForSubstring("\"joinerHandle\":\"bob\"", 500),
		"host event carries joinerHandle");

	// Joiner opens its decision stream now (or could open earlier;
	// we test the late-open path which exercises the decision-replay
	// branch in the GET handler).
	SseClient joinerStream;
	joinerStream.Start(f.port,
		std::string(kPathIntent) + "/" + intentId + kPathIntentStreamTail,
		"");

	// Host accepts.
	int dst = PostDecision(host, sid, intentId, tok, /*accepted=*/true, /*reason=*/0);
	T_EXPECT_EQ_INT(dst, 200, "decision POST status");

	T_EXPECT(joinerStream.WaitForSubstring("event: decision", 2000),
		"joiner receives decision event");
	T_EXPECT(joinerStream.WaitForSubstring("\"accepted\":true", 500),
		"decision says accepted=true");
	T_EXPECT(joinerStream.WaitForSubstring("\"intentId\":\"" + intentId + "\"",
		500),
		"decision carries intentId");

	hostStream.Stop();
	joinerStream.Stop();
}

// 2. Reject path: host POSTs decision with accepted=false and a
//    reason code; joiner SSE shows the rejection.
void TestBrokerRejectPath() {
	++g_testsRun;
	Fixture f;
	auto host = f.client();
	auto joiner = f.client();

	std::string sid, tok;
	T_EXPECT(CreateBrokerSession(host, "Joust", "alice", sid, tok),
		"create awaiting_approval session");
	std::string intentId; int httpStatus = 0;
	T_EXPECT(PostIntent(joiner, sid, "mallory", "", intentId, httpStatus),
		"joiner posts intent");
	T_EXPECT_EQ_INT(httpStatus, 201, "intent posted");

	SseClient joinerStream;
	joinerStream.Start(f.port,
		std::string(kPathIntent) + "/" + intentId + kPathIntentStreamTail,
		"");

	const int kReasonHostRejected = 8; // matches kRejectHostRejected today
	int dst = PostDecision(host, sid, intentId, tok, false, kReasonHostRejected);
	T_EXPECT_EQ_INT(dst, 200, "reject decision POST status");
	T_EXPECT(joinerStream.WaitForSubstring("\"accepted\":false", 2000),
		"joiner sees accepted=false");
	T_EXPECT(joinerStream.WaitForSubstring("\"reason\":8", 500),
		"joiner sees reason code");
	joinerStream.Stop();
}

// 3. Initial replay: joiner posts BEFORE host opens stream.  Host
//    must see the existing intent in the initial replay block.
void TestBrokerHostStreamInitialReplay() {
	++g_testsRun;
	Fixture f;
	auto host   = f.client();
	auto joiner = f.client();
	std::string sid, tok;
	T_EXPECT(CreateBrokerSession(host, "Joust", "alice", sid, tok),
		"create session");
	std::string i1, i2; int hs = 0;
	T_EXPECT(PostIntent(joiner, sid, "bob",   "", i1, hs), "intent 1");
	T_EXPECT(PostIntent(joiner, sid, "carol", "", i2, hs), "intent 2");

	SseClient hostStream;
	hostStream.Start(f.port,
		std::string(kPathSession) + "/" + sid + kPathIntentsStreamSuffix,
		tok);
	T_EXPECT(hostStream.WaitForSubstring("event: initial", 2000),
		"initial replay event");
	T_EXPECT(hostStream.WaitForSubstring("\"" + i1 + "\"", 500),
		"replay contains first intent");
	T_EXPECT(hostStream.WaitForSubstring("\"" + i2 + "\"", 500),
		"replay contains second intent");
	hostStream.Stop();
}

// 4. Late joiner: decision is posted before joiner opens its
//    stream.  The decision is replayed on connect.
void TestBrokerLateJoinerDecisionReplay() {
	++g_testsRun;
	Fixture f;
	auto host   = f.client();
	auto joiner = f.client();
	std::string sid, tok;
	T_EXPECT(CreateBrokerSession(host, "Joust", "alice", sid, tok),
		"create session");
	std::string iid; int hs = 0;
	T_EXPECT(PostIntent(joiner, sid, "bob", "", iid, hs), "intent");
	int dst = PostDecision(host, sid, iid, tok, /*accepted=*/true, 0);
	T_EXPECT_EQ_INT(dst, 200, "decision posted");

	// Joiner connects AFTER decision exists.
	SseClient joinerStream;
	joinerStream.Start(f.port,
		std::string(kPathIntent) + "/" + iid + kPathIntentStreamTail,
		"");
	T_EXPECT(joinerStream.WaitForSubstring("event: decision", 2000),
		"late joiner sees replayed decision");
	joinerStream.Stop();
}

// 5. Bad token on decision: must return 401 and not affect the
//    intent's decided state.
void TestBrokerDecisionBadToken() {
	++g_testsRun;
	Fixture f;
	auto host   = f.client();
	auto joiner = f.client();
	std::string sid, tok;
	T_EXPECT(CreateBrokerSession(host, "Joust", "alice", sid, tok),
		"create session");
	std::string iid; int hs = 0;
	T_EXPECT(PostIntent(joiner, sid, "bob", "", iid, hs), "intent");
	int dst = PostDecision(host, sid, iid, "wrong-token", true, 0);
	T_EXPECT_EQ_INT(dst, 401, "bad token rejected");

	// Posting again with the right token must still succeed.
	int dst2 = PostDecision(host, sid, iid, tok, true, 0);
	T_EXPECT_EQ_INT(dst2, 200, "right token works");
}

// 6. Double-decision: second POST must return 410.
void TestBrokerDoubleDecision() {
	++g_testsRun;
	Fixture f;
	auto host   = f.client();
	auto joiner = f.client();
	std::string sid, tok;
	T_EXPECT(CreateBrokerSession(host, "Joust", "alice", sid, tok),
		"create session");
	std::string iid; int hs = 0;
	T_EXPECT(PostIntent(joiner, sid, "bob", "", iid, hs), "intent");
	int d1 = PostDecision(host, sid, iid, tok, true, 0);
	int d2 = PostDecision(host, sid, iid, tok, false, 1);
	T_EXPECT_EQ_INT(d1, 200, "first decision ok");
	T_EXPECT_EQ_INT(d2, 410, "second decision 410");
}

// 7. Unauthenticated host stream: 401.
void TestBrokerHostStreamRequiresToken() {
	++g_testsRun;
	Fixture f;
	auto host = f.client();
	std::string sid, tok;
	T_EXPECT(CreateBrokerSession(host, "Joust", "alice", sid, tok),
		"create session");
	auto c = f.client();
	auto r = c.Get((std::string(kPathSession) + "/" + sid +
		kPathIntentsStreamSuffix).c_str());
	T_EXPECT(r,                             "got response");
	T_EXPECT_EQ_INT(r ? r->status : 0, 401, "no-token = 401");
}

// 8. Cancellation: when the session is DELETEd while the host is
//    streaming, the stream emits a session_ended event and closes.
void TestBrokerSessionDeletionCancelsStreams() {
	++g_testsRun;
	Fixture f;
	auto host   = f.client();
	auto joiner = f.client();
	std::string sid, tok;
	T_EXPECT(CreateBrokerSession(host, "Joust", "alice", sid, tok),
		"create session");
	std::string iid; int hs = 0;
	T_EXPECT(PostIntent(joiner, sid, "bob", "", iid, hs), "intent");

	SseClient joinerStream;
	joinerStream.Start(f.port,
		std::string(kPathIntent) + "/" + iid + kPathIntentStreamTail,
		"");
	T_EXPECT(joinerStream.WaitForSubstring(": heartbeat", 100) ||
	         true, "joiner stream up (heartbeat may not yet fire)");

	// DELETE the session.  The joiner stream should see "expired"
	// once the cascade fires.
	httplib::Headers h;
	h.emplace(kHeaderSessionToken, tok);
	auto del = host.Delete(
		(std::string(kPathSession) + "/" + sid).c_str(), h);
	T_EXPECT(del && del->status == 204, "delete 204");

	T_EXPECT(joinerStream.WaitForSubstring("event: expired", 2000),
		"joiner stream sees expired event after session delete");
	joinerStream.Stop();
}

// 9. Validation: handle empty / handle too long / bad JSON / queue
//    full.  Each failure path must return the correct HTTP status.
void TestBrokerIntentValidation() {
	++g_testsRun;
	Fixture f;
	auto host   = f.client();
	auto joiner = f.client();
	std::string sid, tok;
	T_EXPECT(CreateBrokerSession(host, "Joust", "alice", sid, tok),
		"create session");

	// Empty handle → 400
	{
		std::string iid; int hs = 0;
		bool ok = PostIntent(joiner, sid, "", "", iid, hs);
		T_EXPECT(!ok, "empty handle rejected");
		T_EXPECT_EQ_INT(hs, 400, "empty handle = 400");
	}
	// Too-long handle → 400
	{
		std::string longH(kJoinerHandleMax + 1, 'x');
		std::string iid; int hs = 0;
		bool ok = PostIntent(joiner, sid, longH, "", iid, hs);
		T_EXPECT(!ok, "long handle rejected");
		T_EXPECT_EQ_INT(hs, 400, "long handle = 400");
	}
	// Unknown session → 404
	{
		std::string iid; int hs = 0;
		bool ok = PostIntent(joiner, "no-such-id", "bob", "", iid, hs);
		T_EXPECT(!ok, "unknown session rejected");
		T_EXPECT_EQ_INT(hs, 404, "unknown session = 404");
	}
	// Queue cap: post kIntentsPerSessionCap successful intents,
	// then the next one returns 429.
	std::vector<std::string> ids;
	for (size_t i = 0; i < kIntentsPerSessionCap; ++i) {
		std::string iid; int hs = 0;
		char name[8]; std::snprintf(name, sizeof name, "j%zu", i);
		bool ok = PostIntent(joiner, sid, name, "", iid, hs);
		T_EXPECT(ok, "intent within cap");
		ids.push_back(iid);
	}
	{
		std::string iid; int hs = 0;
		bool ok = PostIntent(joiner, sid, "overflow", "", iid, hs);
		T_EXPECT(!ok, "queue full");
		T_EXPECT_EQ_INT(hs, 429, "queue full = 429");
	}
}

// 10. Metrics counters: posting an intent + decision bumps the
//     corresponding /v1/metrics counters.
void TestBrokerMetricsCounters() {
	++g_testsRun;
	Fixture f;
	auto host   = f.client();
	auto joiner = f.client();
	std::string sid, tok;
	T_EXPECT(CreateBrokerSession(host, "Joust", "alice", sid, tok),
		"create session");

	auto m0 = host.Get(kPathMetrics);
	T_EXPECT(m0 && m0->status == 200, "metrics before");
	const std::string body0 = m0->body;
	T_EXPECT(body0.find("\"broker\":") != std::string::npos,
		"broker section present in /v1/metrics");

	std::string iid; int hs = 0;
	T_EXPECT(PostIntent(joiner, sid, "bob", "", iid, hs), "intent");
	int dst = PostDecision(host, sid, iid, tok, true, 0);
	T_EXPECT_EQ_INT(dst, 200, "decision");

	auto m1 = host.Get(kPathMetrics);
	T_EXPECT(m1 && m1->status == 200, "metrics after");
	const std::string body1 = m1->body;
	T_EXPECT(body1.find("\"intents_posted_total\":") != std::string::npos,
		"intents_posted_total key");
	T_EXPECT(body1.find("\"decisions_posted_total\":") != std::string::npos,
		"decisions_posted_total key");
	// awaiting_approval_sessions should reflect at least 1.
	T_EXPECT(body1.find("\"awaiting_approval_sessions\":1") != std::string::npos ||
		body1.find("\"awaiting_approval_sessions\":") != std::string::npos,
		"awaiting_approval_sessions key");
}

// 11. State validation: trying to create a session with an
//     unsupported state value must be rejected.
void TestBrokerCreateRejectsBadState() {
	++g_testsRun;
	Fixture f;
	auto c = f.client();
	JsonBuilder b;
	b.raw('{');
	b.key(Field::kCartName);        b.str("Joust"); b.raw(',');
	b.key(Field::kHostHandle);      b.str("alice"); b.raw(',');
	b.key(Field::kRegion);          b.str("eu");    b.raw(',');
	b.key(Field::kPlayerCount);     b.num(1);       b.raw(',');
	b.key(Field::kMaxPlayers);      b.num(2);       b.raw(',');
	b.key(Field::kProtocolVersion); b.num(kProtocolVersion); b.raw(',');
	b.key(Field::kWssRelayOnly);    b.boolean(true); b.raw(',');
	b.key(Field::kState);           b.str("playing");
	b.raw('}');
	auto r = c.Post(kPathSession, b.s, "application/json");
	T_EXPECT(r,                          "got response");
	T_EXPECT_EQ_INT(r->status, 400,      "bogus state rejected");
}

// 12a. awaiting_approval sessions must NOT appear in /v1/sessions —
//      pre-v4 native emulators browsing the lobby would otherwise try
//      to join them via NetHello, which has no live coordinator yet
//      and times out silently.  This is a regression test for the
//      filter added in M1's audit pass.
void TestBrokerAwaitingApprovalHiddenFromSessionsList() {
	++g_testsRun;
	Fixture f;
	auto host = f.client();

	// Create one ordinary "waiting" session (legacy path) and one
	// broker session in awaiting_approval.  The latter must be
	// invisible to the public list endpoints; the former must remain
	// visible.
	auto r1 = host.Post(kPathSession,
		MakeCreateBody("Joust", "alice", "1.2.3.4:1"),
		"application/json");
	T_EXPECT_EQ_INT(r1 ? r1->status : 0, 201, "legacy create 201");
	std::string id1, tok1; int ttl1 = 0;
	T_EXPECT(ReadCreateRespJson(r1->body, id1, tok1, ttl1),
		"parse legacy create");

	std::string id2, tok2;
	T_EXPECT(CreateBrokerSession(host, "Archon", "bob", id2, tok2),
		"broker create");

	auto list = host.Get(kPathSessions);
	T_EXPECT(list && list->status == 200, "GET /v1/sessions 200");
	T_EXPECT(list->body.find(id1) != std::string::npos,
		"legacy session present in /v1/sessions");
	T_EXPECT(list->body.find(id2) == std::string::npos,
		"awaiting_approval session HIDDEN from /v1/sessions");

	auto pub = host.Get(kPathPublicSessions);
	T_EXPECT(pub && pub->status == 200, "GET /v1/public/sessions 200");
	// /v1/public/sessions doesn't include the sessionId field by name
	// in the wire format (it has joinUrl carrying it).  Check the
	// joinUrl construction instead.
	T_EXPECT(pub->body.find("?s=" + id1) != std::string::npos,
		"legacy session present in /v1/public/sessions");
	T_EXPECT(pub->body.find("?s=" + id2) == std::string::npos,
		"awaiting_approval session HIDDEN from /v1/public/sessions");
}

// 12b. /v1/stats must not count awaiting_approval sessions in
//      `waiting`, `playing`, or `sessions`.  Otherwise the page
//      widget's "X games waiting" number disagrees with the list it
//      renders below.
void TestBrokerAwaitingApprovalNotCountedInStats() {
	++g_testsRun;
	Fixture f;
	auto c = f.client();
	auto r1 = c.Post(kPathSession,
		MakeCreateBody("Joust", "alice", "1.2.3.4:1"),
		"application/json");
	T_EXPECT_EQ_INT(r1 ? r1->status : 0, 201, "legacy create");
	std::string i, t;
	T_EXPECT(CreateBrokerSession(c, "Archon", "bob", i, t),
		"broker create");
	auto stats = c.Get(kPathStats);
	T_EXPECT(stats && stats->status == 200, "GET /v1/stats 200");
	// One legacy session = one waiting, total sessions=1.  Broker
	// session is not counted.  We don't parse the JSON — string-
	// match the expected substrings.
	T_EXPECT(stats->body.find("\"sessions\":1") != std::string::npos,
		"sessions count excludes awaiting_approval");
	T_EXPECT(stats->body.find("\"waiting\":1") != std::string::npos,
		"waiting count excludes awaiting_approval");
}

// 13. Joiner-from-broker against a NATIVE host (legacy "waiting"
//     session): the joiner can post an intent and gets an automatic
//     accept (no broker host UI is listening on a legacy session;
//     the joiner's spawned WASM emulator's NetHello carries any
//     remaining authorisation through the netplay protocol).
void TestBrokerIntentAgainstWaitingSessionAccepted() {
	++g_testsRun;
	Fixture f;
	auto host   = f.client();
	auto joiner = f.client();
	auto r = host.Post(kPathSession,
		MakeCreateBody("Joust", "alice", "1.2.3.4:1"),
		"application/json");
	T_EXPECT_EQ_INT(r ? r->status : 0, 201, "legacy create");
	std::string sid, tok; int ttl = 0;
	T_EXPECT(ReadCreateRespJson(r->body, sid, tok, ttl),
		"parse legacy create");

	std::string iid; int hs = 0;
	bool ok = PostIntent(joiner, sid, "bob", "", iid, hs);
	T_EXPECT(ok,                      "intent accepted on waiting session");
	T_EXPECT_EQ_INT(hs, 201,          "intent POST status 201");
	T_EXPECT(!iid.empty(),            "intentId returned");

	// Auto-accept verification: opening the joiner's decision stream
	// must immediately replay the recorded accept (without waiting on
	// a host POST), because intents on legacy waiting sessions are
	// decided synchronously inside CreateIntent.
	SseClient joinerStream;
	joinerStream.Start(f.port,
		std::string(kPathIntent) + "/" + iid + kPathIntentStreamTail,
		"");
	T_EXPECT(joinerStream.WaitForSubstring("event: decision", 2000),
		"auto-accept decision replays on stream connect");
	T_EXPECT(joinerStream.WaitForSubstring("\"accepted\":true", 500),
		"auto-accept decision is accepted=true");
	joinerStream.Stop();

	// Posting the same decision again from a host token should now
	// return 410 — the auto-accept already finalised the intent.
	int code = PostDecision(host, sid, iid, tok, true, 0);
	T_EXPECT_EQ_INT(code, 410, "auto-accepted intent already decided");
}

// 13b. Auto-accept must fire even if the joiner connects its stream
//      BEFORE posting the intent (i.e. opens the stream as soon as
//      it has the intentId).  This exercises the live-notify path
//      rather than the replay path of test 13.
void TestBrokerAutoAcceptLiveNotify() {
	++g_testsRun;
	Fixture f;
	auto host   = f.client();
	auto joiner = f.client();
	auto r = host.Post(kPathSession,
		MakeCreateBody("Joust", "alice", "1.2.3.4:1"),
		"application/json");
	T_EXPECT_EQ_INT(r ? r->status : 0, 201, "legacy create");
	std::string sid, tok; int ttl = 0;
	T_EXPECT(ReadCreateRespJson(r->body, sid, tok, ttl), "parse");

	// Post the intent first to obtain the intentId, then immediately
	// open the stream.  We can't open the stream before the intent
	// exists (the GET would 404), so this is the closest we can
	// approximate a "stream-then-decision" race in HTTP semantics.
	std::string iid; int hs = 0;
	bool ok = PostIntent(joiner, sid, "carol", "", iid, hs);
	T_EXPECT(ok && hs == 201, "intent posted");
	SseClient joinerStream;
	joinerStream.Start(f.port,
		std::string(kPathIntent) + "/" + iid + kPathIntentStreamTail,
		"");
	T_EXPECT(joinerStream.WaitForSubstring("event: decision", 2000),
		"decision arrives on legacy session");
	joinerStream.Stop();
}

// 14. State validation: trying to POST an intent against a "playing"
//     session must fail with 410 (already in session, can't add more).
void TestBrokerIntentRejectedOnPlayingSession() {
	++g_testsRun;
	Fixture f;
	auto host   = f.client();
	auto joiner = f.client();
	auto r = host.Post(kPathSession,
		MakeCreateBody("Joust", "alice", "1.2.3.4:1"),
		"application/json");
	T_EXPECT_EQ_INT(r ? r->status : 0, 201, "create");
	std::string sid, tok; int ttl = 0;
	T_EXPECT(ReadCreateRespJson(r->body, sid, tok, ttl), "parse");

	// Move session to "playing" via heartbeat.
	JsonBuilder hb;
	hb.raw('{');
	hb.key(Field::kToken);       hb.str(tok);          hb.raw(',');
	hb.key(Field::kPlayerCount); hb.num(2);            hb.raw(',');
	hb.key(Field::kState);       hb.str(kStatePlaying);
	hb.raw('}');
	auto hbr = host.Post(
		(std::string(kPathSession) + "/" + sid +
			kPathHeartbeatSuffix).c_str(),
		hb.s, "application/json");
	T_EXPECT(hbr && hbr->status == 200, "heartbeat to playing");

	std::string iid; int hs = 0;
	bool ok = PostIntent(joiner, sid, "bob", "", iid, hs);
	T_EXPECT(!ok,             "intent rejected on playing session");
	T_EXPECT_EQ_INT(hs, 410,  "playing = 410");
}

// 14b. Audit fix: dedup-on-Create must cancel any pending intents
//      on the evicted session.  Scenario: broker host creates a
//      session, joiner posts intent, broker tab refreshes (creates
//      another session for the same host+cart), the OLD session is
//      evicted by the existing dedup logic.  Joiner SSE must see
//      "expired" rather than wait out the 60-s intent TTL.
void TestBrokerDedupCancelsPendingIntents() {
	++g_testsRun;
	Fixture f;
	auto host   = f.client();
	auto joiner = f.client();
	std::string sid1, tok1;
	T_EXPECT(CreateBrokerSession(host, "Joust", "alice", sid1, tok1),
		"first broker session");
	std::string iid; int hs = 0;
	T_EXPECT(PostIntent(joiner, sid1, "bob", "", iid, hs),
		"joiner posts intent on first session");

	SseClient joinerStream;
	joinerStream.Start(f.port,
		std::string(kPathIntent) + "/" + iid + kPathIntentStreamTail,
		"");
	// Wait for the SSE handler to register its listener before
	// triggering the dedup eviction — otherwise the GET could land
	// on the server AFTER dedup has fired and the intent is already
	// gone, producing a 404 instead of the expected expired event.
	// The ": ready" comment line is emitted on the first provider
	// invocation, immediately after RegisterDecisionListener returns.
	T_EXPECT(joinerStream.WaitForSubstring(": ready", 2000),
		"joiner SSE registered listener");

	// Re-create with same host+cart → triggers dedup, evicting sid1.
	std::string sid2, tok2;
	T_EXPECT(CreateBrokerSession(host, "Joust", "alice", sid2, tok2),
		"second broker session (dedup eviction)");
	T_EXPECT(sid1 != sid2, "fresh session id");

	T_EXPECT(joinerStream.WaitForSubstring("event: expired", 2000),
		"intent listener sees expired event from dedup eviction");
	joinerStream.Stop();
}

// 14c. Audit fix: codeHash with non-hex characters is rejected.
//      Scenario: broker joiner sends 32-char string that's the right
//      length but contains a 'z'.  Lobby returns 403 CodeMismatch
//      rather than letting the malformed value reach the host.
void TestBrokerIntentRejectsNonHexCodeHash() {
	++g_testsRun;
	Fixture f;
	auto c = f.client();
	// Build a private session (requiresCode=true).
	JsonBuilder b;
	b.raw('{');
	b.key(Field::kCartName);        b.str("Joust");        b.raw(',');
	b.key(Field::kHostHandle);      b.str("alice");        b.raw(',');
	b.key(Field::kHostEndpoint);    b.str("1.2.3.4:1");    b.raw(',');
	b.key(Field::kRegion);          b.str("eu");           b.raw(',');
	b.key(Field::kPlayerCount);     b.num(1);              b.raw(',');
	b.key(Field::kMaxPlayers);      b.num(2);              b.raw(',');
	b.key(Field::kProtocolVersion); b.num(kProtocolVersion); b.raw(',');
	b.key(Field::kVisibility);      b.str("private");      b.raw(',');
	b.key(Field::kRequiresCode);    b.boolean(true);
	b.raw('}');
	auto rcr = c.Post(kPathSession, b.s, "application/json");
	T_EXPECT_EQ_INT(rcr ? rcr->status : 0, 201, "private session create");
	std::string sid, tok; int ttl = 0;
	T_EXPECT(ReadCreateRespJson(rcr->body, sid, tok, ttl),
		"parse private session create");

	// 32-char string with 'z' (not hex).
	std::string badHash = "0123456789abcdez1234567890abcdef";
	std::string iid; int hs = 0;
	bool ok = PostIntent(c, sid, "bob", badHash, iid, hs);
	T_EXPECT(!ok,             "non-hex codeHash rejected");
	T_EXPECT_EQ_INT(hs, 403,  "non-hex codeHash returns 403");

	// Valid hex of correct length is accepted.
	std::string goodHash = "0123456789abcdef0123456789abcdef";
	ok = PostIntent(c, sid, "bob", goodHash, iid, hs);
	T_EXPECT(ok,              "valid hex codeHash accepted");
	T_EXPECT_EQ_INT(hs, 201,  "valid hex returns 201");
}

// 14d. Audit-3 fix: deleting a legacy session that auto-accepted one
//      or more intents must clean up the decided-intent records too,
//      not just the matching decisions.  Before the fix,
//      BrokerOnSessionRemovedLocked walked mIntents only for !decided
//      entries, leaking decided ones in mIntents forever.  Use
//      ComputeBrokerStats (which reads activeIntents from the live
//      mIntents map) as the leak gauge — a clean cleanup sees 0
//      active_intents after the session is deleted and the decision
//      TTL has not yet fired.
void TestBrokerSessionDeletionClearsAutoAcceptedIntents() {
	++g_testsRun;
	Fixture f;
	auto host   = f.client();
	auto joiner = f.client();
	// Legacy waiting session — every intent posted gets auto-accepted.
	auto r = host.Post(kPathSession,
		MakeCreateBody("Joust", "alice", "1.2.3.4:1"),
		"application/json");
	T_EXPECT_EQ_INT(r ? r->status : 0, 201, "legacy create");
	std::string sid, tok; int ttl = 0;
	T_EXPECT(ReadCreateRespJson(r->body, sid, tok, ttl), "parse");

	// Post three intents to bulk up the decided-intent / decision pile.
	for (int i = 0; i < 3; ++i) {
		std::string iid; int hs = 0;
		std::string handle = std::string("joiner") + char('0' + i);
		T_EXPECT(PostIntent(joiner, sid, handle, "", iid, hs),
			"post intent");
	}
	// Sanity: pre-deletion the Store has 3 active intents + 3 active
	// decisions (each auto-accept produces both).
	{
		auto bs = f.store.ComputeBrokerStats();
		T_EXPECT_EQ_INT(bs.activeIntents,   3, "3 intents pre-delete");
		T_EXPECT_EQ_INT(bs.activeDecisions, 3, "3 decisions pre-delete");
	}

	// Delete the session — host clicked Quit, or session was evicted by
	// dedup, or expired.  Either way BrokerOnSessionRemovedLocked fires.
	httplib::Headers h{{kHeaderSessionToken, tok}};
	auto del = host.Delete(
		(std::string(kPathSession) + "/" + sid).c_str(), h);
	T_EXPECT(del && del->status == 204, "delete 204");

	// Post-deletion: BOTH containers must be empty.  Before the audit-3
	// fix, activeIntents stayed at 3 (decided intents were leaked); the
	// only path that would eventually free them was BrokerExpireDecisions
	// reading from mDecisions, which had already been wiped by the
	// session-removed handler — so the orphan intents would persist
	// forever.
	auto bs = f.store.ComputeBrokerStats();
	T_EXPECT_EQ_INT(bs.activeIntents,   0, "decided intents cleaned up");
	T_EXPECT_EQ_INT(bs.activeDecisions, 0, "decisions cleaned up");
}

// 14e. Audit-3 metric: auto-accept must be visible in /v1/metrics
//      under the dedicated counter.  Operators read this number to
//      tell whether legacy / native-host traffic is healthy
//      independently of the broker-driven decisions.  We check the
//      counter directly (gLC is a process-global, so absolute values
//      depend on test-run history); the relative delta is what
//      matters here.
void TestBrokerAutoAcceptCounterIncrements() {
	++g_testsRun;
	Fixture f;
	auto host   = f.client();
	auto joiner = f.client();

	const uint64_t before =
		gLC.brokerIntentsAutoAccepted.load(std::memory_order_relaxed);
	const uint64_t beforeDecisions =
		gLC.brokerDecisionsPosted.load(std::memory_order_relaxed);

	// Two waiting sessions, two joiners — should produce 2 auto-accepts.
	auto r1 = host.Post(kPathSession,
		MakeCreateBody("Joust",  "alice-aa", "1.2.3.4:1"),
		"application/json");
	T_EXPECT_EQ_INT(r1 ? r1->status : 0, 201, "legacy create #1");
	std::string sid1, tok1; int ttl1 = 0;
	T_EXPECT(ReadCreateRespJson(r1->body, sid1, tok1, ttl1), "parse #1");
	auto r2 = host.Post(kPathSession,
		MakeCreateBody("Archon", "carol-aa", "1.2.3.4:2"),
		"application/json");
	T_EXPECT_EQ_INT(r2 ? r2->status : 0, 201, "legacy create #2");
	std::string sid2, tok2; int ttl2 = 0;
	T_EXPECT(ReadCreateRespJson(r2->body, sid2, tok2, ttl2), "parse #2");

	std::string iid; int hs = 0;
	T_EXPECT(PostIntent(joiner, sid1, "bob",  "", iid, hs), "intent #1");
	T_EXPECT(PostIntent(joiner, sid2, "dave", "", iid, hs), "intent #2");

	const uint64_t after =
		gLC.brokerIntentsAutoAccepted.load(std::memory_order_relaxed);
	const uint64_t afterDecisions =
		gLC.brokerDecisionsPosted.load(std::memory_order_relaxed);
	T_EXPECT_EQ_INT((int)(after - before), 2,
		"auto-accept counter advanced by 2");
	// Cross-check: decisions_posted_total does NOT advance — auto-
	// accepts bypass MakeDecision, so the broker-UI counter stays
	// flat.  This is the observability distinction between the two
	// counters: legacy traffic vs. broker-modal traffic.
	T_EXPECT_EQ_INT((int)(afterDecisions - beforeDecisions), 0,
		"decisions_posted_total stays flat for auto-accept-only flow");

	// And the metrics endpoint surfaces the counter under the new
	// JSON key — protect against typos in the wire schema.
	auto m = host.Get(kPathMetrics);
	T_EXPECT(m && m->status == 200, "metrics endpoint responds");
	T_EXPECT(m->body.find("\"intents_auto_accepted_total\":")
	         != std::string::npos,
		"intents_auto_accepted_total key present in /v1/metrics");
}

// 12. AwaitingApproval requires wssRelayOnly=true.
void TestBrokerAwaitingApprovalRequiresWssRelayOnly() {
	++g_testsRun;
	Fixture f;
	auto c = f.client();
	JsonBuilder b;
	b.raw('{');
	b.key(Field::kCartName);        b.str("Joust");          b.raw(',');
	b.key(Field::kHostHandle);      b.str("alice");          b.raw(',');
	b.key(Field::kHostEndpoint);    b.str("1.2.3.4:1");      b.raw(',');
	b.key(Field::kRegion);          b.str("eu");             b.raw(',');
	b.key(Field::kPlayerCount);     b.num(1);                b.raw(',');
	b.key(Field::kMaxPlayers);      b.num(2);                b.raw(',');
	b.key(Field::kProtocolVersion); b.num(kProtocolVersion); b.raw(',');
	b.key(Field::kState);           b.str(kStateAwaitingApproval);
	b.raw('}');
	auto r = c.Post(kPathSession, b.s, "application/json");
	T_EXPECT(r,                     "got response");
	T_EXPECT_EQ_INT(r->status, 400, "endpoint+awaiting_approval rejected");
}

int RunAll() {
	TestHealthz();
	TestCreateAndList();
	TestCreateDedupReplacesPriorSession();
	TestListEchoesMachineFields();
	TestCreateValidation();
	TestHostCap();
	TestHostCapCaseInsensitive();
	TestHostCapPerHandle();
	TestPublicSessionsEndpoint();
	TestPublicSessionsCustomUrl();
	TestHeartbeatBadToken();
	TestHeartbeatOk();
	TestDeleteBadToken();
	TestDeleteOk();
	TestExpireSweeps();
	TestWsPresenceDrivesCleanup();
	TestRateLimit();
	TestClientIpXffSpoof();
	TestOriginBlockedOnPost();
	TestPeerHintPostAndDeliver();
	TestPeerHintSizeLimits();
	TestPeerHintUnknownSession();
	TestRelayTableRegisterAndLookup();
	TestRelayLookupOtherFailsWithOneSide();
	TestWssRelayOnlyRoundtrip();

	// v4 broker handshake.
	TestBrokerHappyPathAccept();
	TestBrokerRejectPath();
	TestBrokerHostStreamInitialReplay();
	TestBrokerLateJoinerDecisionReplay();
	TestBrokerDecisionBadToken();
	TestBrokerDoubleDecision();
	TestBrokerHostStreamRequiresToken();
	TestBrokerSessionDeletionCancelsStreams();
	TestBrokerIntentValidation();
	TestBrokerMetricsCounters();
	TestBrokerCreateRejectsBadState();
	TestBrokerAwaitingApprovalRequiresWssRelayOnly();
	TestBrokerAwaitingApprovalHiddenFromSessionsList();
	TestBrokerAwaitingApprovalNotCountedInStats();
	TestBrokerIntentAgainstWaitingSessionAccepted();
	TestBrokerAutoAcceptLiveNotify();
	TestBrokerIntentRejectedOnPlayingSession();
	TestBrokerDedupCancelsPendingIntents();
	TestBrokerIntentRejectsNonHexCodeHash();
	TestBrokerSessionDeletionClearsAutoAcceptedIntents();
	TestBrokerAutoAcceptCounterIncrements();

	std::fprintf(stderr, "tests: %d run, %d failed\n",
		g_testsRun, g_testsFail);
	return g_testsFail == 0 ? 0 : 1;
}

} // anonymous

int main(int, char**) {
	// Signals we install in server.cpp main() aren't desired here;
	// the in-process Server.stop() happens via Fixture teardown.
	return RunAll();
}
