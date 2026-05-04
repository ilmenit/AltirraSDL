// Altirra SDL3 netplay - lobby client
//
// Typed facade over http_minimal.cpp + a tiny hand-rolled JSON reader.
// Talks to the altirra-sdl-lobby reference server (Go, deployed at
// http://158.180.27.70:8080 — see NETPLAY_DESIGN_PLAN.md §11).
//
// The lobby is a session directory, not a rendezvous server.  Calls:
//
//   Create()    host → lobby:  announce a new session, get back a
//                               session id + heartbeat token.
//   List()      anyone → lobby: fetch the current session list.
//   Heartbeat() host → lobby:   keep a session alive (90 s TTL).
//   Delete()    host → lobby:   retract a session cleanly.
//
// Private sessions (padlocked tiles in the browser) set
// visibility="private" and requiresCode=true so joiners know the host
// will challenge them for an entry code P2P.
//
// Blocking API: all four calls do network I/O.  Call them from the UI
// thread *only* while showing a spinner, or from a dedicated worker.
// Never from inside the main emulation loop.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ATNetplay {

struct LobbySession {
	std::string sessionId;
	std::string cartName;
	std::string hostHandle;
	std::string hostEndpoint;   // "a.b.c.d:port" — backward-compat single endpoint
	// v3 NAT traversal: semicolon-separated list of endpoints the
	// joiner should try (LAN → srflx/public → loopback).  When
	// non-empty this takes precedence over hostEndpoint.  Empty for
	// old hosts; the client falls back to hostEndpoint in that case.
	std::string candidates;
	std::string region;
	int         playerCount    = 0;
	int         maxPlayers     = 0;
	int         protocolVersion = 0;
	std::string visibility;     // "public" | "private"
	bool        requiresCode   = false;
	std::string cartArtHash;    // optional hex
	std::string createdAt;      // RFC3339
	std::string lastSeen;       // RFC3339

	// Lobby v2 — firmware pre-flight + session state.
	std::string kernelCRC32;    // 8-char hex; empty = no constraint
	std::string basicCRC32;     // 8-char hex or empty (BASIC off)
	std::string hardwareMode;   // "800XL" / "5200" / "1200XL" / etc.
	std::string videoStandard;  // "NTSC" / "PAL" / … — may be empty (old hosts)
	std::string memoryMode;     // "320K" / "1088K" / … — may be empty (old hosts)
	std::string state;          // "waiting" (joinable) | "playing" (in session)

	// v3: WSS-only host (browser).  When true, this session has no UDP
	// endpoints; joiners must skip candidate spray and go straight to
	// lobby relay (the lobby's WS bridge connects the two transports).
	bool        wssRelayOnly   = false;

	// UI-owned: which configured lobby (section name) this session was
	// learned from.  Never set by the HTTP parser; the UI stamps it on
	// entries as they arrive so a subsequent response from the same
	// lobby can retire entries that vanished without a full list wipe.
	std::string sourceLobby;
};

struct LobbyCreateRequest {
	std::string cartName;
	std::string hostHandle;
	std::string hostEndpoint;   // "public-ip-or-hostname:port" — primary/legacy
	// v3 candidates — semicolon-separated "ip:port;ip:port;..." list.
	// The host populates this with every endpoint it thinks a joiner
	// might reach it at (LAN / srflx / loopback / manual public).  The
	// first entry SHOULD match hostEndpoint so that old clients that
	// only understand hostEndpoint still get a reachable address.
	std::vector<std::string> candidates;
	std::string region;
	int         playerCount    = 1;
	int         maxPlayers     = 2;
	int         protocolVersion = 3;
	std::string visibility     = "public";  // or "private"
	bool        requiresCode   = false;
	std::string cartArtHash;    // may be empty
	std::string kernelCRC32;    // may be empty
	std::string basicCRC32;     // may be empty
	std::string hardwareMode;   // may be empty
	std::string videoStandard;  // may be empty
	std::string memoryMode;     // may be empty
	// v3: WASM hosts have no UDP endpoint to publish; set this to
	// true so joiners use lobby-relay from T=0.  Native hosts leave
	// it false (default).
	bool        wssRelayOnly   = false;
};

struct LobbyCreateResponse {
	std::string sessionId;
	std::string token;
	int         ttlSeconds = 0;
};

// v4 two-sided punch: a single joiner hint, either sent up by a
// joiner (PostPeerHint) or delivered down to a host in the body of
// its next heartbeat response (Heartbeat out-param).
struct LobbySessionHint {
	std::string nonceHex;    // 32 hex chars, joiner's sessionNonce
	std::string joinerHandle;
	std::string candidates;  // "ip:port;ip:port;..."
	int         ageMs = 0;   // server-reported ms since joiner POST
};

// v2 aggregate stats returned from GET /v1/stats.  The Browser shows
// this in a single status footer so users can see whether the lobby is
// alive ("5 sessions — 3 in play") without enumerating the full list.
struct LobbyStats {
	int sessions = 0;
	int waiting  = 0;
	int playing  = 0;
	int hosts    = 0;
};

struct LobbyEndpoint {
	// HTTP transport.
	std::string host       = "158.180.27.70";
	uint16_t    port       = 8080;
	// Fail fast — the lobby is optional infrastructure (peers can
	// still connect via direct IP).  A 5 s wait on every Create /
	// Heartbeat / List when the lobby is down made sessions feel
	// broken even though the coordinator was listening fine.
	uint32_t    timeoutMs  = 2000;
};

class LobbyClient {
public:
	LobbyClient() = default;
	explicit LobbyClient(const LobbyEndpoint& ep) : mEp(ep) {}

	void SetEndpoint(const LobbyEndpoint& ep) { mEp = ep; }
	const LobbyEndpoint& GetEndpoint() const { return mEp; }

	const char* LastError() const { return mLastError.c_str(); }
	// HTTP status from the last call (0 if network-level failure or
	// never called).  Used by the UI to distinguish rate-limit 429,
	// auth 401/403, server 5xx etc. for friendly error text.
	int         LastStatus() const { return mLastStatus; }
	// Wall-clock elapsed for the most recent List() call, in
	// milliseconds (0 if never called or the call failed at the
	// transport layer).  Surfaced in the Browser screen as a
	// "Lobby ping" indicator; relayed sessions go through the same
	// host so this is also a lower bound on relayed RTT.
	uint32_t    LastListLatencyMs() const { return mLastListLatencyMs; }

	// Returns true on HTTP 201.  On failure, LastError() holds the reason.
	bool Create(const LobbyCreateRequest& req, LobbyCreateResponse& out);

	// Returns true on HTTP 200 (including empty list).  Populates `out`.
	bool List(std::vector<LobbySession>& out);

	// Returns true on HTTP 200.  `playerCount` must be <= session's
	// maxPlayers; server silently ignores out-of-range values.  Empty
	// `state` leaves the server-side state unchanged; otherwise pass
	// "waiting" or "playing" to transition (piggy-backed on the normal
	// 30 s heartbeat cadence — no extra requests).
	bool Heartbeat(const std::string& sessionId,
	               const std::string& token,
	               int playerCount,
	               const std::string& state = {});

	// Same as Heartbeat, but also returns any buffered peer hints the
	// lobby has for this session (v4 two-sided punch).  Safe to call
	// against a pre-v4 server: it simply returns with `outHints`
	// empty.  Use this from the host's heartbeat thread; pass each
	// hint to Coordinator::IngestPeerHint().
	bool HeartbeatWithHints(const std::string& sessionId,
	                        const std::string& token,
	                        int playerCount,
	                        const std::string& state,
	                        std::vector<LobbySessionHint>& outHints);

	// v4 two-sided punch: the joiner POSTs its own reachable endpoints
	// (LAN + srflx + loopback) plus its sessionNonce to the lobby
	// BEFORE spraying NetHello.  The lobby then hands the hint to the
	// host on the host's next heartbeat so the host can fire outbound
	// NetPunch probes and pre-open its NAT pinhole.  Returns true on
	// HTTP 200.  `sessionNonceHex` must be 32 lowercase hex chars.
	// `candidates` is the usual semicolon-separated "ip:port;..." list.
	bool PostPeerHint(const std::string& sessionId,
	                  const std::string& joinerHandle,
	                  const std::string& sessionNonceHex,
	                  const std::string& candidates);

	// v2: GET /v1/stats → aggregate counts.  One small JSON object;
	// cheap enough for the Browser to call once per 10 s refresh.
	bool Stats(LobbyStats& out);

	// Returns true on HTTP 204.  Safe to call more than once for the
	// same id; second call returns 404 which we surface as a success
	// (the session is gone, which is what the caller wanted).
	bool Delete(const std::string& sessionId, const std::string& token);

private:
	LobbyEndpoint mEp;
	std::string   mLastError;
	int           mLastStatus = 0;
	uint32_t      mLastListLatencyMs = 0;
};

} // namespace ATNetplay
