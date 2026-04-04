//	AltirraSDL - librashader integration wrapper
//	Optional runtime loading of librashader for RetroArch shader presets.
//
//	librashader is loaded via dlopen/LoadLibrary at runtime. If the shared
//	library is not installed, all methods are no-ops. Zero compile-time
//	dependency — only two vendored headers are needed:
//	  vendor/librashader/librashader.h
//	  vendor/librashader/librashader_ld.h
//
//	To enable: install librashader.so (Linux) or librashader.dylib (macOS)
//	from https://github.com/SnowflakePowered/librashader/releases
//	then drop the headers into src/AltirraSDL/vendor/librashader/.

#pragma once

#include <string>

// Forward declaration — actual librashader types are only used internally
// in display_librashader.cpp when the headers are available.
class DisplayBackendGL33;

class LibrashaderRuntime {
public:
	LibrashaderRuntime();
	~LibrashaderRuntime();

	// Returns true if librashader.so was found and loaded successfully.
	bool IsAvailable() const { return mAvailable; }

	// Initialize with the GL context. Call after GL context is created.
	bool Init();

	// Load a shader preset file (.slangp or .glslp).
	// Returns true on success.
	bool LoadPreset(const char *path);

	// Clear the active preset and free the filter chain.
	void ClearPreset();

	// Returns the path of the currently loaded preset, or empty string.
	const std::string &GetPresetPath() const { return mPresetPath; }

	// Apply the shader chain to the input texture, rendering to the output FBO.
	// inputTex: GL texture containing the emulator frame (linear RGB)
	// outputFBO: target FBO (0 for default framebuffer)
	// srcW, srcH: input texture dimensions
	// dstW, dstH: output dimensions
	// frameCount: monotonically increasing frame counter
	void Apply(unsigned int inputTex, unsigned int outputFBO,
		int srcW, int srcH, int dstW, int dstH, unsigned int frameCount);

	// Shut down and release all resources.
	void Shutdown();

private:
	bool mAvailable = false;
	bool mInitialized = false;
	std::string mPresetPath;

	// Opaque handles — only valid when librashader headers are present
	void *mInstance = nullptr;     // libra_instance_t
	void *mFilterChain = nullptr;  // libra_gl_filter_chain_t
};
