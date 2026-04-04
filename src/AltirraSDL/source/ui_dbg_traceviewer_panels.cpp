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
// Enable CPU History callback
// =========================================================================

static ATImGuiTraceViewerEnableCPUHistoryFn s_enableCPUHistoryFn = nullptr;

void ATImGuiTraceViewer_SetEnableCPUHistoryCallback(ATImGuiTraceViewerEnableCPUHistoryFn fn) {
	s_enableCPUHistoryFn = fn;
}

// =========================================================================
// CPU History tab state
// =========================================================================

namespace {

struct HistoryState {
	double mLastFocusTime = -1;
	vdfastvector<ATCPUHistoryEntry> mEntries;
	uint32 mFocusEntryIndex = 0;
	uint32 mBaseIndex = 0; // absolute index of mEntries[0] in the channel
	bool mbValid = false;
	bool mbScrollToFocus = false;
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
	uint32 mLastCollectionGen = 0;

	// Boundary rule state (frame trigger)
	ATProfileBoundaryRule mBoundaryRule = kATProfileBoundaryRule_None;
	char mBoundaryAddrExpr[256] = {};
	char mBoundaryAddrExpr2[256] = {};
	bool mbEndFunction = false;
	bool mbShowBoundaryRuleDialog = false;

	struct SortedRecord {
		uint32 mAddress;
		uint32 mCalls;
		uint32 mInsns;
		uint32 mCycles;
		uint32 mUnhaltedCycles;
		uint32 mCounters[2];
	};
	vdfastvector<SortedRecord> mSortedRecords;

	// Counter modes captured at profile build time (so CSV/table match)
	ATProfileCounterMode mCapturedCounterModes[2] = { kATProfileCounterMode_None, kATProfileCounterMode_None };

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
	double mTimestampOrigin = 0;	// offset for relative timestamps
	int mSelectedRow = -1;
	bool mbScrollToSelected = false;
};

static LogState s_logState;

} // anonymous namespace

// =========================================================================
// CPU History tab
// =========================================================================

static void RenderCPUHistory(ATImGuiTraceViewerContext& ctx) {
	if (!ctx.mpCPUHistoryChannel) {
		ImGui::TextUnformatted("No CPU history in this trace.");
		ImGui::TextUnformatted("Enable 'CPU Instruction History' in Trace settings and re-record.");
		if (!ctx.mSettings.mbTraceCpuInsns) {
			if (ImGui::Button("Enable CPU Instruction History and Start Trace")) {
				ctx.mSettings.mbTraceCpuInsns = true;
				if (s_enableCPUHistoryFn)
					s_enableCPUHistoryFn();
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
	builder.Init(s_profState.mMode, ctx.mProfileCounterModes[0], ctx.mProfileCounterModes[1]);
	builder.SetGlobalAddressesEnabled(ctx.mbGlobalAddressesEnabled);

	uint32 baseCycle = cpuCh.GetHistoryBaseCycle();
	const ATCPUTimestampDecoder& tsDecoder = cpuCh.GetTimestampDecoder();

	// Determine range
	double rangeStart = 0;
	double rangeEnd = cpuCh.GetDuration();
	if (ctx.mbSelectionValid) {
		rangeStart = std::min(ctx.mSelectStart, ctx.mSelectEnd);
		rangeEnd = std::max(ctx.mSelectStart, ctx.mSelectEnd);
	}

	// Use a zero-based cursor since FindEvent returns absolute positions
	// and ReadHistoryEvents adds cursor.mIterPos to the offset
	auto cursor = cpuCh.StartHistoryIteration(0, 0);
	uint32 startIdx = cpuCh.FindEvent(cursor, rangeStart);
	uint32 endIdx = cpuCh.FindEvent(cursor, rangeEnd);
	if (endIdx <= startIdx)
		endIdx = cpuCh.GetEventCount();

	// Open a single frame for the whole range
	const ATCPUHistoryEntry *firstHents[1];
	if (cpuCh.ReadHistoryEvents(cursor, firstHents, startIdx, 1) == 0)
		return;
	builder.SetS(firstHents[0]->mS);
	builder.OpenFrame(firstHents[0]->mCycle, firstHents[0]->mCycle, tsDecoder);

	const ATCPUHistoryEntry *hents[257];
	uint32 pos = startIdx;
	while (pos < endIdx) {
		// Read n+1 entries (Update needs overlap of 1)
		uint32 toRead = std::min<uint32>(endIdx - pos, 256);
		uint32 n = cpuCh.ReadHistoryEvents(cursor, hents, pos, toRead + 1);
		if (n <= 1)
			break;

		builder.Update(tsDecoder, hents, n - 1, ctx.mbGlobalAddressesEnabled);
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
			// Capture counter modes at build time
			s_profState.mCapturedCounterModes[0] = s_profState.mSession.mCounterModes.size() > 0 ? s_profState.mSession.mCounterModes[0] : kATProfileCounterMode_None;
			s_profState.mCapturedCounterModes[1] = s_profState.mSession.mCounterModes.size() > 1 ? s_profState.mSession.mCounterModes[1] : kATProfileCounterMode_None;

			// Build sorted record list
			for (const auto& rec : mergedRaw->mRecords) {
				ProfileState::SortedRecord sr;
				sr.mAddress = rec.mAddress;
				sr.mCalls = rec.mCalls;
				sr.mInsns = rec.mInsns;
				sr.mCycles = rec.mCycles;
				sr.mUnhaltedCycles = rec.mUnhaltedCycles;
				sr.mCounters[0] = rec.mCounters[0];
				sr.mCounters[1] = rec.mCounters[1];
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

static void CopyProfileAsCsv() {
	if (s_profState.mSortedRecords.empty())
		return;

	static const char *kCounterModeNames[] = { "Taken", "NotTaken", "PageCross", "Redundant" };

	VDStringA csv;

	const auto appendField = [&](const char *s) {
		if (strchr(s, ' ') || strchr(s, ',') || strchr(s, '"')) {
			csv += '"';
			for (const char *p = s; *p; ++p) {
				if (*p == '"')
					csv += '"';
				csv += *p;
			}
			csv += '"';
		} else {
			csv += s;
		}
		csv += ',';
	};

	const auto endLine = [&] {
		if (!csv.empty() && csv.back() == ',')
			csv.pop_back();
		csv += "\r\n";
	};

	// Header — matches table columns
	appendField("Address");
	appendField("Calls");
	appendField("Clocks");
	appendField("Insns");
	appendField("Clocks%");
	appendField("Insns%");
	appendField("CPUClocks");
	appendField("CPUClocks%");
	appendField("DMA%");
	for (int i = 0; i < 2; ++i) {
		ATProfileCounterMode cm = s_profState.mCapturedCounterModes[i];
		if (cm != kATProfileCounterMode_None) {
			appendField(kCounterModeNames[cm - 1]);
			char buf[64];
			snprintf(buf, sizeof(buf), "%s%%", kCounterModeNames[cm - 1]);
			appendField(buf);
		}
	}
	endLine();

	uint32 totalCycles = s_profState.mpMergedFrame ? s_profState.mpMergedFrame->mTotalCycles : 1;
	uint32 totalInsns = s_profState.mpMergedFrame ? s_profState.mpMergedFrame->mTotalInsns : 1;
	uint32 totalUnhaltedCycles = s_profState.mpMergedFrame ? s_profState.mpMergedFrame->mTotalUnhaltedCycles : 1;

	for (const auto& rec : s_profState.mSortedRecords) {
		// Address with symbol
		IATDebugger *dbg = ATGetDebugger();
		if (dbg) {
			VDStringA addrText = dbg->GetAddressText(rec.mAddress, false, true);
			appendField(addrText.c_str());
		} else {
			char buf[16];
			snprintf(buf, sizeof(buf), "$%04X", rec.mAddress);
			appendField(buf);
		}

		char buf[128];

		// Calls
		snprintf(buf, sizeof(buf), "%u", rec.mCalls);
		appendField(buf);

		// Clocks
		snprintf(buf, sizeof(buf), "%u", rec.mCycles);
		appendField(buf);

		// Insns
		snprintf(buf, sizeof(buf), "%u", rec.mInsns);
		appendField(buf);

		// Clocks%
		snprintf(buf, sizeof(buf), "%.2f%%", (float)rec.mCycles / (float)totalCycles * 100.0f);
		appendField(buf);

		// Insns%
		snprintf(buf, sizeof(buf), "%.2f%%", (float)rec.mInsns / (float)totalInsns * 100.0f);
		appendField(buf);

		// CPUClocks
		snprintf(buf, sizeof(buf), "%u", rec.mUnhaltedCycles);
		appendField(buf);

		// CPUClocks%
		snprintf(buf, sizeof(buf), "%.2f%%", (float)rec.mUnhaltedCycles / (float)totalUnhaltedCycles * 100.0f);
		appendField(buf);

		// DMA%
		if (rec.mCycles)
			snprintf(buf, sizeof(buf), "%.2f%%", 100.0f * (1.0f - (float)rec.mUnhaltedCycles / (float)rec.mCycles));
		else
			buf[0] = 0;
		appendField(buf);

		// Counter columns
		for (int i = 0; i < 2; ++i) {
			if (s_profState.mCapturedCounterModes[i] != kATProfileCounterMode_None) {
				snprintf(buf, sizeof(buf), "%u", rec.mCounters[i]);
				appendField(buf);
				if (rec.mInsns)
					snprintf(buf, sizeof(buf), "%.2f%%", (float)rec.mCounters[i] / (float)rec.mInsns * 100.0f);
				else
					buf[0] = 0;
				appendField(buf);
			}
		}

		endLine();
	}

	SDL_SetClipboardText(csv.c_str());
}

static void RenderBoundaryRuleDialog() {
	// Temporary edit buffers — populated from s_profState on open, committed
	// back only on OK.  Cancel discards edits (matches Windows behaviour
	// where the dialog has its own copy of the expressions).
	static char sEditAddr1[256];
	static char sEditAddr2[256];
	static bool sEditEndFunction;
	static bool sWasOpen;
	static VDStringA sValidationError;

	if (!s_profState.mbShowBoundaryRuleDialog) {
		sWasOpen = false;
		return;
	}

	// First frame the dialog is shown — snapshot current values
	if (!sWasOpen) {
		memcpy(sEditAddr1, s_profState.mBoundaryAddrExpr, sizeof(sEditAddr1));
		memcpy(sEditAddr2, s_profState.mBoundaryAddrExpr2, sizeof(sEditAddr2));
		sEditEndFunction = s_profState.mbEndFunction;
		sValidationError.clear();
		sWasOpen = true;
	}

	ImGui::SetNextWindowSizeConstraints(ImVec2(420, 0), ImVec2(420, FLT_MAX));
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
		ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

	if (ImGui::Begin("Trigger On PC Address", &s_profState.mbShowBoundaryRuleDialog,
			ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking
			| ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize)) {

		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted("Start frame address");
		ImGui::SameLine(160.0f);
		ImGui::SetNextItemWidth(-1);
		ImGui::InputText("##addr1", sEditAddr1, sizeof(sEditAddr1));

		ImGui::Spacing();
		ImGui::SetCursorPosX(160.0f);
		ImGui::Checkbox("End frame when function returns", &sEditEndFunction);

		ImGui::Spacing();
		ImGui::AlignTextToFramePadding();
		if (sEditEndFunction)
			ImGui::BeginDisabled();
		ImGui::TextUnformatted("End frame address (optional)");
		ImGui::SameLine(160.0f);
		ImGui::SetNextItemWidth(-1);
		ImGui::InputText("##addr2", sEditAddr2, sizeof(sEditAddr2));
		if (sEditEndFunction)
			ImGui::EndDisabled();

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		// Show validation error above buttons (if any)
		if (!sValidationError.empty()) {
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
			ImGui::TextWrapped("%s", sValidationError.c_str());
			ImGui::PopStyleColor();
			ImGui::Spacing();
		}

		float buttonWidth = 80.0f;
		float spacing = ImGui::GetStyle().ItemSpacing.x;
		ImGui::SetCursorPosX(ImGui::GetContentRegionAvail().x - buttonWidth * 2 - spacing + ImGui::GetStyle().WindowPadding.x);

		if (ImGui::Button("OK", ImVec2(buttonWidth, 0))) {
			IATDebugger *dbg = ATGetDebugger();
			sValidationError.clear();
			bool valid = true;

			// Validate start address (required)
			if (!sEditAddr1[0]) {
				sValidationError = "Start frame address is required.";
				valid = false;
			} else if (dbg) {
				try {
					dbg->EvaluateThrow(sEditAddr1);
				} catch (const MyError& e) {
					sValidationError.sprintf("Start address: %s", e.c_str());
					valid = false;
				}
			}

			// Validate end address if applicable
			if (valid && !sEditEndFunction && sEditAddr2[0] && dbg) {
				try {
					dbg->EvaluateThrow(sEditAddr2);
				} catch (const MyError& e) {
					sValidationError.sprintf("End address: %s", e.c_str());
					valid = false;
				}
			}

			if (valid) {
				// Commit edits back to profile state
				memcpy(s_profState.mBoundaryAddrExpr, sEditAddr1, sizeof(s_profState.mBoundaryAddrExpr));
				if (sEditEndFunction) {
					s_profState.mBoundaryRule = kATProfileBoundaryRule_PCAddressFunction;
					s_profState.mBoundaryAddrExpr2[0] = 0;
				} else {
					memcpy(s_profState.mBoundaryAddrExpr2, sEditAddr2, sizeof(s_profState.mBoundaryAddrExpr2));
					s_profState.mBoundaryRule = kATProfileBoundaryRule_PCAddress;
				}
				s_profState.mbEndFunction = sEditEndFunction;
				s_profState.mbShowBoundaryRuleDialog = false;
			}
		}

		ImGui::SameLine();
		if (ImGui::Button("Cancel", ImVec2(buttonWidth, 0))) {
			s_profState.mbShowBoundaryRuleDialog = false;
		}
	}
	ImGui::End();
}

static void RenderCPUProfile(ATImGuiTraceViewerContext& ctx) {
	if (!ctx.mpCPUHistoryChannel) {
		ImGui::TextUnformatted("No CPU history in this trace.");
		ImGui::TextUnformatted("Enable 'CPU Instruction History' in Trace settings and re-record.");
		if (!ctx.mSettings.mbTraceCpuInsns) {
			if (ImGui::Button("Enable CPU Instruction History and Start Trace")) {
				ctx.mSettings.mbTraceCpuInsns = true;
				if (s_enableCPUHistoryFn)
					s_enableCPUHistoryFn();
			}
		} else {
			ImGui::TextDisabled("(CPU Instruction History is now enabled. Start a new trace to capture data.)");
		}
		return;
	}

	// Mode selector
	const char *modeLabels[] = { "Instructions", "Functions", "Call Graph", "Basic Blocks", "Basic Lines" };
	int modeIdx = (int)s_profState.mMode;
	if (ImGui::Combo("Mode", &modeIdx, modeLabels, 5)) {
		s_profState.mMode = (ATProfileMode)modeIdx;
		s_profState.mbNeedsRefresh = true;
	}

	ImGui::SameLine();
	if (ImGui::Button("Refresh"))
		s_profState.mbNeedsRefresh = true;

	ImGui::SameLine();
	if (ImGui::Button("Options"))
		ImGui::OpenPopup("ProfileOptions");

	if (ImGui::BeginPopup("ProfileOptions")) {
		static const char *kCounterNames[] = { "Branch Taken", "Branch Not Taken", "Page Crossing", "Redundant Op" };
		uint32 activeMask = 0;
		for (auto cm : ctx.mProfileCounterModes)
			if (cm) activeMask |= (1 << (cm - 1));
		bool allSlotsFull = (ctx.mProfileCounterModes[0] != kATProfileCounterMode_None && ctx.mProfileCounterModes[1] != kATProfileCounterMode_None);

		for (int i = 0; i < 4; ++i) {
			ATProfileCounterMode mode = (ATProfileCounterMode)(i + 1);
			bool active = (activeMask & (1 << i)) != 0;
			bool enabled = active || !allSlotsFull;
			if (ImGui::MenuItem(kCounterNames[i], nullptr, active, enabled)) {
				if (active) {
					// Remove it
					for (auto& cm : ctx.mProfileCounterModes)
						if (cm == mode) { cm = kATProfileCounterMode_None; break; }
				} else {
					// Add to last slot (Windows uses last slot for new entries)
					ctx.mProfileCounterModes[std::size(ctx.mProfileCounterModes) - 1] = mode;
				}

				// Sort non-None values and pack to front (matching Windows behaviour)
				std::sort(std::begin(ctx.mProfileCounterModes), std::end(ctx.mProfileCounterModes));
				ATProfileCounterMode packed[2] = { kATProfileCounterMode_None, kATProfileCounterMode_None };
				int packIdx = 0;
				for (auto cm : ctx.mProfileCounterModes)
					if (cm != kATProfileCounterMode_None)
						packed[packIdx++] = cm;
				ctx.mProfileCounterModes[0] = packed[0];
				ctx.mProfileCounterModes[1] = packed[1];

				s_profState.mbNeedsRefresh = true;
			}
		}

		ImGui::Separator();

		if (ImGui::BeginMenu("Frame Trigger")) {
			if (ImGui::MenuItem("None", nullptr, s_profState.mBoundaryRule == kATProfileBoundaryRule_None))
				s_profState.mBoundaryRule = kATProfileBoundaryRule_None;
			if (ImGui::MenuItem("Vertical Blank", nullptr, s_profState.mBoundaryRule == kATProfileBoundaryRule_VBlank))
				s_profState.mBoundaryRule = kATProfileBoundaryRule_VBlank;
			if (ImGui::MenuItem("PC Address...", nullptr,
					s_profState.mBoundaryRule == kATProfileBoundaryRule_PCAddress
					|| s_profState.mBoundaryRule == kATProfileBoundaryRule_PCAddressFunction)) {
				s_profState.mbShowBoundaryRuleDialog = true;
			}
			ImGui::EndMenu();
		}

		ImGui::Separator();
		if (ImGui::MenuItem("Enable Global Addresses", nullptr, ctx.mbGlobalAddressesEnabled)) {
			ctx.mbGlobalAddressesEnabled = !ctx.mbGlobalAddressesEnabled;
			s_profState.mbNeedsRefresh = true;
		}
		ImGui::EndPopup();
	}

	// Render boundary rule dialog (outside popup)
	RenderBoundaryRuleDialog();

	if (ctx.mbSelectionValid) {
		ImGui::SameLine();
		ImGui::Text("Range: %.6fs - %.6fs",
			std::min(ctx.mSelectStart, ctx.mSelectEnd),
			std::max(ctx.mSelectStart, ctx.mSelectEnd));
	}

	// Detect collection change
	if (ctx.mCollectionGeneration != s_profState.mLastCollectionGen) {
		s_profState.mLastCollectionGen = ctx.mCollectionGeneration;
		s_profState.mbNeedsRefresh = true;
		s_profState.mbValid = false;
	}

	// Check if selection changed or was cleared
	if (ctx.mbSelectionValid) {
		if (ctx.mSelectStart != s_profState.mLastSelectStart || ctx.mSelectEnd != s_profState.mLastSelectEnd) {
			s_profState.mbNeedsRefresh = true;
			s_profState.mLastSelectStart = ctx.mSelectStart;
			s_profState.mLastSelectEnd = ctx.mSelectEnd;
		}
	} else if (s_profState.mLastSelectStart >= 0) {
		// Selection was cleared — re-profile full range
		s_profState.mbNeedsRefresh = true;
		s_profState.mLastSelectStart = -1;
		s_profState.mLastSelectEnd = -1;
	}

	if (s_profState.mbNeedsRefresh && ctx.mpCPUHistoryChannel && ctx.mpCPUHistoryChannel->GetEventCount() > 0)
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

	// Determine column count (base columns + dynamic counter columns)
	// Base columns: Address, Calls, Clocks, Insns, Clocks%, Insns%, CPUClocks, CPUClocks%, DMA%
	static const int kBaseColumnCount = 9;
	static const char *kCounterModeNames[] = { "Taken", "NotTaken", "PageCross", "Redundant" };

	int numCounterCols = 0;
	for (int i = 0; i < 2; ++i) {
		if (s_profState.mCapturedCounterModes[i] != kATProfileCounterMode_None)
			numCounterCols += 2; // value + percentage
	}
	int totalColumns = kBaseColumnCount + numCounterCols;

	ImGuiTableFlags tableFlags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg
		| ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable
		| ImGuiTableFlags_Sortable | ImGuiTableFlags_SizingFixedFit;

	if (ImGui::BeginTable("##CPUProfile", totalColumns, tableFlags)) {
		ImGui::TableSetupScrollFreeze(0, 1);
		ImGui::TableSetupColumn("Address", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_DefaultSort, 120);
		ImGui::TableSetupColumn("Calls", ImGuiTableColumnFlags_WidthFixed, 60);
		ImGui::TableSetupColumn("Clocks", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_PreferSortDescending, 80);
		ImGui::TableSetupColumn("Insns", ImGuiTableColumnFlags_WidthFixed, 70);
		ImGui::TableSetupColumn("Clocks%", ImGuiTableColumnFlags_WidthFixed, 60);
		ImGui::TableSetupColumn("Insns%", ImGuiTableColumnFlags_WidthFixed, 60);
		ImGui::TableSetupColumn("CPUClocks", ImGuiTableColumnFlags_WidthFixed, 80);
		ImGui::TableSetupColumn("CPUClocks%", ImGuiTableColumnFlags_WidthFixed, 70);
		ImGui::TableSetupColumn("DMA%", ImGuiTableColumnFlags_WidthFixed, 50);

		// Dynamic counter columns
		for (int i = 0; i < 2; ++i) {
			ATProfileCounterMode cm = s_profState.mCapturedCounterModes[i];
			if (cm != kATProfileCounterMode_None) {
				const char *name = kCounterModeNames[cm - 1];
				char buf[64];
				snprintf(buf, sizeof(buf), "%s", name);
				ImGui::TableSetupColumn(buf, ImGuiTableColumnFlags_WidthFixed, 70);
				snprintf(buf, sizeof(buf), "%s%%", name);
				ImGui::TableSetupColumn(buf, ImGuiTableColumnFlags_WidthFixed, 60);
			}
		}
		ImGui::TableHeadersRow();

		// Handle sorting
		ImGuiTableSortSpecs *sortSpecs = ImGui::TableGetSortSpecs();
		if (sortSpecs && sortSpecs->SpecsDirty && sortSpecs->SpecsCount > 0) {
			const ImGuiTableColumnSortSpecs& spec = sortSpecs->Specs[0];
			bool ascending = (spec.SortDirection == ImGuiSortDirection_Ascending);

			// Map column indices to sort keys
			// 0=Address, 1=Calls, 2=Clocks, 3=Insns, 4=Clocks%, 5=Insns%,
			// 6=CPUClocks, 7=CPUClocks%, 8=DMA%, 9+=counters
			const int sortCol = spec.ColumnIndex;

			std::sort(s_profState.mSortedRecords.begin(), s_profState.mSortedRecords.end(),
				[sortCol, ascending](const ProfileState::SortedRecord& a, const ProfileState::SortedRecord& b) {
					int cmp = 0;
					switch (sortCol) {
						case 0: cmp = (a.mAddress < b.mAddress) ? -1 : (a.mAddress > b.mAddress) ? 1 : 0; break;
						case 1: cmp = (a.mCalls < b.mCalls) ? -1 : (a.mCalls > b.mCalls) ? 1 : 0; break;
						case 2: // Clocks (sort by cycles)
						case 4: // Clocks% (same ordering as cycles)
							cmp = (a.mCycles < b.mCycles) ? -1 : (a.mCycles > b.mCycles) ? 1 : 0; break;
						case 3: // Insns
						case 5: // Insns% (same ordering as insns)
							cmp = (a.mInsns < b.mInsns) ? -1 : (a.mInsns > b.mInsns) ? 1 : 0; break;
						case 6: // CPUClocks
						case 7: // CPUClocks%
							cmp = (a.mUnhaltedCycles < b.mUnhaltedCycles) ? -1 : (a.mUnhaltedCycles > b.mUnhaltedCycles) ? 1 : 0; break;
						case 8: // DMA% — sort by (cycles - unhaltedCycles) / cycles
							{
								uint32 dmaA = a.mCycles > a.mUnhaltedCycles ? a.mCycles - a.mUnhaltedCycles : 0;
								uint32 dmaB = b.mCycles > b.mUnhaltedCycles ? b.mCycles - b.mUnhaltedCycles : 0;
								// Cross-multiply to avoid division: dmaA/a.mCycles vs dmaB/b.mCycles
								uint64 lhs = (uint64)dmaA * b.mCycles;
								uint64 rhs = (uint64)dmaB * a.mCycles;
								cmp = (lhs < rhs) ? -1 : (lhs > rhs) ? 1 : 0;
							}
							break;
						case 9:  // Counter0
						case 10: // Counter0%
							cmp = (a.mCounters[0] < b.mCounters[0]) ? -1 : (a.mCounters[0] > b.mCounters[0]) ? 1 : 0; break;
						case 11: // Counter1
						case 12: // Counter1%
							cmp = (a.mCounters[1] < b.mCounters[1]) ? -1 : (a.mCounters[1] > b.mCounters[1]) ? 1 : 0; break;
						default: break;
					}
					return ascending ? cmp < 0 : cmp > 0;
				});

			sortSpecs->SpecsDirty = false;
		}

		uint32 totalCycles = s_profState.mpMergedFrame ? s_profState.mpMergedFrame->mTotalCycles : 1;
		uint32 totalInsns = s_profState.mpMergedFrame ? s_profState.mpMergedFrame->mTotalInsns : 1;
		uint32 totalUnhaltedCycles = s_profState.mpMergedFrame ? s_profState.mpMergedFrame->mTotalUnhaltedCycles : 1;

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
				ImGui::Text("%u", rec.mCycles);
				ImGui::TableSetColumnIndex(3);
				ImGui::Text("%u", rec.mInsns);
				ImGui::TableSetColumnIndex(4);
				ImGui::Text("%.2f%%", (float)rec.mCycles / (float)totalCycles * 100.0f);
				ImGui::TableSetColumnIndex(5);
				ImGui::Text("%.2f%%", (float)rec.mInsns / (float)totalInsns * 100.0f);
				ImGui::TableSetColumnIndex(6);
				ImGui::Text("%u", rec.mUnhaltedCycles);
				ImGui::TableSetColumnIndex(7);
				ImGui::Text("%.2f%%", (float)rec.mUnhaltedCycles / (float)totalUnhaltedCycles * 100.0f);
				ImGui::TableSetColumnIndex(8);
				if (rec.mCycles)
					ImGui::Text("%.2f%%", 100.0f * (1.0f - (float)rec.mUnhaltedCycles / (float)rec.mCycles));

				// Dynamic counter columns
				int colIdx = kBaseColumnCount;
				for (int i = 0; i < 2; ++i) {
					if (s_profState.mCapturedCounterModes[i] != kATProfileCounterMode_None) {
						ImGui::TableSetColumnIndex(colIdx++);
						ImGui::Text("%u", rec.mCounters[i]);
						ImGui::TableSetColumnIndex(colIdx++);
						if (rec.mInsns)
							ImGui::Text("%.2f%%", (float)rec.mCounters[i] / (float)rec.mInsns * 100.0f);
					}
				}
			}
		}

		// Context menu — scoped to the table's scroll area (child window)
		if (ImGui::BeginPopupContextWindow("##ProfileContextMenu", ImGuiPopupFlags_MouseButtonRight)) {
			if (ImGui::MenuItem("Copy As CSV"))
				CopyProfileAsCsv();
			ImGui::EndPopup();
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

static void RenderLog(ATImGuiTraceViewerContext& ctx) {
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

// =========================================================================
// Reset static panel state (call when collection changes to avoid
// dangling pointers to freed channels/collections).
// =========================================================================

void ATImGuiTraceViewer_ResetPanelState() {
	s_histState.mEntries.clear();
	s_histState.mbValid = false;
	s_histState.mLastFocusTime = -1;

	s_profState.mbValid = false;
	s_profState.mbNeedsRefresh = true;
	s_profState.mSortedRecords.clear();
	s_profState.mpMergedFrame.clear();
	s_profState.mLastSelectStart = -1;
	s_profState.mLastSelectEnd = -1;
	s_profState.mCapturedCounterModes[0] = kATProfileCounterMode_None;
	s_profState.mCapturedCounterModes[1] = kATProfileCounterMode_None;

	s_logState.mbValid = false;
	s_logState.mEntries.clear();
	s_logState.mpLastChannel = nullptr;
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
