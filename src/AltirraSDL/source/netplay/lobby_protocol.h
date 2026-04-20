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
inline constexpr int kProtocolVersion = 1;

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
inline constexpr int kMinPlayers       = 2;
inline constexpr int kMaxPlayersLimit  = 8;

// Request body size cap — anything larger is rejected without
// parsing.
inline constexpr int kMaxRequestBodyBytes = 8 * 1024;

// -------------------------------------------------------------------
// Route paths.
// -------------------------------------------------------------------
inline constexpr const char *kPathHealthz         = "/healthz";
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

    // Error response.
    inline constexpr const char *kError           = "error";
}

// Visibility values.
inline constexpr const char *kVisibilityPublic  = "public";
inline constexpr const char *kVisibilityPrivate = "private";

} // namespace ATLobby
