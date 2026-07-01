#include <stdafx.h>

#include <atomic>
#include <mutex>
#include <string>

#include <vd2/system/VDString.h>
#include <vd2/system/refcount.h>
#include <vd2/system/vectors.h>

#include "ui_main.h"
#include "uirender.h"

class IATUIDebuggerConsoleWindow;
struct SDL_Window;
struct ATDebuggerSourceFileInfo;
class IATSourceWindow;

std::mutex g_consoleMutex;
std::string g_consoleText;
std::atomic<bool> g_consoleTextDirty{false};
std::atomic<bool> g_consoleScrollToBottom{false};
IATUIDebuggerConsoleWindow *g_pConsoleWindow = nullptr;
std::mutex g_saveFrameMutex;
VDStringA g_saveFramePath;
SDL_Window *g_pWindow = nullptr;

void ATBridgeRequestAppQuit() {}

void ATUIDebuggerOpen() {}
void ATUIDebuggerClose() {}
bool ATUIDebuggerIsOpen() { return false; }
void ATUIDebuggerShowSourceListDialog() {}
void ATUIDebuggerClosePaneById(unsigned int) {}
bool ATUIDebuggerHistoryPaneJumpToCycle(uint32) { return false; }

IATSourceWindow *ATImGuiFindSourceWindow(const wchar_t *) { return nullptr; }
IATSourceWindow *ATImGuiOpenSourceWindow(const wchar_t *) { return nullptr; }

void ATUISetPanZoomToolActive(bool) {}
void ATAdaptiveInput_ApplyAfterHardwareSwitch() {}
void ATUIPushDeferred(ATDeferredActionType, const char *, int) {}
void ATSetFullscreen(bool) {}

extern "C" void ATLibretroSetDiskLedState(uint32 index, bool active);

namespace {
class ATLibretroNullUIRenderer final : public IATUIRenderer {
public:
	int AddRef() override { return mRefCount.inc(); }
	int Release() override {
		const int n = mRefCount.dec();
		if (!n)
			delete this;
		return n;
	}

	bool IsVisible() const override { return false; }
	void SetVisible(bool) override {}
	void SetCyclesPerSecond(double) override {}
	void SetLedStatus(uint8) override {}
	void SetHeldButtonStatus(uint8) override {}
	void SetPendingHoldMode(bool) override {}
	void SetPendingHeldKey(int) override {}
	void SetPendingHeldButtons(uint8) override {}
	void ClearWatchedValue(int) override {}
	void SetWatchedValue(int, uint32, WatchFormat) override {}
	void SetTracingSize(sint64) override {}
	void SetAudioStatus(const ATUIAudioStatus*) override {}
	void SetAudioMonitor(uint32, ATAudioMonitor*) override {}
	void SetAudioDisplayEnabled(uint32, bool) override {}
	void SetAudioScopeEnabled(bool) override {}
	void SetSlightSID(ATSlightSIDEmulator*) override {}
	vdrect32 GetPadArea() const override { return vdrect32(0, 0, 0, 0); }
	void SetPadInputEnabled(bool) override {}
	void SetFpsIndicator(float) override {}
	void SetMessage(StatusPriority, const wchar_t*) override {}
	void ClearMessage(StatusPriority) override {}
	void SetHoverTip(int, int, const wchar_t*) override {}
	void SetPaused(bool) override {}
	void SetUIManager(ATUIManager*) override {}
	void Relayout(int, int) override {}
	void Update() override {}
	sint32 GetIndicatorSafeHeight() const override { return 0; }
	void AddIndicatorSafeHeightChangedHandler(const vdfunction<void()>*) override {}
	void RemoveIndicatorSafeHeightChangedHandler(const vdfunction<void()>*) override {}
	void BeginCustomization() override {}

	void SetStatusFlags(uint32 flags) override {
		mDiskActivityFlags |= flags & kDiskLedMask;
		UpdateDiskLeds(flags);
	}
	void ResetStatusFlags(uint32 flags, uint32) override {
		mDiskActivityFlags &= ~(flags & kDiskLedMask);
		UpdateDiskLeds(flags);
	}
	void PulseStatusFlags(uint32 flags) override {
		mDiskActivityFlags |= flags & kDiskLedMask;
		UpdateDiskLeds(flags);
	}
	void SetStatusCounter(uint32, uint32) override {}
	void SetDiskLEDState(uint32 index, sint32 ledDisplay) override {
		if (index < kDiskLedCount) {
			if (ledDisplay >= 0)
				mDiskReadoutFlags |= (1u << index);
			else
				mDiskReadoutFlags &= ~(1u << index);
			UpdateDiskLed(index);
		}
	}
	void SetDiskMotorActivity(uint32 index, bool on) override {
		if (index < kDiskLedCount) {
			if (on)
				mDiskMotorFlags |= (1u << index);
			else
				mDiskMotorFlags &= ~(1u << index);
			UpdateDiskLed(index);
		}
	}
	void SetDiskErrorState(uint32, bool) override {}
	void SetHActivity(bool) override {}
	void SetIDEActivity(bool, uint32) override {}
	void SetPCLinkActivity(bool) override {}
	void SetFlashWriteActivity() override {}
	void SetCartridgeActivity(sint32, sint32) override {}
	void SetCassetteIndicatorVisible(bool) override {}
	void SetCassettePosition(float, float, bool, bool) override {}
	void SetRecordingPosition() override {}
	void SetRecordingPositionPaused() override {}
	void SetRecordingPosition(float, sint64, bool) override {}
	void SetModemConnection(const char*) override {}
	void SetStatusMessage(const wchar_t*) override {}
	uint32 AllocateErrorSourceId() override { return 0; }
	void ClearErrors(uint32) override {}
	void ReportError(uint32, const wchar_t*) override {}

private:
	static constexpr uint32 kDiskLedCount = 15;
	static constexpr uint32 kDiskLedMask = (1u << kDiskLedCount) - 1;

	void UpdateDiskLed(uint32 index) {
		const uint32 bit = 1u << index;
		ATLibretroSetDiskLedState(index,
			((mDiskActivityFlags | mDiskMotorFlags | mDiskReadoutFlags) & bit) != 0);
	}

	void UpdateDiskLeds(uint32 flags) {
		flags &= kDiskLedMask;
		for (uint32 index = 0; index < kDiskLedCount; ++index) {
			if (flags & (1u << index))
				UpdateDiskLed(index);
		}
	}

	VDAtomicInt mRefCount{0};
	uint32 mDiskActivityFlags = 0;
	uint32 mDiskMotorFlags = 0;
	uint32 mDiskReadoutFlags = 0;
};
}

void ATCreateUIRenderer(IATUIRenderer **r) {
	if (!r)
		return;

	*r = new ATLibretroNullUIRenderer;
	(*r)->AddRef();
}

void ATActivateUIPane(unsigned int, bool, bool, unsigned int, int) {}

#include <at/atui/uicommandmanager.h>

ATUICommandManager g_ATUICommandMgr;

ATUICommandManager& ATUIGetCommandManager() {
	return g_ATUICommandMgr;
}
