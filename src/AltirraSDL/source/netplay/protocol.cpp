// Altirra SDL3 netplay - packet encode/decode (impl)

#include <stdafx.h>

#include "protocol.h"

#include <cstring>

namespace ATNetplay {

// ---------------------------------------------------------------------------
// CRC32 (PKZIP / PNG polynomial 0xEDB88320)
// ---------------------------------------------------------------------------
//
// Byte-equal to VDCRCTable::CRC32.CRC() — verified by
// protocol_selftest's testCrc32KnownVectors().  See protocol.h for
// rationale (avoid pulling vd2/system/zip into the standalone tests).

namespace {

// Lazy-initialised slice-by-1 lookup table.  256 entries × 4 B = 1 KiB.
// Built on first use; subsequent calls hit the warm table.
class Crc32Table {
public:
	Crc32Table() {
		for (uint32_t i = 0; i < 256; ++i) {
			uint32_t c = i;
			for (int k = 0; k < 8; ++k) {
				c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
			}
			t[i] = c;
		}
	}
	uint32_t t[256];
};

const Crc32Table& Crc32TableSingleton() {
	static const Crc32Table inst;
	return inst;
}

}  // namespace

uint32_t Crc32(const void* data, size_t len) {
	if (data == nullptr || len == 0) return 0u;
	const uint8_t* p = static_cast<const uint8_t*>(data);
	const uint32_t* tbl = Crc32TableSingleton().t;
	uint32_t c = 0xFFFFFFFFu;
	for (size_t i = 0; i < len; ++i) {
		c = tbl[(c ^ p[i]) & 0xFFu] ^ (c >> 8);
	}
	return c ^ 0xFFFFFFFFu;
}

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
// NetWelcome (128 bytes at v5: 92 base + 36 BootConfig).  v5 added a
// uint32 snapshotCRC32 between snapshotChunks and settingsHash.
// ---------------------------------------------------------------------------

size_t EncodeWelcome(const NetWelcome& w, uint8_t* buf, size_t bufSize) {
	if (bufSize < kWireWelcomeSize) return 0;
	put_u32(buf + 0, kMagicWelcome);
	put_u16(buf + 4, w.inputDelayFrames);
	put_u16(buf + 6, w.playerSlot);
	std::memcpy(buf + 8, w.cartName, kCartLen);                     // 8..72
	put_u32(buf + 8 + kCartLen + 0, w.snapshotBytes);               // 72..76
	put_u32(buf + 8 + kCartLen + 4, w.snapshotChunks);              // 76..80
	put_u32(buf + 8 + kCartLen + 8, w.snapshotCRC32);               // 80..84  (v5)
	put_u64(buf + 8 + kCartLen + 12, w.settingsHash);               // 84..92
	// NetBootConfig (36 bytes, offset 92).
	uint8_t *b = buf + 92;
	put_u16(b + 0, w.boot.canonicalProfileVersion);                 //  0..2
	put_u16(b + 2, w.boot.reserved0);                               //  2..4
	b[4] = w.boot.hardwareMode;                                     //  4
	b[5] = w.boot.memoryMode;                                       //  5
	b[6] = w.boot.videoStandard;                                    //  6
	b[7] = w.boot.basicEnabled;                                     //  7
	put_u32(b +  8, w.boot.kernelCRC32);                            //  8..12
	put_u32(b + 12, w.boot.basicCRC32);                             // 12..16
	put_u32(b + 16, w.boot.gameFileCRC32);                          // 16..20
	std::memcpy(b + 20, w.boot.gameExtension, 8);                   // 20..28
	std::memcpy(b + 28, w.boot.reserved1, 8);                       // 28..36
	return kWireWelcomeSize;
}

DecodeResult DecodeWelcome(const uint8_t* buf, size_t len, NetWelcome& out) {
	if (len < kWireWelcomeSize) return DecodeResult::TooShort;
	out.magic = get_u32(buf);
	if (out.magic != kMagicWelcome) return DecodeResult::BadMagic;
	out.inputDelayFrames = get_u16(buf + 4);
	out.playerSlot = get_u16(buf + 6);
	std::memcpy(out.cartName, buf + 8, kCartLen);
	out.snapshotBytes  = get_u32(buf + 8 + kCartLen + 0);
	out.snapshotChunks = get_u32(buf + 8 + kCartLen + 4);
	out.snapshotCRC32  = get_u32(buf + 8 + kCartLen + 8);
	out.settingsHash   = get_u64(buf + 8 + kCartLen + 12);
	const uint8_t *b = buf + 92;
	out.boot.canonicalProfileVersion = get_u16(b + 0);
	out.boot.reserved0     = get_u16(b + 2);
	out.boot.hardwareMode  = b[4];
	out.boot.memoryMode    = b[5];
	out.boot.videoStandard = b[6];
	out.boot.basicEnabled  = b[7];
	out.boot.kernelCRC32   = get_u32(b +  8);
	out.boot.basicCRC32    = get_u32(b + 12);
	out.boot.gameFileCRC32 = get_u32(b + 16);
	std::memcpy(out.boot.gameExtension, b + 20, 8);
	std::memcpy(out.boot.reserved1, b + 28, 8);
	return DecodeResult::Ok;
}

// ---------------------------------------------------------------------------
// NetWelcomeAck (4 bytes; magic only)
// ---------------------------------------------------------------------------

size_t EncodeWelcomeAck(const NetWelcomeAck& a, uint8_t* buf, size_t bufSize) {
	(void)a;
	if (bufSize < kWireWelcomeAckSize) return 0;
	put_u32(buf + 0, kMagicWelcomeAck);
	return kWireWelcomeAckSize;
}

DecodeResult DecodeWelcomeAck(const uint8_t* buf, size_t len, NetWelcomeAck& out) {
	if (len < kWireWelcomeAckSize) return DecodeResult::TooShort;
	out.magic = get_u32(buf);
	if (out.magic != kMagicWelcomeAck) return DecodeResult::BadMagic;
	return DecodeResult::Ok;
}

// ---------------------------------------------------------------------------
// NetReject (v6: 6 bytes; reason narrowed u32→u16)
// ---------------------------------------------------------------------------

size_t EncodeReject(const NetReject& r, uint8_t* buf, size_t bufSize) {
	if (bufSize < kWireRejectSize) return 0;
	put_u32(buf + 0, kMagicReject);
	put_u16(buf + 4, r.reason);
	return kWireRejectSize;
}

DecodeResult DecodeReject(const uint8_t* buf, size_t len, NetReject& out) {
	if (len < kWireRejectSize) return DecodeResult::TooShort;
	out.magic = get_u32(buf);
	if (out.magic != kMagicReject) return DecodeResult::BadMagic;
	out.reason = get_u16(buf + 4);
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
// NetBye (v6: 6 bytes; reason added — u16; was unused 4-byte tail in v5)
// ---------------------------------------------------------------------------

size_t EncodeBye(const NetBye& b, uint8_t* buf, size_t bufSize) {
	if (bufSize < kWireByeSize) return 0;
	put_u32(buf + 0, kMagicBye);
	put_u16(buf + 4, b.reason);
	return kWireByeSize;
}

DecodeResult DecodeBye(const uint8_t* buf, size_t len, NetBye& out) {
	if (len < kWireByeSize) return DecodeResult::TooShort;
	out.magic = get_u32(buf);
	if (out.magic != kMagicBye) return DecodeResult::BadMagic;
	out.reason = get_u16(buf + 4);
	return DecodeResult::Ok;
}

// ---------------------------------------------------------------------------
// NetPhase (v6 observability — 12 bytes)
// ---------------------------------------------------------------------------

size_t EncodePhase(const NetPhase& p, uint8_t* buf, size_t bufSize) {
	if (bufSize < kWireNetPhaseSize) return 0;
	put_u32(buf + 0,  kMagicNetPhase);
	buf[4] = p.phase;
	buf[5] = p.flags;
	put_u16(buf + 6,  p.progNum);
	put_u16(buf + 8,  p.progDen);
	put_u16(buf + 10, p.reserved);
	return kWireNetPhaseSize;
}

DecodeResult DecodePhase(const uint8_t* buf, size_t len, NetPhase& out) {
	if (len < kWireNetPhaseSize) return DecodeResult::TooShort;
	out.magic = get_u32(buf);
	if (out.magic != kMagicNetPhase) return DecodeResult::BadMagic;
	out.phase    = buf[4];
	out.flags    = buf[5];
	out.progNum  = get_u16(buf + 6);
	out.progDen  = get_u16(buf + 8);
	out.reserved = get_u16(buf + 10);
	return DecodeResult::Ok;
}

// ---------------------------------------------------------------------------
// NetEventBatch (v6 observability — 6-byte header + count × 8 bytes)
// ---------------------------------------------------------------------------

size_t EncodeEventBatch(const NetEventBatch& b, uint8_t* buf, size_t bufSize) {
	if (b.count > kMaxEventBatchEvents) return 0;
	const size_t total = kWireNetEventBatchHdrSize
	                   + (size_t)b.count * kWireNetEventSize;
	if (bufSize < total) return 0;
	put_u32(buf + 0, kMagicNetEvents);
	buf[4] = b.count;
	buf[5] = b.reserved;
	uint8_t* off = buf + kWireNetEventBatchHdrSize;
	for (uint8_t i = 0; i < b.count; ++i) {
		put_u16(off + 0, b.items[i].tsOffsetMs);
		off[2] = b.items[i].kind;
		off[3] = b.items[i].code;
		put_u32(off + 4, b.items[i].data);
		off += kWireNetEventSize;
	}
	return total;
}

DecodeResult DecodeEventBatch(const uint8_t* buf, size_t len, NetEventBatch& out) {
	if (len < kWireNetEventBatchHdrSize) return DecodeResult::TooShort;
	out.magic = get_u32(buf);
	if (out.magic != kMagicNetEvents) return DecodeResult::BadMagic;
	out.count    = buf[4];
	out.reserved = buf[5];
	if (out.count > kMaxEventBatchEvents) return DecodeResult::BadSize;
	const size_t total = kWireNetEventBatchHdrSize
	                   + (size_t)out.count * kWireNetEventSize;
	if (len < total) return DecodeResult::TooShort;
	const uint8_t* off = buf + kWireNetEventBatchHdrSize;
	for (uint8_t i = 0; i < out.count; ++i) {
		out.items[i].tsOffsetMs = get_u16(off + 0);
		out.items[i].kind       = off[2];
		out.items[i].code       = off[3];
		out.items[i].data       = get_u32(off + 4);
		off += kWireNetEventSize;
	}
	// Zero the tail of the items array so the caller sees a clean
	// view (decode is the only path that populates this struct;
	// without this, leftover stack bytes from the caller's NetEventBatch
	// instance would show through on items[count..max-1]).
	for (uint8_t i = out.count; i < (uint8_t)kMaxEventBatchEvents; ++i) {
		out.items[i] = NetEvent{};
	}
	return DecodeResult::Ok;
}

// ---------------------------------------------------------------------------
// NetHeartbeat (v6 observability — 16 bytes)
// ---------------------------------------------------------------------------

size_t EncodeHeartbeat(const NetHeartbeat& h, uint8_t* buf, size_t bufSize) {
	if (bufSize < kWireNetHeartbeatSize) return 0;
	put_u32(buf + 0,  kMagicNetHeartbeat);
	put_u16(buf + 4,  h.rttMs);
	buf[6] = h.lossPct5s;
	buf[7] = h.frameSkip5s;
	put_u16(buf + 8,  h.framesBehind);
	buf[10] = h.cpuSaturation;
	buf[11] = h.tabVisible;
	put_u16(buf + 12, h.seq);
	put_u16(buf + 14, h.wallMsLow);
	return kWireNetHeartbeatSize;
}

DecodeResult DecodeHeartbeat(const uint8_t* buf, size_t len, NetHeartbeat& out) {
	if (len < kWireNetHeartbeatSize) return DecodeResult::TooShort;
	out.magic = get_u32(buf);
	if (out.magic != kMagicNetHeartbeat) return DecodeResult::BadMagic;
	out.rttMs         = get_u16(buf + 4);
	out.lossPct5s     = buf[6];
	out.frameSkip5s   = buf[7];
	out.framesBehind  = get_u16(buf + 8);
	out.cpuSaturation = buf[10];
	out.tabVisible    = buf[11];
	out.seq           = get_u16(buf + 12);
	out.wallMsLow     = get_u16(buf + 14);
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
// NetResyncStart (24 bytes)
// ---------------------------------------------------------------------------

size_t EncodeResyncStart(const NetResyncStart& s, uint8_t* buf, size_t bufSize) {
	if (bufSize < kWireResyncStartSize) return 0;
	put_u32(buf + 0,  kMagicResyncStart);
	put_u32(buf + 4,  s.epoch);
	put_u32(buf + 8,  s.stateBytes);
	put_u32(buf + 12, s.stateChunks);
	put_u32(buf + 16, s.resumeFrame);
	put_u32(buf + 20, s.seedHash);
	return kWireResyncStartSize;
}

DecodeResult DecodeResyncStart(const uint8_t* buf, size_t len, NetResyncStart& out) {
	if (len < kWireResyncStartSize) return DecodeResult::TooShort;
	out.magic = get_u32(buf);
	if (out.magic != kMagicResyncStart) return DecodeResult::BadMagic;
	out.epoch       = get_u32(buf + 4);
	out.stateBytes  = get_u32(buf + 8);
	out.stateChunks = get_u32(buf + 12);
	out.resumeFrame = get_u32(buf + 16);
	out.seedHash    = get_u32(buf + 20);
	return DecodeResult::Ok;
}

// ---------------------------------------------------------------------------
// NetResyncDone (12 bytes)
// ---------------------------------------------------------------------------

size_t EncodeResyncDone(const NetResyncDone& d, uint8_t* buf, size_t bufSize) {
	if (bufSize < kWireResyncDoneSize) return 0;
	put_u32(buf + 0, kMagicResyncDone);
	put_u32(buf + 4, d.epoch);
	put_u32(buf + 8, d.resumeFrame);
	return kWireResyncDoneSize;
}

DecodeResult DecodeResyncDone(const uint8_t* buf, size_t len, NetResyncDone& out) {
	if (len < kWireResyncDoneSize) return DecodeResult::TooShort;
	out.magic = get_u32(buf);
	if (out.magic != kMagicResyncDone) return DecodeResult::BadMagic;
	out.epoch       = get_u32(buf + 4);
	out.resumeFrame = get_u32(buf + 8);
	return DecodeResult::Ok;
}

// ---------------------------------------------------------------------------
// NetSimHashDiag (52 bytes)
// ---------------------------------------------------------------------------

size_t EncodeSimHashDiag(const NetSimHashDiag& d, uint8_t* buf, size_t bufSize) {
	if (bufSize < kWireSimHashDiagSize) return 0;
	put_u32(buf +  0, kMagicSimHashDiag);
	put_u32(buf +  4, d.frame);
	put_u32(buf +  8, d.flags);
	put_u32(buf + 12, d.total);
	put_u32(buf + 16, d.cpuRegs);
	put_u32(buf + 20, d.ramBank0);
	put_u32(buf + 24, d.ramBank1);
	put_u32(buf + 28, d.ramBank2);
	put_u32(buf + 32, d.ramBank3);
	put_u32(buf + 36, d.gtiaRegs);
	put_u32(buf + 40, d.anticRegs);
	put_u32(buf + 44, d.pokeyRegs);
	put_u32(buf + 48, d.schedTick);
	return kWireSimHashDiagSize;
}

DecodeResult DecodeSimHashDiag(const uint8_t* buf, size_t len, NetSimHashDiag& out) {
	if (len < kWireSimHashDiagSize) return DecodeResult::TooShort;
	out.magic = get_u32(buf);
	if (out.magic != kMagicSimHashDiag) return DecodeResult::BadMagic;
	out.frame     = get_u32(buf +  4);
	out.flags     = get_u32(buf +  8);
	out.total     = get_u32(buf + 12);
	out.cpuRegs   = get_u32(buf + 16);
	out.ramBank0  = get_u32(buf + 20);
	out.ramBank1  = get_u32(buf + 24);
	out.ramBank2  = get_u32(buf + 28);
	out.ramBank3  = get_u32(buf + 32);
	out.gtiaRegs  = get_u32(buf + 36);
	out.anticRegs = get_u32(buf + 40);
	out.pokeyRegs = get_u32(buf + 44);
	out.schedTick = get_u32(buf + 48);
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

// ---------------------------------------------------------------------------
// NetEmote (8 bytes)
// ---------------------------------------------------------------------------

size_t EncodeEmote(const NetEmote& e, uint8_t* buf, size_t bufSize) {
	if (bufSize < kWireEmoteSize) return 0;
	put_u32(buf + 0, kMagicEmote);
	buf[4] = e.iconId;
	buf[5] = 0;
	buf[6] = 0;
	buf[7] = 0;
	return kWireEmoteSize;
}

DecodeResult DecodeEmote(const uint8_t* buf, size_t len, NetEmote& out) {
	if (len < kWireEmoteSize) return DecodeResult::TooShort;
	out.magic = get_u32(buf);
	if (out.magic != kMagicEmote) return DecodeResult::BadMagic;
	out.iconId = buf[4];
	out.reserved[0] = buf[5];
	out.reserved[1] = buf[6];
	out.reserved[2] = buf[7];
	return DecodeResult::Ok;
}

// ---------------------------------------------------------------------------
// NetPunch (20 bytes)
// ---------------------------------------------------------------------------

size_t EncodePunch(const NetPunch& p, uint8_t* buf, size_t bufSize) {
	if (bufSize < kWirePunchSize) return 0;
	put_u32(buf + 0, kMagicPunch);
	std::memcpy(buf + 4, p.sessionNonce, kSessionNonceLen);
	return kWirePunchSize;
}

DecodeResult DecodePunch(const uint8_t* buf, size_t len, NetPunch& out) {
	if (len < kWirePunchSize) return DecodeResult::TooShort;
	out.magic = get_u32(buf);
	if (out.magic != kMagicPunch) return DecodeResult::BadMagic;
	std::memcpy(out.sessionNonce, buf + 4, kSessionNonceLen);
	return DecodeResult::Ok;
}

// ---------------------------------------------------------------------------
// NetSnapSkip (24 bytes) — joiner→host cache-hit signal
// ---------------------------------------------------------------------------

size_t EncodeSnapSkip(const NetSnapSkip& s, uint8_t* buf, size_t bufSize) {
	if (bufSize < kWireSnapSkipSize) return 0;
	put_u32(buf + 0, kMagicSnapSkip);
	std::memcpy(buf + 4, s.sessionNonce, kSessionNonceLen);
	put_u32(buf + 4 + kSessionNonceLen, s.gameFileCRC32);
	return kWireSnapSkipSize;
}

DecodeResult DecodeSnapSkip(const uint8_t* buf, size_t len, NetSnapSkip& out) {
	if (len < kWireSnapSkipSize) return DecodeResult::TooShort;
	out.magic = get_u32(buf);
	if (out.magic != kMagicSnapSkip) return DecodeResult::BadMagic;
	std::memcpy(out.sessionNonce, buf + 4, kSessionNonceLen);
	out.gameFileCRC32 = get_u32(buf + 4 + kSessionNonceLen);
	return DecodeResult::Ok;
}

// ---------------------------------------------------------------------------
// NetRelayRegister (24 bytes)
// ---------------------------------------------------------------------------

size_t EncodeRelayRegister(const NetRelayRegister& r,
                           uint8_t* buf, size_t bufSize) {
	if (bufSize < kWireRelayRegisterSize) return 0;
	put_u32(buf + 0, kMagicRelayRegister);
	std::memcpy(buf + 4, r.sessionId, 16);
	buf[20] = r.role;
	buf[21] = buf[22] = buf[23] = 0;
	return kWireRelayRegisterSize;
}

DecodeResult DecodeRelayRegister(const uint8_t* buf, size_t len,
                                 NetRelayRegister& out) {
	if (len < kWireRelayRegisterSize) return DecodeResult::TooShort;
	out.magic = get_u32(buf);
	if (out.magic != kMagicRelayRegister) return DecodeResult::BadMagic;
	std::memcpy(out.sessionId, buf + 4, 16);
	out.role = buf[20];
	out.reserved[0] = buf[21];
	out.reserved[1] = buf[22];
	out.reserved[2] = buf[23];
	return DecodeResult::Ok;
}

// ---------------------------------------------------------------------------
// NetRelayData header + frame (24 bytes + inner)
// ---------------------------------------------------------------------------

size_t EncodeRelayFrame(const NetRelayDataHeader& h,
                        const uint8_t* inner, size_t innerLen,
                        uint8_t* buf, size_t bufSize) {
	if (bufSize < kWireRelayHeaderSize + innerLen) return 0;
	put_u32(buf + 0, kMagicRelayData);
	std::memcpy(buf + 4, h.sessionId, 16);
	buf[20] = h.role;
	buf[21] = buf[22] = buf[23] = 0;
	if (innerLen > 0) {
		std::memcpy(buf + kWireRelayHeaderSize, inner, innerLen);
	}
	return kWireRelayHeaderSize + innerLen;
}

DecodeResult DecodeRelayFrame(const uint8_t* buf, size_t len,
                              NetRelayDataHeader& outHeader,
                              const uint8_t*& outInner,
                              size_t& outInnerLen) {
	if (len < kWireRelayHeaderSize) return DecodeResult::TooShort;
	outHeader.magic = get_u32(buf);
	if (outHeader.magic != kMagicRelayData) return DecodeResult::BadMagic;
	std::memcpy(outHeader.sessionId, buf + 4, 16);
	outHeader.role = buf[20];
	outHeader.reserved[0] = buf[21];
	outHeader.reserved[1] = buf[22];
	outHeader.reserved[2] = buf[23];
	outInner = buf + kWireRelayHeaderSize;
	outInnerLen = len - kWireRelayHeaderSize;
	return DecodeResult::Ok;
}

// ---------------------------------------------------------------------------
// UUID parse → 16 bytes
// ---------------------------------------------------------------------------

bool UuidHexToBytes16(const char* s, uint8_t out[16]) {
	if (!s) return false;
	int nibblesSeen = 0;
	uint8_t acc = 0;
	for (const char* p = s; *p; ++p) {
		char c = *p;
		if (c == '-' || c == ' ' || c == '\t') continue;
		int v;
		if      (c >= '0' && c <= '9') v = c - '0';
		else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
		else if (c >= 'A' && c <= 'F') v = c - 'A' + 10;
		else return false;
		if ((nibblesSeen & 1) == 0) acc = (uint8_t)(v << 4);
		else {
			if (nibblesSeen / 2 >= 16) return false;
			out[nibblesSeen / 2] = (uint8_t)(acc | (uint8_t)v);
		}
		++nibblesSeen;
	}
	return nibblesSeen == 32;
}

} // namespace ATNetplay
