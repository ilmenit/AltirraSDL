//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2022 Avery Lee
//
//	This program is free software; you can redistribute it and/or modify
//	it under the terms of the GNU General Public License as published by
//	the Free Software Foundation; either version 2 of the License, or
//	(at your option) any later version.
//
//	This program is distributed in the hope that it will be useful,
//	but WITHOUT ANY WARRANTY; without even the implied warranty of
//	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//	GNU General Public License for more details.
//
//	You should have received a copy of the GNU General Public License along
//	with this program. If not, see <http://www.gnu.org/licenses/>.

#include "stdafx.h"
#include <array>
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <vd2/system/binary.h>
#include <vd2/system/color.h>
#include <vd2/system/w32assist.h>
#include <at/atnativeui/theme.h>
#include "console.h"
#include "disasm.h"
#include "resource.h"
#include "simulator.h"
#include "uicommondialogs.h"
#include "uidbgdisasm.h"

extern ATSimulator g_sim;

ATDisassemblyWindow::ATDisassemblyWindow()
	: ATUIDebuggerPaneWindow(kATUIPaneId_Disassembly, L"Disassembly")
{
	mhmenu = LoadMenu(VDGetLocalModuleHandleW32(), MAKEINTRESOURCE(IDR_DISASM_CONTEXT_MENU));
	mPreferredDockCode = kATContainerDockRight;

	const auto refresh = [this] { RemakeView(mFocusAddr); };
	mbShowCodeBytes.Attach(g_ATDbgSettingShowCodeBytes, refresh);
	mbShowLabels.Attach(g_ATDbgSettingShowLabels, refresh);
	mbShowLabelNamespaces.Attach(g_ATDbgSettingShowLabelNamespaces, refresh);
	mbShowProcedureBreaks.Attach(g_ATDbgSettingShowProcedureBreaks, refresh);
	mbShowCallPreviews.Attach(g_ATDbgSettingShowCallPreviews, refresh);
	mbShowSourceInDisasm.Attach(g_ATDbgSettingShowSourceInDisasm, refresh);
	m816MXPredictionMode.Attach(g_ATDbgSetting816MXPredictionMode, refresh);
	mb816PredictD.Attach(g_ATDbgSetting816PredictD, refresh);
}

ATDisassemblyWindow::~ATDisassemblyWindow() {
	if (mhmenu)
		::DestroyMenu(mhmenu);
}

void *ATDisassemblyWindow::AsInterface(uint32 iid) {
	if (iid == IATUIDebuggerDisassemblyPane::kTypeID)
		return static_cast<IATUIDebuggerDisassemblyPane *>(this);

	return ATUIDebuggerPaneWindow::AsInterface(iid);
}

bool ATDisassemblyWindow::OnPaneCommand(ATUIPaneCommandId id) {
	switch(id) {
		case kATUIPaneCommandId_DebugToggleBreakpoint:
			{
				size_t line = (size_t)mpTextEditor->GetCursorLine();

				if (line < mLines.size())
					ATGetDebugger()->ToggleBreakpoint(mLines[line].mXAddress);

				return true;
			}
			break;
	}

	return false;
}

LRESULT ATDisassemblyWindow::WndProc(UINT msg, WPARAM wParam, LPARAM lParam) {
	switch(msg) {
		case WM_SIZE:
			OnSize();
			break;

		case WM_NOTIFY:
			{
				const NMHDR *hdr = (const NMHDR *)lParam;

				if (hdr->idFrom == 101) {
					if (hdr->code == CBEN_ENDEDIT) {
						const NMCBEENDEDIT *info = (const NMCBEENDEDIT *)hdr;

						if (info->iWhy == CBENF_RETURN) {
							VDStringW text(info->szText);

							try {
								sint32 addr = ATGetDebugger()->EvaluateThrow(VDTextWToA(text).c_str());

								SetPosition(addr);

								SendMessageW(mhwndAddress, CBEM_DELETEITEM, 10, 0);

								COMBOBOXEXITEMW item {};
								item.mask = CBEIF_TEXT;
								item.iItem = 0;
								item.pszText = (LPWSTR)text.c_str();
								SendMessageW(mhwndAddress, CBEM_INSERTITEMW, 0, (LPARAM)&item);
							} catch(const MyError&) {
								MessageBeep(MB_ICONERROR);
							}
						}

						return FALSE;
					}
				}
			}
			break;
	}

	return ATUIDebuggerPaneWindow::WndProc(msg, wParam, lParam);
}

bool ATDisassemblyWindow::OnMessage(VDZUINT msg, VDZWPARAM wParam, VDZLPARAM lParam, VDZLRESULT& result) {
	switch(msg) {
		case WM_SYSKEYDOWN:
		case WM_KEYDOWN:
			if (wParam == VK_ESCAPE) {
				if (ATGetUIPane(kATUIPaneId_Console))
					ATActivateUIPane(kATUIPaneId_Console, true);
			}
			break;

		case WM_SYSKEYUP:
			switch(wParam) {
				case VK_ESCAPE:
					return true;
			}
			break;

		case WM_CHAR:
		case WM_DEADCHAR:
		case WM_UNICHAR:
		case WM_SYSCHAR:
		case WM_SYSDEADCHAR:
			if (wParam >= 0x20) {
				ATUIPane *pane = ATGetUIPane(kATUIPaneId_Console);
				if (pane) {
					ATActivateUIPane(kATUIPaneId_Console, true);

					result = SendMessage(::GetFocus(), msg, wParam, lParam);
				} else {
					result = 0;
				}

				return true;
			}
			break;

		case WM_CONTEXTMENU:
			{
				int x = GET_X_LPARAM(lParam);
				int y = GET_Y_LPARAM(lParam);

				HMENU menu = GetSubMenu(mhmenu, 0);
				VDCheckMenuItemByCommandW32(menu, ID_CONTEXT_SHOWCODEBYTES, mbShowCodeBytes);
				VDCheckMenuItemByCommandW32(menu, ID_CONTEXT_SHOWLABELS, mbShowLabels);
				VDCheckMenuItemByCommandW32(menu, ID_CONTEXT_SHOWLABELNAMESPACES, mbShowLabelNamespaces);
				VDCheckMenuItemByCommandW32(menu, ID_CONTEXT_SHOWPROCEDUREBREAKS, mbShowProcedureBreaks);
				VDCheckMenuItemByCommandW32(menu, ID_CONTEXT_SHOWCALLPREVIEWS, mbShowCallPreviews);
				VDCheckMenuItemByCommandW32(menu, ID_CONTEXT_SHOWMIXEDSOURCE, mbShowSourceInDisasm);

				ATDebugger816MXPredictionMode predictMode = m816MXPredictionMode;

				VDCheckRadioMenuItemByCommandW32(menu, ID_65C816M_AUTO, predictMode == ATDebugger816MXPredictionMode::Auto);
				VDCheckRadioMenuItemByCommandW32(menu, ID_65C816M_CURRENTCONTEXT, predictMode == ATDebugger816MXPredictionMode::CurrentContext);
				VDCheckRadioMenuItemByCommandW32(menu, ID_65C816M_M16X16, predictMode == ATDebugger816MXPredictionMode::M16X16);
				VDCheckRadioMenuItemByCommandW32(menu, ID_65C816M_M16X8, predictMode == ATDebugger816MXPredictionMode::M16X8);
				VDCheckRadioMenuItemByCommandW32(menu, ID_65C816M_M8X16, predictMode == ATDebugger816MXPredictionMode::M8X16);
				VDCheckRadioMenuItemByCommandW32(menu, ID_65C816M_M8X8, predictMode == ATDebugger816MXPredictionMode::M8X8);
				VDCheckRadioMenuItemByCommandW32(menu, ID_65C816M_EMULATION, predictMode == ATDebugger816MXPredictionMode::Emulation);

				VDCheckMenuItemByCommandW32(menu, ID_CONTEXT_USEDPREGISTERSTATE, mb816PredictD);

				const bool multiLineSelected = mpTextEditor->IsSelectionMultiLine();

				VDEnableMenuItemByCommandW32(menu, ID_CONTEXT_GOTOSOURCE, !multiLineSelected);
				VDEnableMenuItemByCommandW32(menu, ID_CONTEXT_SETNEXTSTATEMENT, !multiLineSelected);
				VDEnableMenuItemByCommandW32(menu, ID_CONTEXT_TOGGLEBREAKPOINT, !multiLineSelected);

				if (x == -1 && y == -1) {
					const vdpoint32& pt = mpTextEditor->GetScreenPosForContextMenu();
					x = pt.x;
					y = pt.y;
				} else {
					POINT pt = {x, y};

					if (ScreenToClient(mhwndTextEditor, &pt))
						mpTextEditor->SetCursorPixelPosIfOutsideSelection(pt.x, pt.y);
				}

				TrackPopupMenu(menu, TPM_LEFTALIGN|TPM_TOPALIGN, x, y, 0, mhwnd, NULL);
			}
			break;
	}

	return false;
}

bool ATDisassemblyWindow::OnCreate() {
	if (!ATUIDebuggerPaneWindow::OnCreate())
		return false;

	if (!VDCreateTextEditor(~mpTextEditor))
		return false;

	// We cannot set CBS_HASSTRINGS here, because while ComboBox supports it, ComboBoxEx doesn't -- and in fact
	// if you set it, it screws up the drop-down because the combobox has strings while the comboboxex is trying
	// to owner draw it.
	//
	// Also, for some silly reason, the initial height here has to be greater than some unspecified threshold or
	// the drop-down doesn't work, even though it is resized to the actual number of items. No amount of post-creation
	// resizing will fix this.
	mhwndAddress = CreateWindowEx(0, WC_COMBOBOXEXW, _T(""), WS_VISIBLE|WS_CHILD|WS_TABSTOP|WS_CLIPSIBLINGS|CBS_DROPDOWN|CBS_AUTOHSCROLL, 0, 0, 0, 100, mhwnd, (HMENU)kID_Address, VDGetLocalModuleHandleW32(), NULL);

	mhwndButtonPrev = CreateWindowEx(0, WC_BUTTON, _T("<"), WS_VISIBLE|WS_CHILD|WS_TABSTOP|BS_PUSHBUTTON, 0, 0, 0, 0, mhwnd, (HMENU)kID_ButtonPrev, VDGetLocalModuleHandleW32(), nullptr);
	mhwndButtonNext = CreateWindowEx(0, WC_BUTTON, _T(">"), WS_VISIBLE|WS_CHILD|WS_TABSTOP|BS_PUSHBUTTON, 0, 0, 0, 0, mhwnd, (HMENU)kID_ButtonNext, VDGetLocalModuleHandleW32(), nullptr);

	mhwndTextEditor = (HWND)mpTextEditor->Create(WS_EX_NOPARENTNOTIFY, WS_CHILD|WS_TABSTOP|WS_VISIBLE, 0, 0, 0, 0, (VDGUIHandle)mhwnd, kID_TextView);

	OnFontsUpdated();

	mpTextEditor->SetCallback(this);
	mpTextEditor->SetColorizer(this);
	mpTextEditor->SetMsgFilter(this);
	mpTextEditor->SetReadOnly(true);

	OnSize();
	ATGetDebugger()->AddClient(this, true);
	return true;
}

void ATDisassemblyWindow::OnDestroy() {
	ATGetDebugger()->RemoveClient(this);
	ATUIDebuggerPaneWindow::OnDestroy();
}

void ATDisassemblyWindow::OnSize() {
	RECT r;
	if (GetClientRect(mhwnd, &r)) {
		RECT rAddr = {0};
		int comboHt = 0;

		if (mhwndAddress) {
			GetWindowRect(mhwndAddress, &rAddr);

			comboHt = rAddr.bottom - rAddr.top;
			VDVERIFY(SetWindowPos(mhwndAddress, NULL, comboHt * 2, 0, r.right - comboHt * 2, comboHt * 5, SWP_NOZORDER|SWP_NOACTIVATE));
		}

		if (mhwndButtonNext) {
			VDVERIFY(SetWindowPos(mhwndButtonNext, NULL, comboHt, 0, comboHt, comboHt, SWP_NOZORDER|SWP_NOACTIVATE));
		}

		if (mhwndButtonPrev) {
			VDVERIFY(SetWindowPos(mhwndButtonPrev, NULL, 0, 0, comboHt, comboHt, SWP_NOZORDER|SWP_NOACTIVATE));
		}

		if (mhwndTextEditor) {
			VDVERIFY(SetWindowPos(mhwndTextEditor, NULL, 0, comboHt, r.right, r.bottom - comboHt, SWP_NOZORDER|SWP_NOACTIVATE));
		}
	}
}

void ATDisassemblyWindow::OnSetFocus() {
	::SetFocus(mhwndTextEditor);
}

bool ATDisassemblyWindow::OnCommand(UINT cmd, UINT code) {
	const auto UnsafeTruncateAddress = [](uint32 addr) { return (uint16)addr; };

	switch(cmd) {
		case 101:
			if (code == CBN_SELENDOK) {
				LRESULT idx = SendMessage(mhwndAddress, CB_GETCURSEL, 0, 0);
				if (idx != CB_ERR) {
					vdblock<WCHAR> buf(256);
					COMBOBOXEXITEMW item {};
					item.mask = CBEIF_TEXT;
					item.iItem = idx;
					item.cchTextMax = 255;
					item.pszText = buf.data();

					if (SendMessageW(mhwndAddress, CBEM_GETITEMW, 0, (LPARAM)&item)) {
						buf.back() = 0;

						try {
							sint32 addr = ATGetDebugger()->EvaluateThrow(VDTextWToA(item.pszText).c_str());

							SetPosition(addr);
						} catch(const MyError&) {
							MessageBeep(MB_ICONERROR);
						}
					}
				}

				return true;
			}
			break;

		case ID_CONTEXT_GOTOSOURCE:
			{
				int line = mpTextEditor->GetCursorLine();

				if ((uint32)line < mLines.size()) {
					uint32 xaddr = mLines[line].mXAddress;

					if (ATConsoleShowSource(xaddr)) {
						if (mbShowSourceInDisasm) {
							for(const VDStringW& path : mFailedSourcePaths) {
								if (ATGetSourceWindow(path.c_str())) {
									RemakeView(mFocusAddr);
									break;
								}
							}
						}
					} else {
						VDStringW s;
						s.sprintf(L"There is no source line associated with the address: %hs.", ATGetDebugger()->GetAddressText(xaddr, true).c_str());
						ATUIShowError((VDGUIHandle)mhwnd, s.c_str());
					}
				}
			}
			return true;
		case ID_CONTEXT_SHOWNEXTSTATEMENT:
			SetPosition(ATGetDebugger()->GetExtPC());
			return true;
		case ID_CONTEXT_SETNEXTSTATEMENT:
			{
				int line = mpTextEditor->GetCursorLine();

				if ((uint32)line < mLines.size()) {
					ATGetDebugger()->SetPC(UnsafeTruncateAddress(mLines[line].mXAddress));
				} else
					MessageBeep(MB_ICONEXCLAMATION);
			}
			return true;
		case ID_CONTEXT_TOGGLEBREAKPOINT:
			{
				int line = mpTextEditor->GetCursorLine();

				if ((uint32)line < mLines.size())
					ATGetDebugger()->ToggleBreakpoint(mLines[line].mXAddress);
				else
					MessageBeep(MB_ICONEXCLAMATION);
			}
			return true;
		case ID_CONTEXT_SHOWCODEBYTES:
			mbShowCodeBytes = !mbShowCodeBytes;
			return true;
		case ID_CONTEXT_SHOWLABELS:
			mbShowLabels = !mbShowLabels;
			return true;
		case ID_CONTEXT_SHOWLABELNAMESPACES:
			mbShowLabelNamespaces = !mbShowLabelNamespaces;
			return true;
		case ID_CONTEXT_SHOWPROCEDUREBREAKS:
			mbShowProcedureBreaks = !mbShowProcedureBreaks;
			return true;
		case ID_CONTEXT_SHOWCALLPREVIEWS:
			mbShowCallPreviews = !mbShowCallPreviews;
			return true;
		case ID_CONTEXT_SHOWMIXEDSOURCE:
			mbShowSourceInDisasm = !mbShowSourceInDisasm;
			return true;

		case ID_65C816M_AUTO:
			m816MXPredictionMode = ATDebugger816MXPredictionMode::Auto;
			break;

		case ID_65C816M_CURRENTCONTEXT:
			m816MXPredictionMode = ATDebugger816MXPredictionMode::CurrentContext;
			break;

		case ID_65C816M_M8X8:
			m816MXPredictionMode = ATDebugger816MXPredictionMode::M8X8;
			break;

		case ID_65C816M_M8X16:
			m816MXPredictionMode = ATDebugger816MXPredictionMode::M8X16;
			break;

		case ID_65C816M_M16X8:
			m816MXPredictionMode = ATDebugger816MXPredictionMode::M16X8;
			break;

		case ID_65C816M_M16X16:
			m816MXPredictionMode = ATDebugger816MXPredictionMode::M16X16;
			break;

		case ID_65C816M_EMULATION:
			m816MXPredictionMode = ATDebugger816MXPredictionMode::Emulation;
			break;

		case ID_CONTEXT_USEDPREGISTERSTATE:
			mb816PredictD = !mb816PredictD;
			RemakeView(mFocusAddr);
			break;

		case kID_ButtonPrev:
			GoPrev();
			return true;

		case kID_ButtonNext:
			GoNext();
			return true;
	}

	return false;
}

void ATDisassemblyWindow::OnFontsUpdated() {
	if (mhwndButtonPrev)
		SendMessage(mhwndButtonPrev, WM_SETFONT, (WPARAM)ATGetConsoleFontW32(), TRUE);

	if (mhwndButtonNext)
		SendMessage(mhwndButtonNext, WM_SETFONT, (WPARAM)ATGetConsoleFontW32(), TRUE);

	if (mhwndTextEditor)
		SendMessage(mhwndTextEditor, WM_SETFONT, (WPARAM)ATGetConsoleFontW32(), TRUE);
}

void ATDisassemblyWindow::PushAndJump(uint32 fromXAddr, uint32 toXAddr) {
	mHistory[mHistoryNext] = fromXAddr;
	if (++mHistoryNext >= vdcountof(mHistory))
		mHistoryNext = 0;

	if (mHistoryLenBack < vdcountof(mHistory))
		++mHistoryLenBack;

	mHistoryLenForward = 0;

	SetPosition(toXAddr);
}

void ATDisassemblyWindow::GoPrev() {
	if (!mHistoryLenBack)
		return;

	++mHistoryLenForward;
	--mHistoryLenBack;

	if (mHistoryNext == 0)
		mHistoryNext = (uint32)vdcountof(mHistory);

	--mHistoryNext;

	const uint32 nextXAddr = mHistory[mHistoryNext];

	mHistory[mHistoryNext] = mFocusAddr;

	SetPosition(nextXAddr);
}

void ATDisassemblyWindow::GoNext() {
	if (!mHistoryLenForward)
		return;

	++mHistoryLenBack;
	--mHistoryLenForward;

	const uint32 nextXAddr = mHistory[mHistoryNext];
	if (++mHistoryNext >= (uint32)vdcountof(mHistory))
		mHistoryNext = 0;

	mHistory[mHistoryNext] = mFocusAddr;

	SetPosition(nextXAddr);
}

void ATDisassemblyWindow::RemakeView(uint16 focusAddr) {
	mFocusAddr = focusAddr;

	uint16 pc = mViewStart;

	mpTextEditor->Clear();
	mLines.clear();

	mPCLine = -1;
	mFramePCLine = -1;

	int focusLine = -1;

	if (mLastState.mpDebugTarget) {
		IATDebugTarget *target = mLastState.mpDebugTarget;

		// checksum a small area around the focus address
		mViewChecksum = ComputeViewChecksum();

		ATCPUHistoryEntry hent0;
		ATDisassembleCaptureRegisterContext(target, hent0);

		uint32 viewLength = mpTextEditor->GetVisibleLineCount() * 24;

		if (viewLength < 0x100)
			viewLength = 0x100;

		mbViewDNonZero = false;

		if (target->GetDisasmMode() == kATDebugDisasmMode_65C816) {
			mbViewDNonZero = mb816PredictD && hent0.mD != 0;

			if (!mbViewDNonZero)
				hent0.mD = 0;
		}

		VDStringW buf;
		const auto& result = Disassemble(buf,
			mLines,
			0,
			hent0,
			mViewStart + mViewAddrBank,
			focusAddr + mViewAddrBank,
			viewLength,
			viewLength,
			false);

		pc = (uint16)result.mEndingPC;

		size_t n = mLines.size();
		for(uint32 i = 0; i < n; ++i) {
			const LineInfo& li = mLines[i];

			if (li.mbIsComment)
				continue;

			if (li.mXAddress == mPCAddr)
				mPCLine = i;
			else if (li.mXAddress == mFramePCAddr)
				mFramePCLine = i;

			// Set focus line to earliest line that reaches the focus address.
			// Note that we must be wrap-safe here, thus the modulo comparison.
			if ((uint16)(li.mXAddress - focusAddr - 1) >= 0x8000)
				focusLine = (int)i;
		}

		mpTextEditor->SetUpdateEnabled(false);
		mpTextEditor->Append(buf.c_str());
		mpTextEditor->SetUpdateEnabled(true);
	}

	mLastSubMode = g_sim.GetCPU().GetCPUSubMode();

	if (focusLine >= 0) {
		mpTextEditor->CenterViewOnLine(focusLine);
		mpTextEditor->SetCursorPos(focusLine, 0);
	}

	mViewLength = (uint16)(pc - mViewStart);
	mpTextEditor->RecolorAll();
}

ATDisassemblyWindow::DisasmResult ATDisassemblyWindow::Disassemble(
	VDStringW& buf,
	vdfastvector<LineInfo>& lines,
	uint32 nestingLevel,
	const ATCPUHistoryEntry& initialState,
	uint32 startAddr,
	uint32 focusAddr,
	uint32 maxBytes,
	uint32 maxLines,
	bool stopOnProcedureEnd)
{
	ATCPUHistoryEntry hent(initialState);
	VDStringA abuf;

	IATDebugTarget *target = mLastState.mpDebugTarget;
	const ATDebugDisasmMode disasmMode = target->GetDisasmMode();
	bool autoModeSwitching = false;

	if (disasmMode == kATDebugDisasmMode_65C816) {
		bool forcedMode = false;
		uint8 forcedModeMX = 0;
		bool forcedModeEmulation = false;

		switch(m816MXPredictionMode) {
			case ATDebugger816MXPredictionMode::Auto:
				forcedMode = false;
				autoModeSwitching = true;
				break;

			case ATDebugger816MXPredictionMode::CurrentContext:
				forcedMode = false;
				autoModeSwitching = false;
				break;

			case ATDebugger816MXPredictionMode::M8X8:
				forcedMode = true;
				forcedModeMX = 0x30;
				forcedModeEmulation = false;
				break;

			case ATDebugger816MXPredictionMode::M8X16:
				forcedMode = true;
				forcedModeMX = 0x20;
				forcedModeEmulation = false;
				break;

			case ATDebugger816MXPredictionMode::M16X8:
				forcedMode = true;
				forcedModeMX = 0x10;
				forcedModeEmulation = false;
				break;

			case ATDebugger816MXPredictionMode::M16X16:
				forcedMode = true;
				forcedModeMX = 0x00;
				forcedModeEmulation = false;
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

	const uint32 bankBase = startAddr & UINT32_C(0xFFFF0000);
	const uint8 bank = (uint8)(bankBase >> 16);
	const uint16 pc0 = (uint16)startAddr;
	uint16 pc = pc0;
	bool procedureOpen = stopOnProcedureEnd;
	const uint32 indent = nestingLevel * 2;

	enum : uint8 {
		kBM_ExpandNone = 0x00,
		kBM_ExpandAbs16 = 0x01,
		kBM_ExpandAbs16BE = 0x02,
		kBM_ExpandAbs24 = 0x03,
		kBM_ExpandRel8 = 0x04,
		kBM_ExpandRel16BE = 0x05,
		kBM_ExpandMask = 0x07,

		kBM_EndBlock = 0x08,
		kBM_Special = 0x10
	};

	static constexpr std::array<uint8, 256> kBreakMap6502 = [] {
		std::array<uint8, 256> breakMap {};

		// JSR abs
		breakMap[0x20] |= kBM_ExpandAbs16;

		// JMP abs
		breakMap[0x4C] |= kBM_ExpandAbs16 | kBM_EndBlock;

		// JMP (abs)
		breakMap[0x6C] |= kBM_EndBlock;

		// RTI
		breakMap[0x40] |= kBM_EndBlock;

		// RTS
		breakMap[0x60] |= kBM_EndBlock;

		return breakMap;
	}();

	static constexpr std::array<uint8, 256> kBreakMap65C02 = [] {
		std::array<uint8, 256> breakMap = kBreakMap6502;

		// BRA rel8
		breakMap[0x80] |= kBM_EndBlock;

		// JMP (abs,X)
		breakMap[0x7C] |= kBM_EndBlock;

		return breakMap;
	}();

	static constexpr std::array<uint8, 256> kBreakMap65C816 = [] {
		std::array<uint8, 256> breakMap = kBreakMap65C02;

		// JSL al
		breakMap[0x22] |= kBM_ExpandAbs24;

		// JML al
		breakMap[0x5C] |= kBM_EndBlock | kBM_ExpandAbs24;

		// RTL
		breakMap[0x6B] |= kBM_EndBlock;

		// BRL rl
		breakMap[0x82] |= kBM_EndBlock;

		return breakMap;
	}();

	static constexpr std::array<uint8, 256> kBreakMap6809 = [] {
		std::array<uint8, 256> breakMap {};

		breakMap[0x16] |= kBM_EndBlock;		// LBRA
		breakMap[0x17] |= kBM_ExpandRel16BE;		// LBSR rel16
		breakMap[0x20] |= kBM_EndBlock;		// BRA
		breakMap[0x39] |= kBM_EndBlock;		// RTS
		breakMap[0x6E] |= kBM_EndBlock;		// JMP indexed
		breakMap[0x7E] |= kBM_EndBlock | kBM_ExpandAbs16BE;		// JMP extended
		breakMap[0x8D] |= kBM_ExpandRel8;			// BSR rel8
		breakMap[0xBD] |= kBM_ExpandAbs16BE;		// JSR extended

		breakMap[0x35] |= kBM_Special;		// PULS PC
		breakMap[0x37] |= kBM_Special;		// PULU PC

		return breakMap;
	}();

	static constexpr std::array<uint8, 256> kBreakMap8048 = [] {
		std::array<uint8, 256> breakMap {};

		breakMap[0x04] |= kBM_EndBlock;		// JMP
		breakMap[0x24] |= kBM_EndBlock;		// JMP
		breakMap[0x44] |= kBM_EndBlock;		// JMP
		breakMap[0x64] |= kBM_EndBlock;		// JMP
		breakMap[0x84] |= kBM_EndBlock;		// JMP
		breakMap[0xA4] |= kBM_EndBlock;		// JMP
		breakMap[0xC4] |= kBM_EndBlock;		// JMP
		breakMap[0xE4] |= kBM_EndBlock;		// JMP

		breakMap[0x83] |= kBM_EndBlock;		// RET
		breakMap[0x93] |= kBM_EndBlock;		// RETR
		breakMap[0xB3] |= kBM_EndBlock;		// JMPP

		return breakMap;
	}();

	static constexpr std::array<uint8, 256> kBreakMap8051 = [] {
		std::array<uint8, 256> breakMap {};

		breakMap[0x01] |= kBM_EndBlock;		// AJMP
		breakMap[0x21] |= kBM_EndBlock;		// AJMP
		breakMap[0x41] |= kBM_EndBlock;		// AJMP
		breakMap[0x61] |= kBM_EndBlock;		// AJMP
		breakMap[0x81] |= kBM_EndBlock;		// AJMP
		breakMap[0xA1] |= kBM_EndBlock;		// AJMP
		breakMap[0xC1] |= kBM_EndBlock;		// AJMP
		breakMap[0xE1] |= kBM_EndBlock;		// AJMP

		breakMap[0x02] |= kBM_EndBlock;		// LJMP
		breakMap[0x22] |= kBM_EndBlock;		// RET
		breakMap[0x32] |= kBM_EndBlock;		// RETI
		breakMap[0x73] |= kBM_EndBlock;		// JMP @A+DPTR

		return breakMap;
	}();

	static constexpr std::array<uint8, 256> kBreakMapZ80 = [] {
		std::array<uint8, 256> breakMap {};

		breakMap[0x18] |= kBM_EndBlock;		// JR r8 (12T)
		breakMap[0xC3] |= kBM_EndBlock;		// JP (10T)
		breakMap[0xE9] |= kBM_EndBlock;		// JP (HL) (4T)
		breakMap[0xC9] |= kBM_EndBlock;		// RET

		return breakMap;
	}();

	const uint8 *VDRESTRICT breakMap = nullptr;
	uint8 (*breakMapSpecialHandler)(const uint8 *insn) = nullptr;

	switch(disasmMode) {
		case kATDebugDisasmMode_6502:
			breakMap = kBreakMap6502.data();
			break;

		case kATDebugDisasmMode_65C02:
			breakMap = kBreakMap65C02.data();
			break;

		case kATDebugDisasmMode_65C816:
			breakMap = kBreakMap65C816.data();
			break;

		case kATDebugDisasmMode_6809:
			breakMap = kBreakMap6809.data();
			breakMapSpecialHandler = [](const uint8 *insn) -> uint8 {
				switch(insn[0]) {
					case 0x35:	// PULS
					case 0x37:	// PULU
						return insn[1] & 0x80 ? kBM_EndBlock : 0;
				}

				return 0;
			};
			break;

		case kATDebugDisasmMode_8048:
			breakMap = kBreakMap8048.data();
			break;

		case kATDebugDisasmMode_8051:
			breakMap = kBreakMap8051.data();
			break;

		case kATDebugDisasmMode_Z80:
			breakMap = kBreakMapZ80.data();
			break;
	}

	struct ModFileHash {
		size_t operator()(const std::pair<uint32, uint32>& modFile) const {
			return modFile.first + (modFile.second << 12);
		}
	};

	vdhashmap<std::pair<uint32, uint32>, IATSourceWindow *, ModFileHash> sourceWindowCache;
	uint32 lastModuleId = 0;
	uint32 lastFileId = 0;
	uint32 lastLine = 0;

	mFailedSourcePaths.clear();

	while((uint16)(pc - pc0) < maxBytes) {
		if (!maxLines--)
			break;

		if (autoModeSwitching && pc == (uint16)mPCAddr)
			hent = initialState;

		LineInfo li {};
		li.mXAddress = bankBase + pc;
		li.mNestingLevel = nestingLevel;
		li.mP = hent.mP;
		li.mbEmulation = hent.mbEmulation;
		li.mbIsComment = false;
		li.mbIsSource = false;

		if (disasmMode == kATDebugDisasmMode_6502)
			ATDisassembleCaptureInsnContext(target, bankBase + pc, hent);
		else
			ATDisassembleCaptureInsnContext(target, pc, bank, hent);

		if (mbShowSourceInDisasm) {
			uint32 moduleId;
			ATSourceLineInfo lineInfo;
			if (ATGetDebuggerSymbolLookup()->LookupLine(pc, false, moduleId, lineInfo)) {
				if (lineInfo.mOffset == pc && lineInfo.mLine > 0) {
					if (lastModuleId != moduleId || lastFileId != lineInfo.mFileId) {
						lastModuleId = moduleId;
						lastFileId = lineInfo.mFileId;
						lastLine = 0;
					}

					if (lineInfo.mLine != lastLine) {
						auto sourceWinLookup = sourceWindowCache.insert(std::pair<uint32, uint32>(moduleId, lineInfo.mFileId));
						ATDebuggerSourceFileInfo sourceFileInfo;

						if (sourceWinLookup.second) {
							if (ATGetDebuggerSymbolLookup()->GetSourceFilePath(moduleId, lineInfo.mFileId, sourceFileInfo)) {
								IATSourceWindow *sw = ATGetSourceWindow(sourceFileInfo.mSourcePath.c_str());

								sourceWinLookup.first->second = sw;

								if (!sw)
									mFailedSourcePaths.emplace_back(sourceFileInfo.mSourcePath);
							}
						}

						IATSourceWindow *sw = sourceWinLookup.first->second;
						VDStringW sourceLines;

						if (sw) {
							int lineStartIndex = (int)lineInfo.mLine - 1;
							int lineEndIndex = (int)lineInfo.mLine;

							for(int lineIndex = lineStartIndex; lineIndex < lineEndIndex; ++lineIndex) {
								const VDStringW& line = sw->ReadLine(lineIndex);

								if (!line.empty()) {
									sourceLines.append_sprintf(L"%4d  ", lineIndex + 1);
									sourceLines += line;
								}
							}
						}

						if (sourceLines.empty()) {
							const wchar_t *pathStr = sourceFileInfo.mSourcePath.c_str();

							if (sw)
								pathStr = sw->GetPath();
							else if (!*pathStr) {
								ATGetDebuggerSymbolLookup()->GetSourceFilePath(moduleId, lineInfo.mFileId, sourceFileInfo);
								pathStr = sourceFileInfo.mSourcePath.c_str();
							}

							sourceLines.sprintf(L"[%ls:%d]\n", sw ? sw->GetPath() : sourceFileInfo.mSourcePath.c_str(), lineInfo.mLine);
						}

						VDStringRefW sourceRange(sourceLines);
						VDStringRefW sourceLine;
						LineInfo li2(li);

						li2.mbIsSource = true;

						while(sourceRange.split('\n', sourceLine)) {
							buf.append(indent, L' ');
							buf += L';';
							buf += sourceLine;
							buf += L'\n';

							lines.push_back(li2);
						}
					}

					lastLine = lineInfo.mLine;
				}
			}
		}

		buf.append(indent, ' ');
		size_t lineStart = buf.size();

		abuf.clear();
		const ATDisasmResult& result = ATDisassembleInsn(abuf, target, disasmMode, hent, false, false, true, mbShowCodeBytes, mbShowLabels, false, false, mbShowLabelNamespaces, true, true);

		const size_t abuflen = abuf.size();
		buf.resize(buf.size() + abuflen);

		for(size_t i=0; i<abuflen; ++i)
			(buf.end() - abuflen)[i] = (wchar_t)abuf[i];

		uint16 newpc = result.mNextPC;

		bool procBreak = false;
		bool procSep = false;

		if (breakMap) {
			const uint8 opcode = hent.mOpcode[0];
			uint8 breakInfo = breakMap[opcode];

			if (breakInfo & kBM_Special)
				breakInfo = breakMapSpecialHandler(hent.mOpcode);

			if (breakInfo & kBM_ExpandMask) {
				switch(breakInfo & kBM_ExpandMask) {
					case kBM_ExpandAbs16:
						li.mTargetXAddr = bankBase + VDReadUnalignedLEU16(&hent.mOpcode[1]);
						break;

					case kBM_ExpandAbs16BE:
						li.mTargetXAddr = bankBase + VDReadUnalignedBEU16(&hent.mOpcode[1]);
						break;

					case kBM_ExpandAbs24:
						li.mTargetXAddr = hent.mOpcode[1] + ((uint32)hent.mOpcode[2] << 8) + ((uint32)hent.mOpcode[3] << 16);
						break;

					case kBM_ExpandRel8:
						li.mTargetXAddr = bankBase + ((newpc + (sint8)hent.mOpcode[1]) & 0xFFFF);
						break;

					case kBM_ExpandRel16BE:
						li.mTargetXAddr = bankBase + ((newpc + VDReadUnalignedBES16(&hent.mOpcode[1])) & 0xFFFF);
						break;
				}

				li.mOperandSelCode = kSelCode_JumpTarget;
				li.mOperandStart = (uint32)result.mOperandStart + indent;
				li.mOperandEnd = (uint32)result.mOperandEnd + indent;

				if (mbShowCallPreviews) {
					auto len = buf.size() - lineStart;

					if (len < 50)
						buf.append(50 - len, ' ');

					li.mCommentPos = (uint32)(buf.size() - lineStart) + indent + 1;
					li.mbIsExpandable = true;
					buf += L" ;[expand]";
				}
			}

			if (breakInfo & kBM_EndBlock) {
				if (stopOnProcedureEnd)
					procBreak = true;
				else
					procSep = mbShowProcedureBreaks;
			}
		}

		lines.push_back(li);

		buf += '\n';

		if (procSep) {
			LineInfo sepLI {};
			sepLI.mbIsComment = true;
			lines.push_back(sepLI);

			buf.append(indent, ' ');
			buf += ';';
			buf.append(50, '-');
			buf += '\n';
		}

		// auto-switch mode if necessary
		if (autoModeSwitching)
			ATDisassemblePredictContext(hent, disasmMode);

		// don't allow disassembly to skip the desired PC
		if (newpc > (uint16)focusAddr && pc < (uint16)focusAddr) {
			newpc = (uint16)focusAddr;

			buf.append(indent, ' ');
			buf.append_sprintf(L"; reverse disassembly mismatch -- address forced from $%04X to $%04X\n", pc, newpc);

			li.mbIsComment = true;
			li.mbIsExpandable = false;
			lines.push_back(li);
		}

		pc = newpc;

		if (procBreak) {
			procedureOpen = false;
			break;
		}
	}

	if (procedureOpen) {
		LineInfo li {};
		li.mNestingLevel = nestingLevel;
		li.mbIsComment = true;
		lines.push_back(li);

		buf.append(indent, ' ');
		buf.append(L"    ...\n");
	}

	return DisasmResult { (startAddr & ~UINT32_C(0xFFFF)) + pc };
}

uint32 ATDisassemblyWindow::ComputeViewChecksum() const {
	if (!mLastState.mpDebugTarget)
		return 0;

	uint32 buf[4] {};
	mLastState.mpDebugTarget->DebugReadMemory(mViewAddrBank + ((mViewStart + mViewLength / 2) & 0xFFFF), buf, 16);

	return buf[0]
		+ (uint32)(buf[1] << 2) + (uint32)(buf[1] << (32 - 2))
		+ (uint32)(buf[2] << 4) + (uint32)(buf[2] << (32 - 4))
		+ (uint32)(buf[3] << 6) + (uint32)(buf[3] << (32 - 6));
}

void ATDisassemblyWindow::OnDebuggerSystemStateUpdate(const ATDebuggerSystemState& state) {
	if (state.mbRunning) {
		if (mLastState.mpDebugTarget != state.mpDebugTarget)
			mLastState.mpDebugTarget = nullptr;

		mPCLine = -1;
		mFramePCLine = -1;
		mpTextEditor->RecolorAll();
		return;
	}

	bool memoryViewChanged = false;
	bool changed = false;

	if (mLastState.mpDebugTarget != state.mpDebugTarget) {
		changed = true;
		memoryViewChanged = true;
	} else if (mLastState.mPC != state.mPC || mLastState.mFrameExtPC != state.mFrameExtPC || mLastState.mPCBank != state.mPCBank)
		changed = true;

	if (changed && !memoryViewChanged) {
		if ((uint32)(state.mFrameExtPC - (mViewAddrBank + mViewStart)) >= mViewLength || g_sim.GetCPU().GetCPUSubMode() != mLastSubMode)
			memoryViewChanged = true;

		bool dNonZero = false;
		if (!memoryViewChanged && state.mpDebugTarget->GetDisasmMode() == kATDebugDisasmMode_65C816)
			dNonZero = mb816PredictD && state.mExecState.m6502.mDP != 0;

		if (mbViewDNonZero != dNonZero)
			memoryViewChanged = true;
	}

	// if we haven't detected a full change and cycles have gone by, check whether the memory we last scanned is still the
	// same
	if (!memoryViewChanged && mLastState.mCycle != state.mCycle) {
		if (mViewChecksum != ComputeViewChecksum()) {
			changed = true;
			memoryViewChanged = true;
		}
	}

	mLastState = state;

	mPCAddr = (uint32)state.mInsnPC + ((uint32)state.mPCBank << 16);
	mPCLine = -1;
	mFramePCAddr = state.mFrameExtPC;
	mFramePCLine = -1;

	if (changed && memoryViewChanged) {
		SetPosition(mFramePCAddr);
	} else {
		const int n = (int)mLines.size();

		for(int line = 0; line < n; ++line) {
			const LineInfo& li = mLines[line];

			if (li.mNestingLevel == 0) {
				uint32 addr = li.mXAddress;

				if (addr == mPCAddr)
					mPCLine = line;
				else if (addr == mFramePCAddr)
					mFramePCLine = line;
			}
		}

		if (changed) {
			if (mFramePCLine >= 0) {
				mpTextEditor->SetCursorPos(mFramePCLine, 0);
				mpTextEditor->MakeLineVisible(mFramePCLine);
			} else if (mPCLine >= 0) {
				mpTextEditor->SetCursorPos(mPCLine, 0);
				mpTextEditor->MakeLineVisible(mPCLine);
			} else {
				SetPosition(mFramePCAddr);
				return;
			}
		}

		mpTextEditor->RecolorAll();
	}
}

void ATDisassemblyWindow::OnDebuggerEvent(ATDebugEvent eventId) {
	if (eventId == kATDebugEvent_BreakpointsChanged)
		mpTextEditor->RecolorAll();
	else if (eventId == kATDebugEvent_SymbolsChanged)
		RemakeView(mFocusAddr);
}

void ATDisassemblyWindow::OnTextEditorUpdated() {
}

void ATDisassemblyWindow::OnTextEditorScrolled(int firstVisiblePara, int lastVisiblePara, int visibleParaCount, int totalParaCount) {
	if (mpTextEditor->IsSelectionPresent())
		return;

	if (!mLines.empty()) {
		const bool prev = (firstVisiblePara < visibleParaCount) && mbViewCanScrollUp;
		const bool next = (lastVisiblePara >= totalParaCount - visibleParaCount);

		if (prev != next) {
			int cenIdx = mpTextEditor->GetParagraphForYPos(mpTextEditor->GetVisibleHeight() >> 1);
			
			cenIdx = std::clamp(cenIdx, 0, (int)mLines.size() - 1);

			SetPosition(mLines[cenIdx].mXAddress);
		}
	}
}

void ATDisassemblyWindow::OnLinkSelected(uint32 selectionCode, int para, int offset) {
	if (para < 0)
		return;

	const uint32 line = (uint32)para;
	const uint32 n = mLines.size();
	if (line >= n)
		return;

	const LineInfo& li = mLines[line];

	if (selectionCode == kSelCode_JumpTarget) {
		PushAndJump(li.mXAddress, li.mTargetXAddr);
	} else if (selectionCode == kSelCode_Expand) {
		VDASSERT(li.mbIsExpandable);

		bool expanded = false;

		if (line + 1 < n) {
			if (mLines[line + 1].mNestingLevel > li.mNestingLevel)
				expanded = true;
		}

		if (expanded) {
			// [contract] -> [expand]
			mpTextEditor->RemoveAt(line, li.mCommentPos + 2, line, li.mCommentPos + 10);
			mpTextEditor->InsertAt(line, li.mCommentPos + 2, L"expand");

			uint32 rangeStart = line + 1;
			uint32 rangeEnd = rangeStart;

			while(rangeEnd < n && mLines[rangeEnd].mNestingLevel > li.mNestingLevel)
				++rangeEnd;

			const int rangeLen = (int)(rangeEnd - rangeStart);

			if (mPCLine >= (int)rangeEnd)
				mPCLine -= rangeLen;

			if (mFramePCLine >= (int)rangeEnd)
				mFramePCLine -= rangeLen;

			mLines.erase(mLines.begin() + rangeStart, mLines.begin() + rangeEnd);
			mpTextEditor->RemoveAt(rangeStart, 0, rangeEnd, 0);
		} else {
			// [expand] -> [contract]
			mpTextEditor->RemoveAt(line, li.mCommentPos + 2, line, li.mCommentPos + 8);
			mpTextEditor->InsertAt(line, li.mCommentPos + 2, L"contract");

			IATDebugTarget *target = mLastState.mpDebugTarget;
			ATCPUHistoryEntry initialState;
			ATDisassembleCaptureRegisterContext(target, initialState);

			initialState.mP = li.mP;
			initialState.mbEmulation = li.mbEmulation;

			vdfastvector<LineInfo> newLines;
			VDStringW buf;

			Disassemble(buf,
				newLines,
				li.mNestingLevel + 1,
				initialState,
				li.mTargetXAddr,
				li.mTargetXAddr,
				200,
				20,
				true);

			const uint32 rangeLen = (uint32)newLines.size();

			if (mPCLine > (int)line)
				mPCLine += rangeLen;

			if (mFramePCLine > (int)line)
				mFramePCLine += rangeLen;

			// mLines[] must be updated first as the colorizer will get invoked as
			// soon as we insert the text.
			mLines.insert(mLines.begin() + line + 1, newLines.begin(), newLines.end());
			mpTextEditor->InsertAt(line + 1, 0, buf.c_str());
		}
	}
}

void ATDisassemblyWindow::RecolorLine(int line, const wchar_t *text, int length, IVDTextEditorColorization *cl) {
	int next = 0;
	sint32 bg = -1;
	sint32 fg = -1;
	const LineInfo *li = nullptr;
	const auto& tc = ATUIGetThemeColors();

	if ((size_t)line < mLines.size()) {
		li = &mLines[line];

		if (li->mNestingLevel)
			bg = tc.mStaticBg;

		if (li->mbIsComment) {
			cl->AddTextColorPoint(0, tc.mCommentText, bg);
			return;
		}

		if (li->mbIsSource) {
			cl->AddTextColorPoint(0, (uint32)VDPackedColorRGB8::Average(VDPackedColorRGB8(tc.mContentFg), VDPackedColorRGB8(tc.mContentBg)), tc.mContentBg);
			return;
		}

		uint32 addr = li->mXAddress;

		if (auto *dbg = ATGetDebugger(); dbg->IsBreakpointAtPC(addr)) {
			cl->AddTextColorPoint(next, 0x000000, 0xFF8080);
			next += 4;
		}
	}

	bool suppressLinkHighlights = false;
	if (line == mPCLine) {
		bg = tc.mDirectLocationBg;
		fg = tc.mDirectLocationFg;
		suppressLinkHighlights = true;
	} else if (line == mFramePCLine) {
		bg = tc.mIndirectLocationBg;
		fg = tc.mIndirectLocationFg;
		suppressLinkHighlights = true;
	}

	if (bg >= 0)
		cl->AddTextColorPoint(next, fg, bg);

	if (li) {
		const sint32 linkColor = suppressLinkHighlights ? fg : ATUIGetThemeColors().mHyperlinkText;

		if (li->mOperandSelCode) {
			cl->AddTextColorPoint(li->mOperandStart, linkColor, bg, li->mOperandSelCode);
			cl->AddTextColorPoint(li->mOperandEnd, fg, bg);
		}

		if (li->mbIsExpandable)
			cl->AddTextColorPoint(li->mCommentPos, linkColor, bg, kSelCode_Expand);
	}
}

void ATDisassemblyWindow::SetPosition(uint32 addr) {
	IATDebugger *dbg = ATGetDebugger();
	VDStringA text(dbg->GetAddressText(addr, true));
	VDSetWindowTextFW32(mhwndAddress, L"%hs", text.c_str());

	uint32 addr16 = addr & 0xffff;
	uint32 offset = mpTextEditor->GetVisibleLineCount() * 12;

	if (offset < 0x80)
		offset = 0x80;

	mViewAddrBank = addr & 0xFFFF0000;
	mViewBank = (uint8)(addr >> 16);
	mbViewCanScrollUp = (addr16 > offset);

	mViewStart = ATDisassembleGetFirstAnchor(dbg->GetTarget(), addr16 >= offset ? addr16 - offset : 0, addr16, mViewAddrBank);

	RemakeView(addr);
}

void ATUIDebuggerRegisterDisassemblyPane() {
	ATRegisterUIPaneType(kATUIPaneId_Disassembly, VDRefCountObjectFactory<ATDisassemblyWindow, ATUIPane>);
}
