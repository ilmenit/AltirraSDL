// Altirra SDL3 netplay - initial state transfer
//
// State machines for the one-shot snapshot channel.  Two roles:
//
//   SnapshotSender    host-side; chunks a byte buffer and tracks
//                     stop-and-wait ACKs with retries
//   SnapshotReceiver  joiner-side; reassembles chunks and knows when
//                     the full payload has arrived
//
// Both types are intentionally transport-agnostic: they produce and
// consume decoded NetSnapChunk / NetSnapAck values, leaving socket
// I/O to the owning coordinator.  That keeps them driveable in tests
// without a real UDP pair and without any timing wall-clock.
//
// The sender drives retries from a caller-supplied monotonic clock
// (milliseconds since some fixed epoch; only differences are
// compared).  The receiver is stateless w.r.t. time — it trusts the
// caller to re-ACK every valid chunk it hands us, matching the Go
// PoC's handshake.go policy where every decoded chunk prompts an
// ACK regardless of whether it was a duplicate.

#pragma once

#include "packets.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ATNetplay {

// ---------------------------------------------------------------------------
// SnapshotSender — host side
// ---------------------------------------------------------------------------

class SnapshotSender {
public:
	enum class Status {
		Idle,      // no transfer in progress
		Sending,   // chunks in flight, waiting for ACKs
		Done,      // all chunks acknowledged
		Failed,    // exceeded retry budget for some chunk
	};

	// Same policy as the Go PoC: 500 ms per attempt, 5 attempts per
	// chunk before giving up.  Exposed for tests that want to
	// fast-forward.
	static constexpr uint64_t kRetryIntervalMs = 500;
	static constexpr int      kMaxAttemptsPerChunk = 5;

	// Begin a new transfer.  `data` must live for the whole transfer
	// (sender does not copy).  Re-starting mid-transfer is allowed —
	// it discards the previous state.
	void Begin(const uint8_t* data, size_t bytes);

	// Called every main-loop tick.  On return true, `out` describes
	// the chunk to marshal with EncodeSnapChunk() and send over UDP
	// (out.payload points into the caller's original data buffer).
	// On return false, there is nothing to send right now — either
	// waiting for the current chunk's ACK or the transfer finished.
	bool NextOutgoing(NetSnapChunk& out, uint64_t nowMs);

	// Hand in an ACK received from the peer.  Advances to the next
	// chunk when the ACK matches the in-flight index.
	void OnAckReceived(uint32_t chunkIdx);

	Status GetStatus() const { return mStatus; }
	uint32_t TotalChunks() const { return mTotalChunks; }
	// Chunks fully acknowledged so far (progress indicator).
	uint32_t AcknowledgedChunks() const { return mCurrentChunk; }

	uint32_t TotalBytes() const { return (uint32_t)mBytes; }

private:
	const uint8_t* mData = nullptr;
	size_t         mBytes = 0;
	uint32_t       mTotalChunks = 0;

	uint32_t mCurrentChunk = 0;      // index of the chunk we are
	                                 //  currently trying to get ACKed
	int      mAttempts = 0;          // attempts spent on mCurrentChunk
	uint64_t mLastSendMs = 0;
	bool     mSentAtLeastOnce = false;

	Status   mStatus = Status::Idle;
};

// ---------------------------------------------------------------------------
// SnapshotReceiver — joiner side
// ---------------------------------------------------------------------------

class SnapshotReceiver {
public:
	// Prepare to receive an expected total.  Clears any previous
	// buffer.  `expectedBytes` and `expectedChunks` come from the
	// host's NetWelcome.
	void Begin(uint32_t expectedChunks, uint32_t expectedBytes);

	// Merge a decoded chunk into the reassembly buffer.  Returns
	// true when the chunk is consistent with this transfer — new
	// payload bytes were merged OR the chunk was a duplicate of one
	// we already have (legitimate retransmission after a lost ACK).
	// The caller should send a NetSnapAck back to the peer in both
	// those cases.
	//
	// Returns false for structurally unexpected chunks: out-of-range
	// index, mismatching totalChunks, or a bad payload length.  The
	// caller should NOT ACK in this case — such a packet is more
	// likely to be from an unrelated peer (stale session, crossed
	// wires) than our real host, and echoing an ACK back would
	// confuse that peer.
	bool OnChunk(const NetSnapChunk& c);

	bool IsComplete() const { return mReceived == mExpectedChunks && mExpectedChunks > 0; }

	uint32_t ExpectedChunks() const { return mExpectedChunks; }
	uint32_t ReceivedChunks() const { return mReceived; }
	uint32_t ExpectedBytes() const { return mExpectedBytes; }

	// The reassembled payload.  Valid only after IsComplete() turns
	// true; partial data is exposed for diagnostics, not consumption.
	const std::vector<uint8_t>& Data() const { return mData; }

private:
	std::vector<uint8_t> mData;
	std::vector<uint8_t> mFilled;    // 0/1 flags per chunk; bool avoids vector<bool> pitfalls
	uint32_t mExpectedChunks = 0;
	uint32_t mExpectedBytes = 0;
	uint32_t mReceived = 0;
};

} // namespace ATNetplay
