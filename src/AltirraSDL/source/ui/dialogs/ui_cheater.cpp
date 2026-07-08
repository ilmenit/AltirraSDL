//	AltirraSDL - Cheater dialog
//	ImGui implementation of the Windows IDD_CHEATER / IDD_CHEAT_EDIT dialogs.
//	Provides memory search, active cheat management, and .atcheats file I/O.

#include <stdafx.h>
#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <mutex>
#include <vector>
#include <SDL3/SDL.h>
#include <imgui.h>
#include "ui_file_dialog_sdl3.h"

#include <vd2/system/vdtypes.h>
#include <vd2/system/VDString.h>
#include <vd2/system/text.h>

#include "ui_main.h"
#include "simulator.h"
#include "cheatengine.h"
#include "logging.h"

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
static char s_editErrorMessage[96] = "";
static char s_errorMessage[192] = "";
static std::vector<uint32> s_cheatOrder;

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
	s_totalMatches = ce->GetValidOffsets(s_resultOffsets, kMaxSearchResults);
	s_resultCount = s_totalMatches >= kMaxSearchResults ? 0 : s_totalMatches;
	s_selectedResult = -1;
}

static bool ParseUintField(const char *text, uint32& value) {
	while (*text == ' ' || *text == '\t')
		++text;

	if (!*text)
		return false;

	int base = 10;
	if (*text == '$') {
		base = 16;
		++text;
	}

	errno = 0;
	char *end = nullptr;
	unsigned long v = strtoul(text, &end, base);
	if (end == text || errno == ERANGE)
		return false;

	while (*end == ' ' || *end == '\t')
		++end;

	if (*end)
		return false;

	value = (uint32)v;
	return true;
}

static bool ParseSintField(const char *text, sint32& value) {
	while (*text == ' ' || *text == '\t')
		++text;

	if (!*text)
		return false;

	if (*text == '$') {
		uint32 v = 0;
		if (!ParseUintField(text, v))
			return false;

		value = (sint32)v;
		return true;
	}

	errno = 0;
	char *end = nullptr;
	long v = strtol(text, &end, 10);
	if (end == text || errno == ERANGE)
		return false;

	while (*end == ' ' || *end == '\t')
		++end;

	if (*end)
		return false;

	value = (sint32)v;
	return true;
}

static void AddCheatFromResult(ATCheatEngine *ce, uint32 offset, bool bit16, bool enabled) {
	ATCheatEngine::Cheat cheat {};
	cheat.mAddress = offset;
	cheat.mValue = (uint16)ce->GetOffsetCurrentValue(offset, bit16);
	cheat.mb16Bit = bit16;
	cheat.mbEnabled = enabled;
	ce->AddCheat(cheat);
}

static void OpenEditCheatPopup(bool isNew, int editIdx, const ATCheatEngine::Cheat *cheat) {
	s_showEditCheat = true;
	s_editIsNew = isNew;
	s_editIndex = editIdx;
	s_editErrorMessage[0] = 0;
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

	ImGui::Text("Address:"); ImGui::SameLine();
	ImGui::SetNextItemWidth(80);
	ImGui::InputText("##addr", s_editAddress, sizeof(s_editAddress));

	ImGui::Text("Value:  "); ImGui::SameLine();
	ImGui::SetNextItemWidth(80);
	ImGui::InputText("##val", s_editValue, sizeof(s_editValue));

	ImGui::RadioButton("8-bit (0-255)", &s_editIs16Bit, 0); ImGui::SameLine();
	ImGui::RadioButton("16-bit (0-65535)", &s_editIs16Bit, 1);

	if (s_editErrorMessage[0]) {
		ImGui::PushStyleColor(ImGuiCol_Text, ATUIColorWarningText());
		ImGui::TextUnformatted(s_editErrorMessage);
		ImGui::PopStyleColor();
	}

	ImGui::Separator();

	if (ImGui::Button("OK", ImVec2(80, 0))) {
		uint32 addr = 0;
		uint32 val = 0;
		if (ParseUintField(s_editAddress, addr) &&
			ParseUintField(s_editValue, val)) {
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
			ImGui::CloseCurrentPopup();
			s_editErrorMessage[0] = 0;
		} else {
			snprintf(s_editErrorMessage, sizeof s_editErrorMessage,
				"Invalid address or value.");
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(80, 0))) {
		s_editErrorMessage[0] = 0;
		ImGui::CloseCurrentPopup();
	}

	ImGui::EndPopup();
}

// Deferred file operations from async SDL file dialog callbacks.
static std::mutex s_cheatFileMutex;
static std::string s_pendingCheatLoadPath;
static std::string s_pendingCheatSavePath;

static void LoadCheatsCallback(void *, const char * const *filelist, int) {
	if (!filelist || !filelist[0])
		return;
	std::lock_guard<std::mutex> lock(s_cheatFileMutex);
	s_pendingCheatLoadPath = filelist[0];
}

static void SaveCheatsCallback(void *, const char * const *filelist, int) {
	if (!filelist || !filelist[0])
		return;
	std::lock_guard<std::mutex> lock(s_cheatFileMutex);
	s_pendingCheatSavePath = filelist[0];
}

void ATUIRenderCheater(ATSimulator &sim, ATUIState &state) {
	// Enable cheat engine on first open (idempotent, but avoid per-frame call)
	if (!sim.GetCheatEngine()) {
		sim.SetCheatEngineEnabled(true);
	}
	ATCheatEngine *ce = sim.GetCheatEngine();
	if (!ce)
		return;

	// Process deferred file operations from async SDL file dialog callbacks
	{
		std::lock_guard<std::mutex> lock(s_cheatFileMutex);
		if (!s_pendingCheatLoadPath.empty()) {
			VDStringW wpath = VDTextU8ToW(VDStringSpanA(s_pendingCheatLoadPath.c_str()));
			s_pendingCheatLoadPath.clear();
			try {
				ce->Load(wpath.c_str());
				s_resultCount = 0;
				s_totalMatches = 0;
				s_selectedResult = -1;
				s_selectedCheat = -1;
			} catch (const MyError &e) {
				LOG_ERROR("UI", "Failed to load cheats: %s", e.c_str());
			}
		}
		if (!s_pendingCheatSavePath.empty()) {
			VDStringW wpath = VDTextU8ToW(VDStringSpanA(s_pendingCheatSavePath.c_str()));
			s_pendingCheatSavePath.clear();
			try {
				ce->Save(wpath.c_str());
			} catch (const MyError &e) {
				LOG_ERROR("UI", "Failed to save cheats: %s", e.c_str());
			}
		}
	}

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
	ImGui::InputText("##searchval", s_searchValue, sizeof(s_searchValue));
	if (!needsValue) ImGui::EndDisabled();

	ImGui::SameLine();
	if (ImGui::Button("Update")) {
		uint32 refVal = 0;
		const bool bit16 = s_search16Bit != 0;
		s_errorMessage[0] = 0;

		switch(s_searchMode) {
			case kATCheatSnapMode_Replace:
				ce->Snapshot(kATCheatSnapMode_Replace, 0, false);
				break;

			case kATCheatSnapMode_Equal:
			case kATCheatSnapMode_NotEqual:
			case kATCheatSnapMode_Less:
			case kATCheatSnapMode_LessEqual:
			case kATCheatSnapMode_Greater:
			case kATCheatSnapMode_GreaterEqual:
				ce->Snapshot((ATCheatSnapshotMode)s_searchMode, 0, bit16);
				break;

			case kATCheatSnapMode_EqualRef:
				{
					sint32 signedVal = 0;
					if (!ParseSintField(s_searchValue, signedVal)) {
						snprintf(s_errorMessage, sizeof s_errorMessage,
							"Invalid search value.");
						break;
					}

					if (bit16) {
						if (signedVal < -32768 || signedVal > 65535) {
							snprintf(s_errorMessage, sizeof s_errorMessage,
								"Invalid search value. The search value must be within -32768 to 65535.");
							break;
						}
					} else {
						if (signedVal < -128 || signedVal > 255) {
							snprintf(s_errorMessage, sizeof s_errorMessage,
								"Invalid search value. The search value must be within -128 to 255.");
							break;
						}
					}

					refVal = (uint32)signedVal;
					ce->Snapshot(kATCheatSnapMode_EqualRef, refVal, bit16);
				}
				break;

			default:
				break;
		}
		if (!s_errorMessage[0])
			UpdateSearchResults(ce);
	}

	ImGui::SameLine();
	ImGui::RadioButton("8-bit", &s_search16Bit, 0); ImGui::SameLine();
	ImGui::RadioButton("16-bit", &s_search16Bit, 1);

	if (s_totalMatches >= kMaxSearchResults)
		ImGui::Text("Results: %u shown of %u matches", s_resultCount, s_totalMatches);
	else
		ImGui::Text("Results: %u matches", s_totalMatches);

	if (s_errorMessage[0]) {
		ImGui::PushStyleColor(ImGuiCol_Text, ATUIColorWarningText());
		ImGui::TextUnformatted(s_errorMessage);
		ImGui::PopStyleColor();
	}

	// --- Two-column layout: results | transfer buttons | active cheats ---
	float availW = ImGui::GetContentRegionAvail().x;
	float btnColW = 50;
	float listW = (availW - btnColW) * 0.5f;
	float listH = ImGui::GetContentRegionAvail().y - 40; // reserve space for bottom buttons

	// Left column: Search Results
	ImGui::BeginChild("##results", ImVec2(listW, listH), ImGuiChildFlags_Borders);
	ImGui::Text("Search Results");
	ImGui::Separator();

	if (!s_totalMatches) {
		ImGui::TextUnformatted("No results left. Try again.");
	} else if (s_totalMatches >= kMaxSearchResults) {
		ImGui::Text("Too many results (%u).", s_totalMatches);
	} else {
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
				if (ImGui::IsMouseDoubleClicked(0))
					AddCheatFromResult(ce, off, s_search16Bit != 0, false);
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

	const bool canTransfer = s_selectedResult >= 0 && s_selectedResult < (int)s_resultCount;
	if (!canTransfer) ImGui::BeginDisabled();
	if (ImGui::Button(">", ImVec2(36, 0)) && canTransfer)
		AddCheatFromResult(ce, s_resultOffsets[s_selectedResult], s_search16Bit != 0, false);
	if (!canTransfer) ImGui::EndDisabled();
	ImGui::SetItemTooltip("Transfer selected result to active cheats");

	const bool canTransferAll = s_totalMatches > 0;
	if (!canTransferAll) ImGui::BeginDisabled();
	if (ImGui::Button(">>", ImVec2(36, 0))) {
		for (uint32 i = 0; i < s_resultCount; ++i)
			AddCheatFromResult(ce, s_resultOffsets[i], s_search16Bit != 0, true);
	}
	if (!canTransferAll) ImGui::EndDisabled();
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

	s_cheatOrder.clear();
	s_cheatOrder.reserve(cheatCount);
	for (uint32 i = 0; i < cheatCount; ++i)
		s_cheatOrder.push_back(i);

	std::sort(s_cheatOrder.begin(), s_cheatOrder.end(),
		[ce](uint32 a, uint32 b) {
			const ATCheatEngine::Cheat& ca = ce->GetCheatByIndex(a);
			const ATCheatEngine::Cheat& cb = ce->GetCheatByIndex(b);

			if (ca.mAddress != cb.mAddress)
				return ca.mAddress < cb.mAddress;

			return a < b;
		});

	for (uint32 i : s_cheatOrder) {
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
		ATUIShowOpenFileDialog('CHET', LoadCheatsCallback, nullptr,
			SDL_GetKeyboardFocus(), kCheatFilters, 2, false);
	}
	ImGui::SameLine();
	if (ImGui::Button("Save...")) {
		static const SDL_DialogFileFilter kCheatFilters[] = {
			{ "Altirra cheat set", "atcheats" },
			{ "All Files", "*" },
		};
		ATUIShowSaveFileDialog('CHET', SaveCheatsCallback, nullptr,
			SDL_GetKeyboardFocus(), kCheatFilters, 2);
	}

	// Edit cheat popup (modal)
	RenderEditCheatPopup(ce);
	RenderEditCheatPopupContent(ce);

	ImGui::End();
}
