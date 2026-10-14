//	VirtualDub - Video processing and capture application
//	Copyright (C) 1998-2025 Avery Lee
//
//	This program is free software; you can redistribute it and/or
//	modify it under the terms of the GNU General Public License
//	as published by the Free Software Foundation; either version 2
//	of the License, or (at your option) any later version.
//
//	This program is distributed in the hope that it will be useful,
//	but WITHOUT ANY WARRANTY; without even the implied warranty of
//	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//	GNU General Public License for more details.
//
//	You should have received a copy of the GNU General Public License along
//	with this program. If not, see <http://www.gnu.org/licenses/>.

#include <d3dcommon.h>
#include <d3d10.h>
#include <vd2/system/binary.h>
#include <vd2/system/error.h>
#include <vd2/system/file.h>
#include <vd2/system/filesys.h>
#include <vd2/system/seh.h>
#include <vd2/system/vdalloc.h>
#include <vd2/system/VDString.h>
#include <vd2/system/vectors.h>
#include <vd2/system/vdstl_vectorview.h>
#include <vd2/system/w32assist.h>
#include <vd2/Kasumi/pixmap.h>
#include <vd2/Kasumi/pixmapops.h>
#include <vd2/Kasumi/pixmaputils.h>
#include <vd2/VDDisplay/display.h>
#include <vd2/VDDisplay/minid3dx.h>
#include <vd2/VDDisplay/internal/customeffectd3d9.h>
#include <vd2/VDDisplay/internal/customeffectimpld3d9.h>
#include <vd2/VDDisplay/internal/customeffectutils.h>
#include <vd2/VDDisplay/internal/customeffectutilsd3d.h>

class VDD3D9Exception : public MyError {
public:
	VDD3D9Exception(uint32 hr) : MyError("Direct3D error: %08X", hr) {}
};

VDDisplayCustomShaderD3D9::VDDisplayCustomShaderD3D9(uint32 passIndex, VDD3D9Manager *d3d9mgr)
	: VDDCustomEffectPassBase(passIndex)
	, mpD3DMgr(d3d9mgr)
{
	d3d9mgr->Attach(this);
}

VDDisplayCustomShaderD3D9::~VDDisplayCustomShaderD3D9() {
	// do not release custom textures -- they are aliased against master table
	mCustomTextures.clear();

	mpD3DMgr->Detach(this);
}

void VDDisplayCustomShaderD3D9::Init(
	const char *shaderPath8,
	const VDDisplayCustomShaderProps& propLookup,
	const vdhashmap<VDStringA, TextureSpec>& customTextureLookup,
	vdspan<const bool> passInputsFiltered,
	vdspan<const bool> passInputsSrgb,
	const wchar_t *basePath,
	vdspan<uint32> maxPrevFrames,
	VDDCustomEffectVarStorage& varStorage)
{
	const auto shaderPath = VDTextU8ToW(VDStringSpanA(shaderPath8));
	const auto shaderPrefix = VDFileSplitExtLeftSpan(shaderPath);

	// attempt to read in the vertex and pixel shaders
	VDDCustomEffectCompilerD3D compiler;

	vdfastvector<uint32> vsbytecode;
	vdfastvector<uint32> psbytecode;

	const bool shaderPrecompile = propLookup.GetBool(VDDCsPropKeyView("shader_precompile", nullptr), false);
	const auto& caps = mpD3DMgr->GetCaps();

	for(int i=0; i<2; ++i) {
		const char *shaderType = i ? "pixel" : "vertex";
		const auto precompiledPath = VDMakePath(VDStringSpanW(basePath), shaderPrefix + (i ? L"-d3d9.psh" : L"-d3d9.vsh"));
		auto& bytecode = i ? psbytecode : vsbytecode;
			
		// try to load a precompiled shader first
		if (!shaderPrecompile) {
			VDFile pref;
			if (pref.tryOpen(precompiledPath.c_str())) {
				auto size = pref.size();

				if (size > 0x10000)
					throw VDException(L"Pass %u %hs shader '%ls' is too large.", mPassIndex, shaderType, precompiledPath.c_str());

				uint32 size4 = (uint32)size >> 2;
				bytecode.resize(size4);
				pref.read(bytecode.data(), size4 << 2);

				// check for malformed shaders
				const uint32 versionBase = i ? 0xFFFF0000 : 0xFFFE0000;
				const uint32 versionLo = versionBase + 0x0101;		// vs/ps_1_1
				const uint32 versionHi = versionBase + 0x0300;		// vs/ps_3_0
				if (!VDD3DXCheckShaderSize(bytecode.data(), size4 * 4) || bytecode[0] < versionLo || bytecode[0] > versionHi)
					throw VDException(L"Pass %u: invalid precompiled %hs shader '%ls'", mPassIndex, shaderType, precompiledPath.c_str());

				continue;
			}
		}

		// no dice... try fx specific next
		auto fxPath = VDMakePath(VDStringSpanW(basePath), shaderPrefix + L".fx");
		if (!VDDoesPathExist(fxPath.c_str())) {
			// nope... try original
			fxPath = VDMakePath(VDStringSpanW(basePath), shaderPath);
			if (!VDDoesPathExist(fxPath.c_str()))
				throw VDException(L"Pass %u %hs shader: cannot find '%ls' or a precompiled/HLSL specific version", mPassIndex, shaderType, shaderPath.c_str());
		}

		const char *profile = propLookup.GetString(VDDCsPropKeyView("shader_profile_d3d9_", mPassIndex));

		if (!profile)
			profile = propLookup.GetString(VDDCsPropKeyView("shader_profile_d3d", 9));

		if (profile) {
			if (!i && (!strcmp(profile, "2_a") || !strcmp(profile, "2_b")))
				profile = "2_0";
		} else {
			if (i) {
				// pixel shader
				if (caps.PixelShaderVersion >= D3DPS_VERSION(3, 0))
					profile = "3_0";
				else if (caps.PixelShaderVersion >= D3DPS_VERSION(2, 0)) {
					profile = "2_0";

					if (caps.PS20Caps.Caps && caps.PS20Caps.NumTemps >= 22) {
						// predication and >=22 temps supported (NVIDIA GeForce FX)
						profile = "2_a";
					} else if (caps.PS20Caps.NumTemps >= 32) {
						// >= 32 temps supported (ATI Radeon 9xxx)
						profile = "2_b";
					}
				} else
					throw VDException("Graphics device does not support pixel shaders.");

			} else {
				// vertex shader
				if (caps.VertexShaderVersion >= D3DVS_VERSION(3, 0))
					profile = "3_0";
				else
					profile = "2_0";
			}
		}

		try {
			const auto& outputBlob = compiler.Compile(fxPath.c_str(), i ? "main_fragment" : "main_vertex", (VDStringA(i ? "ps_" : "vs_") + profile).c_str(), false, false);

			size_t len = outputBlob.size() >> 2;
			bytecode.resize(len);
			memcpy(bytecode.data(), outputBlob.data(), len*4);
		} catch(const VDDCustomShaderCompileException& ex) {
			throw VDException(L"Pass %u %hs shader: %ls", mPassIndex, shaderType, ex.wc_str());
		}

		if (shaderPrecompile) {
			VDFile fout(precompiledPath.c_str(), nsVDFile::kWrite | nsVDFile::kDenyAll | nsVDFile::kCreateAlways);

			fout.write(bytecode.data(), (sint32)(bytecode.size() * sizeof(bytecode[0])));
		}
	}

	// check for mismatched shaders -- must be both <3.0 or both 3.0
	uint32 vsversion = vsbytecode[0];
	uint32 psversion = psbytecode[0];
	const bool vs3 = vsversion >= 0xFFFE0300;
	const bool ps3 = psversion >= 0xFFFF0300;

	if (vs3 != ps3)
		throw MyError("Pass %u has mismatched shaders -- cannot mix shader model 1/2 shaders with shader model 3 shaders", mPassIndex);

	// check if we can actually run the shaders
	if (vsversion > caps.VertexShaderVersion)
		throw MyError("Pass %u requires a vertex shader version greater than supported by graphics device (%08X > %08X)", mPassIndex, vsversion, caps.VertexShaderVersion);

	if (psversion > caps.PixelShaderVersion)
		throw MyError("Pass %u requires a pixel shader version greater than supported by graphics device (%08X > %08X)", mPassIndex, psversion, caps.PixelShaderVersion);

	IDirect3DDevice9 *const dev = mpD3DMgr->GetDevice();
	HRESULT hr = dev->CreateVertexShader((const DWORD *)vsbytecode.data(), ~mpVertexShader);
	if (hr != D3D_OK)
		throw VDD3D9Exception(hr);

	hr = dev->CreatePixelShader((const DWORD *)psbytecode.data(), ~mpPixelShader);
	if (hr != D3D_OK)
		throw VDD3D9Exception(hr);

	mbPassCanBeCached = true;

	mbVSConstantTableInited = ProcessShader(vsbytecode.data(), (uint32)(vsbytecode.size() * 4), mVertexShaderInfo, customTextureLookup, maxPrevFrames, varStorage);
	mbPSConstantTableInited = ProcessShader(psbytecode.data(), (uint32)(psbytecode.size() * 4), mPixelShaderInfo, customTextureLookup, maxPrevFrames, varStorage);

	std::ranges::sort(mPassInputReferenceTable);
	mPassInputReferenceTable.erase(std::unique(mPassInputReferenceTable.begin(), mPassInputReferenceTable.end()), mPassInputReferenceTable.end());

	ParseCommonProps(propLookup);

	// init filtering
	for(const auto& texBinding : mTextureBindings) {
		const auto refType = texBinding.mTexKey.mType;
		const auto refPassIndex = texBinding.mTexKey.mPassIndex;
		const auto refTextureIndex = texBinding.mTexKey.mTextureIndex;

		const uint32 clampToBorder = (mpD3DMgr->GetCaps().TextureAddressCaps & D3DPTADDRESSCAPS_BORDER) ? D3DTADDRESS_BORDER : D3DTADDRESS_CLAMP;

		if (refType == VDDCETextureType::PassInput) {
			const bool refLinear = (refPassIndex == mPassIndex) ? mbFilterInput : passInputsFiltered[refPassIndex];

			const SamplerState newStates[] = {
				{ texBinding.mStage, D3DSAMP_ADDRESSU, clampToBorder },
				{ texBinding.mStage, D3DSAMP_ADDRESSV, clampToBorder },
				{ texBinding.mStage, D3DSAMP_MAGFILTER, refLinear ? (uint32)D3DTEXF_LINEAR : (uint32)D3DTEXF_POINT },
				{ texBinding.mStage, D3DSAMP_MINFILTER, refLinear ? (uint32)D3DTEXF_LINEAR : (uint32)D3DTEXF_POINT },
				{ texBinding.mStage, D3DSAMP_MIPFILTER, D3DTEXF_NONE },
				{ texBinding.mStage, D3DSAMP_MIPMAPLODBIAS, 0 },
				{ texBinding.mStage, D3DSAMP_SRGBTEXTURE, passInputsSrgb[mPassIndex] ? (DWORD)TRUE : (DWORD)FALSE },
				{ texBinding.mStage, D3DSAMP_BORDERCOLOR, 0 },
			};

			mSamplerStates.insert(mSamplerStates.end(), std::begin(newStates), std::end(newStates));

			if (maxPrevFrames[refPassIndex] < refTextureIndex)
				maxPrevFrames[refPassIndex] = refTextureIndex;
		} else if (refType == VDDCETextureType::Custom) {
			const auto& texInfo = mCustomTextures[refTextureIndex];

			const SamplerState newStates[] = {
				{ texBinding.mStage, D3DSAMP_ADDRESSU, clampToBorder },
				{ texBinding.mStage, D3DSAMP_ADDRESSV, clampToBorder },
				{ texBinding.mStage, D3DSAMP_MAGFILTER, texInfo.mbLinear ? (uint32)D3DTEXF_LINEAR : (uint32)D3DTEXF_POINT },
				{ texBinding.mStage, D3DSAMP_MINFILTER, texInfo.mbLinear ? (uint32)D3DTEXF_LINEAR : (uint32)D3DTEXF_POINT },
				{ texBinding.mStage, D3DSAMP_MIPFILTER, D3DTEXF_NONE },
				{ texBinding.mStage, D3DSAMP_MIPMAPLODBIAS, 0 },
				{ texBinding.mStage, D3DSAMP_SRGBTEXTURE, texInfo.mbSrgb ? (DWORD)TRUE : (DWORD)FALSE },
				{ texBinding.mStage, D3DSAMP_BORDERCOLOR, 0 },
			};

			mSamplerStates.insert(mSamplerStates.end(), std::begin(newStates), std::end(newStates));
		}
	}

	mRenderStates.push_back(RenderState { D3DRS_SRGBWRITEENABLE, mbSrgbFramebuffer ? (DWORD)TRUE : (DWORD)FALSE });
}

void VDDisplayCustomShaderD3D9::ResetVariables(VDDCustomEffectVarStorage& varStorage, uint32 prevOutputFramesNeeded, bool outputFilteringNeeded) {
	mOutputTextures.resize(prevOutputFramesNeeded + 1);


	varStorage.ResolvePassParamOffsets(mPassParamOffsets, mPassIndex);

	const vdfloat4 frameDirection(1.0f, 0.0f, 0.0f, 0.0f);
	varStorage.SetVector(mPassParamOffsets.mFrameDirection, frameDirection);

	uint32 frameIndex = 0;
	for(TextureSpec& outputTexSpec : mOutputTextures) {
		outputTexSpec.mbLinear = outputFilteringNeeded;

		varStorage.ResolveFrameParamOffsets(outputTexSpec.mFrameParams, mPassIndex + 1, frameIndex++);
	}
}

bool VDDisplayCustomShaderD3D9::Run(const vdrect32f *dstRect, vdspan<const VDDTexSpecView<const TextureSpec>> texSpecGrid, const vdint2& viewportSize, bool lastStage,
	vdspan<bool> inputInvalidationTable,
	VDDCustomEffectVarStorage& varStorage)
{
	const TextureSpec& srcTexSpec = texSpecGrid[mPassIndex][0];
	const vdint2 srcTexSize = { (sint32)srcTexSpec.mTexWidth, (sint32)srcTexSpec.mTexHeight };
	const vdint2 srcImageSize = { (sint32)srcTexSpec.mImageWidth, (sint32)srcTexSpec.mImageHeight };
	const vdrect32f srcRect = { 0.0f, 0.0f, (float)srcTexSpec.mImageWidth / (float)srcTexSpec.mTexWidth, srcTexSpec.mImageHeight / (float)srcTexSpec.mTexHeight };

	IDirect3DDevice9 *const dev = mpD3DMgr->GetDevice();
	HRESULT hr;

	auto [renderWidth, renderHeight] = ComputeRenderSize(srcImageSize, viewportSize);

	// update pass parameters
	varStorage.SetVector(mPassParamOffsets.mVideoSize, vdfloat4 {
		(float)srcTexSpec.mImageWidth,
		(float)srcTexSpec.mImageHeight,
		0.0f,
		0.0f			
	});

	varStorage.SetVector(mPassParamOffsets.mTextureSize, vdfloat4 {
		(float)srcTexSpec.mTexWidth,
		(float)srcTexSpec.mTexHeight,
		0.0f,
		0.0f			
	});

	// This explicitly can't be the current pass output size or whole chain size;
	// some old shaders like VINTAGE TV otherwise break.
	varStorage.SetVector(mPassParamOffsets.mOutputSize, vdfloat4 {
		(float)srcTexSpec.mImageWidth,
		(float)srcTexSpec.mImageHeight,
		0.0f,
		0.0f			
	});

	uint32 frame = mFrame;
	for(auto& inputFrame : texSpecGrid[mPassIndex]) {
		varStorage.SetVector(inputFrame.mFrameParams.mFrameCount, vdfloat4 {
			(float)frame,
			0.0f,
			0.0f,
			0.0f
		});

		if (frame)
			--frame;
	}

	// update input frame parameters
	if (lastStage || (mPrevSrcWidth != srcTexSize.x || mPrevSrcHeight != srcTexSize.y)) {
		for(auto& outputTex : mOutputTextures) {
			outputTex.mpTextureRef = nullptr;
			outputTex.mpSurfaceRef = nullptr;
			outputTex.mpTexture = nullptr;
		}
	}

	bool needTextureInit = false;
	for(const auto& outputTex : mOutputTextures) {
		if (!outputTex.mpTexture) {
			needTextureInit = true;
			break;
		}
	}

	// if we didn't need a texture init and the pass is cacheable, check if we need
	// to re-run the pass
	if (!needTextureInit && mbPassCanBeCached) {
		bool inputInvalidated = false;

		for(const auto& inputRef : mPassInputReferenceTable) {
			if (inputInvalidationTable[inputRef]) {
				inputInvalidated = true;
				break;
			}
		}

		if (!inputInvalidated)
			return false;
	}

	// reinitialize textures as necessary
	bool clearRT = false;
	if (!lastStage && needTextureInit) {
		int w = renderWidth;
		int h = renderHeight;

		if (!mpD3DMgr->AdjustTextureSize(w, h))
			throw VDD3D9Exception(E_FAIL);

		D3DFORMAT fmt = D3DFMT_A8R8G8B8;

		if (mbFloatFramebuffer) {
			if (mbHalfFloatFramebuffer) {
				if (mpD3DMgr->IsTextureFormatAvailable(D3DFMT_A16B16G16R16F))
					fmt = D3DFMT_A16B16G16R16F;
				else if (mpD3DMgr->IsTextureFormatAvailable(D3DFMT_A32B32G32R32F))
					fmt = D3DFMT_A32B32G32R32F;
			} else {
				if (mpD3DMgr->IsTextureFormatAvailable(D3DFMT_A32B32G32R32F))
					fmt = D3DFMT_A32B32G32R32F;
				else if (mpD3DMgr->IsTextureFormatAvailable(D3DFMT_A16B16G16R16F))
					fmt = D3DFMT_A16B16G16R16F;
			}
		}

		for(auto& outputTex : mOutputTextures) {
			outputTex.mTexWidth = w;
			outputTex.mTexHeight = h;

			outputTex.mpTexture = nullptr;
			outputTex.mpTextureRef = nullptr;
			outputTex.mpSurfaceRef = nullptr;

			hr = dev->CreateTexture(w, h, 1, D3DUSAGE_RENDERTARGET, fmt, D3DPOOL_DEFAULT, ~outputTex.mpTextureRef, nullptr);
			if (hr != D3D_OK)
				throw VDD3D9Exception(hr);

			hr = outputTex.mpTextureRef->GetSurfaceLevel(0, ~outputTex.mpSurfaceRef);
			if (hr != D3D_OK) {
				vdsaferelease <<= outputTex.mpTexture;
				throw VDD3D9Exception(hr);
			}

			outputTex.mpTexture = outputTex.mpTextureRef;
		}

		clearRT = true;
	}

	mPrevSrcWidth = srcTexSize.x;
	mPrevSrcHeight = srcTexSize.y;

	for(auto& outputTex : mOutputTextures) {
		outputTex.mImageWidth = renderWidth;
		outputTex.mImageHeight = renderHeight;
	}

	hr = dev->SetVertexShader(mpVertexShader);
	if (hr != D3D_OK)
		throw VDD3D9Exception(hr);

	hr = dev->SetPixelShader(mpPixelShader);
	if (hr != D3D_OK)
		throw VDD3D9Exception(hr);

	UpdateVariables(mVertexShaderInfo, varStorage);
	UploadShaderData<const BOOL, &IDirect3DDevice9::SetVertexShaderConstantB>(dev, mVertexShaderInfo.mUploadSpansB, mVertexShaderInfo.mConstantBuffer.data());
	UploadShaderData<const int, &IDirect3DDevice9::SetVertexShaderConstantI>(dev, mVertexShaderInfo.mUploadSpansI, mVertexShaderInfo.mConstantBuffer.data());
	UploadShaderData<const float, &IDirect3DDevice9::SetVertexShaderConstantF>(dev, mVertexShaderInfo.mUploadSpansF, mVertexShaderInfo.mConstantBuffer.data());
	UpdateVariables(mPixelShaderInfo, varStorage);
	UploadShaderData<const BOOL, &IDirect3DDevice9::SetPixelShaderConstantB>(dev, mPixelShaderInfo.mUploadSpansB, mPixelShaderInfo.mConstantBuffer.data());
	UploadShaderData<const int, &IDirect3DDevice9::SetPixelShaderConstantI>(dev, mPixelShaderInfo.mUploadSpansI, mPixelShaderInfo.mConstantBuffer.data());
	UploadShaderData<const float, &IDirect3DDevice9::SetPixelShaderConstantF>(dev, mPixelShaderInfo.mUploadSpansF, mPixelShaderInfo.mConstantBuffer.data());

	for(const auto& rs : mRenderStates) {
		hr = dev->SetRenderState((D3DRENDERSTATETYPE)rs.mState, rs.mValue);
		if (hr != D3D_OK)
			throw VDD3D9Exception(hr);
	}

	for(const auto& ss : mSamplerStates) {
		hr = dev->SetSamplerState(ss.mStage, (D3DSAMPLERSTATETYPE)ss.mState, ss.mValue);
		if (hr != D3D_OK)
			throw VDD3D9Exception(hr);
	}

	for(const auto& binding : mTextureBindings) {
		IDirect3DTexture9 *tex = nullptr;

		switch(binding.mTexKey.mType) {
			case VDDCETextureType::PassInput:
				tex = texSpecGrid[binding.mTexKey.mPassIndex][binding.mTexKey.mTextureIndex].mpTexture;
				break;

			case VDDCETextureType::Custom:
				tex = mCustomTextures[binding.mTexKey.mTextureIndex].mpTexture;
				break;
		}

		hr = dev->SetTexture(binding.mStage, tex);

		if (hr != D3D_OK)
			throw VDD3D9Exception(hr);
	}

	auto& output = mOutputTextures[0];
	if (output.mpSurfaceRef) {
		hr = dev->SetRenderTarget(0, output.mpSurfaceRef);
		if (hr != D3D_OK)
			throw VDD3D9Exception(hr);

		if (clearRT) {
			hr = dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 0, 0);
			if (hr != D3D_OK)
				throw VDD3D9Exception(hr);
		}
	}

	if (!mpD3DMgr->BeginScene())
		throw VDD3D9Exception(E_FAIL);

	D3DVIEWPORT9 vp;
	hr = dev->GetViewport(&vp);
	if (hr != D3D_OK)
		throw VDD3D9Exception(hr);

	sint32 xcen2 = !dstRect ? (sint32)vp.Width : (sint32)(dstRect->left + dstRect->right);
	sint32 ycen2 = !dstRect ? (sint32)vp.Height : (sint32)(dstRect->top + dstRect->bottom);
	sint32 xoffset = output.mpTexture ? 0 : (xcen2 - (sint32)renderWidth) >> 1;
	sint32 yoffset = output.mpTexture ? 0 : (ycen2 - (sint32)renderHeight) >> 1;
	const float xscale = 2.0f / (float)vp.Width;
	const float yscale = -2.0f / (float)vp.Height;

	float x0 = ((float)xoffset - 0.5f) * xscale - 1.0f;
	float y0 = ((float)yoffset - 0.5f) * yscale + 1.0f;
	float x1 = x0 + (float)renderWidth * xscale;
	float y1 = y0 + (float)renderHeight * yscale;

	const float srcu0 = srcRect.left;
	const float srcv0 = srcRect.top;
	const float srcu1 = srcRect.right;
	const float srcv1 = srcRect.bottom;
	const float auxu0 = 0;
	const float auxu1 = 1;
	const float auxv0 = 0;
	const float auxv1 = 1;

	auto *vx = mpD3DMgr->LockVertices(4);
	vd_seh_guard_try {
		vx[0].SetFF2(x0, y0, UINT32_C(0xFFFFFFFF), srcu0, srcv0, auxu0, auxv0);
		vx[1].SetFF2(x0, y1, UINT32_C(0xFFFFFFFF), srcu0, srcv1, auxu0, auxv1);
		vx[2].SetFF2(x1, y0, UINT32_C(0xFFFFFFFF), srcu1, srcv0, auxu1, auxv0);
		vx[3].SetFF2(x1, y1, UINT32_C(0xFFFFFFFF), srcu1, srcv1, auxu1, auxv1);
	} vd_seh_guard_except {
		mpD3DMgr->UnlockVertices();
		throw VDD3D9Exception(E_FAIL);
	}

	mpD3DMgr->UnlockVertices();

	hr = dev->SetStreamSource(0, mpD3DMgr->GetVertexBuffer(), 0, sizeof(nsVDD3D9::Vertex));
	if (hr != D3D_OK)
		throw VDD3D9Exception(hr);

	hr = dev->SetVertexDeclaration(mpD3DMgr->GetVertexDeclaration());
	if (hr != D3D_OK)
		throw VDD3D9Exception(hr);

	hr = mpD3DMgr->DrawArrays(D3DPT_TRIANGLESTRIP, 0, 2);
	if (hr != D3D_OK)
		throw VDD3D9Exception(hr);

	if (output.mpSurfaceRef) {
		if (!mpD3DMgr->EndScene())
			throw VDD3D9Exception(E_FAIL);
	}

	// clear texture stages
	for (int i=0; i<16; ++i) {
		hr = dev->SetTexture(i, nullptr);
		if (hr != D3D_OK)
			throw VDD3D9Exception(hr);
	}

	// reset SRGBWRITEENABLE if it was set
	if (mbSrgbFramebuffer) {
		hr = dev->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
		if (hr)
			throw VDD3D9Exception(hr);
	}

	// bump frame counter
	if (mFrame >= mFrameCountLimit)
		mFrame = 0;
	else
		++mFrame;

	return true;
}

VDDTexSpecView<const VDDisplayCustomShaderD3D9::TextureSpec> VDDisplayCustomShaderD3D9::GetOutputTexSpecs() const {
	return VDDTexSpecView<const VDDisplayCustomShaderD3D9::TextureSpec>(std::from_range, mOutputTextures);
}

void VDDisplayCustomShaderD3D9::OnPreDeviceReset() {
	for(auto& outputTex : mOutputTextures) {
		outputTex.mpTexture = nullptr;
		vdsaferelease <<= outputTex.mpSurfaceRef;
		vdsaferelease <<= outputTex.mpTextureRef;
	}
}

void VDDisplayCustomShaderD3D9::OnPostDeviceReset() {
}

void VDDisplayCustomShaderD3D9::UpdateVariables(ShaderInfo& shaderInfo, VDDCustomEffectVarStorage& varStorage) {
	varStorage.GatherBools(shaderInfo.mConstantBuffer.data(), shaderInfo.mConstantBufferBoolGathers);
	varStorage.GatherVecs(shaderInfo.mConstantBuffer.data(), shaderInfo.mConstantBufferVecGathers);
}

void VDDisplayCustomShaderD3D9::ConvertBoolData(vdspan<uint32> dst, const float *src) {
	size_t numRegs = dst.size() >> 2;

	for(size_t i = 0; i < numRegs; ++i) {
		dst[i] = (*src != 0);
		src += 4;
	}
}

template<class T, HRESULT (__stdcall IDirect3DDevice9::*T_UploadFn)(UINT, T *, UINT)>
void VDDisplayCustomShaderD3D9::UploadShaderData(IDirect3DDevice9 *dev, const vdfastvector<UploadSpan>& spans, const void *src) {
	for(const auto& span : spans) {
		HRESULT hr = (dev->*T_UploadFn)(span.mStart, (T *)((const char *)src + span.mSrcOffset), span.mCount);

		if (hr != D3D_OK)
			throw VDD3D9Exception(hr);
	}
}

bool VDDisplayCustomShaderD3D9::ProcessShader(const uint32 *shader, uint32 shaderSize, ShaderInfo& shaderInfo, const vdhashmap<VDStringA, TextureSpec>& customTextureLookup, vdspan<uint32> maxPrevFrames, VDDCustomEffectVarStorage& varStorage) {
	VDD3DXConstantTable ct;

	if (!VDD3DXGetShaderConstantTable(shader, shaderSize, ct))
		return false;

	const uint32 numParams = ct.GetParameterCount();

	// process sampler bindings
	for(uint32 i=0; i<numParams; ++i) {
		const auto param = ct.GetParameter(i);

		if (param.GetRegisterSet() != kVDD3DXRegisterSet_Sampler)
			continue;
		
		const uint32 regIndex = param.GetRegisterIndex();

		VDStringSpanA name(param.GetName());
		VDDCETextureKey texKey {};

		VDDCustomEffectFrameRef frameRef {};
		
		if (name.ends_with("_texture"))
			frameRef = VDDCustomEffectFrameRef::Parse(name.subspan(0, name.size() - 8), mPassIndex);

		if (frameRef.mbValid) {
			texKey.mType = VDDCETextureType::PassInput;
			texKey.mPassIndex = frameRef.mPassIndex;
			texKey.mTextureIndex = frameRef.mElementIndex;
		} else {
			auto itCustom = customTextureLookup.find_as(VDStringSpanA(param.GetName()));

			if (itCustom != customTextureLookup.end()) {
				IDirect3DTexture9 *tex = itCustom->second.mpTexture;

				auto itTexReg = std::find_if(mCustomTextures.begin(), mCustomTextures.end(), [tex](const TextureSpec& ts) { return ts.mpTexture == tex; });

				texKey.mType = VDDCETextureType::Custom;
				texKey.mTextureIndex = (uint32)(itTexReg - mCustomTextures.end());

				if (itTexReg == mCustomTextures.end())
					mCustomTextures.push_back(itCustom->second);
			} else {
				// This is not a texture we know about. If it is in t0/s0, map it to IN.
				if (regIndex == 0) {
					texKey.mType = VDDCETextureType::PassInput;
					texKey.mPassIndex = mPassIndex;
				}
			}
		}

		switch(texKey.mType) {
			case VDDCETextureType::PassInput:
				mPassInputReferenceTable.push_back(texKey.mPassIndex);
				break;

			default:
				break;
		}

		mTextureBindings.push_back({ regIndex, texKey });
	}

	// Compute constant buffer size. Note that we must include parameters even if
	// they have no variable mapping -- we still want to upload safe defaults for them.
	uint32 totalBools = 0;
	uint32 totalInts = 0;
	uint32 totalFloats = 0;
	vdfastvector<VDD3DXCTParameter> parameters;

	for(uint32 i=0; i<numParams; ++i) {
		auto param = ct.GetParameter(i);
		uint32_t rc = param.GetRegisterCount();

		switch(param.GetRegisterSet()) {
			case kVDD3DXRegisterSet_Bool:
				totalBools += rc;
				break;

			case kVDD3DXRegisterSet_Float4:
				totalFloats += rc;
				break;

			case kVDD3DXRegisterSet_Int4:
				totalInts += rc;
				break;

			default:
				break;
		}

		parameters.emplace_back(std::move(param));
	}

	totalBools = (totalBools + 3) & ~(uint32)3;

	totalFloats *= 4;
	totalInts *= 4;

	const uint32 totalElements = totalBools + totalInts + totalFloats;

	shaderInfo.mConstantBuffer.resize(totalElements, 0);

	// loop over the params again and copy default parameter data
	for(uint32 paramTypeIndex = 0; paramTypeIndex < 3; ++paramTypeIndex) {
		const VDD3DXRegisterSet registerSet
			= paramTypeIndex == 0 ? kVDD3DXRegisterSet_Bool
			: paramTypeIndex == 1 ? kVDD3DXRegisterSet_Float4
			: kVDD3DXRegisterSet_Int4;

		vdfastvector<UploadSpan>& uploadSpans
			= paramTypeIndex == 0 ? shaderInfo.mUploadSpansB
			: paramTypeIndex == 1 ? shaderInfo.mUploadSpansF
			: shaderInfo.mUploadSpansI;

		vdfastvector<VDD3DXCTParameter> sortedParams;

		// filter only the parameter type being handled this pass
		for(uint32 i=0; i<numParams; ++i) {
			const auto param = ct.GetParameter(i);

			if (param.GetRegisterSet() == registerSet && param.GetRegisterCount() > 0 && param.GetRegisterIndex() < 0x10000)
				sortedParams.emplace_back(std::move(param));
		}

		// early out if no parameters of this type
		if (sortedParams.empty())
			continue;

		// sort parameters by ascending register
		std::sort(sortedParams.begin(), sortedParams.end(),
			[](const VDD3DXCTParameter& a, const VDD3DXCTParameter& b) {
				return a.GetRegisterIndex() < b.GetRegisterIndex();
			}
		);

		// build upload span list, compile gathers, and copy in default values
		uint32 nextRegIndex = ~sortedParams[0].GetRegisterIndex();
		uint32 cbOffset = 0;
		uint32 registerSize = (registerSet == kVDD3DXRegisterSet_Bool) ? 4 : 16;

		for(const VDD3DXCTParameter& param : sortedParams) {
			if (param.GetRegisterIndex() != nextRegIndex) {
				uploadSpans.emplace_back(
					UploadSpan {
						.mStart = param.GetRegisterIndex(),
						.mCount = 0,
						.mSrcOffset = cbOffset
					}
				);
			}

			const uint32_t rc = param.GetRegisterCount();
			uploadSpans.back().mCount += rc;

			if (const void *src = param.GetDefaultValue())
				memcpy((char *)shaderInfo.mConstantBuffer.data() + cbOffset, src, rc * registerSize);
			
			// Scan parameters for variables we can bind to. Note that we explicitly ignore the
			// register set type as it's valid to bind to any type or even double-bind a variable,
			// i.e. IN.frame_count -> bool and float. One way this happens is due to a mixed type
			// struct.
			VDStringSpanA name(param.GetName());
			const auto paramClass = param.GetParamClass();
			const uint32 paramRegisterIndex = param.GetRegisterIndex();
			const uint32 paramRegisterCount = param.GetRegisterCount();

			if (paramClass == kVDD3DXParameterClass_Matrix_ColumnMajor || paramClass == kVDD3DXParameterClass_Matrix_RowMajor) {
				if (name == "$modelViewProj") {
					VDDCustomEffectVec4x4Gather gather;

					if (paramClass == kVDD3DXParameterClass_Matrix_ColumnMajor)
						gather = varStorage.RequestColumnMajorMatrix(cbOffset, VDDCustomEffectVariable::ModelViewProj, 0, 0);
					else
						gather = varStorage.RequestRowMajorMatrix(cbOffset, VDDCustomEffectVariable::ModelViewProj, 0, 0);

					const uint32 copyRows = std::min<uint32>(rc, 4);

					if (registerSet == kVDD3DXRegisterSet_Bool) {
						for(uint32 i = 0; i < copyRows; ++i) {
							shaderInfo.mConstantBufferBoolGathers.push_back(
								{
									(uint32)(cbOffset + 4*i),
									gather.mVec[i].mOffset[0]
								}
							);
						}
					} else {
						for(uint32 i = 0; i < copyRows; ++i)
							shaderInfo.mConstantBufferVecGathers.push_back(gather.mVec[i]);
					}
				}
			} else if (paramClass == kVDD3DXParameterClass_Struct) {
				// iterate over sub-struct fields
				uint32 varPassIndex = mPassIndex;
				uint32 varElementIndex = 0;
			
				if (name.empty() || name[0] != '$')
					continue;

				auto parsedFrameRef = VDDCustomEffectFrameRef::Parse(name.subspan(1), mPassIndex);

				if (!parsedFrameRef.mbValid)
					continue;

				varPassIndex = parsedFrameRef.mPassIndex;
				varElementIndex = parsedFrameRef.mElementIndex;

				VDD3DXCTParameter member;
				auto members = param.GetMembers();
				while(members.Next(member)) {
					const auto memberClass = member.GetParamClass();
					if (memberClass != kVDD3DXParameterClass_Scalar
						&& memberClass != kVDD3DXParameterClass_Vector
						&& memberClass != kVDD3DXParameterClass_Matrix_ColumnMajor
						&& memberClass != kVDD3DXParameterClass_Matrix_RowMajor)
						continue;

					const auto memberType = member.GetParamType();
					switch(memberType) {
						case kVDD3DXParameterType_Bool:
							if (registerSet != kVDD3DXRegisterSet_Bool)
								continue;
							break;

						case kVDD3DXParameterType_Float:
							if (registerSet != kVDD3DXRegisterSet_Float4)
								continue;
							break;

						case kVDD3DXParameterType_Int:
							if (registerSet != kVDD3DXRegisterSet_Int4)
								continue;
							break;

						default:
							continue;
					}

					const uint32 memberRegisterIndex = member.GetRegisterIndex();
					const uint32 memberRegisterCount = member.GetRegisterCount();

					// unused parameters can be present with register count 0; they should be
					// skipped before register range checking
					if (!memberRegisterCount)
						continue;

					if (memberRegisterIndex < paramRegisterIndex || memberRegisterIndex >= paramRegisterIndex + paramRegisterCount) {
						VDFAIL("Member outside of containing parameter");
						continue;
					}

					if (paramRegisterIndex + paramRegisterCount - memberRegisterIndex < memberRegisterCount) {
						VDFAIL("Member outside of containing parameter");
						continue;
					}

					ptrdiff_t memberDstOffset = cbOffset + (member.GetRegisterIndex() - param.GetRegisterIndex()) * 16;

					VDStringSpanA memberName(member.GetName());
					VDDCustomEffectVariable var {};

					if (memberName == "video_size")
						var = VDDCustomEffectVariable::VideoSize;
					else if (memberName == "texture_size")
						var = VDDCustomEffectVariable::TextureSize;
					else if (memberName == "output_size")
						var = VDDCustomEffectVariable::OutputSize;
					else if (memberName == "frame_count") {
						var = VDDCustomEffectVariable::FrameCount;

						mbPassCanBeCached = false;
					} else if (memberName == "frame_direction")
						var = VDDCustomEffectVariable::FrameDirection;
					else
						continue;

					if (var != VDDCustomEffectVariable::FrameCount)
						varElementIndex = 0;

					VDDCustomEffectVec4x4Gather gather;
					uint32 availVecs = 4;

					switch(memberClass) {
						case kVDD3DXParameterClass_Scalar:
						case kVDD3DXParameterClass_Vector:
							gather.mVec[0] = varStorage.RequestVector(memberDstOffset, var, varPassIndex, varElementIndex);
							availVecs = 1;
							break;

						case kVDD3DXParameterClass_Matrix_ColumnMajor:
							gather = varStorage.RequestColumnMajorMatrix(memberDstOffset, var, varPassIndex, varElementIndex);
							break;

						case kVDD3DXParameterClass_Matrix_RowMajor:
							gather = varStorage.RequestRowMajorMatrix(memberDstOffset, var, varPassIndex, varElementIndex);
							break;

						default:
							continue;
					}

					uint32 vecsToCopy = std::min<uint32>(availVecs, memberRegisterCount);

					if (registerSet == kVDD3DXRegisterSet_Bool) {
						for(const auto& vecGather : vdspan(gather.mVec, vecsToCopy)) {
							shaderInfo.mConstantBufferBoolGathers.push_back(
								VDDCustomEffectScalarGather {
									(uint32)memberDstOffset,
									vecGather.mOffset[0]
								}
							);

							memberDstOffset += 4;
						}
					} else {
						shaderInfo.mConstantBufferVecGathers.append_range(vdspan(gather.mVec, vecsToCopy));
					}
				}
			}

			cbOffset += registerSize * rc;
		}
	}

	// validate offsets
	const uint32 cbSize = 4 * totalElements;
	const uint32 cbBoolLimit = std::max<uint32>(cbSize, 3) - 3;
	const uint32 cbVecLimit = std::max<uint32>(cbSize, 15) - 15;

	for(const VDDCustomEffectVec4Gather& vecGather : shaderInfo.mConstantBufferVecGathers) {
		if ((vecGather.mDstOffset & 15) || vecGather.mDstOffset >= cbVecLimit)
			throw VDException("Internal error - vector gather out of bounds");
	}

	for(const VDDCustomEffectScalarGather& boolGather : shaderInfo.mConstantBufferBoolGathers) {
		if ((boolGather.mDstOffset & 3) || (boolGather.mSrcOffset & 3) || boolGather.mDstOffset >= cbBoolLimit)
			throw VDException("Internal error - bool gather out of bounds");
	}

	return true;
}

///////////////////////////////////////////////////////////////////////////

VDDisplayCustomShaderPipelineD3D9::VDDisplayCustomShaderPipelineD3D9(VDD3D9Manager *d3d9mgr)
	: mpManager(d3d9mgr)
{
	mpManager->Attach(this);
}

VDDisplayCustomShaderPipelineD3D9::~VDDisplayCustomShaderPipelineD3D9() {
	DestroyQueries();

	for(auto& tex : mCustomTextures) {
		vdsaferelease <<= tex.second.mpTexture;
	}

	mCustomTextures.clear();

	while(!mPasses.empty()) {
		auto *p = mPasses.back();
		mPasses.pop_back();

		delete p;
	}

	mpManager->Detach(this);
}

bool VDDisplayCustomShaderPipelineD3D9::HasTimingInfo() const {
	return !mPassInfos.empty();
}

vdspan<const VDDisplayCustomShaderPassInfo> VDDisplayCustomShaderPipelineD3D9::GetPassTimings() {
	const uint32 n = (uint32)mPassInfos.size();

	if (!n)
		return {};

	if (mbQueryQueries) {
		while(mNextTimingValueQuery < n) {
			IDirect3DQuery9 *q = mpTimingQueries[mNextTimingValueQuery];

			if (!q)
				break;

			UINT64 t = 0;
			HRESULT hr = q->GetData(&t, sizeof t, 0);
			if (hr != S_OK)
				break;

			mTimingValues[mNextTimingValueQuery++] = t;
		}

		if (mNextTimingValueQuery >= n) {
			for(uint32 i=1; i<n; ++i)
				mPassInfos[i - 1].mTiming = (float)(mTimingValues[i] - mTimingValues[i - 1]) * mTicksToSeconds;

			mPassInfos[n - 1].mTiming = (float)(mTimingValues[n - 1] - mTimingValues[0]) * mTicksToSeconds;

			mNextTimingValueIssue = 0;
			mNextTimingValueQuery = 0;
			mbQueryQueries = false;
			mbIssueQueries = true;
		}
	}

	for(uint32 i=0; i<n - 1; ++i) {
		auto& info = mPassInfos[i];
		const auto& texInfoArray = mTexSpecGrid[i + 1];

		if (texInfoArray.empty()) {
			info = {};
		} else {
			info.mOutputWidth = texInfoArray[0].mImageWidth;
			info.mOutputHeight = texInfoArray[0].mImageHeight;
			info.mbOutputLinear = texInfoArray[0].mbLinear;
			info.mbOutputFloat = mPasses[i]->IsOutputFloat();
			info.mbOutputHalfFloat = mPasses[i]->IsOutputHalfFloat();
			info.mbOutputSrgb = mPasses[i]->IsOutputSrgb();
			info.mbCached = !mInputInvalidationTable[i + 1];
		}
	}

	return mPassInfos;
}

bool VDDisplayCustomShaderPipelineD3D9::ContainsFinalBlit() const {
	return !mPasses.empty() && mPasses.back()->HasScalingFactor();
}

void VDDisplayCustomShaderPipelineD3D9::Run(IDirect3DTexture9 *const *srcTextures, const vdint2& texSize, const vdint2& imageSize, const vdint2& viewportSize) {
	if (mPasses.empty())
		return;

	for(auto& origTexSpec : mOrigTexSpecs) {
		origTexSpec.mImageWidth = imageSize.x;
		origTexSpec.mImageHeight = imageSize.y;
		origTexSpec.mTexWidth = texSize.x;
		origTexSpec.mTexHeight = texSize.y;
		origTexSpec.mpTexture = *srcTextures++;
		origTexSpec.mbLinear = false;
		origTexSpec.mbSrgb = false;
	}

	std::ranges::fill(mInputInvalidationTable, false);
	mInputInvalidationTable[0] = true;

	mTexSpecGrid[0] = VDDTexSpecView<const VDDisplayCustomShaderD3D9::TextureSpec>(std::from_range, mOrigTexSpecs);

	auto it = mPasses.begin();
	auto itEnd = mPasses.end();

	if (mPasses.back()->HasScalingFactor())
		--itEnd;

	uint32 passIndex = 0;
	for(; it != itEnd; ++it, ++passIndex) {
		VDDisplayCustomShaderD3D9 *pass = static_cast<VDDisplayCustomShaderD3D9 *>(*it);

		if (mbIssueQueries && mNextTimingValueIssue == passIndex) {
			HRESULT hr = mpTimingQueries[passIndex]->Issue(D3DISSUE_END);
			if (hr != D3D_OK)
				throw VDD3D9Exception(hr);

			++mNextTimingValueIssue;
		}

		mInputInvalidationTable[passIndex + 1] = pass->Run(nullptr, mTexSpecGrid, viewportSize, false, mInputInvalidationTable, mVarStorage);

		mTexSpecGrid[passIndex + 1] = pass->GetOutputTexSpecs();
	}

	if (mbIssueQueries && !ContainsFinalBlit() && mNextTimingValueIssue == passIndex) {
		HRESULT hr = mpTimingQueries[passIndex]->Issue(D3DISSUE_END);
		if (hr != D3D_OK)
			throw VDD3D9Exception(hr);

		++mNextTimingValueIssue;
		mbIssueQueries = false;
		mbQueryQueries = true;
	}
}

void VDDisplayCustomShaderPipelineD3D9::RunFinal(const vdrect32f& dstRect, const vdint2& viewportSize) {
	if (!mPasses.empty() && ContainsFinalBlit()) {
		uint32 passIndex = (uint32)mPasses.size() - 1;
		if (mbIssueQueries && mNextTimingValueIssue == passIndex) {
			HRESULT hr = mpTimingQueries[passIndex]->Issue(D3DISSUE_END);
			if (hr != D3D_OK)
				throw VDD3D9Exception(hr);

			++mNextTimingValueIssue;
		}
		
		VDDisplayCustomShaderD3D9 *pass = static_cast<VDDisplayCustomShaderD3D9 *>(mPasses.back());

		mInputInvalidationTable.back() = pass->Run(&dstRect, mTexSpecGrid, viewportSize, true, mInputInvalidationTable, mVarStorage);
		
		mTexSpecGrid.back() = pass->GetOutputTexSpecs();

		++passIndex;
		if (mbIssueQueries && mNextTimingValueIssue == passIndex) {
			HRESULT hr = mpTimingQueries[passIndex]->Issue(D3DISSUE_END);
			if (hr != D3D_OK)
				throw VDD3D9Exception(hr);

			++mNextTimingValueIssue;
			mbIssueQueries = false;
			mbQueryQueries = true;
		}
	}
}

IDirect3DTexture9 *VDDisplayCustomShaderPipelineD3D9::GetFinalOutput(uint32& imageWidth, uint32& imageHeight) {
	const auto& result = mTexSpecGrid.back()[0];

	imageWidth = result.mImageWidth;
	imageHeight = result.mImageHeight;

	return result.mpTexture;
}

void VDDisplayCustomShaderPipelineD3D9::OnPreDeviceReset() {
	DestroyQueries();

	for(auto& spec : mOrigTexSpecs)
		spec.mpTexture = nullptr;
}

void VDDisplayCustomShaderPipelineD3D9::OnPostDeviceReset() {
	CreateQueries();
}

void VDDisplayCustomShaderPipelineD3D9::CreateQueries() {
	IDirect3DDevice9 *dev = mpManager->GetDevice();

	for(IDirect3DQuery9 *& p : mpTimingQueries) {
		if (!p) {
			HRESULT hr = dev->CreateQuery(D3DQUERYTYPE_TIMESTAMP, &p);

			if (hr != D3D_OK)
				throw VDD3D9Exception(hr);
		}
	}
}

void VDDisplayCustomShaderPipelineD3D9::DestroyQueries() {
	uint32 index = 0;

	for(auto *&ptr : mpTimingQueries) {
		if (ptr && index < mNextTimingValueIssue) {
			for(;;) {
				HRESULT hr = ptr->GetData(nullptr, 0, D3DGETDATA_FLUSH);

				if (hr != S_FALSE)
					break;
			}
		}

		vdsaferelease <<= ptr;

		++index;
	}
}

void VDDisplayCustomShaderPipelineD3D9::LoadTexture(const char *name, const wchar_t *path, bool linear) {
	auto insertResult = mCustomTextures.insert_as(VDStringSpanA(name));
	if (!insertResult.second)
		return;

	VDPixmapBuffer buf;
	VDDLoadCustomShaderTexture(buf, path);

	IDirect3DDevice9 *const dev = mpManager->GetDevice();
	vdrefptr<IDirect3DTexture9> tex;

	HRESULT hr = dev->CreateTexture(buf.w, buf.h, 1, 0, D3DFMT_A8R8G8B8, mpManager->IsD3D9ExEnabled() ? D3DPOOL_SYSTEMMEM : D3DPOOL_MANAGED, ~tex, nullptr);
	if (hr != D3D_OK)
		throw VDD3D9Exception(hr);

	D3DLOCKED_RECT lr;
	hr = tex->LockRect(0, &lr, nullptr, 0);
	if (hr != D3D_OK)
		throw VDD3D9Exception(hr);

	VDPixmap pxdst {};
	pxdst.format = nsVDPixmap::kPixFormat_XRGB8888;
	pxdst.w = buf.w;
	pxdst.h = buf.h;
	pxdst.pitch = lr.Pitch;
	pxdst.data = lr.pBits;
	VDPixmapBlt(pxdst, buf);

	tex->UnlockRect(0);

	if (mpManager->IsD3D9ExEnabled()) {
		vdrefptr<IDirect3DTexture9> tex2;
		hr = dev->CreateTexture(buf.w, buf.h, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, ~tex2, nullptr);
		if (hr != D3D_OK)
			throw VDD3D9Exception(hr);

		hr = dev->UpdateTexture(tex, tex2);
		if (hr != D3D_OK)
			throw VDD3D9Exception(hr);

		tex = std::move(tex2);
	}

	auto& newTex = insertResult.first->second;
	newTex.mpTexture = tex.release();
	newTex.mImageWidth = buf.w;
	newTex.mImageHeight = buf.h;
	newTex.mTexWidth = buf.w;
	newTex.mTexHeight = buf.h;
	newTex.mbLinear = linear;
}

void VDDisplayCustomShaderPipelineD3D9::BeginPasses(uint32 numPasses) {
	mTexSpecGrid.resize(numPasses + 1);
}

void VDDisplayCustomShaderPipelineD3D9::AddPass(uint32 passIndex, const VDDisplayCustomShaderProps& props, const char *shaderPath, const wchar_t *basePath) {
	mPasses.push_back(nullptr);
	auto *pass = new VDDisplayCustomShaderD3D9(passIndex, mpManager);
	mPasses.back() = pass;
	
	pass->Init(shaderPath, props, mCustomTextures, mPassInputsFiltered, mPassInputsSrgb, basePath, mMaxPrevFramesPerPass, mVarStorage);
}

void VDDisplayCustomShaderPipelineD3D9::EndPasses() {
	mOrigTexSpecs.resize(mMaxPrevFramesPerPass[0] + 1);

	uint32 frameIndex = 0;
	for(auto& otex : mOrigTexSpecs)
		mVarStorage.ResolveFrameParamOffsets(otex.mFrameParams, 0, frameIndex++);
}

void VDDisplayCustomShaderPipelineD3D9::InitProfiling() {
	IDirect3DDevice9 *dev = mpManager->GetDevice();

	// check if timestamp queries supported
	HRESULT hr = dev->CreateQuery(D3DQUERYTYPE_TIMESTAMP, NULL);
	if (hr != D3D_OK)
		return;

	// query timestamp frequency
	vdrefptr<IDirect3DQuery9> q;
	hr = dev->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ, ~q);
	if (hr != D3D_OK)
		return;

	hr = q->Issue(D3DISSUE_END);
	if (hr != D3D_OK)
		return;

	UINT64 freq = 0;

	for(;;) {
		hr = q->GetData(&freq, sizeof freq, D3DGETDATA_FLUSH);
		if (hr != S_FALSE)
			break;

		Sleep(1);
	}

	// set up events for each pass
	if (hr == S_OK && freq > 0) {
		const uint32 passCount = (uint32)mPasses.size();

		mPassInfos.resize(passCount + 1, {});
		mpTimingQueries.resize(passCount + 1, nullptr);
		mTimingValues.resize(passCount + 1, 0);

		mTicksToSeconds = 1.0f / (float)freq;

		CreateQueries();

		mbIssueQueries = true;
	}
}

vdrefptr<IVDDisplayCustomEffectD3D9> VDDisplayParseCustomShaderPipeline(VDD3D9Manager *d3d9mgr, const wchar_t *path) {
	auto p = vdmakerefptr(new VDDisplayCustomShaderPipelineD3D9(d3d9mgr));

	p->Parse(path);

	return p;
}
