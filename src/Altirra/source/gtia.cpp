//	Altirra - Atari 800/800XL emulator
//	Copyright (C) 2008 Avery Lee
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
//	You should have received a copy of the GNU General Public License
//	along with this program; if not, write to the Free Software
//	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

#include <stdafx.h>
#include <numeric>
#include <vd2/system/binary.h>
#include <vd2/system/cpuaccel.h>
#include <vd2/system/math.h>
#include <vd2/system/vecmath.h>
#include <vd2/VDDisplay/display.h>
#include <vd2/VDDisplay/displaytypes.h>
#include <vd2/Kasumi/pixmap.h>
#include <vd2/Kasumi/pixmapops.h>
#include <vd2/Kasumi/pixmaputils.h>
#include <at/atcore/configvar.h>
#include <at/atcore/profile.h>
#include <at/atcore/enumparseimpl.h>
#include <at/atcore/serialization.h>
#include <at/atcore/snapshotimpl.h>
#include "gtia.h"
#include "gtiatables.h"
#include "gtiarenderer.h"
#include "palettegenerator.h"
#include "console.h"
#include "artifacting.h"
#include "savestate.h"
#include "uirender.h"
#include "vbxe.h"

using namespace ATGTIA;

AT_DEFINE_ENUM_TABLE_BEGIN(ATLumaRampMode)
	{ kATLumaRampMode_Linear, "linear" },
	{ kATLumaRampMode_XL, "xl" },
AT_DEFINE_ENUM_TABLE_END(ATLumaRampMode, kATLumaRampMode_Linear)

AT_DEFINE_ENUM_TABLE_BEGIN(ATColorMatchingMode)
	{ ATColorMatchingMode::None, "none" },
	{ ATColorMatchingMode::SRGB, "srgb" },
	{ ATColorMatchingMode::AdobeRGB, "adobergb" },
	{ ATColorMatchingMode::Gamma22, "gamma22" },
	{ ATColorMatchingMode::Gamma24, "gamma24" },
AT_DEFINE_ENUM_TABLE_END(ATColorMatchingMode, ATColorMatchingMode::None)

AT_DEFINE_ENUM_TABLE_BEGIN(ATMonitorMode)
	{ ATMonitorMode::Color, "color" },
	{ ATMonitorMode::Peritel, "peritel" },
	{ ATMonitorMode::MonoAmber, "monoamber" },
	{ ATMonitorMode::MonoGreen, "monogreen" },
	{ ATMonitorMode::MonoBluishWhite, "monobluishwhite" },
	{ ATMonitorMode::MonoWhite, "monowhite" },
AT_DEFINE_ENUM_TABLE_END(ATMonitorMode, ATMonitorMode::Color)

AT_DEFINE_ENUM_TABLE_BEGIN(ATVideoDeinterlaceMode)
	{ ATVideoDeinterlaceMode::None, "none" },
	{ ATVideoDeinterlaceMode::AdaptiveBob, "adaptivebob" },
AT_DEFINE_ENUM_TABLE_END(ATVideoDeinterlaceMode, ATVideoDeinterlaceMode::None)

#ifdef VD_CPU_X86
extern "C" void VDCDECL atasm_update_playfield_160_sse2(
	void *dst,
	const uint8 *src,
	uint32 n
);
#endif

#if defined(VD_CPU_X86) || defined(VD_CPU_X64)
#include "gtia_sse2_intrin.inl"
#elif defined(VD_CPU_ARM64)
#include "gtia_neon.inl"
#endif

///////////////////////////////////////////////////////////////////////////

ATConfigVarInt32 g_ATCVDisplayDropCountThreshold("display.drop_count_threshold", 20);
ATConfigVarFloat g_ATCVDisplayDropLagThreshold("display.drop_lag_threshold", 0.007f);
ATConfigVarBool g_ATCVDisplayForceHdrRendering("display.force_hdr_rendering", false);
ATConfigVarBool g_ATCVDisplayForceWcgRendering("display.force_wcg_rendering", false);
ATConfigVarBool g_ATCVDevicesVbxeNtscOffset("devices.vbxe.ntsc_vertical_offset", true);

///////////////////////////////////////////////////////////////////////////

bool ATColorParams::IsSimilar(const ATColorParams& other) const {
	const auto IsSimilar = [](float x, float y) { return fabsf(x - y) < 1e-5f; };

	return IsSimilar(mHueStart			, other.mHueStart)
		&& IsSimilar(mHueRange			, other.mHueRange)
		&& IsSimilar(mBrightness		, other.mBrightness)
		&& IsSimilar(mContrast			, other.mContrast)
		&& IsSimilar(mSaturation		, other.mSaturation)
		&& IsSimilar(mGammaCorrect		, other.mGammaCorrect)
		&& IsSimilar(mIntensityScale	, other.mIntensityScale)
		&& IsSimilar(mArtifactHue		, other.mArtifactHue)
		&& IsSimilar(mArtifactSat		, other.mArtifactSat)
		&& IsSimilar(mArtifactSharpness	, other.mArtifactSharpness)
		&& IsSimilar(mRedShift			, other.mRedShift)
		&& IsSimilar(mRedScale			, other.mRedScale)
		&& IsSimilar(mGrnShift			, other.mGrnShift)
		&& IsSimilar(mGrnScale			, other.mGrnScale)
		&& IsSimilar(mBluShift			, other.mBluShift)
		&& IsSimilar(mBluScale			, other.mBluScale)
		&& mbUsePALQuirks == other.mbUsePALQuirks
		&& mLumaRampMode == other.mLumaRampMode
		&& mColorMatchingMode == other.mColorMatchingMode;
}

ATArtifactingParams ATArtifactingParams::GetDefault() {
	ATArtifactingParams params = {};
	params.mScanlineIntensity = 0.75f;
	params.mDistortionViewAngleX = 35.0f;
	params.mDistortionYRatio = 0.90f;
	params.mbEnableVignette = true;
	params.mVignetteIntensity = 0.18f;
	params.mbEnableBloom = true;
	params.mbBloomScanlineCompensation = true;
	params.mBloomRadius = 0.20f;
	params.mBloomDirectIntensity = 1.00f;
	params.mBloomIndirectIntensity = 0.70f;
	params.mSDRIntensity = 200.0f;
	params.mHDRIntensity = 350.0f;
	params.mbUseSystemSDR = false;
	params.mbUseSystemSDRAsHDR = false;

	return params;
}

///////////////////////////////////////////////////////////////////////////

namespace nsATColorPresets {
	constexpr ATColorParams GetPresetBase() {
		ATColorParams pa {};
		pa.mRedShift = 0;
		pa.mRedScale = 1;
		pa.mGrnShift = 0;
		pa.mGrnScale = 1;
		pa.mBluShift = 0;
		pa.mBluScale = 1;
		pa.mGammaCorrect = 1.0f;
		pa.mIntensityScale = 1.0f;
		pa.mArtifactSharpness = 0.50f;
		pa.mbUsePALQuirks = false;
		pa.mLumaRampMode = kATLumaRampMode_XL;
		pa.mColorMatchingMode = ATColorMatchingMode::None;

		return pa;
	}

	constexpr ATColorParams GetDefaultNTSCPreset() {
		ATColorParams pa = GetPresetBase();
		pa.mHueStart = -57.0f;
		pa.mHueRange = 27.1f * 15.0f;
		pa.mBrightness = -0.04f;
		pa.mContrast = 1.04f;
		pa.mSaturation = 0.20f;
		pa.mArtifactHue = 252.0f;
		pa.mArtifactSat = 1.15f;
		pa.mArtifactSharpness = 0.50f;
		pa.mColorMatchingMode = ATColorMatchingMode::SRGB;

		return pa;
	}

	constexpr ATColorParams GetDefaultPALPreset() {
		ATColorParams pa = GetPresetBase();

		// HueStart is specified in terms of I/Q, so the +V phase of the swinging subcarrier is at -57d + 45d = -12d.
		pa.mHueStart = -12.0f;
		pa.mHueRange = 18.3f * 15.0f;
		pa.mBrightness = 0.0f;
		pa.mContrast = 1.0f;
		pa.mSaturation = 0.29f;
		pa.mArtifactHue = 80.0f;
		pa.mArtifactSat = 0.80f;
		pa.mArtifactSharpness = 0.50f;
		pa.mbUsePALQuirks = true;

		return pa;
	}
}

static constexpr struct ATColorPreset {
	const char *mpTag;
	const wchar_t *mpName;
	ATColorParams mParams;
} kColorPresets[] = {
	{ "default_ntsc", L"Default NTSC (XL)", nsATColorPresets::GetDefaultNTSCPreset()},
	{ "default_pal", L"Default PAL", nsATColorPresets::GetDefaultPALPreset()},

	{ "ntsc_xl_contemporary", L"NTSC Contemporary (XL)", []() -> ATColorParams {
			ATColorParams pa = nsATColorPresets::GetDefaultNTSCPreset();
			pa.mHueStart = -33.0f;
			pa.mHueRange = 24.0f * 15.0f;
			return pa;
		}() },

	{ "ntsc_xe", L"NTSC (XE)", []() -> ATColorParams {
			ATColorParams pa = nsATColorPresets::GetDefaultNTSCPreset();
			pa.mArtifactHue = 191.0f;
			pa.mArtifactSat = 1.32f;
			return pa;
		}() },

	{ "ntsc_800", L"NTSC (800)", []() -> ATColorParams {
			ATColorParams pa = nsATColorPresets::GetPresetBase();
			pa.mHueStart = -57.0f;
			pa.mHueRange = 27.1f * 15.0f;
			pa.mBrightness = -0.04f;
			pa.mContrast = 1.04f;
			pa.mSaturation = 0.20f;
			pa.mGammaCorrect = 1.0f;
			pa.mIntensityScale = 0.77f;
			pa.mArtifactHue = 124.0f;
			pa.mArtifactSat = 2.08f;
			pa.mColorMatchingMode = ATColorMatchingMode::SRGB;
			return pa;
		}() },

	{ "ntsc_xl_1702", L"NTSC (XL + Commodore 1702 monitor)", []() -> ATColorParams {
			ATColorParams pa = nsATColorPresets::GetPresetBase();
			pa.mHueStart = -33.0f;
			pa.mHueRange = 24.0f * 15.0f;
			pa.mBrightness = 0;
			pa.mContrast = 1.08f;
			pa.mSaturation = 0.30f;
			pa.mGammaCorrect = 1.0f;
			pa.mArtifactHue = 277.0f;
			pa.mArtifactSat = 2.13f;
			pa.mGrnScale = 0.60f;
			pa.mBluShift = -5.5f;
			pa.mBluScale = 1.56f;
			return pa;
		}() },

	{ "altirra320_pal", L"Altirra 3.20 PAL", []() -> ATColorParams {
			ATColorParams pa = nsATColorPresets::GetPresetBase();

			// Hue start definition for PAL quirks has been changed; it now has the same defintion
			// as NTSC and non-quirk PAL decoding. In 3.20, hue 1 is placed at 33d behind the hue
			// step followed by two hue steps forward.
			pa.mHueStart = -10.0f;
			pa.mHueRange = 23.5f * 15.0f;
			pa.mBrightness = 0.0f;
			pa.mContrast = 1.0f;
			pa.mSaturation = 0.29f;
			pa.mArtifactHue = 80.0f;
			pa.mArtifactSat = 0.80f;
			pa.mArtifactSharpness = 0.50f;
			pa.mbUsePALQuirks = true;
			return pa;
		}() },

	{ "altirra310_ntsc", L"Altirra 3.10 NTSC", []() -> ATColorParams {
			ATColorParams pa = nsATColorPresets::GetPresetBase();
			pa.mHueStart = -57.0f;
			pa.mHueRange = 27.1f * 15.0f;
			pa.mBrightness = -0.04f;
			pa.mContrast = 1.04f;
			pa.mSaturation = 0.20f;
			pa.mArtifactHue = 252.0f;
			pa.mArtifactSat = 1.15f;
			pa.mArtifactSharpness = 0.50f;
			pa.mBluScale = 1.50f;
			return pa;
		}() },

	{ "altirra280_ntsc", L"Altirra 2.80 NTSC", []() -> ATColorParams {
			ATColorParams pa = nsATColorPresets::GetPresetBase();
			pa.mHueStart = -36.0f;
			pa.mHueRange = 25.5f * 15.0f;
			pa.mBrightness = -0.08f;
			pa.mContrast = 1.08f;
			pa.mSaturation = 0.33f;
			pa.mGammaCorrect = 1.0f;
			pa.mArtifactHue = 279.0f;
			pa.mArtifactSat = 0.68f;
			return pa;
		}() },

	{ "altirra250_ntsc", L"Altirra 2.50 NTSC", []() -> ATColorParams {
			ATColorParams pa = nsATColorPresets::GetPresetBase();
			pa.mHueStart = -51.0f;
			pa.mHueRange = 27.9f * 15.0f;
			pa.mBrightness = 0.0f;
			pa.mContrast = 1.0f;
			pa.mSaturation = 75.0f / 255.0f;
			pa.mGammaCorrect = 1.0f;
			pa.mArtifactHue = -96.0f;
			pa.mArtifactSat = 2.76f;
			return pa;
		}() },

	{ "jakub", L"Jakub", []() -> ATColorParams {
			ATColorParams pa = nsATColorPresets::GetPresetBase();
			pa.mHueStart = -9.36754f;
			pa.mHueRange = 361.019f;
			pa.mBrightness = +0.174505f;
			pa.mContrast = 0.82371f;
			pa.mSaturation = 0.21993f;
			pa.mGammaCorrect = 1.0f;
			pa.mArtifactHue = -96.0f;
			pa.mArtifactSat = 2.76f;
			return pa;
		}() },

	{ "olivierpal", L"OlivierPAL", []() -> ATColorParams {
			ATColorParams pa = nsATColorPresets::GetPresetBase();
			pa.mHueStart = -14.7889f;
			pa.mHueRange = 385.155f;
			pa.mBrightness = +0.057038f;
			pa.mContrast = 0.941149f;
			pa.mSaturation = 0.195861f;
			pa.mGammaCorrect = 1.0f;
			pa.mArtifactHue = 80.0f;
			pa.mArtifactSat = 0.80f;
			return pa;
		}() },
};

uint32 ATGetColorPresetCount() {
	return vdcountof(kColorPresets);
}

const char *ATGetColorPresetTagByIndex(uint32 i) {
	return kColorPresets[i].mpTag;
}

sint32 ATGetColorPresetIndexByTag(const char *tags) {
	VDStringRefA tagsRef(tags);

	while(!tagsRef.empty()) {
		VDStringRefA tag;
		if (!tagsRef.split(',', tag)) {
			tag = tagsRef;
			tagsRef.clear();
		}

		for(uint32 i=0; i<vdcountof(kColorPresets); ++i) {
			if (tag == kColorPresets[i].mpTag)
				return (sint32)i;
		}
	}

	return -1;
}

const wchar_t *ATGetColorPresetNameByIndex(uint32 i) {
	return kColorPresets[i].mpName;
}

ATColorParams ATGetColorPresetByIndex(uint32 i) {
	return kColorPresets[i].mParams;
}

///////////////////////////////////////////////////////////////////////////

class ATFrameTracker final : public vdrefcounted<IVDRefCount> {
public:
	ATFrameTracker() : mActiveFrames(0) { }
	VDAtomicInt mActiveFrames;
};

class ATFrameBuffer final : public VDVideoDisplayFrame, public IVDVideoDisplayScreenFXEngine {
public:
	ATFrameBuffer(ATFrameTracker *tracker, ATArtifactingEngine& artengine)
		: mpTracker(tracker)
		, mArtEngine(artengine)
	{
		if (mpTracker)
			++mpTracker->mActiveFrames;

		mpScreenFXEngine = this;
	}

	~ATFrameBuffer() {
		if (mpTracker)
			--mpTracker->mActiveFrames;
	}

	VDPixmap ApplyScreenFX(const VDPixmap& px) override;
	VDPixmap ApplyScreenFX(VDPixmapBuffer& dst, const VDPixmap& px, bool enableSignedOutput);
	void DuplicateField();

	vdrefptr<ATFrameTracker> mpTracker;
	ATArtifactingEngine& mArtEngine;
	VDPixmapBuffer mBuffer;
	VDPixmapBuffer mEmulatedFXBuffer {};
	VDVideoDisplayScreenFXInfo mScreenFX {};

	uint32 mViewX1 = 0;
	uint32 mViewY1 = 0;
	float mRawPAR = 1.0f;
	const uint32 *mpPalette = nullptr;

	bool mbDroppedFrame = false;		// this frame is being rendered but won't be presented
	bool mbDualFieldFrame = false;		// frame is interlaced
	bool mbLowerField = false;			// computer rendered to lower field of interlaced frame
	bool mbDuplicateField = false;		// set if the computer should render to _both_ fields
	bool mbIncludeHBlank = false;
	bool mbScanlineHasHires[312] {};
};

VDPixmap ATFrameBuffer::ApplyScreenFX(const VDPixmap& px) {
	return ApplyScreenFX(mEmulatedFXBuffer, px, false);
}

VDPixmap ATFrameBuffer::ApplyScreenFX(VDPixmapBuffer& dstBuffer, const VDPixmap& px, bool enableSignedOutput) {
	// Software scanlines only support noninterlaced frames.
	const bool scanlines = mScreenFX.mScanlineIntensity != 0.0f && !mbDualFieldFrame;

	const bool src32 = (mBuffer.format == nsVDPixmap::kPixFormat_XRGB8888);
	const uint32 w = px.w;
	const uint32 h = px.h;
	dstBuffer.init(src32 ? w : mBuffer.w, scanlines ? h * 2 : h, nsVDPixmap::kPixFormat_XRGB8888);

	const char *src = src32 ? (const char *)px.data : (const char *)mBuffer.data + mBuffer.pitch * mViewY1;
	char *dst = (char *)dstBuffer.data;

	const bool palBlending = (mScreenFX.mPALBlendingOffset != 0);

	// If we have P8 input, we need to enable output correction even if screenFX doesn't call for it, as it
	// needs to be baked into the palette.
	const bool bypassOutputCorrection = (mScreenFX.mColorCorrectionMatrix[0][0] == 0.0f) && src32;

	// We may be called in the middle of a frame for an immediate update, so we must suspend
	// and restore the existing frame settings.
	mArtEngine.SuspendFrame();
	mArtEngine.BeginFrame(palBlending, palBlending, false, false, false, false, false, bypassOutputCorrection, mScreenFX.mbSignedRGBEncoding, enableSignedOutput && mScreenFX.mbSignedRGBEncoding,
		false);

	const uint32 interpW = src32 ? w : mBuffer.w;
	const uint32 bpr = src32 ? interpW * 4 : interpW;

	const char *last = nullptr;
	for(uint32 y = 0; y < h; ++y) {
		char *dst1 = nullptr;
		if (last && scanlines) {
			dst1 = dst;
			dst += dstBuffer.pitch;
		}

		char *dst2 = dst;
		dst += dstBuffer.pitch;
		if (src32) {
			memcpy(dst2, src, bpr);
			mArtEngine.Artifact32(y, (uint32 *)dst2, w, false, mbIncludeHBlank);
		} else {
			bool hires = mbDualFieldFrame ? mbScanlineHasHires[y >> 1] : mbScanlineHasHires[y];

			mArtEngine.Artifact8(y, (uint32 *)dst2, (const uint8 *)src, hires, false, mbIncludeHBlank);
		}

		src += mBuffer.pitch;

		if (dst1) {
			mArtEngine.InterpolateScanlines((uint32 *)dst1, (const uint32 *)last, (const uint32 *)dst2, interpW);
		}

		last = dst2;
	}
	
	mArtEngine.ResumeFrame();

	if (scanlines)
		memcpy(dst, last, interpW*4);

	VDPixmap result = dstBuffer;

	if (!src32)
		result.data = (char *)result.data + mViewX1 * 4;

	result.w = px.w;
	result.palette = mpPalette;

	VDASSERT(VDAssertValidPixmap(result));

	return result;
}

void ATFrameBuffer::DuplicateField() {
	if (!mbDuplicateField || !mbDualFieldFrame)
		return;

	const auto& srcField = VDPixmapExtractField(mPixmap, mbLowerField);
	const auto& dstField = VDPixmapExtractField(mPixmap, !mbLowerField);

	VDPixmapBlt(dstField, srcField);
}

///////////////////////////////////////////////////////////////////////////

class ATGTIALightSensorImpl final : public ATGTIALightSensor {
public:
	uint32 mAccumulatedLight = 0;
	uint32 mLightThreshold = 0;
	const uint8 *mpWeights = nullptr;
	vdfunction<void(int)> mTriggerFn;
};

///////////////////////////////////////////////////////////////////////////

struct ATGTIAEmulator::Impl {
	VDDScreenMaskParams mScreenMaskParams = ATGTIAEmulator::GetDefaultScreenMaskParams();
};

///////////////////////////////////////////////////////////////////////////

namespace {
	const int kPlayerWidths[4]={8,16,8,32};
	const int kMissileWidths[4]={2,4,2,8};

	const uint32 kSpriteShiftMasks[4]={
		0xFFFFFFFF,
		0x00,
		0x00,
		0x00,
	};

	const uint8 kSpriteStateTransitions[4][4]={
		{ 0, 0, 0, 0 },
		{ 1, 0, 1, 0 },
		{ 0, 2, 2, 0 },
		{ 1, 2, 3, 0 },
	};
}

///////////////////////////////////////////////////////////////////////////

void ATGTIAEmulator::SpriteState::Reset() {
	mShiftRegister = 0;
	mShiftState = 0;
	mSizeMode = 0;
	mDataLatch = 0;
}

// Detect collisions
uint8 ATGTIAEmulator::SpriteState::Detect(uint32 ticks, const uint8 *src) {
	uint8 shifter = mShiftRegister;
	int state = mShiftState;
	const uint8 *VDRESTRICT stateTransitions = kSpriteStateTransitions[mSizeMode];
	uint8 detect = 0;

	do {
		detect |= (*src++) & (uint8)((sint8)shifter >> 7);

		state = stateTransitions[state];
		shifter += shifter & kSpriteShiftMasks[state];
	} while(--ticks);

	return detect;
}

uint8 ATGTIAEmulator::SpriteState::Detect(uint32 ticks, const uint8 *src, const uint8 *hires) {
	uint8 shifter = mShiftRegister;
	int state = mShiftState;
	const uint8 *VDRESTRICT stateTransitions = kSpriteStateTransitions[mSizeMode];
	uint8 detect = 0;

	do {
		if ((sint8)shifter < 0) {
			detect |= (*src & (P01 | P23));

			if (*hires)
				detect |= PF2;
		}

		++src;
		++hires;

		state = stateTransitions[state];
		shifter += shifter & kSpriteShiftMasks[state];
	} while(--ticks);

	return detect;
}

uint8 ATGTIAEmulator::SpriteState::Generate(uint32 ticks, uint8 mask, uint8 *dst) {
	uint8 shifter = mShiftRegister;
	int state = mShiftState;
	const uint8 *VDRESTRICT stateTransitions = kSpriteStateTransitions[mSizeMode];
	uint8 detect = 0;

	do {
		if ((sint8)shifter < 0) {
			detect |= *dst;
			*dst |= mask;
		}

		++dst;

		state = stateTransitions[state];
		shifter += shifter & kSpriteShiftMasks[state];
	} while(--ticks);

	return detect;
}

uint8 ATGTIAEmulator::SpriteState::Generate(uint32 ticks, uint8 mask, uint8 *dst, const uint8 *hires) {
	uint8 shifter = mShiftRegister;
	int state = mShiftState;
	const uint8 *VDRESTRICT stateTransitions = kSpriteStateTransitions[mSizeMode];
	uint8 detect = 0;

	do {
		if ((sint8)shifter < 0) {
			detect |= (*dst & (P01 | P23));
			*dst |= mask;

			if (*hires)
				detect |= PF2;
		}

		++dst;
		++hires;

		state = stateTransitions[state];
		shifter += shifter & kSpriteShiftMasks[state];
	} while(--ticks);

	return detect;
}

// Advance the shift state for a player or missile by a number of ticks,
// without actually generating image data.
void ATGTIAEmulator::SpriteState::Advance(uint32 ticks) {
	int shifts = 0;

	switch(mSizeMode) {
		case 0:
			shifts = ticks;
			mShiftState = 0;
			break;

		case 1:
			shifts = ((mShiftState & 1) + ticks) >> 1;
			mShiftState = (ticks + mShiftState) & 1;
			break;

		case 2:
			// 00,11 -> 00
			// 01,10 -> 10
			switch(mShiftState) {
				case 0:
				case 3:
					shifts = ticks;
					mShiftState = 0;
					break;

				case 1:
					mShiftState = 2;
				case 2:
					break;
			}
			break;

		case 3:
			shifts = (mShiftState + ticks) >> 2;
			mShiftState = (mShiftState + ticks) & 3;
			break;
	}

	if (shifts >= 32)
		mShiftRegister = 0;
	else
		mShiftRegister <<= shifts;
}

///////////////////////////////////////////////////////////////////////////

void ATGTIAEmulator::Sprite::Sync(int pos) {
	if (mLastSync != pos) {
		mState.Advance(pos - mLastSync);
		mLastSync = pos;
	}
}

///////////////////////////////////////////////////////////////////////////

ATGTIAEmulator::ATGTIAEmulator()
	: mpConn(NULL)
	, mpFrameTracker(new ATFrameTracker)
	, mbCTIAMode(false)
	, mb50HzMode(false)
	, mbPALMode(false)
	, mbSECAMMode(false)
	, mArtifactMode(ATArtifactMode::None)
	, mOverscanMode(kOverscanNormal)
	, mVerticalOverscanMode(kVerticalOverscan_Default)
	, mVBlankMode(kVBlankModeOn)
	, mbVsyncEnabled(true)
	, mbBlendMode(false)
	, mbBlendModeLastFrame(false)
	, mbOverscanPALExtended(false)
	, mbInterlaceEnabled(false)
	, mbScanlinesEnabled(false)
	, mPreArtifactFrameBuffer()
	, mpArtifactingEngine(new ATArtifactingEngine)
	, mpRenderer(new ATGTIARenderer)
	, mpUIRenderer(NULL)
	, mpVBXE(NULL)
	, mRCIndex(0)
	, mRCCount(0)
	, mpFreeSpriteImages(NULL)
{
	mpImpl = new Impl;
	mpRenderer->SetDefectMode(mDefectMode);

	ResetColors();

	mPreArtifactFrameBuffer.resize(464*312+16, 0);

	mPreArtifactFrame.data = mPreArtifactFrameBuffer.data();
	mPreArtifactFrame.pitch = 464;
	mPreArtifactFrame.palette = mPalette;
	mPreArtifactFrame.data2 = NULL;
	mPreArtifactFrame.data3 = NULL;
	mPreArtifactFrame.pitch2 = 0;
	mPreArtifactFrame.pitch3 = 0;
	mPreArtifactFrame.w = 456;
	mPreArtifactFrame.h = 262;
	mPreArtifactFrame.format = nsVDPixmap::kPixFormat_Pal8;

	mpFrameTracker->AddRef();

	mSwitchOutput = 8;
	mSwitchInput = 15;
	mForcedSwitchInput = 15;
	mPRIOR = 0;
	mActivePRIOR = 0;

	for(int i=0; i<4; ++i) {
		mTRIG[i] = 0x01;
		mTRIGLatched[i] = 0x01;
		mTRIGSECAM[i] = 0x01;
		mTRIGSECAMLastUpdate[i] = 0;
	}

	SetAnalysisMode(kAnalyzeNone);
	memset(mPlayerCollFlags, 0, sizeof mPlayerCollFlags);
	memset(mMissileCollFlags, 0, sizeof mMissileCollFlags);
	mCollisionMask = 0xFF;

	mbTurbo = false;
	mbForcedBorder = false;

	SetPALMode(false);
}

ATGTIAEmulator::~ATGTIAEmulator() {
	SetVideoOutput(nullptr);

	mpLastFrame = NULL;

	if (mpFrameTracker) {
		mpFrameTracker->Release();
		mpFrameTracker = NULL;
	}

	delete mpArtifactingEngine;
	delete mpRenderer;
}

void ATGTIAEmulator::Init(IATGTIAEmulatorConnections *conn) {
	mpConn = conn;
	mY = 0;

	ColdReset();
}

void ATGTIAEmulator::ColdReset() {
	memset(mSpritePos, 0, sizeof mSpritePos);
	memset(mPMColor, 0, sizeof mPMColor);
	memset(mPFColor, 0, sizeof mPFColor);

	memset(&mState, 0, sizeof mState);

	memset(mPMColor, 0, sizeof mPMColor);
	memset(mPFColor, 0, sizeof mPFColor);
	mPFBAK = 0;
	mPRIOR = 0;
	mActivePRIOR = 0;
	mVDELAY = 0;
	mGRACTL = 0;
	mSwitchOutput = 15;

	memset(mPlayerCollFlags, 0, sizeof mPlayerCollFlags);
	memset(mMissileCollFlags, 0, sizeof mMissileCollFlags);

	// MxPF and PxPF tend to power up $00.
	// MxPL tend to power up $0F.
	// PxPL tend to power up $0F - self bits.
	for(uint8& v : mMissileCollFlags)
		v = 0xF0;

	mPlayerCollFlags[1] = 0x10;
	mPlayerCollFlags[2] = 0x30;
	mPlayerCollFlags[3] = 0x70;

	ResetSprites();

	mpConn->GTIASelectController(0, false);
	mpRenderer->ColdReset();
}

void ATGTIAEmulator::SetVBXE(ATVBXEEmulator *vbxe) {
	if (mpVBXE)
		mpVBXE->SetDefaultPalette(nullptr, nullptr);

	mpVBXE = vbxe;

	if (mpVBXE) {
		// we don't save the uncorrected palette, so we must recompute it
		RecomputePalette();
	}

	// kill current frame update
	mpDst = NULL;
	mpFrame = NULL;

	// force feed register updates into the renderer to resync it
	const auto resyncRenderer = [this](auto& r) {
		for(int i=0; i<32; ++i)
			r.SetRegisterImmediate(i, mState.mReg[i]);
	};

	if (mpVBXE)
		resyncRenderer(*mpVBXE);
	else
		resyncRenderer(*mpRenderer);
}

void ATGTIAEmulator::SetUIRenderer(IATUIRenderer *r) {
	mpUIRenderer = r;
}

ATColorSettings ATGTIAEmulator::GetDefaultColorSettings() const {
	ATColorSettings cs {};

	cs.mNTSCParams.mPresetTag = "default_ntsc";
	static_cast<ATColorParams&>(cs.mNTSCParams) = ATGetColorPresetByIndex(ATGetColorPresetIndexByTag("default_ntsc"));
	cs.mPALParams.mPresetTag = "default_pal";
	static_cast<ATColorParams&>(cs.mPALParams) = ATGetColorPresetByIndex(ATGetColorPresetIndexByTag("default_pal"));
	cs.mbUsePALParams = true;

	return cs;
}

ATColorSettings ATGTIAEmulator::GetColorSettings() const {
	return mColorSettings;
}

void ATGTIAEmulator::SetColorSettings(const ATColorSettings& settings) {
	mColorSettings = settings;
	RecomputePalette();
}

ATArtifactingParams ATGTIAEmulator::GetArtifactingParams() const {
	return mpArtifactingEngine->GetArtifactingParams();
}

void ATGTIAEmulator::SetArtifactingParams(const ATArtifactingParams& params) {
	mpArtifactingEngine->SetArtifactingParams(params);
}

void ATGTIAEmulator::ResetColors() {
	mColorSettings = GetDefaultColorSettings();

	RecomputePalette();
}

void ATGTIAEmulator::GetPalette(uint32 pal[256]) const {
	memcpy(pal, mPalette, sizeof(uint32)*256);
}

void ATGTIAEmulator::GetNTSCArtifactColors(uint32 c[2]) const {
	mpArtifactingEngine->GetNTSCArtifactColors(c);
}

bool ATGTIAEmulator::AreAcceleratedEffectsAvailable() const {
	switch(GetAcceleratedEffectsAvailability()) {
		case AccelFXAvailability::Available:
			return true;

		case AccelFXAvailability::NotEnabled:
			return g_ATCVDisplayForceHdrRendering || g_ATCVDisplayForceWcgRendering;

		default:
			return false;
	}
}

ATGTIAEmulator::AccelFXAvailability ATGTIAEmulator::GetAcceleratedEffectsAvailability() const {
	if (!mpDisplay || !mpDisplay->IsScreenFXPreferred())
		return AccelFXAvailability::NoSupport;

	if (!mbAccelScreenFX)
		return AccelFXAvailability::NotEnabled;

	return AccelFXAvailability::Available;
}

ATGTIAEmulator::HighColorAvailability ATGTIAEmulator::CheckHighColorAvailability(bool hdr) const {
	if (!mpDisplay)
		return HighColorAvailability::NoMinidriverSupport;

	switch(hdr ? mpDisplay->GetHDRCapability() : mpDisplay->GetWCGCapability()) {
		case VDDHighColorAvailability::NoMinidriverSupport:
			return HighColorAvailability::NoMinidriverSupport;

		case VDDHighColorAvailability::NoSystemSupport:
			return HighColorAvailability::NoSystemSupport;

		case VDDHighColorAvailability::NoHardwareSupport:
			return HighColorAvailability::NoHardwareSupport;

		case VDDHighColorAvailability::NotEnabledOnDisplay:
			if (!(hdr ? g_ATCVDisplayForceHdrRendering : g_ATCVDisplayForceWcgRendering))
				return HighColorAvailability::NotEnabledOnDisplay;

			break;

		case VDDHighColorAvailability::NoDisplaySupport:
			if (!(hdr ? g_ATCVDisplayForceHdrRendering : g_ATCVDisplayForceWcgRendering))
				return HighColorAvailability::NoDisplaySupport;
			break;

		case VDDHighColorAvailability::Available:
			break;
	}

	if (!mbAccelScreenFX)
		return HighColorAvailability::AccelNotEnabled;

	return HighColorAvailability::Available;
}

void ATGTIAEmulator::SetAnalysisMode(AnalysisMode mode) {
	mAnalysisMode = mode;
	mpRenderer->SetAnalysisMode(mode != kAnalyzeNone);
}

void ATGTIAEmulator::SetOverscanMode(OverscanMode mode) {
	mOverscanMode = mode;
}

void ATGTIAEmulator::SetVerticalOverscanMode(VerticalOverscanMode mode) {
	mVerticalOverscanMode = mode;
}

void ATGTIAEmulator::SetOverscanPALExtended(bool extended) {
	mbOverscanPALExtended = extended;
}

void ATGTIAEmulator::SetMonitorMode(ATMonitorMode mode) {
	if (mMonitorMode != mode) {
		mMonitorMode = mode;
		RecomputePalette();
	}
}

vdrect32 ATGTIAEmulator::GetFrameScanArea() const {
	int xlo = 44;
	int xhi = 212;
	int ylo = 8;
	int yhi = 248;

	bool palext = mbPALMode && mbOverscanPALExtended;
	if (palext) {
		ylo -= 25;
		yhi += 25;
	}

	OverscanMode omode = mOverscanMode;
	VerticalOverscanMode vomode = DeriveVerticalOverscanMode();

	if (mAnalysisMode || mbForcedBorder) {
		omode = kOverscanFull;
		vomode = kVerticalOverscan_Full;
	}

	switch(omode) {
		case kOverscanFull:
			xlo = 0;
			xhi = 228;
			break;

		case kOverscanExtended:
			xlo = 34;
			xhi = 222;
			break;

		case kOverscanNormal:
			break;

		case kOverscanOSScreen:
			xlo = 48;
			xhi = 208;
			break;

		case kOverscanWidescreen:
			xlo = 128 - 176/2;
			xhi = 128 + 176/2;
			break;
	}

	switch(vomode) {
		case kVerticalOverscan_Full:
			ylo = 0;
			yhi = 262;

			if (palext) {
				ylo = -25;
				yhi = 287;
			}
			break;

		case kVerticalOverscan_Extended:
			break;

		case kVerticalOverscan_OSScreen:
			if (!palext) {
				ylo = 32;
				yhi = 224;
			}
			break;

		case kVerticalOverscan_Normal:
			if (!mbPALMode) {
				ylo = 16;
				yhi = 240;
			}
			break;
	}

	return vdrect32(xlo, ylo, xhi, yhi);
}

void ATGTIAEmulator::GetRawFrameFormat(int& w, int& h, bool& rgb32) const {
	rgb32 = (mpVBXE != NULL) || mArtifactMode != ATArtifactMode::None || mbBlendMode || mbScanlinesEnabled || (mbInterlaceEnabled && mDeinterlaceMode != ATVideoDeinterlaceMode::None);

	const vdrect32 scanArea = GetFrameScanArea();

	w = scanArea.width() * 2;
	h = scanArea.height();

	int ix = 1, iy = 1;
	GetPixelAspectMultiple(ix, iy);

	w *= ix;
	h *= iy;
}

void ATGTIAEmulator::GetFrameSize(int& w, int& h) const {
	const vdrect32 scanArea = GetFrameScanArea();

	w = scanArea.width() * 2;
	h = scanArea.height();

	int ix = 1, iy = 1;
	GetPixelAspectMultiple(ix, iy);
	int ixy = std::max(ix, iy);

	w *= ixy;
	h *= ixy;
}

void ATGTIAEmulator::GetPixelAspectMultiple(int& x, int& y) const {
	int ix = 1;
	int iy = 1;

	if (mbInterlaceEnabled || mbScanlinesEnabled)
		iy = 2;

	if (mpVBXE != NULL || mArtifactMode == ATArtifactMode::NTSCHi || mArtifactMode == ATArtifactMode::PALHi || mArtifactMode == ATArtifactMode::AutoHi)
		ix = 2;

	x = ix;
	y = iy;
}

double ATGTIAEmulator::GetPixelAspectRatio() const {
	// NTSC-50 and PAL-60 are de-facto standards for when NTSC video needs to be played in
	// a PAL environment or vice versa. PAL-60, for instance, involves munging NTSC video
	// into a form good enough for a PAL decoder to accept. Therefore, we assume that the
	// video uses NTSC pixel aspect ratio, but is displayed with PAL colors.
	const double parCorrection = mb50HzMode ? 1.03964 : 0.857141;

	int ix = 1, iy = 1;
	GetPixelAspectMultiple(ix, iy);

	return (double)iy / (double)ix * parCorrection;
}

bool ATGTIAEmulator::ArePMCollisionsEnabled() const {
	return (mCollisionMask & 0xf0) != 0;
}

void ATGTIAEmulator::SetPMCollisionsEnabled(bool enable) {
	if (enable) {
		if (!(mCollisionMask & 0xf0)) {
			// we clear the collision flags directly when re-enabling collisions
			// as they were being masked in the register read
			for(int i=0; i<4; ++i) {
				mPlayerCollFlags[i] &= 0x0f;
				mMissileCollFlags[i] &= 0x0f;
			}
		}

		mCollisionMask |= 0xf0;
	} else {
		mCollisionMask &= 0x0f;
	}
}

bool ATGTIAEmulator::ArePFCollisionsEnabled() const {
	return (mCollisionMask & 0x0f) != 0;
}

void ATGTIAEmulator::SetPFCollisionsEnabled(bool enable) {
	if (enable) {
		if (!(mCollisionMask & 0x0f)) {
			// we clear the collision flags directly when re-enabling collisions
			// as they were being masked in the register read
			for(int i=0; i<4; ++i) {
				mPlayerCollFlags[i] &= 0xf0;
				mMissileCollFlags[i] &= 0xf0;
			}
		}

		mCollisionMask |= 0x0f;
	} else {
		mCollisionMask &= 0xf0;
	}
}

IVDVideoDisplay *ATGTIAEmulator::GetVideoOutput() const {
	return mpDisplay;
}

void ATGTIAEmulator::SetVideoOutput(IVDVideoDisplay *pDisplay) {
	if (mpDisplay == pDisplay)
		return;

	if (mpDisplay)
		mpDisplay->SetOnFrameStatusUpdated(nullptr);

	mpDisplay = pDisplay;

	if (mpDisplay) {
		mpDisplay->SetOnFrameStatusUpdated(
			[this](int framesQueued) {
				if (framesQueued == 0 && mbWaitingForFrame) {
					mbWaitingForFrame = false;

					if (mpOnRetryFrame)
						mpOnRetryFrame();
				}
			}
		);
	} else {
		mpFrame = NULL;
		mpDst = NULL;
	}

	mbWaitingForFrame = false;
}

void ATGTIAEmulator::SetCTIAMode(bool enabled) {
	mbCTIAMode = enabled;

	if (enabled && (mPRIOR & 0xC0)) {
		mPRIOR &= 0x3F;

		mpRenderer->SetCTIAMode();

		// scrub any register changes
		for(int i=mRCIndex; i<mRCCount; ++i) {
			if (mRegisterChanges[i].mReg == 0x1B)
				mRegisterChanges[i].mValue &= 0x3F;
		}
	}
}

void ATGTIAEmulator::SetDefectMode(ATGTIADefectMode defectMode) {
	if (mDefectMode != defectMode) {
		mDefectMode = defectMode;

		mpRenderer->SetDefectMode(defectMode);
	}
}

void ATGTIAEmulator::Set50HzMode(bool enabled) {
	mb50HzMode = enabled;
}

void ATGTIAEmulator::SetPALMode(bool enabled) {
	mbPALMode = enabled;

	RecomputePalette();
}

void ATGTIAEmulator::SetPALPhase(int phase) {
	if (mPALPhase != phase) {
		mPALPhase = phase;

		if (mColorSettings.mbUsePALParams)
			RecomputePalette();
	}
}

void ATGTIAEmulator::SetSECAMMode(bool enabled) {
	mbSECAMMode = enabled;

	mpRenderer->SetSECAMMode(enabled);
}

VDDScreenMaskParams ATGTIAEmulator::GetDefaultScreenMaskParams() {
	VDDScreenMaskParams params {};
	params.mSourcePixelsPerDot = 0.38f;
	params.mOpenness = 0.90f;
	params.mbScreenMaskIntensityCompensation = true;

	return params;
}

VDDScreenMaskParams ATGTIAEmulator::GetScreenMaskParams() const {
	return mpImpl->mScreenMaskParams;
}

void ATGTIAEmulator::SetScreenMaskParams(const VDDScreenMaskParams& params) {
	mpImpl->mScreenMaskParams = params;
}

void ATGTIAEmulator::SetConsoleSwitch(uint8 c, bool set) {
	mSwitchInput &= ~c;

	if (!set)			// bit is active low
		mSwitchInput |= c;
}

uint8 ATGTIAEmulator::ReadConsoleSwitchInputs() const {
	return (mSwitchInput & mForcedSwitchInput) & 15;
}

uint8 ATGTIAEmulator::ReadConsoleSwitches() const {
	return (~mSwitchOutput & mSwitchInput & mForcedSwitchInput) & 15;
}

void ATGTIAEmulator::SetForcedConsoleSwitches(uint8 c) {
	mForcedSwitchInput = c;
}

void ATGTIAEmulator::AddVideoTap(IATGTIAVideoTap *vtap) {
	mVideoTaps.Add(vtap);
}

void ATGTIAEmulator::RemoveVideoTap(IATGTIAVideoTap *vtap) {
	mVideoTaps.Remove(vtap);
}

void ATGTIAEmulator::AddRawFrameCallback(const ATGTIARawFrameFn *fn) {
	mRawFrameCallbacks.Add(fn);
}

void ATGTIAEmulator::RemoveRawFrameCallback(const ATGTIARawFrameFn *fn) {
	mRawFrameCallbacks.Remove(fn);
}

bool ATGTIAEmulator::IsLastFrameBufferAvailable() const {
	return mpLastFrame != nullptr;
}

bool ATGTIAEmulator::GetLastFrameBuffer(VDPixmapBuffer& pxbuf, VDPixmap& px) const {
	if (!mpLastFrame)
		return false;

	if (mpLastFrame->mpScreenFX) {
		px = mpLastFrame->ApplyScreenFX(pxbuf, mpLastFrame->mPixmap, false);
	} else {
		px = mpLastFrame->mPixmap;
	}

	return true;
}

bool ATGTIAEmulator::GetLastFrameBufferRaw(VDPixmapBuffer& pxbuf, VDPixmap& px, float& par) const {
	if (!mpLastFrame)
		return false;

	px = mpLastFrame->mPixmap;
	par = mpLastFrame->mRawPAR;
	return true;
}

void ATGTIAEmulator::DumpStatus() {
	for(int i=0; i<4; ++i) {
		ATConsolePrintf("Player  %d: color = %02X, pos = %02X, size=%d, data = %02X\n"
			, i
			, mPMColor[i]
			, mSpritePos[i]
			, mSprites[i].mState.mSizeMode
			, mSprites[i].mState.mDataLatch
			);
	}

	for(int i=0; i<4; ++i) {
		ATConsolePrintf("Missile %d: color = %02X, pos = %02X, size=%d, data = %02X\n"
			, i
			, mPRIOR & 0x10 ? mPFColor[3] : mPMColor[i]
			, mSpritePos[i+4]
			, mSprites[i+4].mState.mSizeMode
			, mSprites[i+4].mState.mDataLatch >> 6
			);
	}

	ATConsolePrintf("Playfield colors: %02X | %02X %02X %02X %02X\n"
		, mPFBAK
		, mPFColor[0]
		, mPFColor[1]
		, mPFColor[2]
		, mPFColor[3]);

	ATConsolePrintf("PRIOR:  %02X (pri=%2d%s%s %s)\n"
		, mPRIOR
		, mPRIOR & 15
		, mPRIOR & 0x10 ? ", pl5" : ""
		, mPRIOR & 0x20 ? ", multicolor" : ""
		, (mPRIOR & 0xc0) == 0x00 ? ", normal"
		: (mPRIOR & 0xc0) == 0x40 ? ", 1 color / 16 lumas"
		: (mPRIOR & 0xc0) == 0x80 ? ", 9 colors"
		: ", 16 colors / 1 luma");

	ATConsolePrintf("VDELAY: %02X\n", mVDELAY);

	ATConsolePrintf("GRACTL: %02X%s%s%s\n"
		, mGRACTL
		, mGRACTL & 0x04 ? ", latched" : ""
		, mGRACTL & 0x02 ? ", player DMA" : ""
		, mGRACTL & 0x01 ? ", missile DMA" : ""
		);

	uint8 consol = ~(mSwitchInput & mForcedSwitchInput & ~mSwitchOutput);
	ATConsolePrintf("CONSOL: %02X set <-> %02X input%s%s%s%s\n"
		, mSwitchOutput
		, mSwitchInput
		, mSwitchOutput & 0x08 ? ", speaker" : ""
		, consol & 0x04 ? ", option" : ""
		, consol & 0x02 ? ", select" : ""
		, consol & 0x01 ? ", start" : ""
		);

	uint8 v;
	for(int i=0; i<4; ++i) {
		v = ReadByte(0x00 + i);
		ATConsolePrintf("M%cPF:%s%s%s%s\n"
			, '0' + i
			, v & 0x01 ? " PF0" : ""
			, v & 0x02 ? " PF1" : ""
			, v & 0x04 ? " PF2" : ""
			, v & 0x08 ? " PF3" : "");
	}

	for(int i=0; i<4; ++i) {
		v = ReadByte(0x04 + i);
		ATConsolePrintf("P%cPF:%s%s%s%s\n"
			, '0' + i
			, v & 0x01 ? " PF0" : ""
			, v & 0x02 ? " PF1" : ""
			, v & 0x04 ? " PF2" : ""
			, v & 0x08 ? " PF3" : "");
	}

	for(int i=0; i<4; ++i) {
		v = ReadByte(0x08 + i);
		ATConsolePrintf("M%cPL:%s%s%s%s\n"
			, '0' + i
			, v & 0x01 ? " P0" : ""
			, v & 0x02 ? " P1" : ""
			, v & 0x04 ? " P2" : ""
			, v & 0x08 ? " P3" : "");
	}

	for(int i=0; i<4; ++i) {
		v = ReadByte(0x0c + i);
		ATConsolePrintf("P%cPL:%s%s%s%s\n"
			, '0' + i
			, v & 0x01 ? " PF0" : ""
			, v & 0x02 ? " PF1" : ""
			, v & 0x04 ? " PF2" : ""
			, v & 0x08 ? " PF3" : "");
	}
}

template<class T>
void ATGTIAEmulator::ExchangeStatePrivate(T& io) {
	for(int i=0; i<4; ++i)
		io != mPlayerCollFlags[i];

	for(int i=0; i<4; ++i)
		io != mMissileCollFlags[i];

	io != mbHiresMode;
	io != mActivePRIOR;
}

void ATGTIAEmulator::BeginLoadState(ATSaveStateReader& reader) {
	reader.RegisterHandlerMethod(kATSaveStateSection_Arch, VDMAKEFOURCC('G', 'T', 'I', 'A'), this, &ATGTIAEmulator::LoadStateArch);
	reader.RegisterHandlerMethod(kATSaveStateSection_Private, VDMAKEFOURCC('G', 'T', 'I', 'A'), this, &ATGTIAEmulator::LoadStatePrivate);
	reader.RegisterHandlerMethod(kATSaveStateSection_ResetPrivate, 0, this, &ATGTIAEmulator::LoadStateResetPrivate);
	reader.RegisterHandlerMethod(kATSaveStateSection_End, 0, this, &ATGTIAEmulator::EndLoadState);

	ResetSprites();
}

void ATGTIAEmulator::LoadStateArch(ATSaveStateReader& reader) {
	// P/M pos
	for(int i=0; i<8; ++i)
		reader != mSpritePos[i];

	// P/M size
	for(int i=0; i<4; ++i)
		mSprites[i].mState.mSizeMode = reader.ReadUint8() & 3;

	const uint8 missileSize = reader.ReadUint8();
	for(int i=0; i<4; ++i)
		mSprites[i+4].mState.mSizeMode = (missileSize >> (2*i)) & 3;

	// graphics latches
	for(int i=0; i<4; ++i)
		mSprites[i].mState.mDataLatch = reader.ReadUint8();

	const uint8 missileData = reader.ReadUint8();
	for(int i=0; i<4; ++i)
		mSprites[i+4].mState.mDataLatch = ((missileData >> (2*i)) & 3) << 6;

	// colors
	for(int i=0; i<4; ++i)
		reader != mPMColor[i];

	for(int i=0; i<4; ++i)
		reader != mPFColor[i];

	reader != mPFBAK;

	// misc registers
	reader != mPRIOR;
	reader != mVDELAY;
	reader != mGRACTL;
	reader != mSwitchOutput;
}

void ATGTIAEmulator::LoadStatePrivate(ATSaveStateReader& reader) {
	ExchangeStatePrivate(reader);

	// read register changes
	mRCCount = reader.ReadUint32();
	mRCIndex = 0;
	mRegisterChanges.resize(mRCCount);
	for(int i=0; i<mRCCount; ++i) {
		RegisterChange& rc = mRegisterChanges[i];

		rc.mPos = reader.ReadUint8();
		rc.mReg = reader.ReadUint8();
		rc.mValue = reader.ReadUint8();
	}

	mpRenderer->LoadState(reader);
}

void ATGTIAEmulator::LoadStateResetPrivate(ATSaveStateReader& reader) {
	for(int i=0; i<8; ++i) {
		mSprites[i].mState.mShiftState = 0;
		mSprites[i].mState.mShiftRegister = 0;
	}

	mbHiresMode = false;
	
	mRegisterChanges.clear();
	mRCCount = 0;
	mRCIndex = 0;

	mpRenderer->ResetState();
	mpRenderer->SetRegisterImmediate(0x1B, mPRIOR);
}

void ATGTIAEmulator::EndLoadState(ATSaveStateReader& writer) {
	PostLoadState();
}

class ATSaveStateGtiaInternal final : public ATSnapExchangeObject<ATSaveStateGtiaInternal, "ATSaveStateGtiaInternal"> {
public:
	template<typename T>
	void Exchange(T& rw) {
		rw.Transfer("register_changes", &mRegisterChanges);
		rw.Transfer("hires_latch", &mbHiresLatch);
		rw.Transfer("active_prior", &mActivePRIOR);
		rw.Transfer("renderer_state", &mpRendererState);

		if constexpr (T::IsReader) {
			const size_t n = mRegisterChanges.size();

			if (n % 3)
				throw ATInvalidSaveStateException();

			for(size_t i = 0; i < n; i += 3) {
				// Register changes are nominally within [0, 228) with a little bit of offset for
				// pipeline delays. We allow a little bit of slop just in case (we can accommodate
				// slightly wacky timings, just don't want times off by a whole frame or so).
				if (mRegisterChanges[i] < -16 || mRegisterChanges[i] > 228 + 16)
					throw ATInvalidSaveStateException();

				if ((uint32)mRegisterChanges[i+1] > 255)
					throw ATInvalidSaveStateException();

				if ((uint32)mRegisterChanges[i+2] > 255)
					throw ATInvalidSaveStateException();
			}
		}
	}

	vdfastvector<sint16> mRegisterChanges;
	bool mbHiresLatch;
	uint8 mActivePRIOR;

	vdrefptr<IATSerializable> mpRendererState;
};

class ATSaveStateGtia final : public ATSnapExchangeObject<ATSaveStateGtia, "ATSaveStateGtia"> {
public:
	template<typename T>
	void Exchange(T& rw) {
		rw.TransferArray("hpospn", mHPOSP);
		rw.TransferArray("hposmn", mHPOSM);
		rw.TransferArray("sizepn", mSIZEP);
		rw.Transfer("sizem", &mSIZEM);
		rw.TransferArray("grafpn", mGRAFP);
		rw.Transfer("grafm", &mGRAFM);
		rw.TransferArray("colpm", mCOLPM);
		rw.TransferArray("colpf", mCOLPF);
		rw.Transfer("colbk", &mCOLBK);
		rw.Transfer("prior", &mPRIOR);
		rw.Transfer("vdelay", &mVDELAY);
		rw.Transfer("gractl", &mGRACTL);
		rw.Transfer("consol", &mCONSOL);
		rw.TransferArray("pnpf", mPnPF);
		rw.TransferArray("mnpf", mMnPF);
		rw.TransferArray("pnpl", mPnPL);
		rw.TransferArray("mnpl", mMnPL);
		rw.Transfer("internal_state", &mpInternalState);
	}
	
	uint8 mHPOSP[4];
	uint8 mHPOSM[4];
	uint8 mSIZEP[4];
	uint8 mSIZEM;
	uint8 mGRAFP[4];
	uint8 mGRAFM;
	uint8 mCOLPM[4];
	uint8 mCOLPF[4];
	uint8 mCOLBK;
	uint8 mPRIOR;
	uint8 mVDELAY;
	uint8 mGRACTL;
	uint8 mCONSOL;
	uint8 mPnPF[4];
	uint8 mMnPF[4];
	uint8 mPnPL[4];
	uint8 mMnPL[4];

	vdrefptr<ATSaveStateGtiaInternal> mpInternalState;
};

void ATGTIAEmulator::SaveState(IATObjectState **pp) {
	vdrefptr<ATSaveStateGtia> obj(new ATSaveStateGtia);
	vdrefptr<ATSaveStateGtiaInternal> obj2(new ATSaveStateGtiaInternal);

	// P/M pos
	for(int i=0; i<4; ++i)
		obj->mHPOSP[i] = mSpritePos[i];

	for(int i=0; i<4; ++i)
		obj->mHPOSM[i] = mSpritePos[i + 4];

	// P/M size
	for(int i=0; i<4; ++i)
		obj->mSIZEP[i] = mSprites[i].mState.mSizeMode;

	obj->mSIZEM =
		(mSprites[4].mState.mSizeMode << 0) +
		(mSprites[5].mState.mSizeMode << 2) +
		(mSprites[6].mState.mSizeMode << 4) +
		(mSprites[7].mState.mSizeMode << 6);

	// graphics latches
	for(int i=0; i<4; ++i)
		obj->mGRAFP[i] = mSprites[i].mState.mDataLatch;

	obj->mGRAFM =
		(mSprites[4].mState.mDataLatch >> 6) +
		(mSprites[5].mState.mDataLatch >> 4) +
		(mSprites[6].mState.mDataLatch >> 2) +
		(mSprites[7].mState.mDataLatch >> 0);

	// colors
	for(int i=0; i<4; ++i)
		obj->mCOLPM[i] = mPMColor[i];

	for(int i=0; i<4; ++i)
		obj->mCOLPF[i] = mPFColor[i];

	obj->mCOLBK = mPFBAK;
	obj->mPRIOR = mPRIOR;
	obj->mVDELAY = mVDELAY;
	obj->mGRACTL = mGRACTL;
	obj->mCONSOL = mSwitchOutput;

	for(int i=0; i<4; ++i)
		obj->mPnPF[i] = mPlayerCollFlags[i] & 15;

	for(int i=0; i<4; ++i)
		obj->mMnPF[i] = mMissileCollFlags[i] & 15;

	for(int i=0; i<4; ++i)
		obj->mMnPL[i] = mMissileCollFlags[i] >> 4;

	obj->mPnPL[0] = ( ((mPlayerCollFlags[1] >> 3) & 0x02)	// 1 -> 0
					+ ((mPlayerCollFlags[2] >> 2) & 0x04)	// 2 -> 0
					+ ((mPlayerCollFlags[3] >> 1) & 0x08));	// 3 -> 0

	obj->mPnPL[1] = ( ((mPlayerCollFlags[1] >> 4) & 0x01)	// 1 -> 0
					+ ((mPlayerCollFlags[2] >> 3) & 0x04)	// 2 -> 1
					+ ((mPlayerCollFlags[3] >> 2) & 0x08));	// 3 -> 1

	obj->mPnPL[2] = ( ((mPlayerCollFlags[2] >> 4) & 0x03)	// 2 -> 0, 1
					+ ((mPlayerCollFlags[3] >> 3) & 0x08));	// 3 -> 2

	obj->mPnPL[3] = ( ((mPlayerCollFlags[3] >> 4) & 0x07));	// 3 -> 0, 1, 2


	obj2->mbHiresLatch = mbHiresMode;
	obj2->mActivePRIOR = mActivePRIOR;

	for(int i=mRCIndex; i<mRCCount; ++i) {
		const RegisterChange& rc = mRegisterChanges[i];
		
		obj2->mRegisterChanges.push_back(rc.mPos);
		obj2->mRegisterChanges.push_back(rc.mReg);
		obj2->mRegisterChanges.push_back(rc.mValue);
	}

	mpRenderer->SaveState(~obj2->mpRendererState);

	obj->mpInternalState = std::move(obj2);
	*pp = obj.release();
}

void ATGTIAEmulator::LoadState(const IATObjectState *state) {
	if (state)
		LoadState(atser_cast<const ATSaveStateGtia&>(*state));
	else
		LoadState(ATSaveStateGtia{});
}

void ATGTIAEmulator::LoadState(const ATSaveStateGtia& gstate) {
	// P/M pos
	for(int i=0; i<4; ++i)
		mSpritePos[i] = gstate.mHPOSP[i];

	for(int i=0; i<4; ++i)
		mSpritePos[i + 4] = gstate.mHPOSM[i];

	// P/M size
	for(int i=0; i<4; ++i)
		mSprites[i].mState.mSizeMode = gstate.mSIZEP[i];

	for(int i=0; i<4; ++i)
		mSprites[i+4].mState.mSizeMode = (gstate.mSIZEM >> (2 * i)) & 3;

	// graphics latches
	for(int i=0; i<4; ++i)
		mSprites[i].mState.mDataLatch = gstate.mGRAFP[i];

	for(int i=0; i<4; ++i)
		mSprites[i+4].mState.mDataLatch = (gstate.mGRAFM << (2*i)) & 0xC0;

	// colors
	for(int i=0; i<4; ++i)
		mPMColor[i] = gstate.mCOLPM[i];

	for(int i=0; i<4; ++i)
		mPFColor[i] = gstate.mCOLPF[i];

	mPFBAK = gstate.mCOLBK;
	mPRIOR = gstate.mPRIOR;
	mVDELAY = gstate.mVDELAY;
	mGRACTL = gstate.mGRACTL;
	mSwitchOutput = gstate.mCONSOL;

	for(int i=0; i<4; ++i)
		mPlayerCollFlags[i] = gstate.mPnPF[i];

	for(int i=0; i<4; ++i)
		mMissileCollFlags[i] = gstate.mMnPF[i] + (gstate.mMnPL[i] << 4);

	// Reconstitute internal player collision flags.
	//
	// The PnPL matrix is diagonally symmetric, so we only need to
	// extract half of the data. The internal matrix only tracks
	// the unique six bits.

	mPlayerCollFlags[1] += (gstate.mPnPL[1] << 4) & 0x10;
	mPlayerCollFlags[2] += (gstate.mPnPL[2] << 4) & 0x30;
	mPlayerCollFlags[3] += (gstate.mPnPL[3] << 4) & 0x70;

	mRegisterChanges.clear();
	mRCIndex = 0;
	mRCCount = 0;

	mActivePRIOR = mPRIOR;
	mbHiresMode = mbANTICHiresMode && !(mPRIOR & 0xC0);

	if (gstate.mpInternalState) {
		const auto& gistate = *gstate.mpInternalState;

		mbHiresMode = gistate.mbHiresLatch;
		mActivePRIOR = gistate.mActivePRIOR;

		size_t len = gistate.mRegisterChanges.size();
		size_t numChanges = len / 3;
		mRegisterChanges.resize(numChanges);
		mRCCount = numChanges;

		for(size_t i = 0, j = 0; i < numChanges; ++i, j += 3) {
			RegisterChange& rc = mRegisterChanges[i];

			rc.mPos = gistate.mRegisterChanges[j + 0];
			rc.mReg = gistate.mRegisterChanges[j + 1];
			rc.mValue = gistate.mRegisterChanges[j + 2];
		}

		mpRenderer->LoadState(gistate.mpRendererState);
	} else
		mpRenderer->LoadState(nullptr);

	PostLoadState();
}

void ATGTIAEmulator::PostLoadState() {
	// recompute derived state
	mpConn->GTIASetSpeaker(0 != (mSwitchOutput & 8));

	for(int i=0; i<4; ++i) {
		mpRenderer->SetRegisterImmediate(0x12 + i, mPMColor[i]);
		mpRenderer->SetRegisterImmediate(0x16 + i, mPFColor[i]);
	}

	mpRenderer->SetRegisterImmediate(0x1A, mPFBAK);

	// Terminate existing scan line
	mpDst = NULL;
	mpRenderer->EndScanline();

	// set the sprite flag in case any sprites should be loaded; it'll get
	// auto-cleared if there aren't any found on next sprite evaluation
	mbSpritesActive = true;
}

void ATGTIAEmulator::GetRegisterState(ATGTIARegisterState& state) const {
	state = mState;

	// $D000-D007 HPOSP0-3, HPOSM0-3
	for(int i=0; i<8; ++i)
		state.mReg[i] = mSpritePos[i];

	// $D008-D00B SIZEP0-3
	for(int i=0; i<4; ++i)
		state.mReg[i+8] = mSprites[i].mState.mSizeMode;

	// $D00C SIZEM
	state.mReg[0x0C] = 
		(mSprites[4].mState.mSizeMode << 0) +
		(mSprites[5].mState.mSizeMode << 2) +
		(mSprites[6].mState.mSizeMode << 4) +
		(mSprites[7].mState.mSizeMode << 6);

	// GRAFP0-GRAFP3
	state.mReg[0x0D] = mSprites[0].mState.mDataLatch;
	state.mReg[0x0E] = mSprites[1].mState.mDataLatch;
	state.mReg[0x0F] = mSprites[2].mState.mDataLatch;
	state.mReg[0x10] = mSprites[3].mState.mDataLatch;

	// GRAFM
	state.mReg[0x11] = 
		(mSprites[4].mState.mDataLatch >> 6) +
		(mSprites[5].mState.mDataLatch >> 4) +
		(mSprites[6].mState.mDataLatch >> 2) +
		(mSprites[7].mState.mDataLatch >> 0);

	state.mReg[0x12] = mPMColor[0];
	state.mReg[0x13] = mPMColor[1];
	state.mReg[0x14] = mPMColor[2];
	state.mReg[0x15] = mPMColor[3];
	state.mReg[0x16] = mPFColor[0];
	state.mReg[0x17] = mPFColor[1];
	state.mReg[0x18] = mPFColor[2];
	state.mReg[0x19] = mPFColor[3];
	state.mReg[0x1A] = mPFBAK;
	state.mReg[0x1B] = mPRIOR;
	state.mReg[0x1C] = mVDELAY;
	state.mReg[0x1D] = mGRACTL;
	state.mReg[0x1F] = mSwitchOutput;
}

void ATGTIAEmulator::DumpHighArtifactingFilters(ATConsoleOutput& output) {
	mpArtifactingEngine->DumpHighArtifactingFilters(output);
}

void ATGTIAEmulator::SetNextFieldPolarity(ATVideoFieldPolarity nextFieldPolarity) {
	mNextFieldPolarity = nextFieldPolarity;
}

void ATGTIAEmulator::SetVBLANK(VBlankMode vblMode) {
	mVBlankMode = vblMode;
}

void ATGTIAEmulator::SetOnRetryFrame(vdfunction<void()> fn) {
	mpOnRetryFrame = fn;
}

bool ATGTIAEmulator::BeginFrame(uint32 frameNumber, uint32 y, bool force, bool drop) {
	if (mpFrame)
		return true;

	if (!mpDisplay)
		return true;

	ResetLightSensors();

	// check if we have a video tap, which means that we must always generate
	// a frame even if we aren't displaying it
	const bool alwaysNeedFrame = !mVideoTaps.IsEmpty();

	// grab a frame if we are not being asked to drop it
	if (mpDisplay->GetVSyncStatus().mPresentQueueTime > g_ATCVDisplayDropLagThreshold) {
		++mDisplayLagCounter;

		if (mDisplayLagCounter > g_ATCVDisplayDropCountThreshold) {
			mDisplayLagCounter = 0;
			if (!alwaysNeedFrame)
				drop = true;
		}
	} else
		mDisplayLagCounter = 0;

	if (!drop || alwaysNeedFrame) {
		const bool isFramePending = mpDisplay->GetQueuedFrames() > 1;

		vdrefptr<VDVideoDisplayFrame> newFrame;
		if (!isFramePending && mpDisplay->RevokeBuffer(false, ~newFrame)) {
			mpFrame = static_cast<ATFrameBuffer *>(newFrame.get());
			VDASSERT(!mpFrame->mbDroppedFrame);
		} else {
			if ((!isFramePending || mbTurbo) && mpFrameTracker->mActiveFrames < 3) {
				// create a new frame
				ATFrameBuffer *fb = new ATFrameBuffer(mpFrameTracker, *mpArtifactingEngine);
				mpFrame = fb;

				fb->mPixmap.format = 0;
				fb->mbAllowConversion = true;
				fb->mFlags = 0;
			} else if (alwaysNeedFrame || (!mbTurbo && !isFramePending)) {
				// try to recycle a frame
				if (mpDisplay->RevokeBuffer(true, ~newFrame)) {
					mpFrame = static_cast<ATFrameBuffer *>(newFrame.get());
					VDASSERT(!mpFrame->mbDroppedFrame);
				} else {
					// we can't get a free framebuffer -- block if we are allowed to
					if (!force) {
						mbWaitingForFrame = true;
						return false;
					}
				}

				// proceed with frame without generating/rendering it
			}
		}
	}

	if (mpFrame) {
		mpFrame->mFrameNumber = frameNumber;
	}
	
	mbWaitingForFrame = false;

	mRawFrame.data = nullptr;

	// Process interlace detection/prediction. We do this here so that auto-flip
	// priority is preserved even when skipping frames.
	if (mFrameProperties.mbInterlaced) {
		// If field polarity is alternating, then we can use the polarity from the video
		// stream; otherwise, auto-flip from the last polarity. This emulates when a TV
		// simulates an interlaced stream from a non-interlaced source.
		//
		// We may not be able to reliably detect field polarity for both fields, so we
		// simply require one of the polarities to not be unknown -- which we will then
		// invert for the other field.

		if (mNextFieldPolarity != mLastFieldPolarity) {
			if (mNextFieldPolarity == ATVideoFieldPolarity::Unknown)
				mbUseLowerField = mLastFieldPolarity != ATVideoFieldPolarity::Lower;
			else
				mbUseLowerField = mNextFieldPolarity == ATVideoFieldPolarity::Lower;
		} else {
			mbUseLowerField = !mbUseLowerField;
		}

		mLastFieldPolarity = mNextFieldPolarity;

		// Interlace looks pretty bad if we lose fields, so we need to render the
		// frame to a side buffer. This frame needs not to be included in the frame
		// tracker as it doesn't get presented.
		if (!mpFrame) {
			if (!mpDroppedFrame) {
				mpDroppedFrame = new ATFrameBuffer(nullptr, *mpArtifactingEngine);
				mpDroppedFrame->mbDroppedFrame = true;
			}

			mpFrame = mpDroppedFrame;
		}
	}

	if (mpFrame) {
		ATFrameBuffer *fb = mpFrame;

		fb->mFlags &= ~(IVDVideoDisplay::kVSync | IVDVideoDisplay::kVSyncAdaptive);

		if (mbVsyncEnabled) {
			fb->mFlags |= IVDVideoDisplay::kVSync;

			if (mbVsyncAdaptiveEnabled)
				fb->mFlags |= IVDVideoDisplay::kVSyncAdaptive;
		}

		mbFrameCopiedFromPrev = false;

		SetFrameProperties();

		// needed for mRawFrame below even if no postprocessing
		mPreArtifactFrame.h = mFrameProperties.mbOverscanPALExtended ? 312 : 262;

		int format = mFrameProperties.mbOutputRgb32 ? nsVDPixmap::kPixFormat_XRGB8888 : nsVDPixmap::kPixFormat_Pal8;

		// compute size of full frame buffer, including overscan
		int frameWidth = 456;
		if (mFrameProperties.mbOutputHoriz2x)
			frameWidth *= 2;

		int frameHeight = mFrameProperties.mbOverscanPALExtended ? 312 : 262;
		
		const bool dualFieldFrame = mFrameProperties.mbInterlaced || mFrameProperties.mbSoftScanlines;
		if (dualFieldFrame)
			frameHeight *= 2;

		// check if we need to reinitialize the frame bitmap
		if (fb->mBuffer.format != format || fb->mBuffer.w != frameWidth || fb->mBuffer.h != frameHeight) {
			VDPixmapLayout layout;
			VDPixmapCreateLinearLayout(layout, format, frameWidth, frameHeight, 16);

			// Add a little extra width on the end so we can go over slightly with MASKMOVDQU on SSE2
			// routines.
			fb->mBuffer.init(layout, 32);

			memset(fb->mBuffer.base(), 0, fb->mBuffer.size());
		}

		fb->mbDualFieldFrame = dualFieldFrame;

		fb->mbLowerField = mFrameProperties.mbInterlaced && mbUseLowerField;
		fb->mbDuplicateField = false;

		fb->mPixmap = fb->mBuffer;

		if (mFrameProperties.mbPaletteOutputCorrection) {
			if (mFrameProperties.mbOutputExtendedRange)
				fb->mPixmap.palette = mSignedPalette;
			else
				fb->mPixmap.palette = mPalette;
		} else {
			if (mFrameProperties.mbOutputExtendedRange)
				fb->mPixmap.palette = mUncorrectedSignedPalette;
			else
				fb->mPixmap.palette = mUncorrectedPalette;
		}

		fb->mpPalette = mPalette;

		fb->mRawPAR = (float)GetPixelAspectRatio();

		if (mFrameProperties.mbAccelScanlines)
			fb->mRawPAR *= 0.5f;

		mRawFrame = mPreArtifactFrame;

		if (!mFrameProperties.mbSoftPostProcess8) {
			mRawFrame.data = fb->mBuffer.data;
			mRawFrame.pitch = fb->mBuffer.pitch;

			if (dualFieldFrame) {
				mRawFrame.pitch *= 2;

				if (fb->mbLowerField)
					mRawFrame.data = (char *)mRawFrame.data + fb->mBuffer.pitch;
			}
		}

		// get visible area in color clocks
		vdrect32 scanArea = GetFrameScanArea();

		// In PAL extended mode, the top of the frame extends above scan 0 by our
		// numbering, so we must rebias the scan window here. The rendering code
		// similarly compensates by 16 scans.
		if (scanArea.top < 0) {
			scanArea.bottom -= scanArea.top;
			scanArea.top = 0;
		}

		// convert view area to hires pixels (320 res), our standard output image res.
		vdrect32 frameViewRect = scanArea;

		frameViewRect.left *= 2;
		frameViewRect.right *= 2;

		// double left/right if we're generating at double hires (14MHz instead of 7MHz)
		if (mFrameProperties.mbOutputHoriz2x) {
			frameViewRect.left *= 2;
			frameViewRect.right *= 2;
		}

		// convert the frame view rect to image view rect, which is the same except if the
		// image contains two fields (interlace or soft scanlines, but NOT accel scanlines)
		vdrect32 imageViewRect = frameViewRect;
		if (dualFieldFrame) {
			imageViewRect.top *= 2;
			imageViewRect.bottom *= 2;
		}

		// set pixmap on view area over framebuffer
		fb->mPixmap.w = imageViewRect.width();
		fb->mPixmap.h = imageViewRect.height();

		fb->mPixmap.data = (char *)fb->mPixmap.data + imageViewRect.left * (mFrameProperties.mbOutputRgb32 ? 4 : 1) + fb->mPixmap.pitch * imageViewRect.top;

		// set up hardware screen FX
		if (mFrameProperties.mbAccelPostProcess) {
			const ATColorParams& params = mActiveColorParams;
			const auto& ap = mpArtifactingEngine->GetArtifactingParams();

			fb->mpScreenFX = &fb->mScreenFX;

			fb->mScreenFX = {};
			fb->mScreenFX.mScanlineIntensity = mFrameProperties.mbAccelScanlines ? mpArtifactingEngine->GetArtifactingParams().mScanlineIntensity : 0.0f;
			fb->mScreenFX.mPALBlendingOffset = mFrameProperties.mbAccelPalArtifacting ? dualFieldFrame ? -2.0f : -1.0f : 0.0f;
			fb->mScreenFX.mbSignedRGBEncoding = mFrameProperties.mbOutputExtendedRange;
			fb->mScreenFX.mHDRIntensity = !mFrameProperties.mbOutputSceneReferred ? 1.0f : ap.mbUseSystemSDRAsHDR ? -1.0f : ap.mHDRIntensity / 80.0f;

			// Set color correction matrix and gamma. For 32-bit, we can and do want to hardware accelerate
			// this lookup if possible. If we're using a raw 8-bit frame, the color correction and gamma
			// correction is already baked into the palette for free and we should not apply it again.
			if (mFrameProperties.mbAccelOutputCorrection) {
				memcpy(fb->mScreenFX.mColorCorrectionMatrix, mColorMatchingMatrix, sizeof fb->mScreenFX.mColorCorrectionMatrix);
				fb->mScreenFX.mGamma = params.mGammaCorrect;
				fb->mScreenFX.mOutputGamma = mOutputGamma;
			} else {
				memset(fb->mScreenFX.mColorCorrectionMatrix, 0, sizeof fb->mScreenFX.mColorCorrectionMatrix);
				fb->mScreenFX.mGamma = 1.0f;
			}

			fb->mScreenFX.mDistortionX = ap.mDistortionViewAngleX;
			fb->mScreenFX.mDistortionYRatio = ap.mDistortionYRatio;
			fb->mScreenFX.mVignetteIntensity = ap.mbEnableVignette
				? std::max(0.0f, ap.mVignetteIntensity) : 0.0f;

			if (mFrameProperties.mbAccelScreenMask)
				fb->mScreenFX.mScreenMaskParams = mpImpl->mScreenMaskParams;
			else
				fb->mScreenFX.mScreenMaskParams = {};

			// modify the dot pitch to change units from dots/pitch to dots/cc
			fb->mScreenFX.mScreenMaskParams.mSourcePixelsPerDot *=
				mFrameProperties.mbOutputHoriz2x ? 4.0f : 2.0f;

			if (ap.mbEnableBloom) {
				fb->mScreenFX.mbBloomEnabled = true;
				fb->mScreenFX.mbBloomClipToCustomPrimaries = mFrameProperties.mbOutputAdvancedColor && !mFrameProperties.mbOutputSceneReferred;
				fb->mScreenFX.mBloomRadius = ap.mBloomRadius;
				fb->mScreenFX.mBloomDirectIntensity = ap.mBloomDirectIntensity;
				fb->mScreenFX.mBloomIndirectIntensity = ap.mBloomIndirectIntensity;

				if (ap.mbBloomScanlineCompensation) {
					if (fb->mScreenFX.mScanlineIntensity != 0) {
						// Scanline intensity is described in gamma space while bloom intensity is
						// linear; convert the scanline intensities to linear, average in linear,
						// then inverse scale the bloom intensities to compensate.
						const float i1 = 1.0f;
						const float i2 = fb->mScreenFX.mScanlineIntensity;
						float i3 = 0.5f * (powf(i1, 2.2f) + powf(i2, 2.2f));

						fb->mScreenFX.mBloomDirectIntensity /= i3;
						fb->mScreenFX.mBloomIndirectIntensity /= i3;
					}
				}
			} else {
				fb->mScreenFX.mbBloomEnabled = false;
				fb->mScreenFX.mbBloomClipToCustomPrimaries = false;
				fb->mScreenFX.mBloomThreshold = 0;
				fb->mScreenFX.mBloomRadius = 0;
				fb->mScreenFX.mBloomDirectIntensity = 0;
				fb->mScreenFX.mBloomIndirectIntensity = 0;
			}
		} else {
			fb->mpScreenFX = nullptr;
		}

		mPreArtifactFrameVisibleY1 = frameViewRect.top;
		mPreArtifactFrameVisibleY2 = frameViewRect.bottom;

		fb->mViewX1 = imageViewRect.left;
		fb->mViewY1 = imageViewRect.top;
		fb->mbIncludeHBlank = mFrameProperties.mbIncludeHBlank;

		// copy over previous field
		if (mFrameProperties.mbInterlaced) {
			VDPixmap dstField(VDPixmapExtractField(mpFrame->mPixmap, !fb->mbLowerField));

			if (mpLastFrame &&
				mpLastFrame->mPixmap.w == mpFrame->mPixmap.w &&
				mpLastFrame->mPixmap.h == mpFrame->mPixmap.h &&
				mpLastFrame->mPixmap.format == mpFrame->mPixmap.format
				) {
				VDPixmap srcField(VDPixmapExtractField(mpLastFrame->mPixmap, !fb->mbLowerField));

				VDPixmapBlt(dstField, srcField);
			} else {
				fb->mbDuplicateField = true;
			}
		}
	}

	mFrameTimestamp = mpConn->GTIAGetTimestamp64();

	// Reset Y to avoid weirdness in immediate updates from being between BeginFrame() and
	// the first BeginScanline().
	mY = y;

	return true;
}

void ATGTIAEmulator::BeginScanline(int y, bool hires) {
	// Flush remaining register changes (required for PRIOR to interact properly with hires)
	//
	// Note that we must use a cycle offset of -1 here because we haven't done DMA fetches
	// for this cycle yet!
	Sync(-1);

	mbMixedRendering = false;
	mbANTICHiresMode = hires;
	mbHiresMode = hires && !(mActivePRIOR & 0xc0);
	mbGTIADisableTransition = false;
	mbDefectType2Active = false;

	if ((unsigned)(y - 8) < 240)
		mbScanlinesWithHiRes[y - 8] = mbHiresMode;

	if (mpVBXE) {
		if (y >= 8 && y <= 9) {
			if (y == (!mbPALMode && g_ATCVDevicesVbxeNtscOffset ? 9 : 8))
				mpVBXE->BeginFrame(mFrameProperties.mbPaletteOutputCorrection ? (int)mActiveColorParams.mColorMatchingMode : 0, mFrameProperties.mbOutputExtendedRange);
		} else if (y == 248)
			mpVBXE->EndFrame();
	}

	mpDst = NULL;
	
	mY = y;
	mbPMRendered = false;

	if (mpFrame) {
		int yw = y;
		int h = mRawFrame.h;

		if (mFrameProperties.mbOverscanPALExtended) {
			// What we do here is wrap the last 16 lines back up to the top of
			// the display. This isn't correct, as it causes those lines to
			// lead by a frame, but it at least solves the vertical position
			// issue.
			if (yw >= 312 - 16)
				yw -= 312 - 16;
			else
				yw += 16;
		}

		if (yw < h)
			mpDst = (uint8 *)mRawFrame.data + mRawFrame.pitch * yw;

		if (y == 248 && !mRawFrameCallbacks.IsEmpty()) {
			VDPixmap px(mRawFrame);

			px.w = 376;
			px.h = 240;
			px.data = (char *)px.data + px.pitch * 8;

			mRawFrameCallbacks.NotifyAll([&](const ATGTIARawFrameFn *fn) { (*fn)(px); });
		}
	}

	memset(mMergeBuffer, 0, sizeof mMergeBuffer);
	memset(mAnticData, 0, sizeof mAnticData);

	if (mpVBXE)
		mpVBXE->BeginScanline(y, (uint32*)mpDst, mMergeBuffer, mAnticData, mbHiresMode);
	else if (mpDst) {
		mpRenderer->SetVBlank((uint32)(y - 8) >= 240);
		mpRenderer->BeginScanline(mpDst, mMergeBuffer, mAnticData, mbHiresMode);
	}
}

void ATGTIAEmulator::EndScanline(uint8 dlControl, bool pfrendered) {
	// flush any remaining changes
	Sync();

	if (mpDst) {
		if (mpVBXE)
			mpVBXE->RenderScanline(222, pfrendered || mbPMRendered);
		else
			mpRenderer->RenderScanline(222, pfrendered, mbPMRendered, mbMixedRendering);
	}

	if (!mLightSensors.empty()) [[unlikely]]
		ProcessLightSensors();

	if (mpVBXE)
		mpVBXE->EndScanline();
	else
		mpRenderer->EndScanline();

	// move down buffers as necessary and offset all pending render changes by -scanline
	if (mRCIndex >= 64) {
		mRegisterChanges.erase(mRegisterChanges.begin(), mRegisterChanges.begin() + mRCIndex);
		mRCCount -= mRCIndex;
		mRCIndex = 0;
	}

	VDASSERT(mRCCount == mRegisterChanges.size());

	for(int i=mRCIndex; i<mRCCount; ++i) {
		mRegisterChanges[i].mPos -= 228;
	}

	for(int i=0; i<8; ++i) {
		Sprite& sprite = mSprites[i];

		sprite.mLastSync -= 228;

		// make sure the sprites don't get too far behind
		if (sprite.mLastSync < -10000)
			sprite.Sync(-2);

		for(SpriteImage *image = sprite.mpImageHead; image; image = image->mpNext) {
			image->mX1 -= 228;
			image->mX2 -= 228;
		}

		// delete any sprites that are too old (this can happen in vblank)
		while(sprite.mpImageHead && sprite.mpImageHead->mX2 < 34) {
			SpriteImage *next = sprite.mpImageHead->mpNext;
			FreeSpriteImage(sprite.mpImageHead);
			sprite.mpImageHead = next;

			if (!next)
				sprite.mpImageTail = NULL;
		}

		// check if the sprite is stuck -- if so, continue extending the current image
		if (sprite.mpImageTail && sprite.mState.mSizeMode == 2 && (sprite.mState.mShiftState == 1 || sprite.mState.mShiftState == 2)) {
			sprite.mpImageTail->mX1 = -2;
			sprite.mpImageTail->mX2 = 1024;
		}
	}

	// We have to restart at -2 instead of 0 because GTIA runs two color clocks head of ANTIC
	// for timing purposes.
	mLastSyncX = -2;

	if (mY >= 8 && mY < 248) {
		auto& lineColors = mColorTrace.mColors[mY - 8];

		for(int i=0; i<4; ++i) {
			lineColors[i] = mPMColor[i];
			lineColors[i+4] = mPFColor[i];
		}

		lineColors[8] = mPFBAK;
	}

	if (!mpDst)
		return;

	if (mAnalysisMode != kAnalyzeNone) {
		uint8 analysisPixels[9];
		int numPixels = 0;

		if (mAnalysisMode == kAnalyzeColors) {
			numPixels = 9;

			for(int i=0; i<4; ++i) {
				analysisPixels[i] = mPMColor[i];
				analysisPixels[i+4] = mPFColor[i];
			}

			analysisPixels[8] = mPFBAK;
		}

		if (mAnalysisMode == kAnalyzeDList) {
			numPixels = 6;

			analysisPixels[0] = (dlControl & 0x80) ? 0x1f : 0x00;
			analysisPixels[1] = (dlControl & 0x40) ? 0x3f : 0x00;
			analysisPixels[2] = (dlControl & 0x20) ? 0x5f : 0x00;
			analysisPixels[3] = (dlControl & 0x10) ? 0x7f : 0x00;
			analysisPixels[4] = analysisPixels[5] = ((dlControl & 0x0f) << 4) + 15;
		}

		if (mFrameProperties.mbRenderRgb32) {
			uint32 *dst32 = (uint32 *)mpDst;
			const uint32 *VDRESTRICT palette = mFrameProperties.mbOutputExtendedRange ? mSignedPalette : mPalette;

			if (mFrameProperties.mbRenderHoriz2x) {
				for(int i=0; i<numPixels; ++i) {
					dst32[0] = dst32[1] = dst32[2] = dst32[3] = palette[analysisPixels[i]];
					dst32 += 4;
				}
			} else {
				for(int i=0; i<numPixels; ++i) {
					dst32[0] = dst32[1] = palette[analysisPixels[i]];
					dst32 += 2;
				}
			}
		} else {
			for(int i=0; i<numPixels; ++i) {
				mpDst[i*2] = mpDst[i*2+1] = analysisPixels[i];
			}
		}
	}
}

void ATGTIAEmulator::UpdatePlayer(bool odd, int index, uint8 byte) {
	if (mGRACTL & 2) {
		if (odd || !(mVDELAY & (0x10 << index))) {
			const uint8 xpos = mpConn->GTIAGetXClock();
			AddRegisterChange(xpos + 3, 0x0D + index, byte);
		}
	}
}

void ATGTIAEmulator::UpdateMissile(bool odd, uint8 byte) {
	if (mGRACTL & 1) {
		const uint8 xpos = mpConn->GTIAGetXClock();
		AddRegisterChange(xpos + 3, 0x20, byte);
	}
}

void ATGTIAEmulator::UpdatePlayfield160(uint32 x, uint8 byte) {
	VDASSERT(x < 114);

	uint8 *dst = &mMergeBuffer[x*2];

	dst[0] = (byte >>  4) & 15;
	dst[1] = (byte      ) & 15;
}

void ATGTIAEmulator::UpdatePlayfield160(uint32 x, const uint8 *__restrict src, uint32 n) {
	if (!n)
		return;

	VDASSERT(x < 114);
	uint8 *__restrict dst = &mMergeBuffer[x*2];

#ifdef VD_CPU_X86
	if (SSE2_enabled) {
		atasm_update_playfield_160_sse2(dst, src, n);
		return;
	}
#endif

#ifdef VD_CPU_AMD64
	atasm_update_playfield_160_sse2(dst, src, n);
#elif defined(VD_CPU_ARM64)
	atasm_update_playfield_160_neon(dst, src, n);
#else
	do {
		const uint8 byte = *src++;
		dst[0] = (byte >>  4) & 15;
		dst[1] = (byte      ) & 15;
		dst += 2;
	} while(--n);
#endif
}

void ATGTIAEmulator::UpdatePlayfield320(uint32 x, uint8 byte) {
	uint8 *dstx = &mMergeBuffer[x];
	dstx[0] = PF2;
	dstx[1] = PF2;
	
	VDASSERT(x < 228);

	uint8 *dst = &mAnticData[x];
	dst[0] = (byte >> 2) & 3;
	dst[1] = (byte >> 0) & 3;
}

void ATGTIAEmulator::UpdatePlayfield320(uint32 x, const uint8 *src, uint32 n) {
	VDASSERT(x < 228);

#if VD_CPU_X86 || VD_CPU_X64
	atasm_update_playfield_320_sse2(mMergeBuffer, mAnticData, src, x, n);
#else
	memset(&mMergeBuffer[x], PF2, n*2);
	
	uint8 *VDRESTRICT dst = &mAnticData[x];
	do {
		const uint8 byte = *src++;
		dst[0] = (byte >> 2) & 3;
		dst[1] = (byte >> 0) & 3;
		dst += 2;
	} while(--n);
#endif
}

namespace {
	void Convert160To320(int x1, int x2, uint8 *dst, const uint8 *src) {
		static const uint8 kPriTable[16]={
			0,		// BAK
			0,		// PF0
			1,		// PF1
			1,		// PF01
			2,		// PF2
			2,		// PF02
			2,		// PF12
			2,		// PF012
			3,		// PF3
			3,		// PF03
			3,		// PF13
			3,		// PF013
			3,		// PF23
			3,		// PF023
			3,		// PF123
			3,		// PF0123
		};

		for(int x=x1; x<x2; ++x)
			dst[x] = kPriTable[src[x]];
	}

	void Convert320To160(int x1, int x2, uint8 *dst, const uint8 *src) {
		for(int x=x1; x<x2; ++x) {
			uint8 c = src[x];

			if (dst[x] & PF2)
				dst[x] = 1 << c;
		}
	}
}

void ATGTIAEmulator::Sync(int offset) {
	mpConn->GTIARequestAnticSync(offset);

	int xend = (int)mpConn->GTIAGetXClock() + 2;

	if (xend > 228)
		xend = 228;

	SyncTo(xend);
}

void ATGTIAEmulator::SyncTo(int xend) {
	int x1 = mLastSyncX;

	if (x1 >= xend)
		return;

	// render spans and process register changes
	do {
		int x2 = xend;

		if (mRCIndex < mRCCount) {
			const RegisterChange *rc0 = &mRegisterChanges[mRCIndex];
			const RegisterChange *rc = rc0;

			do {
				int xchg = rc->mPos;
				if (xchg > x1) {
					if (x2 > xchg)
						x2 = xchg;
					break;
				}

				++rc;
			} while(++mRCIndex < mRCCount);

			UpdateRegisters(rc0, (int)(rc - rc0));
		}

		if (x2 > x1) {
			if (mbSpritesActive)
				GenerateSpriteImages(x1, x2);

			Render(x1, x2);
			x1 = x2;
		}
	} while(x1 < xend);

	mLastSyncX = x1;
}

void ATGTIAEmulator::Render(int x1, int x2) {
	if (mVBlankMode == kVBlankModeOn)
		return;

	// determine displayed range
	int xc1 = x1;
	if (xc1 < 34)
		xc1 = 34;

	int xc2 = x2;
	if (xc2 > 222)
		xc2 = 222;

	if (xc2 <= xc1)
		return;

	// convert modes if necessary
	bool needHires = mbHiresMode || (mActivePRIOR & 0xC0);
	if (needHires != mbANTICHiresMode) {
		int xc1start = xc1;

		// We need to convert one clock back to support the case of a mode 8/L -> 10 transition;
		// we handle PRIOR changes one cycle later here than in the renderer, but the renderer
		// needs the converted result one half cycle (1cc) in.
		if (!mbMixedRendering) {
			mbMixedRendering = true;
			--xc1start;
		}

		if (mbANTICHiresMode)
			Convert320To160(xc1start, xc2, mMergeBuffer, mAnticData);
		else
			Convert160To320(xc1start, xc2, mAnticData, mMergeBuffer);
	}

	static const uint8 kPFTable[16]={
		0, 0, 0, 0, PF0, PF1, PF2, PF3,
		0, 0, 0, 0, PF0, PF1, PF2, PF3,
	};

	static const uint8 kPFMask[16]={
		0xF0, 0xFF, 0xFF, 0xFF,
		0xFF, 0xFF, 0xFF, 0xFF,
		0xFF, 0xFF, 0xFF, 0xFF,
		0xFF, 0xFF, 0xFF, 0xFF,
	};

	if (xc1 < xc2) {
		switch(mActivePRIOR & 0xC0) {
			case 0x00:
				break;

			case 0x80: {
				// mode 10 -> translate GTIA inputs to PF layers; we don't need
				// PM layers as only the PF layers can trigger collisions
				const uint8 *__restrict ad = &mAnticData[(xc1 - 1) & ~1];
				uint8 *__restrict dst = &mMergeBuffer[xc1];

				int w = xc2 - xc1;

				if (mDefectMode != ATGTIADefectMode::None) [[unlikely]] {
					// Emulate defect modes:
					//
					//	Type 1: first bit pair of pixel bleeds into second pair
					//	Type 2: pixel pairing flip/flop has incorrect phase if
					//		GTIA mode enabled mid-line.
					//
					if (mDefectMode == ATGTIADefectMode::Type1) {
						if (mbANTICHiresMode) {
							if (!(xc1 & 1)) {
								*dst++ = kPFTable[ad[0]*4 | ad[1]*5];
								ad += 2;
								--w;
							}

							int w2 = w >> 1;
							while(w2--) {
								dst[0] = kPFTable[ad[0]*4 + ad[1]];
								dst[1] = kPFTable[ad[0]*4 | ad[1]*5];
								ad += 2;
								dst += 2;
							}

							if (w & 1)
								*dst = kPFTable[ad[0]*4 + ad[1]];
						} else {
							if (!(xc1 & 1)) {
								*dst = kPFTable[ad[0]*4 + ad[1]] & kPFMask[dst[-1] & 15];
								ad += 2;
								++dst;
								--w;
							}

							int w2 = w >> 1;
							while(w2--) {
								const uint8 pfmask = kPFMask[dst[0] & 15];

								dst[0] = kPFTable[ad[0]*4 + ad[1]] & pfmask;
								dst[1] = kPFTable[ad[0]*4 | ad[1]*5] & pfmask;

								ad += 2;
								dst += 2;
							}

							if (w & 1)
								*dst = kPFTable[ad[0]*4 | ad[1]*5] & kPFMask[dst[0] & 15];
						}

						break;
					} else if (mDefectMode == ATGTIADefectMode::Type2 && mbDefectType2Active) {
						--ad;
					}
				}

				if (mbANTICHiresMode) {
					if (!(xc1 & 1)) {
						uint8 c = ad[0]*4 + ad[1];
						ad += 2;

						*dst++ = kPFTable[c];
						--w;
					}

					int w2 = w >> 1;
					while(w2--) {
						uint8 c = ad[0]*4 + ad[1];
						ad += 2;

						dst[0] = dst[1] = kPFTable[c];
						dst += 2;
					}

					if (w & 1) {
						uint8 c = ad[0]*4 + ad[1];
						*dst++ = kPFTable[c];
					}
				} else {
					if (!(xc1 & 1)) {
						uint8 c = ad[0]*4 + ad[1];
						ad += 2;

						*dst = kPFTable[c] & kPFMask[dst[-1] & 15];
						++dst;
						--w;
					}

					int w2 = w >> 1;
					while(w2--) {
						uint8 c = ad[0]*4 + ad[1];
						ad += 2;

						dst[0] = dst[1] = kPFTable[c] & kPFMask[dst[0] & 15];
						dst += 2;
					}

					if (w & 1) {
						uint8 c = ad[0]*4 + ad[1];
						*dst = kPFTable[c] & kPFMask[dst[0] & 15];
					}
				}
				break;
			}

			case 0x40:
			case 0xC0:
				// mode 9/11 -> clear all playfield and P/M graphics layers
				memset(mMergeBuffer + xc1, 0, (xc2 - xc1));
				break;
		}
	}

	if (mbGTIADisableTransition) {
		mbGTIADisableTransition = false;

		// The effects of the GTIA ANx latches are still in effect, which causes the low
		// two bits to be repeated.

		if (x1 >= xc1 && mMergeBuffer[x1])
			mMergeBuffer[x1] = kPFTable[4 + mAnticData[x1 - 1]];
	}

	// flush player images
	for(int i=0; i<4; ++i) {
		Sprite& sprite = mSprites[i];

		// check if we have any sprite images
		if (!sprite.mpImageHead)
			continue;

		// expire old sprite images
		for(;;) {
			if (sprite.mpImageHead->mX2 > xc1)
				break;

			SpriteImage *next = sprite.mpImageHead->mpNext;
			FreeSpriteImage(sprite.mpImageHead);
			sprite.mpImageHead = next;

			if (!next) {
				sprite.mpImageTail = NULL;
				break;
			}
		}

		// render out existing images
		for(SpriteImage *image = sprite.mpImageHead; image; image = image->mpNext) {
			if (image->mX1 >= xc2)
				break;

			if (image->mX1 < xc1) {
				image->mState.Advance((uint32)(xc1 - image->mX1));
				image->mX1 = xc1;
			}

			int minx2 = image->mX2;
			if (minx2 > xc2)
				minx2 = xc2;

			if (mbHiresMode)
				mPlayerCollFlags[i] |= image->mState.Generate(minx2 - image->mX1, P0 << i, mMergeBuffer + image->mX1, mAnticData + image->mX1);
			else
				mPlayerCollFlags[i] |= image->mState.Generate(minx2 - image->mX1, P0 << i, mMergeBuffer + image->mX1); 

			mbPMRendered = true;
		}
	}

	// Flush missile images.
	//
	// We _have_ to do this as two pass as the missiles will start overlapping with
	// the players once the scanout has started. Therefore, we do detection over all
	// missiles first before rendering any of them.

	for(int i=0; i<4; ++i) {
		Sprite& sprite = mSprites[i + 4];

		// check if we have any sprite images
		if (!sprite.mpImageHead)
			continue;

		// expire old sprite images
		for(;;) {
			if (sprite.mpImageHead->mX2 > xc1)
				break;

			SpriteImage *next = sprite.mpImageHead->mpNext;
			FreeSpriteImage(sprite.mpImageHead);
			sprite.mpImageHead = next;

			if (!next) {
				sprite.mpImageTail = NULL;
				break;
			}
		}

		// render out existing images
		for(SpriteImage *image = sprite.mpImageHead; image; image = image->mpNext) {
			if (image->mX1 >= xc2)
				break;

			if (image->mX1 < xc1) {
				image->mState.Advance((uint32)(xc1 - image->mX1));
				image->mX1 = xc1;
			}

			int minx2 = image->mX2;
			if (minx2 > xc2)
				minx2 = xc2;

			if (mbHiresMode)
				mMissileCollFlags[i] |= image->mState.Detect(minx2 - image->mX1, mMergeBuffer + image->mX1, mAnticData + image->mX1);
			else
				mMissileCollFlags[i] |= image->mState.Detect(minx2 - image->mX1, mMergeBuffer + image->mX1);
		}
	}

	for(int i=0; i<4; ++i) {
		Sprite& sprite = mSprites[i + 4];

		// render out existing images
		for(SpriteImage *image = sprite.mpImageHead; image; image = image->mpNext) {
			if (image->mX1 >= xc2)
				break;

			int minx2 = image->mX2;
			if (minx2 > xc2)
				minx2 = xc2;

			image->mState.Generate(minx2 - image->mX1, (mActivePRIOR & 0x10) ? PF3 : (P0 << i), mMergeBuffer + image->mX1); 
			mbPMRendered = true;
		}
	}
}

void ATGTIAEmulator::RenderActivityMap(const uint8 *src) {
	if (!mpFrame)
		return;

	ATFrameBuffer *fb = static_cast<ATFrameBuffer *>(&*mpFrame);
	const VDPixmap& pxdst = mFrameProperties.mbSoftPostProcess8 ? mPreArtifactFrame : fb->mBuffer;
	uint8 *dst = (uint8 *)pxdst.data;
	ptrdiff_t dstpitch = pxdst.pitch;

	if (!mFrameProperties.mbSoftPostProcess8 && fb->mbDualFieldFrame) {
		if (fb->mbLowerField)
			dst += dstpitch;

		dstpitch += dstpitch;
	}

	uint8 *dst0 = dst;

	// if PAL extended is enabled, there are 16 lines wrapped from the bottom to the top
	// of the framebuffer that we must skip and loop back to
	if (mFrameProperties.mbOverscanPALExtended)
		dst += 16 * dstpitch;

	int h = mFrameProperties.mbOverscanPALExtended ? 312 : 262;

	for(int y=0; y<h; ++y) {
		if (mFrameProperties.mbRenderRgb32) {
			uint32 *dst32 = (uint32 *)dst;

			// If the output is signed, the normal range is mapped to 64-191 instead of 0-255.
			// This means that we only want to add 64 instead of 128, but there is an additional
			// 32 we must add to make up for halving the 64 black bias.
			const uint32 bright = mFrameProperties.mbOutputExtendedRange ? 0x60606060 : 0x80808080;

			if (mFrameProperties.mbOutputHoriz2x) {
				for(int x=0; x<114; ++x) {
					uint32 add = src[x] & 1 ? bright : 0x00;

					dst32[0] = ((dst32[0] & 0xfefefefe) >> 1) + add;
					dst32[1] = ((dst32[1] & 0xfefefefe) >> 1) + add;
					dst32[2] = ((dst32[2] & 0xfefefefe) >> 1) + add;
					dst32[3] = ((dst32[3] & 0xfefefefe) >> 1) + add;
					dst32[4] = ((dst32[4] & 0xfefefefe) >> 1) + add;
					dst32[5] = ((dst32[5] & 0xfefefefe) >> 1) + add;
					dst32[6] = ((dst32[6] & 0xfefefefe) >> 1) + add;
					dst32[7] = ((dst32[7] & 0xfefefefe) >> 1) + add;
					dst32 += 8;
				}
			} else {
				for(int x=0; x<114; ++x) {
					uint32 add = src[x] & 1 ? bright : 0x00;

					dst32[0] = ((dst32[0] & 0xfefefefe) >> 1) + add;
					dst32[1] = ((dst32[1] & 0xfefefefe) >> 1) + add;
					dst32[2] = ((dst32[2] & 0xfefefefe) >> 1) + add;
					dst32[3] = ((dst32[3] & 0xfefefefe) >> 1) + add;
					dst32 += 4;
				}
			}
		} else {
			uint8 *dst2 = dst;

			for(int x=0; x<114; ++x) {
				uint8 add = src[x] & 1 ? 0x08 : 0x00;
				dst2[0] = (dst2[0] & 0xf0) + ((dst2[0] & 0xf) >> 1) + add;
				dst2[1] = (dst2[1] & 0xf0) + ((dst2[1] & 0xf) >> 1) + add;
				dst2[2] = (dst2[2] & 0xf0) + ((dst2[2] & 0xf) >> 1) + add;
				dst2[3] = (dst2[3] & 0xf0) + ((dst2[3] & 0xf) >> 1) + add;
				dst2 += 4;
			}
		}

		src += 114;
		dst += dstpitch;

		if (y == 312 - 16 - 1)
			dst = dst0;
	}
}

void ATGTIAEmulator::UpdateScreen(bool immediate, bool forceAnyScreen) {
	if (!mpFrame) {
		if (forceAnyScreen && mpLastFrame) {
			mpDisplay->SetHDREnabled(mFrameProperties.mbOutputAdvancedColor);
			mpDisplay->SetSourcePersistent(true, mpLastFrame->mPixmap);
		}

		return;
	}

	ATFrameBuffer *fb = static_cast<ATFrameBuffer *>(&*mpFrame);

	if (immediate) {
		VDPixmap pxdst = mFrameProperties.mbSoftPostProcess8 ? mPreArtifactFrame : fb->mBuffer;
		uint32 x = mpConn->GTIAGetXClock();

		Sync();

		if (mpDst) {
			if (mpVBXE)
				mpVBXE->RenderScanline(x, true);
			else
				mpRenderer->RenderScanline(x, true, mbPMRendered, mbMixedRendering);
		}

		uint32 y = mY + 1;
		uint32 ysplit = 248;

		if (mFrameProperties.mbOverscanPALExtended) {
			// What we do here is wrap the last 16 lines back up to the top of
			// the display. This isn't correct, as it causes those lines to
			// lead by a frame, but it at least solves the vertical position
			// issue.
			if (y >= 312 - 16)
				y -= 312 - 16;
			else
				y += 16;

			ysplit += 16;
		}

		if (!mFrameProperties.mbSoftPostProcess8 && fb->mbDualFieldFrame)
			pxdst = VDPixmapExtractField(pxdst, fb->mbLowerField);

		if (y < (uint32)pxdst.h) {
			uint8 *row = (uint8 *)pxdst.data + y*pxdst.pitch;

			switch(pxdst.format) {
				case nsVDPixmap::kPixFormat_XRGB8888:
					VDMemset32(row, 0x00, 4*x);
					VDMemset32(row + x*4*4, 0xFFFF00, 912 - 4*x);
					break;

				case nsVDPixmap::kPixFormat_Pal8:
					VDMemset8(row, 0x00, 2*x);
					VDMemset8(row + x*2, 0xFF, 464 - 2*x);
					break;
			}
		}

		// if we haven't copied over the frame and aren't going through the temp buffer
		// for post processing, copy over previous frame starting at the next scanline
		if (!mbFrameCopiedFromPrev && !mFrameProperties.mbSoftPostProcess8 && mpLastFrame) {
			const ATFrameBuffer& prevFrame = *mpLastFrame;
			VDPixmap pxprev = prevFrame.mBuffer;

			if (prevFrame.mbDualFieldFrame) {
				// We need to copy using the current frame's field polarity, not the
				// previous frame's polarity -- because we need to fill in the rest of
				// the field that we're in the progress of drawing.
				pxprev = VDPixmapExtractField(pxprev, fb->mbLowerField);
			}

			mbFrameCopiedFromPrev = true;

			// We split frames at 248, so we 'end' the blit there, wrapping around as needed.
			// If we're on 247, no need to copy anything as we're on the last line of the frame
			// with everything else already drawn in the current frame.
			if (y < ysplit - 1) {
				// before split -- copy from after current line to split
				VDPixmapBlt(
					pxdst, 0, y+1,
					pxprev, 0, y+1,
					pxdst.w, ysplit - (y+1)
				);
			} else if (y >= ysplit) {
				// after split -- copy from current -> end and start -> split
				VDPixmapBlt(
					pxdst, 0, 0,
					pxprev, 0, 0,
					pxdst.w, ysplit
				);

				VDPixmapBlt(
					pxdst, 0, y+1,
					pxprev, 0, y+1,
					pxdst.w, pxdst.h - (y+1)
				);
			}
		}

		ApplyArtifacting(true);
		fb->DuplicateField();

		// frame is incomplete and has some past data, so just suppress all lores optimizations
		std::fill(std::begin(fb->mbScanlineHasHires), std::end(fb->mbScanlineHasHires), true);

		mpDisplay->SetHDREnabled(mFrameProperties.mbOutputAdvancedColor);
		mpDisplay->SetSourcePersistent(true, mpFrame->mPixmap, true, mpFrame->mpScreenFX, mpFrame->mpScreenFXEngine);
	} else {
		ApplyArtifacting(false);
		fb->DuplicateField();

		mVideoTaps.NotifyAll(
			[this](IATGTIAVideoTap *p) {
				p->WriteFrame(mpFrame->mPixmap, mFrameTimestamp, mpConn->GTIAGetTimestamp64(), GetPixelAspectRatio());
			}
		);

		if (mbTurbo)
			fb->mFlags |= IVDVideoDisplay::kDoNotWait;
		else
			fb->mFlags &= ~IVDVideoDisplay::kDoNotWait;

		std::fill(std::begin(fb->mbScanlineHasHires), std::end(fb->mbScanlineHasHires), false);

		sint32 dsty1 = 0;
		sint32 dsty2 = mPreArtifactFrameVisibleY2 - mPreArtifactFrameVisibleY1;
		const sint32 vstart = mFrameProperties.mbOverscanPALExtended ? 24 : 8;
		sint32 srcy1 = (sint32)mPreArtifactFrameVisibleY1 - vstart;
		sint32 srcy2 = (sint32)mPreArtifactFrameVisibleY2 - vstart;

		if (dsty1 < 0) {
			srcy1 -= dsty1;
			dsty1 = 0;
		}

		if (srcy1 < 0) {
			dsty1 -= srcy1;
			srcy1 = 0;
		}

		if (srcy2 > 240) {
			dsty2 -= (240 - srcy2);
			srcy2 = 240;
		}

		if (dsty2 > (sint32)vdcountof(fb->mbScanlineHasHires)) {
			sint32 offset = dsty2 - (sint32)vdcountof(fb->mbScanlineHasHires);
			dsty2 = (sint32)vdcountof(fb->mbScanlineHasHires);
			srcy2 -= offset;
		}

		if (dsty2 > dsty1)
			std::copy(mbScanlinesWithHiRes + srcy1, mbScanlinesWithHiRes + srcy2, fb->mbScanlineHasHires + dsty1);

		if (!fb->mbDroppedFrame) {
			ATProfileBeginRegionWithArg(kATProfileRegion_DisplayPost, fb->mFrameNumber);

			const auto& ap = mpArtifactingEngine->GetArtifactingParams();
			mpDisplay->SetHDREnabled(mFrameProperties.mbOutputAdvancedColor);
			mpDisplay->SetSDRBrightness(!mFrameProperties.mbOutputSceneReferred ? 80.0f : ap.mbUseSystemSDR ? -1.0f : ap.mSDRIntensity);

			mpDisplay->PostBuffer(fb);

			ATProfileEndRegion(kATProfileRegion_DisplayPost);
		}

		mpLastFrame = fb;
		mbBlendModeLastFrame = mFrameProperties.mbSoftBlending;
		mbBlendModeLastFrameMonoPersistence = mFrameProperties.mbSoftBlendingMonoPersistence;

		mpFrame = NULL;
	}
}

void ATGTIAEmulator::RecomputePalette() {
	using namespace nsVDVecMath;

	mActiveColorParams = mColorSettings.mbUsePALParams && mbPALMode ? mColorSettings.mPALParams : mColorSettings.mNTSCParams;

	ATColorPaletteGenerator gen;
	gen.Generate(mActiveColorParams, mMonitorMode);

	memcpy(mPalette, gen.mPalette, sizeof mPalette);
	memcpy(mSignedPalette, gen.mSignedPalette, sizeof mSignedPalette);
	memcpy(mUncorrectedPalette, gen.mUncorrectedPalette, sizeof mUncorrectedPalette);
	memcpy(mUncorrectedSignedPalette, gen.mUncorrectedSignedPalette, sizeof mUncorrectedSignedPalette);

	const bool useMatrix = gen.mColorMatchingMatrix.has_value();
	vdfloat3x3 mx;

	mOutputGamma = gen.mOutputGamma;

	if (useMatrix) {
		mx = gen.mColorMatchingMatrix.value();

		static_assert(sizeof(mColorMatchingMatrix) == sizeof(mx));
		memcpy(mColorMatchingMatrix, &mx, sizeof mColorMatchingMatrix);
	} else {
		memset(mColorMatchingMatrix, 0, sizeof mColorMatchingMatrix);
	}

	mpArtifactingEngine->SetColorParams(mActiveColorParams, useMatrix ? &mx : nullptr, gen.mTintColor.has_value() ? &gen.mTintColor.value() : nullptr, mMonitorMode, mPALPhase);

	if (mpVBXE) {
		// For VBXE, we need to push the uncorrected palette since it has to do the correction
		// on its side in order to handle RGB values written into palette registers. We also
		// inject Y into the alpha channel for use by the artifacting engine, since this makes
		// it substantially faster to do PAL chroma blending.

		mpVBXE->SetDefaultPalette(gen.mUncorrectedPalette, mpArtifactingEngine);
	}
}

sint32 ATGTIAEmulator::ReadBackWriteRegister(uint8 reg) const {
	reg &= 0x1F;

	// filter registers that are actually write register -- only HITCLR
	// ($D01E) is unusable as it is a strobe

	if (reg != 0x1E)
		return mState.mReg[reg];

	return -1;
}

uint8 ATGTIAEmulator::ReadByte(uint8 reg) {
	reg &= 0x1F;

	// fast registers
	switch(reg) {
		case 0x10:
		case 0x11:
		case 0x12:
		case 0x13:
			if (mbSECAMMode)
				UpdateSECAMTriggerLatch(reg - 0x10);

			return (mGRACTL & 4) ? mTRIGLatched[reg - 0x10] : mTRIG[reg - 0x10];
		case 0x14:
			return mbPALMode ? 0x01 : 0x0F;
		case 0x15:
		case 0x16:
		case 0x17:	// must return LSB0 set or Recycle hangs
		case 0x18:
		case 0x19:
		case 0x1A:
		case 0x1B:
		case 0x1C:
		case 0x1D:
		case 0x1E:
			return 0x0F;
		case 0x1F:		// $D01F CONSOL
			return ReadConsoleSwitches();
	}

	Sync();	

	switch(reg) {
		// missile-to-playfield collisions
		case 0x00:	return mMissileCollFlags[0] & 15 & mCollisionMask;
		case 0x01:	return mMissileCollFlags[1] & 15 & mCollisionMask;
		case 0x02:	return mMissileCollFlags[2] & 15 & mCollisionMask;
		case 0x03:	return mMissileCollFlags[3] & 15 & mCollisionMask;

		// player-to-playfield collisions
		case 0x04:	return mPlayerCollFlags[0] & 15 & mCollisionMask;
		case 0x05:	return mPlayerCollFlags[1] & 15 & mCollisionMask;
		case 0x06:	return mPlayerCollFlags[2] & 15 & mCollisionMask;
		case 0x07:	return mPlayerCollFlags[3] & 15 & mCollisionMask;

		// missile-to-player collisions
		case 0x08:	return (mMissileCollFlags[0] & mCollisionMask) >> 4;
		case 0x09:	return (mMissileCollFlags[1] & mCollisionMask) >> 4;
		case 0x0A:	return (mMissileCollFlags[2] & mCollisionMask) >> 4;
		case 0x0B:	return (mMissileCollFlags[3] & mCollisionMask) >> 4;

		// player-to-player collisions
		case 0x0C:	return (  ((mPlayerCollFlags[1] >> 3) & 0x02)	// 1 -> 0
							+ ((mPlayerCollFlags[2] >> 2) & 0x04)	// 2 -> 0
							+ ((mPlayerCollFlags[3] >> 1) & 0x08)) & (mCollisionMask >> 4);	// 3 -> 0

		case 0x0D:	return (  ((mPlayerCollFlags[1] >> 4) & 0x01)	// 1 -> 0
							+ ((mPlayerCollFlags[2] >> 3) & 0x04)	// 2 -> 1
							+ ((mPlayerCollFlags[3] >> 2) & 0x08)) & (mCollisionMask >> 4);	// 3 -> 1

		case 0x0E:	return (  ((mPlayerCollFlags[2] >> 4) & 0x03)	// 2 -> 0, 1
							+ ((mPlayerCollFlags[3] >> 3) & 0x08)) & (mCollisionMask >> 4);	// 3 -> 2

		case 0x0F:	return (  ((mPlayerCollFlags[3] >> 4) & 0x07)) & (mCollisionMask >> 4);	// 3 -> 0, 1, 2

		default:
//			__debugbreak();
			break;
	}
	return 0;
}

void ATGTIAEmulator::WriteByte(uint8 reg, uint8 value) {
	reg &= 0x1F;

	mState.mReg[reg] = value;

	switch(reg) {
		case 0x12:
		case 0x13:
		case 0x14:
		case 0x15:
			mPMColor[reg - 0x12] = value & 0xfe;
			if (mpVBXE)
				mpVBXE->AddRegisterChange(mpConn->GTIAGetXClock() + 1, reg, value);
			else
				mpRenderer->AddRegisterChange(mpConn->GTIAGetXClock() + 1, reg, value);
			break;

		case 0x16:
		case 0x17:
		case 0x18:
		case 0x19:
			mPFColor[reg - 0x16] = value & 0xfe;
			if (mpVBXE)
				mpVBXE->AddRegisterChange(mpConn->GTIAGetXClock() + 1, reg, value);
			else
				mpRenderer->AddRegisterChange(mpConn->GTIAGetXClock() + 1, reg, value);
			break;

		case 0x1A:
			mPFBAK = value & 0xfe;
			if (mpVBXE)
				mpVBXE->AddRegisterChange(mpConn->GTIAGetXClock() + 1, reg, value);
			else
				mpRenderer->AddRegisterChange(mpConn->GTIAGetXClock() + 1, reg, value);
			break;

		case 0x1B:
			if (mbCTIAMode)
				value &= 0x3F;

			mPRIOR = value;
			break;

		case 0x1C:
			mVDELAY = value;
			return;

		case 0x1D:
			// We actually need to sync the latches when latching is *enabled*, since they
			// are always updated but only read when latching is enabled.
			if (~mGRACTL & value & 4) {
				if (mbSECAMMode) {
					for(int i=0; i<4; ++i)
						UpdateSECAMTriggerLatch(i);
				}

				mTRIGLatched[0] = mTRIG[0];
				mTRIGLatched[1] = mTRIG[1];
				mTRIGLatched[2] = mTRIG[2];
				mTRIGLatched[3] = mTRIG[3];
			}

			mGRACTL = value;
			return;

		case 0x1F:		// $D01F CONSOL
			{
				uint8 newConsol = value & 0x0F;
				uint8 delta = newConsol ^ mSwitchOutput;
				if (delta) {
					if (delta & 8)
						mpConn->GTIASetSpeaker(0 != (newConsol & 8));

					if (delta & 7)
						mpConn->GTIASelectController(newConsol & 3, (newConsol & 4) != 0);
				}
				mSwitchOutput = newConsol;
			}
			return;
	}

	const uint8 xpos = mpConn->GTIAGetXClock();

	switch(reg) {
		case 0x00:	// $D000 HPOSP0
		case 0x01:	// $D001 HPOSP1
		case 0x02:	// $D002 HPOSP2
		case 0x03:	// $D003 HPOSP3
		case 0x04:	// $D004 HPOSM0
		case 0x05:	// $D005 HPOSM1
		case 0x06:	// $D006 HPOSM2
		case 0x07:	// $D007 HPOSM3
			AddRegisterChange(xpos + 5, reg, value);
			break;
		case 0x08:	// $D008 SIZEP0
		case 0x09:	// $D009 SIZEP1
		case 0x0A:	// $D00A SIZEP2
		case 0x0B:	// $D00B SIZEP3
		case 0x0C:	// $D00C SIZEM
			AddRegisterChange(xpos + 3, reg, value);
			break;
		case 0x0D:	// $D00D GRAFP0
		case 0x0E:	// $D00E GRAFP1
		case 0x0F:	// $D00F GRAFP2
		case 0x10:	// $D010 GRAFP3
		case 0x11:	// $D011 GRAFM
			AddRegisterChange(xpos + 3, reg, value);
			break;

		case 0x1B:	// $D01B PRIOR

			// PRIOR is quite an annoying register, since several of its components
			// take effect at different stages in the color pipeline:
			//
			//	|			|			|			|			|			|
			//	|			B->	PFx	----B-> priA ---B-> priB ---B->color----B-> output
			//	|			|	decode	|	 ^		|	 ^		|  lookup	|     ^
			//	|			|			|  pri0-3	|   5th		|			|	  |
			//	|			|			|			|  player	| mode 9/11	B-----/
			//	|			|			|			|			|	enable	|
			//	|			|			|			|			|			|
			//	2			1			2			1			2			1

			AddRegisterChange(xpos + 2, reg, value);
			if (mpVBXE)
				mpVBXE->AddRegisterChange(xpos + 1, reg, value);
			else
				mpRenderer->AddRegisterChange(xpos + 1, reg, value);
			break;

		case 0x1E:	// $D01E HITCLR
			AddRegisterChange(xpos + 3, reg, value);
			break;
	}
}

ATGTIALightSensor *ATGTIAEmulator::RegisterLightSensor(int hrPxWidth, int hrPxHeight, const uint8 *weights, float lightThreshold, vdfunction<void(int)> triggerFn) {
	if (hrPxWidth <= 0 || hrPxHeight <= 0 || !weights)
		VDRaiseInternalFailure();

	vdautoptr sensor(new ATGTIALightSensorImpl);

	sensor->mTriggerFn = std::move(triggerFn);
	sensor->mpWeights = weights;
	sensor->mLightThreshold = VDRoundToInt32(lightThreshold * lightThreshold * (255.0f * 255.0f) * (float)std::accumulate(weights, weights + hrPxWidth * hrPxHeight, 0.0f));
	sensor->mAccumulatedLight = 0;

	LightSensor& ls = mLightSensors.emplace_back();
	ls.mHrPixelArea = vdrect32(0, 0, hrPxWidth, hrPxHeight);
	ls.mpSensor = sensor;

	return sensor.release();
}

void ATGTIAEmulator::SetLightSensorPosition(ATGTIALightSensor *sensor, int hrPxX, int hrPxY) {
	for(LightSensor& ls : mLightSensors) {
		if (ls.mpSensor == sensor) {
			ls.mHrPixelArea.translate(hrPxX - ls.mHrPixelArea.left, hrPxY - ls.mHrPixelArea.top);
			return;
		}
	}

	VDRaiseInternalFailure();
}

void ATGTIAEmulator::UnregisterLightSensor(ATGTIALightSensor *sensor) {
	if (!sensor)
		return;

	auto it = std::find_if(mLightSensors.begin(), mLightSensors.end(),
		[sensor](const LightSensor& ls) {
			return ls.mpSensor == sensor;
		}
	);

	if (it == mLightSensors.end())
		VDRaiseInternalFailure();

	delete it->mpSensor;

	if (&*it != &mLightSensors.back())
		*it = std::move(mLightSensors.back());

	mLightSensors.pop_back();
}

void ATGTIAEmulator::ApplyArtifacting(bool immediate) {
	if (mpVBXE) {
		const bool doBlending = mArtifactMode == ATArtifactMode::PAL || (mArtifactMode == ATArtifactMode::Auto && mFrameProperties.mbPAL) || mFrameProperties.mbSoftBlending;

		if (doBlending || mFrameProperties.mbSoftScanlines || mFrameProperties.mbSoftDeinterlace) {
			ATFrameBuffer *fb = static_cast<ATFrameBuffer *>(&*mpFrame);
			char *dstrow = (char *)fb->mBuffer.data;
			ptrdiff_t dstpitch = fb->mBuffer.pitch;
			uint32 h = fb->mBuffer.h;

			if (mFrameProperties.mbInterlaced) {
				if (fb->mbLowerField)
					dstrow += dstpitch;

				if (!mFrameProperties.mbSoftDeinterlace)
					dstpitch *= 2;

				h >>= 1;
			} else if (mFrameProperties.mbSoftScanlines)
				h >>= 1;

			for(uint32 row=0; row<h; ++row) {
				uint32 *dst = (uint32 *)dstrow;

				if (doBlending)
					mpArtifactingEngine->Artifact32(row, dst, 912, immediate, mFrameProperties.mbIncludeHBlank);

				if (mFrameProperties.mbSoftDeinterlace) {
					if (row) {
						mpArtifactingEngine->Deinterlace(
							row*2+(fb->mbLowerField ? 1 : 0),
							(uint32 *)(dstrow - dstpitch),
							(const uint32 *)(dstrow - dstpitch*2),
							dst,
							912
						);
					} else {
						mpArtifactingEngine->Deinterlace(
							row*2+(fb->mbLowerField ? 1 : 0),
							nullptr,
							nullptr,
							dst,
							912
						);
					}

					dstrow += dstpitch;
				} else if (mFrameProperties.mbSoftScanlines) {
					if (row)
						mpArtifactingEngine->InterpolateScanlines((uint32 *)(dstrow - dstpitch), (const uint32 *)(dstrow - 2*dstpitch), dst, 912);

					dstrow += dstpitch;
				}

				dstrow += dstpitch;
			}

			if (mFrameProperties.mbSoftScanlines) {
				mpArtifactingEngine->InterpolateScanlines(
					(uint32 *)(dstrow - dstpitch),
					(const uint32 *)(dstrow - 2*dstpitch),
					(const uint32 *)(dstrow - 2*dstpitch),
					912);
			}
		}

		return;
	}

	if (!mFrameProperties.mbSoftPostProcess8)
		return;

	ATFrameBuffer *fb = static_cast<ATFrameBuffer *>(&*mpFrame);
	char *dstrow = (char *)fb->mBuffer.data;
	ptrdiff_t dstpitch = fb->mBuffer.pitch;

	if (mFrameProperties.mbInterlaced) {
		if (fb->mbLowerField)
			dstrow += dstpitch;

		if (!mFrameProperties.mbSoftDeinterlace)
			dstpitch *= 2;
	}

	const uint8 *srcrow = (const uint8 *)mPreArtifactFrame.data;
	ptrdiff_t srcpitch = mPreArtifactFrame.pitch;

	uint32 y1 = mPreArtifactFrameVisibleY1;
	uint32 y2 = mPreArtifactFrameVisibleY2;

	if (y1)
		--y1;

	if (mFrameProperties.mbSoftScanlines || mFrameProperties.mbSoftDeinterlace)
		dstrow += dstpitch * 2 * y1;
	else
		dstrow += dstpitch * y1;

	srcrow += srcpitch * y1;

	// In PAL extended mode, we wrap the bottom 16 lines back up to the top, thus
	// the weird adjustment here.
	const uint32 vstart = mFrameProperties.mbOverscanPALExtended ? 24 : 8;
	const uint32 w = mFrameProperties.mbOutputHoriz2x ? 912 : 456;

	for(uint32 row=y1; row<y2; ++row) {
		uint32 *dst = (uint32 *)dstrow;
		const uint8 *src = srcrow;

		uint32 relativeRow = row - vstart;

		mpArtifactingEngine->Artifact8(row, dst, src, relativeRow < 240 && mbScanlinesWithHiRes[relativeRow], immediate, mFrameProperties.mbIncludeHBlank);

		if (mFrameProperties.mbSoftDeinterlace) {
			if (row) {
				mpArtifactingEngine->Deinterlace(
					row*2+(fb->mbLowerField ? 1 : 0),
					(uint32 *)(dstrow - dstpitch),
					(const uint32 *)(dstrow - dstpitch*2),
					dst,
					w
				);
			} else {
				mpArtifactingEngine->Deinterlace(
					row*2+(fb->mbLowerField ? 1 : 0),
					nullptr,
					nullptr,
					dst,
					w
				);
			}

			dstrow += dstpitch;
		} else if (mFrameProperties.mbSoftScanlines) {
			if (row > y1)
				mpArtifactingEngine->InterpolateScanlines((uint32 *)(dstrow - dstpitch), (const uint32 *)(dstrow - 2*dstpitch), dst, w);

			dstrow += dstpitch;
		}

		srcrow += srcpitch;
		dstrow += dstpitch;
	}

	if (mFrameProperties.mbSoftScanlines) {
		mpArtifactingEngine->InterpolateScanlines(
			(uint32 *)(dstrow - dstpitch),
			(const uint32 *)(dstrow - 2*dstpitch),
			(const uint32 *)(dstrow - 2*dstpitch),
			w);
	}
}

void ATGTIAEmulator::AddRegisterChange(uint8 pos, uint8 addr, uint8 value) {
	RegisterChanges::iterator it(mRegisterChanges.end()), itBegin(mRegisterChanges.begin() + mRCIndex);

	while(it != itBegin && it[-1].mPos > pos)
		--it;

	RegisterChange change;
	change.mPos = pos;
	change.mReg = addr;
	change.mValue = value;
	mRegisterChanges.insert(it, change);

	++mRCCount;
}

void ATGTIAEmulator::UpdateRegisters(const RegisterChange *rc, int count) {
	while(count--) {
		uint8 value = rc->mValue;

		switch(rc->mReg) {
			case 0x00:
			case 0x01:
			case 0x02:
			case 0x03:
			case 0x04:
			case 0x05:
			case 0x06:
			case 0x07:
				mSpritePos[rc->mReg] = value;
				break;

			case 0x08:
			case 0x09:
			case 0x0A:
			case 0x0B:
				{
					Sprite& sprite = mSprites[rc->mReg & 3];
					uint8 newSize = value & 3;

					// If VBXE is enabled, block size mode 2 -- it is equivalent to 0 and doesn't
					// trigger the lockup state.
					if (mpVBXE && newSize == 2)
						newSize = 0;

					if (sprite.mState.mSizeMode != newSize) {
						// catch sprite state up to this point
						sprite.Sync(rc->mPos);

						// change sprite mode
						sprite.mState.mSizeMode = newSize;

						// generate update image
						GenerateSpriteImage(sprite, rc->mPos);
					}
				}
				break;

			case 0x0C:
				// If VBXE is enabled, block size mode 2 -- it is equivalent to 0 and doesn't
				// trigger the lockup state.
				if (mpVBXE) {
					const uint8 mode2Mask = value & (~value << 1);

					value -= mode2Mask & 0xAA;
				}

				for(int i=0; i<4; ++i) {
					Sprite& sprite = mSprites[i+4];

					const uint8 newSize = (value >> (2*i)) & 3;

					if (sprite.mState.mSizeMode != newSize) {
						// catch sprite state up to this point
						sprite.Sync(rc->mPos);

						// switch size mode
						sprite.mState.mSizeMode = newSize;

						// generate update image
						GenerateSpriteImage(sprite, rc->mPos);
					}
				}
				break;
			case 0x0D:
			case 0x0E:
			case 0x0F:
			case 0x10:
				mSprites[rc->mReg - 0x0D].mState.mDataLatch = value;
				if (value)
					mbSpritesActive = true;
				break;
			case 0x11:
				mSprites[4].mState.mDataLatch = (value << 6) & 0xc0;
				mSprites[5].mState.mDataLatch = (value << 4) & 0xc0;
				mSprites[6].mState.mDataLatch = (value << 2) & 0xc0;
				mSprites[7].mState.mDataLatch = (value     ) & 0xc0;
				if (value)
					mbSpritesActive = true;
				break;

			case 0x1B:
				if (!(value & 0xc0) && (mActivePRIOR & 0xc0)) {
					mbGTIADisableTransition = true;

					if (rc->mPos >= 34 && rc->mPos < 222)
						mbDefectType2Active = true;
				}

				mActivePRIOR = value;

				if (value & 0xC0)
					mbHiresMode = false;

				break;
			case 0x1E:		// $D01E HITCLR
				memset(mPlayerCollFlags, 0, sizeof mPlayerCollFlags);
				memset(mMissileCollFlags, 0, sizeof mMissileCollFlags);
				break;

			case 0x20:		// missile DMA
				{
					uint8 mask = 0x0F;

					// We get called after ANTIC has bumped the scanline but before GTIA knows about it,
					// so mY is actually one off.
					if (mY & 1)
						mask = ~mVDELAY;

					if (mask & 0x01)
						mSprites[4].mState.mDataLatch = (value << 6) & 0xc0;

					if (mask & 0x02)
						mSprites[5].mState.mDataLatch = (value << 4) & 0xc0;

					if (mask & 0x04)
						mSprites[6].mState.mDataLatch = (value << 2) & 0xc0;

					if (mask & 0x08)
						mSprites[7].mState.mDataLatch = (value     ) & 0xc0;

					if (value)
						mbSpritesActive = true;
				}
				break;
		}

		++rc;
	}
}

void ATGTIAEmulator::UpdateSECAMTriggerLatch(int index) {
	// The 107 is a guess. The triggers start shifting into FGTIA on the
	// third cycle of horizontal blank according to the FGTIA doc.
	uint32 t = mpConn->GTIAGetLineEdgeTimingId(107 + index);

	if (mTRIGSECAMLastUpdate[index] != t) {
		mTRIGSECAMLastUpdate[index] = t;

		const uint8 v = mTRIGSECAM[index];
		mTRIG[index] = v;
		mTRIGLatched[index] &= v;
	}
}

void ATGTIAEmulator::ResetSprites() {
	for(int i=0; i<8; ++i) {
		Sprite& sprite = mSprites[i];

		sprite.mLastSync = 0;
		sprite.mState.Reset();

		SpriteImage *p = sprite.mpImageHead;
		while(p) {
			SpriteImage *next = p->mpNext;

			FreeSpriteImage(p);

			p = next;
		}

		sprite.mpImageHead = NULL;
		sprite.mpImageTail = NULL;
	}

	mbSpritesActive = false;
}

void ATGTIAEmulator::GenerateSpriteImages(int x1, int x2) {
	unsigned xr = (unsigned)(x2 - x1);

	// Trigger new sprite images
	bool foundActiveSprite = false;

	for(int i=0; i<8; ++i) {
		Sprite& sprite = mSprites[i];

		// Check if there is any latched or shifting data -- if not, we do not care because:
		//
		// - the only impact of the shifter state is its output for priority and collision
		// - shifting in any non-zero data would reset the state to 0
		//
		// Note that we still need to do this if data is still available to shift out as
		// we can re-capture that in a new image. Also, we are doing this on the last synced
		// state of the shift hardware instead of the state at the time of the trigger, but
		// we will check again below.

		if (!(sprite.mState.mDataLatch | sprite.mState.mShiftRegister))
			continue;

		foundActiveSprite = true;

		int pos = mSpritePos[i];

		if ((unsigned)(pos - x1) < xr) {
			// catch sprite state up to this point
			sprite.Sync(pos);

			// latch in new image
			if (sprite.mState.mShiftState) {
				sprite.mState.mShiftState = 0;
				sprite.mState.mShiftRegister += sprite.mState.mShiftRegister;
			}

			sprite.mState.mShiftRegister |= sprite.mState.mDataLatch;

			// generate new sprite image
			GenerateSpriteImage(sprite, pos);
		}
	}

	if (!foundActiveSprite)
		mbSpritesActive = false;
}

void ATGTIAEmulator::GenerateSpriteImage(Sprite& sprite, int pos) {
	// if we have a previous image, truncate it
	if (sprite.mpImageTail && sprite.mpImageTail->mX2 > pos)
		sprite.mpImageTail->mX2 = pos;

	// skip all zero images
	if (sprite.mState.mShiftRegister) {
		// compute sprite width
		static const int kWidthLookup[4] = {8,16,8,32};
		int width = kWidthLookup[sprite.mState.mSizeMode];

		// check for special case lockup
		if (sprite.mState.mSizeMode == 2 && (sprite.mState.mShiftState == 1 || sprite.mState.mShiftState == 2))
			width = 1024;

		// record image
		SpriteImage *image = AllocSpriteImage();
		image->mX1 = pos;
		image->mX2 = pos + width;
		image->mState = sprite.mState;
		image->mpNext = NULL;

		if (sprite.mpImageTail)
			sprite.mpImageTail->mpNext = image;
		else
			sprite.mpImageHead = image;

		sprite.mpImageTail = image;
	}
}

void ATGTIAEmulator::FreeSpriteImage(SpriteImage *p) {
	p->mpNext = mpFreeSpriteImages;
	mpFreeSpriteImages = p;
}

ATGTIAEmulator::SpriteImage *ATGTIAEmulator::AllocSpriteImage() {
	if (!mpFreeSpriteImages)
		return mNodeAllocator.Allocate<SpriteImage>();

	SpriteImage *p = mpFreeSpriteImages;
	mpFreeSpriteImages = p->mpNext;

	return p;
}

ATGTIAEmulator::VerticalOverscanMode ATGTIAEmulator::DeriveVerticalOverscanMode() const {
	if (mVerticalOverscanMode != kVerticalOverscan_Default)
		return mVerticalOverscanMode;

	switch(mOverscanMode) {
		case kOverscanFull:
			return kVerticalOverscan_Full;

		case kOverscanExtended:
			return kVerticalOverscan_Extended;

		default:
		case kOverscanNormal:
		case kOverscanWidescreen:
			return kVerticalOverscan_Normal;

		case kOverscanOSScreen:
			return kVerticalOverscan_OSScreen;
	}
}

void ATGTIAEmulator::SetFrameProperties() {
	auto& fp = mFrameProperties;

	// Establish baseline feature enables and availability.
	const auto& ap = mpArtifactingEngine->GetArtifactingParams();
	const bool canAccelFX = mpDisplay->IsScreenFXPreferred();
	const bool canDisplayHdr = CheckHighColorAvailability(true) == HighColorAvailability::Available;
	const bool canDisplayWcg = CheckHighColorAvailability(false) == HighColorAvailability::Available;
	const bool canAccelXColor = (canDisplayHdr || canDisplayWcg) && ap.mbEnableHDR;

	// Check if output correction is enabled.
	const ATColorParams& params = mActiveColorParams;

	const bool monoTintEnabled = mMonitorMode != ATMonitorMode::Color && mMonitorMode != ATMonitorMode::Peritel;
	const bool outputCorrectionEnabled = (params.mGammaCorrect != 1.0f) || (params.mColorMatchingMode != ATColorMatchingMode::None && !monoTintEnabled);
	const bool distortionEnabled = ap.mDistortionViewAngleX > 0;
	const bool bloomEnabled = ap.mbEnableBloom;

	fp.mbPAL= mbPALMode;
	fp.mbOverscanPALExtended = fp.mbPAL && mbOverscanPALExtended;
	fp.mbInterlaced = mbInterlaceEnabled;

	// deinterlace is only effective when interlace is enabled
	fp.mbSoftDeinterlace = mbInterlaceEnabled && mDeinterlaceMode != ATVideoDeinterlaceMode::None;

	// deinterlace has priority over blending
	fp.mbSoftBlending = mbBlendMode && !fp.mbSoftDeinterlace;
	fp.mbSoftBlendingMonoPersistence = fp.mbSoftBlending && mbBlendMonoPersistence && monoTintEnabled;

	// Compute effective artifacting mode.
	//
	// - Auto is converted to video standard specific type.
	// - If VBXE is active, high artifacting is disabled.
	//
	fp.mArtifactMode = mArtifactMode;

	switch(fp.mArtifactMode) {
		case ATArtifactMode::Auto:
			fp.mArtifactMode = mbPALMode ? ATArtifactMode::PAL : ATArtifactMode::NTSC;
			break;

		case ATArtifactMode::AutoHi:
			fp.mArtifactMode = mbPALMode ? ATArtifactMode::PALHi : ATArtifactMode::NTSCHi;
			break;

		default:
			break;
	}

	// VBXE only supports PAL standard artifacting
	if (mpVBXE) {
		switch(fp.mArtifactMode) {
			case ATArtifactMode::PALHi:
				fp.mArtifactMode = ATArtifactMode::PAL;
				break;

			case ATArtifactMode::NTSC:
			case ATArtifactMode::NTSCHi:
				fp.mArtifactMode = ATArtifactMode::None;
				break;

			default:
				break;
		}
	}

	// Try to use hardware accelerated screen effects except when:
	//
	// - The current display driver doesn't support them.
	// - We have a video recording tap active.
	// - We have a raw frame callback active.
	//
	const bool preferSoftFX = !mbAccelScreenFX || !canAccelFX || !mVideoTaps.IsEmpty() || !mRawFrameCallbacks.IsEmpty();

	// Horizontal resolution is doubled (640ish) if VBXE or high artifacting is enabled.
	const bool outputHoriz2x = (mpVBXE != NULL) || fp.mArtifactMode == ATArtifactMode::NTSCHi || fp.mArtifactMode == ATArtifactMode::PALHi;
	fp.mbOutputHoriz2x = outputHoriz2x;

	bool useArtifacting = fp.mArtifactMode != ATArtifactMode::None;
	bool usePalArtifacting = fp.mArtifactMode == ATArtifactMode::PAL || fp.mArtifactMode == ATArtifactMode::PALHi;
	bool useHighArtifacting = fp.mArtifactMode == ATArtifactMode::NTSCHi || fp.mArtifactMode == ATArtifactMode::PALHi;

	// Compute whether we should use accelerated PAL blending:
	// - We are not preferring soft FX
	// - Standard PAL artifacting is enabled (not PAL high)
	// - We are not doing frame blending in linear mode, which cannot apply to signed encoding used by the accel FX
	bool useSoftArtifacting = useArtifacting;
	bool useAccelPALBlending = false;

	if (!preferSoftFX && fp.mArtifactMode == ATArtifactMode::PAL && !(fp.mbSoftBlending && mbBlendLinear)) {
		useAccelPALBlending = true;
		useSoftArtifacting = false;
	}

	fp.mbAccelPalArtifacting = useAccelPALBlending;

	// Soft scanlines only support noninterlaced operation. Accel scanlines support both.
	fp.mbSoftScanlines = preferSoftFX && mbScanlinesEnabled && !mbInterlaceEnabled;
	fp.mbAccelScanlines = !preferSoftFX && mbScanlinesEnabled;

	// Screen mask support
	const bool useAccelScreenMask = !preferSoftFX && canAccelFX && mpImpl->mScreenMaskParams.mType != VDDScreenMaskType::None;
	fp.mbAccelScreenMask = useAccelScreenMask;

	// Output is RGB32 instead of P8 when VBXE, artifacting, deinterlacing, frame blending, or soft scanlines are active.
	const bool rgb32 = mpVBXE || useSoftArtifacting || fp.mbSoftBlending || fp.mbSoftScanlines || fp.mbSoftDeinterlace;
	fp.mbOutputRgb32 = rgb32;

	fp.mbRenderRgb32 = mpVBXE != nullptr;
	fp.mbRenderHoriz2x = mpVBXE != nullptr;

	fp.mbIncludeHBlank = mOverscanMode == kOverscanFull || mAnalysisMode || mbForcedBorder;
		
	// Check if we have any hardware accelerated screen FX to enable:
	// - We are not preferring soft FX, and one of these is enabled:
	//   - Accelerated scanlines
	//   - RGB32 output correction (it is palette-folded for P8 or VBXE)
	//   - Accelerated PAL blending
	// - We can do accelerated FX, and one of these hardware-required FX is enabled:
	//   - Distortion
	//   - Bloom
	//
	// For color correction, palette > accel > soft. However, if output mask is
	// enabled, we must use accel output correction since the mask is applied
	// in the shader and must occur in source linear space.
	const bool useAccelDistortion = canAccelFX && distortionEnabled;
	const bool useAccelBloom = canAccelFX && bloomEnabled;
	fp.mbAccelOutputCorrection = !preferSoftFX && ((rgb32 && outputCorrectionEnabled && !mpVBXE) || useAccelScreenMask) && !(fp.mbSoftBlending && mbBlendLinear);
	fp.mbAccelPostProcess = fp.mbAccelScanlines || fp.mbAccelOutputCorrection || useAccelPALBlending || useAccelDistortion || useAccelBloom || useAccelScreenMask || canAccelXColor;

	fp.mbSoftOutputCorrection = outputCorrectionEnabled && !fp.mbAccelOutputCorrection && rgb32 && !mpVBXE;

	fp.mbPaletteOutputCorrection = outputCorrectionEnabled && !fp.mbSoftOutputCorrection && !fp.mbAccelOutputCorrection;

	// frame blending is currently only supported by the software postfx engine
	fp.mbSoftPostProcess = useSoftArtifacting || fp.mbSoftBlending || fp.mbSoftScanlines || fp.mbSoftOutputCorrection || fp.mbSoftDeinterlace;

	fp.mbSoftPostProcess8 = fp.mbSoftPostProcess && !fp.mbRenderRgb32;
	fp.mbOutputExtendedRange = fp.mbAccelPalArtifacting || canAccelXColor;
	fp.mbOutputAdvancedColor = canAccelXColor;
	fp.mbOutputSceneReferred = canAccelXColor && canDisplayHdr;

	if (fp.mbSoftPostProcess) {
		mpArtifactingEngine->BeginFrame(
			usePalArtifacting,
			useSoftArtifacting,
			useHighArtifacting,
			mbBlendModeLastFrame && mbBlendModeLastFrameMonoPersistence == fp.mbSoftBlendingMonoPersistence,
			fp.mbSoftBlending,
			mbBlendLinear,
			fp.mbSoftBlendingMonoPersistence,
			fp.mbAccelOutputCorrection || mpVBXE,
			fp.mbOutputExtendedRange,
			fp.mbOutputExtendedRange,
			fp.mbSoftDeinterlace
		);
	}
}

void ATGTIAEmulator::ResetLightSensors() {
	for(LightSensor& ls : mLightSensors) {
		ls.mpSensor->mAccumulatedLight = 0;
	}
}

void ATGTIAEmulator::ProcessLightSensors() {
	sint32 y = (sint32)mY;
	for(LightSensor& ls : mLightSensors) {
		if (y < ls.mHrPixelArea.top || y >= ls.mHrPixelArea.bottom)
			continue;

		ATGTIALightSensorImpl& sensorImpl = *ls.mpSensor;
		if (sensorImpl.mAccumulatedLight >= sensorImpl.mLightThreshold)
			continue;

		const uint8 *weights = sensorImpl.mpWeights + ls.mHrPixelArea.width() * (mY - ls.mHrPixelArea.top);
		uint32 sum = 0;
		if (mpVBXE)
			sum = mpVBXE->SenseScanline(ls.mHrPixelArea.left, ls.mHrPixelArea.right, weights);
		else
			sum = mpRenderer->SenseScanline(ls.mHrPixelArea.left, ls.mHrPixelArea.right, weights);

		sensorImpl.mAccumulatedLight += sum;
		if (sensorImpl.mAccumulatedLight >= sensorImpl.mLightThreshold) {
			if (sensorImpl.mTriggerFn)
				sensorImpl.mTriggerFn(y);
		}
	}
}
