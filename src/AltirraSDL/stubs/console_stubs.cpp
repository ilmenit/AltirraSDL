//	Altirra SDL3 frontend - console.h stub and routing implementations
//	Console output is routed to the ImGui console pane when it exists,
//	or to stderr as fallback.  Remaining stubs are for Win32-only
//	functions that have no SDL3 equivalent yet.

#include <stdafx.h>
#include <stdio.h>
#include <stdarg.h>
#include <mutex>
#include <string>
#include <SDL3/SDL.h>
#include <vd2/system/vdtypes.h>
#include <vd2/system/VDString.h>
#include <at/atnativeui/uiframe.h>
#include "console.h"
#include "debugger.h"
#include "simulator.h"
#include "antic.h"

// Defined in ui_dbg_console.cpp
extern IATUIDebuggerConsoleWindow *g_pConsoleWindow;

// Shared console text buffer (in ui_dbg_console.cpp) — used before
// the console pane exists.
extern std::mutex g_consoleMutex;
extern std::string g_consoleText;
extern bool g_consoleNeedsScroll;

static void ConsoleAppend(const char *s) {
	if (g_pConsoleWindow) {
		// Match Windows ATConsoleWrite: call both Write() and ShowEnd()
		g_pConsoleWindow->Write(s);
		g_pConsoleWindow->ShowEnd();
	} else {
		// Buffer text even before console pane exists
		std::lock_guard<std::mutex> lock(g_consoleMutex);
		g_consoleText.append(s);
		g_consoleNeedsScroll = true;
	}
}

// =========================================================================
// Console output routing
// =========================================================================

void ATConsoleOpenLogFile(const wchar_t*) {}
void ATConsoleCloseLogFileNT() {}
void ATConsoleCloseLogFile() {}

void ATConsoleWrite(const char *s) {
	ConsoleAppend(s);
}

void ATConsolePrintfImpl(const char *format, ...) {
	// Match Windows: only output when console window exists
	if (!g_pConsoleWindow)
		return;

	char buf[3072];
	va_list val;
	va_start(val, format);
	unsigned result = (unsigned)vsnprintf(buf, sizeof(buf), format, val);
	va_end(val);
	// Match Windows behavior: silently drop on overflow
	if (result < sizeof(buf))
		ConsoleAppend(buf);
}

void ATConsoleTaggedPrintfImpl(const char *format, ...) {
	// Match Windows: only output when console window exists
	if (!g_pConsoleWindow)
		return;

	// Windows prepends "(frame:y,x) " beam position prefix.
	char buf[3072];
	extern ATSimulator g_sim;
	auto& antic = g_sim.GetAntic();
	unsigned prefixLen = (unsigned)snprintf(buf, sizeof(buf), "(%3d:%3d,%3d) ",
		antic.GetRawFrameCounter(), antic.GetBeamY(), antic.GetBeamX());

	if (prefixLen < sizeof(buf)) {
		va_list val;
		va_start(val, format);
		unsigned messageLen = (unsigned)vsnprintf(buf + prefixLen, sizeof(buf) - prefixLen, format, val);
		va_end(val);

		if (messageLen < sizeof(buf) - prefixLen)
			ConsoleAppend(buf);
	}
}

void ATConsoleSetTraceLogger(vdfunction<void(const char*, uint64)>) {}

// =========================================================================
// Font stubs (Win32-specific, no-op on SDL3)
// =========================================================================

void ATConsoleGetFont(struct tagLOGFONTW&, int&) {}
void ATConsoleGetCharMetrics(int& charWidth, int& lineHeight) {
	charWidth = 8;
	lineHeight = 16;
}
void ATConsoleSetFont(const struct tagLOGFONTW&, int) {}
void ATConsoleSetFontDpi(unsigned) {}

// =========================================================================
// Console open/close — wired to ImGui debugger
// =========================================================================

// Forward declarations from ui_debugger.cpp
void ATUIDebuggerOpen();
void ATUIDebuggerClose();
bool ATUIDebuggerIsOpen();

bool ATConsoleShowSource(uint32) { return false; }

void ATShowConsole() {
	ATUIDebuggerOpen();
}

void ATOpenConsole() {
	// Windows exits fullscreen when the debugger opens (debugger.cpp:4780).
	// This is called both from user action and from auto-open on breakpoint.
	extern SDL_Window *g_pWindow;
	if (g_pWindow && (SDL_GetWindowFlags(g_pWindow) & SDL_WINDOW_FULLSCREEN))
		SDL_SetWindowFullscreen(g_pWindow, false);

	ATUIDebuggerOpen();
}

void ATCloseConsole() {
	ATUIDebuggerClose();
}

bool ATIsDebugConsoleActive() {
	return ATUIDebuggerIsOpen();
}

// =========================================================================
// Source window routing — delegate to ImGui source pane (ui_dbg_source.cpp)
// =========================================================================

// Defined in ui_dbg_source.cpp
extern IATSourceWindow *ATImGuiFindSourceWindow(const wchar_t *path);
extern IATSourceWindow *ATImGuiOpenSourceWindow(const wchar_t *path);
extern void ATUIDebuggerShowSourceListDialog();

IATSourceWindow *ATGetSourceWindow(const wchar_t *s) {
	return ATImGuiFindSourceWindow(s);
}

IATSourceWindow *ATOpenSourceWindow(const wchar_t *s) {
	return ATImGuiOpenSourceWindow(s);
}

IATSourceWindow *ATOpenSourceWindow(const ATDebuggerSourceFileInfo& sfi, bool) {
	return ATImGuiOpenSourceWindow(sfi.mSourcePath.c_str());
}

void ATUIShowSourceListDialog() {
	ATUIDebuggerShowSourceListDialog();
}

// =========================================================================
// Pane lookup stubs (will be replaced as panes are implemented)
// =========================================================================

void ATGetUIPanes(vdfastvector<ATUIPane*>&) {}
ATUIPane *ATGetUIPane(uint32) { return nullptr; }
void *ATGetUIPaneAs(uint32, uint32) { return nullptr; }
ATUIPane *ATGetUIPaneByFrame(ATFrameWindow*) { return nullptr; }
void ATCloseUIPane(uint32) {}

ATUIPane *ATUIGetActivePane() { return nullptr; }
void *ATUIGetActivePaneAs(uint32) { return nullptr; }
uint32 ATUIGetActivePaneId() { return 0; }

bool ATRestorePaneLayout(const char*) { return false; }
void ATSavePaneLayout(const char*) {}
void ATLoadDefaultPaneLayout() {}

// =========================================================================
// Win32-specific stubs (no-op on SDL3)
// =========================================================================

VDZHFONT ATGetConsoleFontW32() { return nullptr; }
int ATGetConsoleFontLineHeightW32() { return 16; }
VDZHFONT ATConsoleGetPropFontW32() { return nullptr; }
int ATConsoleGetPropFontLineHeightW32() { return 16; }
VDZHMENU ATUIGetSourceContextMenuW32() { return nullptr; }

void ATConsoleAddFontNotification(const vdfunction<void()>*) {}
void ATConsoleRemoveFontNotification(const vdfunction<void()>*) {}

void ATConsolePingBeamPosition(uint32, uint32, uint32) {}

// =========================================================================
// debugger.cpp forward-declared function (defined in console.cpp on Win32)
// =========================================================================

bool ATConsoleCheckBreak() { return false; }

// ATRegisterUIPaneType, ATRegisterUIPaneClass, ATActivateUIPane —
// now provided by ui_debugger.cpp (ImGui pane manager).
