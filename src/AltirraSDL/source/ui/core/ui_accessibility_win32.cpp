// Reuse the native Altirra UI Automation text provider with SDL's HWND.
#include <stdafx.h>
#include "ui_accessibility.h"
#include <windows.h>
#include <commctrl.h>
#include <UIAutomation.h>
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include "uidisplayaccessibility.h"
#include "uidisplayaccessibility_win32.h"

namespace {
class DisplayCallbacks final : public IATUIDisplayAccessibilityCallbacksW32 {
public:
	std::shared_ptr<const ATSDLAccessibilitySnapshot> snapshot;
	HWND window = nullptr;

	vdpoint32 GetNearestBeamPositionForScreenPoint(sint32 x, sint32 y) const override {
		POINT point {x, y};
		ScreenToClient(window, &point);
		if (!snapshot || snapshot->lines.empty())
			return vdpoint32(0, 0);
		int index = 0;
		for (const auto& line : snapshot->lines) {
			if (point.y < line.y + line.height) {
				double column = line.width > 0
					? (point.x - (double)line.x) * line.length / line.width : 0;
				return vdpoint32((int)std::clamp(column, 0.0, (double)line.length), index);
			}
			++index;
		}
		return vdpoint32(0, index);
	}
	vdrect32 GetTextSpanBoundingBox(int row, int first, int last) const override {
		if (!snapshot || row < 0 || row >= (int)snapshot->lines.size())
			return vdrect32(0, 0, 0, 0);
		const auto& line = snapshot->lines[row];
		float cell = line.length ? line.width / line.length : 0;
		POINT origin {};
		ClientToScreen(window, &origin);
		return vdrect32(origin.x + (int)std::floor(line.x + first * cell),
			origin.y + (int)std::floor(line.y),
			origin.x + (int)std::ceil(line.x + last * cell),
			origin.y + (int)std::ceil(line.y + line.height));
	}
};

DisplayCallbacks g_callbacks;
vdrefptr<IATUIDisplayAccessibilityProviderW32> g_provider;
bool g_comInitialized = false;
bool g_providerUsed = false;

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam,
	LPARAM lparam, UINT_PTR, DWORD_PTR) {
	if (message == WM_GETOBJECT && g_provider && (LONG)lparam == UiaRootObjectId)
		return UiaReturnRawElementProvider(window, wparam, lparam,
			g_provider->AsRawElementProviderSimple());
	if (message == WM_NCDESTROY) {
		if (g_provider) {
			g_provider->Detach();
			if (g_providerUsed)
				UiaDisconnectProvider(g_provider->AsRawElementProviderSimple());
			g_provider = nullptr;
		}
		g_callbacks.window = nullptr;
		RemoveWindowSubclass(window, WindowProc, 1);
	}
	return DefSubclassProc(window, message, wparam, lparam);
}
}

// The upstream provider uses this only to mark process-wide UIA use. SDL owns
// provider shutdown directly below and does not link the Win32 UI manager.
void ATUIAccSetUsed() { g_providerUsed = true; }

void ATSDLAccessibilityPlatformUpdate(SDL_Window *window,
	std::shared_ptr<const ATSDLAccessibilitySnapshot> snapshot) {
	HWND hwnd = (HWND)SDL_GetPointerProperty(SDL_GetWindowProperties(window),
		SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
	if (!hwnd) {
		ATSDLAccessibilityPublishStatus(ATSDLAccessibilityState::Unavailable,
			"Windows screen reader support requires a native SDL desktop window.");
		return;
	}
	if (g_callbacks.window && g_callbacks.window != hwnd)
		ATSDLAccessibilityPlatformShutdown();
	if (!g_provider && g_comInitialized)
		ATSDLAccessibilityPlatformShutdown();
	if (!g_provider) {
		// UseComThreading on the upstream provider marshals accessibility calls
		// to this STA; neither the provider nor its geometry callbacks see core
		// state. Balance only the COM initialization acquired by this adapter.
		HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
		if (FAILED(hr)) {
			ATSDLAccessibilityPublishStatus(ATSDLAccessibilityState::Unavailable,
				"Windows UI Automation could not initialize the window's COM apartment. Restart AltirraSDL with the standard desktop backend.");
			return;
		}
		g_comInitialized = true;
		g_callbacks.window = hwnd;
		if (!SetWindowSubclass(hwnd, WindowProc, 1, 0)) {
			ATSDLAccessibilityPlatformShutdown();
			ATSDLAccessibilityPublishStatus(ATSDLAccessibilityState::Unavailable,
				"Windows UI Automation could not attach to the SDL window. Disable and enable screen reader support to retry.");
			return;
		}
		ATUICreateDisplayAccessibilityProviderW32(hwnd, g_callbacks, ~g_provider);
	}
	const auto previous = g_callbacks.snapshot;
	g_callbacks.snapshot = snapshot;
	if (!previous || previous->text != snapshot->text || previous->lines != snapshot->lines) {
		vdrefptr<ATUIDisplayAccessibilityScreen> screen(new ATUIDisplayAccessibilityScreen);
		screen->mText = snapshot->text.c_str();
		screen->mLines.clear();
		for (const auto& line : snapshot->lines) {
			ATUIDisplayAccessibilityLineInfo info;
			info.mStartBeamY = (uint32)screen->mLines.size();
			info.mHeight = 1;
			info.mTextOffset = line.offset;
			info.mTextLength = line.length;
			screen->mLines.push_back(info);
		}
		if (screen->mLines.empty())
			screen->Clear();
		else {
			screen->mFormatSpans.clear();
			screen->mFormatSpans.push_back(ATUIDisplayAccFormatSpan {0, 0xFFFFFF, 0});
			screen->mFormatSpans.push_back(ATUIDisplayAccFormatSpan {(uint32)screen->mText.size(), 0xFFFFFF, 0});
		}
		g_provider->SetScreen(screen);
	}
	if (snapshot->focused && (!previous || !previous->focused))
		g_provider->OnGainedFocus();
	ATSDLAccessibilityPublishStatus(ATSDLAccessibilityState::Active,
		"Windows UI Automation screen reader support is active.");
}

void ATSDLAccessibilityPlatformShutdown() {
	if (g_callbacks.window)
		RemoveWindowSubclass(g_callbacks.window, WindowProc, 1);
	if (g_provider) {
		g_provider->Detach();
		if (g_providerUsed)
			UiaDisconnectProvider(g_provider->AsRawElementProviderSimple());
		g_provider = nullptr;
	}
	g_callbacks.snapshot.reset();
	g_callbacks.window = nullptr;
	g_providerUsed = false;
	if (g_comInitialized) {
		CoUninitialize();
		g_comInitialized = false;
	}
}
