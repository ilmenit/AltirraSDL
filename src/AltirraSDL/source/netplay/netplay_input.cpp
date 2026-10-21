// Altirra SDL3 netplay - per-frame input capture + injection (impl)

#include <stdafx.h>

#include "netplay_input.h"

#include "packets.h"  // NetInput

#include "simulator.h"
#include "inputmanager.h"
#include "inputdefs.h"
#include "devicemanager.h"
#include "cpu.h"
#include "input/input_sdl3.h"  // ATInputSDL3_ReleaseAllKeys

#include <at/atcore/deviceport.h>
#include <at/ataudio/pokey.h>
#include <at/atcore/logging.h>

#include "gtia.h"

extern ATLogChannel g_ATLCNetplay;

#include <cstring>
#include <algorithm>
#include "constants.h"

extern ATSimulator g_sim;

namespace ATNetplayInput {

namespace {

// NetInput::stickDir bit assignments (active-high = "pressed"):
constexpr uint8_t kDirUp    = 1 << 0;
constexpr uint8_t kDirDown  = 1 << 1;
constexpr uint8_t kDirLeft  = 1 << 2;
constexpr uint8_t kDirRight = 1 << 3;

// NetInput::buttons bit assignments (active-high):
constexpr uint8_t kBtnTrigger = 1 << 0;
constexpr uint8_t kBtnStart   = 1 << 1;   // GTIA console 0x01
constexpr uint8_t kBtnSelect  = 1 << 2;   // GTIA console 0x02
constexpr uint8_t kBtnOption  = 1 << 3;   // GTIA console 0x04

// NetInput::extFlags bit assignments (active-high):
constexpr uint8_t kExtShift = 1 << 0;
constexpr uint8_t kExtCtrl  = 1 << 1;
constexpr uint8_t kExt5200Top = 1 << 2;

// Per-player owned controller port.  Allocated in BeginSession,
// released in EndSession.
IATDeviceControllerPort *g_port[2] = { nullptr, nullptr };

bool g_active        = false;
bool g_testSession   = false;
bool g_wasRestricted = false;
bool g_wasConsoleCapture = false;

// ---------------------------------------------------------------------------
// Port-1 capture proxy.
//
// During netplay we install this as a redirect target on every
// ATPortInputController bound to port index 0 (port 1 in user-facing
// terms).  All local port-1 input -- arrow keys, user-bound keyboard
// mappings, any attached gamepad (via ATInputManager's standard
// mappings), the Gaming-Mode on-screen touch joypad -- flows through
// the full ATInputManager pipeline, lands on this proxy instead of
// the real hardware port, and is harvested by PollLocal() each frame.
//
// Net effect: on the joiner side, local port-1 input does NOT drive
// the real port 1 -- it is captured, sent across the wire, and shows
// up as the peer's port-2 input after the lockstep delay.  Real port
// 1 on the joiner is driven by what the host sends over the wire.
// ---------------------------------------------------------------------------

class NetplayCapturePort final : public IATDeviceControllerPort {
public:
	void AddRef() override { ++mRefs; }
	// Singleton: never actually freed, but keep the count consistent
	// so debug asserts on refcount balance hold.
	void Release() override { --mRefs; }

	bool IsPotNoiseEnabled() const override { return false; }
	void SetEnabled(bool /*enabled*/) override {}
	void SetDirOutputMask(uint8 /*mask*/) override {}

	void SetDirInput(uint8 mask) override {
		mDirMask = mask;
	}

	void SetDirInputBits(uint8 newSignals, uint8 changeMask) override {
		mDirMask = (mDirMask & ~changeMask) | (newSignals & changeMask);
	}

	void SetOnDirOutputChanged(uint8, vdfunction<void()>, bool /*callNow*/) override {}

	uint8 GetCurrentDirOutput() const override { return 0x0F; }

	void SetTriggerDown(bool down) override {
		mTrigger = down;
	}

	void ResetPotPosition(bool potB) override {
		mPot[potB ? 1 : 0] = ATPokeyEmulator::kPotHiPosUnconnected;
	}
	void SetPotPosition(bool potB, sint32 pos) override {
		SetPotHiresPosition(potB, std::clamp(pos, 0, 228) * 65536,
			false);
	}
	void SetPotHiresPosition(bool potB, sint32 hiPos, bool grounded) override {
		mPot[potB ? 1 : 0] = grounded
			? ATPokeyEmulator::kPotHiPosGrounded
			: std::clamp(hiPos, 1, 228 << 16);
	}
	void Set5200KeyboardState(const bool matrix[64], bool top) override {
		m5200Keys = 0;
		for (unsigned i = 0; i < 64; ++i)
			if (matrix[i]) m5200Keys |= uint64_t{1} << i;
		m5200Top = top;
	}
	uint32 GetCyclesToBeamPosition(int, int) const override { return 1; }

	// Snapshot of the captured state for PollLocal.
	uint8 GetDirMask() const { return mDirMask; }
	bool  GetTriggerDown() const { return mTrigger; }
	uint32_t GetPot(unsigned index) const { return mPot[index]; }
	uint64_t Get5200Keys() const { return m5200Keys; }
	bool Get5200Top() const { return m5200Top; }

	void Reset() {
		mDirMask = 0x0F;  // all high = no direction
		mTrigger = false;
		mPot[0] = mPot[1] = ATPokeyEmulator::kPotHiPosUnconnected;
		m5200Keys = 0;
		m5200Top = false;
	}

private:
	int mRefs = 1;
	uint8 mDirMask = 0x0F;  // Active-low: 0x0F = neutral, bit clear = pressed.
	bool mTrigger = false;
	uint32_t mPot[2] = {ATPokeyEmulator::kPotHiPosUnconnected,
		ATPokeyEmulator::kPotHiPosUnconnected};
	uint64_t m5200Keys = 0;
	bool m5200Top = false;
};

NetplayCapturePort g_capturePort;
NetplayCapturePort g_discardPorts[3];

// Pre-session CPU debug-flag snapshot.  Netplay demands the two peers
// execute the same opcode stream — if one peer has e.g.
// IllegalInsnsEnabled=false it traps on illegal opcodes that the other
// peer executes fine.  On BeginSession we force all three flags to
// gameplay-friendly values and restore the user's prior settings on
// EndSession.
bool g_cpuIllegalSaved = true;
bool g_cpuStopOnBRKSaved = false;
bool g_cpuPathBrkSaved = false;

// Sim-event logger removed: it was a diagnostic aid used to find
// the $01FE HLE-trap round-trip bug.  Keeping it registered adds a
// virtual-dispatch cost to every NotifyEvent (AbnormalDMA fires many
// times per frame for some games — cheap individually, but gratuitous
// per-frame overhead when netplay is active).  The CPU register dump
// in ATEmuErrorHandlerSDL3 covers real trap diagnostics already.

// Capture events are wall-clock input and must never be rolled back.
KeyboardCapture g_keyboard;
// Apply bookkeeping is restored with the emulator checkpoint.
AppliedState g_applied;

void Apply5200Keyboard(int index) {
	bool matrix[64]{};
	bool top = false;
	if (index >= 0 && index < 2) {
		const auto& input = g_applied.inputs[index];
		for (unsigned bit = 0; bit < 64; ++bit)
			matrix[bit] = (input.keyHeldMask & (uint64_t{1} << bit)) != 0;
		top = (input.extFlags & kExt5200Top) != 0;
	}
	auto& pokey = g_sim.GetPokey();
	pokey.SetKeyMatrix(matrix);
	pokey.SetShiftKeyState(top, false);
	pokey.SetControlKeyState(top);
	pokey.SetBreakKeyState(top, false);
}

void ApplyPorts() {
	for (int i = 0; i < 2; ++i) {
		if (!g_port[i]) continue;
		const auto& input = g_applied.inputs[i];
		g_port[i]->SetDirInput(uint8_t(~input.stickDir & 15));
		g_port[i]->SetTriggerDown((input.buttons & kBtnTrigger) != 0);
		for (unsigned pot = 0; pot < 2; ++pot) {
			const uint32_t pos = input.pot[pot];
			if (pos == ATPokeyEmulator::kPotHiPosUnconnected)
				g_port[i]->ResetPotPosition(pot != 0);
			else
				g_port[i]->SetPotHiresPosition(pot != 0,
					(sint32)std::min(pos, uint32_t(228 << 16)),
					pos == ATPokeyEmulator::kPotHiPosGrounded);
		}
	}
}

// Capture-side shadow of the locally-driven console-switch state.
// OnLocalConsoleSwitch sets/clears bits here; PollLocal reads them.
uint8_t g_localConsoleMask = 0;


} // anonymous

void BeginSession() {
	if (g_active) return;

	// Zero out any currently-held local input so we don't leak a
	// pre-session stuck "direction pressed" into the first netplay
	// frame.  This walks ATInputManager's joystick class, POKEY's
	// shift/ctrl/raw keys and GTIA's console switches — exactly the
	// set of things our suppression gate will then block from
	// firing again.
	ATInputSDL3_ReleaseAllKeys();

	if (ATInputManager *im = g_sim.GetInputManager()) {
		// ATInputSDL3_ReleaseAllKeys only clears keyboard-class codes
		// (0..JoyClass-1).  Also release the mouse + joystick classes
		// so a held D-pad or gamepad button at session-start doesn't
		// leak into the normal input manager's port controllers
		// (which merge with ours via the port manager).
		im->ReleaseButtons(kATInputCode_JoyClass, 0xFFFF);

		// Restricted mode silences console triggers, keyboard
		// triggers, paddle pot writes, and any other non-UI input
		// path that would otherwise double-fire locally.  Joystick
		// controllers on port 0 are exempted via the port-redirect
		// escape hatch below (IsTriggerRestricted checks
		// IsRedirected()), so they flow through ATInputManager's
		// full pipeline into our capture proxy.
		g_wasRestricted = im->IsRestrictedMode();
		g_wasConsoleCapture = im->IsConsoleCaptureEnabled();
		im->SetConsoleCaptureEnabled(true);
		im->SetRestrictedMode(true);

		// Capture everything that would write to port 1 (port index 0).
		// This covers ATJoystickController, ATPaddleController's
		// trigger, ATDrivingController, and any other
		// ATPortInputController the user has mapped onto port 1.
		g_capturePort.Reset();
		im->SetPortRedirect(0, &g_capturePort);
		// Other locally mapped controllers must not leak jitter or pot
		// writes into hardware; the two network slots own those ports.
		for (int i = 1; i < 4; ++i) {
			g_discardPorts[i - 1].Reset();
			im->SetPortRedirect(i, &g_discardPorts[i - 1]);
		}
		if (g_sim.GetHardwareMode() == kATHardwareMode_5200)
			im->Set5200ControllerOverride(Apply5200Keyboard);
	}

	// Force CPU debug flags to gameplay-safe values so the two peers
	// don't diverge on a game that uses illegal 6502 opcodes, BRK, or
	// path-trace breakpoints.  Save prior values for EndSession restore.
	{
		ATCPUEmulator &cpu = g_sim.GetCPU();
		g_cpuIllegalSaved   = cpu.AreIllegalInsnsEnabled();
		g_cpuStopOnBRKSaved = cpu.GetStopOnBRK();
		g_cpuPathBrkSaved   = cpu.IsPathBreakEnabled();
		cpu.SetIllegalInsnsEnabled(true);  // games use undocumented ops
		cpu.SetStopOnBRK(false);
		cpu.SetPathBreakEnabled(false);
		g_ATLCNetplay("input: CPU flags forced to gameplay-safe "
			"(was illegal=%d stopOnBRK=%d pathBrk=%d)",
			g_cpuIllegalSaved ? 1 : 0,
			g_cpuStopOnBRKSaved ? 1 : 0,
			g_cpuPathBrkSaved ? 1 : 0);
	}

	AttachEventLogger();

	ATDeviceManager *dm = g_sim.GetDeviceManager();
	if (dm) {
		if (auto *pm = dm->GetService<IATDevicePortManager>()) {
			pm->AllocControllerPort(0, &g_port[0]);
			pm->AllocControllerPort(1, &g_port[1]);
			for (int i = 0; i < 2; ++i) {
				if (g_port[i]) {
					g_port[i]->SetEnabled(true);
					g_port[i]->SetDirInput(0x0F);
					g_port[i]->SetTriggerDown(false);
				}
			}
		}
	}

	g_keyboard.Reset();
	g_applied = {};
	g_localConsoleMask = 0;

	g_active = true;
}

void EndSession() {
	if (!g_active) return;
	g_testSession = false;

	for (int i = 0; i < 2; ++i) {
		if (g_port[i]) {
			g_port[i]->SetDirInput(0x0F);
			g_port[i]->SetTriggerDown(false);
			g_port[i]->ResetPotPosition(false);
			g_port[i]->ResetPotPosition(true);
			g_port[i]->Release();
			g_port[i] = nullptr;
		}
	}

	// Clear any console switches we drove high.
	if (ATGTIAEmulator *gtia = &g_sim.GetGTIA())
		gtia->SetConsoleSwitch(0x07, false);

	if (ATInputManager *im = g_sim.GetInputManager()) {
		im->Set5200ControllerOverride(nullptr);
		for (int i = 0; i < 4; ++i) im->SetPortRedirect(i, nullptr);
		// Release every tracked input code, not just joystick-class:
		// during the session, non-joy input codes (e.g. a keyboard key
		// bound to fire/direction) can have activated mappings for
		// port-writing triggers via the redirect escape hatch. A
		// narrow JoyClass release would leave those mappings with a
		// stale mbTriggerActivated=true, silently swallowing the next
		// fresh press after the session ends.
		im->ReleaseButtons(0, 0xFFFFFFFF);
		im->SetRestrictedMode(g_wasRestricted);
		im->SetConsoleCaptureEnabled(g_wasConsoleCapture);
	}
	g_capturePort.Reset();

	// Restore the user's pre-session CPU debug flags.
	{
		ATCPUEmulator &cpu = g_sim.GetCPU();
		cpu.SetIllegalInsnsEnabled(g_cpuIllegalSaved);
		cpu.SetStopOnBRK(g_cpuStopOnBRKSaved);
		cpu.SetPathBreakEnabled(g_cpuPathBrkSaved);
	}

	DetachEventLogger();
	g_sim.GetPokey().ReleaseAllRawKeys(true);
	g_sim.GetPokey().SetShiftKeyState(false, true);
	g_sim.GetPokey().SetControlKeyState(false);
	ReleaseLocalInputs();
	g_applied = {};
	g_active = false;
}

bool IsActive()                { return g_active; }
bool IsTestSession()           { return g_testSession; }
void BeginTestSession() { BeginSession(); g_testSession = true; }
bool IsSuppressingLocalInput() { return g_active; }

// No-op stubs: the sim-event logger was removed (see comment above).
// Kept in the public API so the call sites in ui_main.cpp /
// BeginSession compile unchanged — they're harmless.
void AttachEventLogger() {}
void DetachEventLogger() {}

AppliedState CaptureAppliedState() {
	auto state = g_applied;
	state.rawKeyMask = g_sim.GetPokey().GetRawKeyMask();
	state.shiftHeld = g_sim.GetPokey().GetShiftKeyState();
	state.ctrlHeld = g_sim.GetPokey().GetControlKeyState();
	state.consoleMask = uint8_t(~g_sim.GetGTIA().GetConsoleSwitchInputState() & 7);
	if (auto* input = g_sim.GetInputManager()) {
		state.selected5200 = input->Get5200ControllerIndex();
		state.pots5200Enabled = input->Are5200PotsEnabled();
	}
	return state;
}

void RestoreAppliedState(const AppliedState& state) {
	// Restore ports before the emulator snapshot restores POKEY/GTIA.
	g_applied = state;
	ApplyPorts();
	auto& pokey = g_sim.GetPokey();
	const uint64 currentKeys = pokey.GetRawKeyMask();
	for (unsigned key = 0; key < 64; ++key) {
		const uint64 bit = uint64_t{1} << key;
		if ((currentKeys & bit) && !(state.rawKeyMask & bit))
			pokey.ReleaseRawKey((uint8)key, false);
		if (state.rawKeyMask & bit) pokey.PushRawKey((uint8)key, false);
	}
	pokey.SetShiftKeyState(state.shiftHeld, true);
	pokey.SetControlKeyState(state.ctrlHeld);
	g_sim.GetGTIA().SetConsoleSwitch(7, false);
	g_sim.GetGTIA().SetConsoleSwitch(state.consoleMask, true);
	// Preserve the selected port even when the snapshot will switch hardware
	// from XL back to 5200. Selection bookkeeping is hardware independent;
	// the following core restore replaces any temporary chip side effects.
	if (auto* input = g_sim.GetInputManager())
		input->Select5200Controller(state.selected5200,
			state.pots5200Enabled);
}

void OnLocalKeyDown(uint8_t scan, bool repeat, bool raw) {
	if (g_active) g_keyboard.Press(scan, repeat, raw);
}

void OnLocalKeyUp(uint8_t scan) {
	if (g_active) g_keyboard.Release(scan);
}

void ReleaseLocalInputs() {
	g_keyboard.Reset();
	g_localConsoleMask = 0;
	g_capturePort.Reset();
}

void OnLocalShiftCtrlState(bool shift, bool ctrl) {
	if (g_active) g_keyboard.SetModifiers(shift, ctrl);
}

void OnLocalConsoleSwitch(uint8_t switchMask, bool down) {
	if (!g_active) return;
	if (down) g_localConsoleMask |=  (switchMask & 0x07);
	else      g_localConsoleMask &= ~(switchMask & 0x07);
}

ATNetplay::NetInput PollLocal() {
	ATNetplay::NetInput in{};
	if (!g_active) return in;

	// --- stick + trigger: read the port-1 capture proxy ---
	// ATInputManager has already translated keyboard, gamepad, touch
	// joypad, and user-bound custom mappings through its full trigger
	// pipeline and deposited the resulting dir/trigger state on
	// g_capturePort.  We just harvest it here.  The proxy uses the
	// Atari hardware convention: active-low dir mask (bit clear =
	// direction held), 4 bits (U/D/L/R in PIA ordering).
	{
		const uint8 dir = g_capturePort.GetDirMask();
		if (!(dir & 0x01)) in.stickDir |= kDirUp;     // bit 0 = up
		if (!(dir & 0x02)) in.stickDir |= kDirDown;   // bit 1 = down
		if (!(dir & 0x04)) in.stickDir |= kDirLeft;   // bit 2 = left
		if (!(dir & 0x08)) in.stickDir |= kDirRight;  // bit 3 = right
		if (g_capturePort.GetTriggerDown()) in.buttons |= kBtnTrigger;
	}

	// --- console switches: latched shadow set by OnLocalConsoleSwitch ---
	if (g_localConsoleMask & 0x01) in.buttons |= kBtnStart;
	if (g_localConsoleMask & 0x02) in.buttons |= kBtnSelect;
	if (g_localConsoleMask & 0x04) in.buttons |= kBtnOption;

	// Port writes include paddle fire and driving-controller quadrature;
	// preserve their exact direction bits instead of normalizing them.
	in.pot[0] = g_capturePort.GetPot(0);
	in.pot[1] = g_capturePort.GetPot(1);
	g_keyboard.Poll(in);
	if (g_sim.GetHardwareMode() == kATHardwareMode_5200) {
		in.keyHeldMask = g_capturePort.Get5200Keys();
		if (g_capturePort.Get5200Top()) in.extFlags |= kExt5200Top;
	}

	return in;
}

void ApplyFrameInputs(const ATNetplay::NetInput& p1,
                      const ATNetplay::NetInput& p2) {
	if (!g_active) return;

	const ATNetplay::NetInput *inputs[2] = { &p1, &p2 };

	g_applied.inputs[0] = p1;
	g_applied.inputs[1] = p2;
	ApplyPorts();

	// Console switches — OR both players.  On the Atari these are
	// shared physical switches on the machine, so either player
	// holding them is the same as both.
	uint8_t consoleMask = 0;
	auto mapConsole = [&](uint8_t b) {
		uint8_t m = 0;
		if (b & kBtnStart)  m |= 0x01;
		if (b & kBtnSelect) m |= 0x02;
		if (b & kBtnOption) m |= 0x04;
		return m;
	};
	consoleMask |= mapConsole(p1.buttons);
	consoleMask |= mapConsole(p2.buttons);
	if (consoleMask != g_applied.consoleMask) {
		ATGTIAEmulator *gtia = &g_sim.GetGTIA();
		uint8_t changed = consoleMask ^ g_applied.consoleMask;
		uint8_t down    = consoleMask & changed;
		uint8_t up      = ~consoleMask & changed;
		if (down) gtia->SetConsoleSwitch(down, true);
		if (up)   gtia->SetConsoleSwitch(up,   false);
		g_applied.consoleMask = consoleMask;
	}

	if (g_sim.GetHardwareMode() == kATHardwareMode_5200) {
		if (auto* input = g_sim.GetInputManager())
			Apply5200Keyboard(input->Get5200ControllerIndex());
		return;
	}

	// Modifier release is state, not a keypress: apply it every frame.
	ATPokeyEmulator& pokey = g_sim.GetPokey();
	pokey.SetShiftKeyState(((p1.extFlags | p2.extFlags) & kExtShift) != 0,
		true);
	pokey.SetControlKeyState(((p1.extFlags | p2.extFlags) & kExtCtrl) != 0);

	const uint64_t rawMask = p1.keyHeldMask | p2.keyHeldMask;
	for (unsigned key = 0; key < 64; ++key) {
		const uint64_t bit = uint64_t{1} << key;
		if ((g_applied.rawKeyMask & bit) && !(rawMask & bit))
			pokey.ReleaseRawKey((uint8_t)key, false);
		// Reassert held keys after any cooked event switched modes.
		if (rawMask & bit) pokey.PushRawKey((uint8_t)key, false);
	}
	g_applied.rawKeyMask = rawMask;
	for (const auto* input : inputs) {
		if ((input->keyFlags & (kKeyEvent | kKeyRaw)) == kKeyEvent)
			pokey.PushKey(input->keyScan,
				(input->keyFlags & kKeyRepeat) != 0);
	}

}

// ---------------------------------------------------------------------------
// Routing helpers — for code paths that bypass ATInputManager
// (touch overlay, on-screen keyboard).
// ---------------------------------------------------------------------------

void RouteConsoleSwitch(ATGTIAEmulator* gtia, uint8_t bit, bool down) {
	if (g_active) {
		// In-session: feed the lockstep pipeline; the apply step on both
		// peers will drive GTIA at the deterministic frame.  Don't write
		// GTIA here or the local sim races ahead of the peer.
		OnLocalConsoleSwitch(bit, down);
		return;
	}
	if (gtia) gtia->SetConsoleSwitch(bit, down);
}

void RouteRawKeyDown(ATPokeyEmulator* pokey, uint8_t scanCode,
                     bool naturalKb) {
	if (g_active) {
		// Just queue the bare scancode.  Shift/ctrl state is owned by
		// whoever manages modifier press/release (physical-keyboard
		// path at input_sdl3.cpp:858-877, on-screen keyboard's
		// ApplyModifierState) — they call OnLocalShiftCtrlState
		// independently so the latest modifier mask reflects every
		// modifier source.  Reading POKEY here would be wrong: in
		// netplay mode POKEY's shift register is intentionally NOT
		// kept in sync with the local user's modifier state, since
		// modifiers are applied at the lockstep apply step instead.
		OnLocalKeyDown(scanCode | (g_keyboard.Modifiers() << 6));
		return;
	}
	if (pokey) pokey->PushRawKey(scanCode, naturalKb);
}

bool ShouldSuppressWarmReset() { return g_active; }
bool ShouldSuppressBreak()     { return g_active; }

} // namespace ATNetplayInput
