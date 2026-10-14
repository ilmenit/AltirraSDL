//	VirtualDub - Video processing and capture application
//	Copyright (C) 1998-2005 Avery Lee
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
//

#ifndef f_VD2_RIZA_DISPLAYDRVDX9_H
#define f_VD2_RIZA_DISPLAYDRVDX9_H

#include <vd2/system/refcount.h>
#include <vd2/VDDisplay/renderer.h>

struct IDirect3DTexture9;
class IVDVideoDisplayDX9Manager;
class VDD3D9Manager;

namespace nsVDD3D9 {
	struct Vertex;
}

namespace nsVDDisplay {
	struct TechniqueInfo;
}

class VDINTERFACE IVDFontRendererD3D9 : public IVDRefCount {
public:
	virtual bool Init(VDD3D9Manager *d3dmgr) = 0;
	virtual void Shutdown() = 0;

	virtual bool Begin() = 0;
	virtual void DrawTextLine(int x, int y, uint32 textColor, uint32 outlineColor, const char *s) = 0;
	virtual void End() = 0;
};

bool VDCreateFontRendererD3D9(IVDFontRendererD3D9 **);

class VDINTERFACE IVDDisplayRendererD3D9 : public IVDRefCount, public IVDDisplayRenderer {
public:
	virtual bool Init(VDD3D9Manager *d3dmgr, IVDVideoDisplayDX9Manager *vidmgr) = 0;
	virtual void Shutdown() = 0;

	virtual bool Begin() = 0;
	virtual void End() = 0;
};

bool VDCreateDisplayRendererD3D9(IVDDisplayRendererD3D9 **);

class VDINTERFACE IVDVideoDisplayDX9Manager : public IVDRefCount {
public:
	enum CubicMode {
		kCubicNotInitialized,
		kCubicNotPossible,
		kCubicUsePS2_0Path,
		kMaxCubicMode = kCubicUsePS2_0Path
	};

	struct StdParamData {
		float vpsize[4];			// (viewport size)			vpwidth, vpheight, 1/vpheight, 1/vpwidth
		float srcsize[4];			// (source size)			srcwidth, srcheight, 1/srcheight, 1/srcwidth
		float texsize[4];			// (texture size)			texwidth, texheight, 1/texheight, 1/texwidth
		float tex2size[4];			// (texture2 size)			tex2width, tex2height, 1/tex2height, 1/tex2width
		float tempsize[4];			// (temp rtt size)			tempwidth, tempheight, 1/tempheight, 1/tempwidth
		float interphtexsize[4];	// (cubic htex interp info)
		float interpvtexsize[4];	// (cubic vtex interp info)
		float interptexsize[4];		// (interp tex info)
		float srcarea[4];			// (source area in texels)	u1, v1, u2, v2
		float chromauvscale[4];		// (chroma UV scale)		U scale, V scale, und, und
		float chromauvoffset[4];	// (chroma UV offset)		U offset, V offset, und, und
		float pixelsharpness[4];	// (pixel sharpness)		X factor, Y factor, ?, ?
		float outputxform[4];		// (output transform)		X scale, Y scale, Y offset, X offset (maps 0-1 to output area,includes half pixel offset)
	};


	struct EffectContext {
		IDirect3DTexture9 *mpSourceTexture1 = nullptr;
		IDirect3DTexture9 *mpSourceTexture2 = nullptr;
		IDirect3DTexture9 *mpSourceTexture3 = nullptr;
		IDirect3DTexture9 *mpSourceTexture4 = nullptr;
		IDirect3DTexture9 *mpSourceTexture5 = nullptr;
		IDirect3DTexture9 *mpPaletteTexture = nullptr;
		IDirect3DTexture9 *mpInterpFilterH = nullptr;
		IDirect3DTexture9 *mpInterpFilterV = nullptr;
		IDirect3DTexture9 *mpInterpFilter = nullptr;
		uint32 mSourceW = 0;
		uint32 mSourceH = 0;
		uint32 mSourceTexW = 0;
		uint32 mSourceTexH = 0;
		float mInterpHTexW = 0;
		float mInterpHTexH = 0;
		float mInterpVTexW = 0;
		float mInterpVTexH = 0;
		uint32 mInterpTexW = 0;
		uint32 mInterpTexH = 0;

		/// Source rect.
		vdrect32f mSourceArea { 0.0f, 0.0f, 0.0f, 0.0f };

		/// Output viewport.
		int mViewportX = 0;
		int mViewportY = 0;
		int mViewportW = 0;
		int mViewportH = 0;

		/// Desired output width and height. May extend outside of viewport, in which case clipping is desired.
		float mOutputX = 0;
		float mOutputY = 0;
		float mOutputW = 0;
		float mOutputH = 0;

		bool mbOutputClear = false;
		int mOutputTessellationX = 1;
		int mOutputTessellationY = 1;

		float mDefaultUVScaleCorrectionX = 0;
		float mDefaultUVScaleCorrectionY = 0;

		float mChromaScaleU = 0;
		float mChromaScaleV = 0;
		float mChromaOffsetU = 0;
		float mChromaOffsetV = 0;

		float mPixelSharpnessX = 0;
		float mPixelSharpnessY = 0;

		bool mbHighPrecision = false;

		// If enabled, map 'autobilinear' samplers to bilinear instead of point.
		bool mbAutoBilinear = false;

		bool mbUseUV0Scale = false;
		bool mbUseUV0Area = false;
		bool mbUseUV1Area = false;

		vdfloat2 mUV0Scale { 0.0f, 0.0f };
		vdrect32f mUV0Area { 0.0f, 0.0f, 0.0f, 0.0f };
		vdrect32f mUV1Area { 0.0f, 0.0f, 0.0f, 0.0f };

		const vdfunction<void(vdspan<nsVDD3D9::Vertex> vxs, const StdParamData& data)> *mVertexProcessor = nullptr;
	};

	virtual bool IsPS20Enabled() const = 0;
	virtual bool RunEffect(const EffectContext& ctx, const nsVDDisplay::TechniqueInfo& technique, IDirect3DSurface9 *pRTOverride) = 0;
};

class VDINTERFACE IVDVideoUploadContextD3D9 : public IVDRefCount {
public:
	virtual IDirect3DTexture9 *GetD3DTexture(int i) = 0;

	virtual bool Init(void *hmonitor, bool use9ex, const VDPixmap& source, bool allowConversion, int buffers, bool use16bit) = 0;
	virtual void Shutdown() = 0;

	virtual bool Update(const VDPixmap& source) = 0;
};

class VDDisplayCachedImageD3D9 final : public vdrefcounted<IVDRefUnknown>, public vdlist_node {
	VDDisplayCachedImageD3D9(const VDDisplayCachedImageD3D9&) = delete;
	VDDisplayCachedImageD3D9& operator=(const VDDisplayCachedImageD3D9&) = delete;
public:
	enum { kTypeID = "cim9"_vdfcctypeid };

	VDDisplayCachedImageD3D9();
	~VDDisplayCachedImageD3D9();

	void *AsInterface(uint32 iid);

	bool Init(VDD3D9Manager *mgr, void *owner, const VDDisplayImageView& imageView);
	void Shutdown();

	void Update(const VDDisplayImageView& imageView);

public:
	void *mpOwner;
	vdrefptr<IDirect3DTexture9> mpD3DTexture;
	sint32	mWidth;
	sint32	mHeight;
	sint32	mTexWidth;
	sint32	mTexHeight;
	uint32	mUniquenessCounter;
};

bool VDCreateVideoUploadContextD3D9(IVDVideoUploadContextD3D9 **);

#endif
