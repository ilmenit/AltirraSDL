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
// v1 = Go PoC, v2 adds entryCodeHash, v3 replaces savestate transfer
// with cold-boot (Welcome carries NetBootConfig; "snapshot" chunks are
// now the raw game-file bytes).
constexpr uint16_t kProtocolVersion   = 3;
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

// NetBootConfig — v3 addition to NetWelcome.  40 bytes on the wire.
// The host ships its MachineConfig + firmware CRCs + master RNG seed
// so the joiner can reproduce a bit-identical cold boot.
struct NetBootConfig {
	uint8_t  hardwareMode    = 0;  // ATHardwareMode enum
	uint8_t  memoryMode      = 0;  // ATMemoryMode enum
	uint8_t  videoStandard   = 0;  // ATVideoStandard enum
	uint8_t  basicEnabled    = 0;  // 0/1
	uint8_t  cpuMode         = 0;  // ATCPUMode enum
	uint8_t  sioAcceleration = 1;  // 0=off, 1=full-speed patch
	uint8_t  reserved0[2]    = {};
	uint32_t kernelCRC32     = 0;  // 0 = no firmware required
	uint32_t basicCRC32      = 0;
	uint32_t masterSeed      = 0;  // SetLockedRandomSeed on both peers
	uint16_t bootFrames      = 0;  // v1: always 0 (user sees boot live)
	uint16_t reserved1       = 0;
	uint32_t gameFileCRC32   = 0;  // for joiner local-cache lookup
	char     gameExtension[8] = {}; // ".xex" / ".atr" / ".car", NUL-pad
	uint8_t  reserved2[4]    = {};
};

// NetWelcome — host → joiner (accept).  128 bytes on the wire at v3
// (88 base + 40 BootConfig).  "snapshot" fields are now the raw
// game-file bytes — framing is unchanged.
struct NetWelcome {
	uint32_t magic = kMagicWelcome;
	uint16_t inputDelayFrames = 3;
	uint16_t playerSlot = 2;       // 2 = first joiner; matches §9.6
	uint8_t  cartName[kCartLen] = {};
	uint32_t snapshotBytes = 0;
	uint32_t snapshotChunks = 0;
	uint64_t settingsHash = 0;     // digest over BootConfig + CRCs
	NetBootConfig boot;
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
constexpr size_t kWireBootCfgSize   = 1 + 1 + 1 + 1 + 1 + 1 + 2 + 4 + 4 + 4 + 2 + 2 + 4 + 8 + 4;                   // 40
constexpr size_t kWireWelcomeSize   = 4 + 2 + 2 + kCartLen + 4 + 4 + 8 + kWireBootCfgSize;                        // 128
constexpr size_t kWireRejectSize    = 8;
constexpr size_t kWireInputSize     = 4;
constexpr size_t kWireInputPktSize  = 4 + 4 + 2 + 2 + 4 + kRedundancyR * kWireInputSize;                           // 36 at R=5
constexpr size_t kWireByeSize       = 8;
constexpr size_t kWireChunkHdrSize  = 16;                              // + payloadLen bytes
constexpr size_t kWireAckSize       = 8;

// Maximum UDP datagram we need to send at once; chunks are the biggest.
constexpr size_t kMaxDatagramSize = kWireChunkHdrSize + kSnapshotChunkSize;

// Compile-time guard against NetWelcome layout drift.  The v3 wire
// layout is 88 base bytes + 40 BootConfig bytes = 128.  Anyone adding
// a field to NetBootConfig must also bump kProtocolVersion and update
// EncodeWelcome/DecodeWelcome in protocol.cpp.
static_assert(kWireBootCfgSize == 40, "NetBootConfig wire layout drift");
static_assert(kWireWelcomeSize == 128, "NetWelcome wire layout drift");

} // namespace ATNetplay
