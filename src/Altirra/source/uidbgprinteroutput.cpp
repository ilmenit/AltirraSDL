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
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <vd2/system/binary.h>
#include <vd2/system/bitmath.h>
#include <vd2/system/color.h>
#include <vd2/system/cpuaccel.h>
#include <vd2/system/file.h>
#include <vd2/system/strutil.h>
#include <vd2/system/vdstl_vectorview.h>
#include <vd2/system/zip.h>
#include <vd2/system/w32assist.h>
#include <vd2/Dita/services.h>
#include <at/atcore/atascii.h>
#include <at/atnativeui/theme.h>
#include <at/atnativeui/theme_win32.h>
#include <at/atnativeui/canvas_win32.h>
#include <at/atnativeui/uinativewindow.h>
#include "oshelper.h"
#include "printerexport.h"
#include "printeroutput.h"
#include "printerrasterizer.h"
#include "resource.h"
#include "simulator.h"
#include "texteditor.h"
#include "uidbgprinteroutput.h"

extern ATSimulator g_sim;

///////////////////////////////////////////////////////////////////////////////

class ATUIPrinterCachePage final : public vdrefcount {
public:
	ATUIPrinterCachePage(HDC hdc, uint32 w, uint32 h);
	~ATUIPrinterCachePage();

	void ClearPixels(const vdrect32& dst, uint32 c);
	void UpdatePixels(const vdrect32& dst, const void *data32, ptrdiff_t pitch);

	HDC GetCachePageDC() const { return mhdc; }

private:
	void Shutdown();

	HDC mhdc = nullptr;
	HBITMAP mhbm = nullptr;
	HGDIOBJ mhgoOldBitmap = nullptr;
};

ATUIPrinterCachePage::ATUIPrinterCachePage(HDC hdc, uint32 w, uint32 h) {
	if ((mhbm = CreateCompatibleBitmap(hdc, w, h))) {
		if ((mhdc = CreateCompatibleDC(hdc))) {
			if ((mhgoOldBitmap = SelectObject(mhdc, mhbm))) {
				return;
			}
		}
	}

	Shutdown();
}

ATUIPrinterCachePage::~ATUIPrinterCachePage() {
	Shutdown();
}

void ATUIPrinterCachePage::ClearPixels(const vdrect32& dst, uint32 c) {
	if (dst.empty() || !mhdc)
		return;

	SetBkMode(mhdc, OPAQUE);
	SetBkColor(mhdc, VDSwizzleU32(c) >> 8);

	RECT r { dst.left, dst.top, dst.right, dst.bottom };
	ExtTextOutW(mhdc, dst.left, dst.top, ETO_OPAQUE, &r, L"", 0, nullptr);
}

void ATUIPrinterCachePage::UpdatePixels(const vdrect32& dst, const void *data32, ptrdiff_t pitch) {
	if (dst.empty() || !mhdc)
		return;

	if (!pitch || (pitch & 3) || pitch > 0x10000 || pitch < -0x10000) {
		VDFAIL("Unsupported bitmap configuration");
		return;
	}

	const sint32 w = dst.width();
	const sint32 h = dst.height();

	BITMAPINFO bi {};
	bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bi.bmiHeader.biWidth = abs((int)pitch) >> 2;
	bi.bmiHeader.biHeight = h;
	bi.bmiHeader.biCompression = BI_RGB;
	bi.bmiHeader.biPlanes = 1;
	bi.bmiHeader.biBitCount = 32;

	// use top-down DIB as needed
	char *bitmapStart = (char *)data32;
	if (pitch >= 0)
		bi.bmiHeader.biHeight = -bi.bmiHeader.biHeight;
	else
		bitmapStart += pitch * (h - 1);

	VDVERIFY(::SetDIBitsToDevice(mhdc, dst.left, dst.top, w, h, 0, 0, 0, h, bitmapStart, &bi, DIB_RGB_COLORS));
}

void ATUIPrinterCachePage::Shutdown() {
	if (mhdc) {
		if (mhgoOldBitmap) {
			SelectObject(mhdc, mhgoOldBitmap);
			mhgoOldBitmap = nullptr;
		}

		DeleteDC(mhdc);
		mhdc = nullptr;
	}

	if (mhbm) {
		DeleteObject(mhbm);
		mhbm = nullptr;
	}
}

///////////////////////////////////////////////////////////////////////////////

class ATUIPrinterGraphicalOutputWindow final : public ATUINativeWindow {
public:
	ATUIPrinterGraphicalOutputWindow();

	void AttachToOutput(ATPrinterGraphicalOutput& output);

	void SetShowPrintHead(bool enable);

	void Clear();

	void ResetView();
	void SetPrintPosition(sint32 clientY);
	void SaveAsPNG(float dpi);
	void SaveAsPDF(float pageWidth, float pageHeight);
	void SaveAsSVG();

private:
	using TileLocation = vdpoint32;

	struct PendingRenderTile {
		TileLocation mTile { 0, 0 };
		vdrect32 mRefreshRect { 0, 0, 0, 0 };
	};

	struct CacheTile {
		TileLocation mTile { -1, -1 };
		vdrect32 mDirtyRect { 0, 0, 0, 0 };
	};

	LRESULT WndProc(UINT msg, WPARAM wParam, LPARAM lParam) override;
	LRESULT OnNCCalcSize(WPARAM wParam, LPARAM lParam);
	void OnDestroy();
	void OnSize();
	void OnKeyDown(uint32 vk, uint32 flags);
	void OnMouseMove(int x, int y);
	void OnMouseDownL(int x, int y);
	void OnMouseUpL(int x, int y);
	void OnMouseWheel(int x, int y, float delta);
	void OnMouseLeave();
	void OnCaptureChanged();
	bool OnGesture(WPARAM wParam, LPARAM lParam);
	void OnPaint();
	void OnTimer(uint32 id);
	void OnDpiChanged(int dpi);

	void OnOutputInvalidation();
	void OnPenChange(uint32 c);
	void OnHorizontalMove(double y);
	void OnVerticalMove(double y);

	vdrect32 ComputeVerticalCursorArea(float y) const;
	void RecomputeVerticalCursorArea();

	vdrect32 ComputeHorizontalCursorArea(float x, float y) const;
	void RecomputeHorizontalCursorArea();

	void UpdateViewOrigin();
	void ScrollByPixels(int dx, int dy);
	void ScrollByPixelsImmediate(int dx, int dy);
	void SetZoom(float clicks);
	void SetZoom(float clicks, const vdpoint32& centerPt);
	void UpdateZoom(const vdpoint32 *centerPt);

	void RequestLazyUpdate();
	void ForceFullInvalidation();
	void ProcessPendingInvalidation();
	void ProcessPendingVerticalMove();
	void InvalidateViewArea();
	void InvalidatePaperArea(float x1, float y1, float x2, float y2);
	void InvalidateCanvasArea(const vdrect32& r);

	void StartPendingRenders();
	void ProcessPendingRenders();
	void RenderTile(const PendingRenderTile& rtile);
	void RenderTiles(vdspan<const PendingRenderTile> rtiles);

	vdrect32 ClipCanvasRectToPaperArea(const vdrect32& r) const;
	static vdrect32 CanvasRectToOutsideTileRect(const vdrect32& r);
	static vdrect32 CanvasRectToInsideTileRect(const vdrect32& r);
	static vdrect32 TileRectToCanvasRect(const vdrect32& r);
	static vdrect32 TileToCanvasRect(const TileLocation& tloc);
	static vdrect32 CanvasRectToTileRelativeRect(const vdrect32& r, const TileLocation& tloc);
	static vdrect32 TileRelativeRectToCanvasRect(const vdrect32& r, const TileLocation& tloc);
	vdrect32 CanvasRectToViewportRect(const vdrect32& r) const;
	vdrect32 ViewportRectToCanvasRect(const vdrect32& r) const;

	struct CachePageTileAddress {
		uint32 mPageIndex;
		uint32 mPageTileX;
		uint32 mPageTileY;
	};

	CachePageTileAddress CacheTileToPageTile(uint32 tileIndex) const {
		return { tileIndex >> 4, tileIndex & 15, 0 };
	}

	CachePageTileAddress CacheTileToPageTile(const CacheTile& ct) const;

	CacheTile& AllocateCacheTile(const TileLocation& tloc);
	CacheTile& GetCacheTile(const TileLocation& tloc);

	vdrect32f GetDocumentBounds() const;

	float mPageWidthMM = 0;
	float mPageVBorderMM = 0;
	float mDotRadiusMM = 0;

	sint32 mViewOriginPixelX = 0;
	sint32 mViewOriginPixelY = 0;
	sint32 mViewCenterPixelX = 0;
	sint32 mViewCenterPixelY = 0;
	float mViewPixelsPerMM = 1.0f;
	float mViewMMPerPixel = 1.0f;
	sint32 mViewWidthPixels = 0;
	sint32 mViewHeightPixels = 0;
	sint32 mViewPaperWidthPixels = 0;
	vdrect32 mViewTileArea { 0, 0, 0, 0 };
	int mViewDpi = 96;

	float mViewCursorY = 0;
	float mViewCursorX = 10.0f;
	uint32 mPenColorLinear = 0;
	uint32 mPenColorSrgb = 0;

	bool mbShowPrintHead = false;
	bool mbDraggingView = false;
	bool mbDraggingCursor = false;
	bool mbHoveringCursor = false;
	sint32 mDragLastX = 0;
	sint32 mDragLastY = 0;
	float mDragViewCursorInitialY = 0;
	float mDragViewCursorCurrentY = 0;
	float mWheelAccum = 0;

	bool mbInGesture = false;
	bool mbFirstGestureEvent = false;
	vdpoint32 mGestureOrigin {0,0};
	float mGestureZoomOrigin = 0;

	// Every 5 zoom clicks is a power of 2, so this is a range of 0.25 - 32.0.
	// An 8.5" x 11" letter page (215.9 x 279.4 mm) at max zoom at 200% scaling
	// (192 dpi) is 52224 x 156058. This leaves us plenty of coordinate space
	// in 32-bit signed coordinates.
	static constexpr float kZoomMin = -10.0f;
	static constexpr float kZoomMax = 25.0f;
	float mZoomClicks = 0;

	UINT_PTR mLazyUpdateTimerId = 0;
	sint32 mLazyScrollDX = 0;
	sint32 mLazyScrollDY = 0;
	bool mbInvalidationPending = false;
	bool mbCursorMovePending = false;
	bool mbRenderPending = false;

	sint32 mCacheWidthTiles = 0;
	sint32 mCacheHeightTiles = 0;

	vdrect32 mHorizontalCursorArea { 0, 0, 0, 0 };
	vdrect32 mVerticalCursorArea { 0, 0, 0, 0 };

	uint32 mTotalPixelsRendered = 0;

	ATPrinterGraphicalOutput *mpOutput = nullptr;
	ATUICanvasW32 mCanvas;

	// Tiles that are awaiting rendering because of invalidations. These are
	// only queued for invalidations within the viewport tile area. However,
	// when renders occur, the entire dirty area for the tile is rendered,
	// and then a refresh is queued.
	vdvector<PendingRenderTile> mPendingRenders;

	vdvector<CacheTile> mCacheTiles;
	vdfastvector<uint32> mCacheTileMap;
	vdfastvector<bool> mPaintTileMap;

	template<typename T>
	auto *FindTile(T& container, const TileLocation& tloc) {
		auto it = std::find_if(container.begin(), container.end(),
			[=](const auto& tile) {
				return tile.mTile == tloc;
			}
		);

		return it != container.end() ? &*it : nullptr;
	}

	vdvector<vdrefptr<ATUIPrinterCachePage>> mCachePages;

	ATPrinterRasterizer mRasterizer;
};

ATUIPrinterGraphicalOutputWindow::ATUIPrinterGraphicalOutputWindow() {
}

void ATUIPrinterGraphicalOutputWindow::AttachToOutput(ATPrinterGraphicalOutput& output) {
	const ATPrinterGraphicsSpec& spec = output.GetGraphicsSpec();

	mpOutput = &output;
	mpOutput->SetOnInvalidation(
		[this] {
			OnOutputInvalidation();
		}
	);

	mpOutput->SetOnPenChange(
		[this](uint32 c) {
			OnPenChange(c);
		}
	);

	mpOutput->SetOnHorizontalMove(
		[this](double y) {
			OnHorizontalMove(y);
		}
	);

	mpOutput->SetOnVerticalMove(
		[this](double y) {
			OnVerticalMove(y);
		}
	);

	mViewCursorX = mpOutput->GetHorizontalPos();
	mViewCursorY = mpOutput->GetVerticalPos();

	mPageWidthMM = spec.mPageWidthMM;
	mPageVBorderMM = spec.mPageVBorderMM;
	mDotRadiusMM = spec.mDotRadiusMM;

	ResetView();
}

void ATUIPrinterGraphicalOutputWindow::SetShowPrintHead(bool enable) {
	if (mbShowPrintHead != enable) {
		mbShowPrintHead = enable;

		InvalidateArea(mHorizontalCursorArea);
		RecomputeHorizontalCursorArea();
		InvalidateArea(mHorizontalCursorArea);
	}
}

void ATUIPrinterGraphicalOutputWindow::Clear() {
	mViewCenterPixelY = 0;

	UpdateViewOrigin();

	if (mpOutput)
		mpOutput->Clear();
}

void ATUIPrinterGraphicalOutputWindow::ResetView() {
	mZoomClicks = -1000.0f;
	SetZoom(0);

	mLazyScrollDX = 0;
	mLazyScrollDY = 0;

	mViewCenterPixelX = VDRoundToInt32(mPageWidthMM * 0.5f * mViewPixelsPerMM);
	mViewCenterPixelY = VDRoundToInt32(mPageVBorderMM * mViewPixelsPerMM);

	UpdateViewOrigin();

	ForceFullInvalidation();
}

void ATUIPrinterGraphicalOutputWindow::SetPrintPosition(sint32 clientY) {
	// convert client Y position to view Y
	const float viewY = std::max<float>(0.0f, (float)(mViewOriginPixelY + clientY) * mViewMMPerPixel);

	// move if different
	if (mViewCursorY != viewY) {
		InvalidateArea(mVerticalCursorArea);
		InvalidateArea(mHorizontalCursorArea);

		mViewCursorY = viewY;

		RecomputeHorizontalCursorArea();
		RecomputeVerticalCursorArea();

		InvalidateArea(mHorizontalCursorArea);
		InvalidateArea(mVerticalCursorArea);

		if (mpOutput)
			mpOutput->SetVerticalPos(mViewCursorY);
	}
}

void ATUIPrinterGraphicalOutputWindow::SaveAsPNG(float dpi) {
	const VDStringW& fn = VDGetSaveFileName("PrinterSaveAsPNG"_vdtypeid, (VDGUIHandle)mhwnd, L"Save PNG image", L"PNG image\0*.png\0", L"png");
	if (fn.empty())
		return;

	// get bounds in document space
	vdrect32f documentBounds = GetDocumentBounds();

	// compute pixel bounds
	const float mmToInches = 1.0f / 25.4f;
	const sint32 w = std::max<sint32>(1, (sint32)ceilf(documentBounds.width() * mmToInches * dpi));
	const sint32 h = std::max<sint32>(1, (sint32)ceilf(documentBounds.height() * mmToInches * dpi));

	// render whole screen
	ATPrinterRasterizer::ViewTransform vt;
	vt.mOriginX = documentBounds.left;
	vt.mOriginY = documentBounds.top;
	vt.mPixelsPerMM = mmToInches * dpi;
	vt.mMMPerPixel = 1.0f / vt.mPixelsPerMM;
	const VDPixmap px = mRasterizer.Render(mpOutput, vt, 0, 0, w, h, true).value();

	// save as PNG
	ATSaveFrame(px, fn.c_str());

	// clear out large buffers
	mRasterizer.TrimBuffers();
}

void ATUIPrinterGraphicalOutputWindow::SaveAsPDF(float pageWidth, float pageHeight) {
	if (!mpOutput)
		return;

	const VDStringW& fn = VDGetSaveFileName("PrinterSaveAsPDF"_vdtypeid, (VDGUIHandle)mhwnd, L"Save PDF", L"PDF document\0*.pdf\0", L"pdf");
	if (fn.empty())
		return;

	ATPrinterExportAsPDF(fn.c_str(), *mpOutput, pageWidth, pageHeight);
}

void ATUIPrinterGraphicalOutputWindow::SaveAsSVG() {
	if (!mpOutput)
		return;

	const VDStringW& fn = VDGetSaveFileName("PrinterSaveAsSVG"_vdtypeid, (VDGUIHandle)mhwnd, L"Save SVG", L"SVG 1.1 document\0*.svg\0", L"svg");
	if (fn.empty())
		return;

	ATPrinterExportAsSVG(fn.c_str(), *mpOutput, true);
}

LRESULT ATUIPrinterGraphicalOutputWindow::WndProc(UINT msg, WPARAM wParam, LPARAM lParam) {
	switch(msg) {
		case WM_CREATE:
			mCanvas.Init(mhwnd);
			SetTouchMode(kATUITouchMode_2DPanSmooth);
			break;

		case WM_DESTROY:
			OnDestroy();
			break;

		case WM_SIZE:
			OnSize();
			return 0;

		case WM_NCCALCSIZE:
			return OnNCCalcSize(wParam, lParam);

		case WM_KEYDOWN:
			OnKeyDown(wParam, lParam);
			return 0;

		case WM_MOUSEMOVE:
			OnMouseMove(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
			return 0;

		case WM_LBUTTONDOWN:
		case WM_LBUTTONDBLCLK:
			Focus();
			OnMouseDownL(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
			return 0;

		case WM_LBUTTONUP:
			OnMouseUpL(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
			return 0;

		case WM_MOUSEWHEEL:
			OnMouseWheel(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), (float)GET_WHEEL_DELTA_WPARAM(wParam) / (float)WHEEL_DELTA);
			return 0;

		case WM_CAPTURECHANGED:
			OnCaptureChanged();
			return 0;

		case WM_PAINT:
			OnPaint();
			return 0;

		case WM_TIMER:
			OnTimer(wParam);
			return 0;

		case WM_ERASEBKGND:
			return 0;

		case WM_GESTURE:
			if (OnGesture(wParam, lParam))
				return 0;
			break;

		case WM_DPICHANGED:
		case ATWM_INHERIT_DPICHANGED:
			OnDpiChanged(wParam);
			break;

		case WM_USER + 100:
			ProcessPendingInvalidation();
			return 0;

		case WM_USER + 101:
			ProcessPendingVerticalMove();
			return 0;

		case WM_USER + 102:
			ProcessPendingRenders();
			return 0;
	}

	return ATUINativeWindow::WndProc(msg, wParam, lParam);
}

LRESULT ATUIPrinterGraphicalOutputWindow::OnNCCalcSize(WPARAM wParam, LPARAM lParam) {
	if (!wParam)
		return DefWindowProc(mhwnd, WM_NCCALCSIZE, wParam, lParam);

	NCCALCSIZE_PARAMS& params = *(NCCALCSIZE_PARAMS *)lParam;

	// ask DWP to compute the new client size
	DefWindowProc(mhwnd, WM_NCCALCSIZE, FALSE, (LPARAM)&params.rgrc[0]);

	// compute the source and destination client rect sizes
	sint32 neww = params.rgrc[0].right  - params.rgrc[0].left;
	sint32 newh = params.rgrc[0].bottom - params.rgrc[0].top;
	sint32 oldw = params.rgrc[2].right  - params.rgrc[2].left;
	sint32 oldh = params.rgrc[2].bottom - params.rgrc[2].top;

	// compute the difference in center offsets
	sint32 newcx = neww / 2;
	sint32 newcy = newh / 2;
	sint32 oldcx = oldw / 2;
	sint32 oldcy = oldh / 2;

	// compute the size to copy
	sint32 savecx = std::min<sint32>(oldcx, newcx);
	sint32 savecy = std::min<sint32>(oldcy, newcy);
	sint32 savew = std::min<sint32>(oldw, neww);
	sint32 saveh = std::min<sint32>(oldh, newh);

	// set source and dest copy rects
	params.rgrc[1].left   = params.rgrc[0].left + newcx - savecx;
	params.rgrc[1].top    = params.rgrc[0].top  + newcy - savecy;
	params.rgrc[1].right  = params.rgrc[1].left + savew;
	params.rgrc[1].bottom = params.rgrc[1].top  + saveh;

	params.rgrc[2].left   = params.rgrc[2].left + oldcx - savecx;
	params.rgrc[2].top    = params.rgrc[2].top  + oldcy - savecy;
	params.rgrc[2].right  = params.rgrc[2].left + savew;
	params.rgrc[2].bottom = params.rgrc[2].top  + saveh;

	return WVR_VALIDRECTS;
}

void ATUIPrinterGraphicalOutputWindow::OnDestroy() {
	if (mpOutput) {
		mpOutput->SetOnInvalidation(nullptr);
		mpOutput = nullptr;
	}
}

void ATUIPrinterGraphicalOutputWindow::OnSize() {
	auto [w, h] = GetClientSize();

	if (mViewWidthPixels != w || mViewHeightPixels != h) {
		mViewWidthPixels = w;
		mViewHeightPixels = h;

		// Compute the minimum number of cache pages needed. Note that we may need almost an entire
		// extra page depending on the sub-tile offset. A 128x128 (1 tile) viewport, for instance,
		// actually needs at least a 2x2 tile map.
		const sint32 tileWidthNeeded = ((w + 127) >> 7) + 1;
		const sint32 tileHeightNeeded = ((h + 127) >> 7) + 1;
		const uint32 tileCountNeeded = tileWidthNeeded * tileHeightNeeded;

		// determine the number of cache pages needed and allocate new ones if needed
		const uint32 tilePagesNeeded = (tileCountNeeded + 15) >> 4;

		if (mCachePages.size() < tilePagesNeeded) {
			HDC hdc = GetDC(mhwnd);

			while(mCachePages.size() < tilePagesNeeded) {
				mCachePages.emplace_back(vdmakerefcounted<ATUIPrinterCachePage>(hdc, 128*16, 128*1));
			}

			ReleaseDC(mhwnd, hdc);

			mCacheTiles.resize(tilePagesNeeded * 16);
		}

		// If the tile map is not the correct size, reshape it now.
		if (mCacheWidthTiles != tileWidthNeeded || mCacheHeightTiles != tileHeightNeeded) {
			mCacheWidthTiles = tileWidthNeeded;
			mCacheHeightTiles = tileHeightNeeded;

			mCacheTileMap.resize(tileCountNeeded);

			// for now, just invalidate the entire cache
			for(uint32 i = 0; i < tileCountNeeded; ++i) {
				mCacheTileMap[i] = i;
				mCacheTiles[i].mTile = TileLocation(-1, -1);
			}
		}

		// Update the view origin based on the center and new size. We don't redraw
		// here as that's already done by the NCCALCSIZE correction.
		UpdateViewOrigin();

		RecomputeHorizontalCursorArea();
		RecomputeVerticalCursorArea();
	}
}

void ATUIPrinterGraphicalOutputWindow::OnKeyDown(uint32 vk, uint32 flags) {
	const bool ctrl = GetKeyState(VK_CONTROL) < 0;

	switch(vk) {
		case VK_ESCAPE:
			if (ATGetUIPane(kATUIPaneId_Display))
				ATActivateUIPane(kATUIPaneId_Display, true);
			break;

		case VK_LEFT:
			ScrollByPixels(ctrl ? 1 : 100, 0);
			break;

		case VK_RIGHT:
			ScrollByPixels(ctrl ? -1 : -100, 0);
			break;

		case VK_UP:
			ScrollByPixels(0, ctrl ? 1 : 100);
			break;

		case VK_DOWN:
			ScrollByPixels(0, ctrl ? -1 : -100);
			break;

		case VK_PRIOR:
			ScrollByPixels(0, mViewHeightPixels);
			break;

		case VK_NEXT:
			ScrollByPixels(0, -mViewHeightPixels);
			break;

		case VK_OEM_PLUS:
			if (ctrl)
				SetZoom(mZoomClicks + 1.0f);
			break;

		case VK_OEM_MINUS:
			if (ctrl)
				SetZoom(mZoomClicks - 1.0f);
			break;

#ifdef ATNRELEASE
		case 'R':
			if (GetKeyState(VK_CONTROL) < 0 && GetKeyState(VK_SHIFT) < 0)
				Invalidate();
			break;

		case 'B':
			if (GetKeyState(VK_CONTROL) < 0 && GetKeyState(VK_SHIFT) < 0) {
				const auto start = VDGetPreciseTick();
				int iterations = 0;

				mTotalPixelsRendered = 0;

				for(uint32 tick = VDGetCurrentTick(); VDGetCurrentTick() - tick < 20000;) {
					for(int i=0; i<10; ++i) {
						InvalidateRect(mhwnd, nullptr, FALSE);
						InvalidateViewArea();
						ProcessPendingRenders();
						UpdateWindow(mhwnd);
					}

					iterations += 10;
				}

				const auto end = VDGetPreciseTick();
				const double elapsed = (double)(end - start) * VDGetPreciseSecondsPerTick();
				VDDEBUG2("%d iterations in %.2fms (%.2fms/iteration) | %u pixels rendered (%ux%u viewport)\n"
					, iterations
					, elapsed * 1000.0
					, elapsed * 1000.0 / (double)iterations
					, mTotalPixelsRendered
					, mViewWidthPixels
					, mViewHeightPixels
				);

				PostQuitMessage(0);
			}
			break;
#endif
	}
}

void ATUIPrinterGraphicalOutputWindow::OnMouseMove(int x, int y) {
	if (mbDraggingView) {
		const int dx = x - mDragLastX;
		const int dy = y - mDragLastY;
		mDragLastX = x;
		mDragLastY = y;

		ScrollByPixels(dx, dy);
	} else if (mbDraggingCursor) {
		// compute screen space vertical delta from origin (we do not update
		// drag last Y in this path)
		const int dy = y - mDragLastY;

		// compute new cursor Y in view space
		const float newViewY = std::max<float>(0.0f, mDragViewCursorInitialY + mViewMMPerPixel * (float)dy);
		mDragViewCursorCurrentY = newViewY;

		InvalidateArea(mVerticalCursorArea);
		mVerticalCursorArea = ComputeVerticalCursorArea(newViewY);
		InvalidateArea(mVerticalCursorArea);
	} else {
		const bool inCursor = mVerticalCursorArea.contains(vdpoint32(x, y));
		if (mbHoveringCursor != inCursor) {
			mbHoveringCursor = inCursor;

			InvalidateArea(mVerticalCursorArea);

			if (inCursor) {
				TRACKMOUSEEVENT tme {};
				tme.cbSize = sizeof(TRACKMOUSEEVENT);
				tme.dwFlags = TME_LEAVE;
				tme.hwndTrack = mhwnd;

				TrackMouseEvent(&tme);
			}
		}
	}
}

void ATUIPrinterGraphicalOutputWindow::OnMouseDownL(int x, int y) {
	mDragLastX = x;
	mDragLastY = y;

	if (mVerticalCursorArea.contains(vdpoint32(x, y))) {
		mbDraggingCursor = true;
		mDragViewCursorInitialY = mViewCursorY;
		mDragViewCursorCurrentY = mViewCursorY;

		::SetCapture(mhwnd);

		InvalidateArea(mVerticalCursorArea);
	} else {
		mbDraggingView = true;
		::SetCapture(mhwnd);
	}
}

void ATUIPrinterGraphicalOutputWindow::OnMouseUpL(int x, int y) {
	if (mbDraggingView) {
		mbDraggingView = false;
		::ReleaseCapture();
	} else if (mbDraggingCursor) {
		mbDraggingCursor = false;
		::ReleaseCapture();

		// force redraw to cancel the highlight
		InvalidateArea(mVerticalCursorArea);

		// commit the cursor movement
		if (mpOutput) {
			mpOutput->SetVerticalPos(mDragViewCursorCurrentY);
			mViewCursorY = mDragViewCursorCurrentY;
		}

		// recompute and update the horizontal cursor
		InvalidateArea(mHorizontalCursorArea);
		RecomputeHorizontalCursorArea();
		InvalidateArea(mHorizontalCursorArea);
	}
}

void ATUIPrinterGraphicalOutputWindow::OnMouseWheel(int x, int y, float delta) {
	// if there is a pending cursor drag, cancel it
	if (mbDraggingCursor)
		OnCaptureChanged();

	mWheelAccum += delta;

	const int clicks = (int)floorf(mWheelAccum + 0.5f);

	if (clicks) {
		const vdpoint32& cpt = TransformScreenToClient(vdpoint32(x, y));
		SetZoom(mZoomClicks + (float)clicks, cpt);

		mWheelAccum -= (int)clicks;
	}
}

void ATUIPrinterGraphicalOutputWindow::OnMouseLeave() {
	// cancel cursor hover effect if active
	if (mbHoveringCursor) {
		mbHoveringCursor = false;

		InvalidateArea(mVerticalCursorArea);
	}
}

void ATUIPrinterGraphicalOutputWindow::OnCaptureChanged() {
	mbDraggingView = false;

	if (mbDraggingCursor) {
		mbDraggingCursor = false;

		// cancel the cursor drag
		InvalidateArea(mVerticalCursorArea);
		mVerticalCursorArea = ComputeVerticalCursorArea(mViewCursorY);
		InvalidateArea(mVerticalCursorArea);
	}
}

bool ATUIPrinterGraphicalOutputWindow::OnGesture(WPARAM wParam, LPARAM lParam) {
	GESTUREINFO gestureInfo {sizeof(GESTUREINFO)};

	const BOOL haveInfo = GetGestureInfo((HGESTUREINFO)lParam, &gestureInfo);
	if (!haveInfo)
		return false;

	CloseGestureInfoHandle((HGESTUREINFO)lParam);

	switch(gestureInfo.dwID) {
		case GID_BEGIN:
			mbInGesture = true;
			mbFirstGestureEvent = true;
			break;

		case GID_END:
			mbInGesture = false;
			mbFirstGestureEvent = false;
			break;

		default:
			if (mbInGesture) {
				vdpoint32 pt(gestureInfo.ptsLocation.x, gestureInfo.ptsLocation.y);

				switch(gestureInfo.dwID) {
					case GID_PAN:
						if (!mbFirstGestureEvent)
							ScrollByPixels(pt.x - mGestureOrigin.x, pt.y - mGestureOrigin.y);

						mGestureOrigin = pt;
						break;

					case GID_ZOOM:
						if (float distance = (float)gestureInfo.ullArguments; mbFirstGestureEvent) {
							mGestureZoomOrigin = distance;
						} else {
							if (distance > mGestureZoomOrigin * 1.10f) {
								mGestureZoomOrigin *= 1.1487f;
								SetZoom(mZoomClicks + 1.0f);
							} else if (distance < mGestureZoomOrigin / 1.10f) {
								mGestureZoomOrigin /= 1.1487f;
								SetZoom(mZoomClicks - 1.0f);
							}
						}
						break;
				}

				mbFirstGestureEvent = false;
			}
			break;
	}

	return false;
}

void ATUIPrinterGraphicalOutputWindow::OnPaint() {
	PAINTSTRUCT ps;
	HDC hdc = mCanvas.BeginDirect(ps, false);

	if (!hdc)
		return;

	const auto updateRects = mCanvas.GetDetailedUpdateRects(hdc, ps);
	if (!updateRects.empty()) {
		vdrect32 paintRect { ps.rcPaint.left, ps.rcPaint.top, ps.rcPaint.right, ps.rcPaint.bottom };

		// convert left and right to document coordinates
		const float viewfx1 = (float)(mViewOriginPixelX + paintRect.left) * mViewMMPerPixel;
		const float viewfx2 = (float)(mViewOriginPixelX + paintRect.right) * mViewMMPerPixel;

		// if we are full left/right, just clear to background color
		SetBkColor(hdc, 0x808080);
		SetBkMode(hdc, OPAQUE);

		const auto clearRect = [&](const vdrect32& r) {
			RECT r2 { r.left, r.top, r.right, r.bottom };

			ExtTextOutW(hdc, 0, 0, ETO_OPAQUE, &r2, L"", 0, nullptr);
		};

		if (viewfx2 <= 0 || viewfx1 >= mPageWidthMM) {
			clearRect(paintRect);
		} else {
			// compute clipped page width within paint bounds, now that we know we won't
			// massively overflow
			vdrect32 pagePaintRect = paintRect;

			// check for clipping at top of page
			const float viewfy1 = (float)(mViewOriginPixelY + pagePaintRect.top) * mViewMMPerPixel;
			if (viewfy1 < 0) {
				const sint32 painty1 = (sint32)ceilf(-(float)mViewOriginPixelY - 0.5f / 8.0f);

				vdrect32 topRect = pagePaintRect;
				topRect.bottom = painty1;

				clearRect(topRect);

				pagePaintRect.top = painty1;
			}

			if (viewfx1 < 0) {
				const sint32 paintx1 = (sint32)ceilf(-(float)mViewOriginPixelX - 0.5f / 8.0f);

				vdrect32 leftRect = pagePaintRect;
				leftRect.right = paintx1;

				clearRect(leftRect);

				pagePaintRect.left = paintx1;
			}

			if (viewfx2 > mPageWidthMM) {
				const sint32 paintx2 = (sint32)ceilf(mPageWidthMM * mViewPixelsPerMM - mViewOriginPixelX - 7.5f / 8.0f);

				vdrect32 rightRect = pagePaintRect;
				rightRect.left = paintx2;

				clearRect(rightRect);

				pagePaintRect.right = paintx2;
			}

			// Paint page area. This part is more expensive as it covers rendered tiles, so we switch to
			// using the update rects. BitBlt() seems oddly expensive on Windows 11 -- some weird ETW
			// logging with stack walking mischief going on in the kernel -- so we use the granular update
			// rects to determine the tile coverage, but only do one BitBlt() each, relying on GDI to clip
			// to the update region.
			//
			// Our tiles are 128x128, so on a 4K display (3840x2160), the largest tile grid we normally
			// deal with is 31x18. L-shaped update regions are common due to 2D scrolling. Easiest thing
			// to do here is just to rasterize the update rects into a bitmap and then scan the bitmap.
			// This is skipped if there is only one update rect, in which case it is guaranteed that
			// every tile is included.

			if (!pagePaintRect.empty()) {
				SetBkColor(hdc, VDSwizzleU32(mRasterizer.GetPaperBackgroundColor()) >> 8);

				// convert paint rectangle to tile rect
				const vdrect32 pagePaintTileArea = CanvasRectToOutsideTileRect(ViewportRectToCanvasRect(pagePaintRect));

				// rasterize the update rects
				const size_t tileWidth = pagePaintTileArea.width();
				const size_t numTiles = tileWidth * pagePaintTileArea.height();
				mPaintTileMap.clear();

				if (updateRects.size() <= 1) {
					mPaintTileMap.resize(numTiles, true);
				} else {
					mPaintTileMap.resize(numTiles, false);

					for(vdrect32 updateRect : updateRects) {
						const vdrect32& updateTileRect = CanvasRectToOutsideTileRect(ViewportRectToCanvasRect(updateRect)) & pagePaintTileArea;

						for(sint32 tiley = updateTileRect.top; tiley < updateTileRect.bottom; ++tiley) {
							bool *dstRow = &mPaintTileMap[(tiley - pagePaintTileArea.top) * tileWidth];

							for(sint32 tilex = updateTileRect.left; tilex < updateTileRect.right; ++tilex) {
								dstRow[tilex - pagePaintTileArea.left] = true;
							}
						}
					}

				}

				// iterate over all tiles covered by the page paint area and an update rect
				const bool *tileFlag = mPaintTileMap.data();

				for(sint32 tiley = pagePaintTileArea.top; tiley < pagePaintTileArea.bottom; ++tiley) {
					for(sint32 tilex = pagePaintTileArea.left; tilex < pagePaintTileArea.right; ++tilex) {
						// skip tile if not covered by an update rect
						if (!*tileFlag++)
							continue;

						// compute tile rect in viewport coordinates
						const TileLocation tloc(tilex, tiley);
						const vdrect32 paintTileCanvasRect = TileToCanvasRect(tloc);
						const vdrect32 paintTileViewRect = CanvasRectToViewportRect(paintTileCanvasRect);

						// clip against the paint region; we don't want to paint partial tiles beyond the page margins
						const vdrect32 paintClippedTileViewRect = paintTileViewRect & pagePaintRect;

						// look up the cache tile
						CacheTile& ct = GetCacheTile(tloc);

						if (ct.mTile == tloc) {
							const auto& ctaddr = CacheTileToPageTile(ct);
							const auto& ctpage = mCachePages[ctaddr.mPageIndex];

							if (ctpage) {
								HDC hdcSrc = ctpage->GetCachePageDC();

								if (hdc) {
									// We are about to blit a tile. Check if we can extend the blit further
									// right.
									sint32 tileExtX = tilex + 1;
									while(tileExtX < pagePaintTileArea.right) {
										if (!*tileFlag)
											break;

										const TileLocation tlocExt(tileExtX, tiley);
										CacheTile& ctExt = GetCacheTile(tlocExt);

										if (ctExt.mTile != tlocExt)
											break;

										const auto& ctExtAddr = CacheTileToPageTile(ctExt);
										if (ctExtAddr.mPageIndex != ctaddr.mPageIndex ||
											ctExtAddr.mPageTileY != ctaddr.mPageTileY ||
											ctExtAddr.mPageTileX != ctaddr.mPageTileX + (tileExtX - tilex))
										{
											break;
										}

										++tileFlag;
										++tileExtX;
									}

									const sint32 cacheSrcX = (paintClippedTileViewRect.left - paintTileViewRect.left) + (ctaddr.mPageTileX << 7);
									const sint32 cacheSrcY = (paintClippedTileViewRect.top - paintTileViewRect.top) + (ctaddr.mPageTileY << 7);

									--tileExtX;
									if (tilex != tileExtX) {
										const sint32 extWidth = (tileExtX - tilex) * 128;
										tilex = tileExtX;

										vdrect32 paintTileExtViewRect = paintTileViewRect;
										paintTileExtViewRect.right += extWidth;

										const vdrect32 paintClippedTileExtViewRect = paintTileExtViewRect & pagePaintRect;

										::BitBlt(
											hdc,
											paintClippedTileExtViewRect.left,
											paintClippedTileExtViewRect.top,
											paintClippedTileExtViewRect.width(),
											paintClippedTileExtViewRect.height(),
											hdcSrc,
											cacheSrcX,
											cacheSrcY,
											SRCCOPY
										);
									} else {
										::BitBlt(
											hdc,
											paintClippedTileViewRect.left,
											paintClippedTileViewRect.top,
											paintClippedTileViewRect.width(),
											paintClippedTileViewRect.height(),
											hdcSrc,
											cacheSrcX,
											cacheSrcY,
											SRCCOPY
										);
									}
								} else {
									// The cache page exists but has no valid bitmap. Fill background.
									clearRect(paintClippedTileViewRect);
								}
							} else {
								// The cache page is invalid. Fill background.
								clearRect(paintClippedTileViewRect);
							}
						} else {
							// No cached tile exists. Fill background and queue an update.
							clearRect(paintClippedTileViewRect);

							InvalidateCanvasArea(paintTileCanvasRect);
						}
					}
				}
			}

			static constexpr bool kShowUpdateRects = false;

			if constexpr(kShowUpdateRects) {
				SelectObject(hdc, GetStockObject(DC_PEN));
				SelectObject(hdc, GetStockObject(NULL_BRUSH));
				static int phase = 0;
				SetDCPenColor(hdc, phase & 255);
				phase += 16;
				Rectangle(hdc, paintRect.left, paintRect.top, paintRect.right, paintRect.bottom);
			}
		}

		// Draw cursors
		if (!(mVerticalCursorArea & paintRect).empty()) {
			SelectObject(hdc, GetStockObject(NULL_PEN));
			SelectObject(hdc, GetStockObject(DC_BRUSH));

			static constexpr COLORREF kColorDefault = RGB(224, 224, 224);
			static constexpr COLORREF kColorHover = RGB(102, 163, 224);
			static constexpr COLORREF kColorDragging = RGB(255, 255, 255);
			SetDCBrushColor(hdc, mbDraggingCursor ? kColorDragging : mbHoveringCursor ? kColorHover : kColorDefault);

			POINT posPts[3] {
				{ mVerticalCursorArea.left, mVerticalCursorArea.bottom },
				{ mVerticalCursorArea.right, (mVerticalCursorArea.top + mVerticalCursorArea.bottom) >> 1 },
				{ mVerticalCursorArea.left, mVerticalCursorArea.top },
			};

			Polygon(hdc, posPts, 3);
		}

		if (!(mHorizontalCursorArea & paintRect).empty()) {
			SelectObject(hdc, GetStockObject(DC_PEN));
			SelectObject(hdc, GetStockObject(DC_BRUSH));

			static constexpr COLORREF kColorDefault = RGB(224, 224, 224);
			SetDCBrushColor(hdc, VDSwizzleU32(mPenColorSrgb) >> 8);
			SetDCPenColor(hdc, RGB(0, 0, 0));

			POINT posPts[3] {
				{ mHorizontalCursorArea.left + 1, mHorizontalCursorArea.bottom - 1 },
				{ (mHorizontalCursorArea.left + mHorizontalCursorArea.right) >> 1, mHorizontalCursorArea.top + 1 },
				{ mHorizontalCursorArea.right - 1, mHorizontalCursorArea.bottom - 1 },
			};

			Polygon(hdc, posPts, 3);
		}
	}

	mCanvas.EndDirect(ps);
}

void ATUIPrinterGraphicalOutputWindow::OnTimer(uint32 id) {
	KillTimer(mhwnd, mLazyUpdateTimerId);
	mLazyUpdateTimerId = 0;

	if (mLazyScrollDX || mLazyScrollDY) {
		const auto dx = std::exchange(mLazyScrollDX, 0);
		const auto dy = std::exchange(mLazyScrollDY, 0);

		ScrollByPixelsImmediate(dx, dy);
	}
}

void ATUIPrinterGraphicalOutputWindow::OnDpiChanged(int dpi) {
	if (dpi > 0 && dpi < 10000) {
		if (mViewDpi != dpi)
			UpdateZoom(nullptr);
	}
}

void ATUIPrinterGraphicalOutputWindow::OnOutputInvalidation() {
	if (!mbInvalidationPending) {
		mbInvalidationPending = true;

		::PostMessage(mhwnd, WM_USER + 100, 0, 0);
	}
}

void ATUIPrinterGraphicalOutputWindow::OnPenChange(uint32 c) {
	if (mPenColorLinear != c) {
		mPenColorLinear = c;

		mPenColorSrgb = mpOutput->ConvertLinearColorToSrgb(c);

		// The pen colors used are typically a bit dark, so brighten up the color
		// so it has good contrast.
		c = ((c & 0xFEFEFE) >> 1) + 0x808080;

		InvalidateArea(mHorizontalCursorArea);
	}
}

void ATUIPrinterGraphicalOutputWindow::OnHorizontalMove(double x) {
	if (mViewCursorX != (float)x) {
		mViewCursorX = (float)x;

		if (!mbCursorMovePending) {
			mbCursorMovePending = true;

			::PostMessage(mhwnd, WM_USER + 101, 0, 0);
		}
	}
}

void ATUIPrinterGraphicalOutputWindow::OnVerticalMove(double y) {
	if (mViewCursorY != (float)y) {
		mViewCursorY = (float)y;

		if (!mbCursorMovePending) {
			mbCursorMovePending = true;

			::PostMessage(mhwnd, WM_USER + 101, 0, 0);
		}
	}
}

vdrect32 ATUIPrinterGraphicalOutputWindow::ComputeVerticalCursorArea(float y) const {
	// compute metrics
	const sint32 offsetX = (mViewDpi * 8 + 48) / 96;
	const sint32 triW = (mViewDpi * 24 + 48) / 96;
	const sint32 triH2 = (mViewDpi * 14 + 48) / 96;

	// convert corner at left edge in view coordinates to client coordinates
	const float fpCornerX = -(float)mViewOriginPixelX - offsetX;
	const float fpCornerY = y * mViewPixelsPerMM - (float)mViewOriginPixelY;

	// collapse the rect if it is beyond a guard band distance of the edge
	const float guardX = (float)triW * 3.0f;
	const float guardY = (float)triH2 * 6.0f;

	if (fpCornerX < -guardX ||
		fpCornerY < -guardY ||
		fpCornerX > (float)mViewWidthPixels + guardX ||
		fpCornerY > (float)mViewHeightPixels + guardY)
	{
		return vdrect32(0,0,0,0);
	}

	// snap position to nearest pixel
	const sint32 cornerX = VDRoundToInt32(fpCornerX);
	const sint32 cornerY = VDRoundToInt32(fpCornerY);

	// return rect
	return vdrect32 { cornerX - triW, cornerY - triH2, cornerX, cornerY + triH2 };
}

void ATUIPrinterGraphicalOutputWindow::RecomputeVerticalCursorArea() {
	mVerticalCursorArea = ComputeVerticalCursorArea(mViewCursorY);
}

vdrect32 ATUIPrinterGraphicalOutputWindow::ComputeHorizontalCursorArea(float x, float y) const {
	if (!mbShowPrintHead)
		return vdrect32{};

	// compute metrics
	const sint32 offsetY = (mViewDpi * 8 + 48) / 96;
	const sint32 triW2 = (mViewDpi * 9 + 48) / 96;
	const sint32 triH = (mViewDpi * 16 + 48) / 96;

	// convert corner at left edge in view coordinates to client coordinates
	const float fpCornerX = x * mViewPixelsPerMM - (float)mViewOriginPixelX;
	const float fpCornerY = y * mViewPixelsPerMM - (float)mViewOriginPixelY + offsetY;

	// collapse the rect if it is beyond a guard band distance of the edge
	const float guardX = (float)triW2 * 6.0f;
	const float guardY = (float)triH * 3.0f;

	if (fpCornerX < -guardX ||
		fpCornerY < -guardY ||
		fpCornerX > (float)mViewWidthPixels + guardX ||
		fpCornerY > (float)mViewHeightPixels + guardY)
	{
		return vdrect32(0,0,0,0);
	}

	// snap position to nearest pixel
	const sint32 cornerX = VDRoundToInt32(fpCornerX);
	const sint32 cornerY = VDRoundToInt32(fpCornerY);

	// return rect
	return vdrect32 { cornerX - triW2, cornerY, cornerX + triW2, cornerY + triH };
}

void ATUIPrinterGraphicalOutputWindow::RecomputeHorizontalCursorArea() {
	mHorizontalCursorArea = ComputeHorizontalCursorArea(mViewCursorX, mViewCursorY);
}

void ATUIPrinterGraphicalOutputWindow::UpdateViewOrigin() {
	mViewOriginPixelX = mViewCenterPixelX - mViewWidthPixels  / 2;
	mViewOriginPixelY = mViewCenterPixelY - mViewHeightPixels / 2;

	const vdrect32 newViewTileArea = CanvasRectToOutsideTileRect(ViewportRectToCanvasRect(vdrect32(0, 0, mViewWidthPixels, mViewHeightPixels)));

	if (mViewTileArea != newViewTileArea) {
		const auto oldViewTileArea = mViewTileArea;
		mViewTileArea = newViewTileArea;

		// Remove any render requests for tiles that are no longer within the
		// viewport.
		mPendingRenders.erase(
			std::remove_if(
				mPendingRenders.begin(),
				mPendingRenders.end(),
				[=](const PendingRenderTile& rtile) {
					return !newViewTileArea.contains(rtile.mTile);
				}
			),
			mPendingRenders.end()
		);

		// Check for any tiles that have been added to the view rect and have
		// dirty rects but do not have pending renders queued. These need to
		// be requeued.
		for(sint32 tiley = newViewTileArea.top; tiley < newViewTileArea.bottom; ++tiley) {
			for(sint32 tilex = newViewTileArea.left; tilex < newViewTileArea.right; ++tilex) {
				const TileLocation tloc(tilex, tiley);

				if (oldViewTileArea.contains(tloc))
					continue;

				const CacheTile& ct = GetCacheTile(tloc);
				if (ct.mTile != tloc || ct.mDirtyRect.empty())
					continue;

				InvalidateCanvasArea(TileRelativeRectToCanvasRect(ct.mDirtyRect, tloc));
			}
		}
	}
}

void ATUIPrinterGraphicalOutputWindow::ScrollByPixels(int dx, int dy) {
	return ScrollByPixelsImmediate(dx, dy);
	if (dx || dy) {
		mLazyScrollDX += dx;
		mLazyScrollDY += dy;

		// Windows 11 can become atrociously slow if too many update regions are
		// accumulated in ScrollWindowEx(), so we make sure to buffer these
		// somewhat.
		RequestLazyUpdate();
	}
}

void ATUIPrinterGraphicalOutputWindow::ScrollByPixelsImmediate(int dx, int dy) {
	if (dx || dy) {
		mViewCenterPixelX -= dx;
		mViewCenterPixelY -= dy;
		UpdateViewOrigin();

		mCanvas.Scroll(dx, dy);

		if (mVerticalCursorArea.empty()) {
			RecomputeVerticalCursorArea();
		} else {
			mVerticalCursorArea.translate(dx, dy);

			if (mVerticalCursorArea.right <= 0 ||
				mVerticalCursorArea.bottom <= 0 ||
				mVerticalCursorArea.left >= mViewWidthPixels ||
				mVerticalCursorArea.top >= mViewHeightPixels)
			{
				mVerticalCursorArea.set(0,0,0,0);
			}
		}

		if (mHorizontalCursorArea.empty()) {
			RecomputeHorizontalCursorArea();
		} else {
			mHorizontalCursorArea.translate(dx, dy);

			if (mHorizontalCursorArea.right <= 0 ||
				mHorizontalCursorArea.bottom <= 0 ||
				mHorizontalCursorArea.left >= mViewWidthPixels ||
				mHorizontalCursorArea.top >= mViewHeightPixels)
			{
				mHorizontalCursorArea.set(0,0,0,0);
			}
		}

		// Force a full flush of all pending renders so we don't get flickering
		ProcessPendingRenders();

		UpdateWindow(mhwnd);
	}
}

void ATUIPrinterGraphicalOutputWindow::SetZoom(float clicks) {
	SetZoom(clicks, vdpoint32(mViewWidthPixels / 2, mViewHeightPixels / 2));
}

void ATUIPrinterGraphicalOutputWindow::SetZoom(float clicks, const vdpoint32& centerPt) {
	float newZoom = std::clamp(clicks, kZoomMin, kZoomMax);

	if (fabsf(mZoomClicks - newZoom) < 0.0001f)
		return;

	mZoomClicks = newZoom;
	mLazyScrollDX = 0;
	mLazyScrollDY = 0;

	UpdateZoom(&centerPt);
}

void ATUIPrinterGraphicalOutputWindow::UpdateZoom(const vdpoint32* centerPt) {
	float basePixelsPerMM = (float)mViewDpi / 25.4f;
	float newPixelsPerMM = basePixelsPerMM * powf(2.0f, (float)mZoomClicks / 5.0f);

	if (fabsf(mViewPixelsPerMM - newPixelsPerMM) > 1e-3f*std::max(mViewPixelsPerMM, newPixelsPerMM)) {
		const float pivotDX = centerPt ? centerPt->x - mViewWidthPixels / 2 : 0.0f;
		const float pivotDY = centerPt ? centerPt->y - mViewHeightPixels / 2 : 0.0f;

		float anchorX = mViewMMPerPixel * ((float)mViewCenterPixelX + pivotDX);
		float anchorY = mViewMMPerPixel * ((float)mViewCenterPixelY + pivotDY);

		mViewPixelsPerMM = newPixelsPerMM;
		mViewMMPerPixel = 1.0f / mViewPixelsPerMM;

		mViewPaperWidthPixels = VDRoundToInt32(newPixelsPerMM * mPageWidthMM);

		mViewCenterPixelX = VDRoundToInt32(anchorX * mViewPixelsPerMM - pivotDX);
		mViewCenterPixelY = VDRoundToInt32(anchorY * mViewPixelsPerMM - pivotDY);
		UpdateViewOrigin();

		ForceFullInvalidation();
		UpdateWindow(mhwnd);
	}
}

void ATUIPrinterGraphicalOutputWindow::RequestLazyUpdate() {
	if (!mLazyUpdateTimerId)
		mLazyUpdateTimerId = SetTimer(mhwnd, 1, 10, nullptr);
}

void ATUIPrinterGraphicalOutputWindow::ForceFullInvalidation() {
	mLazyScrollDX = 0;
	mLazyScrollDY = 0;

	// invalidate all cache tiles
	for(CacheTile& ct : mCacheTiles)
		ct.mTile = TileLocation(-1, -1);

	InvalidateRect(mhwnd, nullptr, false);

	mVerticalCursorArea = ComputeVerticalCursorArea(mViewCursorY);
	mHorizontalCursorArea = ComputeHorizontalCursorArea(mViewCursorX, mViewCursorY);

	if (mpOutput) {
		bool all;
		vdrect32f r;
		mpOutput->ExtractInvalidationRect(all, r);
	}

	// Force a full flush of all pending renders so we don't get flickering
	InvalidateViewArea();
	ProcessPendingRenders();
}

void ATUIPrinterGraphicalOutputWindow::ProcessPendingInvalidation() {
	mbInvalidationPending = false;

	if (mpOutput) {
		bool invAll;
		vdrect32f invRect;

		if (mpOutput->ExtractInvalidationRect(invAll, invRect)) {
			if (invAll) {
				// invalidate all cache tiles
				for(CacheTile& ct : mCacheTiles)
					ct.mTile = TileLocation(-1, -1);

				InvalidateRect(mhwnd, nullptr, false);
			} else {
				InvalidatePaperArea(invRect.left, invRect.top, invRect.right, invRect.bottom);
			}
		}
	}
}

void ATUIPrinterGraphicalOutputWindow::ProcessPendingVerticalMove() {
	mbCursorMovePending = false;

	// skip view update if dragging is occurring
	if (!mbDraggingCursor) {
		const vdrect32 newVCursorArea = ComputeVerticalCursorArea(mViewCursorY);
		if (mVerticalCursorArea != newVCursorArea) {
			InvalidateArea(mVerticalCursorArea);
			mVerticalCursorArea = newVCursorArea;
			InvalidateArea(mVerticalCursorArea);
		}

		const vdrect32 newHCursorArea = ComputeHorizontalCursorArea(mViewCursorX, mViewCursorY);
		if (mHorizontalCursorArea != newHCursorArea) {
			InvalidateArea(mHorizontalCursorArea);
			mHorizontalCursorArea = newHCursorArea;
			InvalidateArea(mHorizontalCursorArea);
		}
	}
}

void ATUIPrinterGraphicalOutputWindow::InvalidateViewArea() {
	InvalidateCanvasArea(ViewportRectToCanvasRect(vdrect32(0, 0, mViewWidthPixels, mViewHeightPixels)));
}

void ATUIPrinterGraphicalOutputWindow::InvalidatePaperArea(float x1, float y1, float x2, float y2) {
	if (x1 >= x2 || y1 >= y2)
		return;

	sint32 x1i = ceilf(x1 * mViewPixelsPerMM - 0.5f - 3.5f / 8.0f);
	sint32 y1i = ceilf(y1 * mViewPixelsPerMM - 0.5f - 3.5f / 8.0f);
	sint32 x2i = ceilf(x2 * mViewPixelsPerMM - 0.5f + 3.5f / 8.0f);
	sint32 y2i = ceilf(y2 * mViewPixelsPerMM - 0.5f + 3.5f / 8.0f);

	InvalidateCanvasArea(vdrect32(x1i, y1i, x2i, y2i));
}

void ATUIPrinterGraphicalOutputWindow::InvalidateCanvasArea(const vdrect32& r0) {
	const vdrect32 r = ClipCanvasRectToPaperArea(r0);

	// early out if the canvas rect is empty
	if (r.empty())
		return;

	const vdrect32 invTileArea = CanvasRectToOutsideTileRect(r);

	// queue dirty rects for any tiles covered by the invalidation -- note that
	// some of these may not end up rendered if they're not within the viewport
	for(sint32 tiley = invTileArea.top; tiley < invTileArea.bottom; ++tiley) {
		for(sint32 tilex = invTileArea.left; tilex < invTileArea.right; ++tilex) {
			const TileLocation tloc(tilex, tiley);

			CacheTile& ct = GetCacheTile(tloc);
			if (ct.mTile == tloc) {
				// compute intersection of invalidation area with tile area
				ct.mDirtyRect |= CanvasRectToTileRelativeRect(r, ct.mTile);
			}
		}
	}

	// queue render requests for each tile covered by the refresh rectangle
	const vdrect32 tileRect = CanvasRectToOutsideTileRect(r) & mViewTileArea;

	for(sint32 tiley = tileRect.top; tiley < tileRect.bottom; ++tiley) {
		for(sint32 tilex = tileRect.left; tilex < tileRect.right; ++tilex) {
			const TileLocation tloc(tilex, tiley);

			const vdrect32& tileRefreshRect = CanvasRectToTileRelativeRect(r, tloc);

			PendingRenderTile *rtile = FindTile(mPendingRenders, tloc);
			if (!rtile) {
				rtile = &mPendingRenders.emplace_back();
				rtile->mTile = tloc;
				rtile->mRefreshRect = tileRefreshRect;
			} else {
				rtile->mRefreshRect |= tileRefreshRect;
			}
		}
	}

	StartPendingRenders();
}

void ATUIPrinterGraphicalOutputWindow::StartPendingRenders() {
	if (!mbRenderPending) {
		mbRenderPending = true;

		::PostMessage(mhwnd, WM_USER + 102, 0, 0);
	}
}

void ATUIPrinterGraphicalOutputWindow::ProcessPendingRenders() {
	if (!mbRenderPending)
		return;

	mbRenderPending = false;

	if (mPendingRenders.empty())
		return;

	// sort tiles by Y and then X so we can coalesce spans, which is super
	// important for performance
	std::sort(
		mPendingRenders.begin(),
		mPendingRenders.end(),
		[](const PendingRenderTile& a, const PendingRenderTile& b) {
			if (a.mTile.y != b.mTile.y)
				return a.mTile.y < b.mTile.y;

			return a.mTile.x < b.mTile.x;
		}
	);

	// render spans
	vdrect32 fullTileRect(0, 0, 128, 128);

	auto it = mPendingRenders.begin(), end = mPendingRenders.end();
	while(it != end) {
		const PendingRenderTile& rt = *it;
		const CacheTile& ct = AllocateCacheTile(rt.mTile);

		if (ct.mDirtyRect == fullTileRect) {
			auto it2 = it + 1;

			while(it2 != end) {
				if (it2->mTile.y != it->mTile.y)
					break;

				if (it2->mTile.x != it2[-1].mTile.x + 1)
					break;

				const CacheTile& ct2 = AllocateCacheTile(it2->mTile);
				if (ct2.mDirtyRect != fullTileRect)
					break;

				++it2;
			}

			if (it2 != it + 1) {
				RenderTiles(vdspan(it, it2));
				it = it2;
				continue;
			}
		}

		RenderTile(rt);
		++it;
	}

	mPendingRenders.clear();
}

void ATUIPrinterGraphicalOutputWindow::RenderTile(const PendingRenderTile& rtile) {
	ATPrinterRasterizer::ViewTransform viewTransform;
	viewTransform.mOriginX = 0;
	viewTransform.mOriginY = 0;
	viewTransform.mMMPerPixel = mViewMMPerPixel;
	viewTransform.mPixelsPerMM = mViewPixelsPerMM;

	CacheTile& ct = AllocateCacheTile(rtile.mTile);
	const vdrect32& tileDirtyCanvasRect = TileRelativeRectToCanvasRect(ct.mDirtyRect, ct.mTile);

	mTotalPixelsRendered += tileDirtyCanvasRect.width() * tileDirtyCanvasRect.height();

	// render the dirty rect for the tile
	const auto result = mRasterizer.Render(
		mpOutput,
		viewTransform,
		tileDirtyCanvasRect.left,
		tileDirtyCanvasRect.top,
		tileDirtyCanvasRect.width(),
		tileDirtyCanvasRect.height(),
		false);

	// blit the result into the cache page
	const auto& ctaddr = CacheTileToPageTile(ct);
	ATUIPrinterCachePage& cp = *mCachePages[ctaddr.mPageIndex];

	const vdrect32 pageDirtyRect = ct.mDirtyRect.translated(ctaddr.mPageTileX << 7, ctaddr.mPageTileY << 7);

	if (result.has_value()) {
		cp.UpdatePixels(pageDirtyRect, result->data, result->pitch);
	} else {
		cp.ClearPixels(pageDirtyRect, mRasterizer.GetPaperBackgroundColor());
	}

	// clear the dirty rect
	ct.mDirtyRect = vdrect32(0, 0, 0, 0);

	// compute refresh area
	vdrect32 refreshArea = CanvasRectToViewportRect(TileRelativeRectToCanvasRect(rtile.mRefreshRect, rtile.mTile)) & vdrect32(0, 0, mViewWidthPixels, mViewHeightPixels);

	if (!refreshArea.empty())
		InvalidateArea(refreshArea);
}

void ATUIPrinterGraphicalOutputWindow::RenderTiles(const vdspan<const PendingRenderTile> rtiles) {
	if (rtiles.empty())
		return;

	const sint32 n = (sint32)rtiles.size();

	VDASSERT(rtiles.front().mTile.y == rtiles.back().mTile.y);
	VDASSERT(rtiles.front().mTile.x + n - 1 == rtiles.back().mTile.x);

	ATPrinterRasterizer::ViewTransform viewTransform;
	viewTransform.mOriginX = 0;
	viewTransform.mOriginY = 0;
	viewTransform.mMMPerPixel = mViewMMPerPixel;
	viewTransform.mPixelsPerMM = mViewPixelsPerMM;

	const vdrect32& tileDirtyCanvasRect = TileRelativeRectToCanvasRect(vdrect32(0, 0, 128*n, 128), rtiles.front().mTile);

	mTotalPixelsRendered += tileDirtyCanvasRect.width() * tileDirtyCanvasRect.height();

	// render the dirty rect for the tile
	const auto result = mRasterizer.Render(
		mpOutput,
		viewTransform,
		tileDirtyCanvasRect.left,
		tileDirtyCanvasRect.top,
		tileDirtyCanvasRect.width(),
		tileDirtyCanvasRect.height(),
		false);

	// blit the results into the cache pages
	for(sint32 i = 0; i < n; ++i) {
		const PendingRenderTile& rtile = rtiles[i];

		CacheTile& ct = AllocateCacheTile(rtile.mTile);
		const auto& ctaddr = CacheTileToPageTile(ct);
		ATUIPrinterCachePage& cp = *mCachePages[ctaddr.mPageIndex];

		const vdrect32 pageDirtyRect = ct.mDirtyRect.translated(ctaddr.mPageTileX << 7, ctaddr.mPageTileY << 7);

		if (result.has_value()) {
			cp.UpdatePixels(pageDirtyRect, (const char *)result->data + 4 * 128 * i, result->pitch);
		} else {
			cp.ClearPixels(pageDirtyRect, mRasterizer.GetPaperBackgroundColor());
		}

		// clear the dirty rect
		ct.mDirtyRect = vdrect32(0, 0, 0, 0);

		// compute refresh area
		vdrect32 refreshArea = CanvasRectToViewportRect(TileRelativeRectToCanvasRect(rtile.mRefreshRect, rtile.mTile)) & vdrect32(0, 0, mViewWidthPixels, mViewHeightPixels);

		if (!refreshArea.empty())
			InvalidateArea(refreshArea);
	}
}

vdrect32 ATUIPrinterGraphicalOutputWindow::ClipCanvasRectToPaperArea(const vdrect32& r) const {
	return vdrect32 {
		std::max(r.left, 0),
		std::max(r.top, 0),
		std::min(r.right, mViewPaperWidthPixels),
		r.bottom
	};
}

vdrect32 ATUIPrinterGraphicalOutputWindow::CanvasRectToOutsideTileRect(const vdrect32& r) {
	if (r.empty())
		return vdrect32(0, 0, 0, 0);

	return vdrect32(
		r.left >> 7,
		r.top >> 7,
		(r.right + 127) >> 7,
		(r.bottom + 127) >> 7
	);
}

vdrect32 ATUIPrinterGraphicalOutputWindow::CanvasRectToInsideTileRect(const vdrect32& r) {
	return vdrect32(
		(r.left + 127) >> 7,
		(r.top + 127) >> 7,
		r.right >> 7,
		r.bottom >> 7);
}

vdrect32 ATUIPrinterGraphicalOutputWindow::TileRectToCanvasRect(const vdrect32& r) {
	return vdrect32(
		r.left << 7,
		r.top << 7,
		r.right << 7,
		r.bottom << 7);
}

vdrect32 ATUIPrinterGraphicalOutputWindow::TileToCanvasRect(const TileLocation& tloc) {
	const sint32 x = tloc.x << 7;
	const sint32 y = tloc.y << 7;
	return vdrect32(x, y, x + 128, y + 128);
}

vdrect32 ATUIPrinterGraphicalOutputWindow::CanvasRectToTileRelativeRect(const vdrect32& r, const TileLocation& tloc) {
	return r.translated(-(tloc.x << 7), -(tloc.y << 7)) & vdrect32(0, 0, 128, 128);
}

vdrect32 ATUIPrinterGraphicalOutputWindow::TileRelativeRectToCanvasRect(const vdrect32& r, const TileLocation& tloc) {
	return r.translated(tloc.x << 7, tloc.y << 7);
}

vdrect32 ATUIPrinterGraphicalOutputWindow::CanvasRectToViewportRect(const vdrect32& r) const {
	return r.translated(-mViewOriginPixelX, -mViewOriginPixelY);
}

vdrect32 ATUIPrinterGraphicalOutputWindow::ViewportRectToCanvasRect(const vdrect32& r) const {
	return r.translated(mViewOriginPixelX, mViewOriginPixelY);
}

ATUIPrinterGraphicalOutputWindow::CachePageTileAddress ATUIPrinterGraphicalOutputWindow::CacheTileToPageTile(const CacheTile& ct) const {
	return CacheTileToPageTile((uint32)(&ct - mCacheTiles.data()));
}

ATUIPrinterGraphicalOutputWindow::CacheTile& ATUIPrinterGraphicalOutputWindow::AllocateCacheTile(const TileLocation& tloc) {
	VDASSERT(tloc.x >= 0);

	CacheTile& ct = GetCacheTile(tloc);
	if (ct.mTile != tloc) {
		ct.mTile = tloc;
		ct.mDirtyRect = vdrect32(0, 0, 128, 128);
	}

	return ct;
}

ATUIPrinterGraphicalOutputWindow::CacheTile& ATUIPrinterGraphicalOutputWindow::GetCacheTile(const TileLocation& tloc) {
	sint32 vpTileX = tloc.x % mCacheWidthTiles;
	sint32 vpTileY = tloc.y % mCacheHeightTiles;

	if (vpTileX < 0)
		vpTileX += mCacheWidthTiles;

	if (vpTileY < 0)
		vpTileY += mCacheHeightTiles;

	const uint32 cacheTileIndex = mCacheTileMap[vpTileX + vpTileY * mCacheWidthTiles];
	return mCacheTiles[cacheTileIndex];
}

vdrect32f ATUIPrinterGraphicalOutputWindow::GetDocumentBounds() const {
	if (mpOutput)
		return mpOutput->GetDocumentBounds();
	else
		return vdrect32f(0, 0, 0, 0);
}

///////////////////////////////////////////////////////////////////////////////

struct ATPrinterOutputWindow::PrinterOutputSort {
	bool operator()(ATPrinterOutputBase *p, ATPrinterOutputBase *q) const {
		return vdwcsicmp(p->GetName(), q->GetName()) < 0;
	}
};

///////////////////////////////////////////////////////////////////////////////

ATPrinterOutputWindow::ATPrinterOutputWindow()
	: ATUIPaneWindow(kATUIPaneId_PrinterOutput, L"Printer Output")
	, mhwndTextEditor(NULL)
	, mLineBufIdx(0)
{
	mPreferredDockCode = kATContainerDockBottom;

	mAddedOutputFn = [this](ATPrinterOutput& output) { OnAddedOutput(output); };
	mRemovingOutputFn = [this](ATPrinterOutput& output) { OnRemovingOutput(output); };
	mAddedGraphicalOutputFn = [this](ATPrinterGraphicalOutput& output) { OnAddedGraphicalOutput(output); };
	mRemovingGraphicalOutputFn = [this](ATPrinterGraphicalOutput& output) { OnRemovingGraphicalOutput(output); };

	mToolbar.SetOnClicked([this](uint32 id) { OnToolbarItemClicked(id); });
}

ATPrinterOutputWindow::~ATPrinterOutputWindow() {
}

LRESULT ATPrinterOutputWindow::WndProc(UINT msg, WPARAM wParam, LPARAM lParam) {
	if (VDZLRESULT r {}; mDispatcher.TryDispatch(msg, wParam, lParam, r))
		return r;

	switch(msg) {
		case WM_SIZE:
			OnSize();
			break;

		case WM_CONTEXTMENU:
			try {
				int x = GET_X_LPARAM(lParam);
				int y = GET_Y_LPARAM(lParam);

				HMENU menu0 = LoadMenu(NULL, mpGraphicWindow ? MAKEINTRESOURCE(IDR_PRINTER_GRAPHIC_CONTEXT_MENU) : MAKEINTRESOURCE(IDR_PRINTER_CONTEXT_MENU));
				if (menu0) {
					HMENU menu = GetSubMenu(menu0, 0);
					BOOL cmd = 0;

					if (x == -1 && y == -1) {
						const vdpoint32& pt = mpGraphicWindow
							? mpGraphicWindow->TransformClientToScreen(vdpoint32(0, 0))
							: mpTextEditor->GetScreenPosForContextMenu();

						x = pt.x;
						y = pt.y;
					} else {
						POINT pt = {x, y};

						if (!mpGraphicWindow && ScreenToClient(mhwndTextEditor, &pt))
							mpTextEditor->SetCursorPixelPos(pt.x, pt.y);
					}

					cmd = TrackPopupMenu(menu, TPM_LEFTALIGN|TPM_TOPALIGN|TPM_RETURNCMD, x, y, 0, mhwnd, NULL);

					DestroyMenu(menu0);

					switch(cmd) {
						case ID_CONTEXT_CLEAR:
							Clear();
							break;

						case ID_CONTEXT_RESETVIEW:
							if (mpGraphicWindow)
								mpGraphicWindow->ResetView();
							break;

						case ID_CONTEXT_SETPRINTPOSITION:
							if (mpGraphicWindow)
								mpGraphicWindow->SetPrintPosition(mpGraphicWindow->TransformScreenToClient(vdpoint32{x, y}).y);
							break;

						case ID_SAVEAS_PNGIMAGE96:
							if (mpGraphicWindow)
								mpGraphicWindow->SaveAsPNG(96.0f);
							break;

						case ID_SAVEAS_PNGIMAGE300:
							if (mpGraphicWindow)
								mpGraphicWindow->SaveAsPNG(300.0f);
							break;

						case ID_PDFDOCUMENT_AUTO:
							if (mpGraphicWindow)
								mpGraphicWindow->SaveAsPDF(0.0f, 0.0f);
							break;

						case ID_PDFDOCUMENT_LETTER:
							if (mpGraphicWindow)
								mpGraphicWindow->SaveAsPDF(216.0f, 279.4f);
							break;

						case ID_PDFDOCUMENT_A4:
							if (mpGraphicWindow)
								mpGraphicWindow->SaveAsPDF(210.0f, 297.0f);
							break;

						case ID_PDFDOCUMENT_CONT12:
							if (mpGraphicWindow)
								mpGraphicWindow->SaveAsPDF(210.0f, 304.8f);
							break;

						case ID_SAVEAS_SVG:
							if (mpGraphicWindow)
								mpGraphicWindow->SaveAsSVG();
							break;
					}
				}
			} catch(const VDException& ex) {
				if (ex.visible())
					VDDialogFrameW32::ShowError((VDGUIHandle)mhwnd, ex.wc_str(), nullptr);
			}
			break;
	}

	return ATUIPaneWindow::WndProc(msg, wParam, lParam);
}

bool ATPrinterOutputWindow::OnCreate() {
	if (!ATUIPaneWindow::OnCreate())
		return false;

	if (!VDCreateTextEditor(~mpTextEditor))
		return false;

	mhwndToolbar = CreateWindow(TOOLBARCLASSNAME, _T(""), WS_CHILD | WS_VISIBLE | TBSTYLE_LIST | TBSTYLE_FLAT | TBSTYLE_CUSTOMERASE, 0, 0, 0, 0, mhwnd, (HMENU)kControlId_Toolbar, VDGetLocalModuleHandleW32(), NULL);
	if (!mhwndToolbar)
		return false;

	mToolbar.Attach(mhwndToolbar);
	mDispatcher.AddControl(&mToolbar);

	mToolbar.SetDarkModeEnabled(true);
	mToolbar.AddDropdownButton(kControlId_Output, -1, L"");
	mToolbar.AddSeparator();
	mToolbar.AddButton(kControlId_Clear, -1, L"Clear");
	mToolbar.AddButton(kControlId_ResetView, -1, L"Reset View");
	mToolbar.AddButton(kControlId_ShowPrintHead, -1, L"Show Print Head");

	mToolbar.SetItemPressed(kControlId_ShowPrintHead, mbShowPrintHead);

	mhwndTextEditor = (HWND)mpTextEditor->Create(WS_EX_NOPARENTNOTIFY, WS_CHILD|WS_VISIBLE, 0, 0, 0, 0, (VDGUIHandle)mhwnd, kControlId_TextEditor);

	OnFontsUpdated();

	mpTextEditor->SetReadOnly(true);

	OnSize();

	mpOutputMgr = static_cast<ATPrinterOutputManager *>(&g_sim.GetPrinterOutputManager());
	mpOutputMgr->OnAddedOutput.Add(&mAddedOutputFn);
	mpOutputMgr->OnRemovingOutput.Add(&mRemovingOutputFn);
	mpOutputMgr->OnAddedGraphicalOutput.Add(&mAddedGraphicalOutputFn);
	mpOutputMgr->OnRemovingGraphicalOutput.Add(&mRemovingGraphicalOutputFn);

	// enumerate pre-existing outputs
	for(uint32 i = 0, n = mpOutputMgr->GetOutputCount(); i < n; ++i) {
		mPrinterOutputs.emplace_back(&mpOutputMgr->GetOutput(i));
	}

	for(uint32 i = 0, n = mpOutputMgr->GetGraphicalOutputCount(); i < n; ++i) {
		mPrinterOutputs.emplace_back(&mpOutputMgr->GetGraphicalOutput(i));
	}

	std::sort(mPrinterOutputs.begin(), mPrinterOutputs.end(), PrinterOutputSort());

	UpdateToolbarForOutput();
	AttachToAnyOutput();

	return true;
}

void ATPrinterOutputWindow::OnDestroy() {
	DetachFromTextOutput();
	DetachFromGraphicsOutput();

	if (mpOutputMgr) {
		mpOutputMgr->OnAddedOutput.Remove(&mAddedOutputFn);
		mpOutputMgr->OnRemovingOutput.Remove(&mRemovingOutputFn);
		mpOutputMgr->OnAddedGraphicalOutput.Remove(&mAddedGraphicalOutputFn);
		mpOutputMgr->OnRemovingGraphicalOutput.Remove(&mRemovingGraphicalOutputFn);
		mpOutputMgr = nullptr;
	}

	if (mhwndTextEditor) {
		DestroyWindow(mhwndTextEditor);
		mhwndTextEditor = nullptr;
	}

	if (mhwndToolbar) {
		mDispatcher.RemoveControl(mhwndToolbar);
		mToolbar.Detach();

		DestroyWindow(mhwndToolbar);
		mhwndToolbar = nullptr;
	}

	ATUIPaneWindow::OnDestroy();
}

void ATPrinterOutputWindow::OnSize() {
	mToolbar.AutoSize();

	vdrect32 rToolbar = mToolbar.GetWindowArea();
	vdrect32 r = GetClientArea();

	rToolbar.left = 0;
	rToolbar.right = r.right;
	rToolbar.bottom -= rToolbar.top;
	rToolbar.top = 0;

	mToolbar.SetArea(rToolbar);

	vdrect32 r2(r);
	r2.top = rToolbar.bottom;
	r2.bottom = std::max<sint32>(r2.top, r.bottom);

	if (mpGraphicWindow) {
		mpGraphicWindow->SetArea(r2);
	} else if (mhwndTextEditor) {
		ATUINativeWindowProxy proxy(mhwndTextEditor);

		proxy.SetArea(r2);
	}
}

void ATPrinterOutputWindow::OnFontsUpdated() {
	if (mhwndTextEditor)
		SendMessage(mhwndTextEditor, WM_SETFONT, (WPARAM)ATGetConsoleFontW32(), TRUE);
}

void ATPrinterOutputWindow::OnSetFocus() {
	if (mpGraphicWindow)
		mpGraphicWindow->Focus();
	else
		::SetFocus(mhwndTextEditor);
}

void ATPrinterOutputWindow::OnToolbarItemClicked(uint32 id) {
	if (id == kControlId_Output) {
		size_t n = mPrinterOutputs.size();

		vdfastvector<const wchar_t *> items(n + 1, nullptr);
		for(size_t i = 0; i < n; ++i)
			items[i] = mPrinterOutputs[i]->GetName();

		sint32 selectedIdx = mToolbar.ShowDropDownMenu(id, items.data());
		if (selectedIdx >= 0 && (size_t)selectedIdx < n) {
			ATPrinterOutputBase *selectedOutput = mPrinterOutputs[selectedIdx];

			if (IATPrinterOutput *textOutput = vdpoly_cast<IATPrinterOutput *>(selectedOutput)) {
				AttachToTextOutput(*static_cast<ATPrinterOutput *>(textOutput));
			} else {
				IATPrinterGraphicalOutput *graphicalOutput = vdpoly_cast<IATPrinterGraphicalOutput *>(selectedOutput);

				if (graphicalOutput)
					AttachToGraphicsOutput(*static_cast<ATPrinterGraphicalOutput *>(graphicalOutput));
			}
		}
	} else if (id == kControlId_Clear) {
		Clear();
	} else if (id == kControlId_ResetView) {
		ResetView();
	} else if (id == kControlId_ShowPrintHead) {
		mbShowPrintHead = !mbShowPrintHead;

		mToolbar.SetItemPressed(kControlId_ShowPrintHead, mbShowPrintHead);

		UpdateGraphicsWindowSettings();
	}
}

void ATPrinterOutputWindow::Clear() {
	if (mpGraphicWindow)
		mpGraphicWindow->Clear();
	else if (mpTextEditor) {
		mpTextEditor->Clear();
		mLastTextOffset = 0;

		if (mpTextOutput) {
			mpTextOutput->Clear();
			mpTextOutput->Revalidate();
		}
	}
}

void ATPrinterOutputWindow::ResetView() {
	if (mpGraphicWindow)
		mpGraphicWindow->ResetView();
}

void ATPrinterOutputWindow::OnAddedOutput(ATPrinterOutput& output) {
	AddOutput(output);

	if (!mpTextOutput && !mpGraphicsOutput)
		AttachToAnyOutput();
}

void ATPrinterOutputWindow::OnRemovingOutput(ATPrinterOutput& output) {
	RemoveOutput(output);

	if (&output == mpTextOutput) {
		DetachFromTextOutput();

		AttachToAnyOutput();
	}

	if (mPrinterOutputs.empty())
		UpdateToolbarForOutput();
}

void ATPrinterOutputWindow::OnAddedGraphicalOutput(ATPrinterGraphicalOutput& output) {
	AddOutput(output);

	if (!mpGraphicsOutput)
		AttachToAnyOutput();
}

void ATPrinterOutputWindow::OnRemovingGraphicalOutput(ATPrinterGraphicalOutput& output) {
	RemoveOutput(output);

	if (&output == mpGraphicsOutput) {
		DetachFromGraphicsOutput();

		AttachToAnyOutput();
	}

	if (mPrinterOutputs.empty())
		UpdateToolbarForOutput();
}

void ATPrinterOutputWindow::AddOutput(ATPrinterOutputBase& output) {
	auto it = std::lower_bound(mPrinterOutputs.begin(), mPrinterOutputs.end(), &output, PrinterOutputSort());

	mPrinterOutputs.insert(it, vdrefptr<ATPrinterOutputBase>(&output));
}

void ATPrinterOutputWindow::RemoveOutput(ATPrinterOutputBase& output) {
	auto it = std::find(mPrinterOutputs.begin(), mPrinterOutputs.end(), &output);

	if (it != mPrinterOutputs.end())
		mPrinterOutputs.erase(it);
}

void ATPrinterOutputWindow::UpdateToolbarForOutput() {
	ATPrinterOutputBase *output = mpTextOutput ? static_cast<ATPrinterOutputBase *>(mpTextOutput) : mpGraphicsOutput;

	if (!output)
		mToolbar.SetItemText(kControlId_Output, L"(No printer outputs)");
	else
		mToolbar.SetItemText(kControlId_Output, output->GetName());

	mToolbar.SetItemVisible(kControlId_Clear, mpTextOutput != nullptr || mpGraphicsOutput != nullptr);
	mToolbar.SetItemVisible(kControlId_ResetView, mpGraphicsOutput != nullptr);
}

void ATPrinterOutputWindow::AttachToAnyOutput() {
	if (mpOutputMgr) {
		if (mpOutputMgr->GetGraphicalOutputCount() > 0)
			AttachToGraphicsOutput(mpOutputMgr->GetGraphicalOutput(0));
		else if (mpOutputMgr->GetOutputCount() > 0)
			AttachToTextOutput(mpOutputMgr->GetOutput(0));
	}
}

void ATPrinterOutputWindow::AttachToTextOutput(ATPrinterOutput& output) {
	DetachFromGraphicsOutput();

	mLastTextOffset = 0;
	if (mpTextEditor)
		mpTextEditor->Clear();

	mpTextOutput = &output;
	mpTextOutput->SetOnInvalidation(
		[this] {
			UpdateTextOutput();
		}
	);

	UpdateTextOutput();
	UpdateToolbarForOutput();
}

void ATPrinterOutputWindow::DetachFromTextOutput() {
	if (mpTextOutput) {
		mpTextOutput->SetOnInvalidation(nullptr);
		mpTextOutput = nullptr;

		if (mpTextEditor)
			mpTextEditor->Clear();
	}
}

void ATPrinterOutputWindow::UpdateTextOutput() {
	if (mpTextOutput) {
		const size_t offset = mpTextOutput->GetLength();

		if (offset > mLastTextOffset) {
			if (mpTextEditor)
				mpTextEditor->Append(mpTextOutput->GetTextPointer(mLastTextOffset));

			mLastTextOffset = offset;
		}

		mpTextOutput->Revalidate();
	}
}

void ATPrinterOutputWindow::AttachToGraphicsOutput(ATPrinterGraphicalOutput& output) {
	DetachFromTextOutput();
	DetachFromGraphicsOutput();

	mpGraphicsOutput = &output;

	mpTextEditor->Clear();

	ATUINativeWindowProxy proxy(mhwndTextEditor);
	proxy.Hide();

	mpGraphicWindow = new ATUIPrinterGraphicalOutputWindow;
	mpGraphicWindow->CreateChild(mhdlg, 101, 0, 0, 0, 0, WS_CHILD | WS_VISIBLE | WS_TABSTOP);

	OnSize();

	mpGraphicWindow->AttachToOutput(output);
	UpdateToolbarForOutput();
	UpdateGraphicsWindowSettings();
}

void ATPrinterOutputWindow::DetachFromGraphicsOutput() {
	if (mpGraphicsOutput) {
		mpGraphicsOutput->SetOnInvalidation(nullptr);
		mpGraphicsOutput->SetOnPenChange(nullptr);
		mpGraphicsOutput->SetOnHorizontalMove(nullptr);
		mpGraphicsOutput->SetOnVerticalMove(nullptr);
		mpGraphicsOutput = nullptr;
	}

	if (mpGraphicWindow) {
		mpGraphicWindow->Destroy();
		mpGraphicWindow = nullptr;

		// the text window was not resized while the graphic window existed, so fix that now
		OnSize();

		mpTextEditor->Clear();

		ATUINativeWindowProxy proxy(mhwndTextEditor);
		proxy.Show();
	}
}

void ATPrinterOutputWindow::UpdateGraphicsWindowSettings() {
	if (mpGraphicWindow)
		mpGraphicWindow->SetShowPrintHead(mbShowPrintHead);
}

////////////////////////////////////////////////////////////////////////////////

void ATUIDebuggerRegisterPrinterOutputPane() {
	ATRegisterUIPaneType(kATUIPaneId_PrinterOutput, VDRefCountObjectFactory<ATPrinterOutputWindow, ATUIPane>);
}
