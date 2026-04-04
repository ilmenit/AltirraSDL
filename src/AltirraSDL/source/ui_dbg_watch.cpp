//	AltirraSDL - Dear ImGui debugger watch pane
//	Replaces Win32 ATWatchWindow (uidbgwatch.cpp).
//	Supports 4 instances with editable expression watches.
//	Each watch entry parses a debugger expression and re-evaluates it on
//	every debugger state update, showing the result as decimal + hex.

#include <stdafx.h>
#include <algorithm>
#include <string>
#include <vector>
#include <imgui.h>
#include <vd2/system/vdtypes.h>
#include <vd2/system/VDString.h>
#include <vd2/system/vdalloc.h>
#include <vd2/system/error.h>
#include <at/atdebugger/expression.h>
#include "ui_debugger.h"
#include "console.h"
#include "debugger.h"
#include "debuggerexp.h"
#include "simulator.h"

extern ATSimulator g_sim;

// =========================================================================
// Watch pane — up to 4 instances (Watch 1 through Watch 4)
// =========================================================================

class ATImGuiWatchPaneImpl final : public ATImGuiDebuggerPane {
public:
	ATImGuiWatchPaneImpl(uint32 paneId, int instanceIndex);

	bool Render() override;
	void OnDebuggerSystemStateUpdate(const ATDebuggerSystemState& state) override;
	void OnDebuggerEvent(ATDebugEvent eventId) override;

	void AddWatch(const char *expr);

private:
	void UpdateAllWatches();

	struct WatchEntry {
		std::string mExprStr;		// user-entered expression text
		vdautoptr<ATDebugExpNode> mpExpr;	// parsed expression tree (null if parse error)
		VDStringA mValueStr;		// formatted result or error message
		bool mbHasValue = false;

		void SetExpr(const char *expr);
		bool Update();				// re-evaluate; returns true if value changed
	};

	int mInstanceIndex;
	std::vector<WatchEntry> mEntries;
	bool mbNeedsUpdate = true;

	// Editing state
	int mEditingRow = -1;			// which row is being edited (-1 = none)
	char mEditBuf[256] = {};
	bool mbStartEdit = false;		// request to start editing on next frame
	int mStartEditRow = -1;
	int mEditGraceFrames = 0;		// skip focus-loss commit for N frames after edit starts

	// Delete request (deferred to avoid modifying vector during iteration)
	int mDeleteRow = -1;
};

void ATImGuiWatchPaneImpl::WatchEntry::SetExpr(const char *expr) {
	mExprStr = expr;
	mpExpr.reset();
	mValueStr.clear();
	mbHasValue = false;

	if (!expr || !expr[0])
		return;

	try {
		IATDebugger *dbg = ATGetDebugger();
		if (dbg) {
			mpExpr = ATDebuggerParseExpression(
				expr,
				ATGetDebuggerSymbolLookup(),
				dbg->GetExprOpts()
			);
		}
	} catch (const ATDebuggerExprParseException& ex) {
		mValueStr.sprintf("<Evaluation error: %s>", ex.c_str());
	}
}

bool ATImGuiWatchPaneImpl::WatchEntry::Update() {
	if (!mpExpr)
		return false;

	IATDebugger *dbg = ATGetDebugger();
	if (!dbg) return false;

	ATDebugExpEvalContext ctx = dbg->GetEvalContext();

	sint32 result;
	VDStringA newValue;
	if (mpExpr->Evaluate(result, ctx))
		newValue.sprintf("%d ($%04x)", result, (uint32)result);
	else
		newValue = "<Unable to evaluate>";

	if (mValueStr == newValue)
		return false;

	mValueStr = newValue;
	mbHasValue = true;
	return true;
}

ATImGuiWatchPaneImpl::ATImGuiWatchPaneImpl(uint32 paneId, int instanceIndex)
	: ATImGuiDebuggerPane(paneId, "Watch")
	, mInstanceIndex(instanceIndex)
{
	VDStringA title;
	title.sprintf("Watch %d", instanceIndex + 1);
	mTitle = title;

	// Start with one blank entry for user input (spreadsheet pattern)
	mEntries.emplace_back();
}

void ATImGuiWatchPaneImpl::OnDebuggerSystemStateUpdate(const ATDebuggerSystemState& state) {
	ATImGuiDebuggerPane::OnDebuggerSystemStateUpdate(state);
	if (!state.mbRunning)
		mbNeedsUpdate = true;
}

void ATImGuiWatchPaneImpl::OnDebuggerEvent(ATDebugEvent eventId) {
	if (eventId == kATDebugEvent_SymbolsChanged)
		mbNeedsUpdate = true;
}

void ATImGuiWatchPaneImpl::AddWatch(const char *expr) {
	// Insert before the last (blank) entry
	WatchEntry entry;
	entry.SetExpr(expr);
	entry.Update();

	int insertPos = (int)mEntries.size() - 1;
	if (insertPos < 0) insertPos = 0;
	mEntries.insert(mEntries.begin() + insertPos, std::move(entry));
}

void ATImGuiWatchPaneImpl::UpdateAllWatches() {
	mbNeedsUpdate = false;

	// Update all entries except the last blank one
	int n = (int)mEntries.size();
	if (n > 0) --n;  // skip last blank entry

	for (int i = 0; i < n; ++i)
		mEntries[i].Update();
}

bool ATImGuiWatchPaneImpl::Render() {
	bool open = true;

	if (mbFocusRequested) {
		ImGui::SetNextWindowFocus();
		mbFocusRequested = false;
	}

	ImGui::SetNextWindowSize(ImVec2(350, 250), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin(mTitle.c_str(), &open)) {
		mbHasFocus = false;
		ImGui::End();
		return open;
	}
	mbHasFocus = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

	if (mbNeedsUpdate)
		UpdateAllWatches();

	// Process deferred delete
	if (mDeleteRow >= 0 && mDeleteRow < (int)mEntries.size() - 1) {
		mEntries.erase(mEntries.begin() + mDeleteRow);
		if (mEditingRow == mDeleteRow)
			mEditingRow = -1;
		else if (mEditingRow > mDeleteRow)
			mEditingRow--;
	}
	mDeleteRow = -1;

	// Table with two columns: Expression | Value
	const ImGuiTableFlags tableFlags =
		ImGuiTableFlags_ScrollY |
		ImGuiTableFlags_RowBg |
		ImGuiTableFlags_Resizable |
		ImGuiTableFlags_BordersInnerV;

	if (ImGui::BeginTable("WatchTable", 2, tableFlags)) {
		ImGui::TableSetupScrollFreeze(0, 1);
		ImGui::TableSetupColumn("Expression", ImGuiTableColumnFlags_WidthStretch, 0.5f);
		ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 0.5f);
		ImGui::TableHeadersRow();

		int n = (int)mEntries.size();

		for (int i = 0; i < n; ++i) {
			WatchEntry& entry = mEntries[i];
			bool isBlankRow = (i == n - 1);
			ImGui::TableNextRow();
			ImGui::PushID(i);

			// Column 0: Expression (editable)
			ImGui::TableSetColumnIndex(0);

			if (mEditingRow == i) {
				// Inline editing mode
				ImGui::SetNextItemWidth(-1);
				bool committed = false;

				if (mbStartEdit && mStartEditRow == i) {
					ImGui::SetKeyboardFocusHere();
					mbStartEdit = false;
					mStartEditRow = -1;
					mEditGraceFrames = 2;  // skip focus-loss check for 2 frames
				}

				if (ImGui::InputText("##edit", mEditBuf, sizeof(mEditBuf),
						ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)) {
					committed = true;
				}

				// Also commit on focus loss (but not during the grace period
				// after SetKeyboardFocusHere, which takes effect next frame)
				if (mEditGraceFrames > 0)
					mEditGraceFrames--;
				else if (!committed && !ImGui::IsItemActive() && !ImGui::IsItemFocused()) {
					committed = true;
				}

				if (committed) {
					if (mEditBuf[0]) {
						// Non-empty: update expression
						entry.SetExpr(mEditBuf);
						entry.Update();

						// If this was the blank row, add a new blank row
						if (isBlankRow)
							mEntries.emplace_back();
					} else {
						// Empty: delete the entry (unless it's already the blank row)
						if (!isBlankRow)
							mDeleteRow = i;
					}
					mEditingRow = -1;
				}

				if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
					mEditingRow = -1;
				}
			} else {
				// Display mode — click or F2 to edit
				const char *displayText = entry.mExprStr.empty()
					? (isBlankRow ? "" : "(empty)")
					: entry.mExprStr.c_str();

				ImGui::Selectable("##sel", false,
					ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap);

				// Double-click to edit
				if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
					mEditingRow = i;
					snprintf(mEditBuf, sizeof(mEditBuf), "%s", entry.mExprStr.c_str());
					mbStartEdit = true;
					mStartEditRow = i;
				}

				// Delete key
				if (ImGui::IsItemFocused() && ImGui::IsKeyPressed(ImGuiKey_Delete) && !isBlankRow) {
					mDeleteRow = i;
				}

				// F2 to edit
				if (ImGui::IsItemFocused() && ImGui::IsKeyPressed(ImGuiKey_F2)) {
					mEditingRow = i;
					snprintf(mEditBuf, sizeof(mEditBuf), "%s", entry.mExprStr.c_str());
					mbStartEdit = true;
					mStartEditRow = i;
				}

				ImGui::SameLine();
				if (isBlankRow)
					ImGui::TextDisabled("%s", displayText);
				else
					ImGui::TextUnformatted(displayText);
			}

			// Column 1: Value
			ImGui::TableSetColumnIndex(1);
			if (!entry.mValueStr.empty())
				ImGui::TextUnformatted(entry.mValueStr.c_str());

			ImGui::PopID();
		}

		// Handle typing to start editing selected/last row
		if (mEditingRow < 0 && ImGui::IsWindowFocused() && !ImGui::GetIO().WantTextInput) {
			for (ImGuiKey key = ImGuiKey_A; key <= ImGuiKey_Z; key = (ImGuiKey)(key + 1)) {
				if (ImGui::IsKeyPressed(key)) {
					int editRow = n - 1;  // default to blank row
					mEditingRow = editRow;
					// Start with the typed character
					char ch = 'a' + (key - ImGuiKey_A);
					if (ImGui::GetIO().KeyShift)
						ch = 'A' + (key - ImGuiKey_A);
					mEditBuf[0] = ch;
					mEditBuf[1] = '\0';
					mbStartEdit = true;
					mStartEditRow = editRow;
					break;
				}
			}
		}

		ImGui::EndTable();
	}

	// Escape → focus Console (matches Windows pattern), but only if not editing
	if (mEditingRow < 0
			&& ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)
			&& !ImGui::GetIO().WantTextInput
			&& ImGui::IsKeyPressed(ImGuiKey_Escape))
		ATUIDebuggerFocusConsole();

	ImGui::End();
	return open;
}

// =========================================================================
// Registration — 4 instances
// =========================================================================

void ATUIDebuggerEnsureWatchPane(int index) {
	uint32 paneId = kATUIPaneId_WatchN + (uint32)index;
	if (!ATUIDebuggerGetPane(paneId)) {
		auto *pane = new ATImGuiWatchPaneImpl(paneId, index);
		ATUIDebuggerRegisterPane(pane);
	}
}

void ATUIDebuggerAddToWatch(const char *expr) {
	// Ensure Watch 1 exists and add the expression
	ATUIDebuggerEnsureWatchPane(0);
	auto *pane = static_cast<ATImGuiWatchPaneImpl *>(ATUIDebuggerGetPane(kATUIPaneId_WatchN));
	if (pane) {
		pane->AddWatch(expr);
		ATActivateUIPane(kATUIPaneId_WatchN, true, true);
	}
}
