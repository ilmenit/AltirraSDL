//	AltirraSDL - Online Play emote picker.
//
//	4x4 grid of icon buttons.  Opened by F6 (desktop), R3 (gamepad),
//	or the touch button in Gaming Mode.  Closes on pick / Esc / B /
//	click-outside.  Works identically in Desktop and Gaming Mode — the
//	only difference is button size (larger in Gaming Mode for touch).

#pragma once

#ifdef ALTIRRA_NETPLAY_ENABLED

namespace ATEmotePicker {

// Toggle the picker.  No-op if netplay is not lockstepping or the
// "Send emotes" toggle is off — the picker is pointless without a
// peer to send to.
void Open();

// Force-close the picker (e.g. on netplay teardown).
void Close();

bool IsOpen();

// Render the picker if open.  Call once per frame from the main UI
// pass (after the menu bar, before tool windows).  Consumes the F6
// key so it can't retrigger itself when focused.
void Render();

} // namespace ATEmotePicker

#else // !ALTIRRA_NETPLAY_ENABLED

namespace ATEmotePicker {
    inline void Open()      {}
    inline void Close()     {}
    inline bool IsOpen()    { return false; }
    inline void Render()    {}
} // namespace ATEmotePicker

#endif // ALTIRRA_NETPLAY_ENABLED
