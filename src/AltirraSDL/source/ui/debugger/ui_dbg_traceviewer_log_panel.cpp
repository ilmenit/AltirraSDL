//	AltirraSDL - Trace viewer: Log panel
//	Split out of ui_dbg_traceviewer_panels.cpp (Phase 2b).

#include <stdafx.h>
#include <vector>
#include <SDL3/SDL.h>
#include <imgui.h>
#include <vd2/system/vdtypes.h>
#include <vd2/system/VDString.h>
#include "ui_dbg_traceviewer.h"
#include "traceviewer_panels_internal.h"
#include "trace.h"

namespace at_traceviewer_panels {

namespace {

struct LogState {
	struct LogEntry {
		double mTime;
		VDStringA mName;
	};
	std::vector<LogEntry> mEntries;
	bool mbValid = false;
	int mTimestampMode = 2;		// 0=none, 1=beam, 2=cycle, 3=microseconds
	IATTraceChannel *mpLastChannel = nullptr;
	double mTimestampOrigin = 0;	// offset for relative timestamps
	int mSelectedRow = -1;
	bool mbScrollToSelected = false;
};

LogState s_logState;

} // namespace

void ResetLogState() {
	s_logState.mbValid = false;
	s_logState.mEntries.clear();
	s_logState.mpLastChannel = nullptr;
}

// --- body extracted from ui_dbg_traceviewer_panels.cpp ---------------------
// =========================================================================
// Log tab
// =========================================================================

static void RebuildLogEntries(ATImGuiTraceViewerContext& ctx) {
	s_logState.mEntries.clear();
	s_logState.mbValid = false;
	s_logState.mpLastChannel = ctx.mpLogChannel;
	s_logState.mSelectedRow = -1;
	s_logState.mTimestampOrigin = 0;

	if (!ctx.mpLogChannel)
		return;

	IATTraceChannel *ch = ctx.mpLogChannel;
	ch->StartIteration(0, ch->GetDuration(), 0);

	ATTraceEvent ev;
	while (ch->GetNextEvent(ev)) {
		LogState::LogEntry entry;
		entry.mTime = ev.mEventStart;
		if (ev.mpName)
			entry.mName = VDTextWToU8(VDStringSpanW(ev.mpName));
		s_logState.mEntries.push_back(std::move(entry));
	}

	s_logState.mbValid = true;
}

static void FormatLogTimestamp(char *buf, size_t bufSize, double time, int mode, const ATImGuiTraceViewerContext& ctx) {
	double relTime = time - s_logState.mTimestampOrigin;

	switch (mode) {
		case 0:		// None
			buf[0] = 0;
			break;
		case 1: {	// Beam position: (Frame:Y,X) — padded to match Windows
			if (ctx.mpCPUHistoryChannel) {
				uint32 cycle = (uint32)(time / ctx.mpCPUHistoryChannel->GetSecondsPerTick());
				ATCPUBeamPosition bp = ctx.mTimestampDecoder.GetBeamPosition(cycle);
				snprintf(buf, bufSize, "(%5u:%3u,%3u) ", bp.mFrame, bp.mY, bp.mX);
			} else {
				buf[0] = 0;
			}
			break;
		}
		case 2:		// Cycle: (T±cycles) — padded to match Windows
			if (ctx.mpCPUHistoryChannel) {
				double secsPerTick = ctx.mpCPUHistoryChannel->GetSecondsPerTick();
				sint64 relCycles = (sint64)(relTime / secsPerTick);
				snprintf(buf, bufSize, "(T%+-9lld) ", (long long)relCycles);
			} else {
				snprintf(buf, bufSize, "(%9.6f) ", relTime);
			}
			break;
		case 3:		// Microseconds: (seconds.microseconds) — padded to match Windows
			snprintf(buf, bufSize, "(%9.6f) ", relTime);
			break;
		default:
			buf[0] = 0;
			break;
	}
}

static void CopyLogEntries(const ATImGuiTraceViewerContext& ctx, bool allEntries, int selectedRow) {
	VDStringA result;
	char tsBuf[64];

	for (int i = 0; i < (int)s_logState.mEntries.size(); ++i) {
		if (!allEntries && i != selectedRow)
			continue;

		const auto& entry = s_logState.mEntries[i];
		if (s_logState.mTimestampMode > 0) {
			FormatLogTimestamp(tsBuf, sizeof(tsBuf), entry.mTime, s_logState.mTimestampMode, ctx);
			result += tsBuf;
		}
		result += entry.mName;
		result += '\n';
	}

	if (!result.empty())
		SDL_SetClipboardText(result.c_str());
}

void RenderLog(ATImGuiTraceViewerContext& ctx) {
	if (!ctx.mpLogChannel) {
		ImGui::TextUnformatted("No log channel in this trace.");
		return;
	}

	// Rebuild if channel changed
	if (ctx.mpLogChannel != s_logState.mpLastChannel)
		RebuildLogEntries(ctx);

	if (!s_logState.mbValid) {
		RebuildLogEntries(ctx);
		if (!s_logState.mbValid)
			return;
	}

	// Auto-select nearest log entry when focus time changes
	if (ctx.mbFocusTimeChanged && ctx.mFocusTime >= 0 && !s_logState.mEntries.empty()) {
		// Binary search for nearest entry
		int best = 0;
		int lo = 0, hi = (int)s_logState.mEntries.size() - 1;
		while (lo <= hi) {
			int mid = (lo + hi) / 2;
			if (s_logState.mEntries[mid].mTime < ctx.mFocusTime)
				lo = mid + 1;
			else
				hi = mid - 1;
		}
		// lo is now the first entry >= focusTime; check lo and lo-1
		best = lo;
		if (best >= (int)s_logState.mEntries.size())
			best = (int)s_logState.mEntries.size() - 1;
		if (best > 0 && fabs(s_logState.mEntries[best - 1].mTime - ctx.mFocusTime) < fabs(s_logState.mEntries[best].mTime - ctx.mFocusTime))
			best = best - 1;
		s_logState.mSelectedRow = best;
		s_logState.mbScrollToSelected = true;
	}

	// Timestamp mode selector
	const char *tsModes[] = { "None", "Beam Position", "Cycle", "Microseconds" };
	ImGui::Combo("Timestamp", &s_logState.mTimestampMode, tsModes, 4);

	ImGui::SameLine();
	if (ImGui::Button("Copy Selected"))
		CopyLogEntries(ctx, false, s_logState.mSelectedRow);
	ImGui::SameLine();
	if (ImGui::Button("Copy All"))
		CopyLogEntries(ctx, true, -1);
	ImGui::SameLine();
	if (ImGui::Button("Reset Origin"))
		s_logState.mTimestampOrigin = 0;

	ImGuiTableFlags tableFlags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg
		| ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable;

	if (ImGui::BeginTable("##LogTable", 2, tableFlags)) {
		ImGui::TableSetupScrollFreeze(0, 1);
		ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, 150);
		ImGui::TableSetupColumn("Event", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableHeadersRow();

		char tsBuf[64];

		ImGuiListClipper clipper;
		clipper.Begin((int)s_logState.mEntries.size());
		while (clipper.Step()) {
			for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
				const auto& entry = s_logState.mEntries[row];
				bool isSelected = (row == s_logState.mSelectedRow);

				ImGui::TableNextRow();

				if (isSelected)
					ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, IM_COL32(60, 60, 100, 255));

				// Timestamp column
				ImGui::TableSetColumnIndex(0);
				FormatLogTimestamp(tsBuf, sizeof(tsBuf), entry.mTime, s_logState.mTimestampMode, ctx);
				ImGui::TextUnformatted(tsBuf);

				// Event column
				ImGui::TableSetColumnIndex(1);
				ImGui::TextUnformatted(entry.mName.c_str());

				// Row interaction
				if (ImGui::IsItemHovered()) {
					if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
						s_logState.mSelectedRow = row;

					// Double-click to navigate timeline
					if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
						ctx.mFocusTime = entry.mTime;
						ctx.mbFocusTimeChanged = true;
					}

					// Right-click context menu
					if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
						s_logState.mSelectedRow = row;
						ImGui::OpenPopup("LogContextMenu");
					}
				}
			}
		}

		// Context menu
		if (ImGui::BeginPopup("LogContextMenu")) {
			if (ImGui::MenuItem("Copy Selected", nullptr, false, s_logState.mSelectedRow >= 0))
				CopyLogEntries(ctx, false, s_logState.mSelectedRow);
			if (ImGui::MenuItem("Copy All", nullptr, false, !s_logState.mEntries.empty()))
				CopyLogEntries(ctx, true, -1);

			ImGui::Separator();

			if (ImGui::MenuItem("Reset Timestamp Origin"))
				s_logState.mTimestampOrigin = 0;
			if (ImGui::MenuItem("Set Timestamp Origin", nullptr, false, s_logState.mSelectedRow >= 0 && s_logState.mSelectedRow < (int)s_logState.mEntries.size()))
				s_logState.mTimestampOrigin = s_logState.mEntries[s_logState.mSelectedRow].mTime;

			ImGui::Separator();

			for (int m = 0; m < 4; ++m) {
				if (ImGui::MenuItem(tsModes[m], nullptr, s_logState.mTimestampMode == m))
					s_logState.mTimestampMode = m;
			}

			ImGui::EndPopup();
		}

		// Auto-scroll to selected entry
		if (s_logState.mbScrollToSelected && s_logState.mSelectedRow >= 0) {
			s_logState.mbScrollToSelected = false;
			float itemHeight = ImGui::GetTextLineHeightWithSpacing();
			float targetY = s_logState.mSelectedRow * itemHeight - ImGui::GetWindowHeight() * 0.5f;
			if (targetY < 0) targetY = 0;
			float maxY = ImGui::GetScrollMaxY();
			if (targetY > maxY) targetY = maxY;
			ImGui::SetScrollY(targetY);
		}

		ImGui::EndTable();
	}
}

} // namespace at_traceviewer_panels
