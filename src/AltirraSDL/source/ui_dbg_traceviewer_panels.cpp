//	AltirraSDL - Dear ImGui Performance Analyzer (Trace Viewer)
//	Bottom panel tabs: CPU History, CPU Profile, Log.

#include <stdafx.h>
#include <cmath>
#include <algorithm>
#include <vector>
#include <SDL3/SDL.h>
#include <imgui.h>
#include <vd2/system/vdtypes.h>
#include <vd2/system/VDString.h>
#include <vd2/system/text.h>
#include <vd2/system/unknown.h>
#include <at/atcpu/history.h>
#include <at/atcpu/states.h>
#include "ui_dbg_traceviewer.h"
#include "trace.h"
#include "tracecpu.h"
#include "profiler.h"
#include "debugger.h"
#include "simulator.h"
#include "disasm.h"

extern ATSimulator g_sim;

// =========================================================================
// CPU History tab state
// =========================================================================

namespace {

struct HistoryState {
	double mLastFocusTime = -1;
	vdfastvector<ATCPUHistoryEntry> mEntries;
	uint32 mFocusEntryIndex = 0;
	bool mbValid = false;
};

static HistoryState s_histState;

// =========================================================================
// CPU Profile tab state
// =========================================================================

struct ProfileState {
	ATProfileSession mSession;
	vdrefptr<ATProfileMergedFrame> mpMergedFrame;
	ATProfileMode mMode = kATProfileMode_Insns;
	bool mbNeedsRefresh = true;
	bool mbValid = false;
	double mLastSelectStart = -1;
	double mLastSelectEnd = -1;

	struct SortedRecord {
		uint32 mAddress;
		uint32 mCalls;
		uint32 mInsns;
		uint32 mCycles;
		uint32 mUnhaltedCycles;
	};
	vdfastvector<SortedRecord> mSortedRecords;
	ImGuiTableSortSpecs *mpLastSortSpecs = nullptr;
};

static ProfileState s_profState;

// =========================================================================
// Log tab state
// =========================================================================

struct LogState {
	struct LogEntry {
		double mTime;
		VDStringA mName;
	};
	std::vector<LogEntry> mEntries;
	bool mbValid = false;
	int mTimestampMode = 2;		// 0=none, 1=beam, 2=cycle, 3=microseconds
	IATTraceChannel *mpLastChannel = nullptr;
};

static LogState s_logState;

} // anonymous namespace

// =========================================================================
// CPU History tab
// =========================================================================

static void RenderCPUHistory(ATImGuiTraceViewerContext& ctx) {
	if (!ctx.mpCPUHistoryChannel) {
		ImGui::TextUnformatted("No CPU history in this trace. Enable 'CPU Instruction History' in Trace settings.");
		return;
	}

	ATTraceChannelCPUHistory& cpuCh = *ctx.mpCPUHistoryChannel;

	// Rebuild when focus time changes
	if (ctx.mbFocusTimeChanged || ctx.mFocusTime != s_histState.mLastFocusTime) {
		s_histState.mLastFocusTime = ctx.mFocusTime;
		s_histState.mEntries.clear();
		s_histState.mbValid = false;

		if (ctx.mFocusTime >= 0 && cpuCh.GetEventCount() > 0) {
			// Find the event at focus time and read surrounding entries
			auto cursor = cpuCh.StartHistoryIteration(ctx.mFocusTime, -100);
			uint32 focusIdx = cpuCh.FindEvent(cursor, ctx.mFocusTime);

			// Read a window of entries around focus
			uint32 startIdx = (focusIdx > 100) ? focusIdx - 100 : 0;
			uint32 endIdx = std::min(startIdx + 200, cpuCh.GetEventCount());

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
			s_histState.mbValid = !s_histState.mEntries.empty();
		}

		ctx.mbFocusTimeChanged = false;
	}

	if (!s_histState.mbValid) {
		ImGui::TextUnformatted("Click on the timeline to show CPU instructions at that point.");
		return;
	}

	uint32 baseCycle = cpuCh.GetHistoryBaseCycle();

	ImGuiTableFlags tableFlags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg
		| ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingFixedFit;

	if (ImGui::BeginTable("##CPUHistory", 7, tableFlags)) {
		ImGui::TableSetupScrollFreeze(0, 1);
		ImGui::TableSetupColumn("Cycle", ImGuiTableColumnFlags_WidthFixed, 70);
		ImGui::TableSetupColumn("PC", ImGuiTableColumnFlags_WidthFixed, 50);
		ImGui::TableSetupColumn("Opcode", ImGuiTableColumnFlags_WidthFixed, 80);
		ImGui::TableSetupColumn("A", ImGuiTableColumnFlags_WidthFixed, 30);
		ImGui::TableSetupColumn("X", ImGuiTableColumnFlags_WidthFixed, 30);
		ImGui::TableSetupColumn("Y", ImGuiTableColumnFlags_WidthFixed, 30);
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
				ImGui::Text("%u", he.mCycle - baseCycle);

				// PC
				ImGui::TableSetColumnIndex(1);
				ImGui::Text("%04X", he.mPC);

				// Opcode / disassembly
				ImGui::TableSetColumnIndex(2);
				{
					VDStringA buf;
					ATDisassembleInsn(buf, nullptr, cpuCh.GetDisasmMode(), he, false, false, false, true, false);
					ImGui::TextUnformatted(buf.c_str());
				}

				// Registers
				ImGui::TableSetColumnIndex(3);
				ImGui::Text("%02X", he.mA);
				ImGui::TableSetColumnIndex(4);
				ImGui::Text("%02X", he.mX);
				ImGui::TableSetColumnIndex(5);
				ImGui::Text("%02X", he.mY);

				// Flags
				ImGui::TableSetColumnIndex(6);
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

		// Auto-scroll to focus entry
		if (ctx.mFocusTime != s_histState.mLastFocusTime || s_histState.mFocusEntryIndex > 0) {
			float itemHeight = ImGui::GetTextLineHeightWithSpacing();
			ImGui::SetScrollY(s_histState.mFocusEntryIndex * itemHeight - ImGui::GetWindowHeight() * 0.5f);
		}

		ImGui::EndTable();
	}
}

// =========================================================================
// CPU Profile tab
// =========================================================================

static void BuildProfile(ATImGuiTraceViewerContext& ctx) {
	s_profState.mbValid = false;
	s_profState.mSortedRecords.clear();

	if (!ctx.mpCPUHistoryChannel)
		return;

	ATTraceChannelCPUHistory& cpuCh = *ctx.mpCPUHistoryChannel;
	if (cpuCh.GetEventCount() == 0)
		return;

	ATCPUProfileBuilder builder;
	builder.Init(s_profState.mMode, kATProfileCounterMode_None, kATProfileCounterMode_None);

	uint32 baseCycle = cpuCh.GetHistoryBaseCycle();
	const ATCPUTimestampDecoder& tsDecoder = cpuCh.GetTimestampDecoder();

	// Determine range
	double rangeStart = 0;
	double rangeEnd = cpuCh.GetDuration();
	if (ctx.mbSelectionValid) {
		rangeStart = std::min(ctx.mSelectStart, ctx.mSelectEnd);
		rangeEnd = std::max(ctx.mSelectStart, ctx.mSelectEnd);
	}

	auto cursor = cpuCh.StartHistoryIteration(rangeStart, 0);
	uint32 startIdx = cpuCh.FindEvent(cursor, rangeStart);
	uint32 endIdx = cpuCh.FindEvent(cursor, rangeEnd);
	if (endIdx <= startIdx)
		endIdx = cpuCh.GetEventCount();

	// Open a single frame for the whole range
	const ATCPUHistoryEntry *firstHents[1];
	cpuCh.ReadHistoryEvents(cursor, firstHents, startIdx, 1);
	builder.OpenFrame(firstHents[0]->mCycle, firstHents[0]->mCycle, tsDecoder);

	const ATCPUHistoryEntry *hents[257];
	uint32 pos = startIdx;
	while (pos < endIdx) {
		// Read n+1 entries (Update needs overlap of 1)
		uint32 toRead = std::min<uint32>(endIdx - pos, 256);
		uint32 n = cpuCh.ReadHistoryEvents(cursor, hents, pos, toRead + 1);
		if (n <= 1)
			break;

		builder.Update(tsDecoder, hents, n - 1, false);
		pos += n - 1;
	}

	// Read one more for the final timestamp
	const ATCPUHistoryEntry *lastHents[1];
	if (cpuCh.ReadHistoryEvents(cursor, lastHents, endIdx > 0 ? endIdx - 1 : 0, 1) > 0)
		builder.CloseFrame(lastHents[0]->mCycle, lastHents[0]->mCycle, true);
	else
		builder.CloseFrame(0, 0, true);

	builder.Finalize();
	builder.TakeSession(s_profState.mSession);

	// Merge all frames
	if (!s_profState.mSession.mpFrames.empty()) {
		ATProfileMergedFrame *mergedRaw = nullptr;
		ATProfileMergeFrames(s_profState.mSession, 0, (uint32)s_profState.mSession.mpFrames.size(), &mergedRaw);
		s_profState.mpMergedFrame = mergedRaw;

		if (mergedRaw) {
			// Build sorted record list
			for (const auto& rec : mergedRaw->mRecords) {
				ProfileState::SortedRecord sr;
				sr.mAddress = rec.mAddress;
				sr.mCalls = rec.mCalls;
				sr.mInsns = rec.mInsns;
				sr.mCycles = rec.mCycles;
				sr.mUnhaltedCycles = rec.mUnhaltedCycles;
				s_profState.mSortedRecords.push_back(sr);
			}

			// Default sort by cycles descending
			std::sort(s_profState.mSortedRecords.begin(), s_profState.mSortedRecords.end(),
				[](const ProfileState::SortedRecord& a, const ProfileState::SortedRecord& b) {
					return a.mCycles > b.mCycles;
				});

			s_profState.mbValid = true;
		}
	}

	s_profState.mbNeedsRefresh = false;
}

static void RenderCPUProfile(ATImGuiTraceViewerContext& ctx) {
	if (!ctx.mpCPUHistoryChannel) {
		ImGui::TextUnformatted("No CPU history in this trace.");
		return;
	}

	// Mode selector
	const char *modeLabels[] = { "Instructions", "Functions", "Call Graph", "Basic Blocks" };
	int modeIdx = (int)s_profState.mMode;
	if (ImGui::Combo("Mode", &modeIdx, modeLabels, 4)) {
		s_profState.mMode = (ATProfileMode)modeIdx;
		s_profState.mbNeedsRefresh = true;
	}

	ImGui::SameLine();
	if (ImGui::Button("Refresh"))
		s_profState.mbNeedsRefresh = true;

	if (ctx.mbSelectionValid) {
		ImGui::SameLine();
		ImGui::Text("Range: %.6fs - %.6fs",
			std::min(ctx.mSelectStart, ctx.mSelectEnd),
			std::max(ctx.mSelectStart, ctx.mSelectEnd));
	}

	// Check if selection changed
	if (ctx.mbSelectionValid &&
		(ctx.mSelectStart != s_profState.mLastSelectStart || ctx.mSelectEnd != s_profState.mLastSelectEnd)) {
		s_profState.mbNeedsRefresh = true;
		s_profState.mLastSelectStart = ctx.mSelectStart;
		s_profState.mLastSelectEnd = ctx.mSelectEnd;
	}

	if (s_profState.mbNeedsRefresh)
		BuildProfile(ctx);

	if (!s_profState.mbValid) {
		ImGui::TextUnformatted("No profile data. Click Refresh to build.");
		return;
	}

	// Total stats
	if (s_profState.mpMergedFrame) {
		ImGui::Text("Total: %u cycles, %u insns",
			s_profState.mpMergedFrame->mTotalCycles,
			s_profState.mpMergedFrame->mTotalInsns);
	}

	ImGuiTableFlags tableFlags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg
		| ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable
		| ImGuiTableFlags_Sortable | ImGuiTableFlags_SizingFixedFit;

	if (ImGui::BeginTable("##CPUProfile", 5, tableFlags)) {
		ImGui::TableSetupScrollFreeze(0, 1);
		ImGui::TableSetupColumn("Address", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_DefaultSort, 120);
		ImGui::TableSetupColumn("Calls", ImGuiTableColumnFlags_WidthFixed, 60);
		ImGui::TableSetupColumn("Insns", ImGuiTableColumnFlags_WidthFixed, 70);
		ImGui::TableSetupColumn("Cycles", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_PreferSortDescending, 80);
		ImGui::TableSetupColumn("%", ImGuiTableColumnFlags_WidthFixed, 60);
		ImGui::TableHeadersRow();

		// Handle sorting
		ImGuiTableSortSpecs *sortSpecs = ImGui::TableGetSortSpecs();
		if (sortSpecs && sortSpecs->SpecsDirty && sortSpecs->SpecsCount > 0) {
			const ImGuiTableColumnSortSpecs& spec = sortSpecs->Specs[0];
			bool ascending = (spec.SortDirection == ImGuiSortDirection_Ascending);

			std::sort(s_profState.mSortedRecords.begin(), s_profState.mSortedRecords.end(),
				[&spec, ascending](const ProfileState::SortedRecord& a, const ProfileState::SortedRecord& b) {
					int cmp = 0;
					switch (spec.ColumnIndex) {
						case 0: cmp = (a.mAddress < b.mAddress) ? -1 : (a.mAddress > b.mAddress) ? 1 : 0; break;
						case 1: cmp = (a.mCalls < b.mCalls) ? -1 : (a.mCalls > b.mCalls) ? 1 : 0; break;
						case 2: cmp = (a.mInsns < b.mInsns) ? -1 : (a.mInsns > b.mInsns) ? 1 : 0; break;
						case 3: cmp = (a.mCycles < b.mCycles) ? -1 : (a.mCycles > b.mCycles) ? 1 : 0; break;
						case 4: cmp = (a.mCycles < b.mCycles) ? -1 : (a.mCycles > b.mCycles) ? 1 : 0; break;
						default: break;
					}
					return ascending ? cmp < 0 : cmp > 0;
				});

			sortSpecs->SpecsDirty = false;
		}

		uint32 totalCycles = s_profState.mpMergedFrame ? s_profState.mpMergedFrame->mTotalCycles : 1;

		ImGuiListClipper clipper;
		clipper.Begin((int)s_profState.mSortedRecords.size());
		while (clipper.Step()) {
			for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
				const auto& rec = s_profState.mSortedRecords[row];

				ImGui::TableNextRow();

				// Address with symbol
				ImGui::TableSetColumnIndex(0);
				{
					IATDebugger *dbg = ATGetDebugger();
					if (dbg) {
						VDStringA addrText = dbg->GetAddressText(rec.mAddress, false, true);
						ImGui::TextUnformatted(addrText.c_str());
					} else {
						ImGui::Text("$%04X", rec.mAddress);
					}
				}

				ImGui::TableSetColumnIndex(1);
				ImGui::Text("%u", rec.mCalls);
				ImGui::TableSetColumnIndex(2);
				ImGui::Text("%u", rec.mInsns);
				ImGui::TableSetColumnIndex(3);
				ImGui::Text("%u", rec.mCycles);
				ImGui::TableSetColumnIndex(4);
				ImGui::Text("%.1f%%", (double)rec.mCycles * 100.0 / (double)totalCycles);
			}
		}

		ImGui::EndTable();
	}
}

// =========================================================================
// Log tab
// =========================================================================

static void RebuildLogEntries(ATImGuiTraceViewerContext& ctx) {
	s_logState.mEntries.clear();
	s_logState.mbValid = false;
	s_logState.mpLastChannel = ctx.mpLogChannel;

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

static void RenderLog(ATImGuiTraceViewerContext& ctx) {
	if (!ctx.mpLogChannel) {
		ImGui::TextUnformatted("No log channel in this trace.");
		return;
	}

	// Rebuild if channel changed
	if (ctx.mpLogChannel != s_logState.mpLastChannel)
		RebuildLogEntries(ctx);

	// Timestamp mode selector
	const char *tsModes[] = { "None", "Beam Position", "Cycle", "Microseconds" };
	ImGui::Combo("Timestamp", &s_logState.mTimestampMode, tsModes, 4);

	ImGui::SameLine();
	if (ImGui::Button("Copy All")) {
		VDStringA all;
		for (const auto& entry : s_logState.mEntries) {
			if (s_logState.mTimestampMode == 2)
				all.append_sprintf("%.6f  ", entry.mTime);
			else if (s_logState.mTimestampMode == 3)
				all.append_sprintf("%.2f us  ", entry.mTime * 1000000.0);
			all += entry.mName;
			all += '\n';
		}
		SDL_SetClipboardText(all.c_str());
	}

	if (!s_logState.mbValid) {
		RebuildLogEntries(ctx);
		if (!s_logState.mbValid)
			return;
	}

	ImGuiTableFlags tableFlags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg
		| ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable;

	if (ImGui::BeginTable("##LogTable", 2, tableFlags)) {
		ImGui::TableSetupScrollFreeze(0, 1);
		ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, 120);
		ImGui::TableSetupColumn("Event", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableHeadersRow();

		ImGuiListClipper clipper;
		clipper.Begin((int)s_logState.mEntries.size());
		while (clipper.Step()) {
			for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
				const auto& entry = s_logState.mEntries[row];

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);

				switch (s_logState.mTimestampMode) {
					case 0:		// None
						break;
					case 1: {	// Beam position
						uint32 cycle = (uint32)(entry.mTime / ctx.mpCPUHistoryChannel->GetSecondsPerTick());
						ATCPUBeamPosition bp = ctx.mTimestampDecoder.GetBeamPosition(cycle);
						ImGui::Text("%u:%u", bp.mFrame, bp.mY * 114 + bp.mX);
						break;
					}
					case 2:		// Cycle
						ImGui::Text("%.6f", entry.mTime);
						break;
					case 3:		// Microseconds
						ImGui::Text("%.2f us", entry.mTime * 1000000.0);
						break;
				}

				ImGui::TableSetColumnIndex(1);
				ImGui::TextUnformatted(entry.mName.c_str());

				// Double-click to navigate
				if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
					ctx.mFocusTime = entry.mTime;
					ctx.mbFocusTimeChanged = true;
				}
			}
		}

		ImGui::EndTable();
	}
}

// =========================================================================
// Main panels renderer
// =========================================================================

void ATImGuiTraceViewer_RenderPanels(ATImGuiTraceViewerContext& ctx) {
	if (ImGui::BeginTabBar("##TraceViewerTabs")) {
		if (ImGui::BeginTabItem("CPU History")) {
			RenderCPUHistory(ctx);
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("CPU Profile")) {
			RenderCPUProfile(ctx);
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Log")) {
			RenderLog(ctx);
			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}
}
