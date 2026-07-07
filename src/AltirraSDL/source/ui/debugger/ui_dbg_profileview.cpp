//	AltirraSDL - Dear ImGui debugger Profile View pane

#include <stdafx.h>
#include <algorithm>
#include <cstring>
#include <numeric>
#include <SDL3/SDL.h>
#include <imgui.h>
#include <vd2/system/error.h>
#include <vd2/system/vdtypes.h>
#include <vd2/system/VDString.h>
#include <at/atcpu/history.h>
#include <at/atdebugger/target.h>
#include "ui_debugger.h"
#include "cpu.h"
#include "debugger.h"
#include "disasm.h"
#include "simulator.h"
#include "profiler.h"

extern ATSimulator g_sim;

namespace {

const char *const kProfileModeNames[] = {
	"Instructions",
	"Functions",
	"Call Graph",
	"Basic Blocks",
	"BASIC Lines"
};

const char *const kCounterOptionNames[] = {
	"Branch Taken",
	"Branch Not Taken",
	"Page Crossing",
	"Redundant Op"
};

const char *const kCounterColumnNames[] = {
	"Taken",
	"NotTaken",
	"PageCross",
	"Redundant"
};

constexpr int kTimelineZoomFactors[] = {
	1, 2, 3, 4, 6, 8, 10, 12, 14, 16, 20, 24, 28, 32
};

const char *GetCounterModeName(ATProfileCounterMode cm) {
	int idx = (int)cm - 1;
	if (idx >= 0 && idx < (int)std::size(kCounterColumnNames))
		return kCounterColumnNames[idx];

	return "?";
}

const char *GetContextName(uint32 context) {
	switch (context) {
		case kATProfileContext_Main:		return "Main";
		case kATProfileContext_Interrupt:	return "Interrupt";
		case kATProfileContext_IRQ:			return "IRQ";
		case kATProfileContext_VBI:			return "VBI";
		case kATProfileContext_DLI:			return "DLI";
		default:							return "";
	}
}

struct ProfileTableRecord {
	uint32 mRecordId;
	uint32 mAddress;
	uint32 mContext;
	uint32 mCalls;
	uint32 mInsns;
	uint32 mCycles;
	uint32 mUnhaltedCycles;
	uint32 mCounters[2];
};

class ATImGuiProfileViewPane final : public ATImGuiDebuggerPane {
public:
	ATImGuiProfileViewPane();

	bool Render() override;

private:
	void RenderToolbar();
	void RenderOptionsPopup();
	void RenderBoundaryRuleDialog();
	void RenderTimeline();
	void RenderFrameRangeControls();
	void RenderProfileTable();
	void RenderProfileSelectionStatus() const;
	void RenderCallGraphTree();
	void RenderCallGraphNode(uint32 idx, const vdfastvector<uint32>& firstChild,
		const vdfastvector<uint32>& nextSibling);
	void RenderDetailWindow();

	void StartProfiler();
	void StopProfiler();
	void UnloadProfile();
	void LoadProfile();
	void MergeFrames(uint32 start, uint32 end);
	void RemakeView();
	void RebuildSortedRecords();
	void ToggleCounterMode(ATProfileCounterMode mode);
	void CopyProfileAsCsv() const;
	void OpenDetailForAddress(uint32 addr);
	void BuildDetailRecords(const ATProfileFrame& frame);
	const ATProfileRecord *FindDetailRecord(uint32 addr) const;
	void BuildDetailLines(vdfastvector<uint32>& lines, uint32 visibleLines) const;
	void ScrollDetailByLines(int delta);
	void ScrollDetailByPage(bool down);
	bool IsProfileRecordSelected(uint32 recordId) const;
	void SelectProfileRecord(uint32 recordId, int sortedRow);
	void UpdateTimelineVerticalRange();
	void SetTimelineZoomIndex(int index, float focusX);
	float ClampTimelineScroll(float scroll, float width) const;
	int TimelinePointToFrame(float x, float width, bool clamp) const;

	ATProfileMode mProfileMode = kATProfileMode_Insns;
	ATProfileMode mCapturedProfileMode = kATProfileMode_Insns;
	ATProfileCounterMode mProfileCounterModes[2] = {};
	ATProfileCounterMode mProfileSessionCounterModes[2] = {};
	ATProfileBoundaryRule mBoundaryRule = kATProfileBoundaryRule_None;
	char mBoundaryAddrExpr[256] = {};
	char mBoundaryAddrExpr2[256] = {};
	bool mbGlobalAddressesEnabled = false;
	bool mbShowBoundaryRuleDialog = false;
	bool mbBoundaryEndFunction = false;

	ATProfileSession mSession;
	vdrefptr<ATProfileMergedFrame> mpMergedFrame;
	const ATProfileFrame *mpCurrentFrame = nullptr;
	const ATProfileFrame::Records *mpRecords = nullptr;
	vdfastvector<ProfileTableRecord> mSortedRecords;
	vdfastvector<ATProfileRecord> mDetailRecords;
	vdfastvector<uint32> mSelectedProfileRecordIds;

	uint32 mFrameStart = 0;
	uint32 mFrameEnd = 1;
	uint32 mDetailTargetAddress = 0;
	uint32 mLastSelectedProfileRecordId = 0;
	uint32 mTimelineVerticalRange = 1;
	float mTimelineScrollX = 0.0f;
	int mTimelineZoomIndex = 0;
	int mTimelineHoverFrame = -1;
	int mTimelineDragStart = -1;
	int mTimelineDragEnd = -1;
	bool mbTimelineAutoFitPending = false;
	bool mbDetailOpen = false;
	VDStringA mError;
};

ATImGuiProfileViewPane::ATImGuiProfileViewPane()
	: ATImGuiDebuggerPane(kATUIPaneId_Profiler, "Profile View")
{
}

bool ATImGuiProfileViewPane::Render() {
	bool open = true;

	if (mbFocusRequested) {
		ImGui::SetNextWindowFocus();
		mbFocusRequested = false;
	}

	ImGui::SetNextWindowSize(ImVec2(760, 500), ImGuiCond_Appearing);

	if (!ImGui::Begin("Profile View", &open)) {
		ImGui::End();
		return open;
	}

	mbHasFocus = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

	RenderToolbar();
	RenderBoundaryRuleDialog();

	if (!mError.empty()) {
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.25f, 0.25f, 1.0f));
		ImGui::TextWrapped("%s", mError.c_str());
		ImGui::PopStyleColor();
	}

	if (!mpRecords || mpRecords->empty()) {
		ImGui::TextWrapped("No profiling data is available. Begin execution with the Play button in this profiler pane to begin data collection and Stop to end the session.");
	} else {
		RenderTimeline();
		RenderFrameRangeControls();
		RenderProfileTable();
	}

	ImGui::End();

	RenderDetailWindow();
	return open;
}

void ATImGuiProfileViewPane::RenderToolbar() {
	ATCPUProfiler *profiler = g_sim.GetProfiler();
	const bool profiling = profiler && profiler->IsRunning();
	const bool running = g_sim.IsRunning();

	if (!profiling)
		ImGui::BeginDisabled();
	if (ImGui::Button("Stop")) {
		try {
			StopProfiler();
		} catch (const MyError& e) {
			mError = e.c_str();
		}
	}
	if (!profiling)
		ImGui::EndDisabled();

	ImGui::SameLine();
	if (profiling && !running)
		ImGui::BeginDisabled();
	if (ImGui::Button("Break+Profile")) {
		try {
			StartProfiler();
			UnloadProfile();
			if (IATDebugger *dbg = ATGetDebugger())
				dbg->Break();
		} catch (const MyError& e) {
			mError = e.c_str();
		}
	}
	if (profiling && !running)
		ImGui::EndDisabled();

	ImGui::SameLine();
	if (profiling && running)
		ImGui::BeginDisabled();
	if (ImGui::Button("Run+Profile")) {
		try {
			StartProfiler();
			UnloadProfile();
			if (IATDebugger *dbg = ATGetDebugger())
				dbg->Run(kATDebugSrcMode_Same);
		} catch (const MyError& e) {
			mError = e.c_str();
		}
	}
	if (profiling && running)
		ImGui::EndDisabled();

	ImGui::SameLine();
	ImGui::SetNextItemWidth(145.0f);
	int mode = (int)mProfileMode;
	if (ImGui::Combo("Mode", &mode, kProfileModeNames, (int)std::size(kProfileModeNames)))
		mProfileMode = (ATProfileMode)mode;

	ImGui::SameLine();
	if (ImGui::Button("Options"))
		ImGui::OpenPopup("ProfileOptions");

	RenderOptionsPopup();
}

void ATImGuiProfileViewPane::RenderOptionsPopup() {
	if (!ImGui::BeginPopup("ProfileOptions"))
		return;

	uint32 activeMask = 0;
	for (auto cm : mProfileCounterModes) {
		if (cm)
			activeMask |= (1 << ((uint32)cm - 1));
	}

	const bool allSlotsFull = mProfileCounterModes[0] != kATProfileCounterMode_None
		&& mProfileCounterModes[1] != kATProfileCounterMode_None;

	for (int i = 0; i < 4; ++i) {
		const ATProfileCounterMode mode = (ATProfileCounterMode)(i + 1);
		const bool active = (activeMask & (1 << i)) != 0;
		const bool enabled = active || !allSlotsFull;

		if (ImGui::MenuItem(kCounterOptionNames[i], nullptr, active, enabled))
			ToggleCounterMode(mode);
	}

	ImGui::Separator();
	if (ImGui::BeginMenu("Frame Trigger")) {
		if (ImGui::MenuItem("None", nullptr, mBoundaryRule == kATProfileBoundaryRule_None))
			mBoundaryRule = kATProfileBoundaryRule_None;
		if (ImGui::MenuItem("Vertical Blank", nullptr, mBoundaryRule == kATProfileBoundaryRule_VBlank))
			mBoundaryRule = kATProfileBoundaryRule_VBlank;
		if (ImGui::MenuItem("PC Address...", nullptr,
				mBoundaryRule == kATProfileBoundaryRule_PCAddress
				|| mBoundaryRule == kATProfileBoundaryRule_PCAddressFunction)) {
			mbBoundaryEndFunction = mBoundaryRule == kATProfileBoundaryRule_PCAddressFunction;
			mbShowBoundaryRuleDialog = true;
		}
		ImGui::EndMenu();
	}

	ImGui::Separator();
	ImGui::MenuItem("Enable Global Addresses", nullptr, &mbGlobalAddressesEnabled);

	ImGui::EndPopup();
}

void ATImGuiProfileViewPane::RenderBoundaryRuleDialog() {
	static char sEditAddr1[256];
	static char sEditAddr2[256];
	static bool sEditEndFunction;
	static bool sWasOpen;
	static VDStringA sValidationError;

	if (!mbShowBoundaryRuleDialog) {
		sWasOpen = false;
		return;
	}

	if (!sWasOpen) {
		memcpy(sEditAddr1, mBoundaryAddrExpr, sizeof(sEditAddr1));
		memcpy(sEditAddr2, mBoundaryAddrExpr2, sizeof(sEditAddr2));
		sEditEndFunction = mbBoundaryEndFunction;
		sValidationError.clear();
		sWasOpen = true;
	}

	ImGui::SetNextWindowSizeConstraints(ImVec2(420, 0), ImVec2(420, FLT_MAX));
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
		ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

	if (ImGui::Begin("Trigger On PC Address", &mbShowBoundaryRuleDialog,
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
		if (sEditEndFunction)
			ImGui::BeginDisabled();
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted("End frame address (optional)");
		ImGui::SameLine(160.0f);
		ImGui::SetNextItemWidth(-1);
		ImGui::InputText("##addr2", sEditAddr2, sizeof(sEditAddr2));
		if (sEditEndFunction)
			ImGui::EndDisabled();

		if (!sValidationError.empty()) {
			ImGui::Spacing();
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.25f, 0.25f, 1.0f));
			ImGui::TextWrapped("%s", sValidationError.c_str());
			ImGui::PopStyleColor();
		}

		ImGui::Separator();

		const float buttonWidth = 80.0f;
		if (ImGui::Button("OK", ImVec2(buttonWidth, 0))) {
			sValidationError.clear();

			if (!sEditAddr1[0]) {
				sValidationError = "Start frame address is required.";
			} else {
				try {
					if (IATDebugger *dbg = ATGetDebugger()) {
						dbg->EvaluateThrow(sEditAddr1);
						if (!sEditEndFunction && sEditAddr2[0])
							dbg->EvaluateThrow(sEditAddr2);
					}

					memcpy(mBoundaryAddrExpr, sEditAddr1, sizeof(mBoundaryAddrExpr));
					if (sEditEndFunction) {
						mBoundaryAddrExpr2[0] = 0;
						mBoundaryRule = kATProfileBoundaryRule_PCAddressFunction;
					} else {
						memcpy(mBoundaryAddrExpr2, sEditAddr2, sizeof(mBoundaryAddrExpr2));
						mBoundaryRule = kATProfileBoundaryRule_PCAddress;
					}

					mbBoundaryEndFunction = sEditEndFunction;
					mbShowBoundaryRuleDialog = false;
				} catch (const MyError& e) {
					sValidationError = e.c_str();
				}
			}
		}

		ImGui::SameLine();
		if (ImGui::Button("Cancel", ImVec2(buttonWidth, 0)))
			mbShowBoundaryRuleDialog = false;
	}
	ImGui::End();
}

void ATImGuiProfileViewPane::RenderFrameRangeControls() {
	if (mSession.mpFrames.size() <= 1)
		return;

	ImGui::Separator();
	ImGui::Text("Frames: %u", (uint32)mSession.mpFrames.size());

	int start = (int)mFrameStart;
	int end = (int)mFrameEnd;
	const int maxFrame = (int)mSession.mpFrames.size();

	ImGui::SameLine();
	ImGui::SetNextItemWidth(90.0f);
	bool changed = ImGui::InputInt("Start", &start);
	ImGui::SameLine();
	ImGui::SetNextItemWidth(90.0f);
	changed |= ImGui::InputInt("End", &end);

	if (changed) {
		start = std::clamp(start, 0, maxFrame - 1);
		end = std::clamp(end, start + 1, maxFrame);
		MergeFrames((uint32)start, (uint32)end);
	}
}

void ATImGuiProfileViewPane::RenderTimeline() {
	const uint32 frameCount = (uint32)mSession.mpFrames.size();
	if (frameCount <= 1)
		return;

	ImGui::Separator();

	const float width = ImGui::GetContentRegionAvail().x;
	const float height = 72.0f;
	if (width <= 1.0f)
		return;

	if (mbTimelineAutoFitPending) {
		const int targetPixelsPerFrame = std::max(1, (int)(width / (float)frameCount));
		auto it = std::upper_bound(std::begin(kTimelineZoomFactors),
			std::end(kTimelineZoomFactors), targetPixelsPerFrame);
		mTimelineZoomIndex = std::max(0, (int)(it - std::begin(kTimelineZoomFactors)) - 1);
		mTimelineScrollX = 0.0f;
		mbTimelineAutoFitPending = false;
	}

	const int pixelsPerFrame = kTimelineZoomFactors[mTimelineZoomIndex];
	mTimelineScrollX = ClampTimelineScroll(mTimelineScrollX, width);

	const ImVec2 canvasPos = ImGui::GetCursorScreenPos();
	const ImVec2 canvasSize(width, height);
	ImGui::InvisibleButton("##ProfileTimeline", canvasSize,
		ImGuiButtonFlags_MouseButtonLeft);

	const bool hovered = ImGui::IsItemHovered();
	const bool active = ImGui::IsItemActive();
	const ImVec2 canvasEnd(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y);
	ImDrawList *drawList = ImGui::GetWindowDrawList();

	drawList->AddRectFilled(canvasPos, canvasEnd, IM_COL32(32, 32, 32, 255));
	drawList->AddRect(canvasPos, canvasEnd, IM_COL32(80, 80, 80, 255));

	if (hovered || active) {
		ImGuiIO& io = ImGui::GetIO();
		if (io.MouseWheel != 0.0f)
			SetTimelineZoomIndex(mTimelineZoomIndex + (io.MouseWheel > 0.0f ? 1 : -1),
				io.MousePos.x - canvasPos.x);

		if (ImGui::IsKeyPressed(ImGuiKey_KeypadAdd)
			|| ImGui::IsKeyPressed(ImGuiKey_Equal))
			SetTimelineZoomIndex(mTimelineZoomIndex + 1, io.MousePos.x - canvasPos.x);

		if (ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract)
			|| ImGui::IsKeyPressed(ImGuiKey_Minus))
			SetTimelineZoomIndex(mTimelineZoomIndex - 1, io.MousePos.x - canvasPos.x);
	}

	if (hovered || active)
		mTimelineHoverFrame = TimelinePointToFrame(ImGui::GetIO().MousePos.x - canvasPos.x,
			width, active);
	else
		mTimelineHoverFrame = -1;

	if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
		mTimelineDragStart = TimelinePointToFrame(ImGui::GetIO().MousePos.x - canvasPos.x,
			width, true);
		mTimelineDragEnd = mTimelineDragStart;
	}

	if (active && mTimelineDragStart >= 0) {
		mTimelineDragEnd = TimelinePointToFrame(ImGui::GetIO().MousePos.x - canvasPos.x,
			width, true);
	}

	if (ImGui::IsItemDeactivated() && mTimelineDragStart >= 0 && mTimelineDragEnd >= 0) {
		const uint32 selStart = (uint32)std::min(mTimelineDragStart, mTimelineDragEnd);
		const uint32 selEnd = (uint32)std::max(mTimelineDragStart, mTimelineDragEnd) + 1;
		MergeFrames(selStart, selEnd);
		mTimelineDragStart = -1;
		mTimelineDragEnd = -1;
	}

	uint32 hotStart = mFrameStart;
	uint32 hotEnd = mFrameEnd;
	if (mTimelineDragStart >= 0 && mTimelineDragEnd >= 0) {
		hotStart = (uint32)std::min(mTimelineDragStart, mTimelineDragEnd);
		hotEnd = (uint32)std::max(mTimelineDragStart, mTimelineDragEnd) + 1;
	}

	const float x1 = mTimelineScrollX;
	const float x2 = mTimelineScrollX + width;
	const uint32 firstFrame = std::min<uint32>((uint32)(x1 / pixelsPerFrame), frameCount);
	const uint32 lastFrame = std::min<uint32>((uint32)((x2 + pixelsPerFrame - 1) / pixelsPerFrame),
		frameCount);
	const float vscale = height / (float)std::max<uint32>(1, mTimelineVerticalRange);

	for (uint32 frame = firstFrame; frame < lastFrame; ++frame) {
		const ATProfileFrame *frameInfo = mSession.mpFrames[frame];
		if (!frameInfo)
			continue;

		const float x = canvasPos.x + (float)frame * pixelsPerFrame - mTimelineScrollX;
		const float xNext = canvasPos.x + (float)(frame + 1) * pixelsPerFrame - mTimelineScrollX;
		const float y = canvasEnd.y - std::min(height, (float)frameInfo->mTotalCycles * vscale);
		const bool selected = frame >= hotStart && frame < hotEnd;
		const bool hover = (int)frame == mTimelineHoverFrame;

		ImU32 color = IM_COL32(80, 80, 80, 255);
		if (hover)
			color = IM_COL32(224, 80, 80, 255);
		else if (selected)
			color = IM_COL32(144, 192, 240, 255);

		drawList->AddRectFilled(
			ImVec2(x, y),
			ImVec2(std::max(x + 1.0f, xNext - (pixelsPerFrame > 4 ? 1.0f : 0.0f)), canvasEnd.y),
			color);
	}

	if (mTimelineHoverFrame >= 0 && (uint32)mTimelineHoverFrame < frameCount) {
		const ATProfileFrame *frameInfo = mSession.mpFrames[mTimelineHoverFrame];
		if (frameInfo) {
			ImGui::SetTooltip("Frame %u\nCycles: %u\nCPU cycles: %u\nInsns: %u",
				(uint32)mTimelineHoverFrame,
				frameInfo->mTotalCycles,
				frameInfo->mTotalUnhaltedCycles,
				frameInfo->mTotalInsns);
		}
	}

	const float maxScroll = std::max(0.0f, (float)frameCount * pixelsPerFrame - width);
	if (maxScroll > 0.0f) {
		ImGui::SetNextItemWidth(-1.0f);
		if (ImGui::SliderFloat("##ProfileTimelineScroll", &mTimelineScrollX, 0.0f,
				maxScroll, "", ImGuiSliderFlags_NoInput)) {
			mTimelineScrollX = ClampTimelineScroll(mTimelineScrollX, width);
		}
	}
}

void ATImGuiProfileViewPane::StartProfiler() {
	if (g_sim.IsProfilingEnabled())
		return;

	uint32 param = 0;
	uint32 param2 = 0;

	IATDebugger *dbg = ATGetDebugger();
	if (mBoundaryRule == kATProfileBoundaryRule_PCAddressFunction) {
		if (!dbg)
			throw MyError("Debugger is not available.");

		param = dbg->EvaluateThrow(mBoundaryAddrExpr);
	} else if (mBoundaryRule == kATProfileBoundaryRule_PCAddress) {
		if (!dbg)
			throw MyError("Debugger is not available.");

		param = dbg->EvaluateThrow(mBoundaryAddrExpr);
		param2 = (uint32)0 - 1;

		if (mBoundaryAddrExpr2[0])
			param2 = dbg->EvaluateThrow(mBoundaryAddrExpr2);
	}

	mError.clear();
	mCapturedProfileMode = mProfileMode;

	g_sim.SetProfilingEnabled(true);

	ATCPUProfiler *profiler = g_sim.GetProfiler();
	if (!profiler) {
		g_sim.SetProfilingEnabled(false);
		throw MyError("Unable to start CPU profiler.");
	}

	profiler->SetBoundaryRule(mBoundaryRule, param, param2);
	profiler->Start(mProfileMode, mProfileCounterModes[0], mProfileCounterModes[1]);
	profiler->SetGlobalAddressesEnabled(mbGlobalAddressesEnabled);
}

void ATImGuiProfileViewPane::StopProfiler() {
	ATCPUProfiler *profiler = g_sim.GetProfiler();
	if (!profiler || !profiler->IsRunning())
		return;

	if (IATDebugger *dbg = ATGetDebugger())
		dbg->Break();

	profiler->End();
	LoadProfile();
	g_sim.SetProfilingEnabled(false);
}

void ATImGuiProfileViewPane::UnloadProfile() {
	mbDetailOpen = false;
	mDetailRecords.clear();
	mSession = {};
	mpMergedFrame.clear();
	mpCurrentFrame = nullptr;
	mpRecords = nullptr;
	mSortedRecords.clear();
	mSelectedProfileRecordIds.clear();
	mLastSelectedProfileRecordId = 0;
	mFrameStart = 0;
	mFrameEnd = 1;
}

void ATImGuiProfileViewPane::LoadProfile() {
	UnloadProfile();

	ATCPUProfiler *profiler = g_sim.GetProfiler();
	if (!profiler)
		return;

	profiler->GetSession(mSession);
	memcpy(mProfileSessionCounterModes, mProfileCounterModes, sizeof mProfileSessionCounterModes);

	UpdateTimelineVerticalRange();
	mbTimelineAutoFitPending = true;
	MergeFrames(0, 1);
}

void ATImGuiProfileViewPane::MergeFrames(uint32 start, uint32 end) {
	mpMergedFrame.clear();
	mpCurrentFrame = nullptr;
	mpRecords = nullptr;
	mSortedRecords.clear();
	mSelectedProfileRecordIds.clear();
	mLastSelectedProfileRecordId = 0;

	if (mSession.mpFrames.empty())
		return;

	const uint32 n = (uint32)mSession.mpFrames.size();
	if (start > n - 1)
		start = n - 1;
	if (end <= start)
		end = start + 1;
	if (end > n)
		end = n;

	mFrameStart = start;
	mFrameEnd = end;

	if (start + 1 == end && mSession.mContexts.empty()) {
		mpCurrentFrame = mSession.mpFrames[start];
	} else {
		ATProfileMergeFrames(mSession, start, end, ~mpMergedFrame);
		mpCurrentFrame = mpMergedFrame;
	}

	RemakeView();
}

void ATImGuiProfileViewPane::RemakeView() {
	mpRecords = nullptr;
	mSortedRecords.clear();

	if (mpCurrentFrame) {
		switch (mCapturedProfileMode) {
			case kATProfileMode_Insns:
			case kATProfileMode_BasicLines:
			case kATProfileMode_CallGraph:
				mpRecords = &mpCurrentFrame->mRecords;
				break;

			default:
				mpRecords = &mpCurrentFrame->mBlockRecords;
				break;
		}
	}

	RebuildSortedRecords();
}

void ATImGuiProfileViewPane::RebuildSortedRecords() {
	if (!mpRecords)
		return;

	uint32 recordId = 1;
	for (const auto& rec : *mpRecords) {
		ProfileTableRecord sr {};
		sr.mRecordId = recordId++;
		sr.mAddress = rec.mAddress;
		sr.mContext = rec.mContext;
		sr.mCalls = rec.mCalls;
		sr.mInsns = rec.mInsns;
		sr.mCycles = rec.mCycles;
		sr.mUnhaltedCycles = rec.mUnhaltedCycles;
		sr.mCounters[0] = rec.mCounters[0];
		sr.mCounters[1] = rec.mCounters[1];
		mSortedRecords.push_back(sr);
	}

	std::sort(mSortedRecords.begin(), mSortedRecords.end(),
		[](const ProfileTableRecord& a, const ProfileTableRecord& b) {
			return a.mCycles > b.mCycles;
		});
}

void ATImGuiProfileViewPane::UpdateTimelineVerticalRange() {
	const size_t n = mSession.mpFrames.size();
	if (!n) {
		mTimelineVerticalRange = 1;
		return;
	}

	vdfastvector<uint32> frameDurations;
	frameDurations.resize(n);

	std::transform(mSession.mpFrames.begin(), mSession.mpFrames.end(),
		frameDurations.begin(),
		[](const ATProfileFrame *frame) { return frame ? frame->mTotalCycles : 0; });

	std::sort(frameDurations.begin(), frameDurations.end());

	const size_t n4 = n / 4;
	const size_t n2 = n - n4 * 2;
	const uint64 sum = std::accumulate(frameDurations.begin() + n4,
		frameDurations.end() - n4, (uint64)0);

	mTimelineVerticalRange = (uint32)((sum * 2 + (n2 / 2)) / n2);
	if (!mTimelineVerticalRange)
		mTimelineVerticalRange = 1;
}

float ATImGuiProfileViewPane::ClampTimelineScroll(float scroll, float width) const {
	const uint32 frameCount = (uint32)mSession.mpFrames.size();
	const int pixelsPerFrame = kTimelineZoomFactors[mTimelineZoomIndex];
	const float maxScroll = std::max(0.0f, (float)frameCount * pixelsPerFrame - width);

	return std::clamp(scroll, 0.0f, maxScroll);
}

void ATImGuiProfileViewPane::SetTimelineZoomIndex(int index, float focusX) {
	index = std::clamp(index, 0, (int)std::size(kTimelineZoomFactors) - 1);
	if (index == mTimelineZoomIndex)
		return;

	const int oldPixelsPerFrame = kTimelineZoomFactors[mTimelineZoomIndex];
	const int newPixelsPerFrame = kTimelineZoomFactors[index];
	const float width = ImGui::GetContentRegionAvail().x;
	const float centerX = (focusX >= 0.0f && focusX < width) ? focusX : width * 0.5f;
	const float focusFrame = (mTimelineScrollX + centerX) / (float)oldPixelsPerFrame;

	mTimelineZoomIndex = index;
	mTimelineScrollX = focusFrame * (float)newPixelsPerFrame - centerX;
	mTimelineScrollX = ClampTimelineScroll(mTimelineScrollX, width);
}

int ATImGuiProfileViewPane::TimelinePointToFrame(float x, float width, bool clamp) const {
	const uint32 frameCount = (uint32)mSession.mpFrames.size();
	if (!frameCount)
		return -1;

	if (x < 0.0f)
		return clamp ? 0 : -1;

	if (x >= width)
		return clamp ? (int)frameCount - 1 : -1;

	const int pixelsPerFrame = kTimelineZoomFactors[mTimelineZoomIndex];
	const int frame = (int)((x + mTimelineScrollX) / (float)pixelsPerFrame);

	if (frame < 0)
		return clamp ? 0 : -1;

	if ((uint32)frame >= frameCount)
		return clamp ? (int)frameCount - 1 : -1;

	return frame;
}

void ATImGuiProfileViewPane::ToggleCounterMode(ATProfileCounterMode mode) {
	for (auto& cm : mProfileCounterModes) {
		if (cm == mode) {
			cm = kATProfileCounterMode_None;
			mode = kATProfileCounterMode_None;
			break;
		}
	}

	if (mode)
		mProfileCounterModes[std::size(mProfileCounterModes) - 1] = mode;

	std::sort(std::begin(mProfileCounterModes), std::end(mProfileCounterModes));

	ATProfileCounterMode packed[2] = {};
	int dst = 0;
	for (auto cm : mProfileCounterModes) {
		if (cm)
			packed[dst++] = cm;
	}

	mProfileCounterModes[0] = packed[0];
	mProfileCounterModes[1] = packed[1];
}

void ATImGuiProfileViewPane::CopyProfileAsCsv() const {
	if (mSortedRecords.empty())
		return;

	const uint32 totalCycles = mpCurrentFrame ? std::max(1u, mpCurrentFrame->mTotalCycles) : 1;
	const uint32 totalInsns = mpCurrentFrame ? std::max(1u, mpCurrentFrame->mTotalInsns) : 1;
	const uint32 totalUnhaltedCycles = mpCurrentFrame ? std::max(1u, mpCurrentFrame->mTotalUnhaltedCycles) : 1;
	VDStringA csv;

	const auto appendField = [&csv](const char *s) {
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

	const auto endLine = [&csv] {
		if (!csv.empty() && csv.back() == ',')
			csv.pop_back();
		csv += "\r\n";
	};

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
	for (auto cm : mProfileSessionCounterModes) {
		if (cm != kATProfileCounterMode_None) {
			appendField(GetCounterModeName(cm));

			char pct[64];
			snprintf(pct, sizeof pct, "%s%%", GetCounterModeName(cm));
			appendField(pct);
		}
	}
	endLine();

	for (const auto& rec : mSortedRecords) {
		appendField(GetContextName(rec.mContext));

		if (IATDebugger *dbg = ATGetDebugger()) {
			VDStringA addrText = dbg->GetAddressText(rec.mAddress, false, true);
			appendField(addrText.c_str());
		} else {
			char buf[16];
			snprintf(buf, sizeof buf, "$%04X", rec.mAddress);
			appendField(buf);
		}

		char buf[128];
		snprintf(buf, sizeof buf, "%u", rec.mCalls);
		appendField(buf);
		snprintf(buf, sizeof buf, "%u", rec.mCycles);
		appendField(buf);
		snprintf(buf, sizeof buf, "%u", rec.mInsns);
		appendField(buf);
		snprintf(buf, sizeof buf, "%.2f%%", (float)rec.mCycles / (float)totalCycles * 100.0f);
		appendField(buf);
		snprintf(buf, sizeof buf, "%.2f%%", (float)rec.mInsns / (float)totalInsns * 100.0f);
		appendField(buf);
		snprintf(buf, sizeof buf, "%u", rec.mUnhaltedCycles);
		appendField(buf);
		snprintf(buf, sizeof buf, "%.2f%%", (float)rec.mUnhaltedCycles / (float)totalUnhaltedCycles * 100.0f);
		appendField(buf);
		if (rec.mCycles)
			snprintf(buf, sizeof buf, "%.2f%%", 100.0f * (1.0f - (float)rec.mUnhaltedCycles / (float)rec.mCycles));
		else
			buf[0] = 0;
		appendField(buf);

		for (int i = 0; i < 2; ++i) {
			if (mProfileSessionCounterModes[i] != kATProfileCounterMode_None) {
				snprintf(buf, sizeof buf, "%u", rec.mCounters[i]);
				appendField(buf);
				if (rec.mInsns)
					snprintf(buf, sizeof buf, "%.2f%%", (float)rec.mCounters[i] / (float)rec.mInsns * 100.0f);
				else
					buf[0] = 0;
				appendField(buf);
			}
		}

		endLine();
	}

	SDL_SetClipboardText(csv.c_str());
}

void ATImGuiProfileViewPane::OpenDetailForAddress(uint32 addr) {
	if (!mpCurrentFrame)
		return;

	if (addr == 0xFFFFFF) {
		mError = "The selected entry corresponds to the unknown function executing when profiling was started. It can't be expanded.";
		return;
	}

	BuildDetailRecords(*mpCurrentFrame);
	mDetailTargetAddress = addr;
	mbDetailOpen = true;
}

void ATImGuiProfileViewPane::BuildDetailRecords(const ATProfileFrame& frame) {
	mDetailRecords = frame.mRecords;

	std::sort(mDetailRecords.begin(), mDetailRecords.end(),
		[](const ATProfileRecord& a, const ATProfileRecord& b) {
			return a.mAddress < b.mAddress;
		});

	auto itDst = mDetailRecords.begin();
	for (auto it = mDetailRecords.begin(), itEnd = mDetailRecords.end(); it != itEnd; ) {
		const uint32 addr = it->mAddress;
		ATProfileRecord& dst = *itDst++;
		dst = *it++;

		while (it != itEnd && it->mAddress == addr) {
			dst.mCalls += it->mCalls;
			dst.mCycles += it->mCycles;
			dst.mInsns += it->mInsns;
			dst.mUnhaltedCycles += it->mUnhaltedCycles;
			dst.mCounters[0] += it->mCounters[0];
			dst.mCounters[1] += it->mCounters[1];
			++it;
		}
	}

	mDetailRecords.erase(itDst, mDetailRecords.end());
}

const ATProfileRecord *ATImGuiProfileViewPane::FindDetailRecord(uint32 addr) const {
	auto it = std::lower_bound(mDetailRecords.begin(), mDetailRecords.end(), addr,
		[](const ATProfileRecord& rec, uint32 key) {
			return rec.mAddress < key;
		});

	if (it != mDetailRecords.end() && it->mAddress == addr)
		return &*it;

	return nullptr;
}

void ATImGuiProfileViewPane::BuildDetailLines(vdfastvector<uint32>& lines,
	uint32 visibleLines) const {
	const uint32 scrollMarginLines = 5;
	const uint32 linesAbove = (visibleLines >> 1) + scrollMarginLines;
	const uint32 bankSpace = mDetailTargetAddress & 0xffff0000;
	uint32 nextAddr = (mDetailTargetAddress - linesAbove * 3) & 0xffff;
	uint32 stepAddr = nextAddr;

	lines.clear();

	while (stepAddr + bankSpace != mDetailTargetAddress) {
		if (nextAddr != stepAddr && !FindDetailRecord(bankSpace + stepAddr)) {
			stepAddr = (stepAddr + 1) & 0xffff;
			continue;
		}

		const ATProfileRecord *rec = FindDetailRecord(bankSpace + nextAddr);
		const uint8 opcode = g_sim.DebugGlobalReadByte(bankSpace + nextAddr);

		if (rec)
			nextAddr += ATGetOpcodeLength(opcode, rec->mModeBits << 4, rec->mEmulationMode != 0);
		else
			nextAddr += ATGetOpcodeLength(opcode);

		nextAddr &= 0xffff;
		lines.push_back(bankSpace + stepAddr);

		stepAddr = (stepAddr + 1) & 0xffff;
	}

	if (lines.size() > linesAbove)
		lines.erase(lines.begin(), lines.begin() + (lines.size() - linesAbove));

	while (lines.size() < visibleLines + scrollMarginLines * 2) {
		if (nextAddr != stepAddr && !FindDetailRecord(bankSpace + stepAddr)) {
			stepAddr = (stepAddr + 1) & 0xffff;
			continue;
		}

		const ATProfileRecord *rec = FindDetailRecord(bankSpace + nextAddr);
		const uint8 opcode = g_sim.DebugGlobalReadByte(bankSpace + nextAddr);

		if (rec)
			nextAddr += ATGetOpcodeLength(opcode, rec->mModeBits << 4, rec->mEmulationMode != 0);
		else
			nextAddr += ATGetOpcodeLength(opcode);

		nextAddr &= 0xffff;
		lines.push_back(bankSpace + stepAddr);
		stepAddr = (stepAddr + 1) & 0xffff;
	}
}

void ATImGuiProfileViewPane::ScrollDetailByLines(int delta) {
	vdfastvector<uint32> lines;
	const uint32 visibleLines = 96;
	const uint32 scrollMarginLines = 5;
	BuildDetailLines(lines, visibleLines);

	if (lines.empty())
		return;

	int idx = (int)scrollMarginLines + ((int)visibleLines >> 1) + delta;
	idx = std::clamp(idx, 0, (int)lines.size() - 1);
	mDetailTargetAddress = (mDetailTargetAddress & 0xffff0000) | (lines[idx] & 0xffff);
}

void ATImGuiProfileViewPane::ScrollDetailByPage(bool down) {
	vdfastvector<uint32> lines;
	const uint32 visibleLines = 96;
	const uint32 scrollMarginLines = 5;
	BuildDetailLines(lines, visibleLines);

	if (lines.empty())
		return;

	int idx;
	if (down) {
		idx = std::max<int>((int)lines.size(), (int)scrollMarginLines + 1)
			- ((int)scrollMarginLines + 1);
	} else {
		idx = std::min<int>((int)lines.size(), (int)scrollMarginLines + 1) - 1;
	}

	idx = std::clamp(idx, 0, (int)lines.size() - 1);
	mDetailTargetAddress = (mDetailTargetAddress & 0xffff0000) | (lines[idx] & 0xffff);
}

bool ATImGuiProfileViewPane::IsProfileRecordSelected(uint32 recordId) const {
	return std::find(mSelectedProfileRecordIds.begin(), mSelectedProfileRecordIds.end(),
		recordId) != mSelectedProfileRecordIds.end();
}

void ATImGuiProfileViewPane::SelectProfileRecord(uint32 recordId, int sortedRow) {
	ImGuiIO& io = ImGui::GetIO();

	if (io.KeyShift && mLastSelectedProfileRecordId) {
		int anchorRow = -1;
		for (int i = 0, n = (int)mSortedRecords.size(); i < n; ++i) {
			if (mSortedRecords[i].mRecordId == mLastSelectedProfileRecordId) {
				anchorRow = i;
				break;
			}
		}

		if (anchorRow >= 0) {
			const int first = std::min(anchorRow, sortedRow);
			const int last = std::max(anchorRow, sortedRow);
			mSelectedProfileRecordIds.clear();
			for (int i = first; i <= last; ++i)
				mSelectedProfileRecordIds.push_back(mSortedRecords[i].mRecordId);
			return;
		}
	}

	if (io.KeyCtrl) {
		auto it = std::find(mSelectedProfileRecordIds.begin(),
			mSelectedProfileRecordIds.end(), recordId);
		if (it != mSelectedProfileRecordIds.end()) {
			mSelectedProfileRecordIds.erase(it);
		} else {
			mSelectedProfileRecordIds.push_back(recordId);
			mLastSelectedProfileRecordId = recordId;
		}
		return;
	}

	mSelectedProfileRecordIds.clear();
	mSelectedProfileRecordIds.push_back(recordId);
	mLastSelectedProfileRecordId = recordId;
}

void ATImGuiProfileViewPane::RenderDetailWindow() {
	if (!mbDetailOpen || !mpCurrentFrame)
		return;

	ImGui::SetNextWindowSize(ImVec2(820, 520), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("Altirra Profiler - Detailed View", &mbDetailOpen)) {
		ImGui::End();
		return;
	}

	char addrBuf[16];
	snprintf(addrBuf, sizeof addrBuf, "%04X", mDetailTargetAddress & 0xffff);
	ImGui::SetNextItemWidth(90.0f);
	if (ImGui::InputText("Address", addrBuf, sizeof addrBuf,
			ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_EnterReturnsTrue)) {
		char *end = nullptr;
		const uint32 addr = (uint32)strtoul(addrBuf, &end, 16);
		if (end != addrBuf)
			mDetailTargetAddress = (mDetailTargetAddress & 0xffff0000) | (addr & 0xffff);
	}
	ImGui::SameLine();
	if (ImGui::SmallButton("Up"))
		ScrollDetailByLines(-1);
	ImGui::SameLine();
	if (ImGui::SmallButton("Down"))
		ScrollDetailByLines(1);
	ImGui::SameLine();
	if (ImGui::SmallButton("Page Up"))
		ScrollDetailByPage(false);
	ImGui::SameLine();
	if (ImGui::SmallButton("Page Down"))
		ScrollDetailByPage(true);

	const float lineHeight = ImGui::GetTextLineHeightWithSpacing();
	const float availableHeight = std::max(lineHeight * 8.0f, ImGui::GetContentRegionAvail().y);
	const uint32 visibleLines = std::max<uint32>(1,
		(uint32)(availableHeight / std::max(1.0f, lineHeight)));
	vdfastvector<uint32> lines;
	BuildDetailLines(lines, visibleLines);

	ImGuiTableFlags tableFlags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg
		| ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable
		| ImGuiTableFlags_SizingFixedFit;

	if (ImGui::BeginTable("##ProfileDetail", 6, tableFlags, ImVec2(0, availableHeight))) {
		const bool tableHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
		if (tableHovered) {
			ImGuiIO& io = ImGui::GetIO();
			if (io.MouseWheel != 0.0f)
				ScrollDetailByLines(io.MouseWheel > 0.0f ? -3 : 3);
			if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))
				ScrollDetailByLines(-1);
			if (ImGui::IsKeyPressed(ImGuiKey_DownArrow))
				ScrollDetailByLines(1);
			if (ImGui::IsKeyPressed(ImGuiKey_PageUp))
				ScrollDetailByPage(false);
			if (ImGui::IsKeyPressed(ImGuiKey_PageDown))
				ScrollDetailByPage(true);
		}

		ImGui::TableSetupScrollFreeze(0, 1);
		ImGui::TableSetupColumn("Cycles", ImGuiTableColumnFlags_WidthFixed, 70);
		ImGui::TableSetupColumn("Insns", ImGuiTableColumnFlags_WidthFixed, 60);
		ImGui::TableSetupColumn("CPI", ImGuiTableColumnFlags_WidthFixed, 55);
		ImGui::TableSetupColumn("CCPI", ImGuiTableColumnFlags_WidthFixed, 55);
		ImGui::TableSetupColumn("DMA%", ImGuiTableColumnFlags_WidthFixed, 55);
		ImGui::TableSetupColumn("Text", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableHeadersRow();

		ATCPUHistoryEntry hent;
		ATDisassembleCaptureRegisterContext(hent);
		const ATDebugDisasmMode disasmMode = g_sim.GetCPU().GetDisasmMode();
		IATDebugTarget *target = g_sim.GetDebugTarget();

		for (uint32 addr : lines) {
			const ATProfileRecord *rec = FindDetailRecord(addr);

			hent.mbEmulation = true;
			hent.mP = 0xFF;
			if (rec) {
				hent.mP = (hent.mP & 0xCF) + (rec->mModeBits << 4);
				hent.mbEmulation = rec->mEmulationMode != 0;
			}

			ATDisassembleCaptureInsnContext(addr, hent);

			VDStringA text(" ");
			ATDisassembleInsn(text, target, disasmMode, hent,
				false, false, true, true, true, false, false, true, true, true);

			ImGui::TableNextRow();
			if (addr == mDetailTargetAddress)
				ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,
					ImGui::GetColorU32(ImGuiCol_Header));

			ImGui::TableSetColumnIndex(0);
			if (rec)
				ImGui::Text("%u", rec->mCycles);
			ImGui::TableSetColumnIndex(1);
			if (rec)
				ImGui::Text("%u", rec->mInsns);
			ImGui::TableSetColumnIndex(2);
			if (rec)
				ImGui::Text("%.1f", rec->mInsns ? (float)rec->mCycles / (float)rec->mInsns : 0.0f);
			ImGui::TableSetColumnIndex(3);
			if (rec)
				ImGui::Text("%.1f", rec->mInsns ? (float)rec->mUnhaltedCycles / (float)rec->mInsns : 0.0f);
			ImGui::TableSetColumnIndex(4);
			if (rec)
				ImGui::Text("%.1f%%", rec->mCycles
					? 100.0f * (1.0f - (float)rec->mUnhaltedCycles / (float)rec->mCycles)
					: 0.0f);
			ImGui::TableSetColumnIndex(5);
			ImGui::TextUnformatted(text.c_str());
		}

		ImGui::EndTable();
	}

	ImGui::End();
}

void ATImGuiProfileViewPane::RenderProfileTable() {
	ImGui::Separator();

	if (mpCurrentFrame) {
		ImGui::Text("Total: %u cycles, %u insns",
			mpCurrentFrame->mTotalCycles,
			mpCurrentFrame->mTotalInsns);
	}

	if (mCapturedProfileMode == kATProfileMode_CallGraph) {
		RenderCallGraphTree();
		return;
	}

	RenderProfileSelectionStatus();

	const int baseColumnCount = 10;
	int counterColumns = 0;
	for (auto cm : mProfileSessionCounterModes) {
		if (cm != kATProfileCounterMode_None)
			counterColumns += 2;
	}

	ImGuiTableFlags tableFlags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg
		| ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable
		| ImGuiTableFlags_Sortable | ImGuiTableFlags_SizingFixedFit;

	if (!ImGui::BeginTable("##ProfileRecords", baseColumnCount + counterColumns, tableFlags))
		return;

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

	for (auto cm : mProfileSessionCounterModes) {
		if (cm != kATProfileCounterMode_None) {
			const char *name = GetCounterModeName(cm);
			char buf[64];
			snprintf(buf, sizeof buf, "%s", name);
			ImGui::TableSetupColumn(buf, ImGuiTableColumnFlags_WidthFixed, 70);
			snprintf(buf, sizeof buf, "%s%%", name);
			ImGui::TableSetupColumn(buf, ImGuiTableColumnFlags_WidthFixed, 60);
		}
	}

	ImGui::TableHeadersRow();

	if (ImGuiTableSortSpecs *sortSpecs = ImGui::TableGetSortSpecs()) {
		if (sortSpecs->SpecsDirty && sortSpecs->SpecsCount > 0) {
			const ImGuiTableColumnSortSpecs& spec = sortSpecs->Specs[0];
			const bool ascending = spec.SortDirection == ImGuiSortDirection_Ascending;
			const int sortCol = spec.ColumnIndex;

			std::sort(mSortedRecords.begin(), mSortedRecords.end(),
				[sortCol, ascending](const ProfileTableRecord& a, const ProfileTableRecord& b) {
					int cmp = 0;
					switch (sortCol) {
						case 0: cmp = (a.mContext < b.mContext) ? -1 : (a.mContext > b.mContext) ? 1 : 0; break;
						case 1: cmp = (a.mAddress < b.mAddress) ? -1 : (a.mAddress > b.mAddress) ? 1 : 0; break;
						case 2: cmp = (a.mCalls < b.mCalls) ? -1 : (a.mCalls > b.mCalls) ? 1 : 0; break;
						case 3:
						case 5: cmp = (a.mCycles < b.mCycles) ? -1 : (a.mCycles > b.mCycles) ? 1 : 0; break;
						case 4:
						case 6: cmp = (a.mInsns < b.mInsns) ? -1 : (a.mInsns > b.mInsns) ? 1 : 0; break;
						case 7:
						case 8: cmp = (a.mUnhaltedCycles < b.mUnhaltedCycles) ? -1 : (a.mUnhaltedCycles > b.mUnhaltedCycles) ? 1 : 0; break;
						case 9:
							{
								const uint32 dmaA = a.mCycles > a.mUnhaltedCycles ? a.mCycles - a.mUnhaltedCycles : 0;
								const uint32 dmaB = b.mCycles > b.mUnhaltedCycles ? b.mCycles - b.mUnhaltedCycles : 0;
								const uint64 lhs = (uint64)dmaA * b.mCycles;
								const uint64 rhs = (uint64)dmaB * a.mCycles;
								cmp = (lhs < rhs) ? -1 : (lhs > rhs) ? 1 : 0;
							}
							break;
						case 10:
						case 11: cmp = (a.mCounters[0] < b.mCounters[0]) ? -1 : (a.mCounters[0] > b.mCounters[0]) ? 1 : 0; break;
						case 12:
						case 13: cmp = (a.mCounters[1] < b.mCounters[1]) ? -1 : (a.mCounters[1] > b.mCounters[1]) ? 1 : 0; break;
						default: break;
					}
					return ascending ? cmp < 0 : cmp > 0;
				});

			sortSpecs->SpecsDirty = false;
		}
	}

	const uint32 totalCycles = mpCurrentFrame ? std::max(1u, mpCurrentFrame->mTotalCycles) : 1;
	const uint32 totalInsns = mpCurrentFrame ? std::max(1u, mpCurrentFrame->mTotalInsns) : 1;
	const uint32 totalUnhaltedCycles = mpCurrentFrame ? std::max(1u, mpCurrentFrame->mTotalUnhaltedCycles) : 1;

	ImGuiListClipper clipper;
	clipper.Begin((int)mSortedRecords.size());
	while (clipper.Step()) {
		for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
			const auto& rec = mSortedRecords[row];

			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::PushID(row);
			ImGui::Selectable(GetContextName(rec.mContext),
				IsProfileRecordSelected(rec.mRecordId),
				ImGuiSelectableFlags_SpanAllColumns);
			if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
				SelectProfileRecord(rec.mRecordId, row);
			if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
				OpenDetailForAddress(rec.mAddress);
			ImGui::PopID();

			ImGui::TableSetColumnIndex(1);
			if (IATDebugger *dbg = ATGetDebugger()) {
				VDStringA addrText = dbg->GetAddressText(rec.mAddress, false, true);
				ImGui::TextUnformatted(addrText.c_str());
			} else {
				ImGui::Text("$%04X", rec.mAddress);
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

			int col = baseColumnCount;
			for (int i = 0; i < 2; ++i) {
				if (mProfileSessionCounterModes[i] != kATProfileCounterMode_None) {
					ImGui::TableSetColumnIndex(col++);
					ImGui::Text("%u", rec.mCounters[i]);
					ImGui::TableSetColumnIndex(col++);
					if (rec.mInsns)
						ImGui::Text("%.2f%%", (float)rec.mCounters[i] / (float)rec.mInsns * 100.0f);
				}
			}
		}
	}

	if (ImGui::BeginPopupContextWindow("##ProfileContextMenu", ImGuiPopupFlags_MouseButtonRight)) {
		if (ImGui::MenuItem("Copy As CSV"))
			CopyProfileAsCsv();
		ImGui::EndPopup();
	}

	ImGui::EndTable();
}

void ATImGuiProfileViewPane::RenderProfileSelectionStatus() const {
	if (!mpRecords || !mpCurrentFrame || mSelectedProfileRecordIds.empty()) {
		ImGui::TextUnformatted("");
		return;
	}

	uint32 cycles = 0;
	uint32 insns = 0;
	uint32 count = 0;

	for (uint32 recordId : mSelectedProfileRecordIds) {
		if (!recordId || recordId > mpRecords->size())
			continue;

		const ATProfileRecord& rec = (*mpRecords)[recordId - 1];
		cycles += rec.mCycles;
		insns += rec.mInsns;
		++count;
	}

	if (!count) {
		ImGui::TextUnformatted("");
		return;
	}

	ImGui::Text("Selected %u item%s: %u cycles (%.2f%%), %u insns (%.2f%%)",
		count,
		count == 1 ? "" : "s",
		cycles,
		mpCurrentFrame->mTotalCycles ? (float)cycles * 100.0f / (float)mpCurrentFrame->mTotalCycles : 0.0f,
		insns,
		mpCurrentFrame->mTotalInsns ? (float)insns * 100.0f / (float)mpCurrentFrame->mTotalInsns : 0.0f);
}

void ATImGuiProfileViewPane::RenderCallGraphTree() {
	if (!mpCurrentFrame || !mpMergedFrame || mSession.mContexts.empty()
		|| mpMergedFrame->mCallGraphRecords.empty())
		return;

	const uint32 n = (uint32)mpMergedFrame->mCallGraphRecords.size();
	if (!n)
		return;

	vdfastvector<uint32> nextSibling(n, 0);
	vdfastvector<uint32> firstChild(n, 0);

	for (uint32 i = 4; i < n; ++i) {
		if (i >= mpMergedFrame->mInclusiveRecords.size()
			|| !mpMergedFrame->mInclusiveRecords[i].mInclusiveInsns
			|| i >= mSession.mContexts.size())
			continue;

		const uint32 parent = mSession.mContexts[i].mParent;
		if (parent >= n)
			continue;

		nextSibling[i] = firstChild[parent];
		firstChild[parent] = i;
	}

	const auto cgRecordSorter = [records = mpMergedFrame->mInclusiveRecords.data()](uint32 a, uint32 b) {
		return records[a].mInclusiveCycles > records[b].mInclusiveCycles;
	};

	vdfastvector<uint32> roots;
	for (uint32 i = 0; i < std::min<uint32>(4, n); ++i)
		roots.push_back(i);

	std::sort(roots.begin(), roots.end(), cgRecordSorter);

	ImGuiTableFlags tableFlags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg
		| ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable
		| ImGuiTableFlags_SizingStretchProp;

	if (!ImGui::BeginTable("##ProfileCallGraph", 1, tableFlags))
		return;

	ImGui::TableSetupScrollFreeze(0, 1);
	ImGui::TableSetupColumn("Call Graph");
	ImGui::TableHeadersRow();

	for (uint32 root : roots)
		RenderCallGraphNode(root, firstChild, nextSibling);

	ImGui::EndTable();
}

void ATImGuiProfileViewPane::RenderCallGraphNode(uint32 idx,
	const vdfastvector<uint32>& firstChild,
	const vdfastvector<uint32>& nextSibling) {
	if (!mpMergedFrame || idx >= mpMergedFrame->mCallGraphRecords.size()
		|| idx >= mpMergedFrame->mInclusiveRecords.size())
		return;

	VDStringA label;
	const ATProfileCallGraphRecord& cgr = mpMergedFrame->mCallGraphRecords[idx];

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
			if (idx < mSession.mContexts.size()) {
				const uint32 addr = mSession.mContexts[idx].mAddress;
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

	const float cyclesToPercent = mpMergedFrame->mTotalCycles
		? 100.0f / (float)mpMergedFrame->mTotalCycles : 0.0f;
	const float unhaltedCyclesToPercent = mpMergedFrame->mTotalUnhaltedCycles
		? 100.0f / (float)mpMergedFrame->mTotalUnhaltedCycles : 0.0f;
	const float insnsToPercent = mpMergedFrame->mTotalInsns
		? 100.0f / (float)mpMergedFrame->mTotalInsns : 0.0f;
	const ATProfileCallGraphInclusiveRecord& cgir = mpMergedFrame->mInclusiveRecords[idx];

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
		[records = mpMergedFrame->mInclusiveRecords.data()](uint32 a, uint32 b) {
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
	if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
		if (idx < mSession.mContexts.size())
			OpenDetailForAddress(mSession.mContexts[idx].mAddress);
	}

	if (open && !children.empty()) {
		for (uint32 child : children)
			RenderCallGraphNode(child, firstChild, nextSibling);

		ImGui::TreePop();
	}
}

} // namespace

void ATUIDebuggerEnsureProfileViewPane() {
	if (!ATUIDebuggerGetPane(kATUIPaneId_Profiler)) {
		auto *pane = new ATImGuiProfileViewPane();
		ATUIDebuggerRegisterPane(pane);
	}
}
