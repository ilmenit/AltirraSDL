//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2024 Avery Lee
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
#define INITGUID
#include <windows.h>
#include <windowsx.h>
#include <richedit.h>
#include <shldisp.h>
#include <shlguid.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <commctrl.h>
#include <tom.h>
#include <uxtheme.h>
#include <ranges>

#include <vd2/system/color.h>
#include <vd2/system/w32assist.h>
#include <vd2/system/strutil.h>
#include <vd2/system/thunk.h>
#include <vd2/system/vdalloc.h>
#include <vd2/system/vdstl.h>
#include <vd2/Dita/accel.h>
#include <vd2/Kasumi/pixmap.h>
#include <vd2/Kasumi/pixmaputils.h>
#include <vd2/Kasumi/resample.h>
#include <vd2/Riza/bitmap.h>
#include <at/atcore/comsupport_win32.h>
#include <at/atnativeui/theme.h>
#include <at/atnativeui/theme_win32.h>
#include <at/atnativeui/uiframe.h>
#include <at/atnativeui/uiproxies.h>

#pragma comment(lib, "oleaut32")

#ifdef VD_COMPILER_GCC
	__CRT_UUID_DECL(ITextDocument, 0x8CC497C0, 0xA1DF, 0x11ce, 0x80, 0x98, 0x00, 0xAA, 0x00, 0x47, 0xBE, 0x5D)
#endif

namespace {
	union AlphaBitmapHeader {
		BITMAPV5HEADER v5;
		VDAVIBitmapInfoHeader bi;

		AlphaBitmapHeader(uint32 w, uint32 h);
	};

	AlphaBitmapHeader::AlphaBitmapHeader(uint32 w, uint32 h) {
		v5 = {};
		v5.bV5Size = sizeof(BITMAPV5HEADER);
		v5.bV5Width = w;
		v5.bV5Height = h;
		v5.bV5Planes = 1;
		v5.bV5BitCount = 32;
		v5.bV5Compression = BI_BITFIELDS;
		v5.bV5SizeImage = w*h*4;
		v5.bV5XPelsPerMeter = 0;
		v5.bV5YPelsPerMeter = 0;
		v5.bV5ClrUsed = 0;
		v5.bV5ClrImportant = 0;
		v5.bV5RedMask = 0x00FF0000;
		v5.bV5GreenMask = 0x0000FF00;
		v5.bV5BlueMask = 0x000000FF;
		v5.bV5AlphaMask = 0xFF000000;
		v5.bV5CSType = LCS_WINDOWS_COLOR_SPACE;
		v5.bV5Intent = LCS_GM_BUSINESS;
		v5.bV5ProfileSize = 0;
	}

	void AddImagesToImageList(HIMAGELIST hImageList, const VDPixmap& px, uint32 imgw, uint32 imgh, uint32 backgroundColor) {
		if (px.format != nsVDPixmap::kPixFormat_XRGB8888) {
			VDFAIL("Image list not in correct format.");
			return;
		}

		if (!hImageList)
			return;

		HDC hdc2 = nullptr;

		if (HDC hdc = GetDC(nullptr)) {
			hdc2 = CreateCompatibleDC(hdc);
			ReleaseDC(nullptr, hdc);
		}

		if (hdc2) {
			AlphaBitmapHeader hdr(imgw, imgh);

			void *bits;
			HBITMAP hbm = CreateDIBSection(hdc2, (BITMAPINFO *)&hdr, DIB_RGB_COLORS, &bits, nullptr, 0);
			if (hbm) {
				VDPixmap pxbuf = VDGetPixmapForBitmap(hdr.bi, bits);
				const uint32 srcw = px.w;
				const uint32 srch = px.h;
				VDPixmapBuffer alphasrc(srcw, srch, nsVDPixmap::kPixFormat_XRGB8888);
				VDPixmapBuffer alphadst(imgw, imgh, nsVDPixmap::kPixFormat_XRGB8888);

				GdiFlush();

				vdautoptr<IVDPixmapResampler> r(VDCreatePixmapResampler());

				r->SetFilters(IVDPixmapResampler::kFilterCubic, IVDPixmapResampler::kFilterCubic, false);
				r->SetSplineFactor(-0.65f);
				r->SetLinear(false);
				bool rsinited = r->Init(imgw, imgh, nsVDPixmap::kPixFormat_XRGB8888, srcw, srch, nsVDPixmap::kPixFormat_XRGB8888);

				VDMemcpyRect(alphasrc.data, alphasrc.pitch, px.data, px.pitch, srcw*4, srch);

				for(int y = 0; y < alphasrc.h; ++y) {
					uint8 *row = alphasrc.GetPixelRow<uint8>(y);

					for(int x = 0; x < alphasrc.w; ++x) {
						row[0] = (uint8)(((row[0] * row[3] + 128) * 257) >> 16);
						row[1] = (uint8)(((row[1] * row[3] + 128) * 257) >> 16);
						row[2] = (uint8)(((row[2] * row[3] + 128) * 257) >> 16);
						row += 4;
					}
				}

				if (rsinited)
					r->Process(pxbuf, alphasrc);

				// We can't guarantee that the resampler will handle alpha, so resample alpha as color
				// and remerge.

				for(int y = 0; y < alphasrc.h; ++y) {
					uint32 *row = alphasrc.GetPixelRow<uint32>(y);

					for(int x = 0; x < alphasrc.w; ++x) {
						row[x] >>= 24;
					}
				}

				if (rsinited)
					r->Process(alphadst, alphasrc);

				VDColorARGB bk = VDColorARGB::FromARGB8(backgroundColor).Premultiply();

				uint32 allAlpha = 0;
				for(uint32 y=0; y<imgh; ++y) {
					const uint32 *VDRESTRICT src = alphadst.GetPixelRow<const uint32>(y);
					uint32 *VDRESTRICT dst = pxbuf.GetPixelRow<uint32>(y);

					for(uint32 x=0; x<imgw; ++x) {
						VDColorARGB c = VDColorARGB::FromARGB8((dst[x] & 0xffffff) + (src[x] << 24));
						allAlpha |= src[x];

						dst[x] = bk.PremulBlend(c).DivideAlpha().ToARGB8();
					}
				}

				// Check if the image is fully transparent. If so, we need to hack around the image list's
				// attempt to detect alpha -- it checks the alpha bytes of all pixels and ignores the alpha
				// completely if it's all zero. This turns a fully transparent icon into a solid square,
				// which isn't what we want.
				if (!allAlpha)
					*(uint32 *)pxbuf.data |= 0x01000000;
					
				ImageList_Add(hImageList, hbm, nullptr);

				DeleteObject(hbm);
			}

			DeleteDC(hdc2);
		}
	}
}

///////////////////////////////////////////////////////////////////////////////

VDUIImageListW32::VDUIImageListW32() {
}

VDUIImageListW32::~VDUIImageListW32() {
	ReleasePrevImageList();

	if (mhImageList) {
		ImageList_Destroy(mhImageList);
		mhImageList = nullptr;
	}
}

bool VDUIImageListW32::IsEmpty() const {
	return mImages.empty();
}

VDZHIMAGELIST VDUIImageListW32::SetImageSize(int size, uint32 bgColor) {
	if (mImageSize == size || mBgColor == bgColor)
		return nullptr;

	mImageSize = size;
	mBgColor = bgColor;

	if (mImages.empty())
		return nullptr;

	HIMAGELIST hNewImageList = ImageList_Create(mImageSize, mImageSize, ILC_COLOR32, 0, 8);

	if (!hNewImageList)
		return nullptr;

	for(const VDPixmapBuffer& pxbuf : mImages)
		AddImagesToImageList(hNewImageList, pxbuf, mImageSize, mImageSize, mBgColor);

	if (mhImageListPrev)
		ImageList_Destroy(mhImageListPrev);

	mhImageListPrev = mhImageList;
	mhImageList = hNewImageList;

	return hNewImageList;
}

void VDUIImageListW32::ReleasePrevImageList() {
	if (mhImageListPrev) {
		ImageList_Destroy(mhImageListPrev);
		mhImageListPrev = nullptr;
	}
}

VDZHIMAGELIST VDUIImageListW32::AddEmptyImage() {
	uint32 c = 0x00FFFFFF;
	VDPixmap pxEmpty {};
	pxEmpty.format = nsVDPixmap::kPixFormat_XRGB8888;
	pxEmpty.data = &c;
	pxEmpty.w = 1;
	pxEmpty.h = 1;

	return AddImages(pxEmpty);
}

VDZHIMAGELIST VDUIImageListW32::AddImages(const VDPixmap& px) {
	if (px.h <= 0)
		return nullptr;

	int numImages = px.w / px.h;
	if (numImages <= 0)
		return nullptr;

	HIMAGELIST hNewImageList = nullptr;

	if (!mhImageList && mImageSize) {
		mhImageList = ImageList_Create(mImageSize, mImageSize, ILC_COLOR32, 0, 8);
		hNewImageList = mhImageList;
	}

	for(int i=0; i<numImages; ++i) {
		const VDPixmap& pxImage = VDPixmapClip(px, px.h * i, 0, px.h, px.h);

		mImages.emplace_back(pxImage);
		AddImagesToImageList(mhImageList, pxImage, mImageSize, mImageSize, mBgColor);
	}

	return hNewImageList;
}

///////////////////////////////////////////////////////////////////////////////

VDUIProxyControl::VDUIProxyControl()
	: mRedrawInhibitCount(0)
{
}

void VDUIProxyControl::Attach(VDZHWND hwnd) {
	VDASSERT(IsWindow(hwnd));
	mhwnd = hwnd;
}

void VDUIProxyControl::Detach() {
	mhwnd = NULL;
}

void VDUIProxyControl::SetRedraw(bool redraw) {
	if (redraw) {
		if (!--mRedrawInhibitCount) {
			if (mhwnd) {
				OnRedrawResume();
				SendMessage(mhwnd, WM_SETREDRAW, TRUE, 0);
			}
		}
	} else {
		if (!mRedrawInhibitCount++) {
			if (mhwnd) {
				OnRedrawSuspend();
				SendMessage(mhwnd, WM_SETREDRAW, FALSE, 0);
			}
		}
	}
}

VDZLRESULT VDUIProxyControl::On_WM_NOTIFY(VDZWPARAM wParam, VDZLPARAM lParam) {
	return 0;
}

VDZLRESULT VDUIProxyControl::On_WM_COMMAND(VDZWPARAM wParam, VDZLPARAM lParam) {
	return 0;
}

VDZLRESULT VDUIProxyControl::On_WM_HSCROLL(VDZWPARAM wParam, VDZLPARAM lParam) {
	return 0;
}

VDZLRESULT VDUIProxyControl::On_WM_VSCROLL(VDZWPARAM wParam, VDZLPARAM lParam) {
	return 0;
}

bool VDUIProxyControl::On_WM_CONTEXTMENU(VDZWPARAM wParam, VDZLPARAM lParam) {
	return false;
}

VDZHBRUSH VDUIProxyControl::On_WM_CTLCOLORSTATIC(VDZWPARAM wParam, VDZLPARAM lParam) {
	return nullptr;
}

void VDUIProxyControl::OnFontChanged() {
}

void VDUIProxyControl::OnRedrawSuspend() {
}

void VDUIProxyControl::OnRedrawResume() {
}

///////////////////////////////////////////////////////////////////////////////

void VDUIProxyMessageDispatcherW32::AddControl(VDUIProxyControl *control) {
	VDZHWND hwnd = control->GetHandle();
	size_t hc = Hash(hwnd);

	mHashTable[hc].push_back(control);
}

void VDUIProxyMessageDispatcherW32::RemoveControl(VDZHWND hwnd) {
	size_t hc = Hash(hwnd);
	HashChain& hchain = mHashTable[hc];

	HashChain::iterator it(hchain.begin()), itEnd(hchain.end());
	for(; it != itEnd; ++it) {
		VDUIProxyControl *control = *it;

		if (control->GetHandle() == hwnd) {
			hchain.erase(control);
			break;
		}
	}

}

void VDUIProxyMessageDispatcherW32::RemoveAllControls(bool detach) {
	for(int i=0; i<kHashTableSize; ++i) {
		HashChain& hchain = mHashTable[i];

		if (detach) {
			HashChain::iterator it(hchain.begin()), itEnd(hchain.end());
			for(; it != itEnd; ++it) {
				VDUIProxyControl *control = *it;

				control->Detach();
			}
		}

		hchain.clear();
	}
}

bool VDUIProxyMessageDispatcherW32::TryDispatch(VDZUINT msg, VDZWPARAM wParam, VDZLPARAM lParam, VDZLRESULT& result) {
	if (msg == WM_COMMAND)
		return TryDispatch_WM_COMMAND(wParam, lParam, result);

	if (msg == WM_NOTIFY)
		return TryDispatch_WM_NOTIFY(wParam, lParam, result);

	if (msg == WM_HSCROLL)
		return TryDispatch_WM_HSCROLL(wParam, lParam, result);

	if (msg == WM_VSCROLL)
		return TryDispatch_WM_VSCROLL(wParam, lParam, result);

	if (msg == WM_CONTEXTMENU) {
		if (TryDispatch_WM_CONTEXTMENU(wParam, lParam)) {
			result = 0;
			return true;
		}

		return false;
	}

	if (msg == WM_CTLCOLORSTATIC) {
		if (HBRUSH hbr = TryDispatch_WM_CTLCOLORSTATIC(wParam, lParam)) {
			result = (VDZLRESULT)hbr;
			return true;
		}
	}

	if (msg == WM_DESTROY)
		RemoveAllControls(true);

	return false;
}

bool VDUIProxyMessageDispatcherW32::TryDispatch_WM_COMMAND(VDZWPARAM wParam, VDZLPARAM lParam, VDZLRESULT& result) {
	VDUIProxyControl *control = GetControl((HWND)lParam);

	if (control) {
		result = control->On_WM_COMMAND(wParam, lParam);
		return true;
	}

	return false;
}

bool VDUIProxyMessageDispatcherW32::TryDispatch_WM_NOTIFY(VDZWPARAM wParam, VDZLPARAM lParam, VDZLRESULT& result) {
	const NMHDR *hdr = (const NMHDR *)lParam;
	VDUIProxyControl *control = GetControl(hdr->hwndFrom);

	if (control) {
		result = control->On_WM_NOTIFY(wParam, lParam);
		return true;
	}

	return false;
}

bool VDUIProxyMessageDispatcherW32::TryDispatch_WM_HSCROLL(VDZWPARAM wParam, VDZLPARAM lParam, VDZLRESULT& result) {
	if (!lParam)
		return false;

	VDUIProxyControl *control = GetControl((HWND)lParam);
	if (!control)
		return false;

	result = control->On_WM_HSCROLL(wParam, lParam);
	return true;
}

bool VDUIProxyMessageDispatcherW32::TryDispatch_WM_VSCROLL(VDZWPARAM wParam, VDZLPARAM lParam, VDZLRESULT& result) {
	if (!lParam)
		return false;

	VDUIProxyControl *control = GetControl((HWND)lParam);
	if (!control)
		return false;

	result = control->On_WM_VSCROLL(wParam, lParam);
	return true;
}

bool VDUIProxyMessageDispatcherW32::TryDispatch_WM_CONTEXTMENU(VDZWPARAM wParam, VDZLPARAM lParam) {
	if (!wParam)
		return false;

	VDUIProxyControl *control = GetControl((HWND)wParam);
	if (!control)
		return false;

	return control->On_WM_CONTEXTMENU(wParam, lParam);
}

VDZLRESULT VDUIProxyMessageDispatcherW32::Dispatch_WM_COMMAND(VDZWPARAM wParam, VDZLPARAM lParam) {
	VDUIProxyControl *control = GetControl((HWND)lParam);

	if (control)
		return control->On_WM_COMMAND(wParam, lParam);

	return 0;
}

VDZLRESULT VDUIProxyMessageDispatcherW32::Dispatch_WM_NOTIFY(VDZWPARAM wParam, VDZLPARAM lParam) {
	const NMHDR *hdr = (const NMHDR *)lParam;
	VDUIProxyControl *control = GetControl(hdr->hwndFrom);

	if (control)
		return control->On_WM_NOTIFY(wParam, lParam);

	return 0;
}

VDZHBRUSH VDUIProxyMessageDispatcherW32::TryDispatch_WM_CTLCOLORSTATIC(VDZWPARAM wParam, VDZLPARAM lParam) {
	VDUIProxyControl *control = GetControl((HWND)lParam);

	if (control)
		return control->On_WM_CTLCOLORSTATIC(wParam, lParam);

	return nullptr;
}

void VDUIProxyMessageDispatcherW32::DispatchFontChanged() {
	for(HashChain& hc : mHashTable) {
		for(VDUIProxyControl *control : hc) {
			control->OnFontChanged();
		}
	}
}

size_t VDUIProxyMessageDispatcherW32::Hash(VDZHWND hwnd) const {
	return (size_t)hwnd % (size_t)kHashTableSize;
}

VDUIProxyControl *VDUIProxyMessageDispatcherW32::GetControl(VDZHWND hwnd) {
	size_t hc = Hash(hwnd);
	HashChain& hchain = mHashTable[hc];

	HashChain::iterator it(hchain.begin()), itEnd(hchain.end());
	for(; it != itEnd; ++it) {
		VDUIProxyControl *control = *it;

		if (control->GetHandle() == hwnd)
			return control;
	}

	return NULL;
}

///////////////////////////////////////////////////////////////////////////////

struct VDUIProxyListView::Private {
	static LRESULT CALLBACK StaticListViewSubclass(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR subclassId, DWORD_PTR thisPtr) {
		return ((VDUIProxyListView *)thisPtr)->ListViewSubclassProc(hwnd, msg, wParam, lParam);
	}
};

VDUIProxyListView::VDUIProxyListView() {
}

void VDUIProxyListView::Attach(VDZHWND hwnd) {
	VDUIProxyControl::Attach(hwnd);

	// make sure the list view is set to share image lists
	DWORD dwStyle = GetWindowLong(hwnd, GWL_STYLE);

	if (!(dwStyle & LVS_SHAREIMAGELISTS))
		SetWindowLong(hwnd, GWL_STYLE, dwStyle | LVS_SHAREIMAGELISTS);

	bool needHook = false;
	if (ATUIIsDarkThemeActive()) {
		const auto& tc = ATUIGetThemeColors();
		const COLORREF bg = VDSwizzleU32(tc.mContentBg) >> 8;
		const COLORREF fg = VDSwizzleU32(tc.mContentFg) >> 8;

		ListView_SetBkColor(mhwnd, bg);
		ListView_SetTextBkColor(mhwnd, bg);
		ListView_SetTextColor(mhwnd, fg);

		ListView_SetExtendedListViewStyleEx(mhwnd, LVS_EX_GRIDLINES, 0);

		mhwndHeader = ListView_GetHeader(hwnd);
		needHook = true;
	}

	if (mpOnEnterPressed)
		needHook = true;

	if (needHook) {
		SetWindowSubclass(
			mhwnd,
			Private::StaticListViewSubclass,
			1,
			(DWORD_PTR)this
		);
	}
}

void VDUIProxyListView::Detach() {
	if (mhwnd) {
		RemoveWindowSubclass(mhwnd, Private::StaticListViewSubclass, 1);
		ListView_SetImageList(mhwnd, nullptr, LVSIL_SMALL);
	}

	Clear();

	if (mBoldFont) {
		DeleteObject(mBoldFont);
		mBoldFont = nullptr;
	}

	VDUIProxyControl::Detach();
}

void VDUIProxyListView::SetIndexedProvider(IVDUIListViewIndexedProvider *p) {
	mbIndexedMode = true;
	mpIndexedProvider = p;
}

void VDUIProxyListView::AutoSizeColumns(bool expandlast) {
	const int colCount = GetColumnCount();

	int colCacheCount = (int)mColumnWidthCache.size();
	while(colCacheCount < colCount) {
		SendMessage(mhwnd, LVM_SETCOLUMNWIDTH, colCacheCount, LVSCW_AUTOSIZE_USEHEADER);
		mColumnWidthCache.push_back((int)SendMessage(mhwnd, LVM_GETCOLUMNWIDTH, colCacheCount, 0));
		++colCacheCount;
	}

	int totalWidth = 0;
	for(int col=0; col<colCount; ++col) {
		const int hdrWidth = mColumnWidthCache[col];

		SendMessage(mhwnd, LVM_SETCOLUMNWIDTH, col, LVSCW_AUTOSIZE);
		int dataWidth = (int)SendMessage(mhwnd, LVM_GETCOLUMNWIDTH, col, 0);

		if (dataWidth < hdrWidth)
			dataWidth = hdrWidth;

		if (expandlast && col == colCount-1) {
			RECT r;
			if (GetClientRect(mhwnd, &r)) {
				int extraWidth = r.right - totalWidth;

				if (dataWidth < extraWidth)
					dataWidth = extraWidth;
			}
		}

		SendMessage(mhwnd, LVM_SETCOLUMNWIDTH, col, dataWidth);

		totalWidth += dataWidth;
	}
}

void VDUIProxyListView::Clear() {
	if (mhwnd)
		SendMessage(mhwnd, LVM_DELETEALLITEMS, 0, 0);
}

void VDUIProxyListView::ClearAllColumns() {
	if (!mhwnd)
		return;

	uint32 n = GetColumnCount();
	for(uint32 i=n; i; --i)
		ListView_DeleteColumn(mhwnd, i - 1);

	mColumnWidthCache.clear();
}

void VDUIProxyListView::ClearExtraColumns() {
	if (!mhwnd)
		return;

	uint32 n = GetColumnCount();
	for(uint32 i=n; i > 1; --i)
		ListView_DeleteColumn(mhwnd, i - 1);

	if (!mColumnWidthCache.empty())
		mColumnWidthCache.resize(1);
}

void VDUIProxyListView::DeleteItem(int index) {
	if (index >= 0)
		SendMessage(mhwnd, LVM_DELETEITEM, index, 0);
}

int VDUIProxyListView::GetColumnCount() const {
	HWND hwndHeader = (HWND)SendMessage(mhwnd, LVM_GETHEADER, 0, 0);
	if (!hwndHeader)
		return 0;

	return (int)SendMessage(hwndHeader, HDM_GETITEMCOUNT, 0, 0);
}

int VDUIProxyListView::GetItemCount() const {
	return (int)SendMessage(mhwnd, LVM_GETITEMCOUNT, 0, 0);
}

int VDUIProxyListView::GetSelectedIndex() const {
	return ListView_GetNextItem(mhwnd, -1, LVNI_SELECTED);
}

void VDUIProxyListView::SetSelectedIndex(int index) {
	ListView_SetItemState(mhwnd, index, LVIS_SELECTED|LVIS_FOCUSED, LVIS_SELECTED|LVIS_FOCUSED);
}

uint32 VDUIProxyListView::GetSelectedItemId() const {
	int idx = GetSelectedIndex();

	if (idx < 0)
		return 0;

	return GetItemId(idx);
}

IVDUIListViewVirtualItem *VDUIProxyListView::GetSelectedItem() const {
	int idx = GetSelectedIndex();

	if (idx < 0)
		return NULL;

	return GetVirtualItem(idx);
}

void VDUIProxyListView::GetSelectedIndices(vdfastvector<int>& indices) const {
	int idx = -1;

	indices.clear();
	for(;;) {
		idx = ListView_GetNextItem(mhwnd, idx, LVNI_SELECTED);
		if (idx < 0)
			return;

		indices.push_back(idx);
	}
}

void VDUIProxyListView::SetFullRowSelectEnabled(bool enabled) {
	ListView_SetExtendedListViewStyleEx(mhwnd, LVS_EX_FULLROWSELECT, enabled ? LVS_EX_FULLROWSELECT : 0);
}

void VDUIProxyListView::SetGridLinesEnabled(bool enabled) {
	ListView_SetExtendedListViewStyleEx(mhwnd, LVS_EX_GRIDLINES, enabled && !ATUIIsDarkThemeActive() ? LVS_EX_GRIDLINES : 0);
}

bool VDUIProxyListView::AreItemCheckboxesEnabled() const {
	return (ListView_GetExtendedListViewStyle(mhwnd) & LVS_EX_CHECKBOXES) != 0;
}

void VDUIProxyListView::SetItemCheckboxesEnabled(bool enabled) {
	ListView_SetExtendedListViewStyleEx(mhwnd, LVS_EX_CHECKBOXES, enabled ? LVS_EX_CHECKBOXES : 0);
}

void VDUIProxyListView::SetActivateOnEnterEnabled(bool enabled) {
	mbActivateOnEnter = true;
}

void VDUIProxyListView::EnsureItemVisible(int index) {
	if (index >= 0)
		ListView_EnsureVisible(mhwnd, index, FALSE);
}

int VDUIProxyListView::GetVisibleTopIndex() {
	return ListView_GetTopIndex(mhwnd);
}

void VDUIProxyListView::SetVisibleTopIndex(int index) {
	int n = ListView_GetItemCount(mhwnd);
	if (n > 0) {
		ListView_EnsureVisible(mhwnd, n - 1, FALSE);
		ListView_EnsureVisible(mhwnd, index, FALSE);
	}
}

IVDUIListViewVirtualItem *VDUIProxyListView::GetSelectedVirtualItem() const {
	int index = GetSelectedIndex();
	if (index < 0)
		return NULL;

	return GetVirtualItem(index);
}

IVDUIListViewVirtualItem *VDUIProxyListView::GetVirtualItem(int index) const {
	if (index < 0)
		return NULL;

	LVITEMW itemw={};
	itemw.mask = LVIF_PARAM;
	itemw.iItem = index;
	itemw.iSubItem = 0;
	if (SendMessage(mhwnd, LVM_GETITEMW, 0, (LPARAM)&itemw))
		return (IVDUIListViewVirtualItem *)itemw.lParam;

	return NULL;
}

uint32 VDUIProxyListView::GetItemId(int index) const {
	if (index < 0)
		return 0;

	LVITEMW itemw={};
	itemw.mask = LVIF_PARAM;
	itemw.iItem = index;
	itemw.iSubItem = 0;
	if (SendMessage(mhwnd, LVM_GETITEMW, 0, (LPARAM)&itemw))
		return (uint32)itemw.lParam;

	return 0;
}

int VDUIProxyListView::FindVirtualItem(const IVDUIListViewVirtualItem& item) const {
	if (!mhwnd)
		return -1;

	LVFINDINFOW find {};
	find.flags = LVFI_PARAM;
	find.lParam = (LPARAM)&item;

	return SendMessage(mhwnd, LVM_FINDITEM, -1, (LPARAM)&find);
}

void VDUIProxyListView::InsertColumn(int index, const wchar_t *label, int width, bool rightAligned) {
	VDASSERT(index || !rightAligned);

	LVCOLUMNW colw = {};

	colw.mask		= LVCF_FMT | LVCF_TEXT | LVCF_WIDTH;
	colw.fmt		= rightAligned ? LVCFMT_RIGHT : LVCFMT_LEFT;
	colw.cx			= width < 0 ? 0 : width;
	colw.pszText	= (LPWSTR)label;

	int colIdx = SendMessageW(mhwnd, LVM_INSERTCOLUMNW, (WPARAM)index, (LPARAM)&colw);

	if (colIdx >= 0 && width < 0)
		SendMessageW(mhwnd, LVM_SETCOLUMNWIDTH, colIdx, LVSCW_AUTOSIZE_USEHEADER);
}

int VDUIProxyListView::InsertItem(int item, const wchar_t *text) {
	if (item < 0)
		item = 0x7FFFFFFF;

	LVITEMW itemw = {};

	itemw.mask		= LVIF_TEXT;
	itemw.iItem		= item;
	itemw.pszText	= (LPWSTR)text;

	return (int)SendMessageW(mhwnd, LVM_INSERTITEMW, 0, (LPARAM)&itemw);
}

int VDUIProxyListView::AddVirtualItem(IVDUIListViewVirtualItem& item) {
	return InsertVirtualItem(-1, &item);
}

int VDUIProxyListView::InsertVirtualItem(int item, IVDUIListViewVirtualItem *lvvi) {
	VDASSERT(!mbIndexedMode);

	if (item < 0)
		item = 0x7FFFFFFF;

	++mChangeNotificationLocks;

	LVITEMW itemw = {};

	itemw.mask		= LVIF_TEXT | LVIF_PARAM;
	itemw.iItem		= item;
	itemw.pszText	= LPSTR_TEXTCALLBACKW;
	itemw.lParam	= (LPARAM)lvvi;

	const int index = (int)SendMessageW(mhwnd, LVM_INSERTITEMW, 0, (LPARAM)&itemw);

	--mChangeNotificationLocks;

	if (index >= 0)
		lvvi->AddRef();

	return index;
}

int VDUIProxyListView::InsertIndexedItem(int item, uint32 id) {
	VDASSERT(mbIndexedMode);
	VDASSERT(id);

	if (item < 0)
		item = 0x7FFFFFFF;

	++mChangeNotificationLocks;

	LVITEMW itemw = {};

	itemw.mask		= LVIF_TEXT | LVIF_PARAM;
	itemw.iItem		= item;
	itemw.pszText	= LPSTR_TEXTCALLBACKW;
	itemw.lParam	= (LPARAM)id;

	const int index = (int)SendMessageW(mhwnd, LVM_INSERTITEMW, 0, (LPARAM)&itemw);

	--mChangeNotificationLocks;

	return index;
}

void VDUIProxyListView::RefreshItem(int item) {
	SendMessage(mhwnd, LVM_REDRAWITEMS, item, item);
}

void VDUIProxyListView::RefreshAllItems() {
	int n = GetItemCount();

	if (n)
		SendMessage(mhwnd, LVM_REDRAWITEMS, 0, n - 1);
}

void VDUIProxyListView::EditItemLabel(int item) {
	ListView_EditLabel(mhwnd, item);
}

void VDUIProxyListView::GetItemText(int item, VDStringW& s) const {
	LVITEMW itemw;
	wchar_t buf[512];

	itemw.iSubItem = 0;
	itemw.cchTextMax = 511;
	itemw.pszText = buf;
	buf[0] = 0;
	SendMessageW(mhwnd, LVM_GETITEMTEXTW, item, (LPARAM)&itemw);

	s = buf;
}

void VDUIProxyListView::SetItemText(int item, int subitem, const wchar_t *text) {
	LVITEMW itemw = {};

	itemw.mask		= LVIF_TEXT;
	itemw.iItem		= item;
	itemw.iSubItem	= subitem;
	itemw.pszText	= (LPWSTR)text;

	SendMessageW(mhwnd, LVM_SETITEMW, 0, (LPARAM)&itemw);
}

bool VDUIProxyListView::IsItemChecked(int item) {
	return ListView_GetCheckState(mhwnd, item) != 0;
}

void VDUIProxyListView::SetItemChecked(int item, bool checked) {
	ListView_SetCheckState(mhwnd, item, checked);
}

void VDUIProxyListView::SetItemCheckedVisible(int item, bool checked) {
	if (!mhwnd)
		return;

	ListView_SetItemState(mhwnd, item, INDEXTOSTATEIMAGEMASK(0), LVIS_STATEIMAGEMASK);
}

void VDUIProxyListView::AddImages(const VDPixmap& px) {
	HIMAGELIST hImageList = nullptr;

	if (mhwnd && mImageList.IsEmpty()) {
		mImageList.SetImageSize(ComputeImageSize(), ATUIGetThemeColors().mContentBg);
		hImageList = mImageList.AddEmptyImage();
		if (hImageList)
			ListView_SetImageList(mhwnd, hImageList, LVSIL_SMALL);
	}

	hImageList = mImageList.AddImages(px);
	if (hImageList)
		ListView_SetImageList(mhwnd, hImageList, LVSIL_SMALL);
}

void VDUIProxyListView::SetItemImage(int item, uint32 imageIndex) {
	LVITEMW itemw = {};

	itemw.mask		= LVIF_IMAGE;
	itemw.iItem		= item;
	itemw.iSubItem	= 0;
	itemw.iImage	= imageIndex;

	SendMessageW(mhwnd, LVM_SETITEMW, 0, (LPARAM)&itemw);
}

bool VDUIProxyListView::GetItemScreenRect(int item, vdrect32& r) const {
	r.set(0, 0, 0, 0);

	if (!mhwnd)
		return false;

	RECT nr = {LVIR_BOUNDS};
	if (!SendMessage(mhwnd, LVM_GETITEMRECT, (WPARAM)item, (LPARAM)&nr))
		return false;

	MapWindowPoints(mhwnd, NULL, (LPPOINT)&nr, 2);

	r.set(nr.left, nr.top, nr.right, nr.bottom);
	return true;
}

vdrect32 VDUIProxyListView::GetSubItemArea(int item, int subItem) const {
	vdrect32 r{0, 0, 0, 0};

	if (mhwnd) {
		RECT rc {};
		rc.top = subItem;
		rc.left = LVIR_BOUNDS;

		if (SendMessage(mhwnd, LVM_GETSUBITEMRECT, (WPARAM)item, (LPARAM)&rc)) {
			r.left = rc.left;
			r.top = rc.top;
			r.right = rc.right;
			r.bottom = rc.bottom;
		}
	}

	return r;
}

void VDUIProxyListView::Sort(IVDUIListViewIndexedComparer& comparer) {
	VDASSERT(mbIndexedMode);

	struct local {
		static int CALLBACK SortAdapter(LPARAM x, LPARAM y, LPARAM cookie) {
			return ((IVDUIListViewIndexedComparer *)cookie)->Compare((uint32)x, (uint32)y);
		};
	};

	ListView_SortItems(mhwnd, local::SortAdapter, (LPARAM)&comparer);
}

void VDUIProxyListView::Sort(IVDUIListViewVirtualComparer& comparer) {
	VDASSERT(!mbIndexedMode);

	struct local {
		static int CALLBACK SortAdapter(LPARAM x, LPARAM y, LPARAM cookie) {
			return ((IVDUIListViewVirtualComparer *)cookie)->Compare((IVDUIListViewVirtualItem *)x, (IVDUIListViewVirtualItem *)y);
		};
	};

	ListView_SortItems(mhwnd, local::SortAdapter, (LPARAM)&comparer);
}

void VDUIProxyListView::SetOnItemDoubleClicked(vdfunction<void(int)> fn) {
	mpOnItemDoubleClicked = std::move(fn);
}

void VDUIProxyListView::SetOnItemContextMenu(vdfunction<void(ContextMenuEvent&)> fn) {
	mpOnItemContextMenu = std::move(fn);
}

void VDUIProxyListView::SetOnItemLabelChanging(vdfunction<bool(LabelChangingEvent&)> fn) {
	mpOnItemLabelChanging = std::move(fn);
}

void VDUIProxyListView::SetOnItemCustomStyle(vdfunction<bool(IVDUIListViewVirtualItem&, sint32&, bool&)> fn) {
	mpOnItemCustomStyle = std::move(fn);
}

void VDUIProxyListView::SetOnSubItemSelected(vdfunction<void(uint32 /*item*/, uint32 /*subitem*/)> fn) {
	mpOnSubItemSelected = std::move(fn);
}

void VDUIProxyListView::SetOnEnterPressed(vdfunction<void()> fn) {
	mpOnEnterPressed = std::move(fn);
}

VDZLRESULT VDUIProxyListView::On_WM_NOTIFY(VDZWPARAM wParam, VDZLPARAM lParam) {
	const NMHDR *hdr = (const NMHDR *)lParam;

	switch(hdr->code) {
		case LVN_GETDISPINFOA:
			{
				NMLVDISPINFOA *dispa = (NMLVDISPINFOA *)hdr;

				if (dispa->item.mask & LVIF_TEXT) {
					mTextW[0].clear();

					if (mbIndexedMode) {
						if (mpIndexedProvider)
							mpIndexedProvider->GetText((uint32)dispa->item.lParam, dispa->item.iSubItem, mTextW[0]);
					} else {
						IVDUIListViewVirtualItem *lvvi = (IVDUIListViewVirtualItem *)dispa->item.lParam;
						if (lvvi)
							lvvi->GetText(dispa->item.iSubItem, mTextW[0]);
					}

					mTextA[mNextTextIndex] = VDTextWToA(mTextW[0]);
					dispa->item.pszText = (LPSTR)mTextA[mNextTextIndex].c_str();

					if (++mNextTextIndex >= 3)
						mNextTextIndex = 0;
				}
			}
			break;

		case LVN_GETDISPINFOW:
			{
				NMLVDISPINFOW *dispw = (NMLVDISPINFOW *)hdr;

				if (dispw->item.mask & LVIF_TEXT) {
					mTextW[mNextTextIndex].clear();

					if (mbIndexedMode) {
						if (mpIndexedProvider)
							mpIndexedProvider->GetText((uint32)dispw->item.lParam, dispw->item.iSubItem, mTextW[mNextTextIndex]);
					} else {
						IVDUIListViewVirtualItem *lvvi = (IVDUIListViewVirtualItem *)dispw->item.lParam;
						if (lvvi)
							lvvi->GetText(dispw->item.iSubItem, mTextW[mNextTextIndex]);
					}

					dispw->item.pszText = (LPWSTR)mTextW[mNextTextIndex].c_str();

					if (++mNextTextIndex >= 3)
						mNextTextIndex = 0;
				}
			}
			break;

		case LVN_DELETEITEM:
			if (!mbIndexedMode) {
				const NMLISTVIEW *nmlv = (const NMLISTVIEW *)hdr;
				IVDUIListViewVirtualItem *lvvi = (IVDUIListViewVirtualItem *)nmlv->lParam;

				if (lvvi)
					lvvi->Release();
			}
			break;

		case LVN_COLUMNCLICK:
			{
				const NMLISTVIEW *nmlv = (const NMLISTVIEW *)hdr;

				mEventColumnClicked.Raise(this, nmlv->iSubItem);
			}
			break;

		case LVN_ITEMCHANGING:
			if (!mChangeNotificationLocks) {
				const NMLISTVIEW *nmlv = (const NMLISTVIEW *)hdr;

				if (nmlv->uChanged & LVIF_STATE) {
					uint32 deltaState = nmlv->uOldState ^ nmlv->uNewState;

					if (deltaState & LVIS_STATEIMAGEMASK) {
						VDASSERT(nmlv->iItem >= 0);

						CheckedChangingEvent event;
						event.mIndex = nmlv->iItem;
						event.mbNewVisible = (nmlv->uNewState & LVIS_STATEIMAGEMASK) != 0;
						event.mbNewChecked = (nmlv->uNewState & 0x2000) != 0;
						event.mbAllowChange = true;
						mEventItemCheckedChanging.Raise(this, &event);

						if (!event.mbAllowChange)
							return TRUE;
					}
				}
			}
			break;

		case LVN_ITEMCHANGED:
			if (!mChangeNotificationLocks) {
				const NMLISTVIEW *nmlv = (const NMLISTVIEW *)hdr;

				if (nmlv->uChanged & LVIF_STATE) {
					uint32 deltaState = nmlv->uOldState ^ nmlv->uNewState;

					if (deltaState & LVIS_SELECTED) {
						int selIndex = ListView_GetNextItem(mhwnd, -1, LVNI_ALL | LVNI_SELECTED);

						mEventItemSelectionChanged.Raise(this, selIndex);
					}

					if (deltaState & LVIS_STATEIMAGEMASK) {
						VDASSERT(nmlv->iItem >= 0);
						mEventItemCheckedChanged.Raise(this, nmlv->iItem);
					}
				}
			}
			break;

		case LVN_BEGINLABELEDITW:
			{
				const NMLVDISPINFOW *di = (const NMLVDISPINFOW *)hdr;

				if (mpOnItemLabelChanging) {
					LabelChangingEvent event;
					event.mIndex = di->item.iItem;
					event.mbUseReplacementEditText = false;

					if (!mpOnItemLabelChanging(event))
						return TRUE;

					if (event.mbUseReplacementEditText) {
						HWND hwndEdit = (HWND)SendMessage(mhwnd, LVM_GETEDITCONTROL, 0, 0);
						
						if (hwndEdit)
							SetWindowTextW(hwndEdit, event.mReplacementEditText.c_str());
					}
				}
			}
			return FALSE;

		case LVN_ENDLABELEDITW:
			{
				const NMLVDISPINFOW *di = (const NMLVDISPINFOW *)hdr;

				LabelChangedEvent event = {
					true,
					di->item.iItem,
					di->item.pszText
				};

				mEventItemLabelEdited.Raise(this, &event);

				if (!event.mbAllowEdit)
					return FALSE;
			}
			return TRUE;

		case LVN_BEGINDRAG:
			{
				const NMLISTVIEW *nmlv = (const NMLISTVIEW *)hdr;

				mEventItemBeginDrag.Raise(this, nmlv->iItem);
			}
			return 0;

		case LVN_BEGINRDRAG:
			{
				const NMLISTVIEW *nmlv = (const NMLISTVIEW *)hdr;

				mEventItemBeginRDrag.Raise(this, nmlv->iItem);
			}
			return 0;

		case LVN_KEYDOWN:
			if (mbActivateOnEnter) {
				const NMLVKEYDOWN& keyDown = *(const NMLVKEYDOWN *)hdr;

				if (keyDown.wVKey == VK_RETURN) {
					const int index = GetSelectedIndex();

					if (index >= 0) {
						mEventItemDoubleClicked.Raise(this, index);

						if (mpOnItemDoubleClicked)
							mpOnItemDoubleClicked(index);
					}
				}

				return 0;
			}
			break;

		case NM_CLICK:
			{
				const NMITEMACTIVATE *nmia = (const NMITEMACTIVATE *)hdr;

				if (mpOnSubItemSelected)
					mpOnSubItemSelected(nmia->iItem, nmia->iSubItem);
			}
			return 0;

		case NM_DBLCLK:
			{
				const NMITEMACTIVATE *nmia = (const NMITEMACTIVATE *)hdr;

				// skip handling if the double click is on the checkbox
				LVHITTESTINFO hti {};
				hti.pt = nmia->ptAction;

				SendMessage(mhwnd, LVM_SUBITEMHITTEST, 0, (LPARAM)&hti);

				if (hti.flags & LVHT_ONITEMSTATEICON)
					return 0;

				mEventItemDoubleClicked.Raise(this, nmia->iItem);
				if (mpOnItemDoubleClicked)
					mpOnItemDoubleClicked(nmia->iItem);
			}
			return 0;

		case NM_CUSTOMDRAW:
			if (bool doDarkDisabled = !IsWindowEnabled(mhwnd) && ATUIIsDarkThemeActive(); mpOnItemCustomStyle || doDarkDisabled) {
				NMLVCUSTOMDRAW& nmcd = *(NMLVCUSTOMDRAW *)hdr;

				if (nmcd.nmcd.dwDrawStage == CDDS_PREPAINT) {
					return CDRF_NOTIFYITEMDRAW;
				} else if (nmcd.nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
					auto *item = (IVDUIListViewVirtualItem *)nmcd.nmcd.lItemlParam;

					// For dark mode, we need to override the background color for items.
					if (doDarkDisabled)
						nmcd.clrTextBk = ATUIGetThemeColorsW32().mStaticBgCRef;

					if (item && mpOnItemCustomStyle) {
						sint32 color = -1;
						bool bold = false;

						if (mpOnItemCustomStyle(*item, color, bold)) {
							if (color >= 0)
								nmcd.clrText = VDSwizzleU32((uint32)color) >> 8;

							if (bold) {
								if (!mBoldFont) {
									HFONT hfont = (HFONT)SendMessage(mhwnd, WM_GETFONT, 0, 0);

									if (hfont) {
										LOGFONT lf {};

										if (GetObject(hfont, sizeof lf, &lf)) {
											lf.lfWeight = 700;

											mBoldFont = CreateFontIndirect(&lf);
										}
									}
								}

								if (mBoldFont)
									SelectObject(nmcd.nmcd.hdc, mBoldFont);
							}
						}
					}
				}
			}

			return CDRF_DODEFAULT;
	}

	return 0;
}

bool VDUIProxyListView::On_WM_CONTEXTMENU(VDZWPARAM wParam, VDZLPARAM lParam) {
	ContextMenuEvent event;

	event.mIndex = -1;

	LVHITTESTINFO ht {};

	vdpoint32 spt(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
	if (spt.x == -1 && spt.y == -1) {
		// keyboard activation
		bool havePoint = false;
		event.mIndex = GetSelectedIndex();

		if (event.mIndex >= 0) {
			ListView_EnsureVisible(mhwnd, event.mIndex, FALSE);

			RECT r {};
			if (ListView_GetItemRect(mhwnd, event.mIndex, &r, LVIR_BOUNDS)) {
				// We bias to the left side as that feels a bit better than the center.
				const vdpoint32& itemPoint = TransformClientToScreen(vdpoint32(r.left + ((r.right - r.left) >> 2), (r.top + r.bottom) >> 1));

				event.mX = itemPoint.x;
				event.mY = itemPoint.y;
				havePoint = true;
			}
		}

		if (!havePoint) {
			const vdsize32& sz = GetClientSize();
			vdpoint32 cpt { sz.w >> 1, sz.h >> 1 };

			const vdpoint32& screenCenter = TransformClientToScreen(cpt);
			event.mX = screenCenter.x;
			event.mY = screenCenter.y;
		}
	} else {
		// mouse activation
		vdpoint32 cpt = TransformScreenToClient(spt);
		ht.pt.x = cpt.x;
		ht.pt.y = cpt.y;
		event.mIndex = ListView_HitTest(mhwnd, &ht);
		event.mX = spt.x;
		event.mY = spt.y;
	}

	event.mbHandled = false;
	mEventItemContextMenu.Raise(this, event);

	if (mpOnItemContextMenu)
		mpOnItemContextMenu(event);

	return event.mbHandled;
}

void VDUIProxyListView::OnFontChanged() {
	// Windows 10 ver 1703 has a problem with leaving list view items at ridiculous height when
	// moving a window from higher to lower DPI in per monitor V2 mode. To work around this
	// issue, we flash the view to list mode and back to force it to recompute the item heights.
	if (AreItemCheckboxesEnabled()) {
		ListView_SetView(mhwnd, LV_VIEW_LIST);
		ListView_SetView(mhwnd, LV_VIEW_DETAILS);
	}

	if (mBoldFont) {
		DeleteObject(mBoldFont);
		mBoldFont = nullptr;
	}

	HIMAGELIST hNewImageList = mImageList.SetImageSize(ComputeImageSize(), 0);
	if (hNewImageList) {
		ListView_SetImageList(mhwnd, hNewImageList, LVSIL_SMALL);
		mImageList.ReleasePrevImageList();
	}
}

int VDUIProxyListView::ComputeImageSize() {
	if (HFONT hfont = (HFONT)SendMessage(mhwnd, WM_GETFONT, 0, 0)) {
		if (HDC hdc = GetDC(mhwnd)) {
			if (HGDIOBJ hOldFont = SelectObject(hdc, hfont)) {
				TEXTMETRICW tm = {};
					
				if (GetTextMetricsW(hdc, &tm)) {
					return tm.tmAscent + tm.tmDescent;
				}

				SelectObject(hdc, hOldFont);
			}

			ReleaseDC(mhwnd, hdc);
		}
	}

	return 16;
}

VDZLRESULT VDUIProxyListView::ListViewSubclassProc(VDZHWND hwnd, VDZUINT msg, VDZWPARAM wParam, VDZLPARAM lParam) {
	switch(msg) {
		case WM_ERASEBKGND:
			if (ATUIIsDarkThemeActive() && !IsWindowEnabled(hwnd)) {
				if (RECT r; GetClientRect(hwnd, &r)) {
					FillRect((HDC)wParam, &r, ATUIGetThemeColorsW32().mStaticBgBrush);
					return TRUE;
				}
			}
			break;

		case WM_NOTIFY:
			if (ATUIIsDarkThemeActive() && lParam) {
				const NMHDR& hdr = *(const NMHDR *)lParam;

				if (hdr.code == NM_CUSTOMDRAW && hdr.hwndFrom == mhwndHeader) {
					const NMCUSTOMDRAW& nmcd = *(const NMCUSTOMDRAW *)lParam;

					const ATUIThemeColors& colors = ATUIGetThemeColors();
					const COLORREF bg = VDSwizzleU32(colors.mListViewHeaderBg) >> 8;
					const COLORREF fg = VDSwizzleU32(colors.mListViewHeaderFg) >> 8;

					if (nmcd.dwDrawStage == CDDS_PREPAINT) {
						SetDCBrushColor(nmcd.hdc, bg);

						RECT r;
						if (GetClientRect(hdr.hwndFrom, &r)) {
							int n = Header_GetItemCount(mhwndHeader);

							if (!n) {
								FillRect(nmcd.hdc, &r, (HBRUSH)GetStockObject(DC_BRUSH));
								return CDRF_SKIPDEFAULT;
							}

							// The header control will by default fill the area after the last column.
							// Defeat this by filling it with the desired color, and then excluding it
							// from the clip rect.

							RECT rLast {};
							Header_GetItemRect(hdr.hwndFrom, n - 1, &rLast);

							if (r.right > rLast.right) {
								RECT r2 = r;
								r2.left = rLast.right;
								FillRect(nmcd.hdc, &r2, (HBRUSH)GetStockObject(DC_BRUSH));

								IntersectClipRect(nmcd.hdc, 0, 0, rLast.right, r.bottom);
							}

							return CDRF_NOTIFYITEMDRAW;
						}
					} else if (nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
						if (nmcd.rc.right <= nmcd.rc.left)
							return CDRF_DODEFAULT;

						// fill the background and draw the divider line at the end
						RECT r = nmcd.rc;
						--r.right;
						SetDCBrushColor(nmcd.hdc, bg);
						FillRect(nmcd.hdc, &r, (HBRUSH)GetStockObject(DC_BRUSH));

						r.left = r.right;
						++r.right;
						SetDCBrushColor(nmcd.hdc, VDSwizzleU32(colors.mListViewHeaderDivider) >> 8);
						FillRect(nmcd.hdc, &r, (HBRUSH)GetStockObject(DC_BRUSH));

						// draw text if there is room
						int margin = ATUIGetDpiScaledSystemMetricForWindowW32(mhwndHeader, SM_CXEDGE) * 3;

						r = nmcd.rc;
						r.left += margin;
						r.right -= margin;

						if (r.right > r.left) {
							WCHAR text[256];
							text[0] = L'\0';

							HDITEMW hdi {};
							hdi.mask = HDI_TEXT;
							hdi.pszText = text;
							hdi.cchTextMax = 256;

							Header_GetItem(mhwndHeader, nmcd.dwItemSpec, &hdi);

							SetBkColor(nmcd.hdc, bg);
							SetBkMode(nmcd.hdc, OPAQUE);
							SetTextColor(nmcd.hdc, fg);

							DrawTextW(nmcd.hdc, text, -1, &r, DT_VCENTER | DT_SINGLELINE | DT_LEFT | DT_NOPREFIX | DT_END_ELLIPSIS);
						}

						return CDRF_SKIPDEFAULT;
					}
				}
			}
			break;

		case WM_KEYDOWN:
			if (wParam == VK_RETURN && mpOnEnterPressed) {
				mpOnEnterPressed();
				return 0;
			}
			break;

		case WM_GETDLGCODE: {
			if (mpOnEnterPressed && lParam) {
				LRESULT code = 0;
				const MSG& message = *(const MSG *)lParam;

				if (message.message == WM_KEYDOWN && message.wParam == VK_RETURN)
					code = DLGC_WANTMESSAGE;

				return code | DefSubclassProc(hwnd, msg, wParam, lParam);
			}

			break;
		}
	}

	return DefSubclassProc(hwnd, msg, wParam, lParam);
}

///////////////////////////////////////////////////////////////////////////

VDUIProxyHotKeyControl::VDUIProxyHotKeyControl() {
}

VDUIProxyHotKeyControl::~VDUIProxyHotKeyControl() {
}

bool VDUIProxyHotKeyControl::GetAccelerator(VDUIAccelerator& accel) const {
	if (!mhwnd)
		return false;

	uint32 v = (uint32)SendMessage(mhwnd, HKM_GETHOTKEY, 0, 0);

	accel.mVirtKey = (uint8)v;
	accel.mModifiers = 0;
	
	const uint8 mods = (uint8)(v >> 8);
	if (mods & HOTKEYF_SHIFT)
		accel.mModifiers |= VDUIAccelerator::kModShift;

	if (mods & HOTKEYF_CONTROL)
		accel.mModifiers |= VDUIAccelerator::kModCtrl;

	if (mods & HOTKEYF_ALT)
		accel.mModifiers |= VDUIAccelerator::kModAlt;

	if (mods & HOTKEYF_EXT)
		accel.mModifiers |= VDUIAccelerator::kModExtended;

	return true;
}

void VDUIProxyHotKeyControl::SetAccelerator(const VDUIAccelerator& accel) {
	uint32 mods = 0;

	if (accel.mModifiers & VDUIAccelerator::kModShift)
		mods |= HOTKEYF_SHIFT;

	if (accel.mModifiers & VDUIAccelerator::kModCtrl)
		mods |= HOTKEYF_CONTROL;

	if (accel.mModifiers & VDUIAccelerator::kModAlt)
		mods |= HOTKEYF_ALT;

	if (accel.mModifiers & VDUIAccelerator::kModExtended)
		mods |= HOTKEYF_EXT;

	SendMessage(mhwnd, HKM_SETHOTKEY, accel.mVirtKey + (mods << 8), 0);
}

VDZLRESULT VDUIProxyHotKeyControl::On_WM_COMMAND(VDZWPARAM wParam, VDZLPARAM lParam) {
	if (HIWORD(wParam) == EN_CHANGE) {
		VDUIAccelerator accel;
		GetAccelerator(accel);
		mEventHotKeyChanged.Raise(this, accel);
	}

	return 0;
}

///////////////////////////////////////////////////////////////////////////

VDUIProxyTabControl::VDUIProxyTabControl() {
}

VDUIProxyTabControl::~VDUIProxyTabControl() {
}

void VDUIProxyTabControl::AddItem(const wchar_t *s) {
	if (!mhwnd)
		return;

	int n = TabCtrl_GetItemCount(mhwnd);
	TCITEMW tciw = { TCIF_TEXT };

	tciw.pszText = (LPWSTR)s;

	SendMessageW(mhwnd, TCM_INSERTITEMW, n, (LPARAM)&tciw);
}

void VDUIProxyTabControl::DeleteItem(int index) {
	if (mhwnd)
		SendMessage(mhwnd, TCM_DELETEITEM, index, 0);
}

vdsize32 VDUIProxyTabControl::GetControlSizeForContent(const vdsize32& sz) const {
	if (!mhwnd)
		return vdsize32(0, 0);

	RECT r = { 0, 0, sz.w, sz.h };
	TabCtrl_AdjustRect(mhwnd, TRUE, &r);

	return vdsize32(r.right - r.left, r.bottom - r.top);
}

vdrect32 VDUIProxyTabControl::GetContentArea() const {
	if (!mhwnd)
		return vdrect32(0, 0, 0, 0);

	RECT r = {0};
	GetWindowRect(mhwnd, &r);

	HWND hwndParent = GetParent(mhwnd);
	if (hwndParent)
		MapWindowPoints(NULL, hwndParent, (LPPOINT)&r, 2);

	TabCtrl_AdjustRect(mhwnd, FALSE, &r);

	return vdrect32(r.left, r.top, r.right, r.bottom);
}

int VDUIProxyTabControl::GetSelection() const {
	if (!mhwnd)
		return -1;

	return (int)SendMessage(mhwnd, TCM_GETCURSEL, 0, 0);
}

void VDUIProxyTabControl::SetSelection(int index) {
	if (mhwnd)
		SendMessage(mhwnd, TCM_SETCURSEL, index, 0);
}

VDZLRESULT VDUIProxyTabControl::On_WM_NOTIFY(VDZWPARAM wParam, VDZLPARAM lParam) {
	if (((const NMHDR *)lParam)->code == TCN_SELCHANGE) {
		mSelectionChanged.Raise(this, GetSelection());
	}

	return 0;
}

///////////////////////////////////////////////////////////////////////////

VDUIProxyListBoxControl::VDUIProxyListBoxControl() {
}

VDUIProxyListBoxControl::~VDUIProxyListBoxControl() {
	if (mpEditWndProcThunk) {
		VDDestroyFunctionThunk(mpEditWndProcThunk);
		mpEditWndProcThunk = NULL;
	}

	if (mpWndProcThunk) {
		VDDestroyFunctionThunk(mpWndProcThunk);
		mpWndProcThunk = NULL;
	}

	CancelEditTimer();

	if (mpEditTimerThunk) {
		VDDestroyFunctionThunk(mpEditTimerThunk);
		mpEditTimerThunk = NULL;
	}
}

void VDUIProxyListBoxControl::EnableAutoItemEditing() {
	if (!mPrevWndProc) {
		mPrevWndProc = (void(*)())GetWindowLongPtr(mhwnd, GWLP_WNDPROC);

		if (!mpWndProcThunk)
			mpWndProcThunk = VDCreateFunctionThunkFromMethod(this, &VDUIProxyListBoxControl::ListBoxWndProc, true);

		SetWindowLongPtr(mhwnd, GWLP_WNDPROC, (LONG_PTR)VDGetThunkFunction<WNDPROC>(mpWndProcThunk));
	}
}

void VDUIProxyListBoxControl::Clear() {
	if (!mhwnd)
		return;

	SendMessage(mhwnd, LB_RESETCONTENT, 0, 0);
}

int VDUIProxyListBoxControl::AddItem(const wchar_t *s, uintptr_t cookie) {
	if (!mhwnd)
		return -1;

	CancelEditTimer();

	int idx;
	idx = (int)SendMessageW(mhwnd, LB_ADDSTRING, 0, (LPARAM)s);

	if (idx >= 0)
		SendMessage(mhwnd, LB_SETITEMDATA, idx, (LPARAM)cookie);

	return idx;
}

int VDUIProxyListBoxControl::InsertItem(int pos, const wchar_t *s, uintptr_t cookie) {
	if (!mhwnd)
		return -1;

	CancelEditTimer();

	int idx;
	idx = (int)SendMessageW(mhwnd, LB_INSERTSTRING, (WPARAM)pos, (LPARAM)s);

	if (idx >= 0)
		SendMessage(mhwnd, LB_SETITEMDATA, idx, (LPARAM)cookie);

	return idx;
}

void VDUIProxyListBoxControl::DeleteItem(int pos) {
	if (!mhwnd)
		return;

	CancelEditTimer();
	SendMessageW(mhwnd, LB_DELETESTRING, (WPARAM)pos, 0);
}

void VDUIProxyListBoxControl::EnsureItemVisible(int pos) {
	if (!mhwnd || pos < 0)
		return;

	CancelEditTimer();

	RECT r;
	if (!GetClientRect(mhwnd, &r))
		return;

	RECT rItem;
	if (LB_ERR != SendMessageW(mhwnd, LB_GETITEMRECT, (WPARAM)pos, (LPARAM)&rItem)) {
		if (rItem.top < r.bottom && r.top < rItem.bottom)
			return;
	}

	// Item isn't visible. Scroll it into view.
	int itemHeight = (int)SendMessageW(mhwnd, LB_GETITEMHEIGHT, 0, 0);
	if (itemHeight <= 0)
		return;

	int topIndex = std::max<int>(0, pos - (r.bottom - itemHeight) / (2 * itemHeight));
	SendMessageW(mhwnd, LB_SETTOPINDEX, (WPARAM)topIndex, 0);
}

void VDUIProxyListBoxControl::EditItem(int index) {
	if (!mhwnd)
		return;

	CancelEditTimer();
	EnsureItemVisible(index);

	RECT rItem {};
	if (LB_ERR == SendMessageW(mhwnd, LB_GETITEMRECT, (WPARAM)index, (LPARAM)&rItem))
		return;

	int textLen = (int)SendMessageW(mhwnd, LB_GETTEXTLEN, (WPARAM)index, 0);
	vdfastvector<WCHAR> textbuf(textLen + 1, 0);

	SendMessageW(mhwnd, LB_GETTEXT, (WPARAM)index, (LPARAM)textbuf.data());

	mEditItem = index;

	int cxedge = 1;
	int cyedge = 1;

	mhwndEdit = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, WC_EDITW, textbuf.data(), WS_CHILD | WS_BORDER | ES_AUTOHSCROLL,
		rItem.left - 0*cxedge,
		rItem.top - 2*cxedge,
		(rItem.right - rItem.left) + 0*cxedge,
		(rItem.bottom - rItem.top) + 4*cyedge,
		mhwnd, NULL, VDGetLocalModuleHandleW32(), NULL);

	SendMessageW(mhwndEdit, WM_SETFONT, (WPARAM)SendMessageW(mhwnd, WM_GETFONT, 0, 0), (LPARAM)TRUE);

	if (!mpEditWndProcThunk)
		mpEditWndProcThunk = VDCreateFunctionThunkFromMethod(this, &VDUIProxyListBoxControl::LabelEditWndProc, true);

	if (mpEditWndProcThunk) {
		mPrevEditWndProc = (void (*)())(WNDPROC)GetWindowLongPtrW(mhwndEdit, GWLP_WNDPROC);

		if (mPrevEditWndProc)
			SetWindowLongPtrW(mhwndEdit, GWLP_WNDPROC, (LONG_PTR)VDGetThunkFunction<WNDPROC>(mpEditWndProcThunk));
	}

	ShowWindow(mhwndEdit, SW_SHOWNOACTIVATE);
	::SetFocus(mhwndEdit);
	SendMessageW(mhwndEdit, EM_SETSEL, 0, -1);
}

VDStringW VDUIProxyListBoxControl::GetItemText(int index) const {
	VDStringW s;

	if (mhwnd && index >= 0) {
		int len = (int)SendMessage(mhwnd, LB_GETTEXTLEN, (WPARAM)index, 0);

		if (len > 0) {
			vdfastvector<WCHAR> buf(len + 1, 0);
			
			SendMessage(mhwnd, LB_GETTEXT, (WPARAM)index, (LPARAM)buf.data());

			buf.back() = 0;
			s = buf.data();
		}
	}

	return s;
}

void VDUIProxyListBoxControl::SetItemText(int index, const wchar_t *s) {
	if (!mhwnd || index < 0)
		return;

	CancelEditTimer();

	int n = (int)SendMessage(mhwnd, LB_GETCOUNT, 0, 0);
	if (index >= n)
		return;

	uintptr_t itemData = (uintptr_t)SendMessage(mhwnd, LB_GETITEMDATA, index, 0);
	bool selected = (GetSelection() == index);

	++mSuppressNotificationCount;
	SendMessage(mhwnd, LB_DELETESTRING, index, 0);
	int newIdx = (int)SendMessageW(mhwnd, LB_INSERTSTRING, 0, (LPARAM)s);
	if (newIdx >= 0) {
		SendMessage(mhwnd, LB_SETITEMDATA, newIdx, (LPARAM)itemData);

		if (selected)
			SetSelection(newIdx);
	}
	--mSuppressNotificationCount;
}

uintptr VDUIProxyListBoxControl::GetItemData(int index) const {
	if (index < 0 || !mhwnd)
		return 0;

	return SendMessage(mhwnd, LB_GETITEMDATA, index, 0);
}

vdrect32 VDUIProxyListBoxControl::GetItemRect(int index) const {
	if (index < 0 || !mhwnd)
		return vdrect32{};

	RECT r {};
	SendMessage(mhwnd, LB_GETITEMRECT, (WPARAM)index, (LPARAM)&r);

	return vdrect32 { r.left, r.top, r.right, r.bottom };
}

int VDUIProxyListBoxControl::PointToItem(const vdpoint32& cpt) const {
	if (!mhwnd || cpt.x < 0 || cpt.y < 0)
		return -1;

	auto result = SendMessage(mhwnd, LB_ITEMFROMPOINT, 0, MAKELONG(cpt.x, cpt.y));

	if (HIWORD(result))
		return -1;

	// if the item is the first or the last, double check it against the item's
	// vertical extents as we only want an exact hit
	const int idx = LOWORD(result);

	RECT rItem {};
	if (LB_ERR == SendMessageW(mhwnd, LB_GETITEMRECT, (WPARAM)idx, (LPARAM)&rItem))
		return -1;

	if (cpt.y < rItem.top || cpt.y >= rItem.bottom)
		return -1;

	return idx;
}

bool VDUIProxyListBoxControl::HaveSelection() const {
	if (!mhwnd)
		return false;

	if (mbIsMulti) 
		return (int)SendMessage(mhwnd, LB_GETSELCOUNT, 0, 0) > 0;
	else
		return GetSelection() >= 0;
}

int VDUIProxyListBoxControl::GetSelection() const {
	if (!mhwnd)
		return -1;

	return (int)SendMessage(mhwnd, LB_GETCURSEL, 0, 0);
}

void VDUIProxyListBoxControl::SetSelection(int index) {
	if (mhwnd) {
		if (mbIsMulti) {
			int selItems[2] {};
			const int fetched = (int)SendMessage(mhwnd, LB_GETSELITEMS, 2, (LPARAM)selItems);

			if (index < 0) {
				if (fetched > 0) {
					SendMessage(mhwnd, LB_SETSEL, FALSE, -1);
				}
			} else {
				if (fetched != 1 || selItems[0] != index) {
					SendMessage(mhwnd, LB_SETSEL, FALSE, -1);
					SendMessage(mhwnd, LB_SETSEL, TRUE, index);
				}
			}
		} else {
			SendMessage(mhwnd, LB_SETCURSEL, index, 0);
		}
	}
}

vdfastvector<int> VDUIProxyListBoxControl::GetSelectedIndices() const {
	vdfastvector<int> indices;

	if (mhwnd) {
		if (mbIsMulti) {
			int n = (int)SendMessage(mhwnd, LB_GETSELCOUNT, 0, 0);

			if (n > 0) {
				indices.resize((size_t)n);

				int n2 = SendMessage(mhwnd, LB_GETSELITEMS, n, (LPARAM)indices.data());

				if (n2 < 0)
					indices.clear();
				else if (n2 < n)
					indices.resize(n2);
			}
		} else {
			int sel = GetSelection();

			if (sel >= 0)
				indices.push_back(sel);
		}
	}

	return indices;
}

void VDUIProxyListBoxControl::SetSelectedIndices(vdspan<const int> indices) {
	if (!mhwnd)
		return;

	if (!mbIsMulti) {
		SetSelection(indices.empty() ? -1 : indices[0]);
		return;
	}

	SendMessage(mhwnd, LB_SETSEL, FALSE, (LPARAM)-1);

	for(int idx : indices)
		SendMessage(mhwnd, LB_SETSEL, TRUE, (LPARAM)idx);
}

int VDUIProxyListBoxControl::GetFocusedItem() const {
	if (!mhwnd)
		return -1;

	return (int)SendMessage(mhwnd, LB_GETCARETINDEX, 0, 0);
}

void VDUIProxyListBoxControl::SetFocusedItem(int idx) {
	if (!mhwnd)
		return;

	SendMessage(mhwnd, LB_SETCARETINDEX, (WPARAM)(idx < 0 ? -1 : idx), FALSE);
}

void VDUIProxyListBoxControl::MakeSelectionVisible() {
	if (!mhwnd)
		return;

	int idx = (int)SendMessage(mhwnd, LB_GETCURSEL, 0, 0);

	if (idx >= 0)
		SendMessage(mhwnd, LB_SETCURSEL, idx, 0);
}

void VDUIProxyListBoxControl::SetOnSelectionChanged(vdfunction<void(int)> fn) {
	mpFnSelectionChanged = fn;
}

void VDUIProxyListBoxControl::SetOnItemDoubleClicked(vdfunction<void(int)> fn) {
	mpFnItemDoubleClicked = fn;
}

void VDUIProxyListBoxControl::SetOnItemEdited(vdfunction<void(int ,const wchar_t *)> fn) {
	mpFnItemEdited = fn;
}

void VDUIProxyListBoxControl::SetOnContextMenu(vdfunction<void(const vdpoint32& cpt, int idx)> fn) {
	mpFnContextMenu = std::move(fn);
}

int VDUIProxyListBoxControl::GetScrollPos() const {
	if (!mhwnd)
		return -1;

	return (int)SendMessage(mhwnd, LB_GETTOPINDEX, 0, 0);
}

void VDUIProxyListBoxControl::SetScrollPos(int pos) {
	if (mhwnd)
		SendMessage(mhwnd, LB_SETTOPINDEX, pos, 0);
}

void VDUIProxyListBoxControl::SetTabStops(const int *units, uint32 n) {
	if (!mhwnd)
		return;

	vdfastvector<INT> v(n);

	for(uint32 i=0; i<n; ++i)
		v[i] = units[i];

	SendMessage(mhwnd, LB_SETTABSTOPS, n, (LPARAM)v.data());
}

void VDUIProxyListBoxControl::EndEditItem() {
	if (mhwndEdit) {
		SetWindowLongPtrW(mhwndEdit, GWLP_WNDPROC, (LONG_PTR)mPrevEditWndProc);

		auto h = mhwndEdit;
		mhwndEdit = nullptr;

		DestroyWindow(h);
	}
}

void VDUIProxyListBoxControl::CancelEditTimer() {
	if (mAutoEditTimer) {
		KillTimer(nullptr, mAutoEditTimer);
		mAutoEditTimer = 0;
	}
}

void VDUIProxyListBoxControl::Attach(VDZHWND hwnd) {
	VDUIProxyControl::Attach(hwnd);

	mbIsMulti = (GetWindowLongPtr(hwnd, GWL_STYLE) & (LBS_MULTIPLESEL | LBS_EXTENDEDSEL)) != 0;
}

void VDUIProxyListBoxControl::Detach() {
	EndEditItem();

	if (mPrevWndProc) {
		SetWindowLongPtrW(mhwnd, GWLP_WNDPROC, (LONG_PTR)mPrevWndProc);
		mPrevWndProc = nullptr;
	}
}

VDZLRESULT VDUIProxyListBoxControl::On_WM_COMMAND(VDZWPARAM wParam, VDZLPARAM lParam) {
	if (mSuppressNotificationCount)
		return 0;

	if (HIWORD(wParam) == LBN_SELCHANGE) {
		int selIndex = GetSelection();

		if (mpFnSelectionChanged)
			mpFnSelectionChanged(selIndex);

		mSelectionChanged.Raise(this, selIndex);
	} else if (HIWORD(wParam) == LBN_DBLCLK) {
		int sel = GetSelection();

		if (sel >= 0) {
			if (mpFnItemDoubleClicked)
				mpFnItemDoubleClicked(sel);

			mEventItemDoubleClicked.Raise(this, sel);
		}
	}

	return 0;
}

bool VDUIProxyListBoxControl::On_WM_CONTEXTMENU(VDZWPARAM wParam, VDZLPARAM lParam) {
	if (mpFnContextMenu) {
		const sint32 x = GET_X_LPARAM(lParam);
		const sint32 y = GET_Y_LPARAM(lParam);

		if (x == -1 && y == -1) {
			int idx = GetFocusedItem();
			vdrect32 r = GetItemRect(idx);
			vdrect32 rc = GetClientArea();

			r.left = std::max(r.left, rc.left);
			r.top = std::max(r.top, rc.top);
			r.right = std::min(r.right, rc.right);
			r.bottom = std::min(r.bottom, rc.bottom);

			if (r.empty())
				r = rc;

			const vdpoint32 cpt { (r.left + r.right) >> 1, (r.top + r.bottom) >> 1 };

			mpFnContextMenu(cpt, idx);
		} else {
			const vdpoint32& cpt = TransformScreenToClient(vdpoint32(x, y));
			const int idx = PointToItem(cpt);

			SetFocusedItem(idx);
			mpFnContextMenu(cpt, idx);
		}
	}

	return false;
}

VDZLRESULT VDUIProxyListBoxControl::ListBoxWndProc(VDZHWND hwnd, VDZUINT msg, VDZWPARAM wParam, VDZLPARAM lParam) {
	switch(msg) {
		case WM_LBUTTONDOWN:
			{
				int prevSel = (int)CallWindowProcW((WNDPROC)mPrevWndProc, hwnd, LB_GETCURSEL, 0, 0);
				LRESULT r = CallWindowProcW((WNDPROC)mPrevWndProc, hwnd, msg, wParam, lParam);
				int nextSel = (int)CallWindowProcW((WNDPROC)mPrevWndProc, hwnd, LB_GETCURSEL, 0, 0);

				CancelEditTimer();

				if (nextSel == prevSel && nextSel >= 0) {
					RECT r {};
					CallWindowProcW((WNDPROC)mPrevWndProc, hwnd, LB_GETITEMRECT, (WPARAM)nextSel, (LPARAM)&r);

					POINT pt = { (SHORT)LOWORD(lParam), (SHORT)HIWORD(lParam) };
					if (PtInRect(&r, pt)) {
						if (!mpEditTimerThunk)
							mpEditTimerThunk = VDCreateFunctionThunkFromMethod(this, &VDUIProxyListBoxControl::AutoEditTimerProc, true);

						if (mpEditTimerThunk)
							mAutoEditTimer = SetTimer(NULL, 0, 1000, VDGetThunkFunction<TIMERPROC>(mpEditTimerThunk));
					}
				}

				return r;
			}

		case WM_KEYDOWN:
			if (wParam == VK_F2) {
				int sel = (int)CallWindowProcW((WNDPROC)mPrevWndProc, hwnd, LB_GETCURSEL, 0, 0);

				if (sel >= 0)
					EditItem(sel);
				return 0;
			}
			break;

		case WM_KEYUP:
			if (wParam == VK_F2)
				return 0;
			break;

		case WM_RBUTTONDOWN:
		case WM_MBUTTONDOWN:
		case WM_XBUTTONDOWN:
		case WM_LBUTTONDBLCLK:
		case WM_RBUTTONDBLCLK:
		case WM_MBUTTONDBLCLK:
		case WM_XBUTTONDBLCLK:
			CancelEditTimer();
			break;
	}

	return CallWindowProcW((WNDPROC)mPrevWndProc, hwnd, msg, wParam, lParam);
}

VDZLRESULT VDUIProxyListBoxControl::LabelEditWndProc(VDZHWND hwnd, VDZUINT msg, VDZWPARAM wParam, VDZLPARAM lParam) {
	switch(msg) {
		case WM_GETDLGCODE:
			return DLGC_WANTALLKEYS;
			break;

		case WM_KEYDOWN:
			if (wParam == VK_RETURN) {
				VDStringW s = VDGetWindowTextW32(hwnd);
				EndEditItem();

				if (mpFnItemEdited)
					mpFnItemEdited(mEditItem, s.c_str());

				return 0;
			} else if (wParam == VK_ESCAPE) {
				EndEditItem();

				if (mpFnItemEdited)
					mpFnItemEdited(mEditItem, nullptr);
				return 0;
			}
			break;

		case WM_KILLFOCUS:
			if (wParam != (WPARAM)hwnd){
				VDStringW s = VDGetWindowTextW32(hwnd);
				EndEditItem();

				if (mpFnItemEdited)
					mpFnItemEdited(mEditItem, s.c_str());
			}
			return 0;

		case WM_MOUSEACTIVATE:
			return MA_NOACTIVATE;
	}

	return CallWindowProcW((WNDPROC)mPrevEditWndProc, hwnd, msg, wParam, lParam);
}

void VDUIProxyListBoxControl::AutoEditTimerProc(VDZHWND, VDZUINT, VDZUINT_PTR, VDZDWORD) {
	CancelEditTimer();

	int idx = (int)CallWindowProcW((WNDPROC)mPrevWndProc, mhwnd, LB_GETCURSEL, 0, 0);

	if (idx >= 0)
		EditItem(idx);
}

///////////////////////////////////////////////////////////////////////////

// Default for CB_SETMINVISIBLE per MSDN docs
const uint32 VDUIProxyComboBoxControl::kDefaultMinVisibleCount = 30;

VDUIProxyComboBoxControl::VDUIProxyComboBoxControl() {
}

VDUIProxyComboBoxControl::~VDUIProxyComboBoxControl() {
}

void VDUIProxyComboBoxControl::Clear() {
	if (!mhwnd)
		return;

	SendMessageW(mhwnd, CB_RESETCONTENT, 0, 0);
}

void VDUIProxyComboBoxControl::AddItem(const wchar_t *s) {
	if (!mhwnd)
		return;

	SendMessageW(mhwnd, CB_ADDSTRING, 0, (LPARAM)s);
}

void VDUIProxyComboBoxControl::InsertItem(int index, const wchar_t *s) {
	if (!mhwnd)
		return;

	SendMessageW(mhwnd, CB_INSERTSTRING, index, (LPARAM)s);
}

void VDUIProxyComboBoxControl::DeleteItem(int index) {
	if (!mhwnd)
		return;

	SendMessageW(mhwnd, CB_DELETESTRING, index, 0);
}

int VDUIProxyComboBoxControl::GetItemCount() const {
	if (!mhwnd)
		return 0;

	return SendMessageW(mhwnd, CB_GETCOUNT, 0, 0);
}

int VDUIProxyComboBoxControl::GetSelection() const {
	if (!mhwnd)
		return -1;

	return (int)SendMessage(mhwnd, CB_GETCURSEL, 0, 0);
}

void VDUIProxyComboBoxControl::SetSelection(int index) {
	if (mhwnd)
		SendMessage(mhwnd, CB_SETCURSEL, index, 0);
}

void VDUIProxyComboBoxControl::DeselectText() {
	if (mhwnd)
		SendMessageW(mhwnd, CB_SETEDITSEL, 0, MAKELONG(-1, 0));
}

void VDUIProxyComboBoxControl::SetOnSelectionChanged(vdfunction<void(int)> fn) {
	mpOnSelectionChangedFn = std::move(fn);
}

void VDUIProxyComboBoxControl::SetOnFocusLost(vdfunction<void()> fn) {
	mpOnFocusLostFn = std::move(fn);
}

void VDUIProxyComboBoxControl::SetOnEndEdit(vdfunction<bool(const wchar_t *)> fn) {
	mpOnEndEditFn = std::move(fn);
}

void VDUIProxyComboBoxControl::Attach(VDZHWND hwnd) {
	VDUIProxyControl::Attach(hwnd);

	COMBOBOXINFO cbi { sizeof(COMBOBOXINFO) };

	LONG style = GetWindowLong(hwnd, GWL_STYLE);

	if ((style & 3) == CBS_DROPDOWN && GetComboBoxInfo(hwnd, &cbi)) {
		mhwndEdit = cbi.hwndItem;

		if (mhwndEdit) {
			SetWindowSubclass(
				mhwndEdit,
				[](HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR subclassId, DWORD_PTR refData) -> LPARAM {
					VDUIProxyComboBoxControl *self = (VDUIProxyComboBoxControl *)refData;

					switch(msg) {
						case WM_KEYDOWN:
							if (wParam == VK_RETURN) {
								if (self->mpOnEndEditFn) {
									self->mpOnEndEditFn(self->GetCaption().c_str());

									// eat return
									return 0;
								}
							}
							break;

						case WM_CHAR:
							if (wParam == '\r')
								return 0;

							break;

						default:
							break;
					}

					return DefSubclassProc(hwnd, msg, wParam, lParam);
				},
				1,
				(DWORD_PTR)this
			);
		}
	}

	SetWindowSubclass(
		hwnd,
		[](HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR subclassId, DWORD_PTR refData) -> LPARAM {
			if (ATUIIsDarkThemeActive()) {
				switch(msg) {
					case WM_ERASEBKGND:
						return FALSE;

					case WM_PAINT: {
						PAINTSTRUCT ps;
						HDC hdc = BeginPaint(hwnd, &ps);

						if (hdc) {
							int cx = ATUIGetDpiScaledSystemMetricForWindowW32(hwnd, SM_CXEDGE)*2;
							int cy = ATUIGetDpiScaledSystemMetricForWindowW32(hwnd, SM_CYEDGE)*2;

							if (RECT r; GetClientRect(hwnd, &r)) {
								const ATUIThemeColorsW32& theme = ATUIGetThemeColorsW32();

								FillRect(hdc, &r, IsWindowEnabled(hwnd) ? theme.mComboBoxBgBrush : theme.mStaticBgBrush);

								RECT rTop { r.left, r.top, r.right, r.top + 1 };
								RECT rLeft { r.left, r.top + 1, r.left + 1, r.bottom - 1 };
								RECT rRight { r.right - 1, r.top + 1, r.right, r.bottom - 1 };
								RECT rBottom { r.left, r.bottom - 1, r.right, r.bottom};

								FillRect(hdc, &rTop, theme.mComboBoxBorderBrush);
								FillRect(hdc, &rLeft, theme.mComboBoxBorderBrush);
								FillRect(hdc, &rRight, theme.mComboBoxBorderBrush);
								FillRect(hdc, &rBottom, theme.mComboBoxBorderBrush);

								IntersectClipRect(hdc, cx, cy, r.right - cx, r.bottom - cy);

								COMBOBOXINFO cbi {};
								cbi.cbSize = sizeof(COMBOBOXINFO);

								if (GetComboBoxInfo(hwnd, &cbi)) {
									ExcludeClipRect(hdc, cbi.rcItem.right, 0, cbi.rcButton.left, r.bottom);
								}
							}

							DefSubclassProc(hwnd, msg, (WPARAM)hdc, lParam);
							EndPaint(hwnd, &ps);
						}

						return 0;
					}

					case WM_CTLCOLOREDIT:
						if (ATUIIsDarkThemeActive()) {
							const auto& tcw32 = ATUIGetThemeColorsW32();
							HDC hdc = (HDC)wParam;

							SetTextColor(hdc, tcw32.mStaticFgCRef);
							SetBkColor(hdc, tcw32.mComboBoxBgCRef);

							return (INT_PTR)tcw32.mComboBoxBgBrush;
						}
						break;
				}
			}

			return DefSubclassProc(hwnd, msg, wParam, lParam);
		},
		1,
		(DWORD_PTR)this
	);
}

VDZLRESULT VDUIProxyComboBoxControl::On_WM_COMMAND(VDZWPARAM wParam, VDZLPARAM lParam) {
	if (HIWORD(wParam) == CBN_SELCHANGE) {
		const int idx = GetSelection();

		if (mpOnSelectionChangedFn)
			mpOnSelectionChangedFn(idx);

		mSelectionChanged.Raise(this, idx);
	} else if (HIWORD(wParam) == CBN_SELENDOK) {
		if (mpOnEndEditFn) {
			int index = GetSelection();

			if (index >= 0) {
				int len = SendMessage(mhwnd, CB_GETLBTEXTLEN, index, 0);

				if (len >= 0) {
					VDStringW str;
					str.resize(len + 1);

					int actualLen = SendMessage(mhwnd, CB_GETLBTEXT, index, (LPARAM)str.data());

					if (actualLen >= 0 && actualLen <= len) {
						str.resize(actualLen);

						mpOnEndEditFn(str.c_str());
					}
				}
			}
		}
	} else if (HIWORD(wParam) == CBN_KILLFOCUS) {
		if (mpOnFocusLostFn)
			mpOnFocusLostFn();
	}

	return 0;
}

void VDUIProxyComboBoxControl::OnRedrawSuspend() {
	// Drop SetMinVisible to prevent the combo box from spending time resizing the list box
	// every time an item is added.
	SendMessage(mhwnd, CB_SETMINVISIBLE, 1, 0);
}

void VDUIProxyComboBoxControl::OnRedrawResume() {
	SendMessage(mhwnd, CB_SETMINVISIBLE, kDefaultMinVisibleCount, 0);
}

///////////////////////////////////////////////////////////////////////////

// Default for CB_SETMINVISIBLE per MSDN docs
const uint32 VDUIProxyComboBoxExControl::kDefaultMinVisibleCount = 30;

VDUIProxyComboBoxExControl::VDUIProxyComboBoxExControl() {
}

VDUIProxyComboBoxExControl::~VDUIProxyComboBoxExControl() {
}

void VDUIProxyComboBoxExControl::Clear() {
	if (!mhwnd)
		return;

	SendMessageW(mhwnd, CB_RESETCONTENT, 0, 0);
}

void VDUIProxyComboBoxExControl::AddItem(const wchar_t *s) {
	if (!mhwnd)
		return;

	COMBOBOXEXITEM item {};
	item.mask = CBEIF_TEXT;
	item.iItem = -1;
	item.pszText = (LPWSTR)s;
	SendMessage(mhwnd, CBEM_INSERTITEM, 0, (LPARAM)&item);
}

int VDUIProxyComboBoxExControl::GetSelection() const {
	if (!mhwnd)
		return -1;

	return (int)SendMessage(mhwnd, CB_GETCURSEL, 0, 0);
}

void VDUIProxyComboBoxExControl::SetSelection(int index) {
	if (mhwnd)
		SendMessage(mhwnd, CB_SETCURSEL, index, 0);
}

void VDUIProxyComboBoxExControl::DeselectText() {
	if (mhwnd)
		SendMessageW(mhwnd, CB_SETEDITSEL, 0, MAKELONG(-1, 0));
}

void VDUIProxyComboBoxExControl::SetOnSelectionChanged(vdfunction<void(int)> fn) {
	mpOnSelectionChangedFn = std::move(fn);
}

void VDUIProxyComboBoxExControl::SetOnEndEdit(vdfunction<bool(const wchar_t *)> fn) {
	mpOnEndEditFn = std::move(fn);
}

void VDUIProxyComboBoxExControl::Attach(VDZHWND hwnd) {
	VDUIProxyControl::Attach(hwnd);

	if (ATUIIsDarkThemeActive()) {
		SetWindowSubclass(
			hwnd,
			[](HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR subclassId, DWORD_PTR refData) -> LPARAM {
				switch(msg) {
					case WM_ERASEBKGND:
						return FALSE;

					case WM_PAINT: {
						PAINTSTRUCT ps;
						HDC hdc = BeginPaint(hwnd, &ps);

						if (hdc) {
							int cx = ATUIGetDpiScaledSystemMetricForWindowW32(hwnd, SM_CXEDGE)*2;
							int cy = ATUIGetDpiScaledSystemMetricForWindowW32(hwnd, SM_CYEDGE)*2;

							if (RECT r; GetClientRect(hwnd, &r)) {
								const ATUIThemeColorsW32& theme = ATUIGetThemeColorsW32();

								SetDCBrushColor(hdc, VDSwizzleU32(ATUIGetThemeColors().mComboBoxBg) >> 8);
								FillRect(hdc, &r, (HBRUSH)GetStockObject(DC_BRUSH));

								RECT rTop { r.left, r.top, r.right, r.top + 1 };
								RECT rLeft { r.left, r.top + 1, r.left + 1, r.bottom - 1 };
								RECT rRight { r.right - 1, r.top + 1, r.right, r.bottom - 1 };
								RECT rBottom { r.left, r.bottom - 1, r.right, r.bottom};

								FillRect(hdc, &rTop, theme.mComboBoxBorderBrush);
								FillRect(hdc, &rLeft, theme.mComboBoxBorderBrush);
								FillRect(hdc, &rRight, theme.mComboBoxBorderBrush);
								FillRect(hdc, &rBottom, theme.mComboBoxBorderBrush);

								IntersectClipRect(hdc, cx, cy, r.right - cx, r.bottom - cy);

								COMBOBOXINFO cbi {};
								cbi.cbSize = sizeof(COMBOBOXINFO);

								if (GetComboBoxInfo(hwnd, &cbi)) {
									ExcludeClipRect(hdc, cbi.rcItem.right, 0, cbi.rcButton.left, r.bottom);
								}
							}

							DefSubclassProc(hwnd, msg, (WPARAM)hdc, lParam);
							EndPaint(hwnd, &ps);
						}

						return 0;
					}

					case WM_CTLCOLOREDIT:
						if (ATUIIsDarkThemeActive()) {
							const auto& tcw32 = ATUIGetThemeColorsW32();
							HDC hdc = (HDC)wParam;

							SetTextColor(hdc, tcw32.mStaticFgCRef);
							SetBkColor(hdc, tcw32.mComboBoxBgCRef);

							return (INT_PTR)tcw32.mComboBoxBgBrush;
						}
						break;
				}

				return DefSubclassProc(hwnd, msg, wParam, lParam);
			},
			1,
			(DWORD_PTR)nullptr
		);
	}
}

VDZLRESULT VDUIProxyComboBoxExControl::On_WM_COMMAND(VDZWPARAM wParam, VDZLPARAM lParam) {
	if (HIWORD(wParam) == CBN_SELCHANGE) {
		const int idx = GetSelection();

		if (mpOnSelectionChangedFn)
			mpOnSelectionChangedFn(idx);
	}

	return 0;
}

VDZLRESULT VDUIProxyComboBoxExControl::On_WM_NOTIFY(VDZWPARAM wParam, VDZLPARAM lParam) {
	const NMHDR *hdr = (const NMHDR *)lParam;

	if (hdr->code == CBEN_ENDEDIT) {
		const NMCBEENDEDIT *info = (const NMCBEENDEDIT *)hdr;

		if (mpOnEndEditFn && info->iWhy == CBENF_RETURN)
			return mpOnEndEditFn(info->szText) ? FALSE : TRUE;
	}

	return 0;
}

void VDUIProxyComboBoxExControl::OnRedrawSuspend() {
	// Drop SetMinVisible to prevent the combo box from spending time resizing the list box
	// every time an item is added.
	SendMessage(mhwnd, CB_SETMINVISIBLE, 1, 0);
}

void VDUIProxyComboBoxExControl::OnRedrawResume() {
	SendMessage(mhwnd, CB_SETMINVISIBLE, kDefaultMinVisibleCount, 0);
}

///////////////////////////////////////////////////////////////////////////

const VDUIProxyTreeViewControl::NodeRef VDUIProxyTreeViewControl::kNodeRoot = (NodeRef)TVI_ROOT;
const VDUIProxyTreeViewControl::NodeRef VDUIProxyTreeViewControl::kNodeFirst = (NodeRef)TVI_FIRST;
const VDUIProxyTreeViewControl::NodeRef VDUIProxyTreeViewControl::kNodeLast = (NodeRef)TVI_LAST;

VDUIProxyTreeViewControl::VDUIProxyTreeViewControl()
	: mNextTextIndex(0)
	, mhfontBold(NULL)
	, mbCreatedBoldFont(false)
	, mbIndexedMode(false)
	, mpIndexedProvider(nullptr)
	, mpEditWndProcThunk(NULL)
{
}

VDUIProxyTreeViewControl::~VDUIProxyTreeViewControl() {
	if (mpEditWndProcThunk) {
		SendMessageW(mhwnd, TVM_ENDEDITLABELNOW, TRUE, 0);

		VDDestroyFunctionThunk(mpEditWndProcThunk);
		mpEditWndProcThunk = NULL;
	}
}

void VDUIProxyTreeViewControl::SetIndexedProvider(IVDUITreeViewIndexedProvider *p) {
	mbIndexedMode = true;
	mpIndexedProvider = p;
}

IVDUITreeViewVirtualItem *VDUIProxyTreeViewControl::GetSelectedVirtualItem() const {
	if (!mhwnd)
		return NULL;

	HTREEITEM hti = TreeView_GetSelection(mhwnd);

	if (!hti)
		return NULL;

	TVITEMW itemw = {0};

	itemw.mask = LVIF_PARAM;
	itemw.hItem = hti;

	SendMessageW(mhwnd, TVM_GETITEMW, 0, (LPARAM)&itemw);
	return (IVDUITreeViewVirtualItem *)itemw.lParam;
}

uint32 VDUIProxyTreeViewControl::GetSelectedItemId() const {
	if (!mhwnd)
		return 0;

	HTREEITEM hti = TreeView_GetSelection(mhwnd);

	if (!hti)
		return 0;

	TVITEMW itemw = {0};

	itemw.mask = LVIF_PARAM;
	itemw.hItem = hti;

	SendMessageW(mhwnd, TVM_GETITEMW, 0, (LPARAM)&itemw);
	return (uint32)itemw.lParam;
}

IVDUITreeViewVirtualItem *VDUIProxyTreeViewControl::GetVirtualItem(NodeRef ref) const {
	if (!mhwnd || !ref)
		return NULL;

	TVITEMW itemw = {0};

	itemw.mask = LVIF_PARAM;
	itemw.hItem = (HTREEITEM)ref;

	SendMessageW(mhwnd, TVM_GETITEMW, 0, (LPARAM)&itemw);

	return (IVDUITreeViewVirtualItem *)itemw.lParam;
}

uint32 VDUIProxyTreeViewControl::GetItemId(NodeRef ref) const {
	if (!mhwnd)
		return 0;

	TVITEMW itemw = {0};

	itemw.mask = LVIF_PARAM;
	itemw.hItem = (HTREEITEM)ref;

	SendMessageW(mhwnd, TVM_GETITEMW, 0, (LPARAM)&itemw);

	return (uint32)itemw.lParam;
}

void VDUIProxyTreeViewControl::Clear() {
	if (mhwnd) {
		TreeView_DeleteAllItems(mhwnd);
	}
}

void VDUIProxyTreeViewControl::DeleteItem(NodeRef ref) {
	if (mhwnd) {
		TreeView_DeleteItem(mhwnd, (HTREEITEM)ref);
	}
}

VDUIProxyTreeViewControl::NodeRef VDUIProxyTreeViewControl::AddItem(NodeRef parent, NodeRef insertAfter, const wchar_t *label) {
	if (!mhwnd)
		return kNodeNull;

	TVINSERTSTRUCTW isw = { 0 };

	isw.hParent = (HTREEITEM)parent;
	isw.hInsertAfter = (HTREEITEM)insertAfter;
	isw.item.mask = TVIF_TEXT | TVIF_PARAM;
	isw.item.pszText = (LPWSTR)label;
	isw.item.lParam = 0;

	return (NodeRef)SendMessageW(mhwnd, TVM_INSERTITEMW, 0, (LPARAM)&isw);
}

VDUIProxyTreeViewControl::NodeRef VDUIProxyTreeViewControl::AddVirtualItem(NodeRef parent, NodeRef insertAfter, IVDUITreeViewVirtualItem *item) {
	VDASSERT(!mbIndexedMode);

	if (!mhwnd)
		return kNodeNull;

	HTREEITEM hti;

	TVINSERTSTRUCTW isw = { 0 };

	isw.hParent = (HTREEITEM)parent;
	isw.hInsertAfter = (HTREEITEM)insertAfter;
	isw.item.mask = TVIF_PARAM | TVIF_TEXT;
	isw.item.lParam = (LPARAM)item;
	isw.item.pszText = LPSTR_TEXTCALLBACKW;

	hti = (HTREEITEM)SendMessageW(mhwnd, TVM_INSERTITEMW, 0, (LPARAM)&isw);

	if (hti) {
		item->AddRef();
		item->SetTreeNode((NodeRef)hti);

		if (parent != kNodeRoot) {
			TreeView_Expand(mhwnd, (HTREEITEM)parent, TVE_EXPAND);
		}
	}

	return (NodeRef)hti;
}

VDUIProxyTreeViewControl::NodeRef VDUIProxyTreeViewControl::AddIndexedItem(NodeRef parent, NodeRef insertAfter, uint32 id) {
	VDASSERT(mbIndexedMode);

	if (!mhwnd)
		return kNodeNull;

	HTREEITEM hti;

	TVINSERTSTRUCTW isw = { 0 };

	isw.hParent = (HTREEITEM)parent;
	isw.hInsertAfter = (HTREEITEM)insertAfter;
	isw.item.mask = TVIF_PARAM | TVIF_TEXT;
	isw.item.lParam = (LPARAM)id;
	isw.item.pszText = LPSTR_TEXTCALLBACKW;

	hti = (HTREEITEM)SendMessageW(mhwnd, TVM_INSERTITEMW, 0, (LPARAM)&isw);

	if (hti && parent != kNodeRoot)
		TreeView_Expand(mhwnd, (HTREEITEM)parent, TVE_EXPAND);

	return (NodeRef)hti;
}

VDUIProxyTreeViewControl::NodeRef VDUIProxyTreeViewControl::AddHiddenNode(NodeRef parent, NodeRef insertAfter) {
	if (!mhwnd)
		return kNodeNull;

	TVINSERTSTRUCTW isw = { 0 };

	isw.hParent = (HTREEITEM)parent;
	isw.hInsertAfter = (HTREEITEM)insertAfter;
	isw.itemex.mask = TVIF_STATEEX;
	isw.itemex.uStateEx = TVIS_EX_FLAT;

	HTREEITEM hti = (HTREEITEM)SendMessageW(mhwnd, TVM_INSERTITEMW, 0, (LPARAM)&isw);

	return (NodeRef)hti;
}

VDUIProxyTreeViewControl::NodeRef VDUIProxyTreeViewControl::GetRootNode() const {
	if (mhwnd)
		return (NodeRef)TreeView_GetRoot(mhwnd);
	else
		return kNodeNull;
}

VDUIProxyTreeViewControl::NodeRef VDUIProxyTreeViewControl::GetChildNode(NodeRef ref, uint32 index) const {
	if (!mhwnd)
		return kNodeNull;

	ref = (NodeRef)TreeView_GetChild(mhwnd, (HTREEITEM)ref);

	while(ref && index) {
		index--;

		ref = (NodeRef)TreeView_GetNextSibling(mhwnd, (HTREEITEM)ref);
	}

	return ref;
}

VDUIProxyTreeViewControl::NodeRef VDUIProxyTreeViewControl::GetParentNode(NodeRef ref) const {
	if (mhwnd)
		return (NodeRef)TreeView_GetParent(mhwnd, (HTREEITEM)ref);
	else
		return kNodeNull;
}

VDUIProxyTreeViewControl::NodeRef VDUIProxyTreeViewControl::GetPrevNode(NodeRef ref) const {
	if (mhwnd)
		return (NodeRef)TreeView_GetPrevSibling(mhwnd, (HTREEITEM)ref);
	else
		return kNodeNull;
}

VDUIProxyTreeViewControl::NodeRef VDUIProxyTreeViewControl::GetNextNode(NodeRef ref) const {
	if (mhwnd)
		return (NodeRef)TreeView_GetNextSibling(mhwnd, (HTREEITEM)ref);
	else
		return kNodeNull;
}

VDUIProxyTreeViewControl::NodeRef VDUIProxyTreeViewControl::GetSelectedNode() const {
	if (mhwnd)
		return (NodeRef)TreeView_GetSelection(mhwnd);
	else
		return kNodeNull;
}

void VDUIProxyTreeViewControl::MakeNodeVisible(NodeRef node) {
	if (mhwnd) {
		TreeView_EnsureVisible(mhwnd, (HTREEITEM)node);
	}
}

void VDUIProxyTreeViewControl::SelectNode(NodeRef node) {
	if (mhwnd && IsValidNodeRef(node)) {
		TreeView_SelectItem(mhwnd, (HTREEITEM)node);
	}
}

void VDUIProxyTreeViewControl::RefreshNode(NodeRef node) {
	VDASSERT(node);

	if (mhwnd) {
		TVITEMW itemw = {0};

		itemw.mask = LVIF_PARAM;
		itemw.hItem = (HTREEITEM)node;

		SendMessageW(mhwnd, TVM_GETITEMW, 0, (LPARAM)&itemw);

		if (itemw.lParam) {
			itemw.mask = LVIF_TEXT;
			itemw.pszText = LPSTR_TEXTCALLBACKW;
			SendMessageW(mhwnd, TVM_SETITEMW, 0, (LPARAM)&itemw);
		}
	}
}

void VDUIProxyTreeViewControl::ExpandNode(NodeRef node, bool expanded) {
	VDASSERT(node);

	if (!mhwnd)
		return;

	TreeView_Expand(mhwnd, (HTREEITEM)node, expanded ? TVE_EXPAND : TVE_COLLAPSE);
}

void VDUIProxyTreeViewControl::EditNodeLabel(NodeRef node) {
	if (!mhwnd)
		return;

	::SetFocus(mhwnd);
	::SendMessageW(mhwnd, TVM_EDITLABELW, 0, (LPARAM)(HTREEITEM)node);
}

void VDUIProxyTreeViewControl::MoveVirtualNodes(NodeRef newParent, NodeRef node) {
	if (!mhwnd)
		return;

	struct NodeInfo {
		vdrefptr<IVDUITreeViewVirtualItem> mpVirtualItem;
		int parentIndex = -1;
		TVITEMW itemw {};
	};

	vdvector<NodeInfo> nodes;

	const auto processNode = [&](NodeRef node2, int parentIndex, const auto& self) -> void {
		auto& nodeInfo = nodes.emplace_back();

		nodeInfo.parentIndex = parentIndex;
		nodeInfo.itemw.mask = TVIF_HANDLE | TVIF_IMAGE | TVIF_PARAM | TVIF_SELECTEDIMAGE | TVIF_STATE;
		nodeInfo.itemw.hItem = (HTREEITEM)node2;
		nodeInfo.itemw.stateMask = TVIS_OVERLAYMASK | TVIS_STATEIMAGEMASK;

		if (TreeView_GetItem(mhwnd, &nodeInfo.itemw)) {
			nodeInfo.mpVirtualItem = (IVDUITreeViewVirtualItem *)nodeInfo.itemw.lParam;

			parentIndex = (int)nodes.size() - 1;

			HTREEITEM child = TreeView_GetChild(mhwnd, (HTREEITEM)node2);
			while(child) {
				self((NodeRef)child, parentIndex, self);

				child = TreeView_GetNextSibling(mhwnd, child);
			}
		} else {
			nodes.pop_back();
		}
	};

	// gather all nodes
	processNode(node, -1, processNode);

	// destroy nodes child first
	for(const NodeInfo& nodeInfo : std::ranges::views::reverse(nodes)) {
		TreeView_DeleteItem(mhwnd, nodeInfo.itemw.hItem);
	}

	// reconstruct node tree at new parent
	for(NodeInfo& nodeInfo : nodes) {
		TVINSERTSTRUCTW isw {};

		if (nodeInfo.parentIndex < 0) {
			isw.hParent = (HTREEITEM)newParent;
		} else {
			isw.hParent = nodes[nodeInfo.parentIndex].itemw.hItem;
			if (!isw.hParent)
				continue;
		}

		isw.hInsertAfter = TVI_LAST;
		isw.item = nodeInfo.itemw;
		isw.item.mask = TVIF_IMAGE | TVIF_PARAM | TVIF_SELECTEDIMAGE | TVIF_STATE | TVIF_TEXT;
		isw.item.pszText = LPSTR_TEXTCALLBACKW;

		nodeInfo.itemw.hItem = TreeView_InsertItem(mhwnd, &isw);
		if (nodeInfo.itemw.hItem && nodeInfo.mpVirtualItem) {
			nodeInfo.mpVirtualItem->SetTreeNode((NodeRef)(nodeInfo.itemw.hItem));
			nodeInfo.mpVirtualItem.release();
		}
	}
}

namespace {
	int CALLBACK TreeNodeCompareFn(LPARAM node1, LPARAM node2, LPARAM comparer) {
		if (!node1)
			return node2 ? -1 : 0;

		if (!node2)
			return 1;

		return ((IVDUITreeViewVirtualItemComparer *)comparer)->Compare(
			*(IVDUITreeViewVirtualItem *)node1,
			*(IVDUITreeViewVirtualItem *)node2);
	}
}

bool VDUIProxyTreeViewControl::HasChildren(NodeRef parent) const {
	return mhwnd && TreeView_GetChild(mhwnd, parent) != NULL;
}

void VDUIProxyTreeViewControl::EnumChildren(NodeRef parent, const vdfunction<void(IVDUITreeViewVirtualItem *)>& callback) const {
	if (!mhwnd)
		return;

	TVITEMW itemw = {0};
	itemw.mask = TVIF_PARAM;

	HTREEITEM hti = TreeView_GetChild(mhwnd, parent);
	while(hti) {
		itemw.hItem = hti;
		if (SendMessageW(mhwnd, TVM_GETITEMW, 0, (LPARAM)&itemw))
			callback((IVDUITreeViewVirtualItem *)itemw.lParam);

		hti = TreeView_GetNextSibling(mhwnd, hti);
	}
}

void VDUIProxyTreeViewControl::EnumChildrenRecursive(NodeRef parent, const vdfunction<void(IVDUITreeViewVirtualItem *)>& callback) const {
	if (!mhwnd)
		return;

	HTREEITEM current = TreeView_GetChild(mhwnd, (HTREEITEM)parent);
	if (!current)
		return;

	vdfastvector<HTREEITEM> traversalStack;

	TVITEMW itemw = {0};
	itemw.mask = TVIF_PARAM;
	for(;;) {
		while(current) {
			itemw.hItem = current;
			if (SendMessageW(mhwnd, TVM_GETITEMW, 0, (LPARAM)&itemw) && itemw.lParam)
				callback((IVDUITreeViewVirtualItem *)itemw.lParam);

			HTREEITEM firstChild = TreeView_GetChild(mhwnd, current);
			if (firstChild)
				traversalStack.push_back(firstChild);

			current = TreeView_GetNextSibling(mhwnd, current);
		}

		if (traversalStack.empty())
			break;

		current = traversalStack.back();
		traversalStack.pop_back();
	}
}

void VDUIProxyTreeViewControl::SortChildren(NodeRef parent, IVDUITreeViewVirtualItemComparer& comparer) {
	if (!mhwnd)
		return;

	TVSORTCB scb;
	scb.hParent = (HTREEITEM)parent;
	scb.lParam = (LPARAM)&comparer;
	scb.lpfnCompare = TreeNodeCompareFn;

	SendMessageW(mhwnd, TVM_SORTCHILDRENCB, 0, (LPARAM)&scb);
}

void VDUIProxyTreeViewControl::InitImageList(uint32 n, uint32 width, uint32 height) {
	if (!mhwnd)
		return;

	if (!width || !height) {
		width = 16;

		if (HFONT hfont = (HFONT)SendMessage(mhwnd, WM_GETFONT, 0, 0)) {
			if (HDC hdc = GetDC(mhwnd)) {
				if (HGDIOBJ hOldFont = SelectObject(hdc, hfont)) {
					TEXTMETRICW tm = {};
					
					if (GetTextMetricsW(hdc, &tm)) {
						width = tm.tmAscent + tm.tmDescent;
					}

					SelectObject(hdc, hOldFont);
				}

				ReleaseDC(mhwnd, hdc);
			}
		}

		height = width;
	}

	mImageWidth = width;
	mImageHeight = height;
	HIMAGELIST imageList = ImageList_Create(width, height, ILC_COLOR32, 0, n);

	SendMessage(mhwnd, TVM_SETIMAGELIST, TVSIL_STATE, (LPARAM)imageList);

	if (mImageList)
		ImageList_Destroy(mImageList);

	mImageList = imageList;

	uint32 c = 0;
	VDPixmap pxEmpty {};
	pxEmpty.format = nsVDPixmap::kPixFormat_XRGB8888;
	pxEmpty.data = &c;
	pxEmpty.w = 1;
	pxEmpty.h = 1;
	AddImage(pxEmpty);
}

void VDUIProxyTreeViewControl::AddImage(const VDPixmap& px) {
	AddImagesToImageList(mImageList, px, mImageWidth, mImageHeight, 0);
}

void VDUIProxyTreeViewControl::AddImages(uint32 n, const VDPixmap& px) {
	if (!mhwnd || !n)
		return;

	VDASSERT(px.w % n == 0);
	uint32 imageWidth = px.w / n;

	for(uint32 i=0; i<n; ++i) {
		AddImage(VDPixmapClip(px, imageWidth * i, 0, imageWidth, px.h));
	}
}

void VDUIProxyTreeViewControl::SetNodeImage(NodeRef node, uint32 imageIndex) {
	if (!mhwnd || !node)
		return;

	TVITEMEX tvi {};
	tvi.hItem = (HTREEITEM)node;
	tvi.mask = TVIF_STATE;
	tvi.state = INDEXTOSTATEIMAGEMASK(imageIndex);
	tvi.stateMask = TVIS_STATEIMAGEMASK;
	SendMessage(mhwnd, TVM_SETITEM, 0, (LPARAM)&tvi);
}

void VDUIProxyTreeViewControl::SetOnItemSelectionChanged(vdfunction<void()> fn) {
	mpOnItemSelectionChanged = fn;
}

void VDUIProxyTreeViewControl::SetOnBeginDrag(const vdfunction<void(const BeginDragEvent& event)>& fn) {
	mpOnBeginDrag = fn;
}

VDUIProxyTreeViewControl::NodeRef VDUIProxyTreeViewControl::FindDropTarget() const {
	POINT pt;
	if (!mhwnd || !GetCursorPos(&pt))
		return kNodeNull;

	if (!ScreenToClient(mhwnd, &pt))
		return kNodeNull;

	TVHITTESTINFO hti = {};
	hti.pt = pt;

	return (NodeRef)TreeView_HitTest(mhwnd, &hti);
}

void VDUIProxyTreeViewControl::SetDropTargetHighlight(NodeRef item) {
	if (mhwnd) {
		TreeView_SelectDropTarget(mhwnd, item);
	}
}

void VDUIProxyTreeViewControl::SetOnContextMenu(vdfunction<bool(const ContextMenuEvent& event)> fn) {
	mpOnContextMenu = std::move(fn);
}

void VDUIProxyTreeViewControl::Attach(VDZHWND hwnd) {
	VDUIProxyControl::Attach(hwnd);

	if (mhwnd) {
		SendMessageW(mhwnd, CCM_SETVERSION, 6, 0);

		if (ATUIIsDarkThemeActive()) {
			const auto& tc = ATUIGetThemeColors();
			const COLORREF fg = (COLORREF)(VDSwizzleU32(tc.mContentFg) >> 8);

			TreeView_SetBkColor(mhwnd, (COLORREF)(VDSwizzleU32(tc.mContentBg) >> 8));
			TreeView_SetTextColor(mhwnd, fg);
			TreeView_SetLineColor(mhwnd, fg);
		} else {
			TreeView_SetBkColor(mhwnd, (COLORREF)-1);
			TreeView_SetTextColor(mhwnd, (COLORREF)-1);
			TreeView_SetLineColor(mhwnd, CLR_DEFAULT);
		}
	}
}

void VDUIProxyTreeViewControl::Detach() {
	DeleteFonts();

	if (mImageList) {
		if (mhwnd)
			SendMessage(mhwnd, TB_SETIMAGELIST, 0, 0);

		ImageList_Destroy(mImageList);
		mImageList = nullptr;
	}

	if (!mbIndexedMode) {
		EnumChildrenRecursive(kNodeRoot,
			[](IVDUITreeViewVirtualItem *vi) {
				vi->SetTreeNode({});
				vi->Release();
			}
		);
	}
}

VDZLRESULT VDUIProxyTreeViewControl::On_WM_NOTIFY(VDZWPARAM wParam, VDZLPARAM lParam) {
	const NMHDR *hdr = (const NMHDR *)lParam;

	switch(hdr->code) {
		case TVN_GETDISPINFOA:
			{
				NMTVDISPINFOA *dispa = (NMTVDISPINFOA *)hdr;

				mTextW[0].clear();

				if (mbIndexedMode) {
					if (mpIndexedProvider)
						mpIndexedProvider->GetText((uint32)dispa->item.lParam, mTextW[0]);
				} else {
					IVDUITreeViewVirtualItem *lvvi = (IVDUITreeViewVirtualItem *)dispa->item.lParam;
					lvvi->GetText(mTextW[0]);
				}

				mTextA[mNextTextIndex] = VDTextWToA(mTextW[0]);
				dispa->item.pszText = (LPSTR)mTextA[mNextTextIndex].c_str();

				if (++mNextTextIndex >= 3)
					mNextTextIndex = 0;
			}
			break;

		case TVN_GETDISPINFOW:
			{
				NMTVDISPINFOW *dispw = (NMTVDISPINFOW *)hdr;

				mTextW[mNextTextIndex].clear();

				if (mbIndexedMode) {
					if (mpIndexedProvider)
						mpIndexedProvider->GetText((uint32)dispw->item.lParam, mTextW[mNextTextIndex]);
				} else {
					IVDUITreeViewVirtualItem *lvvi = (IVDUITreeViewVirtualItem *)dispw->item.lParam;
					lvvi->GetText(mTextW[mNextTextIndex]);
				}

				dispw->item.pszText = (LPWSTR)mTextW[mNextTextIndex].c_str();

				if (++mNextTextIndex >= 3)
					mNextTextIndex = 0;
			}
			break;

		case TVN_DELETEITEMA:
			if (!mbIndexedMode) {
				const NMTREEVIEWA *nmtv = (const NMTREEVIEWA *)hdr;
				IVDUITreeViewVirtualItem *lvvi = (IVDUITreeViewVirtualItem *)nmtv->itemOld.lParam;

				if (lvvi) {
					lvvi->SetTreeNode({});
					lvvi->Release();
				}
			}
			break;

		case TVN_DELETEITEMW:
			if (!mbIndexedMode) {
				const NMTREEVIEWW *nmtv = (const NMTREEVIEWW *)hdr;
				IVDUITreeViewVirtualItem *lvvi = (IVDUITreeViewVirtualItem *)nmtv->itemOld.lParam;

				if (lvvi) {
					lvvi->SetTreeNode({});
					lvvi->Release();
				}
			}
			break;

		case TVN_SELCHANGEDA:
		case TVN_SELCHANGEDW:
			if (mpOnItemSelectionChanged)
				mpOnItemSelectionChanged();

			mEventItemSelectionChanged.Raise(this, 0);
			break;

		case TVN_BEGINLABELEDITA:
			{
				const NMTVDISPINFOA& dispInfo = *(const NMTVDISPINFOA *)hdr;
				BeginEditEvent event {};

				event.mNode = (NodeRef)dispInfo.item.hItem;

				if (mbIndexedMode)
					event.mItemId = (uint32)dispInfo.item.lParam;
				else
					event.mpItem = (IVDUITreeViewVirtualItem *)dispInfo.item.lParam;

				event.mbAllowEdit = true;
				event.mbOverrideText = false;

				mEventItemBeginEdit.Raise(this, &event);

				if (event.mbAllowEdit) {
					HWND hwndEdit = (HWND)SendMessageA(mhwnd, TVM_GETEDITCONTROL, 0, 0);

					if (hwndEdit) {
						if (!mpEditWndProcThunk)
							mpEditWndProcThunk = VDCreateFunctionThunkFromMethod(this, &VDUIProxyTreeViewControl::FixLabelEditWndProcA, true);

						if (mpEditWndProcThunk) {
							mPrevEditWndProc = (void (*)())(WNDPROC)GetWindowLongPtrA(hwndEdit, GWLP_WNDPROC);

							if (mPrevEditWndProc)
								SetWindowLongPtrA(hwndEdit, GWLP_WNDPROC, (LONG_PTR)VDGetThunkFunction<WNDPROC>(mpEditWndProcThunk));
						}

						if (event.mbOverrideText)
							VDSetWindowTextW32(hwndEdit, event.mOverrideText.c_str());
					}
				}

				return !event.mbAllowEdit;
			}

		case TVN_BEGINLABELEDITW:
			{
				const NMTVDISPINFOW& dispInfo = *(const NMTVDISPINFOW *)hdr;
				BeginEditEvent event {};

				event.mNode = (NodeRef)dispInfo.item.hItem;

				if (mbIndexedMode)
					event.mItemId = (uint32)dispInfo.item.lParam;
				else
					event.mpItem = (IVDUITreeViewVirtualItem *)dispInfo.item.lParam;

				event.mbAllowEdit = true;
				event.mbOverrideText = false;

				mEventItemBeginEdit.Raise(this, &event);

				if (event.mbAllowEdit) {
					HWND hwndEdit = (HWND)SendMessageA(mhwnd, TVM_GETEDITCONTROL, 0, 0);

					if (hwndEdit) {
						if (!mpEditWndProcThunk)
							mpEditWndProcThunk = VDCreateFunctionThunkFromMethod(this, &VDUIProxyTreeViewControl::FixLabelEditWndProcW, true);

						if (mpEditWndProcThunk) {
							mPrevEditWndProc = (void (*)())(WNDPROC)GetWindowLongPtrW(hwndEdit, GWLP_WNDPROC);

							if (mPrevEditWndProc)
								SetWindowLongPtrW(hwndEdit, GWLP_WNDPROC, (LONG_PTR)VDGetThunkFunction<WNDPROC>(mpEditWndProcThunk));
						}

						if (event.mbOverrideText)
							VDSetWindowTextW32(hwndEdit, event.mOverrideText.c_str());
					}
				}

				return !event.mbAllowEdit;
			}

		case TVN_ENDLABELEDITA:
			{
				const NMTVDISPINFOA& dispInfo = *(const NMTVDISPINFOA *)hdr;

				if (dispInfo.item.pszText) {
					EndEditEvent event {};

					event.mNode = (NodeRef)dispInfo.item.hItem;

					if (mbIndexedMode)
						event.mItemId = (uint32)dispInfo.item.lParam;
					else
						event.mpItem = (IVDUITreeViewVirtualItem *)dispInfo.item.lParam;

					const VDStringW& text = VDTextAToW(dispInfo.item.pszText);
					event.mpNewText = text.c_str();

					mEventItemEndEdit.Raise(this, &event);
				}
			}
			break;

		case TVN_ENDLABELEDITW:
			{
				const NMTVDISPINFOW& dispInfo = *(const NMTVDISPINFOW *)hdr;

				if (dispInfo.item.pszText) {
					EndEditEvent event;

					event.mNode = (NodeRef)dispInfo.item.hItem;

					if (mbIndexedMode)
						event.mItemId = (uint32)dispInfo.item.lParam;
					else
						event.mpItem = (IVDUITreeViewVirtualItem *)dispInfo.item.lParam;

					event.mpNewText = dispInfo.item.pszText;

					mEventItemEndEdit.Raise(this, &event);
				}
			}
			break;

		case TVN_BEGINDRAGA:
			{
				const NMTREEVIEWA& info = *(const NMTREEVIEWA *)hdr;
				BeginDragEvent event {};

				event.mNode = (NodeRef)info.itemNew.hItem;

				if (mbIndexedMode)
					event.mItemId = (uint32)info.itemNew.lParam;
				else
					event.mpItem = (IVDUITreeViewVirtualItem *)info.itemNew.lParam;

				event.mPos = vdpoint32(info.ptDrag.x, info.ptDrag.y);

				if (mpOnBeginDrag)
					mpOnBeginDrag(event);
			}
			break;

		case TVN_BEGINDRAGW:
			{
				const NMTREEVIEWW& info = *(const NMTREEVIEWW *)hdr;
				BeginDragEvent event {};

				event.mNode = (NodeRef)info.itemNew.hItem;

				if (mbIndexedMode)
					event.mItemId = (uint32)info.itemNew.lParam;
				else
					event.mpItem = (IVDUITreeViewVirtualItem *)info.itemNew.lParam;

				event.mPos = vdpoint32(info.ptDrag.x, info.ptDrag.y);

				if (mpOnBeginDrag)
					mpOnBeginDrag(event);
			}
			break;

		case NM_DBLCLK:
			{
				HTREEITEM hti = TreeView_GetSelection(mhwnd);
				if (!hti)
					return false;

				// check if the double click is to the left of the label and suppress it if
				// so -- this is to prevent acting on the expand/contract button
				RECT r {};
				TreeView_GetItemRect(mhwnd, hti, &r, TRUE);

				DWORD pos = GetMessagePos();
				POINT pt = { (short)LOWORD(pos), (short)HIWORD(pos) };

				ScreenToClient(mhwnd, &pt);

				if (pt.x < r.left)
					return false;

				bool handled = false;
				mEventItemDoubleClicked.Raise(this, &handled);
				return handled;
			}

		case NM_CUSTOMDRAW:
			{
				NMTVCUSTOMDRAW& cd = *(LPNMTVCUSTOMDRAW)lParam;

				if (!mEventItemGetDisplayAttributes.IsEmpty()) {
					if (cd.nmcd.dwDrawStage == CDDS_PREPAINT) {
						return CDRF_NOTIFYITEMDRAW;
					} else if (cd.nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
						GetDispAttrEvent event {};
						
						if (mbIndexedMode)
							event.mItemId = (uint32)cd.nmcd.lItemlParam;
						else
							event.mpItem = (IVDUITreeViewVirtualItem *)cd.nmcd.lItemlParam;

						event.mbIsBold = false;
						event.mbIsMuted = false;

						mEventItemGetDisplayAttributes.Raise(this, &event);

						if (event.mbIsBold) {
							if (!mhfontBold) {
								HFONT hfont = (HFONT)::GetCurrentObject(cd.nmcd.hdc, OBJ_FONT);

								if (hfont) {
									LOGFONTW lfw = {0};
									if (::GetObject(hfont, sizeof lfw, &lfw)) {
										lfw.lfWeight = FW_BOLD;

										mhfontBold = ::CreateFontIndirectW(&lfw);
									}
								}

								mbCreatedBoldFont = true;
							}

							if (mhfontBold) {
								::SelectObject(cd.nmcd.hdc, mhfontBold);
								return CDRF_NEWFONT;
							}
						}

						if (event.mbIsMuted) {
							// blend half of the background color into the foreground color
							cd.clrText = (cd.clrText | (cd.clrTextBk & 0xFFFFFF)) - (((cd.clrText ^ cd.clrTextBk) & 0xFEFEFE) >> 1);
						}
					}
				}
			}
			return CDRF_DODEFAULT;
	}

	return 0;
}

bool VDUIProxyTreeViewControl::On_WM_CONTEXTMENU(VDZWPARAM wParam, VDZLPARAM lParam) {
	if (mpOnContextMenu) {
		POINT pt { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
		POINT ptc = pt;
		ScreenToClient(mhwnd, &ptc);

		TVHITTESTINFO hti {};

		if (pt.x == -1 && pt.y == -1) {
			hti.hItem = TreeView_GetSelection(mhwnd);
		} else {
			hti.pt = ptc;

			TreeView_HitTest(mhwnd, &hti);
		}

		if (hti.hItem) {
			TVITEMEX tvi {};
			tvi.mask = TVIF_PARAM;
			tvi.hItem = hti.hItem;

			TreeView_GetItem(mhwnd, &tvi);

			ContextMenuEvent event {};

			event.mNode = (NodeRef)hti.hItem;

			if (mbIndexedMode)
				event.mItemId = (uint32)tvi.lParam;
			else
				event.mpItem = (IVDUITreeViewVirtualItem *)tvi.lParam;

			if (pt.x == -1 && pt.y == -1) {
				auto size = GetClientSize();

				ptc.x = size.w >> 1;
				ptc.y = size.h >> 1;

				pt = ptc;
				ClientToScreen(mhwnd, &pt);
			}

			event.mScreenPos.x = pt.x;
			event.mScreenPos.y = pt.y;

			TreeView_SelectItem(mhwnd, hti.hItem);

			return mpOnContextMenu(event);
		}
	}

	return false;
}

void VDUIProxyTreeViewControl::OnFontChanged() {
	DeleteFonts();
}

VDZLRESULT VDUIProxyTreeViewControl::FixLabelEditWndProcA(VDZHWND hwnd, VDZUINT msg, VDZWPARAM wParam, VDZLPARAM lParam) {
	switch(msg) {
		case WM_GETDLGCODE:
			return DLGC_WANTALLKEYS;
	}

	return ::CallWindowProcA((WNDPROC)mPrevEditWndProc, hwnd, msg, wParam, lParam);
}

VDZLRESULT VDUIProxyTreeViewControl::FixLabelEditWndProcW(VDZHWND hwnd, VDZUINT msg, VDZWPARAM wParam, VDZLPARAM lParam) {
	switch(msg) {
		case WM_GETDLGCODE:
			return DLGC_WANTALLKEYS;
	}

	return ::CallWindowProcW((WNDPROC)mPrevEditWndProc, hwnd, msg, wParam, lParam);
}

void VDUIProxyTreeViewControl::DeleteFonts() {
	if (mhfontBold) {
		::DeleteObject(mhfontBold);
		mhfontBold = NULL;
		mbCreatedBoldFont = false;
	}
}

bool VDUIProxyTreeViewControl::IsValidNodeRef(NodeRef node) const {
	if (node == kNodeNull)  return false;
	if (node == kNodeFirst) return false;
	if (node == kNodeLast)  return false;
	if (node == kNodeRoot)  return false;

	return true;
}

/////////////////////////////////////////////////////////////////////////////

class VDUIProxyEditControl::AutoCompleteStringBuffer : public vdrefcount {
public:
	vdfastvector<wchar_t> mStrings;
	vdfastvector<wchar_t> mFilteredStrings;
	size_t mOffsetLimit = 0;

	void AddString(const wchar_t *s);
	void Finalize();
};

void VDUIProxyEditControl::AutoCompleteStringBuffer::AddString(const wchar_t *s) {
	size_t n = wcslen(s);

	mStrings.insert(mStrings.end(), s, s + n + 1);
}

void VDUIProxyEditControl::AutoCompleteStringBuffer::Finalize() {
	mFilteredStrings = mStrings;
	mOffsetLimit = mFilteredStrings.size();
}

class VDUIProxyEditControl::AutoCompleteStringSource : public ATCOMQIW32<ATCOMBaseW32<IEnumString>, IEnumString, IUnknown> {
public:
	AutoCompleteStringSource(AutoCompleteStringBuffer& buffer);

	HRESULT STDMETHODCALLTYPE Next(ULONG count, LPOLESTR *strOut, ULONG *actual) override;
	HRESULT STDMETHODCALLTYPE Skip(ULONG count) override;
	HRESULT STDMETHODCALLTYPE Reset() override;
	HRESULT STDMETHODCALLTYPE Clone(IEnumString **obj) override;

	void Refilter(const wchar_t *substr);

private:
	vdrefptr<AutoCompleteStringBuffer> mpBuffer;
	size_t mOffset = 0;
};

VDUIProxyEditControl::AutoCompleteStringSource::AutoCompleteStringSource(AutoCompleteStringBuffer& buffer)
	: mpBuffer(&buffer)
{
}

HRESULT STDMETHODCALLTYPE VDUIProxyEditControl::AutoCompleteStringSource::Next(ULONG count, LPOLESTR *strOut, ULONG *actual) {
	if (actual)
		*actual = 0;

	if (count == 0)
		return S_OK;

	if (!strOut)
		return E_POINTER;

	std::fill(strOut, strOut + count, nullptr);

	vdspan buf(mpBuffer->mFilteredStrings);
	const size_t bufSize = mpBuffer->mOffsetLimit;

	ULONG i = 0;
	size_t offset = mOffset;

	while(i < count && offset < bufSize) {
		const size_t startOffset = offset;

		while(buf[offset++])
			;

		const size_t endOffset = offset;
		const size_t len = endOffset - startOffset;

		OLECHAR *str = (OLECHAR *)CoTaskMemAlloc(sizeof(OLECHAR) * len);
		if (!str) {
			while(i) {
				CoTaskMemFree(strOut[--i]);

				strOut[i] = nullptr;
			}

			return E_OUTOFMEMORY;
		}

		for(size_t j = 0; j < len; ++j)
			str[j] = (OLECHAR)buf[startOffset + j];

		strOut[i++] = str;
	}

	mOffset = offset;

	if (actual)
		*actual = i;

	return i < count ? S_FALSE : S_OK;
}

HRESULT STDMETHODCALLTYPE VDUIProxyEditControl::AutoCompleteStringSource::Skip(ULONG count) {
	vdspan buf(mpBuffer->mFilteredStrings);
	const size_t n = buf.size();

	while(count--) {
		if (mOffset >= n)
			return S_FALSE;

		while(buf[mOffset++])
			;
	}

	return S_OK;
}

HRESULT STDMETHODCALLTYPE VDUIProxyEditControl::AutoCompleteStringSource::Reset() {
	mOffset = 0;

	return S_OK;
}

HRESULT STDMETHODCALLTYPE VDUIProxyEditControl::AutoCompleteStringSource::Clone(IEnumString **obj) {
	if (!obj)
		return E_POINTER;

	AutoCompleteStringSource *clone = new(std::nothrow) AutoCompleteStringSource(*this);
	if (!clone)
		return E_OUTOFMEMORY;

	clone->AddRef();
	*obj = clone;
	return S_OK;
}

void VDUIProxyEditControl::AutoCompleteStringSource::Refilter(const wchar_t *substr) {
	if (!substr || !*substr) {
		mpBuffer->mFilteredStrings = mpBuffer->mStrings;
		mpBuffer->mOffsetLimit = mpBuffer->mFilteredStrings.size();
		return;
	}

	size_t offset = 0;
	size_t offsetLimit = mpBuffer->mStrings.size();
	mpBuffer->mFilteredStrings.resize(offsetLimit);
	wchar_t *dst0 = mpBuffer->mFilteredStrings.data();
	wchar_t *dst = dst0;

	while(offset < offsetLimit) {
		const wchar_t *s = &mpBuffer->mStrings[offset];
		size_t len = wcslen(s);

		if (VDTextContainsSubstringMatchByLocale(VDStringSpanW(s), VDStringSpanW(substr))) {
			memcpy(dst, s, (len + 1) * sizeof(dst[0]));
			dst += (len + 1);
		}

		offset += len + 1;
	}

	mpBuffer->mOffsetLimit = (size_t)(dst - dst0);
}

struct VDUIProxyEditControl::Helpers {
	static VDZLRESULT VDZCALLBACK StaticOnSubclassProc(VDZHWND hwnd, VDZUINT msg, VDZWPARAM wParam, VDZLPARAM lParam, VDZUINT_PTR uIdSubclass, VDZDWORD_PTR dwRefData) {
		return ((VDUIProxyEditControl *)dwRefData)->OnSubclassProc(hwnd, msg, wParam, lParam);
	}
};

VDUIProxyEditControl::VDUIProxyEditControl() {
}

VDUIProxyEditControl::~VDUIProxyEditControl() {
}

void VDUIProxyEditControl::Attach(VDZHWND hwnd) {
	VDUIProxyControl::Attach(hwnd);

	InstallHook();
}

void VDUIProxyEditControl::Detach() {
	if (mbHookInstalled) {
		mbHookInstalled = false;

		if (mhwnd)
			RemoveWindowSubclass(mhwnd, Helpers::StaticOnSubclassProc, 1);
	}

	VDUIProxyControl::Detach();
}

VDZLRESULT VDUIProxyEditControl::On_WM_COMMAND(VDZWPARAM wParam, VDZLPARAM lParam) {
	if (HIWORD(wParam) == EN_CHANGE) {
		if (mpOnTextChanged)
			mpOnTextChanged(this);

		if (mpAutoCompleteSource) {
			mpAutoCompleteSource->Refilter(GetText().c_str());

			if (mpAutoComplete) {
				vdrefptr<IAutoCompleteDropDown> dropDown;
				if (SUCCEEDED(mpAutoComplete->QueryInterface<IAutoCompleteDropDown>(~dropDown)))
					dropDown->ResetEnumerator();
			}
		}
	} else if (HIWORD(wParam) == EN_KILLFOCUS) {
		if (mpOnFocusLost)
			mpOnFocusLost();
	}

	return 0;
}

VDStringW VDUIProxyEditControl::GetText() const {
	if (!mhwnd)
		return VDStringW();

	return VDGetWindowTextW32(mhwnd);
}

void VDUIProxyEditControl::SetText(const wchar_t *s) {
	if (mhwnd)
		::SetWindowText(mhwnd, s);
}

void VDUIProxyEditControl::SetReadOnly(bool ro) {
	if (mhwnd)
		::SendMessage(mhwnd, EM_SETREADONLY, ro ? TRUE : FALSE, FALSE);
}

void VDUIProxyEditControl::SetAutoCompleteList(vdspan<const wchar_t * const> completeList) {
	if (!mhwnd)
		return;

	mpAutoComplete = nullptr;

	vdrefptr<IAutoComplete2> ac;
	HRESULT hr = ::CoCreateInstance(CLSID_AutoComplete, nullptr, CLSCTX_INPROC_SERVER, __uuidof(IAutoComplete2), (void **)~ac);
	if (SUCCEEDED(hr)) {
		vdrefptr buffer(new AutoCompleteStringBuffer);

		for(const wchar_t *s : completeList) {
			buffer->AddString(s);
		}
		
		buffer->Finalize();

		vdrefptr source(new AutoCompleteStringSource(*buffer));

		if (SUCCEEDED(ac->Init(mhwnd, static_cast<IEnumString *>(&*source), nullptr, nullptr))) {
			ac->SetOptions(ACO_AUTOSUGGEST | ACO_UPDOWNKEYDROPSLIST | ACO_NOPREFIXFILTERING);
		}

		mpAutoCompleteSource = std::move(source);
	}

	mpAutoComplete = std::move(ac);
}

void VDUIProxyEditControl::SelectAll() {
	if (mhwnd)
		::SendMessage(mhwnd, EM_SETSEL, 0, -1);
}

void VDUIProxyEditControl::DeselectAll() {
	if (mhwnd)
		::SendMessage(mhwnd, EM_SETSEL, -1, -1);
}

void VDUIProxyEditControl::SetOnTextChanged(vdfunction<void(VDUIProxyEditControl *)> fn) {
	mpOnTextChanged = std::move(fn);
}

void VDUIProxyEditControl::SetOnFocusLost(vdfunction<void()> fn) {
	mpOnFocusLost = std::move(fn);
}

void VDUIProxyEditControl::SetOnTabPressed(vdfunction<void()> fn) {
	mpOnTabPressed = std::move(fn);

	InstallHook();
}

void VDUIProxyEditControl::SetOnEnterPressed(vdfunction<void()> fn) {
	mpOnEnterPressed = std::move(fn);

	InstallHook();
}

void VDUIProxyEditControl::SetOnArrowKeyPressed(vdfunction<bool(uint32)> fn) {
	mpOnArrowKeyPressed = std::move(fn);

	InstallHook();
}

void VDUIProxyEditControl::InstallHook() {
	if (!mhwnd)
		return;

	if (!mpOnTabPressed && !mpOnEnterPressed && !mpOnArrowKeyPressed)
		return;

	if (!mbHookInstalled) {
		mbHookInstalled = true;

		SetWindowSubclass(mhwnd, Helpers::StaticOnSubclassProc, 1, (DWORD_PTR)this);
	}
}

VDZLRESULT VDUIProxyEditControl::OnSubclassProc(VDZHWND hwnd, VDZUINT msg, VDZWPARAM wParam, VDZLPARAM lParam) {
	switch(msg) {
		case WM_GETDLGCODE: {
			LRESULT mask = 0;

			if (mpOnTabPressed)
				mask |= DLGC_WANTTAB;

			if (mpOnEnterPressed && lParam) {
				const MSG& msg = *(const MSG *)lParam;

				if (msg.message == WM_KEYDOWN && msg.wParam == VK_RETURN)
					mask |= DLGC_WANTMESSAGE;
			}

			if (mpOnArrowKeyPressed)
				mask |= DLGC_WANTARROWS;

			return mask | DefSubclassProc(hwnd, msg, wParam, lParam);
		}

		case WM_KEYDOWN:
			switch(wParam) {
				case VK_TAB:
					if (mpOnTabPressed) {
						mpOnTabPressed();
						return 0;
					}
					break;

				case VK_RETURN:
					if (mpOnEnterPressed) {
						mpOnEnterPressed();
						return 0;
					}
					break;

				case VK_LEFT:
				case VK_RIGHT:
				case VK_UP:
				case VK_DOWN:
					if (mpOnArrowKeyPressed && mpOnArrowKeyPressed(wParam))
						return 0;
					break;

				default:
					break;
			}

			break;
	}

	return DefSubclassProc(hwnd, msg, wParam, lParam);
}

/////////////////////////////////////////////////////////////////////////////

VDUIProxyRichEditControl::VDUIProxyRichEditControl() {
}

VDUIProxyRichEditControl::~VDUIProxyRichEditControl() {
	Detach();
}

void VDUIProxyRichEditControl::AppendEscapedRTF(VDStringA& buf, const wchar_t *text) {
	const VDStringA& texta = VDTextWToA(text);
	for(VDStringA::const_iterator it = texta.begin(), itEnd = texta.end();
		it != itEnd;
		++it)
	{
		const unsigned char c = *it;

		if (c < 0x20 || c > 0x80 || c == '{' || c == '}' || c == '\\')
			buf.append_sprintf("\\'%02x", c);
		else if (c == '\n')
			buf += "\\par ";
		else
			buf += c;
	}
}

bool VDUIProxyRichEditControl::IsSelectionPresent() const {
	if (!mhwnd)
		return false;

	DWORD start = 0, end = 0;
	::SendMessage(mhwnd, EM_GETSEL, (WPARAM)&start, (WPARAM)&end);

	return end > start;
}

void VDUIProxyRichEditControl::EnsureCaretVisible() {
	if (mhwnd)
		::SendMessage(mhwnd, EM_SCROLLCARET, 0, 0);
}

void VDUIProxyRichEditControl::SelectAll() {
	if (mhwnd)
		::SendMessage(mhwnd, EM_SETSEL, 0, -1);
}

void VDUIProxyRichEditControl::Copy() {
	if (mhwnd)
		::SendMessage(mhwnd, WM_COPY, 0, 0);
}

void VDUIProxyRichEditControl::SetCaretPos(int lineIndex, int charIndex) {
	if (!mhwnd)
		return;

	int lineStart = (int)::SendMessage(mhwnd, EM_LINEINDEX, (WPARAM)lineIndex, 0);

	if (lineStart >= 0)
		lineStart += charIndex;

	CHARRANGE cr = { lineStart, lineStart };
	::SendMessage(mhwnd, EM_EXSETSEL, 0, (LPARAM)&cr);
}

void VDUIProxyRichEditControl::SetText(const wchar_t *s) {
	if (mhwnd)
		::SetWindowText(mhwnd, s);
}

void VDUIProxyRichEditControl::SetTextRTF(const char *s) {
	if (!mhwnd)
		return;

	SETTEXTEX stex;
	stex.flags = ST_DEFAULT;
	stex.codepage = CP_ACP;
	SendMessageA(mhwnd, EM_SETTEXTEX, (WPARAM)&stex, (LPARAM)s);
}

void VDUIProxyRichEditControl::ReplaceSelectedText(const wchar_t *s) {
	if (!mhwnd)
		return;

	SendMessage(mhwnd, EM_REPLACESEL, TRUE, (LPARAM)s);
}

void VDUIProxyRichEditControl::SetFontFamily(const wchar_t *family) {
	if (!mhwnd)
		return;

	CHARFORMAT cf {};
	cf.cbSize = sizeof(CHARFORMAT);
	cf.dwMask = CFM_FACE;
	vdwcslcpy(cf.szFaceName, family, vdcountof(cf.szFaceName));

	SendMessage(mhwnd, EM_SETCHARFORMAT, SCF_ALL, (LPARAM)&cf);
}

void VDUIProxyRichEditControl::SetBackgroundColor(uint32 c) {
	if (mhwnd)
		SendMessage(mhwnd, EM_SETBKGNDCOLOR, FALSE, VDSwizzleU32(c) >> 8);
}

void VDUIProxyRichEditControl::SetReadOnlyBackground() {
	if (mhwnd) {
		COLORREF bg;

		if (ATUIIsDarkThemeActive())
			bg = ATUIGetThemeColors().mStaticBg;
		else
			bg = GetSysColor(COLOR_3DFACE);

		SendMessage(mhwnd, EM_SETBKGNDCOLOR, FALSE, bg);
	}
}

void VDUIProxyRichEditControl::SetPlainTextMode() {
	if (mhwnd)
		SendMessage(mhwnd, EM_SETTEXTMODE, TM_RICHTEXT, 0);
}

void VDUIProxyRichEditControl::DisableCaret() {
	if (mCaretDisabled)
		return;

	mCaretDisabled = true;

	if (mhwnd) {
		LRESULT mask = SendMessage(mhwnd, EM_GETEVENTMASK, 0, 0);
		SendMessage(mhwnd, EM_SETEVENTMASK, 0, mask | ENM_SELCHANGE);
		InitSubclass();

		// check if the window has the focus -- if so, hide the caret
		// immediately
		if (::GetFocus() == mhwnd)
			HideCaret(mhwnd);
	}
}

void VDUIProxyRichEditControl::DisableSelectOnFocus() {
	if (mhwnd)
		SendMessage(mhwnd, EM_SETOPTIONS, ECOOP_OR, ECO_SAVESEL);
}

void VDUIProxyRichEditControl::SetOnTextChanged(vdfunction<void()> fn) {
	mpOnTextChanged = std::move(fn);

	if (mhwnd) {
		LRESULT mask = SendMessage(mhwnd, EM_GETEVENTMASK, 0, 0);
		SendMessage(mhwnd, EM_SETEVENTMASK, 0, mask | ENM_CHANGE);
	}
}

void VDUIProxyRichEditControl::SetOnLinkSelected(vdfunction<bool(const wchar_t *)> fn) {
	mpOnLinkSelected = std::move(fn);

	UpdateLinkEnableStatus();
}

void VDUIProxyRichEditControl::UpdateMargins(sint32 xpad, sint32 ypad) {
	if (mhwnd) {
		vdrect32 cr = GetClientArea();
		// inset rect
		RECT r { xpad, ypad, cr.right-xpad, cr.bottom-ypad };
		SendMessage(mhwnd, EM_SETRECT, 0, (LPARAM)&r);
	}
}

void VDUIProxyRichEditControl::Attach(VDZHWND hwnd) {
	VDUIProxyControl::Attach(hwnd);

	if (mhwnd) {
		if (!mpTextDoc) {
			vdrefptr<IUnknown> pUnk;
			SendMessage(mhwnd, EM_GETOLEINTERFACE, 0, (LPARAM)~pUnk);

			if (pUnk) {
				pUnk->QueryInterface<ITextDocument>(&mpTextDoc);
			}
		}

		if (ATUIIsDarkThemeActive()) {
			const auto& tcw32 = ATUIGetThemeColorsW32();

			CHARFORMAT2 cf = {};
			cf.cbSize = sizeof cf;
			cf.dwMask = CFM_COLOR | CFM_EFFECTS;
			cf.dwEffects = 0;
			cf.crTextColor = tcw32.mContentFgCRef;

			SendMessage(mhwnd, EM_SETBKGNDCOLOR, FALSE, tcw32.mContentBgCRef);
			SendMessage(mhwnd, EM_SETCHARFORMAT, SCF_ALL | SCF_DEFAULT, (LPARAM)&cf);
		}
	}

	UpdateLinkEnableStatus();

	if (mCaretDisabled)
		InitSubclass();
}

void VDUIProxyRichEditControl::Detach() {
	if (mSubclassed) {
		mSubclassed = false;

		RemoveWindowSubclass(mhwnd, StaticOnSubclassProc, (UINT_PTR)this);
	}

	if (mpTextDoc) {
		mpTextDoc->Release();
		mpTextDoc = nullptr;
	}

	VDUIProxyControl::Detach();
}

VDZLRESULT VDUIProxyRichEditControl::On_WM_COMMAND(VDZWPARAM wParam, VDZLPARAM lParam) {
	if (HIWORD(wParam) == EN_CHANGE) {
		if (mpOnTextChanged)
			mpOnTextChanged();
	}

	return 0;
}

VDZLRESULT VDUIProxyRichEditControl::On_WM_NOTIFY(VDZWPARAM wParam, VDZLPARAM lParam) {
	const auto& hdr = *(const NMHDR *)lParam;

	if (hdr.code == EN_LINK) {
		const ENLINK& link = *(const ENLINK *)lParam;
		if (mpOnLinkSelected && link.msg == WM_LBUTTONDOWN) {

			struct AutoBSTR {
				BSTR p = nullptr;

				~AutoBSTR() { SysFreeString(p); }
			};

			vdrefptr<ITextRange> range;
			AutoBSTR linkText;
			if (SUCCEEDED(mpTextDoc->Range(link.chrg.cpMin, link.chrg.cpMax, ~range))) {
				range->GetText(&linkText.p);
			}

			if (mpOnLinkSelected && mpOnLinkSelected(linkText.p ? linkText.p : L""))
				return 1;
		}
	} else if (hdr.code == EN_SELCHANGE) {
		if (mCaretDisabled)
			HideCaret(mhwnd);
	}
	return 0;
}

VDZLRESULT VDZCALLBACK VDUIProxyRichEditControl::StaticOnSubclassProc(VDZHWND hwnd, VDZUINT msg, VDZWPARAM wParam, VDZLPARAM lParam, VDZUINT_PTR uIdSubclass, VDZDWORD_PTR dwRefData) {
	return ((VDUIProxyRichEditControl *)dwRefData)->OnSubclassProc(hwnd, msg, wParam, lParam);
}

VDZLRESULT VDUIProxyRichEditControl::OnSubclassProc(VDZHWND hwnd, VDZUINT msg, VDZWPARAM wParam, VDZLPARAM lParam) {
	if (mCaretDisabled) {
		if (msg == WM_SETFOCUS) {
			LRESULT lr = DefSubclassProc(hwnd, msg, wParam, lParam);
			HideCaret(hwnd);
			return lr;
		}
	}

	return DefSubclassProc(hwnd, msg, wParam, lParam);
}

void VDUIProxyRichEditControl::UpdateLinkEnableStatus() {
	if (mhwnd) {
		LRESULT mask = SendMessage(mhwnd, EM_GETEVENTMASK, 0, 0);
		SendMessage(mhwnd, EM_SETEVENTMASK, 0, mpOnLinkSelected ? mask | ENM_LINK : mask & ~ENM_LINK);
	}
}

void VDUIProxyRichEditControl::InitSubclass() {
	if (mSubclassed || true)
		return;

	mSubclassed = true;
	SetWindowSubclass(mhwnd, StaticOnSubclassProc, (UINT_PTR)this, (DWORD_PTR)nullptr);
}


/////////////////////////////////////////////////////////////////////////////

VDUIProxyButtonControl::VDUIProxyButtonControl() {
}

VDUIProxyButtonControl::~VDUIProxyButtonControl() {
}

bool VDUIProxyButtonControl::GetChecked() const {
	return mhwnd && SendMessage(mhwnd, BM_GETCHECK, 0, 0) == BST_CHECKED;
}

void VDUIProxyButtonControl::SetChecked(bool enable) {
	if (mhwnd)
		SendMessage(mhwnd, BM_SETCHECK, enable ? BST_CHECKED : BST_UNCHECKED, 0);
}

VDZLRESULT VDUIProxyButtonControl::On_WM_COMMAND(VDZWPARAM wParam, VDZLPARAM lParam) {
	if (HIWORD(wParam) == BN_CLICKED) {
		if (mpOnClicked)
			mpOnClicked();
	}

	return 0;
}

VDZLRESULT VDUIProxyButtonControl::On_WM_NOTIFY(VDZWPARAM wParam, VDZLPARAM lParam) {
	const NMHDR& hdr = *(const NMHDR *)lParam;

	if (hdr.code == BCN_DROPDOWN) {
		if (mpOnDropDown)
			mpOnDropDown();
	}

	return 0;
}

void VDUIProxyButtonControl::SetOnClicked(vdfunction<void()> fn) {
	mpOnClicked = std::move(fn);
}

void VDUIProxyButtonControl::SetOnDropDown(vdfunction<void()> fn) {
	mpOnDropDown = std::move(fn);
}

void VDUIProxyButtonControl::ShowElevationNeeded() {
	if (mhwnd)
		SendMessage(mhwnd, BCM_SETSHIELD, 0, TRUE);
}

/////////////////////////////////////////////////////////////////////////////

VDUIProxyToolbarControl::VDUIProxyToolbarControl() {
}

VDUIProxyToolbarControl::~VDUIProxyToolbarControl() {
}

void VDUIProxyToolbarControl::SetDarkModeEnabled(bool enable) {
	if (mbDarkModeEnabled != enable) {
		mbDarkModeEnabled = enable;

		if (mhwnd && ATUIIsDarkThemeActive())
			SetWindowTheme(mhwnd, L"", L"");
	}
}

void VDUIProxyToolbarControl::Clear() {
	if (mhwnd) {
		while(SendMessage(mhwnd, TB_DELETEBUTTON, 0, 0))
			;
	}
}

void VDUIProxyToolbarControl::AddButton(uint32 id, sint32 imageIndex, const wchar_t *label) {
	if (!mhwnd)
		return;

	TBBUTTON tb {};
	tb.iBitmap = imageIndex >= 0 ? imageIndex : I_IMAGENONE;
	tb.idCommand = id;
	tb.fsState = TBSTATE_ENABLED;
	tb.fsStyle = TBSTYLE_BUTTON | BTNS_AUTOSIZE | (label ? BTNS_SHOWTEXT : 0);
	tb.iString = (INT_PTR)label;

	SendMessage(mhwnd, TB_ADDBUTTONS, 1, (LPARAM)&tb);
}

void VDUIProxyToolbarControl::AddDropdownButton(uint32 id, sint32 imageIndex, const wchar_t *label) {
	if (!mhwnd)
		return;

	TBBUTTON tb {};
	tb.iBitmap = imageIndex >= 0 ? imageIndex : I_IMAGENONE;
	tb.idCommand = id;
	tb.fsState = TBSTATE_ENABLED;
	tb.fsStyle = BTNS_WHOLEDROPDOWN | BTNS_AUTOSIZE | (label ? BTNS_SHOWTEXT : 0);
	tb.iString = (INT_PTR)label;

	SendMessage(mhwnd, TB_ADDBUTTONS, 1, (LPARAM)&tb);
}

void VDUIProxyToolbarControl::AddSeparator() {
	if (!mhwnd)
		return;

	TBBUTTON tb {};
	tb.fsState = TBSTATE_ENABLED;
	tb.fsStyle = TBSTYLE_SEP;

	SendMessage(mhwnd, TB_ADDBUTTONS, 1, (LPARAM)&tb);
}

void VDUIProxyToolbarControl::SetItemVisible(uint32 id, bool visible) {
	if (!mhwnd)
		return;

	SendMessage(mhwnd, TB_HIDEBUTTON, id, !visible);
}

void VDUIProxyToolbarControl::SetItemEnabled(uint32 id, bool enabled) {
	if (!mhwnd)
		return;

	SendMessage(mhwnd, TB_ENABLEBUTTON, id, enabled);
}

void VDUIProxyToolbarControl::SetItemPressed(uint32 id, bool enabled) {
	if (!mhwnd)
		return;

	SendMessage(mhwnd, TB_PRESSBUTTON, id, enabled);
}

void VDUIProxyToolbarControl::SetItemText(uint32 id, const wchar_t *text) {
	if (!mhwnd)
		return;

	TBBUTTONINFO info {};

	info.cbSize = sizeof(TBBUTTONINFO);
	info.dwMask = TBIF_TEXT;
	info.pszText = (LPWSTR)text;

	SendMessage(mhwnd, TB_SETBUTTONINFO, id, (LPARAM)&info);
}

void VDUIProxyToolbarControl::SetItemImage(uint32 id, sint32 imageIndex) {
	if (!mhwnd)
		return;

	SendMessage(mhwnd, TB_CHANGEBITMAP, id, imageIndex >= 0 ? imageIndex : I_IMAGENONE);
}

void VDUIProxyToolbarControl::InitImageList(uint32 n, uint32 width, uint32 height) {
	if (!mhwnd)
		return;

	mImageWidth = width;
	mImageHeight = height;
	HIMAGELIST imageList = ImageList_Create(width, height, ILC_COLOR32, 0, n);

	SendMessage(mhwnd, TB_SETIMAGELIST, 0, (LPARAM)imageList);

	if (mImageList)
		ImageList_Destroy(mImageList);

	mImageList = imageList;
}

void VDUIProxyToolbarControl::AddImage(const VDPixmap& px) {
	AddImagesToImageList(mImageList, px, mImageWidth, mImageHeight, 0);
}

void VDUIProxyToolbarControl::AddImages(uint32 n, const VDPixmap& px) {
	if (!mhwnd || !n)
		return;

	VDASSERT(px.w % n == 0);
	uint32 imageWidth = px.w / n;

	for(uint32 i=0; i<n; ++i) {
		AddImage(VDPixmapClip(px, imageWidth * i, 0, imageWidth, px.h));
	}
}

void VDUIProxyToolbarControl::AutoSize() {
	if (mhwnd)
		SendMessage(mhwnd, TB_AUTOSIZE, 0, 0);
}

sint32 VDUIProxyToolbarControl::ShowDropDownMenu(uint32 itemId, const wchar_t *const *items) {
	if (!mhwnd)
		return -1;

	RECT r {};
	SendMessage(mhwnd, TB_GETRECT, itemId, (LPARAM)&r);
	MapWindowPoints(mhwnd, NULL, (LPPOINT)&r, 2);

	HMENU hmenu = CreatePopupMenu();

	uint32 id = 1;
	while(const wchar_t *s = *items++) {
		AppendMenu(hmenu, MF_STRING | MF_ENABLED, id++, s);
	}

	uint32 selectedId = ShowDropDownMenu(itemId, hmenu);

	DestroyMenu(hmenu);

	return (sint32)selectedId - 1;
}

uint32 VDUIProxyToolbarControl::ShowDropDownMenu(uint32 itemId, VDZHMENU hmenu) {
	if (!mhwnd)
		return -1;

	RECT r {};
	SendMessage(mhwnd, TB_GETRECT, itemId, (LPARAM)&r);
	MapWindowPoints(mhwnd, NULL, (LPPOINT)&r, 2);

	TPMPARAMS tpm;
	tpm.cbSize = sizeof(TPMPARAMS);
	tpm.rcExclude = r;
	uint32 selectedId = (uint32)TrackPopupMenuEx(hmenu, TPM_LEFTALIGN | TPM_LEFTBUTTON | TPM_VERTICAL | TPM_RETURNCMD, r.left, r.bottom, mhwnd, &tpm);

	return selectedId;
}

void VDUIProxyToolbarControl::SetOnClicked(vdfunction<void(uint32)> fn) {
	mpOnClicked = std::move(fn);
}

void VDUIProxyToolbarControl::Attach(VDZHWND hwnd) {
	VDUIProxyControl::Attach(hwnd);

	SendMessage(mhwnd, TB_SETEXTENDEDSTYLE, 0, TBSTYLE_EX_DRAWDDARROWS | TBSTYLE_EX_MIXEDBUTTONS);
	SendMessage(mhwnd, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);

	if (mbDarkModeEnabled && ATUIIsDarkThemeActive())
		SetWindowTheme(mhwnd, L"", L"");
}

void VDUIProxyToolbarControl::Detach() {
	if (mImageList) {
		if (mhwnd)
			SendMessage(mhwnd, TB_SETIMAGELIST, 0, 0);

		ImageList_Destroy(mImageList);
		mImageList = nullptr;
	}

	VDUIProxyControl::Detach();
}

VDZLRESULT VDUIProxyToolbarControl::On_WM_COMMAND(VDZWPARAM wParam, VDZLPARAM lParam) {
	if (HIWORD(wParam) == BN_CLICKED) {
		if (mpOnClicked)
			mpOnClicked(LOWORD(wParam));
	}

	return 0;
}

VDZLRESULT VDUIProxyToolbarControl::On_WM_NOTIFY(VDZWPARAM wParam, VDZLPARAM lParam) {
	const NMHDR& hdr = *(const NMHDR *)lParam;

	if (hdr.code == TBN_DROPDOWN) {
		const NMTOOLBAR& tbhdr = *(const NMTOOLBAR *)lParam;

		if (mpOnClicked)
			mpOnClicked(tbhdr.iItem);

		return TBDDRET_DEFAULT;
	} else if (hdr.code == NM_CUSTOMDRAW) {
		if (!mbDarkModeEnabled || !ATUIIsDarkThemeActive())
			return CDRF_DODEFAULT;

		NMTBCUSTOMDRAW& tbcd = *(NMTBCUSTOMDRAW *)&hdr;

		switch(tbcd.nmcd.dwDrawStage) {
			case CDDS_PREPAINT:
				return CDRF_NOTIFYITEMDRAW;

			case CDDS_PREERASE:
				if (RECT r; GetClientRect(mhwnd, &r))
					FillRect(tbcd.nmcd.hdc, &r, ATUIGetThemeColorsW32().mStaticBgBrush);
				return CDRF_SKIPDEFAULT;

			case CDDS_ITEMPREPAINT:
				tbcd.clrText = ATUIGetThemeColorsW32().mStaticFgCRef;
				return TBCDRF_USECDCOLORS;

			default:
				return CDRF_DODEFAULT;
		}
	}

	return 0;
}

///////////////////////////////////////////////////////////////////////////

VDUIProxySysLinkControl::VDUIProxySysLinkControl() {
}

VDUIProxySysLinkControl::~VDUIProxySysLinkControl() {
}

void VDUIProxySysLinkControl::SetOnClicked(vdfunction<void()> fn) {
	mpOnClicked = std::move(fn);
}

void VDUIProxySysLinkControl::SetOnClickedWithUrl(vdfunction<void(const wchar_t *)> fn) {
	mpOnClickedWithUrl = std::move(fn);
}

sint32 VDUIProxySysLinkControl::GetIdealHeightForWidth(sint32 w) const {
	if (!mhwnd)
		return 0;

	SIZE sz { w, 0 };
	sint32 h = SendMessage(mhwnd, LM_GETIDEALHEIGHT, w, (LPARAM)&sz);

	return h;
}

VDZLRESULT VDUIProxySysLinkControl::On_WM_NOTIFY(VDZWPARAM wParam, VDZLPARAM lParam) {
	const NMHDR& hdr = *(const NMHDR *)lParam;

	if (hdr.code == NM_CLICK) {
		if (mpOnClicked)
			mpOnClicked();

		if (mpOnClickedWithUrl)
			mpOnClickedWithUrl(((const NMLINK&)hdr).item.szUrl);
	} else if (hdr.code == NM_CUSTOMTEXT) {
		NMCUSTOMTEXT& ct = *(NMCUSTOMTEXT *)lParam;

		if (ATUIIsDarkThemeActive()) {
			const auto& colors = ATUIGetThemeColors();

			if (ct.fLink)
				SetTextColor(ct.hDC, VDSwizzleU32(colors.mHyperlinkText) >> 8);
		}
	}

	return 0;
}

///////////////////////////////////////////////////////////////////////////

VDUIProxyTrackbarControl::VDUIProxyTrackbarControl() {
}

VDUIProxyTrackbarControl::~VDUIProxyTrackbarControl() {
}

void VDUIProxyTrackbarControl::SetOnValueChanged(vdfunction<void(sint32, bool)> fn) {
	mpFnOnValueChanged = std::move(fn);
}

sint32 VDUIProxyTrackbarControl::GetValue() const {
	return mhwnd ? (sint32)SendMessage(mhwnd, TBM_GETPOS, 0, 0) : 0;
}

void VDUIProxyTrackbarControl::SetValue(sint32 v) {
	if (mhwnd)
		SendMessage(mhwnd, TBM_SETPOS, TRUE, (LPARAM)v);
}

void VDUIProxyTrackbarControl::SetRange(sint32 minVal, sint32 maxVal) {
	if (mhwnd) {
		SendMessage(mhwnd, TBM_SETRANGEMIN, FALSE, (LPARAM)minVal);
		SendMessage(mhwnd, TBM_SETRANGEMAX, TRUE, (LPARAM)maxVal);
	}
}

void VDUIProxyTrackbarControl::SetPageSize(sint32 pageSize) {
	if (mhwnd)
		SendMessage(mhwnd, TBM_SETPAGESIZE, TRUE, (LPARAM)pageSize);
}

VDZLRESULT VDUIProxyTrackbarControl::On_WM_HSCROLL(VDZWPARAM wParam, VDZLPARAM lParam) {
	if (mpFnOnValueChanged)
		mpFnOnValueChanged(GetValue(), LOWORD(lParam) == TB_THUMBTRACK);

	return 0;
}

VDZLRESULT VDUIProxyTrackbarControl::On_WM_VSCROLL(VDZWPARAM wParam, VDZLPARAM lParam) {
	return On_WM_HSCROLL(wParam, lParam);
}

///////////////////////////////////////////////////////////////////////////

VDUIProxyScrollBarControl::VDUIProxyScrollBarControl() {
}

VDUIProxyScrollBarControl::~VDUIProxyScrollBarControl() {
}

void VDUIProxyScrollBarControl::SetOnValueChanged(vdfunction<void(sint32, bool)> fn) {
	mpFnOnValueChanged = std::move(fn);
}

sint32 VDUIProxyScrollBarControl::GetValue() const {
	if (!mhwnd)
		return 0;

	SCROLLINFO si { sizeof(SCROLLINFO) };
	si.fMask = SIF_POS;
	if (!GetScrollInfo(mhwnd, SB_CTL, &si))
		return 0;

	return si.nPos;
}

void VDUIProxyScrollBarControl::SetValue(sint32 v) {
	SetParams(v, std::nullopt, std::nullopt);
}

void VDUIProxyScrollBarControl::SetRange(sint32 minVal, sint32 maxVal) {
	SetParams(std::nullopt, std::make_pair(minVal, maxVal), std::nullopt);
}

void VDUIProxyScrollBarControl::SetPageSize(sint32 pageSize) {
	SetParams(std::nullopt, std::nullopt, pageSize);
}

void VDUIProxyScrollBarControl::SetParams(
	std::optional<sint32> value,
	std::optional<std::pair<sint32, sint32>> range,
	std::optional<sint32> pageSize
)
{
	if (!mhwnd)
		return;

	SCROLLINFO si { sizeof(SCROLLINFO) };

	if (value.has_value()) {
		si.fMask = SIF_POS;
		si.nPos = value.value();
	}

	if (range.has_value()) {
		auto [rangeMin, rangeMax] = range.value();

		if (rangeMax < rangeMin)
			rangeMax = rangeMin;

		si.nMin = rangeMin;
		si.nMax = rangeMax;

		// read back the position if needed, then clamp it
		if (!(si.fMask & SIF_POS))
			si.nPos = GetValue();

		si.nPos = std::clamp(si.nPos, si.nMin, si.nMax);
		si.fMask |= SIF_DISABLENOSCROLL | SIF_RANGE | SIF_POS;
	}

	if (pageSize.has_value()) {
		si.nPage = std::max<sint32>(1, pageSize.value());
		si.fMask |= SIF_DISABLENOSCROLL | SIF_PAGE;
	}

	if (si.fMask | (SIF_PAGE | SIF_RANGE | SIF_POS))
		SetScrollInfo(mhwnd, SB_CTL, &si, TRUE);
}

VDZLRESULT VDUIProxyScrollBarControl::On_WM_HSCROLL(VDZWPARAM wParam, VDZLPARAM lParam) {
	SCROLLINFO si { sizeof(SCROLLINFO) };
	si.fMask = SIF_ALL;

	if (!GetScrollInfo(mhwnd, SB_CTL, &si))
		return 0;

	sint32 pos = si.nPos;

	switch(LOWORD(wParam)) {
		case SB_LEFT:
			pos = si.nMin;
			break;

		case SB_RIGHT:
			pos = si.nMax;
			break;

		case SB_LINELEFT:
			if (pos > si.nMin)
				--pos;
			break;

		case SB_LINERIGHT:
			if (pos < si.nMax)
				++pos;
			break;

		case SB_PAGELEFT:
			if (pos > si.nMin && (uint32)pos - (uint32)si.nMin > (uint32)si.nPage)
				pos -= si.nPage;
			else
				pos = si.nMin;
			break;

		case SB_PAGERIGHT:
			if (pos < si.nMax && (uint32)si.nMax - (uint32)pos > (uint32)si.nPage)
				pos += si.nPage;
			else
				pos = si.nMax;
			break;

		case SB_THUMBPOSITION:
			if (mpFnOnValueChanged)
				mpFnOnValueChanged(si.nPos, false);
			return 0;

		case SB_THUMBTRACK:
			pos = si.nTrackPos;
			break;
	}

	pos = std::clamp<sint32>(pos, si.nMin, si.nMax);

	if (si.nPos != pos) {
		si.nPos = pos;
		si.fMask = SIF_POS;

		SetScrollInfo(mhwnd, SB_CTL, &si, TRUE);

		if (mpFnOnValueChanged)
			mpFnOnValueChanged(si.nPos, LOWORD(wParam) == SB_THUMBTRACK);
	}

	return 0;
}

VDZLRESULT VDUIProxyScrollBarControl::On_WM_VSCROLL(VDZWPARAM wParam, VDZLPARAM lParam) {
	return On_WM_HSCROLL(wParam, lParam);
}

///////////////////////////////////////////////////////////////////////////

VDUIProxyStatusBarControl::VDUIProxyStatusBarControl() {
}

VDUIProxyStatusBarControl::~VDUIProxyStatusBarControl() {
}

void VDUIProxyStatusBarControl::AutoLayout() {
	if (mhwnd)
		SendMessage(mhwnd, WM_SIZE, 0, 0);
}

///////////////////////////////////////////////////////////////////////////

VDUIProxyStaticControl::VDUIProxyStaticControl() {
}

VDUIProxyStaticControl::~VDUIProxyStaticControl() {
}

void VDUIProxyStaticControl::SetBgOverrideColor(uint32 rgb) {
	sint32 v = rgb & 0xFFFFFF;

	if (mBgColor != v) {
		mBgColor = v;

		Invalidate();
	}
}

VDZHBRUSH VDUIProxyStaticControl::On_WM_CTLCOLORSTATIC(VDZWPARAM wParam, VDZLPARAM lParam) {
	if (mBgColor < 0)
		return nullptr;

	HDC hdc = (HDC)wParam;

	SetDCBrushColor(hdc, VDSwizzleU32(mBgColor) >> 8);

	return (HBRUSH)GetStockObject(DC_BRUSH);
}
