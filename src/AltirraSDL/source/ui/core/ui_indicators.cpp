//	AltirraSDL - HUD indicator renderer
//	Implements IATUIRenderer with ImGui overlay rendering.
//	Replaces the null renderer in uirender_stubs.cpp.
//
//	Renders: drive activity LEDs, cassette position, status messages,
//	FPS counter, pause overlay, recording indicator, error messages,
//	held key/button indicators, audio scope.

#include <stdafx.h>
#include <SDL3/SDL.h>
#include <imgui.h>
#include <vd2/system/vdtypes.h>
#include <vd2/system/refcount.h>
#include <vd2/system/vdstl.h>
#include <vd2/system/VDString.h>
#include <vd2/system/text.h>
#include <at/ataudio/audiooutput.h>
#include "uirender.h"
#include "ui_main.h"

// =========================================================================
// ATUIRendererImGui — real implementation with HUD overlay rendering
// =========================================================================

class ATUIRendererImGui final : public IATUIRenderer {
	int mRefCount = 1;
public:
	int AddRef() override { return ++mRefCount; }
	int Release() override { int n = --mRefCount; if (!n) delete this; return n; }

	// --- IATDeviceIndicatorManager ---
	void SetStatusFlags(uint32 flags) override { mStatusFlags |= flags; }
	void ResetStatusFlags(uint32 mask, uint32 flags) override { mStatusFlags = (mStatusFlags & ~mask) | (flags & mask); }
	void PulseStatusFlags(uint32 flags) override { mPulseFlags |= flags; }
	void SetStatusCounter(uint32 idx, uint32 val) override {
		if (idx < 8) mStatusCounters[idx] = val;
	}

	void SetDiskLEDState(uint32 drive, sint32 state) override {
		if (drive < 15) mDiskLED[drive] = state;
	}
	void SetDiskMotorActivity(uint32 drive, bool active) override {
		if (drive < 15) mDiskMotor[drive] = active;
	}
	void SetDiskErrorState(uint32 drive, bool error) override {
		if (drive < 15) mDiskError[drive] = error;
	}

	void SetHActivity(bool active) override { mbHActive = active; }
	void SetIDEActivity(bool active, uint32) override { mbIDEActive = active; }
	void SetPCLinkActivity(bool active) override { mbPCLinkActive = active; }
	void SetFlashWriteActivity() override { mbFlashWrite = true; }
	void SetCartridgeActivity(sint32, sint32) override {}

	void SetCassetteIndicatorVisible(bool vis) override { mbCassetteVisible = vis; }
	void SetCassettePosition(float pos, float len, bool playing, bool recording) override {
		mCassettePos = pos; mCassetteLen = len;
		mbCassettePlaying = playing; mbCassetteRecording = recording;
	}

	void SetRecordingPosition() override { mbRecording = false; }
	void SetRecordingPositionPaused() override { mbRecordingPaused = true; }
	void SetRecordingPosition(float pos, sint64 size, bool video) override {
		mbRecording = true; mbRecordingPaused = false;
		mRecordPos = pos; mRecordSize = size; mbRecordVideo = video;
	}

	void SetModemConnection(const char *s) override {
		mModemStatus = s ? s : "";
	}

	void SetStatusMessage(const wchar_t *s) override {
		mStatusMessage = s ? VDTextWToU8(VDStringW(s)) : VDStringA();
	}

	uint32 AllocateErrorSourceId() override { return ++mNextErrorSourceId; }
	void ClearErrors(uint32 sourceId) override {
		if (mErrorSourceId == sourceId) mErrorMessage.clear();
	}
	void ReportError(uint32 sourceId, const wchar_t *msg) override {
		mErrorSourceId = sourceId;
		mErrorMessage = msg ? VDTextWToU8(VDStringW(msg)) : VDStringA();
	}

	// --- IATUIRenderer ---
	bool IsVisible() const override { return true; }
	void SetVisible(bool) override {}
	void SetCyclesPerSecond(double cps) override { mCyclesPerSec = cps; }
	void SetLedStatus(uint8 v) override { mLedStatus = v; }
	void SetHeldButtonStatus(uint8 v) override { mHeldButtons = v; }
	void SetPendingHoldMode(bool v) override { mbPendingHold = v; }
	void SetPendingHeldKey(int k) override { mPendingKey = k; }
	void SetPendingHeldButtons(uint8 v) override { mPendingButtons = v; }
	void ClearWatchedValue(int idx) override { if (idx >= 0 && idx < 8) mWatchValid[idx] = false; }
	void SetWatchedValue(int idx, uint32 v, WatchFormat fmt) override {
		if (idx >= 0 && idx < 8) { mWatchValue[idx] = v; mWatchFmt[idx] = fmt; mWatchValid[idx] = true; }
	}
	void SetTracingSize(sint64 s) override { mTracingSize = s; }
	void SetAudioStatus(const ATUIAudioStatus *s) override { mAudioStatus = s ? *s : ATUIAudioStatus{}; }
	void SetAudioMonitor(bool enable, ATAudioMonitor *mon) override { mbAudioMon = enable; mpAudioMon = mon; }
	void SetAudioDisplayEnabled(bool vis, bool scope) override { mbAudioVis = vis; mbAudioScope = scope; }
	void SetAudioScopeEnabled(bool v) override { mbAudioScope = v; }
	void SetSlightSID(ATSlightSIDEmulator *s) override { mpSlightSID = s; }
	vdrect32 GetPadArea() const override { return {}; }
	void SetPadInputEnabled(bool) override {}
	void SetFpsIndicator(float fps) override { mFps = fps; }
	void SetMessage(StatusPriority pri, const wchar_t *msg) override {
		int idx = (int)pri;
		if (idx >= 0 && idx < 4)
			mMessages[idx] = msg ? VDTextWToU8(VDStringW(msg)) : VDStringA();
	}
	void ClearMessage(StatusPriority pri) override {
		int idx = (int)pri;
		if (idx >= 0 && idx < 4) mMessages[idx].clear();
	}
	void SetHoverTip(int x, int y, const wchar_t *tip) override {}
	void SetPaused(bool v) override { mbPaused = v; }
	void SetUIManager(ATUIManager *mgr) override {}
	void Relayout(int w, int h) override { mWidth = w; mHeight = h; }
	void Update() override {}
	sint32 GetIndicatorSafeHeight() const override { return mbShowIndicators ? 20 : 0; }
	void AddIndicatorSafeHeightChangedHandler(const vdfunction<void()> *h) override {}
	void RemoveIndicatorSafeHeightChangedHandler(const vdfunction<void()> *h) override {}
	void BeginCustomization() override {}

	// --- Rendering (called from ui_main.cpp) ---
	void RenderOverlay();

private:
	// Disk state
	sint32 mDiskLED[15] = {};
	bool mDiskMotor[15] = {};
	bool mDiskError[15] = {};

	// Activity indicators
	bool mbHActive = false;
	bool mbIDEActive = false;
	bool mbPCLinkActive = false;
	bool mbFlashWrite = false;

	// Cassette
	bool mbCassetteVisible = false;
	float mCassettePos = 0, mCassetteLen = 0;
	bool mbCassettePlaying = false, mbCassetteRecording = false;

	// Recording
	bool mbRecording = false, mbRecordingPaused = false, mbRecordVideo = false;
	float mRecordPos = 0;
	sint64 mRecordSize = 0;

	// Status
	uint32 mStatusFlags = 0;
	uint32 mPulseFlags = 0;
	uint32 mStatusCounters[8] = {};
	VDStringA mStatusMessage;
	VDStringA mModemStatus;
	VDStringA mErrorMessage;
	uint32 mErrorSourceId = 0;
	uint32 mNextErrorSourceId = 0;

	// HUD
	float mFps = 0;
	bool mbPaused = false;
	double mCyclesPerSec = 0;
	uint8 mLedStatus = 0;
	uint8 mHeldButtons = 0;
	bool mbPendingHold = false;
	int mPendingKey = -1;
	uint8 mPendingButtons = 0;
	sint64 mTracingSize = -1;
	bool mbShowIndicators = true;

	// Watch (8 slots matching ATDebugger::mWatches)
	uint32 mWatchValue[8] = {};
	WatchFormat mWatchFmt[8] = {};
	bool mWatchValid[8] = {};

	// Audio
	ATUIAudioStatus mAudioStatus {};
	bool mbAudioMon = false, mbAudioVis = false, mbAudioScope = false;
	ATAudioMonitor *mpAudioMon = nullptr;
	ATSlightSIDEmulator *mpSlightSID = nullptr;

	// Messages (indexed by StatusPriority)
	VDStringA mMessages[4];

	int mWidth = 0, mHeight = 0;
};

void ATUIRendererImGui::RenderOverlay() {
	ImDrawList *dl = ImGui::GetForegroundDrawList();
	if (!dl) return;

	const ATHudSettings& hud = ATUIGetHudSettings();
	const ImVec2 vp = ImGui::GetMainViewport()->Size;
	const float margin = 8.0f;
	float y = vp.y - margin;

	// --- Status bar at bottom ---
	float ledX = margin;

	// Drive LEDs
	if (hud.showDiskLEDs) {
		for (int i = 0; i < 8; ++i) {
			if (!mDiskMotor[i] && mDiskLED[i] == 0)
				continue;

			char label[8];
			snprintf(label, sizeof(label), "D%d:", i + 1);

			ImU32 color;
			if (mDiskError[i])
				color = IM_COL32(255, 60, 60, 255);    // red for error
			else if (mDiskLED[i] > 0)
				color = IM_COL32(0, 200, 0, 255);       // green for read
			else if (mDiskLED[i] < 0)
				color = IM_COL32(255, 180, 0, 255);     // orange for write
			else
				color = IM_COL32(80, 80, 80, 255);       // dim for motor only

			dl->AddRectFilled(ImVec2(ledX, y - 14), ImVec2(ledX + 8, y - 6), color);
			dl->AddText(ImVec2(ledX + 10, y - 16), IM_COL32(200, 200, 200, 255), label);
			ledX += 40;
		}
	}

	// H: / IDE / PCLink activity
	if (hud.showHActivity) {
		if (mbHActive) {
			dl->AddText(ImVec2(ledX, y - 16), IM_COL32(0, 200, 0, 255), "H:");
			ledX += 24;
		}
		if (mbIDEActive) {
			dl->AddText(ImVec2(ledX, y - 16), IM_COL32(0, 200, 0, 255), "IDE");
			ledX += 32;
		}
	}

	// Cassette position
	if (hud.showCassette && mbCassetteVisible && mCassetteLen > 0) {
		int posMin = (int)(mCassettePos / 60.0f);
		int posSec = (int)mCassettePos % 60;
		int lenMin = (int)(mCassetteLen / 60.0f);
		int lenSec = (int)mCassetteLen % 60;
		char buf[64];
		snprintf(buf, sizeof(buf), "C: %d:%02d/%d:%02d%s", posMin, posSec, lenMin, lenSec,
			mbCassetteRecording ? " REC" : mbCassettePlaying ? " PLAY" : "");
		dl->AddText(ImVec2(ledX, y - 16), IM_COL32(200, 200, 200, 255), buf);
		ledX += 120;
	}

	// Recording indicator
	if (hud.showRecording && mbRecording) {
		const char *recText = mbRecordingPaused ? "REC PAUSED" : (mbRecordVideo ? "REC VIDEO" : "REC AUDIO");
		dl->AddText(ImVec2(ledX, y - 16), IM_COL32(255, 60, 60, 255), recText);
		ledX += 90;
	}

	// Status message (centered at bottom)
	if (hud.showStatusMessage && !mStatusMessage.empty()) {
		ImVec2 sz = ImGui::CalcTextSize(mStatusMessage.c_str());
		dl->AddText(ImVec2((vp.x - sz.x) * 0.5f, y - 16),
			IM_COL32(255, 255, 200, 255), mStatusMessage.c_str());
	}

	// Error message (red, above status)
	if (hud.showErrors && !mErrorMessage.empty()) {
		ImVec2 sz = ImGui::CalcTextSize(mErrorMessage.c_str());
		dl->AddText(ImVec2((vp.x - sz.x) * 0.5f, y - 34),
			IM_COL32(255, 80, 80, 255), mErrorMessage.c_str());
	}

	// On-screen watches (top-left, below any other HUD)
	if (hud.showWatches) {
		float watchY = margin;
		for (int i = 0; i < 8; i++) {
			if (!mWatchValid[i]) continue;
			char buf[32];
			switch (mWatchFmt[i]) {
				case WatchFormat::Hex8:
					snprintf(buf, sizeof(buf), "W%d: $%02X", i, mWatchValue[i]); break;
				case WatchFormat::Hex16:
					snprintf(buf, sizeof(buf), "W%d: $%04X", i, mWatchValue[i]); break;
				case WatchFormat::Hex32:
					snprintf(buf, sizeof(buf), "W%d: $%08X", i, mWatchValue[i]); break;
				case WatchFormat::Dec:
					snprintf(buf, sizeof(buf), "W%d: %d", i, mWatchValue[i]); break;
				default: continue;
			}
			ImVec2 sz = ImGui::CalcTextSize(buf);
			dl->AddRectFilled(ImVec2(margin - 2, watchY - 1),
				ImVec2(margin + sz.x + 2, watchY + sz.y + 1),
				IM_COL32(0, 0, 0, 140));
			dl->AddText(ImVec2(margin, watchY),
				IM_COL32(0, 255, 200, 255), buf);
			watchY += sz.y + 4;
		}
	}

	// FPS (top-right)
	if (hud.showFPS && mFps > 0) {
		char buf[32];
		snprintf(buf, sizeof(buf), "%.1f fps", mFps);
		ImVec2 sz = ImGui::CalcTextSize(buf);
		dl->AddText(ImVec2(vp.x - sz.x - margin, margin),
			IM_COL32(200, 200, 200, 180), buf);
	}

	// Pause overlay — positioned near the bottom of the viewport so
	// it doesn't cover the Atari display.  Use a font-height-based
	// margin (instead of a raw pixel constant) so the overlay sits
	// at the same visual distance from the bottom on a desktop
	// monitor and a 3x-density Android phone, and stays clear of
	// the typical Android navigation bar inset on mobile.
	if (hud.showPauseOverlay && mbPaused) {
		const char *pauseText = "PAUSED";
		ImVec2 sz = ImGui::CalcTextSize(pauseText);
		float px = (vp.x - sz.x) * 0.5f;
		float bottomMargin = sz.y * 3.0f + 16.0f;
		float py = vp.y - sz.y - bottomMargin;
		if (py < 0.0f) py = 0.0f;
		dl->AddRectFilled(ImVec2(px - 8, py - 4), ImVec2(px + sz.x + 8, py + sz.y + 4),
			IM_COL32(0, 0, 0, 160));
		dl->AddText(ImVec2(px, py), IM_COL32(255, 255, 255, 255), pauseText);
	}

	// Clear pulse flags after rendering
	mPulseFlags = 0;
	mbFlashWrite = false;
}

// =========================================================================
// Global renderer instance — accessed from ui_main.cpp for overlay rendering
// =========================================================================

static ATUIRendererImGui *g_pRendererImGui = nullptr;

void ATCreateUIRenderer(IATUIRenderer **r) {
	auto *renderer = new ATUIRendererImGui();
	g_pRendererImGui = renderer;
	*r = renderer;
}

void ATUIRenderHUDOverlay() {
	if (g_pRendererImGui)
		g_pRendererImGui->RenderOverlay();
}

// =========================================================================
// Fullscreen notification — fading "Press Alt+Enter to exit full screen"
// =========================================================================

static uint64_t g_fullscreenNotifyStartTick = 0;
static const float kFullscreenNotifyDuration = 3.0f;  // seconds visible
static const float kFullscreenNotifyFade     = 1.0f;  // seconds to fade out

void ATUIShowFullscreenNotification() {
	g_fullscreenNotifyStartTick = SDL_GetTicks();
}

void ATUIRenderFullscreenNotification() {
	if (g_fullscreenNotifyStartTick == 0)
		return;

	float elapsed = (float)(SDL_GetTicks() - g_fullscreenNotifyStartTick) / 1000.0f;
	float totalDur = kFullscreenNotifyDuration + kFullscreenNotifyFade;
	if (elapsed >= totalDur) {
		g_fullscreenNotifyStartTick = 0;
		return;
	}

	float alpha = 1.0f;
	if (elapsed > kFullscreenNotifyDuration)
		alpha = 1.0f - (elapsed - kFullscreenNotifyDuration) / kFullscreenNotifyFade;

	ImDrawList *dl = ImGui::GetForegroundDrawList();
	if (!dl) return;

	const char *text = "Press Alt+Enter to exit full screen";
	const ImVec2 vp = ImGui::GetMainViewport()->Size;
	ImVec2 sz = ImGui::CalcTextSize(text);

	float px = (vp.x - sz.x) * 0.5f;
	float py = vp.y * 0.15f;
	float pad = 10.0f;

	uint8_t bgA = (uint8_t)(140 * alpha);
	uint8_t fgA = (uint8_t)(255 * alpha);

	dl->AddRectFilled(
		ImVec2(px - pad, py - pad / 2),
		ImVec2(px + sz.x + pad, py + sz.y + pad / 2),
		IM_COL32(0, 0, 0, bgA), 4.0f);
	dl->AddText(ImVec2(px, py), IM_COL32(255, 255, 255, fgA), text);
}
