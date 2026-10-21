// Frontend keyboard state kept separate from the emulated POKEY state.
#pragma once

#include "packets.h"
#include <array>
#include <cstddef>

namespace ATNetplayInput {

constexpr uint8_t kKeyEvent = 1;
constexpr uint8_t kKeyRepeat = 2;
constexpr uint8_t kKeyRaw = 4;

// A zero scan code is the Atari L key, not an empty event. The explicit
// event flag also lets adjacent identical presses remain distinct.
class KeyboardCapture {
public:
	void Reset() { *this = KeyboardCapture{}; }
	void SetModifiers(bool shift, bool ctrl) {
		mModifiers = (shift ? 1 : 0) | (ctrl ? 2 : 0);
	}
	uint8_t Modifiers() const { return mModifiers; }
	void Press(uint8_t scan, bool repeat = false, bool raw = false) {
		if (raw) {
			if (repeat) return;
			mLiveHeldMask |= uint64_t{1} << (scan & 63);
		}
		Enqueue({
			scan, uint8_t(kKeyEvent | (repeat ? kKeyRepeat : 0)
				| (raw ? kKeyRaw : 0)), mModifiers});
	}
	void Release(uint8_t scan) {
		const uint64_t bit = uint64_t{1} << (scan & 63);
		if (!(mLiveHeldMask & bit)) return;
		mLiveHeldMask &= ~bit;
		Enqueue({scan, kKeyRaw, mModifiers});
	}
	void Poll(ATNetplay::NetInput& input) {
		input.extFlags = (input.extFlags & ~3u) | mModifiers;
		if (mCount) {
			const Event event = mEvents[mHead];
			input.keyScan = event.scan;
			input.keyFlags = event.flags;
			input.extFlags = (input.extFlags & ~3u) | event.modifiers;
			// Queue releases too: rapid identical taps need a frame with
			// the key up between them, rather than becoming one long hold.
			if (event.flags & kKeyRaw) {
				const uint64_t bit = uint64_t{1} << (event.scan & 63);
				if (event.flags & kKeyEvent) mHeldMask |= bit;
				else mHeldMask &= ~bit;
			}
			mHead = (mHead + 1) % mEvents.size();
			--mCount;
		}
		input.keyHeldMask = mHeldMask;
	}
private:
	struct Event {
		uint8_t scan = 0;
		uint8_t flags = 0;
		uint8_t modifiers = 0;
	};
	void Enqueue(Event event) {
		if (mCount == mEvents.size()) {
			// On overflow resynchronize the held matrix; dropping only a
			// queued release could otherwise leave a key stuck forever.
			mHead = mCount = 0;
			mHeldMask = mLiveHeldMask;
		}
		mEvents[(mHead + mCount) % mEvents.size()] = event;
		++mCount;
	}
	std::array<Event, 64> mEvents{};
	size_t mHead = 0;
	size_t mCount = 0;
	uint64_t mHeldMask = 0;
	uint64_t mLiveHeldMask = 0;
	uint8_t mModifiers = 0;
};

// Restored with a rollback checkpoint; live keyboard events stay queued.
struct AppliedState {
	uint64_t rawKeyMask = 0;
	uint8_t consoleMask = 0;
	ATNetplay::NetInput inputs[2]{};
	int selected5200 = 0;
	bool pots5200Enabled = false;
	bool shiftHeld = false;
	bool ctrlHeld = false;
};

} // namespace ATNetplayInput
