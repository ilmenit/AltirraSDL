// Altirra SDL3 netplay - lobby client
//
// Typed facade over http_minimal.cpp + a tiny hand-rolled JSON reader.
// Talks to the altirra-sdl-lobby reference server (Go, deployed at
// http://92.5.13.40:8080 — see NETPLAY_DESIGN_PLAN.md §11).
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
	std::string hostEndpoint;   // "a.b.c.d:port"
	std::string region;
	int         playerCount    = 0;
	int         maxPlayers     = 0;
	int         protocolVersion = 0;
	std::string visibility;     // "public" | "private"
	bool        requiresCode   = false;
	std::string cartArtHash;    // optional hex
	std::string createdAt;      // RFC3339
	std::string lastSeen;       // RFC3339
};

struct LobbyCreateRequest {
	std::string cartName;
	std::string hostHandle;
	std::string hostEndpoint;   // "public-ip-or-hostname:port"
	std::string region;
	int         playerCount    = 1;
	int         maxPlayers     = 2;
	int         protocolVersion = 2;
	std::string visibility     = "public";  // or "private"
	bool        requiresCode   = false;
	std::string cartArtHash;    // may be empty
};

struct LobbyCreateResponse {
	std::string sessionId;
	std::string token;
	int         ttlSeconds = 0;
};

struct LobbyEndpoint {
	// HTTP transport.
	std::string host       = "92.5.13.40";
	uint16_t    port       = 8080;
	uint32_t    timeoutMs  = 5000;
};

class LobbyClient {
public:
	LobbyClient() = default;
	explicit LobbyClient(const LobbyEndpoint& ep) : mEp(ep) {}

	void SetEndpoint(const LobbyEndpoint& ep) { mEp = ep; }
	const LobbyEndpoint& GetEndpoint() const { return mEp; }

	const char* LastError() const { return mLastError.c_str(); }

	// Returns true on HTTP 201.  On failure, LastError() holds the reason.
	bool Create(const LobbyCreateRequest& req, LobbyCreateResponse& out);

	// Returns true on HTTP 200 (including empty list).  Populates `out`.
	bool List(std::vector<LobbySession>& out);

	// Returns true on HTTP 200.  `playerCount` must be <= session's
	// maxPlayers; server silently ignores out-of-range values.
	bool Heartbeat(const std::string& sessionId,
	               const std::string& token,
	               int playerCount);

	// Returns true on HTTP 204.  Safe to call more than once for the
	// same id; second call returns 404 which we surface as a success
	// (the session is gone, which is what the caller wanted).
	bool Delete(const std::string& sessionId, const std::string& token);

private:
	LobbyEndpoint mEp;
	std::string   mLastError;
};

} // namespace ATNetplay
