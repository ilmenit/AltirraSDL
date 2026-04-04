//	AltirraSDL - Dear ImGui debugger disassembly pane
//	Replaces Win32 ATDisassemblyWindow (uidbgdisasm.cpp).
//	Shows disassembled instructions around the current PC with current-line
//	highlighting, breakpoint indicators, symbol labels, address navigation,
//	and click-to-toggle breakpoints.

#include <stdafx.h>
#include <algorithm>
#include <array>
#include <imgui.h>
#include <vd2/system/vdtypes.h>
#include <vd2/system/VDString.h>
#include <at/atcpu/history.h>
#include <at/atdebugger/target.h>
#include <at/atdebugger/symbols.h>
#include <vd2/system/text.h>
#include "debuggersettings.h"
#include "ui_debugger.h"
#include "console.h"
#include "debugger.h"
#include "disasm.h"
#include "simulator.h"
#include "cpu.h"

extern ATSimulator g_sim;
extern bool ATImGuiConsoleShowSource(uint32 addr);

// =========================================================================
// Disassembly pane
// =========================================================================

class ATImGuiDisassemblyPaneImpl final : public ATImGuiDebuggerPane {
public:
	ATImGuiDisassemblyPaneImpl();

	bool Render() override;
	void OnDebuggerSystemStateUpdate(const ATDebuggerSystemState& state) override;
	void OnDebuggerEvent(ATDebugEvent eventId) override;

private:
	void RebuildView();
	void SetPosition(uint32 addr);
	void NavigateToExpression(const char *expr);

	struct LineInfo {
		uint32 mAddress;		// full 24-bit address (bank:addr)
		uint16 mPC;				// 16-bit PC within bank
		uint8  mBank;
		bool   mbIsPC;			// this line is the current PC
		bool   mbIsFramePC;		// this line is the frame return PC
		bool   mbIsSeparator;	// procedure break separator line
		bool   mbIsSource;		// source comment line (mixed source/disasm)
		VDStringA mText;
	};

	std::vector<LineInfo> mLines;
	bool mbNeedsRebuild = true;
	int mScrollToLine = -1;

	// View state
	uint32 mViewAddr = 0;		// address to center view around
	bool mbFollowPC = true;		// true = auto-follow PC, false = user navigated
	uint32 mPCAddr = 0;
	uint32 mFramePCAddr = 0;

	// Address bar
	char mAddrInput[64] = {};

	// Context menu state
	uint32 mContextAddr = 0;
	bool mbContextAddrValid = false;

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

	uint32 newPCAddr = (uint32)state.mInsnPC + ((uint32)state.mPCBank << 16);
	uint32 newFramePCAddr = state.mFrameExtPC;

	bool pcChanged = (newPCAddr != mPCAddr || newFramePCAddr != mFramePCAddr);
	mPCAddr = newPCAddr;
	mFramePCAddr = newFramePCAddr;

	if (mbFollowPC && pcChanged) {
		mViewAddr = newFramePCAddr;
		mbNeedsRebuild = true;
	} else if (mbNeedsRebuild) {
		// Force rebuild even if not following PC (e.g. breakpoint change)
	}
}

void ATImGuiDisassemblyPaneImpl::OnDebuggerEvent(ATDebugEvent eventId) {
	if (eventId == kATDebugEvent_BreakpointsChanged
		|| eventId == kATDebugEvent_SymbolsChanged)
		mbNeedsRebuild = true;
}

void ATImGuiDisassemblyPaneImpl::SetPosition(uint32 addr) {
	mViewAddr = addr;
	mbFollowPC = false;
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

void ATImGuiDisassemblyPaneImpl::RebuildView() {
	mbNeedsRebuild = false;
	mLines.clear();
	mScrollToLine = -1;

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

	const uint8 *breakMap = nullptr;
	switch(disasmMode) {
		case kATDebugDisasmMode_6502:    breakMap = kBreakMap6502.data();  break;
		case kATDebugDisasmMode_65C02:   breakMap = kBreakMap65C02.data(); break;
		case kATDebugDisasmMode_65C816:  breakMap = kBreakMap65C816.data(); break;
		case kATDebugDisasmMode_6809:    breakMap = kBreakMap6809.data();  break;
		case kATDebugDisasmMode_8048:    breakMap = kBreakMap8048.data();  break;
		case kATDebugDisasmMode_8051:    breakMap = kBreakMap8051.data();  break;
		case kATDebugDisasmMode_Z80:     breakMap = kBreakMapZ80.data();   break;
	}

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
	const bool showSource = (bool)mbShowSourceInDisasm;

	// Source interleaving state — tracks which source file/line was last
	// shown to avoid repeating the same source line for consecutive instructions.
	IATDebuggerSymbolLookup *symbolLookup = showSource ? ATGetDebuggerSymbolLookup() : nullptr;
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

		// Mixed source/disasm: insert source comment lines before instruction
		// Matches Windows uidbgdisasm.cpp Disassemble() lines 844-918.
		if (symbolLookup) {
			uint32 moduleId;
			ATSourceLineInfo lineInfo;
			if (symbolLookup->LookupLine(pc, false, moduleId, lineInfo)
				&& lineInfo.mOffset == pc && lineInfo.mLine > 0) {

				// Reset line tracking on module/file change
				if (lastSourceModuleId != moduleId || lastSourceFileId != lineInfo.mFileId) {
					lastSourceModuleId = moduleId;
					lastSourceFileId = lineInfo.mFileId;
					lastSourceLine = 0;
				}

				if (lineInfo.mLine != lastSourceLine) {
					// Try to get source text from an already-open source window
					ATDebuggerSourceFileInfo sourceFileInfo;
					VDStringA srcText;

					IATSourceWindow *sw = nullptr;
					if (symbolLookup->GetSourceFilePath(moduleId, lineInfo.mFileId, sourceFileInfo))
						sw = ATGetSourceWindow(sourceFileInfo.mSourcePath.c_str());

					if (sw) {
						VDStringW wline = sw->ReadLine((int)lineInfo.mLine - 1);
						// ReadLine returns empty for out-of-range, "\n" for blank
						// lines, or "text\n" for normal lines.  Only fall through
						// to the [path:line] fallback on truly out-of-range.
						if (!wline.empty()) {
							if (wline.back() == L'\n')
								wline.pop_back();
							VDStringA lineU8 = VDTextWToU8(wline);
							srcText.sprintf(";%4u  %s", lineInfo.mLine, lineU8.c_str());
						}
					}

					// Fallback: show [filename:line] if source window not open
					if (srcText.empty()) {
						const wchar_t *pathStr = sourceFileInfo.mSourcePath.c_str();
						if (sw)
							pathStr = sw->GetPath();
						VDStringA pathU8 = VDTextWToU8(VDStringSpanW(pathStr));
						srcText.sprintf(";[%s:%u]", pathU8.c_str(), lineInfo.mLine);
					}

					LineInfo srcLi;
					srcLi.mAddress = addrBank + pc;
					srcLi.mPC = pc;
					srcLi.mBank = focusBank;
					srcLi.mbIsPC = false;
					srcLi.mbIsFramePC = false;
					srcLi.mbIsSeparator = false;
					srcLi.mbIsSource = true;
					srcLi.mText = std::move(srcText);
					mLines.push_back(std::move(srcLi));

					lastSourceLine = lineInfo.mLine;
				}
			}
		}

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
		if (breakMap) {
			const uint8 opcode = hent.mOpcode[0];
			uint8 breakInfo = breakMap[opcode];

			// Handle 6809 special cases (PULS/PULU with PC bit)
			if (breakInfo & kBM_Special) {
				breakInfo = (hent.mOpcode[1] & 0x80) ? kBM_EndBlock : 0;
			}

			// Append call preview for expandable instructions (JSR, JSL, etc.)
			if (showCallPreviews && (breakInfo & kBM_ExpandMask)) {
				size_t len = buf.size();
				if (len < 50)
					buf.append(50 - len, ' ');
				buf += " ;[expand]";
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

	// Scroll priority: frame PC > PC > navigated focus address
	if (framePCLine >= 0)
		mScrollToLine = framePCLine;
	else if (pcLine >= 0)
		mScrollToLine = pcLine;
	else if (focusLine >= 0)
		mScrollToLine = focusLine;
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
		if (ImGui::Button("Follow PC")) {
			mbFollowPC = true;
			mViewAddr = mFramePCAddr;
			mbNeedsRebuild = true;
		}
		ImGui::Separator();
	}

	if (mbNeedsRebuild)
		RebuildView();

	if (mLines.empty()) {
		if (mbStateValid && mLastState.mbRunning) {
			ImGui::TextDisabled("(running)");
			if (ImGui::Button("Break")) {
				IATDebugger *dbg = ATGetDebugger();
				if (dbg)
					dbg->Break();
			}
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
					ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), ">");
					ImGui::SameLine(0, 0);
				} else if (li.mbIsFramePC) {
					ImGui::TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), ">");
					ImGui::SameLine(0, 0);
				} else {
					ImGui::TextUnformatted(" ");
					ImGui::SameLine(0, 0);
				}

				// Selectable line — click to toggle breakpoint, right-click for context menu
				ImGui::PushID(i);
				if (ImGui::Selectable(li.mText.c_str(), false,
						ImGuiSelectableFlags_AllowOverlap)) {
					// Single click toggles breakpoint at this address
					if (dbg)
						dbg->ToggleBreakpoint(li.mAddress);
				}
				if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
					mContextAddr = li.mAddress;
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
				if (ImGui::MenuItem("Go to Source")) {
					ATImGuiConsoleShowSource(mContextAddr);
				}
				ImGui::Separator();
				// Set Next Statement — matches Windows SetPC
				if (ImGui::MenuItem("Set Next Statement")) {
					dbg->SetPC((uint16)(mContextAddr & 0xFFFF));
				}
				// Show Next Statement — jump to current PC
				if (ImGui::MenuItem("Show Next Statement")) {
					SetPosition(dbg->GetExtPC());
					mbFollowPC = true;
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

		// Page Up/Down scrolling — preserve bank byte, clamp within 16-bit range
		if (ImGui::IsWindowFocused() && !ImGui::GetIO().WantTextInput) {
			if (ImGui::IsKeyPressed(ImGuiKey_PageUp)) {
				uint32 bank = mViewAddr & 0xFFFF0000;
				uint32 offset = mViewAddr & 0xFFFF;
				uint32 scrollAmt = kTotalLines * 2;
				offset = (offset >= scrollAmt) ? offset - scrollAmt : 0;
				mViewAddr = bank | offset;
				mbFollowPC = false;
				mbNeedsRebuild = true;
			}
			if (ImGui::IsKeyPressed(ImGuiKey_PageDown)) {
				uint32 bank = mViewAddr & 0xFFFF0000;
				uint32 offset = mViewAddr & 0xFFFF;
				uint32 scrollAmt = kTotalLines * 2;
				offset = std::min<uint32>(offset + scrollAmt, 0xFFFF);
				mViewAddr = bank | offset;
				mbFollowPC = false;
				mbNeedsRebuild = true;
			}
			// Escape → focus Console
			if (ImGui::IsKeyPressed(ImGuiKey_Escape))
				ATUIDebuggerFocusConsole();
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
			mbFollowPC = false;
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
