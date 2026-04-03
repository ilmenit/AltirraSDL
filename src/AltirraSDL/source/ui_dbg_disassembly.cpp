//	AltirraSDL - Dear ImGui debugger disassembly pane
//	Replaces Win32 ATDisassemblyWindow (uidbgdisasm.cpp).
//	Shows disassembled instructions around the current PC with current-line
//	highlighting, breakpoint indicators, symbol labels, address navigation,
//	and click-to-toggle breakpoints.

#include <stdafx.h>
#include <algorithm>
#include <imgui.h>
#include <vd2/system/vdtypes.h>
#include <vd2/system/VDString.h>
#include <at/atcpu/history.h>
#include <at/atdebugger/target.h>
#include "ui_debugger.h"
#include "console.h"
#include "debugger.h"
#include "disasm.h"
#include "simulator.h"
#include "cpu.h"

extern ATSimulator g_sim;

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

	// Display settings matching Windows defaults
	static constexpr int kLinesAbovePC = 8;
	static constexpr int kTotalLines = 48;
};

ATImGuiDisassemblyPaneImpl::ATImGuiDisassemblyPaneImpl()
	: ATImGuiDebuggerPane(kATUIPaneId_Disassembly, "Disassembly")
{
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
	if (eventId == kATDebugEvent_BreakpointsChanged)
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

	uint16 focusPC = (uint16)mViewAddr;
	uint8 focusBank = (uint8)(mViewAddr >> 16);
	uint32 addrBank = (uint32)focusBank << 16;

	// Find a valid instruction boundary before the focus address
	uint16 startAddr;
	if (focusPC >= kLinesAbovePC * 3)
		startAddr = ATDisassembleGetFirstAnchor(target, focusPC - kLinesAbovePC * 3, focusPC, addrBank);
	else
		startAddr = ATDisassembleGetFirstAnchor(target, 0, focusPC, addrBank);

	VDStringA buf;
	uint16 pc = startAddr;
	int pcLine = -1;
	int framePCLine = -1;
	int focusLine = -1;

	for (int i = 0; i < kTotalLines; ++i) {
		ATDisassembleCaptureInsnContext(target, pc, focusBank, hent);

		buf.clear();
		ATDisasmResult result = ATDisassembleInsn(buf,
			target,
			disasmMode,
			hent,
			true,		// decodeReferences
			false,		// decodeRefsHistory
			true,		// showPCAddress
			true,		// showCodeBytes
			true,		// showLabels
			false,		// lowercaseOps
			false,		// wideOpcode
			true,		// showLabelNamespaces
			true,		// showSymbols
			false		// showGlobalPC
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

		LineInfo li;
		li.mAddress = fullAddr;
		li.mPC = pc;
		li.mBank = focusBank;
		li.mbIsPC = isPC;
		li.mbIsFramePC = isFramePC;
		li.mText = buf;
		mLines.push_back(std::move(li));

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
		ImGui::End();
		return open;
	}

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
		if (mbStateValid && mLastState.mbRunning)
			ImGui::TextDisabled("(running)");
		else
			ImGui::TextDisabled("(no disassembly)");
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

				// Selectable line — click to toggle breakpoint
				ImGui::PushID(i);
				if (ImGui::Selectable(li.mText.c_str(), false,
						ImGuiSelectableFlags_AllowOverlap)) {
					// Single click toggles breakpoint at this address
					if (dbg)
						dbg->ToggleBreakpoint(li.mAddress);
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
