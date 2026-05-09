// Altirra SDL3 netplay - lockstep loop (impl)

#include <stdafx.h>

#include "lockstep.h"

#include <cstring>

namespace ATNetplay {

namespace {

constexpr uint32_t kRingMask = LockstepLoop::kRingSize - 1;
static_assert((LockstepLoop::kRingSize & kRingMask) == 0,
              "kRingSize must be a power of two");

} // anonymous

// ---------------------------------------------------------------------------
// Ring getters/putters
// ---------------------------------------------------------------------------
//
// The ring is a direct-mapped table keyed by `frame & mask`.  A slot
// is "occupied for frame F" iff slot.occupied && slot.frame == F.
// Writes overwrite stale entries (occupied but frame mismatch) — if
// the lockstep window is well-sized, that stale slot's frame is far
// enough behind that no one cares about it anymore.

bool LockstepLoop::GetLocalInput(uint32_t frame, NetInput& out) const {
	const InputSlot& s = mLocalInputs[frame & kRingMask];
	if (!s.occupied || s.frame != frame) return false;
	out = s.input;
	return true;
}

bool LockstepLoop::GetPeerInput(uint32_t frame, NetInput& out) const {
	const InputSlot& s = mPeerInputs[frame & kRingMask];
	if (!s.occupied || s.frame != frame) return false;
	out = s.input;
	return true;
}

bool LockstepLoop::GetLocalHash(uint32_t frame, uint32_t& out) const {
	const HashSlot& s = mLocalHashes[frame & kRingMask];
	if (!s.occupied || s.frame != frame) return false;
	out = s.hashLow;
	return true;
}

bool LockstepLoop::GetPeerHash(uint32_t frame, uint32_t& out) const {
	const HashSlot& s = mPeerHashes[frame & kRingMask];
	if (!s.occupied || s.frame != frame) return false;
	out = s.hashLow;
	return true;
}

void LockstepLoop::PutLocalInput(uint32_t frame, const NetInput& in) {
	InputSlot& s = mLocalInputs[frame & kRingMask];
	s.frame = frame;
	s.input = in;
	s.occupied = 1;
}

void LockstepLoop::PutPeerInput(uint32_t frame, const NetInput& in) {
	// First-seen wins: redundant copies of a previously-received
	// frame must not clobber the already-accepted value, in case
	// the sender is retransmitting different state (shouldn't
	// happen, but be defensive).
	InputSlot& s = mPeerInputs[frame & kRingMask];
	if (s.occupied && s.frame == frame) return;
	s.frame = frame;
	s.input = in;
	s.occupied = 1;
}

void LockstepLoop::PutLocalHash(uint32_t frame, uint32_t v) {
	HashSlot& s = mLocalHashes[frame & kRingMask];
	s.frame = frame;
	s.hashLow = v;
	s.occupied = 1;
}

void LockstepLoop::PutPeerHash(uint32_t frame, uint32_t v) {
	HashSlot& s = mPeerHashes[frame & kRingMask];
	// Unlike peer inputs, accept a newer hash for the same frame —
	// the peer may send the same frame's hash multiple times, always
	// with the same value.  First-seen would also be fine.
	s.frame = frame;
	s.hashLow = v;
	s.occupied = 1;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void LockstepLoop::Begin(Slot slot, uint32_t inputDelay) {
	mSlot = slot;
	mInputDelay = inputDelay;
	mCurrentFrame = 0;
	mDesyncFrame = -1;
	// mDesyncInjectFrame left as set by SetDesyncInjectFrame()
	mLastPeerRecvMs = 0;
	mAnyPeerPacketSeen = false;

	// Reset ratchet state — every fresh session starts with no pending
	// switch and a clean streak counter.  Tuning constants stay as
	// last set by SetRatchetTuning() so selftests can configure once
	// and call Begin() repeatedly.
	mDelayUpStreak = 0;
	mPendingDelay = 0;
	mPendingDelaySwitchFrame = 0;

	// memset on the ring arrays would trip -Wclass-memaccess because
	// InputSlot / HashSlot hold a NetInput with its own defaulted
	// members.  Loop-assign instead; compilers collapse it to memset
	// where legal.
	for (uint32_t i = 0; i < kRingSize; ++i) {
		mLocalInputs[i] = InputSlot{};
		mPeerInputs[i]  = InputSlot{};
		mLocalHashes[i] = HashSlot{};
		mPeerHashes[i]  = HashSlot{};
	}

	// Warmup: the first `inputDelay` emu frames have no inputs from
	// either side (nothing was captured `inputDelay` wall frames
	// before the session began).  Seed both sides with zeroed
	// NetInputs so the loop can still advance through the warm-up
	// window rather than stalling forever on frame 0.
	NetInput zero {};
	for (uint32_t f = 0; f < inputDelay && f < kRingSize; ++f) {
		PutLocalInput(f, zero);
		PutPeerInput(f, zero);
	}
}

void LockstepLoop::SubmitLocalInput(const NetInput& local) {
	// Apply any queued D-ratchet AT THE BOUNDARY — i.e. before this
	// tick's capture is keyed.  We MUST do the apply here, not at the
	// end of the previous OnFrameAdvanced, because the previous tick's
	// step-4 ferry uses the resulting mInputDelay: if we change D
	// in-between step 1 and step 4 the slot just captured at step 1
	// (key = currentFrame + D_old) never gets ferried (the post-switch
	// window starts at currentFrame + D_new - 4, ahead of D_old).  See
	// the §11.4 design notes in docs/netplay-architecture.md.
	if (mPendingDelay != 0 && mCurrentFrame >= mPendingDelaySwitchFrame) {
		ApplyDelayRatchet();
	}

	// Capture is keyed at (currentFrame + inputDelay) so the input
	// is ready when emulation reaches that frame `inputDelay` ticks
	// from now.  See design doc §4.
	const uint32_t keyFrame = mCurrentFrame + mInputDelay;
	PutLocalInput(keyFrame, local);
}

void LockstepLoop::OnPeerInputPacket(const NetInputPacket& pkt, uint64_t nowMs) {
	mLastPeerRecvMs = nowMs;
	mAnyPeerPacketSeen = true;

	// Merge sliding-window inputs.
	for (uint16_t i = 0; i < pkt.count && i < (uint16_t)kRedundancyR; ++i) {
		const uint32_t f = pkt.baseFrame + (uint32_t)i;
		PutPeerInput(f, pkt.inputs[i]);
	}

	// Record peer's claimed hash at their ackedFrame.  ackedFrame is
	// uint16 on the wire — for PoC this is fine; a production version
	// widens this when sessions exceed ~18 minutes at 60 Hz.  For
	// now, retro-check using the low 16 bits is consistent with how
	// the peer built the packet.
	if (pkt.stateHashLow32 != 0) {
		const uint32_t f = (uint32_t)pkt.ackedFrame;
		PutPeerHash(f, pkt.stateHashLow32);

		// Retro-check: if we've already applied frame f locally,
		// compare now — the peer's apply-time check wouldn't have
		// fired if we computed our hash after they already had
		// theirs.  Only the receiving side can notice in that case.
		uint32_t mine = 0;
		if (GetLocalHash(f, mine) &&
		    mine != pkt.stateHashLow32 &&
		    mDesyncFrame < 0) {
			mDesyncFrame = (int64_t)f;
		}
	}
}

bool LockstepLoop::CanAdvance() const {
	if (mDesyncFrame >= 0) return false;
	NetInput li, pi;
	return GetLocalInput(mCurrentFrame, li) &&
	       GetPeerInput(mCurrentFrame, pi);
}

bool LockstepLoop::GetInputsForCurrentFrame(NetInput& outP1, NetInput& outP2) const {
	NetInput li, pi;
	if (!GetLocalInput(mCurrentFrame, li)) return false;
	if (!GetPeerInput(mCurrentFrame, pi))  return false;

	// Canonical slot ordering: the host's input is always P1, the
	// joiner's is always P2.  The emulated machine sees the same
	// assignment no matter which peer is "us".
	if (mSlot == Slot::Host) {
		outP1 = li; outP2 = pi;
	} else {
		outP1 = pi; outP2 = li;
	}
	return true;
}

void LockstepLoop::OnFrameAdvanced(uint32_t simStateHash) {
	// Precondition: caller checked CanAdvance() before the sim advance.
	// Violating that would leave our hash stream diverged from the
	// peer's, so bail without bumping rather than baking in permanent
	// divergence.
	NetInput p1 {}, p2 {};
	if (!GetInputsForCurrentFrame(p1, p2)) return;

	// Test hook: flip one bit of the LOCAL hash without touching wire
	// bytes.  Simulates the peer diverging from us at `frame`; lets the
	// selftest exercise the detection path deterministically.
	uint32_t myHashLow = simStateHash;
	if (mDesyncInjectFrame >= 0 && (uint32_t)mDesyncInjectFrame == mCurrentFrame) {
		myHashLow ^= 0x00000001u;
		if (myHashLow == 0) myHashLow = 0x00000002u;   // keep non-zero
	}

	PutLocalHash(mCurrentFrame, myHashLow);

	// Apply-time desync check: did peer's hash for this frame arrive
	// before we applied?
	uint32_t peerHash = 0;
	if (GetPeerHash(mCurrentFrame, peerHash) &&
	    peerHash != myHashLow &&
	    mDesyncFrame < 0) {
		mDesyncFrame = (int64_t)mCurrentFrame;
	}

	// Run the deterministic D-ratchet evaluator on the just-applied
	// frame.  Both peers see the same authoritative rttClass(F) (the
	// host's stamp, delivered via NetInput on the wire) and reach the
	// same scheduling + apply decisions.
	EvaluateDelayRatchet();

	++mCurrentFrame;
}

// ---------------------------------------------------------------------------
// Dynamic input-delay ratchet (v7)
// ---------------------------------------------------------------------------
//
// Both peers fold the host's smoothed-RTT byte into every NetInput it
// captures.  At apply time we read the authoritative rttClass for the
// just-applied frame, map it to a target D, and ratchet up if the
// target has exceeded the current D for kRatchetStreakFrames in a row.
//
// The "when to switch" is computed from the deterministic frame number
// alone — both peers schedule for the next frame where
// (frame % kSwitchModulus) == 0.  No coordination message; identical
// rules on both sides yield identical decisions.
//
// At the switch frame the gap slots [F+D_old, F+D_new) are filled by
// repeating slot F+D_old-1 (the highest captured input from the old
// regime) in BOTH the local and peer rings.  Both peers apply the
// same fill rule, so the rings stay byte-identical.

uint32_t LockstepLoop::TargetDelayFromRttClass(uint8_t cls) {
	// rttMs ≈ cls * 4.  Frames at 60 Hz ≈ rttMs / 16.67.
	// target = 2 (margin) + frames-equivalent of one-way RTT.
	// At cls=0 → target=2, clamped up to kDelayMin=3.
	// At cls=64 (≈256 ms RTT) → target = 2 + 16 = 18 frames (≈300 ms).
	uint32_t target = 2u + ((uint32_t)cls / 4u);
	if (target < kDelayMin) target = kDelayMin;
	if (target > kDelayMax) target = kDelayMax;
	return target;
}

uint8_t LockstepLoop::RttClassFromMs(uint16_t rttMs) {
	const uint32_t bucket = (uint32_t)rttMs / 4u;
	return (uint8_t)(bucket > 255u ? 255u : bucket);
}

uint8_t LockstepLoop::AuthoritativeRttClass(uint32_t frame) const {
	// The host's stamp is on the local ring on the host side, the peer
	// ring on the joiner side — by canonical Slot convention.
	NetInput in;
	if (mSlot == Slot::Host) {
		if (GetLocalInput(frame, in)) return in.rttClass;
	} else {
		if (GetPeerInput(frame, in)) return in.rttClass;
	}
	return 0;  // pre-warmup or stale slot — treat as zero RTT
}

void LockstepLoop::EvaluateDelayRatchet() {
	// Compute the target D from the host's authoritative rttClass for
	// the frame we just applied (mCurrentFrame, before the post-Apply
	// increment).  Both peers run this identically.
	const uint8_t cls = AuthoritativeRttClass(mCurrentFrame);
	const uint32_t target = TargetDelayFromRttClass(cls);

	if (target > mInputDelay) {
		// Target wants a bigger D than we currently have; build streak.
		// Defensive: bound the streak counter (the value past the
		// threshold is unused, but unbounded growth on a long session
		// is sloppy).
		if (mDelayUpStreak < UINT32_MAX) ++mDelayUpStreak;
	} else {
		mDelayUpStreak = 0;
	}

	// Schedule a switch when the streak crosses the threshold AND no
	// switch is already pending.  We only arm one switch at a time —
	// further increases are picked up by the next ratchet round after
	// the pending one applies.
	if (mDelayUpStreak >= mRatchetStreakFrames && mPendingDelay == 0) {
		mPendingDelay = target;
		// Next emu frame F where (F % modulus) == 0, strictly greater
		// than mCurrentFrame.  Adding (modulus - F % modulus) lands on
		// the next boundary; if mCurrentFrame is already a boundary the
		// formula advances by a full modulus so both peers consistently
		// pick the FUTURE boundary, not the present one.
		const uint32_t mod = mRatchetSwitchModulus;
		const uint32_t step = mod - (mCurrentFrame % mod);
		mPendingDelaySwitchFrame = mCurrentFrame + step;
		mDelayUpStreak = 0;
	}

	// Apply is intentionally NOT done here.  See SubmitLocalInput for
	// why the apply must happen at the start of the switch tick (after
	// the previous tick's ferry, before the new tick's capture).
}

void LockstepLoop::ApplyDelayRatchet() {
	// W is the emu frame the current tick will capture for — i.e.
	// mCurrentFrame at the moment the switch fires (in SubmitLocalInput
	// before the capture is keyed).  D_old is the input delay that
	// produced the captures already in the ring; D_new is the target
	// value scheduled by EvaluateDelayRatchet.
	const uint32_t W     = mCurrentFrame;
	const uint32_t D_old = mInputDelay;
	const uint32_t D_new = mPendingDelay;

	// Sanity: ratchet-only.  If something has lowered D between the
	// schedule and now, just discard the pending change.  We never
	// shrink the delay automatically — that would discard already-
	// queued inputs (data loss + desync).
	if (D_new <= D_old) {
		mPendingDelay = 0;
		mPendingDelaySwitchFrame = 0;
		return;
	}

	// Source slot for the gap fill: the highest-keyed input from the
	// pre-switch regime is W + D_old - 1.  After the switch, the next
	// SubmitLocalInput() call (on wall frame W) will key at W + D_new,
	// so slots [W + D_old, W + D_new) would otherwise be empty — the
	// lockstep gate would stall waiting for them.  Repeat the last
	// pre-switch input deterministically into both rings.
	NetInput localFill {};
	NetInput peerFill  {};
	const uint32_t srcFrame = W + D_old - 1;
	GetLocalInput(srcFrame, localFill);
	GetPeerInput(srcFrame, peerFill);

	for (uint32_t f = W + D_old; f < W + D_new; ++f) {
		// Force overwrite even if the slot reports occupied for a
		// different frame number (the ring direct-maps on
		// frame & kRingMask, so we can't trust the existing slot
		// content for these forward frames).
		PutLocalInput(f, localFill);
		PutPeerInput(f, peerFill);
	}

	mInputDelay = D_new;
	mPendingDelay = 0;
	mPendingDelaySwitchFrame = 0;
}

void LockstepLoop::BuildOutgoingInputPacket(NetInputPacket& out) const {
	out = NetInputPacket{};
	out.magic = kMagicInput;

	// Sliding window of the last R LOCAL inputs we've captured.  With
	// input delay D, the highest local key is currentFrame + D; the
	// window ends there and extends back R-1 frames (clamped at 0).
	// Count reflects the number of actually-populated slots so the
	// peer's first-seen dedup doesn't lock in zeros for frames we
	// haven't captured for yet.
	const uint32_t windowEndExcl = mCurrentFrame + mInputDelay + 1;
	const uint32_t windowStart =
		(windowEndExcl >= (uint32_t)kRedundancyR)
			? (windowEndExcl - (uint32_t)kRedundancyR)
			: 0;

	out.baseFrame = windowStart;
	uint16_t count = 0;
	for (uint32_t f = windowStart;
	     f < windowEndExcl && count < (uint16_t)kRedundancyR; ++f) {
		NetInput in;
		if (GetLocalInput(f, in)) {
			out.inputs[count] = in;
			++count;
		}
	}
	out.count = count;

	// ackedFrame is the highest frame we've APPLIED locally, not the
	// highest peer-input frame we hold.  Two reasons:
	//   (1) we're guaranteed to have a hash for a frame we've
	//       applied — sending a frame number we don't have a hash
	//       for defeats the whole point of the ack field, and
	//   (2) the peer uses this to do retro-check: they compare their
	//       own hash at `ackedFrame` against ours.  They can only do
	//       that if they've also applied it.  In lockstep the two
	//       "highest applied" values differ by at most one frame,
	//       guaranteeing the check can fire.
	if (mCurrentFrame > 0) {
		const uint32_t applied = mCurrentFrame - 1;
		out.ackedFrame = (uint16_t)(applied & 0xFFFF);
		uint32_t h = 0;
		if (GetLocalHash(applied, h)) {
			out.stateHashLow32 = h;
		}
	}
}

void LockstepLoop::ResumeAt(uint32_t resumeFrame, uint32_t seedHash) {
	// Wipe every ring — the pre-resync timeline no longer describes the
	// state we are about to run from.
	for (uint32_t i = 0; i < kRingSize; ++i) {
		mLocalInputs[i] = InputSlot{};
		mPeerInputs[i]  = InputSlot{};
		mLocalHashes[i] = HashSlot{};
		mPeerHashes[i]  = HashSlot{};
	}

	mCurrentFrame = resumeFrame;
	mDesyncFrame = -1;
	// Keep peer-silence watchdog state — packet flow continued during
	// the resync (host sent ResyncStart/chunks, joiner sent ResyncDone)
	// so LastPeerRecvMs is still meaningful.

	// Reset ratchet state.  The pre-resync streak and pending-switch
	// frame referred to the now-discarded timeline; the host's RTT
	// might also have changed during the resync gap.  mInputDelay
	// itself is preserved — both peers re-enter lockstep with the
	// value they were running before the resync.
	mDelayUpStreak = 0;
	mPendingDelay = 0;
	mPendingDelaySwitchFrame = 0;

	// Warmup: seed `inputDelay` frames of zeroed inputs so the first
	// Advance() can run without stalling.  Mirrors Begin() semantics.
	NetInput zero {};
	for (uint32_t k = 0; k < mInputDelay && k < kRingSize; ++k) {
		PutLocalInput(resumeFrame + k, zero);
		PutPeerInput(resumeFrame + k, zero);
	}

	// Seed the hash at (resumeFrame - 1) so BuildOutgoingInputPacket
	// has a valid ackedFrame/hash pair for its first post-resync emit.
	// If resumeFrame is 0 (pathological — we'd never resync that early)
	// skip the seed.
	if (resumeFrame > 0) {
		PutLocalHash(resumeFrame - 1, seedHash);
	}
}

bool LockstepLoop::PeerTimedOut(uint64_t nowMs, uint64_t timeoutMs) const {
	if (!mAnyPeerPacketSeen) return false;
	return nowMs > mLastPeerRecvMs && (nowMs - mLastPeerRecvMs) > timeoutMs;
}

} // namespace ATNetplay
