//	AltirraSDL - Dear ImGui debugger disassembly pane
//	Replaces Win32 ATDisassemblyWindow (uidbgdisasm.cpp).
//	Shows disassembled instructions around the current PC with current-line
//	highlighting, breakpoint indicators, symbol labels, address navigation,
//	and F9/context-menu breakpoint toggling.

#include <stdafx.h>
#include <algorithm>
#include <array>
#include <imgui.h>
#include <vd2/system/binary.h>
#include <vd2/system/vdtypes.h>
#include <vd2/system/VDString.h>
#include <at/atcpu/history.h>
#include <at/atdebugger/target.h>
#include <at/atdebugger/symbols.h>
#include <vd2/system/text.h>
#include "../core/ui_main.h"
#include "debuggersettings.h"
#include "ui_debugger.h"
#include "console.h"
#include "debugger.h"
#include "disasm.h"
#include "simulator.h"
#include "cpu.h"
#include "uicommondialogs.h"

extern ATSimulator g_sim;
extern bool ATImGuiConsoleShowSource(uint32 addr);

static bool ATImGuiEncodeUTF8(ImWchar c, char (&buf)[5]) {
	if (c < 0x80) {
		buf[0] = (char)c;
		buf[1] = 0;
		return true;
	}

	if (c < 0x800) {
		buf[0] = (char)(0xC0 | (c >> 6));
		buf[1] = (char)(0x80 | (c & 0x3F));
		buf[2] = 0;
		return true;
	}

	if (c < 0x10000) {
		buf[0] = (char)(0xE0 | (c >> 12));
		buf[1] = (char)(0x80 | ((c >> 6) & 0x3F));
		buf[2] = (char)(0x80 | (c & 0x3F));
		buf[3] = 0;
		return true;
	}

	if (c <= 0x10FFFF) {
		buf[0] = (char)(0xF0 | (c >> 18));
		buf[1] = (char)(0x80 | ((c >> 12) & 0x3F));
		buf[2] = (char)(0x80 | ((c >> 6) & 0x3F));
		buf[3] = (char)(0x80 | (c & 0x3F));
		buf[4] = 0;
		return true;
	}

	buf[0] = 0;
	return false;
}

// Break maps for procedure breaks and call previews — per CPU type.
// Matches the Windows kBreakMap* tables from uidbgdisasm.cpp.
enum : uint8 {
	kBM_ExpandNone    = 0x00,
	kBM_ExpandAbs16   = 0x01,
	kBM_ExpandAbs16BE = 0x02,
	kBM_ExpandAbs24   = 0x03,
	kBM_ExpandRel8    = 0x04,
	kBM_ExpandRel16BE = 0x05,
	kBM_ExpandMask    = 0x07,

	kBM_EndBlock      = 0x08,
	kBM_Special       = 0x10
};

static const uint8 *ATImGuiGetDisasmBreakMap(ATDebugDisasmMode disasmMode) {
	static constexpr std::array<uint8, 256> kBreakMap6502 = [] {
		std::array<uint8, 256> m {};
		m[0x20] |= kBM_ExpandAbs16;                   // JSR abs
		m[0x4C] |= kBM_ExpandAbs16 | kBM_EndBlock;    // JMP abs
		m[0x6C] |= kBM_EndBlock;                       // JMP (abs)
		m[0x40] |= kBM_EndBlock;                       // RTI
		m[0x60] |= kBM_EndBlock;                       // RTS
		return m;
	}();

	static constexpr std::array<uint8, 256> kBreakMap65C02 = [] {
		std::array<uint8, 256> m = kBreakMap6502;
		m[0x80] |= kBM_EndBlock;                       // BRA rel8
		m[0x7C] |= kBM_EndBlock;                       // JMP (abs,X)
		return m;
	}();

	static constexpr std::array<uint8, 256> kBreakMap65C816 = [] {
		std::array<uint8, 256> m = kBreakMap65C02;
		m[0x22] |= kBM_ExpandAbs24;                    // JSL al
		m[0x5C] |= kBM_EndBlock | kBM_ExpandAbs24;     // JML al
		m[0x6B] |= kBM_EndBlock;                       // RTL
		m[0x82] |= kBM_EndBlock;                       // BRL rl
		return m;
	}();

	static constexpr std::array<uint8, 256> kBreakMap6809 = [] {
		std::array<uint8, 256> m {};
		m[0x16] |= kBM_EndBlock;                       // LBRA
		m[0x17] |= kBM_ExpandRel16BE;                  // LBSR rel16
		m[0x20] |= kBM_EndBlock;                       // BRA
		m[0x39] |= kBM_EndBlock;                       // RTS
		m[0x6E] |= kBM_EndBlock;                       // JMP indexed
		m[0x7E] |= kBM_EndBlock | kBM_ExpandAbs16BE;   // JMP extended
		m[0x8D] |= kBM_ExpandRel8;                     // BSR rel8
		m[0xBD] |= kBM_ExpandAbs16BE;                  // JSR extended
		m[0x35] |= kBM_Special;                        // PULS PC
		m[0x37] |= kBM_Special;                        // PULU PC
		return m;
	}();

	static constexpr std::array<uint8, 256> kBreakMap8048 = [] {
		std::array<uint8, 256> m {};
		m[0x04] |= kBM_EndBlock;  m[0x24] |= kBM_EndBlock;  // JMP
		m[0x44] |= kBM_EndBlock;  m[0x64] |= kBM_EndBlock;  // JMP
		m[0x84] |= kBM_EndBlock;  m[0xA4] |= kBM_EndBlock;  // JMP
		m[0xC4] |= kBM_EndBlock;  m[0xE4] |= kBM_EndBlock;  // JMP
		m[0x83] |= kBM_EndBlock;                              // RET
		m[0x93] |= kBM_EndBlock;                              // RETR
		m[0xB3] |= kBM_EndBlock;                              // JMPP
		return m;
	}();

	static constexpr std::array<uint8, 256> kBreakMap8051 = [] {
		std::array<uint8, 256> m {};
		m[0x01] |= kBM_EndBlock;  m[0x21] |= kBM_EndBlock;  // AJMP
		m[0x41] |= kBM_EndBlock;  m[0x61] |= kBM_EndBlock;  // AJMP
		m[0x81] |= kBM_EndBlock;  m[0xA1] |= kBM_EndBlock;  // AJMP
		m[0xC1] |= kBM_EndBlock;  m[0xE1] |= kBM_EndBlock;  // AJMP
		m[0x02] |= kBM_EndBlock;                              // LJMP
		m[0x22] |= kBM_EndBlock;                              // RET
		m[0x32] |= kBM_EndBlock;                              // RETI
		m[0x73] |= kBM_EndBlock;                              // JMP @A+DPTR
		return m;
	}();

	static constexpr std::array<uint8, 256> kBreakMapZ80 = [] {
		std::array<uint8, 256> m {};
		m[0x18] |= kBM_EndBlock;   // JR r8
		m[0xC3] |= kBM_EndBlock;   // JP
		m[0xE9] |= kBM_EndBlock;   // JP (HL)
		m[0xC9] |= kBM_EndBlock;   // RET
		return m;
	}();

	switch(disasmMode) {
		case kATDebugDisasmMode_6502:    return kBreakMap6502.data();
		case kATDebugDisasmMode_65C02:   return kBreakMap65C02.data();
		case kATDebugDisasmMode_65C816:  return kBreakMap65C816.data();
		case kATDebugDisasmMode_6809:    return kBreakMap6809.data();
		case kATDebugDisasmMode_8048:    return kBreakMap8048.data();
		case kATDebugDisasmMode_8051:    return kBreakMap8051.data();
		case kATDebugDisasmMode_Z80:     return kBreakMapZ80.data();
	}

	return nullptr;
}

static bool ATImGuiDecodeDisasmTarget(
	uint8 breakInfo,
	const ATCPUHistoryEntry& hent,
	uint32 addrBank,
	uint16 nextPC,
	uint32& targetAddr)
{
	switch(breakInfo & kBM_ExpandMask) {
		case kBM_ExpandAbs16:
			targetAddr = addrBank + VDReadUnalignedLEU16(&hent.mOpcode[1]);
			return true;

		case kBM_ExpandAbs16BE:
			targetAddr = addrBank + VDReadUnalignedBEU16(&hent.mOpcode[1]);
			return true;

		case kBM_ExpandAbs24:
			targetAddr = hent.mOpcode[1]
				+ ((uint32)hent.mOpcode[2] << 8)
				+ ((uint32)hent.mOpcode[3] << 16);
			return true;

		case kBM_ExpandRel8:
			targetAddr = addrBank + ((nextPC + (sint8)hent.mOpcode[1]) & 0xFFFF);
			return true;

		case kBM_ExpandRel16BE:
			targetAddr = addrBank + ((nextPC + VDReadUnalignedBES16(&hent.mOpcode[1])) & 0xFFFF);
			return true;
	}

	targetAddr = 0;
	return false;
}

// =========================================================================
// Disassembly pane
// =========================================================================

class ATImGuiDisassemblyPaneImpl final : public ATImGuiDebuggerPane, public IATUIDebuggerDisassemblyPane {
public:
	ATImGuiDisassemblyPaneImpl();

	bool Render() override;
	bool OnPaneCommand(ATUIPaneCommandId id) override;
	void *AsPaneInterface(uint32 iid) override;
	void OnDebuggerSystemStateUpdate(const ATDebuggerSystemState& state) override;
	void OnDebuggerEvent(ATDebugEvent eventId) override;
	void SetPosition(uint32 addr) override;
	bool QuerySelectedBreakpointForTest(int& line, uint32& addr, bool& hasBreakpoint);
	bool ToggleSelectedBreakpointForTest(int& line, uint32& addr, bool& hasBreakpoint);
	bool SimulateRunStopForTest(uint32 frameAddr, int& line, uint32& addr, bool& hasBreakpoint);
	bool GoToSelectedSourceForTest(int& line, uint32& addr);
	bool TargetNavigationForTest(VDStringA& outState);
	bool CallPreviewForTest(VDStringA& outState);

private:
	void RebuildView();
	void NavigateToExpression(const char *expr);
	int ResolveBreakpointLine() const;
	bool ToggleSelectedBreakpoint();
	bool GoToSourceForAddress(uint32 addr);
	void PushAndJump(uint32 fromAddr, uint32 toAddr);
	void GoPrev();
	void GoNext();
	void ToggleCallPreview(int lineIndex);

	struct LineInfo {
		uint32 mAddress;		// full 24-bit address (bank:addr)
		uint16 mPC;				// 16-bit PC within bank
		uint8  mBank;
		bool   mbIsPC;			// this line is the current PC
		bool   mbIsFramePC;		// this line is the frame return PC
		bool   mbIsSeparator;	// procedure break separator line
		bool   mbIsSource;		// source comment line (mixed source/disasm)
		bool   mbHasJumpTarget = false;
		bool   mbIsExpandable = false;
		bool   mbExpanded = false;
		uint32 mNestingLevel = 0;
		uint32 mTargetAddress = 0;
		uint8  mP = 0;
		bool   mbEmulation = false;
		VDStringA mText;
	};

	void AppendSourceLinesForAddress(uint16 pc, uint8 bank, uint32 nestingLevel,
		uint32& lastSourceModuleId, uint32& lastSourceFileId,
		uint32& lastSourceLine, std::vector<LineInfo>& lines);
	void BuildCallPreviewLines(const LineInfo& parent, std::vector<LineInfo>& lines);

	std::vector<LineInfo> mLines;
	bool mbNeedsRebuild = true;
	int mScrollToLine = -1;

	// View state
	uint32 mViewAddr = 0;		// address to center view around
	uint32 mPCAddr = 0;
	uint32 mFramePCAddr = 0;

	// Address bar
	char mAddrInput[64] = {};

	// Selection and context menu state
	int mSelectedLine = -1;			// line clicked by user (for F9 etc.)
	int mContextLine = -1;
	uint32 mContextAddr = 0;
	uint32 mContextTargetAddr = 0;
	bool mbContextAddrValid = false;
	bool mbContextTargetValid = false;

	// Native disassembly keeps a circular jump history for target links.
	std::array<uint32, 64> mHistory {};
	uint32 mHistoryNext = 0;
	uint32 mHistoryLenBack = 0;
	uint32 mHistoryLenForward = 0;

	// Display settings — backed by global debugger settings with local views.
	// ATDebuggerSettingView syncs with the global setting and calls the
	// refresh callback when the value changes.
	ATDebuggerSettingView<bool> mbShowCodeBytes;
	ATDebuggerSettingView<bool> mbShowLabels;
	ATDebuggerSettingView<bool> mbShowLabelNamespaces;
	ATDebuggerSettingView<bool> mbShowProcedureBreaks;
	ATDebuggerSettingView<bool> mbShowCallPreviews;
	ATDebuggerSettingView<bool> mbShowSourceInDisasm;
	ATDebuggerSettingView<ATDebugger816MXPredictionMode> m816MXPredictionMode;
	ATDebuggerSettingView<bool> mb816PredictD;

	// Display settings matching Windows defaults
	static constexpr int kLinesAbovePC = 8;
	static constexpr int kTotalLines = 48;
};

ATImGuiDisassemblyPaneImpl::ATImGuiDisassemblyPaneImpl()
	: ATImGuiDebuggerPane(kATUIPaneId_Disassembly, "Disassembly")
{
	const auto refresh = [this] { mbNeedsRebuild = true; };
	mbShowCodeBytes.Attach(g_ATDbgSettingShowCodeBytes, refresh);
	mbShowLabels.Attach(g_ATDbgSettingShowLabels, refresh);
	mbShowLabelNamespaces.Attach(g_ATDbgSettingShowLabelNamespaces, refresh);
	mbShowProcedureBreaks.Attach(g_ATDbgSettingShowProcedureBreaks, refresh);
	mbShowCallPreviews.Attach(g_ATDbgSettingShowCallPreviews, refresh);
	mbShowSourceInDisasm.Attach(g_ATDbgSettingShowSourceInDisasm, refresh);
	m816MXPredictionMode.Attach(g_ATDbgSetting816MXPredictionMode, refresh);
	mb816PredictD.Attach(g_ATDbgSetting816PredictD, refresh);
}

void ATImGuiDisassemblyPaneImpl::OnDebuggerSystemStateUpdate(const ATDebuggerSystemState& state) {
	ATImGuiDebuggerPane::OnDebuggerSystemStateUpdate(state);

	if (state.mbRunning) {
		mbNeedsRebuild = true;
		return;
	}

	// Windows Altirra always snaps the disassembly view to the current PC
	// when execution stops (breakpoint, step, etc.).  There is no "Follow PC"
	// toggle — the view unconditionally centers on the frame PC.
	uint32 newPCAddr = (uint32)state.mInsnPC + ((uint32)state.mPCBank << 16);
	uint32 newFramePCAddr = state.mFrameExtPC;

	mPCAddr = newPCAddr;
	mFramePCAddr = newFramePCAddr;

	mViewAddr = newFramePCAddr;
	mbNeedsRebuild = true;
}

void ATImGuiDisassemblyPaneImpl::OnDebuggerEvent(ATDebugEvent eventId) {
	// Breakpoint changes only affect coloring — no need to rebuild the full
	// view (which would reset scroll position and selection).  ImGui redraws
	// every frame so the breakpoint indicators update automatically.
	// Symbol changes require a full rebuild because label text changes.
	if (eventId == kATDebugEvent_SymbolsChanged)
		mbNeedsRebuild = true;
}

void ATImGuiDisassemblyPaneImpl::SetPosition(uint32 addr) {
	mViewAddr = addr;
	mbNeedsRebuild = true;
}

void ATImGuiDisassemblyPaneImpl::NavigateToExpression(const char *expr) {
	IATDebugger *dbg = ATGetDebugger();
	if (!dbg) return;

	try {
		sint32 addr = dbg->EvaluateThrow(expr);
		SetPosition((uint32)addr);
	} catch (const MyError&) {
		// Invalid expression
	}
}

bool ATImGuiDisassemblyPaneImpl::OnPaneCommand(ATUIPaneCommandId id) {
	switch(id) {
		case kATUIPaneCommandId_DebugToggleBreakpoint:
			return ToggleSelectedBreakpoint();
		default:
			break;
	}

	return false;
}

void *ATImGuiDisassemblyPaneImpl::AsPaneInterface(uint32 iid) {
	if (iid == IATUIDebuggerDisassemblyPane::kTypeID)
		return static_cast<IATUIDebuggerDisassemblyPane *>(this);

	return ATImGuiDebuggerPane::AsPaneInterface(iid);
}

int ATImGuiDisassemblyPaneImpl::ResolveBreakpointLine() const {
	if (mLines.empty())
		return -1;

	int line = mSelectedLine;
	if (line < 0)
		line = mScrollToLine;

	if (line < 0)
		return -1;

	if (line >= (int)mLines.size())
		line = (int)mLines.size() - 1;

	const auto isInsnLine = [this](int idx) {
		const LineInfo& li = mLines[idx];
		return !li.mbIsSeparator && !li.mbIsSource;
	};

	if (isInsnLine(line))
		return line;

	for (int i = line + 1; i < (int)mLines.size(); ++i) {
		if (isInsnLine(i))
			return i;
	}

	for (int i = line - 1; i >= 0; --i) {
		if (isInsnLine(i))
			return i;
	}

	return -1;
}

bool ATImGuiDisassemblyPaneImpl::ToggleSelectedBreakpoint() {
	IATDebugger *dbg = ATGetDebugger();
	if (!dbg)
		return true;

	const int line = ResolveBreakpointLine();
	if (line < 0)
		return true;

	mSelectedLine = line;
	dbg->ToggleBreakpoint(mLines[line].mAddress);

	return true;
}

bool ATImGuiDisassemblyPaneImpl::GoToSourceForAddress(uint32 addr) {
	if (ATImGuiConsoleShowSource(addr))
		return true;

	VDStringW s;
	if (IATDebugger *dbg = ATGetDebugger()) {
		s.sprintf(L"There is no source line associated with the address: %hs.",
			dbg->GetAddressText(addr, true).c_str());
	} else {
		s.sprintf(L"There is no source line associated with the address: $%04X.",
			addr & 0xffff);
	}

	ATUIShowError(nullptr, s.c_str());
	return false;
}

bool ATImGuiDisassemblyPaneImpl::QuerySelectedBreakpointForTest(
	int& line,
	uint32& addr,
	bool& hasBreakpoint)
{
	if (mbNeedsRebuild)
		RebuildView();

	line = mSelectedLine;
	addr = 0;
	hasBreakpoint = false;

	if (mSelectedLine < 0 || (size_t)mSelectedLine >= mLines.size())
		return false;

	const LineInfo& li = mLines[mSelectedLine];
	if (li.mbIsSeparator || li.mbIsSource)
		return false;

	addr = li.mAddress;

	if (IATDebugger *dbg = ATGetDebugger())
		hasBreakpoint = dbg->IsBreakpointAtPC(addr);

	return true;
}

bool ATImGuiDisassemblyPaneImpl::ToggleSelectedBreakpointForTest(
	int& line,
	uint32& addr,
	bool& hasBreakpoint)
{
	if (!QuerySelectedBreakpointForTest(line, addr, hasBreakpoint))
		return false;

	OnPaneCommand(kATUIPaneCommandId_DebugToggleBreakpoint);

	if (IATDebugger *dbg = ATGetDebugger())
		hasBreakpoint = dbg->IsBreakpointAtPC(addr);

	return true;
}

bool ATImGuiDisassemblyPaneImpl::SimulateRunStopForTest(
	uint32 frameAddr,
	int& line,
	uint32& addr,
	bool& hasBreakpoint)
{
	if (!mbStateValid || !mLastState.mpDebugTarget)
		return false;

	ATDebuggerSystemState runningState = mLastState;
	runningState.mbRunning = true;
	OnDebuggerSystemStateUpdate(runningState);

	ATDebuggerSystemState stoppedState = runningState;
	stoppedState.mbRunning = false;
	stoppedState.mPC = (uint16)frameAddr;
	stoppedState.mInsnPC = (uint16)frameAddr;
	stoppedState.mPCBank = (uint8)(frameAddr >> 16);
	stoppedState.mFrameExtPC = frameAddr;
	OnDebuggerSystemStateUpdate(stoppedState);

	return QuerySelectedBreakpointForTest(line, addr, hasBreakpoint);
}

bool ATImGuiDisassemblyPaneImpl::GoToSelectedSourceForTest(int& line, uint32& addr) {
	bool hasBreakpoint = false;
	if (!QuerySelectedBreakpointForTest(line, addr, hasBreakpoint))
		return false;

	return GoToSourceForAddress(addr);
}

bool ATImGuiDisassemblyPaneImpl::TargetNavigationForTest(VDStringA& outState) {
	IATDebugger *dbg = ATGetDebugger();
	if (!dbg)
		return false;

	static constexpr uint8 kSourceBytes[] = {
		0x20, 0x10, 0x20,	// JSR $2010
		0xEA,				// NOP
		0x60				// RTS
	};
	static constexpr uint8 kTargetBytes[] = {
		0x60				// RTS
	};

	dbg->WriteMemoryCPU(0x2000, kSourceBytes, sizeof kSourceBytes);
	dbg->WriteMemoryCPU(0x2010, kTargetBytes, sizeof kTargetBytes);
	SetPosition(0x2000);

	if (mbNeedsRebuild)
		RebuildView();

	int targetLine = -1;
	for (size_t i = 0; i < mLines.size(); ++i) {
		const LineInfo& li = mLines[i];
		if (!li.mbIsSeparator && !li.mbIsSource && li.mbHasJumpTarget) {
			targetLine = (int)i;
			break;
		}
	}

	if (targetLine < 0)
		return false;

	const uint32 startViewAddr = mViewAddr;
	const uint32 fromAddr = mLines[targetLine].mAddress;
	const uint32 targetAddr = mLines[targetLine].mTargetAddress;

	PushAndJump(fromAddr, targetAddr);
	const uint32 afterJumpViewAddr = mViewAddr;
	const uint32 afterJumpBack = mHistoryLenBack;
	const uint32 afterJumpForward = mHistoryLenForward;

	if (mbNeedsRebuild)
		RebuildView();
	const int afterJumpSelectedLine = mSelectedLine;

	GoPrev();
	const uint32 afterBackViewAddr = mViewAddr;
	const uint32 afterBackBack = mHistoryLenBack;
	const uint32 afterBackForward = mHistoryLenForward;

	if (mbNeedsRebuild)
		RebuildView();
	const int afterBackSelectedLine = mSelectedLine;

	GoNext();
	const uint32 afterForwardViewAddr = mViewAddr;
	const uint32 afterForwardBack = mHistoryLenBack;
	const uint32 afterForwardForward = mHistoryLenForward;

	if (mbNeedsRebuild)
		RebuildView();
	const int afterForwardSelectedLine = mSelectedLine;

	outState.sprintf(
		"seeded=1 line=%d start=$%06X from=$%06X target=$%06X "
		"after_jump=$%06X jump_selected=%d jump_back=%u jump_forward=%u "
		"after_back=$%06X back_selected=%d back_back=%u back_forward=%u "
		"after_forward=$%06X forward_selected=%d forward_back=%u forward_forward=%u",
		targetLine,
		startViewAddr,
		fromAddr,
		targetAddr,
		afterJumpViewAddr,
		afterJumpSelectedLine,
		afterJumpBack,
		afterJumpForward,
		afterBackViewAddr,
		afterBackSelectedLine,
		afterBackBack,
		afterBackForward,
		afterForwardViewAddr,
		afterForwardSelectedLine,
		afterForwardBack,
		afterForwardForward);
	return true;
}

bool ATImGuiDisassemblyPaneImpl::CallPreviewForTest(VDStringA& outState) {
	IATDebugger *dbg = ATGetDebugger();
	if (!dbg)
		return false;

	static constexpr uint8 kSourceBytes[] = {
		0x20, 0x10, 0x20,	// JSR $2010
		0xEA,				// NOP
		0x60				// RTS
	};
	static constexpr uint8 kTargetBytes[] = {
		0xEA,				// NOP
		0x60				// RTS
	};

	dbg->WriteMemoryCPU(0x2000, kSourceBytes, sizeof kSourceBytes);
	dbg->WriteMemoryCPU(0x2010, kTargetBytes, sizeof kTargetBytes);

	const bool oldShowCallPreviews = (bool)mbShowCallPreviews;
	const bool oldShowSource = (bool)mbShowSourceInDisasm;
	mbShowCallPreviews = true;
	mbShowSourceInDisasm = true;

	SetPosition(0x2000);
	if (mbNeedsRebuild)
		RebuildView();

	int previewLine = -1;
	for (size_t i = 0; i < mLines.size(); ++i) {
		const LineInfo& li = mLines[i];
		if (!li.mbIsSeparator && !li.mbIsSource && li.mbIsExpandable) {
			previewLine = (int)i;
			break;
		}
	}

	if (previewLine < 0) {
		mbShowCallPreviews = oldShowCallPreviews;
		mbShowSourceInDisasm = oldShowSource;
		return false;
	}

	const size_t beforeLines = mLines.size();
	const uint32 fromAddr = mLines[previewLine].mAddress;
	const uint32 targetAddr = mLines[previewLine].mTargetAddress;
	const bool beforeExpanded = mLines[previewLine].mbExpanded;
	const bool beforeHasExpand = mLines[previewLine].mText.find("[expand]") != VDStringA::npos;

	ToggleCallPreview(previewLine);

	const size_t afterExpandLines = mLines.size();
	const bool afterExpanded = mLines[previewLine].mbExpanded;
	const bool afterHasContract = mLines[previewLine].mText.find("[contract]") != VDStringA::npos;
	size_t nestedInsnLines = 0;
	size_t nestedSourceLines = 0;
	size_t nestedExpandableLines = 0;
	for (size_t i = (size_t)previewLine + 1; i < mLines.size(); ++i) {
		const LineInfo& li = mLines[i];
		if (li.mNestingLevel <= mLines[previewLine].mNestingLevel)
			break;

		if (li.mbIsSource)
			++nestedSourceLines;
		else if (!li.mbIsSeparator) {
			++nestedInsnLines;
			if (li.mbIsExpandable)
				++nestedExpandableLines;
		}
	}

	ToggleCallPreview(previewLine);

	const size_t afterContractLines = mLines.size();
	const bool afterContractExpanded = mLines[previewLine].mbExpanded;
	const bool afterHasExpand = mLines[previewLine].mText.find("[expand]") != VDStringA::npos;

	mbShowCallPreviews = oldShowCallPreviews;
	mbShowSourceInDisasm = oldShowSource;
	mbNeedsRebuild = true;

	outState.sprintf(
		"line=%d from=$%06X target=$%06X before_lines=%zu before_expanded=%d "
		"before_expand_link=%d after_expand_lines=%zu after_expanded=%d "
		"after_contract_link=%d nested_insn=%zu nested_source=%zu "
		"nested_expandable=%zu after_contract_lines=%zu contract_expanded=%d "
		"after_expand_link=%d",
		previewLine,
		fromAddr,
		targetAddr,
		beforeLines,
		beforeExpanded ? 1 : 0,
		beforeHasExpand ? 1 : 0,
		afterExpandLines,
		afterExpanded ? 1 : 0,
		afterHasContract ? 1 : 0,
		nestedInsnLines,
		nestedSourceLines,
		nestedExpandableLines,
		afterContractLines,
		afterContractExpanded ? 1 : 0,
		afterHasExpand ? 1 : 0);
	return true;
}

void ATImGuiDisassemblyPaneImpl::PushAndJump(uint32 fromAddr, uint32 toAddr) {
	mHistory[mHistoryNext] = fromAddr;
	if (++mHistoryNext >= mHistory.size())
		mHistoryNext = 0;

	if (mHistoryLenBack < mHistory.size())
		++mHistoryLenBack;

	mHistoryLenForward = 0;
	SetPosition(toAddr);
}

void ATImGuiDisassemblyPaneImpl::GoPrev() {
	if (!mHistoryLenBack)
		return;

	++mHistoryLenForward;
	--mHistoryLenBack;

	if (mHistoryNext == 0)
		mHistoryNext = (uint32)mHistory.size();

	--mHistoryNext;

	const uint32 nextAddr = mHistory[mHistoryNext];
	mHistory[mHistoryNext] = mViewAddr;
	SetPosition(nextAddr);
}

void ATImGuiDisassemblyPaneImpl::GoNext() {
	if (!mHistoryLenForward)
		return;

	++mHistoryLenBack;
	--mHistoryLenForward;

	const uint32 nextAddr = mHistory[mHistoryNext];
	if (++mHistoryNext >= mHistory.size())
		mHistoryNext = 0;

	mHistory[mHistoryNext] = mViewAddr;
	SetPosition(nextAddr);
}

void ATImGuiDisassemblyPaneImpl::ToggleCallPreview(int lineIndex) {
	if (lineIndex < 0 || (size_t)lineIndex >= mLines.size())
		return;

	LineInfo& li = mLines[lineIndex];
	if (!li.mbIsExpandable)
		return;

	if (li.mbExpanded) {
		const uint32 parentNesting = li.mNestingLevel;
		size_t rangeEnd = (size_t)lineIndex + 1;
		while (rangeEnd < mLines.size()
			&& mLines[rangeEnd].mNestingLevel > parentNesting) {
			++rangeEnd;
		}

		li.mbExpanded = false;
		size_t pos = li.mText.find("[contract]");
		if (pos != VDStringA::npos)
			li.mText.replace(pos, 10, "[expand]", 8);

		mLines.erase(mLines.begin() + lineIndex + 1, mLines.begin() + rangeEnd);
		return;
	}

	std::vector<LineInfo> previewLines;
	BuildCallPreviewLines(li, previewLines);
	if (previewLines.empty())
		return;

	li.mbExpanded = true;
	size_t pos = li.mText.find("[expand]");
	if (pos != VDStringA::npos)
		li.mText.replace(pos, 8, "[contract]", 10);

	mLines.insert(
		mLines.begin() + lineIndex + 1,
		previewLines.begin(),
		previewLines.end());
}

void ATImGuiDisassemblyPaneImpl::AppendSourceLinesForAddress(
	uint16 pc,
	uint8 bank,
	uint32 nestingLevel,
	uint32& lastSourceModuleId,
	uint32& lastSourceFileId,
	uint32& lastSourceLine,
	std::vector<LineInfo>& lines)
{
	if (!mbShowSourceInDisasm)
		return;

	IATDebuggerSymbolLookup *symbolLookup = ATGetDebuggerSymbolLookup();
	if (!symbolLookup)
		return;

	uint32 moduleId;
	ATSourceLineInfo lineInfo;
	if (!symbolLookup->LookupLine(pc, false, moduleId, lineInfo)
		|| lineInfo.mOffset != pc
		|| !lineInfo.mLine) {
		return;
	}

	if (lastSourceModuleId != moduleId || lastSourceFileId != lineInfo.mFileId) {
		lastSourceModuleId = moduleId;
		lastSourceFileId = lineInfo.mFileId;
		lastSourceLine = 0;
	}

	if (lineInfo.mLine == lastSourceLine)
		return;

	ATDebuggerSourceFileInfo sourceFileInfo;
	const bool haveSourcePath = symbolLookup->GetSourceFilePath(
		moduleId, lineInfo.mFileId, sourceFileInfo);

	IATSourceWindow *sw = nullptr;
	if (haveSourcePath)
		sw = ATGetSourceWindow(sourceFileInfo.mSourcePath.c_str());

	VDStringA srcText;
	if (sw) {
		VDStringW wline = sw->ReadLine((int)lineInfo.mLine - 1);
		if (!wline.empty()) {
			if (wline.back() == L'\n')
				wline.pop_back();

			VDStringA lineU8 = VDTextWToU8(wline);
			srcText.sprintf(";%4u  %s", lineInfo.mLine, lineU8.c_str());
		}
	}

	if (srcText.empty()) {
		const wchar_t *pathStr = sw ? sw->GetPath() : sourceFileInfo.mSourcePath.c_str();
		VDStringA pathU8 = haveSourcePath
			? VDTextWToU8(VDStringSpanW(pathStr))
			: VDStringA();

		srcText.sprintf(";[%s:%u]", pathU8.c_str(), lineInfo.mLine);
	}

	LineInfo srcLi;
	srcLi.mAddress = ((uint32)bank << 16) + pc;
	srcLi.mPC = pc;
	srcLi.mBank = bank;
	srcLi.mbIsPC = false;
	srcLi.mbIsFramePC = false;
	srcLi.mbIsSeparator = false;
	srcLi.mbIsSource = true;
	srcLi.mNestingLevel = nestingLevel;
	srcLi.mText.assign(nestingLevel * 2, ' ');
	srcLi.mText += srcText;
	lines.push_back(std::move(srcLi));

	lastSourceLine = lineInfo.mLine;
}

void ATImGuiDisassemblyPaneImpl::BuildCallPreviewLines(
	const LineInfo& parent,
	std::vector<LineInfo>& lines)
{
	if (!mbStateValid || !mLastState.mpDebugTarget || mLastState.mbRunning)
		return;

	IATDebugTarget *target = mLastState.mpDebugTarget;
	const ATDebugDisasmMode disasmMode = target->GetDisasmMode();
	const uint8 *breakMap = ATImGuiGetDisasmBreakMap(disasmMode);

	ATCPUHistoryEntry hent;
	ATDisassembleCaptureRegisterContext(target, hent);
	hent.mP = parent.mP;
	hent.mbEmulation = parent.mbEmulation;

	if (disasmMode == kATDebugDisasmMode_65C816) {
		if (!((bool)mb816PredictD) || hent.mD == 0)
			hent.mD = 0;
	}

	const bool autoModeSwitching = (disasmMode == kATDebugDisasmMode_65C816
		&& (ATDebugger816MXPredictionMode)m816MXPredictionMode
			== ATDebugger816MXPredictionMode::Auto);

	const bool showCallPreviews = (bool)mbShowCallPreviews;
	const bool showCodeBytes = (bool)mbShowCodeBytes;
	const bool showLabels = (bool)mbShowLabels;
	const bool showLabelNS = (bool)mbShowLabelNamespaces;
	const uint32 nestingLevel = parent.mNestingLevel + 1;
	const uint32 indent = nestingLevel * 2;

	uint16 pc = (uint16)parent.mTargetAddress;
	uint8 bank = (uint8)(parent.mTargetAddress >> 16);
	uint32 addrBank = (uint32)bank << 16;
	uint32 bytesDecoded = 0;
	uint32 lastSourceModuleId = 0;
	uint32 lastSourceFileId = 0;
	uint32 lastSourceLine = 0;

	VDStringA buf;
	for (uint32 i = 0; i < 20 && bytesDecoded < 200; ++i) {
		if (disasmMode == kATDebugDisasmMode_6502)
			ATDisassembleCaptureInsnContext(target, addrBank + pc, hent);
		else
			ATDisassembleCaptureInsnContext(target, pc, bank, hent);

		AppendSourceLinesForAddress(pc, bank, nestingLevel,
			lastSourceModuleId, lastSourceFileId, lastSourceLine, lines);

		buf.assign(indent, ' ');
		ATDisasmResult result = ATDisassembleInsn(buf,
			target,
			disasmMode,
			hent,
			false,
			false,
			true,
			showCodeBytes,
			showLabels,
			false,
			false,
			showLabelNS,
			true,
			true);

		const uint32 fullAddr = addrBank + pc;
		bool hasJumpTarget = false;
		bool isExpandable = false;
		bool endBlock = false;
		uint32 jumpTargetAddr = 0;

		if (breakMap) {
			const uint8 opcode = hent.mOpcode[0];
			uint8 breakInfo = breakMap[opcode];

			if (breakInfo & kBM_Special)
				breakInfo = (hent.mOpcode[1] & 0x80) ? kBM_EndBlock : 0;

			if (breakInfo & kBM_ExpandMask) {
				hasJumpTarget = ATImGuiDecodeDisasmTarget(
					breakInfo,
					hent,
					addrBank,
					result.mNextPC,
					jumpTargetAddr);

				if (showCallPreviews) {
					size_t len = buf.size();
					if (len < 50 + indent)
						buf.append(50 + indent - len, ' ');
					buf += " ;[expand]";
					isExpandable = true;
				}
			}

			endBlock = (breakInfo & kBM_EndBlock) != 0;
		}

		LineInfo li;
		li.mAddress = fullAddr;
		li.mPC = pc;
		li.mBank = bank;
		li.mbIsPC = (fullAddr == mPCAddr);
		li.mbIsFramePC = (fullAddr == mFramePCAddr);
		li.mbIsSeparator = false;
		li.mbIsSource = false;
		li.mbHasJumpTarget = hasJumpTarget;
		li.mbIsExpandable = isExpandable;
		li.mNestingLevel = nestingLevel;
		li.mTargetAddress = jumpTargetAddr;
		li.mP = hent.mP;
		li.mbEmulation = hent.mbEmulation;
		li.mText = buf;
		lines.push_back(std::move(li));

		const uint16 nextPC = result.mNextPC;
		uint16 bytesThisInsn = nextPC - pc;
		if (!bytesThisInsn)
			bytesThisInsn = 1;
		bytesDecoded += bytesThisInsn;

		if (endBlock)
			break;

		if (autoModeSwitching)
			ATDisassemblePredictContext(hent, disasmMode);

		pc = nextPC;
	}
}

void ATImGuiDisassemblyPaneImpl::RebuildView() {
	mbNeedsRebuild = false;
	mLines.clear();
	mScrollToLine = -1;
	mSelectedLine = -1;

	if (!mbStateValid || !mLastState.mpDebugTarget || mLastState.mbRunning)
		return;

	IATDebugTarget *target = mLastState.mpDebugTarget;
	const ATDebugDisasmMode disasmMode = target->GetDisasmMode();

	ATCPUHistoryEntry hent;
	ATDisassembleCaptureRegisterContext(target, hent);

	// Handle 65C816 M/X mode prediction and D register state
	if (disasmMode == kATDebugDisasmMode_65C816) {
		if (!((bool)mb816PredictD) || hent.mD == 0)
			hent.mD = 0;

		bool forcedMode = false;
		uint8 forcedModeMX = 0;
		bool forcedModeEmulation = false;

		switch((ATDebugger816MXPredictionMode)m816MXPredictionMode) {
			case ATDebugger816MXPredictionMode::Auto:
				break;
			case ATDebugger816MXPredictionMode::CurrentContext:
				break;
			case ATDebugger816MXPredictionMode::M8X8:
				forcedMode = true;
				forcedModeMX = 0x30;
				break;
			case ATDebugger816MXPredictionMode::M8X16:
				forcedMode = true;
				forcedModeMX = 0x20;
				break;
			case ATDebugger816MXPredictionMode::M16X8:
				forcedMode = true;
				forcedModeMX = 0x10;
				break;
			case ATDebugger816MXPredictionMode::M16X16:
				forcedMode = true;
				forcedModeMX = 0x00;
				break;
			case ATDebugger816MXPredictionMode::Emulation:
				forcedMode = true;
				forcedModeMX = 0x30;
				forcedModeEmulation = true;
				break;
		}

		if (forcedMode) {
			hent.mP = (hent.mP & 0xCF) + forcedModeMX;
			hent.mbEmulation = forcedModeEmulation;
		}
	}

	bool autoModeSwitching = (disasmMode == kATDebugDisasmMode_65C816
		&& (ATDebugger816MXPredictionMode)m816MXPredictionMode == ATDebugger816MXPredictionMode::Auto);

	// Save initial state for PC restore during auto mode switching.
	// When auto-switching, the M/X flags are predicted through the code
	// flow, but at the actual PC we should use the real register context.
	const ATCPUHistoryEntry initialState = hent;

	uint16 focusPC = (uint16)mViewAddr;
	uint8 focusBank = (uint8)(mViewAddr >> 16);
	uint32 addrBank = (uint32)focusBank << 16;

	// Find a valid instruction boundary before the focus address
	uint16 startAddr;
	if (focusPC >= kLinesAbovePC * 3)
		startAddr = ATDisassembleGetFirstAnchor(target, focusPC - kLinesAbovePC * 3, focusPC, addrBank);
	else
		startAddr = ATDisassembleGetFirstAnchor(target, 0, focusPC, addrBank);

	const uint8 *breakMap = ATImGuiGetDisasmBreakMap(disasmMode);

	VDStringA buf;
	uint16 pc = startAddr;
	int pcLine = -1;
	int framePCLine = -1;
	int focusLine = -1;

	const bool showProcBreaks = (bool)mbShowProcedureBreaks;
	const bool showCallPreviews = (bool)mbShowCallPreviews;
	const bool showCodeBytes = (bool)mbShowCodeBytes;
	const bool showLabels = (bool)mbShowLabels;
	const bool showLabelNS = (bool)mbShowLabelNamespaces;

	// Source interleaving state — tracks which source file/line was last
	// shown to avoid repeating the same source line for consecutive instructions.
	uint32 lastSourceModuleId = 0;
	uint32 lastSourceFileId = 0;
	uint32 lastSourceLine = 0;

	for (int i = 0; i < kTotalLines; ++i) {
		// When auto mode switching, restore real register context at the
		// actual PC address (matching Windows Disassemble() logic).
		if (autoModeSwitching && pc == (uint16)mPCAddr)
			hent = initialState;

		// Capture instruction bytes — dispatch matches Windows:
		// 6502 uses globalAddr form, others use (pc, bank) form.
		if (disasmMode == kATDebugDisasmMode_6502)
			ATDisassembleCaptureInsnContext(target, addrBank + pc, hent);
		else
			ATDisassembleCaptureInsnContext(target, pc, focusBank, hent);

		// Mixed source/disasm: insert source comment lines before instruction.
		// The same helper is used for expanded call previews so nested blocks
		// retain the native source interleaving behavior.
		AppendSourceLinesForAddress(pc, focusBank, 0,
			lastSourceModuleId, lastSourceFileId, lastSourceLine, mLines);

		buf.clear();
		ATDisasmResult result = ATDisassembleInsn(buf,
			target,
			disasmMode,
			hent,
			false,			// decodeReferences (matches Windows)
			false,			// decodeRefsHistory
			true,			// showPCAddress
			showCodeBytes,	// showCodeBytes
			showLabels,		// showLabels
			false,			// lowercaseOps
			false,			// wideOpcode
			showLabelNS,	// showLabelNamespaces
			true,			// showSymbols
			true			// showGlobalPC (matches Windows)
		);

		uint32 fullAddr = addrBank + pc;
		bool isPC = (fullAddr == mPCAddr);
		bool isFramePC = (fullAddr == mFramePCAddr);

		if (isPC)
			pcLine = (int)mLines.size();
		if (isFramePC)
			framePCLine = (int)mLines.size();
		// First line at or past the focus address
		if (focusLine < 0 && pc >= focusPC)
			focusLine = (int)mLines.size();

		// Check break map for call previews and procedure breaks
		bool procSep = false;
		bool hasJumpTarget = false;
		bool isExpandable = false;
		uint32 jumpTargetAddr = 0;
		if (breakMap) {
			const uint8 opcode = hent.mOpcode[0];
			uint8 breakInfo = breakMap[opcode];

			// Handle 6809 special cases (PULS/PULU with PC bit)
			if (breakInfo & kBM_Special) {
				breakInfo = (hent.mOpcode[1] & 0x80) ? kBM_EndBlock : 0;
			}

			// Append call preview for expandable instructions (JSR, JSL, etc.)
			if (breakInfo & kBM_ExpandMask) {
				hasJumpTarget = ATImGuiDecodeDisasmTarget(
					breakInfo,
					hent,
					addrBank,
					result.mNextPC,
					jumpTargetAddr);

				if (showCallPreviews) {
					size_t len = buf.size();
					if (len < 50)
						buf.append(50 - len, ' ');
					buf += " ;[expand]";
					isExpandable = true;
				}
			}

			// Procedure break separator after block-ending instructions
			if ((breakInfo & kBM_EndBlock) && showProcBreaks) {
				procSep = true;
			}
		}

		LineInfo li;
		li.mAddress = fullAddr;
		li.mPC = pc;
		li.mBank = focusBank;
		li.mbIsPC = isPC;
		li.mbIsFramePC = isFramePC;
		li.mbIsSeparator = false;
		li.mbIsSource = false;
		li.mbHasJumpTarget = hasJumpTarget;
		li.mbIsExpandable = isExpandable;
		li.mNestingLevel = 0;
		li.mTargetAddress = jumpTargetAddr;
		li.mP = hent.mP;
		li.mbEmulation = hent.mbEmulation;
		li.mText = buf;
		mLines.push_back(std::move(li));

		// Insert procedure break separator line after instruction
		if (procSep) {
			LineInfo sep;
			sep.mAddress = fullAddr;
			sep.mPC = pc;
			sep.mBank = focusBank;
			sep.mbIsPC = false;
			sep.mbIsFramePC = false;
			sep.mbIsSeparator = true;
			sep.mbIsSource = false;
			sep.mText = ";--------------------------------------------------";
			mLines.push_back(std::move(sep));
		}

		// Auto-switch mode for 65C816 — must happen before pc advances
		if (autoModeSwitching)
			ATDisassemblePredictContext(hent, disasmMode);

		pc = result.mNextPC;
	}

	// Selection/scroll priority follows native ATDisassemblyWindow:
	// after a rebuild or stop, the editor cursor moves to frame PC, then PC,
	// then the navigated focus line. Debug.ToggleBreakpoint/F9 use this cursor.
	if (framePCLine >= 0)
		mScrollToLine = framePCLine;
	else if (pcLine >= 0)
		mScrollToLine = pcLine;
	else if (focusLine >= 0)
		mScrollToLine = focusLine;

	mSelectedLine = mScrollToLine;
}

bool ATImGuiDisassemblyPaneImpl::Render() {
	bool open = true;

	if (mbFocusRequested) {
		ImGui::SetNextWindowFocus();
		mbFocusRequested = false;
	}

	if (!ImGui::Begin(mTitle.c_str(), &open)) {
		mbHasFocus = false;
		ImGui::End();
		return open;
	}
	mbHasFocus = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

	// Address bar
	{
		ImGui::SetNextItemWidth(140);
		if (ImGui::InputText("##addr", mAddrInput, sizeof(mAddrInput),
				ImGuiInputTextFlags_EnterReturnsTrue)) {
			NavigateToExpression(mAddrInput);
		}
		ImGui::SameLine();
		if (ImGui::Button("Go"))
			NavigateToExpression(mAddrInput);
		ImGui::SameLine();
		ImGui::BeginDisabled(!mHistoryLenBack);
		if (ImGui::Button("<"))
			GoPrev();
		ImGui::EndDisabled();
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Back");
		ImGui::SameLine();
		ImGui::BeginDisabled(!mHistoryLenForward);
		if (ImGui::Button(">"))
			GoNext();
		ImGui::EndDisabled();
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Forward");
		ImGui::SameLine();
		if (mbStateValid && mLastState.mbRunning) {
			// Break button — stops execution (nice addition over Windows)
			if (ImGui::Button("Break")) {
				IATDebugger *dbg = ATGetDebugger();
				if (dbg)
					dbg->Break();
			}
		} else {
			// Show PC button — snaps view to current PC while paused
			if (ImGui::Button("Show PC")) {
				mViewAddr = mFramePCAddr;
				mbNeedsRebuild = true;
			}
		}
		ImGui::Separator();
	}

	if (mbNeedsRebuild)
		RebuildView();

	if (mLines.empty()) {
		if (mbStateValid && mLastState.mbRunning) {
			ImGui::TextDisabled("(running)");
		} else {
			ImGui::TextDisabled("(no disassembly)");
		}
		ImGui::End();
		return open;
	}

	const float lineHeight = ImGui::GetTextLineHeightWithSpacing();

	if (ImGui::BeginChild("DisasmScroll", ImVec2(0, 0), ImGuiChildFlags_None,
			ImGuiWindowFlags_HorizontalScrollbar)) {

		IATDebugger *dbg = ATGetDebugger();

		ImGuiListClipper clipper;
		clipper.Begin((int)mLines.size());

		while (clipper.Step()) {
			for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
				const LineInfo& li = mLines[i];

				// Separator lines (procedure breaks) and source comment lines
				if (li.mbIsSeparator) {
					ImGui::TextDisabled("%s", li.mText.c_str());
					continue;
				}
				if (li.mbIsSource) {
					ImGui::TextColored(ImVec4(0.4f, 0.7f, 0.4f, 1.0f), "%s", li.mText.c_str());
					continue;
				}

				// Determine highlight color
				ImU32 bgColor = 0;
				if (li.mbIsPC)
					bgColor = IM_COL32(128, 128, 0, 80);
				else if (li.mbIsFramePC)
					bgColor = IM_COL32(0, 128, 128, 80);
				else if (li.mNestingLevel)
					bgColor = IM_COL32(64, 64, 64, 55);

				bool hasBP = false;
				if (dbg)
					hasBP = dbg->IsBreakpointAtPC(li.mAddress);

				ImVec2 pos = ImGui::GetCursorScreenPos();
				float width = ImGui::GetContentRegionAvail().x;

				// Background highlight
				if (bgColor) {
					ImGui::GetWindowDrawList()->AddRectFilled(
						pos, ImVec2(pos.x + width, pos.y + lineHeight), bgColor);
				}

				// Breakpoint indicator (red bar on the left)
				if (hasBP) {
					ImGui::GetWindowDrawList()->AddRectFilled(
						pos, ImVec2(pos.x + 8, pos.y + lineHeight),
						IM_COL32(255, 80, 80, 200));
				}

				// Arrow indicator for current PC
				if (li.mbIsPC) {
					ImGui::TextColored(ATUIColorWarningText(), ">");
					ImGui::SameLine(0, 0);
				} else if (li.mbIsFramePC) {
					ImGui::TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), ">");
					ImGui::SameLine(0, 0);
				} else {
					ImGui::TextUnformatted(" ");
					ImGui::SameLine(0, 0);
				}

				// Selectable line — click to select, right-click for context menu.
				// Breakpoints are toggled via F9 or the context menu, matching
				// Windows Altirra (single click just selects the line).
				ImGui::PushID(i);
				if (li.mbHasJumpTarget && !li.mbIsPC && !li.mbIsFramePC)
					ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextLink));
				if (ImGui::Selectable(li.mText.c_str(), i == mSelectedLine,
						ImGuiSelectableFlags_AllowOverlap)) {
					mSelectedLine = i;
				}
				if (li.mbHasJumpTarget && !li.mbIsPC && !li.mbIsFramePC)
					ImGui::PopStyleColor();
				if (ImGui::IsItemHovered()
					&& ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
					bool toggledPreview = false;

					if (li.mbIsExpandable) {
						size_t linkPos = li.mText.find(li.mbExpanded ? "[contract]" : "[expand]");

						if (linkPos != VDStringA::npos) {
							const float charWidth = ImGui::CalcTextSize("0").x;
							const float clickX = ImGui::GetMousePos().x - ImGui::GetItemRectMin().x;
							const int clickCol = (int)(clickX / charWidth);
							const size_t linkLen = li.mbExpanded ? 10 : 8;

							if (clickCol >= (int)linkPos
								&& clickCol < (int)(linkPos + linkLen)) {
								ToggleCallPreview(i);
								toggledPreview = true;
							}
						}
					}

					if (toggledPreview)
						continue;

					if (li.mbHasJumpTarget)
						PushAndJump(li.mAddress, li.mTargetAddress);
				}
				if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
					mSelectedLine = i;
					mContextLine = i;
					mContextAddr = li.mAddress;
					mContextTargetAddr = li.mTargetAddress;
					mbContextTargetValid = li.mbHasJumpTarget;
					mbContextAddrValid = true;
				}
				ImGui::PopID();
			}
		}

		// Scroll to the target line
		if (mScrollToLine >= 0) {
			float targetY = mScrollToLine * lineHeight;
			float visibleHeight = ImGui::GetWindowHeight();
			ImGui::SetScrollY(targetY - visibleHeight * 0.3f);
			mScrollToLine = -1;
		}

		// Context menu — rendered outside clipper loop so it persists
		if (mbContextAddrValid) {
			ImGui::OpenPopup("DisasmCtx");
			mbContextAddrValid = false;
		}
		if (ImGui::BeginPopup("DisasmCtx")) {
			if (dbg) {
				// Go to Source — matches Windows ID_CONTEXT_GOTOSOURCE
				if (ImGui::MenuItem("Go to Source"))
					GoToSourceForAddress(mContextAddr);
				ImGui::Separator();
				// Set Next Statement — matches Windows SetPC
				if (ImGui::MenuItem("Set Next Statement")) {
					dbg->SetPC((uint16)(mContextAddr & 0xFFFF));
				}
				// Show Next Statement — jump to current PC
				if (ImGui::MenuItem("Show Next Statement")) {
					mViewAddr = dbg->GetExtPC();
					mbNeedsRebuild = true;
				}
				const bool canTogglePreview = mContextLine >= 0
					&& (size_t)mContextLine < mLines.size()
					&& mLines[mContextLine].mbIsExpandable;
				const char *previewLabel = (canTogglePreview && mLines[mContextLine].mbExpanded)
					? "Contract Preview"
					: "Expand Preview";
				if (ImGui::MenuItem(previewLabel, nullptr, false, canTogglePreview)) {
					ToggleCallPreview(mContextLine);
				}
				if (ImGui::MenuItem("Go to Target", nullptr, false, mbContextTargetValid)) {
					PushAndJump(mContextAddr, mContextTargetAddr);
				}
				ImGui::Separator();
				if (ImGui::MenuItem("Toggle Breakpoint")) {
					dbg->ToggleBreakpoint(mContextAddr);
				}
				ImGui::Separator();

				// Display toggles
				{
					bool v;

					v = (bool)mbShowCodeBytes;
					if (ImGui::MenuItem("Show Code Bytes", nullptr, v)) {
						mbShowCodeBytes = !v;
					}

					v = (bool)mbShowLabels;
					if (ImGui::MenuItem("Show Labels", nullptr, v)) {
						mbShowLabels = !v;
					}

					v = (bool)mbShowLabelNamespaces;
					if (ImGui::MenuItem("Show Label Namespaces", nullptr, v, (bool)mbShowLabels)) {
						mbShowLabelNamespaces = !v;
					}

					v = (bool)mbShowProcedureBreaks;
					if (ImGui::MenuItem("Show Procedure Breaks", nullptr, v)) {
						mbShowProcedureBreaks = !v;
					}

					v = (bool)mbShowCallPreviews;
					if (ImGui::MenuItem("Show Call Previews", nullptr, v)) {
						mbShowCallPreviews = !v;
					}

					v = (bool)mbShowSourceInDisasm;
					if (ImGui::MenuItem("Show Mixed Source/Disasm", nullptr, v)) {
						mbShowSourceInDisasm = !v;
					}
				}

				// 65C816 mode submenu — only shown when CPU mode is 65C816.
				// Matches Windows RC: IDR_DISASM_CONTEXT_MENU "65C816 Mode Handling" popup.
				if (mLastState.mpDebugTarget
					&& mLastState.mpDebugTarget->GetDisasmMode() == kATDebugDisasmMode_65C816) {
					if (ImGui::BeginMenu("65C816 Mode Handling")) {
						ATDebugger816MXPredictionMode curMode = (ATDebugger816MXPredictionMode)m816MXPredictionMode;

						if (ImGui::MenuItem("Auto M/X", nullptr, curMode == ATDebugger816MXPredictionMode::Auto))
							m816MXPredictionMode = ATDebugger816MXPredictionMode::Auto;
						if (ImGui::MenuItem("Current M/X Context", nullptr, curMode == ATDebugger816MXPredictionMode::CurrentContext))
							m816MXPredictionMode = ATDebugger816MXPredictionMode::CurrentContext;
						ImGui::Separator();
						if (ImGui::MenuItem("M8, X8", nullptr, curMode == ATDebugger816MXPredictionMode::M8X8))
							m816MXPredictionMode = ATDebugger816MXPredictionMode::M8X8;
						if (ImGui::MenuItem("M8, X16", nullptr, curMode == ATDebugger816MXPredictionMode::M8X16))
							m816MXPredictionMode = ATDebugger816MXPredictionMode::M8X16;
						if (ImGui::MenuItem("M16, X8", nullptr, curMode == ATDebugger816MXPredictionMode::M16X8))
							m816MXPredictionMode = ATDebugger816MXPredictionMode::M16X8;
						if (ImGui::MenuItem("M16, X16", nullptr, curMode == ATDebugger816MXPredictionMode::M16X16))
							m816MXPredictionMode = ATDebugger816MXPredictionMode::M16X16;
						if (ImGui::MenuItem("Emulation", nullptr, curMode == ATDebugger816MXPredictionMode::Emulation))
							m816MXPredictionMode = ATDebugger816MXPredictionMode::Emulation;
						ImGui::Separator();
						bool dpState = (bool)mb816PredictD;
						if (ImGui::MenuItem("Use DP Register State", nullptr, dpState)) {
							mb816PredictD = !dpState;
						}

						ImGui::EndMenu();
					}
				}
			}
			ImGui::EndPopup();
		}

		// Keyboard shortcuts — matching Windows Altirra keybindings
		if (mbHasFocus && !ImGui::GetIO().WantTextInput) {
			// F9 — Toggle Breakpoint (matches Windows Debug.ToggleBreakpoint)
			if (ImGui::IsKeyPressed(ImGuiKey_F9)) {
				ToggleSelectedBreakpoint();
			}
			// Page Up/Down scrolling — preserve bank byte, clamp within 16-bit range
			if (ImGui::IsKeyPressed(ImGuiKey_PageUp)) {
				uint32 bank = mViewAddr & 0xFFFF0000;
				uint32 offset = mViewAddr & 0xFFFF;
				uint32 scrollAmt = kTotalLines * 2;
				offset = (offset >= scrollAmt) ? offset - scrollAmt : 0;
				mViewAddr = bank | offset;
				mbNeedsRebuild = true;
			}
			if (ImGui::IsKeyPressed(ImGuiKey_PageDown)) {
				uint32 bank = mViewAddr & 0xFFFF0000;
				uint32 offset = mViewAddr & 0xFFFF;
				uint32 scrollAmt = kTotalLines * 2;
				offset = std::min<uint32>(offset + scrollAmt, 0xFFFF);
				mViewAddr = bank | offset;
				mbNeedsRebuild = true;
			}
			// Escape → focus Console
			if (ImGui::IsKeyPressed(ImGuiKey_Escape))
				ATUIDebuggerFocusConsole();

			// Native disassembly forwards printable characters to the Console.
			ImGuiIO& io = ImGui::GetIO();
			for (int n = 0; n < io.InputQueueCharacters.Size; ++n) {
				const ImWchar c = io.InputQueueCharacters[n];

				if (c >= 0x20 && c != 0x7F) {
					char utf8[5];
					if (ATImGuiEncodeUTF8(c, utf8))
						ATUIDebuggerFocusConsoleWithText(utf8);
				}
			}
		}

		// Mouse wheel scrolling — ~3 lines per tick, ~3 bytes per line
		float wheel = ImGui::GetIO().MouseWheel;
		if (wheel != 0 && ImGui::IsWindowHovered()) {
			sint32 scrollBytes = -(sint32)(wheel * 3) * 3;
			uint32 bank = mViewAddr & 0xFFFF0000;
			sint32 offset = (sint32)(mViewAddr & 0xFFFF) + scrollBytes;
			if (offset < 0) offset = 0;
			if (offset > 0xFFFF) offset = 0xFFFF;
			mViewAddr = bank | (uint32)offset;
			mbNeedsRebuild = true;
		}
	}
	ImGui::EndChild();

	ImGui::End();
	return open;
}

// =========================================================================
// Registration
// =========================================================================

void ATUIDebuggerEnsureDisassemblyPane() {
	if (!ATUIDebuggerGetPane(kATUIPaneId_Disassembly)) {
		auto *pane = new ATImGuiDisassemblyPaneImpl();
		ATUIDebuggerRegisterPane(pane);
	}
}

void ATUIDebuggerSetDisassemblyPosition(uint32 addr) {
	ATActivateUIPane(kATUIPaneId_Disassembly, true, true);

	ATImGuiDebuggerPane *basePane = ATUIDebuggerGetPane(kATUIPaneId_Disassembly);
	if (!basePane)
		return;

	static_cast<ATImGuiDisassemblyPaneImpl *>(basePane)->SetPosition(addr);
}

bool ATUIDebuggerQueryDisassemblySelectedBreakpointForTest(
	int& line,
	uint32& addr,
	bool& hasBreakpoint)
{
	ATImGuiDebuggerPane *basePane = ATUIDebuggerGetPane(kATUIPaneId_Disassembly);
	if (!basePane)
		return false;

	return static_cast<ATImGuiDisassemblyPaneImpl *>(basePane)
		->QuerySelectedBreakpointForTest(line, addr, hasBreakpoint);
}

bool ATUIDebuggerToggleDisassemblySelectedBreakpointForTest(
	int& line,
	uint32& addr,
	bool& hasBreakpoint)
{
	ATImGuiDebuggerPane *basePane = ATUIDebuggerGetPane(kATUIPaneId_Disassembly);
	if (!basePane)
		return false;

	return static_cast<ATImGuiDisassemblyPaneImpl *>(basePane)
		->ToggleSelectedBreakpointForTest(line, addr, hasBreakpoint);
}

bool ATUIDebuggerSimulateDisassemblyRunStopForTest(
	uint32 frameAddr,
	int& line,
	uint32& addr,
	bool& hasBreakpoint)
{
	ATImGuiDebuggerPane *basePane = ATUIDebuggerGetPane(kATUIPaneId_Disassembly);
	if (!basePane)
		return false;

	return static_cast<ATImGuiDisassemblyPaneImpl *>(basePane)
		->SimulateRunStopForTest(frameAddr, line, addr, hasBreakpoint);
}

bool ATUIDebuggerDisassemblyGoToSelectedSourceForTest(
	int& line,
	uint32& addr,
	bool& outApplied)
{
	outApplied = false;

	ATUIDebuggerOpen();
	ATActivateUIPane(kATUIPaneId_Disassembly, true, true);

	ATImGuiDebuggerPane *basePane = ATUIDebuggerGetPane(kATUIPaneId_Disassembly);
	if (!basePane)
		return false;

	outApplied = static_cast<ATImGuiDisassemblyPaneImpl *>(basePane)
		->GoToSelectedSourceForTest(line, addr);
	return true;
}

bool ATUIDebuggerDisassemblyTargetNavigationForTest(VDStringA& outState) {
	ATUIDebuggerOpen();
	ATActivateUIPane(kATUIPaneId_Disassembly, true, true);

	ATImGuiDebuggerPane *basePane = ATUIDebuggerGetPane(kATUIPaneId_Disassembly);
	if (!basePane)
		return false;

	return static_cast<ATImGuiDisassemblyPaneImpl *>(basePane)
		->TargetNavigationForTest(outState);
}

bool ATUIDebuggerDisassemblyCallPreviewForTest(VDStringA& outState) {
	ATUIDebuggerOpen();
	ATActivateUIPane(kATUIPaneId_Disassembly, true, true);

	ATImGuiDebuggerPane *basePane = ATUIDebuggerGetPane(kATUIPaneId_Disassembly);
	if (!basePane)
		return false;

	return static_cast<ATImGuiDisassemblyPaneImpl *>(basePane)
		->CallPreviewForTest(outState);
}
