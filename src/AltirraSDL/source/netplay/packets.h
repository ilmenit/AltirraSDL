// Altirra SDL3 netplay - wire format
//
// Fixed-layout, little-endian UDP packets.  Mirrors the Go reference
// implementation in altirra-netplay-poc/protocol/, which has been
// validated end-to-end against itself and the reference lobby server.
//
// See NETPLAY_DESIGN_PLAN.md §5 for the authoritative spec.  Magics
// are ASCII "ANP" + role letter, read as little-endian uint32 — so
// the bytes on the wire always begin with 41 4E 50 XX.
//
// The Go PoC's NetHello did NOT carry an entry-code hash; this C++
// version does (§5.1, §9.3).  Protocol version is therefore bumped
// to 2 — a v1 PoC client and a v2 AltirraSDL client will not
// interoperate, which is intentional.

#pragma once

#include <cstdint>
#include <cstddef>

namespace ATNetplay {

// Magic numbers.  Bytes on the wire: 41 4E 50 XX ('A' 'N' 'P' role).
constexpr uint32_t kMagicHello   = 0x4C504E41u; // 'ANPL'
constexpr uint32_t kMagicWelcome = 0x57504E41u; // 'ANPW'
constexpr uint32_t kMagicReject  = 0x52504E41u; // 'ANPR'
constexpr uint32_t kMagicInput   = 0x49504E41u; // 'ANPI'
constexpr uint32_t kMagicBye     = 0x42504E41u; // 'ANPB'
constexpr uint32_t kMagicChunk   = 0x43504E41u; // 'ANPC'
constexpr uint32_t kMagicAck     = 0x41504E41u; // 'ANPA'

// Protocol constants.
constexpr uint16_t kProtocolVersion   = 2;       // v1 = PoC, v2 adds entryCodeHash
constexpr int      kRedundancyR       = 5;       // sliding input-window length
constexpr int      kFrameHz           = 60;      // simulated emulator frame rate
constexpr uint32_t kSnapshotChunkSize = 1200;    // payload bytes per NetSnapChunk

constexpr size_t kHandleLen       = 32;
constexpr size_t kCartLen         = 64;
constexpr size_t kEntryCodeHashLen = 16;
constexpr size_t kSessionNonceLen  = 16;

// Reject reasons (§5.1).
enum : uint32_t {
	kRejectOsMismatch     = 1,
	kRejectBasicMismatch  = 2,
	kRejectVersionSkew    = 3,
	kRejectTosNotAccept   = 4,
	kRejectHostFull       = 5,
	kRejectHostNotReady   = 6,
	kRejectBadEntryCode   = 7,  // v2: private session code mismatch
	kRejectHostRejected   = 8,  // v2: prompt-me host declined this joiner
};

// Bye reasons.
enum : uint32_t {
	kByeCleanExit        = 1,
	kByeDesyncDetected   = 2,
	kByeTimeout          = 3,
	kByeVersionMismatch  = 4,
};

// --- packet structs --------------------------------------------------------
//
// These are plain data; they do NOT match byte-for-byte memory layout of
// the wire because C++ struct alignment and padding rules differ from the
// on-wire format.  The wire layout lives in protocol.cpp.

// NetHello — joiner → host.  90 bytes on the wire at v2.
struct NetHello {
	uint32_t magic = kMagicHello;
	uint16_t protocolVersion = kProtocolVersion;
	uint16_t flags = 0;
	uint8_t  sessionNonce[kSessionNonceLen] = {};
	uint64_t osRomHash = 0;
	uint64_t basicRomHash = 0;
	uint8_t  playerHandle[kHandleLen] = {};
	uint16_t acceptTos = 0;
	uint8_t  entryCodeHash[kEntryCodeHashLen] = {};  // zeroed for public sessions
};

// NetWelcome — host → joiner (accept).  88 bytes on the wire.
// settingsHash is a placeholder for §8.2 — occupies the wire slot now so
// future additions don't shift field offsets.
struct NetWelcome {
	uint32_t magic = kMagicWelcome;
	uint16_t inputDelayFrames = 3;
	uint16_t playerSlot = 2;       // 2 = first joiner; matches §9.6
	uint8_t  cartName[kCartLen] = {};
	uint32_t snapshotBytes = 0;
	uint32_t snapshotChunks = 0;
	uint64_t settingsHash = 0;     // xxHash64 of netplay-relevant settings
};

// NetReject — host → joiner (refuse).  8 bytes.
struct NetReject {
	uint32_t magic = kMagicReject;
	uint32_t reason = 0;
};

// NetInput — 4 bytes per frame per player.
struct NetInput {
	uint8_t stickDir = 0;  // bits: up|down|left|right (low 4 bits)
	uint8_t buttons  = 0;  // trig|start|select|option|reset
	uint8_t keyScan  = 0;  // ATUIGetScanCodeForVirtualKey() or 0
	uint8_t extFlags = 0;  // bit 0: paddle/5200 extension follows (v2 reserved)
};

// NetInputPacket — 36 bytes at R=5.  Hot path, sent every frame per peer.
struct NetInputPacket {
	uint32_t magic = kMagicInput;
	uint32_t baseFrame = 0;
	uint16_t count = 0;       // ≤ kRedundancyR; unused slots zeroed
	uint16_t ackedFrame = 0;  // highest frame received from peer
	uint32_t stateHashLow32 = 0;
	NetInput inputs[kRedundancyR] = {};
};

// NetBye — disconnect.  8 bytes.
struct NetBye {
	uint32_t magic = kMagicBye;
	uint32_t reason = 0;
};

// NetSnapChunk — chunked snapshot.  16-byte header + variable payload.
struct NetSnapChunk {
	uint32_t magic = kMagicChunk;
	uint32_t chunkIdx = 0;
	uint32_t totalChunks = 0;
	uint32_t payloadLen = 0;
	const uint8_t* payload = nullptr;  // non-owning on encode; set by caller
};

// NetSnapAck — per-chunk ack.  8 bytes.
struct NetSnapAck {
	uint32_t magic = kMagicAck;
	uint32_t chunkIdx = 0;
};

// Wire sizes.  Must match protocol.cpp.
constexpr size_t kWireHelloSize     = 4 + 2 + 2 + kSessionNonceLen + 8 + 8 + kHandleLen + 2 + kEntryCodeHashLen;   // 90
constexpr size_t kWireWelcomeSize   = 4 + 2 + 2 + kCartLen + 4 + 4 + 8;                                           // 88
constexpr size_t kWireRejectSize    = 8;
constexpr size_t kWireInputSize     = 4;
constexpr size_t kWireInputPktSize  = 4 + 4 + 2 + 2 + 4 + kRedundancyR * kWireInputSize;                           // 36 at R=5
constexpr size_t kWireByeSize       = 8;
constexpr size_t kWireChunkHdrSize  = 16;                              // + payloadLen bytes
constexpr size_t kWireAckSize       = 8;

// Maximum UDP datagram we need to send at once; chunks are the biggest.
constexpr size_t kMaxDatagramSize = kWireChunkHdrSize + kSnapshotChunkSize;

} // namespace ATNetplay
