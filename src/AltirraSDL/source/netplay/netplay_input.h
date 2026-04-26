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

class ATGTIAEmulator;
class ATPokeyEmulator;

namespace ATNetplay { struct NetInput; }

#ifdef ALTIRRA_NETPLAY_ENABLED

namespace ATNetplayInput {

// Begin/end tied to the aggregate IsLockstepping state in
// netplay_glue.  Safe to call multiple times.
void BeginSession();
void EndSession();
bool IsActive();

// Attach / detach the diagnostic sim-event logger independently of
// BeginSession.  The main loop uses BeginSession only when
// lockstep is actually active — but the interesting events often
// fire BEFORE that (during the joiner's first Advance right after
// ApplySnapshot, or the host's drain loop).  Call
// AttachEventLogger earlier in those paths so the first bad event
// gets captured.  Detach is automatic on EndSession too.
void AttachEventLogger();
void DetachEventLogger();

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

// -----------------------------------------------------------------------
// Routing helpers for code paths that bypass ATInputManager.
//
// ATInputManager's restricted-mode gate (set during BeginSession) silences
// console / keyboard / flag triggers from gamepads + custom-bound keys so
// they don't double-fire alongside the lockstep pipeline.  But on-screen
// touch controls and the on-screen virtual keyboard write to GTIA / POKEY
// / ATSimulator directly — the input manager never sees them, so its
// gate doesn't apply.  These helpers encapsulate the
// "if a session is live, route through OnLocalConsoleSwitch /
// OnLocalKeyDown; otherwise drive the hardware directly" pattern that
// input_sdl3.cpp's keyboard path already follows.
// -----------------------------------------------------------------------

// Console-switch edge (Start=0x01, Select=0x02, Option=0x04).
void RouteConsoleSwitch(ATGTIAEmulator* gtia, uint8_t bit, bool down);

// Raw POKEY scancode press.  Outside a session, calls
// pokey->PushRawKey(scanCode, naturalKb) verbatim.  In-session, queues
// the bare scancode for the next NetInput.keyScan.  The caller is
// responsible for keeping shift/ctrl state in sync via
// OnLocalShiftCtrlState — same convention as the physical-keyboard
// path at input_sdl3.cpp:858-877.  Pass scanCode WITHOUT the
// shift/ctrl high-bit folding (no +0x40 / +0x80) when the helper is
// used inside a session; the apply side does the folding via
// PushKey + SetShiftKeyState.
void RouteRawKeyDown(ATPokeyEmulator* pokey, uint8_t scanCode,
                     bool naturalKb);

// Hardware actions with no defined lockstep encoding — silenced during
// a session.  Callers should check the return value and skip the
// underlying call when these return true.
bool ShouldSuppressWarmReset();
bool ShouldSuppressBreak();

} // namespace ATNetplayInput

#else // !ALTIRRA_NETPLAY_ENABLED

// WASM build: netplay is compiled out, so the routing helpers fall
// through to the direct hardware path.  Inlined so the compiler can
// fold them away at the call site, matching the no-netplay-stub
// approach used by netplay_glue.h.

#include "gtia.h"
#include <at/ataudio/pokey.h>

namespace ATNetplayInput {

inline bool IsActive()                { return false; }
inline bool IsSuppressingLocalInput() { return false; }

inline void OnLocalKeyDown(uint8_t)        {}
inline void OnLocalShiftCtrlState(bool, bool) {}
inline void OnLocalConsoleSwitch(uint8_t, bool) {}

inline void RouteConsoleSwitch(ATGTIAEmulator* gtia, uint8_t bit, bool down) {
    if (gtia) gtia->SetConsoleSwitch(bit, down);
}

inline void RouteRawKeyDown(ATPokeyEmulator* pokey, uint8_t scanCode,
                            bool naturalKb) {
    if (pokey) pokey->PushRawKey(scanCode, naturalKb);
}

inline bool ShouldSuppressWarmReset() { return false; }
inline bool ShouldSuppressBreak()     { return false; }

} // namespace ATNetplayInput

#endif // ALTIRRA_NETPLAY_ENABLED
