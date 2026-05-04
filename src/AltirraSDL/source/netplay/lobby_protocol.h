// Altirra SDL netplay - lobby HTTP/JSON protocol constants
//
// Shared between the C++ client (lobby_client.cpp) and the C++ server
// (server/lobby/server.cpp).  Both sides #include this file so that
// field names, TTL values, rate-limit parameters and route paths are
// defined in exactly one place and cannot drift between peers.
//
// There is no backward-compatibility negotiation on the wire; the
// lobby is single-community and both ends are built from the same
// source tree.  A change here therefore requires rebuilding both
// client and server in the same commit.

#pragma once

#include <cstddef>
#include <cstdint>

namespace ATLobby {

// Wire protocol version.  Increment when changing field names or
// adding a required field.
//
// v2 — added kernelCRC32, basicCRC32, hardwareMode (firmware pre-flight),
//      state (waiting|playing — playing sessions stay listed but
//      non-joinable), and GET /v1/stats endpoint.  Also publishes
//      videoStandard ("NTSC"/"PAL"/…) and memoryMode ("320K", "576K", …)
//      so the Browser can show the host's full machine spec before
//      anyone commits to joining.  New keys are additive and default
//      to empty strings, so the version number stays at 2.
//
// v3 — added `wssRelayOnly` (bool).  Hosts running in a browser
//      (WASM build) cannot open UDP sockets, so they publish no
//      `hostEndpoint` / `candidates` and set this flag instead.
//      Joiners that see the flag MUST skip the candidate spray and
//      go straight to the lobby's relay path from T=0 (the lobby's
//      WS bridge bridges UDP↔WS internally).  Pre-v3 native joiners
//      ignore unknown JSON fields, so they will simply fail to
//      connect to a WSS-only host (Hello timeout) — that's the
//      expected behaviour: a v2 client can join only v2 hosts.
inline constexpr int kProtocolVersion = 3;

// Session TTL in seconds.  Clients should heartbeat well inside this
// window; 30 s is the configured cadence on the client side.  60 s
// allows exactly one missed heartbeat before eviction — long enough
// to absorb a brief network blip but short enough that an actual
// host crash / kill / loss-of-power leaves the session listed for
// at most ~90 s (60 s TTL + 30 s sweep cadence) instead of ~120 s.
// Combined with server-side dedup-on-Create (same hostHandle +
// cartName replaces the prior session), this is what keeps the
// public list from accumulating ghost entries.
inline constexpr int kSessionTTLSeconds = 60;

// Rate limit (per source IP).  Token bucket: `kRateBurst` tokens
// capacity, one token refilled every `kRateRefillMillis` ms.
inline constexpr int kRateBurst         = 120;
inline constexpr int kRateRefillMillis  = 1000;

// How long an idle per-IP rate-limit bucket is kept before the
// sweeper prunes it.
inline constexpr int kRateBucketKeepMillis = 10 * 60 * 1000;  // 10 min

// Session-store sweep cadence — how often the server scans for
// expired sessions and prunes cold rate-limit buckets.
inline constexpr int kSweepIntervalMillis = 30 * 1000;        // 30 s

// Lobby capacity (hard cap on concurrent sessions).  Additional
// Create requests return 429 once full.
inline constexpr int kMaxSessions = 1000;

// Maximum sessions returned in a single List response.
inline constexpr int kListCap = 500;

// Validation bounds.
inline constexpr int kCartNameMax      = 64;
inline constexpr int kHostHandleMax    = 32;
inline constexpr int kRegionMax        = 32;
inline constexpr int kCartArtHashMax   = 64;
inline constexpr int kHardwareModeMax  = 16;   // "800XL", "5200", "1200XL" etc.
inline constexpr int kStateMax         = 16;   // "waiting" / "playing"
inline constexpr int kVideoStandardMax = 8;    // "NTSC", "PAL", "NTSC50", "PAL60", "SECAM"
inline constexpr int kMemoryModeMax    = 8;    // "8K", "16K", "320K", "1088K", etc.
inline constexpr int kMinPlayers       = 2;
inline constexpr int kMaxPlayersLimit  = 8;

// Request body size cap — anything larger is rejected without
// parsing.
inline constexpr int kMaxRequestBodyBytes = 8 * 1024;

// -------------------------------------------------------------------
// Route paths.
// -------------------------------------------------------------------
inline constexpr const char *kPathHealthz         = "/healthz";
inline constexpr const char *kPathStats           = "/v1/stats";
inline constexpr const char *kPathMetrics         = "/v1/metrics";
inline constexpr const char *kPathSessions        = "/v1/sessions";
inline constexpr const char *kPathSession         = "/v1/session";
// Dynamic ID suffix; the server pattern-matches on kPathSession + "/"
// + <id> [+ "/heartbeat"] and the client does the same string build.
inline constexpr const char *kPathHeartbeatSuffix = "/heartbeat";
// v4 two-sided punch: joiner POSTs its own candidates here before
// spraying Hello.  The lobby buffers them on the session and hands
// them to the host on the next heartbeat so the host can fire
// outbound UDP probes to the joiner, pre-opening its NAT pinhole.
inline constexpr const char *kPathPeerHintSuffix  = "/peer-hint";

// Header name carrying the session's delete token.
inline constexpr const char *kHeaderSessionToken = "X-Session-Token";

// UDP reflector (STUN-lite).  Co-located with the HTTP lobby — a
// single UDP socket on this port echoes the observed source
// endpoint back to the sender.  Stateless; see nat_discovery.h for
// the 8-byte request / 24-byte response wire format.
inline constexpr uint16_t kReflectorPortDefault = 8081;

// -------------------------------------------------------------------
// JSON field names — kept as constants so a server-side typo can't
// silently break the client-side parser (and vice versa).
// -------------------------------------------------------------------
namespace Field {
    // Session / Create request + Session list entry.
    inline constexpr const char *kSessionId       = "sessionId";
    inline constexpr const char *kToken           = "token";
    inline constexpr const char *kTTLSeconds      = "ttlSeconds";
    inline constexpr const char *kCartName        = "cartName";
    inline constexpr const char *kHostHandle      = "hostHandle";
    inline constexpr const char *kHostEndpoint    = "hostEndpoint";
    inline constexpr const char *kRegion          = "region";
    inline constexpr const char *kPlayerCount     = "playerCount";
    inline constexpr const char *kMaxPlayers      = "maxPlayers";
    inline constexpr const char *kProtocolVersion = "protocolVersion";
    inline constexpr const char *kVisibility      = "visibility";
    inline constexpr const char *kRequiresCode    = "requiresCode";
    inline constexpr const char *kCartArtHash     = "cartArtHash";
    inline constexpr const char *kCreatedAt       = "createdAt";
    inline constexpr const char *kLastSeen        = "lastSeen";

    // v2: firmware pre-flight + session state.
    inline constexpr const char *kKernelCRC32     = "kernelCRC32";  // hex string, 8 chars
    inline constexpr const char *kBasicCRC32      = "basicCRC32";   // hex string, 8 chars; "" if BASIC off
    inline constexpr const char *kHardwareMode    = "hardwareMode"; // "800XL" | "5200" | etc.
    inline constexpr const char *kVideoStandard   = "videoStandard"; // "NTSC" | "PAL" | …
    inline constexpr const char *kMemoryMode      = "memoryMode";    // "320K" | "1088K" | …
    inline constexpr const char *kState           = "state";        // "waiting" | "playing"

    // v2: /v1/stats response.
    inline constexpr const char *kSessionCount    = "sessions";
    inline constexpr const char *kWaitingCount    = "waiting";
    inline constexpr const char *kPlayingCount    = "playing";
    inline constexpr const char *kHostCount       = "hosts";

    // v3: NAT traversal.  The host enumerates every endpoint at which
    // a joiner might be able to reach it and sends them all in
    // `candidates`, a SEMICOLON-separated "ip:port;ip:port;..." list
    // ordered "best first" (LAN → public/srflx → loopback).  Old
    // clients ignore the field and fall back to hostEndpoint (which
    // is the first entry); new clients spray NetHello to every
    // endpoint in parallel and let the first responder win.  The
    // format is a plain JSON string — no array parser needed on
    // either side.
    inline constexpr const char *kCandidates      = "candidates";

    // v3: WebSocket relay-only sessions.  Browser-hosted (WASM)
    // sessions set this flag to true; the host has no UDP endpoints
    // to publish.  Joiners that see it MUST skip direct candidate
    // spray and go straight to lobby-relay from T=0 (native joiners
    // via UDP-ASDF, browser joiners via WSS — the lobby bridges).
    inline constexpr const char *kWssRelayOnly    = "wssRelayOnly";

    // v4 two-sided punch: peer-hint request + heartbeat response.
    // The joiner sends { joinerHandle, sessionNonce (32 hex chars),
    // candidates "ip:port;ip:port;..." }; the lobby stores it on the
    // session and hands it to the host on the next heartbeat as an
    // array under "hints" (each element: { sessionNonce, joinerHandle,
    // candidates, ageMs }).  Old clients ignore unknown fields.
    inline constexpr const char *kJoinerHandle    = "joinerHandle";
    inline constexpr const char *kSessionNonce    = "sessionNonce";
    inline constexpr const char *kAgeMs           = "ageMs";
    inline constexpr const char *kHints           = "hints";

    // Error response.
    inline constexpr const char *kError           = "error";
}

// v4 two-sided punch: server-side limits.
inline constexpr int    kPeerHintCandidatesMax = 512;
inline constexpr int    kPeerHintNonceHexLen   = 32;
inline constexpr int    kPeerHintTtlMs         = 30 * 1000;
inline constexpr size_t kPeerHintCap           = 8;

// v4 UDP relay-fallback limits (server-side, applies on kReflectorPort).
// Two-stage trigger to keep the perceived join time short while still
// giving honest direct-punch a fair shot:
//   T+0           — both peers spray Hello probes at each other
//   T+kPrearm     — both peers send one 'ASGR' to prime the relay table
//                   (no payload yet — still trying direct).  Pre-arming
//                   lets the T+kFallback switch be instant: no extra
//                   round-trip for ASGR at fallback time.
//   T+kFallback   — if no NetWelcome has arrived, wrap outbound traffic
//                   in 'ASDF' frames addressed to the reflector port.
//                   The server forwards the inner bytes to the other
//                   peer's observed endpoint (looked up via the table
//                   primed at T+kPrearm).
// The fallback budget (6 s) covers the worst case of:
//   ~1.5 s lobby heartbeat poll until the host learns of the joiner's
//          peer-hint
//   ~1 s for both peers to start probes
//   ~3 s of probe overlap (12 probes at 250 ms intervals) — well past
//          typical cone–cone NAT punch completion (~200–800 ms once
//          both sides are blasting).
// Symmetric-NAT users see the relay engage at T+6 s, not T+10 s, which
// is the difference between "slow connect" and "stuck".
// TTL is intentionally short so crashed peers don't keep relay slots
// alive indefinitely.
inline constexpr int kRelayPrearmAfterMs   = 3000;
inline constexpr int kRelayFallbackAfterMs = 6000;
inline constexpr int kRelayFailTimeoutMs   = 25000;
inline constexpr int kRelayPeerIdleMs      = 30000;
// Sustain interval for periodic relay re-registration.  The server's
// RelayTable prunes a slot after kRelayPeerIdleMs of silence.  A host
// that's waiting for a joiner emits no other relay traffic, so without
// a periodic ASGR refresh the slot would expire and the safety-net
// relay path would silently stop forwarding.  10 s gives 3x headroom
// against the 30 s prune; cheap (28 bytes per send) and bounded (only
// fires while in WaitingForJoiner / Handshaking / SendingSnapshot —
// during Lockstepping, ordinary ASDF traffic refreshes the slot).
// Also covers single-packet UDP loss at first registration: a dropped
// register packet recovers in one interval instead of breaking the
// session for its lifetime.
inline constexpr int kRelayRegisterIntervalMs = 10000;
// Per-pair token bucket on the relay forward path.  Lockstep runs at
// 60 Hz in each direction, so a healthy session forwards ~120 pps
// through the server.  Cap each pair at 240 pps (2x headroom for
// snapshot bursts and resync chunks); excess packets are silently
// dropped and accounted to relayDroppedRateLimit.  Keeps a single
// hostile/buggy peer from amplifying load on the Free-Tier box.
inline constexpr int kRelayPeerPpsBurst    = 240;
inline constexpr int kRelayPeerPpsRefill   = 240; // tokens added per second

// Visibility values.
inline constexpr const char *kVisibilityPublic  = "public";
inline constexpr const char *kVisibilityPrivate = "private";

// Session state values.
inline constexpr const char *kStateWaiting = "waiting";
inline constexpr const char *kStatePlaying = "playing";

} // namespace ATLobby
