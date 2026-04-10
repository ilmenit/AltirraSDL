//	AltirraSDL - Trace viewer: CPU History panel
//	Split out of ui_dbg_traceviewer_panels.cpp (Phase 2b).

#include <stdafx.h>
#include <algorithm>
#include <imgui.h>
#include <vd2/system/vdtypes.h>
#include <vd2/system/VDString.h>
#include <at/atcpu/history.h>
#include <at/atcpu/states.h>
#include "ui_dbg_traceviewer.h"
#include "traceviewer_panels_internal.h"
#include "trace.h"
#include "tracecpu.h"
#include "disasm.h"

namespace at_traceviewer_panels {

namespace {

struct HistoryState {
	double mLastFocusTime = -1;
	vdfastvector<ATCPUHistoryEntry> mEntries;
	uint32 mFocusEntryIndex = 0;
	uint32 mBaseIndex = 0; // absolute index of mEntries[0] in the channel
	bool mbValid = false;
	bool mbScrollToFocus = false;
};

HistoryState s_histState;

} // namespace

void ResetCPUHistoryState() {
	s_histState.mEntries.clear();
	s_histState.mbValid = false;
	s_histState.mLastFocusTime = -1;
}

// --- body extracted from ui_dbg_traceviewer_panels.cpp ---------------------
void RenderCPUHistory(ATImGuiTraceViewerContext& ctx) {
	if (!ctx.mpCPUHistoryChannel) {
		ImGui::TextUnformatted("No CPU history in this trace.");
		ImGui::TextUnformatted("Enable 'CPU Instruction History' in Trace settings and re-record.");
		if (!ctx.mSettings.mbTraceCpuInsns) {
			if (ImGui::Button("Enable CPU Instruction History and Start Trace")) {
				ctx.mSettings.mbTraceCpuInsns = true;
				if (auto fn = GetEnableCPUHistoryCallback())
					fn();
			}
		} else {
			ImGui::TextDisabled("(CPU Instruction History is now enabled. Start a new trace to capture data.)");
		}
		return;
	}

	ATTraceChannelCPUHistory& cpuCh = *ctx.mpCPUHistoryChannel;

	// Rebuild when focus time changes
	if (ctx.mbFocusTimeChanged || ctx.mFocusTime != s_histState.mLastFocusTime) {
		s_histState.mLastFocusTime = ctx.mFocusTime;
		s_histState.mEntries.clear();
		s_histState.mbValid = false;

		if (ctx.mFocusTime >= 0 && cpuCh.GetEventCount() > 0) {
			// Use a zero-based cursor since FindEvent returns absolute positions
			// and ReadHistoryEvents adds cursor.mIterPos to the offset
			auto cursor = cpuCh.StartHistoryIteration(0, 0);
			uint32 focusIdx = cpuCh.FindEvent(cursor, ctx.mFocusTime);

			// Read a window of entries around focus (400K window, matching Windows)
			uint32 startIdx = (focusIdx > 200000) ? focusIdx - 200000 : 0;
			uint32 endIdx = std::min(startIdx + 400000, cpuCh.GetEventCount());

			const ATCPUHistoryEntry *hents[256];
			uint32 pos = startIdx;
			uint32 relFocusIdx = 0;

			while (pos < endIdx) {
				uint32 n = cpuCh.ReadHistoryEvents(cursor, hents, pos, std::min<uint32>(endIdx - pos, 256));
				if (!n)
					break;

				for (uint32 i = 0; i < n; ++i) {
					if (pos + i == focusIdx)
						relFocusIdx = (uint32)s_histState.mEntries.size();
					s_histState.mEntries.push_back(*hents[i]);
				}
				pos += n;
			}

			s_histState.mFocusEntryIndex = relFocusIdx;
			s_histState.mBaseIndex = startIdx;
			s_histState.mbValid = !s_histState.mEntries.empty();
			s_histState.mbScrollToFocus = s_histState.mbValid;
		}

		// Note: mbFocusTimeChanged is NOT cleared here — the main Render() clears
		// it after all panels have processed it, so Log tab also sees the change.
	}

	if (!s_histState.mbValid) {
		ImGui::TextUnformatted("Click on the timeline to show CPU instructions at that point.");
		return;
	}

	uint32 baseCycle = cpuCh.GetHistoryBaseCycle();

	ImGuiTableFlags tableFlags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg
		| ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingFixedFit;

	if (ImGui::BeginTable("##CPUHistory", 8, tableFlags)) {
		ImGui::TableSetupScrollFreeze(0, 1);
		ImGui::TableSetupColumn("Cycle", ImGuiTableColumnFlags_WidthFixed, 70);
		ImGui::TableSetupColumn("PC", ImGuiTableColumnFlags_WidthFixed, 50);
		ImGui::TableSetupColumn("Instruction", ImGuiTableColumnFlags_WidthFixed, 200);
		ImGui::TableSetupColumn("A", ImGuiTableColumnFlags_WidthFixed, 30);
		ImGui::TableSetupColumn("X", ImGuiTableColumnFlags_WidthFixed, 30);
		ImGui::TableSetupColumn("Y", ImGuiTableColumnFlags_WidthFixed, 30);
		ImGui::TableSetupColumn("S", ImGuiTableColumnFlags_WidthFixed, 30);
		ImGui::TableSetupColumn("Flags", ImGuiTableColumnFlags_WidthFixed, 80);
		ImGui::TableHeadersRow();

		ImGuiListClipper clipper;
		clipper.Begin((int)s_histState.mEntries.size());
		while (clipper.Step()) {
			for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
				const ATCPUHistoryEntry& he = s_histState.mEntries[row];
				bool isFocus = ((uint32)row == s_histState.mFocusEntryIndex);

				ImGui::TableNextRow();

				if (isFocus) {
					ImU32 hlColor = IM_COL32(80, 80, 40, 255);
					ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, hlColor);
				}

				// Cycle
				ImGui::TableSetColumnIndex(0);
				// Make the first column selectable for click-to-navigate
				char labelBuf[32];
				snprintf(labelBuf, sizeof(labelBuf), "%u", he.mCycle - baseCycle);
				if (ImGui::Selectable(labelBuf, isFocus, ImGuiSelectableFlags_SpanAllColumns)) {
					// Navigate to this instruction's time
					uint32 absIdx = s_histState.mBaseIndex + (uint32)row;
					auto cursor = cpuCh.StartHistoryIteration(0, 0);
					double t = cpuCh.GetEventTime(cursor, absIdx);
					if (t >= 0) {
						ctx.mFocusTime = t;
						ctx.mbFocusTimeChanged = true;
					}
				}

				// PC
				ImGui::TableSetColumnIndex(1);
				ImGui::Text("%04X", he.mPC);

				// Opcode / disassembly
				ImGui::TableSetColumnIndex(2);
				{
					VDStringA buf;
					// decodeRefsHistory=true: we have no live IATDebugTarget, so
					// the disassembler must use hent.mEA for indirect modes
					// (JMP (abs), JML [dp], etc.) instead of target->DebugReadByte,
					// which would segfault on nullptr.
					ATDisassembleInsn(buf, nullptr, cpuCh.GetDisasmMode(), he, false, true, false, true, false);
					ImGui::TextUnformatted(buf.c_str());
				}

				// Registers
				ImGui::TableSetColumnIndex(3);
				ImGui::Text("%02X", he.mA);
				ImGui::TableSetColumnIndex(4);
				ImGui::Text("%02X", he.mX);
				ImGui::TableSetColumnIndex(5);
				ImGui::Text("%02X", he.mY);
				ImGui::TableSetColumnIndex(6);
				ImGui::Text("%02X", he.mS);

				// Flags
				ImGui::TableSetColumnIndex(7);
				{
					char flags[9];
					flags[0] = (he.mP & 0x80) ? 'N' : '-';
					flags[1] = (he.mP & 0x40) ? 'V' : '-';
					flags[2] = '*';
					flags[3] = (he.mP & 0x10) ? 'B' : '-';
					flags[4] = (he.mP & 0x08) ? 'D' : '-';
					flags[5] = (he.mP & 0x04) ? 'I' : '-';
					flags[6] = (he.mP & 0x02) ? 'Z' : '-';
					flags[7] = (he.mP & 0x01) ? 'C' : '-';
					flags[8] = 0;
					ImGui::TextUnformatted(flags);
				}
			}
		}

		// Auto-scroll to focus entry (only once after rebuild)
		if (s_histState.mbScrollToFocus) {
			s_histState.mbScrollToFocus = false;
			float itemHeight = ImGui::GetTextLineHeightWithSpacing();
			float targetY = s_histState.mFocusEntryIndex * itemHeight - ImGui::GetWindowHeight() * 0.5f;
			if (targetY < 0) targetY = 0;
			float maxY = ImGui::GetScrollMaxY();
			if (targetY > maxY) targetY = maxY;
			ImGui::SetScrollY(targetY);
		}

		ImGui::EndTable();
	}
}

} // namespace at_traceviewer_panels
