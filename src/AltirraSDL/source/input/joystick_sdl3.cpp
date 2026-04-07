//	Altirra SDL3 frontend - joystick/gamepad support
//
//	Implements IATJoystickManager using SDL3's gamepad AND raw joystick
//	APIs.  This is the SDL3 equivalent of the Windows XInput/DirectInput
//	implementation in src/Altirra/source/joystick.cpp.
//
//	Two device classes are handled:
//
//	  * SDL3 Gamepads — devices that SDL recognises via its
//	    gamecontrollerdb mapping (Xbox, PlayStation, MFi, most modern
//	    USB gamepads).  Opened via SDL_OpenGamepad; buttons and axes
//	    are remapped into XInput order so the Windows default input maps
//	    work unchanged.
//
//	  * Raw SDL3 Joysticks — generic HID joysticks with no mapping in
//	    gamecontrollerdb (retro USB Atari-style joysticks, cheap arcade
//	    sticks, no-name HID devices).  Opened via SDL_OpenJoystick;
//	    buttons 0..N map directly to kATInputCode_JoyButton0+N, axes 0/1
//	    become left stick X/Y, axes 2/3 become right stick X/Y, and the
//	    first hat drives the d-pad axis-button bits.  This mirrors what
//	    the Windows DirectInput path does for devices that are not
//	    XInput-capable (joystick.cpp:761-onwards).
//
//	The simulator calls Poll() once per VBlank via AnticOnVBlank().
//	Poll() reads the current state of all connected devices and reports
//	button/axis changes to ATInputManager, which then routes them through
//	the input mapping system to emulated Atari controllers.
//
//	Button and axis numbering matches the XInput convention used by the
//	Windows version, so Altirra's default and user-created input maps
//	work identically on both platforms.

#include <stdafx.h>
#include <SDL3/SDL.h>
#include <vd2/system/math.h>
#include <vd2/system/vdtypes.h>
#include <vd2/system/VDString.h>
#include "joystick_sdl3.h"
#include "inputmanager.h"
#include "logging.h"
#ifdef ALTIRRA_MOBILE
#include "mobile_gamepad.h"
#endif

namespace {

// =========================================================================
// SDL3 gamepad button → Altirra button index (XInput order)
//
// XInput:  A=0, B=1, X=2, Y=3, LB=4, RB=5, Back=6, Start=7,
//          LThumb=8, RThumb=9, Guide=10
// SDL3:    South=0, East=1, West=2, North=3, Back=4, Guide=5,
//          Start=6, LStick=7, RStick=8, LShoulder=9, RShoulder=10
// =========================================================================

static const int kSDLButtonToAltirra[] = {
	0,	// SDL_GAMEPAD_BUTTON_SOUTH  → A (button 0)
	1,	// SDL_GAMEPAD_BUTTON_EAST   → B (button 1)
	2,	// SDL_GAMEPAD_BUTTON_WEST   → X (button 2)
	3,	// SDL_GAMEPAD_BUTTON_NORTH  → Y (button 3)
	6,	// SDL_GAMEPAD_BUTTON_BACK   → Back (button 6)
	10,	// SDL_GAMEPAD_BUTTON_GUIDE  → Guide (button 10)
	7,	// SDL_GAMEPAD_BUTTON_START  → Start (button 7)
	8,	// SDL_GAMEPAD_BUTTON_LEFT_STICK  → LThumb (button 8)
	9,	// SDL_GAMEPAD_BUTTON_RIGHT_STICK → RThumb (button 9)
	4,	// SDL_GAMEPAD_BUTTON_LEFT_SHOULDER  → LB (button 4)
	5,	// SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER → RB (button 5)
};

static const int kNumMappedButtons = sizeof(kSDLButtonToAltirra) / sizeof(kSDLButtonToAltirra[0]);

// Convert analog stick X/Y to a 4-bit direction mask.
// Matches ATController::ConvertAnalogToDirectionMask in joystick.cpp.
// Returns: bit 0 = left, 1 = right, 2 = up, 3 = down.
static uint32 ConvertAnalogToDirectionMask(sint32 x, sint32 y, sint32 deadZone) {
	const float kTan22_5d = 0.4142135623730950488016887242097f;
	float dxf = fabsf((float)x);
	float dyf = fabsf((float)y);
	uint32 mask = 0;

	if (dxf * dxf + dyf * dyf < (float)deadZone * (float)deadZone)
		return 0;

	if (dxf > dyf * kTan22_5d) {
		if (x < 0) mask |= (1 << 0);
		if (x > 0) mask |= (1 << 1);
	}

	if (dyf > dxf * kTan22_5d) {
		if (y > 0) mask |= (1 << 2);
		if (y < 0) mask |= (1 << 3);
	}

	return mask;
}

} // anonymous namespace

// =========================================================================
// Per-gamepad controller state
// =========================================================================

struct ATControllerSDL3 {
	// Exactly one of mpGamepad / mpJoystick is non-null for an open device.
	SDL_Gamepad *mpGamepad = nullptr;
	SDL_Joystick *mpJoystick = nullptr;	// raw-HID fallback (non-mapped devices)
	bool mbIsGamepad = false;
	int mNumButtons = 0;	// raw-joystick path only
	int mNumAxes = 0;		// raw-joystick path only
	int mNumHats = 0;		// raw-joystick path only

	SDL_JoystickID mInstanceID = 0;
	int mUnit = -1;			// ATInputManager unit ID
	ATInputUnitIdentifier mId {};

	// Previous frame state for delta detection
	uint32 mLastButtons = 0;		// 11 button bits (XInput order)
	uint32 mLastAxisButtons = 0;	// 16 axis-button bits
	sint32 mLastAxisVals[6] {};
	sint32 mLastDeadAxisVals[6] {};
};

// Format an SDL GUID as a 32-character hex string for diagnostic logging.
// SDL's own SDL_GUIDToString would suffice but takes a user-provided buffer;
// returning a VDStringA keeps call sites clean.
static VDStringA FormatGuid(const SDL_GUID& guid) {
	VDStringA s;
	s.reserve(32);
	static const char kHex[] = "0123456789abcdef";
	for (int i = 0; i < 16; ++i) {
		s += kHex[(guid.data[i] >> 4) & 0xF];
		s += kHex[guid.data[i] & 0xF];
	}
	return s;
}

// =========================================================================
// ATJoystickManagerSDL3Impl
// =========================================================================

class ATJoystickManagerSDL3Impl final : public ATJoystickManagerSDL3 {
public:
	~ATJoystickManagerSDL3Impl() override;

	bool Init(void *hwnd, ATInputManager *inputMan) override;
	void Shutdown() override;

	ATJoystickTransforms GetTransforms() const override { return mTransforms; }
	void SetTransforms(const ATJoystickTransforms& transforms) override { mTransforms = transforms; }
	void SetCaptureMode(bool capture) override { mbCaptureMode = capture; }
	void SetOnActivity(const vdfunction<void()>& fn) override { mOnActivity = fn; }
	void RescanForDevices() override;

	PollResult Poll() override;
	bool PollForCapture(int& unit, uint32& inputCode, uint32& inputCode2) override;
	const ATJoystickState *PollForCapture(uint32& n) override;
	uint32 GetJoystickPortStates() const override;

	void CloseGamepad(SDL_JoystickID id) override;

private:
	void OpenDevice(SDL_JoystickID id);
	void OpenGamepad(SDL_JoystickID id);
	void OpenRawJoystick(SDL_JoystickID id);
	void LogEnumerationStartup();
	ATControllerSDL3 *FindController(SDL_JoystickID id);

	void PollController(ATControllerSDL3& ctrl, bool& activity);
	void ReadGamepadState(ATControllerSDL3& ctrl,
		uint32& buttonStates, uint32& axisButtonStates,
		sint32 axisVals[6], sint32 deadVals[6]);
	void ReadRawJoystickState(ATControllerSDL3& ctrl,
		uint32& buttonStates, uint32& axisButtonStates,
		sint32 axisVals[6], sint32 deadVals[6]);
	void ConvertStick(sint32 dst[2], sint32 x, sint32 y);

	ATInputManager *mpInputManager = nullptr;
	ATJoystickTransforms mTransforms {};
	vdfunction<void()> mOnActivity;
	bool mbCaptureMode = false;

	vdfastvector<ATControllerSDL3 *> mControllers;
	vdfastvector<ATJoystickState> mCaptureStates;
};

ATJoystickManagerSDL3Impl::~ATJoystickManagerSDL3Impl() {
	Shutdown();
}

bool ATJoystickManagerSDL3Impl::Init(void *, ATInputManager *inputMan) {
	mpInputManager = inputMan;

	// Set default transforms matching the Windows defaults
	mTransforms.mStickAnalogDeadZone = (sint32)(0.15f * 65536);
	mTransforms.mStickDigitalDeadZone = (sint32)(0.45f * 65536);
	mTransforms.mStickAnalogPower = 1.0f;
	mTransforms.mTriggerAnalogDeadZone = (sint32)(0.05f * 65536);
	mTransforms.mTriggerDigitalDeadZone = (sint32)(0.20f * 65536);
	mTransforms.mTriggerAnalogPower = 1.0f;

	// Enumerate all joysticks SDL can see and log full details for each,
	// then open them (gamepads via the mapped API, others as raw HID).
	// This is the single source of diagnostic truth for input-device
	// problems on any platform (Windows, macOS, Linux, Android).
	LogEnumerationStartup();
	RescanForDevices();

	LOG_INFO("Joystick",
		"SDL3 device enumeration complete: %u device(s) open (gamepads + raw joysticks)",
		(unsigned)mControllers.size());
	if (mControllers.empty()) {
		LOG_WARN("Joystick",
			"No input devices were opened. If you expected a joystick or "
			"gamepad to work, check the per-device log lines above: each "
			"joystick SDL can see is listed with its name, GUID, VID/PID, "
			"and whether SDL classifies it as a gamepad. If the device is "
			"present but shows as a raw joystick on a platform where it "
			"used to work as a gamepad, SDL may be missing a "
			"gamecontrollerdb mapping for it. If the device is completely "
			"absent, the OS has not surfaced it to SDL (on macOS: check "
			"that the binary is running from a signed .app bundle; on "
			"Linux: check /dev/input permissions; on Windows: check that "
			"the HID driver is installed).");
	}

	return true;
}

void ATJoystickManagerSDL3Impl::LogEnumerationStartup() {
	// One-shot diagnostic dump — runs once at Init() time. Identifies
	// the SDL build, the host platform, and every joystick SDL can see
	// before we try to open any of them. This is the first line of
	// defense when a user reports "my joystick doesn't work".
	const int sdlVer = SDL_GetVersion();
	const char *platform = SDL_GetPlatform();
	LOG_INFO("Joystick", "SDL version %d.%d.%d on platform '%s'",
		SDL_VERSIONNUM_MAJOR(sdlVer),
		SDL_VERSIONNUM_MINOR(sdlVer),
		SDL_VERSIONNUM_MICRO(sdlVer),
		platform ? platform : "unknown");

	int count = 0;
	SDL_JoystickID *ids = SDL_GetJoysticks(&count);
	LOG_INFO("Joystick", "SDL_GetJoysticks reports %d joystick(s) visible to SDL", count);

	if (!ids || count == 0) {
		if (ids) SDL_free(ids);
		return;
	}

	for (int i = 0; i < count; ++i) {
		const SDL_JoystickID id = ids[i];
		const char *name = SDL_GetJoystickNameForID(id);
		const SDL_GUID guid = SDL_GetJoystickGUIDForID(id);
		const Uint16 vid = SDL_GetJoystickVendorForID(id);
		const Uint16 pid = SDL_GetJoystickProductForID(id);
		const bool isGamepad = SDL_IsGamepad(id);

		LOG_INFO("Joystick",
			"  [%d] instance=%u name='%s' vid=0x%04x pid=0x%04x guid=%s gamepad=%s",
			i, (unsigned)id,
			name ? name : "(null)",
			(unsigned)vid, (unsigned)pid,
			FormatGuid(guid).c_str(),
			isGamepad ? "yes" : "no (will be opened as raw HID joystick)");
	}

	SDL_free(ids);
}

void ATJoystickManagerSDL3Impl::Shutdown() {
	for (auto *ctrl : mControllers) {
		if (ctrl->mUnit >= 0 && mpInputManager)
			mpInputManager->UnregisterInputUnit(ctrl->mUnit);
		if (ctrl->mpGamepad)
			SDL_CloseGamepad(ctrl->mpGamepad);
		if (ctrl->mpJoystick)
			SDL_CloseJoystick(ctrl->mpJoystick);
		delete ctrl;
	}
	mControllers.clear();
	mpInputManager = nullptr;
}

void ATJoystickManagerSDL3Impl::RescanForDevices() {
	// Enumerate EVERY joystick SDL can see — not just the ones it
	// classifies as gamepads — so that generic HID joysticks (retro
	// USB Atari-style pads, arcade sticks, no-name devices) can still
	// be used. OpenDevice() dispatches to the appropriate path.
	int count = 0;
	SDL_JoystickID *ids = SDL_GetJoysticks(&count);
	if (!ids)
		return;

	// We intentionally do NOT reclassify already-tracked devices.
	// SDL3 completes gamepad classification synchronously before
	// firing SDL_EVENT_{GAMEPAD,JOYSTICK}_ADDED, so the first time we
	// see a device SDL_IsGamepad already returns its final answer.
	// Closing and reopening an existing device in place would shift
	// mControllers ordering and therefore the port-index assignments
	// that GetConnectedDevices derives from it, which would surface
	// as the player-index of already-working devices changing on the
	// fly. If a device ever does end up on the wrong codepath the
	// user can recover by unplugging and replugging it.
	for (int i = 0; i < count; ++i) {
		if (!FindController(ids[i]))
			OpenDevice(ids[i]);
	}

	SDL_free(ids);
}

void ATJoystickManagerSDL3Impl::OpenDevice(SDL_JoystickID id) {
	if (SDL_IsGamepad(id))
		OpenGamepad(id);
	else
		OpenRawJoystick(id);
}

void ATJoystickManagerSDL3Impl::OpenGamepad(SDL_JoystickID id) {
	SDL_Gamepad *gp = SDL_OpenGamepad(id);
	if (!gp) {
		LOG_WARN("Joystick", "SDL_OpenGamepad(%u) failed: %s",
			(unsigned)id, SDL_GetError());
		return;
	}

	auto *ctrl = new ATControllerSDL3();
	ctrl->mpGamepad = gp;
	ctrl->mbIsGamepad = true;
	ctrl->mInstanceID = id;

	// Build a stable identifier from the joystick GUID
	SDL_GUID guid = SDL_GetGamepadGUIDForID(id);
	static_assert(sizeof(guid.data) >= sizeof(ctrl->mId.buf), "GUID too small");
	memcpy(ctrl->mId.buf, guid.data, sizeof(ctrl->mId.buf));

	// Register with ATInputManager
	const char *name = SDL_GetGamepadName(gp);
	VDStringW wname;
	if (name)
		wname.sprintf(L"SDL Gamepad: %hs", name);
	else
		wname = L"SDL Gamepad";

	ctrl->mUnit = mpInputManager->RegisterInputUnit(ctrl->mId, wname.c_str(), nullptr);

	mControllers.push_back(ctrl);

	const Uint16 vid = SDL_GetJoystickVendorForID(id);
	const Uint16 pid = SDL_GetJoystickProductForID(id);
	LOG_INFO("Joystick",
		"Gamepad opened: name='%s' unit=%d instance=%u vid=0x%04x pid=0x%04x guid=%s",
		name ? name : "unknown",
		ctrl->mUnit, (unsigned)id,
		(unsigned)vid, (unsigned)pid,
		FormatGuid(guid).c_str());
}

void ATJoystickManagerSDL3Impl::OpenRawJoystick(SDL_JoystickID id) {
	SDL_Joystick *j = SDL_OpenJoystick(id);
	if (!j) {
		LOG_WARN("Joystick", "SDL_OpenJoystick(%u) failed: %s",
			(unsigned)id, SDL_GetError());
		return;
	}

	auto *ctrl = new ATControllerSDL3();
	ctrl->mpJoystick = j;
	ctrl->mbIsGamepad = false;
	ctrl->mInstanceID = id;
	ctrl->mNumButtons = SDL_GetNumJoystickButtons(j);
	ctrl->mNumAxes = SDL_GetNumJoystickAxes(j);
	ctrl->mNumHats = SDL_GetNumJoystickHats(j);

	SDL_GUID guid = SDL_GetJoystickGUIDForID(id);
	static_assert(sizeof(guid.data) >= sizeof(ctrl->mId.buf), "GUID too small");
	memcpy(ctrl->mId.buf, guid.data, sizeof(ctrl->mId.buf));

	const char *name = SDL_GetJoystickNameForID(id);
	VDStringW wname;
	if (name)
		wname.sprintf(L"SDL Joystick: %hs", name);
	else
		wname = L"SDL Joystick";

	ctrl->mUnit = mpInputManager->RegisterInputUnit(ctrl->mId, wname.c_str(), nullptr);

	mControllers.push_back(ctrl);

	const Uint16 vid = SDL_GetJoystickVendorForID(id);
	const Uint16 pid = SDL_GetJoystickProductForID(id);
	LOG_INFO("Joystick",
		"Raw joystick opened: name='%s' unit=%d instance=%u buttons=%d axes=%d hats=%d "
		"vid=0x%04x pid=0x%04x guid=%s",
		name ? name : "unknown",
		ctrl->mUnit, (unsigned)id,
		ctrl->mNumButtons, ctrl->mNumAxes, ctrl->mNumHats,
		(unsigned)vid, (unsigned)pid,
		FormatGuid(guid).c_str());
}

void ATJoystickManagerSDL3Impl::CloseGamepad(SDL_JoystickID id) {
	// Name is historical — this handles both gamepads and raw joysticks,
	// dispatched by the SDL_EVENT_{GAMEPAD,JOYSTICK}_REMOVED events.
	for (auto it = mControllers.begin(); it != mControllers.end(); ++it) {
		if ((*it)->mInstanceID == id) {
			auto *ctrl = *it;
			LOG_INFO("Joystick", "Device disconnected: unit %d (%s)",
				ctrl->mUnit, ctrl->mbIsGamepad ? "gamepad" : "raw joystick");

			if (ctrl->mUnit >= 0 && mpInputManager)
				mpInputManager->UnregisterInputUnit(ctrl->mUnit);
			if (ctrl->mpGamepad)
				SDL_CloseGamepad(ctrl->mpGamepad);
			if (ctrl->mpJoystick)
				SDL_CloseJoystick(ctrl->mpJoystick);
			delete ctrl;
			mControllers.erase(it);
			return;
		}
	}
}

ATControllerSDL3 *ATJoystickManagerSDL3Impl::FindController(SDL_JoystickID id) {
	for (auto *ctrl : mControllers) {
		if (ctrl->mInstanceID == id)
			return ctrl;
	}
	return nullptr;
}

IATJoystickManager::PollResult ATJoystickManagerSDL3Impl::Poll() {
	if (mControllers.empty())
		return kPollResult_NoControllers;

#ifdef ALTIRRA_MOBILE
	// While the mobile UI owns the gamepad (drawer/dialog open),
	// the player is navigating ImGui, not the emulated joystick.
	// Drain SDL state into the per-controller "last" cache without
	// dispatching deltas — this prevents stuck buttons or a sudden
	// burst of fake input the moment the dialog closes.
	if (ATMobileGamepad_IsUIOwning()) {
		for (auto *ctrl : mControllers) {
			if (ctrl->mUnit < 0)
				continue;
			if (ctrl->mbIsGamepad && !ctrl->mpGamepad)
				continue;
			if (!ctrl->mbIsGamepad && !ctrl->mpJoystick)
				continue;

			uint32 buttonStates = 0;
			uint32 axisButtonStates = 0;
			sint32 axisVals[6] {};
			sint32 deadVals[6] {};

			if (ctrl->mbIsGamepad)
				ReadGamepadState(*ctrl, buttonStates, axisButtonStates, axisVals, deadVals);
			else
				ReadRawJoystickState(*ctrl, buttonStates, axisButtonStates, axisVals, deadVals);

			ctrl->mLastButtons = buttonStates;
			ctrl->mLastAxisButtons = axisButtonStates;
			memcpy(ctrl->mLastAxisVals, axisVals, sizeof(axisVals));
		}
		return kPollResult_NoActivity;
	}
#endif

	bool activity = false;
	for (auto *ctrl : mControllers)
		PollController(*ctrl, activity);

	if (activity && mOnActivity)
		mOnActivity();

	return activity ? kPollResult_OK : kPollResult_NoActivity;
}

void ATJoystickManagerSDL3Impl::PollController(ATControllerSDL3& ctrl, bool& activity) {
	if (ctrl.mUnit < 0)
		return;
	if (ctrl.mbIsGamepad && !ctrl.mpGamepad)
		return;
	if (!ctrl.mbIsGamepad && !ctrl.mpJoystick)
		return;

	uint32 buttonStates = 0;
	uint32 axisButtonStates = 0;
	sint32 axisVals[6] {};
	sint32 deadVals[6] {};

	if (ctrl.mbIsGamepad)
		ReadGamepadState(ctrl, buttonStates, axisButtonStates, axisVals, deadVals);
	else
		ReadRawJoystickState(ctrl, buttonStates, axisButtonStates, axisVals, deadVals);

#ifdef ALTIRRA_MOBILE
	// On mobile, the Start and Back buttons are reserved for the
	// UI (cold-boot/pause and drawer toggle).  Mask them out of
	// the per-controller button state so they can't fire as
	// emulated 5200/joystick inputs even if the user has bound
	// them in the input map.  Their remapped XInput indices are
	// 7 (Start) and 6 (Back) — see kSdlGamepadButtonToXInput[].
	if (ctrl.mbIsGamepad) {
		buttonStates &= ~((1u << 7) | (1u << 6));
		// Match against the cached state too, otherwise a press
		// while a dialog is open would be reported as released
		// the moment the dialog closes.
		ctrl.mLastButtons &= ~((1u << 7) | (1u << 6));
	}
#endif

	// --- Report changes to ATInputManager ---

	// Axis button deltas
	const uint32 axisButtonDelta = axisButtonStates ^ ctrl.mLastAxisButtons;
	for (uint32 i = 0; i < 16; ++i) {
		if (axisButtonDelta & (1 << i)) {
			if (axisButtonStates & (1 << i))
				mpInputManager->OnButtonDown(ctrl.mUnit, kATInputCode_JoyStick1Left + i);
			else
				mpInputManager->OnButtonUp(ctrl.mUnit, kATInputCode_JoyStick1Left + i);
		}
	}

	// Button deltas
	const uint32 buttonDelta = buttonStates ^ ctrl.mLastButtons;
	for (int i = 0; i < 11; ++i) {
		if (buttonDelta & (1 << i)) {
			if (buttonStates & (1 << i))
				mpInputManager->OnButtonDown(ctrl.mUnit, kATInputCode_JoyButton0 + i);
			else
				mpInputManager->OnButtonUp(ctrl.mUnit, kATInputCode_JoyButton0 + i);
		}
	}

	if (axisButtonDelta || buttonDelta)
		activity = true;

	// Axis value changes
	for (int i = 0; i < 6; ++i) {
		if (axisVals[i] != ctrl.mLastAxisVals[i])
			mpInputManager->OnAxisInput(ctrl.mUnit, kATInputCode_JoyHoriz1 + i,
				axisVals[i], deadVals[i]);
	}

	// Save state for next poll
	ctrl.mLastButtons = buttonStates;
	ctrl.mLastAxisButtons = axisButtonStates;
	memcpy(ctrl.mLastAxisVals, axisVals, sizeof(axisVals));
	memcpy(ctrl.mLastDeadAxisVals, deadVals, sizeof(deadVals));
}

void ATJoystickManagerSDL3Impl::ReadGamepadState(ATControllerSDL3& ctrl,
	uint32& buttonStates, uint32& axisButtonStates,
	sint32 axisVals[6], sint32 deadVals[6])
{
	// --- Read button state ---
	// Map SDL3 buttons to XInput-order button bits
	for (int i = 0; i < kNumMappedButtons; ++i) {
		if (SDL_GetGamepadButton(ctrl.mpGamepad, (SDL_GamepadButton)i))
			buttonStates |= (1 << kSDLButtonToAltirra[i]);
	}

	// --- Read axis values ---
	// SDL3 thumbstick range: -32768 to 32767
	// Altirra axis range: -65536 to 65536 (XInput multiplies by 2)
	sint32 lx = SDL_GetGamepadAxis(ctrl.mpGamepad, SDL_GAMEPAD_AXIS_LEFTX);
	sint32 ly = SDL_GetGamepadAxis(ctrl.mpGamepad, SDL_GAMEPAD_AXIS_LEFTY);
	sint32 rx = SDL_GetGamepadAxis(ctrl.mpGamepad, SDL_GAMEPAD_AXIS_RIGHTX);
	sint32 ry = SDL_GetGamepadAxis(ctrl.mpGamepad, SDL_GAMEPAD_AXIS_RIGHTY);

	axisVals[0] = lx * 2;		// left stick X
	axisVals[1] = ly * 2;		// left stick Y
	axisVals[3] = rx * 2;		// right stick X
	axisVals[4] = ry * 2;		// right stick Y

	// Apply deadzone to sticks
	ConvertStick(deadVals, lx * 2, ly * 2);
	ConvertStick(deadVals + 3, rx * 2, ry * 2);

	// Triggers: SDL3 range 0-32767, Altirra expects 0-65536
	for (int i = 0; i < 2; ++i) {
		SDL_GamepadAxis trigAxis = i ? SDL_GAMEPAD_AXIS_RIGHT_TRIGGER : SDL_GAMEPAD_AXIS_LEFT_TRIGGER;
		sint32 rawVal = SDL_GetGamepadAxis(ctrl.mpGamepad, trigAxis);
		float fVal = (float)rawVal / 32767.0f;
		sint32 axisVal = (sint32)(fVal * 65536.0f);
		sint32 adjVal = 0;

		float trigThreshold = (float)mTransforms.mTriggerAnalogDeadZone / 65536.0f;
		if (fVal > trigThreshold) {
			float deadVal = (fVal - trigThreshold) / (1.0f - trigThreshold);
			adjVal = (sint32)(65536.0f * powf(deadVal, mTransforms.mTriggerAnalogPower));
		}

		if (i) {
			axisVals[5] = axisVal;
			deadVals[5] = adjVal;
		} else {
			axisVals[2] = axisVal;
			deadVals[2] = adjVal;
		}
	}

	// --- Compute axis buttons (digital from analog) ---
	// ConvertAnalogToDirectionMask expects positive-up Y (Windows convention).
	// SDL3 Y-axis is positive-down, so we negate Y here.
	axisButtonStates |= ConvertAnalogToDirectionMask(lx * 2, -ly * 2,
		mTransforms.mStickDigitalDeadZone / 2);

	// Left trigger pressed = bit 5 (SDL3 triggers are 0-32767, threshold is in 0-65536 space)
	if (SDL_GetGamepadAxis(ctrl.mpGamepad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) >
		(mTransforms.mTriggerDigitalDeadZone >> 1))
		axisButtonStates |= (1 << 5);

	// Right stick = bits 6-9
	axisButtonStates |= ConvertAnalogToDirectionMask(rx * 2, -ry * 2,
		mTransforms.mStickDigitalDeadZone / 2) << 6;

	// Right trigger pressed = bit 11
	if (SDL_GetGamepadAxis(ctrl.mpGamepad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) >
		(mTransforms.mTriggerDigitalDeadZone >> 1))
		axisButtonStates |= (1 << 11);

	// D-pad as axis buttons: bits 12-15
	if (SDL_GetGamepadButton(ctrl.mpGamepad, SDL_GAMEPAD_BUTTON_DPAD_LEFT))
		axisButtonStates |= (1 << 12);
	if (SDL_GetGamepadButton(ctrl.mpGamepad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT))
		axisButtonStates |= (1 << 13);
	if (SDL_GetGamepadButton(ctrl.mpGamepad, SDL_GAMEPAD_BUTTON_DPAD_UP))
		axisButtonStates |= (1 << 14);
	if (SDL_GetGamepadButton(ctrl.mpGamepad, SDL_GAMEPAD_BUTTON_DPAD_DOWN))
		axisButtonStates |= (1 << 15);
}

void ATJoystickManagerSDL3Impl::ReadRawJoystickState(ATControllerSDL3& ctrl,
	uint32& buttonStates, uint32& axisButtonStates,
	sint32 axisVals[6], sint32 deadVals[6])
{
	// --- Buttons 0..10 ---
	// Raw HID joysticks report buttons in device-defined order; we pass
	// them through unchanged. Any input-map preset that matches on
	// JoyButton0..JoyButton10 will pick them up regardless of label.
	const int nb = ctrl.mNumButtons < 11 ? ctrl.mNumButtons : 11;
	for (int i = 0; i < nb; ++i) {
		if (SDL_GetJoystickButton(ctrl.mpJoystick, i))
			buttonStates |= (1 << i);
	}

	// --- Axes ---
	// First two axes become "left stick", next two become "right stick".
	// SDL3 joystick axes are in the range -32768..32767; Altirra expects
	// -65536..65536, so we multiply by 2 to match the Gamepad path.
	sint32 lx = 0, ly = 0, rx = 0, ry = 0;
	if (ctrl.mNumAxes >= 1)
		lx = SDL_GetJoystickAxis(ctrl.mpJoystick, 0);
	if (ctrl.mNumAxes >= 2)
		ly = SDL_GetJoystickAxis(ctrl.mpJoystick, 1);
	if (ctrl.mNumAxes >= 3)
		rx = SDL_GetJoystickAxis(ctrl.mpJoystick, 2);
	if (ctrl.mNumAxes >= 4)
		ry = SDL_GetJoystickAxis(ctrl.mpJoystick, 3);

	axisVals[0] = lx * 2;
	axisVals[1] = ly * 2;
	axisVals[3] = rx * 2;
	axisVals[4] = ry * 2;

	ConvertStick(deadVals, lx * 2, ly * 2);
	ConvertStick(deadVals + 3, rx * 2, ry * 2);

	// Left stick digital direction — bits 0..3
	axisButtonStates |= ConvertAnalogToDirectionMask(lx * 2, -ly * 2,
		mTransforms.mStickDigitalDeadZone / 2);

	// Right stick digital direction — bits 6..9
	axisButtonStates |= ConvertAnalogToDirectionMask(rx * 2, -ry * 2,
		mTransforms.mStickDigitalDeadZone / 2) << 6;

	// --- Hat → d-pad axis-button bits 12..15 ---
	// Use the first hat only; the Windows DirectInput path does the same.
	if (ctrl.mNumHats > 0) {
		const Uint8 hat = SDL_GetJoystickHat(ctrl.mpJoystick, 0);
		if (hat & SDL_HAT_LEFT)  axisButtonStates |= (1 << 12);
		if (hat & SDL_HAT_RIGHT) axisButtonStates |= (1 << 13);
		if (hat & SDL_HAT_UP)    axisButtonStates |= (1 << 14);
		if (hat & SDL_HAT_DOWN)  axisButtonStates |= (1 << 15);
	}
}

void ATJoystickManagerSDL3Impl::ConvertStick(sint32 dst[2], sint32 x, sint32 y) {
	// Matches ATControllerXInput::ConvertStick — apply analog deadzone
	float fx = (float)x;
	float fy = (float)y;
	const float mag = sqrtf(fx * fx + fy * fy);
	sint32 rx = 0, ry = 0;

	if (mag > mTransforms.mStickAnalogDeadZone) {
		float scale = (mag - mTransforms.mStickAnalogDeadZone) /
			(mag * (65536.0f - mTransforms.mStickAnalogDeadZone));

		scale *= powf(mag * scale, mTransforms.mStickAnalogPower - 1.0f);

		fx *= scale;
		fy *= scale;

		rx = (sint32)(fx * 65536.0f);
		ry = (sint32)(fy * 65536.0f);

		if (rx < -65536) rx = -65536; else if (rx > 65536) rx = 65536;
		if (ry < -65536) ry = -65536; else if (ry > 65536) ry = 65536;
	}

	dst[0] = rx;
	dst[1] = ry;
}

bool ATJoystickManagerSDL3Impl::PollForCapture(int& unit, uint32& inputCode, uint32& inputCode2) {
	// Read the current state of every open device (gamepad or raw
	// joystick) using the same helpers as Poll(), then find the first
	// button/axis-button that transitioned low→high this call. This
	// powers the "press something to bind" UI mode.
	for (auto *ctrl : mControllers) {
		if (ctrl->mUnit < 0)
			continue;
		if (ctrl->mbIsGamepad && !ctrl->mpGamepad)
			continue;
		if (!ctrl->mbIsGamepad && !ctrl->mpJoystick)
			continue;

		uint32 buttonStates = 0;
		uint32 axisButtonStates = 0;
		sint32 axisVals[6] {};
		sint32 deadVals[6] {};
		if (ctrl->mbIsGamepad)
			ReadGamepadState(*ctrl, buttonStates, axisButtonStates, axisVals, deadVals);
		else
			ReadRawJoystickState(*ctrl, buttonStates, axisButtonStates, axisVals, deadVals);

		const uint32 newButtons = buttonStates & ~ctrl->mLastButtons;
		ctrl->mLastButtons = buttonStates;
		if (newButtons) {
			unit = ctrl->mUnit;
			for (int i = 0; i < 11; ++i) {
				if (newButtons & (1 << i)) {
					inputCode = kATInputCode_JoyButton0 + i;
					inputCode2 = 0;
					return true;
				}
			}
		}

		const uint32 newAxisButtons = axisButtonStates & ~ctrl->mLastAxisButtons;
		ctrl->mLastAxisButtons = axisButtonStates;
		if (newAxisButtons) {
			unit = ctrl->mUnit;
			for (int i = 0; i < 16; ++i) {
				if (newAxisButtons & (1 << i)) {
					inputCode = kATInputCode_JoyStick1Left + i;
					inputCode2 = kATInputCode_JoyHoriz1 + (i >> 1);
					return true;
				}
			}
		}
	}

	return false;
}

const ATJoystickState *ATJoystickManagerSDL3Impl::PollForCapture(uint32& n) {
	mCaptureStates.clear();

	for (auto *ctrl : mControllers) {
		if (ctrl->mUnit < 0)
			continue;
		if (ctrl->mbIsGamepad && !ctrl->mpGamepad)
			continue;
		if (!ctrl->mbIsGamepad && !ctrl->mpJoystick)
			continue;

		ATJoystickState state {};
		state.mUnit = ctrl->mUnit;
		state.mButtons = ctrl->mLastButtons;
		state.mAxisButtons = ctrl->mLastAxisButtons;
		memcpy(state.mAxisVals, ctrl->mLastAxisVals, sizeof(state.mAxisVals));
		memcpy(state.mDeadifiedAxisVals, ctrl->mLastDeadAxisVals, sizeof(state.mDeadifiedAxisVals));

		mCaptureStates.push_back(state);
	}

	n = (uint32)mCaptureStates.size();
	return mCaptureStates.empty() ? nullptr : mCaptureStates.data();
}

uint32 ATJoystickManagerSDL3Impl::GetJoystickPortStates() const {
	// Combine all controller axis button states for port readback
	uint32 states = 0;
	for (auto *ctrl : mControllers)
		states |= ctrl->mLastAxisButtons;
	return states;
}

// =========================================================================
// Factory function — replaces the stub in joystick_stubs.cpp
// =========================================================================

IATJoystickManager *ATCreateJoystickManager() {
	return new ATJoystickManagerSDL3Impl();
}
