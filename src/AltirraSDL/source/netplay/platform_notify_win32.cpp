// Altirra SDL3 netplay - Windows notification backend
//
// Uses Shell_NotifyIconW with NIM_ADD on first init and NIM_MODIFY on
// each post to show a balloon.  No external deps beyond shell32/user32
// already linked by the SDL3 frontend.

#include <stdafx.h>

#define ALTIRRA_NETPLAY_NOTIFY_WIN32 1
#include "platform_notify.h"

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>

#include <cstring>
#include <cwchar>

namespace ATNetplay {

namespace {

NOTIFYICONDATAW g_nid = {};
bool            g_ok  = false;
HWND            g_ownerWnd = nullptr;

HWND GetOwnerWindow() {
	if (g_ownerWnd) return g_ownerWnd;
	// Find the main SDL window by class name.  SDL3 uses "SDL_APP"
	// for its window class on Windows.  Fall back to GetDesktopWindow
	// if not found — balloons still appear, just without an owner.
	HWND h = FindWindowExW(nullptr, nullptr, L"SDL_APP", nullptr);
	if (!h) h = GetDesktopWindow();
	g_ownerWnd = h;
	return h;
}

void FillString(wchar_t* dst, size_t dstCap, const char* src) {
	if (!dst || dstCap == 0) return;
	if (!src) { dst[0] = 0; return; }
	int n = MultiByteToWideChar(CP_UTF8, 0, src, -1, dst, (int)dstCap);
	if (n <= 0) dst[0] = 0;
	dst[dstCap - 1] = 0;
}

} // anonymous

int PlatformNotifyInit(const char* appName) {
	if (g_ok) return 1;

	std::memset(&g_nid, 0, sizeof g_nid);
	g_nid.cbSize = sizeof g_nid;
	g_nid.hWnd   = GetOwnerWindow();
	g_nid.uID    = 0xA117;  // arbitrary tag
	g_nid.uFlags = NIF_ICON | NIF_TIP;
	g_nid.hIcon  = LoadIconW(nullptr, IDI_INFORMATION);
	FillString(g_nid.szTip, sizeof g_nid.szTip / sizeof(wchar_t),
		appName ? appName : "AltirraSDL");

	if (Shell_NotifyIconW(NIM_ADD, &g_nid)) {
		g_ok = true;
		return 1;
	}
	return 0;
}

void PlatformNotifyShutdown() {
	if (g_ok) {
		Shell_NotifyIconW(NIM_DELETE, &g_nid);
		g_ok = false;
	}
}

int PlatformNotifyPost(const char* title, const char* body) {
	if (!g_ok) return 0;
	g_nid.uFlags = NIF_INFO | NIF_TIP | NIF_ICON;
	g_nid.dwInfoFlags = NIIF_INFO;
	FillString(g_nid.szInfoTitle,
		sizeof g_nid.szInfoTitle / sizeof(wchar_t), title);
	FillString(g_nid.szInfo,
		sizeof g_nid.szInfo / sizeof(wchar_t), body);
	return Shell_NotifyIconW(NIM_MODIFY, &g_nid) ? 1 : 0;
}

} // namespace ATNetplay

#endif // _WIN32
