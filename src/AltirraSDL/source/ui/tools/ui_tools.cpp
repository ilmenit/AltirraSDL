//	AltirraSDL - Tools menu dialogs
//	Implements Advanced Configuration, and placeholder stubs for dialogs
//	not yet fully implemented.

#include <stdafx.h>
#include <algorithm>
#include <string>
#include <mutex>
#include <imgui.h>
#include <SDL3/SDL.h>
#include <vd2/system/VDString.h>
#include <vd2/system/text.h>
#include <vd2/system/file.h>
#include <vd2/system/filesys.h>
#include <vd2/system/error.h>
#include <vd2/system/date.h>
#include <vd2/system/registry.h>
#include <at/atcore/configvar.h>
#include <vd2/Dita/accel.h>
#include "ui_main.h"
#include "accel_sdl3.h"
#include "simulator.h"
#include "gtia.h"
#include "constants.h"
#include "firmwaremanager.h"
#include "firmwaredetect.h"
#include "settings.h"
#include "uiaccessors.h"
#include "uitypes.h"
#include "options.h"

extern ATSimulator g_sim;

// =========================================================================
// Advanced Configuration
// Reference: src/Altirra/source/uiadvancedconfiguration.cpp
// =========================================================================

// Per-variable edit state
struct AdvConfigEditState {
	ATConfigVar *pVar = nullptr;
	char editBuf[256] = {};
	bool editBool = false;
	bool active = false;
};

static AdvConfigEditState g_advEditState;

void ATUIRenderAdvancedConfig(ATUIState &state) {
	ImGui::SetNextWindowSize(ImVec2(650, 500), ImGuiCond_Appearing);
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	if (!ImGui::Begin("Advanced Configuration", &state.showAdvancedConfig, ImGuiWindowFlags_NoSavedSettings)) {
		ImGui::End();
		return;
	}

	if (ATUICheckEscClose()) {
		state.showAdvancedConfig = false;
		g_advEditState.active = false;
		ImGui::End();
		return;
	}

	// Gather config vars
	ATConfigVar **vars = nullptr;
	size_t numVars = 0;
	ATGetConfigVars(vars, numVars);

	const VDStringA *uvars = nullptr;
	size_t numUVars = 0;
	ATGetUndefinedConfigVars(uvars, numUVars);

	// Build sorted index
	struct VarEntry {
		const char *name;
		ATConfigVar *pVar;  // nullptr for undefined vars
	};

	std::vector<VarEntry> sorted;
	sorted.reserve(numVars + numUVars);

	for (size_t i = 0; i < numVars; ++i)
		sorted.push_back({ vars[i]->mpVarName, vars[i] });

	for (size_t i = 0; i < numUVars; ++i)
		sorted.push_back({ uvars[i].c_str(), nullptr });

	std::sort(sorted.begin(), sorted.end(),
		[](const VarEntry &a, const VarEntry &b) { return strcmp(a.name, b.name) < 0; });

	// Filter
	static char filterBuf[128] = {};
	ImGui::SetNextItemWidth(-1);
	ImGui::InputTextWithHint("##filter", "Filter variables...", filterBuf, sizeof(filterBuf));

	ImGui::Separator();

	if (ImGui::BeginTable("ConfigVars", 2,
		ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable |
		ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp,
		ImVec2(0, ImGui::GetContentRegionAvail().y - 40))) {

		ImGui::TableSetupScrollFreeze(0, 1);
		ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 0.55f);
		ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 0.45f);
		ImGui::TableHeadersRow();

		for (auto &entry : sorted) {
			// Apply filter
			if (filterBuf[0] && !strcasestr(entry.name, filterBuf))
				continue;

			ImGui::TableNextRow();
			ImGui::PushID(entry.name);

			// Name column
			ImGui::TableNextColumn();

			// Bold if overridden
			bool overridden = entry.pVar && entry.pVar->mbOverridden;
			if (overridden) {
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 0.4f, 1.0f));
			}
			ImGui::TextUnformatted(entry.name);
			if (overridden) {
				ImGui::PopStyleColor();
			}

			// Value column
			ImGui::TableNextColumn();

			if (!entry.pVar) {
				// Undefined variable
				ImGui::TextDisabled("<unknown cvar>");
				if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
					ImGui::OpenPopup("UndefinedCtx");
				if (ImGui::BeginPopup("UndefinedCtx")) {
					if (ImGui::MenuItem("Unset")) {
						ATUnsetUndefinedConfigVar(entry.name);
					}
					ImGui::EndPopup();
				}
			} else if (g_advEditState.active && g_advEditState.pVar == entry.pVar) {
				// Inline editing
				ATConfigVarType type = entry.pVar->GetVarType();

				if (type == ATConfigVarType::Bool) {
					if (ImGui::Checkbox("##editbool", &g_advEditState.editBool)) {
						static_cast<ATConfigVarBool &>(*entry.pVar) = g_advEditState.editBool;
						g_advEditState.active = false;
					}
					if (ImGui::IsKeyPressed(ImGuiKey_Escape))
						g_advEditState.active = false;
				} else if (type == ATConfigVarType::Float) {
					// Drag-to-edit for floats (mirrors Win32 mouse-drag behavior)
					float val;
					sscanf(g_advEditState.editBuf, "%f", &val);
					if (ImGui::DragFloat("##editfloat", &val, 0.001f, 0.0f, 0.0f, "%.4f")) {
						snprintf(g_advEditState.editBuf, sizeof(g_advEditState.editBuf), "%.4f", val);
						static_cast<ATConfigVarFloat &>(*entry.pVar) = val;
					}
					if (ImGui::IsKeyPressed(ImGuiKey_Escape) || ImGui::IsKeyPressed(ImGuiKey_Enter))
						g_advEditState.active = false;
				} else if (type == ATConfigVarType::RGBColor) {
					uint32 rgb = 0;
					sscanf(g_advEditState.editBuf, "%x", &rgb);
					float col[3] = {
						((rgb >> 16) & 0xFF) / 255.0f,
						((rgb >> 8) & 0xFF) / 255.0f,
						(rgb & 0xFF) / 255.0f,
					};
					if (ImGui::ColorEdit3("##editcolor", col, ImGuiColorEditFlags_NoInputs)) {
						uint32 newRgb = ((uint32)(col[0] * 255.0f) << 16)
							| ((uint32)(col[1] * 255.0f) << 8)
							| (uint32)(col[2] * 255.0f);
						snprintf(g_advEditState.editBuf, sizeof(g_advEditState.editBuf), "%06X", newRgb);
						static_cast<ATConfigVarRGBColor &>(*entry.pVar) = newRgb;
					}
					if (ImGui::IsKeyPressed(ImGuiKey_Escape))
						g_advEditState.active = false;
				} else {
					// Int32 or other text-based editing
					ImGui::SetNextItemWidth(-1);
					if (ImGui::InputText("##editval", g_advEditState.editBuf, sizeof(g_advEditState.editBuf),
						ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)) {
						if (entry.pVar->FromString(g_advEditState.editBuf))
							g_advEditState.active = false;
					}
					if (!ImGui::IsItemActive() && ImGui::IsKeyPressed(ImGuiKey_Escape))
						g_advEditState.active = false;
					// Auto-focus on first frame
					if (ImGui::IsWindowAppearing())
						ImGui::SetKeyboardFocusHere(-1);
				}
			} else {
				// Display mode
				VDStringA valStr = entry.pVar->ToString();

				if (entry.pVar->GetVarType() == ATConfigVarType::RGBColor) {
					// Show color swatch
					uint32 rgb = 0;
					sscanf(valStr.c_str(), "%x", &rgb);
					ImVec4 col(((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f, (rgb & 0xFF) / 255.0f, 1.0f);
					ImGui::ColorButton("##swatch", col, ImGuiColorEditFlags_NoPicker | ImGuiColorEditFlags_NoTooltip, ImVec2(16, 16));
					ImGui::SameLine();
				}

				ImGui::TextUnformatted(valStr.c_str());

				// Double-click to edit
				if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
					g_advEditState.pVar = entry.pVar;
					g_advEditState.active = true;
					VDStringA s = entry.pVar->ToString();
					strncpy(g_advEditState.editBuf, s.c_str(), sizeof(g_advEditState.editBuf) - 1);
					g_advEditState.editBuf[sizeof(g_advEditState.editBuf) - 1] = 0;
					if (entry.pVar->GetVarType() == ATConfigVarType::Bool)
						g_advEditState.editBool = static_cast<ATConfigVarBool &>(*entry.pVar);
				}

				// Right-click context menu
				if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
					ImGui::OpenPopup("VarCtx");
				if (ImGui::BeginPopup("VarCtx")) {
					if (ImGui::MenuItem("Edit")) {
						g_advEditState.pVar = entry.pVar;
						g_advEditState.active = true;
						VDStringA s = entry.pVar->ToString();
						strncpy(g_advEditState.editBuf, s.c_str(), sizeof(g_advEditState.editBuf) - 1);
						g_advEditState.editBuf[sizeof(g_advEditState.editBuf) - 1] = 0;
						if (entry.pVar->GetVarType() == ATConfigVarType::Bool)
							g_advEditState.editBool = static_cast<ATConfigVarBool &>(*entry.pVar);
					}
					if (ImGui::MenuItem("Reset to Default")) {
						entry.pVar->Unset();
					}
					ImGui::EndPopup();
				}
			}

			ImGui::PopID();
		}

		ImGui::EndTable();
	}

	// Bottom buttons
	ImGui::Spacing();
	float buttonWidth = 80;
	if (ImGui::Button("Close", ImVec2(buttonWidth, 0))) {
		state.showAdvancedConfig = false;
		g_advEditState.active = false;
	}

	ImGui::End();
}

