//	AltirraSDL - Dear ImGui debugger targets pane
//	Replaces Win32 ATTargetsWindow (uidbgtargets.cpp).
//	Lists all available debug targets (CPU cores, peripherals) with their
//	type and clock frequency.  Double-click to switch the active target.

#include <stdafx.h>
#include <imgui.h>
#include <vd2/system/vdtypes.h>
#include <vd2/system/VDString.h>
#include <vd2/system/vdstl.h>
#include <vd2/system/text.h>
#include <at/atdebugger/target.h>
#include "ui_debugger.h"
#include "console.h"
#include "debugger.h"
#include "simulator.h"

extern ATSimulator g_sim;

// =========================================================================
// Targets pane
// =========================================================================

class ATImGuiTargetsPaneImpl final : public ATImGuiDebuggerPane {
public:
	ATImGuiTargetsPaneImpl();

	bool Render() override;
	void OnDebuggerSystemStateUpdate(const ATDebuggerSystemState& state) override;
	void OnDebuggerEvent(ATDebugEvent eventId) override;
	bool ActivateSelectedForTest(int row,
		uint32& outCurrentTarget,
		uint32& outTargetCount,
		VDStringA& outError);

private:
	void RebuildList();
	bool ActivateSelectedTarget();

	struct TargetEntry {
		uint32 mIndex;
		bool mbCurrent;
		VDStringA mName;
		VDStringA mType;		// disasm mode name + clock frequency
	};

	std::vector<TargetEntry> mEntries;
	bool mbNeedsRebuild = true;
	int mSelectedRow = -1;
};

ATImGuiTargetsPaneImpl::ATImGuiTargetsPaneImpl()
	: ATImGuiDebuggerPane(kATUIPaneId_Targets, "Targets")
{
}

void ATImGuiTargetsPaneImpl::OnDebuggerSystemStateUpdate(const ATDebuggerSystemState& state) {
	ATImGuiDebuggerPane::OnDebuggerSystemStateUpdate(state);
}

void ATImGuiTargetsPaneImpl::OnDebuggerEvent(ATDebugEvent eventId) {
	if (eventId == kATDebugEvent_TargetsChanged
		|| eventId == kATDebugEvent_CurrentTargetChanged)
		mbNeedsRebuild = true;
}

void ATImGuiTargetsPaneImpl::RebuildList() {
	mbNeedsRebuild = false;
	mEntries.clear();

	IATDebugger *dbg = ATGetDebugger();
	if (!dbg)
		return;

	vdfastvector<IATDebugTarget *> targets;
	dbg->GetTargetList(targets);

	uint32 currentIdx = dbg->GetTargetIndex();

	for (uint32 i = 0; i < (uint32)targets.size(); ++i) {
		IATDebugTarget *t = targets[i];
		if (!t)
			continue;

		TargetEntry entry;
		entry.mIndex = i;
		entry.mbCurrent = (i == currentIdx);
		entry.mName = t->GetName();

		// Type: disasm mode display name + clock frequency
		// ATDebugGetDisasmModeDisplayName returns wchar_t*, convert to UTF-8
		const wchar_t *modeName = ATDebugGetDisasmModeDisplayName(t->GetDisasmMode());
		VDStringA modeNameA = VDTextWToU8(VDStringW(modeName));

		float clockMHz = t->GetDisplayCPUClock() / 1000000.0f;
		entry.mType.sprintf("%s @ %.2fMHz", modeNameA.c_str(), clockMHz);

		mEntries.push_back(std::move(entry));
	}

	if (mSelectedRow >= (int)mEntries.size())
		mSelectedRow = -1;
}

bool ATImGuiTargetsPaneImpl::ActivateSelectedTarget() {
	if (mSelectedRow < 0 || mSelectedRow >= (int)mEntries.size())
		return false;

	IATDebugger *dbg = ATGetDebugger();
	if (!dbg)
		return false;

	return dbg->SetTarget(mEntries[mSelectedRow].mIndex);
}

bool ATImGuiTargetsPaneImpl::ActivateSelectedForTest(int row,
	uint32& outCurrentTarget,
	uint32& outTargetCount,
	VDStringA& outError)
{
	if (mbNeedsRebuild)
		RebuildList();

	outTargetCount = (uint32)mEntries.size();

	IATDebugger *dbg = ATGetDebugger();
	if (!dbg) {
		outCurrentTarget = 0;
		outError = "debugger is not available";
		return false;
	}

	outCurrentTarget = dbg->GetTargetIndex();

	if (row < 0 || row >= (int)mEntries.size()) {
		outError.sprintf("target row %d is out of range", row);
		return false;
	}

	mSelectedRow = row;

	const bool activated = ActivateSelectedTarget();
	outCurrentTarget = dbg->GetTargetIndex();
	if (!activated)
		outError.sprintf("target %u could not be activated",
			mEntries[row].mIndex);

	return activated;
}

bool ATImGuiTargetsPaneImpl::Render() {
	bool open = true;

	if (mbFocusRequested) {
		ImGui::SetNextWindowFocus();
		mbFocusRequested = false;
	}

	ImGui::SetNextWindowSize(ImVec2(400, 200), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin(mTitle.c_str(), &open)) {
		mbHasFocus = false;
		ImGui::End();
		return open;
	}
	mbHasFocus = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

	if (mbNeedsRebuild)
		RebuildList();

	if (mEntries.empty()) {
		ImGui::TextDisabled("(no targets)");
		ImGui::End();
		return open;
	}

	// Table with 4 columns matching Windows: Indicator | ID | Type | Description
	const ImGuiTableFlags tableFlags =
		ImGuiTableFlags_ScrollY |
		ImGuiTableFlags_RowBg |
		ImGuiTableFlags_Resizable |
		ImGuiTableFlags_BordersInnerV;

	if (ImGui::BeginTable("TargetsTable", 4, tableFlags)) {
		ImGui::TableSetupScrollFreeze(0, 1);
		ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 20.0f);
		ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 30.0f);
		ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 150.0f);
		ImGui::TableSetupColumn("Description", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableHeadersRow();

		IATDebugger *dbg = ATGetDebugger();

		for (int i = 0; i < (int)mEntries.size(); ++i) {
			const TargetEntry& entry = mEntries[i];
			ImGui::TableNextRow();
			ImGui::PushID(i);

			// Full-row selectable for double-click detection
			ImGui::TableSetColumnIndex(0);
			if (ImGui::Selectable("##row", i == mSelectedRow,
				ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap))
			{
				mSelectedRow = i;
			}

			// Double-click to switch target
			if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
				mSelectedRow = i;
				ActivateSelectedTarget();
			}

			// Current target indicator
			ImGui::SameLine();
			if (entry.mbCurrent)
				ImGui::TextUnformatted("\xe2\x96\xba");  // U+25BA ►
			else
				ImGui::TextUnformatted(" ");

			ImGui::TableSetColumnIndex(1);
			ImGui::Text("%u", entry.mIndex);

			ImGui::TableSetColumnIndex(2);
			ImGui::TextUnformatted(entry.mType.c_str());

			ImGui::TableSetColumnIndex(3);
			ImGui::TextUnformatted(entry.mName.c_str());

			ImGui::PopID();
		}

		ImGui::EndTable();
	}

	// Escape → focus Console (matches Windows pattern)
	if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)
			&& !ImGui::GetIO().WantTextInput
			&& ImGui::IsKeyPressed(ImGuiKey_Escape)) {
		ATUIDebuggerFocusConsole();
	} else if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)
			&& !ImGui::GetIO().WantTextInput
			&& (ImGui::IsKeyPressed(ImGuiKey_Enter)
				|| ImGui::IsKeyPressed(ImGuiKey_KeypadEnter))) {
		ActivateSelectedTarget();
	}

	ImGui::End();
	return open;
}

// =========================================================================
// Registration
// =========================================================================

void ATUIDebuggerEnsureTargetsPane() {
	if (!ATUIDebuggerGetPane(kATUIPaneId_Targets)) {
		auto *pane = new ATImGuiTargetsPaneImpl();
		ATUIDebuggerRegisterPane(pane);
	}
}

bool ATUIDebuggerActivateTargetForTest(int row,
	uint32& outCurrentTarget,
	uint32& outTargetCount,
	VDStringA& outError)
{
	ATUIDebuggerOpen();
	ATActivateUIPane(kATUIPaneId_Targets, true, true);

	auto *pane = static_cast<ATImGuiTargetsPaneImpl *>(
		ATUIDebuggerGetPane(kATUIPaneId_Targets));
	if (!pane) {
		outError = "targets pane is not available";
		outCurrentTarget = 0;
		outTargetCount = 0;
		return false;
	}

	return pane->ActivateSelectedForTest(row,
		outCurrentTarget,
		outTargetCount,
		outError);
}
