//	AltirraSDL - MCP Server settings (SDL3-only, no Windows equivalent).
//
//	Machine-global preferences for the MCP server exposed in
//	Configure System > Emulator > MCP Server.  Stored in the fork-owned
//	"MCP Server" registry key, so they are not per-profile.

#ifndef f_AT_MCP_SERVER_SETTINGS_H
#define f_AT_MCP_SERVER_SETTINGS_H

#include <stdint.h>
#include <string>

constexpr uint16_t kATMCPServerDefaultPort = 7750;

bool ATMCPServerGetEnabled();
void ATMCPServerSetEnabled(bool enabled);

// Always returns a valid TCP port (1-65535); out-of-range stored values
// fall back to kATMCPServerDefaultPort.
uint16_t ATMCPServerGetPort();
void ATMCPServerSetPort(uint16_t port);

// Optional bearer-token authentication (off by default). When enabled,
// every request must carry "Authorization: Bearer <token>".
bool ATMCPServerGetRequireToken();
void ATMCPServerSetRequireToken(bool require);

// The access token (32 hex characters). Generated and persisted on first
// use, so it stays stable across restarts until regenerated.
std::string ATMCPServerGetToken();
void ATMCPServerRegenerateToken();

#endif
