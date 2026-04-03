//	Altirra SDL3 frontend - Win32 and excluded-file stubs
//	Provides null/stub implementations of symbols from files that are
//	excluded from the Linux build.

#include <stdafx.h>
#include <vd2/system/vdtypes.h>
#include <vd2/system/VDString.h>
#include <vd2/system/refcount.h>
#include <vd2/system/vdalloc.h>
#include <vd2/system/function.h>
#include <vd2/system/registry.h>
#include <vd2/Kasumi/pixmap.h>
#include <at/atcore/asyncdispatcher.h>
#include <at/atcore/timerservice.h>
#include <at/atdebugger/target.h>
#include "debugger.h"
#include "oshelper.h"
#include "asyncdownloader.h"
#include "uiaccessors.h"
#include "settings.h"

// ============================================================
// oshelper.h stubs
// ============================================================

void ATLoadFrame(VDPixmapBuffer& px, const wchar_t *filename) {}
void ATLoadFrameFromMemory(VDPixmapBuffer& px, const void *mem, size_t len) {}
void ATCopyTextToClipboard(void *hwnd, const char *s) {}
void ATCopyTextToClipboard(void *hwnd, const wchar_t *s) {}
void ATLaunchURL(const wchar_t *url) {}

// debugger.h — debugger.cpp is now compiled; ATGetDebugger()/ATGetDebuggerSymbolLookup() are real.

// ATUISaveFrame — defined in main.cpp (Windows), used by debuggerautotest.cpp
bool ATUISaveFrame(const wchar_t *path) { return false; }

// settings.h — settings.cpp is now compiled; register/unregister are real.

// ============================================================
// asyncdownloader stubs
// ============================================================

void ATAsyncDownloadUrl(const wchar_t *url, const wchar_t *userAgent, size_t maxLen,
	vdfunction<void(const void *, size_t)> fn, IATAsyncDownloader **downloader) {
	if (downloader) *downloader = nullptr;
}

// UI accessor stubs are now in uiaccessors_stubs.cpp.

// ============================================================
// ATTimerService stub (timerserviceimpl_win32.cpp excluded)
// ============================================================

class ATTimerServiceStub final : public IATTimerService {
public:
	void Request(uint64 *token, float delay, vdfunction<void()> fn) override {
		if (token) *token = 0;
	}
	void Cancel(uint64 *token) override {
		if (token) *token = 0;
	}
};

IATTimerService *ATCreateTimerService(IATAsyncDispatcher&) {
	return new ATTimerServiceStub();
}

// ============================================================
// ATGetNameForWindowMessageW32 stub
// ============================================================

const char *ATGetNameForWindowMessageW32(uint32 msgId) { return "(unknown)"; }

// ============================================================
// UI dialog/clipboard stubs (Win32 UI excluded)
// ============================================================
#include "uiclipboard.h"
#include "uicommondialogs.h"
#include "uiaccessors.h"
#include "devicemanager.h"

bool ATUIClipGetText(VDStringW& s) { return false; }
bool ATUIIsElevationRequiredForMountVHDImage() { return false; }
void ATUIShowWarning(VDGUIHandle, const wchar_t*, const wchar_t*) {}
bool ATUIShowWarningConfirm(VDGUIHandle, const wchar_t*, const wchar_t*) { return false; }
// ATUISwitchHardwareMode is now in uiaccessors_stubs.cpp
bool ATUISwitchKernel(VDGUIHandle, uint64) { return false; }
void ATUITemporarilyMountVHDImageW32(VDGUIHandle, const wchar_t*, bool) {}
bool ATUpdateVerifyFeedSignature(const void*, const void*, size_t) { return false; }

// ATRegisterDeviceConfigurers: device UI configurers not needed for Phase 5
void ATRegisterDeviceConfigurers(ATDeviceManager&) {}

// ============================================================
// VDUIGetAcceleratorString stub (Dita/accel not linked)
// ============================================================
#include <vd2/Dita/accel.h>

void VDUIGetAcceleratorString(const VDUIAccelerator&, VDStringW& s) { s.clear(); }

// ============================================================
// ATUIShowDialogDiskExplorer / ATUIShowGenericDialog stubs
// ============================================================
#include <at/atnativeui/genericdialog.h>
#include <at/atcore/blockdevice.h>

void ATUIShowDialogDiskExplorer(VDGUIHandle, IATBlockDevice*, const wchar_t*) {}
ATUIGenericResult ATUIShowGenericDialog(const ATUIGenericDialogOptions&) { return kATUIGenericResult_Cancel; }

// Test mode symbols are now provided by ui_testmode.cpp (full implementation).
