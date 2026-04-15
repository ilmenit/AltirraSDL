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
//	You should have received a copy of the GNU General Public License along
//	with this program. If not, see <http://www.gnu.org/licenses/>.

#ifndef AT_GTIA_H
#define AT_GTIA_H

#include <vd2/system/function.h>
#include <vd2/system/linearalloc.h>
#include <vd2/system/refcount.h>
#include <vd2/system/vdstl.h>
#include <vd2/system/vectors.h>
#include <vd2/Kasumi/pixmap.h>
#include <at/atcore/enumparse.h>
#include <at/atcore/notifylist.h>
#include "gtiatypes.h"

class IVDVideoDisplay;
class VDVideoDisplayFrame;
class IATUIRenderer;
class VDPixmapBuffer;
struct VDDScreenMaskParams;
class ATConsoleOutput;

class IATGTIAEmulatorConnections {
public:
	virtual uint32 GTIAGetXClock() = 0;
	virtual uint32 GTIAGetTimestamp() const = 0;
	virtual uint64 GTIAGetTimestamp64() const = 0;
	virtual void GTIASetSpeaker(bool state) = 0;
	virtual void GTIASelectController(uint8 index, bool potsEnabled) = 0;
	virtual void GTIARequestAnticSync(int offset) = 0;
	virtual uint32 GTIAGetLineEdgeTimingId(uint32 offset) const = 0;
};

class IATGTIAVideoTap {
public:
	virtual void WriteFrame(const VDPixmap& px, uint64 timestampStart, uint64 timestampEnd, float par) = 0;
};

using ATGTIARawFrameFn = vdfunction<void(const VDPixmap& px)>;

class ATFrameBuffer;
class ATFrameTracker;
class ATArtifactingEngine;
class ATSaveStateReader;
class IATObjectState;
class ATGTIARenderer;
class ATVBXEEmulator;

enum ATLumaRampMode : uint8 {
	kATLumaRampMode_Linear,
	kATLumaRampMode_XL,
	kATLumaRampModeCount
};

AT_DECLARE_ENUM_TABLE(ATLumaRampMode);

enum class ATColorMatchingMode : uint8 {
	None,
	SRGB,
	AdobeRGB,
	Gamma22,
	Gamma24,
};

AT_DECLARE_ENUM_TABLE(ATColorMatchingMode);

struct ATColorParams {
	float mHueStart;			// I-Q plane angle of hue 1
	float mHueRange;			// I-Q plane cumulative angle for 15 hue steps (disregarding PAL uneven steps)
	float mBrightness;			// Luma 0 output level
	float mContrast;			// Luma 0->15 output range
	float mSaturation;
	float mGammaCorrect;
	float mIntensityScale;
	float mArtifactHue;	
	float mArtifactSat;
	float mArtifactSharpness;
	float mRedShift;
	float mRedScale;
	float mGrnShift;
	float mGrnScale;
	float mBluShift;
	float mBluScale;
	bool mbUsePALQuirks;
	ATLumaRampMode mLumaRampMode;
	ATColorMatchingMode mColorMatchingMode;

	bool IsSimilar(const ATColorParams& params) const;
};

struct ATNamedColorParams : public ATColorParams {
	VDStringA mPresetTag;
};

struct ATColorSettings {
	ATNamedColorParams	mNTSCParams;
	ATNamedColorParams	mPALParams;
	bool	mbUsePALParams;
};

struct ATArtifactingParams {
	// Intensity ratio of darkest point between scanlines to the brightest portion of
	// scanlines, in gamma space.
	float mScanlineIntensity;

	// Horizontal view angle in degrees (0-179).
	float mDistortionViewAngleX;

	// Ratio of vertical distortion to horizontal distortion.
	float mDistortionYRatio;

	// Vignette (radial edge darkening).  Intensity 0..1 darkens the
	// corners smoothly; 0 is disabled regardless of mbEnableVignette.
	bool mbEnableVignette;
	float mVignetteIntensity;

	bool mbEnableBloom;
	bool mbBloomScanlineCompensation;
	float mBloomRadius;
	float mBloomDirectIntensity;
	float mBloomIndirectIntensity;

	bool mbEnableHDR;
	float mSDRIntensity;
	float mHDRIntensity;
	bool mbUseSystemSDR;
	bool mbUseSystemSDRAsHDR;

	static ATArtifactingParams GetDefault();
};

enum class ATArtifactMode : uint8 {
	None,
	NTSC,
	PAL,
	NTSCHi,
	PALHi,
	Auto,
	AutoHi,
	Count
};

enum class ATMonitorMode : uint8 {
	Color,
	Peritel,
	MonoGreen,
	MonoAmber,
	MonoBluishWhite,
	MonoWhite,
	Count
};

AT_DECLARE_ENUM_TABLE(ATMonitorMode);

enum class ATVideoFieldPolarity : uint8 {
	Unknown,
	Upper,
	Lower
};

enum class ATVideoDeinterlaceMode : uint8 {
	None,
	AdaptiveBob,
};

AT_DECLARE_ENUM_TABLE(ATVideoDeinterlaceMode);

struct ATVideoFrameProperties {
	ATArtifactMode mArtifactMode;

	bool mbPAL;
	bool mbOverscanPALExtended;
	bool mbInterlaced;
	bool mbIncludeHBlank;

	// True if rendering is at twice ANTIC F resolution / 640x / 14MHz dot clock. This is true for
	// VBXE, but not for high artifacting since in that case the 2x expansion is after rendering.
	bool mbRenderHoriz2x;

	// True if GTIA rendering is occurring to 32-bit RGB instead of 8-bit indexed. This is true when
	// VBXE is active. It is NOT true when VBXE is off and soft postprocessing is enabled -- in that
	// case, rendering is to P8 and the post-fx engine then transforms that to RGB32.
	bool mbRenderRgb32;

	// True if the output is at twice ANTIC F resolution / 640x / 14MHz dot clock. This is true for
	// VBXE and when high artifacting is active.
	bool mbOutputHoriz2x;

	// True if the output is 32-bit RGB instead of Pal8.
	bool mbOutputRgb32;

	// True if the output is using a signed output where 0-1 maps to 64-191 instead of 0-255. This
	// provides additional headroom for PAL artifacting and for extended color in HDR mode. In 8-bit
	// mode, this is in the palette, and in 32-bit mode, in the framebuffer.
	bool mbOutputExtendedRange;

	// True if the output is intended for WCG/HDR scenarios.
	bool mbOutputAdvancedColor;

	// True if the output is intended for HDR scenarios and should render scene referred instead of
	// display referred.
	bool mbOutputSceneReferred;

	// Color correction is being performed through the palette.
	bool mbPaletteOutputCorrection;

	// The software postprocessing engine is active.
	bool mbSoftPostProcess;

	// The software postprocessing engine is active in 8-bit mode, and so the pre-artifact framebuffer
	// is in use. For 32-bit RGB (false), the framebuffer is used directly.
	bool mbSoftPostProcess8;

	bool mbSoftBlending;
	bool mbSoftBlendingMonoPersistence;
	bool mbSoftScanlines;
	bool mbSoftOutputCorrection;
	bool mbSoftDeinterlace;

	bool mbAccelPostProcess;
	bool mbAccelScanlines;
	bool mbAccelPalArtifacting;
	bool mbAccelOutputCorrection;
	bool mbAccelScreenMask;
};

uint32 ATGetColorPresetCount();
const char *ATGetColorPresetTagByIndex(uint32 i);
sint32 ATGetColorPresetIndexByTag(const char *tags);
const wchar_t *ATGetColorPresetNameByIndex(uint32 i);
ATColorParams ATGetColorPresetByIndex(uint32 i);

struct ATGTIARegisterState {
	uint8	mReg[0x20];
};

struct ATGTIAColorTrace {
	uint8	mColors[240][9];
};

class ATGTIALightSensor {
protected:
	~ATGTIALightSensor() = default;
};

class ATGTIAEmulator final {
	ATGTIAEmulator(const ATGTIAEmulator&);
	ATGTIAEmulator& operator=(const ATGTIAEmulator&);
public:
	ATGTIAEmulator();
	~ATGTIAEmulator();

	void Init(IATGTIAEmulatorConnections *);
	void ColdReset();

	ATVBXEEmulator *GetVBXE() const { return mpVBXE; }
	void SetVBXE(ATVBXEEmulator *);
	void SetUIRenderer(IATUIRenderer *);
	
	enum AnalysisMode {
		kAnalyzeNone,
		kAnalyzeLayers,
		kAnalyzeColors,
		kAnalyzeDList,
		kAnalyzeCount
	};

	enum OverscanMode {
		kOverscanNormal,		// 168cc
		kOverscanExtended,		// 192cc
		kOverscanFull,			// 228cc
		kOverscanOSScreen,		// 160cc
		kOverscanWidescreen,	// 176cc
		kOverscanCount
	};
	
	enum VerticalOverscanMode {
		kVerticalOverscan_Default,
		kVerticalOverscan_OSScreen,		// 192 lines
		kVerticalOverscan_Normal,		// 224 lines
		kVerticalOverscan_Extended,		// 240 lines
		kVerticalOverscan_Full,
		kVerticalOverscanCount
	};

	ATColorSettings GetDefaultColorSettings() const;
	ATColorSettings GetColorSettings() const;
	void SetColorSettings(const ATColorSettings& settings);

	ATArtifactingParams GetArtifactingParams() const;
	void SetArtifactingParams(const ATArtifactingParams& params);

	void ResetColors();
	void GetPalette(uint32 pal[256]) const;
	void GetNTSCArtifactColors(uint32 c[2]) const;

	const ATGTIAColorTrace& GetColorTrace() const {
		return mColorTrace;
	}

	bool IsFrameInProgress() const { return mpFrame != NULL; }
	bool AreAcceleratedEffectsAvailable() const;

	enum class AccelFXAvailability : uint8 {
		Available,
		NotEnabled,
		NoSupport
	};

	AccelFXAvailability GetAcceleratedEffectsAvailability() const;

	enum class HighColorAvailability : uint8 {
		// These must be sorted in ascending capability.
		NoMinidriverSupport,
		NoSystemSupport,
		NoHardwareSupport,
		NotEnabledOnDisplay,
		NoDisplaySupport,
		AccelNotEnabled,
		Available,
	};

	HighColorAvailability CheckHighColorAvailability(bool hdr) const;

	bool IsVsyncEnabled() const { return mbVsyncEnabled; }
	void SetVsyncEnabled(bool enabled) { mbVsyncEnabled = enabled; }

	void SetVsyncAdaptiveEnabled(bool enabled) { mbVsyncAdaptiveEnabled = enabled; }

	AnalysisMode GetAnalysisMode() const { return mAnalysisMode; }
	void SetAnalysisMode(AnalysisMode mode);

	OverscanMode GetOverscanMode() const { return mOverscanMode; }
	void SetOverscanMode(OverscanMode mode);

	VerticalOverscanMode GetVerticalOverscanMode() const { return mVerticalOverscanMode; }
	void SetVerticalOverscanMode(VerticalOverscanMode mode);

	bool IsOverscanPALExtended() const { return mbOverscanPALExtended; }
	void SetOverscanPALExtended(bool extended);

	ATMonitorMode GetMonitorMode() const { return mMonitorMode; }
	void SetMonitorMode(ATMonitorMode mode);

	// Return the scan area covered by the raw frame, in ANTIC/GTIA horizontal and vertical
	// positions. Horizontal units are color clocks and vertical units are non-interlaced
	// scanlines.
	vdrect32 GetFrameScanArea() const;

	// Return the pixel size of the raw image. This can be heavily distorted when high
	// artifacting or interlacing/scanlines are enabled; the pixel aspect multiple restores
	// gross aspect to nearest integral factor and using the PAR gives the fully accurate
	// aspect.
	void GetRawFrameFormat(int& w, int& h, bool& rgb32) const;

	// Return the frame size to display the raw frame image at, taking integer multipliers
	// into account. This is the raw frame size times the transpose of the pixel aspect
	// multiple. Essentially, if one axis is doubled due to higher resolution rendering,
	// the other axis is doubled in the frame size to compensate.
	void GetFrameSize(int& w, int& h) const;

	// Size of a hires pixel in the raw image. 1x1 for base display modes, 2x1 if
	// high artifacting is enabled, and 1x2 if interlace or scanlines. y/x is
	// usually a close approximation to the true PAR.
	void GetPixelAspectMultiple(int& x, int& y) const;

	// Pixel aspect ratio (PAR) of each pixel in the raw image as width/height, where
	// PAR > 1 represents a pixel wider than square. This is the actual PAR including true
	// NTSC/PAL aspect ratio adjustments, and includes the pixel multipliers returned by
	// GetPixelAspectMultiple(). Note that this is relative to the raw image size and not
	// the frame size.
	double GetPixelAspectRatio() const;

	void SetForcedBorder(bool forcedBorder) { mbForcedBorder = forcedBorder; }
	void SetFrameSkip(bool turbo) { mbTurbo = turbo; }

	bool ArePMCollisionsEnabled() const;
	void SetPMCollisionsEnabled(bool enable);

	bool ArePFCollisionsEnabled() const;
	void SetPFCollisionsEnabled(bool enable);

	IVDVideoDisplay *GetVideoOutput() const;
	void SetVideoOutput(IVDVideoDisplay *pDisplay);

	bool IsCTIAMode() const { return mbCTIAMode; }
	void SetCTIAMode(bool enabled);

	ATGTIADefectMode GetDefectMode() const { return mDefectMode; }
	void SetDefectMode(ATGTIADefectMode defectMode);

	// Query/set whether the video mode is 50Hz or 60Hz. Note that this is NOT the same
	// as PAL/NTSC, as 50Hz can be NTSC50 and 60Hz can be PAL60.
	bool Is50HzMode() const { return mb50HzMode; }
	void Set50HzMode(bool enabled);

	// Query/set whether the video standard is PAL. This does not necessarily imply 50Hz
	// as it can be PAL60.
	bool IsPALMode() const { return mbPALMode; }
	void SetPALMode(bool enabled);

	int GetPALPhase() const { return mPALPhase; }
	void SetPALPhase(int phase);

	// Query/set whether the video standard is SECAM.
	bool IsSECAMMode() const { return mbSECAMMode; }
	void SetSECAMMode(bool enabled);

	ATArtifactMode GetArtifactingMode() const { return mArtifactMode; }
	void SetArtifactingMode(ATArtifactMode mode) { mArtifactMode = mode; }

	bool IsBlendModeEnabled() const { return mbBlendMode; }
	void SetBlendModeEnabled(bool enable) { mbBlendMode = enable; }

	bool IsBlendMonoPersistenceEnabled() const { return mbBlendMonoPersistence; }
	void SetBlendMonoPersistenceEnabled(bool enable) { mbBlendMonoPersistence = enable; }

	bool IsLinearBlendEnabled() const { return mbBlendLinear; }
	void SetLinearBlendEnabled(bool enable) { mbBlendLinear = enable; }

	bool IsInterlaceEnabled() const { return mbInterlaceEnabled; }
	void SetInterlaceEnabled(bool enable) { mbInterlaceEnabled = enable; }

	ATVideoDeinterlaceMode GetDeinterlaceMode() const { return mDeinterlaceMode; }
	void SetDeinterlaceMode(ATVideoDeinterlaceMode mode) { mDeinterlaceMode = mode; }

	bool AreScanlinesEnabled() const { return mbScanlinesEnabled; }
	void SetScanlinesEnabled(bool enable) { mbScanlinesEnabled = enable; }

	bool GetAccelScreenFXEnabled() const { return mbAccelScreenFX; }
	void SetAccelScreenFXEnabled(bool enabled) { mbAccelScreenFX = enabled; }

	static VDDScreenMaskParams GetDefaultScreenMaskParams();
	VDDScreenMaskParams GetScreenMaskParams() const;
	void SetScreenMaskParams(const VDDScreenMaskParams& params);

	void SetConsoleSwitch(uint8 c, bool down);
	uint8 ReadConsoleSwitchInputs() const;
	uint8 ReadConsoleSwitches() const;

	uint8 GetForcedConsoleSwitches() const { return mForcedSwitchInput; }
	void SetForcedConsoleSwitches(uint8 c);
	 
	void SetControllerTrigger(int index, bool state) {
		uint8 v = state ? 0x00 : 0x01;

		if (mbSECAMMode) {
			UpdateSECAMTriggerLatch(index);
			mTRIGSECAM[index] = v;
		} else {
			mTRIG[index] = v;
			mTRIGLatched[index] &= v;
		}
	}

	void AddVideoTap(IATGTIAVideoTap *vtap);
	void RemoveVideoTap(IATGTIAVideoTap *vtap);

	void AddRawFrameCallback(const ATGTIARawFrameFn *fn);
	void RemoveRawFrameCallback(const ATGTIARawFrameFn *fn);

	bool IsLastFrameBufferAvailable() const;
	bool GetLastFrameBuffer(VDPixmapBuffer& pxbuf, VDPixmap& px) const;
	bool GetLastFrameBufferRaw(VDPixmapBuffer& pxbuf, VDPixmap& px, float& par) const;

	uint32 GetBackgroundColor24() const { return mPalette[mPFBAK]; }
	uint32 GetPlayfieldColor24(int index) const { return mPalette[mPFColor[index]]; }
	uint32 GetPlayfieldColorPF2H() const { return mPalette[(mPFColor[2] & 0xf0) + (mPFColor[1] & 0x0f)]; }

	bool IsPhantomDMAPossible() const {
		return (mGRACTL & 3) != 0;
	}

	void DumpStatus();

	void BeginLoadState(ATSaveStateReader& reader);
	void LoadStateArch(ATSaveStateReader& reader);
	void LoadStatePrivate(ATSaveStateReader& reader);
	void LoadStateResetPrivate(ATSaveStateReader& reader);
	void EndLoadState(ATSaveStateReader& reader);

	void SaveState(IATObjectState **pp);
	void LoadState(const IATObjectState *state);
	void LoadState(const class ATSaveStateGtia& state);
	void PostLoadState();

	void GetRegisterState(ATGTIARegisterState& state) const;
	void DumpHighArtifactingFilters(ATConsoleOutput& output);

	enum VBlankMode {
		kVBlankModeOff,
		kVBlankModeOn,
		kVBlankModeBugged
	};

	void SetNextFieldPolarity(ATVideoFieldPolarity polarity);
	void SetVBLANK(VBlankMode vblMode);
	void SetOnRetryFrame(vdfunction<void()> fn);
	bool BeginFrame(uint32 frameNumber, uint32 y, bool force, bool drop);
	void BeginScanline(int y, bool hires);
	void EndScanline(uint8 dlControl, bool pfRendered);
	void UpdatePlayer(bool odd, int index, uint8 byte);
	void UpdateMissile(bool odd, uint8 byte);
	void UpdatePlayfield160(uint32 x, uint8 byte);
	void UpdatePlayfield160(uint32 x, const uint8 *src, uint32 n);
	void UpdatePlayfield320(uint32 x, uint8 byte);
	void UpdatePlayfield320(uint32 x, const uint8 *src, uint32 n);
	void Sync(int offset = 0);

	void RenderActivityMap(const uint8 *src);
	void UpdateScreen(bool immediate, bool forceAnyScreen);
	void RecomputePalette();

	sint32 ReadBackWriteRegister(uint8 reg) const;

	uint8 DebugReadByte(uint8 reg) {
		return ReadByte(reg);
	}

	uint8 ReadByte(uint8 reg);
	void WriteByte(uint8 reg, uint8 value);

	ATGTIALightSensor *RegisterLightSensor(int hrPxWidth, int hrPxHeight, const uint8 *weights, float lightThreshold, vdfunction<void(int)> triggerFn);
	void SetLightSensorPosition(ATGTIALightSensor *sensor, int hrPxX, int hrPxY);
	void UnregisterLightSensor(ATGTIALightSensor *sensor);

protected:
	struct RegisterChange {
		sint16 mPos;
		uint8 mReg;
		uint8 mValue;
	};

	struct SpriteState {
		uint8	mShiftRegister;
		uint8	mShiftState;
		uint8	mSizeMode;
		uint8	mDataLatch;

		void Reset();
		uint8 Detect(uint32 ticks, const uint8 *src);
		uint8 Detect(uint32 ticks, const uint8 *src, const uint8 *hires);
		uint8 Generate(uint32 ticks, uint8 mask, uint8 *dst);
		uint8 Generate(uint32 ticks, uint8 mask, uint8 *dst, const uint8 *hires);
		void Advance(uint32 ticks);
	};

	struct SpriteImage {
		SpriteImage *mpNext;
		sint16	mX1;
		sint16	mX2;
		SpriteState mState;
	};

	struct Sprite {
		SpriteState mState;
		SpriteImage *mpImageHead;
		SpriteImage *mpImageTail;
		int mLastSync;

		void Sync(int pos);
	};

	template<class T> void ExchangeStatePrivate(T& io);
	void SyncTo(int xend);
	void Render(int x1, int targetX);
	void ApplyArtifacting(bool immediate);
	void AddRegisterChange(uint8 pos, uint8 addr, uint8 value);
	void UpdateRegisters(const RegisterChange *rc, int count);
	void UpdateSECAMTriggerLatch(int index);
	void ResetSprites();
	void GenerateSpriteImages(int x1, int x2);
	void GenerateSpriteImage(Sprite& sprite, int pos);
	void FreeSpriteImage(SpriteImage *);
	SpriteImage *AllocSpriteImage();
	VerticalOverscanMode DeriveVerticalOverscanMode() const;

	void SetFrameProperties();
	void ResetLightSensors();
	void ProcessLightSensors();

	// critical variables - sync
	IATGTIAEmulatorConnections *mpConn; 
	int		mLastSyncX;
	VBlankMode		mVBlankMode;
	bool	mbANTICHiresMode;
	bool	mbHiresMode;

	typedef vdfastvector<RegisterChange> RegisterChanges;
	RegisterChanges mRegisterChanges;
	int mRCIndex;
	int mRCCount;

	// critical variables - sprite update
	uint8	mSpritePos[8];
	bool	mbSpritesActive;
	bool	mbPMRendered;
	SpriteImage *mpFreeSpriteImages;
	Sprite	mSprites[8];
	uint8	mPlayerCollFlags[4];
	uint8	mMissileCollFlags[4];

	// non-critical variables
	IVDVideoDisplay *mpDisplay;
	ATNotifyList<IATGTIAVideoTap *> mVideoTaps;
	uint32	mY;

	AnalysisMode	mAnalysisMode;
	ATArtifactMode	mArtifactMode;
	OverscanMode	mOverscanMode;
	VerticalOverscanMode	mVerticalOverscanMode;
	bool	mbVsyncEnabled = false;
	bool	mbVsyncAdaptiveEnabled = false;
	sint32	mDisplayLagCounter = 0;
	ATMonitorMode	mMonitorMode = ATMonitorMode::Color;
	ATVideoDeinterlaceMode mDeinterlaceMode = ATVideoDeinterlaceMode::None;
	bool	mbBlendMode = false;
	bool	mbBlendModeLastFrame = false;
	bool	mbBlendModeLastFrameMonoPersistence = false;
	bool	mbBlendLinear = true;				// frame blending operates in linear color
	bool	mbBlendMonoPersistence = false;
	bool	mbFrameCopiedFromPrev = false;
	bool	mbOverscanPALExtended = false;
	bool	mbInterlaceEnabled = false;
	bool	mbScanlinesEnabled = false;
	bool	mbAccelScreenFX = false;

	ATVideoFieldPolarity mNextFieldPolarity = ATVideoFieldPolarity::Unknown;
	ATVideoFieldPolarity mLastFieldPolarity = ATVideoFieldPolarity::Unknown;
	bool mbUseLowerField = false;

	ATVideoFrameProperties mFrameProperties {};

	ATGTIARegisterState	mState;

	// used during register read
	uint8	mCollisionMask;

	// The following 9 registers must be contiguous.
	uint8	mPMColor[4];		// $D012-D015 player and missile colors
	uint8	mPFColor[4];		// $D016-D019 playfield colors
	uint8	mPFBAK;				// $D01A background color

	uint8	mActivePRIOR;		// $D01B priority - currently live value
	uint8	mPRIOR;				// $D01B priority - architectural value
	uint8	mVDELAY;			// $D01C vertical delay
	uint8	mGRACTL;			// $D01D
								// bit 2: latch trigger inputs
								// bit 1: enable players
								// bit 0: enable missiles
	uint8	mSwitchOutput;		// $D01F (CONSOL) output from GTIA
								// bit 3: speaker output
	uint8	mSwitchInput;		// $D01F (CONSOL) input to GTIA
	uint8	mForcedSwitchInput;

	uint8	mTRIG[4];
	uint8	mTRIGLatched[4];
	uint8	mTRIGSECAM[4];
	uint32	mTRIGSECAMLastUpdate[4];

	uint8	*mpDst;
	vdrefptr<ATFrameBuffer>	mpFrame;
	vdrefptr<ATFrameBuffer>	mpDroppedFrame;
	VDPixmap	mRawFrame;
	uint64	mFrameTimestamp;
	ATFrameTracker *mpFrameTracker;
	bool	mbMixedRendering;	// GTIA mode with non-hires or pseudo mode E
	bool	mbGTIADisableTransition;
	bool	mbTurbo;
	bool	mbCTIAMode;
	bool	mb50HzMode;
	bool	mbPALMode;
	bool	mbSECAMMode;
	bool	mbForcedBorder;
	bool	mbDefectType2Active = false;
	int		mPALPhase = 0;
	ATGTIADefectMode mDefectMode = ATGTIADefectMode::Type1;

	struct LightSensor {
		vdrect32 mHrPixelArea {};
		class ATGTIALightSensorImpl *mpSensor = nullptr;
	};

	vdvector<LightSensor> mLightSensors;

	VDALIGN(16)	uint8	mMergeBuffer[228+12];
	VDALIGN(16)	uint8	mAnticData[228+12];
	bool	mbScanlinesWithHiRes[240];

	ATColorParams mActiveColorParams;
	ATColorSettings mColorSettings;
	float mColorMatchingMatrix[3][3];

	// output gamma, e.g. 2.2 or 2.4, or 0 for sRGB EOTF
	float mOutputGamma = 0;

	vdfastvector<uint8, vdaligned_alloc<uint8> > mPreArtifactFrameBuffer;
	VDPixmap	mPreArtifactFrame;
	uint32		mPreArtifactFrameVisibleY1;
	uint32		mPreArtifactFrameVisibleY2;

	ATArtifactingEngine	*mpArtifactingEngine;
	vdrefptr<ATFrameBuffer> mpLastFrame;

	ATGTIARenderer *mpRenderer;
	IATUIRenderer *mpUIRenderer;
	ATVBXEEmulator *mpVBXE;

	vdfunction<void()> mpOnRetryFrame;
	bool mbWaitingForFrame = false;

	ATGTIAColorTrace mColorTrace {};

	ATNotifyList<const ATGTIARawFrameFn *> mRawFrameCallbacks;

	VDLinearAllocator mNodeAllocator;

	struct Impl;
	vdautoptr<Impl> mpImpl;

	uint32	mPalette[256];
	uint32	mSignedPalette[256];
	uint32	mUncorrectedPalette[256];
	uint32	mUncorrectedSignedPalette[256];
};

inline auto operator<=>(ATGTIAEmulator::HighColorAvailability a, ATGTIAEmulator::HighColorAvailability b) {
	return static_cast<std::underlying_type_t<ATGTIAEmulator::HighColorAvailability>>(a) <=>
	       static_cast<std::underlying_type_t<ATGTIAEmulator::HighColorAvailability>>(b);
}

#endif
