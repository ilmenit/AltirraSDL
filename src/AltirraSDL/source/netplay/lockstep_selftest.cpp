// Altirra SDL3 netplay - lockstep loop self-test
//
// Runs two LockstepLoop instances against each other — one as Host,
// one as Joiner — ferrying NetInputPackets between them and driving
// the apply gate.  Mirrors altirra-netplay-poc/lockstep/loop_test.go:
// clean convergence + induced desync detection.
//
// Build (shim stdafx.h):
//
//   mkdir -p /tmp/netplay_selftest
//   : > /tmp/netplay_selftest/stdafx.h
//   g++ -std=c++17 -I /tmp/netplay_selftest -I src/AltirraSDL/source/netplay   src/AltirraSDL/source/netplay/lockstep.cpp   src/AltirraSDL/source/netplay/lockstep_selftest.cpp   -o /tmp/netplay_selftest/lockstep_run
//   /tmp/netplay_selftest/lockstep_run

#include "lockstep.h"

#include <cstdio>
#include <cstdint>
#include <cstring>

using namespace ATNetplay;

static int fails = 0;

#define CHECK(cond) do { \
		if (!(cond)) { \
			std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
			++fails; \
		} \
	} while (0)

// Tiny deterministic PRNG — same idea as the Go PoC's seeded RNG,
// but portable.  Don't use rand() because the host's stdlib varies.
struct XorShift32 {
	uint32_t state;
	explicit XorShift32(uint32_t seed) : state(seed ? seed : 1) {}
	uint32_t next() {
		uint32_t x = state;
		x ^= x << 13; x ^= x >> 17; x ^= x << 5;
		state = x;
		return x;
	}
};

static NetInput genInput(XorShift32& r) {
	NetInput in;
	in.stickDir = (uint8_t)(r.next() & 0x0F);
	in.buttons  = (uint8_t)(r.next() & 0x1F);
	in.keyScan  = (uint8_t)(r.next() & 0xFF);
	in.extFlags = 0;
	return in;
}

// Ferry: host has just submitted + possibly advanced; build its
// outgoing packet and hand to joiner (and vice versa).
static void ferryPackets(LockstepLoop& from, LockstepLoop& to, uint64_t nowMs) {
	NetInputPacket pkt;
	from.BuildOutgoingInputPacket(pkt);
	to.OnPeerInputPacket(pkt, nowMs);
}

// One wall tick from both peers' perspective.  Matches the design §6
// main-loop order: capture local, drain incoming packets, apply at
// most ONE emu frame, then emit our outgoing packet.  The "at most
// one apply per tick" rule is what makes emu advance at wall-clock
// rate rather than gulping down a backlog in a single burst.
static void tick(LockstepLoop& host, LockstepLoop& joiner,
                 const NetInput& hostLocal, const NetInput& joinerLocal,
                 uint64_t nowMs) {
	// 1. Capture local.
	host.SubmitLocalInput(hostLocal);
	joiner.SubmitLocalInput(joinerLocal);

	// 2. Drain incoming (packets built in the PREVIOUS tick's step 4).
	//    On the very first tick both sides have nothing queued, which
	//    is fine — the warm-up frames apply off the pre-filled zero
	//    inputs.

	// 3. Apply at most one emu frame.
	if (host.CanAdvance())   host.OnFrameAdvanced();
	if (joiner.CanAdvance()) joiner.OnFrameAdvanced();

	// 4. Emit outgoing packets; peer consumes in their next step 2.
	ferryPackets(host, joiner, nowMs);
	ferryPackets(joiner, host, nowMs);
}

// -------------------------------------------------------------------
// Cases
// -------------------------------------------------------------------

static void testCleanConvergence() {
	LockstepLoop host, joiner;
	host.Begin(Slot::Host, 3);
	joiner.Begin(Slot::Joiner, 3);

	CHECK(host.CurrentFrame() == 0);
	CHECK(joiner.CurrentFrame() == 0);

	XorShift32 hostRng(1'000'003u * 1), joinerRng(1'000'003u * 2);

	const uint32_t kFrames = 120;
	for (uint32_t i = 0; i < kFrames + 30 /*slack for warm/drain*/; ++i) {
		NetInput h = genInput(hostRng);
		NetInput j = genInput(joinerRng);
		tick(host, joiner, h, j, (uint64_t)i * 16);

		// Neither side should spontaneously go desync.
		CHECK(!host.IsDesynced());
		CHECK(!joiner.IsDesynced());
	}

	CHECK(host.CurrentFrame() >= kFrames);
	CHECK(joiner.CurrentFrame() >= kFrames);

	// Both peers must be on the same emu frame (lockstep invariant).
	CHECK(host.CurrentFrame() == joiner.CurrentFrame());
}

static void testInducedDesyncIsDetected() {
	LockstepLoop host, joiner;
	host.Begin(Slot::Host, 3);
	joiner.Begin(Slot::Joiner, 3);

	// Corrupt host's local hash at emu frame 50.  Wire bytes sent to
	// joiner are untouched, so joiner's hash stays clean; the two
	// streams diverge from frame 50 onward.
	host.SetDesyncInjectFrame(50);

	XorShift32 hostRng(1'000'003u * 1), joinerRng(1'000'003u * 2);

	int64_t firstHostDetect   = -1;
	int64_t firstJoinerDetect = -1;

	for (uint32_t i = 0; i < 120; ++i) {
		NetInput h = genInput(hostRng);
		NetInput j = genInput(joinerRng);
		tick(host, joiner, h, j, (uint64_t)i * 16);

		if (firstHostDetect   < 0 && host.IsDesynced())
			firstHostDetect = host.DesyncFrame();
		if (firstJoinerDetect < 0 && joiner.IsDesynced())
			firstJoinerDetect = joiner.DesyncFrame();

		if (firstHostDetect >= 0 && firstJoinerDetect >= 0) break;
	}

	// Both peers must have noticed within a few frames of the
	// injection point.  Loose bound: detect within 10 frames.
	CHECK(firstHostDetect   >= 50 && firstHostDetect   < 60);
	CHECK(firstJoinerDetect >= 50 && firstJoinerDetect < 60);
}

static void testWarmupAdvances() {
	// With inputDelay = 3, the first 3 emu frames should advance
	// immediately with zero inputs (both sides pre-filled zeroes in
	// Begin()).
	LockstepLoop host, joiner;
	host.Begin(Slot::Host, 3);
	joiner.Begin(Slot::Joiner, 3);

	CHECK(host.CanAdvance());
	CHECK(joiner.CanAdvance());

	host.OnFrameAdvanced();
	joiner.OnFrameAdvanced();
	CHECK(host.CurrentFrame() == 1);
	CHECK(joiner.CurrentFrame() == 1);

	// Frames 0, 1, 2 are the warm-up.  Frame 3 needs real input.
	CHECK(host.CanAdvance());
	host.OnFrameAdvanced();
	CHECK(host.CanAdvance());
	host.OnFrameAdvanced();
	CHECK(host.CurrentFrame() == 3);
	// No one has submitted input for frame 3 yet.
	CHECK(!host.CanAdvance());
}

static void testCanonicalSlotOrdering() {
	// If the canonical ordering worked, host folds (hostInput,
	// peerInput) and joiner folds (peerInput, hostInput) — but
	// because each side remaps its own view, both see (P1, P2) in
	// the same order.  The test: run one frame on each side with
	// different-but-known inputs, compare hashes.

	LockstepLoop host, joiner;
	host.Begin(Slot::Host, 0);       // zero delay for simplicity
	joiner.Begin(Slot::Joiner, 0);

	NetInput hostIn{};  hostIn.stickDir = 0x0A;  hostIn.buttons = 0xB0;
	NetInput joinerIn{}; joinerIn.stickDir = 0x01; joinerIn.buttons = 0x20;

	host.SubmitLocalInput(hostIn);
	joiner.SubmitLocalInput(joinerIn);

	// Hand each peer the other's input.
	NetInputPacket hp, jp;
	host.BuildOutgoingInputPacket(hp);
	joiner.BuildOutgoingInputPacket(jp);
	joiner.OnPeerInputPacket(hp, 0);
	host.OnPeerInputPacket(jp, 0);

	CHECK(host.CanAdvance());
	CHECK(joiner.CanAdvance());
	host.OnFrameAdvanced();
	joiner.OnFrameAdvanced();

	// If ordering is canonical, neither side sees a desync from a
	// trivial slot permutation.  Run another frame to let the
	// receive-time hash compare fire.
	host.SubmitLocalInput(hostIn);
	joiner.SubmitLocalInput(joinerIn);
	host.BuildOutgoingInputPacket(hp);
	joiner.BuildOutgoingInputPacket(jp);
	joiner.OnPeerInputPacket(hp, 1);
	host.OnPeerInputPacket(jp, 1);

	CHECK(!host.IsDesynced());
	CHECK(!joiner.IsDesynced());
}

static void testPeerTimeout() {
	LockstepLoop l;
	l.Begin(Slot::Host, 3);

	// No peer packets seen yet → not timed out regardless of time.
	CHECK(!l.PeerTimedOut(1'000'000, 1000));

	// Feed one packet at t=100.
	NetInputPacket pkt {};
	pkt.magic = kMagicInput;
	pkt.baseFrame = 0;
	pkt.count = 1;
	l.OnPeerInputPacket(pkt, 100);

	CHECK(!l.PeerTimedOut(500, 1000));    // 400 ms since last — ok
	CHECK(!l.PeerTimedOut(1099, 1000));   // 999 ms since — still ok
	CHECK(l.PeerTimedOut(1101, 1000));    // 1001 ms since — timed out
}

static void testOutgoingPacketShape() {
	// Just after Begin with inputDelay=3 and no SubmitLocalInput
	// calls, frames [0, 3) have been pre-filled with zeros (warmup).
	// The outgoing window should include those three frames and
	// nothing else.
	LockstepLoop l;
	l.Begin(Slot::Host, 3);

	NetInputPacket pkt;
	l.BuildOutgoingInputPacket(pkt);

	CHECK(pkt.magic == kMagicInput);
	CHECK(pkt.count == 3);
	CHECK(pkt.baseFrame == 0);

	// After submitting one local input at currentFrame=0 (keying at
	// frame 3), the window extends to include frame 3.
	NetInput one {}; one.stickDir = 0x0F;
	l.SubmitLocalInput(one);
	l.BuildOutgoingInputPacket(pkt);
	CHECK(pkt.count == 4);          // frames 0..3
	// The window covers [end - R, end) clamped at 0.  end = 4, R =
	// 5, so window start = 0 and we get 4 populated slots.
}

int main() {
	testCleanConvergence();
	testInducedDesyncIsDetected();
	testWarmupAdvances();
	testCanonicalSlotOrdering();
	testPeerTimeout();
	testOutgoingPacketShape();

	if (fails == 0) {
		std::printf("netplay lockstep selftest: OK\n");
		return 0;
	}
	std::fprintf(stderr, "netplay lockstep selftest: %d FAILURES\n", fails);
	return 1;
}
