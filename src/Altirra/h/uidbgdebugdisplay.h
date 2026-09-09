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

#ifndef f_AT_UIDBGDISPLAY_H
#define f_AT_UIDBGDISPLAY_H

#include "console.h"
#include "debugger.h"
#include "debugdisplay.h"
#include "uidbgpane.h"

class IVDVideoDisplay;

class ATDebugDisplayWindow final : public ATUIDebuggerPaneWindow, public IATDebuggerClient
{
public:
	ATDebugDisplayWindow(uint32 id = kATUIPaneId_DebugDisplay);
	~ATDebugDisplayWindow();

	void OnDebuggerSystemStateUpdate(const ATDebuggerSystemState& state) override;
	void OnDebuggerEvent(ATDebugEvent eventId) override;

protected:
	LRESULT WndProc(UINT msg, WPARAM wParam, LPARAM lParam) override;

	bool OnCreate() override;
	void OnDestroy() override;
	void OnSize() override;
	void OnFontsUpdated() override;

	void OnDeferredUpdate() override;

	void OnDLAddrChanged(int idx);
	void OnPFAddrChanged(int idx);

	LRESULT DLAddrEditWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
	LRESULT PFAddrEditWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

	HWND	mhwndDisplay;
	HMENU	mhmenu;
	int		mComboResizeInProgress;
	bool	mbSharpBilinear = true;

	VDUIProxyComboBoxControl mDLAddrCombo;
	VDUIProxyComboBoxControl mPFAddrCombo;

	VDFunctionThunkInfo *mpThunkDLEditCombo;
	VDFunctionThunkInfo *mpThunkPFEditCombo;
	WNDPROC	mWndProcDLAddrEdit;
	WNDPROC	mWndProcPFAddrEdit;

	IVDVideoDisplay *mpDisplay;
	ATDebugDisplay	mDebugDisplay;
};

#endif
