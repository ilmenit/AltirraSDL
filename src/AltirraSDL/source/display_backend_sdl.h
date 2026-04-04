//	AltirraSDL - SDL_Renderer display backend
//	Wraps the existing SDL_Renderer path behind IDisplayBackend.

#pragma once

#include <SDL3/SDL.h>
#include "display_backend.h"

class DisplayBackendSDLRenderer final : public IDisplayBackend {
public:
	DisplayBackendSDLRenderer(SDL_Window *window, SDL_Renderer *renderer);
	~DisplayBackendSDLRenderer() override;

	DisplayBackendType GetType() const override { return DisplayBackendType::SDLRenderer; }

	void UploadFrame(const void *pixels, int width, int height, int pitch) override;
	void BeginFrame() override;
	void RenderFrame(float dstX, float dstY, float dstW, float dstH,
		int srcW, int srcH) override;
	void Present() override;
	bool ReadPixels(void *dst, int dstPitch, int x, int y, int w, int h) override;
	void OnResize(int w, int h) override;

	bool SupportsScreenFX() const override { return false; }
	void UpdateScreenFX(const VDVideoDisplayScreenFXInfo &) override {}
	void SetFilterMode(int mode) override;
	void SetFilterSharpness(float) override {}

	bool SupportsExternalShaders() const override { return false; }
	bool LoadShaderPreset(const char *) override { return false; }
	void ClearShaderPreset() override {}
	const char *GetShaderPresetPath() const override { return ""; }
	bool HasShaderPreset() const override { return false; }
	std::vector<LibrashaderParam> GetShaderParameters() const override { return {}; }
	bool SetShaderParameter(const char *, float) override { return false; }

	SDL_Renderer *GetSDLRenderer() override { return mpRenderer; }
	SDL_Window *GetWindow() override { return mpWindow; }

	bool HasTexture() const override { return mpTexture != nullptr; }
	int GetTextureWidth() const override { return mTexW; }
	int GetTextureHeight() const override { return mTexH; }
	void *GetImGuiTextureID() const override { return (void *)mpTexture; }

private:
	SDL_Window *mpWindow;
	SDL_Renderer *mpRenderer;
	SDL_Texture *mpTexture = nullptr;
	int mTexW = 0;
	int mTexH = 0;
};
