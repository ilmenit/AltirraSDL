//	AltirraSDL - Windows command icon atlas for ImGui controls.

#pragma once

#include <imgui.h>

namespace ATCommandIcons {

struct Icon {
	ImTextureID mTexID = (ImTextureID)0;
	ImVec2 mUV0 = ImVec2(0, 0);
	ImVec2 mUV1 = ImVec2(1, 1);
	int mWidth = 0;
	int mHeight = 0;
};

bool Get(const char *name, Icon& icon);
void Shutdown();

}
