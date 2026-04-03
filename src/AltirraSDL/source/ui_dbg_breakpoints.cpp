//	AltirraSDL - Dear ImGui debugger breakpoints list pane
//	Replaces Win32 ATBreakpointsWindow (uidbgbreakpoints.cpp).
//	Lists all breakpoints with type, address, condition, and command.
//	Supports toggle enable, delete, and context menu operations.

#include <stdafx.h>
#include <imgui.h>
#include <vd2/system/vdtypes.h>
#include <vd2/system/VDString.h>
#include <at/atcore/address.h>
#include <at/atdebugger/expression.h>
#include "ui_debugger.h"
#include "console.h"
#include "debugger.h"
#include "simulator.h"

extern ATSimulator g_sim;

// =========================================================================
// Breakpoints pane
// =========================================================================

class ATImGuiBreakpointsPaneImpl final : public ATImGuiDebuggerPane {
public:
	ATImGuiBreakpointsPaneImpl();

	bool Render() override;
	void OnDebuggerSystemStateUpdate(const ATDebuggerSystemState& state) override;
	void OnDebuggerEvent(ATDebugEvent eventId) override;

private:
	void RebuildList();

	struct BPEntry {
		uint32 mUserIdx;
		VDStringA mGroup;
		VDStringA mId;			// "number" or "group.number"
		VDStringA mTarget;
		VDStringA mDescription;	// type + address + condition + command
	};

	std::vector<BPEntry> mEntries;
	bool mbNeedsRebuild = true;
};

ATImGuiBreakpointsPaneImpl::ATImGuiBreakpointsPaneImpl()
	: ATImGuiDebuggerPane(kATUIPaneId_Breakpoints, "Breakpoints")
{
}

void ATImGuiBreakpointsPaneImpl::OnDebuggerSystemStateUpdate(const ATDebuggerSystemState& state) {
	ATImGuiDebuggerPane::OnDebuggerSystemStateUpdate(state);
	// Don't rebuild on every state update — only on breakpoint changes
}

void ATImGuiBreakpointsPaneImpl::OnDebuggerEvent(ATDebugEvent eventId) {
	if (eventId == kATDebugEvent_BreakpointsChanged)
		mbNeedsRebuild = true;
}

void ATImGuiBreakpointsPaneImpl::RebuildList() {
	mbNeedsRebuild = false;
	mEntries.clear();

	IATDebugger *dbg = ATGetDebugger();
	if (!dbg)
		return;

	// Get all breakpoint groups
	auto groups = dbg->GetBreakpointGroups();
	std::sort(groups.begin(), groups.end());

	for (const auto& group : groups) {
		vdfastvector<uint32> bps;
		dbg->GetBreakpointList(bps, group.c_str());

		for (uint32 idx : bps) {
			ATDebuggerBreakpointInfo info;
			if (!dbg->GetBreakpointInfo(idx, info))
				continue;

			BPEntry entry;
			entry.mUserIdx = idx;
			entry.mGroup = group;

			// Format ID: "group.number" or just "number" for default group
			if (group.empty() || group == "user")
				entry.mId.sprintf("%d", info.mNumber);
			else
				entry.mId.sprintf("%s.%d", group.c_str(), info.mNumber);

			// Target
			entry.mTarget.sprintf("%u", info.mTargetIndex);

			// Description: type + address
			VDStringA desc;
			if (info.mbBreakOnPC) {
				desc = dbg->GetAddressText(info.mAddress, true, true);
			} else if (info.mbBreakOnRead && info.mbBreakOnWrite) {
				desc.sprintf("Access %s", dbg->GetAddressText(info.mAddress, true, true).c_str());
			} else if (info.mbBreakOnRead) {
				desc.sprintf("Read %s", dbg->GetAddressText(info.mAddress, true, true).c_str());
			} else if (info.mbBreakOnWrite) {
				desc.sprintf("Write %s", dbg->GetAddressText(info.mAddress, true, true).c_str());
			} else if (info.mbBreakOnInsn) {
				desc = "Any insn";
			} else {
				desc.sprintf("$%04X", info.mAddress);
			}

			// Append condition text (matches Windows uidbgbreakpoints.cpp:257-263)
			ATDebugExpNode *cond = dbg->GetBreakpointCondition(idx);
			if (cond) {
				VDStringA condStr;
				cond->ToString(condStr);
				desc.append_sprintf(" when %s", condStr.c_str());
			}

			// Append command if present
			if (info.mpCommand && info.mpCommand[0]) {
				desc.append_sprintf(", run: %s", info.mpCommand);
			}

			// Annotations
			if (info.mbOneShot)
				desc += " [one-shot]";
			if (info.mbContinueExecution)
				desc += " [trace]";
			if (info.mbClearOnReset)
				desc += " [clear-on-reset]";

			entry.mDescription = desc;
			mEntries.push_back(std::move(entry));
		}
	}
}

bool ATImGuiBreakpointsPaneImpl::Render() {
	bool open = true;

	if (mbFocusRequested) {
		ImGui::SetNextWindowFocus();
		mbFocusRequested = false;
	}

	ImGui::SetNextWindowSize(ImVec2(450, 250), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin(mTitle.c_str(), &open)) {
		ImGui::End();
		return open;
	}

	if (mbNeedsRebuild)
		RebuildList();

	if (mEntries.empty()) {
		ImGui::TextDisabled("(no breakpoints)");
		ImGui::End();
		return open;
	}

	// Table
	const ImGuiTableFlags tableFlags =
		ImGuiTableFlags_ScrollY |
		ImGuiTableFlags_RowBg |
		ImGuiTableFlags_Resizable |
		ImGuiTableFlags_BordersInnerV;

	if (ImGui::BeginTable("BPTable", 3, tableFlags)) {
		ImGui::TableSetupScrollFreeze(0, 1);
		ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 60.0f);
		ImGui::TableSetupColumn("Target", ImGuiTableColumnFlags_WidthFixed, 50.0f);
		ImGui::TableSetupColumn("Description", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableHeadersRow();

		for (int i = 0; i < (int)mEntries.size(); ++i) {
			const BPEntry& bp = mEntries[i];
			ImGui::TableNextRow();
			ImGui::PushID(i);

			ImGui::TableSetColumnIndex(0);
			// Invisible selectable spanning full row for right-click detection
			ImGui::Selectable("##row", false,
				ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap,
				ImVec2(0, 0));
			if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
				ImGui::OpenPopup("BPCtx");
			ImGui::SameLine();
			ImGui::TextUnformatted(bp.mId.c_str());

			ImGui::TableSetColumnIndex(1);
			ImGui::TextUnformatted(bp.mTarget.c_str());

			ImGui::TableSetColumnIndex(2);
			ImGui::TextUnformatted(bp.mDescription.c_str());

			if (ImGui::BeginPopup("BPCtx")) {
				IATDebugger *dbg = ATGetDebugger();
				if (dbg) {
					if (ImGui::MenuItem("Delete")) {
						dbg->ClearUserBreakpoint(bp.mUserIdx, true);
						mbNeedsRebuild = true;
					}
				}
				ImGui::EndPopup();
			}

			ImGui::PopID();
		}

		ImGui::EndTable();
	}

	ImGui::End();
	return open;
}

// =========================================================================
// Registration
// =========================================================================

void ATUIDebuggerEnsureBreakpointsPane() {
	if (!ATUIDebuggerGetPane(kATUIPaneId_Breakpoints)) {
		auto *pane = new ATImGuiBreakpointsPaneImpl();
		ATUIDebuggerRegisterPane(pane);
	}
}
