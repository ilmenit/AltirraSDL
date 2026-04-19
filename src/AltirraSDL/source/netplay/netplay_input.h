// Altirra SDL3 netplay - per-frame input capture + injection
//
// Captures local user input (stick + trigger via polling, keyboard
// scancodes + console switches via hooks from input_sdl3) and packs
// it into a NetInput each frame.  On the apply side, drives two
// dedicated device controller ports with both players' stick+trigger,
// PushKey's the peer's keyboard scancode, and sets console switches.
//
// While a session is active IsSuppressingLocalInput() returns true —
// input_sdl3 and joystick_sdl3 honour this to skip the normal direct
// paths to POKEY / GTIA / the default port controllers, so local
// input only reaches the sim via the lockstep pipeline (delayed by D
// frames, mirrored to the peer).

#pragma once

#include <cstdint>

namespace ATNetplay { struct NetInput; }

namespace ATNetplayInput {

// Begin/end tied to the aggregate IsLockstepping state in
// netplay_glue.  Safe to call multiple times.
void BeginSession();
void EndSession();
bool IsActive();

// True while BeginSession has been called and EndSession hasn't.
// Consumed by input_sdl3 / joystick_sdl3 to gate their normal paths.
bool IsSuppressingLocalInput();

// Called from input_sdl3 HandleKeyDown *before* it pushes to POKEY.
// Queues one edge-triggered key event for the next PollLocal().
// Auto-repeats are dropped (only the initial press goes on the wire).
void OnLocalKeyDown(uint8_t atariScanCode);

// Called from input_sdl3 on Shift / Ctrl state changes.  Latched into
// the next PollLocal()'s keyScan high bits, same encoding POKEY uses.
void OnLocalShiftCtrlState(bool shift, bool ctrl);

// Called from input_sdl3 HandleSpecialScanCode for Start / Select /
// Option edges.  Mask uses the GTIA console-switch bit assignments
// (0x01 = Start, 0x02 = Select, 0x04 = Option).
void OnLocalConsoleSwitch(uint8_t switchMask, bool down);

// Poll SDL for current local stick + trigger + console-switch state
// and drain the key-event queue into one NetInput.  Safe to call when
// IsActive() is false (returns a zeroed NetInput).
ATNetplay::NetInput PollLocal();

// Drive the simulator from both peers' inputs for the current frame:
//   - controller port 0 ← p1 stick+trigger
//   - controller port 1 ← p2 stick+trigger
//   - GTIA console switches ← OR of p1 and p2 console bits
//   - POKEY PushKey ← whichever peer's keyScan changed this frame
//                     (edge-triggered, so held keys don't repeat)
void ApplyFrameInputs(const ATNetplay::NetInput& p1,
                      const ATNetplay::NetInput& p2);

} // namespace ATNetplayInput
