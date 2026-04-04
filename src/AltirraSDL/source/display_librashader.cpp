//	AltirraSDL - librashader integration wrapper implementation
//
//	This file compiles regardless of whether librashader headers are present.
//	When the headers are not available, all methods are no-ops.
//
//	To enable librashader support:
//	1. Create vendor/librashader/ directory
//	2. Copy librashader.h and librashader_ld.h from
//	   https://github.com/SnowflakePowered/librashader/releases
//	3. Rebuild — the #if below will pick up the headers automatically
//	4. Install librashader.so on the target system

#include <stdafx.h>
#include "display_librashader.h"
#include <stdio.h>

// Check if librashader headers are available
#if __has_include("../vendor/librashader/librashader_ld.h")
#define HAVE_LIBRASHADER 1
#define LIBRA_RUNTIME_OPENGL
#include "../vendor/librashader/librashader_ld.h"
#include <SDL3/SDL.h>  // for SDL_GL_GetProcAddress
#else
#define HAVE_LIBRASHADER 0
#endif

LibrashaderRuntime::LibrashaderRuntime() {
}

LibrashaderRuntime::~LibrashaderRuntime() {
	Shutdown();
}

bool LibrashaderRuntime::Init() {
#if HAVE_LIBRASHADER
	if (mInitialized)
		return mAvailable;

	mInitialized = true;

	// Try to load the shared library
	libra_instance_t instance = librashader_load_instance();
	if (!instance.instance_loaded) {
		fprintf(stderr, "[librashader] Shared library not found — shader presets disabled\n");
		mAvailable = false;
		return false;
	}

	mInstance = new libra_instance_t(instance);
	mAvailable = true;
	fprintf(stderr, "[librashader] Loaded successfully — shader presets available\n");
	return true;
#else
	mInitialized = true;
	mAvailable = false;
	return false;
#endif
}

bool LibrashaderRuntime::LoadPreset(const char *path) {
#if HAVE_LIBRASHADER
	if (!mAvailable || !mInstance)
		return false;

	ClearPreset();

	libra_instance_t *instance = (libra_instance_t *)mInstance;

	// Load the preset file
	libra_shader_preset_t preset = nullptr;
	libra_error_t err = instance->preset_create(path, &preset);
	if (err) {
		fprintf(stderr, "[librashader] Failed to load preset: %s\n", path);
		return false;
	}

	// Create the GL filter chain
	libra_gl_filter_chain_t chain = nullptr;
	filter_chain_gl_opt_t opts = {};
	opts.version = LIBRASHADER_CURRENT_VERSION;
	opts.glsl_version = 330;
	opts.use_dsa = false;
	opts.force_no_mipmaps = false;
	opts.disable_cache = false;

	err = instance->gl_filter_chain_create(
		&preset,
		(libra_gl_loader_t)SDL_GL_GetProcAddress,
		&opts,
		&chain);

	if (err) {
		fprintf(stderr, "[librashader] Failed to create filter chain for: %s\n", path);
		instance->preset_free(&preset);
		return false;
	}

	instance->preset_free(&preset);
	mFilterChain = chain;
	mPresetPath = path;

	fprintf(stderr, "[librashader] Loaded preset: %s\n", path);
	return true;
#else
	(void)path;
	return false;
#endif
}

void LibrashaderRuntime::ClearPreset() {
#if HAVE_LIBRASHADER
	if (mFilterChain && mInstance) {
		libra_instance_t *instance = (libra_instance_t *)mInstance;
		libra_gl_filter_chain_t chain = (libra_gl_filter_chain_t)mFilterChain;
		instance->gl_filter_chain_free(&chain);
	}
	mFilterChain = nullptr;
	mPresetPath.clear();
#endif
}

void LibrashaderRuntime::Apply(unsigned int inputTex, unsigned int outputFBO,
	int srcW, int srcH, int dstW, int dstH, unsigned int frameCount)
{
#if HAVE_LIBRASHADER
	if (!mFilterChain || !mInstance)
		return;

	libra_instance_t *instance = (libra_instance_t *)mInstance;
	libra_gl_filter_chain_t chain = (libra_gl_filter_chain_t)mFilterChain;

	libra_source_image_gl_t input = {};
	input.handle = inputTex;
	input.format = 0x8058; // GL_RGBA8
	input.width = srcW;
	input.height = srcH;

	libra_output_image_gl_t output = {};
	output.handle = outputFBO;
	output.format = 0x8058; // GL_RGBA8
	output.width = dstW;
	output.height = dstH;

	libra_output_framebuffer_gl_t framebuffer = {};
	framebuffer.fbo = outputFBO;
	framebuffer.format = 0x8058;
	framebuffer.width = dstW;
	framebuffer.height = dstH;

	instance->gl_filter_chain_frame(&chain, frameCount,
		input, framebuffer, nullptr, nullptr);
#else
	(void)inputTex; (void)outputFBO;
	(void)srcW; (void)srcH; (void)dstW; (void)dstH;
	(void)frameCount;
#endif
}

void LibrashaderRuntime::Shutdown() {
#if HAVE_LIBRASHADER
	ClearPreset();
	if (mInstance) {
		delete (libra_instance_t *)mInstance;
		mInstance = nullptr;
	}
#endif
	mAvailable = false;
	mInitialized = false;
}
