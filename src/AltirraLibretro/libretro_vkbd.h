#pragma once

#include <cstddef>
#include <cstdint>

struct ATLibretroVkbdEvent {
	enum Type {
		kNone,
		kKey,
		kConsoleStart,
		kConsoleSelect,
		kConsoleOption,
		kWarmReset,
		kColdReset,
		k5200Key
	};

	Type type = kNone;
	unsigned keycode = 0;
	uint32_t trigger = 0;
	uint32_t character = 0;
	bool shift = false;
	bool ctrl = false;
};

void ATLibretroVkbdReset();
void ATLibretroVkbdSetOpen(bool open);
bool ATLibretroVkbdIsOpen();
bool ATLibretroVkbdUpdate(uint16_t joypadState, uint16_t toggleButtonMask,
	bool toggleSelectR2, bool is5200Mode, ATLibretroVkbdEvent& event);
void ATLibretroVkbdRenderXrgb8888(void *data, int w, int h, ptrdiff_t pitch,
	bool is5200Mode);
