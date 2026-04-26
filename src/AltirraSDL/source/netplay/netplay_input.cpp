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

// Per-player owned controller port.  Allocated in BeginSession,
// released in EndSession.
IATDeviceControllerPort *g_port[2] = { nullptr, nullptr };

bool g_active        = false;
bool g_wasRestricted = false;

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

	void ResetPotPosition(bool /*potB*/) override {}
	void SetPotPosition(bool /*potB*/, sint32 /*pos*/) override {}
	void SetPotHiresPosition(bool /*potB*/, sint32 /*hiPos*/, bool /*grounded*/) override {}
	uint32 GetCyclesToBeamPosition(int, int) const override { return 1; }

	// Snapshot of the captured state for PollLocal.
	uint8 GetDirMask() const { return mDirMask; }
	bool  GetTriggerDown() const { return mTrigger; }

	void Reset() {
		mDirMask = 0x0F;  // all high = no direction
		mTrigger = false;
	}

private:
	int mRefs = 1;
	uint8 mDirMask = 0x0F;  // Active-low: 0x0F = neutral, bit clear = pressed.
	bool mTrigger = false;
};

NetplayCapturePort g_capturePort;

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

// Capture-side event state.
struct KeyEvent { uint8_t scan; };
constexpr int kKeyQueueCap = 16;
KeyEvent g_keyQueue[kKeyQueueCap] = {};
int      g_keyHead = 0;  // read index
int      g_keyTail = 0;  // write index
int      g_keyCount = 0;
bool     g_shiftHeld = false;
bool     g_ctrlHeld  = false;

// Apply-side edge tracking for console switches.  We hold a "last
// applied" mask per port so we only toggle GTIA when the aggregate
// mask actually changes.
uint8_t g_lastConsoleMask = 0;

// Apply-side edge tracking for keyScan so we don't re-push a held
// key every frame (PushKey is transient; repeat=true only makes sense
// when we know the key is still down AND the host layer is queueing
// auto-repeats, which we aren't).  Two separate trackers because p1
// and p2 are independent keyboards.
uint8_t g_lastKeyScan[2] = { 0, 0 };

// Capture-side shadow of the locally-driven console-switch state.
// OnLocalConsoleSwitch sets/clears bits here; PollLocal reads them.
uint8_t g_localConsoleMask = 0;

void ResetKeyQueue() {
	g_keyHead = g_keyTail = g_keyCount = 0;
	g_shiftHeld = g_ctrlHeld = false;
}

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
		g_wasRestricted = false;  // documented default (see input_sdl3.cpp:329)
		im->SetRestrictedMode(true);

		// Capture everything that would write to port 1 (port index 0).
		// This covers ATJoystickController, ATPaddleController's
		// trigger, ATDrivingController, and any other
		// ATPortInputController the user has mapped onto port 1.
		g_capturePort.Reset();
		im->SetPortRedirect(0, &g_capturePort);
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

	ResetKeyQueue();
	g_lastConsoleMask = 0;
	g_localConsoleMask = 0;
	g_lastKeyScan[0] = g_lastKeyScan[1] = 0;
	g_active = true;
}

void EndSession() {
	if (!g_active) return;

	for (int i = 0; i < 2; ++i) {
		if (g_port[i]) {
			g_port[i]->SetDirInput(0x0F);
			g_port[i]->SetTriggerDown(false);
			g_port[i]->Release();
			g_port[i] = nullptr;
		}
	}

	// Clear any console switches we drove high.
	if (ATGTIAEmulator *gtia = &g_sim.GetGTIA())
		gtia->SetConsoleSwitch(0x07, false);

	if (ATInputManager *im = g_sim.GetInputManager()) {
		im->SetPortRedirect(0, nullptr);
		// Release every tracked input code, not just joystick-class:
		// during the session, non-joy input codes (e.g. a keyboard key
		// bound to fire/direction) can have activated mappings for
		// port-writing triggers via the redirect escape hatch. A
		// narrow JoyClass release would leave those mappings with a
		// stale mbTriggerActivated=true, silently swallowing the next
		// fresh press after the session ends.
		im->ReleaseButtons(0, 0xFFFFFFFF);
		im->SetRestrictedMode(g_wasRestricted);
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

	ResetKeyQueue();
	g_active = false;
}

bool IsActive()                { return g_active; }
bool IsSuppressingLocalInput() { return g_active; }

// No-op stubs: the sim-event logger was removed (see comment above).
// Kept in the public API so the call sites in ui_main.cpp /
// BeginSession compile unchanged — they're harmless.
void AttachEventLogger() {}
void DetachEventLogger() {}

void OnLocalKeyDown(uint8_t atariScanCode) {
	if (!g_active) return;
	if (atariScanCode == 0) return;
	if (g_keyCount >= kKeyQueueCap) {
		// Queue full — drop oldest to make room (favours freshest
		// keypresses, which is what the user actually sees on screen).
		g_keyHead = (g_keyHead + 1) & (kKeyQueueCap - 1);
		--g_keyCount;
	}
	g_keyQueue[g_keyTail].scan = atariScanCode;
	g_keyTail = (g_keyTail + 1) & (kKeyQueueCap - 1);
	++g_keyCount;
}

void OnLocalShiftCtrlState(bool shift, bool ctrl) {
	if (!g_active) return;
	g_shiftHeld = shift;
	g_ctrlHeld  = ctrl;
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

	// --- diagonal normalisation ---
	if ((in.stickDir & (kDirUp | kDirDown)) == (kDirUp | kDirDown))
		in.stickDir &= ~(kDirUp | kDirDown);
	if ((in.stickDir & (kDirLeft | kDirRight)) == (kDirLeft | kDirRight))
		in.stickDir &= ~(kDirLeft | kDirRight);

	// --- keyboard (event-queued, one per frame) ---
	if (g_keyCount > 0) {
		in.keyScan = g_keyQueue[g_keyHead].scan;
		g_keyHead = (g_keyHead + 1) & (kKeyQueueCap - 1);
		--g_keyCount;
	}
	if (g_shiftHeld) in.extFlags |= kExtShift;
	if (g_ctrlHeld)  in.extFlags |= kExtCtrl;

	return in;
}

void ApplyFrameInputs(const ATNetplay::NetInput& p1,
                      const ATNetplay::NetInput& p2) {
	if (!g_active) return;

	const ATNetplay::NetInput *inputs[2] = { &p1, &p2 };

	// Stick + trigger per-port.
	for (int i = 0; i < 2; ++i) {
		if (!g_port[i]) continue;
		const ATNetplay::NetInput& in = *inputs[i];
		uint8_t dirMask = 0x0F;
		if (in.stickDir & kDirUp)    dirMask &= ~0x01;
		if (in.stickDir & kDirDown)  dirMask &= ~0x02;
		if (in.stickDir & kDirLeft)  dirMask &= ~0x04;
		if (in.stickDir & kDirRight) dirMask &= ~0x08;
		g_port[i]->SetDirInput(dirMask);
		g_port[i]->SetTriggerDown((in.buttons & kBtnTrigger) != 0);
	}

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
	if (consoleMask != g_lastConsoleMask) {
		ATGTIAEmulator *gtia = &g_sim.GetGTIA();
		uint8_t changed = consoleMask ^ g_lastConsoleMask;
		uint8_t down    = consoleMask & changed;
		uint8_t up      = ~consoleMask & changed;
		if (down) gtia->SetConsoleSwitch(down, true);
		if (up)   gtia->SetConsoleSwitch(up,   false);
		g_lastConsoleMask = consoleMask;
	}

	// POKEY keyboard — edge-triggered per peer.  PushKey is transient
	// (one character appears in the input queue); we only push when
	// keyScan transitions 0→N or N→M, to avoid one key fire per
	// frame while held.
	ATPokeyEmulator& pokey = g_sim.GetPokey();
	for (int i = 0; i < 2; ++i) {
		const uint8_t k = inputs[i]->keyScan;
		if (k != 0 && k != g_lastKeyScan[i]) {
			// Apply shift/ctrl state first so PushKey's latched
			// scan code sees the right modifiers.  This is a
			// shared POKEY register — we write the OR of both
			// players' modifier state so neither side loses
			// modifiers to the other.
			const bool shift =
				(p1.extFlags & kExtShift) || (p2.extFlags & kExtShift);
			const bool ctrl =
				(p1.extFlags & kExtCtrl)  || (p2.extFlags & kExtCtrl);
			pokey.SetShiftKeyState(shift, true);
			pokey.SetControlKeyState(ctrl);
			pokey.PushKey(k, false);
		}
		g_lastKeyScan[i] = k;
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
		OnLocalKeyDown(scanCode);
		return;
	}
	if (pokey) pokey->PushRawKey(scanCode, naturalKb);
}

bool ShouldSuppressWarmReset() { return g_active; }
bool ShouldSuppressBreak()     { return g_active; }

} // namespace ATNetplayInput
