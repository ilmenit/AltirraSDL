//	AltirraSDL - Tools dialog (split from ui_tools.cpp, Phase 2k)

#include <stdafx.h>
#include <algorithm>
#include <string>
#include <mutex>
#include <thread>
#include <vector>
#include <cstring>
#include <cstdio>
#include <imgui.h>
#include <SDL3/SDL.h>
#include <vd2/system/vdtypes.h>
#include <vd2/system/VDString.h>
#include <vd2/system/text.h>
#include <vd2/system/file.h>
#include <vd2/system/filesys.h>
#include <vd2/system/error.h>
#include <vd2/system/date.h>
#include <vd2/system/registry.h>
#include <vd2/system/vdstl.h>
#include <at/atcore/configvar.h>
#include <at/atcore/propertyset.h>
#include <at/atcore/media.h>
#include <at/atio/image.h>
#include <at/atio/diskimage.h>
#include <at/atio/cartridgeimage.h>
#include <at/atio/cassetteimage.h>
#include <vd2/Dita/accel.h>
#include "ui_main.h"
#include "accel_sdl3.h"
#include "simulator.h"
#include "gtia.h"
#include "constants.h"
#include "disk.h"
#include "diskinterface.h"
#include "firmwaremanager.h"
#include "firmwaredetect.h"
#include "compatengine.h"
#include "settings.h"
#include "uiaccessors.h"
#include "uikeyboard.h"
#include "uitypes.h"
#include "options.h"
#include "oshelper.h"

extern ATSimulator g_sim;

// =========================================================================
// Keyboard Shortcuts
// Reference: src/ATNativeUI/source/acceleditdialog.cpp
//
// Data-driven shortcut editor using accelerator tables.
// Reads live data from ATUIGetAccelTables(), supports rebinding via
// double-click or "Rebind" button, conflict detection, and persistence.
// =========================================================================

// State for the shortcut editor
static int s_selectedRow = -1;
static int s_selectedContext = -1;
static int s_selectedIndex = -1;
static bool s_captureRequested = false;
static bool s_showConflict = false;
static char s_conflictCmd[256] = {};

void ATUIRenderKeyboardShortcuts(ATUIState &state) {
	static const char *kContextNames[] = { "Global", "Display", "Debugger" };

	ImGui::SetNextWindowSize(ImVec2(620, 480), ImGuiCond_Appearing);
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	if (!ImGui::Begin("Keyboard Shortcuts", &state.showKeyboardShortcuts, ImGuiWindowFlags_NoSavedSettings)) {
		ImGui::End();
		return;
	}

	// Handle pending shortcut assign from right-click menu
	if (!g_pendingShortcutAssign.empty()) {
		// Find the command in the accel tables and select it
		for (int ctx = 0; ctx < kATUIAccelContextCount; ++ctx) {
			const VDAccelTableDefinition& table = ATUIGetAccelTables()[ctx];
			for (uint32 i = 0; i < table.GetSize(); ++i) {
				if (!strcmp(table[i].mpCommand, g_pendingShortcutAssign.c_str())) {
					s_selectedContext = ctx;
					s_selectedIndex = (int)i;
					s_captureRequested = true;
					goto found;
				}
			}
		}
		found:
		g_pendingShortcutAssign.clear();
	}

	if (ATUICheckEscClose() && !g_shortcutCaptureActive) {
		state.showKeyboardShortcuts = false;
		ImGui::End();
		return;
	}

	// Filter
	static char filterBuf[128] = {};
	ImGui::SetNextItemWidth(200);
	ImGui::InputTextWithHint("##filter", "Filter...", filterBuf, sizeof(filterBuf));
	ImGui::SameLine();
	ImGui::TextDisabled("(?)");
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("Double-click a row to rebind its shortcut.\n"
			"Press ESC to cancel during key capture.");

	ImGui::Separator();

	float buttonAreaHeight = 35;
	if (ImGui::BeginTable("Shortcuts", 3,
		ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable |
		ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp,
		ImVec2(0, ImGui::GetContentRegionAvail().y - buttonAreaHeight))) {

		ImGui::TableSetupScrollFreeze(0, 1);
		ImGui::TableSetupColumn("Command", ImGuiTableColumnFlags_WidthStretch, 0.45f);
		ImGui::TableSetupColumn("Shortcut", ImGuiTableColumnFlags_WidthStretch, 0.30f);
		ImGui::TableSetupColumn("Context", ImGuiTableColumnFlags_WidthStretch, 0.25f);
		ImGui::TableHeadersRow();

		int rowId = 0;
		for (int ctx = 0; ctx < kATUIAccelContextCount; ++ctx) {
			const VDAccelTableDefinition& table = ATUIGetAccelTables()[ctx];
			for (uint32 i = 0; i < table.GetSize(); ++i) {
				const VDAccelTableEntry& entry = table[i];

				// Skip key-up entries (e.g. PulseWarpOff) — they're paired with key-down
				if (entry.mAccel.mModifiers & VDUIAccelerator::kModUp)
					continue;

				// Format display strings
				VDStringW accelStrW;
				VDUIGetAcceleratorString(entry.mAccel, accelStrW);
				VDStringA accelStr = VDTextWToU8(accelStrW);

				// Apply filter (search raw command name and shortcut string)
				if (filterBuf[0]) {
					if (!strcasestr(entry.mpCommand, filterBuf) &&
						!strcasestr(accelStr.c_str(), filterBuf))
						continue;
				}

				ImGui::TableNextRow();
				ImGui::TableNextColumn();

				// Selectable row — show raw command name (matches Windows dialog)
				bool isSelected = (s_selectedContext == ctx && s_selectedIndex == (int)i);
				ImGui::PushID(rowId);
				if (ImGui::Selectable(entry.mpCommand, isSelected,
					ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)) {
					s_selectedContext = ctx;
					s_selectedIndex = (int)i;
					s_selectedRow = rowId;

					if (ImGui::IsMouseDoubleClicked(0))
						s_captureRequested = true;
				}

				// Right-click context menu on rows
				if (ImGui::BeginPopupContextItem("##row_ctx")) {
					s_selectedContext = ctx;
					s_selectedIndex = (int)i;
					if (ImGui::MenuItem("Rebind..."))
						s_captureRequested = true;
					if (ImGui::MenuItem("Remove")) {
						ATUIGetAccelTables()[ctx].RemoveAt(i);
						ATUISaveAccelTables();
						ATUIInvalidateShortcutCache();
						s_selectedContext = -1;
						s_selectedIndex = -1;
					}
					ImGui::EndPopup();
				}

				ImGui::PopID();

				ImGui::TableNextColumn();
				ImGui::TextUnformatted(accelStr.c_str());
				ImGui::TableNextColumn();
				ImGui::TextDisabled("%s", kContextNames[ctx]);

				rowId++;
			}
		}

		ImGui::EndTable();
	}

	// Buttons
	ImGui::Spacing();

	if (ImGui::Button("Rebind", ImVec2(80, 0))) {
		if (s_selectedContext >= 0 && s_selectedIndex >= 0)
			s_captureRequested = true;
	}
	ImGui::SameLine();

	if (ImGui::Button("Remove", ImVec2(80, 0))) {
		if (s_selectedContext >= 0 && s_selectedIndex >= 0) {
			VDAccelTableDefinition& table = ATUIGetAccelTables()[s_selectedContext];
			if (s_selectedIndex < (int)table.GetSize()) {
				table.RemoveAt(s_selectedIndex);
				ATUISaveAccelTables();
				ATUIInvalidateShortcutCache();
				s_selectedContext = -1;
				s_selectedIndex = -1;
			}
		}
	}
	ImGui::SameLine();

	if (ImGui::Button("Reset to Defaults", ImVec2(130, 0))) {
		const VDAccelTableDefinition *defaults = ATUIGetDefaultAccelTables();
		for (int i = 0; i < kATUIAccelContextCount; ++i)
			ATUIGetAccelTables()[i] = defaults[i];
		ATUISaveAccelTables();
		ATUIInvalidateShortcutCache();
		s_selectedContext = -1;
		s_selectedIndex = -1;
	}
	ImGui::SameLine();

	float closeX = ImGui::GetContentRegionAvail().x - 80;
	if (closeX > 0)
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + closeX);
	if (ImGui::Button("Close", ImVec2(80, 0)))
		state.showKeyboardShortcuts = false;

	// ------------------------------------------------------------------
	// Key capture modal
	// ------------------------------------------------------------------
	if (s_captureRequested && s_selectedContext >= 0 && s_selectedIndex >= 0) {
		s_captureRequested = false;
		g_shortcutCaptureResult.captured = false;
		g_shortcutCaptureActive = true;
		s_showConflict = false;
		ImGui::OpenPopup("Capture Key");
	}

	// Key capture popup
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	if (ImGui::BeginPopupModal("Capture Key", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
		if (s_selectedContext >= 0 && s_selectedIndex >= 0) {
			const VDAccelTableDefinition& table = ATUIGetAccelTables()[s_selectedContext];
			if (s_selectedIndex < (int)table.GetSize())
				ImGui::Text("Press a key combination for: %s", table[s_selectedIndex].mpCommand);
		}
		ImGui::Text("(Press ESC to cancel)");
		ImGui::Spacing();

		if (g_shortcutCaptureResult.captured) {
			g_shortcutCaptureActive = false;

			// Check for conflicts
			const VDAccelTableEntry *conflict = ATUIFindConflictingVirtKeyMapping(
				g_shortcutCaptureResult.vk,
				(g_shortcutCaptureResult.modifiers & VDUIAccelerator::kModAlt) != 0,
				(g_shortcutCaptureResult.modifiers & VDUIAccelerator::kModCtrl) != 0,
				(g_shortcutCaptureResult.modifiers & VDUIAccelerator::kModShift) != 0,
				(g_shortcutCaptureResult.modifiers & VDUIAccelerator::kModExtended) != 0,
				(ATUIAccelContext)s_selectedContext);

			// Don't flag conflict with itself
			if (conflict && s_selectedContext >= 0 && s_selectedIndex >= 0) {
				const VDAccelTableDefinition& table = ATUIGetAccelTables()[s_selectedContext];
				if (s_selectedIndex < (int)table.GetSize() &&
					!strcmp(conflict->mpCommand, table[s_selectedIndex].mpCommand))
					conflict = nullptr;
			}

			if (conflict) {
				snprintf(s_conflictCmd, sizeof(s_conflictCmd), "%s", conflict->mpCommand);
				s_showConflict = true;
			} else {
				// Apply the binding — cache command name before RemoveAt frees it
				VDAccelTableDefinition& table = ATUIGetAccelTables()[s_selectedContext];
				if (s_selectedIndex < (int)table.GetSize()) {
					VDStringA cmdName(table[s_selectedIndex].mpCommand);
					VDAccelTableEntry newEntry;
					newEntry.mpCommand = cmdName.c_str();
					newEntry.mCommandId = 0;
					newEntry.mAccel.mVirtKey = g_shortcutCaptureResult.vk;
					newEntry.mAccel.mModifiers = g_shortcutCaptureResult.modifiers;
					table.RemoveAt(s_selectedIndex);
					table.Add(newEntry);  // Add() copies the string
					ATUISaveAccelTables();
					ATUIInvalidateShortcutCache();
					// Select the newly added entry (it's at the end)
					s_selectedIndex = (int)table.GetSize() - 1;
				}
				g_shortcutCaptureResult.captured = false;
				ImGui::CloseCurrentPopup();
			}
		}

		if (!g_shortcutCaptureActive && !g_shortcutCaptureResult.captured && !s_showConflict)
			ImGui::CloseCurrentPopup();

		ImGui::EndPopup();
	}

	// Conflict confirmation popup
	if (s_showConflict) {
		ImGui::OpenPopup("Shortcut Conflict");
		s_showConflict = false;
	}

	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	if (ImGui::BeginPopupModal("Shortcut Conflict", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
		VDUIAccelerator newAccel;
		newAccel.mVirtKey = g_shortcutCaptureResult.vk;
		newAccel.mModifiers = g_shortcutCaptureResult.modifiers;
		VDStringW accelStrW;
		VDUIGetAcceleratorString(newAccel, accelStrW);
		VDStringA accelStr = VDTextWToU8(accelStrW);

		ImGui::Text("%s is already assigned to: %s", accelStr.c_str(), s_conflictCmd);
		ImGui::Text("Replace the existing binding?");
		ImGui::Spacing();

		if (ImGui::Button("Replace", ImVec2(100, 0))) {
			// Cache the command name before any table modifications
			VDStringA selectedCmd;
			if (s_selectedContext >= 0 && s_selectedIndex >= 0) {
				VDAccelTableDefinition& selTable = ATUIGetAccelTables()[s_selectedContext];
				if (s_selectedIndex < (int)selTable.GetSize())
					selectedCmd = selTable[s_selectedIndex].mpCommand;
			}

			// Remove the conflicting binding
			for (int ctx = 0; ctx < kATUIAccelContextCount; ++ctx) {
				VDAccelTableDefinition& table = ATUIGetAccelTables()[ctx];
				for (uint32 i = 0; i < table.GetSize(); ++i) {
					if (!strcmp(table[i].mpCommand, s_conflictCmd) &&
						table[i].mAccel.mVirtKey == g_shortcutCaptureResult.vk) {
						table.RemoveAt(i);
						// If this was in the same table as our selected entry,
						// adjust the selected index
						if (ctx == s_selectedContext && (int)i < s_selectedIndex)
							s_selectedIndex--;
						break;
					}
				}
			}

			// Apply the new binding
			if (!selectedCmd.empty() && s_selectedContext >= 0 && s_selectedIndex >= 0) {
				VDAccelTableDefinition& table = ATUIGetAccelTables()[s_selectedContext];
				if (s_selectedIndex < (int)table.GetSize()) {
					VDAccelTableEntry newEntry;
					newEntry.mpCommand = selectedCmd.c_str();
					newEntry.mCommandId = 0;
					newEntry.mAccel.mVirtKey = g_shortcutCaptureResult.vk;
					newEntry.mAccel.mModifiers = g_shortcutCaptureResult.modifiers;
					table.RemoveAt(s_selectedIndex);
					table.Add(newEntry);  // Add() copies the string
					s_selectedIndex = (int)table.GetSize() - 1;
				}
			}
			ATUISaveAccelTables();
			ATUIInvalidateShortcutCache();
			g_shortcutCaptureResult.captured = false;
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();

		if (ImGui::Button("Cancel", ImVec2(100, 0))) {
			g_shortcutCaptureResult.captured = false;
			ImGui::CloseCurrentPopup();
		}

		ImGui::EndPopup();
	}

	ImGui::End();
}

