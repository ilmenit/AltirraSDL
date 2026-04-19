// Altirra SDL3 netplay - snapshot channel (impl)

#include <stdafx.h>

#include "snapshot_channel.h"

#include <cstring>

namespace ATNetplay {

// ---------------------------------------------------------------------------
// SnapshotSender
// ---------------------------------------------------------------------------

void SnapshotSender::Begin(const uint8_t* data, size_t bytes) {
	mData = data;
	mBytes = bytes;
	mCurrentChunk = 0;
	mAttempts = 0;
	mLastSendMs = 0;
	mSentAtLeastOnce = false;

	if (bytes == 0 || data == nullptr) {
		mTotalChunks = 0;
		mStatus = Status::Done;   // nothing to send → immediately done
		return;
	}

	mTotalChunks = (uint32_t)((bytes + kSnapshotChunkSize - 1) / kSnapshotChunkSize);
	mStatus = Status::Sending;
}

bool SnapshotSender::NextOutgoing(NetSnapChunk& out, uint64_t nowMs) {
	if (mStatus != Status::Sending) return false;

	// Decide whether it's time to send (or re-send) the current chunk.
	// Two cases fire: the first attempt (never sent before) or the
	// retry timer has elapsed.
	const bool firstAttempt = !mSentAtLeastOnce;
	const bool retryDue = mSentAtLeastOnce &&
	                      (nowMs - mLastSendMs) >= kRetryIntervalMs;
	if (!firstAttempt && !retryDue) return false;

	if (mAttempts >= kMaxAttemptsPerChunk) {
		mStatus = Status::Failed;
		return false;
	}

	// Compute byte range for the current chunk.
	const uint64_t start = (uint64_t)mCurrentChunk * (uint64_t)kSnapshotChunkSize;
	uint64_t end = start + (uint64_t)kSnapshotChunkSize;
	if (end > (uint64_t)mBytes) end = (uint64_t)mBytes;
	const uint32_t payloadLen = (uint32_t)(end - start);

	out = NetSnapChunk{};
	out.magic = kMagicChunk;
	out.chunkIdx = mCurrentChunk;
	out.totalChunks = mTotalChunks;
	out.payloadLen = payloadLen;
	out.payload = mData + start;

	mLastSendMs = nowMs;
	mSentAtLeastOnce = true;
	++mAttempts;
	return true;
}

void SnapshotSender::OnAckReceived(uint32_t chunkIdx) {
	if (mStatus != Status::Sending) return;
	// Stale ACK for a chunk we already moved past — ignore.
	if (chunkIdx != mCurrentChunk) return;

	++mCurrentChunk;
	mAttempts = 0;
	mSentAtLeastOnce = false;

	if (mCurrentChunk >= mTotalChunks) {
		mStatus = Status::Done;
	}
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

} // namespace ATNetplay
