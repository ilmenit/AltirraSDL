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

// The list endpoint must echo every schema field the host supplied on
// create — kernelCRC32 / basicCRC32 / hardwareMode — and must also
// include the keys (as empty strings) when the host omitted them.
// The "?" hardwareMode bug in the desktop browser was caused by an
// older server silently dropping these keys on GET /v1/sessions, so
// this test locks the schema contract in place.
void TestListEchoesFirmwareFields() {
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
	b.key(Field::kHardwareMode);    b.str("800XL");
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
	T_EXPECT(body2.find("\"hardwareMode\":\"\"") != std::string::npos,
		"list body contains empty hardwareMode key");
	T_EXPECT(body2.find("\"cartArtHash\":\"\"")  != std::string::npos,
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
	Session out = f.store.Create(seed);
	T_EXPECT(!out.id.empty(), "seeded session");

	int n = f.store.ExpireOnce(NowMs() +
		(int64_t)(f.cfg.ttlSeconds + 1) * 1000);
	T_EXPECT_EQ_INT(n, 1, "one session expired");
	Session probe;
	T_EXPECT(!f.store.Get(out.id, probe), "session removed");
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

int RunAll() {
	TestHealthz();
	TestCreateAndList();
	TestListEchoesFirmwareFields();
	TestCreateValidation();
	TestHeartbeatBadToken();
	TestHeartbeatOk();
	TestDeleteBadToken();
	TestDeleteOk();
	TestExpireSweeps();
	TestRateLimit();
	TestOriginBlockedOnPost();

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
