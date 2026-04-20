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

	++mCurrentFrame;
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

bool LockstepLoop::PeerTimedOut(uint64_t nowMs, uint64_t timeoutMs) const {
	if (!mAnyPeerPacketSeen) return false;
	return nowMs > mLastPeerRecvMs && (nowMs - mLastPeerRecvMs) > timeoutMs;
}

} // namespace ATNetplay
