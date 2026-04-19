// Altirra SDL3 netplay - packet encode/decode (impl)

#include <stdafx.h>

#include "protocol.h"

#include <cstring>

namespace ATNetplay {

// ---------------------------------------------------------------------------
// Little-endian byte I/O
// ---------------------------------------------------------------------------
//
// We hand-roll these rather than memcpy-and-cast so the code compiles
// and behaves identically on big-endian hosts.  The Altirra build
// targets are all little-endian today, but the cost is zero and the
// correctness guarantee is worth it.

namespace {

inline void put_u16(uint8_t* p, uint16_t v) {
	p[0] = (uint8_t)(v & 0xFF);
	p[1] = (uint8_t)((v >> 8) & 0xFF);
}

inline void put_u32(uint8_t* p, uint32_t v) {
	p[0] = (uint8_t)(v & 0xFF);
	p[1] = (uint8_t)((v >> 8) & 0xFF);
	p[2] = (uint8_t)((v >> 16) & 0xFF);
	p[3] = (uint8_t)((v >> 24) & 0xFF);
}

inline void put_u64(uint8_t* p, uint64_t v) {
	put_u32(p, (uint32_t)(v & 0xFFFFFFFFu));
	put_u32(p + 4, (uint32_t)((v >> 32) & 0xFFFFFFFFu));
}

inline uint16_t get_u16(const uint8_t* p) {
	return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

inline uint32_t get_u32(const uint8_t* p) {
	return ((uint32_t)p[0])
	     | ((uint32_t)p[1] << 8)
	     | ((uint32_t)p[2] << 16)
	     | ((uint32_t)p[3] << 24);
}

inline uint64_t get_u64(const uint8_t* p) {
	return ((uint64_t)get_u32(p)) | (((uint64_t)get_u32(p + 4)) << 32);
}

} // anonymous

// ---------------------------------------------------------------------------
// PeekMagic
// ---------------------------------------------------------------------------

bool PeekMagic(const uint8_t* buf, size_t len, uint32_t& outMagic) {
	if (len < 4) return false;
	outMagic = get_u32(buf);
	return true;
}

// ---------------------------------------------------------------------------
// NetHello (90 bytes at v2)
// ---------------------------------------------------------------------------

size_t EncodeHello(const NetHello& h, uint8_t* buf, size_t bufSize) {
	if (bufSize < kWireHelloSize) return 0;
	put_u32(buf + 0, kMagicHello);
	put_u16(buf + 4, h.protocolVersion);
	put_u16(buf + 6, h.flags);
	std::memcpy(buf + 8, h.sessionNonce, kSessionNonceLen);        // 8..24
	put_u64(buf + 24, h.osRomHash);                                 // 24..32
	put_u64(buf + 32, h.basicRomHash);                              // 32..40
	std::memcpy(buf + 40, h.playerHandle, kHandleLen);              // 40..72
	put_u16(buf + 40 + kHandleLen, h.acceptTos);                    // 72..74
	std::memcpy(buf + 40 + kHandleLen + 2, h.entryCodeHash, kEntryCodeHashLen); // 74..90
	return kWireHelloSize;
}

DecodeResult DecodeHello(const uint8_t* buf, size_t len, NetHello& out) {
	if (len < kWireHelloSize) return DecodeResult::TooShort;
	out.magic = get_u32(buf);
	if (out.magic != kMagicHello) return DecodeResult::BadMagic;
	out.protocolVersion = get_u16(buf + 4);
	out.flags = get_u16(buf + 6);
	std::memcpy(out.sessionNonce, buf + 8, kSessionNonceLen);
	out.osRomHash = get_u64(buf + 24);
	out.basicRomHash = get_u64(buf + 32);
	std::memcpy(out.playerHandle, buf + 40, kHandleLen);
	out.acceptTos = get_u16(buf + 40 + kHandleLen);
	std::memcpy(out.entryCodeHash, buf + 40 + kHandleLen + 2, kEntryCodeHashLen);
	return DecodeResult::Ok;
}

// ---------------------------------------------------------------------------
// NetWelcome (88 bytes)
// ---------------------------------------------------------------------------

size_t EncodeWelcome(const NetWelcome& w, uint8_t* buf, size_t bufSize) {
	if (bufSize < kWireWelcomeSize) return 0;
	put_u32(buf + 0, kMagicWelcome);
	put_u16(buf + 4, w.inputDelayFrames);
	put_u16(buf + 6, w.playerSlot);
	std::memcpy(buf + 8, w.cartName, kCartLen);                     // 8..72
	put_u32(buf + 8 + kCartLen, w.snapshotBytes);                   // 72..76
	put_u32(buf + 8 + kCartLen + 4, w.snapshotChunks);              // 76..80
	put_u64(buf + 8 + kCartLen + 8, w.settingsHash);                // 80..88
	return kWireWelcomeSize;
}

DecodeResult DecodeWelcome(const uint8_t* buf, size_t len, NetWelcome& out) {
	if (len < kWireWelcomeSize) return DecodeResult::TooShort;
	out.magic = get_u32(buf);
	if (out.magic != kMagicWelcome) return DecodeResult::BadMagic;
	out.inputDelayFrames = get_u16(buf + 4);
	out.playerSlot = get_u16(buf + 6);
	std::memcpy(out.cartName, buf + 8, kCartLen);
	out.snapshotBytes = get_u32(buf + 8 + kCartLen);
	out.snapshotChunks = get_u32(buf + 8 + kCartLen + 4);
	out.settingsHash = get_u64(buf + 8 + kCartLen + 8);
	return DecodeResult::Ok;
}

// ---------------------------------------------------------------------------
// NetReject (8 bytes)
// ---------------------------------------------------------------------------

size_t EncodeReject(const NetReject& r, uint8_t* buf, size_t bufSize) {
	if (bufSize < kWireRejectSize) return 0;
	put_u32(buf + 0, kMagicReject);
	put_u32(buf + 4, r.reason);
	return kWireRejectSize;
}

DecodeResult DecodeReject(const uint8_t* buf, size_t len, NetReject& out) {
	if (len < kWireRejectSize) return DecodeResult::TooShort;
	out.magic = get_u32(buf);
	if (out.magic != kMagicReject) return DecodeResult::BadMagic;
	out.reason = get_u32(buf + 4);
	return DecodeResult::Ok;
}

// ---------------------------------------------------------------------------
// NetInputPacket (36 bytes at R=5)
// ---------------------------------------------------------------------------

size_t EncodeInputPacket(const NetInputPacket& p, uint8_t* buf, size_t bufSize) {
	if (bufSize < kWireInputPktSize) return 0;
	// Fail fast rather than emit a packet the peer will reject.
	if (p.count > (uint16_t)kRedundancyR) return 0;
	put_u32(buf + 0, kMagicInput);
	put_u32(buf + 4, p.baseFrame);
	put_u16(buf + 8, p.count);
	put_u16(buf + 10, p.ackedFrame);
	put_u32(buf + 12, p.stateHashLow32);
	uint8_t* off = buf + 16;
	for (int i = 0; i < kRedundancyR; ++i) {
		off[0] = p.inputs[i].stickDir;
		off[1] = p.inputs[i].buttons;
		off[2] = p.inputs[i].keyScan;
		off[3] = p.inputs[i].extFlags;
		off += kWireInputSize;
	}
	return kWireInputPktSize;
}

DecodeResult DecodeInputPacket(const uint8_t* buf, size_t len, NetInputPacket& out) {
	if (len < kWireInputPktSize) return DecodeResult::TooShort;
	out.magic = get_u32(buf);
	if (out.magic != kMagicInput) return DecodeResult::BadMagic;
	out.baseFrame = get_u32(buf + 4);
	out.count = get_u16(buf + 8);
	out.ackedFrame = get_u16(buf + 10);
	out.stateHashLow32 = get_u32(buf + 12);
	if (out.count > (uint16_t)kRedundancyR) return DecodeResult::BadSize;
	const uint8_t* off = buf + 16;
	for (int i = 0; i < kRedundancyR; ++i) {
		out.inputs[i].stickDir = off[0];
		out.inputs[i].buttons = off[1];
		out.inputs[i].keyScan = off[2];
		out.inputs[i].extFlags = off[3];
		off += kWireInputSize;
	}
	return DecodeResult::Ok;
}

// ---------------------------------------------------------------------------
// NetBye (8 bytes)
// ---------------------------------------------------------------------------

size_t EncodeBye(const NetBye& b, uint8_t* buf, size_t bufSize) {
	if (bufSize < kWireByeSize) return 0;
	put_u32(buf + 0, kMagicBye);
	put_u32(buf + 4, b.reason);
	return kWireByeSize;
}

DecodeResult DecodeBye(const uint8_t* buf, size_t len, NetBye& out) {
	if (len < kWireByeSize) return DecodeResult::TooShort;
	out.magic = get_u32(buf);
	if (out.magic != kMagicBye) return DecodeResult::BadMagic;
	out.reason = get_u32(buf + 4);
	return DecodeResult::Ok;
}

// ---------------------------------------------------------------------------
// NetSnapChunk (16-byte header + payload)
// ---------------------------------------------------------------------------

size_t EncodeSnapChunk(const NetSnapChunk& c, uint8_t* buf, size_t bufSize) {
	if (c.payloadLen > kSnapshotChunkSize) return 0;
	if (bufSize < kWireChunkHdrSize + c.payloadLen) return 0;
	// Refuse to emit a header that claims N payload bytes without an
	// actual source buffer — otherwise we'd ship whatever is in `buf`
	// over the wire and the receiver would read it as valid payload.
	if (c.payloadLen > 0 && c.payload == nullptr) return 0;
	put_u32(buf + 0, kMagicChunk);
	put_u32(buf + 4, c.chunkIdx);
	put_u32(buf + 8, c.totalChunks);
	put_u32(buf + 12, c.payloadLen);
	if (c.payloadLen > 0) {
		std::memcpy(buf + 16, c.payload, c.payloadLen);
	}
	return kWireChunkHdrSize + c.payloadLen;
}

DecodeResult DecodeSnapChunk(const uint8_t* buf, size_t len, NetSnapChunk& out) {
	if (len < kWireChunkHdrSize) return DecodeResult::TooShort;
	out.magic = get_u32(buf);
	if (out.magic != kMagicChunk) return DecodeResult::BadMagic;
	out.chunkIdx = get_u32(buf + 4);
	out.totalChunks = get_u32(buf + 8);
	out.payloadLen = get_u32(buf + 12);
	if (out.payloadLen > kSnapshotChunkSize) return DecodeResult::BadSize;
	if (len < kWireChunkHdrSize + out.payloadLen) return DecodeResult::TooShort;
	out.payload = (out.payloadLen > 0) ? (buf + kWireChunkHdrSize) : nullptr;
	return DecodeResult::Ok;
}

// ---------------------------------------------------------------------------
// NetSnapAck (8 bytes)
// ---------------------------------------------------------------------------

size_t EncodeSnapAck(const NetSnapAck& a, uint8_t* buf, size_t bufSize) {
	if (bufSize < kWireAckSize) return 0;
	put_u32(buf + 0, kMagicAck);
	put_u32(buf + 4, a.chunkIdx);
	return kWireAckSize;
}

DecodeResult DecodeSnapAck(const uint8_t* buf, size_t len, NetSnapAck& out) {
	if (len < kWireAckSize) return DecodeResult::TooShort;
	out.magic = get_u32(buf);
	if (out.magic != kMagicAck) return DecodeResult::BadMagic;
	out.chunkIdx = get_u32(buf + 4);
	return DecodeResult::Ok;
}

// ---------------------------------------------------------------------------
// String helpers
// ---------------------------------------------------------------------------

namespace {

void fixedFromString(const char* s, uint8_t* out, size_t fieldLen) {
	// Zero the whole field first, then copy at most fieldLen-1 bytes so
	// the trailing byte is always NUL.  The effective maximum string
	// length is therefore fieldLen-1 characters (31 for handles, 63 for
	// cart names) — receivers that read the field as a C string via
	// HandleToCStr()/CartToCStr() can rely on termination without
	// explicit bounds checking.
	std::memset(out, 0, fieldLen);
	if (!s || fieldLen == 0) return;
	for (size_t i = 0; i + 1 < fieldLen; ++i) {
		char c = s[i];
		if (c == '\0') return;
		out[i] = (uint8_t)c;
	}
}

const char* fixedToCStr(const uint8_t* in, size_t fieldLen, size_t* outLen) {
	// Scan for a NUL terminator so the caller can treat `in` as a C
	// string.  If the field is fully occupied (no NUL), we still return
	// `in` but report fieldLen as the length — callers that need a
	// guaranteed NUL-terminated copy should memcpy and append '\0'.
	size_t n = fieldLen;
	for (size_t i = 0; i < fieldLen; ++i) {
		if (in[i] == 0) { n = i; break; }
	}
	if (outLen) *outLen = n;
	return reinterpret_cast<const char*>(in);
}

} // anonymous

void HandleFromString(const char* s, uint8_t out[kHandleLen]) {
	fixedFromString(s, out, kHandleLen);
}

void CartFromString(const char* s, uint8_t out[kCartLen]) {
	fixedFromString(s, out, kCartLen);
}

const char* HandleToCStr(const uint8_t in[kHandleLen], size_t* outLen) {
	return fixedToCStr(in, kHandleLen, outLen);
}

const char* CartToCStr(const uint8_t in[kCartLen], size_t* outLen) {
	return fixedToCStr(in, kCartLen, outLen);
}

} // namespace ATNetplay
