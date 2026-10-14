//	VirtualDub - Video processing and capture application
//	A/V interface library
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

///////////////////////////////////////////////////////////////////////////////
//
//	This file is the DirectX 9 driver for the video display subsystem.
//	It does traditional point sampled and bilinearly filtered upsampling
//	as well as a special multipass algorithm for emulated bicubic
//	filtering.
//

#include <vd2/system/vdtypes.h>

#define DIRECTDRAW_VERSION 0x0900
#define INITGUID
#include <d3d9.h>
#include <vd2/system/vdtypes.h>
#include <vd2/system/vdalloc.h>
#include <vd2/system/error.h>
#include <vd2/system/binary.h>
#include <vd2/system/refcount.h>
#include <vd2/system/math.h>
#include <vd2/system/seh.h>
#include <vd2/system/time.h>
#include <vd2/system/vdstl.h>
#include <vd2/system/vdstl_vectorview.h>
#include <vd2/system/w32assist.h>
#include <vd2/Kasumi/blitter.h>
#include <vd2/Kasumi/pixmap.h>
#include <vd2/Kasumi/pixmapops.h>
#include <vd2/Kasumi/pixmaputils.h>
#include <vd2/Kasumi/region.h>

#include <displaydrvd3d9.h>
#include <vd2/VDDisplay/direct3d.h>
#include <vd2/VDDisplay/compositor.h>
#include <vd2/VDDisplay/display.h>
#include <vd2/VDDisplay/displaydrv.h>
#include <vd2/VDDisplay/logging.h>
#include <vd2/VDDisplay/renderer.h>
#include <vd2/VDDisplay/textrenderer.h>
#include <vd2/VDDisplay/internal/bloom.h>
#include <vd2/VDDisplay/internal/customeffectd3d9.h>
#include <vd2/VDDisplay/internal/screenfx.h>

namespace nsVDDisplay {
	#include "displayd3d9_shader.inl"
}

#ifdef _MSC_VER
#pragma warning(disable: 4351)		// warning C4351: new behavior: elements of array 'VDVideoUploadContextD3D9::mpD3DImageTextures' will be default initialized
#endif

#define VDDEBUG_DX9DISP(...) VDDispLogF(__VA_ARGS__)

#define D3D_DO(x) VDVERIFY(SUCCEEDED(mpD3DDevice->x))

using namespace nsVDD3D9;

class VDD3D9TextureGeneratorFullSizeRTT : public vdrefcounted<IVDD3D9TextureGenerator> {
public:
	bool GenerateTexture(VDD3D9Manager *pManager, IVDD3D9Texture *pTexture) {
		const D3DDISPLAYMODE& dmode = pManager->GetDisplayMode();

		int w = dmode.Width;
		int h = dmode.Height;

		pManager->AdjustTextureSize(w, h);

		IDirect3DDevice9 *dev = pManager->GetDevice();
		IDirect3DTexture9 *tex;
		HRESULT hr = dev->CreateTexture(w, h, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &tex, NULL);
		if (FAILED(hr))
			return false;

		pTexture->SetD3DTexture(tex);
		tex->Release();
		return true;
	}
};

bool VDCreateD3D9TextureGeneratorFullSizeRTT(IVDD3D9TextureGenerator **ppGenerator) {
	*ppGenerator = new VDD3D9TextureGeneratorFullSizeRTT;
	if (!*ppGenerator)
		return false;
	(*ppGenerator)->AddRef();
	return true;
}

///////////////////////////////////////////////////////////////////////////

class VDD3D9TextureGeneratorFullSizeRTT16F : public vdrefcounted<IVDD3D9TextureGenerator> {
public:
	bool GenerateTexture(VDD3D9Manager *pManager, IVDD3D9Texture *pTexture) {
		const D3DDISPLAYMODE& dmode = pManager->GetDisplayMode();

		int w = dmode.Width;
		int h = dmode.Height;

		pManager->AdjustTextureSize(w, h);

		IDirect3DDevice9 *dev = pManager->GetDevice();
		IDirect3DTexture9 *tex;
		HRESULT hr = dev->CreateTexture(w, h, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A16B16G16R16F, D3DPOOL_DEFAULT, &tex, NULL);
		if (FAILED(hr))
			return false;

		pTexture->SetD3DTexture(tex);
		tex->Release();
		return true;
	}
};

bool VDCreateD3D9TextureGeneratorFullSizeRTT16F(IVDD3D9TextureGenerator **ppGenerator) {
	*ppGenerator = new VDD3D9TextureGeneratorFullSizeRTT16F;
	if (!*ppGenerator)
		return false;
	(*ppGenerator)->AddRef();
	return true;
}

///////////////////////////////////////////////////////////////////////////

class VDD3D9TextureGeneratorHEvenOdd final : public vdrefcounted<IVDD3D9TextureGenerator> {
public:
	bool GenerateTexture(VDD3D9Manager *pManager, IVDD3D9Texture *pTexture) {
		vdrefptr<IVDD3D9InitTexture> tex;
		if (!pManager->CreateInitTexture(16, 1, 1, D3DFMT_A8R8G8B8, ~tex))
			return false;

		VDD3D9LockInfo lockInfo;
		if (!tex->Lock(0, lockInfo)) {
			VDDEBUG_DX9DISP("VideoDisplay/DX9: Failed to load horizontal even/odd texture.");
			return false;
		}

		for(int i=0; i<16; ++i)
			((uint32 *)lockInfo.mpData)[i] = (uint32)-(sint32)(i&1);

		tex->Unlock(0);

		return pTexture->Init(tex);
	}
};

class VDD3D9TextureGeneratorCubicFilter final : public vdrefcounted<IVDD3D9TextureGenerator> {
public:
	bool GenerateTexture(VDD3D9Manager *pManager, IVDD3D9Texture *pTexture) {
		vdrefptr<IVDD3D9InitTexture> tex;
		if (!pManager->CreateInitTexture(256, 4, 1, D3DFMT_A8R8G8B8, ~tex))
			return false;

		VDD3D9LockInfo lr;
		if (!tex->Lock(0, lr)) {
			VDDEBUG_DX9DISP("VideoDisplay/DX9: Failed to load cubic filter texture.");
			return false;
		}

		MakeCubic4Texture((uint32 *)lr.mpData, lr.mPitch, -0.75);

		tex->Unlock(0);

		return pTexture->Init(tex);
	}

protected:
	static void MakeCubic4Texture(uint32 *texture, ptrdiff_t pitch, double A) {
		int i;

		uint32 *p0 = texture;

		for(i=0; i<256; i++) {
			double d = (double)(i&63) / 64.0;
			int y1, y2, y3, y4, ydiff;

			// Coefficients for all four pixels *must* add up to 1.0 for
			// consistent unity gain.
			//
			// Two good values for A are -1.0 (original VirtualDub bicubic filter)
			// and -0.75 (closely matches Photoshop).

			double c1 =         +     A*d -       2.0*A*d*d +       A*d*d*d;
			double c2 = + 1.0             -     (A+3.0)*d*d + (A+2.0)*d*d*d;
			double c3 =         -     A*d + (2.0*A+3.0)*d*d - (A+2.0)*d*d*d;
			double c4 =                   +           A*d*d -       A*d*d*d;

			const int maxval = 255;
			double scale = maxval / (c1 + c2 + c3 + c4);

			y1 = (int)floor(0.5 + c1 * scale);
			y2 = (int)floor(0.5 + c2 * scale);
			y3 = (int)floor(0.5 + c3 * scale);
			y4 = (int)floor(0.5 + c4 * scale);

			ydiff = maxval - y1 - y2 - y3 - y4;

			int ywhole = ydiff<0 ? (ydiff-2)/4 : (ydiff+2)/4;
			ydiff -= ywhole*4;

			y1 += ywhole;
			y2 += ywhole;
			y3 += ywhole;
			y4 += ywhole;

			if (ydiff < 0) {
				if (y1<y4)
					y1 += ydiff;
				else
					y4 += ydiff;
			} else if (ydiff > 0) {
				if (y2 > y3)
					y2 += ydiff;
				else
					y3 += ydiff;
			}

			p0[i] = (-y1 << 24) + (y2 << 16) + (y3 << 8) + (-y4);
		}
	}
};

///////////////////////////////////////////////////////////////////////////

#ifdef _MSC_VER
	#pragma warning(push)
	#pragma warning(disable: 4584)		// warning C4584: 'VDVideoDisplayDX9Manager' : base-class 'vdlist_node' is already a base-class of 'VDD3D9Client'
#endif

struct VDVideoDisplayDX9ManagerNode : public vdlist_node {};

class VDVideoDisplayDX9Manager final : public IVDVideoDisplayDX9Manager, public VDD3D9Client, public VDVideoDisplayDX9ManagerNode {
public:
	VDVideoDisplayDX9Manager(VDThreadID tid, HMONITOR hmonitor, bool use9ex);
	~VDVideoDisplayDX9Manager();

	int AddRef() override;
	int Release() override;

	bool Init();
	void Shutdown();

	CubicMode InitBicubic();
	void ShutdownBicubic();

	bool InitBicubicTempSurfaces();
	void ShutdownBicubicTempSurfaces();

	bool IsD3D9ExEnabled() const { return mbUseD3D9Ex; }
	bool Is16FEnabled() const { return mbIs16FEnabled; }
	bool IsPS11Enabled() const { return mbIsPS11Enabled; }
	bool IsPS20Enabled() const override { return mbIsPS20Enabled; }

	VDThreadID GetThreadId() const { return mThreadId; }
	HMONITOR GetMonitor() const { return mhMonitor; }

	IVDD3D9Texture	*GetTempRTT(int i) const { return mpRTTs[i]; }
	IVDD3D9Texture	*GetFilterTexture() const { return mpFilterTexture; }
	IVDD3D9Texture	*GetHEvenOddTexture() const { return mpHEvenOddTexture; }

	void		DetermineBestTextureFormat(int srcFormat, int& dstFormat, D3DFORMAT& dstD3DFormat);

	bool ValidateBicubicShader(CubicMode mode);

	bool BlitFixedFunction(const EffectContext& ctx, IDirect3DSurface9 *pRTOverride, bool bilinear);
	bool RunEffect(const EffectContext& ctx, const nsVDDisplay::TechniqueInfo& technique, IDirect3DSurface9 *pRTOverride) override;

public:
	void OnPreDeviceReset() override {}
	void OnPostDeviceReset() override {}

protected:
	bool BlitFixedFunction2(const EffectContext& ctx, IDirect3DSurface9 *pRTOverride, bool bilinear);
	bool RunEffect2(const EffectContext& ctx, const nsVDDisplay::TechniqueInfo& technique, IDirect3DSurface9 *pRTOverride);
	bool InitEffect();
	void ShutdownEffect();

	VDD3D9Manager		*mpManager;
	vdrefptr<IVDD3D9Texture>	mpFilterTexture;
	vdrefptr<IVDD3D9Texture>	mpHEvenOddTexture;
	vdrefptr<IVDD3D9Texture>	mpRTTs[2];

	vdfastvector<IDirect3DVertexShader9 *>	mVertexShaders;
	vdfastvector<IDirect3DPixelShader9 *>	mPixelShaders;

	vdfastvector<Vertex> mVertexWorkspace;

	CubicMode			mCubicMode;
	int					mCubicRefCount;
	int					mCubicTempSurfacesRefCount;
	bool				mbIs16FEnabled;
	bool				mbIsPS11Enabled;
	bool				mbIsPS20Enabled;
	bool				mbUseD3D9Ex;

	const VDThreadID	mThreadId;
	const HMONITOR		mhMonitor;
	int					mRefCount;
};

#ifdef _MSC_VER
	#pragma warning(pop)
#endif

///////////////////////////////////////////////////////////////////////////

VDDisplayCachedImageD3D9::VDDisplayCachedImageD3D9() {
	mListNodePrev = NULL;
	mListNodeNext = NULL;
}

VDDisplayCachedImageD3D9::~VDDisplayCachedImageD3D9() {
	if (mListNodePrev)
		vdlist_base::unlink(*this);
}

void *VDDisplayCachedImageD3D9::AsInterface(uint32 iid) {
	if (iid == kTypeID)
		return this;

	return NULL;
}

bool VDDisplayCachedImageD3D9::Init(VDD3D9Manager *mgr, void *owner, const VDDisplayImageView& imageView) {
	const VDPixmap& px = imageView.GetImage();
	int w = px.w;
	int h = px.h;

	if (!mgr->AdjustTextureSize(w, h, true))
		return false;

	IDirect3DDevice9 *dev = mgr->GetDevice();
	HRESULT hr = dev->CreateTexture(w, h, 1, 0, D3DFMT_X8R8G8B8, mgr->GetDeviceEx() ? D3DPOOL_DEFAULT : D3DPOOL_MANAGED, ~mpD3DTexture, NULL);
	if (FAILED(hr))
		return false;

	mWidth = px.w;
	mHeight = px.h;
	mTexWidth = w;
	mTexHeight = h;
	mpOwner = owner;
	mUniquenessCounter = imageView.GetUniquenessCounter() - 2;

	Update(imageView);
	return true;
}

void VDDisplayCachedImageD3D9::Shutdown() {
	mpD3DTexture.clear();
	mpOwner = NULL;
}

void VDDisplayCachedImageD3D9::Update(const VDDisplayImageView& imageView) {
	uint32 newCounter = imageView.GetUniquenessCounter();
	bool partialUpdateOK = ((mUniquenessCounter + 1) == newCounter);

	mUniquenessCounter = newCounter;

	if (mpD3DTexture) {
		const VDPixmap& px = imageView.GetImage();

		const uint32 numRects = imageView.GetDirtyListSize();

		D3DLOCKED_RECT lr;

		if (partialUpdateOK && numRects) {
			const vdrect32 *rects = imageView.GetDirtyList();

			if (SUCCEEDED(mpD3DTexture->LockRect(0, &lr, NULL, D3DLOCK_NO_DIRTY_UPDATE))) {
				VDPixmap dst = {};
				dst.format = nsVDPixmap::kPixFormat_XRGB8888;
				dst.w = mTexWidth;
				dst.h = mTexHeight;
				dst.pitch = lr.Pitch;
				dst.data = lr.pBits;

				for(uint32 i=0; i<numRects; ++i) {
					const vdrect32& r = rects[i];

					VDPixmapBlt(dst, r.left, r.top, px, r.left, r.top, r.width(), r.height());
				}

				mpD3DTexture->UnlockRect(0);

				for(uint32 i=0; i<numRects; ++i) {
					const vdrect32& r = rects[i];

					RECT r2 = {r.left, r.top, r.right, r.bottom};
					mpD3DTexture->AddDirtyRect(&r2);
				}
			}
		} else {
			if (SUCCEEDED(mpD3DTexture->LockRect(0, &lr, NULL, 0))) {
				VDPixmap dst = {};
				dst.format = nsVDPixmap::kPixFormat_XRGB8888;
				dst.w = mTexWidth;
				dst.h = mTexHeight;
				dst.pitch = lr.Pitch;
				dst.data = lr.pBits;

				VDPixmapBlt(dst, px);

				mpD3DTexture->UnlockRect(0);
			}
		}
	}
}

///////////////////////////////////////////////////////////////////////////

static VDCriticalSection g_csVDDisplayDX9Managers;
static vdlist<VDVideoDisplayDX9ManagerNode> g_VDDisplayDX9Managers;

bool VDInitDisplayDX9(HMONITOR hmonitor, bool use9ex, VDVideoDisplayDX9Manager **ppManager) {
	VDVideoDisplayDX9Manager *pMgr = NULL;
	bool firstClient = false;

	vdsynchronized(g_csVDDisplayDX9Managers) {
		vdlist<VDVideoDisplayDX9ManagerNode>::iterator it(g_VDDisplayDX9Managers.begin()), itEnd(g_VDDisplayDX9Managers.end());

		VDThreadID tid = VDGetCurrentThreadID();

		for(; it != itEnd; ++it) {
			VDVideoDisplayDX9Manager *mgr = static_cast<VDVideoDisplayDX9Manager *>(*it);

			if (mgr->GetThreadId() == tid && mgr->GetMonitor() == hmonitor && mgr->IsD3D9ExEnabled() == use9ex) {
				pMgr = mgr;
				break;
			}
		}

		if (!pMgr) {
			pMgr = new_nothrow VDVideoDisplayDX9Manager(tid, hmonitor, use9ex);
			if (!pMgr)
				return false;

			g_VDDisplayDX9Managers.push_back(pMgr);
			firstClient = true;
		}

		pMgr->AddRef();
	}

	if (firstClient) {
		if (!pMgr->Init()) {
			vdsynchronized(g_csVDDisplayDX9Managers) {
				g_VDDisplayDX9Managers.erase(pMgr);
			}
			pMgr->Release();
			return NULL;
		}
	}

	*ppManager = pMgr;
	return true;
}

VDVideoDisplayDX9Manager::VDVideoDisplayDX9Manager(VDThreadID tid, HMONITOR hmonitor, bool use9ex)
	: mpManager(NULL)
	, mCubicRefCount(0)
	, mThreadId(tid)
	, mhMonitor(hmonitor)
	, mRefCount(0)
	, mbIs16FEnabled(false)
	, mbIsPS20Enabled(false)
	, mbUseD3D9Ex(use9ex)
{
	mCubicTempSurfacesRefCount = 0;
}

VDVideoDisplayDX9Manager::~VDVideoDisplayDX9Manager() {
	VDASSERT(!mRefCount);
	VDASSERT(!mCubicRefCount);
	VDASSERT(!mCubicTempSurfacesRefCount);

	vdsynchronized(g_csVDDisplayDX9Managers) {
		g_VDDisplayDX9Managers.erase(this);
	}
}

int VDVideoDisplayDX9Manager::AddRef() {
	return ++mRefCount;
}

int VDVideoDisplayDX9Manager::Release() {
	int rc = --mRefCount;
	if (!rc) {
		Shutdown();
		delete this;
	}
	return rc;
}

bool VDVideoDisplayDX9Manager::Init() {
	VDASSERT(!mpManager);
	mpManager = VDInitDirect3D9(this, mhMonitor, mbUseD3D9Ex);
	if (!mpManager)
		return false;

	// Check for 16F capability.
	//
	// We need:
	//	* Vertex and pixel shader 2.0.
	//	* 16F texture support.
	//	* 16F blending.

	const D3DCAPS9& caps = mpManager->GetCaps();

	mbIs16FEnabled = false;

	mbIsPS11Enabled = false;
	if (caps.VertexShaderVersion >= D3DVS_VERSION(1, 1) &&
		caps.PixelShaderVersion >= D3DPS_VERSION(1, 1))
	{
		mbIsPS11Enabled = true;
	}

	if (caps.VertexShaderVersion >= D3DVS_VERSION(2, 0) &&
		caps.PixelShaderVersion >= D3DPS_VERSION(2, 0))
	{
		mbIsPS20Enabled = true;

		if (mpManager->CheckResourceFormat(0, D3DRTYPE_TEXTURE, D3DFMT_A16B16G16R16F) &&
			mpManager->CheckResourceFormat(D3DUSAGE_QUERY_FILTER, D3DRTYPE_TEXTURE, D3DFMT_A16B16G16R16F))
		{
			mbIs16FEnabled = true;
		}
	}

	if (!mpManager->CreateSharedTexture<VDD3D9TextureGeneratorHEvenOdd>("hevenodd", ~mpHEvenOddTexture)) {
		Shutdown();
		return false;
	}

	if (!InitEffect()) {
		Shutdown();
		return false;
	}

	return true;
}

void VDVideoDisplayDX9Manager::Shutdown() {
	VDASSERT(!mCubicRefCount);
	VDASSERT(!mCubicTempSurfacesRefCount);

	mpHEvenOddTexture = NULL;

	ShutdownEffect();

	if (mpManager) {
		VDDeinitDirect3D9(mpManager, this);
		mpManager = NULL;
	}
}

bool VDVideoDisplayDX9Manager::InitEffect() {
	using namespace nsVDDisplay;

	IDirect3DDevice9 *pD3DDevice = mpManager->GetDevice();
	const D3DCAPS9& caps = mpManager->GetCaps();

	// initialize vertex shaders
	if (g_effect.mVertexShaderOffsets.size() > 1 && mVertexShaders.empty()) {
		const size_t n = g_effect.mVertexShaderOffsets.size() - 1;
		mVertexShaders.resize(n, nullptr);

		for(uint32 i=0; i<n; ++i) {
			const uint32 *pVertexShaderData = &*g_effect.mShaderData.begin() + *(g_effect.mVertexShaderOffsets.begin() + i);

			if ((pVertexShaderData[0] & 0xffff) > (caps.VertexShaderVersion & 0xffff))
				continue;

			HRESULT hr = pD3DDevice->CreateVertexShader((const DWORD *)pVertexShaderData, &mVertexShaders[i]);
			if (FAILED(hr)) {
				VDDEBUG_DX9DISP("VideoDisplay/DX9: Unable to create vertex shader #%d.", i+1);
				VDDEBUG_DX9DISP("VideoDisplay/DX9: Vertex shader version is: %x.", pVertexShaderData[0]);
				VDDEBUG_DX9DISP("VideoDisplay/DX9: Supported vertex shader version is: %x.", caps.VertexShaderVersion);
				return false;
			}
		}
	}

	// initialize pixel shaders
	if (g_effect.mPixelShaderOffsets.size() > 1 && mPixelShaders.empty()) {
		const size_t n = g_effect.mPixelShaderOffsets.size() - 1;
		mPixelShaders.resize(n, nullptr);

		for(uint32 i=0; i<n; ++i) {
			const uint32 *pPixelShaderData = &*g_effect.mShaderData.begin() + *(g_effect.mPixelShaderOffsets.begin() + i);

			if ((pPixelShaderData[0] & 0xffff) > (caps.PixelShaderVersion & 0xffff))
				continue;

			HRESULT hr = pD3DDevice->CreatePixelShader((const DWORD *)pPixelShaderData, &mPixelShaders[i]);
			if (FAILED(hr)) {
				VDDEBUG_DX9DISP("VideoDisplay/DX9: Unable to create pixel shader #%d.", i+1);
				VDDEBUG_DX9DISP("VideoDisplay/DX9: Pixel shader version is: %x.", pPixelShaderData[0]);
				VDDEBUG_DX9DISP("VideoDisplay/DX9: Supported pixel shader version is: %x.", caps.PixelShaderVersion);
				return false;
			}
		}
	}

	return true;
}

void VDVideoDisplayDX9Manager::ShutdownEffect() {
	while(!mPixelShaders.empty()) {
		IDirect3DPixelShader9 *ps = mPixelShaders.back();
		mPixelShaders.pop_back();

		if (ps)
			ps->Release();
	}

	while(!mVertexShaders.empty()) {
		IDirect3DVertexShader9 *vs = mVertexShaders.back();
		mVertexShaders.pop_back();

		if (vs)
			vs->Release();
	}
}

VDVideoDisplayDX9Manager::CubicMode VDVideoDisplayDX9Manager::InitBicubic() {
	VDASSERT(mRefCount > 0);
	VDASSERT(mCubicRefCount >= 0);

	if (++mCubicRefCount > 1)
		return mCubicMode;

	mCubicMode = (CubicMode)kMaxCubicMode;
	while(mCubicMode > kCubicNotPossible) {
		if (ValidateBicubicShader(mCubicMode))
			break;
		mCubicMode = (CubicMode)(mCubicMode - 1);
	}

	if (mCubicMode == kCubicNotPossible)
		ShutdownBicubic();

	return mCubicMode;
}

void VDVideoDisplayDX9Manager::ShutdownBicubic() {
	VDASSERT(mCubicRefCount > 0);
	if (--mCubicRefCount)
		return;

	mpFilterTexture = NULL;
}

bool VDVideoDisplayDX9Manager::InitBicubicTempSurfaces() {
	VDASSERT(mRefCount > 0);
	VDASSERT(mCubicTempSurfacesRefCount >= 0);

	if (++mCubicTempSurfacesRefCount > 1)
		return true;

	// create horizontal resampling texture
	if (!mpManager->CreateSharedTexture("rtt1", VDCreateD3D9TextureGeneratorFullSizeRTT, ~mpRTTs[0])) {
		ShutdownBicubicTempSurfaces();
		return false;
	}

	return true;
}

void VDVideoDisplayDX9Manager::ShutdownBicubicTempSurfaces() {
	VDASSERT(mCubicTempSurfacesRefCount > 0);
	if (--mCubicTempSurfacesRefCount)
		return;

	mpRTTs[1] = NULL;
	mpRTTs[0] = NULL;
}

namespace {
	D3DFORMAT GetD3DTextureFormatForPixmapFormat(int format) {
		using namespace nsVDPixmap;

		switch(format) {
			case nsVDPixmap::kPixFormat_XRGB1555:
				return D3DFMT_X1R5G5B5;

			case nsVDPixmap::kPixFormat_RGB565:
				return D3DFMT_R5G6B5;

			case nsVDPixmap::kPixFormat_XRGB8888:
				return D3DFMT_X8R8G8B8;

			case nsVDPixmap::kPixFormat_Y8_FR:
				return D3DFMT_L8;

			default:
				return D3DFMT_UNKNOWN;
		}
	}
}

void VDVideoDisplayDX9Manager::DetermineBestTextureFormat(int srcFormat, int& dstFormat, D3DFORMAT& dstD3DFormat) {
	using namespace nsVDPixmap;

	// Try direct format first. If that doesn't work, try a fallback (in practice, we
	// only have one).

	dstFormat = srcFormat;
	for(int i=0; i<2; ++i) {
		dstD3DFormat = GetD3DTextureFormatForPixmapFormat(dstFormat);
		if (dstD3DFormat && mpManager->IsTextureFormatAvailable(dstD3DFormat)) {
			dstFormat = srcFormat;
			return;
		}

		// fallback
		switch(dstFormat) {
			case kPixFormat_XRGB1555:
				dstFormat = kPixFormat_RGB565;
				break;

			case kPixFormat_RGB565:
				dstFormat = kPixFormat_XRGB1555;
				break;

			default:
				goto fail;
		}
	}
fail:

	// Just use X8R8G8B8. We always know this works (we reject the device if it doesn't).
	dstFormat = kPixFormat_XRGB8888;
	dstD3DFormat = D3DFMT_X8R8G8B8;
}

bool VDVideoDisplayDX9Manager::ValidateBicubicShader(CubicMode mode) {
	using namespace nsVDDisplay;

	if (mode != kCubicUsePS2_0Path)
		return false;

	// Validate caps bits.
	const D3DCAPS9& caps = mpManager->GetCaps();
	if (caps.PixelShaderVersion < D3DPS_VERSION(2, 0))
		return false;

	return true;
}

bool VDVideoDisplayDX9Manager::BlitFixedFunction(const EffectContext& ctx, IDirect3DSurface9 *pRTOverride, bool bilinear) {
	mpManager->BeginScope(L"BlitFixedFunction");
	bool success = BlitFixedFunction2(ctx, pRTOverride, bilinear);
	mpManager->EndScope();

	return success;
}

bool VDVideoDisplayDX9Manager::BlitFixedFunction2(const EffectContext& ctx, IDirect3DSurface9 *pRTOverride, bool bilinear) {
	using namespace nsVDDisplay;

	const D3DDISPLAYMODE& dmode = mpManager->GetDisplayMode();
	int clippedWidth = std::min<int>(ctx.mOutputX + ctx.mOutputW, dmode.Width);
	int clippedHeight = std::min<int>(ctx.mOutputY + ctx.mOutputH, dmode.Height);

	if (clippedWidth <= 0 || clippedHeight <= 0)
		return true;

	IDirect3DDevice9 *dev = mpManager->GetDevice();

	// bind vertex and pixel shaders
	HRESULT hr = dev->SetVertexShader(nullptr);
	if (FAILED(hr)) {
		VDDEBUG_DX9DISP("VideoDisplay/DX9: Couldn't clear vertex shader! hr=%08x %s", hr, VDDispDecodeD3D9Error(hr));
		return false;
	}

	hr = dev->SetPixelShader(nullptr);
	if (FAILED(hr)) {
		VDDEBUG_DX9DISP("VideoDisplay/DX9: Couldn't clear pixel shader! hr=%08x %s", hr, VDDispDecodeD3D9Error(hr));
		return false;
	}

	static const struct TextureStageState {
		DWORD mStage;
		D3DTEXTURESTAGESTATETYPE mState;
		DWORD mValue;
	} kTexStageStates[] = {
		{ 0, D3DTSS_COLOROP, D3DTOP_SELECTARG1 },
		{ 0, D3DTSS_COLORARG1, D3DTA_TEXTURE },
		{ 0, D3DTSS_COLORARG2, D3DTA_DIFFUSE },
		{ 0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1 },
		{ 0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE },
		{ 0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE },
		{ 1, D3DTSS_COLOROP, D3DTOP_DISABLE },
		{ 1, D3DTSS_ALPHAOP, D3DTOP_DISABLE },
		{ 2, D3DTSS_COLOROP, D3DTOP_DISABLE },
		{ 2, D3DTSS_ALPHAOP, D3DTOP_DISABLE },
	};

	for(const auto& tss : kTexStageStates) {
		hr = dev->SetTextureStageState(tss.mStage, tss.mState, tss.mValue);

		if (FAILED(hr)) {
			VDDEBUG_DX9DISP("VideoDisplay/DX9: Failed to set texture stage state! hr=%08x %s", hr, VDDispDecodeD3D9Error(hr));
			return false;
		}
	}

	static const struct SamplerState {
		D3DSAMPLERSTATETYPE mState;
		DWORD mValue;
	} kSamplerStatesPoint[] = {
		{ D3DSAMP_MINFILTER, D3DTEXF_POINT },
		{ D3DSAMP_MAGFILTER, D3DTEXF_POINT },
		{ D3DSAMP_MIPFILTER, D3DTEXF_NONE },
		{ D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP },
		{ D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP },
	}, kSamplerStatesBilinear[] = {
		{ D3DSAMP_MINFILTER, D3DTEXF_LINEAR },
		{ D3DSAMP_MAGFILTER, D3DTEXF_LINEAR },
		{ D3DSAMP_MIPFILTER, D3DTEXF_NONE },
		{ D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP },
		{ D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP },
	};

	for(const auto& ss : bilinear ? kSamplerStatesBilinear : kSamplerStatesPoint) {
		hr = dev->SetSamplerState(0, ss.mState, ss.mValue);

		if (FAILED(hr)) {
			VDDEBUG_DX9DISP("VideoDisplay/DX9: Failed to set sampler state! hr=%08x %s", hr, VDDispDecodeD3D9Error(hr));
			return false;
		}
	}

	hr = dev->SetTexture(0, ctx.mpSourceTexture1);
	if (FAILED(hr)) {
		VDDEBUG_DX9DISP("VideoDisplay/DX9: Failed to set texture! hr=%08x %s", hr, VDDispDecodeD3D9Error(hr));
		return false;
	}

	// change viewport
	VDASSERT(ctx.mViewportX >= 0);
	VDASSERT(ctx.mViewportY >= 0);
	VDASSERT(ctx.mViewportW >= 0);
	VDASSERT(ctx.mViewportH >= 0);

	D3DVIEWPORT9 vp;
	vp.X = ctx.mViewportX;
	vp.Y = ctx.mViewportY;
	vp.Width = ctx.mViewportW;
	vp.Height = ctx.mViewportH;
	vp.MinZ = 0;
	vp.MaxZ = 1;

	hr = dev->SetViewport(&vp);
	if (FAILED(hr)) {
		VDDEBUG_DX9DISP("VideoDisplay/DX9: Failed to set viewport! hr=%08x %s", hr, VDDispDecodeD3D9Error(hr));
		return false;
	}

	// render!
	bool validDraw = true;

	const float ustep = 1.0f / (float)(int)ctx.mSourceTexW;
	const float vstep = 1.0f / (float)(int)ctx.mSourceTexH;
	const float u0 = ctx.mSourceArea.left   * ustep;
	const float v0 = ctx.mSourceArea.top    * vstep;
	const float u1 = ctx.mSourceArea.right  * ustep;
	const float v1 = ctx.mSourceArea.bottom * vstep;

	const float invVpW = 1.f / (float)vp.Width;
	const float invVpH = 1.f / (float)vp.Height;
	const float xstep =  2.0f * invVpW;
	const float ystep = -2.0f * invVpH;

	const float x0 = -1.0f - invVpW + ctx.mOutputX * xstep;
	const float y0 =  1.0f + invVpH + ctx.mOutputY * ystep;
	const float x1 = x0 + ctx.mOutputW * xstep;
	const float y1 = y0 + ctx.mOutputH * ystep;

	if (Vertex *pvx = mpManager->LockVertices(4)) {
		vd_seh_guard_try {
			pvx[0].SetFF2(x0, y0, 0xFFFFFFFF, u0, v0, 0, 0);
			pvx[1].SetFF2(x1, y0, 0xFFFFFFFF, u1, v0, 1, 0);
			pvx[2].SetFF2(x0, y1, 0xFFFFFFFF, u0, v1, 0, 1);
			pvx[3].SetFF2(x1, y1, 0xFFFFFFFF, u1, v1, 1, 1);
		} vd_seh_guard_except {
			validDraw = false;
		}

		mpManager->UnlockVertices();
	}

	if (!validDraw) {
		VDDEBUG_DX9DISP("VideoDisplay/DX9: Invalid vertex buffer lock detected -- bailing.");
		return false;
	}

	if (!mpManager->BeginScene())
		return false;

	hr = mpManager->DrawArrays(D3DPT_TRIANGLESTRIP, 0, 2);

	if (FAILED(hr)) {
		VDDEBUG_DX9DISP("VideoDisplay/DX9: Failed to draw primitive! hr=%08x %s", hr, VDDispDecodeD3D9Error(hr));
		return false;
	}

	return true;
}

bool VDVideoDisplayDX9Manager::RunEffect(const EffectContext& ctx, const nsVDDisplay::TechniqueInfo& technique, IDirect3DSurface9 *pRTOverride) {
	mpManager->BeginScope(L"RunEffect");
	bool success = RunEffect2(ctx, technique, pRTOverride);
	mpManager->EndScope();

	return success;
}

bool VDVideoDisplayDX9Manager::RunEffect2(const EffectContext& ctx, const nsVDDisplay::TechniqueInfo& technique, IDirect3DSurface9 *pRTOverride) {
	using namespace nsVDDisplay;

	const int firstRTTIndex = 0;

	IDirect3DTexture9 *const textures[14]={
		NULL,
		ctx.mpSourceTexture1,
		ctx.mpSourceTexture2,
		ctx.mpSourceTexture3,
		ctx.mpSourceTexture4,
		ctx.mpSourceTexture5,
		ctx.mpPaletteTexture,
		mpRTTs[firstRTTIndex] ? mpRTTs[firstRTTIndex]->GetD3DTexture() : NULL,
		mpRTTs[1] ? mpRTTs[1]->GetD3DTexture() : NULL,
		mpFilterTexture ? mpFilterTexture->GetD3DTexture() : NULL,
		mpHEvenOddTexture ? mpHEvenOddTexture->GetD3DTexture() : NULL,
		ctx.mpInterpFilterH,
		ctx.mpInterpFilterV,
		ctx.mpInterpFilter
	};

	const D3DDISPLAYMODE& dmode = mpManager->GetDisplayMode();
	int clippedWidth = std::min<int>(VDCeilToInt(ctx.mOutputX + ctx.mOutputW - 0.5f), dmode.Width);
	int clippedHeight = std::min<int>(VDCeilToInt(ctx.mOutputY + ctx.mOutputH - 0.5f), dmode.Height);

	if (clippedWidth <= 0 || clippedHeight <= 0)
		return true;

	VDASSERT(ctx.mOutputW);
	VDASSERT(ctx.mOutputH);
	VDASSERT(ctx.mSourceTexW);
	VDASSERT(ctx.mSourceTexH);
	VDASSERT(ctx.mSourceW);
	VDASSERT(ctx.mSourceH);

	StdParamData data;

	data.texsize[0] = (float)(int)ctx.mSourceTexW;
	data.texsize[1] = (float)(int)ctx.mSourceTexH;
	data.texsize[2] = 1.0f / (float)(int)ctx.mSourceTexH;
	data.texsize[3] = 1.0f / (float)(int)ctx.mSourceTexW;
	data.tex2size[0] = 1.f;
	data.tex2size[1] = 1.f;
	data.tex2size[2] = 1.f;
	data.tex2size[3] = 1.f;
	data.srcsize[0] = (float)(int)ctx.mSourceW;
	data.srcsize[1] = (float)(int)ctx.mSourceH;
	data.srcsize[2] = 1.0f / (float)(int)ctx.mSourceH;
	data.srcsize[3] = 1.0f / (float)(int)ctx.mSourceW;
	data.tempsize[0] = 1.f;
	data.tempsize[1] = 1.f;
	data.tempsize[2] = 1.f;
	data.tempsize[3] = 1.f;
	data.interphtexsize[0] = (float)ctx.mInterpHTexW;
	data.interphtexsize[1] = (float)ctx.mInterpHTexH;
	data.interphtexsize[2] = ctx.mInterpHTexH ? 1.0f / (float)ctx.mInterpHTexH : 0.0f;
	data.interphtexsize[3] = ctx.mInterpHTexW ? 1.0f / (float)ctx.mInterpHTexW : 0.0f;
	data.interpvtexsize[0] = (float)ctx.mInterpVTexW;
	data.interpvtexsize[1] = (float)ctx.mInterpVTexH;
	data.interpvtexsize[2] = ctx.mInterpVTexH ? 1.0f / (float)ctx.mInterpVTexH : 0.0f;
	data.interpvtexsize[3] = ctx.mInterpVTexW ? 1.0f / (float)ctx.mInterpVTexW : 0.0f;
	data.interptexsize[0] = (float)ctx.mInterpTexW;
	data.interptexsize[1] = (float)ctx.mInterpTexH;
	data.interptexsize[2] = ctx.mInterpTexH ? 1.0f / (float)ctx.mInterpTexH : 0.0f;
	data.interptexsize[3] = ctx.mInterpTexW ? 1.0f / (float)ctx.mInterpTexW : 0.0f;
	data.srcarea[0] = ctx.mSourceArea.left;
	data.srcarea[1] = ctx.mSourceArea.top;
	data.srcarea[2] = ctx.mSourceArea.right;
	data.srcarea[3] = ctx.mSourceArea.bottom;
	data.chromauvscale[0] = ctx.mChromaScaleU;
	data.chromauvscale[1] = ctx.mChromaScaleV;
	data.chromauvscale[2] = 0.0f;
	data.chromauvscale[3] = 0.0f;
	data.chromauvoffset[0] = ctx.mChromaOffsetU;
	data.chromauvoffset[1] = ctx.mChromaOffsetV;
	data.chromauvoffset[2] = 0.0f;
	data.chromauvoffset[3] = 0.0f;
	data.pixelsharpness[0] = ctx.mPixelSharpnessX;
	data.pixelsharpness[1] = ctx.mPixelSharpnessY;
	data.pixelsharpness[2] = 0.0f;
	data.pixelsharpness[3] = 0.0f;

	if (ctx.mpSourceTexture2) {
		D3DSURFACE_DESC desc;

		HRESULT hr = ctx.mpSourceTexture2->GetLevelDesc(0, &desc);
		if (FAILED(hr))
			return false;

		float w = (float)desc.Width;
		float h = (float)desc.Height;

		data.tex2size[0] = w;
		data.tex2size[1] = h;
		data.tex2size[2] = 1.0f / h;
		data.tex2size[3] = 1.0f / w;
	}

	if (mpRTTs[firstRTTIndex]) {
		data.tempsize[0] = (float)mpRTTs[firstRTTIndex]->GetWidth();
		data.tempsize[1] = (float)mpRTTs[firstRTTIndex]->GetHeight();
		data.tempsize[2] = 1.0f / data.tempsize[1];
		data.tempsize[3] = 1.0f / data.tempsize[0];
	}

	IDirect3DDevice9 *dev = mpManager->GetDevice();
	bool rtmain = true;

	for(const PassInfo& pi : technique.mPasses) {

		// bind vertex and pixel shaders
		HRESULT hr = dev->SetVertexShader(pi.mVertexShaderIndex >= 0 ? mVertexShaders[pi.mVertexShaderIndex] : NULL);
		if (FAILED(hr)) {
			VDDEBUG_DX9DISP("VideoDisplay/DX9: Couldn't set vertex shader! hr=%08x %s", hr, VDDispDecodeD3D9Error(hr));
			return false;
		}

		hr = dev->SetPixelShader(pi.mPixelShaderIndex >= 0 ? mPixelShaders[pi.mPixelShaderIndex] : NULL);
		if (FAILED(hr)) {
			VDDEBUG_DX9DISP("VideoDisplay/DX9: Couldn't set pixel shader! hr=%08x %s", hr, VDDispDecodeD3D9Error(hr));
			return false;
		}

		if (pi.mBumpEnvScale) {
			const float scaleData[4] = {
				data.texsize[3],
				0,
				0,
				data.texsize[2],
			};

			DWORD scaleData2[4];
			memcpy(scaleData2, scaleData, sizeof scaleData2);

			for(int i=0; i<4; ++i) {
				hr = dev->SetTextureStageState(1, (D3DTEXTURESTAGESTATETYPE)(D3DTSS_BUMPENVMAT00 + i), scaleData2[i]);

				if (FAILED(hr)) {
					VDDEBUG_DX9DISP("VideoDisplay/DX9: Failed to set state! hr=%08x %s", hr, VDDispDecodeD3D9Error(hr));
					return false;
				}
			}
		}

		// set textures
		for(auto&& texb : pi.mTextureBindings) {
			VDASSERT(texb.mTexture < vdcountof(textures));

			hr = dev->SetTexture(texb.mStage, textures[texb.mTexture]);

			if (SUCCEEDED(hr))
				hr = dev->SetSamplerState(texb.mStage, D3DSAMP_ADDRESSU, texb.mbWrapU ? D3DTADDRESS_WRAP : D3DTADDRESS_CLAMP);
			
			if (SUCCEEDED(hr))
				hr = dev->SetSamplerState(texb.mStage, D3DSAMP_ADDRESSV, texb.mbWrapV ? D3DTADDRESS_WRAP : D3DTADDRESS_CLAMP);
			
			const bool bilinear = texb.mbBilinear || (texb.mbAutoBilinear && ctx.mbAutoBilinear);

			if (SUCCEEDED(hr))
				hr = dev->SetSamplerState(texb.mStage, D3DSAMP_MINFILTER, bilinear ? D3DTEXF_LINEAR : D3DTEXF_POINT);

			if (SUCCEEDED(hr))
				hr = dev->SetSamplerState(texb.mStage, D3DSAMP_MAGFILTER, bilinear ? D3DTEXF_LINEAR : D3DTEXF_POINT);

			if (SUCCEEDED(hr))
				hr = dev->SetSamplerState(texb.mStage, D3DSAMP_MIPFILTER, D3DTEXF_NONE);

			if (FAILED(hr)) {
				VDDEBUG_DX9DISP("VideoDisplay/DX9: Failed to set texture/sampler state! hr=%08x %s", hr, VDDispDecodeD3D9Error(hr));
				return false;
			}
		}

		// change render target
		if (pi.mRenderTarget >= 0) {
			if (!mpManager->EndScene())
				return false;

			HRESULT hr = E_FAIL;
			rtmain = false;

			switch(pi.mRenderTarget) {
				case 0:
					hr = dev->SetRenderTarget(0, pRTOverride ? pRTOverride : mpManager->GetRenderTarget());
					rtmain = true;
					break;
				case 1:
					if (mpRTTs[firstRTTIndex]) {
						IDirect3DSurface9 *pSurf;
						hr = mpRTTs[firstRTTIndex]->GetD3DTexture()->GetSurfaceLevel(0, &pSurf);
						if (SUCCEEDED(hr)) {
							hr = dev->SetRenderTarget(0, pSurf);
							pSurf->Release();
						}
					}
					break;
				case 2:
					if (mpRTTs[1]) {
						IDirect3DSurface9 *pSurf;
						hr = mpRTTs[1]->GetD3DTexture()->GetSurfaceLevel(0, &pSurf);
						if (SUCCEEDED(hr)) {
							hr = dev->SetRenderTarget(0, pSurf);
							pSurf->Release();
						}
					}
					break;
			}

			if (FAILED(hr)) {
				VDDEBUG_DX9DISP("VideoDisplay/DX9: Failed to set render target! hr=%08x %s", hr, VDDispDecodeD3D9Error(hr));
				return false;
			}
		}

		// change viewport
		D3DVIEWPORT9 vp { 0, 0, 1, 1, 0, 1 };
		if (pi.mViewportW | pi.mViewportH) {
			HRESULT hr;

			IDirect3DSurface9 *rt;
			hr = dev->GetRenderTarget(0, &rt);
			if (SUCCEEDED(hr)) {
				D3DSURFACE_DESC desc;
				hr = rt->GetDesc(&desc);
				if (SUCCEEDED(hr)) {
					// full, src, out, unclipped
					const DWORD hsizes[4]={ desc.Width, ctx.mSourceW, (DWORD)clippedWidth, (DWORD)ctx.mOutputW };
					const DWORD vsizes[4]={ desc.Height, ctx.mSourceH, (DWORD)clippedHeight, (DWORD)ctx.mOutputH };

					vp.X = rtmain ? ctx.mViewportX : 0;
					vp.Y = rtmain ? ctx.mViewportY : 0;
					vp.Width = hsizes[pi.mViewportW];
					vp.Height = vsizes[pi.mViewportH];
					vp.MinZ = 0;
					vp.MaxZ = 1;

					hr = dev->SetViewport(&vp);
				}
				rt->Release();
			}

			if (FAILED(hr)) {
				VDDEBUG_DX9DISP("VideoDisplay/DX9: Failed to set viewport! hr=%08x %s", hr, VDDispDecodeD3D9Error(hr));
				return false;
			}
		} else {
			vp.X = ctx.mViewportX;
			vp.Y = ctx.mViewportY;
			vp.Width = ctx.mViewportW;
			vp.Height = ctx.mViewportH;
			vp.MinZ = 0;
			vp.MaxZ = 1;

			HRESULT hr = dev->SetViewport(&vp);
			if (FAILED(hr)) {
				VDDEBUG_DX9DISP("VideoDisplay/DX9: Failed to retrieve viewport! hr=%08x %s", hr, VDDispDecodeD3D9Error(hr));
				return false;
			}
		}

		// update viewport-dependent constants
		data.vpsize[0] = (float)vp.Width;
		data.vpsize[1] = (float)vp.Height;
		data.vpsize[2] = 1.0f / (float)vp.Height;
		data.vpsize[3] = 1.0f / (float)vp.Width;
		data.outputxform[0] = ctx.mOutputW / (float)vp.Width * 2.0f;
		data.outputxform[1] = ctx.mOutputH / (float)vp.Height * -2.0f;
		data.outputxform[2] = (ctx.mOutputY - 0.5f) * (-2.0f / (float)vp.Height) + 1.0f;
		data.outputxform[3] = (ctx.mOutputX - 0.5f) * ( 2.0f / (float)vp.Width) - 1.0f;

		// upload shader constants
		hr = dev->SetVertexShaderConstantF(0, (const float *)&data, sizeof data / sizeof(float[4]));
		if (FAILED(hr)) {
			VDDEBUG_DX9DISP("VideoDisplay/DX9: Failed to upload vertex shader constants! hr=%08x %s", hr, VDDispDecodeD3D9Error(hr));
			return false;
		}

		hr = dev->SetPixelShaderConstantF(0, (const float *)&data, sizeof data / sizeof(float[4]));
		if (FAILED(hr)) {
			VDDEBUG_DX9DISP("VideoDisplay/DX9: Failed to upload pixel shader constants! hr=%08x %s", hr, VDDispDecodeD3D9Error(hr));
			return false;
		}

		// render!
		bool validDraw = true;

		const float ustep = 1.0f / (float)(int)ctx.mSourceTexW * ctx.mDefaultUVScaleCorrectionX;
		const float vstep = 1.0f / (float)(int)ctx.mSourceTexH * ctx.mDefaultUVScaleCorrectionY;
		float u0 = ctx.mSourceArea.left   * ustep;
		float v0 = ctx.mSourceArea.top    * vstep;
		float u1 = ctx.mSourceArea.right  * ustep;
		float v1 = ctx.mSourceArea.bottom * vstep;

		const float invVpW = 1.f / (float)vp.Width;
		const float invVpH = 1.f / (float)vp.Height;
		const float xstep =  2.0f * invVpW;
		const float ystep = -2.0f * invVpH;

		const float x0 = -1.0f - invVpW + (pi.mbUseOutputH ? ctx.mOutputX * xstep : 0);
		const float y0 =  1.0f + invVpH + (pi.mbUseOutputV ? ctx.mOutputY * ystep : 0);
		const float x1 = pi.mbUseOutputH ? x0 + ctx.mOutputW * xstep : 1.f - invVpW;
		const float y1 = pi.mbUseOutputV ? y0 + ctx.mOutputH * ystep : -1.f + invVpH;

		float u2 = 0;
		float u3 = 1;
		float v2 = 0;
		float v3 = 1;

		if (ctx.mbUseUV0Scale) {
			u0 *= ctx.mUV0Scale.x;
			v0 *= ctx.mUV0Scale.y;
			u1 *= ctx.mUV0Scale.x;
			v1 *= ctx.mUV0Scale.y;
		}

		if (ctx.mbUseUV1Area) {
			u2 = ctx.mUV1Area.left;
			v2 = ctx.mUV1Area.top;
			u3 = ctx.mUV1Area.right;
			v3 = ctx.mUV1Area.bottom;
		}

		const int numTiles = ctx.mOutputTessellationX * ctx.mOutputTessellationY;
		const int numVertices = (ctx.mOutputTessellationX + 1) * (ctx.mOutputTessellationY + 1);
		bool tessellated = false;
		if (ctx.mOutputTessellationX > 1 || ctx.mOutputTessellationY > 1) {
			mVertexWorkspace.resize(numVertices);

			Vertex *VDRESTRICT dst = mVertexWorkspace.data();

			for(int y = 0; y <= ctx.mOutputTessellationY; ++y) {
				const float yf1 = (float)y / (float)ctx.mOutputTessellationY;
				const float yf0 = 1.0f - yf1;

				for(int x = 0; x <= ctx.mOutputTessellationX; ++x) {
					const float xf1 = (float)x / (float)ctx.mOutputTessellationX;
					const float xf0 = 1.0f - xf1;

					(dst++)->SetFF2(
						x0*xf0 + x1*xf1,
						y0*yf0 + y1*yf1,
						0xFFFFFFFF,
						u0*xf0 + u1*xf1,
						v0*yf0 + v1*yf1,
						u2*xf0 + u3*xf1,
						v2*yf0 + v3*yf1
					);
				}
			}

			if (ctx.mVertexProcessor)
				(*ctx.mVertexProcessor)(mVertexWorkspace, data);

			if (Vertex *pvx = mpManager->LockVertices(numVertices)) {
				validDraw = VDMemcpyGuarded(pvx, mVertexWorkspace.data(), sizeof(Vertex) * numVertices);

				mpManager->UnlockVertices();
			} else {
				validDraw = false;
			}

			if (validDraw) {
				if (uint16 *idx = mpManager->LockIndices(numTiles * 6)) {
					vd_seh_guard_try {
						for(int y = 0; y < ctx.mOutputTessellationY; ++y) {
							int idx0 = y * (ctx.mOutputTessellationX + 1);
							int idx1 = idx0 + (ctx.mOutputTessellationX + 1);

							for(int x = 0; x < ctx.mOutputTessellationX; ++x, ++idx0, ++idx1) {
								*idx++ = idx0;
								*idx++ = idx1;
								*idx++ = idx0+1;
								*idx++ = idx0+1;
								*idx++ = idx1;
								*idx++ = idx1+1;
							}
						}
					} vd_seh_guard_except {
						validDraw = false;
					}

					mpManager->UnlockIndices();
				} else {
					validDraw = false;
				}
			}

			tessellated = true;
		} else {
			if (Vertex *pvx = mpManager->LockVertices(4)) {
				vd_seh_guard_try {
					pvx[0].SetFF2(x0, y0, 0xFFFFFFFF, u0, v0, u2, v2);
					pvx[1].SetFF2(x1, y0, 0xFFFFFFFF, u1, v0, u3, v2);
					pvx[2].SetFF2(x0, y1, 0xFFFFFFFF, u0, v1, u2, v3);
					pvx[3].SetFF2(x1, y1, 0xFFFFFFFF, u1, v1, u3, v3);
				} vd_seh_guard_except {
					validDraw = false;
				}

				mpManager->UnlockVertices();
			} else {
				validDraw = false;
			}
		}

		if (!validDraw) {
			VDDEBUG_DX9DISP("VideoDisplay/DX9: Invalid vertex buffer lock detected -- bailing.");
			return false;
		}

		if (!mpManager->BeginScene())
			return false;

		if (ctx.mbOutputClear) {
			hr = dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 0, 0);

			if (FAILED(hr)) {
				VDDEBUG_DX9DISP("VideoDisplay/DX9: Failed to clear target! hr=%08x %s", hr, VDDispDecodeD3D9Error(hr));
				return false;
			}
		}

		if (tessellated)
			hr = mpManager->DrawElements(D3DPT_TRIANGLELIST, 0, numVertices, 0, numTiles * 2);
		else
			hr = mpManager->DrawArrays(D3DPT_TRIANGLESTRIP, 0, 2);

		if (FAILED(hr)) {
			VDDEBUG_DX9DISP("VideoDisplay/DX9: Failed to draw primitive! hr=%08x %s", hr, VDDispDecodeD3D9Error(hr));
			return false;
		}
	}

	// NVPerfHUD 3.1 draws a bit funny if we leave this set to REVSUBTRACT, even
	// with alpha blending off....
	dev->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);

	return true;
}

///////////////////////////////////////////////////////////////////////////

class VDVideoUploadContextD3D9 final : public vdrefcounted<IVDVideoUploadContextD3D9>, public VDD3D9Client {
public:
	VDVideoUploadContextD3D9();
	~VDVideoUploadContextD3D9();

	IDirect3DTexture9 *GetD3DTexture(int i = 0) override {
		return !mpD3DConversionTextures.empty() ? mpD3DConversionTextures[i] : mpD3DImageTextures[i];
	}

	IDirect3DTexture9 *const *GetD3DTextures() {
		return !mpD3DConversionTextures.empty() ? mpD3DConversionTextures.data() : mpD3DImageTextures.data();
	}

	bool Init(void *hmonitor, bool use9ex, const VDPixmap& source, bool allowConversion, int buffers, bool use16bit) override;
	void Shutdown() override;

	void SetBufferCount(uint32 buffers);
	bool Update(const VDPixmap& source) override;

protected:
	bool Lock(IDirect3DTexture9 *tex, IDirect3DTexture9 *upload, D3DLOCKED_RECT *lr);
	bool Unlock(IDirect3DTexture9 *tex, IDirect3DTexture9 *upload);

	void OnPreDeviceReset() override;
	void OnPostDeviceReset() override;

	bool ReinitImageTextures();
	bool ReinitVRAMTextures();
	void ClearImageTexture(uint32 i);

	VDD3D9Manager	*mpManager = nullptr;
	vdrefptr<VDVideoDisplayDX9Manager> mpVideoManager;

	enum UploadMode {
		kUploadModeNormal,
		kUploadModeDirect8,
		kUploadModeDirect16
	} mUploadMode = {};

	int mBufferCount = 0;
	int	mConversionTexW = 0;
	int	mConversionTexH = 0;
	bool mbHighPrecision = false;
	bool mbPaletteTextureValid = false;
	bool mbPaletteTextureIdentity = false;

	int mSourceFmt = 0;
	D3DFORMAT mTexD3DFmt = {};
	VDPixmap			mTexFmt;
	VDPixmapCachedBlitter mCachedBlitter;

	vdfastvector<IDirect3DTexture9 *> mpD3DImageTextures;
	IDirect3DTexture9	*mpD3DImageTextureUpload = nullptr;
	IDirect3DTexture9	*mpD3DPaletteTexture = nullptr;
	IDirect3DTexture9	*mpD3DPaletteTextureUpload = nullptr;
	IDirect3DTexture9	*mpD3DImageTexture2a = nullptr;
	IDirect3DTexture9	*mpD3DImageTexture2aUpload = nullptr;
	IDirect3DTexture9	*mpD3DImageTexture2b = nullptr;
	IDirect3DTexture9	*mpD3DImageTexture2bUpload = nullptr;
	IDirect3DTexture9	*mpD3DImageTexture2c = nullptr;
	IDirect3DTexture9	*mpD3DImageTexture2cUpload = nullptr;
	IDirect3DTexture9	*mpD3DImageTexture2d = nullptr;
	IDirect3DTexture9	*mpD3DImageTexture2dUpload = nullptr;
	vdfastvector<IDirect3DTexture9 *> mpD3DConversionTextures;

	vdblock<uint32> mLastPalette;
};

bool VDCreateVideoUploadContextD3D9(IVDVideoUploadContextD3D9 **ppContext) {
	return VDRefCountObjectFactory<VDVideoUploadContextD3D9, IVDVideoUploadContextD3D9>(ppContext);
}

VDVideoUploadContextD3D9::VDVideoUploadContextD3D9() {
}

VDVideoUploadContextD3D9::~VDVideoUploadContextD3D9() {
	Shutdown();
}

bool VDVideoUploadContextD3D9::Init(void *hmonitor, bool use9ex, const VDPixmap& source, bool allowConversion, int buffers, bool use16bit) {
	mCachedBlitter.Invalidate();

	mBufferCount = buffers;

	VDASSERT(!mpManager);
	mpManager = VDInitDirect3D9(this, (HMONITOR)hmonitor, use9ex);
	if (!mpManager)
		return false;

	if (!VDInitDisplayDX9((HMONITOR)hmonitor, use9ex, ~mpVideoManager)) {
		Shutdown();
		return false;
	}

	// check capabilities
	const D3DCAPS9& caps = mpManager->GetCaps();

	if (caps.MaxTextureWidth < (uint32)source.w || caps.MaxTextureHeight < (uint32)source.h) {
		VDDEBUG_DX9DISP("VideoDisplay/DX9: source image is larger than maximum texture size");
		Shutdown();
		return false;
	}

	// create source texture
	int texw = source.w;
	int texh = source.h;

	mpManager->AdjustTextureSize(texw, texh);

	memset(&mTexFmt, 0, sizeof mTexFmt);
	mTexFmt.format		= nsVDPixmap::kPixFormat_XRGB8888;

	if (use16bit) {
		if (mpManager->IsTextureFormatAvailable(D3DFMT_R5G6B5))
			mTexFmt.format		= nsVDPixmap::kPixFormat_RGB565;
		else if (mpManager->IsTextureFormatAvailable(D3DFMT_X1R5G5B5))
			mTexFmt.format		= nsVDPixmap::kPixFormat_XRGB1555;
	}

	mSourceFmt = source.format;

	HRESULT hr;
	D3DFORMAT d3dfmt;
	IDirect3DDevice9 *dev = mpManager->GetDevice();

	mUploadMode = kUploadModeNormal;

	const bool useDefault = (mpManager->GetDeviceEx() != NULL);
	const D3DPOOL texPool = useDefault ? D3DPOOL_DEFAULT : D3DPOOL_MANAGED;

	switch(source.format) {
		case nsVDPixmap::kPixFormat_YUV420_Planar_709:
		case nsVDPixmap::kPixFormat_YUV420_Planar_FR:
		case nsVDPixmap::kPixFormat_YUV420_Planar_709_FR:
		case nsVDPixmap::kPixFormat_YUV422_Planar_709:
		case nsVDPixmap::kPixFormat_YUV422_Planar_FR:
		case nsVDPixmap::kPixFormat_YUV422_Planar_709_FR:
		case nsVDPixmap::kPixFormat_YUV444_Planar_709:
		case nsVDPixmap::kPixFormat_YUV444_Planar_FR:
		case nsVDPixmap::kPixFormat_YUV444_Planar_709_FR:
			if (mpManager->IsTextureFormatAvailable(D3DFMT_L8) && caps.PixelShaderVersion >= D3DPS_VERSION(2, 0)) {
				mUploadMode = kUploadModeDirect8;
				d3dfmt = D3DFMT_L8;

				uint32 subw = texw;
				uint32 subh = texh;

				switch(source.format) {
					case nsVDPixmap::kPixFormat_YUV444_Planar:
					case nsVDPixmap::kPixFormat_YUV444_Planar_709:
					case nsVDPixmap::kPixFormat_YUV444_Planar_FR:
					case nsVDPixmap::kPixFormat_YUV444_Planar_709_FR:
						break;
					case nsVDPixmap::kPixFormat_YUV422_Planar:
					case nsVDPixmap::kPixFormat_YUV422_Planar_709:
					case nsVDPixmap::kPixFormat_YUV422_Planar_FR:
					case nsVDPixmap::kPixFormat_YUV422_Planar_709_FR:
						subw >>= 1;
						break;
					case nsVDPixmap::kPixFormat_YUV420_Planar:
					case nsVDPixmap::kPixFormat_YUV420_Planar_709:
					case nsVDPixmap::kPixFormat_YUV420_Planar_FR:
					case nsVDPixmap::kPixFormat_YUV420_Planar_709_FR:
						subw >>= 2;
						subh >>= 2;
						break;
				}

				if (subw < 1)
					subw = 1;
				if (subh < 1)
					subh = 1;

				hr = dev->CreateTexture(subw, subh, 1, 0, D3DFMT_L8, texPool, &mpD3DImageTexture2a, NULL);
				if (FAILED(hr)) {
					Shutdown();
					return false;
				}

				hr = dev->CreateTexture(subw, subh, 1, 0, D3DFMT_L8, texPool, &mpD3DImageTexture2b, NULL);
				if (FAILED(hr)) {
					Shutdown();
					return false;
				}

				if (useDefault) {
					hr = dev->CreateTexture(subw, subh, 1, 0, D3DFMT_L8, D3DPOOL_SYSTEMMEM, &mpD3DImageTexture2aUpload, NULL);
					if (FAILED(hr)) {
						Shutdown();
						return false;
					}

					hr = dev->CreateTexture(subw, subh, 1, 0, D3DFMT_L8, D3DPOOL_SYSTEMMEM, &mpD3DImageTexture2bUpload, NULL);
					if (FAILED(hr)) {
						Shutdown();
						return false;
					}
				}
			}
			break;

		case nsVDPixmap::kPixFormat_Pal8:
		case nsVDPixmap::kPixFormat_YUV420_Planar:
		case nsVDPixmap::kPixFormat_YUV422_Planar:
		case nsVDPixmap::kPixFormat_YUV444_Planar:
			if (mpManager->IsTextureFormatAvailable(D3DFMT_L8) && caps.PixelShaderVersion >= D3DPS_VERSION(1, 1)) {
				mUploadMode = kUploadModeDirect8;
				d3dfmt = D3DFMT_L8;

				uint32 subw = texw;
				uint32 subh = texh;

				switch(source.format) {
					case nsVDPixmap::kPixFormat_Pal8:
					case nsVDPixmap::kPixFormat_YUV444_Planar:
						break;
					case nsVDPixmap::kPixFormat_YUV422_Planar:
						subw >>= 1;
						break;
					case nsVDPixmap::kPixFormat_YUV420_Planar:
						subw >>= 1;
						subh >>= 1;
						break;
				}

				if (subw < 1)
					subw = 1;
				if (subh < 1)
					subh = 1;

				if (source.format == nsVDPixmap::kPixFormat_Pal8) {
					mbPaletteTextureValid = false;
					mLastPalette.resize(256);

					hr = dev->CreateTexture(256, 1, 1, 0, D3DFMT_X8R8G8B8, texPool, &mpD3DPaletteTexture, NULL);
					if (FAILED(hr)) {
						Shutdown();
						return false;
					}

					if (useDefault) {
						hr = dev->CreateTexture(256, 1, 1, 0, D3DFMT_X8R8G8B8, D3DPOOL_SYSTEMMEM, &mpD3DPaletteTextureUpload, NULL);
						if (FAILED(hr)) {
							Shutdown();
							return false;
						}
					}
				}

				hr = dev->CreateTexture(subw, subh, 1, 0, D3DFMT_L8, texPool, &mpD3DImageTexture2a, NULL);
				if (FAILED(hr)) {
					Shutdown();
					return false;
				}

				hr = dev->CreateTexture(subw, subh, 1, 0, D3DFMT_L8, texPool, &mpD3DImageTexture2b, NULL);
				if (FAILED(hr)) {
					Shutdown();
					return false;
				}

				if (useDefault) {
					hr = dev->CreateTexture(subw, subh, 1, 0, D3DFMT_L8, D3DPOOL_SYSTEMMEM, &mpD3DImageTexture2aUpload, NULL);
					if (FAILED(hr)) {
						Shutdown();
						return false;
					}

					hr = dev->CreateTexture(subw, subh, 1, 0, D3DFMT_L8, D3DPOOL_SYSTEMMEM, &mpD3DImageTexture2bUpload, NULL);
					if (FAILED(hr)) {
						Shutdown();
						return false;
					}
				}
			}
			break;
	}
	
	if (mUploadMode == kUploadModeNormal) {
		mpVideoManager->DetermineBestTextureFormat(source.format, mTexFmt.format, d3dfmt);

		if (source.format != mTexFmt.format) {
			if (!allowConversion) {
				Shutdown();
				return false;
			}
		}
	}

	if (mUploadMode != kUploadModeNormal) {
		mpD3DImageTextures.resize(1, nullptr);
		mpD3DConversionTextures.resize(buffers, nullptr);
	} else {
		mpD3DImageTextures.resize(buffers, nullptr);
	}

	mConversionTexW = texw;
	mConversionTexH = texh;
	if (!ReinitVRAMTextures()) {
		Shutdown();
		return false;
	}

	if (mUploadMode == kUploadModeDirect16) {
		texw = (source.w + 1) >> 1;
		texh = source.h;
		mpManager->AdjustTextureSize(texw, texh);
	}

	mTexFmt.w			= texw;
	mTexFmt.h			= texh;
	mTexD3DFmt = d3dfmt;

	if (!ReinitImageTextures()) {
		Shutdown();
		return false;
	}

	if (useDefault) {
		hr = dev->CreateTexture(texw, texh, 1, 0, d3dfmt, D3DPOOL_SYSTEMMEM, &mpD3DImageTextureUpload, nullptr);
		if (FAILED(hr)) {
			Shutdown();
			return false;
		}
	}

	// clear source textures
	for(uint32 i=0; i<mpD3DImageTextures.size(); ++i) {
		ClearImageTexture(i);
	}

	VDDEBUG_DX9DISP("VideoDisplay/DX9: Init successful for %dx%d source image (%s -> %s); monitor=%p", source.w, source.h, VDPixmapGetInfo(source.format).name, VDPixmapGetInfo(mTexFmt.format).name, hmonitor);
	return true;
}

void VDVideoUploadContextD3D9::Shutdown() {
	for(IDirect3DTexture9 *&tex : mpD3DConversionTextures)
		vdsaferelease <<= tex;

	for(IDirect3DTexture9 *&tex : mpD3DImageTextures)
		vdsaferelease <<= tex;

	vdsaferelease <<= mpD3DPaletteTexture;
	vdsaferelease <<= mpD3DPaletteTextureUpload;
	vdsaferelease <<= mpD3DImageTexture2d;
	vdsaferelease <<= mpD3DImageTexture2dUpload;
	vdsaferelease <<= mpD3DImageTexture2c;
	vdsaferelease <<= mpD3DImageTexture2cUpload;
	vdsaferelease <<= mpD3DImageTexture2b;
	vdsaferelease <<= mpD3DImageTexture2bUpload;
	vdsaferelease <<= mpD3DImageTexture2a;
	vdsaferelease <<= mpD3DImageTexture2aUpload;
	vdsaferelease <<= mpD3DImageTextureUpload;

	mpVideoManager = NULL;
	if (mpManager) {
		VDDeinitDirect3D9(mpManager, this);
		mpManager = NULL;
	}
}

void VDVideoUploadContextD3D9::SetBufferCount(uint32 buffers) {
	if (buffers == 0)
		buffers = 1;

	if (mpD3DConversionTextures.empty()) {
		while(mpD3DImageTextures.size() > buffers) {
			vdsaferelease <<= mpD3DImageTextures.back();
			mpD3DImageTextures.pop_back();
		}

		mpD3DImageTextures.resize(buffers, nullptr);

		ReinitImageTextures();
	} else {
		while(mpD3DConversionTextures.size() > buffers) {
			vdsaferelease <<= mpD3DConversionTextures.back();
			mpD3DConversionTextures.pop_back();
		}

		mpD3DConversionTextures.resize(buffers, nullptr);

		ReinitVRAMTextures();
	}
}

bool VDVideoUploadContextD3D9::Update(const VDPixmap& source) {
	using namespace nsVDDisplay;

	if (mpD3DConversionTextures.size() > 1)
		std::rotate(mpD3DConversionTextures.begin(), mpD3DConversionTextures.end() - 1, mpD3DConversionTextures.end());

	if (mpD3DImageTextures.size() > 1)
		std::rotate(mpD3DImageTextures.begin(), mpD3DImageTextures.end() - 1, mpD3DImageTextures.end());

	D3DLOCKED_RECT lr;
	HRESULT hr;

	if (mpD3DPaletteTexture) {
		bool paletteValid = mbPaletteTextureValid;
		if (paletteValid) {
			if (source.palette) {
				if (memcmp(mLastPalette.data(), source.palette, sizeof(uint32)*256))
					paletteValid = false;
			} else {
				if (!mbPaletteTextureIdentity)
					paletteValid = false;
			}
		}

		if (!paletteValid) {
			if (!Lock(mpD3DPaletteTexture, mpD3DPaletteTextureUpload, &lr))
				return false;

			if (source.palette) {
				memcpy(mLastPalette.data(), source.palette, 256*4);

				mbPaletteTextureIdentity = false;
			} else {
				uint32 *dst = mLastPalette.data();
				uint32 v = 0;
				for(uint32 i=0; i<256; ++i) {
					*dst++ = v;
					v += 0x010101;
				}

				mbPaletteTextureIdentity = true;
			}

			memcpy(lr.pBits, mLastPalette.data(), 256*4);

			VDVERIFY(Unlock(mpD3DPaletteTexture, mpD3DPaletteTextureUpload));

			mbPaletteTextureValid = true;
		}
	}
	
	if (!Lock(mpD3DImageTextures[0], mpD3DImageTextureUpload, &lr))
		return false;

	mTexFmt.data		= lr.pBits;
	mTexFmt.pitch		= lr.Pitch;

	VDPixmap dst(mTexFmt);
	VDPixmap src(source);

	if (mUploadMode == kUploadModeDirect16) {
		VDMemcpyRect(dst.data, dst.pitch, src.data, src.pitch, src.w * 2, src.h);
	} else if (mUploadMode == kUploadModeDirect8) {
		VDMemcpyRect(dst.data, dst.pitch, src.data, src.pitch, src.w, src.h);
	} else {
		if (dst.w > src.w)
			dst.w = src.w;
		
		if (dst.h > src.h)
			dst.h = src.h;

		mCachedBlitter.Blit(dst, src);
	}

	VDVERIFY(Unlock(mpD3DImageTextures[0], mpD3DImageTextureUpload));

	if (mUploadMode == kUploadModeDirect8) {
		uint32 subw = source.w;
		uint32 subh = source.h;

		switch(source.format) {
			case nsVDPixmap::kPixFormat_YUV420_Planar:
			case nsVDPixmap::kPixFormat_YUV420_Planar_FR:
			case nsVDPixmap::kPixFormat_YUV420_Planar_709:
			case nsVDPixmap::kPixFormat_YUV420_Planar_709_FR:
				subw >>= 1;
				subh >>= 1;
				break;
			case nsVDPixmap::kPixFormat_YUV422_Planar:
			case nsVDPixmap::kPixFormat_YUV422_Planar_FR:
			case nsVDPixmap::kPixFormat_YUV422_Planar_709:
			case nsVDPixmap::kPixFormat_YUV422_Planar_709_FR:
				subw >>= 1;
				break;
			case nsVDPixmap::kPixFormat_YUV444_Planar:
			case nsVDPixmap::kPixFormat_YUV444_Planar_FR:
			case nsVDPixmap::kPixFormat_YUV444_Planar_709:
			case nsVDPixmap::kPixFormat_YUV444_Planar_709_FR:
				break;
		}

		if (subw < 1)
			subw = 1;
		if (subh < 1)
			subh = 1;

		if (source.format != nsVDPixmap::kPixFormat_Pal8) {
			// upload Cb plane
			if (!Lock(mpD3DImageTexture2a, mpD3DImageTexture2aUpload, &lr))
				return false;

			VDMemcpyRect(lr.pBits, lr.Pitch, source.data2, source.pitch2, subw, subh);

			VDVERIFY(Unlock(mpD3DImageTexture2a, mpD3DImageTexture2aUpload));

			// upload Cr plane
			if (!Lock(mpD3DImageTexture2b, mpD3DImageTexture2bUpload, &lr))
				return false;

			VDMemcpyRect(lr.pBits, lr.Pitch, source.data3, source.pitch3, subw, subh);

			VDVERIFY(Unlock(mpD3DImageTexture2b, mpD3DImageTexture2bUpload));
		}
	}

	if (mUploadMode != kUploadModeNormal) {
		IDirect3DDevice9 *dev = mpManager->GetDevice();
		vdrefptr<IDirect3DSurface9> rtsurface;

		hr = mpD3DConversionTextures[0]->GetSurfaceLevel(0, ~rtsurface);
		if (FAILED(hr))
			return false;

		hr = dev->SetStreamSource(0, mpManager->GetVertexBuffer(), 0, sizeof(Vertex));
		if (FAILED(hr))
			return false;

		hr = dev->SetIndices(mpManager->GetIndexBuffer());
		if (FAILED(hr))
			return false;

		hr = dev->SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX2);
		if (FAILED(hr))
			return false;

		hr = dev->SetRenderTarget(0, rtsurface);
		if (FAILED(hr))
			return false;

		static const uint32 kRenderStates[][2]={
			{	D3DRS_LIGHTING,			FALSE				},
			{	D3DRS_CULLMODE,			D3DCULL_NONE		},
			{	D3DRS_ZENABLE,			FALSE				},
			{	D3DRS_ALPHATESTENABLE,	FALSE				},
			{	D3DRS_ALPHABLENDENABLE,	FALSE				},
			{	D3DRS_STENCILENABLE,	FALSE				},
			{	D3DRS_SRGBWRITEENABLE,	FALSE				},
		};

		for(int i=0; i<sizeof(kRenderStates)/sizeof(kRenderStates[0]); ++i) {
			const uint32 (&rs)[2] = kRenderStates[i];

			hr = dev->SetRenderState((D3DRENDERSTATETYPE)rs[0], rs[1]);
			if (FAILED(hr))
				return false;
		}

		hr = dev->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, FALSE);
		if (FAILED(hr))
			return false;

		hr = dev->SetSamplerState(1, D3DSAMP_SRGBTEXTURE, FALSE);
		if (FAILED(hr))
			return false;

		bool success = false;
		if (mpManager->BeginScene()) {
			success = true;

			D3DVIEWPORT9 vp = { 0, 0, (DWORD)source.w, (DWORD)source.h, 0, 1 };
			hr = dev->SetViewport(&vp);
			if (FAILED(hr))
				success = false;

			if (success) {
				VDVideoDisplayDX9Manager::EffectContext ctx {};

				ctx.mpSourceTexture1 = mpD3DImageTextures[0];
				ctx.mpSourceTexture2 = mpD3DImageTexture2a;
				ctx.mpSourceTexture3 = mpD3DImageTexture2b;
				ctx.mpSourceTexture4 = mpD3DImageTexture2c;
				ctx.mpSourceTexture5 = mpD3DImageTexture2d;
				ctx.mpPaletteTexture = mpD3DPaletteTexture;
				ctx.mpInterpFilterH = NULL;
				ctx.mpInterpFilterV = NULL;
				ctx.mpInterpFilter = NULL;
				ctx.mSourceW = source.w;
				ctx.mSourceH = source.h;
				ctx.mSourceTexW = mTexFmt.w;
				ctx.mSourceTexH = mTexFmt.h;
				ctx.mSourceArea.set(0.0f, 0.0f, (float)source.w, (float)source.h);
				ctx.mInterpHTexW = 0;
				ctx.mInterpHTexH = 0;
				ctx.mInterpVTexW = 0;
				ctx.mInterpVTexH = 0;
				ctx.mInterpTexW = 0;
				ctx.mInterpTexH = 0;
				ctx.mViewportX = 0;
				ctx.mViewportY = 0;
				ctx.mViewportW = source.w;
				ctx.mViewportH = source.h;
				ctx.mOutputX = 0;
				ctx.mOutputY = 0;
				ctx.mOutputW = source.w;
				ctx.mOutputH = source.h;
				ctx.mDefaultUVScaleCorrectionX = 1.0f;
				ctx.mDefaultUVScaleCorrectionY = 1.0f;
				ctx.mChromaScaleU = 1.0f;
				ctx.mChromaScaleV = 1.0f;
				ctx.mChromaOffsetU = 0.0f;
				ctx.mChromaOffsetV = 0.0f;
				ctx.mbHighPrecision = mbHighPrecision;
				ctx.mPixelSharpnessX = 0.0f;
				ctx.mPixelSharpnessY = 0.0f;

				switch(source.format) {
					case nsVDPixmap::kPixFormat_YUV444_Planar_709:
						if (!mpVideoManager->RunEffect(ctx, g_technique_ycbcr_709_to_rgb_2_0, rtsurface))
							success = false;
						break;
					case nsVDPixmap::kPixFormat_YUV444_Planar_FR:
						if (!mpVideoManager->RunEffect(ctx, g_technique_ycbcr_601fr_to_rgb_2_0, rtsurface))
							success = false;
						break;
					case nsVDPixmap::kPixFormat_YUV444_Planar_709_FR:
						if (!mpVideoManager->RunEffect(ctx, g_technique_ycbcr_709fr_to_rgb_2_0, rtsurface))
							success = false;
						break;
					case nsVDPixmap::kPixFormat_YUV422_Planar_709:
						ctx.mChromaScaleU = 0.5f;
						ctx.mChromaOffsetU = -0.25f;
						if (!mpVideoManager->RunEffect(ctx, g_technique_ycbcr_709_to_rgb_2_0, rtsurface))
							success = false;
						break;
					case nsVDPixmap::kPixFormat_YUV422_Planar_FR:
						ctx.mChromaScaleU = 0.5f;
						ctx.mChromaOffsetU = -0.25f;
						if (!mpVideoManager->RunEffect(ctx, g_technique_ycbcr_601fr_to_rgb_2_0, rtsurface))
							success = false;
						break;
					case nsVDPixmap::kPixFormat_YUV422_Planar_709_FR:
						ctx.mChromaScaleU = 0.5f;
						ctx.mChromaOffsetU = -0.25f;
						if (!mpVideoManager->RunEffect(ctx, g_technique_ycbcr_709fr_to_rgb_2_0, rtsurface))
							success = false;
						break;
					case nsVDPixmap::kPixFormat_YUV420_Planar_709:
						ctx.mChromaScaleU = 0.5f;
						ctx.mChromaScaleV = 0.5f;
						ctx.mChromaOffsetU = -0.25f;
						if (!mpVideoManager->RunEffect(ctx, g_technique_ycbcr_709_to_rgb_2_0, rtsurface))
							success = false;
						break;
					case nsVDPixmap::kPixFormat_YUV420_Planar_FR:
						ctx.mChromaScaleU = 0.5f;
						ctx.mChromaScaleV = 0.5f;
						ctx.mChromaOffsetU = -0.25f;
						if (!mpVideoManager->RunEffect(ctx, g_technique_ycbcr_601fr_to_rgb_2_0, rtsurface))
							success = false;
						break;
					case nsVDPixmap::kPixFormat_YUV420_Planar_709_FR:
						ctx.mChromaScaleU = 0.5f;
						ctx.mChromaScaleV = 0.5f;
						ctx.mChromaOffsetU = -0.25f;
						if (!mpVideoManager->RunEffect(ctx, g_technique_ycbcr_709fr_to_rgb_2_0, rtsurface))
							success = false;
						break;

					case nsVDPixmap::kPixFormat_Pal8:
						if (mpVideoManager->IsPS20Enabled()) {
							if (!mpVideoManager->RunEffect(ctx, g_technique_pal8_to_rgb_2_0, rtsurface))
								success = false;
						} else {
							if (!mpVideoManager->RunEffect(ctx, g_technique_pal8_to_rgb_1_1, rtsurface))
								success = false;
						}
						break;

					default:
						switch(source.format) {
							case nsVDPixmap::kPixFormat_YUV444_Planar:
								if (!mpVideoManager->RunEffect(ctx, g_technique_ycbcr_601_to_rgb_2_0, rtsurface))
									success = false;
								break;

							case nsVDPixmap::kPixFormat_YUV422_Planar:
								ctx.mChromaScaleU = 0.5f;
								ctx.mChromaOffsetU = -0.25f;
								if (!mpVideoManager->RunEffect(ctx, g_technique_ycbcr_601_to_rgb_2_0, rtsurface))
									success = false;
								break;

							case nsVDPixmap::kPixFormat_YUV420_Planar:
								ctx.mChromaScaleU = 0.5f;
								ctx.mChromaScaleV = 0.5f;
								ctx.mChromaOffsetU = -0.25f;
								if (!mpVideoManager->RunEffect(ctx, g_technique_ycbcr_601_to_rgb_2_0, rtsurface))
									success = false;
								break;
						}
						break;
				}
			}

			if (!mpManager->EndScene())
				success = false;
		}

		dev->SetRenderTarget(0, mpManager->GetRenderTarget());

		return success;
	}

	return true;
}

bool VDVideoUploadContextD3D9::Lock(IDirect3DTexture9 *tex, IDirect3DTexture9 *upload, D3DLOCKED_RECT *lr) {
	HRESULT hr = (upload ? upload : tex)->LockRect(0, lr, NULL, 0);

	return SUCCEEDED(hr);
}

bool VDVideoUploadContextD3D9::Unlock(IDirect3DTexture9 *tex, IDirect3DTexture9 *upload) {
	HRESULT hr;

	if (upload) {
		hr = upload->UnlockRect(0);
		if (FAILED(hr))
			return false;

		hr = mpManager->GetDevice()->UpdateTexture(upload, tex);
	} else {
		hr = tex->UnlockRect(0);
	}

	return SUCCEEDED(hr);
}

void VDVideoUploadContextD3D9::OnPreDeviceReset() {
	for(IDirect3DTexture9 *&tex : mpD3DConversionTextures)
		vdsaferelease <<= tex;
}

void VDVideoUploadContextD3D9::OnPostDeviceReset() {
	ReinitVRAMTextures();
}

bool VDVideoUploadContextD3D9::ReinitImageTextures() {
	for(IDirect3DTexture9 *& tex : mpD3DImageTextures) {
		if (!tex) {
			const bool useDefault = (mpManager->GetDeviceEx() != NULL);
			const D3DPOOL texPool = useDefault ? D3DPOOL_DEFAULT : D3DPOOL_MANAGED;

			HRESULT hr = mpManager->GetDevice()->CreateTexture(mTexFmt.w, mTexFmt.h, 1, 0, mTexD3DFmt, texPool, &tex, NULL);
			if (FAILED(hr))
				return false;
		}
	}

	return true;
}

bool VDVideoUploadContextD3D9::ReinitVRAMTextures() {
	if (mUploadMode != kUploadModeNormal) {
		IDirect3DDevice9 *dev = mpManager->GetDevice();

		for(IDirect3DTexture9 *&tex : mpD3DConversionTextures) {
			if (!tex) {
				HRESULT hr = dev->CreateTexture(mConversionTexW, mConversionTexH, 1, D3DUSAGE_RENDERTARGET, D3DFMT_X8R8G8B8, D3DPOOL_DEFAULT, &tex, NULL);
				if (FAILED(hr))
					return false;

				mpManager->ClearRenderTarget(tex);
			}
		}
	}

	return true;
}

void VDVideoUploadContextD3D9::ClearImageTexture(uint32 i) {
	if (!mpD3DImageTextures[i])
		return;

	uint32 texw = mTexFmt.w;
	uint32 texh = mTexFmt.h;

	IDirect3DTexture9 *pDstTex = mpD3DImageTextures[i];
	IDirect3DTexture9 *pSrcTex = mpD3DImageTextureUpload;

	if (!pSrcTex)
		pSrcTex = pDstTex;
		
	D3DLOCKED_RECT lr;
	if (FAILED(pSrcTex->LockRect(0, &lr, NULL, 0)))
		return;

	switch(mSourceFmt) {
		case nsVDPixmap::kPixFormat_YUV444_Planar:
		case nsVDPixmap::kPixFormat_YUV444_Planar_709:
		case nsVDPixmap::kPixFormat_YUV422_Planar:
		case nsVDPixmap::kPixFormat_YUV422_Planar_709:
		case nsVDPixmap::kPixFormat_YUV420_Planar:
		case nsVDPixmap::kPixFormat_YUV420_Planar_709:
			VDMemset8Rect(lr.pBits, lr.Pitch, 0x10, texw, texh);
			break;
		case nsVDPixmap::kPixFormat_XRGB1555:
		case nsVDPixmap::kPixFormat_RGB565:
			VDMemset16Rect(lr.pBits, lr.Pitch, 0, texw, texh);
			break;
		case nsVDPixmap::kPixFormat_YUV444_Planar_FR:
		case nsVDPixmap::kPixFormat_YUV444_Planar_709_FR:
		case nsVDPixmap::kPixFormat_YUV422_Planar_FR:
		case nsVDPixmap::kPixFormat_YUV422_Planar_709_FR:
		case nsVDPixmap::kPixFormat_YUV420_Planar_FR:
		case nsVDPixmap::kPixFormat_YUV420_Planar_709_FR:
		case nsVDPixmap::kPixFormat_Pal8:
		case nsVDPixmap::kPixFormat_Y8:
		case nsVDPixmap::kPixFormat_Y8_FR:
			VDMemset8Rect(lr.pBits, lr.Pitch, 0, texw, texh);
			break;
		default:
			VDMemset32Rect(lr.pBits, lr.Pitch, 0, texw, texh);
			break;
	}

	pSrcTex->UnlockRect(0);

	if (pSrcTex != pDstTex)
		mpManager->GetDevice()->UpdateTexture(pSrcTex, pDstTex);
}

///////////////////////////////////////////////////////////////////////////

class VDVideoDisplayMinidriverDX9 final : public VDVideoDisplayMinidriver, public IVDDisplayCompositionEngine, protected VDD3D9Client {
public:
	VDVideoDisplayMinidriverDX9(bool clipToMonitor, bool use9ex);
	~VDVideoDisplayMinidriverDX9();

protected:
	bool PreInit(HWND hwnd, HMONITOR hmonitor) override;
	bool Init(HWND hwnd, HMONITOR hmonitor, const VDVideoDisplaySourceInfo& info) override;
	void Shutdown() override;

	bool ModifySource(const VDVideoDisplaySourceInfo& info) override;

	bool IsValid() override;
	bool IsScreenFXSupported() const override { return mbScreenFXSupported; }
	bool IsFramePending() override { return mbSwapChainPresentPending; }
	void SetFilterMode(FilterMode mode) override;
	void SetFullScreen(bool fs, uint32 w, uint32 h, uint32 refresh, bool use16bit) override;
	bool SetScreenFX(const VDVideoDisplayScreenFXInfo *screenFX) override;

	bool Tick(int id) override;
	void Poll() override;
	bool Resize(int w, int h) override;
	bool Invalidate() override;
	void PresentQueued() override { Poll(); }
	bool Update(UpdateMode) override;
	void Refresh(UpdateMode) override;
	bool Paint(HDC hdc, const RECT& rClient, UpdateMode mode) override;

	void SetLogicalPalette(const uint8 *pLogicalPalette) override;
	bool AreVSyncTicksNeeded() const override { return (mbSwapChainVsync && !mbSwapChainVsyncEvent) || (mbFullScreen && mbSwapChainPresentPolling); }
	float GetSyncDelta() const override { return mSyncDelta; }

	IVDDisplayCompositionEngine *GetDisplayCompositionEngine() override { return this; }

public:
	void LoadCustomEffect(const wchar_t *path) override;

protected:
	void OnPreDeviceReset() override;
	void OnPostDeviceReset() override {}

	void OnVsyncEvent(IVDVideoDisplayMinidriverCallback *cb);

	void InitBicubic();
	void ShutdownBicubic();
	bool InitBicubicPS2Filters(const vdrect32f& dstArea, const vdrect32f& srcArea);
	void ShutdownBicubicPS2Filters();
	bool InitBoxlinearPS11Filters(float x, float w, float y, float h, int vpw, int vph, float facx, float facy);
	void ShutdownBoxlinearPS11Filters();

	bool UpdateSwapChain();
	bool UpdateSwapChain(int w, int h, int displayW, int displayH);
	bool UpdateBackbuffer(const RECT& rClient, UpdateMode updateMode);
	bool UpdateScreen(const RECT& rClient, UpdateMode updateMode, bool polling);

	void DrawDebugInfo(FilterMode mode, const RECT& rClient);

	HWND				mhwnd = nullptr;
	VDD3D9Manager		*mpManager = nullptr;
	vdrefptr<VDVideoDisplayDX9Manager>	mpVideoManager;
	IDirect3DDevice9	*mpD3DDevice = nullptr;			// weak ref
	vdrefptr<IDirect3DTexture9>	mpD3DInterpFilterTextureH;
	vdrefptr<IDirect3DTexture9>	mpD3DInterpFilterTextureV;
	vdrefptr<IDirect3DTexture9>	mpD3DInterpFilterTexture;

	vdrect32f mCubicSrcArea { 0, 0, 0, 0 };
	vdrect32f mCubicDstArea { 0, 0, 0, 0 };
	float	mCubicFilterHTexScale = 0;
	float	mCubicFilterHTexOffset = 0;
	float	mCubicFilterVTexScale = 0;
	float	mCubicFilterVTexOffset = 0;

	float	mInterpFilterHPosDst = 0;		// output x
	float	mInterpFilterHSizeDst = 0;		// output w
	int		mInterpFilterHSizeSrc = 0;		// source width
	int		mInterpFilterHTexSize = 0;		// cubic horiz texture size or boxlinear texture width
	float	mInterpFilterVPosDst = 0;		// output y
	float	mInterpFilterVSizeDst = 0;		// output h
	int		mInterpFilterVSizeSrc = 0;		// source height
	int		mInterpFilterVTexSize = 0;		// cubic vert texture size or boxlinear texture height
	vdrect32f	mInterpFilterCoords { 0, 0, 0, 0 };
	int		mInterpFilterViewportW = 0;
	int		mInterpFilterViewportH = 0;
	float	mInterpFilterFactorX = 0;
	float	mInterpFilterFactorY = 0;

	vdrefptr<IDirect3DTexture9> mpD3DScanlineMaskTexture;
	uint32 mCachedScanlineSrcH = 0;
	uint32 mCachedScanlineDstH = 0;
	float mCachedScanlineOutY = 0;
	float mCachedScanlineOutH = 0;
	float mCachedScanlineIntensity = 0;
	bool mbCachedScanlineWrapMapped = false;

	vdrefptr<IDirect3DTexture9> mpD3DScreenMaskTexture;
	uint32 mCachedScreenMaskSrcW = 0;
	uint32 mCachedScreenMaskSrcH = 0;
	float mCachedScreenMaskDstW = 0;
	float mCachedScreenMaskDstH = 0;
	float mCachedScreenMaskOutX = 0;
	float mCachedScreenMaskOutY = 0;
	float mCachedScreenMaskOutW = 0;
	float mCachedScreenMaskOutH = 0;
	int mScreenMaskTexW = 1;
	int mScreenMaskTexH = 1;
	VDDScreenMaskParams mCachedScreenMaskParams {};
	float mCachedMaskHScale = 0;
	float mCachedMaskHBase = 0;
	float mCachedMaskVScale = 0;
	float mCachedMaskVBase = 0;

	vdrefptr<IDirect3DTexture9> mpD3DGammaRampTexture;
	float mCachedGamma = 0;
	float mCachedOutputGamma = 0;
	bool mbCachedGammaHasInputConversion = false;

	vdrefptr<IDirect3DTexture9> mpD3DBloomInputRTT;
	vdrefptr<IDirect3DTexture9> mpD3DBloomConvRTT;
	vdrefptr<IDirect3DTexture9> mpD3DBloomPyramid1RTT[6];
	vdrefptr<IDirect3DTexture9> mpD3DBloomPyramid2RTT[5];
	vdfloat2 mBloomPyramidSizeF[6] {};
	vdsize32 mBloomInputSize {};
	vdsize32 mBloomConvSize {};
	vdsize32 mBloomPyramidSize[6] {};
	vdsize32 mBloomInputTexSize {};
	vdsize32 mBloomConvTexSize {};
	vdsize32 mBloomPyramidTexSize[6] {};

	vdrefptr<IDirect3DTexture9> mpD3DArtifactingRTT;
	uint32 mCachedArtifactingW = 0;
	uint32 mCachedArtifactingH = 0;
	uint32 mCachedArtifactingTexW = 0;
	uint32 mCachedArtifactingTexH = 0;

	vdrefptr<VDVideoUploadContextD3D9>	mpUploadContext;
	vdrefptr<IVDDisplayRendererD3D9>	mpRenderer;
	vdrefptr<IVDDisplayFont> mpDebugFont;
	bool mbDebugFontCreated = false;

	vdrefptr<IVDD3D9SwapChain>	mpSwapChain;
	int					mSwapChainW = 0;
	int					mSwapChainH = 0;
	bool				mbSwapChainImageValid = false;
	bool				mbSwapChainPresentPending = false;
	bool				mbSwapChainPresentPolling = false;
	bool				mbSwapChainVsync = false;
	bool				mbSwapChainVsyncEvent = false;
	bool				mbSwapChainVsyncEventWaiting = false;
	VDAtomicBool		mbSwapChainVsyncEventPending = false;
	bool				mbFirstPresent = true;
	bool				mbFullScreen = false;
	bool				mbFullScreenSet = false;
	uint32				mFullScreenWidth = 0;
	uint32				mFullScreenHeight = 0;
	uint32				mFullScreenRefreshRate = 0;
	bool				mbFullScreen16Bit = false;
	const bool			mbClipToMonitor;

	VDVideoDisplayDX9Manager::CubicMode	mCubicMode;
	bool				mbCubicInitialized = false;
	bool				mbCubicAttempted = false;
	bool				mbCubicTempSurfacesInitialized = false;
	bool				mbBoxlinearCapable11 = false;
	bool				mbScreenFXSupported = false;
	bool				mbBloomSupported = false;
	const bool			mbUseD3D9Ex;

	bool mbUseScreenFX = false;
	VDVideoDisplayScreenFXInfo mScreenFXInfo {};

	FilterMode			mPreferredFilter = kFilterAnySuitable;
	float				mSyncDelta = 0;
	VDD3DPresentHistory	mPresentHistory;

	VDPixmap					mTexFmt;

	VDVideoDisplaySourceInfo	mSource;

	VDStringA		mFormatString;
	VDStringA		mDebugString;
	VDStringA		mErrorString;

	vdrefptr<IVDDisplayCustomEffectD3D9> mpCustomPipeline;
};

IVDVideoDisplayMinidriver *VDCreateVideoDisplayMinidriverDX9(bool clipToMonitor, bool use9ex) {
	return new VDVideoDisplayMinidriverDX9(clipToMonitor, use9ex);
}

VDVideoDisplayMinidriverDX9::VDVideoDisplayMinidriverDX9(bool clipToMonitor, bool use9ex)
	: mbClipToMonitor(clipToMonitor)
	, mbUseD3D9Ex(use9ex)
{
}

VDVideoDisplayMinidriverDX9::~VDVideoDisplayMinidriverDX9() {
	Shutdown();
}

bool VDVideoDisplayMinidriverDX9::PreInit(HWND hwnd, HMONITOR hmonitor) {
	VDASSERT(!mpManager);

	mhwnd = hwnd;
	mbFullScreenSet = false;

	mpManager = VDInitDirect3D9(this, hmonitor, mbUseD3D9Ex);
	if (!mpManager) {
		Shutdown();
		return false;
	}

	if (!VDInitDisplayDX9(hmonitor, mbUseD3D9Ex, ~mpVideoManager)) {
		Shutdown();
		return false;
	}

	mbScreenFXSupported = false;
	mbBloomSupported = false;

	if (mpVideoManager->IsPS20Enabled()) {
		mbScreenFXSupported = true;

		// Check if sRGB read/write is supported. This is in all likelihood supported for
		// any PS2.0+ card since the following all of the baseline PS2 cards support it:
		//
		//	- Intel Mobile 945 (GMA 950)
		//	- NVIDIA GeForce FX
		//	- ATI Radeon 9800
		//
		// Nevertheless, double-check just to be sure since it's not guaranteed by D3D9.

		IDirect3D9 *d3d9 = mpManager->GetD3D();
		HRESULT hr = d3d9->CheckDeviceFormat(mpManager->GetAdapter(), mpManager->GetDeviceType(), mpManager->GetDisplayMode().Format, D3DUSAGE_QUERY_SRGBREAD, D3DRTYPE_TEXTURE, D3DFMT_X8R8G8B8);
		if (SUCCEEDED(hr)) {
			hr = d3d9->CheckDeviceFormat(mpManager->GetAdapter(), mpManager->GetDeviceType(), mpManager->GetDisplayMode().Format, D3DUSAGE_QUERY_SRGBWRITE, D3DRTYPE_TEXTURE, D3DFMT_X8R8G8B8);
			if (SUCCEEDED(hr))
				mbBloomSupported = true;
		}
	}

	return true;
}

bool VDVideoDisplayMinidriverDX9::Init(HWND hwnd, HMONITOR hmonitor, const VDVideoDisplaySourceInfo& info) {
	VDASSERT(mpVideoManager);

	mSource = info;

	// attempt to initialize D3D9
	if (mbFullScreen && !mbFullScreenSet) {
		mbFullScreenSet = true;
		mpManager->AdjustFullScreen(true, mFullScreenWidth, mFullScreenHeight, mFullScreenRefreshRate, mbFullScreen16Bit, mhwnd);
	}

	mpD3DDevice = mpManager->GetDevice();

	if (!VDCreateDisplayRendererD3D9(~mpRenderer)) {
		Shutdown();
		return false;
	}

	mpRenderer->Init(mpManager, mpVideoManager);

	mpUploadContext = new_nothrow VDVideoUploadContextD3D9;
	const uint32 bufferCount = mpCustomPipeline ? mpCustomPipeline->GetMaxPrevFrames() + 1 : 1;
	if (!mpUploadContext || !mpUploadContext->Init(hmonitor, mbUseD3D9Ex, info.pixmap, info.bAllowConversion, bufferCount, info.use16bit)) {
		Shutdown();
		return false;
	}

	mSyncDelta = 0.0f;
	mbFirstPresent = true;

	mbBoxlinearCapable11 = mpVideoManager->IsPS11Enabled();

	mErrorString.clear();

	// Pre-create windowed swap chains immediately so we can report now if the device won't work. This is
	// needed because Wine has the goofy habit of silently reporting a software emulated D3D9 device with
	// only 1MB of VRAM, which tends to fail only once we try to create the swap chain. This forces an
	// early failure so we know it's unrecoverable.

	return UpdateSwapChain();
}

void VDVideoDisplayMinidriverDX9::LoadCustomEffect(const wchar_t *path) {
	mErrorString.clear();

	if (path && *path) {
		try {
			mpCustomPipeline = VDDisplayParseCustomShaderPipeline(mpManager, path);

			if (mpUploadContext)
				mpUploadContext->SetBufferCount(mpCustomPipeline->GetMaxPrevFrames() + 1);
		} catch(const MyError& e) {
			mErrorString = e.c_str();

			if (mpUploadContext)
				mpUploadContext->SetBufferCount(1);
		}
	} else {
		mpCustomPipeline = nullptr;

		if (mpUploadContext)
			mpUploadContext->SetBufferCount(1);
	}
}

void VDVideoDisplayMinidriverDX9::OnPreDeviceReset() {
	vdsaferelease <<= mpD3DBloomInputRTT;
	vdsaferelease <<= mpD3DBloomConvRTT;

	for(auto& pyramidTex : mpD3DBloomPyramid1RTT)
		vdsaferelease <<= pyramidTex;

	for(auto& pyramidTex : mpD3DBloomPyramid2RTT)
		vdsaferelease <<= pyramidTex;

	ShutdownBicubic();
	ShutdownBicubicPS2Filters();
	ShutdownBoxlinearPS11Filters();
	mpSwapChain = NULL;
	mSwapChainW = 0;
	mSwapChainH = 0;
	mbSwapChainImageValid = false;
	mbSwapChainVsync = false;
	mbSwapChainVsyncEvent = false;
}

void VDVideoDisplayMinidriverDX9::OnVsyncEvent(IVDVideoDisplayMinidriverCallback *cb) {
	if (mbSwapChainVsyncEventPending.xchg(false))
		mSource.mpCB->QueuePresent();
}

void VDVideoDisplayMinidriverDX9::InitBicubic() {
	if (mbCubicInitialized || mbCubicAttempted)
		return;

	mbCubicAttempted = true;

	mCubicMode = mpVideoManager->InitBicubic();

	if (mCubicMode == VDVideoDisplayDX9Manager::kCubicNotPossible)
		return;

	VDASSERT(!mbCubicTempSurfacesInitialized);
	mbCubicTempSurfacesInitialized = mpVideoManager->InitBicubicTempSurfaces();
	if (!mbCubicTempSurfacesInitialized) {
		mpVideoManager->ShutdownBicubic();
		mCubicMode = VDVideoDisplayDX9Manager::kCubicNotPossible;
		return;
	}

	VDDEBUG_DX9DISP("VideoDisplay/DX9: Bicubic initialization complete.");
	mbCubicInitialized = true;
}

void VDVideoDisplayMinidriverDX9::ShutdownBicubic() {
	if (mbCubicInitialized) {
		mbCubicInitialized = mbCubicAttempted = false;

		if (mbCubicTempSurfacesInitialized) {
			mbCubicTempSurfacesInitialized = false;

			mpVideoManager->ShutdownBicubicTempSurfaces();
		}

		mpVideoManager->ShutdownBicubic();
	}
}

///////////////////////////////////////////////////////////////////////////

namespace {
	struct PS2CubicInfo {
		int mTexSize = -1;

		// 1D transform from normalized 0-1 to texture lookup U.
		float mScale = 0;
		float mOffset = 0;

		bool IsValid() const { return mTexSize > 0; }
	};

	PS2CubicInfo GeneratePS2CubicTexture(VDD3D9Manager *pManager, float dstx1, float dstx2, float srcx1, float srcx2, vdrefptr<IDirect3DTexture9>& pTexture, int existingTexW) {
		IDirect3DDevice9 *dev = pManager->GetDevice();

		// pixel snap destination
		const int clipx1 = VDCeilToInt(dstx1 - 0.5f);
		const int clipx2 = std::max<int>(VDCeilToInt(dstx2 - 0.5f), clipx1 + 1);
		const int clipw = clipx2 - clipx1;

		// Round up to next multiple of 128 pixels to reduce reallocation.
		int texw = (clipw + 127) & ~127;
		int texh = 1;
		pManager->AdjustTextureSize(texw, texh);

		// If we can't fit the texture, bail.
		if (texw < clipw)
			return {};

		// Check if we need to reallocate the texture.
		HRESULT hr;
		D3DFORMAT format = D3DFMT_A8R8G8B8;
		const bool useDefault = (pManager->GetDeviceEx() != NULL);

		if (!pTexture || existingTexW != texw) {
			hr = dev->CreateTexture(texw, texh, 1, 0, format, useDefault ? D3DPOOL_DEFAULT : D3DPOOL_MANAGED, ~pTexture, NULL);
			if (FAILED(hr))
				return {};
		}

		vdrefptr<IDirect3DTexture9> uploadtex;
		if (useDefault) {
			hr = dev->CreateTexture(texw, texh, 1, 0, format, D3DPOOL_SYSTEMMEM, ~uploadtex, NULL);
			if (FAILED(hr))
				return {};
		} else {
			uploadtex = pTexture;
		}

		// Fill the texture.
		D3DLOCKED_RECT lr;
		hr = uploadtex->LockRect(0, &lr, NULL, 0);
		VDASSERT(SUCCEEDED(hr));
		if (FAILED(hr)) {
			VDDEBUG_DX9DISP("VideoDisplay/DX9: Failed to load bicubic texture.");
			return {};
		}

		const double dstw = dstx2 - dstx1;
		const double srcw = srcx2 - srcx1;
		double dudx = (double)srcw / (double)dstw;
		double u = srcx1 + dudx * (0.5 + (clipx1 - dstx1));
		double u0 = srcx1 + 0.5;
		double ud0 = srcx1 + 1.5;
		double ud1 = srcx2 - 1.5;
		double u1 = srcx2 - 0.5;
		uint32 *p0 = (uint32 *)lr.pBits;

		for(int x = 0; x < texw; ++x) {
			double ut = u;
			if (ut < u0)
				ut = u0;
			else if (ut > u1)
				ut = u1;
			int ix = VDFloorToInt(ut - 0.5);
			double d = ut - ((double)ix + 0.5);

			static const double m = -0.75;
			double c0 = (( (m    )*d - 2.0*m    )*d +   m)*d;
			double c1 = (( (m+2.0)*d -     m-3.0)*d      )*d + 1.0;
			double c2 = ((-(m+2.0)*d + 2.0*m+3.0)*d -   m)*d;
			double c3 = ((-(m    )*d +     m    )*d      )*d;

			double k1 = d < 0.5 ? d < 1e-5 ? -m : c2 / d : d > 1-1e-5 ? -m : c1 / (1-d);
			double kx = d < 0.5 ? c1 - k1*(1-d) : c2 - k1*d;

			// if we're too close to the source edge, kill cubic filtering to prevent edge artifacts
			if (ut < ud0 || ut > ud1) {
				c0 = 0;
				k1 = 1.0;
				kx = 0.0;
				c3 = 0;
			}

			double blue		= -c0*4;
			double green	= k1 - 1.0 + 128.0f/255.0f;
			double red		= kx * 2;
			double alpha	= -c3*4;

			blue = fabs(c0 + c3) > 1e-9 ? c0 / (c0 + c3) : 0;
			green = fabs(green + red) > 1e-9 ? green / (green + red) : 0;
			red = fabs(c0 + c3);

			// The rounding here is a bit tricky. Here's how we use the values:
			//	r = p2 * (g + 0.5) + p3 * (r / 2) - p1 * (b / 4) - p4 * (a / 4)
			//
			// Which means we need:
			//	g + 0.5 + r/2 - b/4 - a/4 = 1
			//	g + r/2 - b/4 - a/4 = 0.5
			//	g*4 + r*2 - (b + a) = 2 (510 / 1020)

			uint8 ib = VDClampedRoundFixedToUint8Fast((float)blue);
			uint8 ig = VDClampedRoundFixedToUint8Fast((float)green);
			uint8 ir = VDClampedRoundFixedToUint8Fast((float)red);
			uint8 ia = VDClampedRoundFixedToUint8Fast((float)alpha);

			p0[x] = (uint32)ib + ((uint32)ig << 8) + ((uint32)ir << 16) + ((uint32)ia << 24);

			u += dudx;
		}

		VDVERIFY(SUCCEEDED(uploadtex->UnlockRect(0)));

		if (useDefault) {
			hr = pManager->GetDevice()->UpdateTexture(uploadtex, pTexture);
			if (FAILED(hr)) {
				pTexture.clear();
				return {};
			}
		}

		PS2CubicInfo info;
		info.mTexSize = texw;

		// The clip range is mapped 1:1 to texels at the left of the texture.
		info.mScale = (float)clipw / (float)texw;
		info.mOffset = -info.mScale * ((float)clipx1 - dstx1);
		return info;
	}

	struct PS11BoxlinearInfo {
		int mTexWidth = -1;
		int mTexHeight = -1;
	};

	PS11BoxlinearInfo GeneratePS11BoxlinearTexture(VDD3D9Manager *pManager, int w, int h, int outw, int outh, int srcw, int srch, float offsetx, float offsety, float facx, float facy, vdrefptr<IDirect3DTexture9>& pTexture, int existingTexW, int existingTexH) {
		IDirect3DDevice9 *dev = pManager->GetDevice();

		// Round up to next multiple of 128 pixels to reduce reallocation.
		int texw = (w + 127) & ~127;
		int texh = (h + 127) & ~127;
	
		pManager->AdjustTextureSize(texw, texh);

		// If we can't fit the texture, bail.
		if (texw < w || texh < h)
			return {};

		// Check if we need to reallocate the texture.
		HRESULT hr;
		const bool useDefault = (pManager->GetDeviceEx() != NULL);

		if (!pTexture || existingTexW != texw || existingTexH != texh) {
			hr = dev->CreateTexture(texw, texh, 1, 0, D3DFMT_V8U8, useDefault ? D3DPOOL_DEFAULT : D3DPOOL_MANAGED, ~pTexture, NULL);
			if (FAILED(hr))
				return {};
		}

		vdrefptr<IDirect3DTexture9> uploadtex;
		if (useDefault) {
			hr = dev->CreateTexture(texw, texh, 1, 0, D3DFMT_V8U8, D3DPOOL_SYSTEMMEM, ~uploadtex, NULL);
			if (FAILED(hr))
				return {};
		} else {
			uploadtex = pTexture;
		}

		// Fill the texture.
		D3DLOCKED_RECT lr;
		hr = uploadtex->LockRect(0, &lr, NULL, 0);
		VDASSERT(SUCCEEDED(hr));
		if (FAILED(hr)) {
			VDDEBUG_DX9DISP("VideoDisplay/DX9: Failed to load boxlinear texture.");
			return {};
		}

		double dudx = (double)srcw / (double)outw;
		double dvdy = (double)srch / (double)outh;
		double u = dudx * (offsetx + 0.5);
		double v = dvdy * (offsety + 0.5);

		vdfastvector<sint8> hfilter(texw);
		for(int x = 0; x < texw; ++x) {
			double edgePos = floor(u + 0.5);
			double snappedPos = edgePos - fmax(-0.5f, fmin((edgePos - u) * facx, 0.5));
			hfilter[x] = (sint8)floor(0.5 + 127.0 * (snappedPos - u));
			u += dudx;
		}

		vdfastvector<sint8> vfilter(texh);
		for(int y = 0; y < texh; ++y) {
			double edgePos = floor(v + 0.5);
			double snappedPos = edgePos - fmax(-0.5f, fmin((edgePos - v) * facy, 0.5));
			vfilter[y] = (sint8)floor(0.5 + 127.0 * (snappedPos - v));
			v += dvdy;
		}

		sint8 *p0 = (sint8 *)lr.pBits;

		for(int y = 0; y < texh; ++y) {
			sint8 *p = p0;
			sint8 vc = vfilter[y];

			for(int x = 0; x < texw; ++x) {
				p[0] = hfilter[x];
				p[1] = vc;
				p += 2;
			}

			p0 += lr.Pitch;
		}

		VDVERIFY(SUCCEEDED(uploadtex->UnlockRect(0)));

		if (useDefault) {
			hr = pManager->GetDevice()->UpdateTexture(uploadtex, pTexture);
			if (FAILED(hr)) {
				pTexture.clear();
				return {};
			}
		}

		PS11BoxlinearInfo info;
		info.mTexWidth = texw;
		info.mTexHeight = texh;
		return info;
	}
}

bool VDVideoDisplayMinidriverDX9::InitBicubicPS2Filters(const vdrect32f& dstArea, const vdrect32f& srcArea) {
	// requires PS2.0 path
	if (mCubicMode != VDVideoDisplayDX9Manager::kCubicUsePS2_0Path)
		return false;

	// update horiz filter
	if (!mpD3DInterpFilterTextureH ||
		mCubicDstArea.left != dstArea.left ||
		mCubicDstArea.right != dstArea.right ||
		mCubicSrcArea.left != srcArea.left ||
		mCubicSrcArea.right != srcArea.right)
	{
		const auto hinfo = GeneratePS2CubicTexture(mpManager, dstArea.left, dstArea.right, srcArea.left, srcArea.right, mpD3DInterpFilterTextureH, mInterpFilterHTexSize);
		if (!hinfo.IsValid())
			return false;

		mInterpFilterHTexSize = hinfo.mTexSize;
		mCubicFilterHTexOffset = hinfo.mOffset;
		mCubicFilterHTexScale = hinfo.mScale;
	}

	// update vert filter
	if (!mpD3DInterpFilterTextureV ||
		mCubicDstArea.top != dstArea.top ||
		mCubicDstArea.bottom != dstArea.bottom ||
		mCubicSrcArea.top != srcArea.top ||
		mCubicSrcArea.bottom != srcArea.bottom)
	{
		const auto vinfo = GeneratePS2CubicTexture(mpManager, dstArea.top, dstArea.bottom, srcArea.top, srcArea.bottom, mpD3DInterpFilterTextureV, mInterpFilterVTexSize);
		if (!vinfo.IsValid())
			return false;

		mInterpFilterVTexSize = vinfo.mTexSize;
		mCubicFilterVTexOffset = vinfo.mOffset;
		mCubicFilterVTexScale = vinfo.mScale;
	}

	mCubicDstArea = dstArea;
	mCubicSrcArea = srcArea;

	return true;
}

void VDVideoDisplayMinidriverDX9::ShutdownBicubicPS2Filters() {
	mpD3DInterpFilterTextureH = NULL;
	mpD3DInterpFilterTextureV = NULL;
	mInterpFilterHSizeDst = 0;
	mInterpFilterHSizeSrc = 0;
	mInterpFilterHTexSize = 0;
	mInterpFilterVSizeDst = 0;
	mInterpFilterVSizeSrc = 0;
	mInterpFilterVTexSize = 0;
}

bool VDVideoDisplayMinidriverDX9::InitBoxlinearPS11Filters(float x, float w, float y, float h, int vpw, int vph, float facx, float facy) {
	// update horiz filter
	if (!mpD3DInterpFilterTexture
		|| mInterpFilterHPosDst != x
		|| mInterpFilterHSizeDst != w
		|| mInterpFilterHSizeSrc != mSource.pixmap.w
		|| mInterpFilterVPosDst != y
		|| mInterpFilterVSizeDst != h
		|| mInterpFilterVSizeSrc != mSource.pixmap.h
		|| mInterpFilterViewportW != vpw
		|| mInterpFilterViewportH != vph
		|| mInterpFilterFactorX != facx
		|| mInterpFilterFactorY != facy)
	{
		mInterpFilterHPosDst = x;
		mInterpFilterHSizeDst = w;
		mInterpFilterHSizeSrc = mSource.pixmap.w;
		mInterpFilterVPosDst = y;
		mInterpFilterVSizeDst = h;
		mInterpFilterVSizeSrc = mSource.pixmap.h;
		mInterpFilterViewportW = vpw;
		mInterpFilterViewportH = vph;
		mInterpFilterFactorX = facx;
		mInterpFilterFactorY = facy;

		// compute unclipped range
		const float x1 = x;
		const float y1 = y;
		const float x2 = x + w;
		const float y2 = y + h;

		// compute clipped range
		const float clipx1 = std::max<float>(x1, 0.0f);
		const float clipy1 = std::max<float>(y1, 0.0f);
		const float clipx2 = std::min<float>(x2, (float)vpw);
		const float clipy2 = std::min<float>(y2, (float)vph);

		// convert to integer range by fill rules
		const int ix1 = VDCeilToInt(clipx1 - 0.5f);
		const int iy1 = VDCeilToInt(clipy1 - 0.5f);
		const int ix2 = VDCeilToInt(clipx2 - 0.5f);
		const int iy2 = VDCeilToInt(clipy2 - 0.5f);

		// reject if we didn't cover any pixels
		if (ix1 >= ix2 || iy1 >= iy2)
			return false;

		// compute integral mapped pixel area
		const int idx = ix2 - ix1;
		const int idy = iy2 - iy1;

		// generate/update texture
		auto info = GeneratePS11BoxlinearTexture(mpManager, idx, idy, w, h, mSource.pixmap.w, mSource.pixmap.h, (float)ix1 - x1, (float)iy1 - y1, facx, facy, mpD3DInterpFilterTexture, mInterpFilterHTexSize, mInterpFilterVTexSize);
		if (info.mTexWidth < 0)
			return false;

		// Generate texture mapping area.
		//
		// We are supplying coordinates for the entire unclipped area (x1,y1)-(x2,y2)
		// and want the clipped area (ix1,iy1)-(ix2,iy2) to map to the UV area
		// (0,0)-(du,dv). Therefore:
		//
		// u(x) = u1 + (u2-u1)*(x-x1)/(x2-x1)
		// u(ix1) = 0   => u1 + (u2-u1)*(ix1-x1)/(x2-x1) = 0
		//              => u1*[(x2-x1)/(ix1-x1) - 1] + u2 = 0
		// u(ix2) = du  => u1 + (u2-u1)*(ix2-x1)/(x2-x1) = du
		//              => u1*[(x2-x1)/(ix2-x1) - 1] + u2 = du*(x2-x1)/(ix2-x1)
		//
		// u1*[(x2-x1)/(ix2-x1) - 1] + u2 = du*(x2-x1)/(ix2-x1)
		// u1*[(x2-x1)/(ix1-x1) - 1] + u2 = 0
		//
		// u1*[(x2-x1)/(ix2-x1) - (x2-x1)/(ix1-x1)] = du*(x2-x1)/(ix2-x1)
		// u1 = du/(ix2-ix1)*(x1-ix1)
		// u2 = du/(ix2-ix1)*(x2-ix1)
		// 
		const float ustep = 1.0f /(float)info.mTexWidth;
		const float vstep = 1.0f /(float)info.mTexHeight;

		mInterpFilterCoords.set(
			(x1 - (float)ix1) * ustep,
			(y1 - (float)iy1) * vstep,
			(x2 - (float)ix1) * ustep,
			(y2 - (float)iy1) * vstep
		);

		mInterpFilterHTexSize = info.mTexWidth;
		mInterpFilterVTexSize = info.mTexHeight;
	}

	return true;
}

void VDVideoDisplayMinidriverDX9::ShutdownBoxlinearPS11Filters() {
	mpD3DInterpFilterTexture.clear();
	mInterpFilterHTexSize = 0;
	mInterpFilterVTexSize = 0;
}

void VDVideoDisplayMinidriverDX9::Shutdown() {
	vdsaferelease <<= mpD3DGammaRampTexture;
	vdsaferelease <<= mpD3DScanlineMaskTexture;
	vdsaferelease <<= mpD3DScreenMaskTexture;

	vdsaferelease <<= mpD3DBloomInputRTT, mpD3DBloomConvRTT;
	
	for(auto& tex : mpD3DBloomPyramid1RTT)
		vdsaferelease <<= tex;

	for(auto& tex : mpD3DBloomPyramid2RTT)
		vdsaferelease <<= tex;

	vdsaferelease <<= mpD3DArtifactingRTT;

	mpCustomPipeline = nullptr;

	mpUploadContext = NULL;

	if (mpRenderer) {
		mpRenderer->Shutdown();
		mpRenderer.clear();
	}

	ShutdownBicubic();
	ShutdownBicubicPS2Filters();
	ShutdownBoxlinearPS11Filters();

	mpSwapChain = NULL;
	mbSwapChainVsync = false;
	mbSwapChainVsyncEvent = false;
	mSwapChainW = 0;
	mSwapChainH = 0;

	mpVideoManager = NULL;

	if (mpManager) {
		if (mbFullScreenSet) {
			mbFullScreenSet = false;
			mpManager->AdjustFullScreen(false, 0, 0, 0, false, nullptr);
		}

		VDDeinitDirect3D9(mpManager, this);
		mpManager = NULL;
	}

	mbCubicAttempted = false;
}

bool VDVideoDisplayMinidriverDX9::ModifySource(const VDVideoDisplaySourceInfo& info) {
	bool fastPath = false;

	if (mSource.pixmap.w == info.pixmap.w && mSource.pixmap.h == info.pixmap.h) {
		const int prevFormat = mSource.pixmap.format;
		const int nextFormat = info.pixmap.format;

		if (prevFormat == nextFormat)
			fastPath = true;
	}

	if (!fastPath) {
		ShutdownBoxlinearPS11Filters();

		mpUploadContext.clear();

		mpUploadContext = new_nothrow VDVideoUploadContextD3D9;
		const uint32 bufferCount = mpCustomPipeline ? mpCustomPipeline->GetMaxPrevFrames() + 1 : 1;
		if (!mpUploadContext || !mpUploadContext->Init(mpManager->GetMonitor(), mbUseD3D9Ex, info.pixmap, info.bAllowConversion, bufferCount, mSource.use16bit)) {
			mpUploadContext.clear();
			return false;
		}
	}

	mSource = info;
	return true;
}

bool VDVideoDisplayMinidriverDX9::IsValid() {
	return mpD3DDevice != 0;
}

void VDVideoDisplayMinidriverDX9::SetFilterMode(FilterMode mode) {
	mPreferredFilter = mode;

	if (mode != kFilterBicubic && mode != kFilterAnySuitable) {
		ShutdownBicubicPS2Filters();

		if (mbCubicInitialized)
			ShutdownBicubic();
	}
}

void VDVideoDisplayMinidriverDX9::SetFullScreen(bool fs, uint32 w, uint32 h, uint32 refresh, bool use16bit) {
	if (mbFullScreen != fs) {
		mbFullScreen = fs;
		mFullScreenWidth = w;
		mFullScreenHeight = h;
		mFullScreenRefreshRate = refresh;
		mbFullScreen16Bit = use16bit;

		if (mpManager) {
			if (mbFullScreenSet != fs) {
				mbFullScreenSet = fs;
				mpManager->AdjustFullScreen(fs, w, h, refresh, use16bit, mhwnd);

				mbSwapChainPresentPending = false;
				mbSwapChainVsyncEventWaiting = false;
			}
		}
	}
}

bool VDVideoDisplayMinidriverDX9::SetScreenFX(const VDVideoDisplayScreenFXInfo *screenFX) {
	if (screenFX) {
		if (!mbScreenFXSupported)
			return false;

		mbUseScreenFX = true;
		mScreenFXInfo = *screenFX;

		if (!mbBloomSupported)
			mScreenFXInfo.mbBloomEnabled = false;

		const bool useInputConversion = (mScreenFXInfo.mColorCorrectionMatrix[0][0] != 0.0f) || mScreenFXInfo.mScreenMaskParams.mType != VDDScreenMaskType::None;
		if (!mpD3DGammaRampTexture
			|| mCachedGamma != mScreenFXInfo.mGamma
			|| mCachedOutputGamma != mScreenFXInfo.mOutputGamma
			|| mbCachedGammaHasInputConversion != useInputConversion)
		{
			mCachedGamma = mScreenFXInfo.mGamma;
			mCachedOutputGamma = mScreenFXInfo.mOutputGamma;
			mbCachedGammaHasInputConversion = useInputConversion;

			vdrefptr<IVDD3D9InitTexture> initTex;
			if (!mpManager->CreateInitTexture(256, 1, 1, D3DFMT_A8R8G8B8, ~initTex))
				return false;

			VDD3D9LockInfo lockInfo;
			if (!initTex->Lock(0, lockInfo))
				return false;

			VDDisplayCreateGammaRamp((uint32 *)lockInfo.mpData, 256, useInputConversion, mScreenFXInfo.mOutputGamma, mScreenFXInfo.mGamma);

			initTex->Unlock(0);

			vdrefptr<IVDD3D9Texture> tex;
			if (!mpManager->CreateTexture(initTex, ~tex))
				return false;

			mpD3DGammaRampTexture = tex->GetD3DTexture();
		}
	} else {
		mbUseScreenFX = false;
		vdsaferelease <<= mpD3DGammaRampTexture, mpD3DScanlineMaskTexture, mpD3DScreenMaskTexture;
	}

	if (!mbUseScreenFX || !mScreenFXInfo.mbBloomEnabled) {
		vdsaferelease <<= mpD3DBloomInputRTT;
		vdsaferelease <<= mpD3DBloomConvRTT;

		for(auto& tex : mpD3DBloomPyramid1RTT)
			vdsaferelease <<= tex;

		for(auto& tex : mpD3DBloomPyramid2RTT)
			vdsaferelease <<= tex;
	}

	if (!mbUseScreenFX || mScreenFXInfo.mPALBlendingOffset == 0.0f) {
		vdsaferelease <<= mpD3DArtifactingRTT;
	}

	return true;
}

bool VDVideoDisplayMinidriverDX9::Tick(int id) {
	return true;
}

void VDVideoDisplayMinidriverDX9::Poll() {
	if (mbSwapChainPresentPending) {
		RECT rClient = { mClientRect.left, mClientRect.top, mClientRect.right, mClientRect.bottom };
		if (!UpdateScreen(rClient, kModeVSync, true))
			mSource.mpCB->RequestNextFrame();
	}
}

bool VDVideoDisplayMinidriverDX9::Resize(int w, int h) {
	mbSwapChainImageValid = false;
	return VDVideoDisplayMinidriver::Resize(w, h);
}

bool VDVideoDisplayMinidriverDX9::Invalidate() {
	mbSwapChainImageValid = false;
	return false;
}

bool VDVideoDisplayMinidriverDX9::Update(UpdateMode mode) {
	if (!mpManager->CheckDevice())
		return false;

	if (!mpUploadContext->Update(mSource.pixmap))
		return false;

	mbSwapChainImageValid = false;

	return true;
}

void VDVideoDisplayMinidriverDX9::Refresh(UpdateMode mode) {
	if (mClientRect.right > 0 && mClientRect.bottom > 0) {
		RECT rClient = { mClientRect.left, mClientRect.top, mClientRect.right, mClientRect.bottom };

		if (!Paint(NULL, rClient, mode)) {
			VDDEBUG_DX9DISP("Refresh() failed in Paint()");
		}
	}
}

bool VDVideoDisplayMinidriverDX9::Paint(HDC, const RECT& rClient, UpdateMode updateMode) {
	if (!mbSwapChainImageValid) {
		if (!UpdateBackbuffer(rClient, updateMode)) {
			VDDEBUG_DX9DISP("Paint() failed in UpdateBackbuffer()");
			return false;
		}
	}

	if (!UpdateScreen(rClient, updateMode, 0 != (updateMode & kModeVSync))) {
		VDDEBUG_DX9DISP("Paint() failed in UpdateScreen()");
		return false;
	}

	return true;
}

void VDVideoDisplayMinidriverDX9::SetLogicalPalette(const uint8 *pLogicalPalette) {
}

bool VDVideoDisplayMinidriverDX9::UpdateSwapChain() {
	if (mbFullScreen)
		return true;

	RECT rClient {};
	if (!GetClientRect(mhwnd, &rClient)) {
		VDDEBUG_DX9DISP("UpdateSwapChain() unable to get client rect");
		return false;
	}

	// If the client is currently zero size, bail out -- we don't need a swap chain anyway.
	if (rClient.right <= 0 || rClient.bottom <= 0)
		return true;

	const D3DDISPLAYMODE& displayMode = mpManager->GetDisplayMode();
	int rtw = displayMode.Width;
	int rth = displayMode.Height;
	RECT rClippedClient={0,0,std::min<int>(rClient.right, rtw), std::min<int>(rClient.bottom, rth)};

	// Make sure the device is sane.
	if (!mpManager->CheckDevice())
		return false;

	// Check if we need to create or resize the swap chain.
	if (!UpdateSwapChain(rClippedClient.right, rClippedClient.bottom, rtw, rth))
		return false;

	return true;
}

bool VDVideoDisplayMinidriverDX9::UpdateSwapChain(int w, int h, int displayW, int displayH) {
	// full screen mode uses the implicit swap chain
	if (mbFullScreen)
		return true;

	if (mpManager->GetDeviceEx()) {
		if (mSwapChainW != w || mSwapChainH != h) {
			mpSwapChain = NULL;
			mbSwapChainVsync = false;
			mbSwapChainVsyncEvent = false;
			mSwapChainW = 0;
			mSwapChainH = 0;
			mbSwapChainImageValid = false;
		}

		if (!mpSwapChain || mSwapChainW != w || mSwapChainH != h) {
			int scw = std::min<int>(w, displayW);
			int sch = std::min<int>(h, displayH);

			if (!mpManager->CreateSwapChain(mhwnd, scw, sch, mbClipToMonitor, mSource.use16bit, ~mpSwapChain)) {
				VDDEBUG_DX9DISP("Unable to create %dx%d swap chain", scw, sch);
				return false;
			}

			mSwapChainW = scw;
			mSwapChainH = sch;
		}
	} else {
		if (mSwapChainW >= w + 128 || mSwapChainH >= h + 128) {
			mpSwapChain = NULL;
			mbSwapChainVsync = false;
			mbSwapChainVsyncEvent = false;
			mSwapChainW = 0;
			mSwapChainH = 0;
			mbSwapChainImageValid = false;
		}

		if (!mpSwapChain || mSwapChainW < w || mSwapChainH < h) {
			int scw = std::min<int>((w + 127) & ~127, displayW);
			int sch = std::min<int>((h + 127) & ~127, displayH);

			if (!mpManager->CreateSwapChain(mhwnd, scw, sch, mbClipToMonitor, mSource.use16bit, ~mpSwapChain)) {
				VDDEBUG_DX9DISP("Unable to create %dx%d swap chain", scw, sch);
				return false;
			}

			mSwapChainW = scw;
			mSwapChainH = sch;
		}
	}

	return true;
}

bool VDVideoDisplayMinidriverDX9::UpdateBackbuffer(const RECT& rClient0, UpdateMode updateMode) {
	using namespace nsVDDisplay;

	const D3DDISPLAYMODE& displayMode = mpManager->GetDisplayMode();
	int rtw = displayMode.Width;
	int rth = displayMode.Height;
	RECT rClient = rClient0;
	if (mbFullScreen) {
		rClient.right = rtw;
		rClient.bottom = rth;
	}

	RECT rClippedClient={0,0,std::min<int>(rClient.right, rtw), std::min<int>(rClient.bottom, rth)};

	// Make sure the device is sane.
	if (!mpManager->CheckDevice())
		return false;

	// Check if we need to create or resize the swap chain.
	if (!UpdateSwapChain(rClippedClient.right, rClippedClient.bottom, rtw, rth))
		return false;

	VDDisplayCompositeInfo compInfo = {};

	if (mpCompositor) {
		compInfo.mWidth = rClient.right;
		compInfo.mHeight = rClient.bottom;

		mpCompositor->PreComposite(compInfo);
	}

	// Do we need to switch bicubic modes?
	FilterMode mode = mPreferredFilter;

	if (mode == kFilterAnySuitable)
		mode = kFilterBicubic;

	// bicubic modes cannot clip
	if (rClient.right != rClippedClient.right || rClient.bottom != rClippedClient.bottom)
		mode = kFilterBilinear;

	// must force bilinear if screen effects are enabled
	if (mode == kFilterBicubic && mbUseScreenFX)
		mode = kFilterBilinear;

	if (mode != kFilterBicubic && mbCubicInitialized)
		ShutdownBicubic();
	else if (mode == kFilterBicubic && !mbCubicInitialized && !mbCubicAttempted)
		InitBicubic();

	if (mpD3DInterpFilterTexture && mPixelSharpnessX <= 1 && mPixelSharpnessY <= 1)
		ShutdownBoxlinearPS11Filters();

	static const D3DMATRIX ident={
		{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1}
	};

	D3D_DO(SetTransform(D3DTS_WORLD, &ident));
	D3D_DO(SetTransform(D3DTS_VIEW, &ident));
	D3D_DO(SetTransform(D3DTS_PROJECTION, &ident));

	D3D_DO(SetStreamSource(0, mpManager->GetVertexBuffer(), 0, sizeof(Vertex)));
	D3D_DO(SetIndices(mpManager->GetIndexBuffer()));
	D3D_DO(SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX2));
	D3D_DO(SetRenderState(D3DRS_LIGHTING, FALSE));
	D3D_DO(SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE));
	D3D_DO(SetRenderState(D3DRS_ZENABLE, FALSE));
	D3D_DO(SetRenderState(D3DRS_ALPHATESTENABLE, FALSE));
	D3D_DO(SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE));
	D3D_DO(SetRenderState(D3DRS_STENCILENABLE, FALSE));
	D3D_DO(SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE));
	D3D_DO(SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0));
	D3D_DO(SetTextureStageState(1, D3DTSS_TEXCOORDINDEX, 1));
	D3D_DO(SetTextureStageState(2, D3DTSS_TEXCOORDINDEX, 2));
	D3D_DO(SetSamplerState(0, D3DSAMP_SRGBTEXTURE, FALSE));
	D3D_DO(SetSamplerState(1, D3DSAMP_SRGBTEXTURE, FALSE));

	vdrefptr<IDirect3DSurface9> pRTMain;

	mpManager->SetSwapChainActive(NULL);

	if (mpSwapChain) {
		IDirect3DSwapChain9 *sc = mpSwapChain->GetD3DSwapChain();
		HRESULT hr = sc->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, ~pRTMain);
		if (FAILED(hr))
			return false;
	} else {
		mpManager->SetSwapChainActive(NULL);
		mpD3DDevice->GetRenderTarget(0, ~pRTMain);
	}

	mbSwapChainImageValid = false;

	bool bSuccess = false;

	bool needBlit = true;
	if (mColorOverride) {
		mpManager->SetSwapChainActive(mpSwapChain);

		D3DRECT rClear;
		rClear.x1 = rClient.left;
		rClear.y1 = rClient.top;
		rClear.x2 = rClient.right;
		rClear.y2 = rClient.bottom;
		HRESULT hr = mpD3DDevice->Clear(1, &rClear, D3DCLEAR_TARGET, mColorOverride, 0.0f, 0);

		bSuccess = SUCCEEDED(hr);
		needBlit = false;
	} 

	IDirect3DTexture9 *srcTex = mpUploadContext->GetD3DTexture();
	uint32 srcWidth = mSource.pixmap.w;
	uint32 srcHeight = mSource.pixmap.h;

	if (needBlit && mpCustomPipeline) {
		needBlit = false;

		try {
			vdsize32 viewportSize(rClient.right, rClient.bottom);

			if (mbDestRectEnabled)
				viewportSize = mDestRect.size();

			mpCustomPipeline->Run(mpUploadContext->GetD3DTextures(), vdint2(srcWidth, srcHeight), vdint2(mSource.pixmap.w, mSource.pixmap.h), vdint2(viewportSize.w, viewportSize.h));

			if (mpCustomPipeline->ContainsFinalBlit()) {
				mpManager->SetSwapChainActive(mpSwapChain);

				vdrefptr<IDirect3DSurface9> rt;
				HRESULT hr = mpD3DDevice->GetRenderTarget(0, ~rt);
				if (SUCCEEDED(hr)) {
					D3DSURFACE_DESC desc;
					hr = rt->GetDesc(&desc);
					if (SUCCEEDED(hr)) {
						D3DVIEWPORT9 vp = {};
						vp.X = 0;
						vp.Y = 0;
						vp.Width = (DWORD)rClippedClient.right;
						vp.Height = (DWORD)rClippedClient.bottom;
						vp.MinZ = 0;
						vp.MaxZ = 1;

						hr = mpD3DDevice->SetViewport(&vp);
						if (SUCCEEDED(hr)) {

							D3DRECT r;
							r.x1 = 0;
							r.y1 = 0;
							r.x2 = rClippedClient.right;
							r.y2 = rClippedClient.bottom;

							hr = mpD3DDevice->Clear(1, &r, D3DCLEAR_TARGET, mBackgroundColor, 0.0f, 0);
							if (FAILED(hr))
								return false;

							vdrect32f dstRect(0.0f, 0.0f, (float)rClient0.right, (float)rClient0.bottom);

							if (mbDestRectEnabled)
								dstRect = vdrect32f((float)mDestRect.left, (float)mDestRect.top, (float)mDestRect.right, (float)mDestRect.bottom);

							mpCustomPipeline->RunFinal(
								dstRect, vdint2(viewportSize.w, viewportSize.h)
							);

							bSuccess = true;
						}
					}
				}
			} else {
				needBlit = true;

				srcTex = mpCustomPipeline->GetFinalOutput(srcWidth, srcHeight);
			}
		} catch(const MyError&) {
		}
	}
	
	if (needBlit) {
		bSuccess = false;
		needBlit = false;

		D3DRECT rects[4];
		D3DRECT *nextRect = rects;
		RECT rDest = rClippedClient;

		if (mbDestRectEnabled) {
			// clip client rect to dest rect
			if (rDest.left < mDrawRect.left)
				rDest.left = mDrawRect.left;

			if (rDest.top < mDrawRect.top)
				rDest.top = mDrawRect.top;

			if (rDest.right > mDrawRect.right)
				rDest.right = mDrawRect.right;

			if (rDest.bottom > mDrawRect.bottom)
				rDest.bottom = mDrawRect.bottom;

			// fix rect in case dest rect lies entirely outside of client rect
			if (rDest.left > rClippedClient.right)
				rDest.left = rClippedClient.right;

			if (rDest.top > rClippedClient.bottom)
				rDest.top = rClippedClient.bottom;

			if (rDest.right < rDest.left)
				rDest.right = rDest.left;

			if (rDest.bottom < rDest.top)
				rDest.bottom = rDest.top;
		}

		if (rDest.right <= rDest.left || rDest.bottom <= rDest.top) {
			mpManager->SetSwapChainActive(mpSwapChain);

			D3DRECT r;
			r.x1 = rClippedClient.left;
			r.y1 = rClippedClient.top;
			r.x2 = rClippedClient.right;
			r.y2 = rClippedClient.bottom;

			HRESULT hr = mpD3DDevice->Clear(1, &r, D3DCLEAR_TARGET, mBackgroundColor, 0.0f, 0);
			if (FAILED(hr))
				return false;
		} else {
			const sint32 destW = rDest.right - rDest.left;
			const sint32 destH = rDest.bottom - rDest.top;

			if (rDest.top > rClippedClient.top) {
				nextRect->x1 = rClippedClient.left;
				nextRect->y1 = rClippedClient.top;
				nextRect->x2 = rClippedClient.right;
				nextRect->y2 = rDest.top;
				++nextRect;
			}

			if (rDest.left > rClippedClient.left) {
				nextRect->x1 = rClippedClient.left;
				nextRect->y1 = rDest.top;
				nextRect->x2 = rDest.left;
				nextRect->y2 = rDest.bottom;
				++nextRect;
			}

			if (rDest.right < rClippedClient.right) {
				nextRect->x1 = rDest.right;
				nextRect->y1 = rDest.top;
				nextRect->x2 = rClippedClient.right;
				nextRect->y2 = rDest.bottom;
				++nextRect;
			}

			if (rDest.bottom < rClippedClient.bottom) {
				nextRect->x1 = rClippedClient.left;
				nextRect->y1 = rDest.bottom;
				nextRect->x2 = rClippedClient.right;
				nextRect->y2 = rClippedClient.bottom;
				++nextRect;
			}

			HRESULT hr;
			if (nextRect > rects) {
				mpManager->SetSwapChainActive(mpSwapChain);

				hr = mpD3DDevice->Clear(nextRect - rects, rects, D3DCLEAR_TARGET, mBackgroundColor, 0.0f, 0);
				if (FAILED(hr))
					return false;
			}

			VDVideoDisplayDX9Manager::EffectContext ctx {};

			ctx.mpSourceTexture1 = srcTex;
			ctx.mpSourceTexture2 = NULL;
			ctx.mpSourceTexture3 = NULL;
			ctx.mpSourceTexture4 = NULL;
			ctx.mpSourceTexture5 = NULL;
			ctx.mpInterpFilterH = NULL;
			ctx.mpInterpFilterV = NULL;
			ctx.mpInterpFilter = NULL;
			ctx.mSourceW = srcWidth;
			ctx.mSourceH = srcHeight;
			ctx.mSourceArea.set(0, 0, (float)ctx.mSourceW, (float)ctx.mSourceH);

			D3DSURFACE_DESC desc;

			hr = ctx.mpSourceTexture1->GetLevelDesc(0, &desc);
			if (FAILED(hr))
				return false;

			ctx.mSourceTexW = desc.Width;
			ctx.mSourceTexH = desc.Height;
			ctx.mInterpHTexW = 1;
			ctx.mInterpHTexH = 1;
			ctx.mInterpVTexW = 1;
			ctx.mInterpVTexH = 1;
			ctx.mInterpTexW = 1;
			ctx.mInterpTexH = 1;
			ctx.mViewportX = rDest.left;
			ctx.mViewportY = rDest.top;
			ctx.mViewportW = rDest.right - rDest.left;
			ctx.mViewportH = rDest.bottom - rDest.top;

			ctx.mOutputX = mDestRectF.left - (float)rDest.left;
			ctx.mOutputY = mDestRectF.top - (float)rDest.top;
			ctx.mOutputW = mDestRectF.right - mDestRectF.left;
			ctx.mOutputH = mDestRectF.bottom - mDestRectF.top;

			ctx.mDefaultUVScaleCorrectionX = 1.0f;
			ctx.mDefaultUVScaleCorrectionY = 1.0f;
			ctx.mChromaScaleU = 1.0f;
			ctx.mChromaScaleV = 1.0f;
			ctx.mChromaOffsetU = 0.0f;
			ctx.mChromaOffsetV = 0.0f;
			ctx.mPixelSharpnessX = mPixelSharpnessX;
			ctx.mPixelSharpnessY = mPixelSharpnessY;

			if (mbCubicInitialized) {
				int cubicMode = mCubicMode;

				// viewport clip the blit
				vdrect32f unclippedOutput(ctx.mOutputX, ctx.mOutputY, ctx.mOutputX + ctx.mOutputW, ctx.mOutputY + ctx.mOutputH);
				vdrect32f clippedOutput;

				clippedOutput.left   = std::max<float>(unclippedOutput.left,   0.0f);
				clippedOutput.top    = std::max<float>(unclippedOutput.top,    0.0f);
				clippedOutput.right  = std::min<float>(unclippedOutput.right,  ctx.mViewportW);
				clippedOutput.bottom = std::min<float>(unclippedOutput.bottom, ctx.mViewportH);

				// pixel snap blit
				clippedOutput.left   = ceilf(clippedOutput.left   - 0.5f);
				clippedOutput.top    = ceilf(clippedOutput.top    - 0.5f);
				clippedOutput.right  = ceilf(clippedOutput.right  - 0.5f);
				clippedOutput.bottom = ceilf(clippedOutput.bottom - 0.5f);

				if (clippedOutput != unclippedOutput) {
					float invW = 1.0f / unclippedOutput.width();
					float invH = 1.0f / unclippedOutput.height();

					const vdrect32f normClip(
						(clippedOutput.left   - unclippedOutput.left) * invW,
						(clippedOutput.top    - unclippedOutput.top ) * invH,
						(clippedOutput.right  - unclippedOutput.left) * invW,
						(clippedOutput.bottom - unclippedOutput.top ) * invH
					);

					const vdrect32f origSource = ctx.mSourceArea;

					ctx.mOutputX = clippedOutput.left;
					ctx.mOutputY = clippedOutput.top;
					ctx.mOutputW = clippedOutput.width();
					ctx.mOutputH = clippedOutput.height();

					ctx.mSourceArea.left   = origSource.left * (1.0f - normClip.left  ) + origSource.right  * normClip.left  ;
					ctx.mSourceArea.top    = origSource.top  * (1.0f - normClip.top   ) + origSource.bottom * normClip.top   ;
					ctx.mSourceArea.right  = origSource.left * (1.0f - normClip.right ) + origSource.right  * normClip.right ;
					ctx.mSourceArea.bottom = origSource.top  * (1.0f - normClip.bottom) + origSource.bottom * normClip.bottom;
				}

				ctx.mViewportX += VDRoundToInt(ctx.mOutputX);
				ctx.mViewportY += VDRoundToInt(ctx.mOutputY);
				ctx.mViewportW = VDRoundToInt(ctx.mOutputW);
				ctx.mViewportH = VDRoundToInt(ctx.mOutputH);
				ctx.mOutputX = 0;
				ctx.mOutputY = 0;

				if (!InitBicubicPS2Filters(clippedOutput, ctx.mSourceArea))
					cubicMode = VDVideoDisplayDX9Manager::kCubicNotPossible;
				else {
					ctx.mpInterpFilterH = mpD3DInterpFilterTextureH;
					ctx.mpInterpFilterV = mpD3DInterpFilterTextureV;
					ctx.mInterpHTexW = mCubicFilterHTexScale;
					ctx.mInterpHTexH = mCubicFilterHTexOffset;
					ctx.mInterpVTexW = mCubicFilterVTexScale;
					ctx.mInterpVTexH = mCubicFilterVTexOffset;
				}

				switch(cubicMode) {
				case VDVideoDisplayDX9Manager::kCubicUsePS2_0Path:
					bSuccess = mpVideoManager->RunEffect(ctx, g_technique_bicubic_2_0, pRTMain);
					break;
				default:
					mpManager->SetSwapChainActive(mpSwapChain);
					bSuccess = mpVideoManager->BlitFixedFunction(ctx, pRTMain, true);
					break;
				}
			} else if (mbUseScreenFX) {
				bSuccess = true;

				if (mScreenFXInfo.mPALBlendingOffset != 0) {
					// rebuild PAL artifacting texture if necessary
					uint32 srcw = ctx.mSourceW;
					uint32 srch = ctx.mSourceH;
					uint32 texw = ctx.mSourceW;
					uint32 texh = ctx.mSourceH;

					// can't take nonpow2conditional due to dependent read in subsequent passes
					if (!mpManager->AdjustTextureSize(texw, texh)) {
						bSuccess = false;
					}

					if (bSuccess && mpD3DArtifactingRTT && (mCachedArtifactingTexW != texw || mCachedArtifactingTexH != texh)) {
						vdsaferelease <<= mpD3DArtifactingRTT;
					}

					if (bSuccess && !mpD3DArtifactingRTT) {
						hr = mpD3DDevice->CreateTexture(texw, texh, 1, D3DUSAGE_RENDERTARGET, D3DFMT_X8R8G8B8, D3DPOOL_DEFAULT, ~mpD3DArtifactingRTT, nullptr);
						bSuccess = SUCCEEDED(hr);
						if (bSuccess) {
							mCachedArtifactingTexW = texw;
							mCachedArtifactingTexH = texh;
						}
					}

					if (mpD3DArtifactingRTT) {
						if (mCachedArtifactingW != srcw || mCachedArtifactingH != srch) {
							mCachedArtifactingW = srcw;
							mCachedArtifactingH = srch;

							mpManager->ClearRenderTarget(mpD3DArtifactingRTT);
						}
					}

					if (bSuccess) {
						vdrefptr<IDirect3DSurface9> palRTSurface;
						hr = mpD3DArtifactingRTT->GetSurfaceLevel(0, ~palRTSurface);
						bSuccess = SUCCEEDED(hr);

						if (bSuccess) {
							auto ctx2 = ctx;
							ctx2.mOutputX = 0;
							ctx2.mOutputY = 0;
							ctx2.mOutputW = ctx.mSourceW;
							ctx2.mOutputH = ctx.mSourceH;
							ctx2.mViewportX = 0;
							ctx2.mViewportY = 0;
							ctx2.mViewportW = ctx2.mSourceW;
							ctx2.mViewportH = ctx2.mSourceH;
							ctx2.mbUseUV1Area = true;
							ctx2.mUV1Area.set(
								0.0f,
								0.0f,
								(float)ctx2.mSourceW / (float)ctx2.mSourceTexW,
								(float)ctx2.mSourceH / (float)ctx2.mSourceTexH
							);

							ctx2.mUV1Area.translate(0.0f, (float)mScreenFXInfo.mPALBlendingOffset / (float)ctx2.mSourceTexH);

							bSuccess = mpVideoManager->RunEffect(ctx2, g_technique_screenfx_palartifacting, palRTSurface);

							ctx.mpSourceTexture1 = mpD3DArtifactingRTT;
							ctx.mSourceTexW = mCachedArtifactingTexW;
							ctx.mSourceTexH = mCachedArtifactingTexH;
						}
					}
				}

				const bool scanlinesEnabled = mScreenFXInfo.mScanlineIntensity > 0;
				const bool maskEnabled = mScreenFXInfo.mScreenMaskParams.mType != VDDScreenMaskType::None;
				bool maskHorizChanged = false;
				bool maskVertChanged = false;

				if (scanlinesEnabled || maskEnabled) {
					if (   mCachedScreenMaskSrcW != ctx.mSourceW
						|| mCachedScreenMaskDstW != mDrawRect.width()
						|| mCachedScreenMaskOutX != ctx.mOutputX
						|| mCachedScreenMaskOutW != ctx.mOutputW)
					{
						maskHorizChanged = true;

						mCachedScreenMaskSrcW = ctx.mSourceW;
						mCachedScreenMaskDstW = mDrawRect.width();
						mCachedScreenMaskOutX = ctx.mOutputX;
						mCachedScreenMaskOutW = ctx.mOutputW;
					}

					if (   mCachedScreenMaskSrcH != ctx.mSourceH
						|| mCachedScreenMaskDstH != mDrawRect.height()
						|| mCachedScreenMaskOutY != ctx.mOutputY
						|| mCachedScreenMaskOutH != ctx.mOutputH)
					{
						maskVertChanged = true;

						mCachedScreenMaskSrcH = ctx.mSourceH;
						mCachedScreenMaskDstH = mDrawRect.height();
						mCachedScreenMaskOutY = ctx.mOutputY;
						mCachedScreenMaskOutH = ctx.mOutputH;
					}

					if (maskHorizChanged || maskVertChanged) {
						mScreenMaskTexW = mDrawRect.width();
						mScreenMaskTexH = mDrawRect.height();

						mpManager->AdjustTextureSize(mScreenMaskTexW, mScreenMaskTexH, true);

						mCachedMaskHScale = (float)ctx.mOutputW / (float)mScreenMaskTexW;
						mCachedMaskHBase = ctx.mOutputX / (float)mScreenMaskTexW;
						mCachedMaskVScale = (float)ctx.mOutputH / (float)mScreenMaskTexH;
						mCachedMaskVBase = ctx.mOutputY / (float)mScreenMaskTexH;
					}
				}

				// rebuild the scanline texture if necessary
				const bool useDistortion = mScreenFXInfo.mDistortionX > 0;
				const bool useScanlineWrapMapping = useDistortion;

				if (scanlinesEnabled && (!mpD3DScanlineMaskTexture
					|| maskVertChanged
					|| mCachedScanlineIntensity != mScreenFXInfo.mScanlineIntensity
					|| mbCachedScanlineWrapMapped != useScanlineWrapMapping
					))
				{
					vdsaferelease <<= mpD3DScanlineMaskTexture;

					const int texh = useScanlineWrapMapping ? 256 : mScreenMaskTexH;

					vdrefptr<IVDD3D9InitTexture> initTex;
					if (mpManager->CreateInitTexture(1, texh, 1, D3DFMT_A8R8G8B8, ~initTex)) {
						VDD3D9LockInfo lockInfo;
						if (initTex->Lock(0, lockInfo)) {
							if (useScanlineWrapMapping)
								VDDisplayCreateScanlineMaskTexture((uint32 *)lockInfo.mpData, lockInfo.mPitch, 1.0f, 256.0f, 0, 256.0f, texh, mScreenFXInfo.mScanlineIntensity, false, false);
							else
								VDDisplayCreateScanlineMaskTexture((uint32 *)lockInfo.mpData, lockInfo.mPitch, ctx.mSourceH, mDrawRect.height(), ctx.mOutputY, ctx.mOutputH, texh, mScreenFXInfo.mScanlineIntensity, true, false);

							initTex->Unlock(0);

							vdrefptr<IVDD3D9Texture> tex;
							if (mpManager->CreateTexture(initTex, ~tex)) {
								mpD3DScanlineMaskTexture = tex->GetD3DTexture();

								mCachedScanlineIntensity = mScreenFXInfo.mScanlineIntensity;
								mbCachedScanlineWrapMapped = useDistortion;
							} else {
								bSuccess = false;
							}
						} else {
							bSuccess = false;
						}
					} else {
						bSuccess = false;
					}
				}

				// rebuild the screen mask texture if necessary
				if (maskEnabled) {
					if (!mpD3DScreenMaskTexture
						|| maskHorizChanged
						|| maskVertChanged
						|| mCachedScreenMaskParams != mScreenFXInfo.mScreenMaskParams)
					{
						int maskw = mDrawRect.width();
						int maskh = mDrawRect.height();

						int texw = mScreenMaskTexW;
						int texh = mScreenMaskTexH;

						if (mScreenFXInfo.mScreenMaskParams.mType == VDDScreenMaskType::ApertureGrille) {
							maskh = 1;
							texh = 1;
						}

						vdblock<uint32> imageDotMask;
						imageDotMask.resize(texw * texh, 0xFFFFFFFF);

						if (mScreenFXInfo.mScreenMaskParams.mType == VDDScreenMaskType::DotTriad) {
							VDDisplayTriadDotMaskParams dotMaskParams(mScreenFXInfo.mScreenMaskParams, ctx.mOutputW, (float)ctx.mSourceW);

							VDDisplayCreateTriadDotMaskTexture(
								imageDotMask.data(),
								sizeof(imageDotMask[0]) * texw,
								maskw,
								maskh,
								ctx.mOutputX,
								-ctx.mOutputY,
								ctx.mOutputW,
								ctx.mOutputH,
								dotMaskParams);
						} else if (mScreenFXInfo.mScreenMaskParams.mType == VDDScreenMaskType::SlotMask) {
							VDDisplaySlotMaskParams slotParams(mScreenFXInfo.mScreenMaskParams, ctx.mOutputW, (float)ctx.mSourceW);

							VDDisplayCreateSlotMaskTexture(
								imageDotMask.data(),
								sizeof(imageDotMask[0]) * texw,
								maskw,
								maskh,
								ctx.mOutputX,
								-ctx.mOutputY,
								ctx.mOutputW,
								ctx.mOutputH,
								slotParams);
						} else {
							VDDisplayApertureGrilleParams dotMaskParams(mScreenFXInfo.mScreenMaskParams, ctx.mOutputW, (float)ctx.mSourceW);

							VDDisplayCreateApertureGrilleTexture(imageDotMask.data(), maskw, ctx.mOutputX, dotMaskParams);
						}

						vdrefptr<IVDD3D9InitTexture> initTex;
						if (mpManager->CreateInitTexture(texw, texh, 1, D3DFMT_A8R8G8B8, ~initTex)) {
							VDD3D9LockInfo lockInfo;

							if (initTex->Lock(0, lockInfo)) {
								VDMemcpyRect(lockInfo.mpData, lockInfo.mPitch, imageDotMask.data(), texw * sizeof(imageDotMask[0]), texw * sizeof(imageDotMask[0]), texh);
								initTex->Unlock(0);

								vdrefptr<IVDD3D9Texture> tex;
								if (mpManager->CreateTexture(initTex, ~tex)) {
									mpD3DScreenMaskTexture = tex->GetD3DTexture();

									mCachedScreenMaskParams = mScreenFXInfo.mScreenMaskParams;
								} else {
									bSuccess = false;
								}
							} else {
								bSuccess = false;
							}
						} else {
							bSuccess = false;
						}						
					} else {
						bSuccess = false;
					}
				} else {
					vdsaferelease <<= mpD3DScreenMaskTexture;
				}

				static constexpr const TechniqueInfo *kTechniques
					[2]		// sharp
					[3]		// scanline mode
					[4]		// color mode
				{
					{
						{
							&g_technique_screenfx_ptlinear_noscanlines_linear,
							&g_technique_screenfx_ptlinear_noscanlines_gamma,
							&g_technique_screenfx_ptlinear_noscanlines_cc,
							&g_technique_screenfx_ptlinear_noscanlines_mask_cc
						},
						{
							&g_technique_screenfx_ptlinear_scanlines_linear,
							&g_technique_screenfx_ptlinear_scanlines_gamma,
							&g_technique_screenfx_ptlinear_scanlines_cc,
							&g_technique_screenfx_ptlinear_scanlines_mask_cc
						},
						{
							&g_technique_screenfx_ptlinear_scanlines_linear_distort,
							&g_technique_screenfx_ptlinear_scanlines_gamma_distort,
							&g_technique_screenfx_ptlinear_scanlines_cc_distort,
							&g_technique_screenfx_ptlinear_scanlines_mask_cc_distort
						},
					},
					{
						{
							&g_technique_screenfx_sharp_noscanlines_linear,
							&g_technique_screenfx_sharp_noscanlines_gamma,
							&g_technique_screenfx_sharp_noscanlines_cc,
							&g_technique_screenfx_sharp_noscanlines_mask_cc
						},
						{
							&g_technique_screenfx_sharp_scanlines_linear,
							&g_technique_screenfx_sharp_scanlines_gamma,
							&g_technique_screenfx_sharp_scanlines_cc,
							&g_technique_screenfx_sharp_scanlines_mask_cc
						},
						{
							&g_technique_screenfx_sharp_scanlines_linear_distort,
							&g_technique_screenfx_sharp_scanlines_gamma_distort,
							&g_technique_screenfx_sharp_scanlines_cc_distort,
							&g_technique_screenfx_sharp_scanlines_mask_cc_distort
						},
					},
				};

				const bool useSharpBilinear = mPixelSharpnessX > 1 || mPixelSharpnessY > 1;
				const bool useScanlines = mScreenFXInfo.mScanlineIntensity > 0.0f;
				const bool useScreenMask = mScreenFXInfo.mScreenMaskParams.mType != VDDScreenMaskType::None;
				const bool useGammaCorrection = mScreenFXInfo.mGamma != 1.0f;
				const bool useColorCorrection = mScreenFXInfo.mColorCorrectionMatrix[0][0] != 0.0f;

				struct VSConstants {
					float mScanlineInfo[4];
					float mMaskInfo[4];
					float mDistortionInfo[4];
					float mDistortionScales[4];
					float mSrcUVArea[4];
				} vsConstants {};

				if (useScanlineWrapMapping) {
					vsConstants.mScanlineInfo[0] = 0.0f;
					vsConstants.mScanlineInfo[1] = ctx.mSourceH;
					vsConstants.mScanlineInfo[2] = 0.0f;
					vsConstants.mScanlineInfo[3] = 0.0f;
				} else {
					vsConstants.mScanlineInfo[0] = mCachedMaskHScale;
					vsConstants.mScanlineInfo[1] = mCachedMaskVScale;
					vsConstants.mScanlineInfo[2] = mCachedMaskVBase;
					vsConstants.mScanlineInfo[3] = mCachedMaskHBase;
				}

				vsConstants.mMaskInfo[0] = mCachedMaskHScale;
				vsConstants.mMaskInfo[1] = mCachedMaskVScale;
				vsConstants.mMaskInfo[2] = mCachedMaskVBase;
				vsConstants.mMaskInfo[3] = mCachedMaskHBase;
				vsConstants.mDistortionInfo[0] = 1.0f;
				vsConstants.mDistortionInfo[1] = useDistortion ? -0.5f : 0.0f;

				VDDisplayDistortionMapping distortionMapping;
				distortionMapping.Init(mScreenFXInfo.mDistortionX, mScreenFXInfo.mDistortionYRatio, (float)ctx.mOutputW / (float)ctx.mOutputH);

				vsConstants.mDistortionScales[0] = distortionMapping.mScaleX;
				vsConstants.mDistortionScales[1] = distortionMapping.mScaleY;
				vsConstants.mDistortionScales[2] = distortionMapping.mSqRadius;

				vsConstants.mSrcUVArea[0] = useSharpBilinear ? (float)ctx.mSourceW : (float)ctx.mSourceW / (float)ctx.mSourceTexW;
				vsConstants.mSrcUVArea[1] = useSharpBilinear ? (float)ctx.mSourceH : (float)ctx.mSourceH / (float)ctx.mSourceTexH;
				vsConstants.mSrcUVArea[2] = useScanlines ? useSharpBilinear ? 0.25f : 0.25f / (float)ctx.mSourceTexH : 0.0f;
				vsConstants.mSrcUVArea[3] = 0;

				struct PSConstants {
					float mSharpnessInfo[4];
					float mDistortionScales[4];
					float mImageUVSize[4];
					float mColorCorrectMatrix[3][4];
				} psConstants {};

				psConstants.mSharpnessInfo[0] = mPixelSharpnessX;
				psConstants.mSharpnessInfo[1] = mPixelSharpnessY;
				psConstants.mSharpnessInfo[2] = 1.0f / desc.Height;
				psConstants.mSharpnessInfo[3] = 1.0f / desc.Width;
	
				psConstants.mDistortionScales[0] = vsConstants.mDistortionScales[0];
				psConstants.mDistortionScales[1] = vsConstants.mDistortionScales[1];
				psConstants.mDistortionScales[2] = vsConstants.mDistortionScales[2];

				psConstants.mImageUVSize[0] = vsConstants.mSrcUVArea[0];
				psConstants.mImageUVSize[1] = vsConstants.mSrcUVArea[1];
				psConstants.mImageUVSize[2] = vsConstants.mSrcUVArea[2];
				psConstants.mImageUVSize[3] = vsConstants.mSrcUVArea[3];

				psConstants.mDistortionScales[0] = distortionMapping.mScaleX;
				psConstants.mDistortionScales[1] = distortionMapping.mScaleY;
				psConstants.mDistortionScales[2] = distortionMapping.mSqRadius;

				psConstants.mImageUVSize[0] = useSharpBilinear ? (float)ctx.mSourceW : (float)ctx.mSourceW / (float)ctx.mSourceTexW;
				psConstants.mImageUVSize[1] = useSharpBilinear ? (float)ctx.mSourceH : (float)ctx.mSourceH / (float)ctx.mSourceTexH;
				psConstants.mImageUVSize[2] = useScanlines ? useSharpBilinear ? 0.25f : 0.25f / (float)ctx.mSourceTexH : 0.0f;
				psConstants.mImageUVSize[3] = mCachedMaskVScale;
				
				// need to transpose matrix to column major storage, and force an identity matrix if
				// color correction wasn't requested but is being forced on by masking
				if (!useColorCorrection) {
					psConstants.mColorCorrectMatrix[0][0] = 1.0f;
					psConstants.mColorCorrectMatrix[0][1] = 0.0f;
					psConstants.mColorCorrectMatrix[0][2] = 0.0f;
					psConstants.mColorCorrectMatrix[1][0] = 0.0f;
					psConstants.mColorCorrectMatrix[1][1] = 1.0f;
					psConstants.mColorCorrectMatrix[1][2] = 0.0f;
					psConstants.mColorCorrectMatrix[2][0] = 0.0f;
					psConstants.mColorCorrectMatrix[2][1] = 0.0f;
					psConstants.mColorCorrectMatrix[2][2] = 1.0f;
				} else {
					psConstants.mColorCorrectMatrix[0][0] = mScreenFXInfo.mColorCorrectionMatrix[0][0];
					psConstants.mColorCorrectMatrix[0][1] = mScreenFXInfo.mColorCorrectionMatrix[1][0];
					psConstants.mColorCorrectMatrix[0][2] = mScreenFXInfo.mColorCorrectionMatrix[2][0];
					psConstants.mColorCorrectMatrix[1][0] = mScreenFXInfo.mColorCorrectionMatrix[0][1];
					psConstants.mColorCorrectMatrix[1][1] = mScreenFXInfo.mColorCorrectionMatrix[1][1];
					psConstants.mColorCorrectMatrix[1][2] = mScreenFXInfo.mColorCorrectionMatrix[2][1];
					psConstants.mColorCorrectMatrix[2][0] = mScreenFXInfo.mColorCorrectionMatrix[0][2];
					psConstants.mColorCorrectMatrix[2][1] = mScreenFXInfo.mColorCorrectionMatrix[1][2];
					psConstants.mColorCorrectMatrix[2][2] = mScreenFXInfo.mColorCorrectionMatrix[2][2];
				}

				if (useScreenMask && mScreenFXInfo.mScreenMaskParams.mbScreenMaskIntensityCompensation) {
					const float scale = 1.0f / mScreenFXInfo.mScreenMaskParams.GetMaskIntensityScale();

					for(int i=0; i<3; ++i) {
						for(int j=0; j<3; ++j)
							psConstants.mColorCorrectMatrix[i][j] *= scale;
					}
				}

				psConstants.mColorCorrectMatrix[0][3] = 0.0f;
				psConstants.mColorCorrectMatrix[1][3] = 1.0f;

				mpD3DDevice->SetVertexShaderConstantF(16, (const float *)&vsConstants, sizeof(vsConstants)/16);
				mpD3DDevice->SetPixelShaderConstantF(16, (const float *)&psConstants, sizeof(psConstants)/16);

				ctx.mbAutoBilinear = (mPreferredFilter != kFilterPoint);
				ctx.mpSourceTexture2 = mpD3DGammaRampTexture;
				ctx.mpSourceTexture3 = mpD3DScanlineMaskTexture;
				ctx.mpSourceTexture4 = mpD3DScreenMaskTexture;

				if (useScanlines) 
					ctx.mSourceArea.translate(0.0f, 0.25f);

				if (useSharpBilinear) {
					ctx.mbUseUV0Scale = true;
					ctx.mUV0Scale = vdfloat2{(float)ctx.mSourceTexW, (float)ctx.mSourceTexH};
				}

				const size_t scanlineMode = useScanlines ? useDistortion ? 2 : 1 : 0;
				const TechniqueInfo& technique = *kTechniques[useSharpBilinear][scanlineMode][useScreenMask ? 3 : useColorCorrection ? 2 : useGammaCorrection ? 1 : 0];
				vdfunction<void(vdspan<nsVDD3D9::Vertex> vx, const IVDVideoDisplayDX9Manager::StdParamData& data)> vertexProcessor;

				const auto distortionProcessor = [&](vdspan<nsVDD3D9::Vertex> vxs, const IVDVideoDisplayDX9Manager::StdParamData& data) {
					for(nsVDD3D9::Vertex& vx : vxs) {
						vdfloat2 v = vdfloat2 { vx.u1, vx.v1 } - vdfloat2 { 0.5f, 0.5f };
						vdfloat2 v2 = v * vdfloat2 { distortionMapping.mScaleX, distortionMapping.mScaleY };
						vdfloat2 pos = v * sqrtf(distortionMapping.mSqRadius / (1.0f + nsVDMath::dot(v2, v2))) + vdfloat2 { 0.5f, 0.5f };

						float xmul = data.outputxform[0];
						float ymul = data.outputxform[1];
						float xadd = data.outputxform[3];
						float yadd = data.outputxform[2];

						vx.x = pos.x * xmul + xadd;
						vx.y = pos.y * ymul + yadd;
						vx.u0 = pos.x;
						vx.v0 = pos.y;
					}
				};

				if (useDistortion) {
					ctx.mbOutputClear = true;
					ctx.mOutputTessellationX = 24;
					ctx.mOutputTessellationY = 24;

					vertexProcessor = [&](vdspan<nsVDD3D9::Vertex> vxs, const IVDVideoDisplayDX9Manager::StdParamData& data) {
						distortionProcessor(vxs, data);
					};
				}

				if (!mScreenFXInfo.mbBloomEnabled) {

					if (vertexProcessor)
						ctx.mVertexProcessor = &vertexProcessor;

					bSuccess = mpVideoManager->RunEffect(ctx, technique, pRTMain);

					ctx.mVertexProcessor = nullptr;
				} else {
					// compute input and pyramid level sizes
					vdsize32 inputSize(destW, destH);

					bSuccess = true;

					vdsize32 inputTexSize = inputSize;
					if (!mpManager->AdjustTextureSize(inputTexSize, false))
						bSuccess = false;

					vdsize32 pyramidSize[6];
					vdsize32 pyramidTexSize[6];
					for(int level = 0; level < 6; ++level) {
						mBloomPyramidSizeF[level] = vdfloat2 { (float)destW, (float)destH } * ldexpf(0.5f, -level);
						pyramidSize[level] = vdsize32(
							std::max<sint32>(1, (sint32)ceilf(mBloomPyramidSizeF[level].x)),
							std::max<sint32>(1, (sint32)ceilf(mBloomPyramidSizeF[level].y))
						);

						pyramidTexSize[level] = pyramidSize[level];
						if (!mpManager->AdjustTextureSize(pyramidTexSize[level], false))
							bSuccess = false;
					}

					bool needClear = false;
					if (bSuccess) {
						if (mBloomInputSize != inputSize) {
							mBloomInputSize = inputSize;
							needClear = true;
						}

						for(int level = 0; level < 6; ++level) {
							if (mBloomPyramidSize[level] != pyramidSize[level]) {
								mBloomPyramidSize[level] = pyramidSize[level];

								needClear = true;
							}
						}
					}

					if (mBloomInputTexSize != inputTexSize) {
						vdsaferelease <<= mpD3DBloomInputRTT;
						vdsaferelease <<= mpD3DBloomConvRTT;
						mBloomInputTexSize = inputTexSize;
					}

					for(int level = 0; level < 6; ++level) {
						if (mBloomPyramidTexSize[level] != pyramidTexSize[level]) {
							vdsaferelease <<= mpD3DBloomPyramid1RTT[level];

							if (level < 5)
								vdsaferelease <<= mpD3DBloomPyramid2RTT[level];

							mBloomPyramidTexSize[level] = pyramidTexSize[level];
						}
					}

					// reinit RTTs
					if (bSuccess && !mpD3DBloomInputRTT) {
						hr = mpD3DDevice->CreateTexture(mBloomInputTexSize.w, mBloomInputTexSize.h, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, ~mpD3DBloomInputRTT, nullptr);
						if (FAILED(hr))
							bSuccess = false;

						needClear = true;
					}

					for(int level = 0; level < 6 && bSuccess; ++level) {
						if (!mpD3DBloomPyramid1RTT[level]) {
							hr = mpD3DDevice->CreateTexture(mBloomPyramidTexSize[level].w, mBloomPyramidTexSize[level].h, 1, D3DUSAGE_RENDERTARGET, D3DFMT_X8R8G8B8, D3DPOOL_DEFAULT, ~mpD3DBloomPyramid1RTT[level], nullptr);
							if (FAILED(hr))
								bSuccess = false;

							needClear = true;
						}
					}

					for(int level = 0; level < 5 && bSuccess; ++level) {
						if (bSuccess && !mpD3DBloomPyramid2RTT[level]) {
							hr = mpD3DDevice->CreateTexture(mBloomPyramidTexSize[level].w, mBloomPyramidTexSize[level].h, 1, D3DUSAGE_RENDERTARGET, D3DFMT_X8R8G8B8, D3DPOOL_DEFAULT, ~mpD3DBloomPyramid2RTT[level], nullptr);
							if (FAILED(hr))
								bSuccess = false;

							needClear = true;
						}
					}

					VDASSERT(bSuccess);

					if (needClear) {
						if (mpD3DBloomInputRTT)
							mpManager->ClearRenderTarget(mpD3DBloomInputRTT);

						if (mpD3DBloomConvRTT)
							mpManager->ClearRenderTarget(mpD3DBloomConvRTT);

						for(IDirect3DTexture9 *tex : mpD3DBloomPyramid1RTT) {
							if (tex)
								mpManager->ClearRenderTarget(tex);
						}

						for(IDirect3DTexture9 *tex : mpD3DBloomPyramid2RTT) {
							if (tex)
								mpManager->ClearRenderTarget(tex);
						}
					}

					// compute rendering parameters
					VDDBloomV2ControlParams bloomControlParams {};

					// The blur radius is specified in source pixels in the FXInfo, which must be
					// converted to destination pixels.
					bloomControlParams.mBaseRadius = (float)destW / (float)mSource.pixmap.w;
					bloomControlParams.mAdjustRadius = mScreenFXInfo.mBloomRadius;

					bloomControlParams.mDirectIntensity = mScreenFXInfo.mBloomDirectIntensity;
					bloomControlParams.mIndirectIntensity = mScreenFXInfo.mBloomIndirectIntensity;

					const VDDBloomV2RenderParams bloomRenderParams(VDDComputeBloomV2Parameters(bloomControlParams));

					// input render
					auto ctx2 = ctx;
					ctx2.mViewportX = 0;
					ctx2.mViewportY = 0;

					if (bSuccess) {
						vdrefptr<IDirect3DSurface9> inputSurface;
						hr = mpD3DBloomInputRTT->GetSurfaceLevel(0, ~inputSurface);

						if (vertexProcessor)
							ctx2.mVertexProcessor = &vertexProcessor;

						bSuccess = SUCCEEDED(hr) && mpVideoManager->RunEffect(ctx2, technique, inputSurface);

						ctx2.mVertexProcessor = nullptr;
					}

					// input conversion
					mpD3DDevice->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, TRUE);
					mpD3DDevice->SetSamplerState(1, D3DSAMP_SRGBTEXTURE, TRUE);
					mpD3DDevice->SetRenderState(D3DRS_SRGBWRITEENABLE, TRUE);

					ctx2.mOutputX = 0;
					ctx2.mOutputY = 0;
					ctx.mOutputTessellationX = 1;
					ctx.mOutputTessellationY = 1;
					ctx.mbOutputClear = false;

					ctx2.mbUseUV0Scale = false;
					
					// pyramid down passes
					struct PSConstants {
						vdfloat4 mUVStep;
						vdfloat4 mBlendFactors;
					} psConstants {};

					for(int level = 0; level < 6 && bSuccess; ++level) {
						if (level) {
							ctx2.mpSourceTexture1 = mpD3DBloomPyramid1RTT[level - 1];
							ctx2.mSourceTexW = mBloomPyramidTexSize[level - 1].w;
							ctx2.mSourceTexH = mBloomPyramidTexSize[level - 1].h;
						} else {
							if (mpD3DBloomConvRTT)
								ctx2.mpSourceTexture1 = mpD3DBloomConvRTT;
							else
								ctx2.mpSourceTexture1 = mpD3DBloomInputRTT;

							ctx2.mSourceTexW = mBloomInputTexSize.w;
							ctx2.mSourceTexH = mBloomInputTexSize.h;
						}

						ctx2.mViewportW = mBloomPyramidSize[level].w;
						ctx2.mViewportH = mBloomPyramidSize[level].h;
						ctx2.mOutputW = (float)ctx2.mViewportW;
						ctx2.mOutputH = (float)ctx2.mViewportH;

						ctx2.mSourceArea = vdrect32f(
							0.0f,
							0.0f,
							ctx2.mOutputW * 2.0f,
							ctx2.mOutputH * 2.0f
						);

						psConstants.mUVStep = vdfloat4 {
							1.0f / (float)mBloomPyramidTexSize[level].w,
							1.0f / (float)mBloomPyramidTexSize[level].h,
							(float)mBloomPyramidTexSize[level].h,
							(float)mBloomPyramidTexSize[level].w
						};

						mpD3DDevice->SetPixelShaderConstantF(16, (const float *)&psConstants, sizeof(psConstants)/16);

						vdrefptr<IDirect3DSurface9> pyramidSurface;
						hr = mpD3DBloomPyramid1RTT[level]->GetSurfaceLevel(0, ~pyramidSurface);
						bSuccess = SUCCEEDED(hr) && mpVideoManager->RunEffect(ctx2, g_technique_screenfx_bloomv2_down, pyramidSurface);
					}

					// pyramid up passes
					for(int level = 4; level >= 0 && bSuccess; --level) {
						ctx2.mpSourceTexture1 = level == 4 ? mpD3DBloomPyramid1RTT[5] : mpD3DBloomPyramid2RTT[level + 1];
						ctx2.mpSourceTexture2 = mpD3DBloomPyramid1RTT[level];
						ctx2.mSourceTexW = mBloomPyramidTexSize[level + 1].w;
						ctx2.mSourceTexH = mBloomPyramidTexSize[level + 1].h;
						ctx2.mViewportW = mBloomPyramidSize[level].w;
						ctx2.mViewportH = mBloomPyramidSize[level].h;
						ctx2.mOutputW = (float)ctx2.mViewportW;
						ctx2.mOutputH = (float)ctx2.mViewportH;
						
						ctx2.mSourceArea = vdrect32f(
							0.0f,
							0.0f,
							ctx2.mOutputW * 0.5f,
							ctx2.mOutputH * 0.5f
						);

						ctx2.mbUseUV0Area = true;
						ctx2.mUV0Area = vdrect32f(
							0.0f,
							0.0f,
							(float)ctx2.mOutputW * 0.5f / (float)mBloomPyramidTexSize[level + 1].w,
							(float)ctx2.mOutputH * 0.5f / (float)mBloomPyramidTexSize[level + 1].h
						);

						ctx2.mbUseUV1Area = true;
						ctx2.mUV1Area = vdrect32f(
							0.0f,
							0.0f,
							(float)ctx2.mOutputW / (float)mBloomPyramidTexSize[level].w,
							(float)ctx2.mOutputH / (float)mBloomPyramidTexSize[level].h
						);

						psConstants.mUVStep = vdfloat4 {
							1.0f / (float)mBloomPyramidTexSize[level + 1].w,
							1.0f / (float)mBloomPyramidTexSize[level + 1].h,
							(float)mBloomPyramidTexSize[level + 1].h,
							(float)mBloomPyramidTexSize[level + 1].w
						};

						psConstants.mBlendFactors.x = bloomRenderParams.mPassBlendFactors[4 - level].x;
						psConstants.mBlendFactors.y = bloomRenderParams.mPassBlendFactors[4 - level].y;

						mpD3DDevice->SetPixelShaderConstantF(16, (const float *)&psConstants, sizeof(psConstants)/16);

						vdrefptr<IDirect3DSurface9> pyramidSurface;
						hr = mpD3DBloomPyramid2RTT[level]->GetSurfaceLevel(0, ~pyramidSurface);
						bSuccess = SUCCEEDED(hr) && mpVideoManager->RunEffect(ctx2, g_technique_screenfx_bloomv2_up_noblend, pyramidSurface);
					}

					// final pass
					if (bSuccess) {
						struct PSFinalConstants {
							vdfloat4 mUVStep;
							vdfloat4 mBlendFactors;
							vdfloat4 mShoulderCurve;
							vdfloat4 mThresholds;
							vdfloat4 mBaseUVStep;
							vdfloat4 mBaseWeights;
						} psFinalConstants {};

						psFinalConstants.mUVStep = vdfloat4 {
							1.0f / (float)mBloomPyramidTexSize[0].w,
							1.0f / (float)mBloomPyramidTexSize[0].h,
							(float)mBloomPyramidTexSize[0].h,
							(float)mBloomPyramidTexSize[0].w
						};

						psFinalConstants.mBlendFactors.x = bloomRenderParams.mPassBlendFactors[5].x;
						psFinalConstants.mBlendFactors.y = bloomRenderParams.mPassBlendFactors[5].y;
						psFinalConstants.mShoulderCurve = bloomRenderParams.mShoulder;
						psFinalConstants.mThresholds = bloomRenderParams.mThresholds;

						psFinalConstants.mBaseUVStep = vdfloat4 {
							bloomRenderParams.mBaseUVStepScale / (float)mBloomInputTexSize.w,
							bloomRenderParams.mBaseUVStepScale / (float)mBloomInputTexSize.h,
							0.0f,
							0.0f
						};

						psFinalConstants.mBaseWeights = bloomRenderParams.mBaseWeights;

						mpD3DDevice->SetPixelShaderConstantF(16, (const float *)&psFinalConstants, sizeof(psFinalConstants)/16);

						ctx.mpSourceTexture1 = mpD3DBloomPyramid2RTT[0];
						ctx.mpSourceTexture2 = mpD3DBloomInputRTT;
						ctx.mSourceTexW = mBloomPyramidTexSize[0].w;
						ctx.mSourceTexH = mBloomPyramidTexSize[0].h;
						ctx.mSourceArea = vdrect32f(0, 0, mBloomPyramidSizeF[0].x, mBloomPyramidSizeF[0].y);
						ctx.mOutputX = mDrawRect.left - ctx.mViewportX;
						ctx.mOutputY = mDrawRect.top - ctx.mViewportY;
						ctx.mOutputW = mDrawRect.width();
						ctx.mOutputH = mDrawRect.height();
						ctx.mbUseUV0Scale = false;
						ctx.mbUseUV0Area = true;
						ctx.mbUseUV1Area = true;

						ctx.mUV0Area = vdrect32f(
							0.0f,
							0.0f,
							(float)ctx.mOutputW * 0.5f / (float)mBloomPyramidTexSize[0].w,
							(float)ctx.mOutputH * 0.5f / (float)mBloomPyramidTexSize[0].h
						);

						ctx.mUV1Area = vdrect32f(
							0.0f,
							0.0f,
							(float)ctx.mOutputW / (float)mBloomInputTexSize.w,
							(float)ctx.mOutputH / (float)mBloomInputTexSize.h
						);

						bSuccess = mpVideoManager->RunEffect(ctx,
							g_technique_screenfx_bloomv2_final,
							pRTMain
						);
					}

					mpD3DDevice->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, FALSE);
					mpD3DDevice->SetSamplerState(1, D3DSAMP_SRGBTEXTURE, FALSE);
					mpD3DDevice->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);

					VDASSERT(bSuccess);
				}

				ctx.mbOutputClear = false;
				ctx.mOutputTessellationX = 1;
				ctx.mOutputTessellationY = 1;
			} else {
				if (mPreferredFilter == kFilterPoint) {
					mpManager->SetSwapChainActive(mpSwapChain);
					bSuccess = mpVideoManager->BlitFixedFunction(ctx, pRTMain, false);
				} else if ((mPixelSharpnessX > 1 || mPixelSharpnessY > 1) && mpVideoManager->IsPS20Enabled())
					bSuccess = mpVideoManager->RunEffect(ctx, g_technique_boxlinear_2_0, pRTMain);
				else if ((mPixelSharpnessX > 1 || mPixelSharpnessY > 1) && mbBoxlinearCapable11) {
					if (!InitBoxlinearPS11Filters(ctx.mOutputX, ctx.mOutputW, ctx.mOutputY, ctx.mOutputH, ctx.mViewportW, ctx.mViewportH, mPixelSharpnessX, mPixelSharpnessY))
						mbBoxlinearCapable11 = false;
					else {
						ctx.mpInterpFilter = mpD3DInterpFilterTexture;
						ctx.mInterpTexW = mInterpFilterHTexSize;
						ctx.mInterpTexH = mInterpFilterVTexSize;
						ctx.mbUseUV1Area = true;
						ctx.mUV1Area = mInterpFilterCoords;
					}

					if (ctx.mpInterpFilter)
						bSuccess = mpVideoManager->RunEffect(ctx, g_technique_boxlinear_1_1, pRTMain);
					else {
						mpManager->SetSwapChainActive(mpSwapChain);
						bSuccess = mpVideoManager->BlitFixedFunction(ctx, pRTMain, true);
					}
				} else {
					mpManager->SetSwapChainActive(mpSwapChain);

					bSuccess = mpVideoManager->BlitFixedFunction(ctx, pRTMain, true);
				}
			}
		}
	}

	pRTMain = NULL;

	if (mpCompositor) {
		D3DVIEWPORT9 vp;

		vp.X = 0;
		vp.Y = 0;
		vp.Width = rClippedClient.right;
		vp.Height = rClippedClient.bottom;
		vp.MinZ = 0;
		vp.MaxZ = 1;
		mpD3DDevice->SetViewport(&vp);

		if (mpRenderer->Begin()) {
			mpCompositor->Composite(*mpRenderer, compInfo);
			mpRenderer->End();
		}
	}

	if (mbDisplayDebugInfo || !mErrorString.empty() || (mpCustomPipeline && mpCustomPipeline->HasTimingInfo())) {
		D3DVIEWPORT9 vp;

		vp.X = 0;
		vp.Y = 0;
		vp.Width = rClippedClient.right;
		vp.Height = rClippedClient.bottom;
		vp.MinZ = 0;
		vp.MaxZ = 1;
		mpD3DDevice->SetViewport(&vp);

		if (mpRenderer->Begin()) {
			DrawDebugInfo(mode, rClient);
			mpRenderer->End();
		}
	}

	if (bSuccess && CheckForCapturePending() && mSource.mpCB) {
		VDPixmapBuffer buf;
		IDirect3DDevice9 *d3d9 = mpManager->GetDevice();

		vdrefptr<IDirect3DSurface9> rtsurf;
		HRESULT hr = d3d9->GetRenderTarget(0, ~rtsurf);
		if (SUCCEEDED(hr)) {
			D3DSURFACE_DESC desc {};
			hr = rtsurf->GetDesc(&desc);
			if (SUCCEEDED(hr) && (desc.Format == D3DFMT_A8R8G8B8 || desc.Format == D3DFMT_X8R8G8B8)) {
				uint32 copyw = std::min<uint32>(desc.Width, rClippedClient.right);
				uint32 copyh = std::min<uint32>(desc.Height, rClippedClient.bottom);

				buf.init(copyw, copyh, nsVDPixmap::kPixFormat_XRGB8888);

				vdrefptr<IDirect3DSurface9> rbsurf;
				hr = d3d9->CreateOffscreenPlainSurface(desc.Width, desc.Height, desc.Format, D3DPOOL_SYSTEMMEM, ~rbsurf, nullptr);
				if (SUCCEEDED(hr)) {
					hr = d3d9->GetRenderTargetData(rtsurf, rbsurf);
					if (SUCCEEDED(hr)) {
						D3DLOCKED_RECT lr {};
						hr = rbsurf->LockRect(&lr, nullptr, D3DLOCK_READONLY);
						if (SUCCEEDED(hr)) {
							VDMemcpyRect(buf.data, buf.pitch, lr.pBits, lr.Pitch, 4 * copyw, copyh);

							hr = rbsurf->UnlockRect();
						}
					}
				}
			}
		}

		if (SUCCEEDED(hr)) {
			mSource.mpCB->OnFrameCaptured(&buf);
			mpManager->CheckReturn(hr);
		} else {
			mSource.mpCB->OnFrameCaptured(nullptr);
		}
	}

	if (bSuccess && !mpManager->EndScene())
		bSuccess = false;

	if (updateMode & kModeVSync)
		mpManager->Flush();

	mpManager->SetSwapChainActive(NULL);

	if (!bSuccess) {
		VDDEBUG_DX9DISP("VideoDisplay/DX9: Render failed -- applying boot to the head.");

		if (!mpManager->Reset())
			return false;

	} else {
		mbSwapChainImageValid = true;
		mbSwapChainPresentPending = true;
		mbSwapChainPresentPolling = false;
		mbSwapChainVsyncEventWaiting = false;
	}

	return bSuccess;
}

bool VDVideoDisplayMinidriverDX9::UpdateScreen(const RECT& rClient, UpdateMode updateMode, bool polling) {
	if (!mbSwapChainImageValid)
		return false;

	HRESULT hr;
	if (mbFullScreen) {
		hr = mpManager->PresentFullScreen(!polling && !(updateMode & kModeDoNotWait));

		if (!polling || !mbSwapChainPresentPolling) {
			mPresentHistory.mPresentStartTime = VDGetPreciseTick();
		}

		if (hr == S_OK) {
			mPresentHistory.mAveragePresentTime += ((VDGetPreciseTick() - mPresentHistory.mPresentStartTime)*VDGetPreciseSecondsPerTick() - mPresentHistory.mAveragePresentTime) * 0.01f;
		}

		if (hr == S_FALSE && polling) {
			++mPresentHistory.mPollCount;
			mPresentHistory.mbPresentPending = true;
		} else {
			mPresentHistory.mbPresentPending = false;
		}

	} else {
		bool vsync = (updateMode & kModeVSync) != 0;

		if (mbSwapChainVsync != vsync) {
			mbSwapChainVsync = vsync;

			if (vsync) {
				const auto refreshRate = mpManager->GetDisplayMode().RefreshRate;
				uint32 delay = refreshRate == 0 ? 5 : refreshRate > 50 ? 500 / refreshRate : 10;

				auto *cb = mSource.mpCB;
				mbSwapChainVsyncEvent = mpSwapChain->SetVsyncCallback(mpManager->GetMonitor(), [this, cb]{ OnVsyncEvent(cb); }, delay);
			} else {
				mpSwapChain->SetVsyncCallback(nullptr, {}, 0);
				mbSwapChainVsyncEvent = false;
			}
		}

		// check if we have DXGI vsync
		if (mbSwapChainVsyncEvent) {
			if (!mbSwapChainVsyncEventWaiting) {
				mpSwapChain->RequestVsyncCallback();
				mbSwapChainVsyncEventPending = true;
				mbSwapChainVsyncEventWaiting = true;
				mpManager->Flush();
				return true;
			} else if (mbSwapChainVsyncEventPending) {
				return true;
			} else {
				hr = mpManager->PresentSwapChain(mpSwapChain, &rClient, mhwnd, false, true, false, mSyncDelta, mPresentHistory);
			}
		} else {
			hr = mpManager->PresentSwapChain(mpSwapChain, &rClient, mhwnd, vsync, !polling || !mbSwapChainPresentPolling, polling || (updateMode & kModeDoNotWait) != 0, mSyncDelta, mPresentHistory);
		}
	}

	if (hr == S_FALSE && polling) {
		mbSwapChainPresentPolling = true;
		return true;
	}

	// Workaround for Windows Vista DWM composition chain not updating.
	if (!mbFullScreen && mbFirstPresent) {
		SetWindowPos(mhwnd, NULL, 0, 0, 0, 0, SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_NOZORDER|SWP_FRAMECHANGED);
		mbFirstPresent = false;
	}

	mbSwapChainPresentPending = false;
	mbSwapChainPresentPolling = false;
	mbSwapChainVsyncEventWaiting = false;
	VDASSERT(!mPresentHistory.mbPresentPending);

	if (FAILED(hr)) {
		VDDEBUG_DX9DISP("VideoDisplay/DX9: Render failed in UpdateScreen() with hr=%08X (%s) -- applying boot to the head.", hr, VDDispDecodeD3D9Error(hr));

		// TODO: Need to free all DEFAULT textures before proceeding

		if (!mpManager->Reset())
			return false;
	}

	mSource.mpCB->RequestNextFrame();
	return true;
}

void VDVideoDisplayMinidriverDX9::DrawDebugInfo(FilterMode mode, const RECT& rClient) {
	if (!mpDebugFont) {
		if (mbDebugFontCreated)
			return;

		mbDebugFontCreated = true;
		VDCreateDisplaySystemFont(20, false, "Arial", ~mpDebugFont);
	}

	VDDisplayTextRenderer *r = mpRenderer->GetTextRenderer();
	if (!r)
		return;

	r->Begin();
	r->SetAlignment(r->kAlignLeft, r->kVertAlignTop);
	r->SetFont(mpDebugFont);

	if (mbDisplayDebugInfo) {
		const char *modestr = "point";

		switch(mode) {
			case kFilterBilinear:
				modestr = "bilinear";
				break;
			case kFilterBicubic:
				modestr = "bicubic";
				break;
		}

		GetFormatString(mSource, mFormatString);
		mDebugString.sprintf("Direct3D9%s minidriver - %s (%s)  Average present time: %6.2fms"
			, mpManager->GetDeviceEx() ? "Ex" : ""
			, mFormatString.c_str()
			, modestr
			, mPresentHistory.mAveragePresentTime * 1000.0);

		r->SetColorRGB(0xFFFF00);
		r->DrawTextLine(10, rClient.bottom - 40, VDTextAToW(mDebugString).c_str());

		mDebugString.sprintf("Target scanline: %7.2f  Average bracket [%7.2f,%7.2f]  Last bracket [%4d,%4d]  Poll count %5d"
				, mPresentHistory.mScanlineTarget
				, mPresentHistory.mAverageStartScanline
				, mPresentHistory.mAverageEndScanline
				, mPresentHistory.mLastBracketY1
				, mPresentHistory.mLastBracketY2
				, mPresentHistory.mPollCount);
		mPresentHistory.mPollCount = 0;
		r->DrawTextLine(10, rClient.bottom - 20, VDTextAToW(mDebugString).c_str());
	}

	if (!mErrorString.empty()) {
		r->SetColorRGB(0xFF4040);
		r->DrawTextLine(10, rClient.bottom - 60, VDTextAToW(mErrorString).c_str());
	}

	if (mpCustomPipeline && mpCustomPipeline->HasTimingInfo()) {
		const auto passInfos = mpCustomPipeline->GetPassTimings();

		if (!passInfos.empty()) {
			r->SetColorRGB(0xFFFF00);

			const uint32 numTimings = (uint32)passInfos.size();
			for(uint32 i=0; i<numTimings; ++i) {
				if (i + 1 == numTimings)
					mDebugString.sprintf("Total: %7.2fms %ux%u", passInfos[i].mTiming * 1000.0f, mbDestRectEnabled ? mDestRect.width() : rClient.right, mbDestRectEnabled ? mDestRect.height() : rClient.bottom);
				else {
					const auto& passInfo = passInfos[i];

					mDebugString.sprintf("Pass #%-2u: %7.2fms %ux%u %s%s%s%s"
						, i + 1
						, passInfo.mTiming * 1000.0f
						, passInfo.mOutputWidth
						, passInfo.mOutputHeight
						, passInfo.mbOutputLinear ? "linear" : "point"
						, passInfo.mbOutputFloat ? passInfos[i].mbOutputHalfFloat ? " half" : " float" : ""
						, passInfo.mbOutputSrgb ? " sRGB" : ""
						, passInfo.mbCached ? " cached" : ""
					);
				}

				r->DrawTextLine(10, 10 + 14*i, VDTextAToW(mDebugString).c_str());
			}
		}
	}

	r->End();
}

#undef VDDEBUG_DX9DISP
#undef D3D_DO
