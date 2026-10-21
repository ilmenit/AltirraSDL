#include "netplay_input_state.h"
#include <cstdio>

using namespace ATNetplay;
using namespace ATNetplayInput;

static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "%d: %s\n", \
	__LINE__, #x); ++failures; } } while (0)

static NetInput Poll(KeyboardCapture& keyboard) {
	NetInput input{};
	keyboard.Poll(input);
	return input;
}

int main() {
	KeyboardCapture keyboard;
	// L has scan code zero. Two consecutive presses are independent.
	keyboard.Press(0);
	keyboard.Press(0);
	for (int i = 0; i < 2; ++i) {
		const auto input = Poll(keyboard);
		CHECK(input.keyScan == 0);
		CHECK(input.keyFlags == kKeyEvent);
	}
	CHECK(Poll(keyboard).keyFlags == 0);

	// The encoded key's modifiers are retained, and modifier-only
	// releases reach the peer without needing another keyboard event.
	keyboard.SetModifiers(true, true);
	keyboard.Press(0x61);
	auto input = Poll(keyboard);
	CHECK(input.keyScan == 0x61 && input.extFlags == 3);
	keyboard.SetModifiers(false, false);
	input = Poll(keyboard);
	CHECK(input.extFlags == 0 && input.keyFlags == 0);

	keyboard.Press(0x21, true);
	CHECK(Poll(keyboard).keyFlags == (kKeyEvent | kKeyRepeat));

	// Raw held keys, including L, survive frames until release. A
	// second key can be held simultaneously; SDL repeat is ignored.
	keyboard.Press(0, false, true);
	keyboard.Press(1, false, true);
	keyboard.Press(1, true, true);
	CHECK(Poll(keyboard).keyHeldMask == 1);
	CHECK(Poll(keyboard).keyHeldMask == 3);
	input = Poll(keyboard);
	CHECK(input.keyHeldMask == 3 && input.keyFlags == 0);
	keyboard.Release(0);
	CHECK(Poll(keyboard).keyHeldMask == 2);
	keyboard.Release(1);
	CHECK(Poll(keyboard).keyHeldMask == 0);
	keyboard.SetModifiers(true, false);
	keyboard.Press(0, false, true);
	keyboard.SetModifiers(false, false);
	keyboard.Release(0);
	input = Poll(keyboard);
	CHECK(input.keyHeldMask == 1 && input.extFlags == 1);
	input = Poll(keyboard);
	CHECK(input.keyHeldMask == 0 && input.extFlags == 0);

	// A quick tap between frame boundaries must not disappear.
	keyboard.Press(0, false, true);
	keyboard.Release(0);
	CHECK(Poll(keyboard).keyHeldMask == 1);
	CHECK(Poll(keyboard).keyHeldMask == 0);
	keyboard.Press(0, false, true);
	keyboard.Release(0);
	keyboard.Press(0, false, true);
	keyboard.Release(0);
	CHECK(Poll(keyboard).keyHeldMask == 1);
	CHECK(Poll(keyboard).keyHeldMask == 0);
	CHECK(Poll(keyboard).keyHeldMask == 1);
	CHECK(Poll(keyboard).keyHeldMask == 0);

	// Focus loss clears pending events, raw keys and modifiers.
	keyboard.Press(7, false, true);
	keyboard.SetModifiers(true, true);
	keyboard.Reset();
	input = Poll(keyboard);
	CHECK(input.keyFlags == 0 && input.keyHeldMask == 0);
	CHECK(input.extFlags == 0);

	// Saving apply bookkeeping for rollback does not rewind live input.
	AppliedState applied{};
	applied.rawKeyMask = uint64_t{1} << 12;
	applied.consoleMask = 3;
	const AppliedState checkpoint = applied;
	keyboard.Press(0);
	applied = {};
	applied = checkpoint;
	CHECK(applied.rawKeyMask == (uint64_t{1} << 12));
	CHECK(applied.consoleMask == 3);
	CHECK(Poll(keyboard).keyFlags == kKeyEvent);

	return failures ? 1 : 0;
}
