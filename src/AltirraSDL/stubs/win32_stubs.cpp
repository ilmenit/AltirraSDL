//	Altirra SDL3 frontend - Win32 and excluded-file stubs
//	Provides null/stub implementations of symbols from files that are
//	excluded from the Linux build.

#include <stdafx.h>
#include <SDL3/SDL.h>
#include <mutex>
#include <vector>
#include <vd2/system/vdtypes.h>
#include <vd2/system/VDString.h>
#include <vd2/system/refcount.h>
#include <vd2/system/vdalloc.h>
#include <vd2/system/function.h>
#include <vd2/system/registry.h>
#include <vd2/system/text.h>
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
// oshelper.h — SDL3 implementations
// ============================================================
//
// ATLoadFrame, ATLoadFrameFromMemory, ATLoadKernelResource*,
// ATLoadImageResource, ATLoadMiscResource, ATLockResource, ATSaveFrame,
// ATFileSetReadOnlyAttribute, ATGenerateGuid, and the
// ATProcessEfficiencyMode enum table now live in
// source/os/oshelper_sdl3.cpp. Only the small clipboard/URL helpers that
// predate that file remain here.

void ATCopyTextToClipboard(void *hwnd, const char *s) {
	if (s && *s) SDL_SetClipboardText(s);
}

void ATCopyTextToClipboard(void *hwnd, const wchar_t *s) {
	if (s && *s) {
		VDStringA u8 = VDTextWToU8(VDStringW(s));
		SDL_SetClipboardText(u8.c_str());
	}
}

void ATLaunchURL(const wchar_t *url) {
	if (url && *url) {
		VDStringA u8 = VDTextWToU8(VDStringW(url));
		SDL_OpenURL(u8.c_str());
	}
}

// debugger.h — debugger.cpp is now compiled; ATGetDebugger()/ATGetDebuggerSymbolLookup() are real.

// ATUISaveFrame — queues frame save for next render cycle.
// The actual save happens in ui_main.cpp when g_saveFramePath is non-empty.
bool ATUISaveFrame(const wchar_t *path) {
	if (!path || !*path) return false;
	extern std::mutex g_saveFrameMutex;
	extern VDStringA g_saveFramePath;
	VDStringA u8 = VDTextWToU8(VDStringW(path));
	std::lock_guard<std::mutex> lock(g_saveFrameMutex);
	g_saveFramePath = u8.c_str();
	return true;
}

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
// ATTimerService — SDL3 implementation
// ============================================================

class ATTimerServiceSDL3 final : public IATTimerService {
public:
	ATTimerServiceSDL3(IATAsyncDispatcher& disp) : mpDispatcher(&disp) {}

	~ATTimerServiceSDL3() {
		for (auto& s : mSlots) {
			if (s.timerId)
				SDL_RemoveTimer(s.timerId);
		}
	}

	void Request(uint64 *token, float delay, vdfunction<void()> fn) override {
		if (!token) return;
		if (*token) Cancel(token);

		size_t idx = mSlots.size();
		for (size_t i = 0; i < mSlots.size(); ++i) {
			if (!mSlots[i].fn) { idx = i; break; }
		}
		if (idx == mSlots.size()) mSlots.push_back({});

		auto& s = mSlots[idx];
		s.fn = std::move(fn);
		s.pSelf = this;
		s.index = idx;

		uint32 ms = std::max(1u, (uint32)(delay * 1000.0f));
		s.timerId = SDL_AddTimer(ms, TimerCB, &s);
		*token = (uint64)(idx + 1);
	}

	void Cancel(uint64 *token) override {
		if (!token || !*token) return;
		size_t idx = (size_t)(*token - 1);
		if (idx < mSlots.size()) {
			auto& s = mSlots[idx];
			if (s.timerId) { SDL_RemoveTimer(s.timerId); s.timerId = 0; }
			s.fn = nullptr;
		}
		*token = 0;
	}

private:
	struct Slot {
		SDL_TimerID timerId = 0;
		vdfunction<void()> fn;
		ATTimerServiceSDL3 *pSelf = nullptr;
		size_t index = 0;
	};

	static uint32 SDLCALL TimerCB(void *ud, SDL_TimerID, uint32) {
		auto *s = static_cast<Slot *>(ud);
		if (s && s->pSelf && s->fn) {
			vdfunction<void()> fn = std::move(s->fn);
			s->timerId = 0;
			s->pSelf->mpDispatcher->Queue(&s->pSelf->mDispToken,
				[fn = std::move(fn)]() { fn(); });
		}
		return 0;
	}

	IATAsyncDispatcher *mpDispatcher;
	uint64 mDispToken = 0;
	std::vector<Slot> mSlots;
};

IATTimerService *ATCreateTimerService(IATAsyncDispatcher& disp) {
	return new ATTimerServiceSDL3(disp);
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

bool ATUIClipIsTextAvailable() {
	return SDL_HasClipboardText();
}

bool ATUIClipGetText(VDStringA& s8, VDStringW& s16, bool& use16) {
	// SDL3 only exposes clipboard text as UTF-8, so we always take the
	// "unicode preferred" path that Windows takes for CF_UNICODETEXT.
	if (!SDL_HasClipboardText())
		return false;

	char *t = SDL_GetClipboardText();
	if (!t) return false;
	if (!*t) { SDL_free(t); return false; }

	s16 = VDTextU8ToW(VDStringA(t));
	SDL_free(t);

	// Mirror Windows: strip at first embedded NUL.
	auto nullPos = s16.find(L'\0');
	if (nullPos != s16.npos)
		s16.erase(nullPos);

	use16 = true;
	return true;
}

bool ATUIClipGetText(VDStringW& s) {
	if (!SDL_HasClipboardText()) return false;
	char *t = SDL_GetClipboardText();
	if (!t || !*t) { SDL_free(t); return false; }
	s = VDTextU8ToW(VDStringA(t));
	SDL_free(t);

	auto nullPos = s.find(L'\0');
	if (nullPos != s.npos)
		s.erase(nullPos);
	return true;
}

bool ATUIIsElevationRequiredForMountVHDImage() { return false; }

void ATUIShowWarning(VDGUIHandle, const wchar_t *caption, const wchar_t *msg) {
	VDStringA c = VDTextWToU8(VDStringW(caption ? caption : L"Warning"));
	VDStringA m = VDTextWToU8(VDStringW(msg ? msg : L""));
	SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING, c.c_str(), m.c_str(), nullptr);
}

bool ATUIShowWarningConfirm(VDGUIHandle, const wchar_t *caption, const wchar_t *msg) {
	VDStringA c = VDTextWToU8(VDStringW(caption ? caption : L"Confirm"));
	VDStringA m = VDTextWToU8(VDStringW(msg ? msg : L""));
	const SDL_MessageBoxButtonData btns[] = {
		{ SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Yes" },
		{ SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "No" },
	};
	SDL_MessageBoxData d = { SDL_MESSAGEBOX_WARNING, nullptr, c.c_str(), m.c_str(), 2, btns, nullptr };
	int id = 0;
	SDL_ShowMessageBox(&d, &id);
	return id == 1;
}
// ATUISwitchHardwareMode and ATUISwitchKernel are now in uiaccessors_stubs.cpp
// (real implementations matching Windows main.cpp).
void ATUITemporarilyMountVHDImageW32(VDGUIHandle, const wchar_t*, bool) {}
bool ATUpdateVerifyFeedSignature(const void*, const void*, size_t) { return false; }

// ATRegisterDeviceConfigurers: device UI configurers not needed for Phase 5
void ATRegisterDeviceConfigurers(ATDeviceManager&) {}

// ============================================================
// ATUIShowDialogDiskExplorer / ATUIShowGenericDialog
#include <vd2/Dita/accel.h>
// ============================================================
#include <at/atnativeui/genericdialog.h>
#include <at/atcore/blockdevice.h>

void ATUIShowDialogDiskExplorer(VDGUIHandle, IATBlockDevice*, const wchar_t*) {}

ATUIGenericResult ATUIShowGenericDialog(const ATUIGenericDialogOptions& opts) {
	if (opts.mpIgnoreTag && *opts.mpIgnoreTag) {
		VDRegistryAppKey key("Confirmations");
		int v = key.getInt(opts.mpIgnoreTag, -1);
		if (v >= 0) return (ATUIGenericResult)v;
	}
	VDStringA cap = VDTextWToU8(VDStringW(opts.mpCaption ? opts.mpCaption : L"Altirra"));
	VDStringA msg = VDTextWToU8(VDStringW(opts.mpMessage ? opts.mpMessage : L""));
	std::vector<SDL_MessageBoxButtonData> btns;
	if (opts.mResultMask & (1 << kATUIGenericResult_Yes))   btns.push_back({ SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, kATUIGenericResult_Yes, "Yes" });
	if (opts.mResultMask & (1 << kATUIGenericResult_No))    btns.push_back({ SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, kATUIGenericResult_No, "No" });
	if (opts.mResultMask & (1 << kATUIGenericResult_OK))    btns.push_back({ SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, kATUIGenericResult_OK, "OK" });
	if (opts.mResultMask & (1 << kATUIGenericResult_Cancel))btns.push_back({ SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, kATUIGenericResult_Cancel, "Cancel" });
	if (btns.empty()) btns.push_back({ SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, kATUIGenericResult_OK, "OK" });
	uint32 flags = SDL_MESSAGEBOX_INFORMATION;
	if (opts.mIconType == kATUIGenericIconType_Warning) flags = SDL_MESSAGEBOX_WARNING;
	else if (opts.mIconType == kATUIGenericIconType_Error) flags = SDL_MESSAGEBOX_ERROR;
	SDL_MessageBoxData d = {}; d.flags = flags; d.title = cap.c_str(); d.message = msg.c_str();
	d.numbuttons = (int)btns.size(); d.buttons = btns.data();
	int id = kATUIGenericResult_Cancel;
	SDL_ShowMessageBox(&d, &id);
	ATUIGenericResult r = (ATUIGenericResult)id;
	// Note: "don't show again" persistence is only stored when the dialog
	// has an explicit suppress option. Since SDL3 message boxes don't support
	// a "don't show again" checkbox, we don't auto-save the result.
	return r;
}

void ATUIGenericDialogUndoAllIgnores() {
	VDRegistryAppKey key;
	key.removeKeyRecursive("DialogDefaults");
	key.removeKeyRecursive("Confirmations");
}

// ============================================================
// ATUIShowAlertError / ATUIExecuteCommandStringAndShowErrors
// ============================================================
//
// Used by the Custom Device VM (customdevicevmtypes.cpp) to surface
// errors and to invoke registered commands. Windows routes both
// through the ATUIFuture/ATUIQueue machinery in main.cpp; the SDL3
// build runs commands directly through g_ATUICommandMgr and surfaces
// errors via SDL_ShowSimpleMessageBox + stderr.

#include "uiqueue.h"
#include <at/atui/uicommandmanager.h>

vdrefptr<ATUIFutureWithResult<bool>> ATUIShowAlertError(const wchar_t *text, const wchar_t *title) {
	VDStringA t = VDTextWToU8(VDStringW(title ? title : L"Error"));
	VDStringA m = VDTextWToU8(VDStringW(text ? text : L""));
	fprintf(stderr, "[error] %s: %s\n", t.c_str(), m.c_str());
	SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, t.c_str(), m.c_str(), nullptr);
	// Synthesise a completed future with result=true (user has been
	// notified). Mirrors the Win32 path returning after the modal closes.
	return vdrefptr<ATUIFutureWithResult<bool>>(new ATUIFutureWithResult<bool>(true));
}

extern ATUICommandManager g_ATUICommandMgr;

void ATUIExecuteCommandStringAndShowErrors(const char *cmd, const ATUICommandOptions *opts) noexcept {
	if (!cmd || !*cmd)
		return;

	bool ok;
	if (opts)
		ok = g_ATUICommandMgr.ExecuteCommandNT(cmd, *opts);
	else
		ok = g_ATUICommandMgr.ExecuteCommandNT(cmd);

	if (!ok && (!opts || !opts->mbQuiet)) {
		fprintf(stderr, "[error] Command failed or not registered: %s\n", cmd);
	}
}

// Accelerator string formatting for shortcut display in menus
void VDUIGetAcceleratorString(const VDUIAccelerator& accel, VDStringW& s) {
	s.clear();
	if (!accel.mVirtKey) return;
	if (accel.mModifiers & VDUIAccelerator::kModCtrl) s += L"Ctrl+";
	if (accel.mModifiers & VDUIAccelerator::kModAlt) s += L"Alt+";
	if (accel.mModifiers & VDUIAccelerator::kModShift) s += L"Shift+";
	uint32 vk = accel.mVirtKey;
	if (vk >= 0x70 && vk <= 0x87) { wchar_t b[8]; swprintf(b, 8, L"F%d", vk - 0x70 + 1); s += b; return; }
	struct M { uint32 k; const wchar_t *n; };
	static const M m[] = { {0x08,L"Backspace"},{0x09,L"Tab"},{0x0D,L"Enter"},{0x1B,L"Esc"},{0x20,L"Space"},
		{0x21,L"PgUp"},{0x22,L"PgDn"},{0x23,L"End"},{0x24,L"Home"},{0x25,L"Left"},{0x26,L"Up"},{0x27,L"Right"},
		{0x28,L"Down"},{0x2D,L"Insert"},{0x2E,L"Delete"},{0x13,L"Pause"} };
	for (auto& e : m) { if (e.k == vk) { s += e.n; return; } }
	if ((vk>='A'&&vk<='Z')||(vk>='0'&&vk<='9')) { s += (wchar_t)vk; return; }
	if (vk==0xBA){s+=L";";return;} if(vk==0xBB){s+=L"=";return;} if(vk==0xBC){s+=L",";return;}
	if (vk==0xBD){s+=L"-";return;} if(vk==0xBE){s+=L".";return;} if(vk==0xBF){s+=L"/";return;}
	wchar_t b[16]; swprintf(b, 16, L"0x%02X", vk); s += b;
}

// Test mode symbols are now provided by ui_testmode.cpp (full implementation).

// ============================================================
// Registry persistence stubs (Windows only)
// ============================================================
// On Windows, the system library uses VDRegistryProviderW32 which talks to
// the Windows registry directly ��� it persists automatically.  The SDL3
// main loop calls ATRegistryLoadFromDisk()/ATRegistryFlushToDisk() which
// are provided by registry_sdl3.cpp on non-Windows.  On Windows we just
// provide no-op stubs since the registry handles persistence.
#ifdef _WIN32
void ATRegistryLoadFromDisk() {}
void ATRegistryFlushToDisk() {}
#endif
