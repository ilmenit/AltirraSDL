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

	// Begin a new session.  `inputDelay` is the number of wall frames
	// between local capture and emulation apply (D in the design
	// doc).  Valid range 0..32; 3 is the LAN default, 4 the internet
	// default.  Slot chooses the canonical hash-folding order.
	void Begin(Slot slot, uint32_t inputDelay);

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
	// Updates the rolling hash using the inputs that were just
	// applied, possibly flags desync, and bumps CurrentFrame().
	void OnFrameAdvanced();

	// Build the outgoing input packet describing our own last R
	// frames of local input plus the highest peer frame we've
	// received (for acking) and our hash at that frame (for desync
	// detection).  The caller marshals with EncodeInputPacket and
	// sends over Transport.
	void BuildOutgoingInputPacket(NetInputPacket& out) const;

	uint32_t CurrentFrame() const { return mCurrentFrame; }
	uint32_t InputDelay() const { return mInputDelay; }
	Slot     GetSlot() const { return mSlot; }

	bool    IsDesynced()  const { return mDesyncFrame >= 0; }
	int64_t DesyncFrame() const { return mDesyncFrame; }

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

	InputSlot mLocalInputs[kRingSize] = {};
	InputSlot mPeerInputs[kRingSize]  = {};
	HashSlot  mLocalHashes[kRingSize] = {};
	HashSlot  mPeerHashes[kRingSize]  = {};

	Slot     mSlot = Slot::Host;
	uint32_t mInputDelay = 3;
	uint32_t mCurrentFrame = 0;       // next emu frame to apply
	uint64_t mRollingHash = kFnvOffset;

	int64_t  mDesyncFrame = -1;       // -1 = no desync yet
	int64_t  mDesyncInjectFrame = -1; // test hook; -1 = disabled

	uint64_t mLastPeerRecvMs = 0;
	bool     mAnyPeerPacketSeen = false;
};

} // namespace ATNetplay
