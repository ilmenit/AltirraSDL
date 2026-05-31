//	AltirraSDL - Mobile UI: gamepad navigation
//	See mobile_gamepad.h for the contract.

#include <stdafx.h>
#include <SDL3/SDL.h>
#include <imgui.h>

#include "ui_mobile.h"
#include "simulator.h"
#include "inputmanager.h"
#include "netplay/netplay_input.h"
#include "mobile_gamepad.h"
#include "mobile_internal.h"

namespace {
	bool s_inited      = false;
	bool s_uiOwning    = false;
	bool s_startHeld   = false;
	bool s_selectHeld  = false;
	bool s_l1Consumed  = false;
	bool s_r1Consumed  = false;

	void SetConsoleSwitch(ATSimulator& sim, uint8 bit, bool down) {
		ATNetplayInput::RouteConsoleSwitch(&sim.GetGTIA(), bit, down);
	}

	void SetHeldConsoleSwitch(ATSimulator& sim, uint8 bit, bool down, bool& held) {
		if (held == down)
			return;

		held = down;
		SetConsoleSwitch(sim, bit, down);
	}

	void TogglePause(ATSimulator& sim) {
		if (sim.IsPaused())
			sim.Resume();
		else
			sim.Pause();
	}

	void ReleaseGamepadMappedInput(ATSimulator& sim) {
		if (ATInputManager *im = sim.GetInputManager())
			im->ReleaseButtons(kATInputCode_JoyClass, 0xFFFF);
	}
}

void ATMobileGamepad_Init() {
	if (s_inited)
		return;
	s_inited = true;

	ImGuiIO &io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

	// NavHighlight colour is now rebound from ATUIApplyTheme() so a
	// runtime theme switch updates the focus ring colour.  Only the
	// config-flag wiring happens here.
}

void ATMobileGamepad_SetUIOwning(bool owning) {
	s_uiOwning = owning;
}

bool ATMobileGamepad_IsUIOwning() {
	return s_uiOwning;
}

bool ATMobileGamepad_IsReservedButton(int sdlGamepadButton) {
	return sdlGamepadButton == SDL_GAMEPAD_BUTTON_START
	    || sdlGamepadButton == SDL_GAMEPAD_BUTTON_BACK
		|| sdlGamepadButton == SDL_GAMEPAD_BUTTON_LEFT_SHOULDER
		|| sdlGamepadButton == SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER;
}

bool ATMobileGamepad_HandleEvent(const SDL_Event &ev,
	ATSimulator &sim, ATMobileUIState &mobileState)
{
	if (ev.type != SDL_EVENT_GAMEPAD_BUTTON_DOWN
		&& ev.type != SDL_EVENT_GAMEPAD_BUTTON_UP)
		return false;

	const int btn = ev.gbutton.button;
	const bool down = ev.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN;

	if (btn == SDL_GAMEPAD_BUTTON_START) {
		SetHeldConsoleSwitch(sim, 0x01, down, s_startHeld);
		return true;
	}

	if (btn == SDL_GAMEPAD_BUTTON_BACK) {
		SetHeldConsoleSwitch(sim, 0x02, down, s_selectHeld);
		return true;
	}

	if (btn == SDL_GAMEPAD_BUTTON_LEFT_SHOULDER) {
		if (!down) {
			const bool consumed = s_l1Consumed;
			s_l1Consumed = false;
			return consumed;
		}

		if (mobileState.currentScreen == ATMobileUIScreen::None) {
			ReleaseGamepadMappedInput(sim);
			ATMobileUI_OpenMenu(sim, mobileState);
			s_uiOwning = true;
			s_l1Consumed = true;
			return true;
		}

		return false;
	}

	if (btn == SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER) {
		if (!down) {
			const bool consumed = s_r1Consumed;
			s_r1Consumed = false;
			return consumed;
		}

		if (mobileState.currentScreen != ATMobileUIScreen::None)
			return false;

		TogglePause(sim);
		s_r1Consumed = true;
		return true;
	}

	if (!down)
		return false;

	return false;
}

void ATMobileGamepad_ReleaseAll(ATSimulator &sim) {
	if (s_startHeld)
		SetHeldConsoleSwitch(sim, 0x01, false, s_startHeld);
	if (s_selectHeld)
		SetHeldConsoleSwitch(sim, 0x02, false, s_selectHeld);
	s_l1Consumed = false;
	s_r1Consumed = false;
}
