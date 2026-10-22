// Per-session joystick wiring, chosen by the host for each game.
#pragma once

#include <cstdint>

namespace ATNetplay {

enum class ControllerLayout : uint8_t {
	SeparateJoysticks = 0,
	SharedJoystick = 1,
};

constexpr bool IsValidControllerLayout(ControllerLayout layout) {
	return layout == ControllerLayout::SeparateJoysticks
		|| layout == ControllerLayout::SharedJoystick;
}

} // namespace ATNetplay
