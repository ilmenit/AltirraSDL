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

#ifndef f_AT_ATNATIVEUI_UIPANEDIALOG_H
#define f_AT_ATNATIVEUI_UIPANEDIALOG_H

#include <at/atnativeui/uipane.h>
#include <at/atnativeui/dialog.h>

class ATUIPaneDialogBase : public vdrefcounted<VDDialogFrameW32> {
public:
	void *AsInterface(uint32 iid);

	bool CreatePaneWindow(ATFrameWindow *w);

protected:
	using vdrefcounted::vdrefcounted;

	virtual ATUIPane& AsPane() = 0;

	VDZINT_PTR DlgProc(VDZUINT msg, VDZWPARAM wParam, VDZLPARAM lParam) override;
	void OnPreLoaded() override;
	void OnPostLoaded() override;
	void OnPreDestroy() override;
	void OnDestroy() override;
	void PostNCDestroy() override;

	bool IsVisibleFast() const { return mbVisibleCached; }
	virtual void OnShow();
	virtual void OnHide();

	void DeferUpdate();
	virtual void OnDeferredUpdate();

private:
	void PostDeferredUpdate();

	bool mbVisibleCached = false;
	bool mbDeferredUpdatePending = false;
	bool mbDeferredUpdatePosted = false;
};

extern template class ATUIPaneWindowT<ATUIPane, ATUIPaneDialogBase>;

using ATUIPaneDialog = ATUIPaneWindowT<ATUIPane, ATUIPaneDialogBase>;

#endif
