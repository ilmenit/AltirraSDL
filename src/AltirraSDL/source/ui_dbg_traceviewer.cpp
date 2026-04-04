//	AltirraSDL - Dear ImGui Performance Analyzer (Trace Viewer)
//	Main shell: pane class, menu bar, toolbar, start/stop, file I/O.

#include <stdafx.h>
#include <cmath>
#include <SDL3/SDL.h>
#include <imgui.h>
#include <vd2/system/vdtypes.h>
#include <vd2/system/VDString.h>
#include <vd2/system/refcount.h>
#include <vd2/system/file.h>
#include <vd2/system/zip.h>
#include <vd2/system/text.h>
#include <vd2/system/unknown.h>
#include <at/atcpu/history.h>
#include "ui_dbg_traceviewer.h"
#include "ui_debugger.h"
#include "logging.h"
#include "console.h"
#include "debugger.h"
#include "simulator.h"
#include "trace.h"
#include "tracecpu.h"
#include "traceio.h"
#include "savestateio.h"
#include "profiler.h"

extern ATSimulator g_sim;
extern SDL_Window *g_pWindow;
vdrefptr<ATTraceCollection> ATLoadTraceFromAtari800(const wchar_t *file);

// =========================================================================
// ZoomDeltaSteps implementation
// =========================================================================

void ATImGuiTraceViewerContext::ZoomDeltaSteps(double centerTime, sint32 steps, float viewWidthPixels) {
	sint32 newLevel = std::clamp(mZoomLevel + steps, -40, 0);
	if (newLevel == mZoomLevel)
		return;

	double oldSecsPerPixel = mSecondsPerPixel;
	mZoomLevel = newLevel;
	mSecondsPerPixel = pow(10.0, mZoomLevel / 5.0);

	// Keep centerTime at the same pixel position
	if (viewWidthPixels > 0) {
		double pixelOffset = (centerTime - mStartTime) / oldSecsPerPixel;
		mStartTime = centerTime - pixelOffset * mSecondsPerPixel;
	}
}

// =========================================================================
// ATImGuiTraceViewerPane
// =========================================================================

class ATImGuiTraceViewerPane final : public ATImGuiDebuggerPane {
public:
	ATImGuiTraceViewerPane();

	bool Render() override;

private:
	void SetCollection(ATTraceCollection *coll);
	void RebuildViews();
	void StartStopTracing();

	void DoLoad();
	void DoSave();
	void DoImportA800();
	void DoExportChrome();

	void ExportToChromeTrace(const wchar_t *path) const;
	void Load(const wchar_t *path);
	void Save(const wchar_t *path) const;
	void ImportA800(const wchar_t *path);

	void RenderMenuBar();
	void RenderToolbar();
	void RenderMemoryStatisticsPopup();

	ATImGuiTraceViewerContext mContext;
	bool mbShowMemStats = false;
	int mCapturedTraceIndex = 0;
	VDStringW mTraceName;
};

ATImGuiTraceViewerPane::ATImGuiTraceViewerPane()
	: ATImGuiDebuggerPane(kATUIPaneId_Profiler, "Performance Analyzer")
{
	mContext.mSettings.mbTraceCpuInsns = true;
}

// =========================================================================
// Render
// =========================================================================

bool ATImGuiTraceViewerPane::Render() {
	bool open = true;

	if (mbFocusRequested) {
		ImGui::SetNextWindowFocus();
		mbFocusRequested = false;
	}

	ImGui::SetNextWindowSize(ImVec2(900, 600), ImGuiCond_Appearing);
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

	ImGuiWindowFlags flags = ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_MenuBar;
	if (!ImGui::Begin(mTitle.c_str(), &open, flags)) {
		ImGui::End();
		return open;
	}

	mbHasFocus = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

	RenderMenuBar();
	RenderToolbar();

	// Timeline takes ~60% of remaining space, panels take ~40%
	float availH = ImGui::GetContentRegionAvail().y;
	float timelineH = availH * 0.6f;

	ImGui::BeginChild("##TVTimeline", ImVec2(0, timelineH), ImGuiChildFlags_Borders);
	ATImGuiTraceViewer_RenderTimeline(mContext);
	ImGui::EndChild();

	ImGui::BeginChild("##TVPanels", ImVec2(0, 0), ImGuiChildFlags_Borders);
	ATImGuiTraceViewer_RenderPanels(mContext);
	ImGui::EndChild();

	if (mbShowMemStats)
		RenderMemoryStatisticsPopup();

	ImGui::End();
	return open;
}

// =========================================================================
// Menu bar
// =========================================================================

void ATImGuiTraceViewerPane::RenderMenuBar() {
	if (!ImGui::BeginMenuBar())
		return;

	if (ImGui::BeginMenu("File")) {
		if (ImGui::MenuItem("Load...", nullptr, false, !mContext.mbRecording))
			DoLoad();
		if (ImGui::MenuItem("Save...", nullptr, false, mContext.mpCollection != nullptr))
			DoSave();

		ImGui::Separator();

		if (ImGui::BeginMenu("Import")) {
			if (ImGui::MenuItem("Atari800WinPlus / Atari800 Monitor Trace...", nullptr, false, !mContext.mbRecording))
				DoImportA800();
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Export")) {
			if (ImGui::MenuItem("Chrome Trace Event Format...", nullptr, false, mContext.mpCPUHistoryChannel != nullptr))
				DoExportChrome();
			ImGui::EndMenu();
		}
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("Trace")) {
		const char *traceLabel = mContext.mbRecording ? "Stop Trace" : "Start Trace";
		if (ImGui::MenuItem(traceLabel))
			StartStopTracing();

		ImGui::Separator();
		ImGui::SeparatorText("Settings");
		ImGui::MenuItem("CPU Instruction History", nullptr, &mContext.mSettings.mbTraceCpuInsns);
		ImGui::MenuItem("Trace Video", nullptr, &mContext.mSettings.mbTraceVideo);
		ImGui::MenuItem("Trace BASIC", nullptr, &mContext.mSettings.mbTraceBasic);
		ImGui::MenuItem("Auto-Limit Memory", nullptr, &mContext.mSettings.mbAutoLimitTraceMemory);
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("Tools")) {
		if (ImGui::MenuItem("View Memory Statistics...", nullptr, false, mContext.mpCollection != nullptr))
			mbShowMemStats = true;
		ImGui::EndMenu();
	}

	ImGui::EndMenuBar();
}

// =========================================================================
// Toolbar
// =========================================================================

void ATImGuiTraceViewerPane::RenderToolbar() {
	const char *startStopLabel = mContext.mbRecording ? "Stop" : "Start";
	if (ImGui::Button(startStopLabel))
		StartStopTracing();

	ImGui::SameLine();
	if (ImGui::Button("Zoom In"))
		mContext.ZoomDeltaSteps(mContext.mStartTime + ImGui::GetContentRegionAvail().x * 0.5 * mContext.mSecondsPerPixel, 2, ImGui::GetContentRegionAvail().x);
	ImGui::SameLine();
	if (ImGui::Button("Zoom Out"))
		mContext.ZoomDeltaSteps(mContext.mStartTime + ImGui::GetContentRegionAvail().x * 0.5 * mContext.mSecondsPerPixel, -2, ImGui::GetContentRegionAvail().x);

	ImGui::SameLine();
	ImGui::Separator();
	ImGui::SameLine();

	if (ImGui::RadioButton("Select", mContext.mbSelectionMode))
		mContext.mbSelectionMode = true;
	ImGui::SameLine();
	if (ImGui::RadioButton("Move", !mContext.mbSelectionMode))
		mContext.mbSelectionMode = false;

	ImGui::Separator();
}

// =========================================================================
// Collection management
// =========================================================================

void ATImGuiTraceViewerPane::SetCollection(ATTraceCollection *coll) {
	mContext.mpCollection = coll;
	mContext.mbSelectionValid = false;
	mContext.mFocusTime = -1;
	mContext.mbFocusTimeChanged = false;
	RebuildViews();
}

void ATImGuiTraceViewerPane::RebuildViews() {
	vdrefptr<IATTraceChannel> frameChannel;
	vdrefptr<ATTraceChannelCPUHistory> cpuHistoryChannel;
	vdrefptr<IATTraceChannel> logChannel;

	mContext.mGroups.clear();
	mContext.mTraceDuration = 0;

	if (mContext.mpCollection) {
		for (size_t i = 0, n = mContext.mpCollection->GetGroupCount(); i < n; ++i) {
			ATTraceGroup *group = mContext.mpCollection->GetGroup(i);

			if (group->GetChannelCount() == 0)
				continue;

			mContext.mTraceDuration = std::max(mContext.mTraceDuration, group->GetDuration());

			ATTraceGroupType type = group->GetType();
			if (type == kATTraceGroupType_Frames) {
				frameChannel = group->GetChannel(0);
				continue;
			} else if (type == kATTraceGroupType_CPUHistory) {
				cpuHistoryChannel = vdpoly_cast<ATTraceChannelCPUHistory *>(group->GetChannel(0));
				continue;
			} else if (type == kATTraceGroupType_Log) {
				logChannel = group->GetChannel(0);
			}

			ATImGuiTraceViewerContext::GroupView gv;
			gv.mName = group->GetName();
			gv.mbVariableHeight = (type == kATTraceGroupType_Video);

			const size_t numChannels = group->GetChannelCount();
			for (size_t chIdx = 0; chIdx < numChannels; ++chIdx) {
				IATTraceChannel *channel = group->GetChannel(chIdx);
				if (channel->IsEmpty())
					continue;

				ATImGuiTraceViewerContext::ChannelView cv;
				cv.mpChannel = channel;
				cv.mName = channel->GetName();
				cv.mType = (type == kATTraceGroupType_Video) ? ATImGuiTraceViewerContext::kChannelType_Video
					: (type == kATTraceGroupType_Tape) ? ATImGuiTraceViewerContext::kChannelType_Tape
					: (type == kATTraceGroupType_Log) ? ATImGuiTraceViewerContext::kChannelType_Log
					: ATImGuiTraceViewerContext::kChannelType_Default;

				gv.mChannels.push_back(std::move(cv));
			}

			if (!gv.mChannels.empty())
				mContext.mGroups.push_back(std::move(gv));
		}
	}

	mContext.mpFrameChannel = frameChannel;
	mContext.mpCPUHistoryChannel = cpuHistoryChannel;
	mContext.mpLogChannel = logChannel;

	if (cpuHistoryChannel)
		mContext.mTimestampDecoder = cpuHistoryChannel->GetTimestampDecoder();
	else
		mContext.mTimestampDecoder = g_sim.GetTimestampDecoder();

	// Reset zoom to show full trace
	if (mContext.mTraceDuration > 0) {
		mContext.mStartTime = 0;
		mContext.mZoomLevel = -15;
		mContext.mSecondsPerPixel = pow(10.0, mContext.mZoomLevel / 5.0);
	}
}

// =========================================================================
// Trace recording
// =========================================================================

void ATImGuiTraceViewerPane::StartStopTracing() {
	if (!mContext.mbRecording) {
		mContext.mbRecording = true;
		SetCollection(nullptr);
		g_sim.StartTracing(mContext.mSettings);
		g_sim.Resume();
	} else {
		mContext.mbRecording = false;
		vdrefptr<ATTraceCollection> newColl(g_sim.GetTraceCollection());
		g_sim.StopTracing();
		g_sim.Pause();
		SetCollection(newColl);
		mTraceName.sprintf(L"Captured Trace %d", ++mCapturedTraceIndex);
	}
}

// =========================================================================
// File I/O wrappers (SDL file dialogs)
// =========================================================================

void ATImGuiTraceViewerPane::DoLoad() {
	static const SDL_DialogFileFilter filters[] = {
		{ "Altirra Trace (*.at2trace)", "at2trace" },
		{ "All Files", "*" },
	};
	SDL_ShowOpenFileDialog([](void *ud, const char * const *fl, int) {
		if (fl && fl[0]) {
			auto *self = static_cast<ATImGuiTraceViewerPane *>(ud);
			try {
				VDStringW wpath = VDTextU8ToW(VDStringA(fl[0]));
				self->Load(wpath.c_str());
				self->mTraceName = wpath;
			} catch (const MyError& e) {
				LOG_ERROR("TraceViewer", "Load failed: %s", e.c_str());
			}
		}
	}, this, g_pWindow, filters, 2, nullptr, false);
}

void ATImGuiTraceViewerPane::DoSave() {
	static const SDL_DialogFileFilter filters[] = {
		{ "Altirra Trace (*.at2trace)", "at2trace" },
	};
	SDL_ShowSaveFileDialog([](void *ud, const char * const *fl, int) {
		if (fl && fl[0]) {
			auto *self = static_cast<ATImGuiTraceViewerPane *>(ud);
			try {
				VDStringW wpath = VDTextU8ToW(VDStringA(fl[0]));
				self->Save(wpath.c_str());
			} catch (const MyError& e) {
				LOG_ERROR("TraceViewer", "Save failed: %s", e.c_str());
			}
		}
	}, this, g_pWindow, filters, 1, nullptr);
}

void ATImGuiTraceViewerPane::DoImportA800() {
	static const SDL_DialogFileFilter filters[] = {
		{ "Atari800 Trace (*.txt)", "txt" },
		{ "All Files", "*" },
	};
	SDL_ShowOpenFileDialog([](void *ud, const char * const *fl, int) {
		if (fl && fl[0]) {
			auto *self = static_cast<ATImGuiTraceViewerPane *>(ud);
			try {
				VDStringW wpath = VDTextU8ToW(VDStringA(fl[0]));
				self->ImportA800(wpath.c_str());
				self->mTraceName = wpath;
			} catch (const MyError& e) {
				LOG_ERROR("TraceViewer", "Import failed: %s", e.c_str());
			}
		}
	}, this, g_pWindow, filters, 2, nullptr, false);
}

void ATImGuiTraceViewerPane::DoExportChrome() {
	static const SDL_DialogFileFilter filters[] = {
		{ "Chrome Trace JSON (*.json)", "json" },
	};
	SDL_ShowSaveFileDialog([](void *ud, const char * const *fl, int) {
		if (fl && fl[0]) {
			auto *self = static_cast<ATImGuiTraceViewerPane *>(ud);
			try {
				VDStringW wpath = VDTextU8ToW(VDStringA(fl[0]));
				self->ExportToChromeTrace(wpath.c_str());
			} catch (const MyError& e) {
				LOG_ERROR("TraceViewer", "Export failed: %s", e.c_str());
			}
		}
	}, this, g_pWindow, filters, 1, nullptr);
}

// =========================================================================
// Load / Save / Import / Export
// =========================================================================

void ATImGuiTraceViewerPane::Load(const wchar_t *path) {
	VDFileStream file(path);
	VDZipArchive ziparch;
	ziparch.Init(&file);

	sint32 n = ziparch.GetFileCount();
	for (sint32 i = 0; i < n; ++i) {
		const VDZipArchive::FileInfo& info = ziparch.GetFileInfo(i);
		if (info.mDecodedFileName == L"trace.json") {
			vdautoptr ds(ATCreateSaveStateDeserializer(L"trace.json"));

			vdrefptr<IATSerializable> rootObj;
			ds->Deserialize(ziparch, ~rootObj, nullptr);

			vdrefptr traceCollection = ATLoadTrace(*rootObj,
				[](int, int) {}
			);

			SetCollection(traceCollection);
			return;
		}
	}
}

void ATImGuiTraceViewerPane::Save(const wchar_t *path) const {
	if (!mContext.mpCollection)
		return;

	VDFileStream fs(path, nsVDFile::kWrite | nsVDFile::kCreateAlways | nsVDFile::kSequential);
	VDBufferedWriteStream bs(&fs, 65536);
	vdautoptr<IVDZipArchiveWriter> zip(VDCreateZipArchiveWriter(bs));

	{
		vdautoptr<IATSaveStateSerializer> ser(ATCreateSaveStateSerializer(L"trace.json"));
		ser->SetCompressionLevel(VDDeflateCompressionLevel::Quick);
		ser->BeginSerialize(*zip);

		vdrefptr<IATSerializable> snapshot = ATSaveTrace(*mContext.mpCollection,
			*ser,
			[](int, int) {}
		);

		ser->EndSerialize(*snapshot);
	}

	zip->Finalize();
	bs.Flush();
	fs.close();
}

void ATImGuiTraceViewerPane::ImportA800(const wchar_t *path) {
	auto traceCollection = ATLoadTraceFromAtari800(path);
	SetCollection(traceCollection);
}

void ATImGuiTraceViewerPane::ExportToChromeTrace(const wchar_t *path) const {
	if (!mContext.mpCollection || !mContext.mpCPUHistoryChannel)
		return;

	ATTraceChannelCPUHistory& cpuTrace = *mContext.mpCPUHistoryChannel;

	VDFileStream fileOutput(path, nsVDFile::kWrite | nsVDFile::kCreateAlways | nsVDFile::kDenyRead | nsVDFile::kSequential);
	VDTextOutputStream textOutput(&fileOutput);

	textOutput.PutLine("{\n");
	textOutput.PutLine("\"traceEvents\": [");

	// Meta events for CPU history (PID 1)
	static constexpr struct { int mThreadId; const char *mpName; } kThreads[] = {
		{ kATProfileContext_Main, "Main" },
		{ kATProfileContext_IRQ, "IRQ" },
		{ kATProfileContext_VBI, "VBI" },
		{ kATProfileContext_DLI, "DLI" },
	};

	textOutput.PutLine(R"--({"pid":1,"tid":0,"ph":"M","name":"process_name","args":{"name":"CPU (calls)"}})--");
	textOutput.PutLine(R"--(,{"pid":1,"tid":0,"ph":"M","name":"process_sort_index","args":{"sort_index":1}})--");

	for (int i = 0; i < (int)(sizeof(kThreads)/sizeof(kThreads[0])); ++i) {
		textOutput.FormatLine(R"--(,{"pid":1,"tid":%d,"ph":"M","name":"thread_name","args":{"name":"%s"}})--",
			kThreads[i].mThreadId, kThreads[i].mpName);
		textOutput.FormatLine(R"--(,{"pid":1,"tid":%d,"ph":"M","name":"thread_sort_index","args":{"sort_index":%d}})--",
			kThreads[i].mThreadId, i);
	}

	// Frame counter under PID 0
	if (mContext.mpFrameChannel) {
		IATTraceChannel *frameCh = mContext.mpFrameChannel;
		textOutput.PutLine(R"--(,{"pid":0,"tid":10,"ph":"C","name":"Frames","ts":0,"args":{"value":0}})--");
		textOutput.PutLine(R"--(,{"pid":0,"tid":0,"ph":"M","name":"process_sort_index","args":{"sort_index":0}})--");

		int nextValue = 1;
		frameCh->StartIteration(0, frameCh->GetDuration(), 0);
		ATTraceEvent frameEvent;
		while (frameCh->GetNextEvent(frameEvent)) {
			textOutput.FormatLine(R"--(,{"pid":0,"tid":10,"ph":"C","name":"Frames","ts":%.2f,"args":{"value":%d}})--",
				frameEvent.mEventStop * 1000000.0, nextValue);
			nextValue ^= 1;
		}
	}

	// Normal trace groups under PID 2+
	unsigned pid = 2;
	for (size_t groupIdx = 0, groupCount = mContext.mpCollection->GetGroupCount(); groupIdx < groupCount; ++groupIdx) {
		ATTraceGroup *traceGroup = mContext.mpCollection->GetGroup(groupIdx);
		if (traceGroup->GetType() != kATTraceGroupType_Normal)
			continue;

		unsigned tid = 0;
		for (size_t channelIdx = 0, channelCount = traceGroup->GetChannelCount(); channelIdx < channelCount; ++channelIdx) {
			IATTraceChannel *channel = traceGroup->GetChannel(channelIdx);
			channel->StartIteration(0, channel->GetDuration(), 0);

			ATTraceEvent ev;
			if (channel->GetNextEvent(ev)) {
				VDStringA prefix;
				prefix.sprintf(R"--(,{"pid":%d,"tid":%d,)--", pid, tid);

				if (tid == 0) {
					textOutput.FormatLine(R"--(,{"pid":%d,"tid":0,"ph":"M","name":"process_name","args":{"name":"%s"}})--",
						pid, VDTextWToU8(VDStringSpanW(traceGroup->GetName())).c_str());
					textOutput.FormatLine(R"--(,{"pid":%d,"tid":0,"ph":"M","name":"process_sort_index","args":{"sort_index":%d}})--",
						pid, pid);
				}

				textOutput.FormatLine(R"--(%s"ph":"M","name":"thread_name","args":{"name":"%s"}})--",
					prefix.c_str(), VDTextWToU8(VDStringSpanW(channel->GetName())).c_str());
				textOutput.FormatLine(R"--(%s"ph":"M","name":"thread_sort_index","args":{"sort_index":"%u"}})--",
					prefix.c_str(), tid);

				do {
					textOutput.FormatLine(R"--(%s"ph":"B","ts":%.2f,"name":"%s"})--",
						prefix.c_str(), ev.mEventStart * 1000000.0, VDTextWToU8(VDStringSpanW(ev.mpName)).c_str());
					textOutput.FormatLine(R"--(%s"ph":"E","ts":%.2f})--",
						prefix.c_str(), ev.mEventStop * 1000000.0);
				} while (channel->GetNextEvent(ev));

				++tid;
			}
		}

		if (tid)
			++pid;
	}

	// CPU history with call stack tracking
	uint32 startEventIdx = 0;
	uint32 endEventIdx = cpuTrace.GetEventCount();
	const ATCPUHistoryEntry *hents[256];
	uint32 pos = startEventIdx;
	uint32 baseCycle = cpuTrace.GetHistoryBaseCycle();

	bool adjustStack = false;
	bool init = true;
	uint8 lastS = 0;

	double cyclesToTimestamp = cpuTrace.GetSecondsPerTick() * 1000000.0;
	uint8 shadowStack[256] {};

	static constexpr unsigned kCallStackLimit = 32;
	uint32 callStack[kCallStackLimit];
	uint8 callStackTid[kCallStackLimit];
	unsigned callStackHeight = 0;

	struct CallFrameHash {
		size_t operator()(const std::pair<uint32, uint32>& v) const {
			return v.first + ((size_t)v.second << 16) + ((size_t)v.second >> 16);
		}
	};

	vdfastvector<std::pair<uint32, uint32>> callFrameTable;
	vdhashmap<std::pair<uint32, uint32>, uint32, CallFrameHash> callFrameLookup;

	auto cursor = cpuTrace.StartHistoryIteration(0, 0);
	int ctid = kATProfileContext_Main;
	int nextTid = ctid;
	bool firstEvent = true;

	while (pos < endEventIdx) {
		uint32 n = cpuTrace.ReadHistoryEvents(cursor, hents, pos, std::min<uint32>(endEventIdx - pos, (uint32)vdcountof(hents)));
		if (!n)
			break;

		if (init) {
			init = false;
			lastS = hents[0]->mS;
		}

		for (uint32 i = 0; i < n; ++i) {
			const ATCPUHistoryEntry& he = *hents[i];

			if (he.mbIRQ != he.mbNMI) {
				adjustStack = true;
				if (he.mbNMI) {
					if (mContext.mTimestampDecoder.IsInterruptPositionVBI(he.mCycle))
						nextTid = kATProfileContext_VBI;
					else
						nextTid = kATProfileContext_DLI;
				} else {
					nextTid = kATProfileContext_IRQ;
				}
			}

			if (adjustStack) {
				sint8 sdir = (sint8)(he.mS - lastS);
				unsigned popToHeight = callStackHeight;

				bool needOpen = false;

				if (sdir > 0) {
					while (lastS != he.mS) {
						uint8 stackLevel = shadowStack[lastS];
						if (stackLevel) {
							shadowStack[lastS] = 0;
							if (popToHeight >= stackLevel)
								popToHeight = stackLevel - 1;
						}
						++lastS;
					}
				} else if (sdir < 0) {
					while (lastS != he.mS)
						shadowStack[--lastS] = 0;
					needOpen = true;
				}

				if (!needOpen && nextTid != ctid) {
					if (popToHeight && shadowStack[lastS]) {
						--popToHeight;
						shadowStack[lastS] = 0;
					}
					needOpen = true;
				}

				while (callStackHeight > popToHeight) {
					textOutput.FormatLine(R"--(,{"pid":1,"tid": %d,"ph":"E","ts":%.2f})--",
						ctid, (double)(he.mCycle - baseCycle) * cyclesToTimestamp);
					--callStackHeight;
					ctid = callStackTid[callStackHeight];
				}

				if (needOpen && callStackHeight < kCallStackLimit) {
					uint32 extpc = he.mPC + (he.mK << 16);
					const unsigned parentFrameId = callStackHeight ? callStack[callStackHeight - 1] : 0;

					auto r = callFrameLookup.insert(std::pair<uint32, uint32>(parentFrameId, extpc));
					if (r.second) {
						callFrameTable.emplace_back(parentFrameId, extpc);
						r.first->second = (uint32)callFrameLookup.size();
					}

					const unsigned frameId = r.first->second;
					callStack[callStackHeight] = frameId;
					callStackTid[callStackHeight] = ctid;
					++callStackHeight;
					shadowStack[lastS] = callStackHeight;
					ctid = nextTid;

					const double ts = (double)(he.mCycle - baseCycle) * cyclesToTimestamp;
					if (firstEvent) {
						firstEvent = false;
						if (ts > 0) {
							textOutput.FormatLine(R"--(,{"pid":1,"tid": %d,"ph":"B","name":"preroll","ts":0})--", ctid);
							textOutput.FormatLine(R"--(,{"pid":1,"tid": %d,"ph":"E","name":"preroll","ts":%.2f})--", ctid, ts);
						}
					}

					textOutput.FormatLine(R"--(,{"pid":1,"tid":%d,"ph":"B","name":"%s","sf":%u,"ts": %.2f})--",
						ctid, ATGetDebugger()->GetAddressText(extpc, false, true).c_str(), frameId, ts);
				}

				adjustStack = false;
			}

			switch (he.mOpcode[0]) {
				case 0x20:		// JSR
				case 0x60:		// RTS
				case 0x40:		// RTI
				case 0x6C:		// JMP (abs)
					adjustStack = true;
					nextTid = ctid;
					if (!(he.mP & AT6502::kFlagI))
						nextTid = kATProfileContext_Main;
					break;
			}
		}

		pos += n;
	}

	textOutput.PutLine("],");
	textOutput.PutLine("\"stackFrames\": {");

	const size_t numFrames = callFrameTable.size();
	for (size_t i = 0; i < numFrames; ++i) {
		const auto [parentFrameId, extpc] = callFrameTable[i];
		if (parentFrameId)
			textOutput.FormatLine(R"--("%u":{"category":"pc","name":"%s","parent":%u}%s)--",
				(unsigned)(i + 1), ATGetDebugger()->GetAddressText(extpc, false, true).c_str(), parentFrameId, (i != numFrames - 1) ? "," : "");
		else
			textOutput.FormatLine(R"--("%u":{"category":"pc","name":"%s"}%s)--",
				(unsigned)(i + 1), ATGetDebugger()->GetAddressText(extpc, false, true).c_str(), (i != numFrames - 1) ? "," : "");
	}

	textOutput.PutLine("}");
	textOutput.PutLine("}");
	textOutput.Flush();
	fileOutput.close();
}

// =========================================================================
// Memory Statistics popup
// =========================================================================

void ATImGuiTraceViewerPane::RenderMemoryStatisticsPopup() {
	ImGui::SetNextWindowSize(ImVec2(500, 300), ImGuiCond_Appearing);
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

	if (ImGui::Begin("Memory Statistics", &mbShowMemStats, ImGuiWindowFlags_NoSavedSettings)) {
		if (mContext.mpCollection) {
			const size_t numGroups = mContext.mpCollection->GetGroupCount();
			for (size_t i = 0; i < numGroups; ++i) {
				ATTraceGroup *group = mContext.mpCollection->GetGroup(i);
				const wchar_t *groupName = group->GetName();

				const size_t channelCount = group->GetChannelCount();
				for (size_t j = 0; j < channelCount; ++j) {
					IATTraceChannel *channel = group->GetChannel(j);
					if (channel->IsEmpty())
						continue;

					const uint64 traceSize = channel->GetTraceSize();
					VDStringA line;
					line.sprintf("%ls:%ls: %.1fMB",
						groupName, channel->GetName(), (double)traceSize / 1048576.0);

					auto *cpuChannel = vdpoly_cast<ATTraceChannelCPUHistory *>(channel);
					if (cpuChannel) {
						VDStringA extra;
						extra.sprintf(" (%u insns @ %.2f bytes/insn)",
							cpuChannel->GetEventCount(),
							(double)traceSize / (double)cpuChannel->GetEventCount());
						line += extra;
					}

					ImGui::TextUnformatted(line.c_str());
				}
			}
		} else {
			ImGui::TextUnformatted("No trace loaded.");
		}
	}
	ImGui::End();
}

// =========================================================================
// Registration
// =========================================================================

void ATUIDebuggerEnsureTraceViewerPane() {
	if (!ATUIDebuggerGetPane(kATUIPaneId_Profiler)) {
		auto *pane = new ATImGuiTraceViewerPane();
		ATUIDebuggerRegisterPane(pane);
	}
}
