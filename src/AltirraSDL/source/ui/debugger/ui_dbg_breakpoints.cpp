//	AltirraSDL - Dear ImGui debugger breakpoints list pane
//	Replaces Win32 ATBreakpointsWindow (uidbgbreakpoints.cpp).
//	Lists all breakpoints with type, address, condition, and command.
//	Supports toggle enable, delete, and context menu operations.
//	Includes Add/Edit Breakpoint modal dialog matching Windows IDD_DEBUG_BREAKPOINT.

#include <stdafx.h>
#include <imgui.h>
#include <vd2/system/vdtypes.h>
#include <vd2/system/VDString.h>
#include <vd2/system/vdalloc.h>
#include <vd2/system/error.h>
#include <at/atcore/address.h>
#include <at/atdebugger/expression.h>
#include "ui_debugger.h"
#include "console.h"
#include "debugger.h"
#include "debuggerexp.h"
#include "simulator.h"

extern ATSimulator g_sim;

// =========================================================================
// Add/Edit Breakpoint dialog state
// =========================================================================

namespace {
	struct BreakpointDialogState {
		bool mbOpen = false;
		bool mbJustOpened = false;
		bool mbFocusLocation = false;	// set when popup opens, consumed on first render
		sint32 mUserIdx = -1;			// -1 = new breakpoint, >=0 = edit existing

		// Location
		int mLocationType = 0;			// 0=PC, 1=Read, 2=Write, 3=Any insn
		char mLocationBuf[256] = {};

		// Condition
		bool mbConditionEnabled = false;
		char mConditionBuf[512] = {};

		// Actions
		bool mbActionStop = true;
		bool mbActionCommandEnabled = false;
		char mCommandBuf[512] = {};
		bool mbActionTraceEnabled = false;
		char mTraceBuf[512] = {};

		// Error message for validation failures
		char mErrorBuf[512] = {};
	};

	BreakpointDialogState g_bpDialog;

	bool BuildTraceCommand(const char *traceText,
		const char *commandSuffix,
		VDStringA& outCommand,
		VDStringA& outError)
	{
		outCommand.clear();
		outError.clear();

		if (!traceText || !*traceText)
			return true;

		VDStringA traceCmdStr(".printf \"");

		for (const char *p = traceText; *p; ++p) {
			const char ch = *p;
			if (ch < 0x20 || ch > 0x7E) {
				outError = "The trace message contains an unsupported character.";
				return false;
			}
			if (ch == '"')
				traceCmdStr += '\\';
			else if (ch == '%')
				traceCmdStr += '%';
			traceCmdStr += ch;
		}

		traceCmdStr += '"';

		if (commandSuffix && *commandSuffix) {
			traceCmdStr += "; ";
			traceCmdStr += commandSuffix;
		}

		outCommand = std::move(traceCmdStr);
		return true;
	}

	bool SubmitBreakpoint(
		sint32 userIdx,
		int locationType,
		const char *location,
		bool conditionEnabled,
		const char *conditionText,
		bool stopExecution,
		bool commandEnabled,
		const char *commandText,
		bool traceEnabled,
		const char *traceText,
		uint32& outUserIdx,
		VDStringA& outError)
	{
		outUserIdx = (uint32)-1;
		outError.clear();

		IATDebugger *dbg = ATGetDebugger();
		if (!dbg) {
			outError = "Debugger is not available.";
			return false;
		}

		vdautoptr<ATDebugExpNode> condition;
		if (conditionEnabled) {
			try {
				condition = ATDebuggerParseExpression(
					conditionText ? conditionText : "",
					ATGetDebuggerSymbolLookup(),
					dbg->GetExprOpts());
			} catch (const ATDebuggerExprParseException& e) {
				outError.sprintf("Unable to parse condition: %s", e.c_str());
				return false;
			}
		}

		ATDebuggerBreakpointInfo bpInfo;
		bpInfo.mTargetIndex = dbg->GetTargetIndex();

		VDStringA commandStr;
		if (commandEnabled && commandText && *commandText)
			commandStr = commandText;

		if (traceEnabled && traceText && *traceText) {
			VDStringA traceCmdStr;
			if (!BuildTraceCommand(traceText, commandStr.c_str(), traceCmdStr, outError))
				return false;

			commandStr = std::move(traceCmdStr);
		}

		bpInfo.mpCondition = condition;
		bpInfo.mpCommand = commandStr.c_str();
		bpInfo.mbContinueExecution = !stopExecution;

		if (bpInfo.mbContinueExecution && commandStr.empty()) {
			outError = "A non-stopping breakpoint must be used with a command or a trace message.";
			return false;
		}

		if (locationType == 3) {
			if (!condition) {
				outError = "A condition must be used with a condition-only breakpoint.";
				return false;
			}

			bpInfo.mbBreakOnInsn = true;
		} else {
			if (!location || !*location) {
				outError = "Please enter an address.";
				return false;
			}

			try {
				bpInfo.mAddress = (uint32)dbg->EvaluateThrow(location);
			} catch (const MyError& e) {
				outError.sprintf("Unable to parse location: %s", e.c_str());
				return false;
			}

			if (locationType == 1)
				bpInfo.mbBreakOnRead = true;
			else if (locationType == 2)
				bpInfo.mbBreakOnWrite = true;
			else
				bpInfo.mbBreakOnPC = true;
		}

		outUserIdx = dbg->SetBreakpoint(userIdx, bpInfo);
		return true;
	}

	void FormatBreakpointDescription(
		IATDebugger& dbg,
		uint32 userIdx,
		const ATDebuggerBreakpointInfo& info,
		VDStringA& desc)
	{
		if (info.mbBreakOnPC) {
			desc = dbg.GetAddressText(info.mAddress, true, true);
		} else if (info.mbBreakOnRead && info.mbBreakOnWrite) {
			desc.sprintf("Access %s", dbg.GetAddressText(info.mAddress, true, true).c_str());
		} else if (info.mbBreakOnRead) {
			desc.sprintf("Read %s", dbg.GetAddressText(info.mAddress, true, true).c_str());
		} else if (info.mbBreakOnWrite) {
			desc.sprintf("Write %s", dbg.GetAddressText(info.mAddress, true, true).c_str());
		} else if (info.mbBreakOnInsn) {
			desc = "Any insn";
		} else {
			desc.sprintf("$%04X", info.mAddress);
		}

		ATDebugExpNode *cond = dbg.GetBreakpointCondition(userIdx);
		if (cond) {
			VDStringA condStr;
			cond->ToString(condStr);
			desc.append_sprintf(" when %s", condStr.c_str());
		}

		if (info.mpCommand && info.mpCommand[0])
			desc.append_sprintf(", run command: %s", info.mpCommand);

		if (info.mbOneShot)
			desc += " [one-shot]";
		if (info.mbContinueExecution)
			desc += " [trace]";
		if (info.mbClearOnReset)
			desc += " [clear-on-reset]";
	}

	bool BreakpointGroupLess(const VDStringA& a, const VDStringA& b) {
		const int ci = a.comparei(b);
		if (ci)
			return ci < 0;

		return a.compare(b) < 0;
	}

	void OpenBreakpointDialog(sint32 userIdx) {
		auto& d = g_bpDialog;
		d.mbOpen = true;
		d.mbJustOpened = true;
		d.mbFocusLocation = true;
		d.mUserIdx = userIdx;
		d.mLocationType = 0;
		d.mLocationBuf[0] = 0;
		d.mbConditionEnabled = false;
		d.mConditionBuf[0] = 0;
		d.mbActionStop = true;
		d.mbActionCommandEnabled = false;
		d.mCommandBuf[0] = 0;
		d.mbActionTraceEnabled = false;
		d.mTraceBuf[0] = 0;
		d.mErrorBuf[0] = 0;

		// If editing an existing breakpoint, load its data
		if (userIdx >= 0) {
			IATDebugger *dbg = ATGetDebugger();
			if (!dbg)
				return;

			ATDebuggerBreakpointInfo info {};
			if (!dbg->GetBreakpointInfo((uint32)userIdx, info))
				return;

			// Location type
			if (info.mbBreakOnInsn) {
				d.mLocationType = 3;
			} else if (info.mbBreakOnRead) {
				d.mLocationType = 1;
				snprintf(d.mLocationBuf, sizeof(d.mLocationBuf), "$%04X", info.mAddress);
			} else if (info.mbBreakOnWrite) {
				d.mLocationType = 2;
				snprintf(d.mLocationBuf, sizeof(d.mLocationBuf), "$%04X", info.mAddress);
			} else {
				d.mLocationType = 0;
				snprintf(d.mLocationBuf, sizeof(d.mLocationBuf), "$%04X", info.mAddress);
			}

			// Condition
			ATDebugExpNode *cond = dbg->GetBreakpointCondition((uint32)userIdx);
			if (cond) {
				d.mbConditionEnabled = true;
				VDStringA condStr;
				cond->ToString(condStr);
				snprintf(d.mConditionBuf, sizeof(d.mConditionBuf), "%s", condStr.c_str());
			}

			// Actions
			d.mbActionStop = !info.mbContinueExecution;

			if (info.mpCommand && info.mpCommand[0]) {
				d.mbActionCommandEnabled = true;
				snprintf(d.mCommandBuf, sizeof(d.mCommandBuf), "%s", info.mpCommand);
			}
		}
	}

	// Returns true if dialog was submitted (OK pressed successfully)
	bool RenderBreakpointDialog() {
		auto& d = g_bpDialog;
		if (!d.mbOpen)
			return false;

		if (d.mbJustOpened) {
			ImGui::OpenPopup("Add/Edit Breakpoint");
			d.mbJustOpened = false;
		}

		ImGui::SetNextWindowSize(ImVec2(480, 0), ImGuiCond_Appearing);
		ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
			ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

		bool submitted = false;

		if (!ImGui::BeginPopupModal("Add/Edit Breakpoint", &d.mbOpen,
				ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
			return false;
		}

		const float labelWidth = 80.0f;

		// --- Location section ---
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted("Location");
		ImGui::SameLine(labelWidth);

		ImGui::BeginGroup();
		ImGui::RadioButton("Break on PC address", &d.mLocationType, 0);
		ImGui::RadioButton("Break on CPU read from address", &d.mLocationType, 1);
		ImGui::RadioButton("Break on CPU write to address", &d.mLocationType, 2);
		ImGui::RadioButton("Break on any CPU instruction with condition", &d.mLocationType, 3);
		ImGui::EndGroup();

		// Address field (disabled for "any insn" mode)
		ImGui::Dummy(ImVec2(0, 2));
		ImGui::Dummy(ImVec2(labelWidth + 10.0f, 0));
		ImGui::SameLine();

		const bool locationDisabled = (d.mLocationType == 3);
		if (locationDisabled) {
			ImGui::BeginDisabled();
		}
		if (d.mbFocusLocation) {
			if (!locationDisabled)
				ImGui::SetKeyboardFocusHere();
			d.mbFocusLocation = false;
		}
		ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
		ImGui::InputText("##location", d.mLocationBuf, sizeof(d.mLocationBuf));
		if (locationDisabled) {
			ImGui::EndDisabled();
		}

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		// --- Condition section ---
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted("Condition");
		ImGui::SameLine(labelWidth);
		ImGui::Checkbox("Enable only on condition", &d.mbConditionEnabled);

		ImGui::Dummy(ImVec2(labelWidth + 10.0f, 0));
		ImGui::SameLine();
		if (!d.mbConditionEnabled) {
			ImGui::BeginDisabled();
		}
		ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
		ImGui::InputText("##condition", d.mConditionBuf, sizeof(d.mConditionBuf));
		if (!d.mbConditionEnabled) {
			ImGui::EndDisabled();
		}

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		// --- Action section ---
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted("Action");
		ImGui::SameLine(labelWidth);
		ImGui::Checkbox("Stop execution", &d.mbActionStop);

		ImGui::Dummy(ImVec2(labelWidth, 0));
		ImGui::SameLine();
		ImGui::Checkbox("Run a command when breakpoint is hit", &d.mbActionCommandEnabled);

		ImGui::Dummy(ImVec2(labelWidth + 10.0f, 0));
		ImGui::SameLine();
		if (!d.mbActionCommandEnabled) {
			ImGui::BeginDisabled();
		}
		ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
		ImGui::InputText("##command", d.mCommandBuf, sizeof(d.mCommandBuf));
		if (!d.mbActionCommandEnabled) {
			ImGui::EndDisabled();
		}

		ImGui::Dummy(ImVec2(labelWidth, 0));
		ImGui::SameLine();
		ImGui::Checkbox("Print a message", &d.mbActionTraceEnabled);

		ImGui::Dummy(ImVec2(labelWidth + 10.0f, 0));
		ImGui::SameLine();
		if (!d.mbActionTraceEnabled) {
			ImGui::BeginDisabled();
		}
		ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
		ImGui::InputText("##trace", d.mTraceBuf, sizeof(d.mTraceBuf));
		if (!d.mbActionTraceEnabled) {
			ImGui::EndDisabled();
		}

		// --- Error message ---
		if (d.mErrorBuf[0]) {
			ImGui::Spacing();
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
			ImGui::TextWrapped("%s", d.mErrorBuf);
			ImGui::PopStyleColor();
		}

		// --- OK / Cancel ---
		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		const float buttonWidth = 80.0f;
		const float spacing = ImGui::GetStyle().ItemSpacing.x;
		float totalWidth = buttonWidth * 2 + spacing;
		ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - totalWidth);

		if (ImGui::Button("OK", ImVec2(buttonWidth, 0))) {
			// --- Validation and submission ---
			d.mErrorBuf[0] = 0;

			uint32 userIdx = (uint32)-1;
			VDStringA error;
			submitted = SubmitBreakpoint(
				d.mUserIdx,
				d.mLocationType,
				d.mLocationBuf,
				d.mbConditionEnabled,
				d.mConditionBuf,
				d.mbActionStop,
				d.mbActionCommandEnabled,
				d.mCommandBuf,
				d.mbActionTraceEnabled,
				d.mTraceBuf,
				userIdx,
				error);

			if (!submitted) {
				snprintf(d.mErrorBuf, sizeof(d.mErrorBuf),
					"%s", error.c_str());
			}

			if (submitted) {
				d.mbOpen = false;
				ImGui::CloseCurrentPopup();
			}
		}

		ImGui::SameLine();
		if (ImGui::Button("Cancel", ImVec2(buttonWidth, 0))) {
			d.mbOpen = false;
			ImGui::CloseCurrentPopup();
		}

		// Escape to cancel (ImGui modals don't close on Escape by default)
		if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
			d.mbOpen = false;
			ImGui::CloseCurrentPopup();
		}

		ImGui::EndPopup();
		return submitted;
	}
}

// Public API for opening the breakpoint dialog from other files
void ATUIDebuggerShowBreakpointDialog(sint32 userIdx) {
	OpenBreakpointDialog(userIdx);
}

bool ATUIDebuggerFormatBreakpointTraceForTest(const char *traceText,
	VDStringA& outCommand,
	VDStringA& outError)
{
	return BuildTraceCommand(traceText, nullptr, outCommand, outError);
}

bool ATUIDebuggerSubmitBreakpointForTest(
	int locationType,
	const char *location,
	bool conditionEnabled,
	const char *conditionText,
	bool stopExecution,
	bool commandEnabled,
	const char *commandText,
	bool traceEnabled,
	const char *traceText,
	uint32& outUserIdx,
	VDStringA& outError)
{
	return SubmitBreakpoint(
		-1,
		locationType,
		location,
		conditionEnabled,
		conditionText,
		stopExecution,
		commandEnabled,
		commandText,
		traceEnabled,
		traceText,
		outUserIdx,
		outError);
}

bool ATUIDebuggerDescribeBreakpointForTest(uint32 userIdx, VDStringA& outDescription) {
	outDescription.clear();

	IATDebugger *dbg = ATGetDebugger();
	if (!dbg)
		return false;

	ATDebuggerBreakpointInfo info;
	if (!dbg->GetBreakpointInfo(userIdx, info))
		return false;

	FormatBreakpointDescription(*dbg, userIdx, info, outDescription);
	return true;
}

bool ATUIDebuggerFormatBreakpointDescriptionForTest(
	bool oneShot,
	bool clearOnReset,
	bool continueExecution,
	VDStringA& outDescription)
{
	outDescription.clear();

	IATDebugger *dbg = ATGetDebugger();
	if (!dbg)
		return false;

	ATDebuggerBreakpointInfo info;
	info.mAddress = 0x2000;
	info.mbBreakOnPC = true;
	info.mbOneShot = oneShot;
	info.mbClearOnReset = clearOnReset;
	info.mbContinueExecution = continueExecution;

	FormatBreakpointDescription(*dbg, (uint32)-1, info, outDescription);
	return true;
}

bool ATUIDebuggerDeleteBreakpointForTest(uint32 userIdx) {
	IATDebugger *dbg = ATGetDebugger();
	return dbg && dbg->ClearUserBreakpoint(userIdx, true);
}

int ATUIDebuggerCountBreakpointsForTest() {
	IATDebugger *dbg = ATGetDebugger();
	if (!dbg)
		return -1;

	vdfastvector<uint32> bps;
	dbg->GetBreakpointList(bps);
	return (int)bps.size();
}

// =========================================================================
// Breakpoints pane
// =========================================================================

class ATImGuiBreakpointsPaneImpl final : public ATImGuiDebuggerPane {
public:
	ATImGuiBreakpointsPaneImpl();

	bool Render() override;
	void OnDebuggerSystemStateUpdate(const ATDebuggerSystemState& state) override;
	void OnDebuggerEvent(ATDebugEvent eventId) override;
	bool SelectForTest(uint32 userIdx);
	bool DeleteSelectedForTest(uint32 userIdx, int& remainingCount);
	void GetRowOrderForTest(VDStringA& rowOrder);

private:
	struct BPEntry {
		uint32 mUserIdx;
		VDStringA mGroup;
		VDStringA mId;			// "number" or "group.number"
		VDStringA mTarget;
		VDStringA mDescription;	// type + address + condition + command
	};

	void RebuildList();
	void DeleteSelectedBreakpoint();
	BPEntry *FindSelectedEntry();

	std::vector<BPEntry> mEntries;
	bool mbNeedsRebuild = true;
	sint32 mSelectedUserIdx = -1;
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
	bool selectedStillPresent = false;

	IATDebugger *dbg = ATGetDebugger();
	if (!dbg) {
		mSelectedUserIdx = -1;
		return;
	}

	// Get all breakpoint groups
	auto groups = dbg->GetBreakpointGroups();
	std::sort(groups.begin(), groups.end(), BreakpointGroupLess);

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
			// Default group is always empty string (matches Windows: `if (*group)`)
			if (group.empty())
				entry.mId.sprintf("%d", info.mNumber);
			else
				entry.mId.sprintf("%s.%d", group.c_str(), info.mNumber);

			// Target
			entry.mTarget.sprintf("%u", info.mTargetIndex);

			VDStringA desc;
			FormatBreakpointDescription(*dbg, idx, info, desc);

			entry.mDescription = desc;
			if ((sint32)entry.mUserIdx == mSelectedUserIdx)
				selectedStillPresent = true;

			mEntries.push_back(std::move(entry));
		}
	}

	if (!selectedStillPresent)
		mSelectedUserIdx = -1;
}

ATImGuiBreakpointsPaneImpl::BPEntry *ATImGuiBreakpointsPaneImpl::FindSelectedEntry() {
	if (mSelectedUserIdx < 0)
		return nullptr;

	for(BPEntry& entry : mEntries) {
		if ((sint32)entry.mUserIdx == mSelectedUserIdx)
			return &entry;
	}

	return nullptr;
}

void ATImGuiBreakpointsPaneImpl::DeleteSelectedBreakpoint() {
	BPEntry *entry = FindSelectedEntry();
	IATDebugger *dbg = ATGetDebugger();
	if (!entry || !dbg)
		return;

	dbg->ClearUserBreakpoint(entry->mUserIdx, true);
	mSelectedUserIdx = -1;
	mbNeedsRebuild = true;
}

bool ATImGuiBreakpointsPaneImpl::SelectForTest(uint32 userIdx) {
	if (mbNeedsRebuild)
		RebuildList();

	mSelectedUserIdx = (sint32)userIdx;
	return FindSelectedEntry() != nullptr;
}

bool ATImGuiBreakpointsPaneImpl::DeleteSelectedForTest(uint32 userIdx, int& remainingCount) {
	remainingCount = -1;

	if (mbNeedsRebuild)
		RebuildList();

	mSelectedUserIdx = (sint32)userIdx;

	if (!FindSelectedEntry())
		return false;

	DeleteSelectedBreakpoint();
	RebuildList();

	remainingCount = (int)mEntries.size();
	return true;
}

void ATImGuiBreakpointsPaneImpl::GetRowOrderForTest(VDStringA& rowOrder) {
	rowOrder.clear();

	if (mbNeedsRebuild)
		RebuildList();

	bool first = true;
	for (const BPEntry& entry : mEntries) {
		if (!first)
			rowOrder += '|';

		rowOrder += entry.mId;
		first = false;
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
		mbHasFocus = false;
		// Still render the dialog even if pane is collapsed
		RenderBreakpointDialog();
		ImGui::End();
		return open;
	}
	mbHasFocus = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

	if (mbNeedsRebuild)
		RebuildList();

	if (mEntries.empty()) {
		ImGui::TextDisabled("(no breakpoints)");
	}

	// Table
	if (!mEntries.empty()) {
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
				const bool selected = (mSelectedUserIdx == (sint32)bp.mUserIdx);
				ImGui::TableNextRow();
				ImGui::PushID(i);

				ImGui::TableSetColumnIndex(0);
				// Invisible selectable spanning full row for click/right-click detection
				if (ImGui::Selectable("##row", selected,
					ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap,
					ImVec2(0, 0))) {
					mSelectedUserIdx = (sint32)bp.mUserIdx;
				}
				// Double-click to edit breakpoint (matches Windows behavior)
				if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
					mSelectedUserIdx = (sint32)bp.mUserIdx;
					OpenBreakpointDialog(bp.mUserIdx);
				}
				if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
					mSelectedUserIdx = (sint32)bp.mUserIdx;
					ImGui::OpenPopup("BPCtx");
				}
				ImGui::SameLine();
				ImGui::TextUnformatted(bp.mId.c_str());

				ImGui::TableSetColumnIndex(1);
				ImGui::TextUnformatted(bp.mTarget.c_str());

				ImGui::TableSetColumnIndex(2);
				ImGui::TextUnformatted(bp.mDescription.c_str());

				if (ImGui::BeginPopup("BPCtx")) {
					IATDebugger *dbg = ATGetDebugger();
					if (dbg) {
						if (ImGui::MenuItem("New Breakpoint...")) {
							OpenBreakpointDialog(-1);
						}
						ImGui::Separator();
						if (ImGui::MenuItem("Delete")) {
							mSelectedUserIdx = (sint32)bp.mUserIdx;
							DeleteSelectedBreakpoint();
						}
					}
					ImGui::EndPopup();
				}

				ImGui::PopID();
			}

			ImGui::EndTable();
		}
	}

	if (mbHasFocus && !ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Delete))
		DeleteSelectedBreakpoint();

	// Right-click on empty area also shows context menu with New Breakpoint
	if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup) && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && !ImGui::IsAnyItemHovered()) {
		ImGui::OpenPopup("BPEmptyCtx");
	}
	if (ImGui::BeginPopup("BPEmptyCtx")) {
		if (ImGui::MenuItem("New Breakpoint...")) {
			OpenBreakpointDialog(-1);
		}
		ImGui::EndPopup();
	}

	// Render the breakpoint dialog (must be inside the frame, but dialog is global)
	if (RenderBreakpointDialog()) {
		mbNeedsRebuild = true;
	}

	// Escape → focus Console
	if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)
			&& !ImGui::GetIO().WantTextInput
			&& ImGui::IsKeyPressed(ImGuiKey_Escape))
		ATUIDebuggerFocusConsole();

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

bool ATUIDebuggerSelectBreakpointForTest(uint32 userIdx) {
	ATUIDebuggerOpen();
	ATActivateUIPane(kATUIPaneId_Breakpoints, true, true);

	ATImGuiDebuggerPane *basePane = ATUIDebuggerGetPane(kATUIPaneId_Breakpoints);
	if (!basePane)
		return false;

	return static_cast<ATImGuiBreakpointsPaneImpl *>(basePane)
		->SelectForTest(userIdx);
}

bool ATUIDebuggerDeleteBreakpointViaPaneForTest(uint32 userIdx, int& remainingCount) {
	ATUIDebuggerOpen();
	ATActivateUIPane(kATUIPaneId_Breakpoints, true, true);

	ATImGuiDebuggerPane *basePane = ATUIDebuggerGetPane(kATUIPaneId_Breakpoints);
	if (!basePane)
		return false;

	return static_cast<ATImGuiBreakpointsPaneImpl *>(basePane)
		->DeleteSelectedForTest(userIdx, remainingCount);
}

bool ATUIDebuggerGetBreakpointPaneOrderForTest(VDStringA& rowOrder) {
	rowOrder.clear();

	ATUIDebuggerOpen();
	ATActivateUIPane(kATUIPaneId_Breakpoints, true, true);

	ATImGuiDebuggerPane *basePane = ATUIDebuggerGetPane(kATUIPaneId_Breakpoints);
	if (!basePane)
		return false;

	static_cast<ATImGuiBreakpointsPaneImpl *>(basePane)
		->GetRowOrderForTest(rowOrder);
	return true;
}
