//	AltirraSDL - Dear ImGui debugger call stack pane
//	Replaces Win32 ATCallStackWindow (uidbgcallstack.cpp).
//	Shows stack frames with SP, PC, symbol names, and current frame indicator.
//	Double-click to jump to a frame.

#include <stdafx.h>
#include <imgui.h>
#include <vd2/system/vdtypes.h>
#include <vd2/system/VDString.h>
#include <at/atdebugger/symbols.h>
#include "ui_debugger.h"
#include "console.h"
#include "debugger.h"
#include "simulator.h"

extern ATSimulator g_sim;

// =========================================================================
// Call Stack pane
// =========================================================================

class ATImGuiCallStackPaneImpl final : public ATImGuiDebuggerPane {
public:
	ATImGuiCallStackPaneImpl();

	bool Render() override;
	void OnDebuggerSystemStateUpdate(const ATDebuggerSystemState& state) override;

private:
	void RebuildStack();

	struct FrameEntry {
		uint32 mPC;
		uint16 mSP;
		uint8  mP;
		bool   mbCurrent;		// this is the active frame
		VDStringA mText;		// formatted display line
	};

	std::vector<FrameEntry> mFrames;
	bool mbNeedsRebuild = true;
	uint32 mFrameExtPC = 0;
};

ATImGuiCallStackPaneImpl::ATImGuiCallStackPaneImpl()
	: ATImGuiDebuggerPane(kATUIPaneId_CallStack, "Call Stack")
{
}

void ATImGuiCallStackPaneImpl::OnDebuggerSystemStateUpdate(const ATDebuggerSystemState& state) {
	ATImGuiDebuggerPane::OnDebuggerSystemStateUpdate(state);
	mFrameExtPC = state.mFrameExtPC;
	if (!state.mbRunning)
		mbNeedsRebuild = true;
}

void ATImGuiCallStackPaneImpl::RebuildStack() {
	mbNeedsRebuild = false;
	mFrames.clear();

	IATDebugger *dbg = ATGetDebugger();
	if (!dbg || !mbStateValid || mLastState.mbRunning)
		return;

	// Get stack frames (up to 64)
	ATCallStackFrame rawFrames[64];
	uint32 count = dbg->GetCallStack(rawFrames, 64);

	IATDebuggerSymbolLookup *lookup = ATGetDebuggerSymbolLookup();

	for (uint32 i = 0; i < count; ++i) {
		const ATCallStackFrame& f = rawFrames[i];

		FrameEntry entry;
		entry.mPC = f.mPC;
		entry.mSP = f.mSP;
		entry.mP = f.mP;
		entry.mbCurrent = (f.mPC == mFrameExtPC);

		// Format: [>]XXXX: [*]XXXX (symbol)
		// > = current frame, * = break flag set
		VDStringA text;
		text.sprintf("%c%04X: %c%04X",
			entry.mbCurrent ? '>' : ' ',
			f.mSP,
			(f.mP & 0x04) ? '*' : ' ',
			f.mPC);

		// Look up symbol name
		if (lookup) {
			ATSymbol sym;
			if (lookup->LookupSymbol(f.mPC, kATSymbol_Execute, sym)) {
				text.append_sprintf(" (%s", sym.mpName);
				if (sym.mOffset != f.mPC)
					text.append_sprintf("+%u", f.mPC - sym.mOffset);
				text += ')';
			}
		}

		entry.mText = text;
		mFrames.push_back(std::move(entry));
	}
}

bool ATImGuiCallStackPaneImpl::Render() {
	bool open = true;

	if (mbFocusRequested) {
		ImGui::SetNextWindowFocus();
		mbFocusRequested = false;
	}

	ImGui::SetNextWindowSize(ImVec2(300, 200), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin(mTitle.c_str(), &open)) {
		mbHasFocus = false;
		ImGui::End();
		return open;
	}
	mbHasFocus = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

	if (mbNeedsRebuild)
		RebuildStack();

	if (mFrames.empty()) {
		if (mbStateValid && mLastState.mbRunning)
			ImGui::TextDisabled("(running)");
		else
			ImGui::TextDisabled("(no call stack)");
		ImGui::End();
		return open;
	}

	IATDebugger *dbg = ATGetDebugger();

	for (int i = 0; i < (int)mFrames.size(); ++i) {
		const FrameEntry& f = mFrames[i];

		// Highlight current frame
		if (f.mbCurrent)
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 0.3f, 1.0f));

		ImGui::PushID(i);
		ImGui::Selectable(f.mText.c_str(), f.mbCurrent);
		// Double-click to jump to frame (matches Windows LBN_DBLCLK)
		if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
			if (dbg)
				dbg->SetFramePC(f.mPC);
		}
		ImGui::PopID();

		if (f.mbCurrent)
			ImGui::PopStyleColor();
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

void ATUIDebuggerEnsureCallStackPane() {
	if (!ATUIDebuggerGetPane(kATUIPaneId_CallStack)) {
		auto *pane = new ATImGuiCallStackPaneImpl();
		ATUIDebuggerRegisterPane(pane);
	}
}
