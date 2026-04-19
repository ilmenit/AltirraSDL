// Altirra SDL3 netplay - self-test
//
// Compile and run manually — not wired into CMake.  Exists to verify
// the C++ packet encoder round-trips and produces wire sizes that
// match the validated Go PoC.
//
// Build and run (from the repo root), using an empty stdafx.h shim.
// Each command is a single line; line-breaks here are prose only.
//
//   mkdir -p /tmp/netplay_selftest
//   : > /tmp/netplay_selftest/stdafx.h
//   g++ -std=c++17 -I /tmp/netplay_selftest -I src/AltirraSDL/source/netplay   src/AltirraSDL/source/netplay/protocol.cpp   src/AltirraSDL/source/netplay/protocol_selftest.cpp   -o /tmp/netplay_selftest/run
//   /tmp/netplay_selftest/run

#include "protocol.h"

#include <cstdio>
#include <cstring>

using namespace ATNetplay;

static int fails = 0;

#define CHECK(cond) do { \
		if (!(cond)) { \
			std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
			++fails; \
		} \
	} while (0)

static void testWireSizes() {
	CHECK(kWireHelloSize    == 90);
	CHECK(kWireWelcomeSize  == 88);
	CHECK(kWireRejectSize   == 8);
	CHECK(kWireInputPktSize == 36);
	CHECK(kWireByeSize      == 8);
	CHECK(kWireChunkHdrSize == 16);
	CHECK(kWireAckSize      == 8);
	CHECK(kRedundancyR      == 5);
}

static void testMagicAnchors() {
	// Bytes on the wire: 41 4E 50 XX.  Anchored because the magic is
	// part of the spec, not an implementation detail.
	uint8_t buf[kWireByeSize];
	uint32_t m = 0;

	NetBye b; b.reason = 0;
	CHECK(EncodeBye(b, buf, sizeof(buf)) == kWireByeSize);
	CHECK(buf[0] == 'A' && buf[1] == 'N' && buf[2] == 'P' && buf[3] == 'B');

	NetReject r; r.reason = 0;
	CHECK(EncodeReject(r, buf, sizeof(buf)) == kWireRejectSize);
	CHECK(buf[0] == 'A' && buf[1] == 'N' && buf[2] == 'P' && buf[3] == 'R');

	CHECK(PeekMagic(buf, sizeof(buf), m));
	CHECK(m == kMagicReject);
}

static void testHelloRoundTrip() {
	NetHello in;
	in.protocolVersion = 2;
	in.flags = 0x1234;
	for (size_t i = 0; i < kSessionNonceLen; ++i) in.sessionNonce[i] = (uint8_t)(0x10 + i);
	in.osRomHash = 0xDEADBEEFCAFEF00DULL;
	in.basicRomHash = 0x0123456789ABCDEFULL;
	HandleFromString("alice", in.playerHandle);
	in.acceptTos = 1;
	for (size_t i = 0; i < kEntryCodeHashLen; ++i) in.entryCodeHash[i] = (uint8_t)(0xA0 + i);

	uint8_t buf[kMaxDatagramSize];
	size_t n = EncodeHello(in, buf, sizeof(buf));
	CHECK(n == kWireHelloSize);

	NetHello out;
	CHECK(DecodeHello(buf, n, out) == DecodeResult::Ok);
	CHECK(out.magic == kMagicHello);
	CHECK(out.protocolVersion == 2);
	CHECK(out.flags == 0x1234);
	CHECK(std::memcmp(out.sessionNonce, in.sessionNonce, kSessionNonceLen) == 0);
	CHECK(out.osRomHash == 0xDEADBEEFCAFEF00DULL);
	CHECK(out.basicRomHash == 0x0123456789ABCDEFULL);
	size_t hlen = 0;
	CHECK(std::strcmp(HandleToCStr(out.playerHandle, &hlen), "alice") == 0);
	CHECK(hlen == 5);
	CHECK(out.acceptTos == 1);
	CHECK(std::memcmp(out.entryCodeHash, in.entryCodeHash, kEntryCodeHashLen) == 0);
}

static void testWelcomeRoundTrip() {
	NetWelcome in;
	in.inputDelayFrames = 4;
	in.playerSlot = 2;
	CartFromString("Joust.atr", in.cartName);
	in.snapshotBytes = 65536;
	in.snapshotChunks = 55;
	in.settingsHash = 0xABCDEF0011223344ULL;

	uint8_t buf[kMaxDatagramSize];
	size_t n = EncodeWelcome(in, buf, sizeof(buf));
	CHECK(n == kWireWelcomeSize);

	NetWelcome out;
	CHECK(DecodeWelcome(buf, n, out) == DecodeResult::Ok);
	CHECK(out.inputDelayFrames == 4);
	CHECK(out.playerSlot == 2);
	CHECK(std::strcmp(CartToCStr(out.cartName), "Joust.atr") == 0);
	CHECK(out.snapshotBytes == 65536);
	CHECK(out.snapshotChunks == 55);
	CHECK(out.settingsHash == 0xABCDEF0011223344ULL);
}

static void testRejectRoundTrip() {
	NetReject in; in.reason = kRejectBadEntryCode;
	uint8_t buf[kWireRejectSize];
	CHECK(EncodeReject(in, buf, sizeof(buf)) == kWireRejectSize);
	NetReject out;
	CHECK(DecodeReject(buf, kWireRejectSize, out) == DecodeResult::Ok);
	CHECK(out.reason == kRejectBadEntryCode);
}

static void testInputPacketRoundTrip() {
	NetInputPacket in;
	in.baseFrame = 12345;
	in.count = 5;
	in.ackedFrame = 12340;
	in.stateHashLow32 = 0xCAFEBABEu;
	for (int i = 0; i < kRedundancyR; ++i) {
		in.inputs[i].stickDir = (uint8_t)(0x0F & (i + 1));
		in.inputs[i].buttons  = (uint8_t)(0x10 + i);
		in.inputs[i].keyScan  = (uint8_t)(0x20 + i);
		in.inputs[i].extFlags = (uint8_t)(0x30 + i);
	}

	uint8_t buf[kMaxDatagramSize];
	size_t n = EncodeInputPacket(in, buf, sizeof(buf));
	CHECK(n == kWireInputPktSize);

	NetInputPacket out;
	CHECK(DecodeInputPacket(buf, n, out) == DecodeResult::Ok);
	CHECK(out.baseFrame == 12345);
	CHECK(out.count == 5);
	CHECK(out.ackedFrame == 12340);
	CHECK(out.stateHashLow32 == 0xCAFEBABEu);
	for (int i = 0; i < kRedundancyR; ++i) {
		CHECK(out.inputs[i].stickDir == (uint8_t)(0x0F & (i + 1)));
		CHECK(out.inputs[i].buttons  == (uint8_t)(0x10 + i));
		CHECK(out.inputs[i].keyScan  == (uint8_t)(0x20 + i));
		CHECK(out.inputs[i].extFlags == (uint8_t)(0x30 + i));
	}

	// Oversize count is rejected by both encoder and decoder.
	NetInputPacket bad = in;
	bad.count = kRedundancyR + 1;
	uint8_t buf2[kMaxDatagramSize];
	CHECK(EncodeInputPacket(bad, buf2, sizeof(buf2)) == 0);

	// Decoder also rejects a hand-crafted oversize-count packet (covers
	// the case where an encoder on the other end doesn't validate).
	uint8_t handCraft[kWireInputPktSize] = {};
	handCraft[0] = 'A'; handCraft[1] = 'N'; handCraft[2] = 'P'; handCraft[3] = 'I';
	handCraft[8] = (uint8_t)(kRedundancyR + 1);  // count low byte
	NetInputPacket out2;
	CHECK(DecodeInputPacket(handCraft, sizeof(handCraft), out2) == DecodeResult::BadSize);
}

static void testSnapChunkRoundTrip() {
	uint8_t payload[200];
	for (size_t i = 0; i < sizeof(payload); ++i) payload[i] = (uint8_t)(i ^ 0xA5);

	NetSnapChunk in;
	in.chunkIdx = 3;
	in.totalChunks = 100;
	in.payloadLen = sizeof(payload);
	in.payload = payload;

	uint8_t buf[kMaxDatagramSize];
	size_t n = EncodeSnapChunk(in, buf, sizeof(buf));
	CHECK(n == kWireChunkHdrSize + sizeof(payload));

	NetSnapChunk out;
	CHECK(DecodeSnapChunk(buf, n, out) == DecodeResult::Ok);
	CHECK(out.chunkIdx == 3);
	CHECK(out.totalChunks == 100);
	CHECK(out.payloadLen == sizeof(payload));
	CHECK(out.payload != nullptr);
	CHECK(std::memcmp(out.payload, payload, sizeof(payload)) == 0);

	// Encoder refuses oversized payloadLen.
	NetSnapChunk big;
	big.chunkIdx = 0;
	big.totalChunks = 1;
	big.payloadLen = kSnapshotChunkSize + 1;
	big.payload = payload;
	uint8_t overflowBuf[kMaxDatagramSize + 32] = {};
	CHECK(EncodeSnapChunk(big, overflowBuf, sizeof(overflowBuf)) == 0);

	// Encoder refuses non-zero payloadLen with a null source pointer
	// (would otherwise ship uninitialised memory over the wire).
	NetSnapChunk nullPayload;
	nullPayload.chunkIdx = 0;
	nullPayload.totalChunks = 1;
	nullPayload.payloadLen = 32;
	nullPayload.payload = nullptr;
	CHECK(EncodeSnapChunk(nullPayload, overflowBuf, sizeof(overflowBuf)) == 0);

	// Decoder rejects a header whose payloadLen exceeds the limit.
	uint8_t hdr[kWireChunkHdrSize];
	hdr[0] = 'A'; hdr[1] = 'N'; hdr[2] = 'P'; hdr[3] = 'C';
	hdr[4] = 0; hdr[5] = 0; hdr[6] = 0; hdr[7] = 0;
	hdr[8] = 1; hdr[9] = 0; hdr[10] = 0; hdr[11] = 0;
	uint32_t bad = kSnapshotChunkSize + 1;
	hdr[12] = (uint8_t)(bad & 0xFF);
	hdr[13] = (uint8_t)((bad >> 8) & 0xFF);
	hdr[14] = (uint8_t)((bad >> 16) & 0xFF);
	hdr[15] = (uint8_t)((bad >> 24) & 0xFF);
	NetSnapChunk outBad;
	CHECK(DecodeSnapChunk(hdr, sizeof(hdr), outBad) == DecodeResult::BadSize);
}

static void testSnapAckRoundTrip() {
	NetSnapAck in; in.chunkIdx = 42;
	uint8_t buf[kWireAckSize];
	CHECK(EncodeSnapAck(in, buf, sizeof(buf)) == kWireAckSize);
	NetSnapAck out;
	CHECK(DecodeSnapAck(buf, sizeof(buf), out) == DecodeResult::Ok);
	CHECK(out.chunkIdx == 42);
}

static void testByeRoundTrip() {
	NetBye in; in.reason = kByeDesyncDetected;
	uint8_t buf[kWireByeSize];
	CHECK(EncodeBye(in, buf, sizeof(buf)) == kWireByeSize);
	NetBye out;
	CHECK(DecodeBye(buf, sizeof(buf), out) == DecodeResult::Ok);
	CHECK(out.reason == kByeDesyncDetected);
}

static void testStringFieldNulTermination() {
	// A 40-character source must be truncated to kHandleLen-1 (31) with
	// the final byte guaranteed zero, so HandleToCStr is safe even
	// without the caller looking at outLen.
	const char* longName =
		"abcdefghijklmnopqrstuvwxyz0123456789abcd"; // 40 chars, no NUL in first 32
	uint8_t field[kHandleLen];
	HandleFromString(longName, field);
	CHECK(field[kHandleLen - 1] == 0);

	size_t n = 0;
	const char* s = HandleToCStr(field, &n);
	CHECK(n == kHandleLen - 1);           // truncated at 31
	CHECK(std::strlen(s) == kHandleLen - 1);  // safe because of guaranteed NUL
	CHECK(std::memcmp(s, longName, kHandleLen - 1) == 0);

	// CartFromString has the same contract.
	uint8_t cart[kCartLen];
	const char* longCart =
		"this_is_a_very_long_cart_name_that_will_be_truncated_on_the_wire_xxxx"; // >63
	CartFromString(longCart, cart);
	CHECK(cart[kCartLen - 1] == 0);
	CHECK(std::strlen(CartToCStr(cart)) == kCartLen - 1);
}

static void testShortAndBadMagic() {
	// Short input.
	uint8_t tiny[2] = { 0, 0 };
	NetHello h;
	CHECK(DecodeHello(tiny, sizeof(tiny), h) == DecodeResult::TooShort);

	// Bad magic.
	uint8_t buf[kWireRejectSize];
	NetReject r; r.reason = 1;
	EncodeReject(r, buf, sizeof(buf));
	buf[0] = 'Z';  // corrupt first byte of magic
	NetReject out;
	CHECK(DecodeReject(buf, sizeof(buf), out) == DecodeResult::BadMagic);
}

int main() {
	testWireSizes();
	testMagicAnchors();
	testHelloRoundTrip();
	testWelcomeRoundTrip();
	testRejectRoundTrip();
	testInputPacketRoundTrip();
	testSnapChunkRoundTrip();
	testSnapAckRoundTrip();
	testByeRoundTrip();
	testStringFieldNulTermination();
	testShortAndBadMagic();

	if (fails == 0) {
		std::printf("netplay protocol selftest: OK\n");
		return 0;
	}
	std::fprintf(stderr, "netplay protocol selftest: %d FAILURES\n", fails);
	return 1;
}
