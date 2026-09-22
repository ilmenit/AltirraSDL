//	Altirra SDL3 frontend — Keyboard Customize Dialog
//	ImGui replacement for Windows IDD_KEYBOARD_CUSTOMIZE
//
//	Copyright (C) 2009-2015 Avery Lee (original Windows dialog)
//	SDL3/ImGui port: 2024-2026
//
//	This program is free software; you can redistribute it and/or modify
//	it under the terms of the GNU General Public License as published by
//	the Free Software Foundation; either version 2 of the License, or
//	(at your option) any later version.

#include <stdafx.h>
#include <algorithm>
#include <mutex>
#include <string>
#include <unordered_map>
#include <SDL3/SDL.h>
#include "ui_file_dialog_sdl3.h"
#include <imgui.h>
#include <vd2/system/vdtypes.h>
#include <vd2/system/VDString.h>
#include <vd2/system/vdstl.h>
#include <vd2/vdjson/jsonreader.h>
#include <vd2/vdjson/jsonwriter.h>
#include <vd2/vdjson/jsonoutput.h>
#include <vd2/vdjson/jsonvalue.h>
#include <vd2/system/file.h>
#include <vd2/system/text.h>
#include <vd2/Dita/accel.h>
#include "uikeyboard.h"
#include "accel_sdl3.h"
#include "uiaccessors.h"
#include "inputdefs.h"
#include "ui_main.h"
#include "ui_keyboard_customize_internal.h"


// =========================================================================
// Dialog state
// =========================================================================

struct KbdCustomizeState {
	bool initialized = false;
	vdfastvector<uint32> mappings;          // working copy
	vdfastvector<uint32> filteredScanCodes; // indices into kScanCodeTable after search
	uint32 scanCodeToEntry[0x200];          // reverse: scan code → table index

	char searchBuf[128] = {};
	int selectedScanCodeIdx = -1;    // index into filteredScanCodes
	int selectedBindingIdx = -1;     // index into mappings
	bool cookedMode = false;         // character vs VK mode
	bool capturingKey = false;
	uint32 capturedMapping = 0;      // packed host key (no scan code)
	bool hasCaptured = false;

	bool confirmClearOpen = false;
	bool scrollBindingIntoView = false;  // request scroll in right panel
	bool scrollScanCodeIntoView = false; // request scroll in left panel
	bool captureJustCompleted = false;   // one-shot: auto-search bindings after capture
	bool pendingAddConflict = false;     // waiting for conflict confirmation
	uint32 pendingAddMapping = 0;        // mapping to add after confirm

	// Deferred file operations — set by async SDL file dialog callbacks,
	// processed on the main thread in ATUIRenderKeyboardCustomize.
	std::mutex pendingFileMutex;
	std::string pendingImportPath;
	std::string pendingExportPath;
};

static KbdCustomizeState s_kc;

static void RebuildScanCodeToEntry() {
	for (auto& e : s_kc.scanCodeToEntry)
		e = (uint32)kScanCodeTableSize;
	for (int i = 0; i < kScanCodeTableSize; ++i)
		s_kc.scanCodeToEntry[kScanCodeTable[i]] = (uint32)i;
}

static bool IsScanCodeMapped(uint32 scanCode) {
	for (uint32 m : s_kc.mappings)
		if ((m & 0x1FF) == scanCode)
			return true;
	return false;
}

static void RebuildFilteredList() {
	s_kc.filteredScanCodes.clear();
	for (int i = 0; i < kScanCodeTableSize; ++i) {
		const char *label = GetNameForKeyCode(kScanCodeTable[i]);
		if (!label)
			continue;

		if (s_kc.searchBuf[0]) {
			// Case-insensitive search
			bool found = false;
			const char *search = s_kc.searchBuf;
			const char *hay = label;
			size_t slen = strlen(search);
			size_t hlen = strlen(hay);
			if (slen <= hlen) {
				for (size_t p = 0; p <= hlen - slen; ++p) {
					bool match = true;
					for (size_t j = 0; j < slen; ++j) {
						if (tolower((unsigned char)hay[p + j]) != tolower((unsigned char)search[j])) {
							match = false;
							break;
						}
					}
					if (match) { found = true; break; }
				}
			}
			if (!found)
				continue;
		}

		s_kc.filteredScanCodes.push_back((uint32)i);
	}
}

static bool CompareMappingOrder(uint32 a, uint32 b) {
	if ((a ^ b) & 0x1FF) {
		uint32 idxA = s_kc.scanCodeToEntry[a & 0x1FF];
		uint32 idxB = s_kc.scanCodeToEntry[b & 0x1FF];
		return idxA < idxB;
	}
	return a < b;
}

static void SortMappings() {
	std::sort(s_kc.mappings.begin(), s_kc.mappings.end(), CompareMappingOrder);
}

static void InitDialog() {
	s_kc.initialized = true;
	ATUIGetCustomKeyMap(s_kc.mappings);
	SortMappings();
	RebuildScanCodeToEntry();
	s_kc.searchBuf[0] = 0;
	s_kc.selectedScanCodeIdx = -1;
	s_kc.selectedBindingIdx = -1;
	s_kc.cookedMode = false;
	s_kc.capturingKey = false;
	s_kc.capturedMapping = 0;
	s_kc.hasCaptured = false;
	s_kc.confirmClearOpen = false;
	s_kc.captureJustCompleted = false;
	s_kc.pendingAddConflict = false;
	s_kc.pendingAddMapping = 0;
	RebuildFilteredList();
}

// Check if a VK mode mapping conflicts with app shortcuts in the accelerator
// tables.  Returns the command name of the conflict, or nullptr if none.
static const char *CheckShortcutConflict(uint32 mapping) {
	if (mapping & kATUIKeyboardMappingModifier_Cooked)
		return nullptr;  // character mode can't conflict with VK shortcuts

	uint32 vk = (mapping >> 9) & 0xFFFF;
	bool hasShift = (mapping & kATUIKeyboardMappingModifier_Shift) != 0;
	bool hasCtrl  = (mapping & kATUIKeyboardMappingModifier_Ctrl) != 0;
	bool hasAlt   = (mapping & kATUIKeyboardMappingModifier_Alt) != 0;
	bool hasExt   = (mapping & kATUIKeyboardMappingModifier_Extended) != 0;

	// Check all accel contexts for any binding with this VK+modifiers
	for (int ctx = 0; ctx < kATUIAccelContextCount; ++ctx) {
		const VDAccelTableEntry *e = ATUIFindConflictingVirtKeyMapping(
			vk, hasAlt, hasCtrl, hasShift, hasExt, (ATUIAccelContext)ctx);
		if (e)
			return e->mpCommand;
	}

	return nullptr;
}

// =========================================================================
// Import/Export
// =========================================================================

static void ImportFromFile(const char *utf8Path) {
	try {
		VDFile f(VDTextU8ToW(VDStringA(utf8Path)).c_str());
		sint64 size = f.size();
		if (size > 0x1000000)
			return;

		vdblock<char> buf((uint32)size);
		f.read(buf.data(), (long)size);
		f.close();

		VDJSONDocument doc;
		VDJSONReader reader;
		if (!reader.Parse(buf.data(), buf.size(), doc))
			return;

		const auto& rootNode = doc.Root();
		if (!rootNode.IsObject())
			return;

		if (wcscmp(rootNode[".type"].AsString(), L"keymap"))
			return;

		const auto& mappingsNode = rootNode["mappings"];
		if (!mappingsNode.IsArray())
			return;

		vdfastvector<uint32> mappings;
		size_t n = mappingsNode.GetArrayLength();
		for (size_t i = 0; i < n; ++i) {
			const auto& mappingNode = mappingsNode[i];
			if (!mappingNode.IsObject())
				return;

			const auto& scanCodeNode = mappingNode["scancode"];
			if (!scanCodeNode.IsValid())
				return;

			sint64 scanCode = scanCodeNode.AsInt64();
			if (scanCode < 0 || scanCode > 511 || !ATIsValidScanCodeLocal((uint32)scanCode))
				continue;

			const auto& charNode = mappingNode["char"];
			if (charNode.IsValid()) {
				uint32 charCode = 0;
				if (charNode.IsString()) {
					const wchar_t *s = charNode.AsString();
					if (!*s || s[1])
						return;
					charCode = (uint32)(uint16)*s;
				} else if (charNode.IsInt()) {
					sint64 cc = charNode.AsInt64();
					if (cc < 0 || cc >= 65535)
						continue;
					charCode = (uint16)cc;
				} else
					return;
				mappings.push_back(ATUIPackKeyboardMapping((uint32)scanCode, charCode, kATUIKeyboardMappingModifier_Cooked));
			} else {
				const auto& vkNode = mappingNode["vk"];
				sint64 vk;
				if (vkNode.IsInt()) {
					vk = vkNode.AsInt64();
					if (vk <= 0 || vk > 65535)
						continue;
				} else if (vkNode.IsString()) {
					const wchar_t *s = vkNode.AsString();
					if (!s[0] || s[1])
						return;
					uint32 ch = (uint32)s[0];
					if ((ch - (uint32)'0') >= 10 && (ch - (uint32)'A') >= 26)
						return;
					vk = ch;
				} else
					return;

				const auto& modifiersNode = mappingNode["modifiers"];
				sint64 mods = 0;
				if (modifiersNode.IsValid()) {
					if (!modifiersNode.IsInt())
						return;
					mods = modifiersNode.AsInt64();
					if (mods <= 0 || mods > 15)
						continue;
				}
				mappings.push_back(ATUIPackKeyboardMapping((uint32)scanCode, (uint32)vk, (uint32)mods << 25));
			}
		}

		std::sort(mappings.begin(), mappings.end());
		mappings.erase(std::unique(mappings.begin(), mappings.end()), mappings.end());

		s_kc.mappings.swap(mappings);
		SortMappings();
		RebuildFilteredList();
	} catch (...) {
		// silently ignore errors
	}
}

static void ExportToFile(const char *utf8Path) {
	try {
		VDStringW s;
		VDJSONStringWriterOutputSysLE output(s);
		VDJSONWriter writer;

		writer.Begin(&output);
		writer.OpenObject();
		writer.WriteMemberName(L".comment");
		writer.WriteString(L"Altirra keyboard map");
		writer.WriteMemberName(L".type");
		writer.WriteString(L"keymap");
		writer.WriteMemberName(L"mappings");

		writer.OpenArray();

		auto sortedMappings = s_kc.mappings;
		std::sort(sortedMappings.begin(), sortedMappings.end());

		for (uint32 mapping : sortedMappings) {
			writer.OpenObject();
			writer.WriteMemberName(L"scancode");
			writer.WriteInt(mapping & 0x1FF);

			if (mapping & kATUIKeyboardMappingModifier_Cooked) {
				writer.WriteMemberName(L"char");
				uint32 ch = (mapping >> 9) & 0xFFFF;
				if (ch >= 0x20 && ch < 0x7F) {
					wchar_t buf[2] = { (wchar_t)ch, 0 };
					writer.WriteString(buf);
				} else
					writer.WriteInt(ch);
			} else {
				writer.WriteMemberName(L"vk");
				uint32 vk = (mapping >> 9) & 0xFFFF;
				if ((vk - 0x30) < 10 || (vk - 0x41) < 26) {
					wchar_t buf[2] = { (wchar_t)vk, 0 };
					writer.WriteString(buf);
				} else
					writer.WriteInt(vk);

				if (mapping >> 25) {
					writer.WriteMemberName(L"modifiers");
					writer.WriteInt(mapping >> 25);
				}
			}

			writer.Close();
		}
		writer.Close();
		writer.Close();
		writer.End();

		VDFileStream fs(VDTextU8ToW(VDStringA(utf8Path)).c_str(),
			nsVDFile::kWrite | nsVDFile::kCreateAlways | nsVDFile::kDenyAll | nsVDFile::kSequential);
		VDStringA u8s(VDTextWToU8(s));
		fs.write(u8s.data(), u8s.size());
	} catch (...) {
		// silently ignore errors
	}
}

static void SDLCALL ImportFileCallback(void *, const char * const *filelist, int) {
	if (filelist && filelist[0]) {
		std::lock_guard<std::mutex> lock(s_kc.pendingFileMutex);
		s_kc.pendingImportPath = filelist[0];
	}
}

static void SDLCALL ExportFileCallback(void *, const char * const *filelist, int) {
	if (filelist && filelist[0]) {
		std::lock_guard<std::mutex> lock(s_kc.pendingFileMutex);
		s_kc.pendingExportPath = filelist[0];
	}
}

// =========================================================================
// Main dialog render
// =========================================================================

void ATUIRenderKeyboardCustomize(ATUIState &state) {
	if (!state.showKeyboardCustomize)
		return;

	if (!s_kc.initialized)
		InitDialog();

	// Process deferred file operations from async SDL file dialog callbacks
	{
		std::lock_guard<std::mutex> lock(s_kc.pendingFileMutex);
		if (!s_kc.pendingImportPath.empty()) {
			ImportFromFile(s_kc.pendingImportPath.c_str());
			s_kc.pendingImportPath.clear();
		}
		if (!s_kc.pendingExportPath.empty()) {
			ExportToFile(s_kc.pendingExportPath.c_str());
			s_kc.pendingExportPath.clear();
		}
	}

	const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
	ImGui::SetNextWindowSize(ImVec2(740, 520), ImGuiCond_Appearing);
	ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

	ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;
	if (!ImGui::Begin("Keyboard Customize", &state.showKeyboardCustomize, flags)) {
		ImGui::End();
		if (!state.showKeyboardCustomize)
			s_kc.initialized = false;
		return;
	}

	// ESC closes dialog only when NOT capturing a key
	if (!s_kc.capturingKey && ATUICheckEscClose()) {
		state.showKeyboardCustomize = false;
		s_kc.initialized = false;
		ImGui::End();
		return;
	}

	// Search
	ImGui::SetNextItemWidth(200);
	if (ImGui::InputTextWithHint("##Search", "Search emulated keys...", s_kc.searchBuf, sizeof(s_kc.searchBuf))) {
		RebuildFilteredList();
		// Reset left panel selection — matches Windows ReloadEmuKeyList() which
		// clears and rebuilds the list (implicitly deselecting).
		s_kc.selectedScanCodeIdx = -1;
	}

	ImGui::Separator();

	// Main content: left panel (emulated keys) + right panel (bindings)
	float panelHeight = ImGui::GetContentRegionAvail().y - 110;  // leave room for bottom controls

	// Left panel: emulated key list
	ImGui::BeginChild("EmuKeys", ImVec2(220, panelHeight), ImGuiChildFlags_Borders);
	for (int i = 0; i < (int)s_kc.filteredScanCodes.size(); ++i) {
		uint32 tableIdx = s_kc.filteredScanCodes[i];
		uint32 sc = kScanCodeTable[tableIdx];
		const char *label = GetNameForKeyCode(sc);
		if (!label)
			continue;

		char displayBuf[128];
		if (!IsScanCodeMapped(sc))
			snprintf(displayBuf, sizeof(displayBuf), "%s [not mapped]", label);
		else
			snprintf(displayBuf, sizeof(displayBuf), "%s", label);

		bool selected = (s_kc.selectedScanCodeIdx == i);
		if (ImGui::Selectable(displayBuf, selected)) {
			s_kc.selectedScanCodeIdx = i;

			// Auto-select first binding for this scan code and scroll to it
			for (int j = 0; j < (int)s_kc.mappings.size(); ++j) {
				if ((s_kc.mappings[j] & 0x1FF) == sc) {
					s_kc.selectedBindingIdx = j;
					s_kc.scrollBindingIntoView = true;
					break;
				}
			}
		}
		// Scroll left panel when right panel changes selection
		if (selected && s_kc.scrollScanCodeIntoView) {
			ImGui::SetScrollHereY();
			s_kc.scrollScanCodeIntoView = false;
		}
	}
	ImGui::EndChild();

	ImGui::SameLine();

	// Right panel: host key bindings table
	ImGui::BeginChild("Bindings", ImVec2(0, panelHeight), ImGuiChildFlags_Borders);
	if (ImGui::BeginTable("BindingsTable", 2,
			ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY)) {
		ImGui::TableSetupColumn("Emulation Key", ImGuiTableColumnFlags_WidthFixed, 180);
		ImGui::TableSetupColumn("Host Key", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableHeadersRow();

		for (int i = 0; i < (int)s_kc.mappings.size(); ++i) {
			uint32 mapping = s_kc.mappings[i];
			uint32 sc = mapping & 0x1FF;
			uint32 tableIdx = s_kc.scanCodeToEntry[sc];
			const char *emuName = (tableIdx < (uint32)kScanCodeTableSize) ? GetNameForKeyCode(sc) : "?";

			char hostKeyBuf[128];
			FormatMappingHostKey(mapping, hostKeyBuf, sizeof(hostKeyBuf));

			ImGui::TableNextRow();
			ImGui::TableNextColumn();

			bool selected = (s_kc.selectedBindingIdx == i);
			char idBuf[32];
			snprintf(idBuf, sizeof(idBuf), "##bind%d", i);

			if (ImGui::Selectable(idBuf, selected, ImGuiSelectableFlags_SpanAllColumns)) {
				s_kc.selectedBindingIdx = i;

				// Sync left panel selection and request scroll
				if (tableIdx < (uint32)kScanCodeTableSize) {
					for (int j = 0; j < (int)s_kc.filteredScanCodes.size(); ++j) {
						if (s_kc.filteredScanCodes[j] == tableIdx) {
							s_kc.selectedScanCodeIdx = j;
							s_kc.scrollScanCodeIntoView = true;
							break;
						}
					}
				}

				// Show current binding in captured display
				s_kc.capturedMapping = mapping & 0xFFFFFE00;
				s_kc.hasCaptured = true;
				s_kc.cookedMode = (mapping & kATUIKeyboardMappingModifier_Cooked) != 0;
			}

			// Scroll right panel when left panel changes selection
			if (selected && s_kc.scrollBindingIntoView) {
				ImGui::SetScrollHereY();
				s_kc.scrollBindingIntoView = false;
			}

			ImGui::SameLine(0, 0);
			ImGui::TextUnformatted(emuName ? emuName : "?");

			ImGui::TableNextColumn();
			ImGui::TextUnformatted(hostKeyBuf);
		}

		ImGui::EndTable();
	}
	ImGui::EndChild();

	ImGui::Separator();

	// Mode selection: Virtual Key / Character
	if (ImGui::RadioButton("Virtual Key", !s_kc.cookedMode))
		s_kc.cookedMode = false;
	ImGui::SameLine();
	if (ImGui::RadioButton("Character", s_kc.cookedMode))
		s_kc.cookedMode = true;
	ImGui::SameLine(0, 30);
	ImGui::TextUnformatted("Host key:");
	ImGui::SameLine();

	// Key capture area
	{
		char captureBuf[128];
		if (s_kc.capturingKey)
			snprintf(captureBuf, sizeof(captureBuf), "[Press a key...]");
		else if (s_kc.hasCaptured) {
			// Display the captured mapping (with dummy scan code 0)
			FormatMappingHostKey(s_kc.capturedMapping, captureBuf, sizeof(captureBuf));
		} else
			snprintf(captureBuf, sizeof(captureBuf), "(none)");

		ImGui::SetNextItemWidth(200);
		ImGui::InputText("##HostKey", captureBuf, sizeof(captureBuf), ImGuiInputTextFlags_ReadOnly);

		if (ImGui::IsItemClicked()) {
			s_kc.capturingKey = true;
			s_kc.hasCaptured = false;
		}
	}

	// Key capture logic
	if (s_kc.capturingKey) {
		ImGui::SetNextFrameWantCaptureKeyboard(true);

		// Escape cancels capture without binding (matches ui_input.cpp pattern)
		if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
			s_kc.capturingKey = false;
		} else if (s_kc.cookedMode) {
			// Character mode: capture from text input queue
			ImGuiIO& io = ImGui::GetIO();
			if (!io.InputQueueCharacters.empty()) {
				uint32 ch = io.InputQueueCharacters[0];
				if (ch > 0 && ch < 0x10000) {
					s_kc.capturedMapping = ATUIPackKeyboardMapping(0, ch, kATUIKeyboardMappingModifier_Cooked);
					s_kc.hasCaptured = true;
					s_kc.capturingKey = false;
					s_kc.captureJustCompleted = true;
				}
				io.InputQueueCharacters.clear();
			}
		} else {
			// VK mode: detect key press via ImGui
			for (int kmi = 0; kmi < kImGuiKeyMapSize; ++kmi) {
				const auto& km = kImGuiKeyMap[kmi];
				// Skip modifier keys and Escape (handled above)
				if (km.key == ImGuiKey_LeftCtrl || km.key == ImGuiKey_RightCtrl
					|| km.key == ImGuiKey_Escape)
					continue;

				if (ImGui::IsKeyPressed(km.key, false)) {
					uint32 modifiers = 0;
					if (ImGui::IsKeyDown(ImGuiKey_LeftShift) || ImGui::IsKeyDown(ImGuiKey_RightShift))
						modifiers |= kATUIKeyboardMappingModifier_Shift;
					if (ImGui::IsKeyDown(ImGuiKey_LeftCtrl) || ImGui::IsKeyDown(ImGuiKey_RightCtrl))
						modifiers |= kATUIKeyboardMappingModifier_Ctrl;
					if (ImGui::IsKeyDown(ImGuiKey_LeftAlt) || ImGui::IsKeyDown(ImGuiKey_RightAlt))
						modifiers |= kATUIKeyboardMappingModifier_Alt;
					if (IsExtendedVK(km.vk))
						modifiers |= kATUIKeyboardMappingModifier_Extended;

					s_kc.capturedMapping = ATUIPackKeyboardMapping(0, km.vk, modifiers);
					s_kc.hasCaptured = true;
					s_kc.capturingKey = false;
					s_kc.captureJustCompleted = true;
					break;
				}
			}
		}
	}

	// After capture completes, auto-select existing binding with same host key
	// (matches Windows OnHotKeyChanged — lets user see what's already mapped)
	if (s_kc.captureJustCompleted) {
		s_kc.captureJustCompleted = false;
		uint32 capturedBase = s_kc.capturedMapping & 0xFFFFFE00;
		for (int j = 0; j < (int)s_kc.mappings.size(); ++j) {
			if ((s_kc.mappings[j] & 0xFFFFFE00) == capturedBase) {
				s_kc.selectedBindingIdx = j;
				s_kc.scrollBindingIntoView = true;

				uint32 sc = s_kc.mappings[j] & 0x1FF;
				uint32 tableIdx = s_kc.scanCodeToEntry[sc];
				if (tableIdx < (uint32)kScanCodeTableSize) {
					for (int k = 0; k < (int)s_kc.filteredScanCodes.size(); ++k) {
						if (s_kc.filteredScanCodes[k] == tableIdx) {
							s_kc.selectedScanCodeIdx = k;
							s_kc.scrollScanCodeIntoView = true;
							break;
						}
					}
				}
				break;
			}
		}
	}

	ImGui::Separator();

	// Action buttons
	{
		// Helper: actually insert the mapping
		auto doAddMapping = [](uint32 mapping) {
			// Remove existing mappings with same host key
			for (size_t i = s_kc.mappings.size(); i; --i) {
				uint32 diff = s_kc.mappings[i - 1] ^ mapping;
				if (!(diff & 0xFFFFFE00))
					s_kc.mappings.erase(s_kc.mappings.begin() + (i - 1));
			}

			// Insert sorted
			auto it = std::lower_bound(s_kc.mappings.begin(), s_kc.mappings.end(), mapping, CompareMappingOrder);
			int insertIdx = (int)(it - s_kc.mappings.begin());
			s_kc.mappings.insert(it, mapping);
			s_kc.selectedBindingIdx = insertIdx;
			s_kc.scrollBindingIntoView = true;
			RebuildFilteredList();
		};

		// Add
		bool canAdd = s_kc.hasCaptured && s_kc.selectedScanCodeIdx >= 0
			&& s_kc.selectedScanCodeIdx < (int)s_kc.filteredScanCodes.size()
			&& s_kc.filteredScanCodes[s_kc.selectedScanCodeIdx] < (uint32)kScanCodeTableSize;
		if (!canAdd) ImGui::BeginDisabled();
		if (ImGui::Button("Add") && canAdd) {
			uint32 tableIdx = s_kc.filteredScanCodes[s_kc.selectedScanCodeIdx];
			uint32 sc = kScanCodeTable[tableIdx];
			uint32 mapping = s_kc.capturedMapping + sc;

			// Check for shortcut conflict (matches Windows ATUIFindConflictingVirtKeyMapping)
			const char *conflict = CheckShortcutConflict(s_kc.capturedMapping);
			if (conflict) {
				s_kc.pendingAddMapping = mapping;
				s_kc.pendingAddConflict = true;
				ImGui::OpenPopup("ShortcutConflict");
			} else {
				doAddMapping(mapping);
			}
		}
		if (!canAdd) ImGui::EndDisabled();

		// Shortcut conflict confirmation popup
		if (ImGui::BeginPopupModal("ShortcutConflict", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
			char hostBuf[128];
			FormatMappingHostKey(s_kc.pendingAddMapping, hostBuf, sizeof(hostBuf));
			const char *conflict = CheckShortcutConflict(s_kc.pendingAddMapping & 0xFFFFFE00);
			ImGui::Text("This key (%s) is already bound as a keyboard shortcut\nto: %s",
				hostBuf, conflict ? conflict : "unknown");
			ImGui::TextUnformatted("The shortcut will override this mapping. Bind it anyway?");
			ImGui::Separator();
			if (ImGui::Button("Yes", ImVec2(80, 0))) {
				doAddMapping(s_kc.pendingAddMapping);
				s_kc.pendingAddConflict = false;
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button("No", ImVec2(80, 0))) {
				s_kc.pendingAddConflict = false;
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}

		ImGui::SameLine();

		// Remove
		bool canRemove = s_kc.selectedBindingIdx >= 0 && s_kc.selectedBindingIdx < (int)s_kc.mappings.size();
		if (!canRemove) ImGui::BeginDisabled();
		if (ImGui::Button("Remove")) {
			s_kc.mappings.erase(s_kc.mappings.begin() + s_kc.selectedBindingIdx);
			if (s_kc.selectedBindingIdx >= (int)s_kc.mappings.size())
				s_kc.selectedBindingIdx = (int)s_kc.mappings.size() - 1;
			RebuildFilteredList();
		}
		if (!canRemove) ImGui::EndDisabled();

		ImGui::SameLine();

		// Clear
		bool canClear = !s_kc.mappings.empty();
		if (!canClear) ImGui::BeginDisabled();
		if (ImGui::Button("Clear"))
			ImGui::OpenPopup("ConfirmClear");
		if (!canClear) ImGui::EndDisabled();

		if (ImGui::BeginPopupModal("ConfirmClear", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
			ImGui::TextUnformatted("Clear all key mappings?");
			ImGui::Separator();
			if (ImGui::Button("OK", ImVec2(80, 0))) {
				s_kc.mappings.clear();
				s_kc.selectedBindingIdx = -1;
				RebuildFilteredList();
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button("Cancel", ImVec2(80, 0)))
				ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}

		ImGui::SameLine(0, 20);

		// Import
		if (ImGui::Button("Import...")) {
			SDL_DialogFileFilter filter = { "Altirra keyboard map", "atkmap" };
			ATUIShowOpenFileDialog('kmap', ImportFileCallback, nullptr,
				SDL_GetKeyboardFocus(), &filter, 1, false);
		}

		ImGui::SameLine();

		// Export
		bool canExport = !s_kc.mappings.empty();
		if (!canExport) ImGui::BeginDisabled();
		if (ImGui::Button("Export...")) {
			SDL_DialogFileFilter filter = { "Altirra keyboard map", "atkmap" };
			ATUIShowSaveFileDialog('kmap', ExportFileCallback, nullptr,
				SDL_GetKeyboardFocus(), &filter, 1);
		}
		if (!canExport) ImGui::EndDisabled();

		ImGui::SameLine(ImGui::GetContentRegionAvail().x - 170);

		// OK
		if (ImGui::Button("OK", ImVec2(80, 0))) {
			ATUISetCustomKeyMap(s_kc.mappings.data(), s_kc.mappings.size());
			// Re-sort the stored map for binary search at runtime
			extern ATUIKeyboardOptions g_kbdOpts;
			ATUIInitVirtualKeyMap(g_kbdOpts);
			state.showKeyboardCustomize = false;
			s_kc.initialized = false;
		}

		ImGui::SameLine();

		// Cancel
		if (ImGui::Button("Cancel", ImVec2(80, 0))) {
			state.showKeyboardCustomize = false;
			s_kc.initialized = false;
		}
	}

	ImGui::End();
}
