//	AltirraSDL - librashader integration wrapper implementation
//
//	This file compiles regardless of whether librashader headers are present.
//	When the headers are not available, all methods are no-ops.
//
//	librashader headers are fetched automatically by CMake when
//	ENABLE_LIBRASHADER=ON (default).  To disable, pass -DENABLE_LIBRASHADER=OFF.
//	The runtime shared library (librashader.so/.dll/.dylib) must be installed
//	separately — it is loaded via dlopen at startup.

#include <stdafx.h>
#include "display_librashader.h"
#include "logging.h"

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
#if defined(_WIN32)
		{
			DWORD err = GetLastError();
			LOG_WARN("librashader", "Failed to load librashader.dll (Win32 error %lu)", err);
			if (err == ERROR_MOD_NOT_FOUND)
				LOG_WARN("librashader", "librashader.dll not found next to AltirraSDL.exe");
			else if (err == ERROR_BAD_EXE_FORMAT)
				LOG_ERROR("librashader", "librashader.dll architecture mismatch (x86 vs x64)");
			else
				LOG_WARN("librashader", "A dependency of librashader.dll may be missing");
		}
#elif defined(__APPLE__)
		{
			const char *err = dlerror();
			LOG_WARN("librashader", "Failed to load librashader.dylib");
			if (err)
				LOG_WARN("librashader", "dlopen error: %s", err);
		}
#else
		{
			const char *err = dlerror();
			LOG_WARN("librashader", "Failed to load librashader.so");
			if (err)
				LOG_WARN("librashader", "dlopen error: %s", err);
		}
#endif
		LOG_INFO("librashader", "Shader presets disabled. To enable, build with: ./build.sh --librashader");
		LOG_INFO("librashader", "Or download from: https://github.com/SnowflakePowered/librashader/releases");
		mAvailable = false;
		return false;
	}

	mInstance = new libra_instance_t(instance);
	mAvailable = true;
	LOG_INFO("librashader", "Loaded successfully — shader presets available");
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
		LOG_ERROR("librashader", "Failed to load preset: %s", path);
		instance->error_free(&err);
		return false;
	}

	// Extract runtime parameter metadata from the preset BEFORE freeing it.
	// The preset owns the parameter name/description strings, so we must
	// copy them into our cached mParams vector here.
	mParams.clear();
	libra_preset_param_list_t paramList = {};
	err = instance->preset_get_runtime_params(&preset, &paramList);
	if (!err && paramList.length > 0) {
		mParams.reserve(paramList.length);
		for (size_t i = 0; i < paramList.length; i++) {
			const auto &src = paramList.parameters[i];
			LibrashaderParam p;
			p.name = src.name ? src.name : "";
			p.description = src.description ? src.description : "";
			p.value = src.initial;
			p.minimum = src.minimum;
			p.maximum = src.maximum;
			p.step = src.step;
			mParams.push_back(std::move(p));
		}
		instance->preset_free_runtime_params(paramList);
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
		LOG_ERROR("librashader", "Failed to create filter chain for: %s", path);
		instance->error_free(&err);
		instance->preset_free(&preset);
		mParams.clear();
		return false;
	}

	instance->preset_free(&preset);
	mFilterChain = chain;
	mPresetPath = path;

	LOG_INFO("librashader", "Loaded preset: %s (%zu parameters)", path, mParams.size());
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
	mParams.clear();
#endif
}

void LibrashaderRuntime::Apply(unsigned int inputTex, unsigned int outputTex,
	int srcW, int srcH, int dstW, int dstH, unsigned int frameCount)
{
#if HAVE_LIBRASHADER
	if (!mFilterChain || !mInstance)
		return;

	libra_instance_t *instance = (libra_instance_t *)mInstance;
	libra_gl_filter_chain_t chain = (libra_gl_filter_chain_t)mFilterChain;

	libra_image_gl_t input = {};
	input.handle = inputTex;
	input.format = 0x8058; // GL_RGBA8
	input.width = srcW;
	input.height = srcH;

	libra_image_gl_t output = {};
	output.handle = outputTex;
	output.format = 0x8058; // GL_RGBA8
	output.width = dstW;
	output.height = dstH;

	libra_viewport_t viewport = {};
	viewport.x = 0.0f;
	viewport.y = 0.0f;
	viewport.width = dstW;
	viewport.height = dstH;

	libra_error_t err = instance->gl_filter_chain_frame(&chain, frameCount,
		input, output, &viewport, nullptr, nullptr);
	if (err) {
		char *msg = nullptr;
		instance->error_write(err, &msg);
		if (msg) {
			LOG_ERROR("librashader", "frame error: %s", msg);
			instance->error_free_string(&msg);
		}
		instance->error_free(&err);
	}
#else
	(void)inputTex; (void)outputTex;
	(void)srcW; (void)srcH; (void)dstW; (void)dstH;
	(void)frameCount;
#endif
}

std::vector<LibrashaderParam> LibrashaderRuntime::GetParameters() const {
#if HAVE_LIBRASHADER
	if (!mFilterChain || !mInstance || mParams.empty())
		return mParams;

	// Return a copy with current runtime values read from the filter chain.
	libra_instance_t *instance = (libra_instance_t *)mInstance;
	libra_gl_filter_chain_t chain = (libra_gl_filter_chain_t)mFilterChain;

	std::vector<LibrashaderParam> result = mParams;
	for (auto &p : result) {
		float val = p.value;
		libra_error_t err = instance->gl_filter_chain_get_param(
			&chain, p.name.c_str(), &val);
		if (!err)
			p.value = val;
	}
	return result;
#else
	return {};
#endif
}

bool LibrashaderRuntime::SetParameter(const char *name, float value) {
#if HAVE_LIBRASHADER
	if (!mFilterChain || !mInstance || !name)
		return false;

	libra_instance_t *instance = (libra_instance_t *)mInstance;
	libra_gl_filter_chain_t chain = (libra_gl_filter_chain_t)mFilterChain;

	libra_error_t err = instance->gl_filter_chain_set_param(
		&chain, name, value);
	return err == nullptr;
#else
	(void)name; (void)value;
	return false;
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
