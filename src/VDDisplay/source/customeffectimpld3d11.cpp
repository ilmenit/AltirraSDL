//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2025 Avery Lee
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
#include <d3dcompiler.h>
#include <vd2/system/bitmath.h>
#include <vd2/system/error.h>
#include <vd2/system/filesys.h>
#include <vd2/system/vdalloc.h>
#include <vd2/Kasumi/pixmaputils.h>
#include <vd2/VDDisplay/internal/customeffectimpld3d11.h>
#include <vd2/VDDisplay/internal/customeffectutils.h>
#include <vd2/VDDisplay/internal/customeffectutilsd3d.h>

////////////////////////////////////////////////////////////////////////////////

VDDCustomEffectPassD3D11::VDDCustomEffectPassD3D11(VDDCustomEffectD3D11& parent, IVDTContext& context, VDDisplayNodeContext3D& dctx, VDDisplayCommandList3D& cmdList, uint32 passIndex)
	: VDDCustomEffectPassBase(passIndex)
	, mParent(parent)
	, mContext(context)
	, mDisplayContext(dctx)
	, mCommandList(cmdList)
{
}

void VDDCustomEffectPassD3D11::Init(
	const char *shaderPath8,
	const VDDisplayCustomShaderProps& propLookup,
	const vdhashmap<VDStringA, uint32>& customTextureLookup,
	vdspan<const TextureSpec> customTextures,
	vdspan<const bool> passInputsFiltered,
	const wchar_t *basePath,
	vdspan<uint32> maxPrevFrames,
	VDDCustomEffectVarStorage& varStorage
) {
	const auto shaderPath = VDTextU8ToW(VDStringSpanA(shaderPath8));
	const auto shaderPrefix = VDFileSplitExtLeftSpan(shaderPath);

	vdfastvector<uint8> vsByteCode;
	vdfastvector<uint8> psByteCode;

	VDDCustomEffectCompilerD3D compiler;

	ParseCommonProps(propLookup);

	const auto& caps = mContext.GetDeviceCaps();

	for(int i=0; i<2; ++i) {
		const char *shaderType = i ? "pixel" : "vertex";
		auto& byteCode = i ? psByteCode : vsByteCode;

		auto fxPath = VDMakePath(VDStringSpanW(basePath), shaderPrefix + L".fx");
		if (!VDDoesPathExist(fxPath.c_str())) {
			// nope... try original
			fxPath = VDMakePath(VDStringSpanW(basePath), shaderPath);
			if (!VDDoesPathExist(fxPath.c_str()))
				throw VDException(L"Pass %u %hs shader: cannot find '%ls' or a precompiled/HLSL specific version", mPassIndex, shaderType, shaderPath.c_str());
		}

		const char *profile = propLookup.GetString(VDDCsPropKeyView("shader_profile_d3d11_", mPassIndex));

		if (!profile) {
			profile = propLookup.GetString(VDDCsPropKeyView("shader_profile_d3d", 11));

			if (!profile) {
				if (caps.mbGraphicsSM5)
					profile = "5_0";
				else if (caps.mbGraphicsSM4)
					profile = "4_0";
				else if (caps.mbGraphicsSM3)
					profile = "4_0_level_9_3";
				else
					profile = "4_0_level_9_1";
			}
		}

		const bool minPrec = i ? caps.mbMinPrecisionPS : caps.mbMinPrecisionNonPS;

		try {
			byteCode = compiler.Compile(fxPath.c_str(), i ? "main_fragment" : "main_vertex", (VDStringA(i ? "ps_" : "vs_") + profile).c_str(), true, minPrec);
		} catch(const VDDCustomShaderCompileException& ex) {
			throw VDException(L"Pass %u %hs shader: %ls", mPassIndex, shaderType, ex.wc_str());
		}
	}

	mbPassCanCache = true;

	ProcessShader(compiler, false, vsByteCode.data(), vsByteCode.size(), mVertexShaderInfo, customTextureLookup, customTextures, maxPrevFrames, varStorage);
	ProcessShader(compiler, true, psByteCode.data(), psByteCode.size(), mPixelShaderInfo, customTextureLookup, customTextures, maxPrevFrames, varStorage);

	// compute pass reference list
	std::ranges::sort(mPassInputReferenceList);
	mPassInputReferenceList.erase(std::unique(mPassInputReferenceList.begin(), mPassInputReferenceList.end()), mPassInputReferenceList.end());

	// check input/output compatibility
	for(size_t i = 0, n = mPixelShaderInfo.mInputBindings.size(); i < n; ++i) {
		const auto& inputBinding = mPixelShaderInfo.mInputBindings[i];

		if (i >= mVertexShaderInfo.mOutputBindings.size())
			throw VDException(L"Pass %u: pixel shader output #%u (%hs[%u]) has no matching vertex shader input (%ls)", mPassIndex, i+1, inputBinding.mSemantic.c_str(), inputBinding.mSemanticIndex, shaderPath.c_str());

		const auto& outputBinding = mVertexShaderInfo.mOutputBindings[i];

		// D3D11 semantic matching rules:
		// - Semantic name matching is case insensitive
		// - Semantic index must match
		// - Semantic index number is stripped off from name and no number is implicitly 0
		// - VS-PS bindings can use arbitrary semantics, unlike PS outputs.
		//
		// Thus, TEXCOORD matches TeXcOOrD0.

		if (inputBinding.mSemantic.comparei(outputBinding.mSemantic) != 0
			|| inputBinding.mSemanticIndex != outputBinding.mSemanticIndex)
		{
			throw VDException(L"Pass %u: pixel shader input #%u (%hs[%u]) does not match vertex shader output #%u (%hs[%u]) (%ls)"
				, mPassIndex
				, i+1
				, inputBinding.mSemantic.c_str()
				, inputBinding.mSemanticIndex
				, i+1
				, outputBinding.mSemantic.c_str()
				, outputBinding.mSemanticIndex
				, shaderPath.c_str()
			);
		}

		if (inputBinding.mRegisterIndex != outputBinding.mRegisterIndex)
			throw VDException(L"Pass %u: VS output / PS input #%u (%hs[%u]) have mismatching register numbers (%ls)"
				, mPassIndex
				, i+1
				, inputBinding.mSemantic.c_str()
				, inputBinding.mSemanticIndex
				, shaderPath.c_str()
			);

		if (inputBinding.mComponentMask & ~outputBinding.mComponentMask)
			throw VDException(L"Pass %u: PS input #%u (%hs[%u]) reads components .%hs%hs%hs%hs, but VS only outputs .%hs%hs%hs%hs (%ls)"
				, mPassIndex
				, i+1
				, inputBinding.mSemantic.c_str()
				, inputBinding.mSemanticIndex
				, inputBinding.mComponentMask & 1 ? "x" : ""
				, inputBinding.mComponentMask & 2 ? "y" : ""
				, inputBinding.mComponentMask & 4 ? "z" : ""
				, inputBinding.mComponentMask & 8 ? "w" : ""
				, outputBinding.mComponentMask & 1 ? "x" : ""
				, outputBinding.mComponentMask & 2 ? "y" : ""
				, outputBinding.mComponentMask & 4 ? "z" : ""
				, outputBinding.mComponentMask & 8 ? "w" : ""
				, shaderPath.c_str()
			);
	}

	if (!mContext.CreateVertexProgram(VDTProgramFormat::kVDTPF_D3D11ByteCode, VDTData { vsByteCode.data(), (uint32)vsByteCode.size() }, ~mpVertexShader))
		throw VDException("Pass %u: unable to create vertex shader", mPassIndex);

	if (mVertexShaderInfo.mSamplerSlotCount || mVertexShaderInfo.mTextureSlotCount)
		throw VDException("Pass %u: Vertex texturing not yet supported", mPassIndex);

	if (!mContext.CreateFragmentProgram(VDTProgramFormat::kVDTPF_D3D11ByteCode, VDTData { psByteCode.data(), (uint32)psByteCode.size() }, ~mpPixelShader))
		throw VDException("Pass %u: unable to create pixel shader", mPassIndex);

	static constexpr VDTVertexElement kVxElements[] {
		{ offsetof(Vertex, x), kVDTET_Float4, kVDTEU_Position, 0 },
		{ offsetof(Vertex, x), kVDTET_Float4, kVDTEU_SV_Position, 0 },
		{ offsetof(Vertex, mColor), kVDTET_UByte4N, kVDTEU_Color, 0 },
		{ offsetof(Vertex, mUV0), kVDTET_Float2, kVDTEU_TexCoord, 0 },
		{ offsetof(Vertex, mUV1), kVDTET_Float2, kVDTEU_TexCoord, 1 },
	};

	if (!mContext.CreateVertexFormat(kVxElements, vdcountof(kVxElements), mpVertexShader, ~mpVertexFormat))
		throw VDException("Pass %u: unable to create input layout", mPassIndex);

	auto mb = mCommandList.AddMesh(mDisplayContext);

	mb.SetVertexFormat(mpVertexFormat);
	mb.SetVertexProgram(mpVertexShader);
	mb.SetFragmentProgram(mpPixelShader);
	mb.SetTopologyQuad();

	if (!mVertexShaderInfo.mConstantBuffers.empty()) {
		uint32 cbMax = 0;

		for(const ConstantBufferInfo& cbInfo : mVertexShaderInfo.mConstantBuffers) {
			cbMax = std::max<uint32>(cbMax, cbInfo.mCbIndex);
		}

		mb.SetVsConstBufferCount(cbMax + 1);

		for(const ConstantBufferInfo& cbInfo : mVertexShaderInfo.mConstantBuffers) {
			mb.SetVPConstData(cbInfo.mConstantBuffer.data(), cbInfo.mConstantBuffer.size() * 4, true, cbInfo.mCbIndex);
		}
	}

	if (!mPixelShaderInfo.mConstantBuffers.empty()) {
		uint32 cbMax = 0;

		for(const ConstantBufferInfo& cbInfo : mPixelShaderInfo.mConstantBuffers) {
			cbMax = std::max<uint32>(cbMax, cbInfo.mCbIndex);
		}

		mb.SetPsConstBufferCount(cbMax + 1);

		for(const ConstantBufferInfo& cbInfo : mPixelShaderInfo.mConstantBuffers) {
			mb.SetFPConstData(cbInfo.mConstantBuffer.data(), cbInfo.mConstantBuffer.size() * 4, true, cbInfo.mCbIndex);
		}
	}

	mb.InitSamplers(mPixelShaderInfo.mSamplerSlotCount);

	for(const SamplerBinding& samplerBinding : mPixelShaderInfo.mSamplers) {
		bool linear = false;

		if (samplerBinding.mTextureKey.mType == VDDCETextureType::PassInput) {
			const uint32 refPassIndex = samplerBinding.mTextureKey.mPassIndex;

			linear = refPassIndex >= mPassIndex ? mbFilterInput : passInputsFiltered[refPassIndex];
		} else if (samplerBinding.mTextureKey.mType == VDDCETextureType::Custom) {
			const TextureSpec& texSpec = customTextures[samplerBinding.mTextureKey.mTextureIndex];

			linear = texSpec.mbLinear;
		}

		mb.SetSampler(samplerBinding.mSamplerIndex,
				linear ? mParent.mpSSBorderLinear : mParent.mpSSBorderPoint
		);
	}
	
	mb.InitTextures(mPixelShaderInfo.mTextureSlotCount);

	for(TextureBinding& texBinding : mPixelShaderInfo.mTextures) {
		if (texBinding.mTextureKey.mType == VDDCETextureType::PassInput) {
			texBinding.mPoolTextureIndex = mCommandList.RegisterTexture(nullptr);

			mb.SetTexture(texBinding.mTextureIndex, texBinding.mPoolTextureIndex);
		} else if (texBinding.mTextureKey.mType == VDDCETextureType::Custom) {
			mb.SetTexture(texBinding.mTextureIndex, texBinding.mPoolTextureIndex);
		}
	}

	mRenderViewId = mCommandList.RegisterRenderView({});
	mb.SetRenderView(mRenderViewId);

	mCommandIndex = mb.GetCommandIndex();
}

void VDDCustomEffectPassD3D11::ResetVariables(VDDCustomEffectVarStorage& varStorage, uint32 prevOutputFramesNeeded, bool outputFiltered) {
	mOutputTextures.resize(prevOutputFramesNeeded + 1);

	uint32 elementIndex = 0;
	for(OutputTexture& otex : mOutputTextures) {
		otex.mTexturePoolIndex = mCommandList.RegisterTexture(nullptr);
		otex.mbLinear = outputFiltered;
		otex.mbSrgb = mbSrgbFramebuffer;

		varStorage.ResolveFrameParamOffsets(otex.mFrameParams, mPassIndex + 1, elementIndex);
		++elementIndex;
	}

	varStorage.ResolvePassParamOffsets(mPassParamOffsets, mPassIndex);
}

VDDisplaySourceTexMapping VDDCustomEffectPassD3D11::ComputeOutputMapping(const vdint2& srcSize, const vdint2& viewportSize) const {
	const vdint2& renderSize = ComputeRenderSize(srcSize, viewportSize);

	VDDisplaySourceTexMapping mapping {};
	mapping.mTexelOffset = vdfloat2 { 0.0f, 0.0f };
	mapping.mTexelSize = vdfloat2 { (float)renderSize.x, (float)renderSize.y };
	if (mContext.GetDeviceCaps().mbNonPow2) {
		mapping.mTexWidth = renderSize.x;
		mapping.mTexHeight = renderSize.y;
	} else {
		mapping.mTexWidth = VDCeilToPow2(renderSize.x);
		mapping.mTexHeight = VDCeilToPow2(renderSize.y);
	}
	mapping.mUVOffset = vdfloat2 { 0.0f, 0.0f };
	mapping.mUVSize = mapping.mTexelSize / vdfloat2 { (float)mapping.mTexWidth, (float)mapping.mTexHeight };

	return mapping;
}

void VDDCustomEffectPassD3D11::IncrementFrame() {
	// rotate output textures
	vdrefptr<IVDTTexture2D> lastTex(std::move(mOutputTextures.back().mpTextureRef));

	for(auto& otex : mOutputTextures) {
		vdrefptr<IVDTTexture2D> nextTex(std::move(otex.mpTextureRef));

		otex.mpTextureRef = std::move(lastTex);
		otex.mpTexture = otex.mpTextureRef;

		lastTex = std::move(nextTex);
		otex.mpTextureRef.clear();
		otex.mpTexture = nullptr;
	}

	if (mFrame >= mFrameCountLimit)
		mFrame = 0;
	else
		++mFrame;
}

void VDDCustomEffectPassD3D11::SetPassUncacheable() {
	mbPassCanCache = false;
}

bool VDDCustomEffectPassD3D11::Run(
	const vdrect32f *dstRect,
	vdspan<const VDDTexSpecView<const TextureSpec>> texSpecGrid,
	const vdint2& viewportSize,
	bool lastStage,
	vdspan<bool> inputInvalidationTable,
	VDDCustomEffectVarStorage& varStorage)
{
	// Check if this pass needs to run:
	//
	//	- Pass is cacheable (doesn't reference frame counter)
	//	- Pass has all output textures initialized
	//	- Pass does not have any input references invalidated
	//
	if (mbPassCanCache && mOutputTextures.back().mpTextureRef) {
		bool inputInvalidated = false;

		for(const auto& passRef : mPassInputReferenceList) {
			if (inputInvalidationTable[passRef]) {
				inputInvalidated = true;
				break;
			}
		}

		if (!inputInvalidated)
			return false;
	}

	IncrementFrame();

	// update the mesh draw command
	{
		auto mb = mCommandList.UpdateMesh(mDisplayContext, mCommandIndex);

		static constexpr Vertex vx[4] {
			{ -1.0f, +1.0f, 0.0f, 1.0f, 0xFFFFFFFF, { 0.0f, 0.0f }, { 0.0f, 0.0f } },
			{ -1.0f, -1.0f, 0.0f, 1.0f, 0xFFFFFFFF, { 0.0f, 1.0f }, { 0.0f, 1.0f } },
			{ +1.0f, +1.0f, 0.0f, 1.0f, 0xFFFFFFFF, { 1.0f, 0.0f }, { 1.0f, 0.0f } },
			{ +1.0f, -1.0f, 0.0f, 1.0f, 0xFFFFFFFF, { 1.0f, 1.0f }, { 1.0f, 1.0f } },
		};

		mb.SetVertices(vx);

		for(const TextureBinding& texBinding : mPixelShaderInfo.mTextures) {
			if (texBinding.mTextureKey.mType == VDDCETextureType::PassInput) {
				mCommandList.SetTexture(texBinding.mPoolTextureIndex,
					texSpecGrid[texBinding.mTextureKey.mPassIndex][texBinding.mTextureKey.mTextureIndex].mpTexture);
			}
		}

		for(ConstantBufferInfo& cbInfo : mVertexShaderInfo.mConstantBuffers) {
			varStorage.GatherFloats(cbInfo.mConstantBuffer.data(), cbInfo.mFloatGathers);
			mb.UpdateVPConstData(cbInfo.mConstantBuffer.data(), cbInfo.mCbIndex);
		}

		for(ConstantBufferInfo& cbInfo : mPixelShaderInfo.mConstantBuffers) {
			varStorage.GatherFloats(cbInfo.mConstantBuffer.data(), cbInfo.mFloatGathers);
			mb.UpdateFPConstData(cbInfo.mConstantBuffer.data(), cbInfo.mCbIndex);
		}
	}

	uint32 frame = mFrame;
	for(const auto& inputFrame : texSpecGrid[mPassIndex]) {
		varStorage.SetVector(
			inputFrame.mFrameParams.mFrameCount, vdfloat4 { (float)frame, 0.0f, 0.0f, 0.0f });

		if (frame)
			--frame;
	}

	const auto& srcImage = texSpecGrid[mPassIndex][0];
	const vdint2 srcImageSize(srcImage.mImageWidth, srcImage.mImageHeight);
	const vdint2& renderSize = ComputeRenderSize(srcImageSize, viewportSize);

	varStorage.SetVector(
		mPassParamOffsets.mVideoSize,
		vdfloat4 {
			(float)srcImage.mImageWidth,
			(float)srcImage.mImageHeight,
			0.0f,
			0.0f
		}
	);

	varStorage.SetVector(
		mPassParamOffsets.mTextureSize,
		vdfloat4 {
			(float)srcImage.mTexWidth,
			(float)srcImage.mTexHeight,
			0.0f,
			0.0f
		}
	);

	varStorage.SetVector(
		mPassParamOffsets.mOutputSize,
		vdfloat4 {
			(float)renderSize.x,
			(float)renderSize.y,
			0.0f,
			0.0f
		}
	);

	if (mOutputTextures[0].mImageWidth != renderSize.x || mOutputTextures[0].mImageHeight != renderSize.y) {
		vdint2 newTexSize = renderSize;

		if (!mContext.GetDeviceCaps().mbNonPow2) {
			newTexSize.x = VDCeilToPow2(newTexSize.x);
			newTexSize.y = VDCeilToPow2(newTexSize.y);
		}

		for(auto& otex : mOutputTextures) {
			otex.mpTextureRef.clear();
			otex.mpTexture = nullptr;

			otex.mImageWidth = renderSize.x;
			otex.mImageHeight = renderSize.y;

			otex.mTexWidth = newTexSize.x;
			otex.mTexHeight = newTexSize.y;
		}
	}

	const auto& caps = mContext.GetDeviceCaps();

	for(auto& otex : mOutputTextures) {
		if (!otex.mpTextureRef) {
			VDTFormat format = mDisplayContext.mBGRAFormat;

			if (mbFloatFramebuffer || mbHalfFloatFramebuffer) {
				format = mDisplayContext.mHDRFormat;

				if (!mbHalfFloatFramebuffer && mContext.IsFormatSupportedTexture2D(kVDTF_R32G32B32A32F))
					format = kVDTF_R32G32B32A32F;
			} else if (mbSrgbFramebuffer) {
				format = mDisplayContext.mBGRASRGBFormat;
			}

			if (otex.mTexWidth > caps.mMaxTextureWidth || otex.mTexHeight > caps.mMaxTextureHeight) {
				throw VDException("Pass %u: Unable to allocate %ux%u output texture: exceeds device limit of %ux%u"
					, mPassIndex
					, otex.mTexWidth
					, otex.mTexHeight
					, caps.mMaxTextureWidth
					, caps.mMaxTextureHeight
				);
			}
				
			mContext.CreateTexture2D(otex.mTexWidth, otex.mTexHeight, format, 1, VDTUsage::Render | VDTUsage::Shader, nullptr, ~otex.mpTextureRef);

			if (!otex.mpTextureRef) 
				throw VDException("Pass %u: Unable to allocate %ux%u output texture", mPassIndex, otex.mTexWidth, otex.mTexHeight);

			otex.mpTexture = otex.mpTextureRef;
		}
	}

	auto& dst = mOutputTextures.front();
	IVDTTexture2D *dstTex = dst.mpTexture;

	VDDRenderView renderView {};
	renderView.mpTarget = dstTex ? dstTex->GetLevelSurface(0) : nullptr;
	renderView.mbBypassSrgb = false;
	renderView.mViewport.mWidth = dst.mImageWidth;
	renderView.mViewport.mHeight = dst.mImageHeight;
	renderView.mSoftViewport.mSize.x = dst.mImageWidth;
	renderView.mSoftViewport.mSize.y = dst.mImageHeight;

	mCommandList.SetRenderView(mRenderViewId, renderView);

	return true;
}

bool VDDCustomEffectPassD3D11::ProcessShader(
	VDDCustomEffectCompilerD3D& compiler,
	bool isPixelShader,
	const void *shader, size_t shaderSize, ShaderInfo& shaderInfo,
	const vdhashmap<VDStringA, uint32>& customTextureLookup,
	vdspan<const TextureSpec> customTextures,
	vdspan<uint32> maxPrevFrames, VDDCustomEffectVarStorage& varStorage)
{
	auto reflect = compiler.ReflectD3D11(shader, shaderSize);
	bool cbMask[4] {};

	try {
		D3D11_SHADER_DESC desc {};
		HRESULT hr = reflect->GetDesc(&desc);
		if (FAILED(hr))
			throw hr;

		if (isPixelShader) {
			D3D11_SIGNATURE_PARAMETER_DESC sigDesc {};

			shaderInfo.mInputBindings.resize(desc.InputParameters);
			for(UINT i = 0; i < desc.InputParameters; ++i) {
				hr = reflect->GetInputParameterDesc(i, &sigDesc);
				if (FAILED(hr))
					throw hr;

				auto& inputBinding = shaderInfo.mInputBindings[i];

				inputBinding.mSemantic = sigDesc.SemanticName;
				inputBinding.mSemanticIndex = sigDesc.SemanticIndex;
				inputBinding.mComponentMask = sigDesc.ReadWriteMask;
				inputBinding.mRegisterIndex = sigDesc.Register;
			}
		} else {
			D3D11_SIGNATURE_PARAMETER_DESC sigDesc {};

			shaderInfo.mOutputBindings.resize(desc.OutputParameters);
			for(UINT i = 0; i < desc.OutputParameters; ++i) {
				hr = reflect->GetOutputParameterDesc(i, &sigDesc);
				if (FAILED(hr))
					throw hr;

				auto& outputBinding = shaderInfo.mOutputBindings[i];

				outputBinding.mSemantic = sigDesc.SemanticName;
				outputBinding.mSemanticIndex = sigDesc.SemanticIndex;

				// For an output,, this indicates what components the VS _doesn't_
				// write, which is weird.
				outputBinding.mComponentMask = sigDesc.ReadWriteMask ^ 15;

				outputBinding.mRegisterIndex = sigDesc.Register;
			}
		}

		for(UINT resIndex = 0; resIndex < desc.BoundResources; ++resIndex) {
			D3D11_SHADER_INPUT_BIND_DESC bindDesc {};
			hr = reflect->GetResourceBindingDesc(resIndex, &bindDesc);
			if (FAILED(hr))
				throw hr;

			VDStringSpanA resName(bindDesc.Name);

			if (bindDesc.Type == D3D_SIT_CBUFFER) {
				if (bindDesc.BindPoint >= 4)
					throw VDException(L"Shader uses constant buffer 'cb%u' beyond supported range", (unsigned)bindDesc.BindPoint);

				if (cbMask[bindDesc.BindPoint])
					throw VDException(L"Constant buffer 'cb%u' bound more than once (second: '%hs')", (unsigned)bindDesc.BindPoint, bindDesc.Name);

				cbMask[bindDesc.BindPoint] = true;

				ID3D11ShaderReflectionConstantBuffer *cb = reflect->GetConstantBufferByName(bindDesc.Name);
				if (!cb)
					throw VDException(L"Shader binds undefined constant buffer '%hs'", bindDesc.Name);

				auto& cbInfo = shaderInfo.mConstantBuffers.emplace_back();
				cbInfo.mCbIndex = bindDesc.BindPoint;

				ProcessConstantBuffer(*cb, cbInfo, varStorage);
			} else if (bindDesc.Type == D3D_SIT_SAMPLER) {
				UINT sreg = bindDesc.BindPoint;

				// When compiling in compatibility mode, tex2D(samp, uv) will result
				// in 'samp' being defined both as a texture and a sampler. Therefore,
				// to support tex2D(IN_texture), we need to accept IN_texture as a
				// sampler reference.
				if (resName.ends_with("_sampler") || resName.ends_with("_texture")) {
					VDStringSpanA resBaseName = resName;
					resBaseName.remove_suffix(8);

					auto frameRef = VDDCustomEffectFrameRef::Parse(resBaseName, mPassIndex);
					if (frameRef.mbValid) {
						shaderInfo.mSamplers.emplace_back(
							sreg,
							VDDCETextureKey {
								.mType = VDDCETextureType::PassInput,
								.mPassIndex = (uint8)frameRef.mPassIndex,
								.mTextureIndex = (uint16)frameRef.mElementIndex
							}
						);

						shaderInfo.mSamplerSlotCount = std::max<uint32>(shaderInfo.mSamplerSlotCount, sreg + 1);
						continue;
					}
				} else {
					auto customTexRef = customTextureLookup.find(resName);

					if (customTexRef != customTextureLookup.end()) {
						shaderInfo.mSamplers.emplace_back(
							sreg,
							VDDCETextureKey {
								.mType = VDDCETextureType::Custom,
								.mTextureIndex = (uint16)customTexRef->second
							}
						);

						shaderInfo.mSamplerSlotCount = std::max<uint32>(shaderInfo.mSamplerSlotCount, sreg + 1);
						continue;
					}
				}

				// We don't recognize this sampler parameter. If it is bound to slot 0,
				// use the input.
				if (sreg == 0) {
					shaderInfo.mSamplers.emplace_back(
						sreg,
						VDDCETextureKey {
							.mType = VDDCETextureType::PassInput,
							.mPassIndex = (uint8)mPassIndex,
							.mTextureIndex = (uint16)0
						}
					);

					shaderInfo.mSamplerSlotCount = std::max<uint32>(shaderInfo.mSamplerSlotCount, sreg + 1);
				}
			} else if (bindDesc.Type == D3D_SIT_TEXTURE) {
				UINT treg = bindDesc.BindPoint;

				if (resName.ends_with("_texture")) {
					VDStringSpanA resBaseName = resName;
					resBaseName.remove_suffix(8);

					auto frameRef = VDDCustomEffectFrameRef::Parse(resBaseName, mPassIndex);
					if (frameRef.mbValid) {
						shaderInfo.mTextures.emplace_back(
							treg,
							VDDPoolTextureIndex{},
							VDDCETextureKey {
								.mType = VDDCETextureType::PassInput,
								.mPassIndex = (uint8)frameRef.mPassIndex,
								.mTextureIndex = (uint16)frameRef.mElementIndex
							}
						);

						shaderInfo.mTextureSlotCount = std::max<uint32>(shaderInfo.mTextureSlotCount, treg + 1);
						continue;
					}
				} else {
					auto customTexRef = customTextureLookup.find(resName);

					if (customTexRef != customTextureLookup.end()) {
						shaderInfo.mTextures.emplace_back(
							treg,
							customTextures[customTexRef->second].mTexturePoolIndex,
							VDDCETextureKey {
								.mType = VDDCETextureType::Custom,
								.mTextureIndex = (uint16)customTexRef->second
							}
						);

						shaderInfo.mTextureSlotCount = std::max<uint32>(shaderInfo.mTextureSlotCount, treg + 1);
						continue;
					}
				}

				// We don't recognize this texture parameter. If it is bound to slot 0,
				// use the input.
				if (treg == 0) {
					shaderInfo.mTextures.emplace_back(
						treg,
						VDDPoolTextureIndex{},
						VDDCETextureKey {
							.mType = VDDCETextureType::PassInput,
							.mPassIndex = (uint8)mPassIndex,
							.mTextureIndex = (uint16)0
						}
					);

					shaderInfo.mTextureSlotCount = std::max<uint32>(shaderInfo.mTextureSlotCount, treg + 1);
				}
			}
		}
	} catch(HRESULT hr) {
		throw VDException(L"Pass %u: error reflecting shader (error code %08X)", (unsigned)mPassIndex, hr);
	} catch(const VDException& ex) {
		throw VDException(L"Pass %u: %ls", (unsigned)mPassIndex, ex.wc_str());
	}

	for(const auto& samplerBinding : shaderInfo.mSamplers) {
		if (samplerBinding.mTextureKey.mType == VDDCETextureType::PassInput
			&& samplerBinding.mTextureKey.mTextureIndex > 0)
		{
			auto& prev = maxPrevFrames[samplerBinding.mTextureKey.mPassIndex];

			prev = std::max<uint32>(prev, samplerBinding.mTextureKey.mTextureIndex);
		}
	}

	for(const auto& textureBinding : shaderInfo.mTextures) {
		if (textureBinding.mTextureKey.mType == VDDCETextureType::PassInput) {
			mPassInputReferenceList.push_back(textureBinding.mTextureKey.mPassIndex);

			if (textureBinding.mTextureKey.mTextureIndex > 0) {
				auto& prev = maxPrevFrames[textureBinding.mTextureKey.mPassIndex];

				prev = std::max<uint32>(prev, textureBinding.mTextureKey.mTextureIndex);
			}
		}
	}

	return true;
}

void VDDCustomEffectPassD3D11::ProcessConstantBuffer(ID3D11ShaderReflectionConstantBuffer& cb, ConstantBufferInfo& cbInfo, VDDCustomEffectVarStorage& varStorage) {
	D3D11_SHADER_BUFFER_DESC cbDesc {};
	HRESULT hr = cb.GetDesc(&cbDesc);
	if (FAILED(hr))
		throw hr;

	cbInfo.mConstantBuffer.resize((cbDesc.Size + 3) >> 2, 0);

	// check for specific named cbs, for HLSL_4 compatibility
	VDStringSpanA cbName(cbDesc.Name);
	bool isCbKnown = false;
	bool isCbOrig = false;
	uint32 cbPassIndex = 0;
	uint32 cbTextureIndex = 0;

	if (cbName == "orig") {
		isCbKnown = true;
		cbPassIndex = 0;
		cbTextureIndex = 0;
	} else if (cbName == "input") {
		isCbKnown = true;
		cbPassIndex = mPassIndex;
		cbTextureIndex = 0;
	} else if (cbName == "prev") {
		// map 'prev' constant buffer to pass prev and not input prev
		isCbKnown = true;
		cbPassIndex = mPassIndex;
		cbTextureIndex = 1;
	}

	for(UINT varIndex = 0; varIndex < cbDesc.Variables; ++varIndex) {
		ID3D11ShaderReflectionVariable *var = cb.GetVariableByIndex(varIndex);
		if (!var)
			continue;

		D3D11_SHADER_VARIABLE_DESC varDesc {};
		hr = var->GetDesc(&varDesc);
		if (FAILED(hr))
			throw hr;

		if (varDesc.Size <= 0)
			continue;

		if (varDesc.StartOffset > cbDesc.Size || cbDesc.Size - varDesc.StartOffset < varDesc.Size)
			throw VDException("Constant buffer member '%s' is %u bytes at offset %u, extending outside constant buffer of %u bytes"
				, varDesc.Name
				, varDesc.Size
				, varDesc.StartOffset
				, cbDesc.Size);

		if (varDesc.DefaultValue)
			memcpy((char *)cbInfo.mConstantBuffer.data() + varDesc.StartOffset, varDesc.DefaultValue, varDesc.Size);

		ID3D11ShaderReflectionType *type = var->GetType();
		if (!type)
			continue;

		D3D11_SHADER_TYPE_DESC typeDesc {};
		hr = type->GetDesc(&typeDesc);
		if (FAILED(hr))
			throw hr;

		if (typeDesc.Class == D3D_SVC_STRUCT) {
			auto frameRef = VDDCustomEffectFrameRef::Parse(varDesc.Name, mPassIndex);

			if (frameRef.mbValid) {
				if (typeDesc.Class == D3D_SVC_STRUCT)
					ParseFrameStruct(frameRef.mPassIndex, frameRef.mElementIndex, varDesc, *type, typeDesc, cbInfo, varStorage);
			} 
		} else if (typeDesc.Class == D3D_SVC_MATRIX_ROWS || typeDesc.Class == D3D_SVC_MATRIX_COLUMNS) {
			if (typeDesc.Type != D3D_SVT_FLOAT)
				continue;

			const bool rowMajor = typeDesc.Class == D3D_SVC_MATRIX_ROWS;
			const uint32 regs = rowMajor ? typeDesc.Columns : typeDesc.Rows;
			const uint32 comps = rowMajor ? typeDesc.Rows : typeDesc.Columns;

			// HLSL 4 allocates a full register for each row of the stored matrix, but the
			// last register can be partial.
			if (varDesc.Size != (regs - 1)*16 + 4*comps || typeDesc.Rows > 4 || typeDesc.Columns > 4)
				throw VDException("Unexpected size %d for %dx%d matrix '%s' (%s)", varDesc.Size, typeDesc.Rows, typeDesc.Columns, varDesc.Name, rowMajor ? "row-major" : "column-major");

			VDStringSpanA name(varDesc.Name);
			
			if (name == "modelViewProj") {
				VDDCustomEffectVec4x4Gather gather {};
				
				if (rowMajor)
					gather = varStorage.RequestRowMajorMatrix(varDesc.StartOffset, VDDCustomEffectVariable::ModelViewProj, 0, 0);
				else
					gather = varStorage.RequestColumnMajorMatrix(varDesc.StartOffset, VDDCustomEffectVariable::ModelViewProj, 0, 0);

				for(const auto& vecGather : gather.mVec) {
					for(UINT x = 0; x < comps; ++x) {
						cbInfo.mFloatGathers.emplace_back(
							vecGather.mDstOffset + 4*x,
							vecGather.mOffset[x]
						);
					}
				}
			}
		} else if (typeDesc.Class == D3D_SVC_VECTOR) {
			if (isCbKnown) {
				VDStringSpanA memberNameView(varDesc.Name);
				VDDCustomEffectVariable var {};

				if (isCbOrig) {
					if (memberNameView == "orig_video_size") {
						var = VDDCustomEffectVariable::VideoSize;
					} else if (memberNameView == "orig_texture_size") {
						var = VDDCustomEffectVariable::TextureSize;
					} else if (memberNameView == "orig_output_size") {
						var = VDDCustomEffectVariable::OutputSize;
					}
				}

				if (memberNameView == "video_size")
					var = VDDCustomEffectVariable::VideoSize;
				else if (memberNameView == "texture_size")
					var = VDDCustomEffectVariable::TextureSize;
				else if (memberNameView == "output_size")
					var = VDDCustomEffectVariable::OutputSize;
				else if (memberNameView == "frame_count") {
					var = VDDCustomEffectVariable::FrameCount;

					// any reference to frame_count forces the pass to run
					mbPassCanCache = false;
				} else if (memberNameView == "frame_direction")
					var = VDDCustomEffectVariable::FrameDirection;

				if (var != VDDCustomEffectVariable::None) {
					mPassInputReferenceList.push_back(cbPassIndex);

					auto vecGather = varStorage.RequestVector(0, var, cbPassIndex,
						var == VDDCustomEffectVariable::FrameCount ? cbTextureIndex : 0);
					for(uint32 i = 0, n = std::min<uint32>(typeDesc.Columns, 4);
						i < n;
						++i)
					{
						cbInfo.mFloatGathers.emplace_back(
							varDesc.StartOffset + typeDesc.Offset + 4*i,
							vecGather.mOffset[i]
						);
					}
				}
			}
		}
	}
}

void VDDCustomEffectPassD3D11::ParseFrameStruct(uint32 varPassIndex, uint32 varElementIndex, const D3D11_SHADER_VARIABLE_DESC& varDesc, ID3D11ShaderReflectionType& type, const D3D11_SHADER_TYPE_DESC& typeDesc, ConstantBufferInfo& cbInfo, VDDCustomEffectVarStorage& varStorage) {
	for(UINT memberIndex = 0; memberIndex < typeDesc.Members; ++memberIndex) {
		const LPCSTR memberName = type.GetMemberTypeName(memberIndex);
		ID3D11ShaderReflectionType *memberType = type.GetMemberTypeByIndex(memberIndex);
		if (!memberType)
			continue;

		D3D11_SHADER_TYPE_DESC memberTypeDesc {};
		HRESULT hr = memberType->GetDesc(&memberTypeDesc);
		if (FAILED(hr))
			throw hr;

		if (memberTypeDesc.Type != D3D_SVT_FLOAT)
			continue;

		const uint32 maxFloats = varDesc.Size - memberTypeDesc.Offset;

		if (memberTypeDesc.Offset >= varDesc.Size
			|| (uint64)memberTypeDesc.Rows * memberTypeDesc.Columns > maxFloats)
			throw VDException("Constant buffer member %hs.%hs extends outside of variable", varDesc.Name, memberName);

		VDStringSpanA memberNameView(memberName);
		VDDCustomEffectVariable var {};

		if (memberNameView == "video_size")
			var = VDDCustomEffectVariable::VideoSize;
		else if (memberNameView == "texture_size")
			var = VDDCustomEffectVariable::TextureSize;
		else if (memberNameView == "output_size")
			var = VDDCustomEffectVariable::OutputSize;
		else if (memberNameView == "frame_count") {
			var = VDDCustomEffectVariable::FrameCount;

			// any reference to frame_count forces the pass to run
			mbPassCanCache = false;
		} else if (memberNameView == "frame_direction")
			var = VDDCustomEffectVariable::FrameDirection;
		else
			continue;

		mPassInputReferenceList.push_back(varPassIndex);

		auto vecGather = varStorage.RequestVector(0, var, varPassIndex,
			var == VDDCustomEffectVariable::FrameCount ? varElementIndex : 0);
		for(uint32 i = 0, n = std::min<uint32>(memberTypeDesc.Columns, 4);
			i < n;
			++i)
		{
			cbInfo.mFloatGathers.emplace_back(
				varDesc.StartOffset + memberTypeDesc.Offset + 4*i,
				vecGather.mOffset[i]
			);
		}
	}
}

////////////////////////////////////////////////////////////////////////////////

VDDCustomEffectD3D11::VDDCustomEffectD3D11(IVDTContext& context, VDDisplayNodeContext3D& dctx)
	: mContext(context)
	, mDisplayContext(dctx)
{
}

VDDCustomEffectD3D11::~VDDCustomEffectD3D11() {
	while(!mPasses.empty()) {
		auto *p = mPasses.back();
		mPasses.pop_back();

		delete p;
	}

	for(auto& tex : mCustomTextures) {
		vdsaferelease <<= tex.mpTexture;
	}

	mCustomTextures.clear();
}

bool VDDCustomEffectD3D11::ContainsFinalBlit() const {
	return !mPasses.empty() && mPasses.back()->HasScalingFactor();
}

bool VDDCustomEffectD3D11::HasTimingInfo() const {
	return !mPassInfos.empty();
}

vdspan<const VDDisplayCustomShaderPassInfo> VDDCustomEffectD3D11::GetPassTimings() {
	if (mbPollingQueries) {
		if (!mPollQueryIndex) {
			if (!mpTimestampFrequencyQuery->IsPending()) {
				++mPollQueryIndex;

				mTimestampFrequency = mpTimestampFrequencyQuery->GetTimestampFrequency();
			}
		}

		for(;;) {
			if (mPollQueryIndex > mTimestamps.size()) {
				mbPollingQueries = false;
				break;
			}

			IVDTTimestampQuery *q = mpTimestampQueries[mPollQueryIndex - 1];

			if (q->IsPending())
				break;

			mTimestamps[mPollQueryIndex - 1] = q->GetTimestamp();
			++mPollQueryIndex;
		}

		if (!mbPollingQueries) {
			const size_t n = mPasses.size();
			const double ticksToSeconds = mTimestampFrequency > 0 ? 1.0 / mTimestampFrequency : 0;

			for(size_t i = 0; i < n; ++i) {
				VDDCustomEffectPassD3D11 *pass = static_cast<VDDCustomEffectPassD3D11 *>(mPasses[i]);
				const uint64 timestamp0 = mTimestamps[i];
				const uint64 timestamp1 = mTimestamps[i+1];
				VDDisplayCustomShaderPassInfo& passInfo = mPassInfos[i];

				passInfo.mTiming = timestamp0 < timestamp1 ? (double)(timestamp1 - timestamp0) * ticksToSeconds : 0;

				const auto& output = pass->GetOutputTexSpecs()[0];
				passInfo.mOutputWidth = output.mImageWidth;
				passInfo.mOutputHeight = output.mImageHeight;
				passInfo.mbOutputLinear = output.mbLinear;
				passInfo.mbOutputHalfFloat = pass->IsOutputHalfFloat();
				passInfo.mbOutputFloat = pass->IsOutputFloat();
				passInfo.mbOutputSrgb = pass->IsOutputSrgb();
				passInfo.mbCached = !mInputInvalidationTable[i + 1];
			}

			const uint64 tsStart = mTimestamps.front();
			const uint64 tsEnd = mTimestamps.back();
			mPassInfos.back().mTiming = tsStart < tsEnd ? (double)(tsEnd - tsStart) * ticksToSeconds : 0;
		}
	}

	return mPassInfos;
}

void VDDCustomEffectD3D11::PreRun() {
	if (mpTimestampFrequencyQuery && !mbIssuingQueries && !mbPollingQueries) {
		mbIssuingQueries = true;

		mpTimestampFrequencyQuery->Begin();

		mpTimestampQueries[0]->Issue();
	}
}

void VDDCustomEffectD3D11::Run(IVDTTexture2D *const *srcTextures, const vdint2& texSize, const vdint2& imageSize, const vdint2& viewportSize) {
	if (mPasses.empty())
		return;

	// clear pass input invalidation table
	std::ranges::fill(mInputInvalidationTable, false);

	// check if input has been invalidated
	if (mbNewFrame) {
		mbNewFrame = false;

		mInputInvalidationTable[0] = true;
	}

	VDASSERT(!mOrigTexSpecs.empty());

	for(auto& origTexSpec : mOrigTexSpecs) {
		origTexSpec.mImageWidth = imageSize.x;
		origTexSpec.mImageHeight = imageSize.y;
		origTexSpec.mTexWidth = texSize.x;
		origTexSpec.mTexHeight = texSize.y;
		origTexSpec.mpTexture = *srcTextures++;
	}

	mTexSpecGrid[0] = VDDTexSpecView<const VDDCustomEffectPassD3D11::TextureSpec>(std::from_range, mOrigTexSpecs);

	auto it = mPasses.begin();
	auto itEnd = mPasses.end();

	if (ContainsFinalBlit())
		--itEnd;

	if (it == itEnd)
		return;

	uint32 passIndex = 0;
	for(; it != itEnd; ++it, ++passIndex) {
		VDDCustomEffectPassD3D11 *pass = static_cast<VDDCustomEffectPassD3D11 *>(*it);

		bool passShouldRun = pass->Run(nullptr, mTexSpecGrid, viewportSize, false, mInputInvalidationTable, mVarStorage);

		mInputInvalidationTable[passIndex + 1] = passShouldRun;

		mTexSpecGrid[passIndex + 1] = pass->GetOutputTexSpecs();
	}
	
	VDTAutoScope scope(mContext, "Custom effect main passes");

	passIndex = 0;

	for(it = mPasses.begin(); it != itEnd; ++it, ++passIndex) {
		VDDCustomEffectPassD3D11 *pass = static_cast<VDDCustomEffectPassD3D11 *>(*it);
		VDDCustomEffectPassD3D11 *passNext = it + 1 != mPasses.end() ? static_cast<VDDCustomEffectPassD3D11 *>(*(it + 1)) : nullptr;

		if (mInputInvalidationTable[passIndex + 1]) {
			mCommandList.ExecuteRange(mContext, mDisplayContext,
				pass->GetCommandIndex(),
				passNext ? passNext->GetCommandIndex() : mCommandList.GetNextCommandIndex()
			);
		}

		if (mbIssuingQueries)
			mpTimestampQueries[passIndex + 1]->Issue();
	}
}

void VDDCustomEffectD3D11::RunFinal(const VDDRenderView& renderView, const vdrect32f& dstRect, const vdint2& viewportSize) {
	if (mPasses.empty() || !ContainsFinalBlit())
		return;
		
	VDDCustomEffectPassD3D11 *pass = static_cast<VDDCustomEffectPassD3D11 *>(mPasses.back());

	mInputInvalidationTable.back() = pass->Run(&dstRect, mTexSpecGrid, viewportSize, true, mInputInvalidationTable, mVarStorage);
		
	mTexSpecGrid.back() = pass->GetOutputTexSpecs();

	mCommandList.SetRenderView(pass->GetRenderViewId(), renderView);

	{
		VDTAutoScope scope(mContext, "Custom effect final pass");

		mCommandList.Execute(mContext, mDisplayContext, pass->GetCommandIndex(), 1);

		if (mbIssuingQueries)
			mpTimestampQueries.back()->Issue();
	}
}

void VDDCustomEffectD3D11::PostRun() {
	if (mbIssuingQueries) {
		mpTimestampFrequencyQuery->End();

		mbIssuingQueries = false;
		mbPollingQueries = true;
		mPollQueryIndex = 0;
	}
}

VDDisplaySourceTexMapping VDDCustomEffectD3D11::ComputeFinalOutputMapping(const vdint2& srcSize, const vdint2& viewportSize) const {
	return static_cast<VDDCustomEffectPassD3D11 *>(mPasses.back())->ComputeOutputMapping(srcSize, viewportSize);
}

IVDTTexture2D *VDDCustomEffectD3D11::GetFinalOutput() {
	const auto& outputFrame = mTexSpecGrid.back()[0];

	return outputFrame.mpTexture;
}

void VDDCustomEffectD3D11::LoadTexture(const char *name, const wchar_t *path, bool linear) {
	auto insertResult = mCustomTextureLookup.insert_as(VDStringSpanA(name));
	if (!insertResult.second)
		return;

	VDPixmapBuffer buf;
	VDDLoadCustomShaderTexture(buf, path);

	const auto& caps = mContext.GetDeviceCaps();

	if ((uint32)buf.w > caps.mMaxTextureWidth || (uint32)buf.h > caps.mMaxTextureHeight) {
		throw VDException("Unable to create %dx%d texture '%s': exceeds device limit of %ux%u"
			, buf.w
			, buf.h
			, name
			, caps.mMaxTextureWidth
			, caps.mMaxTextureHeight
		);
	}

	if (!caps.mbNonPow2 && ((buf.w & (buf.w - 1)) || (buf.h & (buf.h - 1)))) {
		throw VDException("Unable to create %dx%d texture '%s': device requires power-of-two texture sizes"
			, buf.w
			, buf.h
			, name
		);
	}

	VDTInitData2D initData {};
	initData.mpData = buf.data;
	initData.mPitch = buf.pitch;

	vdrefptr<IVDTTexture2D> tex;
	if (!mContext.CreateTexture2D(buf.w, buf.h, mDisplayContext.mBGRAFormat, 1, VDTUsage::Shader, &initData, ~tex))
		throw VDException("Unable to create %dx%d texture '%s'", buf.w, buf.h, name);

	insertResult.first->second = (uint32)mCustomTextures.size();

	mCustomTextures.emplace_back();
	auto& newTex = mCustomTextures.back();
	newTex.mpTexture = tex.release();
	newTex.mImageWidth = buf.w;
	newTex.mImageHeight = buf.h;
	newTex.mTexWidth = buf.w;
	newTex.mTexHeight = buf.h;
	newTex.mbLinear = linear;

	newTex.mTexturePoolIndex = mCommandList.RegisterTexture(newTex.mpTexture);
}

void VDDCustomEffectD3D11::BeginPasses(uint32 numPasses) {
	VDTSamplerStateDesc ssdesc = {};
	ssdesc.mFilterMode = kVDTFilt_Point;
	ssdesc.mAddressU = kVDTAddr_Border;
	ssdesc.mAddressV = kVDTAddr_Border;
	ssdesc.mAddressW = kVDTAddr_Border;

	if (!mContext.GetDeviceCaps().mbSamplerBorder) {
		ssdesc.mAddressU = kVDTAddr_Clamp;
		ssdesc.mAddressV = kVDTAddr_Clamp;
		ssdesc.mAddressW = kVDTAddr_Clamp;
	}

	if (!mContext.CreateSamplerState(ssdesc, ~mpSSBorderPoint))
		throw VDException("Unable to create sampler state");

	ssdesc.mFilterMode = kVDTFilt_Bilinear;

	if (!mContext.CreateSamplerState(ssdesc, ~mpSSBorderLinear))
		throw VDException("Unable to create sampler state");

	mTexSpecGrid.resize(numPasses + 1);
}

void VDDCustomEffectD3D11::AddPass(uint32 passIndex, const VDDisplayCustomShaderProps& props, const char *shaderPath, const wchar_t *basePath) {
	mPasses.push_back(nullptr);
	auto *pass = new VDDCustomEffectPassD3D11(*this, mContext, mDisplayContext, mCommandList, passIndex);
	mPasses.back() = pass;

	pass->Init(
		shaderPath,
		props,
		mCustomTextureLookup,
		mCustomTextures,
		mPassInputsFiltered,
		basePath,
		mMaxPrevFramesPerPass,
		mVarStorage
	);
}

void VDDCustomEffectD3D11::EndPasses() {
	// We always run the last pass regardless of whether it's cachable. Run() needs to
	// know this, or it will fail to update some necessary shader parameters.
	static_cast<VDDCustomEffectPassD3D11 *>(mPasses.back())->SetPassUncacheable();

	mOrigTexSpecs.resize(mMaxPrevFramesPerPass[0] + 1);

	uint32 frameIndex = 0;
	for(auto& texSpec : mOrigTexSpecs)
		mVarStorage.ResolveFrameParamOffsets(texSpec.mFrameParams, 0, frameIndex++);
}

void VDDCustomEffectD3D11::InitProfiling() {
	mpTimestampFrequencyQuery = mContext.CreateTimestampFrequencyQuery();
	if (!mpTimestampFrequencyQuery)
		return;

	size_t n = mPasses.size() + 1;
	mpTimestampQueries.resize(n);

	for(auto& tq : mpTimestampQueries) {
		tq = mContext.CreateTimestampQuery();

		if (!tq) {
			ShutdownProfiling();
			return;
		}
	}

	mTimestamps.resize(n, 0);
	mPassInfos.resize(n, {});
}

void VDDCustomEffectD3D11::ShutdownProfiling() {
	mpTimestampFrequencyQuery = nullptr;

	for(auto& tq : mpTimestampQueries)
		tq = nullptr;
}

////////////////////////////////////////////////////////////////////////////////

vdrefptr<IVDDisplayCustomEffectD3D11> VDDisplayParseCustomEffectD3D11(IVDTContext& context, VDDisplayNodeContext3D& dctx, const wchar_t *path) {
	auto p = vdmakerefptr(new VDDCustomEffectD3D11(context, dctx));

	p->Parse(path);

	return p;
}
