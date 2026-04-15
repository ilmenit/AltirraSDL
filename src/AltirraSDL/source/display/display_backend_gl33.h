//	AltirraSDL - OpenGL display backend
//	GPU-accelerated post-processing: screen effects, bloom, bicubic filtering.
//
//	Handles either of two profiles (selected at context-creation time):
//	  - Desktop OpenGL 3.3 Core     (Windows, Linux, macOS)
//	  - OpenGL ES 3.0               (Android, iOS)
//
//	The class body is profile-agnostic: shader sources are shared and
//	the profile preamble (#version / precision) is prepended by
//	GLCompileShader based on GLGetActiveProfile().  XRGB8888 uploads
//	go through GLCreateXRGB8888Texture / GLUploadXRGB8888, which apply
//	R↔B texture swizzle on GLES to compensate for GLES' lack of GL_BGRA.

#pragma once

#include <SDL3/SDL.h>
#include <unordered_map>
#include <vector>
#include "display_backend.h"
#include "gl_funcs.h"
#include "gl_helpers.h"
#include <vd2/VDDisplay/display.h>
#include <vd2/VDDisplay/displaytypes.h>
#include "display_librashader.h"

// Feature flag bits for screen effects shader program cache.
enum ScreenFXFeature : uint32_t {
	kSFX_Sharp          = 1 << 0,
	kSFX_Scanlines      = 1 << 1,
	kSFX_Gamma          = 1 << 2,
	kSFX_ColorCorrect   = 1 << 3,
	kSFX_CC_SRGB        = 1 << 4,
	kSFX_DotMask        = 1 << 5,
	kSFX_Distortion     = 1 << 6,
};

// Uniform locations for a compiled screen effects program.
struct ScreenFXUniforms {
	GLint uSourceTex = -1;
	GLint uGammaTex = -1;
	GLint uScanlineTex = -1;
	GLint uMaskTex = -1;
	GLint uSharpnessInfo = -1;
	GLint uScanlineInfo = -1;
	GLint uColorCorrectM0 = -1;
	GLint uColorCorrectM1 = -1;
	GLint uColorCorrectM2 = -1;
	GLint uDistortionScales = -1;
	GLint uImageUVSize = -1;
};

struct ScreenFXProgram {
	GLuint program = 0;
	ScreenFXUniforms uniforms;
};

class DisplayBackendGL final : public IDisplayBackend {
public:
	DisplayBackendGL(SDL_Window *window, SDL_GLContext glContext);
	~DisplayBackendGL() override;

	DisplayBackendType GetType() const override { return DisplayBackendType::OpenGL; }

	void UploadFrame(const void *pixels, int width, int height, int pitch) override;
	void BeginFrame() override;
	void RenderFrame(float dstX, float dstY, float dstW, float dstH,
		int srcW, int srcH) override;
	void Present() override;
	bool ReadPixels(void *dst, int dstPitch, int x, int y, int w, int h) override;
	void OnResize(int w, int h) override;

	bool SupportsScreenFX() const override { return true; }
	void UpdateScreenFX(const VDVideoDisplayScreenFXInfo &info) override;
	void SetFilterMode(int mode) override;
	void SetFilterSharpness(float sharpness) override;

	bool SupportsExternalShaders() const override { return mLibrashader.IsAvailable(); }
	bool LoadShaderPreset(const char *path) override;
	void ClearShaderPreset() override;
	const char *GetShaderPresetPath() const override;
	bool HasShaderPreset() const override { return mLibrashader.HasPreset(); }
	std::vector<LibrashaderParam> GetShaderParameters() const override { return mLibrashader.GetParameters(); }
	bool SetShaderParameter(const char *name, float value) override { return mLibrashader.SetParameter(name, value); }

	SDL_Renderer *GetSDLRenderer() override { return nullptr; }
	SDL_Window *GetWindow() override { return mpWindow; }

	bool HasTexture() const override { return mEmuTexture != 0; }
	int GetTextureWidth() const override { return mTexW; }
	int GetTextureHeight() const override { return mTexH; }
	void *GetImGuiTextureID() const override { return (void *)(intptr_t)mEmuTexture; }

	SDL_GLContext GetGLContext() const { return mGLContext; }

private:
	// Inner rendering (built-in effects).  Called by RenderFrame directly,
	// or redirected into an FBO when librashader is active.
	void RenderFrameInner(float dstX, float dstY, float dstW, float dstH,
		int srcW, int srcH);

	// Get or compile a screen FX shader program for the given feature flags.
	const ScreenFXProgram &GetScreenFXProgram(uint32_t features);

	// Compile the passthrough program (no effects, just sampling).
	void CompilePassthroughProgram();


	// Regenerate lookup textures when screen FX params change.
	void UpdateLookupTextures();

	// Render screen effects (called from RenderFrame when effects are active).
	void RenderScreenFX(float dstX, float dstY, float dstW, float dstH,
		int srcW, int srcH);

	// Render bloom V2 passes.  sourceTex is the input texture (may be
	// the emulator texture or the PAL FBO output).
	void RenderBloomV2(int srcW, int srcH, GLuint sourceTex);

	// Allocate/resize bloom FBO pyramid.
	void EnsureBloomPyramid(int baseW, int baseH);

	// Render bicubic filter (2-pass separable).  sourceTex is the input
	// texture (may be the emulator texture or the PAL FBO output).
	void RenderBicubic(int srcW, int srcH, int dstW, int dstH, GLuint sourceTex);

	// Draw a fullscreen triangle with the given program, binding the VAO.
	void DrawFullscreen(GLuint program);

	SDL_Window *mpWindow;
	SDL_GLContext mGLContext;
	int mWinW = 0;
	int mWinH = 0;

	// Emulator frame texture
	GLuint mEmuTexture = 0;
	int mTexW = 0;
	int mTexH = 0;

	// Empty VAO for fullscreen triangle draws
	GLuint mEmptyVAO = 0;

	// Passthrough program (no effects)
	GLuint mPassthroughProgram = 0;
	GLint mPassthroughLoc_SourceTex = -1;

	// Screen effects shader cache
	std::unordered_map<uint32_t, ScreenFXProgram> mScreenFXCache;

	// Current screen FX state — initialised to no-op defaults so that
	// the renderer works correctly even before UpdateScreenFX() is called.
	// The struct default has mGamma=0 and a zero colour-correction matrix,
	// which would route through the screen-FX shader and produce a black frame.
	VDVideoDisplayScreenFXInfo mScreenFX = []() {
		VDVideoDisplayScreenFXInfo fx {};
		fx.mGamma = 1.0f;
		return fx;
	}();
	bool mScreenFXDirty = true;
	int mFilterMode = 0;  // ATDisplayFilterMode
	float mFilterSharpness = 0.0f;

	// Lookup textures
	GLuint mGammaTexture = 0;
	GLuint mScanlineTexture = 0;
	GLuint mMaskTexture = 0;
	int mScanlineTexH = 0;
	int mMaskTexW = 0;
	int mMaskTexH = 0;

	// Bicubic filter textures (regenerated when src/dst dimensions change)
	GLuint mBicubicFilterTexH = 0;  // horizontal pass filter weights
	GLuint mBicubicFilterTexV = 0;  // vertical pass filter weights
	int mBicubicFilterSrcW = 0;
	int mBicubicFilterSrcH = 0;
	int mBicubicFilterDstW = 0;
	int mBicubicFilterDstH = 0;
	GLuint mBicubicProgram = 0;
	GLint mBicubicLoc_SourceTex = -1;
	GLint mBicubicLoc_FilterTex = -1;
	GLint mBicubicLoc_SrcSize = -1;
	GLint mBicubicLoc_FilterCoord = -1;
	GLRenderTarget mBicubicFBO;     // intermediate FBO (horizontal pass output)
	GLRenderTarget mBicubicFBO2;    // final FBO (vertical pass output)

	// Bloom V2 pipeline
	static constexpr int kBloomLevels = 6;
	GLRenderTarget mBloomPyramid[kBloomLevels];
	GLRenderTarget mBloomLinearFBO;  // linear-light copy of source for bloom
	GLRenderTarget mBloomOutputFBO;  // final composited bloom output (sRGB)
	int mBloomBaseW = 0;
	int mBloomBaseH = 0;

	GLuint mBloomGammaProgram = 0;
	GLuint mBloomDownProgram = 0;
	GLuint mBloomUpProgram = 0;
	GLuint mBloomFinalProgram = 0;

	// Bloom uniform locations
	GLint mBloomGammaLoc_SourceTex = -1;
	GLint mBloomDownLoc_SourceTex = -1;
	GLint mBloomDownLoc_UVStep = -1;
	GLint mBloomUpLoc_SourceTex = -1;
	GLint mBloomUpLoc_UVStep = -1;
	GLint mBloomUpLoc_TexSize = -1;
	GLint mBloomUpLoc_BlendFactors = -1;
	GLint mBloomFinalLoc_SourceTex = -1;
	GLint mBloomFinalLoc_BaseTex = -1;
	GLint mBloomFinalLoc_UVStep = -1;
	GLint mBloomFinalLoc_TexSize = -1;
	GLint mBloomFinalLoc_BlendFactors = -1;
	GLint mBloomFinalLoc_ShoulderCurve = -1;
	GLint mBloomFinalLoc_Thresholds = -1;
	GLint mBloomFinalLoc_BaseUVStep = -1;
	GLint mBloomFinalLoc_BaseWeights = -1;

	// PAL artifacting
	GLuint mPALProgram = 0;
	GLint mPALLoc_SourceTex = -1;
	GLint mPALLoc_ChromaOffset = -1;
	GLint mPALLoc_SignedRGB = -1;
	GLRenderTarget mPALFBO;

	// Pixel buffer for lookup texture generation
	std::vector<uint32_t> mLookupBuffer;

	// librashader integration
	LibrashaderRuntime mLibrashader;
	uint32_t mFrameCounter = 0;
	GLRenderTarget mLibrashaderFBO;     // built-in effects → librashader input
	GLRenderTarget mLibrashaderOutFBO;  // librashader output → screen
	GLuint mRenderTargetFBO = 0;        // restore target for sub-passes (0=screen)
};
