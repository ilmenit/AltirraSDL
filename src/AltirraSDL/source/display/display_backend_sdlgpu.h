//	AltirraSDL - SDL_GPU display backend

#pragma once

#include <SDL3/SDL.h>
#include <vd2/VDDisplay/display.h>
#include <vector>
#include "display_backend.h"
#include "display_custom_effect.h"
#include "display_hdr.h"

class DisplayBackendSDLGPU final : public IDisplayBackend {
public:
	static SDL_Renderer *CreateRenderer(SDL_Window *window);
	static DisplayBackendSDLGPU *Create(SDL_Window *window, SDL_Renderer *renderer);
	~DisplayBackendSDLGPU() override;

	DisplayBackendType GetType() const override { return DisplayBackendType::SDLGPU; }

	void UploadFrame(const void *pixels, int width, int height, int pitch) override;
	void BeginFrame() override;
	void RenderFrame(float dstX, float dstY, float dstW, float dstH,
		int srcW, int srcH) override;
	void RenderFrameClipped(float dstX, float dstY, float dstW, float dstH,
		int srcW, int srcH, float clipX, float clipY, float clipW, float clipH) override;
	void Present() override;
	bool ReadPixels(void *dst, int dstPitch, int x, int y, int w, int h) override;
	void OnResize(int w, int h) override;

	bool SupportsScreenFX() const override { return true; }
	bool SupportsHDR() const override;
	bool IsHDREnabled() const override;
	void UpdateScreenFX(const VDVideoDisplayScreenFXInfo &info) override;
	void SetFilterMode(int mode) override;
	void SetFilterSharpness(float sharpness) override;

	SDL_Renderer *GetSDLRenderer() override { return mpRenderer; }
	SDL_Window *GetWindow() override { return mpWindow; }

	bool HasTexture() const override { return mpTexture != nullptr; }
	int GetTextureWidth() const override { return mTexW; }
	int GetTextureHeight() const override { return mTexH; }
	void *GetImGuiTextureID() const override { return (void *)mpTexture; }

private:
	SDL_Texture *RenderCustomEffect(float viewportW, float viewportH, int& w, int& h);
	struct CustomPass {
		ATDisplayCustomEffectPass spec;
		SDL_GPUShader *shader = nullptr;
		SDL_GPURenderState *state = nullptr;
		SDL_Texture *target = nullptr;
		int w = 0, h = 0;
	};
	std::vector<CustomPass> mCustomPasses;
	uint64_t mCustomRevision = 0, mCustomFrame = 0;
	bool mCustomLoadValid = true, mCustomRuntimeFailed = false;
	void SyncCustomEffect(float viewportW, float viewportH, int sourceW, int sourceH);
	void DestroyCustomEffect();

	DisplayBackendSDLGPU(SDL_Window *window, SDL_Renderer *renderer,
		SDL_GPUDevice *device, SDL_GPUShader *shader, SDL_GPURenderState *state);

	struct alignas(16) ShaderConstants {
		float sourceDestSize[4];
		float filterParams[4];
		float effectParams1[4];
		float effectParams2[4];
		float bloomParams[4];
		float maskParams[4];
		float colorFlags[4];
		float colorMatrix0[4];
		float colorMatrix1[4];
		float colorMatrix2[4];
		float passParams0[4];
		float passParams1[4];
		float bloomShoulder[4];
		float bloomThresholds[4];
		float bloomBaseWeights[4];
		float hdrParams[4];
	};

	ShaderConstants BuildShaderConstants(float dstW, float dstH,
		int srcW, int srcH) const;
	ATHDROutputLevels GetOutputLevels() const;
	void SetOutputConstants(ShaderConstants& constants, SDL_Texture *source) const;
	bool ApplyShaderConstants(const ShaderConstants& constants);
	bool EnsureTarget(SDL_Texture *&texture, int& currentW, int& currentH,
		int width, int height,
		SDL_PixelFormat format = SDL_PIXELFORMAT_RGBA64_FLOAT);
	bool RenderPass(SDL_Texture *source, SDL_Texture *target,
		const ShaderConstants& constants, SDL_BlendMode blendMode,
		bool clearTarget);
	SDL_Texture *RenderBloom(SDL_Texture *source, int srcW, int srcH,
		int dstW, int dstH, int baseSourceW, bool sourceSigned);
	bool EnsureScreenMask(float dstX, float dstY, int dstW, int dstH,
		int srcW);
	bool ApplyScreenMask(SDL_Texture *target);
	void DestroyIntermediateTargets();

	SDL_Window *mpWindow = nullptr;
	SDL_Renderer *mpRenderer = nullptr;
	SDL_GPUDevice *mpDevice = nullptr;
	SDL_GPUShader *mpShader = nullptr;
	SDL_GPURenderState *mpRenderState = nullptr;
	SDL_Texture *mpTexture = nullptr;
	SDL_Texture *mpPALTexture = nullptr;
	SDL_Texture *mpFilterTexture = nullptr;
	SDL_Texture *mpBloomLinear = nullptr;
	SDL_Texture *mpBloomCombined = nullptr;
	SDL_Texture *mpBloomOutput = nullptr;
	SDL_Texture *mpScreenFXLinear = nullptr;
	SDL_Texture *mpMaskTexture = nullptr;
	static constexpr int kBloomLevels = 6;
	SDL_Texture *mpBloomPyramid[kBloomLevels] {};
	int mTexW = 0;
	int mTexH = 0;
	int mOutputW = 0;
	int mOutputH = 0;
	float mRenderScaleX = 1.0f;
	float mRenderScaleY = 1.0f;
	int mPALW = 0;
	int mPALH = 0;
	int mFilterW = 0;
	int mFilterH = 0;
	int mBloomW = 0;
	int mBloomH = 0;
	int mScreenFXW = 0;
	int mScreenFXH = 0;
	int mMaskW = 0;
	int mMaskH = 0;
	int mMaskDstW = 0;
	int mMaskDstH = 0;
	int mMaskSrcW = 0;
	float mMaskDstX = 0.0f;
	float mMaskDstY = 0.0f;
	bool mbMaskDirty = true;
	std::vector<uint32> mMaskBuffer;
	int mBloomPyramidW[kBloomLevels] {};
	int mBloomPyramidH[kBloomLevels] {};
	int mFilterMode = 0;
	float mFilterSharpness = 0.0f;
	VDVideoDisplayScreenFXInfo mScreenFX = []() {
		VDVideoDisplayScreenFXInfo fx {};
		fx.mGamma = 1.0f;
		return fx;
	}();
};
