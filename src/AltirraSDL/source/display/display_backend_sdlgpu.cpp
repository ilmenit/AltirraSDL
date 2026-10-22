//	AltirraSDL - SDL_GPU display backend implementation

#include <stdafx.h>
#include "display_backend_sdlgpu.h"
#include "logging.h"
#include "uiaccessors.h"
#include "uitypes.h"
#include "simulator.h"
#include "gtia.h"
#include "video_output.h"

extern ATSimulator g_sim;

#include <vd2/VDDisplay/internal/screenfx.h>
#include <vd2/VDDisplay/internal/bloom.h>

#include <algorithm>
#include <cmath>
#include <cstring>

#include "shaders/generated/sdlgpu_screenfx_frag_dxil.h"
#include "shaders/generated/sdlgpu_screenfx_frag_spv.h"
#include "shaders/generated/sdlgpu_screenfx_frag_msl.h"

// SDL owns the swapchain. Never change its parameters behind the renderer:
// its backbuffer format and all cached pipelines must agree with the composition.
SDL_Renderer *DisplayBackendSDLGPU::CreateRenderer(SDL_Window *window) {
	const auto windowProps = SDL_GetWindowProperties(window);
	if (SDL_GetBooleanProperty(windowProps,
		SDL_PROP_WINDOW_HDR_ENABLED_BOOLEAN, false))
	{
		const auto props = SDL_CreateProperties();
		SDL_SetStringProperty(props, SDL_PROP_RENDERER_CREATE_NAME_STRING, SDL_GPU_RENDERER);
		SDL_SetPointerProperty(props, SDL_PROP_RENDERER_CREATE_WINDOW_POINTER, window);
		SDL_SetNumberProperty(props, SDL_PROP_RENDERER_CREATE_OUTPUT_COLORSPACE_NUMBER,
			SDL_COLORSPACE_SRGB_LINEAR);
		SDL_Renderer *renderer = SDL_CreateRendererWithProperties(props);
		SDL_DestroyProperties(props);
		if (renderer) {
			SDL_GPUDevice *device = SDL_GetGPURendererDevice(renderer);
			if (device && SDL_WindowSupportsGPUSwapchainComposition(device, window,
				SDL_GPU_SWAPCHAINCOMPOSITION_HDR_EXTENDED_LINEAR)
				&& SDL_GetGPUSwapchainTextureFormat(device, window)
					== SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT)
			{
				LOG_INFO("SDL_GPU", "Linear HDR swapchain available");
				return renderer;
			}
			SDL_DestroyRenderer(renderer);
		}
		LOG_INFO("SDL_GPU", "HDR composition unavailable; using SDR: %s", SDL_GetError());
	}
	return SDL_CreateGPURenderer(nullptr, window);
}

bool DisplayBackendSDLGPU::SupportsHDR() const {
	return SDL_GetNumberProperty(SDL_GetRendererProperties(mpRenderer),
		SDL_PROP_RENDERER_OUTPUT_COLORSPACE_NUMBER, SDL_COLORSPACE_SRGB)
		== SDL_COLORSPACE_SRGB_LINEAR;
}

bool DisplayBackendSDLGPU::IsHDREnabled() const {
	return SupportsHDR() && SDL_GetBooleanProperty(
		SDL_GetRendererProperties(mpRenderer),
		SDL_PROP_RENDERER_HDR_ENABLED_BOOLEAN, false);
}

ATHDROutputLevels DisplayBackendSDLGPU::GetOutputLevels() const {
	const auto& params = g_sim.GetGTIA().GetArtifactingParams();
	return ATComputeHDROutputLevels(SupportsHDR(), IsHDREnabled(),
		params.mbEnableHDR, g_sim.GetGTIA().GetAccelScreenFXEnabled(),
		SDL_GetFloatProperty(SDL_GetRendererProperties(mpRenderer),
			SDL_PROP_RENDERER_SDR_WHITE_POINT_FLOAT, 1.0f),
		params.mbUseSystemSDRAsHDR, params.mHDRIntensity,
		params.mbUseSystemSDR, params.mSDRIntensity,
		ATSDLGetActiveVideoOutput() != nullptr);
}

void DisplayBackendSDLGPU::SetOutputConstants(ShaderConstants& constants,
	SDL_Texture *source) const
{
	const auto levels = GetOutputLevels();
	constants.hdrParams[0] = levels.mFrameScale;
	constants.hdrParams[1] = SupportsHDR()
		&& (source == mpTexture || source == mpMaskTexture) ? 1.0f : 0.0f;
	constants.hdrParams[2] = levels.mbHDR ? 1.0f : 0.0f;
	SDL_ScaleMode scaleMode = SDL_SCALEMODE_NEAREST;
	SDL_GetTextureScaleMode(source, &scaleMode);
	constants.hdrParams[3] = scaleMode == SDL_SCALEMODE_LINEAR ? 1.0f : 0.0f;
}

DisplayBackendSDLGPU::DisplayBackendSDLGPU(SDL_Window *window,
	SDL_Renderer *renderer, SDL_GPUDevice *device, SDL_GPUShader *shader,
	SDL_GPURenderState *state)
	: mpWindow(window)
	, mpRenderer(renderer)
	, mpDevice(device)
	, mpShader(shader)
	, mpRenderState(state)
{
}

DisplayBackendSDLGPU *DisplayBackendSDLGPU::Create(SDL_Window *window,
	SDL_Renderer *renderer)
{
	if (!window || !renderer)
		return nullptr;

	SDL_GPUDevice *device = SDL_GetGPURendererDevice(renderer);
	if (!device) {
		LOG_ERROR("SDL_GPU", "SDL_GetGPURendererDevice failed: %s", SDL_GetError());
		return nullptr;
	}

	const SDL_GPUShaderFormat formats = SDL_GetGPUShaderFormats(device);
	SDL_GPUShaderCreateInfo shaderInfo {};
	shaderInfo.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
	shaderInfo.num_samplers = 1;
	shaderInfo.num_uniform_buffers = 1;

	if (formats & SDL_GPU_SHADERFORMAT_DXIL) {
		shaderInfo.format = SDL_GPU_SHADERFORMAT_DXIL;
		shaderInfo.code = kATSDLGPU_ScreenFX_DXIL;
		shaderInfo.code_size = kATSDLGPU_ScreenFX_DXIL_len;
		shaderInfo.entrypoint = "main";
	} else if (formats & SDL_GPU_SHADERFORMAT_SPIRV) {
		shaderInfo.format = SDL_GPU_SHADERFORMAT_SPIRV;
		shaderInfo.code = kATSDLGPU_ScreenFX_SPIRV;
		shaderInfo.code_size = kATSDLGPU_ScreenFX_SPIRV_len;
		shaderInfo.entrypoint = "main";
	} else if (formats & SDL_GPU_SHADERFORMAT_MSL) {
		shaderInfo.format = SDL_GPU_SHADERFORMAT_MSL;
		shaderInfo.code = kATSDLGPU_ScreenFX_MSL;
		shaderInfo.code_size = kATSDLGPU_ScreenFX_MSL_len;
		shaderInfo.entrypoint = "main0";
	} else {
		LOG_ERROR("SDL_GPU", "No bundled shader format is supported by %s",
			SDL_GetGPUDeviceDriver(device));
		return nullptr;
	}

	SDL_GPUShader *shader = SDL_CreateGPUShader(device, &shaderInfo);
	if (!shader) {
		LOG_ERROR("SDL_GPU", "Display shader creation failed: %s", SDL_GetError());
		return nullptr;
	}

	SDL_GPURenderStateCreateInfo stateInfo {};
	stateInfo.fragment_shader = shader;
	SDL_GPURenderState *state = SDL_CreateGPURenderState(renderer, &stateInfo);
	if (!state) {
		LOG_ERROR("SDL_GPU", "Custom render state creation failed: %s", SDL_GetError());
		SDL_ReleaseGPUShader(device, shader);
		return nullptr;
	}

	LOG_INFO("SDL_GPU", "Display backend initialized: driver=%s formats=0x%x",
		SDL_GetGPUDeviceDriver(device), (unsigned)formats);
	auto *backend = new DisplayBackendSDLGPU(window, renderer, device, shader, state);
	int pixelW = 0;
	int pixelH = 0;
	SDL_GetWindowSizeInPixels(window, &pixelW, &pixelH);
	backend->OnResize(pixelW, pixelH);
	return backend;
}

DisplayBackendSDLGPU::~DisplayBackendSDLGPU() {
	DestroyCustomEffect();
	DestroyIntermediateTargets();
	if (mpMaskTexture)
		SDL_DestroyTexture(mpMaskTexture);
	if (mpTexture)
		SDL_DestroyTexture(mpTexture);
	if (mpRenderState)
		SDL_DestroyGPURenderState(mpRenderState);
	if (mpShader)
		SDL_ReleaseGPUShader(mpDevice, mpShader);
}

void DisplayBackendSDLGPU::UploadFrame(const void *pixels, int width, int height,
	int pitch)
{
	if (!pixels || width <= 0 || height <= 0)
		return;

	if (!mpTexture || mTexW != width || mTexH != height) {
		if (mpTexture)
			SDL_DestroyTexture(mpTexture);

		mpTexture = SDL_CreateTexture(mpRenderer, SDL_PIXELFORMAT_XRGB8888,
			SDL_TEXTUREACCESS_STREAMING, width, height);
		mTexW = mpTexture ? width : 0;
		mTexH = mpTexture ? height : 0;
		SetFilterMode(mFilterMode);
	}

	if (mpTexture && !SDL_UpdateTexture(mpTexture, nullptr, pixels, pitch)) {
		LOG_ERROR("SDL_GPU", "Frame upload failed: %s", SDL_GetError());
		SDL_DestroyTexture(mpTexture);
		mpTexture = nullptr;
		mTexW = 0;
		mTexH = 0;
	}
}

void DisplayBackendSDLGPU::BeginFrame() {
	SDL_SetRenderTarget(mpRenderer, nullptr);
	// ImGui and normal SDL draws use SDR brightness; the emulator's custom
	// shader uses its own independently calibrated HDR brightness.
	SDL_SetRenderColorScale(mpRenderer, GetOutputLevels().mUIScale);
	// Scale is render-target state in SDL3. Reassert the window scale every
	// frame so that an aborted offscreen pass cannot leave Retina/HiDPI output
	// in physical-pixel coordinates.
	SDL_SetRenderScale(mpRenderer, mRenderScaleX, mRenderScaleY);
	SDL_SetRenderDrawColor(mpRenderer, 0, 0, 0, 255);
	SDL_RenderClear(mpRenderer);
}

DisplayBackendSDLGPU::ShaderConstants
DisplayBackendSDLGPU::BuildShaderConstants(float dstW, float dstH,
	int srcW, int srcH) const
{
	ShaderConstants c {};
	c.sourceDestSize[0] = (float)srcW;
	c.sourceDestSize[1] = (float)srcH;
	c.sourceDestSize[2] = dstW;
	c.sourceDestSize[3] = dstH;

	static constexpr float kSharpFactors[5] = {
		1.259f, 1.587f, 2.0f, 2.520f, 3.175f
	};
	const int sharpIndex = std::clamp((int)mFilterSharpness + 2, 0, 4);
	const float sharpFactor = kSharpFactors[sharpIndex];
	c.filterParams[0] = (float)mFilterMode;
	c.filterParams[1] = std::max(1.0f, sharpFactor * 0.5f);
	c.filterParams[2] = std::max(1.0f, sharpFactor);
	c.filterParams[3] = mScreenFX.mPALBlendingOffset;

	c.effectParams1[0] = mScreenFX.mGamma > 0.0f ? mScreenFX.mGamma : 1.0f;
	c.effectParams1[1] = mScreenFX.mOutputGamma;
	c.effectParams1[2] = mScreenFX.mScanlineIntensity;
	c.effectParams1[3] = mScreenFX.mVignetteIntensity;

	if (mScreenFX.mDistortionX > 0.0f && dstW > 0.0f && dstH > 0.0f) {
		VDDisplayDistortionMapping mapping;
		mapping.Init(mScreenFX.mDistortionX, mScreenFX.mDistortionYRatio,
			dstW / dstH);
		c.effectParams2[0] = mapping.mScaleX;
		c.effectParams2[1] = mapping.mScaleY;
		c.effectParams2[2] = mapping.mSqRadius;
	}
	c.effectParams2[3] = mScreenFX.mbSignedRGBEncoding ? 1.0f : 0.0f;

	c.bloomParams[0] = mScreenFX.mbBloomEnabled ? 1.0f : 0.0f;
	c.bloomParams[1] = std::max(1.0f, mScreenFX.mBloomRadius);
	c.bloomParams[2] = mScreenFX.mBloomDirectIntensity;
	c.bloomParams[3] = mScreenFX.mBloomIndirectIntensity;

	const VDDScreenMaskParams& mask = mScreenFX.mScreenMaskParams;
	c.maskParams[0] = (float)(int)mask.mType;
	c.maskParams[1] = mask.mSourcePixelsPerDot;
	c.maskParams[2] = mask.mOpenness;
	c.maskParams[3] = mask.mbScreenMaskIntensityCompensation ? 1.0f : 0.0f;

	bool hasColorCorrection = false;
	for (int i = 0; i < 3; ++i) {
		for (int j = 0; j < 3; ++j)
			hasColorCorrection |= mScreenFX.mColorCorrectionMatrix[i][j] != 0.0f;
	}
	c.colorFlags[0] = hasColorCorrection ? 1.0f : 0.0f;
	c.colorFlags[1] = mask.mType != VDDScreenMaskType::None ? 1.0f : 0.0f;
	c.colorFlags[2] = mScreenFX.mBloomThreshold;

	float matrix[3][3] {};
	if (hasColorCorrection) {
		memcpy(matrix, mScreenFX.mColorCorrectionMatrix, sizeof(matrix));
	} else {
		matrix[0][0] = 1.0f;
		matrix[1][1] = 1.0f;
		matrix[2][2] = 1.0f;
	}
	if (mask.mType != VDDScreenMaskType::None
		&& mask.mbScreenMaskIntensityCompensation)
	{
		const float scale = 1.0f / std::max(mask.GetMaskIntensityScale(), 0.01f);
		for (auto& row : matrix)
			for (float& value : row)
				value *= scale;
	}
	for (int i = 0; i < 3; ++i) {
		c.colorMatrix0[i] = matrix[i][0];
		c.colorMatrix1[i] = matrix[i][1];
		c.colorMatrix2[i] = matrix[i][2];
	}

	return c;
}

bool DisplayBackendSDLGPU::ApplyShaderConstants(
	const ShaderConstants& constants)
{
	if (!SDL_SetGPURenderStateFragmentUniforms(mpRenderState, 0, &constants,
		sizeof(constants))) {
		LOG_ERROR("SDL_GPU", "Display uniform update failed: %s", SDL_GetError());
		return false;
	}
	return true;
}

void DisplayBackendSDLGPU::RenderFrame(float dstX, float dstY, float dstW,
	float dstH, int srcW, int srcH)
{
	if (!mpTexture || srcW <= 0 || srcH <= 0 || dstW <= 0.0f
		|| dstH <= 0.0f || !std::isfinite(dstX) || !std::isfinite(dstY)
		|| !std::isfinite(dstW) || !std::isfinite(dstH))
		return;

	const float physicalDstW = dstW * mRenderScaleX;
	const float physicalDstH = dstH * mRenderScaleY;
	ShaderConstants constants = BuildShaderConstants(
		physicalDstW, physicalDstH, srcW, srcH);
	SyncCustomEffect(physicalDstW, physicalDstH, srcW, srcH);
	int sourceW = srcW, sourceH = srcH;
	SDL_Texture *source = RenderCustomEffect(physicalDstW, physicalDstH,
		sourceW, sourceH);
	if (!source) { source = mpTexture; sourceW = srcW; sourceH = srcH; }
	constants = BuildShaderConstants(physicalDstW, physicalDstH, sourceW, sourceH);
	srcW = sourceW;
	srcH = sourceH;

	if (mScreenFX.mPALBlendingOffset != 0.0f
		&& EnsureTarget(mpPALTexture, mPALW, mPALH, srcW, srcH,
			SupportsHDR() ? SDL_PIXELFORMAT_RGBA64_FLOAT
			: SDL_PIXELFORMAT_RGBA8888))
	{
		SDL_ScaleMode oldScaleMode = SDL_SCALEMODE_LINEAR;
		SDL_GetTextureScaleMode(source, &oldScaleMode);
		// Match the established GL path: PAL chroma blending always samples
		// bilinearly, independent of the user's final display filter.
		SDL_SetTextureScaleMode(source, SDL_SCALEMODE_LINEAR);
		ShaderConstants pal = constants;
		pal.passParams0[0] = 1.0f;
		const bool palRendered = RenderPass(source, mpPALTexture, pal,
			SDL_BLENDMODE_NONE, true);
		SDL_SetTextureScaleMode(source, oldScaleMode);
		if (palRendered) {
			source = mpPALTexture;
			const SDL_ScaleMode finalScaleMode =
				mFilterMode == kATDisplayFilterMode_Point
				|| mFilterMode == kATDisplayFilterMode_Bicubic
					? SDL_SCALEMODE_NEAREST : SDL_SCALEMODE_LINEAR;
			SDL_SetTextureScaleMode(source, finalScaleMode);
			constants.filterParams[3] = 0.0f;
			constants.effectParams2[3] = 0.0f;
		}
	}

	// Match the OpenGL pipeline order. Bicubic expansion is a destination-size
	// prepass before bloom; sharp bilinear remains a final-pass filter so its
	// source-pixel grid is retained even when bloom is enabled.
	if (mFilterMode == kATDisplayFilterMode_Bicubic) {
		const int outputW = std::max(mOutputW, 1);
		const int outputH = std::max(mOutputH, 1);
		const int filterW = std::clamp((int)std::ceil(physicalDstW), 1,
			outputW);
		const int filterH = std::clamp((int)std::ceil(physicalDstH), 1,
			outputH);
		if (EnsureTarget(mpFilterTexture, mFilterW, mFilterH,
				filterW, filterH, SupportsHDR() ? SDL_PIXELFORMAT_RGBA64_FLOAT
					: SDL_PIXELFORMAT_RGBA8888))
		{
			ShaderConstants bicubic = constants;
			bicubic.passParams0[0] = 8.0f;
			if (RenderPass(source, mpFilterTexture, bicubic,
					SDL_BLENDMODE_NONE, true))
			{
				source = mpFilterTexture;
				sourceW = filterW;
				sourceH = filterH;
				constants = BuildShaderConstants(physicalDstW,
					physicalDstH, sourceW, sourceH);
				constants.filterParams[0] =
					(float)kATDisplayFilterMode_Bilinear;
				constants.filterParams[3] = 0.0f;
				constants.effectParams2[3] = SupportsHDR()
					? bicubic.effectParams2[3] : 0.0f;
			}
		}
	}

	if (mScreenFX.mbBloomEnabled) {
		// VDDisplay's OpenGL bloom pyramid covers the entire physical output,
		// not only the emulator destination rectangle.
		const int bloomW = mOutputW > 0 ? mOutputW
			: std::max(1, (int)std::ceil(physicalDstW));
		const int bloomH = mOutputH > 0 ? mOutputH
			: std::max(1, (int)std::ceil(physicalDstH));
		if (SDL_Texture *bloom = RenderBloom(source, sourceW, sourceH,
			bloomW, bloomH, srcW,
			SupportsHDR() && constants.effectParams2[3] > 0.5f))
		{
			source = bloom;
			sourceW = bloomW;
			sourceH = bloomH;
			constants = BuildShaderConstants(
				physicalDstW, physicalDstH, sourceW, sourceH);
			if (mFilterMode == kATDisplayFilterMode_SharpBilinear) {
				constants.filterParams[0] =
					(float)kATDisplayFilterMode_SharpBilinear;
				// Sharp bilinear operates on the original source-pixel grid in
				// the established OpenGL path, even after a full-size bloom pass.
				constants.sourceDestSize[0] = (float)srcW;
				constants.sourceDestSize[1] = (float)srcH;
			} else {
				constants.filterParams[0] =
					(float)kATDisplayFilterMode_Bilinear;
			}
			constants.filterParams[3] = 0.0f;
			constants.effectParams2[3] = 0.0f;
			constants.bloomParams[0] = 0.0f;
		}
	}
	// Screen masks and scanlines are anchored to physical window pixels, not
	// to the top of the emulator destination rectangle.
	constants.maskParams[1] = (float)srcH;
	constants.maskParams[3] = dstY * mRenderScaleY;
	constants.colorFlags[3] = (float)mOutputH;

	const VDDScreenMaskParams& mask = mScreenFX.mScreenMaskParams;
	if (mask.mType != VDDScreenMaskType::None) {
		// Snap both destination edges to the physical pixel grid. The screen
		// mask is authored at one texel per physical output pixel, so its
		// intermediate target must be composited back at exactly 1:1. Rounding
		// only the width/height and then drawing to the original fractional
		// rectangle makes SDL duplicate or discard a column, which is visible
		// as a phase discontinuity in the aperture grille.
		const int effectX = (int)std::lround(dstX * mRenderScaleX);
		const int effectY = (int)std::lround(dstY * mRenderScaleY);
		const int effectRight = (int)std::lround(
			(dstX + dstW) * mRenderScaleX);
		const int effectBottom = (int)std::lround(
			(dstY + dstH) * mRenderScaleY);
		const int effectW = std::max(1, effectRight - effectX);
		const int effectH = std::max(1, effectBottom - effectY);
		if (EnsureTarget(mpScreenFXLinear, mScreenFXW, mScreenFXH,
				effectW, effectH)
			&& EnsureScreenMask((float)effectX, (float)effectY,
				effectW, effectH, srcW))
		{
			// Preserve the one-mask-texel-per-output-pixel relationship through
			// the final composite.
			SDL_SetTextureScaleMode(mpScreenFXLinear, SDL_SCALEMODE_NEAREST);
			ShaderConstants preMask = constants;
			preMask.sourceDestSize[2] = (float)effectW;
			preMask.sourceDestSize[3] = (float)effectH;
			preMask.passParams0[0] = 9.0f;
			if (RenderPass(source, mpScreenFXLinear, preMask,
					SDL_BLENDMODE_NONE, true)
				&& ApplyScreenMask(mpScreenFXLinear))
			{
				ShaderConstants postMask = constants;
				postMask.passParams0[0] = 10.0f;
				postMask.sourceDestSize[0] = (float)effectW;
				postMask.sourceDestSize[1] = (float)srcH;
				SetOutputConstants(postMask, mpScreenFXLinear);
				if (ApplyShaderConstants(postMask)
					&& SDL_SetGPURenderState(mpRenderer, mpRenderState))
				{
					SDL_SetTextureBlendMode(mpScreenFXLinear,
						SDL_BLENDMODE_NONE);
					const SDL_FRect rect {
						effectX / mRenderScaleX,
						effectY / mRenderScaleY,
						effectW / mRenderScaleX,
						effectH / mRenderScaleY
					};
					if (!SDL_RenderTexture(mpRenderer, mpScreenFXLinear,
							nullptr, &rect))
					{
						LOG_ERROR("SDL_GPU", "Display draw failed: %s",
							SDL_GetError());
					}
					SDL_SetGPURenderState(mpRenderer, nullptr);
					return;
				}
				LOG_ERROR("SDL_GPU", "Display render state failed: %s",
					SDL_GetError());
				SDL_SetGPURenderState(mpRenderer, nullptr);
			}
		}
	}

	SetOutputConstants(constants, source);
	const bool customStateActive = ApplyShaderConstants(constants)
		&& SDL_SetGPURenderState(mpRenderer, mpRenderState);
	if (!customStateActive) {
		LOG_ERROR("SDL_GPU", "Display render state failed: %s", SDL_GetError());
		SDL_SetGPURenderState(mpRenderer, nullptr);
	}
	SDL_SetTextureBlendMode(source, SDL_BLENDMODE_NONE);
	const SDL_FRect rect { dstX, dstY, dstW, dstH };
	if (!SDL_RenderTexture(mpRenderer, source, nullptr, &rect))
		LOG_ERROR("SDL_GPU", "Display draw failed: %s", SDL_GetError());
	SDL_SetGPURenderState(mpRenderer, nullptr);
}

bool DisplayBackendSDLGPU::EnsureTarget(SDL_Texture *&texture, int& currentW,
	int& currentH, int width, int height, SDL_PixelFormat format)
{
	if (texture && currentW == width && currentH == height)
		return true;
	if (texture)
		SDL_DestroyTexture(texture);
	texture = SDL_CreateTexture(mpRenderer, format,
		SDL_TEXTUREACCESS_TARGET, width, height);
	if (!texture) {
		currentW = 0;
		currentH = 0;
		LOG_ERROR("SDL_GPU", "Render target %dx%d creation failed: %s",
			width, height, SDL_GetError());
		return false;
	}
	currentW = width;
	currentH = height;
	SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_LINEAR);
	SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_NONE);
	return true;
}

bool DisplayBackendSDLGPU::RenderPass(SDL_Texture *source, SDL_Texture *target,
	const ShaderConstants& constants, SDL_BlendMode blendMode, bool clearTarget)
{
	if (!source || !target || !SDL_SetRenderTarget(mpRenderer, target))
		return false;
	// Renderer state is stored independently for each render target. Set the
	// offscreen target to pixel coordinates, then explicitly restore the
	// window's logical-to-physical scale after switching back. Reading the
	// scale here and applying it after SDL_SetRenderTarget(nullptr) would copy
	// the target's 1x scale onto the window and break Retina/HiDPI rendering.
	SDL_SetRenderScale(mpRenderer, 1.0f, 1.0f);
	if (clearTarget) {
		SDL_SetRenderDrawColorFloat(mpRenderer, 0.0f, 0.0f, 0.0f, 0.0f);
		SDL_RenderClear(mpRenderer);
	}
	ShaderConstants passConstants = constants;
	SetOutputConstants(passConstants, source);
	passConstants.hdrParams[0] = 0.0f;
	if (!ApplyShaderConstants(passConstants)) {
		SDL_SetRenderTarget(mpRenderer, nullptr);
		SDL_SetRenderScale(mpRenderer, mRenderScaleX, mRenderScaleY);
		return false;
	}
	if (!SDL_SetGPURenderState(mpRenderer, mpRenderState)) {
		LOG_ERROR("SDL_GPU", "Render-pass state failed: %s", SDL_GetError());
		SDL_SetRenderTarget(mpRenderer, nullptr);
		SDL_SetRenderScale(mpRenderer, mRenderScaleX, mRenderScaleY);
		return false;
	}
	if (!SDL_SetTextureBlendMode(source, blendMode)) {
		LOG_ERROR("SDL_GPU", "Render-pass blend mode failed: %s",
			SDL_GetError());
		SDL_SetGPURenderState(mpRenderer, nullptr);
		SDL_SetRenderTarget(mpRenderer, nullptr);
		SDL_SetRenderScale(mpRenderer, mRenderScaleX, mRenderScaleY);
		return false;
	}
	const bool result = SDL_RenderTexture(mpRenderer, source, nullptr, nullptr);
	SDL_SetTextureBlendMode(source, SDL_BLENDMODE_NONE);
	SDL_SetGPURenderState(mpRenderer, nullptr);
	SDL_SetRenderTarget(mpRenderer, nullptr);
	SDL_SetRenderScale(mpRenderer, mRenderScaleX, mRenderScaleY);
	return result;
}

bool DisplayBackendSDLGPU::EnsureScreenMask(float dstX, float dstY, int dstW,
	int dstH, int srcW)
{
	const VDDScreenMaskParams& mask = mScreenFX.mScreenMaskParams;
	const int textureH = mask.mType == VDDScreenMaskType::ApertureGrille
		? 1 : dstH;
	if (!mbMaskDirty && mpMaskTexture && mMaskW == dstW
		&& mMaskH == textureH && mMaskDstW == dstW && mMaskDstH == dstH
		&& mMaskSrcW == srcW && mMaskDstX == dstX && mMaskDstY == dstY)
	{
		return true;
	}

	mMaskBuffer.assign((size_t)dstW * textureH, 0);
	switch (mask.mType) {
		case VDDScreenMaskType::ApertureGrille: {
			VDDisplayApertureGrilleParams params(mask, (float)dstW,
				(float)srcW);
			VDDisplayCreateApertureGrilleTexture(mMaskBuffer.data(), dstW,
				dstX, params);
			break;
		}
		case VDDScreenMaskType::SlotMask: {
			VDDisplaySlotMaskParams params(mask, (float)dstW,
				(float)srcW);
			VDDisplayCreateSlotMaskTexture(mMaskBuffer.data(), dstW * 4,
				dstW, dstH, dstX, dstY, (float)dstW, (float)dstH, params);
			break;
		}
		case VDDScreenMaskType::DotTriad: {
			VDDisplayTriadDotMaskParams params(mask, (float)dstW,
				(float)srcW);
			VDDisplayCreateTriadDotMaskTexture(mMaskBuffer.data(), dstW * 4,
				dstW, dstH, dstX, dstY, (float)dstW, (float)dstH, params);
			break;
		}
		default:
			return false;
	}
	if (!mpMaskTexture || mMaskW != dstW || mMaskH != textureH) {
		if (mpMaskTexture)
			SDL_DestroyTexture(mpMaskTexture);
		mpMaskTexture = SDL_CreateTexture(mpRenderer,
			SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STATIC,
			dstW, textureH);
		if (!mpMaskTexture) {
			mMaskW = 0;
			mMaskH = 0;
			LOG_ERROR("SDL_GPU", "Screen mask texture creation failed: %s",
				SDL_GetError());
			return false;
		}
		mMaskW = dstW;
		mMaskH = textureH;
		SDL_SetTextureScaleMode(mpMaskTexture, SDL_SCALEMODE_NEAREST);
	}

	if (!SDL_UpdateTexture(mpMaskTexture, nullptr, mMaskBuffer.data(),
			dstW * 4))
	{
		LOG_ERROR("SDL_GPU", "Screen mask upload failed: %s", SDL_GetError());
		return false;
	}

	mMaskDstW = dstW;
	mMaskDstH = dstH;
	mMaskSrcW = srcW;
	mMaskDstX = dstX;
	mMaskDstY = dstY;
	mbMaskDirty = false;
	return true;
}

bool DisplayBackendSDLGPU::ApplyScreenMask(SDL_Texture *target) {
	if (SupportsHDR()) {
		ShaderConstants constants {};
		constants.passParams0[0] = 11.0f;
		// Avoid SDL's SDR-white scaling of this linear multiplier. The mask
		// stays anchored to physical pixels with the same intensity as SDR.
		return RenderPass(mpMaskTexture, target, constants, SDL_BLENDMODE_MOD,
			false);
	}

	if (!target || !mpMaskTexture || !SDL_SetRenderTarget(mpRenderer, target))
		return false;

	SDL_SetRenderScale(mpRenderer, 1.0f, 1.0f);
	SDL_SetGPURenderState(mpRenderer, nullptr);
	SDL_SetTextureBlendMode(mpMaskTexture, SDL_BLENDMODE_MOD);
	const bool result = SDL_RenderTexture(mpRenderer, mpMaskTexture,
		nullptr, nullptr);
	SDL_SetTextureBlendMode(mpMaskTexture, SDL_BLENDMODE_NONE);
	SDL_SetRenderTarget(mpRenderer, nullptr);
	SDL_SetRenderScale(mpRenderer, mRenderScaleX, mRenderScaleY);
	return result;
}

SDL_Texture *DisplayBackendSDLGPU::RenderBloom(SDL_Texture *source, int srcW,
	int srcH, int dstW, int dstH, int baseSourceW, bool sourceSigned)
{
	if (mBloomW != dstW || mBloomH != dstH) {
		if (mpBloomLinear) SDL_DestroyTexture(mpBloomLinear);
		if (mpBloomCombined) SDL_DestroyTexture(mpBloomCombined);
		if (mpBloomOutput) SDL_DestroyTexture(mpBloomOutput);
		mpBloomLinear = nullptr;
		mpBloomCombined = nullptr;
		mpBloomOutput = nullptr;
		mBloomW = 0;
		mBloomH = 0;
	}
	if (!EnsureTarget(mpBloomLinear, mBloomW, mBloomH, dstW, dstH)
		|| !EnsureTarget(mpBloomCombined, mBloomW, mBloomH, dstW, dstH)
		|| !EnsureTarget(mpBloomOutput, mBloomW, mBloomH, dstW, dstH,
			SupportsHDR() ? SDL_PIXELFORMAT_RGBA64_FLOAT
			: SDL_PIXELFORMAT_RGBA8888))
		return nullptr;

	ShaderConstants constants = BuildShaderConstants((float)dstW, (float)dstH,
		srcW, srcH);
	constants.passParams0[0] = 2.0f;
	constants.effectParams2[3] = sourceSigned ? 1.0f : 0.0f;
	if (!RenderPass(source, mpBloomLinear, constants, SDL_BLENDMODE_NONE, true))
		return nullptr;

	SDL_Texture *previous = mpBloomLinear;
	int previousW = dstW;
	int previousH = dstH;
	for (int i = 0; i < kBloomLevels; ++i) {
		const int levelW = std::max(1, previousW / 2);
		const int levelH = std::max(1, previousH / 2);
		if (!EnsureTarget(mpBloomPyramid[i], mBloomPyramidW[i],
			mBloomPyramidH[i], levelW, levelH))
			return nullptr;
		constants = {};
		constants.passParams0[0] = 3.0f;
		constants.passParams0[1] = 1.0f / previousW;
		constants.passParams0[2] = 1.0f / previousH;
		if (!RenderPass(previous, mpBloomPyramid[i], constants,
			SDL_BLENDMODE_NONE, true))
			return nullptr;
		previous = mpBloomPyramid[i];
		previousW = levelW;
		previousH = levelH;
	}

	VDDBloomV2ControlParams controls {};
	controls.mbRenderLinear = false;
	controls.mbEnableSoftClamp = true;
	controls.mBaseRadius = (float)dstW / std::max(baseSourceW, 1);
	controls.mAdjustRadius = mScreenFX.mBloomRadius;
	controls.mDirectIntensity = mScreenFX.mBloomDirectIntensity;
	controls.mIndirectIntensity = mScreenFX.mBloomIndirectIntensity;
	const VDDBloomV2RenderParams params = VDDComputeBloomV2Parameters(controls);

	const SDL_BlendMode upBlend = SDL_ComposeCustomBlendMode(
		SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_SRC_ALPHA,
		SDL_BLENDOPERATION_ADD, SDL_BLENDFACTOR_ONE,
		SDL_BLENDFACTOR_SRC_ALPHA, SDL_BLENDOPERATION_ADD);
	for (int i = kBloomLevels - 2; i >= 0; --i) {
		const int sourceLevel = i + 1;
		const auto& blend = params.mPassBlendFactors[kBloomLevels - 2 - i];
		constants = {};
		constants.passParams0[0] = 4.0f;
		constants.passParams0[1] = 1.0f / mBloomPyramidW[sourceLevel];
		constants.passParams0[2] = 1.0f / mBloomPyramidH[sourceLevel];
		constants.passParams0[3] = blend.x;
		constants.passParams1[0] = blend.y;
		constants.passParams1[1] = (float)mBloomPyramidW[sourceLevel];
		constants.passParams1[2] = (float)mBloomPyramidH[sourceLevel];
		if (!RenderPass(mpBloomPyramid[sourceLevel], mpBloomPyramid[i],
			constants, upBlend, false))
			return nullptr;
	}

	constants = {};
	constants.passParams0[0] = 5.0f;
	constants.passParams1[3] = params.mBaseUVStepScale / dstW;
	constants.colorFlags[3] = params.mBaseUVStepScale / dstH;
	constants.bloomBaseWeights[0] = params.mBaseWeights.x;
	constants.bloomBaseWeights[1] = params.mBaseWeights.y;
	constants.bloomBaseWeights[2] = params.mBaseWeights.z;
	if (!RenderPass(mpBloomLinear, mpBloomCombined, constants,
		SDL_BLENDMODE_NONE, true))
		return nullptr;

	const SDL_BlendMode addBlend = SDL_ComposeCustomBlendMode(
		SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD,
		SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD);
	constants = {};
	constants.passParams0[0] = 6.0f;
	constants.passParams0[1] = 1.0f / mBloomPyramidW[0];
	constants.passParams0[2] = 1.0f / mBloomPyramidH[0];
	constants.passParams0[3] = params.mPassBlendFactors[5].x;
	constants.passParams1[1] = (float)mBloomPyramidW[0];
	constants.passParams1[2] = (float)mBloomPyramidH[0];
	if (!RenderPass(mpBloomPyramid[0], mpBloomCombined, constants,
		addBlend, false))
		return nullptr;

	constants = {};
	constants.passParams0[0] = 7.0f;
	constants.bloomShoulder[0] = params.mShoulder.x;
	constants.bloomShoulder[1] = params.mShoulder.y;
	constants.bloomShoulder[2] = params.mShoulder.z;
	constants.bloomShoulder[3] = params.mShoulder.w;
	constants.bloomThresholds[0] = params.mThresholds.x;
	constants.bloomThresholds[1] = params.mThresholds.y;
	constants.bloomThresholds[2] = params.mThresholds.z;
	constants.bloomThresholds[3] = params.mThresholds.w;
	if (!RenderPass(mpBloomCombined, mpBloomOutput, constants,
		SDL_BLENDMODE_NONE, true))
		return nullptr;

	return mpBloomOutput;
}

void DisplayBackendSDLGPU::DestroyIntermediateTargets() {
	SDL_Texture **textures[] = {
		&mpPALTexture, &mpFilterTexture, &mpBloomLinear,
		&mpBloomCombined, &mpBloomOutput, &mpScreenFXLinear
	};
	for (SDL_Texture **texture : textures) {
		if (*texture) {
			SDL_DestroyTexture(*texture);
			*texture = nullptr;
		}
	}
	for (SDL_Texture *&texture : mpBloomPyramid) {
		if (texture) {
			SDL_DestroyTexture(texture);
			texture = nullptr;
		}
	}
}

void DisplayBackendSDLGPU::RenderFrameClipped(float dstX, float dstY, float dstW,
	float dstH, int srcW, int srcH, float clipX, float clipY, float clipW,
	float clipH)
{
	if (!mpTexture || dstW <= 0.0f || dstH <= 0.0f || clipW <= 0.0f
		|| clipH <= 0.0f)
		return;

	const float left = std::max(dstX, clipX);
	const float top = std::max(dstY, clipY);
	const float right = std::min(dstX + dstW, clipX + clipW);
	const float bottom = std::min(dstY + dstH, clipY + clipH);
	if (right <= left || bottom <= top)
		return;

	const SDL_Rect clipRect {
		(int)std::floor(left), (int)std::floor(top),
		(int)std::ceil(right) - (int)std::floor(left),
		(int)std::ceil(bottom) - (int)std::floor(top)
	};
	SDL_Rect oldClip {};
	const bool hadClip = SDL_RenderClipEnabled(mpRenderer);
	if (hadClip)
		SDL_GetRenderClipRect(mpRenderer, &oldClip);
	SDL_SetRenderClipRect(mpRenderer, &clipRect);
	RenderFrame(dstX, dstY, dstW, dstH, srcW, srcH);
	SDL_SetRenderClipRect(mpRenderer, hadClip ? &oldClip : nullptr);
}

void DisplayBackendSDLGPU::Present() {
	if (!SDL_RenderPresent(mpRenderer))
		LOG_ERROR("SDL_GPU", "Present failed: %s", SDL_GetError());
}

bool DisplayBackendSDLGPU::ReadPixels(void *dst, int dstPitch, int x, int y,
	int w, int h)
{
	const SDL_Rect rect { x, y, w, h };
	SDL_Surface *surface = SDL_RenderReadPixels(mpRenderer, &rect);
	if (!surface)
		return false;

	// SDL returns the actual render-target format, including half floats for
	// HDR. Screenshots remain conventional SDR XRGB8888 images.
	SDL_Surface *converted = SDL_ConvertSurfaceAndColorspace(surface,
		SDL_PIXELFORMAT_XRGB8888, nullptr, SDL_COLORSPACE_SRGB, 0);
	SDL_DestroySurface(surface);
	if (!converted)
		return false;
	surface = converted;
	const int copyW = std::min(w, surface->w) * 4;
	const int copyH = std::min(h, surface->h);
	for (int row = 0; row < copyH; ++row) {
		memcpy((uint8 *)dst + row * dstPitch,
			(const uint8 *)surface->pixels + row * surface->pitch, copyW);
	}
	SDL_DestroySurface(surface);
	return true;
}

void DisplayBackendSDLGPU::OnResize(int pixelW, int pixelH) {
	// SDL_Renderer coordinates are physical pixels unless an explicit scale
	// is installed. Altirra's layout and ImGui both use logical window
	// coordinates, so map them to the backing store (including fractional
	// desktop scaling and Retina) in one place.
	int logicalW = 0;
	int logicalH = 0;
	SDL_GetWindowSize(mpWindow, &logicalW, &logicalH);
	mRenderScaleX = logicalW > 0 && pixelW > 0
		? (float)pixelW / logicalW : 1.0f;
	mRenderScaleY = logicalH > 0 && pixelH > 0
		? (float)pixelH / logicalH : 1.0f;
	if (mOutputW != pixelW || mOutputH != pixelH)
		mbMaskDirty = true;
	mOutputW = pixelW;
	mOutputH = pixelH;
	SDL_SetRenderScale(mpRenderer, mRenderScaleX, mRenderScaleY);
}

void DisplayBackendSDLGPU::UpdateScreenFX(
	const VDVideoDisplayScreenFXInfo& info)
{
	if (!(mScreenFX == info))
		mbMaskDirty = true;
	mScreenFX = info;
}

void DisplayBackendSDLGPU::SetFilterMode(int mode) {
	mFilterMode = mode;
	if (!mpTexture)
		return;

	// Point uses nearest directly. Bicubic also requires nearest because its
	// shader performs all taps explicitly; bilinear sampling there would blur
	// every tap a second time. Sharp bilinear deliberately uses linear sampling.
	const SDL_ScaleMode scaleMode = mode == kATDisplayFilterMode_Point
		|| mode == kATDisplayFilterMode_Bicubic
			? SDL_SCALEMODE_NEAREST : SDL_SCALEMODE_LINEAR;
	SDL_SetTextureScaleMode(mpTexture, scaleMode);
}

void DisplayBackendSDLGPU::SetFilterSharpness(float sharpness) {
	mFilterSharpness = sharpness;
}

void DisplayBackendSDLGPU::DestroyCustomEffect() {
	for (auto& pass : mCustomPasses) {
		if (pass.target) SDL_DestroyTexture(pass.target);
		if (pass.state) SDL_DestroyGPURenderState(pass.state);
		if (pass.shader) SDL_ReleaseGPUShader(mpDevice, pass.shader);
	}
	mCustomPasses.clear();
}

void DisplayBackendSDLGPU::SyncCustomEffect(float viewportW, float viewportH,
	int sourceW, int sourceH) {
	if (mCustomRevision == ATDisplayCustomEffectGetRevision()) return;
	mCustomRevision = ATDisplayCustomEffectGetRevision();
	mCustomLoadValid = false;
	ATDisplayCustomEffectPackage package;
	std::string error;
	if (!ATDisplayCustomEffectLoad(ATDisplayCustomEffectGetPath().c_str(), package, error)) {
		ATDisplayCustomEffectReportResult(error);
		return;
	}
	std::vector<CustomPass> next;
	for (auto& spec : package.passes) {
		SDL_GPUShaderCreateInfo info {};
		info.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
		info.num_samplers = 1;
		info.num_uniform_buffers = 1;
		info.entrypoint = spec.entrypoint.c_str();
		const auto formats = SDL_GetGPUShaderFormats(mpDevice);
		if ((formats & SDL_GPU_SHADERFORMAT_DXIL) && !spec.dxil.empty()) {
			info.format = SDL_GPU_SHADERFORMAT_DXIL;
			info.code = spec.dxil.data(); info.code_size = spec.dxil.size();
		} else if ((formats & SDL_GPU_SHADERFORMAT_SPIRV) && !spec.spirv.empty()) {
			info.format = SDL_GPU_SHADERFORMAT_SPIRV;
			info.code = spec.spirv.data(); info.code_size = spec.spirv.size();
		} else if ((formats & SDL_GPU_SHADERFORMAT_MSL) && !spec.msl.empty()) {
			info.format = SDL_GPU_SHADERFORMAT_MSL;
			info.code = (const uint8_t *)spec.msl.c_str(); info.code_size = spec.msl.size() + 1;
			info.entrypoint = spec.mslEntrypoint.c_str();
		} else {
			error = "The effect lacks a shader representation supported by the current SDL_GPU driver.";
			break;
		}
		CustomPass pass;
		pass.shader = SDL_CreateGPUShader(mpDevice, &info);
		if (!pass.shader) { error = std::string("Effect shader creation failed: ") + SDL_GetError(); break; }
		SDL_GPURenderStateCreateInfo state {};
		state.fragment_shader = pass.shader;
		pass.state = SDL_CreateGPURenderState(mpRenderer, &state);
		if (!pass.state) {
			error = std::string("Effect render state creation failed: ") + SDL_GetError();
			SDL_ReleaseGPUShader(mpDevice, pass.shader);
			break;
		}
		pass.spec = std::move(spec);
		int w = 0, h = 0;
		if (!ATDisplayCustomEffectSize(pass.spec, sourceW, sourceH, viewportW, viewportH, w, h)
			|| !EnsureTarget(pass.target, pass.w, pass.h, w, h,
				SupportsHDR() ? SDL_PIXELFORMAT_RGBA64_FLOAT : SDL_PIXELFORMAT_RGBA8888))
		{
			error = "Effect target exceeds limits or cannot be allocated (8192 per axis, 16 million pixels).";
			SDL_DestroyGPURenderState(pass.state);
			SDL_ReleaseGPUShader(mpDevice, pass.shader);
			break;
		}
		sourceW = w;
		sourceH = h;
		next.push_back(std::move(pass));
	}
	if (!error.empty()) {
		for (auto& pass : next) {
			if (pass.target) SDL_DestroyTexture(pass.target);
			SDL_DestroyGPURenderState(pass.state);
			SDL_ReleaseGPUShader(mpDevice, pass.shader);
		}
		ATDisplayCustomEffectReportResult(error);
		return;
	}
	DestroyCustomEffect();
	mCustomPasses = std::move(next);
	mCustomFrame = 0;
	mCustomLoadValid = true;
	mCustomRuntimeFailed = false;
	ATDisplayCustomEffectReportResult("");
}

SDL_Texture *DisplayBackendSDLGPU::RenderCustomEffect(float viewportW,
	float viewportH, int& w, int& h)
{
	if (mCustomPasses.empty()) return nullptr;
	SDL_Texture *source = mpTexture;
	SDL_Rect oldClip {};
	const bool clipped = SDL_RenderClipEnabled(mpRenderer);
	if (clipped) SDL_GetRenderClipRect(mpRenderer, &oldClip);
	bool success = true;
	for (auto& pass : mCustomPasses) {
		int tw = 0, th = 0;
		if (!ATDisplayCustomEffectSize(pass.spec, w, h, viewportW, viewportH, tw, th)
			|| !EnsureTarget(pass.target, pass.w, pass.h, tw, th,
				SupportsHDR() ? SDL_PIXELFORMAT_RGBA64_FLOAT : SDL_PIXELFORMAT_RGBA8888))
		{
			if (mCustomLoadValid) {
				ATDisplayCustomEffectReportResult("Effect target exceeds limits or cannot be allocated (8192 per axis, 16 million pixels).");
				mCustomRuntimeFailed = true;
			}
			success = false;
			break;
		}
		SDL_ScaleMode oldScale;
		SDL_GetTextureScaleMode(source, &oldScale);
		SDL_SetTextureScaleMode(source, pass.spec.linear ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST);
		const ATDisplayCustomEffectUniforms uniforms {
			{(float)w, (float)h, 1.0f / w, 1.0f / h},
			{(float)tw, (float)th, 1.0f / tw, 1.0f / th},
			{(float)mCustomFrame, (float)(SDL_GetTicks() * 0.001), SupportsHDR() && source == mpTexture ? 1.0f : 0.0f, mScreenFX.mbSignedRGBEncoding ? 1.0f : 0.0f}
		};
		const bool targetOK = SDL_SetRenderTarget(mpRenderer, pass.target);
		SDL_SetRenderScale(mpRenderer, 1, 1);
		SDL_SetRenderClipRect(mpRenderer, nullptr);
		SDL_SetRenderColorScale(mpRenderer, 1);
		SDL_SetTextureBlendMode(source, SDL_BLENDMODE_NONE);
		success = targetOK && SDL_SetGPURenderStateFragmentUniforms(pass.state, 0, &uniforms, sizeof(uniforms))
			&& SDL_SetGPURenderState(mpRenderer, pass.state)
			&& SDL_RenderTexture(mpRenderer, source, nullptr, nullptr);
		SDL_SetGPURenderState(mpRenderer, nullptr);
		SDL_SetTextureScaleMode(source, oldScale);
		if (!success) {
			if (mCustomLoadValid) {
				ATDisplayCustomEffectReportResult(std::string("Effect rendering failed: ") + SDL_GetError());
				mCustomRuntimeFailed = true;
			}
			break;
		}
		source = pass.target;
		w = tw;
		h = th;
	}
	if (success && mCustomRuntimeFailed && mCustomLoadValid) {
		ATDisplayCustomEffectReportResult("");
		mCustomRuntimeFailed = false;
	}
	++mCustomFrame;
	SDL_SetRenderTarget(mpRenderer, nullptr);
	SDL_SetRenderScale(mpRenderer, mRenderScaleX, mRenderScaleY);
	SDL_SetRenderColorScale(mpRenderer, GetOutputLevels().mUIScale);
	SDL_SetRenderClipRect(mpRenderer, clipped ? &oldClip : nullptr);
	if (success) SDL_SetTextureScaleMode(source,
		mFilterMode == kATDisplayFilterMode_Point || mFilterMode == kATDisplayFilterMode_Bicubic
			? SDL_SCALEMODE_NEAREST : SDL_SCALEMODE_LINEAR);
	return success ? source : nullptr;
}
