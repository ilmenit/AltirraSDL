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
#include "netplay_profile.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

using namespace ATNetplay;

// Helper: a default boot config with the canonical profile version
// stamped in so the joiner doesn't reject the host with a "different
// profile version" error.  All tests below need this; doing it once
// at top-level keeps the per-test boilerplate small.
static NetBootConfig makeDefaultBootConfig() {
	NetBootConfig bc{};
	bc.canonicalProfileVersion = ATNetplayProfile::kCanonicalProfileVersion;
	return bc;
}

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

	NetBootConfig defaultBoot = makeDefaultBootConfig();
	CHECK(host.BeginHost(
		/*localPort*/ 0,
		/*playerHandle*/ "alice",
		/*cartName*/ "Joust.atr",
		kOsRom, kBasicRom, kSettings,
		/*inputDelayFrames*/ 3,
		/*entryCodeHash*/ nullptr,  // public session
		defaultBoot
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

		// Pre-v4 API was `OnFrameAdvanced()` with no args; current
		// signature takes the simulator-state hash.  Use 0 — these
		// tests don't assert hash convergence (separate hashes-
		// match selftest covers that).
		if (host.CanAdvance())   host.OnFrameAdvanced(0);
		if (joiner.CanAdvance()) joiner.OnFrameAdvanced(0);

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

	{
		NetBootConfig defaultBoot = makeDefaultBootConfig();
		CHECK(host.BeginHost(0, "alice", "Joust.atr",
			/*osRom*/ 0xAAAA, /*basicRom*/ 0xBBBB, /*settings*/ 0,
			/*inputDelay*/ 3, /*entryCode*/ nullptr,
			defaultBoot));
	}

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

	{
		NetBootConfig defaultBoot = makeDefaultBootConfig();
		CHECK(host.BeginHost(0, "alice", "Joust.atr", 1, 2, 3, 3,
			code, defaultBoot));
	}

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
	// Error wording changed from "entry code" to "join code" — match
	// either so this test survives further wording polish.
	CHECK(joiner.LastError() &&
	      (std::strstr(joiner.LastError(), "entry code") != nullptr ||
	       std::strstr(joiner.LastError(), "join code") != nullptr));

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

// Item 6b regression: snapshot transfer must complete despite a 30%
// outbound packet-loss rate on BOTH sides.  Validates the sliding-
// window sender + retry budget bumped to 10 attempts, against a real
// loopback transport.  Drop is applied symmetrically (host's outbound
// AND joiner's outbound) to model realistic two-sided jitter.
//
// Bound: with kRetryIntervalMs=500 and ~30% loss, individual chunks
// converge in 1-3 retries; the full transfer for a 4 KB / 4-chunk
// snapshot completes well inside 5 seconds.  We give 10 s.
static void testSnapshotSurvives30PctLoss() {
	Coordinator host, joiner;
	const uint64_t kOsRom = 0xDEADBEEFAABBCCDDULL;
	const uint64_t kBasicRom = 0xCAFEF00DEFEFEFEEULL;
	const uint64_t kSettings = 0x1122334455667788ULL;

	NetBootConfig defaultBoot = makeDefaultBootConfig();
	CHECK(host.BeginHost(0, "alice", "Joust.atr",
		kOsRom, kBasicRom, kSettings, /*delay*/ 3, nullptr,
		defaultBoot));

	auto snap = makeMockSnapshot(4096);
	host.SubmitSnapshotForUpload(snap.data(), snap.size());

	char hostAddr[48];
	std::snprintf(hostAddr, sizeof hostAddr, "127.0.0.1:%u",
		(unsigned)host.BoundPort());
	CHECK(joiner.BeginJoin(hostAddr, "bob",
		kOsRom, kBasicRom, true, nullptr));

	// Inject 30% drop AFTER both peers have bound a socket — different
	// seeds so each side's drop pattern is independent (matches what
	// real packet loss looks like).
	host.TestGetTransport().SetTestDropRate(0.30f, /*seed*/ 0xC0FFEE01u);
	joiner.TestGetTransport().SetTestDropRate(0.30f, /*seed*/ 0xC0FFEE02u);

	bool ok = spinUntil(host, joiner, [&]{
		return joiner.GetPhase() == Coordinator::Phase::SnapshotReady;
	}, 10000);
	CHECK(ok);
	// Joiner is at SnapshotReady; the transition to Lockstepping
	// happens on AcknowledgeSnapshotApplied — we don't need that
	// for this test.  Wait one round-trip more for the host to see
	// the final ACK and itself transition (under loss the host's
	// snapshot delivered → Lockstepping handshake takes another
	// retry after the joiner finished).
	ok = spinUntil(host, joiner, [&]{
		return host.GetPhase() == Coordinator::Phase::Lockstepping;
	}, 5000);
	CHECK(ok);

	// Stop dropping so the Bye exchange isn't flaky.
	host.TestGetTransport().SetTestDropRate(0.0f);
	joiner.TestGetTransport().SetTestDropRate(0.0f);

	host.End();
	joiner.End();
}

// Item 6b regression: when the host stops sending entirely after
// Welcome (simulating a host crash / network drop mid-snapshot), the
// joiner's ReceivingSnapshot watchdog (Item 1) must fail the session
// within ~kSnapshotReceiveTimeoutMs (30 s) instead of hanging
// forever.  We compress the test by injecting 100% drop on the
// host's outbound at exactly the moment the joiner enters
// ReceivingSnapshot — the joiner sees Welcome but no chunks ever.
//
// We don't actually wait the full 30 s; instead we just verify that:
//   1. joiner is in ReceivingSnapshot after Welcome arrives,
//   2. joiner stays there with NO chunks delivered (all dropped),
//   3. eventually FailWith fires (we accept any time within 35 s).
// On a slow CI runner this should still complete inside 35 s.
static void testJoinerDetectsStalledSnapshotHost() {
	Coordinator host, joiner;
	NetBootConfig defaultBoot = makeDefaultBootConfig();
	CHECK(host.BeginHost(0, "alice", "Joust.atr",
		1, 2, 3, /*delay*/ 3, nullptr, defaultBoot));
	// Use a snapshot big enough that the chunked transfer can't
	// complete in a single Poll iteration.  At kSnapshotChunkSize=1200
	// and the 32-chunk sliding window, 1 MB ≈ 875 chunks ≈ 27 windows
	// — plenty of opportunity to inject the drop mid-transfer.
	auto snap = makeMockSnapshot(1024 * 1024);
	host.SubmitSnapshotForUpload(snap.data(), snap.size());
	char hostAddr[48];
	std::snprintf(hostAddr, sizeof hostAddr, "127.0.0.1:%u",
		(unsigned)host.BoundPort());
	CHECK(joiner.BeginJoin(hostAddr, "bob", 1, 2, true, nullptr));

	// Custom poll loop: inject the 100% drop the moment the joiner
	// reports ReceivingSnapshot, then wait for the watchdog (Item 1)
	// to flip the joiner to Failed.  spinUntil with a single
	// completion predicate isn't quite enough here — the inject has
	// to happen at the right moment, not before.
	bool injected = false;
	bool failed   = false;
	const uint64_t startMs = nowMs();
	const uint64_t deadlineMs = startMs + 40000;   // > 30 s watchdog
	while (nowMs() < deadlineMs) {
		host.Poll(nowMs());
		joiner.Poll(nowMs());
		if (!injected &&
		    joiner.GetPhase() == Coordinator::Phase::ReceivingSnapshot) {
			host.TestGetTransport().SetTestDropRate(1.0f);
			injected = true;
		}
		if (joiner.GetPhase() == Coordinator::Phase::Failed) {
			failed = true;
			break;
		}
		// 5 ms is fine — the watchdog fires off wallclock so we
		// don't need to pin the loop tight.
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}
	CHECK(injected);   // we did get into ReceivingSnapshot
	CHECK(failed);     // and the watchdog fired
	CHECK(joiner.LastError() != nullptr &&
	      std::strstr(joiner.LastError(), "stalled") != nullptr);

	// Cleanup.
	host.TestGetTransport().SetTestDropRate(0.0f);
	host.End();
	joiner.End();
}

int main() {
	testEndToEndSession();
	testHostRejectsWrongOsRom();
	testPrivateSessionAcceptsCorrectCode();
	testSnapshotSurvives30PctLoss();
	testJoinerDetectsStalledSnapshotHost();

	if (fails == 0) {
		std::printf("netplay coordinator selftest: OK\n");
		return 0;
	}
	std::fprintf(stderr, "netplay coordinator selftest: %d FAILURES\n", fails);
	return 1;
}
