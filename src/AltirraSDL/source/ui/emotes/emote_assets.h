//	AltirraSDL - Online Play emote icons (16 PNGs baked into the binary).
//	Textures are decoded and uploaded once at startup; they live for the
//	entire process lifetime.

#pragma once

#include <imgui.h>
#include <stdint.h>

#ifdef ALTIRRA_NETPLAY_ENABLED

namespace ATEmotes {

constexpr int kCount = 16;

// Decode all 16 baked PNGs and upload them as ImGui textures.  Call once
// after the display backend is ready (OpenGL context / SDL_Renderer).
// Safe to call more than once — subsequent calls are no-ops.
void Initialize();

// Release all emote textures.  Called from the frontend shutdown path.
void Shutdown();

// Returns true once Initialize() has successfully populated the textures.
bool IsReady();

// Returns the texture for icon [0..15], or 0 if not ready / invalid id.
// Width/height are filled with the PNG pixel dimensions when non-null.
ImTextureID GetTexture(int iconId, int *outW = nullptr, int *outH = nullptr);

} // namespace ATEmotes

#else // !ALTIRRA_NETPLAY_ENABLED

// Inline no-op stubs for builds without the netplay module (e.g. WASM).
// See netplay_glue.h for the rationale.
namespace ATEmotes {
    constexpr int kCount = 16;
    inline void Initialize()                                          {}
    inline void Shutdown()                                            {}
    inline bool IsReady()                                             { return false; }
    inline ImTextureID GetTexture(int, int* = nullptr, int* = nullptr){ return (ImTextureID)0; }
} // namespace ATEmotes

#endif // ALTIRRA_NETPLAY_ENABLED
