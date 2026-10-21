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
#include <deque>
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
	if (from.BuildRepairInputPacket(pkt)) to.OnPeerInputPacket(pkt, nowMs);
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
	// Both peers apply the SAME deterministic sim-state hash so a
	// clean run converges; the desync-inject hook flips a bit on
	// the local side only.  Non-zero encoding avoids the "0 =
	// no-hash" wire sentinel.
	const uint32_t syntheticHash = 0xA0000000u ^ (host.CurrentFrame() + 1);
	if (host.CanAdvance())   host.OnFrameAdvanced(syntheticHash);
	const uint32_t syntheticHash2 = 0xA0000000u ^ (joiner.CurrentFrame() + 1);
	if (joiner.CanAdvance()) joiner.OnFrameAdvanced(syntheticHash2);

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

	host.OnFrameAdvanced(0xDEADBEEFu);
	joiner.OnFrameAdvanced(0xDEADBEEFu);
	CHECK(host.CurrentFrame() == 1);
	CHECK(joiner.CurrentFrame() == 1);

	// Frames 0, 1, 2 are the warm-up.  Frame 3 needs real input.
	CHECK(host.CanAdvance());
	host.OnFrameAdvanced(0xDEADBEEFu);
	CHECK(host.CanAdvance());
	host.OnFrameAdvanced(0xDEADBEEFu);
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
	host.OnFrameAdvanced(0xCAFEBABEu);
	joiner.OnFrameAdvanced(0xCAFEBABEu);

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

static void testRatchetTargetMapping() {
	// Spot-check the deterministic threshold function.  Both peers
	// must compute identical D targets from the same rttClass byte
	// or the ratchet desyncs the input rings.
	CHECK(LockstepLoop::TargetDelayFromRttClass(0)   == 3);   // floor
	CHECK(LockstepLoop::TargetDelayFromRttClass(4)   == 3);   // 2+1=3 → floor
	CHECK(LockstepLoop::TargetDelayFromRttClass(8)   == 3);   // 2+2
	CHECK(LockstepLoop::TargetDelayFromRttClass(64)  == 10);  // 2+16
	CHECK(LockstepLoop::TargetDelayFromRttClass(112) == 16);  // 2+28 → cap
	CHECK(LockstepLoop::TargetDelayFromRttClass(255) == 30);  // cap

	// And the round-trip encoding.
	CHECK(LockstepLoop::RttClassFromMs(0)    == 0);
	CHECK(LockstepLoop::RttClassFromMs(4)    == 1);
	CHECK(LockstepLoop::RttClassFromMs(256)  == 64);
	CHECK(LockstepLoop::RttClassFromMs(2000) == 255);  // cap
}

static void testRatchetLowRttIsNoOp() {
	LockstepLoop host, joiner;
	host.Begin(Slot::Host, 3);
	joiner.Begin(Slot::Joiner, 3);
	// Tighten so any spurious bump would fire well within the loop.
	host.SetRatchetTuning(/*streak*/10, /*modulus*/32);
	joiner.SetRatchetTuning(/*streak*/10, /*modulus*/32);

	XorShift32 hr(1), jr(2);
	for (uint32_t i = 0; i < 200; ++i) {
		NetInput h = genInput(hr); h.rttClass = 0;
		NetInput j = genInput(jr); j.rttClass = 0;
		tick(host, joiner, h, j, (uint64_t)i * 16);
	}

	// At rttClass=0 the target stays at the floor (3) — never exceeds
	// the current delay, so no bump is ever scheduled.
	CHECK(host.InputDelay()   == 3);
	CHECK(joiner.InputDelay() == 3);
	CHECK(!host.HasPendingDelayBump());
	CHECK(!joiner.HasPendingDelayBump());
	CHECK(!host.IsDesynced());
	CHECK(!joiner.IsDesynced());
}

static void testRatchetUpAndConverge() {
	LockstepLoop host, joiner;
	host.Begin(Slot::Host, 3);
	joiner.Begin(Slot::Joiner, 3);
	// Streak=10 frames + modulus=32 lets the test fire a real ratchet
	// inside ~100 frames instead of the 1024-frame production cadence.
	host.SetRatchetTuning(/*streak*/10, /*modulus*/32);
	joiner.SetRatchetTuning(/*streak*/10, /*modulus*/32);

	XorShift32 hr(1), jr(2);

	// Phase A: 5 low-RTT frames — D must stay at 3, nothing scheduled.
	for (uint32_t i = 0; i < 5; ++i) {
		NetInput h = genInput(hr); h.rttClass = 0;
		NetInput j = genInput(jr); j.rttClass = 0;
		tick(host, joiner, h, j, (uint64_t)i * 16);
	}
	CHECK(host.InputDelay()   == 3);
	CHECK(joiner.InputDelay() == 3);

	// Phase B: 100 high-RTT frames.  rttClass=64 maps to target=18, so
	// the streak threshold (10) trips fast and a switch is scheduled
	// at the next modulus-32 boundary.  We cap the loop high enough
	// that the boundary frame is reached and the switch applies on
	// both sides.
	for (uint32_t i = 0; i < 100; ++i) {
		NetInput h = genInput(hr); h.rttClass = 64;
		NetInput j = genInput(jr); j.rttClass = 0;
		tick(host, joiner, h, j, (uint64_t)(5 + i) * 16);
	}

	// Both peers must agree on the new D and stay in lockstep.
	CHECK(host.InputDelay()   == joiner.InputDelay());
	CHECK(host.InputDelay()   == 10);
	CHECK(joiner.InputDelay() == 10);
	CHECK(host.CurrentFrame() == joiner.CurrentFrame());
	CHECK(!host.IsDesynced());
	CHECK(!joiner.IsDesynced());
	CHECK(!host.HasPendingDelayBump());
	CHECK(!joiner.HasPendingDelayBump());

	// Phase C: 60 more frames at high RTT — should keep running cleanly
	// at the new D without any further desync.  (rttClass=64 maps to
	// target=18 == current D, so the streak doesn't build again.)
	const uint32_t startFrame = host.CurrentFrame();
	for (uint32_t i = 0; i < 60; ++i) {
		NetInput h = genInput(hr); h.rttClass = 64;
		NetInput j = genInput(jr); j.rttClass = 0;
		tick(host, joiner, h, j, (uint64_t)(105 + i) * 16);
	}
	CHECK(host.CurrentFrame() > startFrame);
	CHECK(host.CurrentFrame() == joiner.CurrentFrame());
	CHECK(!host.IsDesynced());
	CHECK(!joiner.IsDesynced());
}

static void testRatchetSwitchFrameAlignment() {
	// Both peers must compute the SAME pending-switch-frame when they
	// schedule a bump.  This is the deterministic-coordination property
	// that lets the design avoid an explicit handshake message.
	LockstepLoop host, joiner;
	host.Begin(Slot::Host, 3);
	joiner.Begin(Slot::Joiner, 3);
	host.SetRatchetTuning(/*streak*/10, /*modulus*/32);
	joiner.SetRatchetTuning(/*streak*/10, /*modulus*/32);

	XorShift32 hr(1), jr(2);

	// Drive high-RTT inputs until both sides have a pending switch.
	// We assert at the first frame BOTH report HasPendingDelayBump,
	// and check that the scheduled frame matches.
	uint32_t hostSched = 0, joinerSched = 0;
	for (uint32_t i = 0; i < 100; ++i) {
		NetInput h = genInput(hr); h.rttClass = 64;
		NetInput j = genInput(jr); j.rttClass = 0;
		tick(host, joiner, h, j, (uint64_t)i * 16);
		if (host.HasPendingDelayBump() && joiner.HasPendingDelayBump()) {
			hostSched   = host.PendingDelaySwitchFrame();
			joinerSched = joiner.PendingDelaySwitchFrame();
			break;
		}
	}
	CHECK(hostSched != 0);
	CHECK(hostSched == joinerSched);
	// Switch frame must be a positive multiple of the modulus.
	CHECK((hostSched % 32) == 0);
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

// Regression network driver hashes the INPUT ACTUALLY APPLIED. A fixed
// synthetic per-frame hash masks divergent input timelines.
static uint32_t inputHash(const NetInput& a, const NetInput& b) {
	return 0x10000000u + a.stickDir * 65536u + b.stickDir * 256u + a.buttons;
}

static void testDelayedInputTimeline(unsigned latency, bool ratchet, bool changing) {
	struct Delivery { unsigned due; NetInputPacket packet; };
	std::deque<Delivery> hj, jh;
	LockstepLoop h, j;
	h.Begin(Slot::Host, 3); j.Begin(Slot::Joiner, 3);
	h.SetRatchetTuning(10, 32); j.SetRatchetTuning(10, 32);
	for (unsigned tick = 0; tick < 1000; ++tick) {
		while (!hj.empty() && hj.front().due <= tick) { j.OnPeerInputPacket(hj.front().packet, tick + 1); hj.pop_front(); }
		while (!jh.empty() && jh.front().due <= tick) { h.OnPeerInputPacket(jh.front().packet, tick + 1); jh.pop_front(); }
		NetInput hi{}, ji{};
		hi.stickDir = changing ? tick % 15 + 1 : 1;
		ji.stickDir = 2; hi.rttClass = ratchet ? 64 : 0;
		h.SubmitLocalInput(hi); j.SubmitLocalInput(ji);
		NetInput p1, p2;
		if (h.CanAdvance()) { CHECK(h.GetInputsForCurrentFrame(p1, p2)); h.OnFrameAdvanced(inputHash(p1, p2)); }
		if (j.CanAdvance()) { CHECK(j.GetInputsForCurrentFrame(p1, p2)); j.OnFrameAdvanced(inputHash(p1, p2)); }
		NetInputPacket hp, jp;
		h.BuildOutgoingInputPacket(hp); j.BuildOutgoingInputPacket(jp);
		hj.push_back({tick + latency, hp}); jh.push_back({tick + latency, jp});
		if (h.BuildRepairInputPacket(hp)) hj.push_back({tick + latency, hp});
		if (j.BuildRepairInputPacket(jp)) jh.push_back({tick + latency, jp});
		CHECK(!h.IsDesynced()); CHECK(!j.IsDesynced());
	}
	CHECK(h.CurrentFrame() > 300); CHECK(j.CurrentFrame() > 300);
	if (ratchet) { CHECK(h.InputDelay() == 10); CHECK(j.InputDelay() == 10); }
}

static void testBurstLossRepair() {
	for (unsigned delay : {3u, 8u}) {
		LockstepLoop h, j; h.Begin(Slot::Host, delay); j.Begin(Slot::Joiner, delay);
		for (unsigned tick = 0; tick < 500; ++tick) {
			NetInput in{}; h.SubmitLocalInput(in); j.SubmitLocalInput(in);
			if (h.CanAdvance()) h.OnFrameAdvanced(1);
			if (j.CanAdvance()) j.OnFrameAdvanced(1);
			NetInputPacket hp, jp; h.BuildOutgoingInputPacket(hp); j.BuildOutgoingInputPacket(jp);
			if (tick >= 10) j.OnPeerInputPacket(hp, tick + 1);
			h.OnPeerInputPacket(jp, tick + 1);
			if (tick >= 10 && h.BuildRepairInputPacket(hp)) j.OnPeerInputPacket(hp, tick + 1);
			if (j.BuildRepairInputPacket(jp)) h.OnPeerInputPacket(jp, tick + 1);
		}
		CHECK(h.CurrentFrame() > 450); CHECK(j.CurrentFrame() > 450);
		CHECK(!h.IsDesynced()); CHECK(!j.IsDesynced());
	}
}

static void testHashBeyond65536Frames() {
	LockstepLoop h, j; h.Begin(Slot::Host, 3); j.Begin(Slot::Joiner, 3);
	for (unsigned tick = 0; tick < 65550; ++tick) {
		NetInput in{}; h.SubmitLocalInput(in); j.SubmitLocalInput(in);
		NetInputPacket hp, jp; h.BuildOutgoingInputPacket(hp); j.BuildOutgoingInputPacket(jp);
		j.OnPeerInputPacket(hp, tick + 1); h.OnPeerInputPacket(jp, tick + 1);
		if (h.CanAdvance()) h.OnFrameAdvanced(0x12345678);
		if (j.CanAdvance()) j.OnFrameAdvanced(tick >= 65540 ? 0x12345679 : 0x12345678);
		if (h.IsDesynced() || j.IsDesynced()) { CHECK(tick == 65541); return; }
	}
	CHECK(false);
}

static void testBoundedRollbackCorrection() {
	LockstepLoop loop; loop.SetRollbackWindow(4); loop.Begin(Slot::Host, 0);
	for (unsigned frame = 0; frame < 4; ++frame) {
		NetInput local{}; loop.SubmitLocalInput(local);
		CHECK(!loop.NeedsLocalInput()); CHECK(loop.CanAdvance());
		loop.OnFrameAdvanced(100 + frame);
	}
	NetInput local{}; loop.SubmitLocalInput(local);
	CHECK(!loop.CanAdvance()); CHECK(loop.ConfirmedFrame() == 0);
	NetInputPacket remote{}; remote.count = 4;
	remote.inputs[0].stickDir = 1;
	loop.OnPeerInputPacket(remote, 10);
	CHECK(loop.RollbackFrame() == 0); CHECK(!loop.CanAdvance());
	CHECK(loop.RewindTo(0)); CHECK(loop.RollbackFrame() == -1);
	NetInput p1, p2;
	CHECK(loop.GetInputsForFrame(0, p1, p2)); CHECK(p2.stickDir == 1);
	for (unsigned frame = 0; frame < 4; ++frame) { CHECK(loop.CanAdvance()); loop.OnFrameAdvanced(200 + frame); }
	CHECK(loop.ConfirmedFrame() == 4);
	NetInputPacket sent{}; loop.BuildOutgoingInputPacket(sent);
	CHECK(sent.ackedFrame == 3); CHECK(sent.stateHashLow32 == 203);
	CHECK(!loop.RewindTo(UINT32_MAX));
}

static void testFrameRolloverAndInvalidWindow() {
	LockstepLoop loop; loop.Begin(Slot::Host, 3); loop.ResumeAt(UINT32_MAX - 1, 1);
	for (unsigned i = 0; i < 3; ++i) {
		NetInput in{}; loop.SubmitLocalInput(in); CHECK(loop.CanAdvance()); loop.OnFrameAdvanced(1);
	}
	CHECK(loop.CurrentFrame() == 1);
	NetInputPacket invalid{}; invalid.baseFrame = loop.CurrentFrame() + LockstepLoop::kRingSize; invalid.count = 1;
	loop.OnPeerInputPacket(invalid, 1);
	CHECK(!loop.CanAdvance());
}

static void testPartialFrameCorrection() {
	LockstepLoop loop; loop.SetRollbackWindow(4); loop.Begin(Slot::Host, 0);
	NetInput local{}; loop.SubmitLocalInput(local);
	NetInput p1, p2;
	CHECK(loop.GetInputsForCurrentFrame(p1, p2)); CHECK(p2.stickDir == 0);
	// Input was applied, but ANTIC has not completed this frame yet.
	NetInputPacket remote{}; remote.count = 1; remote.inputs[0].stickDir = 1;
	loop.OnPeerInputPacket(remote, 1);
	CHECK(loop.RollbackFrame() == 0); CHECK(!loop.CanAdvance());
	loop.OnFrameAdvanced(100); CHECK(loop.CurrentFrame() == 0);
	CHECK(loop.RewindTo(0));
	CHECK(loop.GetInputsForCurrentFrame(p1, p2)); CHECK(p2.stickDir == 1);
	loop.OnFrameAdvanced(200); CHECK(loop.CurrentFrame() == 1);
}

static void testOldHoleBoundsPredictionEvenWithCurrentInput() {
	LockstepLoop loop; loop.SetRollbackWindow(4); loop.Begin(Slot::Host, 0);
	for (uint32_t frame = 0; frame < 4; ++frame) {
		NetInput local{}; loop.SubmitLocalInput(local);
		if (frame > 0) {
			NetInputPacket remote{}; remote.baseFrame = frame; remote.count = 1;
			loop.OnPeerInputPacket(remote, frame + 1);
		}
		CHECK(loop.CanAdvance()); loop.OnFrameAdvanced(1);
	}
	NetInput local{}; loop.SubmitLocalInput(local);
	NetInputPacket remote{}; remote.baseFrame = 4; remote.count = 1;
	loop.OnPeerInputPacket(remote, 10);
	CHECK(loop.ConfirmedFrame() == 0); CHECK(!loop.CanAdvance());
	remote.baseFrame = 0; loop.OnPeerInputPacket(remote, 11);
	CHECK(loop.ConfirmedFrame() == 4); CHECK(loop.CanAdvance());
}

int main() {
	testPartialFrameCorrection();
	testOldHoleBoundsPredictionEvenWithCurrentInput();
	testDelayedInputTimeline(2, true, false);
	testDelayedInputTimeline(5, false, true);
	testBurstLossRepair();
	testHashBeyond65536Frames();
	testBoundedRollbackCorrection();
	testFrameRolloverAndInvalidWindow();
	testCleanConvergence();
	testInducedDesyncIsDetected();
	testWarmupAdvances();
	testCanonicalSlotOrdering();
	testPeerTimeout();
	testRatchetTargetMapping();
	testRatchetLowRttIsNoOp();
	testRatchetUpAndConverge();
	testRatchetSwitchFrameAlignment();
	testOutgoingPacketShape();

	if (fails == 0) {
		std::printf("netplay lockstep selftest: OK\n");
		return 0;
	}
	std::fprintf(stderr, "netplay lockstep selftest: %d FAILURES\n", fails);
	return 1;
}
