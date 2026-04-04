//	AltirraSDL - OpenGL 3.3 display backend implementation
//	GPU-accelerated post-processing: screen effects, bloom V2, bicubic, PAL.

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

// ============================================================================
// Construction / destruction
// ============================================================================

DisplayBackendGL33::DisplayBackendGL33(SDL_Window *window, SDL_GLContext glContext)
	: mpWindow(window)
	, mGLContext(glContext)
{
	SDL_GL_MakeCurrent(window, glContext);

	fprintf(stderr, "[GL] Renderer: %s\n", glGetString(GL_RENDERER));
	fprintf(stderr, "[GL] Version:  %s\n", glGetString(GL_VERSION));

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

bool DisplayBackendGL33::LoadShaderPreset(const char *path) {
	return mLibrashader.LoadPreset(path);
}

void DisplayBackendGL33::ClearShaderPreset() {
	mLibrashader.ClearPreset();
}

const char *DisplayBackendGL33::GetShaderPresetPath() const {
	return mLibrashader.GetPresetPath().c_str();
}

DisplayBackendGL33::~DisplayBackendGL33() {
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

void DisplayBackendGL33::CompilePassthroughProgram() {
	mPassthroughProgram = GLCreateProgram(kGLSL_FullscreenTriangleVS, kGLSL_PassthroughFS);
	if (mPassthroughProgram) {
		mPassthroughLoc_SourceTex = glGetUniformLocation(mPassthroughProgram, "uSourceTex");
	}
}


const ScreenFXProgram &DisplayBackendGL33::GetScreenFXProgram(uint32_t features) {
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

	// Compile: vertex shader is the display quad VS, fragment is preamble + screenfx
	const char *fsSources[2] = { preamble.c_str(), kGLSL_ScreenFX_FS };

	// The ScreenFX shader already has #version 330 core, so we skip a separate
	// version line. But the preamble needs to go AFTER the #version line.
	// Since the shader source already starts with #version, we prepend defines
	// before it by splitting. Actually, the #version must be first. Let's build
	// the full source with version first, then defines, then shader body.
	std::string fullFS = "#version 330 core\n" + preamble;

	// Strip the #version line from the embedded shader source (the preamble
	// already includes it, so we skip the one in kGLSL_ScreenFX_FS).
	const char *body = kGLSL_ScreenFX_FS;
	{
		const char *p = body;
		while (*p == '\n' || *p == '\r' || *p == ' ') p++;
		if (strncmp(p, "#version", 8) == 0) {
			while (*p && *p != '\n') p++;
			if (*p == '\n') p++;
			body = p;
		}
	}
	fullFS += body;

	GLuint vs = GLCompileShader(GL_VERTEX_SHADER, kGLSL_FullscreenTriangleVS);
	GLuint fs = GLCompileShader(GL_FRAGMENT_SHADER, fullFS.c_str());

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

void DisplayBackendGL33::UploadFrame(const void *pixels, int width, int height, int pitch) {
	if (!pixels || width <= 0 || height <= 0)
		return;

	// Recreate texture if dimensions changed
	if (!mEmuTexture || mTexW != width || mTexH != height) {
		if (mEmuTexture)
			glDeleteTextures(1, &mEmuTexture);

		mEmuTexture = GLCreateTexture2D(width, height, GL_RGBA8, GL_BGRA,
			GL_UNSIGNED_INT_8_8_8_8_REV, nullptr, false);
		mTexW = width;
		mTexH = height;

		// Apply current filter mode
		SetFilterMode(mFilterMode);

		// Mark screen FX dirty to regenerate lookup textures for new dimensions
		mScreenFXDirty = true;
	}

	// Upload pixel data
	glBindTexture(GL_TEXTURE_2D, mEmuTexture);
	glPixelStorei(GL_UNPACK_ROW_LENGTH, pitch / 4);
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height,
		GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, pixels);
	glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
}

// ============================================================================
// Frame rendering
// ============================================================================

void DisplayBackendGL33::BeginFrame() {
	SDL_GetWindowSizeInPixels(mpWindow, &mWinW, &mWinH);
	glViewport(0, 0, mWinW, mWinH);
	glClear(GL_COLOR_BUFFER_BIT);
}

void DisplayBackendGL33::RenderFrame(float dstX, float dstY, float dstW, float dstH,
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
		// Override the window dimensions so viewport calculations inside
		// RenderScreenFX use the FBO size, not the window size.
		int savedWinW = mWinW, savedWinH = mWinH;
		mWinW = vpW;
		mWinH = vpH;

		// Render built-in effects into the FBO at (0,0,vpW,vpH)
		RenderFrameInner(0.0f, 0.0f, dstW, dstH, srcW, srcH);

		// Restore
		mWinW = savedWinW;
		mWinH = savedWinH;
		glBindFramebuffer(GL_FRAMEBUFFER, 0);

		// Now apply librashader on top: FBO texture → screen
		++mFrameCounter;

		// Allocate a second FBO for librashader output
		// (reuse mLibrashaderFBO as input, need a separate output)
		// Actually, librashader reads from a texture and writes to an FBO,
		// so we can't use the same FBO for both. We'll use a static second FBO.
		static GLRenderTarget s_librashaderOut;
		if (s_librashaderOut.width != vpW || s_librashaderOut.height != vpH) {
			s_librashaderOut.Destroy();
			s_librashaderOut.Create(vpW, vpH, GL_RGBA8);
		}

		mLibrashader.Apply(mLibrashaderFBO.tex, s_librashaderOut.fbo, s_librashaderOut.tex,
			vpW, vpH, vpW, vpH, mFrameCounter);

		// Restore GL state after librashader
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		glDisable(GL_BLEND);
		glDisable(GL_DEPTH_TEST);
		glDisable(GL_STENCIL_TEST);
		glDisable(GL_CULL_FACE);
		glDisable(GL_SCISSOR_TEST);

		// Blit librashader output to the screen
		int scrVpX = (int)dstX;
		int scrVpY = savedWinH - (int)(dstY + dstH);
		glViewport(scrVpX, scrVpY, vpW, vpH);
		glBindVertexArray(mEmptyVAO);
		glUseProgram(mPassthroughProgram);

		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, s_librashaderOut.tex);
		glUniform1i(mPassthroughLoc_SourceTex, 0);

		GLDrawFullscreenTriangle();

		glUseProgram(0);
		glBindVertexArray(0);
		glViewport(0, 0, savedWinW, savedWinH);
		return;
	}

	RenderFrameInner(dstX, dstY, dstW, dstH, srcW, srcH);
}

void DisplayBackendGL33::RenderFrameInner(float dstX, float dstY, float dstW, float dstH,
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

	static int s_glDiag = 0;
	if (s_glDiag < 5) {
		fprintf(stderr, "[GL-DIAG] hasEffects=%d bicubic=%d sharpBilinear=%d gamma=%.2f scanline=%.1f filterMode=%d\n",
			hasEffects, useBicubic, useSharpBilinear,
			mScreenFX.mGamma, mScreenFX.mScanlineIntensity, (int)mFilterMode);
		fprintf(stderr, "[GL-DIAG] colorCorr: [%.3f,%.3f,%.3f][%.3f,%.3f,%.3f][%.3f,%.3f,%.3f]\n",
			mScreenFX.mColorCorrectionMatrix[0][0], mScreenFX.mColorCorrectionMatrix[0][1], mScreenFX.mColorCorrectionMatrix[0][2],
			mScreenFX.mColorCorrectionMatrix[1][0], mScreenFX.mColorCorrectionMatrix[1][1], mScreenFX.mColorCorrectionMatrix[1][2],
			mScreenFX.mColorCorrectionMatrix[2][0], mScreenFX.mColorCorrectionMatrix[2][1], mScreenFX.mColorCorrectionMatrix[2][2]);
		fprintf(stderr, "[GL-DIAG] palBlend=%.2f distortion=%.2f bloom=%d maskType=%d\n",
			mScreenFX.mPALBlendingOffset, mScreenFX.mDistortionX,
			mScreenFX.mbBloomEnabled, (int)mScreenFX.mScreenMaskParams.mType);
	}

	if (hasEffects || useBicubic || useSharpBilinear) {
		if (s_glDiag < 5)
			fprintf(stderr, "[GL-DIAG] -> RenderScreenFX path\n");
		++s_glDiag;
		RenderScreenFX(dstX, dstY, dstW, dstH, srcW, srcH);
		return;
	}

	if (s_glDiag < 5)
		fprintf(stderr, "[GL-DIAG] -> passthrough path\n");
	++s_glDiag;

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

void DisplayBackendGL33::Present() {
	SDL_GL_SwapWindow(mpWindow);
}

bool DisplayBackendGL33::ReadPixels(void *dst, int dstPitch, int x, int y, int w, int h) {
	if (!dst || w <= 0 || h <= 0)
		return false;

	glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);

	// GL reads bottom-up; we need top-down. Read row by row in reverse.
	glPixelStorei(GL_PACK_ROW_LENGTH, dstPitch / 4);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);

	// Read the whole region bottom-up and flip
	std::vector<uint8_t> temp(w * 4 * h);
	glReadPixels(x, mWinH - y - h, w, h, GL_RGBA, GL_UNSIGNED_BYTE, temp.data());

	// Flip vertically into destination
	uint8_t *dstPtr = (uint8_t *)dst;
	for (int row = 0; row < h; row++) {
		memcpy(dstPtr + row * dstPitch,
			temp.data() + (h - 1 - row) * w * 4,
			w * 4);
	}

	glPixelStorei(GL_PACK_ROW_LENGTH, 0);
	return true;
}

void DisplayBackendGL33::OnResize(int w, int h) {
	mWinW = w;
	mWinH = h;
	// Lookup textures (scanline mask, etc.) need regeneration on resize
	mScreenFXDirty = true;
}

// ============================================================================
// Filter mode
// ============================================================================

void DisplayBackendGL33::SetFilterMode(int mode) {
	mFilterMode = mode;

	if (!mEmuTexture)
		return;

	glBindTexture(GL_TEXTURE_2D, mEmuTexture);

	GLenum filter = GL_LINEAR;
	if (mode == kATDisplayFilterMode_Point)
		filter = GL_NEAREST;
	// Sharp bilinear and bicubic use their own shader-based filtering,
	// but the base texture should be nearest to avoid double-filtering.
	if (mode == kATDisplayFilterMode_SharpBilinear || mode == kATDisplayFilterMode_Bicubic)
		filter = GL_NEAREST;

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
}

void DisplayBackendGL33::SetFilterSharpness(float sharpness) {
	mFilterSharpness = sharpness;
}

// ============================================================================
// Screen effects
// ============================================================================

void DisplayBackendGL33::UpdateScreenFX(const VDVideoDisplayScreenFXInfo &info) {
	if (!(mScreenFX == info)) {
		mScreenFX = info;
		mScreenFXDirty = true;
	}
}

void DisplayBackendGL33::UpdateLookupTextures() {
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
			if (!mMaskTexture || mMaskTexW != maskW || mMaskTexH != maskH) {
				if (mMaskTexture) glDeleteTextures(1, &mMaskTexture);
				mMaskTexture = GLCreateTexture2D(maskW, maskH, GL_RGBA8, GL_RGBA,
					GL_UNSIGNED_BYTE, mLookupBuffer.data(), true);
				mMaskTexW = maskW;
				mMaskTexH = maskH;
			} else {
				glBindTexture(GL_TEXTURE_2D, mMaskTexture);
				glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, maskW, maskH,
					GL_RGBA, GL_UNSIGNED_BYTE, mLookupBuffer.data());
			}
		}
	}
}

void DisplayBackendGL33::RenderScreenFX(float dstX, float dstY, float dstW, float dstH,
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
			mPALProgram = GLCreateProgram(kGLSL_FullscreenTriangleVS, kGLSL_PALArtifacting_FS);
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
			glUniform2f(mPALLoc_ChromaOffset,
				0.0f, mScreenFX.mPALBlendingOffset / (float)mTexH);

			// Signed RGB encoding flag — when the frame uses extended-range
			// palette encoding, the shader must convert to normal [0,1] range.
			glUniform1i(mPALLoc_SignedRGB, mScreenFX.mbSignedRGBEncoding ? 1 : 0);

			GLDrawFullscreenTriangle();

			glUseProgram(0);
			glBindVertexArray(0);
			glBindFramebuffer(GL_FRAMEBUFFER, 0);

			// Restore the filter mode on the emulator texture
			GLenum filter = (mFilterMode == kATDisplayFilterMode_Point)
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
		// Sharp bilinear parameters
		float snapScaleX = (float)srcW / dstW * (2.0f + mFilterSharpness);
		float snapScaleY = (float)srcH / dstH * (2.0f + mFilterSharpness);
		float uvScaleX = 1.0f / (float)srcW;
		float uvScaleY = 1.0f / (float)srcH;
		glUniform4f(u.uSharpnessInfo, snapScaleX, snapScaleY, uvScaleX, uvScaleY);
	}

	if ((features & kSFX_Scanlines) && u.uScanlineInfo >= 0) {
		// Scanline UV transform: map destination Y to scanline texture V
		float scaleY = dstH / mWinH;
		float offsetY = dstY / mWinH;
		glUniform4f(u.uScanlineInfo, 1.0f, scaleY, 0.0f, offsetY);
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

	glUseProgram(0);
	glBindVertexArray(0);

	// Restore full-window viewport for ImGui
	glViewport(0, 0, mWinW, mWinH);
}

// ============================================================================
// Bloom V2
// ============================================================================

void DisplayBackendGL33::EnsureBloomPyramid(int baseW, int baseH) {
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

void DisplayBackendGL33::RenderBloomV2(int srcW, int srcH, GLuint sourceTex) {
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
		// D3D9 uses 1/targetTexSize as UV step, NOT 0.5/sourceTexSize.
		// The HLSL shader multiplies the step by 1.75 to get the sample offset,
		// so using the target (half-res) texture size gives the correct coverage.
		glUniform2f(mBloomDownLoc_UVStep,
			1.0f / mBloomPyramid[i].width,
			1.0f / mBloomPyramid[i].height);
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

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glViewport(0, 0, mWinW, mWinH);
	glBindVertexArray(0);
}

// ============================================================================
// Bicubic filter
// ============================================================================

void DisplayBackendGL33::RenderBicubic(int srcW, int srcH, int dstW, int dstH, GLuint sourceTex) {
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

	// Generate bicubic filter textures
	if (!mBicubicFilterTexH) {
		mLookupBuffer.resize(dstW);
		VDDisplayCreateBicubicTexture(mLookupBuffer.data(), dstW, srcW);
		mBicubicFilterTexH = GLCreateTexture2D(dstW, 1, GL_RGBA8, GL_RGBA,
			GL_UNSIGNED_BYTE, mLookupBuffer.data(), true);
	}
	if (!mBicubicFilterTexV) {
		mLookupBuffer.resize(dstH);
		VDDisplayCreateBicubicTexture(mLookupBuffer.data(), dstH, srcH);
		mBicubicFilterTexV = GLCreateTexture2D(dstH, 1, GL_RGBA8, GL_RGBA,
			GL_UNSIGNED_BYTE, mLookupBuffer.data(), true);
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

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glViewport(0, 0, mWinW, mWinH);
	glUseProgram(0);
	glBindVertexArray(0);
}

void DisplayBackendGL33::DrawFullscreen(GLuint program) {
	glBindVertexArray(mEmptyVAO);
	glUseProgram(program);
	GLDrawFullscreenTriangle();
	glUseProgram(0);
	glBindVertexArray(0);
}
