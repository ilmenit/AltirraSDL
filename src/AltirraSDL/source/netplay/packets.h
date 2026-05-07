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
// v6 observability layer (M5).  Three new packet families broadcast
// alongside the main wire so peers and the lobby can build a
// bilateral timeline of every Coordinator transition without
// crossing the snapshot/input critical path.
//   ANPS — phase  ("State")
//   ANPV — events ("eVents")
//   ANPT — heartbeat ("Telemetry")
// Letters chosen to avoid collision with existing magics (L,W,R,I,
// B,C,A,D,E,K,M,H,S,P).  Note: 'ANPS' clashed with the prior v5
// kMagicResyncStart — the resync magic stays at 'ANPS' (resync is
// pre-v6 wire-frozen) so we picked 'ANPV' / 'ANPT' for the new
// types and a different letter for phase: 'ANPF' (Frame-state).
constexpr uint32_t kMagicNetPhase     = 0x46504E41u; // 'ANPF'
constexpr uint32_t kMagicNetEvents    = 0x56504E41u; // 'ANPV'
constexpr uint32_t kMagicNetHeartbeat = 0x54504E41u; // 'ANPT'
// v3.1: mid-session resync.  ResyncStart (host→joiner) announces the
// incoming savestate and the frame at which both peers will resume;
// the payload then streams through the existing SnapChunk/SnapAck
// channel.  ResyncDone (joiner→host) confirms apply + requests resume.
constexpr uint32_t kMagicResyncStart  = 0x53504E41u; // 'ANPS'
constexpr uint32_t kMagicResyncDone   = 0x44504E41u; // 'ANPD'
// Online-play communication icon ("emote").  Unreliable fire-and-forget;
// a lost packet just means the peer misses one reaction.
constexpr uint32_t kMagicEmote        = 0x45504E41u; // 'ANPE'
// Joiner → host signal: "I have the game with this CRC32 locally
// (library or netplay cache); skip the chunked snapshot transfer."
// Host responds by jumping its SnapshotSender to Done — the joiner
// uses its local copy as if it had been streamed.  Authenticated via
// session nonce; the gameFileCRC32 must match what the host advertised
// in NetBootConfig.gameFileCRC32 or the host ignores the packet.
constexpr uint32_t kMagicSnapSkip     = 0x4B504E41u; // 'ANPK'
// Joiner → host: "Welcome accepted, I'm in ReceivingSnapshot — start
// shipping chunks now."  Lets the host gate its chunk burst on a real
// signal instead of optimistically firing the entire window in the
// poll tick after Welcome (which created a window where chunks raced
// Welcome on the wire and were silently discarded by a joiner still in
// Phase::Handshaking).  Sender stamps no fields beyond the magic; the
// receive path is matched purely by source endpoint.
constexpr uint32_t kMagicWelcomeAck   = 0x4D504E41u; // 'ANPM'
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
// now the raw game-file bytes), v4 introduces the canonical Netplay
// Session Profile (see ATNetplayProfile in netplay_profile.h): both
// peers force a fixed deterministic configuration for the duration
// of the session and exchange only 6 per-game variables in
// NetBootConfig.  Removed fields (cpuMode, sioAcceleration,
// masterSeed, bootFrames) are now constants pinned by the canonical
// profile.  v5 adds NetWelcome::snapshotCRC32 (joiner verifies the
// assembled snapshot bytes against this before applying — catches
// silent truncation, protocol drift, hostile peer) and the
// kMagicWelcomeAck handshake (joiner signals "ready for chunks" so the
// host's chunk burst is gated on a real ack instead of racing Welcome
// on the wire).  v6 (M4 + M5 release):
//   - Replaces the byte-sized Reject/Bye reason enums with a single
//     SessionTermination : uint16_t covering every terminal-cause
//     code from handshake through transport, broker UX, and
//     internal failures.  Numeric codes for the v5 kReject*/kBye*
//     constants are preserved (so a hex-dump of historical session
//     logs still reads).
//   - Narrows NetReject.reason u32 → u16 and adds a u16 reason to
//     NetBye (was magic-only).  kWireRejectSize and kWireByeSize
//     drop from 8 to 6 bytes — HARD WIRE BREAK from v5: a v5 peer
//     and a v6 peer cannot exchange Reject/Bye, so the version-skew
//     handshake now relies on NetWelcome's protocolVersion check.
//     Mixed-version peers see a clean "decode size mismatch" rather
//     than a silently-wrong reason code.
//   - Adds three new broadcast packet types for observability:
//     NetPhase (12 B, every phase transition + heartbeat),
//     NetEventBatch (variable ≤230 B, fine-grained event stream),
//     NetHeartbeat (16 B, 1-Hz RTT/loss/frame-skip telemetry).
//     Magics 'ANPF' / 'ANPV' / 'ANPT' are above.  Lobby's WS bridge
//     observes these on relay sessions and republishes via SSE so
//     the broker page can render peer status without parsing UDP.
constexpr uint16_t kProtocolVersion   = 6;
constexpr int      kRedundancyR       = 5;       // sliding input-window length
constexpr int      kFrameHz           = 60;      // simulated emulator frame rate
constexpr uint32_t kSnapshotChunkSize = 1200;    // payload bytes per NetSnapChunk

constexpr size_t kHandleLen       = 32;
constexpr size_t kCartLen         = 64;
constexpr size_t kEntryCodeHashLen = 16;
constexpr size_t kSessionNonceLen  = 16;

// Session termination reasons (v6).  Replaces the v5 kReject* (u32)
// and kBye* (u32) byte-sized enums with one strongly-typed family
// covering every terminal-cause code.  Carried on the wire as
// uint16_t in NetReject.reason and NetBye.reason.  Numeric values
// for the v5 kReject* constants (1–9) are preserved; the v5 kBye*
// constants are remapped into the new namespace (legacy hex dumps
// of v5 sessions read against the historical kBye* table).
//
// Code layout (chosen to leave gaps for future expansion):
//   0          — None / no reason
//   1–99       — handshake (negotiated reasons during Hello/Welcome)
//   100–199    — snapshot delivery
//   200–299    — mid-session
//   300–399    — transport
//   400–499    — user / UI
//   500–599    — broker UX
//   65000–     — catchall
enum class SessionTermination : uint16_t {
	None = 0,

	// Handshake (preserve v5 kReject* numerics 1–9).
	OsMismatch                = 1,
	BasicMismatch             = 2,
	VersionSkew               = 3,
	TosNotAccept              = 4,
	HostFull                  = 5,
	HostNotReady              = 6,
	BadEntryCode              = 7,
	HostRejected              = 8,
	CanonicalProfileMismatch  = 9,

	// Snapshot.
	SnapshotCrcMismatch       = 100,
	SnapshotApplyFailed       = 101,
	SnapshotChunkTimeout      = 102,
	SnapshotTooLarge          = 103,
	SnapshotEncodeFailed      = 104,

	// Mid-session.
	DesyncFlapLimit           = 200,
	ProtocolViolation         = 201,
	EmulatorCrashed           = 202,
	PeerVersionMismatchMidSession = 203,

	// Transport.
	NetworkUnreachable        = 300,
	NatTraversalFailed        = 301,
	RelayDisconnect           = 302,
	AckTimeoutExceeded        = 303,

	// User / UI.
	PeerSentBye               = 400,
	LocalUserQuit             = 401,
	LocalUserKick             = 402,

	// Broker UX.
	BrokerSessionExpired      = 500,
	BrokerJoinerCanceled      = 501,
	BrokerHostCanceled        = 502,
	BrokerApprovalTimeout     = 503,

	// Catchall.
	Internal                  = 65000,
	Unknown                   = 65535,
};

// Source-compat aliases for the v5 kReject* / kBye* names.  These
// keep the existing 50+ call sites in coordinator.cpp / netplay_glue
// / tests compiling without a sweeping rename in the same patch.
// The numeric values match the new SessionTermination codes (1–9 for
// kReject*, the v5-specific kBye* codes are remapped to the closest
// SessionTermination semantic).  Hex dumps of v5 session logs read
// against the historical kBye* table; v6 emits these as
// SessionTermination values so no log compatibility cliff.
constexpr uint16_t kRejectOsMismatch              = (uint16_t)SessionTermination::OsMismatch;
constexpr uint16_t kRejectBasicMismatch           = (uint16_t)SessionTermination::BasicMismatch;
constexpr uint16_t kRejectVersionSkew             = (uint16_t)SessionTermination::VersionSkew;
constexpr uint16_t kRejectTosNotAccept            = (uint16_t)SessionTermination::TosNotAccept;
constexpr uint16_t kRejectHostFull                = (uint16_t)SessionTermination::HostFull;
constexpr uint16_t kRejectHostNotReady            = (uint16_t)SessionTermination::HostNotReady;
constexpr uint16_t kRejectBadEntryCode            = (uint16_t)SessionTermination::BadEntryCode;
constexpr uint16_t kRejectHostRejected            = (uint16_t)SessionTermination::HostRejected;
constexpr uint16_t kRejectCanonicalProfileMismatch =
	(uint16_t)SessionTermination::CanonicalProfileMismatch;

// v5 kBye* codes remap into the v6 SessionTermination namespace.
// kByeCleanExit → LocalUserQuit (the user closed the emulator).
// kByeDesyncDetected → DesyncFlapLimit.
// kByeTimeout → AckTimeoutExceeded.
// kByeVersionMismatch → PeerVersionMismatchMidSession.
constexpr uint16_t kByeCleanExit         = (uint16_t)SessionTermination::LocalUserQuit;
constexpr uint16_t kByeDesyncDetected    = (uint16_t)SessionTermination::DesyncFlapLimit;
constexpr uint16_t kByeTimeout           = (uint16_t)SessionTermination::AckTimeoutExceeded;
constexpr uint16_t kByeVersionMismatch   = (uint16_t)SessionTermination::PeerVersionMismatchMidSession;

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

// NetBootConfig — v4 layout, 32 bytes on the wire.
//
// In v4 the host no longer ships an arbitrary subset of MachineConfig
// fields.  Both peers force the canonical Netplay Session Profile
// (see netplay_profile.h: ATNetplayProfile::kCanonicalProfileVersion);
// only the 6 per-game variables below are negotiable per session.
// Removed since v3:
//   cpuMode         — locked to 6502 in canonical profile
//   sioAcceleration — pinned ON in canonical profile
//   masterSeed      — replaced by the constant kLockedRandomSeed
//   bootFrames      — was always 0
struct NetBootConfig {
	uint16_t canonicalProfileVersion = 0; // ATNetplayProfile::kCanonicalProfileVersion
	uint16_t reserved0       = 0;
	uint8_t  hardwareMode    = 0;  // ATHardwareMode enum
	uint8_t  memoryMode      = 0;  // ATMemoryMode enum
	uint8_t  videoStandard   = 0;  // ATVideoStandard enum
	uint8_t  basicEnabled    = 0;  // 0/1
	uint32_t kernelCRC32     = 0;  // 0 = canonical default for hardwareMode
	uint32_t basicCRC32      = 0;  // 0 = canonical default
	uint32_t gameFileCRC32   = 0;  // for joiner local-cache lookup
	char     gameExtension[8] = {}; // ".xex" / ".atr" / ".car", NUL-pad
	uint8_t  reserved1[8]    = {};
};

// NetWelcome — host → joiner (accept).  128 bytes on the wire at v5
// (92 base + 36 BootConfig).  "snapshot" fields are the raw game-file
// bytes; framing matches v4.  Added in v5: snapshotCRC32 — joiner
// verifies the assembled bytes against this before apply, catching
// silent truncation / protocol drift / hostile peer.
struct NetWelcome {
	uint32_t magic = kMagicWelcome;
	uint16_t inputDelayFrames = 3;
	uint16_t playerSlot = 2;       // 2 = first joiner; matches §9.6
	uint8_t  cartName[kCartLen] = {};
	uint32_t snapshotBytes = 0;
	uint32_t snapshotChunks = 0;
	uint32_t snapshotCRC32 = 0;    // CRC32 over the snapshot bytes (v5)
	uint64_t settingsHash = 0;     // digest over BootConfig + CRCs
	NetBootConfig boot;
};

// NetWelcomeAck — joiner → host.  4 bytes on the wire (magic only).
// Sent the instant the joiner transitions to Phase::ReceivingSnapshot
// so the host's chunk pump can fire its first chunk on a confirmed
// "joiner ready" signal instead of racing Welcome on the wire.
struct NetWelcomeAck {
	uint32_t magic = kMagicWelcomeAck;
};

// NetReject — host → joiner (refuse).  v6: 6 bytes (was 8 in v5).
// `reason` is a SessionTermination value, narrowed to u16 in v6 to
// match the new enum's underlying type.  v5 senders' 8-byte Reject
// fails the v6 size check (DecodeResult::TooShort or BadSize) — see
// the v6 release notes at kProtocolVersion above.
struct NetReject {
	uint32_t magic = kMagicReject;
	uint16_t reason = 0;     // SessionTermination raw value
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

// NetBye — disconnect.  v6: 6 bytes (was 8 in v5; reason was u32).
// In v5 NetBye carried no semantic reason — the trailing 4 bytes
// were unused.  v6 repurposes the field: peers explain WHY they
// disconnected via SessionTermination.  Hard wire break vs. v5
// (size + interpretation both change) — same rationale as Reject.
struct NetBye {
	uint32_t magic = kMagicBye;
	uint16_t reason = 0;     // SessionTermination raw value
};

// NetPhase — bilateral broadcast on every Coordinator phase
// transition + 1-Hz heartbeat during stable phases.  Lets the peer
// (and the lobby's WS bridge for relay sessions) build a real-time
// view of where the other side is in the handshake/snapshot/
// lockstep machine without parsing the snapshot payload.  v6 only.
//
// `phase` is the broadcast subset of Coordinator::Phase plus a few
// host-only / joiner-only phases distinguished by `flags` bit 0 (0
// = local is host, 1 = local is joiner).  `progNum` / `progDen`
// carry an optional per-phase progress fraction (e.g.
// "snapshot chunks delivered" = N / total).  When unused both are
// zero.  12 bytes.
struct NetPhase {
	uint32_t magic     = kMagicNetPhase;
	uint8_t  phase     = 0;     // PhaseBroadcast enum (see coordinator.h)
	uint8_t  flags     = 0;     // bit0: 0=host 1=joiner
	uint16_t progNum   = 0;     // numerator (0 if no progress fraction)
	uint16_t progDen   = 0;     // denominator (0 if no progress fraction)
	uint16_t reserved  = 0;     // future fields; must be zero in v6
};

// NetEvent — single fine-grained event in a NetEventBatch.  Twelve-
// byte packet of (kind, code, data, ts_offset_ms).  Kinds + codes
// are ad-hoc per use site (see Coordinator::EmitEvent).  8 bytes.
struct NetEvent {
	uint16_t tsOffsetMs = 0;     // ms since session start (mod 65536)
	uint8_t  kind       = 0;     // EventKind enum
	uint8_t  code       = 0;     // kind-specific subcode
	uint32_t data       = 0;     // kind-specific payload (frame number,
	                             // chunk index, RTT sample, etc.)
};

// NetEventBatch — variable size, ≤230 B.  Header is 6 bytes (4
// magic + 1 count + 1 reserved); each NetEvent is 8 bytes; the cap
// kMaxEventBatchEvents = 28 keeps the total payload below the v5
// snapshot-chunk size.  Sent at most every 100 ms or when the
// per-coordinator pending queue fills.  Drop-oldest on overflow.
constexpr size_t kMaxEventBatchEvents = 28;
struct NetEventBatch {
	uint32_t magic    = kMagicNetEvents;
	uint8_t  count    = 0;       // ≤ kMaxEventBatchEvents
	uint8_t  reserved = 0;
	NetEvent items[kMaxEventBatchEvents] = {};
};

// NetHeartbeat — 1-Hz bilateral telemetry: RTT, packet loss, frame-
// skip, peer-frame lag, local CPU saturation, tab-visible bit.
// Cheap to emit (data is sampled from existing local counters that
// are already maintained for lockstep / relay rate-limiting).
// 16 bytes.
struct NetHeartbeat {
	uint32_t magic         = kMagicNetHeartbeat;
	uint16_t rttMs         = 0;   // local→peer→local round trip estimate
	uint8_t  lossPct5s     = 0;   // 0..100 over the last 5 s window
	uint8_t  frameSkip5s   = 0;   // # of frames dropped in last 5 s
	uint16_t framesBehind  = 0;   // local lockstep frame minus peer's ack
	uint8_t  cpuSaturation = 0;   // 0..100 estimate (sim-thread busy %)
	uint8_t  tabVisible    = 1;   // 1 = tab visible / window focused
	uint16_t seq           = 0;   // monotonic per-sender (skip detect)
	uint16_t wallMsLow     = 0;   // wallclock millis low 16 bits (cadence)
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

// NetSnapSkip — joiner → host (24 bytes).  Sent after Welcome when
// the joiner finds the requested gameFileCRC32 locally (library or
// netplay cache).  Host validates that the claimed CRC matches what
// it advertised in NetBootConfig.gameFileCRC32 and that sessionNonce
// matches; on success it jumps the SnapshotSender to Done so both
// sides skip the chunked transfer.
struct NetSnapSkip {
	uint32_t magic = kMagicSnapSkip;
	uint8_t  sessionNonce[kSessionNonceLen] = {};
	uint32_t gameFileCRC32 = 0;
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
// v4 BootConfig: u16 canonical + u16 reserved0 + 4×u8 + 3×u32 + 8 bytes ext + 8 bytes reserved1 = 36
constexpr size_t kWireBootCfgSize   = 2 + 2 + 1 + 1 + 1 + 1 + 4 + 4 + 4 + 8 + 8;                                   // 36
// v5: 4 magic + 2 delay + 2 slot + 64 cart + 4 bytes + 4 chunks +
// 4 crc + 8 settings + 36 bootcfg = 128
constexpr size_t kWireWelcomeSize   = 4 + 2 + 2 + kCartLen + 4 + 4 + 4 + 8 + kWireBootCfgSize;                    // 128
// New in v5.  4-byte ack with no payload.
constexpr size_t kWireWelcomeAckSize = 4;
// v6: NetReject narrowed reason u32→u16 → 4 + 2 = 6.  NetBye gained
// a real u16 reason (replacing the v5 unused 4-byte tail) → 4 + 2 = 6.
// Mixed-version peers fail at decode (size mismatch) — see the v6
// release notes at kProtocolVersion above.
constexpr size_t kWireRejectSize    = 6;
constexpr size_t kWireInputSize     = 4;
constexpr size_t kWireInputPktSize  = 4 + 4 + 2 + 2 + 4 + kRedundancyR * kWireInputSize;                           // 36 at R=5
constexpr size_t kWireByeSize       = 6;
// v6 observability layer (M5).
constexpr size_t kWireNetPhaseSize     = 4 + 1 + 1 + 2 + 2 + 2;       // 12
constexpr size_t kWireNetEventSize     = 2 + 1 + 1 + 4;               // 8
constexpr size_t kWireNetEventBatchHdrSize = 4 + 1 + 1;               // 6
constexpr size_t kWireNetEventBatchMaxSize =
	kWireNetEventBatchHdrSize + kMaxEventBatchEvents * kWireNetEventSize; // 6 + 28*8 = 230
constexpr size_t kWireNetHeartbeatSize = 4 + 2 + 1 + 1 + 2 + 1 + 1 + 2 + 2; // 16
constexpr size_t kWireChunkHdrSize  = 16;                              // + payloadLen bytes
constexpr size_t kWireAckSize       = 8;
constexpr size_t kWireResyncStartSize = 24;
constexpr size_t kWireResyncDoneSize  = 12;
constexpr size_t kWireEmoteSize       = 8;
constexpr size_t kWireSimHashDiagSize = 4 * 13;   // 52
constexpr size_t kWirePunchSize         = 4 + kSessionNonceLen;   // 20
constexpr size_t kWireSnapSkipSize      = 4 + kSessionNonceLen + 4; // 24
constexpr size_t kWireRelayHeaderSize   = 4 + 16 + 1 + 3;         // 24
constexpr size_t kWireRelayRegisterSize = kWireRelayHeaderSize;   // 24

// Maximum UDP datagram we need to send at once; chunks are the biggest.
constexpr size_t kMaxDatagramSize = kWireChunkHdrSize + kSnapshotChunkSize;

// Maximum UDP datagram we need to RECEIVE on a relay-attached socket.
// Anything inbound from a peer on PeerPath::Relay arrives ASDF-wrapped
// — the lobby prepends kWireRelayHeaderSize before forwarding, so the
// receive buffer must hold the wrap header plus the largest inner
// datagram or recvfrom() will silently truncate the tail bytes (which
// looks like a short read of length kMaxDatagramSize, then DecodeSnapChunk
// rejects every non-tail chunk as TooShort and the snapshot upload
// dies with "no chunk acks received").  SendWrappedViaLobby's TX path
// already sizes its temp buffer this way; this name matches that
// convention so the symmetry is obvious.
constexpr size_t kMaxRelayDatagramSize =
	kMaxDatagramSize + kWireRelayHeaderSize;

// Compile-time guard against NetWelcome layout drift.  The v4 wire
// layout is 88 base bytes + 36 BootConfig bytes = 124.  Anyone adding
// a field to NetBootConfig must also bump kProtocolVersion and update
// EncodeWelcome/DecodeWelcome in protocol.cpp.  Anyone changing the
// canonical-profile contract (defaults applied per session) must also
// bump ATNetplayProfile::kCanonicalProfileVersion in netplay_profile.h.
static_assert(kWireBootCfgSize == 36, "NetBootConfig wire layout drift");
static_assert(kWireWelcomeSize == 128, "NetWelcome wire layout drift");

// Resync messages: 6 × uint32 and 3 × uint32 respectively.  Guarding
// against silent drift if someone reorders or adds fields to the
// structs above.
static_assert(kWireResyncStartSize == 4 * 6, "NetResyncStart wire drift");
static_assert(kWireResyncDoneSize  == 4 * 3, "NetResyncDone wire drift");
static_assert(kWireSimHashDiagSize == 4 * 13, "NetSimHashDiag wire drift");

// v6 wire-size guards.  Bumping kProtocolVersion alone isn't enough
// to catch silent drift — these asserts will trip the build the
// moment someone adds a field without updating the size constant.
static_assert(kWireRejectSize == 6,    "v6 NetReject wire drift");
static_assert(kWireByeSize    == 6,    "v6 NetBye wire drift");
static_assert(kWireNetPhaseSize     == 12,  "NetPhase wire drift");
static_assert(kWireNetEventSize     == 8,   "NetEvent wire drift");
static_assert(kWireNetHeartbeatSize == 16,  "NetHeartbeat wire drift");
static_assert(kProtocolVersion == 6,
	"v6 release: bump kProtocolVersion when wire format changes");

} // namespace ATNetplay
