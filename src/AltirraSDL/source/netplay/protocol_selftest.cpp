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
	CHECK(kWireHelloSize          == 90);
	CHECK(kWireBootCfgSize        == 36);
	CHECK(kWireWelcomeSize        == 128);   // v5: +4 for snapshotCRC32
	CHECK(kWireWelcomeAckSize     == 4);     // v5: new
	// v6: NetReject reason u32→u16 (8→6); NetBye gains u16 reason (8→6).
	CHECK(kWireRejectSize         == 6);
	CHECK(kWireByeSize            == 6);
	CHECK(kWireInputPktSize       == 41);   // v7: NetInput +1 byte for rttClass
	CHECK(kWireInputSize          == 5);    // v7
	CHECK(kWireChunkHdrSize       == 16);
	CHECK(kWireAckSize            == 8);
	CHECK(kRedundancyR            == 5);
	CHECK(kProtocolVersion        == 7);
	// v6 observability layer.
	CHECK(kWireNetPhaseSize       == 12);
	CHECK(kWireNetEventSize       == 8);
	CHECK(kWireNetHeartbeatSize   == 16);
	CHECK(kWireNetEventBatchHdrSize == 6);
	CHECK(kMaxEventBatchEvents    == 28);
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
	in.protocolVersion = kProtocolVersion;
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
	CHECK(out.protocolVersion == kProtocolVersion);
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
	in.snapshotCRC32 = 0xC0FFEE42u;       // v5
	in.settingsHash = 0xABCDEF0011223344ULL;
	// BootConfig (v4) — only 6 per-game vars + canonical-profile version.
	in.boot.canonicalProfileVersion = 1;
	in.boot.hardwareMode    = 1;          // 800XL
	in.boot.memoryMode      = 7;          // 64K
	in.boot.videoStandard   = 0;          // NTSC
	in.boot.basicEnabled    = 1;
	in.boot.kernelCRC32     = 0x1F9CD270u;
	in.boot.basicCRC32      = 0x7D684184u;
	in.boot.gameFileCRC32   = 0xCAFEBABEu;
	std::memcpy(in.boot.gameExtension, ".atr\0\0\0\0", 8);

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
	CHECK(out.snapshotCRC32 == 0xC0FFEE42u);   // v5
	CHECK(out.settingsHash == 0xABCDEF0011223344ULL);
	CHECK(out.boot.canonicalProfileVersion == 1);
	CHECK(out.boot.hardwareMode    == 1);
	CHECK(out.boot.memoryMode      == 7);
	CHECK(out.boot.videoStandard   == 0);
	CHECK(out.boot.basicEnabled    == 1);
	CHECK(out.boot.kernelCRC32     == 0x1F9CD270u);
	CHECK(out.boot.basicCRC32      == 0x7D684184u);
	CHECK(out.boot.gameFileCRC32   == 0xCAFEBABEu);
	CHECK(std::memcmp(out.boot.gameExtension, ".atr\0\0\0\0", 8) == 0);
}

static void testCrc32KnownVectors() {
	// Standard PKZIP / PNG CRC-32 test vectors.  The netplay-internal
	// Crc32() helper MUST produce identical bytes to VDCRCTable::CRC32
	// because the wire-side host computes the snapshot CRC via the
	// internal helper while older log lines / cache lookups continue
	// to use VDCRCTable.
	CHECK(Crc32(nullptr, 0) == 0u);
	CHECK(Crc32("", 0) == 0u);
	// Standard "123456789" vector: 0xCBF43926.
	CHECK(Crc32("123456789", 9) == 0xCBF43926u);
	// "The quick brown fox jumps over the lazy dog" → 0x414FA339
	CHECK(Crc32("The quick brown fox jumps over the lazy dog", 43)
	      == 0x414FA339u);
	// One-byte 'A' → 0xD3D99E8B
	CHECK(Crc32("A", 1) == 0xD3D99E8Bu);
}

static void testWelcomeAckRoundTrip() {
	NetWelcomeAck in;
	uint8_t buf[kWireWelcomeAckSize];
	size_t n = EncodeWelcomeAck(in, buf, sizeof buf);
	CHECK(n == kWireWelcomeAckSize);
	// Anchor on the wire bytes — magic is 'ANPM' little-endian.
	CHECK(buf[0] == 'A' && buf[1] == 'N' && buf[2] == 'P' && buf[3] == 'M');

	NetWelcomeAck out;
	CHECK(DecodeWelcomeAck(buf, n, out) == DecodeResult::Ok);
	CHECK(out.magic == kMagicWelcomeAck);

	// Short-buffer rejection.
	NetWelcomeAck stub;
	CHECK(DecodeWelcomeAck(buf, 0, stub) == DecodeResult::TooShort);
	// Wrong magic rejection.
	uint8_t bad[kWireWelcomeAckSize];
	std::memcpy(bad, buf, sizeof bad);
	bad[0] = 'X';
	CHECK(DecodeWelcomeAck(bad, sizeof bad, stub) == DecodeResult::BadMagic);
}

static void testRejectRoundTrip() {
	// v6 reason field narrowed to u16; existing kReject* aliases now
	// resolve to SessionTermination raw values.
	NetReject in;
	in.reason = (uint16_t)SessionTermination::BadEntryCode;
	uint8_t buf[kWireRejectSize];
	CHECK(EncodeReject(in, buf, sizeof(buf)) == kWireRejectSize);
	NetReject out;
	CHECK(DecodeReject(buf, kWireRejectSize, out) == DecodeResult::Ok);
	CHECK(out.reason == (uint16_t)SessionTermination::BadEntryCode);

	// v5→v6 forward-compatibility check.  A v5 sender's 8-byte
	// NetReject (4-magic + u32 reason; low byte = SessionTermination
	// numeric) is delivered to a v6 receiver verbatim — recvfrom
	// hands the v6 decoder the full 8 bytes, NOT pre-trimmed to 6.
	// v6's lenient decoder (`if (len < 6) TooShort`) accepts the
	// oversized datagram, reads bytes 0..5, and ignores 6..7.
	// Result: a v5 host's "version skew" Reject still arrives at a
	// v6 joiner as SessionTermination::VersionSkew (numeric 3) so
	// the user sees a friendly "Protocol version mismatch" message
	// rather than a generic timeout.  See the kProtocolVersion
	// comment in packets.h for the full v5↔v6 decode-rule matrix.
	uint8_t v5Buf[8] = { 'A', 'N', 'P', 'R', 0x07, 0, 0, 0 };
	NetReject v5out;
	CHECK(DecodeReject(v5Buf, sizeof(v5Buf), v5out) == DecodeResult::Ok);
	CHECK(v5out.reason == 7);  // BadEntryCode (low byte of v5 u32)

	// New v6 reason values round-trip too.
	NetReject snap;
	snap.reason = (uint16_t)SessionTermination::SnapshotCrcMismatch;
	CHECK(EncodeReject(snap, buf, sizeof(buf)) == kWireRejectSize);
	NetReject snapOut;
	CHECK(DecodeReject(buf, kWireRejectSize, snapOut) == DecodeResult::Ok);
	CHECK(snapOut.reason == 100);
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
		in.inputs[i].rttClass = (uint8_t)(0x40 + i);   // v7
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
		CHECK(out.inputs[i].rttClass == (uint8_t)(0x40 + i));   // v7
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
	// v6 Bye gains a u16 reason (was magic + 4 unused bytes in v5).
	NetBye in;
	in.reason = (uint16_t)SessionTermination::DesyncFlapLimit;
	uint8_t buf[kWireByeSize];
	CHECK(EncodeBye(in, buf, sizeof(buf)) == kWireByeSize);
	NetBye out;
	CHECK(DecodeBye(buf, sizeof(buf), out) == DecodeResult::Ok);
	CHECK(out.reason == (uint16_t)SessionTermination::DesyncFlapLimit);

	// Broker reasons in the v6 namespace.
	NetBye broker;
	broker.reason = (uint16_t)SessionTermination::BrokerHostCanceled;
	CHECK(EncodeBye(broker, buf, sizeof(buf)) == kWireByeSize);
	NetBye brokerOut;
	CHECK(DecodeBye(buf, sizeof(buf), brokerOut) == DecodeResult::Ok);
	CHECK(brokerOut.reason == 502);
}

static void testPhaseRoundTrip() {
	NetPhase in;
	in.phase    = 7;            // arbitrary phase code
	in.flags    = 0x01;         // bit0 set → joiner
	in.progNum  = 1234;
	in.progDen  = 5678;
	in.reserved = 0;
	uint8_t buf[kWireNetPhaseSize];
	CHECK(EncodePhase(in, buf, sizeof(buf)) == kWireNetPhaseSize);
	// Magic anchor: 'A' 'N' 'P' 'F'.
	CHECK(buf[0] == 'A' && buf[1] == 'N'
	      && buf[2] == 'P' && buf[3] == 'F');
	NetPhase out;
	CHECK(DecodePhase(buf, sizeof(buf), out) == DecodeResult::Ok);
	CHECK(out.phase    == 7);
	CHECK(out.flags    == 0x01);
	CHECK(out.progNum  == 1234);
	CHECK(out.progDen  == 5678);
	CHECK(out.reserved == 0);

	// Short / wrong-magic rejection.
	NetPhase shortOut;
	CHECK(DecodePhase(buf, kWireNetPhaseSize - 1, shortOut)
	      == DecodeResult::TooShort);
	uint8_t bad[kWireNetPhaseSize];
	std::memcpy(bad, buf, sizeof bad);
	bad[3] = 'X';
	CHECK(DecodePhase(bad, sizeof bad, shortOut) == DecodeResult::BadMagic);
}

static void testEventBatchRoundTrip() {
	NetEventBatch in;
	in.count = 3;
	in.items[0] = NetEvent{ 100, 1,  10, 0xAABBCCDDu };
	in.items[1] = NetEvent{ 250, 2,  20, 0x11223344u };
	in.items[2] = NetEvent{ 999, 99, 7,  0x00u };

	uint8_t buf[kWireNetEventBatchMaxSize];
	const size_t expectedLen =
		kWireNetEventBatchHdrSize + (size_t)in.count * kWireNetEventSize;
	CHECK(EncodeEventBatch(in, buf, sizeof(buf)) == expectedLen);
	CHECK(buf[0] == 'A' && buf[1] == 'N'
	      && buf[2] == 'P' && buf[3] == 'V');

	NetEventBatch out;
	CHECK(DecodeEventBatch(buf, expectedLen, out) == DecodeResult::Ok);
	CHECK(out.count == 3);
	for (uint8_t i = 0; i < 3; ++i) {
		CHECK(out.items[i].tsOffsetMs == in.items[i].tsOffsetMs);
		CHECK(out.items[i].kind       == in.items[i].kind);
		CHECK(out.items[i].code       == in.items[i].code);
		CHECK(out.items[i].data       == in.items[i].data);
	}

	// Encoder rejects oversize count.
	NetEventBatch big;
	big.count = (uint8_t)(kMaxEventBatchEvents + 1);
	uint8_t bigBuf[kWireNetEventBatchMaxSize + 32] = {};
	CHECK(EncodeEventBatch(big, bigBuf, sizeof(bigBuf)) == 0);

	// Decoder rejects oversize count too (defence against hostile peer).
	uint8_t bad[kWireNetEventBatchMaxSize] = {};
	bad[0] = 'A'; bad[1] = 'N'; bad[2] = 'P'; bad[3] = 'V';
	bad[4] = (uint8_t)(kMaxEventBatchEvents + 1);
	NetEventBatch outBad;
	CHECK(DecodeEventBatch(bad, sizeof(bad), outBad) == DecodeResult::BadSize);
}

static void testHeartbeatRoundTrip() {
	NetHeartbeat in;
	in.rttMs         = 73;
	in.lossPct5s     = 12;
	in.frameSkip5s   = 4;
	in.framesBehind  = 2;
	in.cpuSaturation = 88;
	in.tabVisible    = 1;
	in.seq           = 0xBEEF;
	in.wallMsLow     = 0x1234;

	uint8_t buf[kWireNetHeartbeatSize];
	CHECK(EncodeHeartbeat(in, buf, sizeof(buf)) == kWireNetHeartbeatSize);
	CHECK(buf[0] == 'A' && buf[1] == 'N'
	      && buf[2] == 'P' && buf[3] == 'T');

	NetHeartbeat out;
	CHECK(DecodeHeartbeat(buf, sizeof(buf), out) == DecodeResult::Ok);
	CHECK(out.rttMs         == 73);
	CHECK(out.lossPct5s     == 12);
	CHECK(out.frameSkip5s   == 4);
	CHECK(out.framesBehind  == 2);
	CHECK(out.cpuSaturation == 88);
	CHECK(out.tabVisible    == 1);
	CHECK(out.seq           == 0xBEEF);
	CHECK(out.wallMsLow     == 0x1234);

	// Short / wrong-magic rejection.
	NetHeartbeat shortOut;
	CHECK(DecodeHeartbeat(buf, kWireNetHeartbeatSize - 1, shortOut)
	      == DecodeResult::TooShort);
	uint8_t bad[kWireNetHeartbeatSize];
	std::memcpy(bad, buf, sizeof bad);
	bad[3] = 'X';
	CHECK(DecodeHeartbeat(bad, sizeof bad, shortOut) == DecodeResult::BadMagic);
}

static void testSnapSkipRoundTrip() {
	NetSnapSkip in;
	for (size_t i = 0; i < kSessionNonceLen; ++i)
		in.sessionNonce[i] = (uint8_t)(i * 17u + 3u);
	in.gameFileCRC32 = 0xDEADBEEFu;
	uint8_t buf[kWireSnapSkipSize];
	CHECK(EncodeSnapSkip(in, buf, sizeof(buf)) == kWireSnapSkipSize);
	NetSnapSkip out;
	CHECK(DecodeSnapSkip(buf, sizeof(buf), out) == DecodeResult::Ok);
	CHECK(out.magic == kMagicSnapSkip);
	CHECK(out.gameFileCRC32 == 0xDEADBEEFu);
	CHECK(std::memcmp(out.sessionNonce, in.sessionNonce,
	                  kSessionNonceLen) == 0);

	// Short input is rejected.
	NetSnapSkip out2;
	CHECK(DecodeSnapSkip(buf, kWireSnapSkipSize - 1, out2)
	      == DecodeResult::TooShort);

	// Wrong magic is rejected.
	uint8_t bad[kWireSnapSkipSize];
	std::memcpy(bad, buf, kWireSnapSkipSize);
	bad[0] = 'Z';
	CHECK(DecodeSnapSkip(bad, sizeof(bad), out2)
	      == DecodeResult::BadMagic);
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
	testWelcomeAckRoundTrip();
	testCrc32KnownVectors();
	testRejectRoundTrip();
	testInputPacketRoundTrip();
	testSnapChunkRoundTrip();
	testSnapAckRoundTrip();
	testByeRoundTrip();
	testPhaseRoundTrip();
	testEventBatchRoundTrip();
	testHeartbeatRoundTrip();
	testSnapSkipRoundTrip();
	testStringFieldNulTermination();
	testShortAndBadMagic();

	if (fails == 0) {
		std::printf("netplay protocol selftest: OK\n");
		return 0;
	}
	std::fprintf(stderr, "netplay protocol selftest: %d FAILURES\n", fails);
	return 1;
}
