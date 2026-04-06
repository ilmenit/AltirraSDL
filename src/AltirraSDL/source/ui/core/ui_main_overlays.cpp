//	AltirraSDL - Exit confirm + drag-drop overlay (split from ui_main.cpp Phase 3g)

#include <stdafx.h>
#include <SDL3/SDL.h>
#include <imgui.h>
#include <vd2/system/vdtypes.h>
#include <vd2/system/VDString.h>
#include <vd2/system/text.h>
#include <vd2/system/error.h>
#include <vd2/system/file.h>
#include <vd2/system/filesys.h>
#include "ui_main.h"
#include "ui_main_internal.h"
#include "simulator.h"
#include "uitypes.h"
#include "diskinterface.h"
#include "cassette.h"
#include "cartridge.h"
#include "mediamanager.h"
#include <at/atio/cassetteimage.h>
#include <at/atio/image.h>
#include "uiaccessors.h"
#include "debugger.h"

extern ATSimulator g_sim;

// =========================================================================
// Exit confirmation dialog
// =========================================================================

// Persistent message string — built when the dialog opens, displayed until closed.
static VDStringA g_exitConfirmMsgUtf8;

// Build the dirty-storage message, matching Windows ATUIConfirmDiscardAllStorageGetMessage().
// Returns UTF-8 string ready for ImGui, using "  " indent instead of "\t" (tabs
// don't render as indentation in ImGui).
static VDStringA BuildDirtyStorageMessage(ATSimulator &sim) {
	vdfastvector<ATStorageId> dirtyIds;
	sim.GetDirtyStorage(dirtyIds, ~(uint32)0);

	vdfastvector<ATDebuggerStorageId> dbgDirtyIds;
	IATDebugger *dbg = ATGetDebugger();
	if (dbg)
		dbg->GetDirtyStorage(dbgDirtyIds);

	if (dirtyIds.empty() && dbgDirtyIds.empty())
		return VDStringA();

	std::sort(dirtyIds.begin(), dirtyIds.end());
	std::sort(dbgDirtyIds.begin(), dbgDirtyIds.end());

	VDStringA msg;
	msg = "The following modified items have not been saved:\n\n";
	msg += "  Contents of emulation memory\n";

	for (const ATStorageId id : dirtyIds) {
		const uint32 type = id & kATStorageId_TypeMask;
		const uint32 unit = id & kATStorageId_UnitMask;

		switch (type) {
			case kATStorageId_Cartridge:
				msg += "  Cartridge";
				if (unit)
					msg.append_sprintf(" %u", unit + 1);
				break;

			case kATStorageId_Disk:
				msg.append_sprintf("  Disk (D%u:)", unit + 1);
				break;

			case kATStorageId_Tape:
				msg += "  Tape";
				break;

			case kATStorageId_Firmware:
				switch (unit) {
					case 0: msg += "  IDE main firmware"; break;
					case 1: msg += "  IDE SDX firmware"; break;
					case 2: msg += "  Ultimate1MB firmware"; break;
					case 3: msg += "  Rapidus flash firmware"; break;
					case 4: msg += "  Rapidus PBI firmware"; break;
				}
				break;
		}
		msg += '\n';
	}

	for (const ATDebuggerStorageId id : dbgDirtyIds) {
		switch (id) {
			case kATDebuggerStorageId_CustomSymbols:
				msg += "  Debugger: Custom Symbols\n";
				break;
			default:
				break;
		}
	}

	msg += "\nAre you sure you want to exit?";
	return msg;
}

void ATUIRenderExitConfirm(ATSimulator &sim, ATUIState &state) {
	// First frame: build the message.
	if (g_exitConfirmMsgUtf8.empty()) {
		g_exitConfirmMsgUtf8 = BuildDirtyStorageMessage(sim);

		// Windows Altirra always confirms on exit:
		// - If dirty storage: lists dirty items + memory warning
		// - If nothing dirty: still warns about emulation memory loss
		if (g_exitConfirmMsgUtf8.empty())
			g_exitConfirmMsgUtf8 = "Any unsaved work in emulation memory will be lost.\n\nAre you sure you want to exit?";
	}

	ImGui::SetNextWindowSize(ImVec2(440, 0), ImGuiCond_Appearing);
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
		ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

	bool open = state.showExitConfirm;
	if (!ImGui::Begin("Confirm Exit", &open,
			ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize
			| ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
		if (!open) {
			state.showExitConfirm = false;
			g_exitConfirmMsgUtf8.clear();
		}
		ImGui::End();
		return;
	}

	// User closed via title bar X button or ESC
	if (!open || ATUICheckEscClose()) {
		state.showExitConfirm = false;
		g_exitConfirmMsgUtf8.clear();
		ImGui::End();
		return;
	}

	ImGui::TextWrapped("%s", g_exitConfirmMsgUtf8.c_str());
	ImGui::Separator();

	float buttonWidth = 120.0f;
	float spacing = ImGui::GetStyle().ItemSpacing.x;
	float totalWidth = buttonWidth * 2 + spacing;
	ImGui::SetCursorPosX((ImGui::GetWindowWidth() - totalWidth) * 0.5f);

	if (ImGui::Button("OK", ImVec2(buttonWidth, 0))) {
		state.showExitConfirm = false;
		state.exitConfirmed = true;
		g_exitConfirmMsgUtf8.clear();
		ImGui::End();

		// Push quit event so the main loop exits.
		SDL_Event quit{};
		quit.type = SDL_EVENT_QUIT;
		SDL_PushEvent(&quit);
		return;
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(buttonWidth, 0))) {
		state.showExitConfirm = false;
		g_exitConfirmMsgUtf8.clear();
		ImGui::End();
		return;
	}

	ImGui::End();
}

// =========================================================================
// Drag-and-drop overlay — highlight drop target during file drag
// =========================================================================

void ATUIRenderDragDropOverlay() {
	if (!g_dragDropState.active)
		return;

	ImDrawList *fg = ImGui::GetForegroundDrawList();
	float cx = g_dragDropState.x;
	float cy = g_dragDropState.y;

	// Check which drop target the cursor is over
	struct DropTarget {
		ImVec2 pos, size;
		const char *label;
	};
	DropTarget target = {};

	ImVec2 p, s;
	if (ATUIDiskExplorerGetDropRect(p, s)
		&& cx >= p.x && cy >= p.y && cx <= p.x + s.x && cy <= p.y + s.y) {
		target = {p, s, "Import to disk image"};
	} else if (ATUIFirmwareManagerGetDropRect(p, s)
		&& cx >= p.x && cy >= p.y && cx <= p.x + s.x && cy <= p.y + s.y) {
		target = {p, s, "Add firmware"};
	} else {
		// Default: entire viewport = boot image
		ImVec2 vp = ImGui::GetMainViewport()->Pos;
		ImVec2 vs = ImGui::GetMainViewport()->Size;
		target = {vp, vs, "Boot image"};
	}

	// Draw highlight border around target window
	ImU32 borderColor = IM_COL32(80, 160, 255, 200);
	ImU32 fillColor = IM_COL32(80, 160, 255, 30);
	float thickness = 3.0f;
	ImVec2 tl = target.pos;
	ImVec2 br = ImVec2(tl.x + target.size.x, tl.y + target.size.y);
	fg->AddRectFilled(tl, br, fillColor);
	fg->AddRect(tl, br, borderColor, 0.0f, 0, thickness);

	// Draw label tooltip near cursor
	ImVec2 textSize = ImGui::CalcTextSize(target.label);
	ImVec2 padding = ImVec2(8, 4);
	ImVec2 labelPos = ImVec2(cx + 16, cy + 16);

	// Keep label within viewport
	ImVec2 vpMax = ImVec2(ImGui::GetMainViewport()->Pos.x + ImGui::GetMainViewport()->Size.x,
		ImGui::GetMainViewport()->Pos.y + ImGui::GetMainViewport()->Size.y);
	if (labelPos.x + textSize.x + padding.x * 2 > vpMax.x)
		labelPos.x = cx - textSize.x - padding.x * 2 - 4;
	if (labelPos.y + textSize.y + padding.y * 2 > vpMax.y)
		labelPos.y = cy - textSize.y - padding.y * 2 - 4;

	ImVec2 bgMin = labelPos;
	ImVec2 bgMax = ImVec2(labelPos.x + textSize.x + padding.x * 2,
		labelPos.y + textSize.y + padding.y * 2);
	fg->AddRectFilled(bgMin, bgMax, IM_COL32(30, 30, 30, 220), 4.0f);
	fg->AddRect(bgMin, bgMax, borderColor, 4.0f);
	fg->AddText(ImVec2(labelPos.x + padding.x, labelPos.y + padding.y),
		IM_COL32(255, 255, 255, 255), target.label);
}

