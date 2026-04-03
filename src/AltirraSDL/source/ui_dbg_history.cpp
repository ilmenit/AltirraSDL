//	AltirraSDL - Dear ImGui debugger history pane
//	Replaces Win32 ATHistoryWindow (uidbghistory.cpp).
//	Shows recently executed CPU instructions using IATDebugTargetHistory.

#include <stdafx.h>
#include <imgui.h>
#include <vd2/system/vdtypes.h>
#include <vd2/system/VDString.h>
#include <vd2/system/unknown.h>
#include <at/atcpu/history.h>
#include <at/atdebugger/target.h>
#include "ui_debugger.h"
#include "console.h"
#include "debugger.h"
#include "disasm.h"
#include "simulator.h"
#include "cpu.h"

extern ATSimulator g_sim;

// =========================================================================
// History pane
// =========================================================================

class ATImGuiHistoryPaneImpl final : public ATImGuiDebuggerPane {
public:
	ATImGuiHistoryPaneImpl();

	bool Render() override;
	void OnDebuggerSystemStateUpdate(const ATDebuggerSystemState& state) override;
	void OnDebuggerEvent(ATDebugEvent eventId) override;

private:
	void RebuildView();

	struct HistLine {
		uint32 mIndex;
		uint32 mCycle;
		uint16 mPC;
		bool   mbIRQ;
		bool   mbNMI;
		VDStringA mDisasm;
		VDStringA mRegs;	// A=xx X=xx Y=xx S=xx P=xx
	};

	std::vector<HistLine> mLines;
	bool mbNeedsRebuild = true;
	bool mbScrollToBottom = false;
	bool mbHistoryEnabled = false;

	// How many history entries to show
	static constexpr int kMaxDisplayLines = 512;
};

ATImGuiHistoryPaneImpl::ATImGuiHistoryPaneImpl()
	: ATImGuiDebuggerPane(kATUIPaneId_History, "History")
{
}

void ATImGuiHistoryPaneImpl::OnDebuggerSystemStateUpdate(const ATDebuggerSystemState& state) {
	ATImGuiDebuggerPane::OnDebuggerSystemStateUpdate(state);

	// Only rebuild when stopped — matches Windows behavior
	if (!state.mbRunning)
		mbNeedsRebuild = true;
}

void ATImGuiHistoryPaneImpl::OnDebuggerEvent(ATDebugEvent eventId) {
	mbNeedsRebuild = true;
}

void ATImGuiHistoryPaneImpl::RebuildView() {
	mbNeedsRebuild = false;
	mLines.clear();

	IATDebugger *dbg = ATGetDebugger();
	if (!dbg)
		return;

	IATDebugTarget *target = dbg->GetTarget();
	if (!target)
		return;

	IATDebugTargetHistory *history = vdpoly_cast<IATDebugTargetHistory *>(target);
	if (!history) {
		mbHistoryEnabled = false;
		return;
	}

	mbHistoryEnabled = history->GetHistoryEnabled();
	if (!mbHistoryEnabled)
		return;

	auto range = history->GetHistoryRange();
	uint32 start = range.first;
	uint32 end = range.second;

	if (start == end)
		return;

	// Show the most recent N instructions
	uint32 count = end - start;
	if (count > kMaxDisplayLines)
		start = end - kMaxDisplayLines;
	count = end - start;

	const ATDebugDisasmMode disasmMode = target->GetDisasmMode();

	// Extract in batches
	mLines.reserve(count);

	const uint32 kBatchSize = 64;
	const ATCPUHistoryEntry *batch[kBatchSize];

	for (uint32 pos = start; pos < end; ) {
		uint32 n = std::min<uint32>(end - pos, kBatchSize);
		uint32 got = history->ExtractHistory(batch, pos, n);

		for (uint32 i = 0; i < got; ++i) {
			const ATCPUHistoryEntry& he = *batch[i];

			HistLine hl;
			hl.mIndex = pos + i;
			hl.mCycle = he.mCycle;
			hl.mPC = he.mPC;
			hl.mbIRQ = he.mbIRQ;
			hl.mbNMI = he.mbNMI;

			// Disassemble the instruction
			hl.mDisasm.clear();
			ATDisassembleInsn(hl.mDisasm,
				target,
				disasmMode,
				he,
				false,		// decodeReferences
				true,		// decodeRefsHistory
				true,		// showPCAddress
				true,		// showCodeBytes
				true,		// showLabels
				false,		// lowercaseOps
				false,		// wideOpcode
				false,		// showLabelNamespaces
				true,		// showSymbols
				false		// showGlobalPC
			);

			// Format register state (matches Windows history view format)
			if (disasmMode == kATDebugDisasmMode_Z80) {
				hl.mRegs.sprintf("A=%02X F=%02X BC=%02X%02X DE=%02X%02X HL=%02X%02X",
					he.mZ80_A, he.mZ80_F,
					he.mZ80_B, he.mZ80_C,
					he.mZ80_D, he.mExt.mZ80_E,
					he.mExt.mZ80_H, he.mExt.mZ80_L);
			} else {
				hl.mRegs.sprintf("A=%02X X=%02X Y=%02X S=%02X P=%02X",
					he.mA, he.mX, he.mY, he.mS, he.mP);
			}

			mLines.push_back(std::move(hl));
		}

		pos += got;
		if (got < n)
			break;
	}
}

bool ATImGuiHistoryPaneImpl::Render() {
	bool open = true;

	if (mbFocusRequested) {
		ImGui::SetNextWindowFocus();
		mbFocusRequested = false;
	}

	if (!ImGui::Begin(mTitle.c_str(), &open)) {
		ImGui::End();
		return open;
	}

	if (mbNeedsRebuild) {
		RebuildView();
		mbScrollToBottom = true;
	}

	if (!mbHistoryEnabled) {
		ImGui::TextWrapped("CPU history is not enabled.");
		ImGui::Spacing();
		if (ImGui::Button("Enable CPU History")) {
			g_sim.GetCPU().SetHistoryEnabled(true);
			mbNeedsRebuild = true;
		}
		ImGui::End();
		return open;
	}

	if (mLines.empty()) {
		if (mbStateValid && mLastState.mbRunning)
			ImGui::TextDisabled("(running)");
		else
			ImGui::TextDisabled("(no history)");
		ImGui::End();
		return open;
	}

	// Table with columns: Cycle | Disassembly | Registers
	const ImGuiTableFlags tableFlags =
		ImGuiTableFlags_ScrollY |
		ImGuiTableFlags_RowBg |
		ImGuiTableFlags_Resizable |
		ImGuiTableFlags_Reorderable;

	if (ImGui::BeginTable("HistoryTable", 3, tableFlags)) {
		ImGui::TableSetupScrollFreeze(0, 1);
		ImGui::TableSetupColumn("Cycle", ImGuiTableColumnFlags_WidthFixed, 80.0f);
		ImGui::TableSetupColumn("Instruction", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableSetupColumn("Registers", ImGuiTableColumnFlags_WidthFixed, 200.0f);
		ImGui::TableHeadersRow();

		ImGuiListClipper clipper;
		clipper.Begin((int)mLines.size());

		while (clipper.Step()) {
			for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
				const HistLine& hl = mLines[i];
				ImGui::TableNextRow();

				// IRQ/NMI coloring
				ImU32 textColor = 0;
				if (hl.mbNMI && hl.mbIRQ)
					textColor = IM_COL32(200, 200, 0, 255);	// HLE (yellow)
				else if (hl.mbNMI)
					textColor = IM_COL32(255, 128, 128, 255);	// NMI (red)
				else if (hl.mbIRQ)
					textColor = IM_COL32(128, 128, 255, 255);	// IRQ (blue)

				ImGui::TableSetColumnIndex(0);
				if (textColor) ImGui::PushStyleColor(ImGuiCol_Text, textColor);
				ImGui::Text("%u", hl.mCycle);

				ImGui::TableSetColumnIndex(1);
				ImGui::TextUnformatted(hl.mDisasm.c_str(),
					hl.mDisasm.c_str() + hl.mDisasm.size());

				ImGui::TableSetColumnIndex(2);
				ImGui::TextUnformatted(hl.mRegs.c_str(),
					hl.mRegs.c_str() + hl.mRegs.size());
				if (textColor) ImGui::PopStyleColor();
			}
		}

		// Scroll to bottom after rebuild (show most recent instructions)
		if (mbScrollToBottom) {
			ImGui::SetScrollHereY(1.0f);
			mbScrollToBottom = false;
		}

		ImGui::EndTable();
	}

	ImGui::End();
	return open;
}

// =========================================================================
// Registration
// =========================================================================

void ATUIDebuggerEnsureHistoryPane() {
	if (!ATUIDebuggerGetPane(kATUIPaneId_History)) {
		auto *pane = new ATImGuiHistoryPaneImpl();
		ATUIDebuggerRegisterPane(pane);
	}
}
