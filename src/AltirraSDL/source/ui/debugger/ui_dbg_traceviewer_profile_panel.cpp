//	AltirraSDL - Trace viewer: CPU Profile panel
//	Split out of ui_dbg_traceviewer_panels.cpp (Phase 2b).

#include <stdafx.h>
#include <algorithm>
#include <SDL3/SDL.h>
#include <imgui.h>
#include <vd2/system/vdtypes.h>
#include <vd2/system/VDString.h>
#include <vd2/system/unknown.h>
#include <at/atcpu/history.h>
#include "ui_dbg_traceviewer.h"
#include "traceviewer_panels_internal.h"
#include "trace.h"
#include "tracecpu.h"
#include "profiler.h"
#include "debugger.h"
#include "simulator.h"
#include "disasm.h"

extern ATSimulator g_sim;

namespace at_traceviewer_panels {

namespace {

struct ProfileState {
	ATProfileSession mSession;
	vdrefptr<ATProfileMergedFrame> mpMergedFrame;
	ATProfileMode mMode = kATProfileMode_Insns;
	bool mbNeedsRefresh = true;
	bool mbValid = false;
	double mLastSelectStart = -1;
	double mLastSelectEnd = -1;
	uint32 mLastCollectionGen = 0;
	const ATProfileFrame::Records *mpRecords = nullptr;

	struct SortedRecord {
		uint32 mAddress;
		uint32 mContext;
		uint32 mCalls;
		uint32 mInsns;
		uint32 mCycles;
		uint32 mUnhaltedCycles;
		uint32 mCounters[2];
	};
	vdfastvector<SortedRecord> mSortedRecords;

	// Counter modes captured at profile build time (so CSV/table match)
	ATProfileCounterMode mCapturedCounterModes[2] = { kATProfileCounterMode_None, kATProfileCounterMode_None };
};

ProfileState s_profState;

} // namespace

void ResetCPUProfileState() {
	s_profState.mbValid = false;
	s_profState.mbNeedsRefresh = true;
	s_profState.mSortedRecords.clear();
	s_profState.mpMergedFrame.clear();
	s_profState.mpRecords = nullptr;
	s_profState.mLastSelectStart = -1;
	s_profState.mLastSelectEnd = -1;
	s_profState.mCapturedCounterModes[0] = kATProfileCounterMode_None;
	s_profState.mCapturedCounterModes[1] = kATProfileCounterMode_None;
}

// --- body extracted from ui_dbg_traceviewer_panels.cpp ---------------------
// =========================================================================
// CPU Profile tab
// =========================================================================

static void BuildProfile(ATImGuiTraceViewerContext& ctx) {
	s_profState.mbValid = false;
	s_profState.mSortedRecords.clear();
	s_profState.mpMergedFrame.clear();
	s_profState.mpRecords = nullptr;

	if (!ctx.mpCPUHistoryChannel)
		return;

	ATTraceChannelCPUHistory& cpuCh = *ctx.mpCPUHistoryChannel;
	if (cpuCh.GetEventCount() == 0)
		return;

	try {
		ATCPUProfileBuilder builder;
		builder.Init(s_profState.mMode, ctx.mProfileCounterModes[0], ctx.mProfileCounterModes[1]);
		builder.SetGlobalAddressesEnabled(ctx.mbGlobalAddressesEnabled);

		const ATCPUTimestampDecoder& tsDecoder = cpuCh.GetTimestampDecoder();
		const bool useGlobalAddrs = cpuCh.GetDisasmMode() == kATDebugDisasmMode_6502;

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
		builder.OpenFrame(firstHents[0]->mCycle, firstHents[0]->mUnhaltedCycle, tsDecoder);

		const ATCPUHistoryEntry *hents[257];
		uint32 pos = startIdx;
		while (pos < endIdx) {
			// Read n+1 entries (Update needs overlap of 1)
			uint32 toRead = std::min<uint32>(endIdx - pos, 256);
			uint32 n = cpuCh.ReadHistoryEvents(cursor, hents, pos, toRead + 1);
			if (n <= 1)
				break;

			builder.Update(tsDecoder, hents, n - 1, useGlobalAddrs);
			pos += n - 1;
		}

		// Read one more for the final timestamp
		const ATCPUHistoryEntry *lastHents[1];
		if (cpuCh.ReadHistoryEvents(cursor, lastHents, endIdx > 0 ? endIdx - 1 : 0, 1) > 0)
			builder.CloseFrame(lastHents[0]->mCycle, lastHents[0]->mUnhaltedCycle, true);
		else
			builder.CloseFrame(0, 0, true);

		builder.Finalize();
		builder.TakeSession(s_profState.mSession);
	} catch (...) {
		// Protect against crashes in the profiler builder (malformed
		// history data, overflows, etc.)
		s_profState.mbNeedsRefresh = false;
		return;
	}

	// Merge all frames
	if (!s_profState.mSession.mpFrames.empty()) {
		ATProfileMergedFrame *mergedRaw = nullptr;
		ATProfileMergeFrames(s_profState.mSession, 0, (uint32)s_profState.mSession.mpFrames.size(), &mergedRaw);
		s_profState.mpMergedFrame = mergedRaw;

		if (mergedRaw) {
			// Capture counter modes at build time
			s_profState.mCapturedCounterModes[0] = s_profState.mSession.mCounterModes.size() > 0 ? s_profState.mSession.mCounterModes[0] : kATProfileCounterMode_None;
			s_profState.mCapturedCounterModes[1] = s_profState.mSession.mCounterModes.size() > 1 ? s_profState.mSession.mCounterModes[1] : kATProfileCounterMode_None;

			switch (s_profState.mSession.mProfileMode) {
				case kATProfileMode_Insns:
				case kATProfileMode_CallGraph:
					s_profState.mpRecords = &mergedRaw->mRecords;
					break;

				default:
					s_profState.mpRecords = &mergedRaw->mBlockRecords;
					break;
			}

			// Build sorted record list
			for (const auto& rec : *s_profState.mpRecords) {
				ProfileState::SortedRecord sr;
				sr.mAddress = rec.mAddress;
				sr.mContext = rec.mContext;
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

static const char *const kCounterModeNames[] = { "Taken", "NotTaken", "PageCross", "Redundant" };

static const char *GetCounterModeName(ATProfileCounterMode cm) {
	int idx = (int)cm - 1;
	if (idx >= 0 && idx < (int)(sizeof(kCounterModeNames) / sizeof(kCounterModeNames[0])))
		return kCounterModeNames[idx];
	return "?";
}

static const char *GetContextName(uint32 context) {
	switch (context) {
		case kATProfileContext_Main:		return "Main";
		case kATProfileContext_Interrupt:	return "Interrupt";
		case kATProfileContext_IRQ:			return "IRQ";
		case kATProfileContext_VBI:			return "VBI";
		case kATProfileContext_DLI:			return "DLI";
		default:							return "";
	}
}

static void CopyProfileAsCsv() {
	if (s_profState.mSortedRecords.empty())
		return;

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
	appendField("Thread");
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
			appendField(GetCounterModeName(cm));
			char buf[64];
			snprintf(buf, sizeof(buf), "%s%%", GetCounterModeName(cm));
			appendField(buf);
		}
	}
	endLine();

	uint32 totalCycles = s_profState.mpMergedFrame ? std::max(1u, s_profState.mpMergedFrame->mTotalCycles) : 1;
	uint32 totalInsns = s_profState.mpMergedFrame ? std::max(1u, s_profState.mpMergedFrame->mTotalInsns) : 1;
	uint32 totalUnhaltedCycles = s_profState.mpMergedFrame ? std::max(1u, s_profState.mpMergedFrame->mTotalUnhaltedCycles) : 1;

	for (const auto& rec : s_profState.mSortedRecords) {
		// Thread
		appendField(GetContextName(rec.mContext));

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

static void RenderCallGraphNode(uint32 idx,
	const vdfastvector<uint32>& firstChild,
	const vdfastvector<uint32>& nextSibling) {
	if (!s_profState.mpMergedFrame
		|| idx >= s_profState.mpMergedFrame->mCallGraphRecords.size()
		|| idx >= s_profState.mpMergedFrame->mInclusiveRecords.size())
		return;

	VDStringA label;
	const ATProfileCallGraphRecord& cgr = s_profState.mpMergedFrame->mCallGraphRecords[idx];

	switch (idx) {
		case 0:
			label = "Main";
			break;
		case 1:
			label = "IRQ";
			break;
		case 2:
			label = "NMI (VBI)";
			break;
		case 3:
			label = "NMI (DLI)";
			break;
		default:
			if (idx < s_profState.mSession.mContexts.size()) {
				const uint32 addr = s_profState.mSession.mContexts[idx].mAddress;
				if (IATDebugger *dbg = ATGetDebugger())
					label = dbg->GetAddressText(addr, false, true);
				else
					label.sprintf("$%04X", addr & 0xffff);
			} else {
				label.sprintf("#%u", idx);
			}

			label.append_sprintf(" [x%u]", cgr.mCalls);
			break;
	}

	const float cyclesToPercent = s_profState.mpMergedFrame->mTotalCycles
		? 100.0f / (float)s_profState.mpMergedFrame->mTotalCycles : 0.0f;
	const float unhaltedCyclesToPercent = s_profState.mpMergedFrame->mTotalUnhaltedCycles
		? 100.0f / (float)s_profState.mpMergedFrame->mTotalUnhaltedCycles : 0.0f;
	const float insnsToPercent = s_profState.mpMergedFrame->mTotalInsns
		? 100.0f / (float)s_profState.mpMergedFrame->mTotalInsns : 0.0f;
	const ATProfileCallGraphInclusiveRecord& cgir = s_profState.mpMergedFrame->mInclusiveRecords[idx];

	label.append_sprintf(": %u cycles (%.2f%%), %u CPU cycles (%.2f%%), %u insns (%.2f%%)",
		cgir.mInclusiveCycles,
		(float)cgir.mInclusiveCycles * cyclesToPercent,
		cgir.mInclusiveUnhaltedCycles,
		(float)cgir.mInclusiveUnhaltedCycles * unhaltedCyclesToPercent,
		cgir.mInclusiveInsns,
		(float)cgir.mInclusiveInsns * insnsToPercent);

	vdfastvector<uint32> children;
	for (uint32 child = firstChild[idx]; child; child = nextSibling[child])
		children.push_back(child);

	std::sort(children.begin(), children.end(),
		[records = s_profState.mpMergedFrame->mInclusiveRecords.data()](uint32 a, uint32 b) {
			return records[a].mInclusiveCycles > records[b].mInclusiveCycles;
		});

	ImGui::TableNextRow();
	ImGui::TableSetColumnIndex(0);

	ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow
		| ImGuiTreeNodeFlags_OpenOnDoubleClick
		| ImGuiTreeNodeFlags_SpanFullWidth;
	if (children.empty())
		flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

	const bool open = ImGui::TreeNodeEx((void *)(uintptr)idx, flags, "%s", label.c_str());

	if (open && !children.empty()) {
		for (uint32 child : children)
			RenderCallGraphNode(child, firstChild, nextSibling);

		ImGui::TreePop();
	}
}

static void RenderCallGraph() {
	if (!s_profState.mpMergedFrame || s_profState.mSession.mContexts.empty()
		|| s_profState.mpMergedFrame->mCallGraphRecords.empty())
		return;

	const uint32 n = (uint32)s_profState.mpMergedFrame->mCallGraphRecords.size();
	if (!n)
		return;

	vdfastvector<uint32> nextSibling(n, 0);
	vdfastvector<uint32> firstChild(n, 0);

	for (uint32 i = 4; i < n; ++i) {
		if (i >= s_profState.mpMergedFrame->mInclusiveRecords.size()
			|| !s_profState.mpMergedFrame->mInclusiveRecords[i].mInclusiveInsns
			|| i >= s_profState.mSession.mContexts.size())
			continue;

		const uint32 parent = s_profState.mSession.mContexts[i].mParent;
		if (parent >= n)
			continue;

		nextSibling[i] = firstChild[parent];
		firstChild[parent] = i;
	}

	const auto cgRecordSorter = [records = s_profState.mpMergedFrame->mInclusiveRecords.data()](uint32 a, uint32 b) {
		return records[a].mInclusiveCycles > records[b].mInclusiveCycles;
	};

	vdfastvector<uint32> roots;
	for (uint32 i = 0; i < std::min<uint32>(4, n); ++i)
		roots.push_back(i);

	std::sort(roots.begin(), roots.end(), cgRecordSorter);

	ImGuiTableFlags tableFlags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg
		| ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable
		| ImGuiTableFlags_SizingStretchProp;

	if (!ImGui::BeginTable("##CPUProfileCallGraph", 1, tableFlags))
		return;

	ImGui::TableSetupScrollFreeze(0, 1);
	ImGui::TableSetupColumn("Call Graph");
	ImGui::TableHeadersRow();

	for (uint32 root : roots)
		RenderCallGraphNode(root, firstChild, nextSibling);

	ImGui::EndTable();
}

void RenderCPUProfile(ATImGuiTraceViewerContext& ctx) {
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

	// Mode selector: matches native Performance Analyzer CPU Profile.
	static constexpr ATProfileMode kModeValues[] = {
		kATProfileMode_Insns,
		kATProfileMode_Functions,
		kATProfileMode_BasicBlock,
		kATProfileMode_CallGraph
	};
	static const char *const kModeLabels[] = {
		"Instructions",
		"Functions",
		"Basic Blocks",
		"Call Graph"
	};
	int modeIdx = 0;
	for (int i = 0; i < (int)std::size(kModeValues); ++i) {
		if (s_profState.mMode == kModeValues[i]) {
			modeIdx = i;
			break;
		}
	}
	if (ImGui::Combo("Mode", &modeIdx, kModeLabels, (int)std::size(kModeLabels))) {
		s_profState.mMode = kModeValues[modeIdx];
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
		if (ImGui::MenuItem("Enable Global Addresses", nullptr, ctx.mbGlobalAddressesEnabled)) {
			ctx.mbGlobalAddressesEnabled = !ctx.mbGlobalAddressesEnabled;
			s_profState.mbNeedsRefresh = true;
		}
		ImGui::EndPopup();
	}

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

	if (s_profState.mSession.mProfileMode == kATProfileMode_CallGraph) {
		RenderCallGraph();
		return;
	}

	// Determine column count (base columns + dynamic counter columns)
	// Base columns: Thread, Address, Calls, Clocks, Insns, Clocks%, Insns%, CPUClocks, CPUClocks%, DMA%
	static const int kBaseColumnCount = 10;

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
		ImGui::TableSetupColumn("Thread", ImGuiTableColumnFlags_WidthFixed, 65);
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
				const char *name = GetCounterModeName(cm);
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
			// 0=Thread, 1=Address, 2=Calls, 3=Clocks, 4=Insns, 5=Clocks%, 6=Insns%,
			// 7=CPUClocks, 8=CPUClocks%, 9=DMA%, 10+=counters
			const int sortCol = spec.ColumnIndex;

			std::sort(s_profState.mSortedRecords.begin(), s_profState.mSortedRecords.end(),
				[sortCol, ascending](const ProfileState::SortedRecord& a, const ProfileState::SortedRecord& b) {
					int cmp = 0;
					switch (sortCol) {
						case 0: cmp = (a.mContext < b.mContext) ? -1 : (a.mContext > b.mContext) ? 1 : 0; break;
						case 1: cmp = (a.mAddress < b.mAddress) ? -1 : (a.mAddress > b.mAddress) ? 1 : 0; break;
						case 2: cmp = (a.mCalls < b.mCalls) ? -1 : (a.mCalls > b.mCalls) ? 1 : 0; break;
						case 3: // Clocks (sort by cycles)
						case 5: // Clocks% (same ordering as cycles)
							cmp = (a.mCycles < b.mCycles) ? -1 : (a.mCycles > b.mCycles) ? 1 : 0; break;
						case 4: // Insns
						case 6: // Insns% (same ordering as insns)
							cmp = (a.mInsns < b.mInsns) ? -1 : (a.mInsns > b.mInsns) ? 1 : 0; break;
						case 7: // CPUClocks
						case 8: // CPUClocks%
							cmp = (a.mUnhaltedCycles < b.mUnhaltedCycles) ? -1 : (a.mUnhaltedCycles > b.mUnhaltedCycles) ? 1 : 0; break;
						case 9: // DMA% — sort by (cycles - unhaltedCycles) / cycles
							{
								uint32 dmaA = a.mCycles > a.mUnhaltedCycles ? a.mCycles - a.mUnhaltedCycles : 0;
								uint32 dmaB = b.mCycles > b.mUnhaltedCycles ? b.mCycles - b.mUnhaltedCycles : 0;
								// Cross-multiply to avoid division: dmaA/a.mCycles vs dmaB/b.mCycles
								uint64 lhs = (uint64)dmaA * b.mCycles;
								uint64 rhs = (uint64)dmaB * a.mCycles;
								cmp = (lhs < rhs) ? -1 : (lhs > rhs) ? 1 : 0;
							}
							break;
						case 10: // Counter0
						case 11: // Counter0%
							cmp = (a.mCounters[0] < b.mCounters[0]) ? -1 : (a.mCounters[0] > b.mCounters[0]) ? 1 : 0; break;
						case 12: // Counter1
						case 13: // Counter1%
							cmp = (a.mCounters[1] < b.mCounters[1]) ? -1 : (a.mCounters[1] > b.mCounters[1]) ? 1 : 0; break;
						default: break;
					}
					return ascending ? cmp < 0 : cmp > 0;
				});

			sortSpecs->SpecsDirty = false;
		}

		uint32 totalCycles = s_profState.mpMergedFrame ? std::max(1u, s_profState.mpMergedFrame->mTotalCycles) : 1;
		uint32 totalInsns = s_profState.mpMergedFrame ? std::max(1u, s_profState.mpMergedFrame->mTotalInsns) : 1;
		uint32 totalUnhaltedCycles = s_profState.mpMergedFrame ? std::max(1u, s_profState.mpMergedFrame->mTotalUnhaltedCycles) : 1;
		const int recordCount = (int)s_profState.mSortedRecords.size();

		ImGuiListClipper clipper;
		clipper.Begin(recordCount);
		while (clipper.Step()) {
			for (int row = clipper.DisplayStart; row < clipper.DisplayEnd && row < recordCount; ++row) {
				const auto& rec = s_profState.mSortedRecords[row];

				ImGui::TableNextRow();

				// Thread (context)
				ImGui::TableSetColumnIndex(0);
				ImGui::TextUnformatted(GetContextName(rec.mContext));

				// Address with symbol
				ImGui::TableSetColumnIndex(1);
				{
					IATDebugger *dbg = ATGetDebugger();
					if (dbg) {
						VDStringA addrText = dbg->GetAddressText(rec.mAddress, false, true);
						ImGui::TextUnformatted(addrText.c_str());
					} else {
						ImGui::Text("$%04X", rec.mAddress);
					}
				}

				ImGui::TableSetColumnIndex(2);
				ImGui::Text("%u", rec.mCalls);
				ImGui::TableSetColumnIndex(3);
				ImGui::Text("%u", rec.mCycles);
				ImGui::TableSetColumnIndex(4);
				ImGui::Text("%u", rec.mInsns);
				ImGui::TableSetColumnIndex(5);
				ImGui::Text("%.2f%%", (float)rec.mCycles / (float)totalCycles * 100.0f);
				ImGui::TableSetColumnIndex(6);
				ImGui::Text("%.2f%%", (float)rec.mInsns / (float)totalInsns * 100.0f);
				ImGui::TableSetColumnIndex(7);
				ImGui::Text("%u", rec.mUnhaltedCycles);
				ImGui::TableSetColumnIndex(8);
				ImGui::Text("%.2f%%", (float)rec.mUnhaltedCycles / (float)totalUnhaltedCycles * 100.0f);
				ImGui::TableSetColumnIndex(9);
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

} // namespace at_traceviewer_panels
