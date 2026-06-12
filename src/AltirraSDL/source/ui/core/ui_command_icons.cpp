//	AltirraSDL - Windows command icon atlas for ImGui controls.

#include <stdafx.h>
#include <SDL3/SDL.h>
#include <imgui.h>
#include <vector>
#include <cstring>

#ifndef ALTIRRA_NO_SDL3_IMAGE
#include <SDL3_image/SDL_image.h>
#endif

#include "command_icons_data.h"
#include "display_backend.h"
#include "gl_helpers.h"
#include "ui_command_icons.h"

extern SDL_Window *g_pWindow;
extern IDisplayBackend *ATUIGetDisplayBackend();

namespace ATCommandIcons {
namespace {

struct IconInfo {
	const char *mpName;
};

// Same order and 48x48 geometry as Altirra's generated
// uicommandiconinfo.inl for command-icons.png.
constexpr IconInfo kIconInfos[] {
	{ "50hz" },
	{ "60hz" },
	{ "basic" },
	{ "cold_reset" },
	{ "configure" },
	{ "controller_joystick" },
	{ "controller_none" },
	{ "controller_paddles" },
	{ "image_boot" },
	{ "image_open" },
	{ "record_audio" },
	{ "record_video" },
	{ "sio_c_patch" },
	{ "sio_d_patch" },
	{ "speed_normal" },
	{ "speed_pause" },
	{ "speed_slow" },
	{ "speed_warp" },
	{ "warm_reset" },
};

constexpr int kIconWidth = 48;
constexpr int kIconHeight = 48;

bool gInitialized = false;
bool gReady = false;
ImTextureID gTexID = (ImTextureID)0;
GLuint gGLTexture = 0;
SDL_Texture *gSDLTexture = nullptr;
int gAtlasWidth = 0;
int gAtlasHeight = 0;

SDL_Surface *DecodePNG() {
#ifndef ALTIRRA_NO_SDL3_IMAGE
	SDL_IOStream *io = SDL_IOFromConstMem(kATCommandIconsPNGData,
		kATCommandIconsPNGSize);
	if (!io)
		return nullptr;

	SDL_Surface *surf = IMG_Load_IO(io, true);
	if (!surf)
		return nullptr;

	SDL_Surface *conv = SDL_ConvertSurface(surf, SDL_PIXELFORMAT_BGRA32);
	SDL_DestroySurface(surf);
	return conv;
#else
	return nullptr;
#endif
}

bool UploadTexture(SDL_Surface *surf) {
	if (!surf || surf->w <= 0 || surf->h <= 0)
		return false;

	IDisplayBackend *backend = ATUIGetDisplayBackend();
	const bool useGL = backend && backend->GetType() == DisplayBackendType::OpenGL;

	gAtlasWidth = surf->w;
	gAtlasHeight = surf->h;

	if (useGL) {
		gGLTexture = GLCreateXRGB8888Texture(surf->w, surf->h, true, nullptr);
		if (!gGLTexture)
			return false;

		std::vector<uint32_t> buf((size_t)surf->w * (size_t)surf->h);
		const uint8_t *src = (const uint8_t *)surf->pixels;
		for (int y = 0; y < surf->h; ++y)
			memcpy(&buf[(size_t)y * (size_t)surf->w],
				src + (size_t)y * (size_t)surf->pitch,
				(size_t)surf->w * 4);

		glBindTexture(GL_TEXTURE_2D, gGLTexture);
		GLUploadXRGB8888(surf->w, surf->h, buf.data(), 0);

		gTexID = (ImTextureID)(intptr_t)gGLTexture;
		return true;
	}

	SDL_Renderer *renderer = g_pWindow ? SDL_GetRenderer(g_pWindow) : nullptr;
	if (!renderer)
		return false;

	gSDLTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_BGRA32,
		SDL_TEXTUREACCESS_STREAMING, surf->w, surf->h);
	if (!gSDLTexture)
		return false;

	void *pixels = nullptr;
	int pitch = 0;
	if (!SDL_LockTexture(gSDLTexture, nullptr, &pixels, &pitch)) {
		SDL_DestroyTexture(gSDLTexture);
		gSDLTexture = nullptr;
		return false;
	}

	const uint8_t *src = (const uint8_t *)surf->pixels;
	uint8_t *dst = (uint8_t *)pixels;
	const int copyBytes = surf->w * 4;
	for (int y = 0; y < surf->h; ++y) {
		memcpy(dst, src, (size_t)copyBytes);
		src += surf->pitch;
		dst += pitch;
	}
	SDL_UnlockTexture(gSDLTexture);

	gTexID = (ImTextureID)gSDLTexture;
	return true;
}

void Initialize() {
	if (gInitialized)
		return;
	gInitialized = true;

	SDL_Surface *surf = DecodePNG();
	if (!surf) {
		SDL_Log("Command icons: failed to decode atlas");
		return;
	}

	gReady = UploadTexture(surf);
	SDL_DestroySurface(surf);

	if (!gReady)
		SDL_Log("Command icons: failed to upload atlas");
}

} // namespace

bool Get(const char *name, Icon& icon) {
	Initialize();

	if (!gReady || !name || !*name)
		return false;

	int iconIndex = -1;
	for (int i = 0; i < (int)(sizeof kIconInfos / sizeof kIconInfos[0]); ++i) {
		if (!strcmp(kIconInfos[i].mpName, name)) {
			iconIndex = i;
			break;
		}
	}

	if (iconIndex < 0)
		return false;

	const int iconsPerRow = gAtlasWidth / kIconWidth;
	if (iconsPerRow <= 0)
		return false;

	const int x = (iconIndex % iconsPerRow) * kIconWidth;
	const int y = (iconIndex / iconsPerRow) * kIconHeight;

	icon.mTexID = gTexID;
	icon.mWidth = kIconWidth;
	icon.mHeight = kIconHeight;
	icon.mUV0 = ImVec2((float)x / (float)gAtlasWidth,
		(float)y / (float)gAtlasHeight);
	icon.mUV1 = ImVec2((float)(x + kIconWidth) / (float)gAtlasWidth,
		(float)(y + kIconHeight) / (float)gAtlasHeight);
	return true;
}

void Shutdown() {
	if (gGLTexture) {
		glDeleteTextures(1, &gGLTexture);
		gGLTexture = 0;
	}
	if (gSDLTexture) {
		SDL_DestroyTexture(gSDLTexture);
		gSDLTexture = nullptr;
	}

	gTexID = (ImTextureID)0;
	gAtlasWidth = 0;
	gAtlasHeight = 0;
	gReady = false;
	gInitialized = false;
}

} // namespace ATCommandIcons
