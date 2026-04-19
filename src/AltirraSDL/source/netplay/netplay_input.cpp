// Altirra SDL3 netplay - per-frame input capture + injection (impl)

#include <stdafx.h>

#include "netplay_input.h"

#include "packets.h"  // NetInput

#include "simulator.h"
#include "simeventmanager.h"
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

#include <SDL3/SDL.h>

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

// Pre-session CPU debug-flag snapshot.  Netplay demands the two peers
// execute the same opcode stream — if one peer has e.g.
// IllegalInsnsEnabled=false it traps on illegal opcodes that the other
// peer executes fine.  On BeginSession we force all three flags to
// gameplay-friendly values and restore the user's prior settings on
// EndSession.
bool g_cpuIllegalSaved = true;
bool g_cpuStopOnBRKSaved = false;
bool g_cpuPathBrkSaved = false;

// Sim-event diagnostic hook: logs "interesting" simulator events (CPU
// traps, verifier failures, abnormal DMA, illegal instructions) while
// a netplay session is active.  Lets us pinpoint the exact root cause
// of a runaway-CPU or trap on either peer.
class NetplaySimEventLogger final : public IATSimulatorCallback {
public:
	void OnSimulatorEvent(ATSimulatorEvent ev) override {
		// AbnormalDMA is noise — fires many times per second in normal
		// games and does NOT reach the emu-error handler (debugger.cpp
		// early-returns for it).  Filter it out so the rest of the
		// "bad event" signal is visible.
		const char *name = nullptr;
		switch (ev) {
			case kATSimEvent_CPUIllegalInsn:    name = "CPUIllegalInsn";    break;
			case kATSimEvent_VerifierFailure:   name = "VerifierFailure";   break;
			case kATSimEvent_CPUStackBreakpoint:name = "CPUStackBreakpoint";break;
			case kATSimEvent_CPUPCBreakpoint:   name = "CPUPCBreakpoint";   break;
			case kATSimEvent_CPUNewPath:        name = "CPUNewPath";        break;
			case kATSimEvent_ReadBreakpoint:    name = "ReadBreakpoint";    break;
			case kATSimEvent_WriteBreakpoint:   name = "WriteBreakpoint";   break;
			case kATSimEvent_ColdReset:         name = "ColdReset";         break;
			case kATSimEvent_WarmReset:         name = "WarmReset";         break;
			default: return;
		}
		ATCPUEmulator &cpu = g_sim.GetCPU();
		g_ATLCNetplay("sim event: %s at PC=%04X (A=%02X X=%02X Y=%02X S=%02X)",
			name, (unsigned)cpu.GetInsnPC(),
			(unsigned)cpu.GetA(), (unsigned)cpu.GetX(),
			(unsigned)cpu.GetY(), (unsigned)cpu.GetS());
	}
};
NetplaySimEventLogger g_simEventLogger;
bool g_simEventHooked = false;
SDL_Gamepad *g_padCache = nullptr;

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

SDL_Gamepad *AcquireGamepad() {
	if (g_padCache) return g_padCache;
	int count = 0;
	SDL_JoystickID *ids = SDL_GetGamepads(&count);
	if (!ids) return nullptr;
	for (int i = 0; i < count && !g_padCache; ++i) {
		g_padCache = SDL_OpenGamepad(ids[i]);
	}
	SDL_free(ids);
	return g_padCache;
}

void ReleaseGamepad() {
	if (g_padCache) {
		SDL_CloseGamepad(g_padCache);
		g_padCache = nullptr;
	}
}

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
		g_wasRestricted = false;  // documented default (see input_sdl3.cpp:329)
		im->SetRestrictedMode(true);
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

	if (ATInputManager *im = g_sim.GetInputManager())
		im->SetRestrictedMode(g_wasRestricted);

	// Restore the user's pre-session CPU debug flags.
	{
		ATCPUEmulator &cpu = g_sim.GetCPU();
		cpu.SetIllegalInsnsEnabled(g_cpuIllegalSaved);
		cpu.SetStopOnBRK(g_cpuStopOnBRKSaved);
		cpu.SetPathBreakEnabled(g_cpuPathBrkSaved);
	}

	DetachEventLogger();

	ReleaseGamepad();
	ResetKeyQueue();
	g_active = false;
}

bool IsActive()                { return g_active; }
bool IsSuppressingLocalInput() { return g_active; }

void AttachEventLogger() {
	if (g_simEventHooked) return;
	if (auto *em = g_sim.GetEventManager()) {
		em->AddCallback(&g_simEventLogger);
		g_simEventHooked = true;
	}
}

void DetachEventLogger() {
	if (!g_simEventHooked) return;
	if (auto *em = g_sim.GetEventManager())
		em->RemoveCallback(&g_simEventLogger);
	g_simEventHooked = false;
}

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

	// --- stick + trigger (polling) ---
	const bool *ks = SDL_GetKeyboardState(nullptr);
	if (ks) {
		if (ks[SDL_SCANCODE_UP])    in.stickDir |= kDirUp;
		if (ks[SDL_SCANCODE_DOWN])  in.stickDir |= kDirDown;
		if (ks[SDL_SCANCODE_LEFT])  in.stickDir |= kDirLeft;
		if (ks[SDL_SCANCODE_RIGHT]) in.stickDir |= kDirRight;
		if (ks[SDL_SCANCODE_LCTRL] || ks[SDL_SCANCODE_SPACE]
		    || ks[SDL_SCANCODE_LSHIFT])
			in.buttons |= kBtnTrigger;
	}

	SDL_Gamepad *pad = AcquireGamepad();
	if (pad) {
		if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_UP))    in.stickDir |= kDirUp;
		if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_DOWN))  in.stickDir |= kDirDown;
		if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_LEFT))  in.stickDir |= kDirLeft;
		if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) in.stickDir |= kDirRight;

		constexpr Sint16 kDead = 16384;
		Sint16 lx = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX);
		Sint16 ly = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY);
		if (lx < -kDead) in.stickDir |= kDirLeft;
		if (lx >  kDead) in.stickDir |= kDirRight;
		if (ly < -kDead) in.stickDir |= kDirUp;
		if (ly >  kDead) in.stickDir |= kDirDown;

		if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_SOUTH)
		 || SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_EAST)
		 || SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_WEST)
		 || SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_NORTH))
			in.buttons |= kBtnTrigger;

		// Gamepad Start/Select as Start/Select.  No gamepad mapping
		// for Option — Atari Option is rarely needed mid-game.
		if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_START))
			in.buttons |= kBtnStart;
		if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_BACK))
			in.buttons |= kBtnSelect;
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

} // namespace ATNetplayInput
