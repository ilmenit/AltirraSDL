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
				// Check if command starts with .printf " — if so, it's a trace message
				// that was wrapped by the dialog on the previous save
				const char *cmd = info.mpCommand;

				if (strncmp(cmd, ".printf \"", 9) == 0) {
					// Extract trace message from .printf "..."
					const char *msgStart = cmd + 9;
					const char *closeQuote = strchr(msgStart, '"');
					// Walk forward to find the real closing quote (skip escaped ones)
					while (closeQuote && closeQuote > msgStart && *(closeQuote - 1) == '\\')
						closeQuote = strchr(closeQuote + 1, '"');

					if (closeQuote) {
						// Unescape: \" -> ", %% -> %
						VDStringA unescaped;
						for (const char *p = msgStart; p < closeQuote; ++p) {
							if (p + 1 < closeQuote && *p == '\\' && *(p + 1) == '"') {
								unescaped += '"';
								++p;
							} else if (p + 1 < closeQuote && *p == '%' && *(p + 1) == '%') {
								unescaped += '%';
								++p;
							} else {
								unescaped += *p;
							}
						}

						d.mbActionTraceEnabled = true;
						snprintf(d.mTraceBuf, sizeof(d.mTraceBuf), "%s", unescaped.c_str());

						// Check for additional command after the .printf "..."
						const char *rest = closeQuote + 1;
						// Skip leading "; "
						while (*rest == ';' || *rest == ' ')
							++rest;
						if (*rest) {
							d.mbActionCommandEnabled = true;
							snprintf(d.mCommandBuf, sizeof(d.mCommandBuf), "%s", rest);
						}
					} else {
						// Couldn't parse .printf, treat whole thing as command
						d.mbActionCommandEnabled = true;
						snprintf(d.mCommandBuf, sizeof(d.mCommandBuf), "%s", info.mpCommand);
					}
				} else {
					d.mbActionCommandEnabled = true;
					snprintf(d.mCommandBuf, sizeof(d.mCommandBuf), "%s", info.mpCommand);
				}
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
			IATDebugger *dbg = ATGetDebugger();
			d.mErrorBuf[0] = 0;

			if (dbg) {
				// Parse condition expression if enabled
				vdautoptr<ATDebugExpNode> condition;
				if (d.mbConditionEnabled && d.mConditionBuf[0]) {
					try {
						condition = ATDebuggerParseExpression(
							d.mConditionBuf,
							ATGetDebuggerSymbolLookup(),
							dbg->GetExprOpts());
					} catch (const ATDebuggerExprParseException& e) {
						snprintf(d.mErrorBuf, sizeof(d.mErrorBuf),
							"Unable to parse condition: %s", e.c_str());
					}
				} else if (d.mbConditionEnabled && !d.mConditionBuf[0]) {
					// Checkbox enabled but empty — just clear it
					d.mbConditionEnabled = false;
				}

				if (!d.mErrorBuf[0]) {
					ATDebuggerBreakpointInfo bpInfo;
					bpInfo.mTargetIndex = dbg->GetTargetIndex();

					// Build command string
					VDStringA commandStr;
					if (d.mbActionCommandEnabled && d.mCommandBuf[0])
						commandStr = d.mCommandBuf;

					// Build trace → .printf command
					if (d.mbActionTraceEnabled && d.mTraceBuf[0]) {
						VDStringA traceCmdStr(".printf \"");

						bool traceValid = true;
						for (const char *p = d.mTraceBuf; *p; ++p) {
							char ch = *p;
							if (ch < 0x20 || ch > 0x7E) {
								snprintf(d.mErrorBuf, sizeof(d.mErrorBuf),
									"The trace message contains an unsupported character.");
								traceValid = false;
								break;
							}
							if (ch == '"')
								traceCmdStr += '\\';
							else if (ch == '%')
								traceCmdStr += '%';
							traceCmdStr += ch;
						}

						if (traceValid) {
							traceCmdStr += '"';

							if (!commandStr.empty()) {
								traceCmdStr += "; ";
								traceCmdStr += commandStr;
							}

							commandStr = std::move(traceCmdStr);
						}
					}

					if (!d.mErrorBuf[0]) {
						bpInfo.mpCondition = condition;
						bpInfo.mpCommand = commandStr.c_str();
						bpInfo.mbContinueExecution = !d.mbActionStop;

						// Validate: non-stopping breakpoint must have a command or trace
						if (bpInfo.mbContinueExecution && commandStr.empty()) {
							snprintf(d.mErrorBuf, sizeof(d.mErrorBuf),
								"A non-stopping breakpoint must be used with a command or a trace message.");
						}

						if (!d.mErrorBuf[0]) {
							if (d.mLocationType == 3) {
								// Any instruction with condition
								if (!condition) {
									snprintf(d.mErrorBuf, sizeof(d.mErrorBuf),
										"A condition must be used with a condition-only breakpoint.");
								} else {
									bpInfo.mbBreakOnInsn = true;
									dbg->SetBreakpoint(d.mUserIdx, bpInfo);
									submitted = true;
								}
							} else {
								// PC / Read / Write — need address
								if (!d.mLocationBuf[0]) {
									snprintf(d.mErrorBuf, sizeof(d.mErrorBuf),
										"Please enter an address.");
								} else {
									try {
										uint32 address = (uint32)dbg->EvaluateThrow(d.mLocationBuf);
										bpInfo.mAddress = address;

										if (d.mLocationType == 1)
											bpInfo.mbBreakOnRead = true;
										else if (d.mLocationType == 2)
											bpInfo.mbBreakOnWrite = true;
										else
											bpInfo.mbBreakOnPC = true;

										dbg->SetBreakpoint(d.mUserIdx, bpInfo);
										submitted = true;
									} catch (const MyError& e) {
										snprintf(d.mErrorBuf, sizeof(d.mErrorBuf),
											"Unable to parse location: %s", e.c_str());
									}
								}
							}
						}
					}
				}
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
			// Default group is always empty string (matches Windows: `if (*group)`)
			if (group.empty())
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
				desc.append_sprintf(", run command: %s", info.mpCommand);
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
				ImGui::TableNextRow();
				ImGui::PushID(i);

				ImGui::TableSetColumnIndex(0);
				// Invisible selectable spanning full row for click/right-click detection
				ImGui::Selectable("##row", false,
					ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap,
					ImVec2(0, 0));
				// Double-click to edit breakpoint (matches Windows behavior)
				if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
					OpenBreakpointDialog(bp.mUserIdx);
				}
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
						if (ImGui::MenuItem("New Breakpoint...")) {
							OpenBreakpointDialog(-1);
						}
						ImGui::Separator();
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
	}

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
