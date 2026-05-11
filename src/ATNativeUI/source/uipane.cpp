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

#include <stdafx.h>
#include <tchar.h>
#include <vd2/system/w32assist.h>
#include <at/atnativeui/uiframe.h>
#include <at/atnativeui/uipane.h>
#include <at/atnativeui/uipanewindow.h>

void ATRegisterActiveUIPane(uint32 id, ATUIPane *w);
void ATUnregisterActiveUIPane(uint32 id, ATUIPane *w);

ATUIPane::ATUIPane(uint32 paneId, const wchar_t *name)
	: mpName(name)
	, mPaneId(paneId)
	, mPreferredDockCode(-1)
	, mpFrameWindow(nullptr)
{
}

ATUIPane::~ATUIPane() {
}

void *ATUIPane::AsInterface(uint32 iid) {
	if (iid == kTypeID)
		return this;

	return nullptr;
}

void ATUIPane::SetFrameWindow(ATFrameWindow *w) {
	mpFrameWindow = w;
}

void ATUIPane::SetName(const wchar_t *name) {
	mpName = name;
}

void ATUIPane::RegisterUIPane() {
	if (mPaneId)
		ATRegisterActiveUIPane(mPaneId, this);
}

void ATUIPane::UnregisterUIPane() {
	if (mPaneId)
		ATUnregisterActiveUIPane(mPaneId, this);
}

////////////////////////////////////////////////////////////////

ATUIPaneWindowBase::ATUIPaneWindowBase()
	: mDefaultWindowStyles(WS_CHILD|WS_CLIPCHILDREN)
{
}

bool ATUIPaneWindowBase::CreatePaneWindow(ATFrameWindow *frame) {
	AsPane().SetFrameWindow(frame);

	HWND hwnd = CreateWindow((LPCTSTR)(uintptr_t)sWndClass, _T("UI Pane Window"), mDefaultWindowStyles & ~WS_VISIBLE, 0, 0, 0, 0, frame->GetHandleW32(), (HMENU)100, VDGetLocalModuleHandleW32(), static_cast<ATUINativeWindow *>(this));

	if (!hwnd) {
		AsPane().SetFrameWindow(nullptr);
		return false;
	}

	if (frame->IsVisible())
		::ShowWindow(hwnd, SW_SHOWNOACTIVATE);

	return true;
}

LRESULT ATUIPaneWindowBase::WndProc(UINT msg, WPARAM wParam, LPARAM lParam) {
	LRESULT res {};

	if (mMsgDispatcher.TryDispatch(msg, wParam, lParam, res))
		return res;

	switch(msg) {
		case WM_CREATE:
			if (!OnCreate())
				return -1;

			ATUINotifyPaneInterfaces(AsPane(), false);
			break;

		case WM_DESTROY:
			ATUINotifyPaneInterfaces(AsPane(), true);
			OnDestroy();
			break;

		case WM_SIZE:
			OnSize();
			break;

		case WM_SETFOCUS:
			OnSetFocus();
			return 0;

		case WM_COMMAND:
			if (OnCommand(LOWORD(wParam), HIWORD(wParam)))
				return 0;
			break;

		case WM_SHOWWINDOW:
			if (wParam) {
				if (!mbVisibleCached) {
					mbVisibleCached = true;

					if (mbDeferredUpdatePending)
						PostDeferredUpdate();

					OnShow();
				}
			} else {
				if (mbVisibleCached) {
					mbVisibleCached = false;
				
					OnHide();
				}
			}
			break;

		case WM_USER + 0x3FF:
			if (mbDeferredUpdatePending && mbVisibleCached) {
				mbDeferredUpdatePending = false;

				OnDeferredUpdate();
			}

			mbDeferredUpdatePosted = false;
			break;

		case ATWM_FONTSUPDATED:
			OnFontsUpdated();
			break;

		case ATWM_THEMEUPDATED:
			OnFontsUpdated();
			break;
	}

	return ATUINativeWindow::WndProc(msg, wParam, lParam);
}

bool ATUIPaneWindowBase::OnCreate() {
	AsPane().RegisterUIPane();
	OnSize();
	return true;
}

void ATUIPaneWindowBase::OnDestroy() {
	AsPane().UnregisterUIPane();

	mMsgDispatcher.RemoveAllControls(true);
}

void ATUIPaneWindowBase::OnSize() {
}

void ATUIPaneWindowBase::OnSetFocus() {
}

void ATUIPaneWindowBase::OnFontsUpdated() {
}

void ATUIPaneWindowBase::OnThemeUpdated() {
}

bool ATUIPaneWindowBase::OnCommand(uint32 id, uint32 extcode) {
	return false;
}

void ATUIPaneWindowBase::OnShow() {
}

void ATUIPaneWindowBase::OnHide() {
}

void ATUIPaneWindowBase::DeferUpdate() {
	if (!mbDeferredUpdatePending) {
		mbDeferredUpdatePending = true;

		if (mhwnd && mbVisibleCached) {
			PostDeferredUpdate();
		}
	}
}

void ATUIPaneWindowBase::OnDeferredUpdate() {
}

void ATUIPaneWindowBase::PostDeferredUpdate() {
	if (!mbDeferredUpdatePosted) {
		mbDeferredUpdatePosted = true;

		PostMessage(mhwnd, WM_USER + 0x3FF, 0, 0);
	}
}

template class ATUIPaneWindowT<ATUIPane, ATUIPaneWindowBase>;
