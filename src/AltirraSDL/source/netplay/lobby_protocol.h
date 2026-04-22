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
inline constexpr int kProtocolVersion = 2;

// Session TTL in seconds.  Clients should heartbeat well inside this
// window; 30 s is the configured cadence on the client side.
inline constexpr int kSessionTTLSeconds = 90;

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
inline constexpr const char *kPathSessions        = "/v1/sessions";
inline constexpr const char *kPathSession         = "/v1/session";
// Dynamic ID suffix; the server pattern-matches on kPathSession + "/"
// + <id> [+ "/heartbeat"] and the client does the same string build.
inline constexpr const char *kPathHeartbeatSuffix = "/heartbeat";

// Header name carrying the session's delete token.
inline constexpr const char *kHeaderSessionToken = "X-Session-Token";

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

    // Error response.
    inline constexpr const char *kError           = "error";
}

// Visibility values.
inline constexpr const char *kVisibilityPublic  = "public";
inline constexpr const char *kVisibilityPrivate = "private";

// Session state values.
inline constexpr const char *kStateWaiting = "waiting";
inline constexpr const char *kStatePlaying = "playing";

} // namespace ATLobby
