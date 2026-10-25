//	AltirraSDL - MCP Server settings (SDL3-only, no Windows equivalent).

#include <stdafx.h>
#include <vd2/system/registry.h>
#include <vd2/system/VDString.h>

#include <cstdio>
#include <random>

#include "mcp_server_settings.h"

#ifdef ALTIRRA_MCP_ENABLED
#include "mcp/mcp_server.h"
#endif

namespace {
	constexpr const char *kKeyName = "MCP Server";
}

bool ATMCPServerGetEnabled() {
	VDRegistryAppKey key(kKeyName, false);
	return key.getBool("Enabled", false);
}

void ATMCPServerSetEnabled(bool enabled) {
	VDRegistryAppKey key(kKeyName, true);
	key.setBool("Enabled", enabled);
}

uint16_t ATMCPServerGetPort() {
	VDRegistryAppKey key(kKeyName, false);
	const int port = key.getInt("Port", kATMCPServerDefaultPort);
	if (port < 1 || port > 65535)
		return kATMCPServerDefaultPort;

	return (uint16_t)port;
}

void ATMCPServerSetPort(uint16_t port) {
	if (!port)
		port = kATMCPServerDefaultPort;

	VDRegistryAppKey key(kKeyName, true);
	key.setInt("Port", port);
}

bool ATMCPServerGetRequireToken() {
	VDRegistryAppKey key(kKeyName, false);
	return key.getBool("Require token", false);
}

void ATMCPServerSetRequireToken(bool require) {
	VDRegistryAppKey key(kKeyName, true);
	key.setBool("Require token", require);
}

namespace {
	// 128 bits from the OS random source, hex-encoded (same scheme as the
	// AltirraBridge session token).
	std::string GenerateToken() {
		std::random_device rd;
		char buf[33];
		std::snprintf(buf, sizeof buf, "%08x%08x%08x%08x",
			(unsigned)rd(), (unsigned)rd(), (unsigned)rd(), (unsigned)rd());
		return std::string(buf);
	}

	bool IsValidToken(const VDStringA& s) {
		if (s.size() != 32)
			return false;
		for (char c : s) {
			if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
				return false;
		}
		return true;
	}
}

std::string ATMCPServerGetToken() {
	{
		VDRegistryAppKey key(kKeyName, false);
		VDStringA token;
		if (key.getString("Token", token) && IsValidToken(token))
			return std::string(token.c_str());
	}

	ATMCPServerRegenerateToken();

	VDRegistryAppKey key(kKeyName, false);
	VDStringA token;
	key.getString("Token", token);
	return std::string(token.c_str());
}

void ATMCPServerRegenerateToken() {
	VDRegistryAppKey key(kKeyName, true);
	key.setString("Token", GenerateToken().c_str());
}

#ifdef ALTIRRA_MCP_ENABLED
void ATMCPServerApply() {
	ATMCPServer::Config config;
	config.enabled = ATMCPServerGetEnabled();
	config.port = ATMCPServerGetPort();
	config.requireToken = ATMCPServerGetRequireToken();
	if (config.requireToken)
		config.token = ATMCPServerGetToken();
	ATMCPServer::SetConfig(config);
}
#endif
