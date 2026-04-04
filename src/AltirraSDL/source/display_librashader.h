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
#include <vector>

// Forward declaration — actual librashader types are only used internally
// in display_librashader.cpp when the headers are available.
class DisplayBackendGL33;

// Runtime parameter metadata for a loaded shader preset.
struct LibrashaderParam {
	std::string name;
	std::string description;  // display label (may be empty — use name)
	float value;
	float minimum;
	float maximum;
	float step;
};

class LibrashaderRuntime {
public:
	LibrashaderRuntime();
	~LibrashaderRuntime();

	// Returns true if librashader.so was found and loaded successfully.
	bool IsAvailable() const { return mAvailable; }

	// Returns true if a shader preset is currently loaded.
	bool HasPreset() const { return mFilterChain != nullptr; }

	// Initialize with the GL context. Call after GL context is created.
	bool Init();

	// Load a shader preset file (.slangp or .glslp).
	// Returns true on success.  Parameter metadata is extracted and cached.
	bool LoadPreset(const char *path);

	// Clear the active preset and free the filter chain.
	void ClearPreset();

	// Returns the path of the currently loaded preset, or empty string.
	const std::string &GetPresetPath() const { return mPresetPath; }

	// Apply the shader chain to the input texture, rendering to the output FBO.
	// inputTex: GL texture containing the emulator frame
	// outputFBO: target FBO handle
	// outputTex: texture attached to the target FBO (needed by librashader internals)
	// srcW, srcH: input texture dimensions
	// dstW, dstH: output dimensions
	// frameCount: monotonically increasing frame counter
	void Apply(unsigned int inputTex, unsigned int outputFBO, unsigned int outputTex,
		int srcW, int srcH, int dstW, int dstH, unsigned int frameCount);

	// Return the cached parameter metadata.
	// Values are updated by reading from the filter chain each call.
	std::vector<LibrashaderParam> GetParameters() const;

	// Set a runtime parameter value by name.  Returns true on success.
	bool SetParameter(const char *name, float value);

	// Shut down and release all resources.
	void Shutdown();

private:
	bool mAvailable = false;
	bool mInitialized = false;
	std::string mPresetPath;

	// Cached parameter metadata — extracted from the preset at load time.
	std::vector<LibrashaderParam> mParams;

	// Opaque handles — only valid when librashader headers are present
	void *mInstance = nullptr;     // libra_instance_t
	void *mFilterChain = nullptr;  // libra_gl_filter_chain_t
};
