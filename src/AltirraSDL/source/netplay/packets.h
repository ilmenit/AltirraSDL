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
constexpr uint32_t kMagicHello        = 0x4C504E41u; // 'ANPL'
constexpr uint32_t kMagicWelcome      = 0x57504E41u; // 'ANPW'
constexpr uint32_t kMagicReject       = 0x52504E41u; // 'ANPR'
constexpr uint32_t kMagicInput        = 0x49504E41u; // 'ANPI'
constexpr uint32_t kMagicBye          = 0x42504E41u; // 'ANPB'
constexpr uint32_t kMagicChunk        = 0x43504E41u; // 'ANPC'
constexpr uint32_t kMagicAck          = 0x41504E41u; // 'ANPA'
// v3.1: mid-session resync.  ResyncStart (host→joiner) announces the
// incoming savestate and the frame at which both peers will resume;
// the payload then streams through the existing SnapChunk/SnapAck
// channel.  ResyncDone (joiner→host) confirms apply + requests resume.
constexpr uint32_t kMagicResyncStart  = 0x53504E41u; // 'ANPS'
constexpr uint32_t kMagicResyncDone   = 0x44504E41u; // 'ANPD'
// Online-play communication icon ("emote").  Unreliable fire-and-forget;
// a lost packet just means the peer misses one reaction.
constexpr uint32_t kMagicEmote        = 0x45504E41u; // 'ANPE'
// Per-subsystem simulator-state hash breakdown, exchanged on desync
// detection (and on the first lockstep frame of every session) so each
// peer can log which subsystem diverged instead of relying on the user
// to collect and diff two debug logs.  Unreliable one-shot; dropping
// one just means the affected peer logs its own breakdown only.
constexpr uint32_t kMagicSimHashDiag  = 0x48504E41u; // 'ANPH'

// v4 two-sided punch / relay protocol magics.  These live OUTSIDE the
// 'ANP' family so the host's Poll() dispatch can tell a punch-probe
// apart from a Hello (otherwise the spray-in-both-directions would
// read as doubled join attempts), and so the lobby's UDP reflector
// port can distinguish ASDF/ASGR from 'ASDR' probes without
// disturbing the existing stateless reflector path.
//
// kMagicPunch: host → joiner probe to pre-open its own NAT pinhole
//   after it learns a joiner's srflx via the lobby's peer-hint
//   delivery.  Payload: sessionNonce[16].  Joiner silently drops
//   probes whose nonce doesn't match any in-flight join attempt.
//   Distinct from Hello so it is never mis-parsed as a join.
//
// kMagicRelayData:  'ASDF' — sent TO the lobby.  Wraps a whole
//   inner Altirra packet inside a 24-byte header.  The server
//   forwards the inner bytes to the other peer's observed srflx.
//
// kMagicRelayRegister: 'ASGR' — sent ONCE by each peer to the lobby
//   when the direct punch phase times out.  Lets the server learn
//   this peer's reachable srflx and pair it with the other side.
constexpr uint32_t kMagicPunch         = 0x5050414Eu; // 'NAPP'
constexpr uint32_t kMagicRelayData     = 0x46445341u; // 'ASDF'
constexpr uint32_t kMagicRelayRegister = 0x52475341u; // 'ASGR'

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

// NetResyncStart — host → joiner.  Announces a mid-session state
// transfer triggered by a lockstep hash mismatch.  After this the host
// streams the serialized savestate through the existing SnapChunk /
// SnapAck path; joiner reassembles, applies, and returns NetResyncDone
// at which point both peers resume at `resumeFrame`.
//
// `epoch` lets both sides distinguish chunks / acks belonging to this
// resync from any stale ones still in flight from a prior resync in
// the same session.  Incremented by the host on every ResyncStart.
//
// 24 bytes on the wire.
struct NetResyncStart {
	uint32_t magic = kMagicResyncStart;
	uint32_t epoch = 0;           // monotonic per session
	uint32_t stateBytes = 0;      // size of the savestate payload
	uint32_t stateChunks = 0;     // ceil(stateBytes / kSnapshotChunkSize)
	uint32_t resumeFrame = 0;     // frame at which both peers continue
	uint32_t seedHash = 0;        // post-apply sim hash (sanity check only)
};

// NetEmote — either direction.  Fire-and-forget communication icon.
// 8 bytes on the wire (magic + id + 3 reserved bytes for future flags).
struct NetEmote {
	uint32_t magic = kMagicEmote;
	uint8_t  iconId = 0;        // 0..kATEmoteCount-1
	uint8_t  reserved[3] = {};
};

// v4: relay peer role carried in ASGR/ASDF headers so the server can
// pair host and joiner into a single relay entry keyed on sessionId.
enum : uint8_t {
	kRelayRoleHost   = 0,
	kRelayRoleJoiner = 1,
};

// NetPunch — host → joiner probe (20 bytes).  Magic + 16-byte
// session nonce that must match a nonce carried in a NetHello sent
// during the same join attempt, else the joiner drops it.
struct NetPunch {
	uint32_t magic = kMagicPunch;
	uint8_t  sessionNonce[kSessionNonceLen] = {};
};

// NetRelayRegister — peer → lobby (24 bytes).  One-shot packet.
// The server reads the source sockaddr and records it as the
// (sessionId, role) endpoint.  No reply.
struct NetRelayRegister {
	uint32_t magic = kMagicRelayRegister;
	uint8_t  sessionId[16] = {};   // first 16 bytes of UUID (dashes stripped, hex-decoded)
	uint8_t  role = 0;             // kRelayRoleHost / kRelayRoleJoiner
	uint8_t  reserved[3] = {};
};

// NetRelayData — peer → lobby (24-byte header + inner packet).  The
// server looks up the other side's endpoint and forwards the inner
// bytes verbatim.  Inner is an arbitrary Altirra UDP packet (Hello,
// Welcome, Input, Chunk, etc.).  The server does not parse inner.
struct NetRelayDataHeader {
	uint32_t magic = kMagicRelayData;
	uint8_t  sessionId[16] = {};
	uint8_t  role = 0;             // sender's role
	uint8_t  reserved[3] = {};
};

// NetResyncDone — joiner → host.  Confirms the savestate applied
// cleanly; host then exits Resyncing and both peers resume lockstep.
// 12 bytes.
struct NetResyncDone {
	uint32_t magic = kMagicResyncDone;
	uint32_t epoch = 0;
	uint32_t resumeFrame = 0;     // echo of host's resumeFrame
};

// NetSimHashDiag — either direction.  Per-subsystem sim-state hash
// breakdown for `frame`, shipped when a peer has reason to suspect its
// local state has diverged (desync detected, or the first lockstep
// frame of a session).  Receiver compares against its own breakdown
// for the same frame and logs a precise DIFF line naming the
// first-diverging subsystem(s).
//
// 56 bytes.  The subsystem set must stay in sync with
// ATNetplay::SimHashBreakdown — any new field there needs a companion
// field here AND a kProtocolVersion bump (a mismatched receiver would
// silently drop the packet on wire-size check, losing diagnostics).
struct NetSimHashDiag {
	uint32_t magic      = kMagicSimHashDiag;
	uint32_t frame      = 0;        // lockstep frame the breakdown is for
	uint32_t flags      = 0;        // reserved; must be 0
	uint32_t total      = 0;
	uint32_t cpuRegs    = 0;
	uint32_t ramBank0   = 0;
	uint32_t ramBank1   = 0;
	uint32_t ramBank2   = 0;
	uint32_t ramBank3   = 0;
	uint32_t gtiaRegs   = 0;
	uint32_t anticRegs  = 0;
	uint32_t pokeyRegs  = 0;
	uint32_t schedTick  = 0;
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
constexpr size_t kWireResyncStartSize = 24;
constexpr size_t kWireResyncDoneSize  = 12;
constexpr size_t kWireEmoteSize       = 8;
constexpr size_t kWireSimHashDiagSize = 4 * 13;   // 52
constexpr size_t kWirePunchSize         = 4 + kSessionNonceLen;   // 20
constexpr size_t kWireRelayHeaderSize   = 4 + 16 + 1 + 3;         // 24
constexpr size_t kWireRelayRegisterSize = kWireRelayHeaderSize;   // 24

// Maximum UDP datagram we need to send at once; chunks are the biggest.
constexpr size_t kMaxDatagramSize = kWireChunkHdrSize + kSnapshotChunkSize;

// Compile-time guard against NetWelcome layout drift.  The v3 wire
// layout is 88 base bytes + 40 BootConfig bytes = 128.  Anyone adding
// a field to NetBootConfig must also bump kProtocolVersion and update
// EncodeWelcome/DecodeWelcome in protocol.cpp.
static_assert(kWireBootCfgSize == 40, "NetBootConfig wire layout drift");
static_assert(kWireWelcomeSize == 128, "NetWelcome wire layout drift");

// Resync messages: 6 × uint32 and 3 × uint32 respectively.  Guarding
// against silent drift if someone reorders or adds fields to the
// structs above.
static_assert(kWireResyncStartSize == 4 * 6, "NetResyncStart wire drift");
static_assert(kWireResyncDoneSize  == 4 * 3, "NetResyncDone wire drift");
static_assert(kWireSimHashDiagSize == 4 * 13, "NetSimHashDiag wire drift");

} // namespace ATNetplay
