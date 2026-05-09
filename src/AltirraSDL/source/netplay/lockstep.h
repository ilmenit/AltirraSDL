// Altirra SDL3 netplay - lockstep loop
//
// Port of altirra-netplay-poc/lockstep/loop.go, with one structural
// change: no goroutines and no wall-clock reads from inside the loop.
// The caller (netplay coordinator) drives us from the emulator's
// existing 60 Hz tick, feeding us decoded peer packets and our local
// input each frame.  We return the decision "advance or stall" and a
// bytes-to-send packet; the transport happens elsewhere.
//
// Design doc references:
//
//   §2  deterministic-lockstep thesis (why inputs, not state)
//   §4  input-delay picture (wall F -> emu F+D)
//   §5.3 per-frame input packet format
//   §6  integration with the main loop
//
// Invariants:
//
//   1. Local input captured at wall tick W is stored keyed at emu
//      frame (W + D).  At wall tick W the simulator advances to emu
//      frame W (modulo a D-frame warm-up, pre-filled with zeros).
//
//   2. Advance only when both peers' inputs for the target frame are
//      known.  Otherwise the main loop stalls, pacer still runs, and
//      we poll the transport again.
//
//   3. Rolling hash is FNV-1a 64 over (frameLE, p1.raw4, p2.raw4) at
//      apply-time, with a CANONICAL slot ordering so both peers fold
//      bytes in the same sequence.  Low 32 bits go on the wire.
//
//   4. Desync is reported twice: at apply-time (if the peer's hash
//      for that frame has already arrived) and at receive-time (if
//      the peer's hash arrives for a frame we already applied).

#pragma once

#include "packets.h"

#include <cstddef>
#include <cstdint>

namespace ATNetplay {

enum class Slot : uint16_t {
	Host   = 1,
	Joiner = 2,
};

class LockstepLoop {
public:
	// Ring size for input + hash storage.  Must be a power of two.
	// Must comfortably exceed (input delay + R + the largest
	// plausible network burst of out-of-order delivery).
	// 256 frames ≈ 4.3 s at 60 Hz — more than enough.
	static constexpr uint32_t kRingSize = 256;

	// FNV-1a 64 parameters.
	static constexpr uint64_t kFnvOffset = 0xcbf29ce484222325ULL;
	static constexpr uint64_t kFnvPrime  = 0x00000100000001b3ULL;

	// Default tuning for the input-delay ratchet (see EvaluateDelayRatchet).
	// kDelayMin is the floor — the ratchet never lowers D below this.
	// kDelayMax caps runaway growth on pathological connections.
	// kRatchetStreakDefault: how many consecutive frames the target
	// must exceed the current D before scheduling a switch — at 60 fps
	// this is one second of consistent high RTT.
	// kSwitchModulusDefault: switches happen at the next emu frame
	// where (frame % kSwitchModulus) == 0.  At 60 fps this is ~17 s
	// of soonest activation; mean wait ~8.5 s.  Both peers compute
	// identically so no explicit handshake is needed.
	static constexpr uint32_t kDelayMin              = 3;
	static constexpr uint32_t kDelayMax              = 30;
	static constexpr uint32_t kRatchetStreakDefault  = 60;
	static constexpr uint32_t kSwitchModulusDefault  = 1024;

	// Begin a new session.  `inputDelay` is the number of wall frames
	// between local capture and emulation apply (D in the design
	// doc).  Valid range 0..32; 3 is the LAN default, 4 the internet
	// default.  Slot chooses the canonical hash-folding order.
	void Begin(Slot slot, uint32_t inputDelay);

	// Override the ratchet tuning constants for this loop.  Production
	// callers do not need this — Begin() seeds the defaults above.
	// Selftests use it to trigger ratcheting in fewer than 1024 frames.
	// Both peers MUST configure identical values; the ratchet decision
	// is deterministic only if the streak threshold and modulus match.
	void SetRatchetTuning(uint32_t streakFrames, uint32_t switchModulus) {
		mRatchetStreakFrames = streakFrames;
		mRatchetSwitchModulus = switchModulus;
	}

	// Record this wall-tick's local input.  Internally keyed at the
	// emu frame (CurrentFrame() + inputDelay), matching invariant #1.
	// Call at most once per wall tick.
	void SubmitLocalInput(const NetInput& local);

	// Feed a decoded NetInputPacket from the peer.  Merges the
	// sliding-window inputs into the peer-input ring (first-seen
	// wins, so duplicates from redundancy don't clobber earlier
	// state) and records the peer's claimed hash at ackedFrame for
	// the desync check.  Updates the peer-silence watchdog.
	//
	// Note: a peer's stateHashLow32 of 0 is treated as "no hash yet"
	// (the Go PoC convention).  A real FNV-1a-64 hash whose low 32
	// bits are exactly zero will be misread, delaying desync
	// detection by at most one frame.  Probability per frame ≈ 2⁻³²;
	// at 60 Hz that's ≈ one occurrence every 2 years of continuous
	// play.  Acceptable for v1.0; a future protocol revision can
	// steal a flag bit out of the packet's `flags` word to signal
	// presence explicitly.
	void OnPeerInputPacket(const NetInputPacket& pkt, uint64_t nowMs);

	// True iff we have local AND peer inputs for CurrentFrame().
	// The main loop must not call g_sim.Advance() while this returns
	// false.
	bool CanAdvance() const;

	// Fetch the inputs to inject into ATInputManager for the current
	// frame.  Returns false if CanAdvance() would have said false.
	// outP1/outP2 match the emulated ports 1 and 2 respectively,
	// regardless of which peer is host or joiner.
	bool GetInputsForCurrentFrame(NetInput& outP1, NetInput& outP2) const;

	// Called after the simulator has advanced exactly one frame.
	// `simStateHash` is the caller-computed 32-bit hash of the post-
	// apply simulator state (see netplay_simhash.h).  Stored as our
	// local hash for this frame and compared against any peer hash
	// already on file for the same frame; mismatch flags desync.
	// Bumps CurrentFrame().  The old API signature (no arg) is kept
	// for selftests via the default value — real callers must pass a
	// real hash or the desync detector is a no-op.
	void OnFrameAdvanced(uint32_t simStateHash = 0);

	// Build the outgoing input packet describing our own last R
	// frames of local input plus the highest peer frame we've
	// received (for acking) and our hash at that frame (for desync
	// detection).  The caller marshals with EncodeInputPacket and
	// sends over Transport.
	void BuildOutgoingInputPacket(NetInputPacket& out) const;

	uint32_t CurrentFrame() const { return mCurrentFrame; }
	uint32_t InputDelay() const { return mInputDelay; }
	Slot     GetSlot() const { return mSlot; }

	// True if a ratchet bump is queued and will fire when CurrentFrame
	// reaches the returned switch frame.  PendingDelaySwitchFrame()
	// returns 0 when no switch is queued.  Exposed for telemetry/UI
	// and selftest assertions; the actual decision logic is internal.
	bool     HasPendingDelayBump() const { return mPendingDelay != 0; }
	uint32_t PendingDelay()              const { return mPendingDelay; }
	uint32_t PendingDelaySwitchFrame()   const { return mPendingDelaySwitchFrame; }
	uint32_t DelayUpStreak()             const { return mDelayUpStreak; }

	// Map a host-stamped rttClass byte to the corresponding D target.
	// Public so selftests + Coordinator telemetry can reuse the same
	// quantisation rule; both peers must agree on this function for the
	// ratchet to converge.
	static uint32_t TargetDelayFromRttClass(uint8_t cls);

	// Inverse direction: encode a smoothed-RTT value (in ms) into the
	// rttClass byte.  4 ms per unit, capped at 255 (≈1020 ms).  Used by
	// the Coordinator to stamp the host's outgoing inputs.
	static uint8_t  RttClassFromMs(uint16_t rttMs);

	bool    IsDesynced()  const { return mDesyncFrame >= 0; }
	int64_t DesyncFrame() const { return mDesyncFrame; }

	// Lookup our own computed hash for `frame`.  Returns false if the
	// slot is unpopulated or has been recycled for a different frame.
	// Used by the resync initiator to seed the peer's post-apply ack
	// slot with a matching value.
	bool GetLocalHashAt(uint32_t frame, uint32_t& out) const {
		return GetLocalHash(frame, out);
	}

	// Has a peer packet arrived within the watchdog window ending
	// at `nowMs`?  Returns true (timed out) when the last-packet
	// timestamp is older than timeoutMs.  If no packet has ever
	// arrived, returns false — the caller decides what to do with
	// pre-first-packet silence.
	bool PeerTimedOut(uint64_t nowMs, uint64_t timeoutMs) const;

	// Monotonic-ms timestamp of the last peer input packet we
	// processed, or 0 if none has ever arrived.  Exposed for the
	// in-session HUD so it can render a live "packet age" readout.
	uint64_t LastPeerRecvMs() const { return mLastPeerRecvMs; }

	// Test hook: at emulation frame `frame` (not wall frame!), flip
	// one bit of the data fed to the LOCAL rolling hash without
	// touching the wire bytes.  Used by the selftest to confirm
	// desync detection.  Pass a negative value to disable.
	void SetDesyncInjectFrame(int64_t frame) { mDesyncInjectFrame = frame; }

	// Resync support.  ClearDesync() lets the coordinator drop the
	// desync flag after it has committed to running a resync — Keeps
	// CanAdvance() gated externally (by the coordinator phase) while
	// the savestate transfer is in flight, then unblocks normal advance
	// once ResumeAt() has re-seeded the rings.
	void ClearDesync() { mDesyncFrame = -1; }

	// Reset ring state and jump both peers to frame `resumeFrame` after
	// a mid-session savestate transfer.  All cached inputs and hashes
	// are discarded — they described the pre-resync timeline and would
	// collide with the post-apply frame numbering.  The ring is re-
	// warmed with inputDelay zeroed input frames starting at
	// resumeFrame so the first post-resync Advance() does not stall
	// waiting for pre-captured inputs that no longer exist.  `seedHash`
	// is stored for the (resumeFrame-1) slot so the first outgoing
	// InputPacket carries a valid ackedFrame/hash pair.
	void ResumeAt(uint32_t resumeFrame, uint32_t seedHash);

private:
	struct InputSlot {
		uint32_t frame    = 0;
		NetInput input    = {};
		uint8_t  occupied = 0;
	};
	struct HashSlot {
		uint32_t frame    = 0;
		uint32_t hashLow  = 0;
		uint8_t  occupied = 0;
	};

	bool GetLocalInput(uint32_t frame, NetInput& out) const;
	bool GetPeerInput(uint32_t frame, NetInput& out) const;
	bool GetLocalHash(uint32_t frame, uint32_t& out) const;
	bool GetPeerHash(uint32_t frame, uint32_t& out) const;

	void PutLocalInput(uint32_t frame, const NetInput& in);
	void PutPeerInput(uint32_t frame, const NetInput& in);
	void PutLocalHash(uint32_t frame, uint32_t v);
	void PutPeerHash(uint32_t frame, uint32_t v);

	// Ratchet helpers (lockstep.cpp).  EvaluateDelayRatchet is called at
	// the end of OnFrameAdvanced for the just-applied frame and
	// possibly schedules / applies a bump.  ApplyDelayRatchet performs
	// the gap fill and writes the new mInputDelay.
	uint8_t  AuthoritativeRttClass(uint32_t frame) const;
	void     EvaluateDelayRatchet();
	void     ApplyDelayRatchet();

	InputSlot mLocalInputs[kRingSize] = {};
	InputSlot mPeerInputs[kRingSize]  = {};
	HashSlot  mLocalHashes[kRingSize] = {};
	HashSlot  mPeerHashes[kRingSize]  = {};

	Slot     mSlot = Slot::Host;
	uint32_t mInputDelay = 3;
	uint32_t mCurrentFrame = 0;       // next emu frame to apply

	int64_t  mDesyncFrame = -1;       // -1 = no desync yet
	int64_t  mDesyncInjectFrame = -1; // test hook; -1 = disabled

	uint64_t mLastPeerRecvMs = 0;
	bool     mAnyPeerPacketSeen = false;

	// Dynamic input-delay ratchet (v7).  The host stamps an rttClass
	// byte into every NetInput it captures; both peers see the same
	// rttClass(F) on their authoritative ring and run identical
	// threshold logic.  When the target D exceeds the current D for
	// mRatchetStreakFrames consecutive frames, both peers schedule a
	// switch at the next emu frame where (frame % mRatchetSwitchModulus)
	// == 0.  At that frame the gap slots [F+D_old, F+D_new) are filled
	// deterministically by repeating the input from slot F+D_old-1 in
	// both rings, and mInputDelay updates atomically.
	//
	// The ratchet is one-way: D never lowers automatically (lowering
	// would discard already-queued input).  A future enhancement could
	// add careful ratchet-down via the resync path.
	uint32_t mDelayUpStreak           = 0;
	uint32_t mPendingDelay            = 0;   // 0 = no switch queued
	uint32_t mPendingDelaySwitchFrame = 0;   // emu frame to apply at
	uint32_t mRatchetStreakFrames     = kRatchetStreakDefault;
	uint32_t mRatchetSwitchModulus    = kSwitchModulusDefault;
};

} // namespace ATNetplay
