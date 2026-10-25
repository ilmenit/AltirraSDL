//	AltirraSDL - embedded MCP (Model Context Protocol) server
//
//	SDL3-only feature (no Windows equivalent), added on explicit request.
//	Exposes the emulator to MCP clients over Streamable HTTP at
//	http://127.0.0.1:<port>/mcp, as a thin layer over the AltirraBridge
//	command set. See docs/mcp-server-design.md.
//
//	Everything runs on the main thread inside Poll(); there are no threads
//	or locks. The host supplies the configuration with SetConfig(): the
//	SDL3 frontend from Configure System > Emulator > MCP Server (see
//	ATMCPServerApply() in app/mcp_server_settings.cpp), the headless
//	AltirraBridgeServer from its --mcp / --mcp-token options. Poll() then
//	starts, stops or rebinds the listener to match.

#pragma once

#include <cstdint>
#include <string>

class ATSimulator;

namespace ATMCPServer {

enum class State {
	Stopped,        // disabled in settings
	Listening,
	PortInUse,
	Error
};

struct Status {
	State state = State::Stopped;
	uint16_t port = 0;
	int osError = 0;
};

struct Config {
	bool enabled = false;
	uint16_t port = 7750;           // same default as the settings page
	bool requireToken = false;
	std::string token;              // used when requireToken
};

// Replace the configuration; the next Poll() applies it.
void SetConfig(const Config& config);

// Per-frame service: applies pending settings changes, accepts
// connections, handles requests and completes waiting tool calls.
void Poll(ATSimulator& sim);

// Stop the listener, fail waiting calls and release injected input.
void Shutdown(ATSimulator& sim);

Status GetStatus();

}  // namespace ATMCPServer

// SDL3 frontend: re-read the MCP Server settings page values and pass them
// to SetConfig(). Call at startup and after any change on the page.
// Defined in app/mcp_server_settings.cpp.
void ATMCPServerApply();
