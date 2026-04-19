// Altirra SDL3 netplay - end-to-end coordinator self-test
//
// Two Coordinator instances in one process, real UDP loopback,
// driven to completion of a full session: handshake, snapshot
// transfer, 120 lockstep frames with matching hashes, clean Bye.
//
// Build (shim stdafx.h):
//
//   mkdir -p /tmp/netplay_selftest
//   : > /tmp/netplay_selftest/stdafx.h
//   g++ -std=c++17 -Wall -Wextra -pthread   -I /tmp/netplay_selftest -I src/AltirraSDL/source/netplay   src/AltirraSDL/source/netplay/protocol.cpp   src/AltirraSDL/source/netplay/transport.cpp   src/AltirraSDL/source/netplay/snapshot_channel.cpp   src/AltirraSDL/source/netplay/lockstep.cpp   src/AltirraSDL/source/netplay/coordinator.cpp   src/AltirraSDL/source/netplay/coordinator_selftest.cpp   -o /tmp/netplay_selftest/coordinator_run
//   /tmp/netplay_selftest/coordinator_run

#include "coordinator.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

using namespace ATNetplay;

static int fails = 0;

#define CHECK(cond) do { \
		if (!(cond)) { \
			std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
			++fails; \
		} \
	} while (0)

// Monotonic clock in ms, local to the test.
static uint64_t nowMs() {
	using namespace std::chrono;
	return (uint64_t)duration_cast<milliseconds>(
		steady_clock::now().time_since_epoch()).count();
}

static std::vector<uint8_t> makeMockSnapshot(size_t n) {
	std::vector<uint8_t> v(n);
	for (size_t i = 0; i < n; ++i) v[i] = (uint8_t)((i * 37u + 5u) & 0xFF);
	return v;
}

// Spin the poll loop for up to `timeoutMs`, stopping when `done()`
// returns true.  Both coordinators are polled on every iteration.
template <typename DoneFn>
static bool spinUntil(Coordinator& host, Coordinator& joiner,
                      DoneFn done, uint32_t timeoutMs) {
	uint64_t deadline = nowMs() + timeoutMs;
	while (nowMs() < deadline) {
		host.Poll(nowMs());
		joiner.Poll(nowMs());
		if (done()) return true;
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	return false;
}

static void testEndToEndSession() {
	Coordinator host, joiner;

	const uint64_t kOsRom = 0xDEADBEEF01234567ULL;
	const uint64_t kBasicRom = 0xCAFEF00DBABE0001ULL;
	const uint64_t kSettings = 0xABCDEF0011223344ULL;

	CHECK(host.BeginHost(
		/*localPort*/ 0,
		/*playerHandle*/ "alice",
		/*cartName*/ "Joust.atr",
		kOsRom, kBasicRom, kSettings,
		/*inputDelayFrames*/ 3,
		/*entryCodeHash*/ nullptr  // public session
	));
	CHECK(host.GetPhase() == Coordinator::Phase::WaitingForJoiner);

	// Prime the host's snapshot before the joiner arrives, to exercise
	// the non-deferred Welcome path.
	auto snapshot = makeMockSnapshot(4000);
	host.SubmitSnapshotForUpload(snapshot.data(), snapshot.size());

	// Build "127.0.0.1:<host port>".
	char hostAddr[48];
	std::snprintf(hostAddr, sizeof hostAddr, "127.0.0.1:%u",
		(unsigned)host.BoundPort());

	CHECK(joiner.BeginJoin(
		hostAddr,
		"bob",
		kOsRom, kBasicRom,
		/*acceptTos*/ true,
		/*entryCodeHash*/ nullptr
	));
	CHECK(joiner.GetPhase() == Coordinator::Phase::Handshaking);

	// Spin until joiner reaches SnapshotReady.
	bool ok = spinUntil(host, joiner, [&]{
		return joiner.GetPhase() == Coordinator::Phase::SnapshotReady;
	}, /*timeoutMs*/ 5000);
	CHECK(ok);
	if (!ok) {
		std::fprintf(stderr, "joiner stuck in phase %d (host phase %d, err=%s)\n",
			(int)joiner.GetPhase(), (int)host.GetPhase(),
			joiner.LastError());
		return;
	}

	// Joiner: verify snapshot bytes arrived intact.
	const auto& rx = joiner.GetReceivedSnapshot();
	CHECK(rx.size() == snapshot.size());
	CHECK(std::memcmp(rx.data(), snapshot.data(), snapshot.size()) == 0);

	// Apply and ack.
	joiner.AcknowledgeSnapshotApplied();
	CHECK(joiner.GetPhase() == Coordinator::Phase::Lockstepping);

	// Host should also be in lockstep by now (transitioned on last ACK).
	ok = spinUntil(host, joiner, [&]{
		return host.GetPhase() == Coordinator::Phase::Lockstepping;
	}, /*timeoutMs*/ 1000);
	CHECK(ok);

	// Run 120 lockstep frames.  Both sides advance at most one frame
	// per "tick", matching the main-loop pattern.
	const uint32_t kFrames = 120;
	for (uint32_t i = 0; i < kFrames + 20 /*slack*/; ++i) {
		host.Poll(nowMs());
		joiner.Poll(nowMs());

		// Submit identical local inputs (in real life they'd differ;
		// identical is fine for a hash-convergence test because both
		// sides fold in canonical order).
		NetInput hostIn {};
		hostIn.stickDir = (uint8_t)(i & 0x0F);
		hostIn.buttons  = (uint8_t)((i >> 4) & 0x1F);

		NetInput joinerIn {};
		joinerIn.stickDir = (uint8_t)((i * 7) & 0x0F);
		joinerIn.buttons  = (uint8_t)((i * 13) & 0x1F);

		host.SubmitLocalInput(hostIn);
		joiner.SubmitLocalInput(joinerIn);

		// Let packets fly between peers.
		host.Poll(nowMs());
		joiner.Poll(nowMs());

		if (host.CanAdvance())   host.OnFrameAdvanced();
		if (joiner.CanAdvance()) joiner.OnFrameAdvanced();

		CHECK(host.GetPhase() != Coordinator::Phase::Desynced);
		CHECK(joiner.GetPhase() != Coordinator::Phase::Desynced);

		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}

	CHECK(host.Loop().CurrentFrame() >= kFrames);
	CHECK(joiner.Loop().CurrentFrame() >= kFrames);
	// Both peers applied the same number of frames.
	CHECK(host.Loop().CurrentFrame() == joiner.Loop().CurrentFrame());

	// Clean termination.
	host.End();
	// Give the Bye a moment to reach joiner.
	spinUntil(host, joiner, [&]{
		return joiner.GetPhase() == Coordinator::Phase::Ended;
	}, 500);
	CHECK(joiner.GetPhase() == Coordinator::Phase::Ended);
}

static void testHostRejectsWrongOsRom() {
	Coordinator host, joiner;

	CHECK(host.BeginHost(0, "alice", "Joust.atr",
		/*osRom*/ 0xAAAA, /*basicRom*/ 0xBBBB, /*settings*/ 0,
		/*inputDelay*/ 3, /*entryCode*/ nullptr));

	char addr[48];
	std::snprintf(addr, sizeof addr, "127.0.0.1:%u", (unsigned)host.BoundPort());
	CHECK(joiner.BeginJoin(addr, "bob",
		/*osRom*/ 0xCCCC,        // <-- different!
		/*basicRom*/ 0xBBBB,
		/*acceptTos*/ true,
		/*entryCode*/ nullptr));

	bool ok = spinUntil(host, joiner, [&]{
		return joiner.GetPhase() == Coordinator::Phase::Failed;
	}, 2000);
	CHECK(ok);
	CHECK(joiner.LastError() && std::strstr(joiner.LastError(), "OS ROM") != nullptr);
}

static void testPrivateSessionAcceptsCorrectCode() {
	Coordinator host, joiner;
	uint8_t code[kEntryCodeHashLen];
	for (size_t i = 0; i < kEntryCodeHashLen; ++i) code[i] = (uint8_t)(i + 0x10);

	CHECK(host.BeginHost(0, "alice", "Joust.atr", 1, 2, 3, 3, code));

	char addr[48];
	std::snprintf(addr, sizeof addr, "127.0.0.1:%u", (unsigned)host.BoundPort());

	// Wrong code first.
	uint8_t wrong[kEntryCodeHashLen];
	std::memset(wrong, 0xFF, sizeof wrong);
	CHECK(joiner.BeginJoin(addr, "bob", 1, 2, true, wrong));
	bool ok = spinUntil(host, joiner, [&]{
		return joiner.GetPhase() == Coordinator::Phase::Failed;
	}, 2000);
	CHECK(ok);
	CHECK(joiner.LastError() && std::strstr(joiner.LastError(), "entry code") != nullptr);

	// Fresh joiner with correct code.
	Coordinator joiner2;
	CHECK(joiner2.BeginJoin(addr, "bob", 1, 2, true, code));

	// Host snapshot needed for successful handshake.
	auto snap = makeMockSnapshot(128);
	host.SubmitSnapshotForUpload(snap.data(), snap.size());

	ok = spinUntil(host, joiner2, [&]{
		return joiner2.GetPhase() == Coordinator::Phase::SnapshotReady;
	}, 2000);
	CHECK(ok);
}

int main() {
	testEndToEndSession();
	testHostRejectsWrongOsRom();
	testPrivateSessionAcceptsCorrectCode();

	if (fails == 0) {
		std::printf("netplay coordinator selftest: OK\n");
		return 0;
	}
	std::fprintf(stderr, "netplay coordinator selftest: %d FAILURES\n", fails);
	return 1;
}
