//	AltirraSDL - Cheater dialog
//	ImGui implementation of the Windows IDD_CHEATER / IDD_CHEAT_EDIT dialogs.
//	Provides memory search, active cheat management, and .atcheats file I/O.

#include <stdafx.h>
#include <SDL3/SDL.h>
#include <imgui.h>

#include <vd2/system/vdtypes.h>
#include <vd2/system/VDString.h>
#include <vd2/system/text.h>

#include "ui_main.h"
#include "simulator.h"
#include "cheatengine.h"

extern ATSimulator g_sim;

// Max search results shown (matches Windows limit)
static constexpr uint32 kMaxSearchResults = 250;

// Persistent search state
static int s_searchMode = 0;           // ATCheatSnapshotMode index
static char s_searchValue[16] = "";    // hex/decimal input for EqualRef mode
static int s_search16Bit = 0;          // 0 = 8-bit, 1 = 16-bit
static uint32 s_resultOffsets[kMaxSearchResults];
static uint32 s_resultCount = 0;
static uint32 s_totalMatches = 0;
static int s_selectedResult = -1;
static int s_selectedCheat = -1;

// Edit cheat popup state
static bool s_showEditCheat = false;
static bool s_editIsNew = false;       // true = Add, false = Edit
static int  s_editIndex = -1;          // index in cheat list when editing
static char s_editAddress[8] = "";
static char s_editValue[8] = "";
static int s_editIs16Bit = 0;          // 0 = 8-bit, 1 = 16-bit

static const char *kSearchModeLabels[] = {
	"Start with new snapshot",
	"= : Unchanged",
	"!= : Changed",
	"< : Values going down",
	"<= : Values sometimes going down",
	"> : Values going up",
	">= : Values sometimes going up",
	"=X : Find an exact value",
};

static void UpdateSearchResults(ATCheatEngine *ce) {
	s_totalMatches = ce->GetValidOffsets(nullptr, 0);
	s_resultCount = ce->GetValidOffsets(s_resultOffsets, kMaxSearchResults);
	s_selectedResult = -1;
}

static void OpenEditCheatPopup(bool isNew, int editIdx, const ATCheatEngine::Cheat *cheat) {
	s_showEditCheat = true;
	s_editIsNew = isNew;
	s_editIndex = editIdx;
	if (cheat) {
		snprintf(s_editAddress, sizeof(s_editAddress), "%04X", cheat->mAddress);
		if (cheat->mb16Bit)
			snprintf(s_editValue, sizeof(s_editValue), "%04X", cheat->mValue);
		else
			snprintf(s_editValue, sizeof(s_editValue), "%02X", cheat->mValue);
		s_editIs16Bit = cheat->mb16Bit ? 1 : 0;
	} else {
		s_editAddress[0] = 0;
		s_editValue[0] = 0;
		s_editIs16Bit = s_search16Bit;
	}
}

static const char *GetEditCheatPopupId() {
	return s_editIsNew ? "Add Cheat" : "Edit Cheat";
}

static void RenderEditCheatPopup(ATCheatEngine *ce) {
	if (!s_showEditCheat)
		return;

	ImGui::OpenPopup(GetEditCheatPopupId());
	s_showEditCheat = false;

	// Center the popup
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
		ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
}

static void RenderEditCheatPopupContent(ATCheatEngine *ce) {
	// Try both popup IDs since we don't know which was opened
	const char *popupId = GetEditCheatPopupId();
	if (!ImGui::BeginPopupModal(popupId, nullptr,
			ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings))
		return;

	ImGui::Text("Address (hex):"); ImGui::SameLine();
	ImGui::SetNextItemWidth(80);
	ImGui::InputText("##addr", s_editAddress, sizeof(s_editAddress),
		ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_CharsUppercase);

	ImGui::Text("Value (hex):  "); ImGui::SameLine();
	ImGui::SetNextItemWidth(80);
	ImGui::InputText("##val", s_editValue, sizeof(s_editValue),
		ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_CharsUppercase);

	ImGui::RadioButton("8-bit (0-255)", &s_editIs16Bit, 0); ImGui::SameLine();
	ImGui::RadioButton("16-bit (0-65535)", &s_editIs16Bit, 1);

	ImGui::Separator();

	if (ImGui::Button("OK", ImVec2(80, 0))) {
		unsigned addr = 0, val = 0;
		if (sscanf(s_editAddress, "%x", &addr) == 1 &&
			sscanf(s_editValue, "%x", &val) == 1) {
			ATCheatEngine::Cheat cheat = {};
			cheat.mAddress = addr;
			cheat.mValue = (uint16)val;
			cheat.mb16Bit = (s_editIs16Bit != 0);
			cheat.mbEnabled = true;

			if (s_editIsNew) {
				ce->AddCheat(cheat);
			} else if (s_editIndex >= 0) {
				ce->UpdateCheat((uint32)s_editIndex, cheat);
			}
		}
		ImGui::CloseCurrentPopup();
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(80, 0)))
		ImGui::CloseCurrentPopup();

	ImGui::EndPopup();
}

// File dialog callbacks for load/save
static void LoadCheatsCallback(void *userdata, const char * const *filelist, int filter) {
	if (!filelist || !filelist[0])
		return;

	ATCheatEngine *ce = (ATCheatEngine *)userdata;
	VDStringW wpath = VDTextU8ToW(VDStringSpanA(filelist[0]));
	try {
		ce->Load(wpath.c_str());
		// Load calls Clear() which wipes search state — reset our cached results
		s_resultCount = 0;
		s_totalMatches = 0;
		s_selectedResult = -1;
		s_selectedCheat = -1;
	} catch (const MyError &e) {
		fprintf(stderr, "[AltirraSDL] Failed to load cheats: %s\n", e.c_str());
	}
}

static void SaveCheatsCallback(void *userdata, const char * const *filelist, int filter) {
	if (!filelist || !filelist[0])
		return;

	ATCheatEngine *ce = (ATCheatEngine *)userdata;
	VDStringW wpath = VDTextU8ToW(VDStringSpanA(filelist[0]));
	try {
		ce->Save(wpath.c_str());
	} catch (const MyError &e) {
		fprintf(stderr, "[AltirraSDL] Failed to save cheats: %s\n", e.c_str());
	}
}

void ATUIRenderCheater(ATSimulator &sim, ATUIState &state) {
	// Enable cheat engine on first open (idempotent, but avoid per-frame call)
	if (!sim.GetCheatEngine()) {
		sim.SetCheatEngineEnabled(true);
	}
	ATCheatEngine *ce = sim.GetCheatEngine();
	if (!ce)
		return;

	ImGui::SetNextWindowSize(ImVec2(680, 480), ImGuiCond_Appearing);
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
		ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

	if (!ImGui::Begin("Cheater", &state.showCheater,
			ImGuiWindowFlags_NoSavedSettings)) {
		ImGui::End();
		return;
	}

	if (ATUICheckEscClose()) {
		state.showCheater = false;
		ImGui::End();
		return;
	}

	// --- Search controls ---
	ImGui::SetNextItemWidth(280);
	ImGui::Combo("##mode", &s_searchMode, kSearchModeLabels, IM_ARRAYSIZE(kSearchModeLabels));

	ImGui::SameLine();
	bool needsValue = (s_searchMode == kATCheatSnapMode_EqualRef);
	if (!needsValue) ImGui::BeginDisabled();
	ImGui::SetNextItemWidth(80);
	ImGui::InputText("##searchval", s_searchValue, sizeof(s_searchValue),
		ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_CharsUppercase);
	if (!needsValue) ImGui::EndDisabled();

	ImGui::SameLine();
	if (ImGui::Button("Update")) {
		uint32 refVal = 0;
		if (needsValue)
			sscanf(s_searchValue, "%x", &refVal);
		ce->Snapshot((ATCheatSnapshotMode)s_searchMode, refVal, s_search16Bit);
		UpdateSearchResults(ce);
	}

	ImGui::SameLine();
	ImGui::RadioButton("8-bit", &s_search16Bit, 0); ImGui::SameLine();
	ImGui::RadioButton("16-bit", &s_search16Bit, 1);

	if (s_totalMatches > kMaxSearchResults)
		ImGui::Text("Results: %u shown of %u matches", s_resultCount, s_totalMatches);
	else
		ImGui::Text("Results: %u matches", s_totalMatches);

	// --- Two-column layout: results | transfer buttons | active cheats ---
	float availW = ImGui::GetContentRegionAvail().x;
	float btnColW = 50;
	float listW = (availW - btnColW) * 0.5f;
	float listH = ImGui::GetContentRegionAvail().y - 40; // reserve space for bottom buttons

	// Left column: Search Results
	ImGui::BeginChild("##results", ImVec2(listW, listH), ImGuiChildFlags_Borders);
	ImGui::Text("Search Results");
	ImGui::Separator();

	for (uint32 i = 0; i < s_resultCount; ++i) {
		uint32 off = s_resultOffsets[i];
		uint32 val = ce->GetOffsetCurrentValue(off, s_search16Bit);

		char label[64];
		if (s_search16Bit)
			snprintf(label, sizeof(label), "$%04X  $%04X (%u)##r%u", off, val, val, i);
		else
			snprintf(label, sizeof(label), "$%04X  $%02X (%u)##r%u", off, val, val, i);

		bool selected = (s_selectedResult == (int)i);
		if (ImGui::Selectable(label, selected, ImGuiSelectableFlags_AllowDoubleClick)) {
			s_selectedResult = (int)i;
			if (ImGui::IsMouseDoubleClicked(0)) {
				ce->AddCheat(off, s_search16Bit);
			}
		}
	}
	ImGui::EndChild();

	// Middle column: Transfer buttons
	ImGui::SameLine();
	ImGui::BeginChild("##xfer", ImVec2(btnColW, listH));
	ImGui::Spacing();
	float midY = listH * 0.4f;
	ImGui::SetCursorPosY(midY);

	if (ImGui::Button(">", ImVec2(36, 0)) && s_selectedResult >= 0 && s_selectedResult < (int)s_resultCount) {
		ce->AddCheat(s_resultOffsets[s_selectedResult], s_search16Bit);
	}
	ImGui::SetItemTooltip("Transfer selected result to active cheats");

	if (ImGui::Button(">>", ImVec2(36, 0))) {
		for (uint32 i = 0; i < s_resultCount; ++i)
			ce->AddCheat(s_resultOffsets[i], s_search16Bit);
	}
	ImGui::SetItemTooltip("Transfer all results to active cheats");
	ImGui::EndChild();

	// Right column: Active Cheats
	ImGui::SameLine();
	ImGui::BeginChild("##active", ImVec2(listW, listH), ImGuiChildFlags_Borders);
	ImGui::Text("Active Cheats");
	ImGui::Separator();

	uint32 cheatCount = ce->GetCheatCount();
	if (s_selectedCheat >= (int)cheatCount)
		s_selectedCheat = -1;

	for (uint32 i = 0; i < cheatCount; ++i) {
		ATCheatEngine::Cheat cheat = ce->GetCheatByIndex(i);

		ImGui::PushID((int)i);

		// Checkbox for enabled/disabled
		bool enabled = cheat.mbEnabled;
		if (ImGui::Checkbox("##en", &enabled)) {
			cheat.mbEnabled = enabled;
			ce->UpdateCheat(i, cheat);
		}
		ImGui::SameLine();

		char label[64];
		if (cheat.mb16Bit)
			snprintf(label, sizeof(label), "$%04X = $%04X (%u)", cheat.mAddress, cheat.mValue, cheat.mValue);
		else
			snprintf(label, sizeof(label), "$%04X = $%02X (%u)", cheat.mAddress, cheat.mValue, cheat.mValue);

		bool selected = (s_selectedCheat == (int)i);
		if (ImGui::Selectable(label, selected, ImGuiSelectableFlags_AllowDoubleClick)) {
			s_selectedCheat = (int)i;
			if (ImGui::IsMouseDoubleClicked(0))
				OpenEditCheatPopup(false, (int)i, &cheat);
		}

		ImGui::PopID();
	}
	ImGui::EndChild();

	// --- Bottom buttons ---
	if (ImGui::Button("Add...")) {
		OpenEditCheatPopup(true, -1, nullptr);
	}
	ImGui::SameLine();

	bool hasSelection = (s_selectedCheat >= 0 && s_selectedCheat < (int)cheatCount);
	if (!hasSelection) ImGui::BeginDisabled();
	if (ImGui::Button("Edit...") && hasSelection) {
		const ATCheatEngine::Cheat &ch = ce->GetCheatByIndex((uint32)s_selectedCheat);
		OpenEditCheatPopup(false, s_selectedCheat, &ch);
	}
	ImGui::SameLine();
	if (ImGui::Button("Delete") && hasSelection) {
		ce->RemoveCheatByIndex((uint32)s_selectedCheat);
		s_selectedCheat = -1;
	}
	if (!hasSelection) ImGui::EndDisabled();

	ImGui::SameLine();
	ImGui::Text("|");
	ImGui::SameLine();

	if (ImGui::Button("Load...")) {
		static const SDL_DialogFileFilter kCheatFilters[] = {
			{ "Altirra cheat set", "atcheats" },
			{ "All Files", "*" },
		};
		SDL_ShowOpenFileDialog(LoadCheatsCallback, ce,
			SDL_GetKeyboardFocus(), kCheatFilters, 2, nullptr, false);
	}
	ImGui::SameLine();
	if (ImGui::Button("Save...")) {
		static const SDL_DialogFileFilter kCheatFilters[] = {
			{ "Altirra cheat set", "atcheats" },
			{ "All Files", "*" },
		};
		SDL_ShowSaveFileDialog(SaveCheatsCallback, ce,
			SDL_GetKeyboardFocus(), kCheatFilters, 2, nullptr);
	}

	// Edit cheat popup (modal)
	RenderEditCheatPopup(ce);
	RenderEditCheatPopupContent(ce);

	ImGui::End();
}
