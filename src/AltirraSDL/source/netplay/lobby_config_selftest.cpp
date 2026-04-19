// Altirra SDL3 netplay - lobby_config + lan_discovery selftest
//
// Offline only: the encode/decode round-trip, INI parser, LAN socket
// bind+broadcast via loopback.  No external network required.
//
// Build (shim stdafx.h):
//   mkdir -p /tmp/netplay_selftest && : > /tmp/netplay_selftest/stdafx.h
//   g++ -std=c++17 -Wall -Wextra -pthread -I /tmp/netplay_selftest -I src/AltirraSDL/source/netplay src/AltirraSDL/source/netplay/lobby_config.cpp src/AltirraSDL/source/netplay/lan_discovery.cpp src/AltirraSDL/source/netplay/lobby_config_selftest.cpp -o /tmp/netplay_selftest/lobby_cfg_run
//   /tmp/netplay_selftest/lobby_cfg_run

#include "lobby_config.h"
#include "lan_discovery.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

using namespace ATNetplay;

static int fails = 0;

#define CHECK(cond) do { \
		if (!(cond)) { \
			std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
			++fails; \
		} \
	} while (0)

static void testDefaultIni() {
	std::vector<LobbyEntry> xs;
	GetDefaultLobbies(xs);
	CHECK(xs.size() == 2);
	if (xs.size() >= 2) {
		CHECK(xs[0].section == "official");
		CHECK(xs[0].kind == LobbyKind::Http);
		CHECK(xs[0].url == "http://92.5.13.40:8080");
		CHECK(xs[0].enabled);

		CHECK(xs[1].section == "lan");
		CHECK(xs[1].kind == LobbyKind::LanBroadcast);
		CHECK(xs[1].port == 26101);
	}
}

static void testIniParseEdgeCases() {
	const char* txt =
		"; leading comment\n"
		"[a]\n"
		"name = Alice\n"
		"url = http://a:1\n"
		"\n"
		"[b]  ; inline comment after section\n"
		"name=Bob\n"
		"url =http://b:2\n"
		"enabled = no\n"
		"[broken]\n"
		"# no url here — should be dropped\n"
		"[lan1]\n"
		"transport = udp-broadcast\n"
		"port = 26101\n"
		"[lan-bad]\n"
		"transport = udp-broadcast\n"
		"; no port — dropped\n"
		"[c]\n"
		"unknown_key = ignored\n"
		"url = http://c:3\n";

	std::vector<LobbyEntry> xs;
	std::vector<std::string> warns;
	ParseLobbyIni(txt, std::strlen(txt), xs, &warns);
	CHECK(xs.size() == 4);  // a, b, lan1, c — broken + lan-bad dropped
	CHECK(warns.size() >= 2);

	if (xs.size() == 4) {
		CHECK(xs[0].name == "Alice");
		CHECK(xs[1].enabled == false);
		CHECK(xs[2].kind == LobbyKind::LanBroadcast);
		CHECK(xs[2].port == 26101);
		CHECK(xs[3].url == "http://c:3");
	}
}

static void testLanAdRoundTrip() {
	LanAd a;
	a.protocolVersion = 2;
	a.hostPort = 26201;
	a.visibility = 1;
	a.requiresCode = 1;
	a.playerCount = 1;
	a.maxPlayers = 2;
	a.osRomHash = 0xDEADBEEF01234567ull;
	a.basicRomHash = 0xCAFEBABE11223344ull;
	a.settingsHash = 0x1234567890ABCDEFull;
	a.cartName = "Joust.atr";
	a.hostHandle = "alice";
	a.cartArtHash = "abcd1234";

	uint8_t buf[kLanAdSize];
	size_t n = EncodeLanAd(a, buf, sizeof buf);
	CHECK(n == kLanAdSize);

	LanAd b;
	CHECK(DecodeLanAd(buf, n, b));
	CHECK(b.protocolVersion == a.protocolVersion);
	CHECK(b.hostPort == a.hostPort);
	CHECK(b.visibility == a.visibility);
	CHECK(b.requiresCode == a.requiresCode);
	CHECK(b.playerCount == a.playerCount);
	CHECK(b.maxPlayers == a.maxPlayers);
	CHECK(b.osRomHash == a.osRomHash);
	CHECK(b.basicRomHash == a.basicRomHash);
	CHECK(b.settingsHash == a.settingsHash);
	CHECK(b.cartName == a.cartName);
	CHECK(b.hostHandle == a.hostHandle);
	CHECK(b.cartArtHash == a.cartArtHash);
}

static void testLanAdRejectsZeroPort() {
	LanAd a;
	uint8_t buf[kLanAdSize];
	EncodeLanAd(a, buf, sizeof buf);
	LanAd b;
	CHECK(!DecodeLanAd(buf, sizeof buf, b));  // hostPort==0 rejected
}

static void testLanAdRejectsBadMagic() {
	uint8_t buf[kLanAdSize] = {0};
	LanAd b;
	CHECK(!DecodeLanAd(buf, sizeof buf, b));
}

static void testLanDiscoveryLoopback() {
	// Bind two instances on the same port via SO_REUSEADDR and make
	// sure the broadcast from one reaches the other on loopback too.
	// On some stacks loopback won't receive broadcast; in that case the
	// receiver times out and we skip.
	LanDiscovery host, peer;
	bool a = host.Begin(26999);
	bool b = peer.Begin(26999);
	if (!a || !b) {
		std::printf("LAN loopback skipped (bind failed): %s / %s\n",
			host.LastError(), peer.LastError());
		return;
	}

	LanAd ad;
	ad.protocolVersion = 2;
	ad.hostPort = 26201;
	ad.maxPlayers = 2;
	ad.playerCount = 1;
	ad.osRomHash = 0xA;
	ad.basicRomHash = 0xB;
	ad.cartName = "LoopbackCart";
	ad.hostHandle = "hosty";
	host.Advertise(ad);

	using clock = std::chrono::steady_clock;
	auto start = clock::now();
	std::vector<LanAd> rx;
	while (rx.empty() &&
		   std::chrono::duration_cast<std::chrono::milliseconds>(
			   clock::now() - start).count() < 2000) {
		uint64_t now = (uint64_t)std::chrono::duration_cast<
			std::chrono::milliseconds>(
			clock::now().time_since_epoch()).count();
		host.Poll(now, rx);   // emits
		peer.Poll(now, rx);
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
	}
	if (rx.empty()) {
		std::printf("LAN loopback skipped (no broadcast received — "
			"stack policy)\n");
		return;
	}
	bool found = false;
	for (auto& r : rx) {
		if (r.cartName == "LoopbackCart") { found = true; break; }
	}
	CHECK(found);
}

int main() {
	testDefaultIni();
	testIniParseEdgeCases();
	testLanAdRoundTrip();
	testLanAdRejectsZeroPort();
	testLanAdRejectsBadMagic();
	testLanDiscoveryLoopback();

	if (fails == 0) {
		std::printf("netplay lobby-config selftest: OK\n");
		return 0;
	}
	std::fprintf(stderr, "netplay lobby-config selftest: %d FAILURES\n", fails);
	return 1;
}
