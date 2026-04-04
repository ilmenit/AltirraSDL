//	AltirraSDL - SDL_Renderer display backend implementation

#include <stdafx.h>
#include "display_backend_sdl.h"
#include "uiaccessors.h"
#include "uitypes.h"

DisplayBackendSDLRenderer::DisplayBackendSDLRenderer(SDL_Window *window, SDL_Renderer *renderer)
	: mpWindow(window)
	, mpRenderer(renderer)
{
}

DisplayBackendSDLRenderer::~DisplayBackendSDLRenderer() {
	if (mpTexture)
		SDL_DestroyTexture(mpTexture);
}

void DisplayBackendSDLRenderer::UploadFrame(const void *pixels, int width, int height, int pitch) {
	if (!pixels || width <= 0 || height <= 0)
		return;

	// Recreate texture if dimensions changed
	if (!mpTexture || mTexW != width || mTexH != height) {
		if (mpTexture)
			SDL_DestroyTexture(mpTexture);

		mpTexture = SDL_CreateTexture(mpRenderer,
			SDL_PIXELFORMAT_XRGB8888,
			SDL_TEXTUREACCESS_STREAMING,
			width, height);
		mTexW = width;
		mTexH = height;

		if (mpTexture) {
			// Apply current filter mode
			SetFilterMode(ATUIGetDisplayFilterMode());
		}
	}

	if (mpTexture)
		SDL_UpdateTexture(mpTexture, nullptr, pixels, pitch);
}

void DisplayBackendSDLRenderer::BeginFrame() {
	SDL_SetRenderDrawColor(mpRenderer, 0, 0, 0, 255);
	SDL_RenderClear(mpRenderer);
}

void DisplayBackendSDLRenderer::RenderFrame(float dstX, float dstY, float dstW, float dstH,
	int /*srcW*/, int /*srcH*/)
{
	if (!mpTexture)
		return;

	SDL_FRect rect;
	rect.x = dstX;
	rect.y = dstY;
	rect.w = dstW;
	rect.h = dstH;

	SDL_SetTextureBlendMode(mpTexture, SDL_BLENDMODE_NONE);
	SDL_RenderTexture(mpRenderer, mpTexture, nullptr, &rect);
}

void DisplayBackendSDLRenderer::Present() {
	SDL_RenderPresent(mpRenderer);
}

bool DisplayBackendSDLRenderer::ReadPixels(void *dst, int dstPitch, int x, int y, int w, int h) {
	SDL_Rect rect = { x, y, w, h };
	SDL_Surface *surface = SDL_RenderReadPixels(mpRenderer, &rect);
	if (!surface)
		return false;

	// Copy surface data to destination
	const uint8_t *src = (const uint8_t *)surface->pixels;
	uint8_t *dstPtr = (uint8_t *)dst;
	int copyW = std::min(w, surface->w) * 4;
	int copyH = std::min(h, surface->h);
	for (int row = 0; row < copyH; row++) {
		memcpy(dstPtr + row * dstPitch, src + row * surface->pitch, copyW);
	}

	SDL_DestroySurface(surface);
	return true;
}

void DisplayBackendSDLRenderer::OnResize(int /*w*/, int /*h*/) {
	// SDL_Renderer handles window resize automatically
}

void DisplayBackendSDLRenderer::SetFilterMode(int mode) {
	if (!mpTexture)
		return;

	SDL_ScaleMode scaleMode = SDL_SCALEMODE_LINEAR;
	if (mode == kATDisplayFilterMode_Point)
		scaleMode = SDL_SCALEMODE_NEAREST;

	SDL_SetTextureScaleMode(mpTexture, scaleMode);
}
