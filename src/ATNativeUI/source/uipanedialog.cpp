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
#include <at/atnativeui/uiframe.h>
#include <at/atnativeui/uipanedialog.h>

void *ATUIPaneDialogBase::AsInterface(uint32 iid) {
	return nullptr;
}

bool ATUIPaneDialogBase::CreatePaneWindow(ATFrameWindow *frame) {
	AsPane().SetFrameWindow(frame);

	if (!Create((VDGUIHandle)frame->GetHandleW32())) {
		AsPane().SetFrameWindow(nullptr);
		return false;
	}

	if (frame->IsVisible())
		::ShowWindow(mhdlg, SW_SHOWNOACTIVATE);

	return true;
}

INT_PTR ATUIPaneDialogBase::DlgProc(UINT msg, WPARAM wParam, LPARAM lParam) {
	switch(msg) {
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
	}

	return VDDialogFrameW32::DlgProc(msg, wParam, lParam);
}

void ATUIPaneDialogBase::OnPreLoaded() {
	AddRef();
	AsPane().RegisterUIPane();
	return VDDialogFrameW32::OnPreLoaded();
}

void ATUIPaneDialogBase::OnPostLoaded() {
	VDDialogFrameW32::OnPostLoaded();

	ATUINotifyPaneInterfaces(AsPane(), false);
}

void ATUIPaneDialogBase::OnPreDestroy() {
	ATUINotifyPaneInterfaces(AsPane(), true);

	VDDialogFrameW32::OnPreDestroy();
}

void ATUIPaneDialogBase::OnDestroy() {
	AsPane().UnregisterUIPane();
	return VDDialogFrameW32::OnDestroy();
}

void ATUIPaneDialogBase::PostNCDestroy() {
	Release();
}

void ATUIPaneDialogBase::OnShow() {
}

void ATUIPaneDialogBase::OnHide() {
}

void ATUIPaneDialogBase::DeferUpdate() {
	if (!mbDeferredUpdatePending) {
		mbDeferredUpdatePending = true;

		if (mhwnd && mbVisibleCached) {
			PostDeferredUpdate();
		}
	}
}

void ATUIPaneDialogBase::OnDeferredUpdate() {
}

void ATUIPaneDialogBase::PostDeferredUpdate() {
	if (!mbDeferredUpdatePosted) {
		mbDeferredUpdatePosted = true;

		PostMessage(mhwnd, WM_USER + 0x3FF, 0, 0);
	}
}

template class ATUIPaneWindowT<ATUIPane, ATUIPaneDialogBase>;
