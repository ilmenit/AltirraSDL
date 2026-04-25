//	Altirra SDL3 frontend - console.h stub and routing implementations
//	Console output is routed to the ImGui console pane when it exists,
//	or to stderr as fallback.  Remaining stubs are for Win32-only
//	functions that have no SDL3 equivalent yet.

#include <stdafx.h>
#include <stdio.h>
#include <stdarg.h>
#include <atomic>
#include <mutex>
#include <string>
#include <SDL3/SDL.h>
#include <vd2/system/vdtypes.h>
#include <vd2/system/VDString.h>
#include <vd2/system/file.h>
#include <vd2/system/strutil.h>
#include <vd2/system/text.h>
#include <vd2/system/function.h>
#include <vd2/system/vdalloc.h>
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
extern std::atomic<bool> g_consoleScrollToBottom;
extern std::atomic<bool> g_consoleTextDirty;

// =========================================================================
// Log file + trace logger state — port of console.cpp:99-103.
//
// Without these, debugger commands that write huge dumps had no way to
// capture output to a file (ATConsoleOpenLogFile was a no-op stub) and the
// trace-logger callback used by the trace tooling never received writes.
// =========================================================================

namespace {
	std::mutex g_logFileMutex;
	VDFileStream *g_pLogFile = nullptr;
	VDTextOutputStream *g_pLogOutput = nullptr;
	vdfunction<void(const char *, uint64)> g_traceLogger;
}

static void ConsoleWriteToLog(const char *s) {
	std::lock_guard<std::mutex> lock(g_logFileMutex);
	if (!g_pLogOutput || !s)
		return;
	for (;;) {
		const char *lbreak = strchr(s, '\n');
		if (!lbreak) {
			g_pLogOutput->Write(s);
			break;
		}
		g_pLogOutput->PutLine(s, (int)(lbreak - s));
		s = lbreak + 1;
	}
}

static void ConsoleAppend(const char *s) {
	// Trace logger sees output unconditionally (matches console.cpp:973-975).
	extern ATSimulator g_sim;
	if (g_traceLogger && s)
		g_traceLogger(s, g_sim.GetScheduler()->GetTick64());

	if (g_pConsoleWindow) {
		// Match Windows ATConsoleWrite: call both Write() and ShowEnd()
		g_pConsoleWindow->Write(s);
		g_pConsoleWindow->ShowEnd();
	} else {
		// Buffer text even before console pane exists
		std::lock_guard<std::mutex> lock(g_consoleMutex);
		g_consoleText.append(s);
		g_consoleTextDirty = true;
		g_consoleScrollToBottom = true;
	}

	ConsoleWriteToLog(s);
}

// =========================================================================
// Console output routing
// =========================================================================

// Mirrors console.cpp:939-967. The implementation is identical because it
// relies only on the portable VDFileStream / VDTextOutputStream layer.
void ATConsoleOpenLogFile(const wchar_t *path) {
	ATConsoleCloseLogFile();

	vdautoptr<VDFileStream> fs(new VDFileStream(path,
		nsVDFile::kWrite | nsVDFile::kDenyAll | nsVDFile::kCreateAlways));
	vdautoptr<VDTextOutputStream> tos(new VDTextOutputStream(fs));

	std::lock_guard<std::mutex> lock(g_logFileMutex);
	g_pLogFile = fs.release();
	g_pLogOutput = tos.release();
}

void ATConsoleCloseLogFileNT() {
	std::lock_guard<std::mutex> lock(g_logFileMutex);
	delete g_pLogOutput; g_pLogOutput = nullptr;
	delete g_pLogFile;   g_pLogFile = nullptr;
}

void ATConsoleCloseLogFile() {
	try {
		std::lock_guard<std::mutex> lock(g_logFileMutex);
		if (g_pLogOutput) g_pLogOutput->Flush();
		if (g_pLogFile)   g_pLogFile->close();
	} catch (...) {
		ATConsoleCloseLogFileNT();
		throw;
	}
	ATConsoleCloseLogFileNT();
}

void ATConsoleWrite(const char *s) {
	ConsoleAppend(s);
}

void ATConsolePrintfImpl(const char *format, ...) {
	// Skip the format work entirely if there is no sink (no pane, no log,
	// no trace) — matches Windows guard behaviour at console.cpp:973+.
	if (!g_pConsoleWindow && !g_pLogOutput && !g_traceLogger)
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
	if (!g_pConsoleWindow && !g_pLogOutput && !g_traceLogger)
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

void ATConsoleSetTraceLogger(vdfunction<void(const char *, uint64)> traceLogger) {
	g_traceLogger = std::move(traceLogger);
}

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

// Mirrors console.cpp:106-123. Looks up the source line for an address
// and opens the source pane focused on it. Returns false if the address
// has no debug-line mapping or the source file can't be opened.
bool ATConsoleShowSource(uint32 addr) {
	uint32 moduleId;
	ATSourceLineInfo lineInfo;
	IATDebuggerSymbolLookup *lookup = ATGetDebuggerSymbolLookup();
	if (!lookup->LookupLine(addr, false, moduleId, lineInfo))
		return false;

	ATDebuggerSourceFileInfo sourceFileInfo;
	if (!lookup->GetSourceFilePath(moduleId, lineInfo.mFileId, sourceFileInfo) && lineInfo.mLine)
		return false;

	IATSourceWindow *w = ATOpenSourceWindow(sourceFileInfo);
	if (!w)
		return false;

	w->FocusOnLine(lineInfo.mLine - 1);
	return true;
}

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
void ATCloseUIPane(uint32 id) {
	// Delegate to the ImGui pane manager
	extern void ATUIDebuggerClosePaneById(uint32);
	ATUIDebuggerClosePaneById(id);
}

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

// Decode a (frame, vpos, hpos) tuple into a CPU cycle and ask the
// debugger History pane to scroll to it. Mirrors console.cpp:1035-1050
// from the Windows source verbatim.
//
// The bridge server has no UI pane registry, so the helper is gated.

#ifndef ALTIRRA_BRIDGE_HEADLESS
extern bool ATUIDebuggerHistoryPaneJumpToCycle(uint32 cycle);
#endif

void ATConsolePingBeamPosition(uint32 frame, uint32 vpos, uint32 hpos) {
#ifdef ALTIRRA_BRIDGE_HEADLESS
	(void)frame; (void)vpos; (void)hpos;
#else
	extern ATSimulator g_sim;
	const auto& decoder = g_sim.GetTimestampDecoder();

	// Unsigned wrapping is intentional — matches Windows console.cpp:1044.
	const uint32 cycle = decoder.mFrameTimestampBase
		+ (frame - decoder.mFrameCountBase) * decoder.mCyclesPerFrame
		+ 114u * vpos
		+ hpos;

	if (ATUIDebuggerHistoryPaneJumpToCycle(cycle))
		return;

	ATActivateUIPane(kATUIPaneId_History, true);
#endif
}

// =========================================================================
// debugger.cpp forward-declared — Ctrl-Break interrupt for long debugger
// commands (memory dumps, traces, etc.). Replaces console.cpp:1031-1033
// (which uses Win32 GetAsyncKeyState).
//
// SDL3 sets the flag from two paths:
//   * The ImGui debugger console pane's Stop button calls
//     ATConsoleRequestBreak() (extern below).
//   * The keyboard handler routes Ctrl+Pause / Ctrl+Break to it.
// The flag is consumed (auto-cleared) on read, matching the Windows
// edge-trigger semantics — debugger.cpp polls every 16 rows and expects
// successive calls during a single command to keep returning true until
// the command exits.
// =========================================================================

namespace {
	std::atomic<bool> g_breakRequested{false};
}

void ATConsoleRequestBreak() {
	g_breakRequested.store(true, std::memory_order_relaxed);
}

bool ATConsoleCheckBreak() {
	// exchange returns the previous value; the flag stays set until the
	// next call so a rapid burst of CheckBreak() calls within one command
	// all observe the break, matching Windows edge-vs-level behaviour.
	return g_breakRequested.exchange(false, std::memory_order_relaxed);
}

// ATRegisterUIPaneType, ATRegisterUIPaneClass, ATActivateUIPane —
// now provided by ui_debugger.cpp (ImGui pane manager).
