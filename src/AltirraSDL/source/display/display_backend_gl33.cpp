//	AltirraSDL - OpenGL display backend implementation
//	GPU-accelerated post-processing: screen effects, bloom V2, bicubic, PAL.
//	One class — two profiles: Desktop GL 3.3 Core and OpenGL ES 3.0.

#include <stdafx.h>
#include "display_backend_gl33.h"
#include "uiaccessors.h"
#include "uitypes.h"

#include <vd2/VDDisplay/internal/screenfx.h>
#include <vd2/VDDisplay/internal/bloom.h>
#include <bicubic.h>

#include <stdio.h>
#include <string.h>
#include <cmath>
#include <algorithm>
#include <string>

// Embedded GLSL shader sources
#include "shaders_common.inl"
#include "shaders_screenfx.inl"
#include "shaders_bicubic.inl"
#include "shaders_bloom.inl"
#include "logging.h"

// ============================================================================
// Construction / destruction
// ============================================================================

DisplayBackendGL::DisplayBackendGL(SDL_Window *window, SDL_GLContext glContext)
	: mpWindow(window)
	, mGLContext(glContext)
{
	SDL_GL_MakeCurrent(window, glContext);

	LOG_INFO("GL", "Renderer: %s", glGetString(GL_RENDERER));
	LOG_INFO("GL", "Version:  %s", glGetString(GL_VERSION));

	// Create empty VAO for fullscreen triangle draws (GL 3.3 core requires a bound VAO)
	glGenVertexArrays(1, &mEmptyVAO);

	// Compile base programs
	CompilePassthroughProgram();

	// Get initial window size
	SDL_GetWindowSizeInPixels(window, &mWinW, &mWinH);

	// Set default GL state
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_STENCIL_TEST);
	glDisable(GL_CULL_FACE);

	// Try to initialize librashader (no-op if shared library not installed)
	mLibrashader.Init();
}

bool DisplayBackendGL::LoadShaderPreset(const char *path) {
	return mLibrashader.LoadPreset(path);
}

void DisplayBackendGL::ClearShaderPreset() {
	mLibrashader.ClearPreset();
}

const char *DisplayBackendGL::GetShaderPresetPath() const {
	return mLibrashader.GetPresetPath().c_str();
}

DisplayBackendGL::~DisplayBackendGL() {
	SDL_GL_MakeCurrent(mpWindow, mGLContext);

	mLibrashader.Shutdown();

	// Clean up textures
	if (mEmuTexture) glDeleteTextures(1, &mEmuTexture);
	if (mGammaTexture) glDeleteTextures(1, &mGammaTexture);
	if (mScanlineTexture) glDeleteTextures(1, &mScanlineTexture);
	if (mMaskTexture) glDeleteTextures(1, &mMaskTexture);
	if (mBicubicFilterTexH) glDeleteTextures(1, &mBicubicFilterTexH);
	if (mBicubicFilterTexV) glDeleteTextures(1, &mBicubicFilterTexV);

	// Clean up FBOs
	mLibrashaderFBO.Destroy();
	mLibrashaderOutFBO.Destroy();
	mBicubicFBO.Destroy();
	mBicubicFBO2.Destroy();
	mPALFBO.Destroy();
	mBloomLinearFBO.Destroy();
	mBloomOutputFBO.Destroy();
	for (int i = 0; i < kBloomLevels; i++)
		mBloomPyramid[i].Destroy();

	// Clean up programs
	if (mPassthroughProgram) glDeleteProgram(mPassthroughProgram);
	if (mBicubicProgram) glDeleteProgram(mBicubicProgram);
	if (mBloomGammaProgram) glDeleteProgram(mBloomGammaProgram);
	if (mBloomDownProgram) glDeleteProgram(mBloomDownProgram);
	if (mBloomUpProgram) glDeleteProgram(mBloomUpProgram);
	if (mBloomFinalProgram) glDeleteProgram(mBloomFinalProgram);
	if (mPALProgram) glDeleteProgram(mPALProgram);

	for (auto &kv : mScreenFXCache) {
		if (kv.second.program) glDeleteProgram(kv.second.program);
	}

	if (mEmptyVAO) glDeleteVertexArrays(1, &mEmptyVAO);

	SDL_GL_DestroyContext(mGLContext);
}

// ============================================================================
// Program compilation
// ============================================================================

void DisplayBackendGL::CompilePassthroughProgram() {
	mPassthroughProgram = GLCreateProgram(kGLSL_FullscreenTriangleVS, kGLSL_PassthroughFS);
	if (mPassthroughProgram) {
		mPassthroughLoc_SourceTex = glGetUniformLocation(mPassthroughProgram, "uSourceTex");
	}
}


const ScreenFXProgram &DisplayBackendGL::GetScreenFXProgram(uint32_t features) {
	auto it = mScreenFXCache.find(features);
	if (it != mScreenFXCache.end())
		return it->second;

	// Build #define preamble
	std::string preamble;
	if (features & kSFX_Sharp)        preamble += "#define FEAT_SHARP\n";
	if (features & kSFX_Scanlines)    preamble += "#define FEAT_SCANLINES\n";
	if (features & kSFX_Gamma)        preamble += "#define FEAT_GAMMA\n";
	if (features & kSFX_ColorCorrect) preamble += "#define FEAT_COLOR_CORRECT\n";
	if (features & kSFX_CC_SRGB)      preamble += "#define FEAT_CC_SRGB\n";
	if (features & kSFX_DotMask)      preamble += "#define FEAT_DOT_MASK\n";
	if (features & kSFX_Distortion)   preamble += "#define FEAT_DISTORTION\n";

	// Profile preamble (#version + precision) is injected by
	// GLCompileShaderMulti; we only pass the feature-define preamble and
	// the shader body here.  Order of source strings is:
	//   [profile-preamble] [feature-defines] [screenfx body]
	const char *fsSources[2] = { preamble.c_str(), kGLSL_ScreenFX_FS };

	GLuint vs = GLCompileShader(GL_VERTEX_SHADER, kGLSL_FullscreenTriangleVS);
	GLuint fs = GLCompileShaderMulti(GL_FRAGMENT_SHADER, fsSources, 2);

	ScreenFXProgram prog;
	if (vs && fs) {
		prog.program = GLLinkProgram(vs, fs);
	} else {
		if (vs) glDeleteShader(vs);
		if (fs) glDeleteShader(fs);
	}

	if (prog.program) {
		auto &u = prog.uniforms;
		u.uSourceTex = glGetUniformLocation(prog.program, "uSourceTex");
		u.uGammaTex = glGetUniformLocation(prog.program, "uGammaTex");
		u.uScanlineTex = glGetUniformLocation(prog.program, "uScanlineTex");
		u.uMaskTex = glGetUniformLocation(prog.program, "uMaskTex");
		u.uSharpnessInfo = glGetUniformLocation(prog.program, "uSharpnessInfo");
		u.uScanlineInfo = glGetUniformLocation(prog.program, "uScanlineInfo");
		u.uColorCorrectM0 = glGetUniformLocation(prog.program, "uColorCorrectM0");
		u.uColorCorrectM1 = glGetUniformLocation(prog.program, "uColorCorrectM1");
		u.uColorCorrectM2 = glGetUniformLocation(prog.program, "uColorCorrectM2");
		u.uDistortionScales = glGetUniformLocation(prog.program, "uDistortionScales");
		u.uImageUVSize = glGetUniformLocation(prog.program, "uImageUVSize");

		// Bind texture units
		glUseProgram(prog.program);
		if (u.uSourceTex >= 0)   glUniform1i(u.uSourceTex, 0);
		if (u.uGammaTex >= 0)    glUniform1i(u.uGammaTex, 1);
		if (u.uScanlineTex >= 0) glUniform1i(u.uScanlineTex, 2);
		if (u.uMaskTex >= 0)     glUniform1i(u.uMaskTex, 3);
		glUseProgram(0);
	}

	auto result = mScreenFXCache.emplace(features, prog);
	return result.first->second;
}

// ============================================================================
// Frame upload
// ============================================================================

void DisplayBackendGL::UploadFrame(const void *pixels, int width, int height, int pitch) {
	if (!pixels || width <= 0 || height <= 0)
		return;

	// Recreate texture if dimensions changed
	if (!mEmuTexture || mTexW != width || mTexH != height) {
		if (mEmuTexture)
			glDeleteTextures(1, &mEmuTexture);

		// GLCreateXRGB8888Texture picks the right pixel format per GL
		// profile (BGRA/REV on desktop, RGBA/UBYTE+swizzle on GLES).
		mEmuTexture = GLCreateXRGB8888Texture(width, height, false, nullptr);
		mTexW = width;
		mTexH = height;

		// Apply current filter mode
		SetFilterMode(mFilterMode);

		// Mark screen FX dirty to regenerate lookup textures for new dimensions
		mScreenFXDirty = true;
	}

	// Upload pixel data
	glBindTexture(GL_TEXTURE_2D, mEmuTexture);
	GLUploadXRGB8888(width, height, pixels, pitch);
}

// ============================================================================
// Frame rendering
// ============================================================================

void DisplayBackendGL::BeginFrame() {
	SDL_GetWindowSizeInPixels(mpWindow, &mWinW, &mWinH);
	glViewport(0, 0, mWinW, mWinH);
	glClear(GL_COLOR_BUFFER_BIT);
}

void DisplayBackendGL::RenderFrame(float dstX, float dstY, float dstW, float dstH,
	int srcW, int srcH)
{
	if (!mEmuTexture || srcW <= 0 || srcH <= 0)
		return;

	// When librashader is active, redirect built-in rendering into an
	// intermediate FBO so librashader can process the result on top.
	bool useLibrashader = mLibrashader.HasPreset();
	int vpW = (int)dstW;
	int vpH = (int)dstH;

	if (useLibrashader) {
		// Allocate/resize the intermediate FBO at destination size
		if (mLibrashaderFBO.width != vpW || mLibrashaderFBO.height != vpH) {
			mLibrashaderFBO.Destroy();
			mLibrashaderFBO.Create(vpW, vpH, GL_RGBA8);
		}

		// Redirect all subsequent rendering into the FBO.
		// We translate the viewport so built-in effects think they're
		// rendering at (0,0) in the FBO rather than at (dstX,dstY).
		mLibrashaderFBO.Bind();
		glClear(GL_COLOR_BUFFER_BIT);
		// Override the window dimensions so viewport calculations inside
		// RenderScreenFX use the FBO size, not the window size.
		int savedWinW = mWinW, savedWinH = mWinH;
		mWinW = vpW;
		mWinH = vpH;

		// Set the restore target so sub-passes (PAL, bloom, bicubic)
		// return to this FBO instead of FBO 0 (the screen).
		mRenderTargetFBO = mLibrashaderFBO.fbo;

		// Render built-in effects into the FBO at (0,0,vpW,vpH)
		RenderFrameInner(0.0f, 0.0f, dstW, dstH, srcW, srcH);

		// Restore
		mRenderTargetFBO = 0;
		mWinW = savedWinW;
		mWinH = savedWinH;
		glBindFramebuffer(GL_FRAMEBUFFER, 0);

		// Now apply librashader on top: FBO texture → screen
		++mFrameCounter;

		// Allocate a second FBO for librashader output (input and output
		// can't share the same FBO since librashader reads from a texture).
		if (mLibrashaderOutFBO.width != vpW || mLibrashaderOutFBO.height != vpH) {
			mLibrashaderOutFBO.Destroy();
			mLibrashaderOutFBO.Create(vpW, vpH, GL_RGBA8);
		}

		// Our built-in shaders render top-down (Y=0 at top, SDL/ImGui
		// convention) into mLibrashaderFBO, but librashader expects
		// standard OpenGL orientation (Y=0 at bottom).  Blit with a Y
		// flip into mLibrashaderOutFBO, then use that as librashader
		// input and mLibrashaderFBO as output.
		glBindFramebuffer(GL_READ_FRAMEBUFFER, mLibrashaderFBO.fbo);
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, mLibrashaderOutFBO.fbo);
		glBlitFramebuffer(
			0, vpH, vpW, 0,        // src: flip Y
			0, 0, vpW, vpH,        // dst: standard GL orientation
			GL_COLOR_BUFFER_BIT, GL_NEAREST);
		glBindFramebuffer(GL_FRAMEBUFFER, 0);

		mLibrashader.Apply(mLibrashaderOutFBO.tex, mLibrashaderFBO.tex,
			vpW, vpH, vpW, vpH, mFrameCounter);

		// Restore GL state after librashader — some presets may enable
		// sRGB framebuffer writes which would corrupt ImGui rendering.
		glDisable(GL_BLEND);
		glDisable(GL_DEPTH_TEST);
		glDisable(GL_STENCIL_TEST);
		glDisable(GL_CULL_FACE);
		glDisable(GL_SCISSOR_TEST);
		GLSetFramebufferSRGB(false);
		glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

		// Blit librashader output to the screen.  librashader output is
		// in standard GL orientation (Y=0 at bottom); the screen uses
		// SDL convention (Y=0 at top).  Flip src Y to compensate.
		int scrX = (int)dstX;
		int scrY = savedWinH - (int)(dstY + dstH);
		glBindFramebuffer(GL_READ_FRAMEBUFFER, mLibrashaderFBO.fbo);
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
		glBlitFramebuffer(
			0, vpH, vpW, 0,                          // src: flip Y (bottom-up → top-down)
			scrX, scrY, scrX + vpW, scrY + vpH,      // dst: screen position
			GL_COLOR_BUFFER_BIT, GL_NEAREST);
		glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
		glViewport(0, 0, savedWinW, savedWinH);
		return;
	}

	RenderFrameInner(dstX, dstY, dstW, dstH, srcW, srcH);
}

void DisplayBackendGL::RenderFrameInner(float dstX, float dstY, float dstW, float dstH,
	int srcW, int srcH)
{

	// Determine if any screen effects are active
	bool hasEffects = false;
	if (mScreenFX.mScanlineIntensity > 0.0f) hasEffects = true;
	if (mScreenFX.mGamma != 1.0f) hasEffects = true;
	if (mScreenFX.mDistortionX > 0.0f) hasEffects = true;
	if (mScreenFX.mScreenMaskParams.mType != VDDScreenMaskType::None) hasEffects = true;
	if (mScreenFX.mbBloomEnabled) hasEffects = true;

	// Check color correction matrix (any non-zero element means active)
	for (int i = 0; i < 3 && !hasEffects; i++)
		for (int j = 0; j < 3; j++)
			if (mScreenFX.mColorCorrectionMatrix[i][j] != 0.0f)
				{ hasEffects = true; break; }

	// Check PAL blending
	if (mScreenFX.mPALBlendingOffset != 0.0f) hasEffects = true;

	bool useBicubic = (mFilterMode == kATDisplayFilterMode_Bicubic);
	bool useSharpBilinear = (mFilterMode == kATDisplayFilterMode_SharpBilinear);

	// Log whenever the rendering path changes
	static int s_lastPath = -1;  // -1=uninit, 0=passthrough, 1=screenFX
	int curPath = (hasEffects || useBicubic || useSharpBilinear) ? 1 : 0;
	if (curPath != s_lastPath) {
		LOG_INFO("GL", "Render path: %s (effects=%d bicubic=%d sharp=%d gamma=%.2f scanline=%.1f bloom=%d distort=%.2f mask=%d pal=%.2f fbo=%u)", curPath ? "ScreenFX" : "Passthrough",
			hasEffects, useBicubic, useSharpBilinear,
			mScreenFX.mGamma, mScreenFX.mScanlineIntensity,
			mScreenFX.mbBloomEnabled, mScreenFX.mDistortionX,
			(int)mScreenFX.mScreenMaskParams.mType,
			mScreenFX.mPALBlendingOffset, mRenderTargetFBO);
		s_lastPath = curPath;
	}

	if (hasEffects || useBicubic || useSharpBilinear) {
		RenderScreenFX(dstX, dstY, dstW, dstH, srcW, srcH);
		return;
	}

	// Simple passthrough rendering — use glViewport for destination rect
	// and the fullscreen triangle to fill it.
	// GL viewport Y is bottom-up, SDL rect Y is top-down.
	int vpX = (int)dstX;
	int vpY = mWinH - (int)(dstY + dstH);
	int vpW = (int)dstW;
	int vpH = (int)dstH;
	glViewport(vpX, vpY, vpW, vpH);

	glBindVertexArray(mEmptyVAO);
	glUseProgram(mPassthroughProgram);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, mEmuTexture);
	glUniform1i(mPassthroughLoc_SourceTex, 0);

	GLDrawFullscreenTriangle();

	glUseProgram(0);
	glBindVertexArray(0);

	// Restore full-window viewport for ImGui
	glViewport(0, 0, mWinW, mWinH);
}

void DisplayBackendGL::Present() {
	SDL_GL_SwapWindow(mpWindow);
}

bool DisplayBackendGL::ReadPixels(void *dst, int dstPitch, int x, int y, int w, int h) {
	if (!dst || w <= 0 || h <= 0)
		return false;

	glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);

	// GL reads bottom-up; we need top-down. Read into tight temp buffer then flip.
	glPixelStorei(GL_PACK_ROW_LENGTH, 0);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);

	std::vector<uint8_t> temp(w * 4 * h);
	glReadPixels(x, mWinH - y - h, w, h, GL_RGBA, GL_UNSIGNED_BYTE, temp.data());

	// Flip vertically into destination
	uint8_t *dstPtr = (uint8_t *)dst;
	for (int row = 0; row < h; row++) {
		memcpy(dstPtr + row * dstPitch,
			temp.data() + (h - 1 - row) * w * 4,
			w * 4);
	}
	return true;
}

void DisplayBackendGL::OnResize(int w, int h) {
	mWinW = w;
	mWinH = h;
	// Lookup textures (scanline mask, etc.) need regeneration on resize
	mScreenFXDirty = true;
}

// ============================================================================
// Filter mode
// ============================================================================

void DisplayBackendGL::SetFilterMode(int mode) {
	mFilterMode = mode;

	if (!mEmuTexture)
		return;

	glBindTexture(GL_TEXTURE_2D, mEmuTexture);

	GLenum filter = GL_LINEAR;
	if (mode == kATDisplayFilterMode_Point)
		filter = GL_NEAREST;
	// Bicubic uses its own shader-based filtering with explicit samples,
	// so the base texture should be nearest to avoid double-filtering.
	// Sharp bilinear deliberately uses hardware bilinear as part of its
	// algorithm — the shader adjusts UVs to control the blend.
	if (mode == kATDisplayFilterMode_Bicubic)
		filter = GL_NEAREST;

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
}

void DisplayBackendGL::SetFilterSharpness(float sharpness) {
	mFilterSharpness = sharpness;
}

// ============================================================================
// Screen effects
// ============================================================================

void DisplayBackendGL::UpdateScreenFX(const VDVideoDisplayScreenFXInfo &info) {
	if (!(mScreenFX == info)) {
		mScreenFX = info;
		mScreenFXDirty = true;
	}
}

void DisplayBackendGL::UpdateLookupTextures() {
	if (!mScreenFXDirty || mTexW <= 0 || mTexH <= 0)
		return;
	mScreenFXDirty = false;

	// Gamma ramp texture (256x1) — needed for gamma correction AND color correction.
	// A gamma of 1.0 means identity (no correction needed).
	bool needsGamma = (mScreenFX.mGamma != 1.0f);
	bool hasColorCorrect = false;
	for (int i = 0; i < 3; i++)
		for (int j = 0; j < 3; j++)
			if (mScreenFX.mColorCorrectionMatrix[i][j] != 0.0f)
				{ hasColorCorrect = true; needsGamma = true; }

	// Also need gamma when screen mask is active (mask is applied in linear space)
	bool hasMask = (mScreenFX.mScreenMaskParams.mType != VDDScreenMaskType::None);
	if (hasMask)
		needsGamma = true;

	if (needsGamma) {
		mLookupBuffer.resize(256);
		// D3D9 conditionally sets useInputConversion: true when color correction
		// is active or screen mask is enabled, false for gamma-only correction.
		const bool useInputConversion = hasColorCorrect || hasMask;

		VDDisplayCreateGammaRamp(mLookupBuffer.data(), 256,
			useInputConversion, mScreenFX.mOutputGamma, mScreenFX.mGamma);

		if (!mGammaTexture)
			mGammaTexture = GLCreateTexture2D(256, 1, GL_RGBA8, GL_RGBA,
				GL_UNSIGNED_BYTE, mLookupBuffer.data(), true);
		else {
			glBindTexture(GL_TEXTURE_2D, mGammaTexture);
			glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 1,
				GL_RGBA, GL_UNSIGNED_BYTE, mLookupBuffer.data());
		}
	}

	// Scanline mask texture (1 x dstH)
	if (mScreenFX.mScanlineIntensity > 0.0f && mWinH > 0) {
		int texH = mWinH;
		mLookupBuffer.resize(texH);
		VDDisplayCreateScanlineMaskTexture(mLookupBuffer.data(),
			sizeof(uint32),  // pitch: 1 pixel wide, 4 bytes per pixel
			mTexH, texH, texH,
			mScreenFX.mScanlineIntensity, false);

		if (!mScanlineTexture || mScanlineTexH != texH) {
			if (mScanlineTexture) glDeleteTextures(1, &mScanlineTexture);
			mScanlineTexture = GLCreateTexture2D(1, texH, GL_RGBA8, GL_RGBA,
				GL_UNSIGNED_BYTE, mLookupBuffer.data(), true);
			mScanlineTexH = texH;
		} else {
			glBindTexture(GL_TEXTURE_2D, mScanlineTexture);
			glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 1, texH,
				GL_RGBA, GL_UNSIGNED_BYTE, mLookupBuffer.data());
		}
	}

	// Screen mask texture
	const auto &maskParams = mScreenFX.mScreenMaskParams;
	if (maskParams.mType != VDDScreenMaskType::None && mWinW > 0 && mWinH > 0) {
		int maskW = mWinW;
		int maskH = 1;

		switch (maskParams.mType) {
			case VDDScreenMaskType::ApertureGrille: {
				maskH = 1;
				mLookupBuffer.resize(maskW);
				memset(mLookupBuffer.data(), 0, maskW * 4);
				VDDisplayApertureGrilleParams agp(maskParams, (float)mWinW, (float)mTexW);
				VDDisplayCreateApertureGrilleTexture(mLookupBuffer.data(), maskW, 0.0f, agp);
				break;
			}
			case VDDScreenMaskType::SlotMask: {
				maskH = mWinH;
				mLookupBuffer.resize((size_t)maskW * maskH);
				memset(mLookupBuffer.data(), 0, mLookupBuffer.size() * 4);
				VDDisplaySlotMaskParams smp(maskParams, (float)mWinW, (float)mTexW);
				VDDisplayCreateSlotMaskTexture(mLookupBuffer.data(), maskW * 4,
					maskW, maskH, 0.0f, 0.0f, (float)mWinW, (float)mWinH, smp);
				break;
			}
			case VDDScreenMaskType::DotTriad: {
				maskH = mWinH;
				mLookupBuffer.resize((size_t)maskW * maskH);
				memset(mLookupBuffer.data(), 0, mLookupBuffer.size() * 4);
				VDDisplayTriadDotMaskParams tdp(maskParams, (float)mWinW, (float)mTexW);
				VDDisplayCreateTriadDotMaskTexture(mLookupBuffer.data(), maskW * 4,
					maskW, maskH, 0.0f, 0.0f, (float)mWinW, (float)mWinH, tdp);
				break;
			}
			default:
				break;
		}

		if (maskW > 0 && maskH > 0) {
			// The mask generation functions (VDDisplayCreate*Texture) produce
			// uint32 pixels in D3D XRGB8888 convention (byte order B,G,R,X in
			// memory).  GLCreateXRGB8888Texture / GLUploadXRGB8888 pick the
			// correct pixel transfer format per profile; D3D9 uses POINT
			// filtering for the mask (pass linear=false) to preserve sharp
			// phosphor boundaries.
			if (!mMaskTexture || mMaskTexW != maskW || mMaskTexH != maskH) {
				if (mMaskTexture) glDeleteTextures(1, &mMaskTexture);
				mMaskTexture = GLCreateXRGB8888Texture(maskW, maskH, false,
					mLookupBuffer.data());
				mMaskTexW = maskW;
				mMaskTexH = maskH;
			} else {
				glBindTexture(GL_TEXTURE_2D, mMaskTexture);
				GLUploadXRGB8888(maskW, maskH, mLookupBuffer.data(), 0);
			}
		}
	}
}

void DisplayBackendGL::RenderScreenFX(float dstX, float dstY, float dstW, float dstH,
	int srcW, int srcH)
{
	// Regenerate lookup textures if needed
	UpdateLookupTextures();

	// Determine which features are active
	uint32_t features = 0;

	bool hasColorCorrect = false;
	for (int i = 0; i < 3 && !hasColorCorrect; i++)
		for (int j = 0; j < 3; j++)
			if (mScreenFX.mColorCorrectionMatrix[i][j] != 0.0f)
				{ hasColorCorrect = true; break; }

	bool useScreenMask = (mScreenFX.mScreenMaskParams.mType != VDDScreenMaskType::None && mMaskTexture);
	bool useGammaCorrection = (mScreenFX.mGamma != 1.0f);

	if (mFilterMode == kATDisplayFilterMode_SharpBilinear)
		features |= kSFX_Sharp;
	if (mScreenFX.mScanlineIntensity > 0.0f && mScanlineTexture)
		features |= kSFX_Scanlines;

	// In the GLSL shader, FEAT_DOT_MASK is nested inside FEAT_COLOR_CORRECT.
	// When a screen mask is active, we must force CC+Gamma on (using an identity
	// matrix if no actual CC is requested). This matches D3D9 which always uses
	// the mask_cc technique when a mask is active.
	if (useScreenMask) {
		features |= kSFX_DotMask | kSFX_ColorCorrect | kSFX_Gamma;
		if (mScreenFX.mOutputGamma <= 0.0f)
			features |= kSFX_CC_SRGB;
	}

	if (hasColorCorrect) {
		features |= kSFX_ColorCorrect;
		if (mScreenFX.mOutputGamma <= 0.0f)
			features |= kSFX_CC_SRGB;
	}

	// Gamma correction is needed for explicit gamma adjustment, color correction
	// (which requires linearization → matrix → re-gamma), or mask (applied in linear space).
	if ((useGammaCorrection || hasColorCorrect) && mGammaTexture)
		features |= kSFX_Gamma;

	if (mScreenFX.mDistortionX > 0.0f)
		features |= kSFX_Distortion;

	// The source texture for the final render pass.  Each pre-pass
	// (PAL, bicubic, bloom) produces an intermediate that replaces the source.
	GLuint sourceTex = mEmuTexture;

	// PAL artifacting pre-pass: blend chroma across adjacent scanlines.
	// This must run before bloom and screen FX since those operate on the
	// PAL-blended result.  The pass renders at source resolution into mPALFBO,
	// matching D3D9's approach of rendering into a source-sized RT.
	if (mScreenFX.mPALBlendingOffset != 0.0f) {
		// Compile PAL program on first use
		if (!mPALProgram) {
			// Use NoFlip VS: the PAL pre-pass writes into mPALFBO which is later
// sampled by the screen-FX/passthrough pass using the Y-flipping VS.
// To make that downstream flip produce the correct orientation,
// mPALFBO must be stored in GL's native bottom-up layout (matching
// how mEmuTexture's row 0 is the top scanline after the consumer flip).
mPALProgram = GLCreateProgram(kGLSL_FullscreenTriangleVS_NoFlip, kGLSL_PALArtifacting_FS);
			if (mPALProgram) {
				mPALLoc_SourceTex = glGetUniformLocation(mPALProgram, "uSourceTex");
				mPALLoc_ChromaOffset = glGetUniformLocation(mPALProgram, "uChromaOffset");
				mPALLoc_SignedRGB = glGetUniformLocation(mPALProgram, "uSignedRGB");
			}
		}

		if (mPALProgram) {
			// Create/resize PAL FBO to match emulator texture dimensions
			if (mPALFBO.width != mTexW || mPALFBO.height != mTexH) {
				mPALFBO.Destroy();
				mPALFBO.Create(mTexW, mTexH, GL_RGBA8);
			}

			mPALFBO.Bind();
			glViewport(0, 0, mTexW, mTexH);

			glBindVertexArray(mEmptyVAO);
			glUseProgram(mPALProgram);

			// The D3D9 technique uses bilinear filtering for the PAL pass source.
			// Force GL_LINEAR regardless of the current filter mode setting.
			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, sourceTex);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

			glUniform1i(mPALLoc_SourceTex, 0);

			// Chroma offset: mPALBlendingOffset is in scanlines (-1 or -2).
			// Convert to UV space by dividing by source texture height.
			// The PAL pre-pass uses the NoFlip VS, so vUV.y now increases
			// downward in the source (top scanline at vUV.y=0).  The original
			// offset was authored against the Y-flipped UV convention, so
			// negate it to preserve the same logical blending direction.
			glUniform2f(mPALLoc_ChromaOffset,
				0.0f, -mScreenFX.mPALBlendingOffset / (float)mTexH);

			// Signed RGB encoding flag — when the frame uses extended-range
			// palette encoding, the shader must convert to normal [0,1] range.
			glUniform1i(mPALLoc_SignedRGB, mScreenFX.mbSignedRGBEncoding ? 1 : 0);

			GLDrawFullscreenTriangle();

			glUseProgram(0);
			glBindVertexArray(0);
			glBindFramebuffer(GL_FRAMEBUFFER, mRenderTargetFBO);

			// Restore the filter mode on the emulator texture
			// Only Point and Bicubic use NEAREST; SharpBilinear uses LINEAR.
			GLenum filter = (mFilterMode == kATDisplayFilterMode_Point
				|| mFilterMode == kATDisplayFilterMode_Bicubic)
				? GL_NEAREST : GL_LINEAR;
			glBindTexture(GL_TEXTURE_2D, sourceTex);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);

			// All subsequent passes read from the PAL-blended result
			sourceTex = mPALFBO.tex;
		}
	}

	// Handle bicubic (2-pass separable filter → output FBO)
	if (mFilterMode == kATDisplayFilterMode_Bicubic) {
		RenderBicubic(srcW, srcH, (int)dstW, (int)dstH, sourceTex);
		if (mBicubicFBO2.tex)
			sourceTex = mBicubicFBO2.tex;
	}

	// Handle bloom (multi-pass pyramid → composited output in mBloomOutputFBO)
	if (mScreenFX.mbBloomEnabled) {
		RenderBloomV2(srcW, srcH, sourceTex);
		if (mBloomOutputFBO.tex)
			sourceTex = mBloomOutputFBO.tex;
	}

	// Set viewport to destination rect (GL Y is bottom-up, SDL Y is top-down)
	int vpX = (int)dstX;
	int vpY = mWinH - (int)(dstY + dstH);
	int vpW = (int)dstW;
	int vpH = (int)dstH;
	glViewport(vpX, vpY, vpW, vpH);

	// If no features are active but we got here (e.g., just bicubic), use passthrough
	if (features == 0) {
		glBindVertexArray(mEmptyVAO);
		glUseProgram(mPassthroughProgram);

		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, sourceTex);
		glUniform1i(mPassthroughLoc_SourceTex, 0);

		GLDrawFullscreenTriangle();
		glUseProgram(0);
		glBindVertexArray(0);
		glViewport(0, 0, mWinW, mWinH);
		return;
	}

	// Get or compile the screen FX program for this feature set
	const ScreenFXProgram &prog = GetScreenFXProgram(features);
	if (!prog.program) {
		glViewport(0, 0, mWinW, mWinH);
		return;
	}

	glBindVertexArray(mEmptyVAO);
	glUseProgram(prog.program);

	// Bind source texture (may be original emu texture, bicubic output, or bloom output)
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, sourceTex);

	if ((features & kSFX_Gamma) && mGammaTexture) {
		glActiveTexture(GL_TEXTURE0 + 1);
		glBindTexture(GL_TEXTURE_2D, mGammaTexture);
	}

	if ((features & kSFX_Scanlines) && mScanlineTexture) {
		glActiveTexture(GL_TEXTURE0 + 2);
		glBindTexture(GL_TEXTURE_2D, mScanlineTexture);
	}

	if ((features & kSFX_DotMask) && mMaskTexture) {
		glActiveTexture(GL_TEXTURE0 + 3);
		glBindTexture(GL_TEXTURE_2D, mMaskTexture);
	}

	glActiveTexture(GL_TEXTURE0);

	// Set uniforms
	const auto &u = prog.uniforms;

	if ((features & kSFX_Sharp) && u.uSharpnessInfo >= 0) {
		// Sharp bilinear parameters — match the Windows lookup table.
		// mFilterSharpness is an integer -2..+2 from the UI setting.
		static const float kFactors[5] = { 1.259f, 1.587f, 2.0f, 2.520f, 3.175f };
		int idx = std::max(0, std::min(4, (int)mFilterSharpness + 2));
		float factor = kFactors[idx];
		float sharpnessX = std::max(1.0f, factor * 0.5f);
		float sharpnessY = std::max(1.0f, factor);
		float uvScaleX = 1.0f / (float)srcW;
		float uvScaleY = 1.0f / (float)srcH;
		glUniform4f(u.uSharpnessInfo, sharpnessX, sharpnessY, uvScaleX, uvScaleY);
	}

	if ((features & (kSFX_Scanlines | kSFX_DotMask)) && u.uScanlineInfo >= 0) {
		// Scanline/mask UV transform: map viewport UV [0,1] to window UV.
		// In D3D9 this is always set in the VS constants; here we set it
		// whenever scanlines or mask are active (the shader declares the
		// uniform under either feature).
		float scaleX = dstW / mWinW;
		float scaleY = dstH / mWinH;
		float offsetX = dstX / mWinW;
		float offsetY = dstY / mWinH;
		glUniform4f(u.uScanlineInfo, scaleX, scaleY, offsetX, offsetY);
	}

	if ((features & kSFX_ColorCorrect) && u.uColorCorrectM0 >= 0) {
		// Build the color correction matrix.  When CC was not explicitly
		// requested but is being forced on by masking, use an identity matrix.
		float cm[3][3];
		if (hasColorCorrect) {
			// Transpose from row-major mColorCorrectionMatrix to column-major uniforms
			for (int i = 0; i < 3; i++)
				for (int j = 0; j < 3; j++)
					cm[i][j] = mScreenFX.mColorCorrectionMatrix[i][j];
		} else {
			// Identity matrix — CC forced on by screen mask
			memset(cm, 0, sizeof(cm));
			cm[0][0] = 1.0f;
			cm[1][1] = 1.0f;
			cm[2][2] = 1.0f;
		}

		// Apply mask intensity compensation: scale the matrix to counteract
		// the brightness reduction caused by the screen mask pattern.
		if (useScreenMask && mScreenFX.mScreenMaskParams.mbScreenMaskIntensityCompensation) {
			const float scale = 1.0f / mScreenFX.mScreenMaskParams.GetMaskIntensityScale();
			for (int i = 0; i < 3; i++)
				for (int j = 0; j < 3; j++)
					cm[i][j] *= scale;
		}

		// The HLSL code packs the matrix columns with a pre-bias in .w
		// M0.w = 0 (pre-bias addend), M1.w = 1 (pre-bias scale)
		glUniform4f(u.uColorCorrectM0, cm[0][0], cm[1][0], cm[2][0], 0.0f);
		glUniform4f(u.uColorCorrectM1, cm[0][1], cm[1][1], cm[2][1], 1.0f);
		glUniform3f(u.uColorCorrectM2, cm[0][2], cm[1][2], cm[2][2]);
	}

	if ((features & kSFX_Distortion) && u.uDistortionScales >= 0) {
		VDDisplayDistortionMapping dm;
		dm.Init(mScreenFX.mDistortionX, mScreenFX.mDistortionYRatio,
			dstW / dstH);
		glUniform3f(u.uDistortionScales, dm.mScaleX, dm.mScaleY, dm.mSqRadius);
	}

	GLDrawFullscreenTriangle();

	// Clean up bound textures on auxiliary units
	if ((features & kSFX_DotMask) && mMaskTexture) {
		glActiveTexture(GL_TEXTURE0 + 3);
		glBindTexture(GL_TEXTURE_2D, 0);
	}
	if ((features & kSFX_Scanlines) && mScanlineTexture) {
		glActiveTexture(GL_TEXTURE0 + 2);
		glBindTexture(GL_TEXTURE_2D, 0);
	}
	if ((features & kSFX_Gamma) && mGammaTexture) {
		glActiveTexture(GL_TEXTURE0 + 1);
		glBindTexture(GL_TEXTURE_2D, 0);
	}
	glActiveTexture(GL_TEXTURE0);

	glUseProgram(0);
	glBindVertexArray(0);

	// Restore full-window viewport for ImGui
	glViewport(0, 0, mWinW, mWinH);
}

// ============================================================================
// Bloom V2
// ============================================================================

void DisplayBackendGL::EnsureBloomPyramid(int baseW, int baseH) {
	if (mBloomBaseW == baseW && mBloomBaseH == baseH)
		return;

	mBloomBaseW = baseW;
	mBloomBaseH = baseH;

	// Create linear-light FBO at full resolution
	mBloomLinearFBO.Create(baseW, baseH, GL_RGBA16F);

	// Create output FBO for final composition (sRGB output)
	mBloomOutputFBO.Create(baseW, baseH, GL_RGBA8);

	// Create pyramid at decreasing resolutions
	int w = baseW / 2;
	int h = baseH / 2;
	for (int i = 0; i < kBloomLevels; i++) {
		if (w < 1) w = 1;
		if (h < 1) h = 1;
		mBloomPyramid[i].Create(w, h, GL_RGBA16F);
		w /= 2;
		h /= 2;
	}

	// Compile bloom programs if not already done
	if (!mBloomGammaProgram) {
		mBloomGammaProgram = GLCreateProgram(kGLSL_FullscreenTriangleVS, kGLSL_BloomGamma_FS);
		if (mBloomGammaProgram)
			mBloomGammaLoc_SourceTex = glGetUniformLocation(mBloomGammaProgram, "uSourceTex");
	}

	if (!mBloomDownProgram) {
		mBloomDownProgram = GLCreateProgram(kGLSL_FullscreenTriangleVS, kGLSL_BloomDown_FS);
		if (mBloomDownProgram) {
			mBloomDownLoc_SourceTex = glGetUniformLocation(mBloomDownProgram, "uSourceTex");
			mBloomDownLoc_UVStep = glGetUniformLocation(mBloomDownProgram, "uUVStep");
		}
	}

	if (!mBloomUpProgram) {
		mBloomUpProgram = GLCreateProgram(kGLSL_FullscreenTriangleVS, kGLSL_BloomUp_FS);
		if (mBloomUpProgram) {
			mBloomUpLoc_SourceTex = glGetUniformLocation(mBloomUpProgram, "uSourceTex");
			mBloomUpLoc_UVStep = glGetUniformLocation(mBloomUpProgram, "uUVStep");
			mBloomUpLoc_TexSize = glGetUniformLocation(mBloomUpProgram, "uTexSize");
			mBloomUpLoc_BlendFactors = glGetUniformLocation(mBloomUpProgram, "uBlendFactors");
		}
	}

	if (!mBloomFinalProgram) {
		mBloomFinalProgram = GLCreateProgram(kGLSL_FullscreenTriangleVS, kGLSL_BloomFinal_FS);
		if (mBloomFinalProgram) {
			mBloomFinalLoc_SourceTex = glGetUniformLocation(mBloomFinalProgram, "uSourceTex");
			mBloomFinalLoc_BaseTex = glGetUniformLocation(mBloomFinalProgram, "uBaseTex");
			mBloomFinalLoc_UVStep = glGetUniformLocation(mBloomFinalProgram, "uUVStep");
			mBloomFinalLoc_TexSize = glGetUniformLocation(mBloomFinalProgram, "uTexSize");
			mBloomFinalLoc_BlendFactors = glGetUniformLocation(mBloomFinalProgram, "uBlendFactors");
			mBloomFinalLoc_ShoulderCurve = glGetUniformLocation(mBloomFinalProgram, "uShoulderCurve");
			mBloomFinalLoc_Thresholds = glGetUniformLocation(mBloomFinalProgram, "uThresholds");
			mBloomFinalLoc_BaseUVStep = glGetUniformLocation(mBloomFinalProgram, "uBaseUVStep");
			mBloomFinalLoc_BaseWeights = glGetUniformLocation(mBloomFinalProgram, "uBaseWeights");

			glUseProgram(mBloomFinalProgram);
			glUniform1i(mBloomFinalLoc_SourceTex, 0);
			glUniform1i(mBloomFinalLoc_BaseTex, 1);
			glUseProgram(0);
		}
	}
}

void DisplayBackendGL::RenderBloomV2(int srcW, int srcH, GLuint sourceTex) {
	EnsureBloomPyramid(mWinW, mWinH);

	if (!mBloomGammaProgram || !mBloomDownProgram || !mBloomUpProgram || !mBloomFinalProgram)
		return;

	// Compute bloom parameters from current settings
	VDDBloomV2ControlParams cp;
	cp.mbRenderLinear = false;
	cp.mbEnableSoftClamp = true;
	// The bloom radius is specified in source pixels; convert to destination
	// pixels by scaling with the destination/source ratio.
	cp.mBaseRadius = (float)mWinW / (float)srcW;
	cp.mAdjustRadius = mScreenFX.mBloomRadius;
	cp.mDirectIntensity = mScreenFX.mBloomDirectIntensity;
	cp.mIndirectIntensity = mScreenFX.mBloomIndirectIntensity;

	VDDBloomV2RenderParams rp = VDDComputeBloomV2Parameters(cp);

	glBindVertexArray(mEmptyVAO);
	glDisable(GL_BLEND);

	// Pass 1: sRGB to linear (reads from sourceTex, which may be the
	// emulator texture or the PAL FBO output)
	mBloomLinearFBO.Bind();
	glUseProgram(mBloomGammaProgram);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, sourceTex);
	glUniform1i(mBloomGammaLoc_SourceTex, 0);
	GLDrawFullscreenTriangle();

	// Pass 2: Downsample pyramid
	glUseProgram(mBloomDownProgram);
	glUniform1i(mBloomDownLoc_SourceTex, 0);

	GLuint prevTex = mBloomLinearFBO.tex;
	int prevW = mBloomLinearFBO.width;
	int prevH = mBloomLinearFBO.height;

	for (int i = 0; i < kBloomLevels; i++) {
		mBloomPyramid[i].Bind();
		glBindTexture(GL_TEXTURE_2D, prevTex);
		// UV step is 1/sourceTexSize — the downsample kernel samples from
		// the source (previous level), not the target (current level).
		glUniform2f(mBloomDownLoc_UVStep,
			1.0f / prevW,
			1.0f / prevH);
		GLDrawFullscreenTriangle();

		prevTex = mBloomPyramid[i].tex;
		prevW = mBloomPyramid[i].width;
		prevH = mBloomPyramid[i].height;
	}

	// Pass 3: Upsample pyramid (from coarsest to finest)
	glUseProgram(mBloomUpProgram);
	glUniform1i(mBloomUpLoc_SourceTex, 0);
	glEnable(GL_BLEND);
	glBlendFunc(GL_ONE, GL_SRC_ALPHA);  // additive with alpha as blend weight

	// D3D9 upsample: level goes from kBloomLevels-2 down to 0,
	// blend factor index is (kBloomLevels - 2 - level), matching D3D9's [4 - level].
	for (int i = kBloomLevels - 2; i >= 0; i--) {
		mBloomPyramid[i].Bind();  // render into the next-finer level
		glBindTexture(GL_TEXTURE_2D, mBloomPyramid[i + 1].tex);  // read from coarser

		int srcTexW = mBloomPyramid[i + 1].width;
		int srcTexH = mBloomPyramid[i + 1].height;
		glUniform2f(mBloomUpLoc_UVStep, 1.0f / srcTexW, 1.0f / srcTexH);
		glUniform2f(mBloomUpLoc_TexSize, (float)srcTexW, (float)srcTexH);
		glUniform2f(mBloomUpLoc_BlendFactors,
			rp.mPassBlendFactors[kBloomLevels - 2 - i].x,
			rp.mPassBlendFactors[kBloomLevels - 2 - i].y);

		GLDrawFullscreenTriangle();
	}

	glDisable(GL_BLEND);

	// Pass 4: Final composition — bloom pyramid + base → sRGB output FBO
	// mBloomPyramid[0] has the accumulated bloom highlights.
	// mBloomLinearFBO has the linear-light base image.
	// Output goes to mBloomOutputFBO (sRGB).
	mBloomOutputFBO.Bind();
	glUseProgram(mBloomFinalProgram);

	// Bind bloom pyramid (finest level) to texture unit 0
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, mBloomPyramid[0].tex);
	glUniform1i(mBloomFinalLoc_SourceTex, 0);

	// Bind base (linear) image to texture unit 1
	glActiveTexture(GL_TEXTURE0 + 1);
	glBindTexture(GL_TEXTURE_2D, mBloomLinearFBO.tex);
	glUniform1i(mBloomFinalLoc_BaseTex, 1);
	glActiveTexture(GL_TEXTURE0);

	// Bloom upsample UV step (for the finest pyramid level)
	int p0w = mBloomPyramid[0].width;
	int p0h = mBloomPyramid[0].height;
	glUniform2f(mBloomFinalLoc_UVStep, 1.0f / p0w, 1.0f / p0h);
	glUniform2f(mBloomFinalLoc_TexSize, (float)p0w, (float)p0h);

	// Blend factors from bloom parameters
	glUniform2f(mBloomFinalLoc_BlendFactors,
		rp.mPassBlendFactors[5].x, rp.mPassBlendFactors[5].y);

	// Shoulder curve and thresholds
	glUniform4f(mBloomFinalLoc_ShoulderCurve,
		rp.mShoulder.x, rp.mShoulder.y, rp.mShoulder.z, rp.mShoulder.w);
	glUniform4f(mBloomFinalLoc_Thresholds,
		rp.mThresholds.x, rp.mThresholds.y, rp.mThresholds.z, rp.mThresholds.w);

	// Base 9-tap filter UV step and weights
	float baseStepX = rp.mBaseUVStepScale / mBloomBaseW;
	float baseStepY = rp.mBaseUVStepScale / mBloomBaseH;
	glUniform2f(mBloomFinalLoc_BaseUVStep, baseStepX, baseStepY);
	glUniform4f(mBloomFinalLoc_BaseWeights,
		rp.mBaseWeights.x, rp.mBaseWeights.y, rp.mBaseWeights.z, rp.mBaseWeights.w);

	GLDrawFullscreenTriangle();

	// Clean up state
	glActiveTexture(GL_TEXTURE0 + 1);
	glBindTexture(GL_TEXTURE_2D, 0);
	glActiveTexture(GL_TEXTURE0);

	glBindFramebuffer(GL_FRAMEBUFFER, mRenderTargetFBO);
	glViewport(0, 0, mWinW, mWinH);
	glUseProgram(0);
	glBindVertexArray(0);
}

// ============================================================================
// Bicubic filter
// ============================================================================

void DisplayBackendGL::RenderBicubic(int srcW, int srcH, int dstW, int dstH, GLuint sourceTex) {
	if (dstW <= 0 || dstH <= 0)
		return;

	// Compile bicubic program if not done
	if (!mBicubicProgram) {
		mBicubicProgram = GLCreateProgram(kGLSL_FullscreenTriangleVS, kGLSL_Bicubic_FS);
		if (mBicubicProgram) {
			mBicubicLoc_SourceTex = glGetUniformLocation(mBicubicProgram, "uSourceTex");
			mBicubicLoc_FilterTex = glGetUniformLocation(mBicubicProgram, "uFilterTex");
			mBicubicLoc_SrcSize = glGetUniformLocation(mBicubicProgram, "uSrcSize");
			mBicubicLoc_FilterCoord = glGetUniformLocation(mBicubicProgram, "uFilterCoord");

			glUseProgram(mBicubicProgram);
			glUniform1i(mBicubicLoc_SourceTex, 0);
			glUniform1i(mBicubicLoc_FilterTex, 1);
			glUseProgram(0);
		}
	}

	if (!mBicubicProgram)
		return;

	// Generate bicubic filter textures (regenerate when dimensions change)
	if (!mBicubicFilterTexH || mBicubicFilterSrcW != srcW || mBicubicFilterDstW != dstW) {
		if (mBicubicFilterTexH) glDeleteTextures(1, &mBicubicFilterTexH);
		mLookupBuffer.resize(dstW);
		VDDisplayCreateBicubicTexture(mLookupBuffer.data(), dstW, srcW);
		mBicubicFilterTexH = GLCreateTexture2D(dstW, 1, GL_RGBA8, GL_RGBA,
			GL_UNSIGNED_BYTE, mLookupBuffer.data(), true);
		mBicubicFilterSrcW = srcW;
		mBicubicFilterDstW = dstW;
	}
	if (!mBicubicFilterTexV || mBicubicFilterSrcH != srcH || mBicubicFilterDstH != dstH) {
		if (mBicubicFilterTexV) glDeleteTextures(1, &mBicubicFilterTexV);
		mLookupBuffer.resize(dstH);
		VDDisplayCreateBicubicTexture(mLookupBuffer.data(), dstH, srcH);
		mBicubicFilterTexV = GLCreateTexture2D(dstH, 1, GL_RGBA8, GL_RGBA,
			GL_UNSIGNED_BYTE, mLookupBuffer.data(), true);
		mBicubicFilterSrcH = srcH;
		mBicubicFilterDstH = dstH;
	}

	// Ensure intermediate FBOs exist.
	// Pass 1 (horizontal): srcW×srcH → dstW×srcH
	// Pass 2 (vertical):   dstW×srcH → dstW×dstH
	if (mBicubicFBO.width != dstW || mBicubicFBO.height != srcH)
		mBicubicFBO.Create(dstW, srcH, GL_RGBA8);
	if (mBicubicFBO2.width != dstW || mBicubicFBO2.height != dstH)
		mBicubicFBO2.Create(dstW, dstH, GL_RGBA8);

	glBindVertexArray(mEmptyVAO);
	glUseProgram(mBicubicProgram);

	// Pass 1: Horizontal — source texture → intermediate FBO
	// Source texture needs LINEAR for the bilinear center tap trick.
	mBicubicFBO.Bind();
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, sourceTex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glActiveTexture(GL_TEXTURE0 + 1);
	glBindTexture(GL_TEXTURE_2D, mBicubicFilterTexH);
	glActiveTexture(GL_TEXTURE0);

	glUniform2f(mBicubicLoc_SrcSize, (float)srcW, (float)srcH);
	glUniform1f(mBicubicLoc_FilterCoord, 0.0f);  // horizontal axis
	GLDrawFullscreenTriangle();

	// Restore source texture to NEAREST (for non-bicubic paths)
	glBindTexture(GL_TEXTURE_2D, sourceTex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

	// Pass 2: Vertical — intermediate FBO → final FBO
	mBicubicFBO2.Bind();
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, mBicubicFBO.tex);
	// mBicubicFBO.tex was created with LINEAR by GLCreateTexture2D
	glActiveTexture(GL_TEXTURE0 + 1);
	glBindTexture(GL_TEXTURE_2D, mBicubicFilterTexV);
	glActiveTexture(GL_TEXTURE0);

	glUniform2f(mBicubicLoc_SrcSize, (float)dstW, (float)srcH);
	glUniform1f(mBicubicLoc_FilterCoord, 1.0f);  // vertical axis
	GLDrawFullscreenTriangle();

	glBindFramebuffer(GL_FRAMEBUFFER, mRenderTargetFBO);
	glViewport(0, 0, mWinW, mWinH);
	glUseProgram(0);
	glBindVertexArray(0);
}

void DisplayBackendGL::DrawFullscreen(GLuint program) {
	glBindVertexArray(mEmptyVAO);
	glUseProgram(program);
	GLDrawFullscreenTriangle();
	glUseProgram(0);
	glBindVertexArray(0);
}
