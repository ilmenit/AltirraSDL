//	AltirraSDL - Display backend abstraction
//	Defines the interface for SDL_GPU, OpenGL, and SDL_Renderer display backends.

#pragma once

#include <vd2/system/vdtypes.h>

struct SDL_Renderer;
struct SDL_Window;
struct VDVideoDisplayScreenFXInfo;
struct VDDScreenMaskParams;

enum class DisplayBackendType {
	// SDL's cross-platform GPU renderer. Uses Metal on macOS and Vulkan on
	// Linux, with Altirra's custom display shader and full screen effects.
	SDLGPU,

	// SDL3's built-in renderer.  Picks the platform's best 2D path
	// automatically (D3D11/D3D12 on Windows, Metal on macOS, GLES/Vulkan
	// on Android, etc.), but exposes no custom-shader entry point, so
	// built-in screen effects are unavailable through this backend.
	SDLRenderer,

	// AltirraSDL's own GL backend.  The same class (DisplayBackendGL)
	// handles either Desktop OpenGL 3.3 Core (Windows/Linux/macOS) or
	// OpenGL ES 3.0 (Android/iOS) — chosen at context-creation time via
	// GLProfile. Both paths provide the full screen-FX pipeline.
	OpenGL,
};

// User-selected startup renderer. This is separate from DisplayBackendType:
// an SDL_GPU request may resolve to OpenGL when native GPU initialization is
// unavailable. Changing it requires an application restart because the SDL
// window, ImGui backend, and display textures are renderer-specific.
enum class ATDisplayBackendPreference {
	SDLGPU,
	OpenGL,
	SDLRenderer,
};

const char *ATDisplayBackendPreferenceName(ATDisplayBackendPreference preference);
bool ATDisplayBackendPreferenceParse(const char *name,
	ATDisplayBackendPreference& preference);
ATDisplayBackendPreference ATDisplayBackendPreferenceLoad();
void ATDisplayBackendPreferenceSave(ATDisplayBackendPreference preference);
const char *ATDisplayBackendTypeName(DisplayBackendType type);

class IDisplayBackend {
public:
	virtual ~IDisplayBackend() = default;

	virtual DisplayBackendType GetType() const = 0;

	// Upload emulator frame pixels (XRGB8888) to the GPU texture.
	virtual void UploadFrame(const void *pixels, int width, int height, int pitch) = 0;

	// Clear the backbuffer to black, prepare for a new frame.
	virtual void BeginFrame() = 0;

	// Render the emulator frame with active effects to the given destination rect.
	virtual void RenderFrame(float dstX, float dstY, float dstW, float dstH,
		int srcW, int srcH) = 0;

	// Render the emulator frame to the full destination rect while constraining
	// output to a top-left-origin clip rect. This preserves zoom/pan source
	// mapping for debugger panes where the destination can extend outside the
	// visible pane viewport.
	virtual void RenderFrameClipped(float dstX, float dstY, float dstW, float dstH,
		int srcW, int srcH, float clipX, float clipY, float clipW, float clipH) = 0;

	// Finalize and present the frame (swap buffers / SDL_RenderPresent).
	virtual void Present() = 0;

	// Read back pixels from the framebuffer for frame capture.
	// Returns true on success.
	virtual bool ReadPixels(void *dst, int dstPitch, int x, int y, int w, int h) = 0;

	// Notify of window resize.
	virtual void OnResize(int w, int h) = 0;

	// Returns true if this backend supports GPU shader effects.
	virtual bool SupportsScreenFX() const = 0;

	// HDR is available only when the active renderer owns a linear swapchain.
	virtual bool SupportsHDR() const { return false; }
	virtual bool IsHDREnabled() const { return false; }

	// Update screen FX parameters (scanlines, bloom, masks, etc.).
	// No-op if SupportsScreenFX() returns false.
	virtual void UpdateScreenFX(const VDVideoDisplayScreenFXInfo &info) = 0;

	// Update filter mode (point, bilinear, sharp bilinear, bicubic).
	virtual void SetFilterMode(int mode) = 0;

	// Update filter sharpness for sharp bilinear.
	virtual void SetFilterSharpness(float sharpness) = 0;

	// Get the underlying SDL_Renderer (for ImGui SDLRenderer3 backend).
	// Returns nullptr for the OpenGL backend.
	virtual SDL_Renderer *GetSDLRenderer() = 0;

	// Get the SDL_Window associated with this backend.
	virtual SDL_Window *GetWindow() = 0;

	// Whether the backend has a valid emulator texture to render.
	virtual bool HasTexture() const = 0;
	virtual int GetTextureWidth() const = 0;
	virtual int GetTextureHeight() const = 0;

	// Return the emulator frame texture as an ImGui-compatible texture ID.
	// For OpenGL backend this is the GL texture name cast to ImTextureID.
	// For SDL_GPU/SDL_Renderer backends this is SDL_Texture* as ImTextureID.
	virtual void *GetImGuiTextureID() const = 0;
};

// Global accessor — returns the active display backend, or nullptr if not yet
// initialized.  Used by ui_rewind.cpp and ui_screenfx.cpp for texture creation.
IDisplayBackend *ATUIGetDisplayBackend();
void ATUISetDisplayBackend(IDisplayBackend *backend);
