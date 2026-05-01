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

	// Retry policy: 500 ms per attempt, 10 attempts per chunk before
	// giving up.  Bumped from 5 (2.5 s window) to 10 (5 s window)
	// because the 32-chunk sliding window can put many chunks under
	// simultaneous retry pressure during a brief lobby hiccup; with
	// 5 attempts a 1-s lobby blip that drops three retries in a row
	// fails the whole transfer.  10 attempts × 500 ms gives enough
	// headroom that only a sustained ~5 s outage tears down a chunk.
	// Exposed for tests that want to fast-forward.
	static constexpr uint64_t kRetryIntervalMs = 500;
	static constexpr int      kMaxAttemptsPerChunk = 10;

	// Sliding-window size — the maximum number of un-acked chunks
	// allowed in flight simultaneously.  Prior implementation was
	// stop-and-wait (window=1), which forced one full peer round-
	// trip per chunk and dominated session-startup latency on
	// relay-routed traffic (~220 ms × 77 chunks ≈ 17 s for a
	// 92 KB game).  With window=32 and 1200-byte chunks the entire
	// transfer fits in two-three round-trips (~500-700 ms via
	// relay).  The receiver already supports out-of-order delivery
	// (index-based fill in mFilled[]), so no changes there.  A
	// 32-chunk window is ~38 KB of in-flight data — bounded by the
	// kernel UDP send buffer on either side, well below the typical
	// ~200 KB Linux default and the lobby reflector's per-flow
	// allowance.
	static constexpr uint32_t kWindowSize = 32;

	// Begin a new transfer.  `data` must live for the whole transfer
	// (sender does not copy).  Re-starting mid-transfer is allowed —
	// it discards the previous state.
	void Begin(const uint8_t* data, size_t bytes);

	// Called every main-loop tick.  On return true, `out` describes
	// a chunk to marshal with EncodeSnapChunk() and send over UDP
	// (out.payload points into the caller's original data buffer).
	// On return false, there is nothing to send right now — the
	// window is full of in-flight un-acked chunks waiting for ACKs,
	// or the transfer finished.  The caller should call NextOutgoing
	// in a loop until it returns false to flood-fill the window
	// each tick.
	bool NextOutgoing(NetSnapChunk& out, uint64_t nowMs);

	// Hand in an ACK received from the peer.  Marks the chunk acked
	// (out-of-order ACKs are fine) and advances mLowestUnacked
	// until the next un-acked chunk; when mLowestUnacked reaches
	// mTotalChunks the transfer is Done.
	void OnAckReceived(uint32_t chunkIdx);

	// Mark all chunks acknowledged and transition to Done immediately.
	// Used by the netplay-cache "I have it locally" path: the joiner
	// sends NetSnapSkip claiming a CRC32 match, the host validates,
	// and then short-circuits its sender so both sides converge on
	// the existing "snapshot delivered → Lockstepping" path.
	// Idempotent on Done; no-op when not Sending.
	void ForceDone();

	Status GetStatus() const { return mStatus; }
	uint32_t TotalChunks() const { return mTotalChunks; }
	// Chunks fully acknowledged so far (progress indicator).  Same
	// semantics as before: count of chunks that have been ACKed.
	uint32_t AcknowledgedChunks() const { return mAckedCount; }

	uint32_t TotalBytes() const { return (uint32_t)mBytes; }

private:
	const uint8_t* mData = nullptr;
	size_t         mBytes = 0;
	uint32_t       mTotalChunks = 0;

	// Per-chunk bookkeeping — sized to mTotalChunks at Begin().
	std::vector<uint8_t>  mAcked;        // 1 = ACK received
	std::vector<uint64_t> mLastSendMs;   // 0 = never sent yet
	std::vector<uint8_t>  mAttempts;     // up to kMaxAttemptsPerChunk

	uint32_t mLowestUnacked = 0;   // trailing edge: all chunks below
	                               //  this index are ACKed
	uint32_t mNextNew       = 0;   // next never-sent chunk index
	uint32_t mAckedCount    = 0;

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

	// Adopt an externally-provided byte buffer as the fully-received
	// snapshot, bypassing the chunk loop.  Used by the netplay-cache
	// path: joiner found the game in its library or local cache and
	// wants the existing "IsComplete() → Data()" pipeline to work
	// without simulating chunk arrivals.  `len` must equal the
	// `expectedBytes` passed to Begin (or Begin must not have been
	// called yet — this method calls it implicitly with
	// expectedChunks = 1).  Returns true on success.
	bool AdoptBytes(const uint8_t* data, size_t len);

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
