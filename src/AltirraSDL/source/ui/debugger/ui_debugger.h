//	AltirraSDL - Dear ImGui debugger pane manager
//	Provides the pane registry and rendering infrastructure that replaces
//	the Win32 ATUIPane / ATFrameWindow / ATContainerWindow docking system.

#pragma once

#include <vd2/system/vdtypes.h>
#include <vd2/system/refcount.h>
#include <vd2/system/VDString.h>
#include <vd2/system/function.h>
#include <imgui.h>
#include <at/atnativeui/uiframe.h>
#include "console.h"
#include "debugger.h"

// SDL-only tool pane. Native Windows opens the Performance Analyzer through a
// separate trace viewer path, not through kATUIPaneId_Profiler.
constexpr uint32 kATUIPaneId_PerformanceAnalyzerSDL = 0x300;

// Forward
class ATSimulator;
struct ATUIState;

// ---------------------------------------------------------------------------
// ATImGuiDebuggerPane — base class for all ImGui debugger panes.
// Each pane implements IATDebuggerClient to receive state updates.
// ---------------------------------------------------------------------------

class ATImGuiDebuggerPane : public vdrefcounted<IVDRefCount>, public IATDebuggerClient, public IATUIDebuggerPane {
public:
	ATImGuiDebuggerPane(uint32 paneId, const char *title);
	virtual ~ATImGuiDebuggerPane();

	uint32 GetPaneId() const { return mPaneId; }
	const char *GetTitle() const { return mTitle.c_str(); }

	bool IsVisible() const { return mbVisible; }
	void SetVisible(bool vis) { mbVisible = vis; }

	// Focus tracking — set during Render() by each pane
	bool HasFocus() const { return mbHasFocus; }

	// Focus request — set by ATActivateUIPane, consumed on next render
	void RequestFocus() { mbFocusRequested = true; }

	// Called each frame to render the pane.  Returns false if the pane
	// was closed via the X button.
	virtual void OnFrame();
	virtual bool Render() = 0;
	bool OnPaneCommand(ATUIPaneCommandId id) override;
	virtual void *AsPaneInterface(uint32 iid);

	// IATDebuggerClient
	void OnDebuggerSystemStateUpdate(const ATDebuggerSystemState& state) override;
	void OnDebuggerEvent(ATDebugEvent eventId) override;

protected:
	uint32 mPaneId;
	VDStringA mTitle;
	bool mbVisible = true;
	bool mbFocusRequested = false;
	bool mbHasFocus = false;

	// Cached state from last OnDebuggerSystemStateUpdate
	ATDebuggerSystemState mLastState {};
	bool mbStateValid = false;
};

// ---------------------------------------------------------------------------
// Pane manager — singleton that tracks all open debugger panes.
// ---------------------------------------------------------------------------

void ATUIDebuggerInit();
void ATUIDebuggerShutdown();

// Render all open debugger panes.  Called from ATUIRenderFrame().
void ATUIDebuggerRenderPanes(ATSimulator &sim, ATUIState &state);

// Tick the debugger engine (call ATDebugger::Tick).
// Called from the main loop after ATUIPollDeferredActions().
void ATUIDebuggerTick();

// Open/close the debugger (matches Debug > Enable Debugger toggle)
void ATUIDebuggerOpen();
void ATUIDebuggerClose();
bool ATUIDebuggerIsOpen();

// ATActivateUIPane is declared in <at/atnativeui/uiframe.h> (included above)

// Pane registration
void ATUIDebuggerRegisterPane(ATImGuiDebuggerPane *pane);
void ATUIDebuggerUnregisterPane(uint32 paneId);
ATImGuiDebuggerPane *ATUIDebuggerGetPane(uint32 paneId);
void *ATUIDebuggerGetPaneAs(uint32 paneId, uint32 iid);

// Focus management
uint32 ATUIDebuggerGetFocusedPaneId();
void ATUIDebuggerFocusConsole();
void ATUIDebuggerFocusConsoleWithText(const char *text);
const char *ATUIDebuggerGetConsoleInputTextForTest();
bool ATUIDebuggerHandleTextInput(const char *text);
void ATUIDebuggerFocusDisplay();
void ATUIDebuggerSetDisassemblyPosition(uint32 addr);
bool ATUIDebuggerQueryDisassemblySelectedBreakpointForTest(
	int& line,
	uint32& addr,
	bool& hasBreakpoint);
bool ATUIDebuggerToggleDisassemblySelectedBreakpointForTest(
	int& line,
	uint32& addr,
	bool& hasBreakpoint);
bool ATUIDebuggerSimulateDisassemblyRunStopForTest(
	uint32 frameAddr,
	int& line,
	uint32& addr,
	bool& hasBreakpoint);
bool ATUIDebuggerDisassemblyGoToSelectedSourceForTest(
	int& line,
	uint32& addr,
	bool& outApplied);
bool ATUIDebuggerDisassemblyTargetNavigationForTest(VDStringA& outState);
bool ATUIDebuggerDisassemblyCallPreviewForTest(VDStringA& outState);
bool ATUIDebuggerDescribeHistoryForTest(VDStringA& outState);
bool ATUIDebuggerHistoryContextActionForTest(const char *action,
	VDStringA& outState,
	bool& outApplied);
bool ATUIDebuggerSelectHistoryInstructionForTest(bool last, VDStringA& outState);
bool ATUIDebuggerOpenHistoryContextMenuForTest(VDStringA& outState);
bool ATUIDebuggerSetHistoryHorizontalScrollForTest(float x, VDStringA& outState);
bool ATUIDebuggerRequestFileForTest(bool save, const char *utf8Path, VDStringW& outPath);
bool ATUIDebuggerOpenMissingSourceForTest(const char *symbolPathUtf8,
	const char *resolvedPathUtf8,
	VDStringW& outPath,
	VDStringW& outAlias);
bool ATUIDebuggerOpenSourceForTest(const char *utf8Path,
	uint32& outPaneId,
	int& outLineCount,
	VDStringW& outPath);
bool ATUIDebuggerQuerySourceForTest(const char *utf8Path,
	uint32& outPaneId,
	int& outLineCount,
	int& outSelectedLine,
	VDStringW& outPath,
	VDStringA& outFirstLine,
	VDStringA& outLastLine);
bool ATUIDebuggerReloadSourceForTest(const char *utf8Path,
	int selectLine,
	uint32& outPaneId,
	int& outBeforeLineCount,
	int& outAfterLineCount,
	int& outBeforeSelectedLine,
	int& outAfterSelectedLine,
	VDStringA& outBeforeLastLine,
	VDStringA& outAfterLastLine);
bool ATUIDebuggerQuerySourceMappingForTest(const char *utf8Path,
	uint32 address,
	int lineIndex,
	int& outLineForAddress,
	sint32& outAddressForLine);
bool ATUIDebuggerQuerySourceStepRangeForTest(const char *utf8Path,
	uint32 pc,
	bool& outHasRange,
	uint32& outStart,
	uint32& outLength);
bool ATUIDebuggerToggleSourceBreakpointForTest(const char *utf8Path,
	int lineIndex,
	int& outBefore,
	int& outAfter,
	sint32& outAddress);
bool ATUIDebuggerExecuteSourceCommandForTest(const char *utf8Path,
	const char *command,
	bool pcOverride,
	uint32 pcOverrideValue,
	bool& outHandled,
	uint32& outPC,
	bool& outHadRange,
	uint32& outRangeStart,
	uint32& outRangeLength,
	bool& outWasRunning,
	bool& outRunningAfterCommand,
	bool& outRunningAfterCleanup);

// Debug stepping commands (for menu/shortcut wiring)
void ATUIDebuggerRun();
void ATUIDebuggerRunStop();
void ATUIDebuggerBreak();
void ATUIDebuggerStepInto();
void ATUIDebuggerStepOver();
void ATUIDebuggerStepOut();
void ATUIDebuggerToggleBreakpoint();

// Source-level stepping (from ui_dbg_source.cpp)
bool ATUIDebuggerSourceStepInto();
bool ATUIDebuggerSourceStepOver();

// Watch window helper (from ui_dbg_watch.cpp)
extern void ATUIDebuggerAddToWatch(const char *expr);
bool ATUIDebuggerEditWatchForTest(const char *expr, VDStringA& outState);
bool ATUIDebuggerPrintableEditWatchForTest(char ch,
	const char *suffix,
	VDStringA& outState);
bool ATUIDebuggerDeleteSelectedWatchForTest(VDStringA& outState);
bool ATUIDebuggerDescribeWatchForTest(VDStringA& outState);

// Breakpoint dialog (from ui_dbg_breakpoints.cpp)
// Pass userIdx >= 0 to edit existing, -1 for new breakpoint
void ATUIDebuggerShowBreakpointDialog(sint32 userIdx);
bool ATUIDebuggerFormatBreakpointTraceForTest(const char *traceText,
	VDStringA& outCommand,
	VDStringA& outError);
bool ATUIDebuggerSubmitBreakpointForTest(
	int locationType,
	const char *location,
	bool conditionEnabled,
	const char *conditionText,
	bool stopExecution,
	bool commandEnabled,
	const char *commandText,
	bool traceEnabled,
	const char *traceText,
	uint32& outUserIdx,
	VDStringA& outError);
bool ATUIDebuggerDescribeBreakpointForTest(uint32 userIdx, VDStringA& outDescription);
bool ATUIDebuggerFormatBreakpointDescriptionForTest(
	bool oneShot,
	bool clearOnReset,
	bool continueExecution,
	VDStringA& outDescription);
bool ATUIDebuggerDeleteBreakpointForTest(uint32 userIdx);
bool ATUIDebuggerSelectBreakpointForTest(uint32 userIdx);
bool ATUIDebuggerDeleteBreakpointViaPaneForTest(uint32 userIdx, int& remainingCount);
bool ATUIDebuggerGetBreakpointPaneOrderForTest(VDStringA& rowOrder);
int ATUIDebuggerCountBreakpointsForTest();
bool ATUIDebuggerFormatMemoryAddToWatchForTest(uint32 addr,
	bool wordMode,
	VDStringA& expr);
bool ATUIDebuggerTrackMemoryOnScreenForTest(uint32 addr,
	int len,
	int& watchIndex);
bool ATUIDebuggerEnsureMemoryHighlightVisibleForTest(uint32 viewStart,
	uint32 columns,
	uint32 visibleRows,
	uint32 highlightAddr,
	uint32& outViewStart);
bool ATUIDebuggerMemoryNavigationForTest(uint32 viewStart,
	uint32 columns,
	uint32 visibleRows,
	uint32 highlightAddr,
	bool dataColumn,
	const char *op,
	uint32& outViewStart,
	uint32& outHighlightAddr,
	bool& outDataColumn);
bool ATUIDebuggerEditMemoryHexByteForTest(uint32 addr,
	uint8 value,
	uint8& outBefore,
	uint8& outAfter);
bool ATUIDebuggerMemoryHexAutoAdvanceForTest(int mode,
	uint32 viewStart,
	uint32 columns,
	uint32 visibleRows,
	uint32 addr,
	const char *digits,
	uint16& outValue,
	uint32& outSelectedAddr,
	uint32& outViewStart);
bool ATUIDebuggerCancelMemoryHexByteEditForTest(uint32 addr,
	uint8 value,
	uint8& outBefore,
	uint8& outAfter,
	uint32& outSelectedAddr,
	bool& outSelectionEnabled);
bool ATUIDebuggerEditMemoryValueForTest(int mode,
	uint32 addr,
	uint16 value,
	uint16& outBefore,
	uint16& outAfter);
bool ATUIDebuggerEditMemoryTextForTest(int mode,
	uint32 addr,
	uint8 ch,
	uint8& outBefore,
	uint8& outAfter,
	uint8& outWritten);
bool ATUIDebuggerMemoryTextAutoAdvanceForTest(int mode,
	uint32 viewStart,
	uint32 columns,
	uint32 visibleRows,
	uint32 addr,
	uint8 ch,
	uint8& outWritten,
	uint32& outSelectedAddr,
	uint32& outViewStart);
bool ATUIDebuggerActivateTargetForTest(int row,
	uint32& outCurrentTarget,
	uint32& outTargetCount,
	VDStringA& outError);
bool ATUIDebuggerDescribeDebugDisplayForTest(VDStringA& outState);
bool ATUIDebuggerApplyDebugDisplayDLForTest(const char *expr,
	VDStringA& outState);
bool ATUIDebuggerApplyDebugDisplayPFForTest(const char *expr,
	VDStringA& outState);
bool ATUIDebuggerSetDebugDisplayFilterForTest(int mode,
	VDStringA& outState);
bool ATUIDebuggerSetDebugDisplayPaletteForTest(int mode,
	VDStringA& outState);

// Verifier dialog (from ui_dbg_verifier.cpp)
void ATUIShowDialogVerifier();
void ATUIRenderVerifierDialog();

// Close a specific pane by ID (used by ATCloseUIPane stub)
void ATUIDebuggerClosePaneById(uint32 paneId);

// Window management — Close/Undock/Cycle panes (replaces cmdwindow.cpp)
void ATUIDebuggerCloseActivePane();
void ATUIDebuggerUndockActivePane();
void ATUIDebuggerCyclePane(int direction);  // +1 = next, -1 = previous
bool ATUIDebuggerHasVisiblePanes();

// Source file list dialog (from ui_dbg_source.cpp)
void ATUIDebuggerShowSourceListDialog();
void ATUIDebuggerRenderSourceListDialog();
