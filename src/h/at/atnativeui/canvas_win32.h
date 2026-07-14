//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2020 Avery Lee
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

#ifndef f_AT_ATNATIVEUI_CANVAS_WIN32_H
#define f_AT_ATNATIVEUI_CANVAS_WIN32_H

#include <vd2/system/win32/miniwindows.h>
#include <vd2/VDDisplay/renderer.h>

class IVDDisplayRendererGDI;

class ATUICanvasW32 {
	ATUICanvasW32(const ATUICanvasW32&) = delete;
	ATUICanvasW32& operator=(const ATUICanvasW32&) = delete;
public:
	ATUICanvasW32();
	~ATUICanvasW32();

	void Init(VDZHWND hwnd);

	IVDDisplayRenderer *Begin(VDZPAINTSTRUCT& ps, bool enableDoubleBuffering);
	void End(VDZPAINTSTRUCT& ps);

	VDZHDC BeginDirect(VDZPAINTSTRUCT& ps, bool enableDoubleBuffering);
	void EndDirect(VDZPAINTSTRUCT& ps);

	// Scroll the contents of the canvas by the given vector. An original
	// point (x,y) is translated to (x+dx,y+dy).
	void Scroll(sint32 dx, sint32 dy);

	vdspan<const vdrect32> GetDetailedUpdateRects(VDZHDC hdc, const VDZPAINTSTRUCT& ps);

private:
	IVDDisplayRendererGDI *mpRenderer;
	VDZHWND mhwnd;
	VDZHDC mhdc;
	void *mhPaintBuffer;

	struct RegionDataHeaderW32 {
		uint32 mData[8];
	};

	struct RegionDataW32 {
		RegionDataHeaderW32 mHeader;
		char mBuffer[1];
	};

	vdstructex<RegionDataW32> mRegionData;
	vdfastvector<vdrect32> mRegionRects;
};

#endif
