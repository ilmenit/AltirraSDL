// Altirra SDL3 netplay - lobby.ini parser
//
// Parses a tiny INI file that lists the lobby servers the browser
// should federate across.  Format:
//
//   [official]
//   name    = Altirra Official Lobby
//   url     = http://92.5.13.40:8080
//   region  = global
//
//   [lan]
//   name      = LAN
//   transport = udp-broadcast
//   port      = 26101
//
// Keys are lower-case, values are trimmed.  Unknown keys are ignored.
// Sections with transport=udp-broadcast are LAN entries; everything
// else is assumed to be an HTTP lobby.  An HTTP entry requires `url`;
// a LAN entry requires `port`.
//
// The file is written with the default contents above on first
// launch so users can immediately disable/add lobbies.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ATNetplay {

enum class LobbyKind {
	Http,         // HTTP REST to `url`
	LanBroadcast, // UDP broadcast discovery on `port`
};

struct LobbyEntry {
	std::string section;    // [section-id]
	std::string name;       // human-readable label
	LobbyKind   kind = LobbyKind::Http;
	std::string url;        // "http://host:port"  (kind=Http)
	std::string region;     // optional
	uint16_t    port = 0;   // kind=LanBroadcast
	bool        enabled = true;
};

// Parse INI text.  Bad lines are skipped and reported via `warnings`
// (caller may display them or ignore).  Returns the number of entries
// appended to `out`.
size_t ParseLobbyIni(const char* text, size_t len,
                     std::vector<LobbyEntry>& out,
                     std::vector<std::string>* warnings);

// Default contents written on first run (production lobby + LAN entry).
// Null-terminated, plain ASCII.
extern const char* const kDefaultLobbyIni;

// Convenience: return the default set of lobbies without touching the
// filesystem.  Useful for tests and for first-run before disk I/O.
void GetDefaultLobbies(std::vector<LobbyEntry>& out);

} // namespace ATNetplay
