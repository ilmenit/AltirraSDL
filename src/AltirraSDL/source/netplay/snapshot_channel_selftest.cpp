// Altirra SDL3 netplay - snapshot channel self-test
//
// Exercises SnapshotSender and SnapshotReceiver against each other in
// a controlled loop.  No transport; we hand decoded chunks directly
// across the boundary and synthesise the clock.
//
// Build (shim stdafx.h to an empty file):
//
//   mkdir -p /tmp/netplay_selftest
//   : > /tmp/netplay_selftest/stdafx.h
//   g++ -std=c++17 -I /tmp/netplay_selftest -I src/AltirraSDL/source/netplay   src/AltirraSDL/source/netplay/snapshot_channel.cpp   src/AltirraSDL/source/netplay/snapshot_channel_selftest.cpp   -o /tmp/netplay_selftest/snapshot_run
//   /tmp/netplay_selftest/snapshot_run

#include "snapshot_channel.h"

#include <cstdio>
#include <cstring>
#include <vector>

using namespace ATNetplay;

static int fails = 0;

#define CHECK(cond) do { \
		if (!(cond)) { \
			std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
			++fails; \
		} \
	} while (0)

static std::vector<uint8_t> makePattern(size_t n) {
	std::vector<uint8_t> v(n);
	for (size_t i = 0; i < n; ++i) v[i] = (uint8_t)((i * 31u + 7u) & 0xFFu);
	return v;
}

// Ferry a single chunk from sender to receiver and ACK back.
// Advances the clock past the retry interval on each call so the
// sender is always willing to send.
static bool ferryOneChunk(SnapshotSender& tx, SnapshotReceiver& rx,
                          uint64_t& nowMs) {
	NetSnapChunk pkt;
	if (!tx.NextOutgoing(pkt, nowMs)) return false;

	CHECK(rx.OnChunk(pkt));
	tx.OnAckReceived(pkt.chunkIdx);

	nowMs += SnapshotSender::kRetryIntervalMs + 1;
	return true;
}

// -------------------------------------------------------------------
// Cases
// -------------------------------------------------------------------

static void testSinglePerfectTransfer() {
	const size_t kSize = 4096;
	auto data = makePattern(kSize);

	SnapshotSender tx;
	tx.Begin(data.data(), data.size());
	CHECK(tx.GetStatus() == SnapshotSender::Status::Sending);
	CHECK(tx.TotalChunks() ==
	      (uint32_t)((kSize + kSnapshotChunkSize - 1) / kSnapshotChunkSize));

	SnapshotReceiver rx;
	rx.Begin(tx.TotalChunks(), (uint32_t)kSize);
	CHECK(!rx.IsComplete());

	uint64_t now = 1'000'000;
	for (uint32_t i = 0; i < tx.TotalChunks(); ++i) {
		CHECK(ferryOneChunk(tx, rx, now));
	}
	CHECK(tx.GetStatus() == SnapshotSender::Status::Done);
	CHECK(rx.IsComplete());
	CHECK(rx.Data().size() == kSize);
	CHECK(std::memcmp(rx.Data().data(), data.data(), kSize) == 0);

	// After done, NextOutgoing returns false — nothing left to ferry.
	NetSnapChunk extra;
	CHECK(!tx.NextOutgoing(extra, now));
}

static void testEmptyTransfer() {
	SnapshotSender tx;
	tx.Begin(nullptr, 0);
	CHECK(tx.GetStatus() == SnapshotSender::Status::Done);
	CHECK(tx.TotalChunks() == 0);

	NetSnapChunk pkt;
	CHECK(!tx.NextOutgoing(pkt, 0));
}

static void testExactOneChunk() {
	// Exactly kSnapshotChunkSize bytes → 1 chunk, payloadLen == kSnapshotChunkSize.
	auto data = makePattern(kSnapshotChunkSize);
	SnapshotSender tx;
	tx.Begin(data.data(), data.size());
	CHECK(tx.TotalChunks() == 1);

	SnapshotReceiver rx;
	rx.Begin(1, (uint32_t)data.size());
	uint64_t now = 0;
	CHECK(ferryOneChunk(tx, rx, now));
	CHECK(rx.IsComplete());
}

static void testTailChunkIsShort() {
	// kSnapshotChunkSize + 1 bytes → 2 chunks.  Tail is 1 byte.
	const size_t kSize = kSnapshotChunkSize + 1;
	auto data = makePattern(kSize);

	SnapshotSender tx;
	tx.Begin(data.data(), data.size());
	CHECK(tx.TotalChunks() == 2);

	SnapshotReceiver rx;
	rx.Begin(2, (uint32_t)kSize);

	NetSnapChunk first, second;
	CHECK(tx.NextOutgoing(first, 0));
	CHECK(first.payloadLen == kSnapshotChunkSize);
	CHECK(rx.OnChunk(first));
	tx.OnAckReceived(0);

	CHECK(tx.NextOutgoing(second, 1));
	CHECK(second.payloadLen == 1);
	CHECK(rx.OnChunk(second));
	tx.OnAckReceived(1);

	CHECK(tx.GetStatus() == SnapshotSender::Status::Done);
	CHECK(rx.IsComplete());
	CHECK(std::memcmp(rx.Data().data(), data.data(), kSize) == 0);
}

static void testRetryOnDroppedAck() {
	auto data = makePattern(kSnapshotChunkSize * 3);  // 3 chunks
	SnapshotSender tx;
	tx.Begin(data.data(), data.size());

	SnapshotReceiver rx;
	rx.Begin(3, (uint32_t)data.size());

	// Send chunk 0 normally.
	uint64_t now = 100;
	NetSnapChunk pkt;
	CHECK(tx.NextOutgoing(pkt, now));
	CHECK(pkt.chunkIdx == 0);
	rx.OnChunk(pkt);
	tx.OnAckReceived(0);

	// Send chunk 1 but "lose" the ACK — don't call OnAckReceived.
	CHECK(tx.NextOutgoing(pkt, now + 100));
	CHECK(pkt.chunkIdx == 1);
	// Simulate the ACK going missing: receiver saw it, tx did not.
	rx.OnChunk(pkt);

	// Before retry interval elapses, sender has nothing to emit.
	CHECK(!tx.NextOutgoing(pkt, now + 200));

	// Past the retry interval, sender re-emits chunk 1.
	CHECK(tx.NextOutgoing(pkt, now + 100 + SnapshotSender::kRetryIntervalMs + 1));
	CHECK(pkt.chunkIdx == 1);  // still chunk 1
	// Receiver sees a duplicate — still valid, ignored as already-filled.
	CHECK(rx.OnChunk(pkt));
	CHECK(rx.ReceivedChunks() == 2);  // not double-counted

	// This time the ACK gets through.
	tx.OnAckReceived(1);

	// Proceed to chunk 2.
	CHECK(tx.NextOutgoing(pkt, now + 1000));
	CHECK(pkt.chunkIdx == 2);
	rx.OnChunk(pkt);
	tx.OnAckReceived(2);

	CHECK(tx.GetStatus() == SnapshotSender::Status::Done);
	CHECK(rx.IsComplete());
}

static void testFailAfterMaxAttempts() {
	auto data = makePattern(kSnapshotChunkSize);
	SnapshotSender tx;
	tx.Begin(data.data(), data.size());

	uint64_t now = 0;
	NetSnapChunk pkt;
	for (int i = 0; i < SnapshotSender::kMaxAttemptsPerChunk; ++i) {
		CHECK(tx.NextOutgoing(pkt, now));
		now += SnapshotSender::kRetryIntervalMs + 1;
	}
	// One more probe: budget exhausted, sender transitions to Failed.
	CHECK(!tx.NextOutgoing(pkt, now));
	CHECK(tx.GetStatus() == SnapshotSender::Status::Failed);
}

static void testStaleAckIsIgnored() {
	auto data = makePattern(kSnapshotChunkSize * 3);
	SnapshotSender tx;
	tx.Begin(data.data(), data.size());

	NetSnapChunk pkt;
	CHECK(tx.NextOutgoing(pkt, 0));
	tx.OnAckReceived(0);
	CHECK(tx.AcknowledgedChunks() == 1);

	// An ACK for a chunk we already moved past must not advance again.
	tx.OnAckReceived(0);
	CHECK(tx.AcknowledgedChunks() == 1);

	// A wildly out-of-range ACK must not crash or advance.
	tx.OnAckReceived(999);
	CHECK(tx.AcknowledgedChunks() == 1);
}

static void testReceiverRejectsMalformedChunks() {
	SnapshotReceiver rx;
	rx.Begin(3, (uint32_t)(kSnapshotChunkSize * 3));

	// Out-of-range index.
	NetSnapChunk c{};
	c.chunkIdx = 999;
	c.totalChunks = 3;
	c.payloadLen = kSnapshotChunkSize;
	uint8_t dummy[kSnapshotChunkSize] = {};
	c.payload = dummy;
	CHECK(!rx.OnChunk(c));
	CHECK(rx.ReceivedChunks() == 0);

	// Wrong totalChunks.
	c.chunkIdx = 0;
	c.totalChunks = 99;
	CHECK(!rx.OnChunk(c));

	// Short mid-stream chunk.
	c.totalChunks = 3;
	c.chunkIdx = 1;
	c.payloadLen = 100;  // shorter than kSnapshotChunkSize and not the tail
	CHECK(!rx.OnChunk(c));

	// Null-payload chunk with non-zero len must be rejected, not
	// silently accepted as zero bytes.
	c.chunkIdx = 0;
	c.totalChunks = 3;
	c.payloadLen = kSnapshotChunkSize;
	c.payload = nullptr;
	CHECK(!rx.OnChunk(c));
	CHECK(rx.ReceivedChunks() == 0);

	// Good chunk still works after all that abuse.
	c.payload = dummy;
	CHECK(rx.OnChunk(c));
	CHECK(rx.ReceivedChunks() == 1);
}

static void testReceiverDedupsDuplicates() {
	SnapshotReceiver rx;
	rx.Begin(2, (uint32_t)(kSnapshotChunkSize * 2));

	uint8_t dummy[kSnapshotChunkSize] = { 0xA5 };
	NetSnapChunk c{};
	c.totalChunks = 2;
	c.payloadLen = kSnapshotChunkSize;
	c.payload = dummy;

	c.chunkIdx = 0;
	CHECK(rx.OnChunk(c));
	CHECK(rx.ReceivedChunks() == 1);

	// Same chunk arrives again (host retransmitted after lost ACK).
	// It must be accepted (caller still wants to ACK it), but must
	// not re-bump the received count.
	CHECK(rx.OnChunk(c));
	CHECK(rx.ReceivedChunks() == 1);

	c.chunkIdx = 1;
	CHECK(rx.OnChunk(c));
	CHECK(rx.ReceivedChunks() == 2);
	CHECK(rx.IsComplete());
}

int main() {
	testSinglePerfectTransfer();
	testEmptyTransfer();
	testExactOneChunk();
	testTailChunkIsShort();
	testRetryOnDroppedAck();
	testFailAfterMaxAttempts();
	testStaleAckIsIgnored();
	testReceiverRejectsMalformedChunks();
	testReceiverDedupsDuplicates();

	if (fails == 0) {
		std::printf("netplay snapshot-channel selftest: OK\n");
		return 0;
	}
	std::fprintf(stderr, "netplay snapshot-channel selftest: %d FAILURES\n", fails);
	return 1;
}
