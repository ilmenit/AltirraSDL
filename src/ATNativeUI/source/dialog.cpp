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
#include <windows.h>
#include <commctrl.h>
#include <richedit.h>
#include <shellapi.h>
#include <uxtheme.h>
#include <vsstyle.h>
#include <vd2/system/binary.h>
#include <vd2/system/error.h>
#include <vd2/system/strutil.h>
#include <vd2/system/vdalloc.h>
#include <vd2/system/w32assist.h>
#include <at/atnativeui/controlstyles.h>
#include <at/atnativeui/dialog.h>
#include <at/atnativeui/genericdialog.h>
#include <at/atnativeui/progress.h>
#include <at/atnativeui/theme.h>
#include <at/atnativeui/theme_win32.h>
#include <at/atnativeui/uiframe.h>

///////////////////////////////////////////////////////////////////////////////

typedef BOOL (WINAPI *tpSetDialogDpiChangeBehavior)(HWND hDlg, DIALOG_DPI_CHANGE_BEHAVIORS mask, DIALOG_DPI_CHANGE_BEHAVIORS values);
BOOL WINAPI ATSetDialogDpiChangeBehaviorDetectW32(HWND hDlg, DIALOG_DPI_CHANGE_BEHAVIORS mask, DIALOG_DPI_CHANGE_BEHAVIORS values);

tpSetDialogDpiChangeBehavior g_pATSetDialogDpiChangeBehaviorW32 = ATSetDialogDpiChangeBehaviorDetectW32;

BOOL WINAPI ATSetDialogDpiChangeBehaviorDummyW32(HWND hDlg, DIALOG_DPI_CHANGE_BEHAVIORS mask, DIALOG_DPI_CHANGE_BEHAVIORS values) {
	return TRUE;
}

BOOL WINAPI ATSetDialogDpiChangeBehaviorDetectW32(HWND hDlg, DIALOG_DPI_CHANGE_BEHAVIORS mask, DIALOG_DPI_CHANGE_BEHAVIORS values) {
	auto p = GetProcAddress(GetModuleHandleW(L"user32"), "SetDialogDpiChangeBehavior");

	if (p)
		g_pATSetDialogDpiChangeBehaviorW32 = (tpSetDialogDpiChangeBehavior)p;
	else
		g_pATSetDialogDpiChangeBehaviorW32 = ATSetDialogDpiChangeBehaviorDummyW32;

	return g_pATSetDialogDpiChangeBehaviorW32(hDlg, mask, values);
}

///////////////////////////////////////////////////////////////////////////////

typedef BOOL (WINAPI *tpSetDialogControlDpiChangeBehavior)(HWND hDlg, DIALOG_CONTROL_DPI_CHANGE_BEHAVIORS mask, DIALOG_CONTROL_DPI_CHANGE_BEHAVIORS values);
BOOL WINAPI ATSetDialogControlDpiChangeBehaviorDetectW32(HWND hDlg, DIALOG_CONTROL_DPI_CHANGE_BEHAVIORS mask, DIALOG_CONTROL_DPI_CHANGE_BEHAVIORS values);

tpSetDialogControlDpiChangeBehavior g_pATSetDialogControlDpiChangeBehaviorW32 = ATSetDialogControlDpiChangeBehaviorDetectW32;

BOOL WINAPI ATSetDialogControlDpiChangeBehaviorDummyW32(HWND hwnd, DIALOG_CONTROL_DPI_CHANGE_BEHAVIORS mask, DIALOG_CONTROL_DPI_CHANGE_BEHAVIORS values) {
	return TRUE;
}

BOOL WINAPI ATSetDialogControlDpiChangeBehaviorDetectW32(HWND hwnd, DIALOG_CONTROL_DPI_CHANGE_BEHAVIORS mask, DIALOG_CONTROL_DPI_CHANGE_BEHAVIORS values) {
	auto p = GetProcAddress(GetModuleHandleW(L"user32"), "SetDialogControlDpiChangeBehavior");

	if (p)
		g_pATSetDialogControlDpiChangeBehaviorW32 = (tpSetDialogControlDpiChangeBehavior)p;
	else
		g_pATSetDialogControlDpiChangeBehaviorW32 = ATSetDialogControlDpiChangeBehaviorDummyW32;

	return g_pATSetDialogControlDpiChangeBehaviorW32(hwnd, mask, values);
}

///////////////////////////////////////////////////////////////////////////////

typedef BOOL (WINAPI *tpATAdjustWindowRectExForDpiW32)(LPRECT lpRect, DWORD dwStyle, BOOL bMenu, DWORD dwExStyle, UINT dpi);
BOOL WINAPI ATAdjustWindowRectExForDpiDetectW32(LPRECT lpRect, DWORD dwStyle, BOOL bMenu, DWORD dwExStyle, UINT dpi);

tpATAdjustWindowRectExForDpiW32 g_pATAdjustWindowRectExForDpiW32 = ATAdjustWindowRectExForDpiDetectW32;

BOOL WINAPI ATAdjustWindowRectExForDpiDummyW32(LPRECT lpRect, DWORD dwStyle, BOOL bMenu, DWORD dwExStyle, UINT dpi) {
	return AdjustWindowRectEx(lpRect, dwStyle, bMenu, dwExStyle);
}

BOOL WINAPI ATAdjustWindowRectExForDpiDetectW32(LPRECT lpRect, DWORD dwStyle, BOOL bMenu, DWORD dwExStyle, UINT dpi) {
	auto p = GetProcAddress(GetModuleHandleW(L"user32"), "AdjustWindowRectExForDpi");

	if (p)
		g_pATAdjustWindowRectExForDpiW32 = (tpATAdjustWindowRectExForDpiW32)p;
	else
		g_pATAdjustWindowRectExForDpiW32 = ATAdjustWindowRectExForDpiDummyW32;

	return g_pATAdjustWindowRectExForDpiW32(lpRect, dwStyle, bMenu, dwExStyle, dpi);
}

///////////////////////////////////////////////////////////////////////////////

class VDUIDropFileListW32 : public IVDUIDropFileList {
public:
	VDUIDropFileListW32(VDZHDROP hdrop);

	bool GetFileName(int index, VDStringW& fileName);

protected:
	const HDROP mhdrop;
	const int mFileCount;
};

VDUIDropFileListW32::VDUIDropFileListW32(VDZHDROP hdrop)
	: mhdrop(hdrop)
	, mFileCount(DragQueryFile(mhdrop, 0xFFFFFFFF, NULL, 0))
{
}

bool VDUIDropFileListW32::GetFileName(int index, VDStringW& fileName) {
	if (index < 0 || index >= mFileCount)
		return false;

	wchar_t fileBufW[MAX_PATH];

	if (!DragQueryFileW(mhdrop, index, fileBufW, MAX_PATH))
		return false;

	fileName = fileBufW;
	return true;
}

///////////////////////////////////////////////////////////////////////////////

void VDMenuItemInitializer::SetEnabled(bool enable) {
	EnableMenuItem(mhmenu, mPos, enable ? MF_BYPOSITION|MF_ENABLED : MF_BYPOSITION|MF_GRAYED);
}

void VDMenuItemInitializer::SetChecked() {
	VDCheckMenuItemByPositionW32(mhmenu, mPos, true);
}

void VDMenuItemInitializer::SetRadioChecked() {
	VDCheckRadioMenuItemByPositionW32(mhmenu, mPos, true);
}

///////////////////////////////////////////////////////////////////////////////

ATUIPopupMenuBuilder::ATUIPopupMenuBuilder() {
	mMenuStack.push_back(CreatePopupMenu());
}

ATUIPopupMenuBuilder::~ATUIPopupMenuBuilder() {
	HMENU hmenu = mMenuStack.front();

	if (hmenu)
		DestroyMenu(hmenu);
}

void ATUIPopupMenuBuilder::SetNextItemDisabled() {
	mPendingItemFlags |= MF_GRAYED;
}

void ATUIPopupMenuBuilder::SetNextItemChecked() {
	mPendingItemFlags |= MF_CHECKED;
}

void ATUIPopupMenuBuilder::SetNextItemRadioChecked() {
	mbPendingRadioChecked = true;
}

uint32 ATUIPopupMenuBuilder::AddItem(const wchar_t *text) {
	++mLastId;

	HMENU hmenu = mMenuStack.back();

	if (hmenu) {
		VDAppendMenuW32(hmenu, mPendingItemFlags | MF_ENABLED | MF_STRING, mLastId, text);

		if (mbPendingRadioChecked)
			VDCheckRadioMenuItemByPositionW32(hmenu, GetMenuItemCount(hmenu) - 1, true);
	}

	mPendingItemFlags = 0;
	mbPendingRadioChecked = false;

	return mLastId;
}

void ATUIPopupMenuBuilder::AddSpacer() {
	HMENU hmenu = mMenuStack.back();

	if (hmenu)
		VDAppendMenuW32(hmenu, mPendingItemFlags | MF_ENABLED | MF_STRING | MF_DISABLED, 0, L"");

	mPendingItemFlags = 0;
	mbPendingRadioChecked = false;
}

void ATUIPopupMenuBuilder::AddSeparator() {
	HMENU hmenu = mMenuStack.back();

	if (hmenu)
		VDAppendMenuSeparatorW32(hmenu);
}

void ATUIPopupMenuBuilder::StartNewColumn() {
	mPendingItemFlags |= MF_MENUBREAK;
}

void ATUIPopupMenuBuilder::BeginSubMenu(const wchar_t *text) {
	HMENU hmenu = mMenuStack.back();
	HMENU hSubMenu = nullptr;

	if (hmenu) {
		hSubMenu = CreatePopupMenu();

		if (!VDAppendPopupMenuW32(hmenu, mPendingItemFlags | MF_ENABLED | MF_POPUP, hSubMenu, text)) {
			DestroyMenu(hSubMenu);
			hSubMenu = nullptr;
		}
	}

	mPendingItemFlags = 0;
	mMenuStack.push_back(hSubMenu);
}

void ATUIPopupMenuBuilder::EndSubMenu() {
	if (mMenuStack.size() > 1)
		mMenuStack.pop_back();

	mPendingItemFlags = 0;
}

VDZHMENU ATUIPopupMenuBuilder::GetPopupMenu() const {
	return mMenuStack.front();
}

int ATUIPopupMenuBuilder::GetIndexFromItemId(uint32 id) const {
	if (id >= 1 && id <= mLastId)
		return (int)(id - 1);
	else
		return -1;
}

///////////////////////////////////////////////////////////////////////////////

const wchar_t *VDDialogFrameW32::spDefaultCaption = L"";

VDDialogFrameW32::VDDialogFrameW32(uint32 dlgid)
	: mbIsModal(false)
	, mhfont(nullptr)
	, mMinWidth(0)
	, mMinHeight(0)
	, mMaxWidth(INT_MAX)
	, mMaxHeight(INT_MAX)
	, mAccel(nullptr)
	, mTemplateWidthDLUs(0)
	, mTemplateHeightDLUs(0)
	, mTemplateControlCount(0)
	, mpTemplateControls(nullptr)
	, mbResizableWidth(false)
	, mbResizableHeight(false)
	, mpDialogResourceName(MAKEINTRESOURCEW(dlgid))
{
}

VDDialogFrameW32::~VDDialogFrameW32() {
	ClearControlCaches();
}

bool VDDialogFrameW32::Create(std::nullptr_t) {
	return Create((VDGUIHandle)nullptr);
}

bool VDDialogFrameW32::Create(VDGUIHandle parent) {
	if (mhdlg)
		return true;

	mbIsModal = false;

	DoCreate((VDZHWND)parent, false);

	return mhdlg != NULL;
}

bool VDDialogFrameW32::Create(VDDialogFrameW32 *parent) {
	return Create((VDGUIHandle)parent->mhdlg);
}

sintptr VDDialogFrameW32::ShowDialog(std::nullptr_t) {
	return ShowDialog((VDGUIHandle)nullptr);
}

sintptr VDDialogFrameW32::ShowDialog(VDGUIHandle parent) {
	return DoCreate((VDZHWND)parent, true);
}

sintptr VDDialogFrameW32::ShowDialog(VDDialogFrameW32 *parent) {
	return ShowDialog((VDGUIHandle)parent->mhdlg);
}

void VDDialogFrameW32::Sync(bool write) {
	if (mhdlg)
		OnDataExchange(write);
}

vdsize32 VDDialogFrameW32::GetTemplateSizeDLUs() const {
	return vdsize32(mTemplateWidthDLUs, mTemplateHeightDLUs);
}

void VDDialogFrameW32::SetSize(const vdsize32& sz, bool repositionSafe) {
	SetSize(sz);

	if (repositionSafe)
		AdjustPosition();
}

void VDDialogFrameW32::SetArea(const vdrect32& r, bool repositionSafe) {
	SetArea(r);

	if (repositionSafe)
		AdjustPosition();
}

VDZHFONT VDDialogFrameW32::GetFont() const {
	return mhfont;
}

void VDDialogFrameW32::SetFont(VDZHFONT hfont) {
	if (!hfont) {
		VDASSERT(!"Null font passed to SetFont().");
		return;
	}

	if (hfont == mhfont)
		return;

	HFONT hOldFont = mhfont;
	mhfont = hfont;

	RecomputeDialogUnits();

	mResizer.Broadcast(WM_SETFONT, (VDZWPARAM)hfont, TRUE);
	mMsgDispatcher.DispatchFontChanged();

	OnSetFont(mhfont);

	DeleteObject(hOldFont);
}

void VDDialogFrameW32::AdjustPosition() {
	if (!mhdlg)
		return;

	SendMessage(mhdlg, DM_REPOSITION, 0, 0);
}

void VDDialogFrameW32::CenterOnParent() {
	if (!mhdlg)
		return;

	HWND hwndParent = GetParent(mhdlg);
	RECT rParent;
	RECT rSelf;

	if (hwndParent && GetWindowRect(hwndParent, &rParent) && GetWindowRect(mhdlg, &rSelf)) {
		int px = (rParent.left + rParent.right - abs(rSelf.right - rSelf.left)) >> 1;
		int py = (rParent.top + rParent.bottom - abs(rSelf.bottom - rSelf.top)) >> 1;

		SetWindowPos(mhdlg, NULL, px, py, 0, 0, SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOSIZE);

		AdjustPosition();
	}
}

void VDDialogFrameW32::UpdateChildDpi() {
	if (mhdlg) {
		uint32 dpi = ATUIGetWindowDpiW32(mhdlg);

		if (mCurrentDpi != dpi)
			OnDpiChanging(dpi, dpi, nullptr);
	}
}

void VDDialogFrameW32::End(sintptr result) {
	if (!mhdlg)
		return;

	if (mbIsModal)
		EndDialog(mhdlg, result);
	else
		PostMessage(mhdlg, WM_CLOSE, 0, 0);
}

void VDDialogFrameW32::AddProxy(VDUIProxyControl *proxy, uint32 id) {
	HWND hwnd = GetControl(id);

	if (hwnd) {
		proxy->Attach(hwnd);
		mMsgDispatcher.AddControl(proxy);
	}
}

void VDDialogFrameW32::AddProxy(VDUIProxyControl *proxy, VDZHWND hwnd) {
	proxy->Attach(hwnd);
	mMsgDispatcher.AddControl(proxy);
}

bool VDDialogFrameW32::AddControlFromPlaceholder(VDDialogFrameW32& control, uint32 id) {
	HWND hwnd = GetControl(id);

	if (!hwnd)
		return false;

	ATUINativeWindowProxy placeholder(hwnd);

	const vdrect32& area = placeholder.GetArea();
	const bool visible = placeholder.IsVisible();
	const bool tabStop = placeholder.IsTabStop();

	if (!control.Create(this))
		return false;

	control.InsertBelow(hwnd);

	mResizer.AddAlias(control.GetWindowHandle(), hwnd, 0);
	mResizer.Remove(hwnd);

	DestroyWindow(hwnd);

	control.SetWindowId(id);
	control.SetArea(area);
	control.SetTabStop(tabStop);

	if (visible)
		control.Show();
	else
		control.Hide();

	return true;
}

void VDDialogFrameW32::SetCurrentSizeAsMinSize() {
	RECT r;
	if (GetWindowRect(mhdlg, &r)) {
		if (r.right > r.left)
			mMinWidth = r.right - r.left;

		if (r.bottom > r.top)
			mMinHeight = r.bottom - r.top;
	}

	mbResizableWidth = true;
	mbResizableHeight = true;
}

void VDDialogFrameW32::SetCurrentSizeAsMaxSize(bool width, bool height) {
	RECT r;
	if (GetWindowRect(mhdlg, &r)) {
		if (r.right > r.left && width)
			mMaxWidth = r.right - r.left;

		if (r.bottom > r.top && height)
			mMaxHeight = r.bottom - r.top;
	}

	if (width)
		mbResizableWidth = false;

	if (height)
		mbResizableHeight = false;
}

VDZHWND VDDialogFrameW32::GetControl(uint32 id) const {
	if (!mhdlg)
		return NULL;

	return GetDlgItem(mhdlg, id);
}

VDZHWND VDDialogFrameW32::GetFocusedWindow() const {
	return ::GetFocus();
}

void VDDialogFrameW32::SetFocusToControl(uint32 id) {
	if (!mhdlg)
		return;

	HWND hwnd = GetDlgItem(mhdlg, id);
	if (hwnd)
		SendMessage(mhdlg, WM_NEXTDLGCTL, (WPARAM)hwnd, TRUE);
}

void VDDialogFrameW32::SetFocusToNextControl(uint32 id) {
	if (!mhdlg)
		return;

	HWND hwnd = GetDlgItem(mhdlg, id);
	if (hwnd) {
		hwnd = GetNextDlgTabItem(mhdlg, hwnd, FALSE);

		if (hwnd)
			SendMessage(mhdlg, WM_NEXTDLGCTL, (WPARAM)hwnd, TRUE);
	}
}

void VDDialogFrameW32::SetCurrentCursor(const ATUICursorImage& image) {
	switch(image) {
		case kATUICursorImage_Hidden:
			::SetCursor(NULL);
			break;

		case kATUICursorImage_Arrow:
			::SetCursor(::LoadCursor(NULL, IDC_ARROW));
			break;

		case kATUICursorImage_IBeam:
			::SetCursor(::LoadCursor(NULL, IDC_IBEAM));
			break;

		case kATUICursorImage_Cross:
			::SetCursor(::LoadCursor(NULL, IDC_CROSS));
			break;

		case kATUICursorImage_Query:
			::SetCursor(::LoadCursor(NULL, IDC_HELP));
			break;

		case kATUICursorImage_SizeHoriz:
			::SetCursor(::LoadCursor(NULL, IDC_SIZEWE));
			break;

		case kATUICursorImage_SizeVert:
			::SetCursor(::LoadCursor(NULL, IDC_SIZENS));
			break;

		case kATUICursorImage_Move:
			::SetCursor(::LoadCursor(NULL, IDC_SIZEALL));
			break;
	}
}

void VDDialogFrameW32::EnableControl(uint32 id, bool enabled) {
	if (!mhdlg)
		return;

	HWND hwnd = GetDlgItem(mhdlg, id);
	if (hwnd)
		EnableWindow(hwnd, enabled);
}

void VDDialogFrameW32::ShowControl(uint32 id, bool visible) {
	if (!mhdlg)
		return;

	HWND hwnd = GetDlgItem(mhdlg, id);
	if (hwnd)
		ShowWindow(hwnd, visible ? SW_SHOW : SW_HIDE);
}

void VDDialogFrameW32::ApplyFontToControl(uint32 id) {
	if (!mhdlg)
		return;

	HWND hwndControl = GetDlgItem(mhdlg, id);
	if (hwndControl)
		SendMessageW(hwndControl, WM_SETFONT, (WPARAM)mhfont, TRUE);
}

vdrect32 VDDialogFrameW32::GetControlPos(uint32 id) {
	if (mhdlg) {
		HWND hwnd = GetDlgItem(mhdlg, id);
		if (hwnd) {
			RECT r;
			if (GetWindowRect(hwnd, &r)) {
				SetLastError(0);
				if (MapWindowPoints(NULL, mhdlg, (LPPOINT)&r, 2) || !GetLastError())
					return vdrect32(r.left, r.top, r.right, r.bottom);
			}
		}
	}

	return vdrect32(0, 0, 0, 0);
}

void VDDialogFrameW32::SetControlPos(uint32 id, const vdrect32& r) {
	if (mhdlg) {
		const HWND hwnd = GetDlgItem(mhdlg, id);
		if (hwnd)
			SetWindowPos(hwnd, nullptr, r.left, r.top, r.width(), r.height(), SWP_NOZORDER | SWP_NOACTIVATE);
	}
}

vdrect32 VDDialogFrameW32::GetControlScreenPos(uint32 id) {
	if (mhdlg) {
		HWND hwnd = GetDlgItem(mhdlg, id);
		if (hwnd) {
			RECT r;
			if (GetWindowRect(hwnd, &r))
				return vdrect32(r.left, r.top, r.right, r.bottom);
		}
	}

	return vdrect32(0, 0, 0, 0);
}

bool VDDialogFrameW32::GetControlText(uint32 id, VDStringW& s) {
	HWND hwnd = GetDlgItem(mhdlg, id);
	if (!hwnd)
		return false;

	s = VDGetWindowTextW32(hwnd);
	return true;
}

void VDDialogFrameW32::SetCaption(uint32 id, const wchar_t *format) {
	if (mhdlg)
		VDSetWindowTextW32(mhdlg, format);
}

void VDDialogFrameW32::SetControlText(uint32 id, const wchar_t *s) {
	if (!mhdlg)
		return;

	if (!s)
		s = L"";

	HWND hwnd = GetDlgItem(mhdlg, id);
	if (hwnd)
		VDSetWindowTextW32(hwnd, s);
}

void VDDialogFrameW32::SetControlTextF(uint32 id, const wchar_t *format, ...) {
	if (!mhdlg)
		return;

	HWND hwnd = GetDlgItem(mhdlg, id);
	if (hwnd) {
		VDStringW s;
		va_list val;

		va_start(val, format);
		s.append_vsprintf(format, val);
		va_end(val);

		VDSetWindowTextW32(hwnd, s.c_str());
	}
}

sint32 VDDialogFrameW32::GetControlValueSint32(uint32 id) {
	if (!mhdlg) {
		FailValidation(id);
		return 0;
	}

	HWND hwnd = GetDlgItem(mhdlg, id);
	if (!hwnd) {
		FailValidation(id);
		return 0;
	}

	VDStringW s(VDGetWindowTextW32(hwnd));
	int val;
	wchar_t tmp;
	if (1 != swscanf(s.c_str(), L" %d %c", &val, &tmp)) {
		FailValidation(id);
		return 0;
	}

	return val;
}

uint32 VDDialogFrameW32::GetControlValueUint32(uint32 id) {
	if (!mhdlg) {
		FailValidation(id);
		return 0;
	}

	HWND hwnd = GetDlgItem(mhdlg, id);
	if (!hwnd) {
		FailValidation(id);
		return 0;
	}

	VDStringW s(VDGetWindowTextW32(hwnd));
	unsigned val;
	wchar_t tmp;
	if (1 != swscanf(s.c_str(), L" %u %c", &val, &tmp)) {
		FailValidation(id);
		return 0;
	}

	return val;
}

double VDDialogFrameW32::GetControlValueDouble(uint32 id) {
	if (!mhdlg) {
		FailValidation(id);
		return 0;
	}

	HWND hwnd = GetDlgItem(mhdlg, id);
	if (!hwnd) {
		FailValidation(id);
		return 0;
	}

	VDStringW s(VDGetWindowTextW32(hwnd));
	double val;
	wchar_t tmp;
	if (1 != swscanf(s.c_str(), L" %lg %c", &val, &tmp)) {
		FailValidation(id);
		return 0;
	}

	return val;
}

VDStringW VDDialogFrameW32::GetControlValueString(uint32 id) {
	if (!mhdlg) {
		FailValidation(id);
		return VDStringW();
	}

	HWND hwnd = GetDlgItem(mhdlg, id);
	if (!hwnd) {
		FailValidation(id);
		return VDStringW();
	}

	return VDGetWindowTextW32(hwnd);
}

void VDDialogFrameW32::ExchangeControlValueBoolCheckbox(bool write, uint32 id, bool& val) {
	if (write) {
		val = IsButtonChecked(id);
	} else {
		CheckButton(id, val);
	}
}

void VDDialogFrameW32::ExchangeControlValueSint32(bool write, uint32 id, sint32& val, sint32 minVal, sint32 maxVal) {
	if (write) {
		val = GetControlValueSint32(id);
		if (val < minVal || val > maxVal)
			FailValidation(id);
	} else {
		SetControlTextF(id, L"%d", (int)val);
	}
}

void VDDialogFrameW32::ExchangeControlValueUint32(bool write, uint32 id, uint32& val, uint32 minVal, uint32 maxVal) {
	if (write) {
		val = GetControlValueUint32(id);
		if (val < minVal || val > maxVal)
			FailValidation(id);
	} else {
		SetControlTextF(id, L"%u", (unsigned)val);
	}
}

void VDDialogFrameW32::ExchangeControlValueDouble(bool write, uint32 id, const wchar_t *format, double& val, double minVal, double maxVal) {
	if (write) {
		val = GetControlValueDouble(id);
		if (val < minVal || val > maxVal)
			FailValidation(id);
	} else {
		SetControlTextF(id, format, val);
	}
}

void VDDialogFrameW32::ExchangeControlValueString(bool write, uint32 id, VDStringW& s) {
	if (write)
		s = GetControlValueString(id);
	else
		SetControlText(id, s.c_str());
}

void VDDialogFrameW32::CheckButton(uint32 id, bool checked) {
	CheckDlgButton(mhdlg, id, checked ? BST_CHECKED : BST_UNCHECKED);
}

bool VDDialogFrameW32::IsButtonChecked(uint32 id) const {
	return IsDlgButtonChecked(mhdlg, id) != 0;
}

int VDDialogFrameW32::GetButtonTriState(uint32 id) {
	switch(IsDlgButtonChecked(mhdlg, id)) {
		case BST_UNCHECKED:
		default:
			return 0;

		case BST_INDETERMINATE:
			return 1;

		case BST_CHECKED:
			return 2;
	}
}

void VDDialogFrameW32::SetButtonTriState(uint32 id, int state) {
	switch(state) {
		case 0:
		default:
			CheckDlgButton(mhdlg, id, BST_UNCHECKED);
			break;

		case 1:
			CheckDlgButton(mhdlg, id, BST_INDETERMINATE);
			break;

		case 2:
			CheckDlgButton(mhdlg, id, BST_CHECKED);
			break;
	}
}

void VDDialogFrameW32::BeginValidation() {
	mbValidationFailed = false;
}

bool VDDialogFrameW32::EndValidation() {
	if (mbValidationFailed) {
		SignalFailedValidation(mFailedId);
		return false;
	}

	return true;	
}

void VDDialogFrameW32::FailValidation(uint32 id) {
	FailValidation(id, nullptr);
}

void VDDialogFrameW32::FailValidation(uint32 id, const wchar_t *msg, const wchar_t *title) {
	if (!mbValidationFailed) {
		mbValidationFailed = true;
		mFailedId = id;

		if (msg)
			mFailedMsg = msg;
		else
			mFailedMsg.clear();

		if (title)
			mFailedTitle = title;
		else
			mFailedTitle.clear();
	}
}

void VDDialogFrameW32::SignalFailedValidation(uint32 id) {
	if (!mhdlg)
		return;

	HWND hwnd = GetDlgItem(mhdlg, id);

	if (mFailedMsg.empty())
		PlayAlert();

	if (hwnd)
		SetFocus(hwnd);

	if (!mFailedMsg.empty()) {
		ATUIGenericDialogOptions opts;
		opts.mhParent = hwnd ? (VDGUIHandle)hwnd : (VDGUIHandle)mhdlg;
		opts.mpMessage = mFailedMsg.c_str();
		opts.mpTitle = mFailedTitle.empty() ? nullptr : mFailedTitle.c_str();
		opts.mIconType = kATUIGenericIconType_Error;
		opts.mResultMask = kATUIGenericResultMask_OK;

		ATUIShowGenericDialogAutoCenter(opts);
	}
}

void VDDialogFrameW32::PlayAlert() {
	MessageBeep(MB_ICONEXCLAMATION);
}

void VDDialogFrameW32::SetPeriodicTimer(uint32 id, uint32 msperiod) {
	::SetTimer(mhdlg, id, msperiod, NULL);
}

void VDDialogFrameW32::ShowInfo(VDGUIHandle hParent, const wchar_t *message, const wchar_t *caption) {
	ATUIGenericDialogOptions opts;
	opts.mhParent = hParent;
	opts.mpMessage = message;
	opts.mpCaption = caption ? caption : spDefaultCaption;
	opts.mIconType = kATUIGenericIconType_Info;
	opts.mResultMask = kATUIGenericResultMask_OK;

	ATUIShowGenericDialog(opts);
}

void VDDialogFrameW32::ShowError(VDGUIHandle hParent, const wchar_t *message, const wchar_t *caption) {
	ATUIGenericDialogOptions opts;
	opts.mhParent = hParent;
	opts.mpMessage = message;
	opts.mpCaption = caption ? caption : spDefaultCaption;
	opts.mIconType = kATUIGenericIconType_Error;
	opts.mResultMask = kATUIGenericResultMask_OK;

	ATUIShowGenericDialogAutoCenter(opts);
}

void VDDialogFrameW32::ShowInfo(const wchar_t *message, const wchar_t *caption) {
	if (!caption)
		caption = spDefaultCaption;

	ShowInfo((VDGUIHandle)mhdlg, message, caption);
}

void VDDialogFrameW32::ShowInfo2(const wchar_t *message, const wchar_t *title) {
	ATUIGenericDialogOptions opts;
	opts.mhParent = (VDGUIHandle)mhdlg;
	opts.mpMessage = message;
	opts.mpTitle = title;
	opts.mIconType = kATUIGenericIconType_Info;
	opts.mResultMask = kATUIGenericResultMask_OK;

	ATUIShowGenericDialogAutoCenter(opts);
}

void VDDialogFrameW32::ShowWarning(const wchar_t *message, const wchar_t *caption) {
	if (!caption)
		caption = spDefaultCaption;

	ATUIGenericDialogOptions opts;
	opts.mhParent = (VDGUIHandle)mhdlg;
	opts.mpMessage = message;
	opts.mpCaption = caption;
	opts.mIconType = kATUIGenericIconType_Warning;
	opts.mResultMask = kATUIGenericResultMask_OK;

	ATUIShowGenericDialogAutoCenter(opts);
}

void VDDialogFrameW32::ShowError(const wchar_t *message, const wchar_t *caption) {
	ShowError((VDGUIHandle)mhdlg, message, caption);
}

void VDDialogFrameW32::ShowError(const MyError& e) {
	// don't show user abort errors
	if (e.visible())
		ShowError(e.wc_str());
}

void VDDialogFrameW32::ShowError2(const wchar_t *message, const wchar_t *title) {
	ATUIGenericDialogOptions opts;
	opts.mhParent = (VDGUIHandle)mhdlg;
	opts.mpMessage = message;
	opts.mpTitle = title;
	opts.mIconType = kATUIGenericIconType_Error;
	opts.mResultMask = kATUIGenericResultMask_OK;

	ATUIShowGenericDialogAutoCenter(opts);
}

void VDDialogFrameW32::ShowError2(const MyError& e, const wchar_t *title) {
	// don't show user abort errors
	if (e.visible())
		ShowError2(e.wc_str(), title);
}

bool VDDialogFrameW32::Confirm(const wchar_t *message, const wchar_t *caption) {
	if (!caption)
		caption = spDefaultCaption;

	const int result = ::MessageBoxW(mhdlg, message, caption, MB_OKCANCEL | MB_ICONEXCLAMATION);

	return result == IDOK;
}

bool VDDialogFrameW32::Confirm2(const char *ignoreTag, const wchar_t *message, const wchar_t *title) {
	return ATUIConfirm((VDGUIHandle)mhdlg, ignoreTag, message, title);
}

void VDDialogFrameW32::SetDefaultCaption(const wchar_t *caption) {
	spDefaultCaption = caption;
}

struct VDDialogFrameW32::DynamicPopupMenu {
	DynamicPopupMenu(const wchar_t *const *items);
	DynamicPopupMenu(vdspan<PopupMenuItem> items);
	~DynamicPopupMenu();

	HMENU mhmenu = nullptr;
	HBITMAP mhbmElevation = nullptr;
	UINT mCommandIdEnd = 0;
	
	static constexpr UINT kCmdBase = 100;
};

VDDialogFrameW32::DynamicPopupMenu::DynamicPopupMenu(const wchar_t *const *items) {
	mhmenu = CreatePopupMenu();
	if (!mhmenu)
		return;

	UINT commandId = kCmdBase;
	while(const wchar_t *s = *items++) {
		if (!wcscmp(s, L"---"))
			VDAppendMenuSeparatorW32(mhmenu);
		else
			VDAppendMenuW32(mhmenu, MF_ENABLED, commandId, s);

		++commandId;
	}

	mCommandIdEnd = commandId;
}

VDDialogFrameW32::DynamicPopupMenu::DynamicPopupMenu(vdspan<PopupMenuItem> items) {
	mhmenu = CreatePopupMenu();

	if (!mhmenu)
		return;

	UINT commandId = kCmdBase;
	HBITMAP hbmElevation = nullptr;
	for(const PopupMenuItem& item : items) {
		VDAppendMenuW32(mhmenu, item.mbDisabled ? MF_DISABLED : MF_ENABLED, commandId++, item.mDisplayName.c_str());

		if (item.mbElevationRequired) {
			SHSTOCKICONINFO ssii {};
			ssii.cbSize = sizeof(SHSTOCKICONINFO);

			HRESULT hr = SHGetStockIconInfo(SIID_SHIELD, SHGSI_ICON | SHGSI_SMALLICON, &ssii);
			if (SUCCEEDED(hr) && ssii.hIcon) {
				ICONINFOEX iie {};
				iie.cbSize = sizeof(ICONINFOEX);
				if (GetIconInfoEx(ssii.hIcon, &iie)) {
					// we need to clone the icon for the alpha to work; this is imperfect as it thresholds,
					// but it's better than a black background
					hbmElevation = (HBITMAP)CopyImage(iie.hbmColor, IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION);

					if (iie.hbmColor) {
						VDVERIFY(DeleteObject(iie.hbmColor));
					}

					if (iie.hbmMask) {
						VDVERIFY(DeleteObject(iie.hbmMask));
					}
				}

				DestroyIcon(ssii.hIcon);
			}

			SetMenuItemBitmaps(mhmenu, commandId - (kCmdBase + 1), MF_BYPOSITION, hbmElevation, nullptr);
		}
	}

	mCommandIdEnd = commandId;
}

VDDialogFrameW32::DynamicPopupMenu::~DynamicPopupMenu() {
	if (mhmenu)
		DestroyMenu(mhmenu);

	if (mhbmElevation)
		DeleteObject(mhbmElevation);
}

int VDDialogFrameW32::ActivateMenuButton(uint32 id, const wchar_t *const *items) {
	if (!mhdlg)
		return -1;

	HWND hwndItem = GetDlgItem(mhdlg, id);
	if (!hwndItem)
		return -1;

	RECT r;
	if (!GetWindowRect(hwndItem, &r))
		return -1;

	DynamicPopupMenu dpMenu(items);
	if (!dpMenu.mhmenu)
		return -1;

	TPMPARAMS params = { sizeof(TPMPARAMS) };
	params.rcExclude = r;
	UINT selectedId = (UINT)TrackPopupMenuEx(dpMenu.mhmenu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_HORIZONTAL | TPM_NONOTIFY | TPM_RETURNCMD, r.left, r.bottom, mhdlg, &params);

	if (selectedId >= dpMenu.kCmdBase && selectedId < dpMenu.mCommandIdEnd)
		return selectedId - dpMenu.kCmdBase;
	else
		return -1;
}

int VDDialogFrameW32::ActivateMenuButton(uint32 id, vdspan<PopupMenuItem> items) {
	if (!mhdlg)
		return -1;

	HWND hwndItem = GetDlgItem(mhdlg, id);
	if (!hwndItem)
		return -1;

	RECT r;
	if (!GetWindowRect(hwndItem, &r))
		return -1;

	DynamicPopupMenu dpMenu(items);
	if (!dpMenu.mhmenu)
		return -1;

	TPMPARAMS params = { sizeof(TPMPARAMS) };
	params.rcExclude = r;
	UINT selectedId = (UINT)TrackPopupMenuEx(dpMenu.mhmenu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_HORIZONTAL | TPM_NONOTIFY | TPM_RETURNCMD, r.left, r.bottom, mhdlg, &params);

	if (selectedId >= dpMenu.kCmdBase && selectedId < dpMenu.mCommandIdEnd)
		return selectedId - dpMenu.kCmdBase;
	else
		return -1;
}

int VDDialogFrameW32::ActivatePopupMenu(int x, int y, const wchar_t *const *items) {
	if (!mhdlg)
		return -1;

	DynamicPopupMenu dpMenu(items);
	if (!dpMenu.mhmenu)
		return -1;

	UINT selectedId = (UINT)TrackPopupMenuEx(dpMenu.mhmenu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_HORIZONTAL | TPM_NONOTIFY | TPM_RETURNCMD, x, y, mhdlg, NULL);

	if (selectedId >= dpMenu.kCmdBase && selectedId < dpMenu.mCommandIdEnd)
		return selectedId - dpMenu.kCmdBase;
	else
		return -1;
}

int VDDialogFrameW32::ActivatePopupMenu(int x, int y, vdspan<PopupMenuItem> items) {
	if (!mhdlg)
		return -1;

	DynamicPopupMenu dpMenu(items);
	if (!dpMenu.mhmenu)
		return -1;

	UINT selectedId = (UINT)TrackPopupMenuEx(dpMenu.mhmenu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_HORIZONTAL | TPM_NONOTIFY | TPM_RETURNCMD, x, y, mhdlg, NULL);

	if (selectedId >= dpMenu.kCmdBase && selectedId < dpMenu.mCommandIdEnd)
		return selectedId - dpMenu.kCmdBase;
	else
		return -1;
}

void VDDialogFrameW32::ActivateCommandPopupMenu(int x, int y, uint32 menuID, vdfunction<void(uint32 id, VDMenuItemInitializer&)> initer) {
	ActivateCommandPopupMenuInternal(false, vdrect32(x, y, x, y), menuID, std::move(initer));
}

uint32 VDDialogFrameW32::ActivateCommandPopupMenuReturnId(int x, int y, uint32 menuID, vdfunction<void(uint32 id, VDMenuItemInitializer&)> initer) {
	return ActivateCommandPopupMenuInternal(true, vdrect32(x, y, x, y), menuID, std::move(initer));
}

uint32 VDDialogFrameW32::ActivateCommandPopupMenuReturnId(const ATUINativeWindowProxy& anchorControl, const MenuSource& menuHandleOrResId, vdfunction<void(uint32 id, VDMenuItemInitializer&)> initer) {
	const vdrect32& r = anchorControl.GetWindowArea();

	return ActivateCommandPopupMenuInternal(true, r, menuHandleOrResId, std::move(initer));
}

uint32 VDDialogFrameW32::ActivateCommandPopupMenuInternal(bool returnId, const vdrect32& r, const MenuSource& menuHandleOrResId, vdfunction<void(uint32 id, VDMenuItemInitializer&)> initer) {
	HMENU hLoadedMenu = nullptr;
	uint32 selectedId = 0;

	HMENU hmenu = nullptr;

	if (menuHandleOrResId.IsResId()) {
		hLoadedMenu = LoadMenu(VDGetLocalModuleHandleW32(), MAKEINTRESOURCE(menuHandleOrResId.GetResId()));
		if (hLoadedMenu) {
			hmenu = GetSubMenu(hLoadedMenu, 0);
		}
	} else {
		hmenu = menuHandleOrResId.GetHMenu();
	}

	if (hmenu) {
		auto initMenu = [&](HMENU hInitMenu, auto& self) -> void {
			const int numItems = GetMenuItemCount(hInitMenu);

			for(int i=0; i<numItems; ++i) {
				MENUITEMINFO mii {};
				mii.cbSize = sizeof mii;
				mii.fMask = MIIM_ID | MIIM_FTYPE | MIIM_SUBMENU;
				if (GetMenuItemInfo(hInitMenu, i, TRUE, &mii)) {
					if (mii.hSubMenu)
						self(mii.hSubMenu, self);
					else if (!(mii.fType & MFT_SEPARATOR)) {
						VDMenuItemInitializer itemIniter(hInitMenu, i);
						initer(mii.wID, itemIniter);
					}
				}
			}
		};

		if (initer)
			initMenu(hmenu, initMenu);

		TPMPARAMS params {
			sizeof(TPMPARAMS),
			{
				r.left,
				r.top,
				r.right,
				r.bottom
			}
		};

		selectedId = (uint32)TrackPopupMenuEx(hmenu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_HORIZONTAL | (returnId ? TPM_RETURNCMD : 0), r.left, r.bottom, mhdlg, &params);
	}

	if (hLoadedMenu)
		DestroyMenu(hLoadedMenu);

	return selectedId;
}

void VDDialogFrameW32::LBClear(uint32 id) {
	SendDlgItemMessage(mhdlg, id, LB_RESETCONTENT, 0, 0);
}

sint32 VDDialogFrameW32::LBGetSelectedIndex(uint32 id) {
	return (sint32)SendDlgItemMessage(mhdlg, id, LB_GETCURSEL, 0, 0);
}

void VDDialogFrameW32::LBSetSelectedIndex(uint32 id, sint32 idx) {
	SendDlgItemMessage(mhdlg, id, LB_SETCURSEL, idx, 0);
}

void VDDialogFrameW32::LBAddString(uint32 id, const wchar_t *s) {
	SendDlgItemMessageW(mhdlg, id, LB_ADDSTRING, 0, (LPARAM)s);
}

void VDDialogFrameW32::LBAddStringF(uint32 id, const wchar_t *format, ...) {
	VDStringW s;
	va_list val;

	va_start(val, format);
	s.append_vsprintf(format, val);
	va_end(val);

	LBAddString(id, s.c_str());
}

void VDDialogFrameW32::CBClear(uint32 id) {
	SendDlgItemMessage(mhdlg, id, CB_RESETCONTENT, 0, 0);
}

sint32 VDDialogFrameW32::CBGetSelectedIndex(uint32 id) {
	return (sint32)SendDlgItemMessage(mhdlg, id, CB_GETCURSEL, 0, 0);
}

void VDDialogFrameW32::CBSetSelectedIndex(uint32 id, sint32 idx) {
	SendDlgItemMessage(mhdlg, id, CB_SETCURSEL, idx, 0);
}

void VDDialogFrameW32::CBAddString(uint32 id, const wchar_t *s) {
	SendDlgItemMessageW(mhdlg, id, CB_ADDSTRING, 0, (LPARAM)s);
}

sint32 VDDialogFrameW32::TBGetValue(uint32 id) {
	return (sint32)SendDlgItemMessage(mhdlg, id, TBM_GETPOS, 0, 0);
}

void VDDialogFrameW32::TBSetValue(uint32 id, sint32 value) {
	SendDlgItemMessage(mhdlg, id, TBM_SETPOS, TRUE, value);
}

void VDDialogFrameW32::TBSetRange(uint32 id, sint32 minval, sint32 maxval) {
	SendDlgItemMessage(mhdlg, id, TBM_SETRANGEMIN, FALSE, minval);
	SendDlgItemMessage(mhdlg, id, TBM_SETRANGEMAX, TRUE, maxval);
}

void VDDialogFrameW32::TBSetPageStep(uint32 id, sint32 pageStep) {
	SendDlgItemMessage(mhdlg, id, TBM_SETPAGESIZE, 0, pageStep);
}

void VDDialogFrameW32::UDSetRange(uint32 id, sint32 minval, sint32 maxval) {
	SendDlgItemMessage(mhdlg, id, UDM_SETRANGE32, minval, maxval);
}

bool VDDialogFrameW32::PostCall(const vdfunction<void()>& call) {
	bool success = false;

	mMutex.Lock();
	if (mhdlg) {
		bool needPost = mPostedCalls.empty();

		mPostedCalls.push_back(call);
		if (needPost)
			PostMessage(mhdlg, VDWM_APP_POSTEDCALL, 0, 0);

		success = true;
	}
	mMutex.Unlock();

	return success;
}

bool VDDialogFrameW32::PostCall(vdfunction<void()>&& call) {
	bool success = false;

	mMutex.Lock();
	if (mhdlg) {
		bool needPost = mPostedCalls.empty();

		mPostedCalls.push_back(std::move(call));

		if (needPost)
			PostMessage(mhdlg, VDWM_APP_POSTEDCALL, 0, 0);
		success = true;
	}
	mMutex.Unlock();

	return success;
}

void VDDialogFrameW32::OnDataExchange(bool write) {
}

void VDDialogFrameW32::OnPreLoaded() {
	struct ItemHeader {
		DWORD helpID;
		DWORD exStyle;
		DWORD style;
		short x;
		short y;
		short cx;
		short cy;
		DWORD id;
	};

	static const WCHAR *const kBuiltinClasses[] = {
		WC_BUTTONW,
		WC_EDITW,
		WC_STATICW,
		WC_LISTBOXW,
		WC_SCROLLBARW,
		WC_COMBOBOXW
	};

	VDASSERTCT(sizeof(ItemHeader) == 24);

	// Disable dialog auto-resize on DPI change, as we'll be handling it
	g_pATSetDialogDpiChangeBehaviorW32(mhdlg, (DIALOG_DPI_CHANGE_BEHAVIORS)0x03, DDC_DISABLE_ALL);

	// Get current DPI
	mCurrentDpi = ATUIGetWindowDpiW32(mhdlg);

	// Create font
	mhfont = CreateNewFont();

	if (!mhfont)
		return;

	RecomputeDialogUnits();

	// Resize the window
	RECT r = { 0, 0, MulDiv(mTemplateWidthDLUs, mDialogUnits.mWidth4, 4), MulDiv(mTemplateHeightDLUs, mDialogUnits.mHeight8, 8) };

	g_pATAdjustWindowRectExForDpiW32(&r, GetWindowLong(mhdlg, GWL_STYLE), GetMenu(mhdlg) != nullptr, GetWindowLong(mhdlg, GWL_EXSTYLE), mCurrentDpi);

	SetWindowPos(mhdlg, nullptr, 0, 0, r.right - r.left, r.bottom - r.top, SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);

	mResizer.Init(mhdlg);
	mResizer.SetRefUnits(mDialogUnits.mWidth4, mDialogUnits.mHeight8);
	ClearControlCaches();

	// Instantiate controls
	const char *src = mpTemplateControls;
	uint32 defId = 0;

	for(uint32 i=0; i<mTemplateControlCount; ++i) {
		ItemHeader hdr;

		// read base items
		memcpy(&hdr, src, sizeof(hdr));
		src += sizeof(hdr);

		// read window class
		const WCHAR *className = nullptr;
		bool disableTheming = false;

		if (VDReadUnalignedU16(src) == UINT16_C(0xFFFF)) {
			src += 2;

			const uint16 token = VDReadUnalignedU16(src);
			src += 2;

			if (token < 0x80 || token > 0x85) {
				VDASSERT(!"Invalid control type token in dialog item template.");
				return;
			}

			if (token == 0x80) {
				switch(hdr.style & BS_TYPEMASK) {
					case BS_PUSHBUTTON:
					case BS_DEFPUSHBUTTON:
						mButtons.emplace_back(hdr.id, nullptr, ButtonType::Button);
						break;

					case BS_CHECKBOX:
					case BS_AUTOCHECKBOX:
					case BS_3STATE:
					case BS_AUTO3STATE:
						mButtons.emplace_back(hdr.id, nullptr, hdr.style & BS_PUSHLIKE ? ButtonType::Button : ButtonType::Checkbox);
						break;

					case BS_RADIOBUTTON:
					case BS_AUTORADIOBUTTON:
						mButtons.emplace_back(hdr.id, nullptr, ButtonType::Radio);
						break;

					case BS_GROUPBOX:
						disableTheming = true;
						break;

					case BS_SPLITBUTTON:
						if (ATUIIsDarkThemeActive())
							disableTheming = true;
						break;

					default:
						break;
				}
			}

			className = kBuiltinClasses[token - 0x80];
		} else {
			className = (const WCHAR *)src;

			while(VDReadUnalignedU16(src))
				src += 2;

			src += 2;

			// Reclass RichEdit 2.0 controls to RichEdit 5.0, since we need the latter for
			// proper high-DPI operation.
			if (!wcscmp(className, L"RichEdit20W"))
				className = L"RichEdit50W";
		}

		// read title
		const WCHAR *title = nullptr;
		if (VDReadUnalignedU16(src) == UINT16_C(0xFFFF)) {
			src += 2;

			title = MAKEINTRESOURCEW(VDReadUnalignedU16(src));
			src += 2;
		} else {
			title = (const WCHAR *)src;

			while(VDReadUnalignedU16(src))
				src += 2;

			src += 2;
		}

		// read extra count
		const void *extraData = nullptr;
		const uint16 extraSize = VDReadUnalignedU16(src);
		src += 2;

		if (extraSize) {
			extraData = src;
			src += extraSize;
		}

		// alignment
		src = (const char *)(((uintptr)src + 3) & ~(uintptr)3);

		// map child window size to DLUs
		// note: we need to map size instead of rects, or controls will vary in size (yuck)
		const int x = MulDiv(hdr.x, mDialogUnits.mWidth4, 4);
		const int y = MulDiv(hdr.y, mDialogUnits.mHeight8, 8);
		const int cx = MulDiv(hdr.cx, mDialogUnits.mWidth4, 4);
		const int cy = MulDiv(hdr.cy, mDialogUnits.mHeight8, 8);

		// create the window
		HWND hwnd = CreateWindowExW(hdr.exStyle, className, title, hdr.style | WS_CHILD, x, y, cx, cy, mhdlg, (HMENU)(uintptr)hdr.id, VDGetLocalModuleHandleW32(), (LPVOID)extraData);
		if (!hwnd)
			return;

		if (hdr.helpID)
			SetWindowContextHelpId(hwnd, hdr.helpID);

		if (disableTheming)
			SetWindowTheme(hwnd, L"", L"");

		// check if this is a default button
		if (SendMessageW(hwnd, WM_GETDLGCODE, 0, 0) & DLGC_DEFPUSHBUTTON)
			defId = hdr.id;

		// Check if we have an up-down control with the auto-buddy flag set. If
		// so, we need to handle this specially in the resizer. Note that we
		// check the style first as an optimization.
		uint32 alignment = mResizer.kTL;

		if ((hdr.style & UDS_AUTOBUDDY) && !vdwcsicmp(className, UPDOWN_CLASSW))
			alignment |= mResizer.kUpDownAutoBuddy;

		SendMessageW(hwnd, WM_SETFONT, (WPARAM)mhfont, TRUE);

		g_pATSetDialogControlDpiChangeBehaviorW32(hwnd,
			(DIALOG_CONTROL_DPI_CHANGE_BEHAVIORS)(DCDC_DISABLE_FONT_UPDATE | DCDC_DISABLE_RELAYOUT),
			(DIALOG_CONTROL_DPI_CHANGE_BEHAVIORS)(DCDC_DISABLE_FONT_UPDATE | DCDC_DISABLE_RELAYOUT));

		if (!vdwcsicmp(className, MSFTEDIT_CLASS)) {
			alignment |= mResizer.kSuppressFontChange;

			// The RichEdit control has problems on Windows 10 1803 with not setting up initial
			// font scaling properly, only adjusting after a DPI change. We force-feed the control
			// messages here to work around this.
			if (VDIsAtLeast10W32()) {
				SendMessageW(hwnd, WM_DPICHANGED_BEFOREPARENT, 0, 0);
				SendMessageW(hwnd, WM_DPICHANGED_AFTERPARENT, 0, 0);
			}
		}

		// Add the control to tracker. Note that this needs to be the original size, not the current size,
		// for combo boxes to work.
		mResizer.Add(hwnd, x, y, cx, cy, alignment);
	}

	std::sort(mButtons.begin(), mButtons.end());

	if (defId)
		SendMessageW(mhdlg, DM_SETDEFID, defId, 0);
}

bool VDDialogFrameW32::OnLoaded() {
	OnDataExchange(false);
	return false;
}

void VDDialogFrameW32::OnPostLoaded() {
}

bool VDDialogFrameW32::OnOK() {
	BeginValidation();

	try {
		OnDataExchange(true);
	} catch(const MyError& e) {
		ShowError(e);
		return true;
	}

	return !EndValidation();
}

bool VDDialogFrameW32::OnCancel() {
	return false;
}

void VDDialogFrameW32::OnSize() {
	int refX = mDialogUnits.mWidth4;
	int refY = mDialogUnits.mHeight8;
	mResizer.Relayout(&refX, &refY);
}

bool VDDialogFrameW32::OnClose() {
	if (!mbIsModal) {
		// Multi-line edit and rich edit controls within dialogs translate Escape directly to WM_CLOSE
		// instead of WM_COMMAND(IDCANCEL) as dialogs do. We can't just eat the message as that would
		// suppress the event, so instead we propagate it upward.
		if (GetWindowLongPtr(mhdlg, GWL_STYLE) & WS_CHILD) {
			PostMessage(GetParent(mhdlg), WM_CLOSE, 0, 0);
			return true;
		}

		DestroyWindow(mhdlg);
		return true;
	}

	return false;
}

void VDDialogFrameW32::OnPreDestroy() {
}

void VDDialogFrameW32::OnDestroy() {
	mMsgDispatcher.RemoveAllControls(true);
}

void VDDialogFrameW32::OnEnable(bool enabled) {
}

bool VDDialogFrameW32::OnShouldErase() {
	return true;
}

bool VDDialogFrameW32::OnErase(VDZHDC hdc) {
	return false;
}

bool VDDialogFrameW32::OnPaint() {
	return false;
}

bool VDDialogFrameW32::OnTimer(uint32 id) {
	return false;
}

bool VDDialogFrameW32::OnCommand(uint32 id, uint32 extcode) {
	return false;
}

void VDDialogFrameW32::OnHScroll(uint32 code, int id) {
}

void VDDialogFrameW32::OnVScroll(uint32 code, int id) {
}

void VDDialogFrameW32::OnMouseMove(int x, int y) {
}

void VDDialogFrameW32::OnMouseDownL(int x, int y) {
}

void VDDialogFrameW32::OnMouseUpL(int x, int y) {
}

bool VDDialogFrameW32::OnMouseDownR(int x, int y) {
	return false;
}

bool VDDialogFrameW32::OnMouseUpR(int x, int y) {
	return false;
}

void VDDialogFrameW32::OnMouseWheel(int x, int y, sint32 delta) {
}

void VDDialogFrameW32::OnMouseLeave() {
}

void VDDialogFrameW32::OnMouseHover() {
}

bool VDDialogFrameW32::OnSetCursor(int x, int y, ATUICursorImage& image) {
	return OnSetCursor(image);
}

bool VDDialogFrameW32::OnSetCursor(ATUICursorImage& image) {
	return false;
}

void VDDialogFrameW32::OnCaptureLost() {
}

void VDDialogFrameW32::OnDropFiles(VDZHDROP hdrop) {
	VDUIDropFileListW32 dropList(hdrop);

	OnDropFiles(&dropList);
	DragFinish(hdrop);
}

void VDDialogFrameW32::OnDropFiles(IVDUIDropFileList *dropFileList) {
}

void VDDialogFrameW32::OnSetFocus() {
}

void VDDialogFrameW32::OnHelp() {
}

void VDDialogFrameW32::OnInitMenu(VDZHMENU hmenu) {
}

void VDDialogFrameW32::OnContextMenu(uint32 id, int x, int y) {
}

void VDDialogFrameW32::OnSetFont(VDZHFONT hfont) {
}

void VDDialogFrameW32::OnDpiChanging(uint16 newDpiX, uint16 newDpiY, const vdrect32 *suggestedRect) {
	if (mCurrentDpi != newDpiY) {
		mCurrentDpi = newDpiY;

		VDZHFONT hfont = CreateNewFont();
		if (hfont)
			SetFont(hfont);

		const vdsize32 templatePixelSize = ComputeTemplatePixelSize(mDialogUnits, mCurrentDpi);

		if (mMinWidth) {
			mMinWidth = templatePixelSize.w;
			mMinHeight = templatePixelSize.h;

			if (mMaxWidth != INT_MAX)
				mMaxWidth = mMinWidth;

			if (mMaxHeight != INT_MAX)
				mMaxHeight = mMinHeight;
		}

		if (suggestedRect) {
			int newWidth = suggestedRect->width();
			int newHeight = suggestedRect->height();

			AdjustSize(newWidth, newHeight, templatePixelSize, mDialogUnits);

			const int x = suggestedRect->left;
			const int y = suggestedRect->top;
			SetWindowPos(mhdlg, nullptr, x, y, newWidth, newHeight, SWP_NOZORDER | SWP_NOACTIVATE);
		}

		ResetControlCachesForDpiChange();
		OnDpiChanged();
	}
}

void VDDialogFrameW32::OnDpiChanged() {
}

uint32 VDDialogFrameW32::OnButtonCustomDraw(VDZLPARAM lParam, ButtonInfo& buttonInfo) {
	NMCUSTOMDRAW& hdr = *(NMCUSTOMDRAW *)lParam;
	const LONG_PTR style = GetWindowLongPtr(hdr.hdr.hwndFrom, GWL_STYLE);

	if (buttonInfo.mType != ButtonType::Button && buttonInfo.mIconWidth <= 0) {
		if (buttonInfo.mIconWidth < 0)
			return CDRF_DODEFAULT;

		buttonInfo.mIconWidth = -1;

		HTHEME htheme = GetWindowTheme(hdr.hdr.hwndFrom);
		HTHEME htheme2 = nullptr;

		// radio buttons don't have theme data, weirdly
		if (!htheme2)
			htheme = htheme2 = OpenThemeData(hdr.hdr.hwndFrom, L"BUTTON");

		if (htheme) {
			SIZE sz;
			RECT r = hdr.rc;

			if (buttonInfo.mType == ButtonType::Radio)
				GetThemePartSize(htheme, hdr.hdc, BP_RADIOBUTTON, CBS_UNCHECKEDNORMAL, &r, TS_TRUE, &sz);
			else
				GetThemePartSize(htheme, hdr.hdc, BP_CHECKBOX, CBS_UNCHECKEDNORMAL, &r, TS_TRUE, &sz);

			if (sz.cx > 0 && sz.cy > 0) {
				buttonInfo.mIconWidth = sz.cx;
				buttonInfo.mIconHeight = sz.cy;
			}
		}

		if (htheme2)
			CloseThemeData(htheme2);
	}

	if (hdr.dwDrawStage == CDDS_PREERASE) {
		if (buttonInfo.mType == ButtonType::Button)
			return CDRF_NOTIFYPOSTERASE;

		int savedDC = SaveDC(hdr.hdc);
		if (savedDC) {
			const ATUIThemeColors& tc = ATUIGetThemeColors();
			RECT r = hdr.rc;

			const UINT checkedState = SendMessage(hdr.hdr.hwndFrom, BM_GETCHECK, 0, 0);
			const bool showPrefix = (hdr.uItemState & CDIS_SHOWKEYBOARDCUES) != 0;
			const bool checked = checkedState == BST_CHECKED;	// CDIS_CHECKED does not work
			const bool indeterminate = checkedState == BST_INDETERMINATE;
			const bool disabled = (hdr.uItemState & CDIS_DISABLED) != 0;
			const bool focused = (hdr.uItemState & CDIS_FOCUS) != 0;
			const bool pushed = (hdr.uItemState & CDIS_SELECTED) != 0;
			const bool highlighted = (hdr.uItemState & CDIS_HOT) != 0;

			RECT rCheck { 0, 0, buttonInfo.mIconWidth, buttonInfo.mIconHeight };

			if (!(style & BS_MULTILINE)) {
				rCheck.top += (r.bottom - rCheck.bottom) >> 1;
				rCheck.bottom += rCheck.top;
			}

			int cw = buttonInfo.mIconWidth;
			int ch = buttonInfo.mIconHeight;

			const bool isRadio = (buttonInfo.mType == ButtonType::Radio);

			if (!buttonInfo.mpStyle)
				buttonInfo.mpStyle = ATUIGetCheckboxStyleW32(hdr.hdc, cw, ch).release();

			if (buttonInfo.mpStyle)
				buttonInfo.mpStyle->Draw(hdr.hdc, rCheck.left, rCheck.top, isRadio, disabled, pushed, highlighted, checked, indeterminate);

			// Reference for this slightly odd way of determing the gap: https://stackoverflow.com/a/59376905
			INT zeroWidth = 0;
			GetCharWidth32W(hdr.hdc, L'0', L'0', &zeroWidth);

			RECT rLabel = r;
			rLabel.left = buttonInfo.mIconWidth + zeroWidth / 2;

			VDStringW label = VDGetWindowTextW32(hdr.hdr.hwndFrom);

			SetBkColor(hdr.hdc, VDSwizzleU32(tc.mStaticBg) >> 8);
			SetBkMode(hdr.hdc, OPAQUE);
			ExtTextOutW(hdr.hdc, 0, 0, ETO_OPAQUE | ETO_IGNORELANGUAGE, &rLabel, L"", 0, nullptr);

			SetTextColor(hdr.hdc, VDSwizzleU32(disabled ? tc.mDisabledFg : tc.mStaticFg) >> 8);
			SetBkMode(hdr.hdc, TRANSPARENT);

			const UINT baseFormat = DT_LEFT | DT_VCENTER | (style & BS_MULTILINE ? DT_WORDBREAK : DT_SINGLELINE) | (showPrefix ? 0 : DT_HIDEPREFIX);
			if (focused) {
				RECT rText = rLabel;
				DrawTextW(hdr.hdc, label.c_str(), (int)label.size(), &rText, DT_CALCRECT | baseFormat);

				SelectObject(hdr.hdc, GetStockObject(DC_PEN));
				SetDCPenColor(hdr.hdc, VDSwizzleU32(tc.mFocusedRect) >> 8);
				SelectObject(hdr.hdc, GetStockObject(NULL_BRUSH));

				Rectangle(hdr.hdc, rText.left - 1, rText.top, rText.right + 1, rText.bottom);
			}

			DrawTextW(hdr.hdc, label.c_str(), (int)label.size(), &rLabel, baseFormat);

			RestoreDC(hdr.hdc, savedDC);
		}

		return CDRF_SKIPDEFAULT;
	} else if (hdr.dwDrawStage == CDDS_PREPAINT) {
		if (buttonInfo.mType != ButtonType::Button)
			return CDRF_SKIPDEFAULT;

		int savedDC = SaveDC(hdr.hdc);
		if (savedDC) {
			const ATUIThemeColors& tc = ATUIGetThemeColors();
			RECT r = hdr.rc;

			HBRUSH dcBrush = (HBRUSH)GetStockObject(DC_BRUSH);
			const bool pushed = (hdr.uItemState & CDIS_SELECTED) != 0;
			const bool checked = SendMessage(hdr.hdr.hwndFrom, BM_GETCHECK, 0, 0) == BST_CHECKED;	// CDIS_CHECKED does not work
			const bool showPrefix = (hdr.uItemState & CDIS_SHOWKEYBOARDCUES) != 0;
			const bool disabled = (hdr.uItemState & CDIS_DISABLED) != 0;
			const bool focused = (hdr.uItemState & CDIS_FOCUS) != 0;

			SetDCBrushColor(hdr.hdc, VDSwizzleU32(pushed ? tc.mHardNegEdge : tc.mHardPosEdge) >> 8);
			{ RECT r1 { r.left, r.top, r.right, r.top + 1 }; FillRect(hdr.hdc, &r1, dcBrush); }
			{ RECT r2 { r.left, r.top + 1, r.left + 1, r.bottom - 1 }; FillRect(hdr.hdc, &r2, dcBrush); }

			SetDCBrushColor(hdr.hdc, VDSwizzleU32(pushed ? tc.mHardPosEdge : tc.mHardNegEdge) >> 8);
			{ RECT r3 { r.left, r.bottom - 1, r.right, r.bottom }; FillRect(hdr.hdc, &r3, dcBrush); }
			{ RECT r4 { r.right - 1, r.top + 1, r.right, r.bottom - 1 }; FillRect(hdr.hdc, &r4, dcBrush); }

			SetDCBrushColor(hdr.hdc, VDSwizzleU32(pushed ? tc.mSoftNegEdge : tc.mSoftPosEdge) >> 8);
			{ RECT r5 { r.left + 1, r.top + 1, r.right - 1, r.top + 2 }; FillRect(hdr.hdc, &r5, dcBrush); }
			{ RECT r6 { r.left + 1, r.top + 2, r.left + 2, r.bottom - 2 }; FillRect(hdr.hdc, &r6, dcBrush); }

			SetDCBrushColor(hdr.hdc, VDSwizzleU32(pushed ? tc.mSoftPosEdge : tc.mSoftNegEdge) >> 8);
			{ RECT r7 { r.left + 1, r.bottom - 2, r.right - 1, r.bottom - 1 }; FillRect(hdr.hdc, &r7, dcBrush); }
			{ RECT r8 { r.right - 2, r.top + 2, r.right - 1, r.bottom - 2 }; FillRect(hdr.hdc, &r8, dcBrush); }

			COLORREF bkColor = VDSwizzleU32(pushed ? tc.mButtonPushedBg : checked ? tc.mButtonCheckedBg : focused ? tc.mFocusedBg : tc.mButtonBg) >> 8;
			SetDCBrushColor(hdr.hdc, bkColor);
			r.left += 2;
			r.top += 2;
			r.right -= 2;
			r.bottom -= 2;

			FillRect(hdr.hdc, &r, dcBrush);

			SetTextColor(hdr.hdc, VDSwizzleU32(disabled ? tc.mDisabledFg : tc.mButtonFg) >> 8);
			SetBkMode(hdr.hdc, TRANSPARENT);

			BUTTON_IMAGELIST imgList {};
			int imgWidth = 0;
			int imgHeight = 0;
			if (!(style & (BS_ICON | BS_BITMAP)) && Button_GetImageList(hdr.hdr.hwndFrom, &imgList) && imgList.himl && ImageList_GetIconSize(imgList.himl, &imgWidth, &imgHeight)) {
				// We must intercept this case first as BM_GETIMAGE returns bogus handles -- this
				// is used implicitly when BCM_SETSHIELD is called. The style check is necessary as
				// otherwise the button returns an image list anyway (#$&).

				VDStringW label = VDGetWindowTextW32(hdr.hdr.hwndFrom);

				label.insert(label.begin(), L' ');

				SIZE sz {};
				GetTextExtentPoint32W(hdr.hdc, label.c_str(), label.size(), &sz);

				int fullWidth = imgWidth + imgList.margin.left + imgList.margin.right + sz.cx;
				int iconX = r.left + ((r.right - r.left - fullWidth) >> 1) + imgList.margin.left;
				int iconY = r.top + ((r.bottom - r.top - imgHeight) >> 1);
				int textX = iconX + imgList.margin.right + imgWidth;

				IntersectClipRect(hdr.hdc, r.left, r.top, r.right, r.bottom);

				ImageList_DrawEx(imgList.himl, 0, hdr.hdc, iconX, iconY, 0, 0, bkColor, CLR_NONE, ILD_NORMAL);

				RECT rText = r;
				rText.left = textX;
				DrawTextW(hdr.hdc, label.c_str(), (int)label.size(), &rText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | (showPrefix ? 0 : DT_HIDEPREFIX));
			} else if (HICON hIcon = (HICON)SendMessage(hdr.hdr.hwndFrom, BM_GETIMAGE, IMAGE_ICON, 0); hIcon) {
				ICONINFO ii {};
				if (GetIconInfo(hIcon, &ii)) {
					if (ii.hbmColor) {
						BITMAP bm {};

						if (GetObject(ii.hbmColor, sizeof bm, &bm))
							DrawIconEx(hdr.hdc, r.left + (r.right - r.left - bm.bmWidth + 1)/2, r.top + (r.bottom - r.top - bm.bmHeight + 1)/2, hIcon, 0, 0, 0, nullptr, DI_NORMAL);

						DeleteObject(ii.hbmColor);
					}

					if (ii.hbmMask)
						DeleteObject(ii.hbmMask);
				}
			} else {
				const VDStringW& label = VDGetWindowTextW32(hdr.hdr.hwndFrom);
				DrawTextW(hdr.hdc, label.c_str(), (int)label.size(), &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | (showPrefix ? 0 : DT_HIDEPREFIX));
			}

			RestoreDC(hdr.hdc, savedDC);
		}
		return CDRF_SKIPDEFAULT;
	}

	return CDRF_DODEFAULT;
}

bool VDDialogFrameW32::PreNCDestroy() {
	return false;
}

void VDDialogFrameW32::PostNCDestroy() {
}

bool VDDialogFrameW32::OnPreTranslate(VDZMSG& msg) {
	return false;
}

bool VDDialogFrameW32::DelegatePreTranslate(VDZMSG& msg) {
	if (!mAccel || !mhdlg || !msg.hwnd)
		return false;

	if (mhdlg != msg.hwnd && !IsChild(mhdlg, msg.hwnd))
		return false;

	if (!TranslateAccelerator(mhdlg, mAccel, &msg))
		return false;

	return true;
}

bool VDDialogFrameW32::ShouldSetDialogIcon() const {
	return true;
}

sint32 VDDialogFrameW32::GetBackgroundColor() const {
	if (ATUIIsDarkThemeActive()) {
		const auto& tc = ATUIGetThemeColors();

		return (sint32)tc.mStaticBg;
	}

	return -1;
}

void VDDialogFrameW32::RegisterForMouseLeave() {
	TRACKMOUSEEVENT tme = {sizeof(TRACKMOUSEEVENT)};
	tme.dwFlags = TME_LEAVE;
	tme.hwndTrack = mhdlg;
	::TrackMouseEvent(&tme);
}

void VDDialogFrameW32::LoadAcceleratorTable(uint32 id) {
	mAccel = LoadAccelerators(VDGetLocalModuleHandleW32(), MAKEINTRESOURCE(id));
}

sint32 VDDialogFrameW32::GetDpiScaledMetric(int index) const {
	return ATUIGetDpiScaledSystemMetricW32(index, mCurrentDpi);
}

sint32 VDDialogFrameW32::GetDpiScaledMetricXEdge() const {
	return ATUIGetDpiScaledSystemMetricW32(SM_CXEDGE, mCurrentDpi);
}

sint32 VDDialogFrameW32::GetDpiScaledMetricYEdge() const {
	return ATUIGetDpiScaledSystemMetricW32(SM_CYEDGE, mCurrentDpi);
}

void VDDialogFrameW32::ExecutePostedCalls() {
	while(!mPostedCalls.empty()) {
		vdfunction<void()> fn(std::move(mPostedCalls.front()));
		mPostedCalls.pop_front();

		fn();
	}
}
namespace {

	BOOL CALLBACK SetDialogIconCallback(HMODULE hModule, LPCTSTR lpszType, LPTSTR lpszName, LONG_PTR lParam) {
		HWND hdlg = (HWND)lParam;

		HANDLE hLargeIcon = LoadImage(hModule, lpszName, IMAGE_ICON, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_SHARED);
		if (hLargeIcon)
			SendMessage(hdlg, WM_SETICON, ICON_BIG, (LPARAM)hLargeIcon);

		HANDLE hSmallIcon = LoadImage(hModule, lpszName, IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_SHARED);
		if (hSmallIcon)
			SendMessage(hdlg, WM_SETICON, ICON_SMALL, (LPARAM)hSmallIcon);

		return FALSE;
	}
}

void VDDialogFrameW32::SetDialogIcon() {
	HINSTANCE hInst = VDGetLocalModuleHandleW32();

	EnumResourceNames(hInst, RT_GROUP_ICON, SetDialogIconCallback, (LONG_PTR)mhdlg);
}

VDZHFONT VDDialogFrameW32::CreateNewFont(int dpiOverride) const {
	return CreateFontW(-MulDiv(mTemplateFontPointSize, dpiOverride ? dpiOverride : mCurrentDpi, 72), 0, 0, 0, 0, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, mpTemplateFont);
}

VDDialogFrameW32::DialogUnits VDDialogFrameW32::ComputeDialogUnits(VDZHFONT hFont) const {
	// Measure dialog units (thanks to the WINE folks for determining the actual algorithm).
	DialogUnits units { 8, 16 };

	if (HDC hdc = GetDC(mhdlg)) {
		if (HGDIOBJ hfontOld = SelectObject(hdc, hFont)) {
			SIZE sz;
			if (GetTextExtentPoint32W(hdc, L"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ", 52, &sz)) {
				units.mWidth4 = (sz.cx / 26 + 1) / 2;
				units.mHeight8 = sz.cy;
			}

			SelectObject(hdc, hfontOld);
		}

		ReleaseDC(mhdlg, hdc);
	}

	return units;
}

void VDDialogFrameW32::RecomputeDialogUnits() {
	mDialogUnits = ComputeDialogUnits(mhfont);
}

vdsize32 VDDialogFrameW32::DLUsToPixelSize(const vdsize32& dluSize) const {
	return vdsize32(MulDiv(dluSize.w, mDialogUnits.mWidth4, 4), MulDiv(dluSize.h, mDialogUnits.mHeight8, 8));
}

vdsize32 VDDialogFrameW32::DLUsToPixelSizeCeil(const vdsize32& dluSize) const {
	const auto mulDivRoundToInf = [](sint32 a, sint32 b, sint32 c) {
		const sint64 product = (sint64)a * b;

		return (product + c + (c < 0 ? 1 : -1)) / c;
	};

	return vdsize32(
		mulDivRoundToInf(dluSize.w, mDialogUnits.mWidth4, 4),
		mulDivRoundToInf(dluSize.h, mDialogUnits.mHeight8, 8)
	);
}

vdsize32 VDDialogFrameW32::PixelSizeToDLUsCeil(const vdsize32& dluSize) const {
	const auto mulDivRoundToInf = [](sint32 a, sint32 b, sint32 c) {
		const sint64 product = (sint64)a * b;

		return (product + c + (c < 0 ? 1 : -1)) / c;
	};

	return vdsize32(
		mulDivRoundToInf(dluSize.w, 4, mDialogUnits.mWidth4),
		mulDivRoundToInf(dluSize.h, 8, mDialogUnits.mHeight8)
	);
}

vdsize32 VDDialogFrameW32::ComputeTemplatePixelSize() const {
	return ComputeTemplatePixelSize(mDialogUnits, mCurrentDpi);
}

vdsize32 VDDialogFrameW32::ComputeTemplatePixelSize(const DialogUnits& dialogUnits, uint32 dpi) const {
	RECT r = { 0, 0, MulDiv(mTemplateWidthDLUs, dialogUnits.mWidth4, 4), MulDiv(mTemplateHeightDLUs, dialogUnits.mHeight8, 8) };
	g_pATAdjustWindowRectExForDpiW32(&r, GetWindowLong(mhdlg, GWL_STYLE), GetMenu(mhdlg) != nullptr, GetWindowLong(mhdlg, GWL_EXSTYLE), dpi);

	return vdsize32(r.right - r.left, r.bottom - r.top);
}

void VDDialogFrameW32::AdjustSize(int& width, int& height, const vdsize32& templatePixelSize, const DialogUnits& dialogUnits) const {
	if (mbResizableWidth)
		width = std::max<int>(width, templatePixelSize.w);
	else
		width = templatePixelSize.w;

	if (mbResizableHeight)
		height = std::max<int>(height, templatePixelSize.h);
	else
		height = templatePixelSize.h;
}

sintptr VDDialogFrameW32::DoCreate(VDZHWND parent, bool modal) {
	HMODULE hmod = VDGetLocalModuleHandleW32();
	HRSRC hrsrc = FindResource(hmod, mpDialogResourceName, RT_DIALOG);
	if (!hrsrc)
		return false;

	HGLOBAL hres = LoadResource(hmod, hrsrc);
	if (!hres)
		return false;

	const char *p = (const char *)LockResource(hres);
	if (!p)
		return false;

	// get size of dialog resource
	const char *src = p;

	if (VDReadUnalignedU16(src + 2) != UINT16_C(0xFFFF)) {
		VDASSERT(!"Dialog template does not use extended format.");
		return false;
	}

	mTemplateWidthDLUs = VDReadUnalignedU16(src + 22);
	mTemplateHeightDLUs = VDReadUnalignedU16(src + 24);

	src += 26;

	// skip past menu
	if (VDReadUnalignedU16(src) == UINT16_C(0xFFFF)) {
		src += 4;
	} else {
		while(VDReadUnalignedU16(src))
			src += 2;
	
		src += 2;
	}

	// skip past window class
	if (VDReadUnalignedU16(src) == UINT16_C(0xFFFF)) {
		src += 4;
	} else {
		while(VDReadUnalignedU16(src))
			src += 2;

		src += 2;
	}

	// skip past title
	while(VDReadUnalignedU16(src))
		src += 2;

	src += 2;

	// skip past font fields
	const uint32 style = VDReadUnalignedU32(p + 12);

	if (!(style & (DS_SETFONT | DS_SHELLFONT))) {
		VDASSERT(!"Dialog template does not have SETFONT or SHELLFONT styles.");
		return false;
	}

	mTemplateFontPointSize = VDReadUnalignedU16(src);
	src += 2;

	src += 4;

	mpTemplateFont = (const wchar_t *)src;
	while(VDReadUnalignedU16(src))
		src += 2;

	src += 2;

	// make a copy of the dialog template
	const size_t templateSize = (size_t)(src - p);
	char *newTemplate = (char *)malloc(templateSize);
	const vdautoblockptr newTemplateHolder(newTemplate);

	memcpy(newTemplate, p, templateSize);

	// stash and patch the item count (cDlgItems) to 0
	mTemplateControlCount = *(uint16 *)(newTemplate + 16);
	*(uint16 *)(newTemplate + 16) = 0;

	// stash template control pointer (aligned to 4)
	mpTemplateControls = (const char *)(((uintptr)src + 3) & ~(uintptr)3);

	mbIsModal = modal;

	if (modal) {
		thread_local static HHOOK sDialogMsgHook = nullptr;
		thread_local static VDDialogFrameW32 *spCurrentDialog = nullptr;
		bool hookInstalled = false;

		if (!sDialogMsgHook) {
			sDialogMsgHook = SetWindowsHookEx(
				WH_MSGFILTER,
				static_cast<LRESULT (CALLBACK *)(int, WPARAM, LPARAM)>([](int code, WPARAM wParam, LPARAM lParam) -> LRESULT {
					if (code == MSGF_DIALOGBOX) {
						const MSG& msg = *(const MSG *)lParam;

						switch(msg.message) {
							case WM_KEYDOWN:
							case WM_SYSKEYDOWN:
							case WM_KEYUP:
							case WM_SYSKEYUP:
							case WM_CHAR:
								if (HWND hwndOwner = GetAncestor(msg.hwnd, GA_ROOT)) {
									if (hwndOwner == spCurrentDialog->GetHandleW32() && SendMessage(hwndOwner, ATWM_PRETRANSLATE, 0, (LPARAM)&msg))
										return TRUE;
								}
								break;
						}
					}

					return CallNextHookEx(sDialogMsgHook, code, wParam, lParam);
				}),
				nullptr,
				GetCurrentThreadId()
			);
			
			VDVERIFY(sDialogMsgHook);
			if (sDialogMsgHook)
				hookInstalled = true;
		}

		auto *prevDialog = std::exchange(spCurrentDialog, this);

		const INT_PTR rval = DialogBoxIndirectParamW(VDGetLocalModuleHandleW32(), (LPCDLGTEMPLATEW)newTemplate, (HWND)parent, StaticDlgProc, (LPARAM)this);

		VDASSERT(spCurrentDialog == this);
		spCurrentDialog = prevDialog;

		if (hookInstalled) {
			VDVERIFY(UnhookWindowsHookEx(sDialogMsgHook));
			sDialogMsgHook = nullptr;
		}

		return rval;
	} else {
		VDVERIFY(CreateDialogIndirectParamW(VDGetLocalModuleHandleW32(), (LPCDLGTEMPLATEW)newTemplate, (HWND)parent, StaticDlgProc, (LPARAM)this));
		return 0;
	}
}

void VDDialogFrameW32::ResetControlCachesForDpiChange() {
	// clear cached button info
	for(ButtonInfo& bi : mButtons) {
		vdsaferelease <<= bi.mpStyle;
		bi.mIconWidth = 0;
		bi.mIconHeight = 0;
	}
}

void VDDialogFrameW32::ClearControlCaches() {
	while(!mButtons.empty()) {
		if (auto *p = mButtons.back().mpStyle)
			p->Release();

		mButtons.pop_back();
	}
}

VDZINT_PTR VDZCALLBACK VDDialogFrameW32::StaticDlgProc(VDZHWND hwnd, VDZUINT msg, VDZWPARAM wParam, VDZLPARAM lParam) {
	VDDialogFrameW32 *pThis = (VDDialogFrameW32 *)GetWindowLongPtr(hwnd, DWLP_USER);

	if (msg == WM_INITDIALOG) {
		SetWindowLongPtr(hwnd, DWLP_USER, lParam);
		pThis = (VDDialogFrameW32 *)lParam;

		pThis->mMutex.Lock();
		pThis->mhdlg = hwnd;
		pThis->mMutex.Unlock();
	} else if (msg == WM_NCDESTROY) {
		if (pThis) {
			pThis->mResizer.Shutdown();
			bool deleteMe = pThis->PreNCDestroy();

			if (pThis->mhfont) {
				DeleteObject(pThis->mhfont);
				pThis->mhfont = nullptr;
			}

			pThis->mMutex.Lock();
			pThis->mhdlg = NULL;
			pThis->mMutex.Unlock();

			SetWindowLongPtr(hwnd, DWLP_USER, (LONG_PTR)(void *)NULL);

			if (deleteMe)
				delete pThis;
			else
				pThis->PostNCDestroy();

			pThis = NULL;
			return FALSE;
		}
	}

	return pThis ? pThis->DlgProc(msg, wParam, lParam) : FALSE;
}

VDZINT_PTR VDDialogFrameW32::DlgProc(VDZUINT msg, VDZWPARAM wParam, VDZLPARAM lParam) {
	switch(msg) {
		case WM_INITDIALOG: {
			if (mbIsModal) {
				mbProgressParentHooked = true;
				mhPrevProgressParent = ATUIPushProgressParent((VDGUIHandle)mhdlg);
			}

			if (ShouldSetDialogIcon())
				SetDialogIcon();

			OnPreLoaded();

			bool useDefaultFocus = !OnLoaded();

			OnPostLoaded();

			return useDefaultFocus;
		}

		case WM_MOUSEMOVE:
			OnMouseMove((int)(SHORT)LOWORD(lParam), (int)(SHORT)HIWORD(lParam));
			break;

		case WM_LBUTTONDOWN:
			OnMouseDownL((int)(SHORT)LOWORD(lParam), (int)(SHORT)HIWORD(lParam));
			break;

		case WM_LBUTTONUP:
			OnMouseUpL((int)(SHORT)LOWORD(lParam), (int)(SHORT)HIWORD(lParam));
			break;

		case WM_RBUTTONDOWN:
			if (OnMouseDownR((int)(SHORT)LOWORD(lParam), (int)(SHORT)HIWORD(lParam)))
				return 0;

			break;

		case WM_RBUTTONUP:
			if (OnMouseUpR((int)(SHORT)LOWORD(lParam), (int)(SHORT)HIWORD(lParam)))
				return 0;
			break;

		case WM_MOUSELEAVE:
			OnMouseLeave();
			break;

		case WM_MOUSEHOVER:
			OnMouseHover();
			break;

		case WM_MOUSEWHEEL: {
			POINT pt { (int)(SHORT)LOWORD(lParam), (int)(SHORT)HIWORD(lParam) };
			ScreenToClient(mhdlg, &pt);
			OnMouseWheel(pt.x, pt.y, (sint32)(SHORT)HIWORD(wParam));
			break;
		}

		case WM_SETCURSOR:
			if (LOWORD(lParam) == HTCLIENT) {
				ATUICursorImage image {};

				const DWORD pos = ::GetMessagePos();
				POINT pt { (SHORT)LOWORD(pos), (SHORT)HIWORD(pos) };

				ScreenToClient(mhwnd, &pt);

				if (OnSetCursor(pt.x, pt.y, image)) {
					SetCurrentCursor(image);

					SetWindowLongPtr(mhdlg, DWLP_MSGRESULT, TRUE);
					return TRUE;
				}
			}
			break;

		case WM_CAPTURECHANGED:
			OnCaptureLost();
			break;

		case WM_SETFOCUS:
			OnSetFocus();
			break;

		case WM_COMMAND:
			{
				uint32 id = LOWORD(wParam);

				if (id == IDOK) {
					// needed to work around ListView label editing stupidity
					if (HIWORD(wParam) == BN_CLICKED) {
						if (!OnOK())
							End(true);

						return TRUE;
					}
				} else if (id == IDCANCEL) {
					// needed to work around ListView label editing stupidity
					if (HIWORD(wParam) == BN_CLICKED) {
						if (!OnCancel())
							End(false);

						return TRUE;
					}
				} else {
					try {
						if (OnCommand(id, HIWORD(wParam)))
							return TRUE;
					} catch(const MyError& e) {
						ShowError(e);
						return TRUE;
					}
				}
			}

			SetWindowLongPtr(mhdlg, DWLP_MSGRESULT, mMsgDispatcher.Dispatch_WM_COMMAND(wParam, lParam));
			return TRUE;

		case WM_NOTIFY:
			if (NMHDR& hdr = *(NMHDR *)lParam; ATUIIsDarkThemeActive() && hdr.code == NM_CUSTOMDRAW) {
				ButtonInfo bi = {(uint32)hdr.idFrom};
				auto it = std::lower_bound(mButtons.begin(), mButtons.end(), bi);

				if (it != mButtons.end() && *it == bi) {
					SetWindowLongPtr(mhdlg, DWLP_MSGRESULT, OnButtonCustomDraw(lParam, *it));
					return TRUE;
				}
			}

			SetWindowLongPtr(mhdlg, DWLP_MSGRESULT, mMsgDispatcher.Dispatch_WM_NOTIFY(wParam, lParam));
			return TRUE;

		case WM_CLOSE:
			if (OnClose())
				return TRUE;
			break;

		case WM_ENABLE:
			OnEnable(wParam != 0);
			return TRUE;

		case WM_DESTROY:
			OnPreDestroy();
			OnDestroy();

			// ensure this happens even if OnDestroy() doesn't propagate -- though
			// some subclasses may intentionally call it early
			mMsgDispatcher.RemoveAllControls(true);

			if (mbProgressParentHooked) {
				mbProgressParentHooked = false;
				ATUIPopProgressParent((VDGUIHandle)mhdlg, mhPrevProgressParent);
			}
			break;

		case WM_SIZE:
			OnSize();
			return FALSE;

		case WM_TIMER:
			return OnTimer((uint32)wParam);

		case WM_DROPFILES:
			OnDropFiles((VDZHDROP)wParam);
			return TRUE;

		case WM_HSCROLL:
			if (LRESULT r; mMsgDispatcher.TryDispatch_WM_HSCROLL(wParam, lParam, r)) {
				SetWindowLongPtr(mhdlg, DWLP_MSGRESULT, r);
				return TRUE;
			}

			OnHScroll(lParam ? GetWindowLong((HWND)lParam, GWL_ID) : 0, LOWORD(wParam));
			return TRUE;

		case WM_VSCROLL:
			if (LRESULT r; mMsgDispatcher.TryDispatch_WM_VSCROLL(wParam, lParam, r)) {
				SetWindowLongPtr(mhdlg, DWLP_MSGRESULT, r);
				return TRUE;
			}

			OnVScroll(lParam ? GetWindowLong((HWND)lParam, GWL_ID) : 0, LOWORD(wParam));
			return TRUE;

		case WM_ERASEBKGND:
			if (!OnShouldErase()) {
				SetWindowLongPtr(mhdlg, DWLP_MSGRESULT, FALSE);
				return TRUE;
			} else {
				HDC hdc = (HDC)wParam;

				if (!OnErase(hdc))
					mResizer.Erase(&hdc, GetBackgroundColor());

				SetWindowLongPtr(mhdlg, DWLP_MSGRESULT, TRUE);
			}

			return TRUE;

		case WM_PAINT:
			if (OnPaint())
				return TRUE;

			break;

		case WM_GETMINMAXINFO:
			{
				MINMAXINFO& mmi = *(MINMAXINFO *)lParam;

				if (mmi.ptMinTrackSize.x < mMinWidth)
					mmi.ptMinTrackSize.x = mMinWidth;

				if (mmi.ptMinTrackSize.y < mMinHeight)
					mmi.ptMinTrackSize.y = mMinHeight;

				if (mmi.ptMaxTrackSize.x > mMaxWidth)
					mmi.ptMaxTrackSize.x = mMaxWidth;

				if (mmi.ptMaxTrackSize.y > mMaxHeight)
					mmi.ptMaxTrackSize.y = mMaxHeight;
			}
			return TRUE;

		case WM_HELP:
			OnHelp();
			return TRUE;

		case WM_INITMENU:
			OnInitMenu((HMENU)wParam);
			break;

		case WM_CONTEXTMENU:
			if (mMsgDispatcher.TryDispatch_WM_CONTEXTMENU(wParam, lParam)) {
				return 0;
			} else {
				uint32 id = 0;

				if (wParam)
					id = GetWindowLong((HWND)wParam, GWL_ID);

				int x = (short)LOWORD(lParam);
				int y = (short)HIWORD(lParam);

				OnContextMenu(id, x, y);
			}
			return TRUE;

		case WM_GETFONT:
			SetWindowLongPtr(mhdlg, DWLP_MSGRESULT, (LONG_PTR)mhfont);
			return TRUE;

		case WM_SETFONT:
			{
				uint32 dpi = ATUIGetWindowDpiW32(mhdlg);

				if (mCurrentDpi != dpi) {
					mCurrentDpi = dpi;

					HFONT hfont = CreateNewFont();
					SetFont(hfont);

					const int refX = mDialogUnits.mWidth4;
					const int refY = mDialogUnits.mHeight8;
					mResizer.Relayout(&refX, &refY);

					ResetControlCachesForDpiChange();
					OnDpiChanged();
				}
			}
			return TRUE;

		case WM_DPICHANGED:
			{
				const RECT *r = (const RECT *)lParam;

				vdrect32 r2(r->left, r->top, r->right, r->bottom);
				OnDpiChanging((uint16)LOWORD(wParam), (uint16)HIWORD(wParam), &r2);
			}
			return TRUE;

		case ATWM_INHERIT_DPICHANGED:
			OnDpiChanging((uint16)LOWORD(wParam), (uint16)HIWORD(wParam), nullptr);
			return TRUE;

		case WM_GETDPISCALEDSIZE: 
			{
				const int newDpi = (int)wParam;

				if (HFONT hNewFont = CreateNewFont(newDpi)) {
					const DialogUnits units = ComputeDialogUnits(hNewFont);
					DeleteObject(hNewFont);

					const vdsize32 templatePixelSize = ComputeTemplatePixelSize(units, newDpi);

					SIZE& sz = *(SIZE *)lParam;

					int width = (sz.cx * newDpi + (mCurrentDpi >> 1)) / mCurrentDpi;
					int height = (sz.cy * newDpi + (mCurrentDpi >> 1)) / mCurrentDpi;

					AdjustSize(width, height, templatePixelSize, units);

					VDDEBUG("DPI change %d -> %d: %dx%d -> %dx%d (DLU %dx%d, template size %dx%d)\n", mCurrentDpi, newDpi, sz.cx, sz.cy, width, height, units.mWidth4, units.mHeight8, templatePixelSize.w, templatePixelSize.h);

					sz.cx = width;
					sz.cy = height;

					SetWindowLongPtr(mhdlg, DWLP_MSGRESULT, TRUE);
					return TRUE;
				}
			}
			break;

		case VDWM_APP_POSTEDCALL:
			ExecutePostedCalls();
			return TRUE;

		case ATWM_PRETRANSLATE:
			if (OnPreTranslate(*(MSG *)lParam)) {
				SetWindowLongPtr(mhdlg, DWLP_MSGRESULT, TRUE);
				return TRUE;
			}

			if (mAccel && TranslateAccelerator(mhdlg, mAccel, (MSG *)lParam)) {
				SetWindowLongPtr(mhdlg, DWLP_MSGRESULT, TRUE);
				return TRUE;
			}
			break;

		case WM_CTLCOLORSTATIC:
			if (HBRUSH hbrush = mMsgDispatcher.TryDispatch_WM_CTLCOLORSTATIC(wParam, lParam))
				return (INT_PTR)hbrush;

			[[fallthrough]];
		case WM_CTLCOLORBTN:
			if (ATUIIsDarkThemeActive()) {
				const auto& tcw32 = ATUIGetThemeColorsW32();
				HDC hdc = (HDC)wParam;

				if (!IsWindowEnabled((HWND)lParam)) {
					SetTextColor(hdc, tcw32.mDisabledFgCRef);
				} else {
					SetTextColor(hdc, tcw32.mStaticFgCRef);
				}

				SetBkColor(hdc, tcw32.mStaticBgCRef);

				return (INT_PTR)tcw32.mStaticBgBrush;
			}
			break;

		case WM_CTLCOLOREDIT:
		case WM_CTLCOLORLISTBOX:
			if (ATUIIsDarkThemeActive()) {
				const auto& tcw32 = ATUIGetThemeColorsW32();
				HDC hdc = (HDC)wParam;

				SetTextColor(hdc, tcw32.mContentFgCRef);
				SetBkColor(hdc, tcw32.mContentBgCRef);

				return (INT_PTR)tcw32.mContentBgBrush;
			}
			break;
	}

	return FALSE;
}

///////////////////////////////////////////////////////////////////////////////

VDDialogResizerW32::VDDialogResizerW32()
	: mhwndBase(nullptr)
{
}

VDDialogResizerW32::~VDDialogResizerW32() {
}

void VDDialogResizerW32::Init(VDZHWND hwnd) {
	VDASSERT(!mhwndBase);

	mhwndBase = hwnd;
	mWidth = 1;
	mHeight = 1;
	mRefX = 16;
	mRefY = 16;

	RECT r;
	if (GetClientRect(hwnd, &r)) {
		mWidth = r.right;
		mHeight = r.bottom;
	}

	mControls.clear();
}

void VDDialogResizerW32::Shutdown() {
	mhwndBase = nullptr;
	mControls.clear();
}

void VDDialogResizerW32::SetRefUnits(int refX, int refY) {
	mRefX = refX;
	mRefY = refY;
}

void VDDialogResizerW32::Relayout(const int *newRefX, const int *newRefY) {
	if (!mhwndBase)
		return;

	RECT r;

	if (GetClientRect(mhwndBase, &r))
		Relayout(r.right, r.bottom, newRefX, newRefY);
}

void VDDialogResizerW32::Relayout(int width, int height, const int *newRefX, const int *newRefY) {
	if (!mhwndBase)
		return;

	HDWP hdwp = BeginDeferWindowPos((int)mControls.size());

	mWidth = width;
	mHeight = height;

	bool refChanged = false;

	if (newRefX && (mRefX != *newRefX || mRefY != *newRefY)) {
		mRefX = *newRefX;
		mRefY = *newRefY;
		refChanged = true;
	}

	const Anchors anchors = ComputeAnchors();

	for(const ControlEntry& ent : mControls) {
		if (ent.mAlignment & kUpDownAutoBuddy)
			continue;

		uint32 flags;
		int x1;
		int y1;
		int w;
		int h;

		ComputeLayout(ent, anchors, x1, y1, w, h, flags, refChanged);

		if (hdwp) {
			HDWP hdwp2 = DeferWindowPos(hdwp, ent.mhwnd, NULL, x1, y1, w, h, flags);

			if (hdwp2) {
				hdwp = hdwp2;
				continue;
			}
		}

		SetWindowPos(ent.mhwnd, NULL, x1, y1, w, h, flags);
	}

	if (hdwp)
		EndDeferWindowPos(hdwp);

	for(const ControlEntry& ent : mControls) {
		if (ent.mAlignment & kUpDownAutoBuddy) {
			HWND hwndBuddy = (HWND)SendMessage(ent.mhwnd, UDM_GETBUDDY, 0, 0);
			
			SendMessage(ent.mhwnd, UDM_SETBUDDY, (WPARAM)hwndBuddy, 0);
		}
	}
}

void VDDialogResizerW32::Add(uint32 id, uint32 alignment) {
	HWND hwndControl = GetDlgItem(mhwndBase, id);
	if (!hwndControl)
		return;

	Add(hwndControl, alignment);
}

void VDDialogResizerW32::Add(VDZHWND hwndControl, uint32 alignment) {
	// Find if we already have this control (linear search!)
	ControlEntry *ce = nullptr;

	for(auto& e : mControls) {
		if (e.mhwnd == hwndControl) {
			ce = &e;
			break;
		}
	}

	if (!ce) {
		ce = &mControls.push_back();
		ce->mhwnd		= hwndControl;
	}

	RECT r;
	if (!GetWindowRect(hwndControl, &r))
		return;

	SetLastError(0);
	if (!MapWindowPoints(NULL, mhwndBase, (LPPOINT)&r, 2) && GetLastError())
		return;

	ce->mX1			= r.left   - ((mWidth  * ((alignment >> 0) & 3)) >> 1);
	ce->mY1			= r.top    - ((mHeight * ((alignment >> 4) & 3)) >> 1);
	ce->mX2			= r.right  - ((mWidth  * ((alignment >> 2) & 3)) >> 1);
	ce->mY2			= r.bottom - ((mHeight * ((alignment >> 6) & 3)) >> 1);
	ce->mRefX		= mRefX;
	ce->mRefY		= mRefY;
	ce->mAlignment	= alignment;
}

void VDDialogResizerW32::Add(VDZHWND hwndControl, sint32 x, sint32 y, sint32 w, sint32 h, uint32 alignment) {
	ControlEntry *ce = &mControls.push_back();
	ce->mX1			= x     - ((mWidth  * ((alignment >> 0) & 3)) >> 1);
	ce->mY1			= y     - ((mHeight * ((alignment >> 4) & 3)) >> 1);
	ce->mX2			= (x+w) - ((mWidth  * ((alignment >> 2) & 3)) >> 1);
	ce->mY2			= (y+h) - ((mHeight * ((alignment >> 6) & 3)) >> 1);
	ce->mRefX		= mRefX;
	ce->mRefY		= mRefY;
	ce->mhwnd		= hwndControl;
	ce->mAlignment	= alignment;
}

void VDDialogResizerW32::AddWithOffsets(VDZHWND hwnd, sint32 x1, sint32 y1, sint32 x2, sint32 y2, uint32 alignment, bool dlus, bool repositionNow) {
	ControlEntry *ce = &mControls.push_back();
	ce->mX1			= x1;
	ce->mY1			= y1;
	ce->mX2			= x2;
	ce->mY2			= y2;
	ce->mRefX		= dlus ? 4 : mRefX;
	ce->mRefY		= dlus ? 8 : mRefY;
	ce->mhwnd		= hwnd;
	ce->mAlignment	= alignment;

	if (repositionNow) {
		int x1;
		int y1;
		int w;
		int h;
		uint32 flags;
		ComputeLayout(*ce, ComputeAnchors(), x1, y1, w, h, flags, true);

		SetWindowPos(ce->mhwnd, nullptr, x1, y1, w, h, flags);
	}
}

void VDDialogResizerW32::SetOffsets(VDZHWND hwnd, sint32 x1, sint32 y1, sint32 x2, sint32 y2, uint32 alignment, bool dlus) {
	auto it = std::find_if(mControls.begin(), mControls.end(),
		[=](const ControlEntry& ce) { return ce.mhwnd == hwnd; });

	if (it == mControls.end())
		return;

	ControlEntry *ce = &*it;

	ce->mX1			= x1;
	ce->mY1			= y1;
	ce->mX2			= x2;
	ce->mY2			= y2;
	ce->mRefX		= dlus ? 4 : mRefX;
	ce->mRefY		= dlus ? 8 : mRefY;
	ce->mhwnd		= hwnd;
	ce->mAlignment	= alignment;

}

void VDDialogResizerW32::AddAlias(VDZHWND hwndTarget, VDZHWND hwndSource, uint32 mergeFlags) {
	if (!hwndTarget || !hwndSource)
		return;

	ControlEntry *ce = nullptr;
	for(auto& e : mControls) {
		if (e.mhwnd == hwndTarget) {
			ce = &e;
			break;
		}
	}

	for(const auto& e : mControls) {
		if (e.mhwnd == hwndSource) {
			if (!ce) {
				ce = &mControls.push_back();
				ce->mhwnd = hwndTarget;
			}

			ce->mAlignment = e.mAlignment | mergeFlags;
			ce->mX1 = e.mX1 + ((mWidth  * ((e.mAlignment >> 0) & 3)) >> 1) - ((mWidth  * ((ce->mAlignment >> 0) & 3)) >> 1);
			ce->mY1 = e.mY1 + ((mHeight * ((e.mAlignment >> 4) & 3)) >> 1) - ((mHeight * ((ce->mAlignment >> 4) & 3)) >> 1);
			ce->mX2 = e.mX2 + ((mWidth  * ((e.mAlignment >> 2) & 3)) >> 1) - ((mWidth  * ((ce->mAlignment >> 2) & 3)) >> 1);
			ce->mY2 = e.mY2 + ((mHeight * ((e.mAlignment >> 6) & 3)) >> 1) - ((mHeight * ((ce->mAlignment >> 6) & 3)) >> 1);
			ce->mRefX = e.mRefX;
			ce->mRefY = e.mRefY;

			int x1;
			int y1;
			int w;
			int h;
			uint32 flags;
			ComputeLayout(*ce, ComputeAnchors(), x1, y1, w, h, flags, true);

			SetWindowPos(hwndTarget, NULL, x1, y1, w, h, flags);
			break;
		}
	}

}

void VDDialogResizerW32::Remove(VDZHWND hwndControl) {
	auto it = std::find_if(mControls.begin(), mControls.end(),
		[=](const ControlEntry& ce) { return ce.mhwnd == hwndControl; });

	if (it != mControls.end())
		mControls.erase(it);
}

void VDDialogResizerW32::Broadcast(VDZUINT msg, VDZWPARAM wParam, VDZLPARAM lParam) {
	int excludeMask = 0;

	if (msg == WM_SETFONT)
		excludeMask = kSuppressFontChange;

	for(const auto& ce : mControls) {
		if (!(ce.mAlignment & excludeMask))
			SendMessageW(ce.mhwnd, msg, wParam, lParam);
	}
}

void VDDialogResizerW32::Erase(const VDZHDC *phdc, sint32 backgroundColorOverride) {
	HDC hdc = phdc ? *phdc : GetDC(mhwndBase);
	if (hdc) {
		Controls::const_iterator it(mControls.begin()), itEnd(mControls.end());
		for(; it!=itEnd; ++it) {
			const ControlEntry& ce = *it;

			if (ce.mAlignment & kAvoidFlicker) {
				RECT rChild;

				if (GetWindowRect(ce.mhwnd, &rChild)) {
					MapWindowPoints(NULL, mhwndBase, (LPPOINT)&rChild, 2);
					ExcludeClipRect(hdc, rChild.left, rChild.top, rChild.right, rChild.bottom);
				}
			}
		}

		RECT rClient;
		if (GetClientRect(mhwndBase, &rClient)) {
			if (backgroundColorOverride >= 0) {
				SetDCBrushColor(hdc, VDSwizzleU32(backgroundColorOverride) >> 8);
				FillRect(hdc, &rClient, (HBRUSH)GetStockObject(DC_BRUSH));
			} else {
				FillRect(hdc, &rClient, ATUIGetThemeColorsW32().mStaticBgBrush);
			}
		}

		if (!phdc)
			ReleaseDC(mhwndBase, hdc);
	}
}

VDDialogResizerW32::Anchors VDDialogResizerW32::ComputeAnchors() const {
	return Anchors {
		{ 0, mWidth >> 1, mWidth, mWidth },
		{ 0, mHeight >> 1, mHeight, mHeight }
	};
}

void VDDialogResizerW32::ComputeLayout(const ControlEntry& ce, const Anchors& anchors, int& x1, int& y1, int& w, int& h, uint32& flags, bool forceMove) const {
	flags = SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOCOPYBITS;
	const uint8 alignment = ce.mAlignment;

	if (!forceMove) {
		if (!(alignment & kX1Y1Mask))
			flags |= SWP_NOMOVE;

		if ((alignment & kX1Y1Mask) == (alignment & kX2Y2Mask))
			flags |= SWP_NOSIZE;
	}

	x1 = ce.mX1;
	y1 = ce.mY1;
	int x2 = ce.mX2;
	int y2 = ce.mY2;

	// check if we need to handle a DPI rescale
	if (ce.mRefX != mRefX || ce.mRefY != mRefY) {
		x1 = MulDiv(x1, mRefX, ce.mRefX);
		y1 = MulDiv(y1, mRefY, ce.mRefY);
		x2 = MulDiv(x2, mRefX, ce.mRefX);
		y2 = MulDiv(y2, mRefY, ce.mRefY);
	}

	x1 += anchors.mXAnchors[(alignment >> 0) & 3];
	x2 += anchors.mXAnchors[(alignment >> 2) & 3];
	y1 += anchors.mYAnchors[(alignment >> 4) & 3];
	y2 += anchors.mYAnchors[(alignment >> 6) & 3];

	w = x2 - x1;
	h = y2 - y1;

	if (w < 0)
		w = 0;

	if (h < 0)
		h = 0;
}

///////////////////////////////////////////////////////////////////////////

VDResizableDialogFrameW32::VDResizableDialogFrameW32(uint32 id)
	: VDDialogFrameW32(id)
{
}

void VDResizableDialogFrameW32::OnPreLoaded() {
	VDDialogFrameW32::OnPreLoaded();

	SetCurrentSizeAsMinSize();
}
