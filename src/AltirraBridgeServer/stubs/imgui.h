// AltirraBridgeServer stub for <imgui.h>.
//
// The headless server target does not link Dear ImGui. A few of the
// SDL3 frontend's headers (notably ui_main.h) include <imgui.h> at
// the top — usually because they declare functions that take ImVec2
// out-parameters or use inline ImGui::IsKeyPressed() helpers.
//
// We don't need any of those functions to actually work in the
// headless build; we just need the include chain to compile so that
// the parts of those headers we DO use (e.g. uiaccessors function
// declarations) become visible.
//
// This stub provides the minimum surface area for that. It does NOT
// link against any ImGui code — calling any ImGui:: function from
// the bridge server build will fail at link time.

#pragma once

struct ImVec2 {
	float x = 0.0f;
	float y = 0.0f;
	ImVec2() = default;
	ImVec2(float _x, float _y) : x(_x), y(_y) {}
};

struct ImVec4 {
	float x = 0.0f, y = 0.0f, z = 0.0f, w = 0.0f;
};

// Opaque pointer types referenced in ui_main.h. Real ImGui defines
// these as full structs; we leave them incomplete so anything that
// tries to dereference them is a compile error in this build.
struct ImGuiContext;
struct ImGuiIO;
struct ImFont;
struct ImDrawData;
struct ImGuiViewport;

// Enum constants the SDL3 frontend headers reference. Values are
// arbitrary — the bridge server never calls anything that uses them.
typedef int ImGuiKey;
typedef int ImGuiFocusedFlags;
typedef int ImGuiWindowFlags;
typedef int ImGuiCond;
enum : int {
	ImGuiKey_Escape = 0,
	ImGuiKey_None   = 0,
	ImGuiFocusedFlags_None              = 0,
	ImGuiFocusedFlags_RootAndChildWindows = 0,
	ImGuiWindowFlags_None               = 0,
	ImGuiCond_Always                    = 0,
	ImGuiCond_Appearing                 = 0,
};

// Stub namespace. The functions are declared but not defined; if any
// inline header function in the SDL3 frontend gets accidentally
// instantiated by code we link in here, the link will fail with an
// undefined-symbol error pointing straight at the offending callsite.
namespace ImGui {
	bool IsWindowFocused(ImGuiFocusedFlags flags = 0);
	bool IsKeyPressed(ImGuiKey key, bool repeat = true);
}
