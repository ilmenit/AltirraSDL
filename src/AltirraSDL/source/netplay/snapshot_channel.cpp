// Altirra SDL3 netplay - snapshot channel (impl)

#include <stdafx.h>

#include "snapshot_channel.h"

#include <algorithm>
#include <cstring>

namespace ATNetplay {

// ---------------------------------------------------------------------------
// SnapshotSender
// ---------------------------------------------------------------------------

void SnapshotSender::Begin(const uint8_t* data, size_t bytes) {
	mData = data;
	mBytes = bytes;
	mLowestUnacked = 0;
	mNextNew = 0;
	mAckedCount = 0;

	if (bytes == 0 || data == nullptr) {
		mTotalChunks = 0;
		mAcked.clear();
		mLastSendMs.clear();
		mAttempts.clear();
		mStatus = Status::Done;   // nothing to send → immediately done
		return;
	}

	mTotalChunks = (uint32_t)((bytes + kSnapshotChunkSize - 1) / kSnapshotChunkSize);
	mAcked.assign(mTotalChunks, 0u);
	mLastSendMs.assign(mTotalChunks, 0ull);
	mAttempts.assign(mTotalChunks, 0u);
	mStatus = Status::Sending;
}

bool SnapshotSender::NextOutgoing(NetSnapChunk& out, uint64_t nowMs) {
	if (mStatus != Status::Sending) return false;

	// Strategy: prefer never-sent chunks at the head of the window
	// (fastest path to fill the pipe).  Fall back to retransmitting
	// the oldest un-acked chunk whose retry timer has expired.
	// Window covers [mLowestUnacked, mLowestUnacked + kWindowSize).
	const uint32_t windowEnd =
		mLowestUnacked + kWindowSize > mTotalChunks
		? mTotalChunks
		: mLowestUnacked + kWindowSize;

	uint32_t pick = mTotalChunks;  // sentinel: nothing to send

	// Pass 1: emit the next never-sent chunk if we have window slack.
	if (mNextNew < windowEnd && mNextNew < mTotalChunks) {
		pick = mNextNew;
	} else {
		// Pass 2: oldest un-acked chunk whose retry timer expired.
		// Only iterate over the active window, not the whole vector.
		for (uint32_t i = mLowestUnacked; i < windowEnd; ++i) {
			if (mAcked[i]) continue;
			if (mLastSendMs[i] == 0) continue; // never-sent (covered above
			                                   //  but possible if mNextNew
			                                   //  hasn't reached i yet —
			                                   //  not in this window-full
			                                   //  branch by construction)
			if ((nowMs - mLastSendMs[i]) < kRetryIntervalMs) continue;
			if (mAttempts[i] >= (uint8_t)kMaxAttemptsPerChunk) {
				// This chunk has burned its retry budget.  Mark the
				// transfer Failed so the coordinator surfaces an
				// error to the user.  Stop after the first such
				// chunk — additional probing won't recover.
				mStatus = Status::Failed;
				return false;
			}
			pick = i;
			break;   // oldest one wins; no need to scan further
		}
	}

	if (pick >= mTotalChunks) return false;

	// Compute byte range.
	const uint64_t start = (uint64_t)pick * (uint64_t)kSnapshotChunkSize;
	uint64_t end = start + (uint64_t)kSnapshotChunkSize;
	if (end > (uint64_t)mBytes) end = (uint64_t)mBytes;
	const uint32_t payloadLen = (uint32_t)(end - start);

	out = NetSnapChunk{};
	out.magic = kMagicChunk;
	out.chunkIdx = pick;
	out.totalChunks = mTotalChunks;
	out.payloadLen = payloadLen;
	out.payload = mData + start;

	mLastSendMs[pick] = nowMs ? nowMs : 1; // 0 is reserved for "never sent"
	if (mAttempts[pick] < 255) ++mAttempts[pick];
	if (pick == mNextNew && mNextNew < mTotalChunks) ++mNextNew;
	return true;
}

void SnapshotSender::OnAckReceived(uint32_t chunkIdx) {
	if (mStatus != Status::Sending) return;
	if (chunkIdx >= mTotalChunks) return;
	if (mAcked[chunkIdx]) return;   // duplicate ACK — ignore

	mAcked[chunkIdx] = 1u;
	++mAckedCount;

	// Slide the trailing edge forward across any contiguous run of
	// ACKed chunks.  A future ACK that fills the gap will trigger
	// another slide.
	while (mLowestUnacked < mTotalChunks && mAcked[mLowestUnacked]) {
		++mLowestUnacked;
	}

	if (mAckedCount >= mTotalChunks) {
		mStatus = Status::Done;
	}
}

void SnapshotSender::ForceDone() {
	if (mStatus != Status::Sending) return;
	// Mark every chunk acked so AcknowledgedChunks() reports the
	// correct progress (used by the host's milestone log).  We do
	// not stamp mLastSendMs / mAttempts — those are only consulted
	// by NextOutgoing, which short-circuits on Status != Sending.
	for (uint32_t i = mLowestUnacked; i < mTotalChunks; ++i) {
		if (!mAcked[i]) {
			mAcked[i] = 1u;
			++mAckedCount;
		}
	}
	mLowestUnacked = mTotalChunks;
	mStatus = Status::Done;
}

// ---------------------------------------------------------------------------
// SnapshotReceiver
// ---------------------------------------------------------------------------

void SnapshotReceiver::Begin(uint32_t expectedChunks, uint32_t expectedBytes) {
	mExpectedChunks = expectedChunks;
	mExpectedBytes = expectedBytes;
	mReceived = 0;
	mData.assign(expectedBytes, 0u);
	mFilled.assign(expectedChunks, 0u);
}

bool SnapshotReceiver::OnChunk(const NetSnapChunk& c) {
	if (mExpectedChunks == 0) return false;
	if (c.chunkIdx >= mExpectedChunks) return false;
	if (c.totalChunks != mExpectedChunks) return false;

	// Where does this chunk land in the reassembly buffer?
	const uint64_t start = (uint64_t)c.chunkIdx * (uint64_t)kSnapshotChunkSize;
	if (start > (uint64_t)mExpectedBytes) return false;
	const uint64_t end = start + (uint64_t)c.payloadLen;
	if (end > (uint64_t)mExpectedBytes) return false;

	// All chunks except the tail must be exactly kSnapshotChunkSize;
	// the tail chunk carries the remainder.  This catches a malformed
	// host that sends short chunks in the middle of a transfer.
	const bool isTail = (c.chunkIdx + 1 == mExpectedChunks);
	if (!isTail && c.payloadLen != kSnapshotChunkSize) return false;

	// A claim-with-no-payload is never valid: either the sender said
	// N bytes and provided a pointer, or neither.  Reject rather than
	// silently accept N zero bytes and count the chunk as "received".
	if (c.payloadLen > 0 && c.payload == nullptr) return false;

	if (mFilled[c.chunkIdx]) return true;   // duplicate — still a valid chunk

	if (c.payloadLen > 0) {
		std::memcpy(mData.data() + start, c.payload, c.payloadLen);
	}
	mFilled[c.chunkIdx] = 1u;
	++mReceived;
	return true;
}

bool SnapshotReceiver::AdoptBytes(const uint8_t* data, size_t len) {
	if (data == nullptr) return false;
	// First call (cache hit before Begin runs): set up state as if
	// Begin(1, len) was called.  Subsequent calls require the size
	// to match what Begin already set.
	if (mExpectedChunks == 0) {
		Begin(1, (uint32_t)len);
	} else if ((uint64_t)len != (uint64_t)mExpectedBytes) {
		return false;
	}
	mData.assign(data, data + len);
	std::fill(mFilled.begin(), mFilled.end(), (uint8_t)1u);
	mReceived = mExpectedChunks;
	return true;
}

} // namespace ATNetplay
