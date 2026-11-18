// Adapt the SDL backend's controller polling for Gaming Mode navigation.
#include <SDL3/SDL_gamepad.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <cstdlib>

namespace {
	bool s_useGamingNavigation = false;

	bool ReadNavigationButton(SDL_Gamepad *gamepad, SDL_GamepadButton button) {
		const bool pressed = SDL_GetGamepadButton(gamepad, button);
		if (!s_useGamingNavigation || pressed
			|| button < SDL_GAMEPAD_BUTTON_DPAD_UP
			|| button > SDL_GAMEPAD_BUTTON_DPAD_RIGHT)
			return pressed;

		// Prefer an explicit D-pad press over any simultaneous stick input.
		if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_DPAD_UP)
			|| SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_DPAD_DOWN)
			|| SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_DPAD_LEFT)
			|| SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT))
			return false;

		const int x = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTX);
		const int y = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTY);
		constexpr int kNavigationThreshold = 16384;
		if (std::abs(x) > std::abs(y))
			return (x < -kNavigationThreshold && button == SDL_GAMEPAD_BUTTON_DPAD_LEFT)
				|| (x > kNavigationThreshold && button == SDL_GAMEPAD_BUTTON_DPAD_RIGHT);

		return (y < -kNavigationThreshold && button == SDL_GAMEPAD_BUTTON_DPAD_UP)
			|| (y > kNavigationThreshold && button == SDL_GAMEPAD_BUTTON_DPAD_DOWN);
	}

	Sint16 ReadNavigationAxis(SDL_Gamepad *gamepad, SDL_GamepadAxis axis) {
		// ImGui uses its LStick inputs for proportional scrolling.
		if (s_useGamingNavigation) {
			if (axis == SDL_GAMEPAD_AXIS_LEFTX)
				axis = SDL_GAMEPAD_AXIS_RIGHTX;
			else if (axis == SDL_GAMEPAD_AXIS_LEFTY)
				axis = SDL_GAMEPAD_AXIS_RIGHTY;
		}
		return SDL_GetGamepadAxis(gamepad, axis);
	}
}

void ATImGuiSDL3NewFrame(bool enabled) {
	s_useGamingNavigation = enabled;
	ImGuiIO &io = ImGui::GetIO();
	const bool hadGamepad = (io.BackendFlags & ImGuiBackendFlags_HasGamepad) != 0;
	ImGui_ImplSDL3_NewFrame();
	if (hadGamepad && !(io.BackendFlags & ImGuiBackendFlags_HasGamepad)) {
		// The SDL backend stops polling immediately on disconnect.
		for (int key = ImGuiKey_GamepadStart; key <= ImGuiKey_GamepadRStickDown; ++key)
			io.AddKeyEvent((ImGuiKey)key, false);
	}
}

// Scope the remap to ImGui so gameplay, binding capture and the virtual
// keyboard still see raw SDL input. Keep the backend's gamepad lifetime,
// key transitions and selection of the first connected controller.
#define SDL_GetGamepadButton ReadNavigationButton
#define SDL_GetGamepadAxis ReadNavigationAxis
#include <imgui_impl_sdl3.cpp>
#undef SDL_GetGamepadAxis
#undef SDL_GetGamepadButton
