//	AltirraSDL - Configure System > Emulator > MCP Server page.
//
//	SDL3-only page (no Windows equivalent), added on explicit request.
//	Settings are machine-global and owned by app/mcp_server_settings.cpp.
//	See docs/mcp-server-design.md for configuration and protocol details.

#include <stdafx.h>
#include <SDL3/SDL.h>
#include <imgui.h>

#include "simulator.h"
#include "ui_system_internal.h"
#include "../../app/mcp_server_settings.h"

#ifdef ALTIRRA_MCP_ENABLED
#include "../../mcp/mcp_server.h"
#endif

namespace {
	// Settings changes take effect immediately: the server re-reads them
	// on its next poll and starts, stops or rebinds to match.
	void ApplyServerSettings() {
#ifdef ALTIRRA_MCP_ENABLED
		ATMCPServerApply();
#endif
	}

	void RenderServerStatus() {
#ifdef ALTIRRA_MCP_ENABLED
		const ATMCPServer::Status status = ATMCPServer::GetStatus();
		switch (status.state) {
			case ATMCPServer::State::Listening:
				ImGui::Text("Listening on 127.0.0.1:%u", (unsigned)status.port);
				break;

			case ATMCPServer::State::PortInUse:
				ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f),
					"Port %u is in use", (unsigned)status.port);
				break;

			case ATMCPServer::State::Error:
				ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f),
					"Could not listen on port %u (error %d)", (unsigned)status.port, status.osError);
				break;

			case ATMCPServer::State::Stopped:
			default:
				ImGui::TextDisabled("Stopped");
				break;
		}
#else
		ImGui::TextDisabled("The MCP server is not available in this build.");
#endif
	}
}

void RenderMCPServerCategory(ATSimulator & /*sim*/) {
	ImGui::TextWrapped("Settings for the MCP (Model Context Protocol) server.");
	ImGui::Spacing();

	bool enabled = ATMCPServerGetEnabled();
	if (ImGui::Checkbox("Enabled", &enabled)) {
		ATMCPServerSetEnabled(enabled);
		ApplyServerSettings();
	}

	// Edit into a local copy and commit only when the field loses focus,
	// so partially typed values (empty, "0", ...) are never clamped and
	// written back while the user is still typing.
	static int s_editPort = kATMCPServerDefaultPort;
	static bool s_portEditing = false;
	if (!s_portEditing)
		s_editPort = ATMCPServerGetPort();

	ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.0f);
	ImGui::InputInt("Port", &s_editPort, 0, 0);
	s_portEditing = ImGui::IsItemActive();
	ImGui::SetItemTooltip("TCP port the MCP server listens on (1-65535). Default: %u.",
		(unsigned)kATMCPServerDefaultPort);

	if (ImGui::IsItemDeactivatedAfterEdit()) {
		if (s_editPort < 1 || s_editPort > 65535)
			s_editPort = kATMCPServerDefaultPort;

		ATMCPServerSetPort((uint16_t)s_editPort);
		ApplyServerSettings();
	}

	RenderServerStatus();

	ImGui::Spacing();

	bool requireToken = ATMCPServerGetRequireToken();
	if (ImGui::Checkbox("Require access token", &requireToken)) {
		ATMCPServerSetRequireToken(requireToken);
		ApplyServerSettings();
	}
	ImGui::SetItemTooltip("When on, MCP clients must send the header "
		"\"Authorization: Bearer <token>\". The server only accepts connections "
		"from this computer either way.");

	if (requireToken) {
		std::string token = ATMCPServerGetToken();

		ImGui::SetNextItemWidth(ImGui::GetFontSize() * 18.0f);
		ImGui::InputText("##MCPToken", token.data(), token.size() + 1, ImGuiInputTextFlags_ReadOnly);
		ImGui::SameLine();
		if (ImGui::Button("Copy"))
			SDL_SetClipboardText(token.c_str());
		ImGui::SameLine();
		if (ImGui::Button("Regenerate")) {
			ATMCPServerRegenerateToken();
			ApplyServerSettings();
		}
		ImGui::SetItemTooltip("Create a new token. Clients using the old one are rejected.");
	}
}
