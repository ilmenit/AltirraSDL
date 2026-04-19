// Altirra SDL3 netplay - lobby client selftest
//
// End-to-end integration test against the production lobby server
// http://92.5.13.40:8080.  Creates a private session, lists and finds
// it, heartbeats, deletes, and verifies the delete stuck.  Uses a
// unique cart name with the PID + wallclock so concurrent runs don't
// stomp each other.
//
// Build (shim stdafx.h):
//   mkdir -p /tmp/netplay_selftest && : > /tmp/netplay_selftest/stdafx.h
//   g++ -std=c++17 -Wall -Wextra -pthread -I /tmp/netplay_selftest -I src/AltirraSDL/source/netplay src/AltirraSDL/source/netplay/http_minimal.cpp src/AltirraSDL/source/netplay/lobby_client.cpp src/AltirraSDL/source/netplay/lobby_client_selftest.cpp -o /tmp/netplay_selftest/lobby_run
//   /tmp/netplay_selftest/lobby_run
//
// Set LOBBY_HOST / LOBBY_PORT env to point at a different server (e.g.
// a local instance of altirra-sdl-lobby).  Defaults to the production
// deployment.  Set LOBBY_SKIP=1 to skip network tests and run only the
// JSON parser smoke tests.

#include "lobby_client.h"
#include "http_minimal.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>

using namespace ATNetplay;

static int fails = 0;

#define CHECK(cond) do { \
		if (!(cond)) { \
			std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
			++fails; \
		} \
	} while (0)

#define CHECK_MSG(cond, msg) do { \
		if (!(cond)) { \
			std::fprintf(stderr, "FAIL %s:%d: %s (%s)\n", \
				__FILE__, __LINE__, #cond, (msg)); \
			++fails; \
		} \
	} while (0)

// ---------------------------------------------------------------------
// JSON parser smoke tests (don't need the network).
// ---------------------------------------------------------------------
static void testUrlPercentEncode() {
	std::string out;
	UrlPercentEncode("abc 123/=", 9, out);
	CHECK(out == "abc%20123%2F%3D");

	out.clear();
	UrlPercentEncode("ABCxyz0-9_.~", 12, out);
	CHECK(out == "ABCxyz0-9_.~");
}

static void testListRoundTripDoesntCrash() {
	// Exercise List() against a bogus host; we expect a network error
	// (not a crash) and a non-empty LastError().
	LobbyEndpoint ep;
	ep.host = "127.0.0.1";
	ep.port = 1;         // nothing listens here
	ep.timeoutMs = 500;

	LobbyClient c(ep);
	std::vector<LobbySession> xs;
	bool ok = c.List(xs);
	CHECK(!ok);
	CHECK(c.LastError()[0] != '\0');
}

// ---------------------------------------------------------------------
// Live lobby integration.
// ---------------------------------------------------------------------
static std::string envOr(const char* k, const char* d) {
	const char* v = std::getenv(k);
	return v && *v ? v : d;
}

static void testLiveLobbyRoundTrip() {
	const char* skip = std::getenv("LOBBY_SKIP");
	if (skip && *skip) {
		std::printf("lobby_selftest: live tests skipped (LOBBY_SKIP set)\n");
		return;
	}

	LobbyEndpoint ep;
	ep.host = envOr("LOBBY_HOST", "92.5.13.40");
	ep.port = (uint16_t)std::atoi(envOr("LOBBY_PORT", "8080").c_str());
	ep.timeoutMs = 8000;

	LobbyClient c(ep);

	// Unique handle per run so concurrent invocations don't collide.
	pid_t pid = getpid();
	uint64_t ts = (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::steady_clock::now().time_since_epoch()).count();
	char tag[64];
	std::snprintf(tag, sizeof tag, "selftest-%d-%llu",
		(int)pid, (unsigned long long)ts);

	LobbyCreateRequest cr;
	cr.cartName        = std::string("SelfTest-") + tag;
	cr.hostHandle      = "tester";
	cr.hostEndpoint    = "127.0.0.1:26101";
	cr.region          = "selftest";
	cr.playerCount     = 1;
	cr.maxPlayers      = 2;
	cr.protocolVersion = 2;
	cr.visibility      = "private";
	cr.requiresCode    = true;
	cr.cartArtHash     = "deadbeefcafef00d";

	LobbyCreateResponse cresp;
	bool ok = c.Create(cr, cresp);
	if (!ok) {
		std::fprintf(stderr, "Create() failed: %s — is %s:%u reachable?\n",
			c.LastError(), ep.host.c_str(), (unsigned)ep.port);
		++fails;
		return;
	}
	CHECK(!cresp.sessionId.empty());
	CHECK(!cresp.token.empty());
	CHECK(cresp.ttlSeconds >= 30);

	std::printf("created session %s (ttl=%ds)\n",
		cresp.sessionId.c_str(), cresp.ttlSeconds);

	// List should include our session.
	std::vector<LobbySession> items;
	ok = c.List(items);
	CHECK_MSG(ok, c.LastError());

	bool found = false;
	for (const auto& s : items) {
		if (s.sessionId == cresp.sessionId) {
			found = true;
			CHECK(s.cartName == cr.cartName);
			CHECK(s.visibility == "private");
			CHECK(s.requiresCode == true);
			CHECK(s.cartArtHash == cr.cartArtHash);
			CHECK(s.protocolVersion == 2);
			break;
		}
	}
	CHECK_MSG(found, "created session missing from List()");

	// Heartbeat — should succeed.
	ok = c.Heartbeat(cresp.sessionId, cresp.token, 2);
	CHECK_MSG(ok, c.LastError());

	// Heartbeat with wrong token — should fail.
	ok = c.Heartbeat(cresp.sessionId, "deadbeef", 2);
	CHECK(!ok);

	// Delete with correct token.
	ok = c.Delete(cresp.sessionId, cresp.token);
	CHECK_MSG(ok, c.LastError());

	// Second delete returns 404 which we treat as success.
	ok = c.Delete(cresp.sessionId, cresp.token);
	CHECK_MSG(ok, c.LastError());

	// Confirm it's gone from the list.
	ok = c.List(items);
	CHECK(ok);
	for (const auto& s : items) {
		CHECK(s.sessionId != cresp.sessionId);
	}
}

int main() {
	testUrlPercentEncode();
	testListRoundTripDoesntCrash();
	testLiveLobbyRoundTrip();

	if (fails == 0) {
		std::printf("netplay lobby selftest: OK\n");
		return 0;
	}
	std::fprintf(stderr, "netplay lobby selftest: %d FAILURES\n", fails);
	return 1;
}
