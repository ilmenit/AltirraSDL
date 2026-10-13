//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2018 Avery Lee
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

#ifndef f_VD2_VDDISPLAY_INTERNAL_SCREENFX_H
#define f_VD2_VDDISPLAY_INTERNAL_SCREENFX_H

#include <vd2/system/vdtypes.h>
#include <vd2/system/vectors.h>

struct VDDScreenMaskParams;

void VDDisplayCreateGammaRamp(uint32 *gammaTex, uint32 len, bool enableInputConversion, float outputGamma, float gammaAdjust);
void VDDisplayCreateScanlineMaskTexture(uint32 *scanlineTex, ptrdiff_t pitch, uint32 srcH, uint32 dstH, float outY, float outH, uint32 texSize, float intensity, bool halfLineOffset, bool renderLinear);

struct VDDisplayApertureGrilleParams {
	float mPixelsPerTriad = 0;
	float mRedCenter = 0;
	float mRedWidth = 0;
	float mGrnCenter = 0;
	float mGrnWidth = 0;
	float mBluCenter = 0;
	float mBluWidth = 0;

	VDDisplayApertureGrilleParams() = default;
	VDDisplayApertureGrilleParams(const VDDScreenMaskParams& maskParams, float dstW, float srcW);
};

void VDDisplayCreateApertureGrilleTexture(uint32 *tex, uint32 w, float dstX, const VDDisplayApertureGrilleParams& params);

struct VDDisplaySlotMaskParams {
	float mPixelsPerBlockH = 0;
	float mPixelsPerBlockV = 0;
	float mRedCenter = 0;
	float mRedWidth = 0;
	float mRedHeight = 0;
	float mGrnCenter = 0;
	float mGrnWidth = 0;
	float mGrnHeight = 0;
	float mBluCenter = 0;
	float mBluWidth = 0;
	float mBluHeight = 0;

	VDDisplaySlotMaskParams() = default;
	VDDisplaySlotMaskParams(const VDDScreenMaskParams& maskParams, float dstW, float srcW);
};

void VDDisplayCreateSlotMaskTexture(uint32 *tex, ptrdiff_t pitch, uint32 w, uint32 h, float dstX, float dstY, float dstW, float dstH, const VDDisplaySlotMaskParams& params);

struct VDDisplayTriadDotMaskParams {
	float mPixelsPerTriadH = 0;
	float mPixelsPerTriadV = 0;
	float mRedCenter[2][2] {};
	float mRedWidth = 0;
	float mGrnCenter[2][2] {};
	float mGrnWidth = 0;
	float mBluCenter[2][2] {};
	float mBluWidth = 0;

	VDDisplayTriadDotMaskParams() = default;
	VDDisplayTriadDotMaskParams(const VDDScreenMaskParams& maskParams, float dstW, float srcW);
};

void VDDisplayCreateTriadDotMaskTexture(uint32 *tex, ptrdiff_t pitch, uint32 w, uint32 h, float dstX, float dstY, float dstW, float dstH, const VDDisplayTriadDotMaskParams& params);

struct VDDisplayDistortionMapping {
	float mScaleX;
	float mScaleY;
	float mSqRadius;

	void Init(float viewAngleX, float viewRatioY, float viewAspect);

	bool MapImageToScreen(vdfloat2& normDestPt) const;
	bool MapScreenToImage(vdfloat2& normDestPt) const;
};
#endif
