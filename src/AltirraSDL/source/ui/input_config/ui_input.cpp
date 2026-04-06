//	AltirraSDL - Input Mappings / Input Setup dialogs
//	Full reimplementation of Windows uiinput.cpp + uiinputsetup.cpp
//	Dialogs:
//	  1. Input Mappings list         (ATUIRenderInputMappings)
//	  2. Create Input Map wizard     (rendered as modal popup)
//	  3. Edit Input Map              (tree: controllers + bindings)
//	  4. Edit Controller             (controller type, port, flags)
//	  5. Edit Mapping                (source, target, mode, speed/accel)
//	  6. Rebind / listen for input   (captures keyboard/gamepad)
//	  7. Input Setup                 (dead zones, power curves)

#include <stdafx.h>
#include <cmath>
#include <algorithm>
#include <vector>
#include <imgui.h>
#include <vd2/system/vdtypes.h>
#include <vd2/system/VDString.h>
#include <vd2/system/text.h>
#include <vd2/system/registry.h>

#include "ui_main.h"
#include "simulator.h"
#include "inputmanager.h"
#include "inputmap.h"
#include "inputdefs.h"
#include "joystick.h"

extern ATSimulator g_sim;

// =========================================================================
// Shared helpers
// =========================================================================

static const char *ATUIGetControllerTypeName(ATInputControllerType type) {
	switch (type) {
		case kATInputControllerType_Joystick:       return "Joystick";
		case kATInputControllerType_Paddle:         return "Paddle";
		case kATInputControllerType_STMouse:        return "ST Mouse";
		case kATInputControllerType_Console:        return "Console";
		case kATInputControllerType_5200Controller: return "5200 Controller";
		case kATInputControllerType_InputState:     return "Input State";
		case kATInputControllerType_LightGun:       return "Light Gun";
		case kATInputControllerType_Tablet:         return "Tablet";
		case kATInputControllerType_KoalaPad:       return "KoalaPad";
		case kATInputControllerType_AmigaMouse:     return "Amiga Mouse";
		case kATInputControllerType_Keypad:         return "Keypad";
		case kATInputControllerType_Trackball_CX80: return "CX-80 Trackball";
		case kATInputControllerType_5200Trackball:  return "5200 Trackball";
		case kATInputControllerType_Driving:        return "Driving Controller";
		case kATInputControllerType_Keyboard:       return "Keyboard";
		case kATInputControllerType_LightPen:       return "Light Pen";
		case kATInputControllerType_PowerPad:       return "Power Pad";
		case kATInputControllerType_LightPenStack:  return "Light Pen (Stacked)";
		default:                                    return "Unknown";
	}
}

// Format controller text with port info (matches Windows GetText)
static VDStringA FormatControllerText(ATInputControllerType type, int unit, uint32 flagCheckBits) {
	VDStringA s;
	switch (type) {
		case kATInputControllerType_Joystick:
			if (unit >= 4)
				s.sprintf("Joystick (MultiJoy #%d)", unit - 3);
			else
				s.sprintf("Joystick (port %d)", unit + 1);
			break;
		case kATInputControllerType_Paddle:
			s.sprintf("Paddle %c (port %d)", (unit & 1) ? 'B' : 'A', (unit >> 1) + 1);
			break;
		case kATInputControllerType_STMouse:
			s.sprintf("ST Mouse (port %d)", unit + 1);
			break;
		case kATInputControllerType_AmigaMouse:
			s.sprintf("Amiga Mouse (port %d)", unit + 1);
			break;
		case kATInputControllerType_Console:
			s = "Console";
			break;
		case kATInputControllerType_5200Controller:
			s.sprintf("5200 Controller (port %d)", unit + 1);
			break;
		case kATInputControllerType_InputState:
			s = "Input State";
			break;
		case kATInputControllerType_LightGun:
			s.sprintf("Light Gun (XG-1) (port %d)", unit + 1);
			break;
		case kATInputControllerType_Tablet:
			s = "Tablet (Atari Touch Tablet)";
			break;
		case kATInputControllerType_KoalaPad:
			s = "Tablet (KoalaPad)";
			break;
		case kATInputControllerType_Keypad:
			s.sprintf("Numerical Keypad (port %d)", unit + 1);
			break;
		case kATInputControllerType_Trackball_CX80:
			s.sprintf("Trak-Ball (port %d)", unit + 1);
			break;
		case kATInputControllerType_5200Trackball:
			s.sprintf("5200 Trak-Ball (port %d)", unit + 1);
			break;
		case kATInputControllerType_Driving:
			s.sprintf("Driving Controller (port %d)", unit + 1);
			break;
		case kATInputControllerType_Keyboard:
			s.sprintf("Keyboard Controller (port %d)", unit + 1);
			break;
		case kATInputControllerType_LightPen:
			s.sprintf("Light Pen (CX-70/CX-75) (port %d)", unit + 1);
			break;
		case kATInputControllerType_PowerPad:
			s.sprintf("PowerPad (port %d)", unit + 1);
			break;
		case kATInputControllerType_LightPenStack:
			s.sprintf("Light Pen (Stack) (port %d)", unit + 1);
			break;
		default:
			s = "Unknown";
			break;
	}

	if (flagCheckBits & kATInputCode_FlagCheck0) {
		char buf[32];
		snprintf(buf, sizeof(buf), " (F1 %s)", (flagCheckBits & kATInputCode_FlagValue0) ? "on" : "off");
		s += buf;
	}
	if (flagCheckBits & kATInputCode_FlagCheck1) {
		char buf[32];
		snprintf(buf, sizeof(buf), " (F2 %s)", (flagCheckBits & kATInputCode_FlagValue1) ? "on" : "off");
		s += buf;
	}

	return s;
}

static void ATGetInputTriggerModeNameA(uint32 mode, bool addSpeed, VDStringA& s) {
	switch (mode & kATInputTriggerMode_Mask) {
		case kATInputTriggerMode_Default:
		default:
			s += "Default";
			addSpeed = false;
			break;
		case kATInputTriggerMode_AutoFire:
			s += "Auto-fire";
			break;
		case kATInputTriggerMode_Toggle:
			s += "Toggle";
			addSpeed = false;
			break;
		case kATInputTriggerMode_ToggleAF:
			s += "Toggle AF";
			break;
		case kATInputTriggerMode_Relative:
			s += "Relative";
			break;
		case kATInputTriggerMode_Absolute:
			s += "Absolute";
			addSpeed = false;
			break;
		case kATInputTriggerMode_Inverted:
			s += "Inverted";
			addSpeed = false;
			break;
	}

	if (addSpeed) {
		const uint32 idx = (mode & kATInputTriggerSpeed_Mask) >> kATInputTriggerSpeed_Shift;
		char buf[32];
		snprintf(buf, sizeof(buf), " %u", idx);
		s += buf;
		const uint32 accel = (mode & kATInputTriggerAccel_Mask) >> kATInputTriggerAccel_Shift;
		if (accel) {
			snprintf(buf, sizeof(buf), " >>%u", accel);
			s += buf;
		}
	}
}

// =========================================================================
// Input code tables — same as Windows kInputCodes[]
// =========================================================================

static const uint32 kInputCodes[] = {
	kATInputCode_None,

	kATInputCode_KeyLShift,
	kATInputCode_KeyRShift,
	kATInputCode_KeyLControl,
	kATInputCode_KeyRControl,

	kATInputCode_KeyLeft,
	kATInputCode_KeyUp,
	kATInputCode_KeyRight,
	kATInputCode_KeyDown,

	kATInputCode_Key0,
	kATInputCode_Key1,
	kATInputCode_Key2,
	kATInputCode_Key3,
	kATInputCode_Key4,
	kATInputCode_Key5,
	kATInputCode_Key6,
	kATInputCode_Key7,
	kATInputCode_Key8,
	kATInputCode_Key9,

	kATInputCode_KeyA + 0,
	kATInputCode_KeyA + 1,
	kATInputCode_KeyA + 2,
	kATInputCode_KeyA + 3,
	kATInputCode_KeyA + 4,
	kATInputCode_KeyA + 5,
	kATInputCode_KeyA + 6,
	kATInputCode_KeyA + 7,
	kATInputCode_KeyA + 8,
	kATInputCode_KeyA + 9,
	kATInputCode_KeyA + 10,
	kATInputCode_KeyA + 11,
	kATInputCode_KeyA + 12,
	kATInputCode_KeyA + 13,
	kATInputCode_KeyA + 14,
	kATInputCode_KeyA + 15,
	kATInputCode_KeyA + 16,
	kATInputCode_KeyA + 17,
	kATInputCode_KeyA + 18,
	kATInputCode_KeyA + 19,
	kATInputCode_KeyA + 20,
	kATInputCode_KeyA + 21,
	kATInputCode_KeyA + 22,
	kATInputCode_KeyA + 23,
	kATInputCode_KeyA + 24,
	kATInputCode_KeyA + 25,

	kATInputCode_KeyBack,
	kATInputCode_KeyTab,
	kATInputCode_KeyReturn,
	kATInputCode_KeyEscape,
	kATInputCode_KeySpace,
	kATInputCode_KeyPrior,
	kATInputCode_KeyNext,
	kATInputCode_KeyEnd,
	kATInputCode_KeyHome,
	kATInputCode_KeyInsert,
	kATInputCode_KeyDelete,
	kATInputCode_KeyNumpad0,
	kATInputCode_KeyNumpad1,
	kATInputCode_KeyNumpad2,
	kATInputCode_KeyNumpad3,
	kATInputCode_KeyNumpad4,
	kATInputCode_KeyNumpad5,
	kATInputCode_KeyNumpad6,
	kATInputCode_KeyNumpad7,
	kATInputCode_KeyNumpad8,
	kATInputCode_KeyNumpad9,
	kATInputCode_KeyNumpadEnter,
	kATInputCode_KeyMultiply,
	kATInputCode_KeyAdd,
	kATInputCode_KeySubtract,
	kATInputCode_KeyDecimal,
	kATInputCode_KeyDivide,
	kATInputCode_KeyF1,
	kATInputCode_KeyF2,
	kATInputCode_KeyF3,
	kATInputCode_KeyF4,
	kATInputCode_KeyF5,
	kATInputCode_KeyF6,
	kATInputCode_KeyF7,
	kATInputCode_KeyF8,
	kATInputCode_KeyF9,
	kATInputCode_KeyF10,
	kATInputCode_KeyF11,
	kATInputCode_KeyF12,
	kATInputCode_KeyOem1,
	kATInputCode_KeyOemPlus,
	kATInputCode_KeyOemComma,
	kATInputCode_KeyOemMinus,
	kATInputCode_KeyOemPeriod,
	kATInputCode_KeyOem2,
	kATInputCode_KeyOem3,
	kATInputCode_KeyOem4,
	kATInputCode_KeyOem5,
	kATInputCode_KeyOem6,
	kATInputCode_KeyOem7,

	kATInputCode_MouseHoriz,
	kATInputCode_MouseVert,
	kATInputCode_MousePadX,
	kATInputCode_MousePadY,
	kATInputCode_MouseBeamX,
	kATInputCode_MouseBeamY,
	kATInputCode_MouseEmuStickX,
	kATInputCode_MouseEmuStickY,
	kATInputCode_MouseLeft,
	kATInputCode_MouseRight,
	kATInputCode_MouseUp,
	kATInputCode_MouseDown,
	kATInputCode_MouseWheelUp,
	kATInputCode_MouseWheelDown,
	kATInputCode_MouseWheel,
	kATInputCode_MouseHWheelLeft,
	kATInputCode_MouseHWheelRight,
	kATInputCode_MouseHWheel,
	kATInputCode_MouseLMB,
	kATInputCode_MouseMMB,
	kATInputCode_MouseRMB,
	kATInputCode_MouseX1B,
	kATInputCode_MouseX2B,

	kATInputCode_JoyHoriz1,
	kATInputCode_JoyVert1,
	kATInputCode_JoyVert2,
	kATInputCode_JoyHoriz3,
	kATInputCode_JoyVert3,
	kATInputCode_JoyVert4,
	kATInputCode_JoyPOVHoriz,
	kATInputCode_JoyPOVVert,
	kATInputCode_JoyStick1Left,
	kATInputCode_JoyStick1Right,
	kATInputCode_JoyStick1Up,
	kATInputCode_JoyStick1Down,
	kATInputCode_JoyStick2Up,
	kATInputCode_JoyStick2Down,
	kATInputCode_JoyStick3Left,
	kATInputCode_JoyStick3Right,
	kATInputCode_JoyStick3Up,
	kATInputCode_JoyStick3Down,
	kATInputCode_JoyStick4Up,
	kATInputCode_JoyStick4Down,
	kATInputCode_JoyPOVLeft,
	kATInputCode_JoyPOVRight,
	kATInputCode_JoyPOVUp,
	kATInputCode_JoyPOVDown,
	kATInputCode_JoyButton0+0,
	kATInputCode_JoyButton0+1,
	kATInputCode_JoyButton0+2,
	kATInputCode_JoyButton0+3,
	kATInputCode_JoyButton0+4,
	kATInputCode_JoyButton0+5,
	kATInputCode_JoyButton0+6,
	kATInputCode_JoyButton0+7,
	kATInputCode_JoyButton0+8,
	kATInputCode_JoyButton0+9,
	kATInputCode_JoyButton0+10,
	kATInputCode_JoyButton0+11,
	kATInputCode_JoyButton0+12,
	kATInputCode_JoyButton0+13,
	kATInputCode_JoyButton0+14,
	kATInputCode_JoyButton0+15,
	kATInputCode_JoyButton0+16,
	kATInputCode_JoyButton0+17,
	kATInputCode_JoyButton0+18,
	kATInputCode_JoyButton0+19,
	kATInputCode_JoyButton0+20,
	kATInputCode_JoyButton0+21,
	kATInputCode_JoyButton0+22,
	kATInputCode_JoyButton0+23,
	kATInputCode_JoyButton0+24,
	kATInputCode_JoyButton0+25,
	kATInputCode_JoyButton0+26,
	kATInputCode_JoyButton0+27,
	kATInputCode_JoyButton0+28,
	kATInputCode_JoyButton0+29,
	kATInputCode_JoyButton0+30,
	kATInputCode_JoyButton0+31,
};

static constexpr uint32 kInputCodeCount = sizeof(kInputCodes) / sizeof(kInputCodes[0]);

// =========================================================================
// Target code tables per controller type — same as Windows
// =========================================================================

static const uint32 kTargetCodesJoystick[] = {
	kATInputTrigger_Left, kATInputTrigger_Right, kATInputTrigger_Up,
	kATInputTrigger_Down, kATInputTrigger_Button0,
};

static const uint32 kTargetCodesPaddle[] = {
	kATInputTrigger_Left, kATInputTrigger_Right,
	kATInputTrigger_Axis0, kATInputTrigger_Axis0+1, kATInputTrigger_Axis0+2,
	kATInputTrigger_Button0,
};

static const uint32 kTargetCodesMouse[] = {
	kATInputTrigger_Axis0, kATInputTrigger_Axis0+1,
	kATInputTrigger_Left, kATInputTrigger_Right, kATInputTrigger_Up, kATInputTrigger_Down,
	kATInputTrigger_Button0, kATInputTrigger_Button0+1,
};

static const uint32 kTargetCodes5200Controller[] = {
	kATInputTrigger_Button0, kATInputTrigger_Button0+1,
	kATInputTrigger_Left, kATInputTrigger_Right, kATInputTrigger_Up, kATInputTrigger_Down,
	kATInputTrigger_Axis0, kATInputTrigger_Axis0+1,
	kATInputTrigger_5200_0, kATInputTrigger_5200_1, kATInputTrigger_5200_2,
	kATInputTrigger_5200_3, kATInputTrigger_5200_4, kATInputTrigger_5200_5,
	kATInputTrigger_5200_6, kATInputTrigger_5200_7, kATInputTrigger_5200_8,
	kATInputTrigger_5200_9, kATInputTrigger_5200_Star, kATInputTrigger_5200_Pound,
	kATInputTrigger_5200_Start, kATInputTrigger_5200_Reset, kATInputTrigger_5200_Pause,
};

static const uint32 kTargetCodesConsole[] = {
	kATInputTrigger_Start, kATInputTrigger_Select, kATInputTrigger_Option,
	kATInputTrigger_Turbo, kATInputTrigger_ColdReset, kATInputTrigger_WarmReset,
	kATInputTrigger_Rewind, kATInputTrigger_RewindMenu, kATInputTrigger_KeySpace,
	kATInputTrigger_UILeft, kATInputTrigger_UIRight, kATInputTrigger_UIUp, kATInputTrigger_UIDown,
	kATInputTrigger_UIAccept, kATInputTrigger_UIReject, kATInputTrigger_UIMenu,
	kATInputTrigger_UIOption, kATInputTrigger_UISwitchLeft, kATInputTrigger_UISwitchRight,
	kATInputTrigger_UILeftShift, kATInputTrigger_UIRightShift,
};

static const uint32 kTargetCodesInputState[] = {
	kATInputTrigger_Flag0, kATInputTrigger_Flag0+1,
};

static const uint32 kTargetCodesLightPenGun[] = {
	kATInputTrigger_Axis0, kATInputTrigger_Axis0+1,
	kATInputTrigger_Left, kATInputTrigger_Right, kATInputTrigger_Up, kATInputTrigger_Down,
	kATInputTrigger_Button0, kATInputTrigger_Button0+2,
};

static const uint32 kTargetCodesTablet[] = {
	kATInputTrigger_Axis0, kATInputTrigger_Axis0+1,
	kATInputTrigger_Left, kATInputTrigger_Right, kATInputTrigger_Up, kATInputTrigger_Down,
	kATInputTrigger_Button0, kATInputTrigger_Button0+1,
	kATInputTrigger_Button0+2, kATInputTrigger_Button0+3,
};

static const uint32 kTargetCodesKeypad[] = {
	kATInputTrigger_Button0, kATInputTrigger_Button0+1, kATInputTrigger_Button0+2,
	kATInputTrigger_Button0+3, kATInputTrigger_Button0+4, kATInputTrigger_Button0+5,
	kATInputTrigger_Button0+6, kATInputTrigger_Button0+7, kATInputTrigger_Button0+8,
	kATInputTrigger_Button0+9, kATInputTrigger_Button0+10, kATInputTrigger_Button0+11,
	kATInputTrigger_Button0+12, kATInputTrigger_Button0+13, kATInputTrigger_Button0+14,
	kATInputTrigger_Button0+15, kATInputTrigger_Button0+16,
};

static const uint32 kTargetCodesTrackballCX80[] = {
	kATInputTrigger_Axis0, kATInputTrigger_Axis0+1,
	kATInputTrigger_Left, kATInputTrigger_Right, kATInputTrigger_Up, kATInputTrigger_Down,
	kATInputTrigger_Button0,
};

static const uint32 kTargetCodes5200Trackball[] = {
	kATInputTrigger_Button0, kATInputTrigger_Button0+1,
	kATInputTrigger_Left, kATInputTrigger_Right, kATInputTrigger_Up, kATInputTrigger_Down,
	kATInputTrigger_Axis0, kATInputTrigger_Axis0+1,
	kATInputTrigger_5200_0, kATInputTrigger_5200_1, kATInputTrigger_5200_2,
	kATInputTrigger_5200_3, kATInputTrigger_5200_4, kATInputTrigger_5200_5,
	kATInputTrigger_5200_6, kATInputTrigger_5200_7, kATInputTrigger_5200_8,
	kATInputTrigger_5200_9, kATInputTrigger_5200_Star, kATInputTrigger_5200_Pound,
	kATInputTrigger_5200_Start, kATInputTrigger_5200_Reset, kATInputTrigger_5200_Pause,
};

static const uint32 kTargetCodesDriving[] = {
	kATInputTrigger_Axis0,
	kATInputTrigger_Left, kATInputTrigger_Right,
	kATInputTrigger_Button0,
};

static const uint32 kTargetCodesKeyboard[] = {
	kATInputTrigger_Button0, kATInputTrigger_Button0+1, kATInputTrigger_Button0+2,
	kATInputTrigger_Button0+3, kATInputTrigger_Button0+4, kATInputTrigger_Button0+5,
	kATInputTrigger_Button0+6, kATInputTrigger_Button0+7, kATInputTrigger_Button0+8,
	kATInputTrigger_Button0+9, kATInputTrigger_Button0+10, kATInputTrigger_Button0+11,
};

static const uint32 kTargetCodesPowerPad[] = {
	kATInputTrigger_Axis0, kATInputTrigger_Axis0+1,
	kATInputTrigger_Left, kATInputTrigger_Right, kATInputTrigger_Up, kATInputTrigger_Down,
	kATInputTrigger_Axis0+2,
	kATInputTrigger_ScrollUp, kATInputTrigger_ScrollDown,
	kATInputTrigger_Button0, kATInputTrigger_Button0+1,
	kATInputTrigger_Button0+2, kATInputTrigger_Button0+3, kATInputTrigger_Button0+4,
};

struct TargetCodeSet {
	const uint32 *codes;
	uint32 count;
};

static TargetCodeSet GetTargetCodes(ATInputControllerType type) {
	#define TC(arr) { arr, (uint32)(sizeof(arr)/sizeof(arr[0])) }
	switch (type) {
		case kATInputControllerType_Joystick:       return TC(kTargetCodesJoystick);
		case kATInputControllerType_Paddle:         return TC(kTargetCodesPaddle);
		case kATInputControllerType_STMouse:
		case kATInputControllerType_AmigaMouse:     return TC(kTargetCodesMouse);
		case kATInputControllerType_5200Controller: return TC(kTargetCodes5200Controller);
		case kATInputControllerType_Console:        return TC(kTargetCodesConsole);
		case kATInputControllerType_InputState:     return TC(kTargetCodesInputState);
		case kATInputControllerType_LightPen:
		case kATInputControllerType_LightPenStack:
		case kATInputControllerType_LightGun:       return TC(kTargetCodesLightPenGun);
		case kATInputControllerType_Tablet:
		case kATInputControllerType_KoalaPad:       return TC(kTargetCodesTablet);
		case kATInputControllerType_Keypad:         return TC(kTargetCodesKeypad);
		case kATInputControllerType_Trackball_CX80: return TC(kTargetCodesTrackballCX80);
		case kATInputControllerType_5200Trackball:  return TC(kTargetCodes5200Trackball);
		case kATInputControllerType_Driving:        return TC(kTargetCodesDriving);
		case kATInputControllerType_Keyboard:       return TC(kTargetCodesKeyboard);
		case kATInputControllerType_PowerPad:       return TC(kTargetCodesPowerPad);
		default:                                    return TC(kTargetCodesJoystick);
	}
	#undef TC
}

// Trigger modes
static const uint32 kTargetModes[] = {
	kATInputTriggerMode_Default,
	kATInputTriggerMode_AutoFire,
	kATInputTriggerMode_Toggle,
	kATInputTriggerMode_ToggleAF,
	kATInputTriggerMode_Relative,
	kATInputTriggerMode_Absolute,
	kATInputTriggerMode_Inverted,
};

static const char *kTargetModeNames[] = {
	"Default", "Auto-fire", "Toggle", "Toggle AF",
	"Relative", "Absolute", "Inverted",
};

static constexpr uint32 kTargetModeCount = sizeof(kTargetModes) / sizeof(kTargetModes[0]);

// Controller type entries for the Edit Controller dialog — matches Windows
struct ControllerTypeEntry {
	ATInputControllerType mType;
	uint8 mIndexScale;
	uint8 mIndexOffset;
	const char *mpLabel;
};

static constexpr ControllerTypeEntry kControllerTypeEntries[] = {
	{ kATInputControllerType_Joystick,         1, 0, "Joystick (CX40)" },
	{ kATInputControllerType_STMouse,          1, 0, "Mouse (Atari ST)" },
	{ kATInputControllerType_AmigaMouse,       1, 0, "Mouse (Amiga)" },
	{ kATInputControllerType_Paddle,           2, 0, "Paddle A (CX30)" },
	{ kATInputControllerType_Paddle,           2, 1, "Paddle B (CX30)" },
	{ kATInputControllerType_Console,          0, 0, "Console" },
	{ kATInputControllerType_5200Controller,   1, 0, "5200 Controller (CX52)" },
	{ kATInputControllerType_InputState,       0, 0, "Input State" },
	{ kATInputControllerType_LightGun,         1, 0, "Light Gun (XG-1)" },
	{ kATInputControllerType_LightPen,         1, 0, "Light Pen (CX70/CX75)" },
	{ kATInputControllerType_LightPenStack,    1, 0, "Light Pen (Stack)" },
	{ kATInputControllerType_Tablet,           1, 0, "Tablet (Atari touch tablet)" },
	{ kATInputControllerType_KoalaPad,         1, 0, "Tablet (KoalaPad)" },
	{ kATInputControllerType_Keypad,           1, 0, "Numerical Keypad (CX85)" },
	{ kATInputControllerType_Trackball_CX80,   1, 0, "Trak-Ball (CX80)" },
	{ kATInputControllerType_5200Trackball,    1, 0, "5200 Trak-Ball (CX53)" },
	{ kATInputControllerType_Driving,          1, 0, "Driving Controller (CX20)" },
	{ kATInputControllerType_Keyboard,         1, 0, "Keyboard Controller (CX21/23/50)" },
	{ kATInputControllerType_PowerPad,         1, 0, "Chalk Board PowerPad" },
};

static constexpr uint32 kControllerTypeEntryCount = sizeof(kControllerTypeEntries) / sizeof(kControllerTypeEntries[0]);

// Sorted order for controller type combo (Windows sorts alphabetically)
static int g_controllerTypeSortOrder[kControllerTypeEntryCount];
static bool g_controllerTypeSortInit = false;

static void InitControllerTypeSortOrder() {
	if (g_controllerTypeSortInit)
		return;
	for (uint32 i = 0; i < kControllerTypeEntryCount; ++i)
		g_controllerTypeSortOrder[i] = (int)i;
	std::sort(g_controllerTypeSortOrder, g_controllerTypeSortOrder + kControllerTypeEntryCount,
		[](int a, int b) {
			return strcasecmp(kControllerTypeEntries[a].mpLabel, kControllerTypeEntries[b].mpLabel) < 0;
		});
	g_controllerTypeSortInit = true;
}

// Port labels — matches Windows
static const char *kPortLabels[] = {
	"Port 1", "Port 2", "Port 3 (800 only)", "Port 4 (800 only)",
	"MultiJoy #1", "MultiJoy #2", "MultiJoy #3", "MultiJoy #4",
	"MultiJoy #5", "MultiJoy #6", "MultiJoy #7", "MultiJoy #8",
};

static constexpr uint32 kPortLabelCount = sizeof(kPortLabels) / sizeof(kPortLabels[0]);

// =========================================================================
// Create Input Map wizard — matches Windows ATUIDialogCreateInputMap
// =========================================================================

// Virtual inputs for the template system
enum class VirtualInput : uint8 {
	Stick_Left, Stick_Right, Stick_Up, Stick_Down,
	Stick_AnalogX, Stick_AnalogY, Stick_AnalogPosX, Stick_AnalogPosY,
	Stick_AnalogMoveX, Stick_AnalogMoveY, Stick_AnalogBeamX, Stick_AnalogBeamY,
	Button_0, Button_1, Button_2, Button_3,
	Shift, Shift2, Scroll_Vert, Contact,
	Key_0, Key_1, Key_2, Key_3, Key_4, Key_5, Key_6, Key_7, Key_8, Key_9,
	Key_Pound, Key_Star, Key_Start, Key_Pause, Key_Reset,
	Key_Period, Key_PlusEnter, Key_Minus, Key_Y, Key_N, Key_Del, Key_Esc,
};

enum class VirtualInputMask : uint8 { Any, Keyboard, Mouse, Gamepad };

struct InputMapping {
	VirtualInput mVirtualInput;
	uint32 mInputCode;
};

struct InputSourceInfo {
	const char *mpName;
	const char *mpShortName;
	bool mbAllowDualADMapping;
	VirtualInputMask mVirtualInputMask;
	const InputMapping *mMappings;
	uint32 mMappingCount;
};

struct TargetMapping {
	VirtualInput mVirtualInput;
	uint32 mTargetCode;
	VirtualInputMask mVirtualInputMask;
};

struct ExtraMapping {
	uint32 mInputCode;
	uint32 mControllerId;
	uint32 mCode;
};

struct ControllerTypeTemplateInfo {
	ATInputControllerType mType;
	const char *mpName;
	const char *mpShortName;
	int mIndex;
	bool mbHasAbsoluteRelative;
	bool mbRequiresRelative;
	uint32 mAnalogRelativeModifier;
	uint32 mDigitalRelativeModifier;
	const TargetMapping *mMappings;
	uint32 mMappingCount;
	const ExtraMapping *mExtraMappings;
	uint32 mExtraMappingCount;
};

// Macros to define arrays inline
#define ARRAY_AND_COUNT(arr) arr, (uint32)(sizeof(arr)/sizeof(arr[0]))

// Input source mappings — Keyboard (arrows)
static const InputMapping kInputSourceKeyboardArrows[] = {
	{ VirtualInput::Stick_Left,   kATInputCode_KeyLeft },
	{ VirtualInput::Stick_Right,  kATInputCode_KeyRight },
	{ VirtualInput::Stick_Up,     kATInputCode_KeyUp },
	{ VirtualInput::Stick_Down,   kATInputCode_KeyDown },
	{ VirtualInput::Button_0,     kATInputCode_KeyLControl },
	{ VirtualInput::Button_1,     kATInputCode_KeyLShift },
	{ VirtualInput::Key_0, kATInputCode_Key0 }, { VirtualInput::Key_1, kATInputCode_Key1 },
	{ VirtualInput::Key_2, kATInputCode_Key2 }, { VirtualInput::Key_3, kATInputCode_Key3 },
	{ VirtualInput::Key_4, kATInputCode_Key4 }, { VirtualInput::Key_5, kATInputCode_Key5 },
	{ VirtualInput::Key_6, kATInputCode_Key6 }, { VirtualInput::Key_7, kATInputCode_Key7 },
	{ VirtualInput::Key_8, kATInputCode_Key8 }, { VirtualInput::Key_9, kATInputCode_Key9 },
	{ VirtualInput::Key_Pound, kATInputCode_KeyOemPlus },
	{ VirtualInput::Key_Star, kATInputCode_KeyOemMinus },
	{ VirtualInput::Key_Start, kATInputCode_KeyF2 },
	{ VirtualInput::Key_Pause, kATInputCode_KeyF3 },
	{ VirtualInput::Key_Reset, kATInputCode_KeyF4 },
	{ VirtualInput::Key_Period, kATInputCode_KeyOemPeriod },
	{ VirtualInput::Key_PlusEnter, kATInputCode_KeyReturn },
	{ VirtualInput::Key_Minus, kATInputCode_KeyOemMinus },
	{ VirtualInput::Key_Y, kATInputCode_KeyY },
	{ VirtualInput::Key_N, kATInputCode_KeyN },
	{ VirtualInput::Key_Del, kATInputCode_KeyDelete },
	{ VirtualInput::Key_Esc, kATInputCode_KeyEscape },
};

// Keyboard (numpad) — has diagonals
static const InputMapping kInputSourceKeyboardNumpad[] = {
	{ VirtualInput::Stick_Left, kATInputCode_KeyNumpad7 },
	{ VirtualInput::Stick_Left, kATInputCode_KeyNumpad4 },
	{ VirtualInput::Stick_Left, kATInputCode_KeyNumpad1 },
	{ VirtualInput::Stick_Right, kATInputCode_KeyNumpad9 },
	{ VirtualInput::Stick_Right, kATInputCode_KeyNumpad6 },
	{ VirtualInput::Stick_Right, kATInputCode_KeyNumpad3 },
	{ VirtualInput::Stick_Up, kATInputCode_KeyNumpad7 },
	{ VirtualInput::Stick_Up, kATInputCode_KeyNumpad8 },
	{ VirtualInput::Stick_Up, kATInputCode_KeyNumpad9 },
	{ VirtualInput::Stick_Down, kATInputCode_KeyNumpad1 },
	{ VirtualInput::Stick_Down, kATInputCode_KeyNumpad2 },
	{ VirtualInput::Stick_Down, kATInputCode_KeyNumpad3 },
	{ VirtualInput::Button_0, kATInputCode_KeyNumpad0 },
	{ VirtualInput::Key_0, kATInputCode_Key0 }, { VirtualInput::Key_1, kATInputCode_Key1 },
	{ VirtualInput::Key_2, kATInputCode_Key2 }, { VirtualInput::Key_3, kATInputCode_Key3 },
	{ VirtualInput::Key_4, kATInputCode_Key4 }, { VirtualInput::Key_5, kATInputCode_Key5 },
	{ VirtualInput::Key_6, kATInputCode_Key6 }, { VirtualInput::Key_7, kATInputCode_Key7 },
	{ VirtualInput::Key_8, kATInputCode_Key8 }, { VirtualInput::Key_9, kATInputCode_Key9 },
	{ VirtualInput::Key_Pound, kATInputCode_KeyOemPlus },
	{ VirtualInput::Key_Star, kATInputCode_KeyOemMinus },
	{ VirtualInput::Key_Start, kATInputCode_KeyF2 },
	{ VirtualInput::Key_Pause, kATInputCode_KeyF3 },
	{ VirtualInput::Key_Reset, kATInputCode_KeyF4 },
	{ VirtualInput::Key_Period, kATInputCode_KeyOemPeriod },
	{ VirtualInput::Key_PlusEnter, kATInputCode_KeyReturn },
	{ VirtualInput::Key_Minus, kATInputCode_KeyOemMinus },
	{ VirtualInput::Key_Y, kATInputCode_KeyY },
	{ VirtualInput::Key_N, kATInputCode_KeyN },
	{ VirtualInput::Key_Del, kATInputCode_KeyDelete },
	{ VirtualInput::Key_Esc, kATInputCode_KeyEscape },
};

static const InputMapping kInputSourceMouse[] = {
	{ VirtualInput::Stick_AnalogMoveX, kATInputCode_MouseHoriz },
	{ VirtualInput::Stick_AnalogMoveY, kATInputCode_MouseVert },
	{ VirtualInput::Stick_AnalogPosX, kATInputCode_MousePadX },
	{ VirtualInput::Stick_AnalogPosY, kATInputCode_MousePadY },
	{ VirtualInput::Stick_AnalogBeamX, kATInputCode_MouseBeamX },
	{ VirtualInput::Stick_AnalogBeamY, kATInputCode_MouseBeamY },
	{ VirtualInput::Stick_AnalogX, kATInputCode_MouseEmuStickX },
	{ VirtualInput::Stick_AnalogY, kATInputCode_MouseEmuStickY },
	{ VirtualInput::Stick_Left, kATInputCode_MouseLeft },
	{ VirtualInput::Stick_Right, kATInputCode_MouseRight },
	{ VirtualInput::Stick_Up, kATInputCode_MouseUp },
	{ VirtualInput::Stick_Down, kATInputCode_MouseDown },
	{ VirtualInput::Button_0, kATInputCode_MouseLMB },
	{ VirtualInput::Button_1, kATInputCode_MouseRMB },
	{ VirtualInput::Button_2, kATInputCode_MouseMMB },
	{ VirtualInput::Scroll_Vert, kATInputCode_MouseWheel },
	{ VirtualInput::Key_0, kATInputCode_Key0 }, { VirtualInput::Key_1, kATInputCode_Key1 },
	{ VirtualInput::Key_2, kATInputCode_Key2 }, { VirtualInput::Key_3, kATInputCode_Key3 },
	{ VirtualInput::Key_4, kATInputCode_Key4 }, { VirtualInput::Key_5, kATInputCode_Key5 },
	{ VirtualInput::Key_6, kATInputCode_Key6 }, { VirtualInput::Key_7, kATInputCode_Key7 },
	{ VirtualInput::Key_8, kATInputCode_Key8 }, { VirtualInput::Key_9, kATInputCode_Key9 },
	{ VirtualInput::Key_Pound, kATInputCode_KeyOemPlus },
	{ VirtualInput::Key_Star, kATInputCode_KeyOemMinus },
	{ VirtualInput::Key_Start, kATInputCode_KeyF2 },
	{ VirtualInput::Key_Pause, kATInputCode_KeyF3 },
	{ VirtualInput::Key_Reset, kATInputCode_KeyF4 },
};

static const InputMapping kInputSourceGamepad[] = {
	{ VirtualInput::Stick_Left, kATInputCode_JoyPOVLeft },
	{ VirtualInput::Stick_Right, kATInputCode_JoyPOVRight },
	{ VirtualInput::Stick_Up, kATInputCode_JoyPOVUp },
	{ VirtualInput::Stick_Down, kATInputCode_JoyPOVDown },
	{ VirtualInput::Stick_AnalogX, kATInputCode_JoyHoriz1 },
	{ VirtualInput::Stick_AnalogY, kATInputCode_JoyVert1 },
	{ VirtualInput::Button_0, kATInputCode_JoyButton0 },
	{ VirtualInput::Button_1, kATInputCode_JoyButton0+1 },
	{ VirtualInput::Button_2, kATInputCode_JoyButton0+2 },
	{ VirtualInput::Button_3, kATInputCode_JoyButton0+3 },
	{ VirtualInput::Shift, kATInputCode_JoyStick2Down },
	{ VirtualInput::Shift2, kATInputCode_JoyStick4Down },
	{ VirtualInput::Scroll_Vert, kATInputCode_JoyVert3 },
	{ VirtualInput::Key_0, kATInputCode_Key0 }, { VirtualInput::Key_1, kATInputCode_Key1 },
	{ VirtualInput::Key_2, kATInputCode_Key2 }, { VirtualInput::Key_3, kATInputCode_Key3 },
	{ VirtualInput::Key_4, kATInputCode_Key4 }, { VirtualInput::Key_5, kATInputCode_Key5 },
	{ VirtualInput::Key_6, kATInputCode_Key6 }, { VirtualInput::Key_7, kATInputCode_Key7 },
	{ VirtualInput::Key_8, kATInputCode_Key8 }, { VirtualInput::Key_9, kATInputCode_Key9 },
	{ VirtualInput::Key_Pound, kATInputCode_KeyOemPlus },
	{ VirtualInput::Key_Star, kATInputCode_KeyOemMinus },
	{ VirtualInput::Key_Start, kATInputCode_KeyF2 },
	{ VirtualInput::Key_Pause, kATInputCode_KeyF3 },
	{ VirtualInput::Key_Reset, kATInputCode_KeyF4 },
};

static const InputSourceInfo kInputSourceInfo[] = {
	{ "Keyboard (arrows + left ctrl/shift)", "Keyboard (arrows)", false,
	  VirtualInputMask::Keyboard, ARRAY_AND_COUNT(kInputSourceKeyboardArrows) },
	{ "Keyboard (numpad)", "Keyboard (numpad)", false,
	  VirtualInputMask::Keyboard, ARRAY_AND_COUNT(kInputSourceKeyboardNumpad) },
	{ "Mouse", nullptr, false,
	  VirtualInputMask::Mouse, ARRAY_AND_COUNT(kInputSourceMouse) },
	{ "Gamepad", nullptr, true,
	  VirtualInputMask::Gamepad, ARRAY_AND_COUNT(kInputSourceGamepad) },
};

static constexpr uint32 kInputSourceCount = sizeof(kInputSourceInfo) / sizeof(kInputSourceInfo[0]);

// Controller type templates — abbreviated, only types that the Create dialog uses.
// This matches the Windows kControllerTypeInfo[] array from uiinput.cpp lines 301-692.
// The target mappings for each controller type define how virtual inputs map to trigger codes.

// Joystick target mappings
static const TargetMapping kJoystickTargetMappings[] = {
	{ VirtualInput::Stick_Left, kATInputTrigger_Left, VirtualInputMask::Any },
	{ VirtualInput::Stick_Right, kATInputTrigger_Right, VirtualInputMask::Any },
	{ VirtualInput::Stick_Up, kATInputTrigger_Up, VirtualInputMask::Any },
	{ VirtualInput::Stick_Down, kATInputTrigger_Down, VirtualInputMask::Any },
	{ VirtualInput::Button_0, kATInputTrigger_Button0, VirtualInputMask::Any },
};

static const TargetMapping kPaddleTargetMappings[] = {
	{ VirtualInput::Stick_AnalogMoveX, kATInputTrigger_Axis0, VirtualInputMask::Any },
	{ VirtualInput::Stick_AnalogX, kATInputTrigger_Axis0, VirtualInputMask::Any },
	{ VirtualInput::Stick_Left, kATInputTrigger_Left, VirtualInputMask::Any },
	{ VirtualInput::Stick_Right, kATInputTrigger_Right, VirtualInputMask::Any },
	{ VirtualInput::Button_0, kATInputTrigger_Button0, VirtualInputMask::Any },
};

static const TargetMapping kSTMouseTargetMappings[] = {
	{ VirtualInput::Stick_AnalogMoveX, kATInputTrigger_Axis0, VirtualInputMask::Any },
	{ VirtualInput::Stick_AnalogMoveY, kATInputTrigger_Axis0+1, VirtualInputMask::Any },
	{ VirtualInput::Stick_AnalogX, kATInputTrigger_Axis0, VirtualInputMask::Any },
	{ VirtualInput::Stick_AnalogY, kATInputTrigger_Axis0+1, VirtualInputMask::Any },
	{ VirtualInput::Stick_Left, kATInputTrigger_Left, VirtualInputMask::Any },
	{ VirtualInput::Stick_Right, kATInputTrigger_Right, VirtualInputMask::Any },
	{ VirtualInput::Stick_Up, kATInputTrigger_Up, VirtualInputMask::Any },
	{ VirtualInput::Stick_Down, kATInputTrigger_Down, VirtualInputMask::Any },
	{ VirtualInput::Button_0, kATInputTrigger_Button0, VirtualInputMask::Any },
	{ VirtualInput::Button_1, kATInputTrigger_Button0+1, VirtualInputMask::Any },
};

static const TargetMapping kLightPenTargetMappings[] = {
	{ VirtualInput::Stick_AnalogBeamX, kATInputTrigger_Axis0, VirtualInputMask::Any },
	{ VirtualInput::Stick_AnalogBeamY, kATInputTrigger_Axis0+1, VirtualInputMask::Any },
	{ VirtualInput::Stick_AnalogPosX, kATInputTrigger_Axis0, VirtualInputMask::Any },
	{ VirtualInput::Stick_AnalogPosY, kATInputTrigger_Axis0+1, VirtualInputMask::Any },
	{ VirtualInput::Stick_AnalogMoveX, kATInputTrigger_Axis0, VirtualInputMask::Any },
	{ VirtualInput::Stick_AnalogMoveY, kATInputTrigger_Axis0+1, VirtualInputMask::Any },
	{ VirtualInput::Stick_AnalogX, kATInputTrigger_Axis0, VirtualInputMask::Any },
	{ VirtualInput::Stick_AnalogY, kATInputTrigger_Axis0+1, VirtualInputMask::Any },
	{ VirtualInput::Stick_Left, kATInputTrigger_Left, VirtualInputMask::Any },
	{ VirtualInput::Stick_Right, kATInputTrigger_Right, VirtualInputMask::Any },
	{ VirtualInput::Stick_Up, kATInputTrigger_Up, VirtualInputMask::Any },
	{ VirtualInput::Stick_Down, kATInputTrigger_Down, VirtualInputMask::Any },
	{ VirtualInput::Button_0, kATInputTrigger_Button0, VirtualInputMask::Any },
};

static const ExtraMapping kLightPenExtraMappings[] = {
	{ kATInputCode_None, 0, kATInputTrigger_Button0+2 | kATInputTriggerMode_Inverted },
};

static const TargetMapping kTabletTargetMappings[] = {
	{ VirtualInput::Stick_AnalogMoveX, kATInputTrigger_Axis0, VirtualInputMask::Any },
	{ VirtualInput::Stick_AnalogMoveY, kATInputTrigger_Axis0+1, VirtualInputMask::Any },
	{ VirtualInput::Stick_AnalogBeamX, kATInputTrigger_Axis0, VirtualInputMask::Any },
	{ VirtualInput::Stick_AnalogBeamY, kATInputTrigger_Axis0+1, VirtualInputMask::Any },
	{ VirtualInput::Stick_AnalogPosX, kATInputTrigger_Axis0, VirtualInputMask::Any },
	{ VirtualInput::Stick_AnalogPosY, kATInputTrigger_Axis0+1, VirtualInputMask::Any },
	{ VirtualInput::Stick_AnalogX, kATInputTrigger_Axis0, VirtualInputMask::Any },
	{ VirtualInput::Stick_AnalogY, kATInputTrigger_Axis0+1, VirtualInputMask::Any },
	{ VirtualInput::Stick_Left, kATInputTrigger_Left, VirtualInputMask::Any },
	{ VirtualInput::Stick_Right, kATInputTrigger_Right, VirtualInputMask::Any },
	{ VirtualInput::Stick_Up, kATInputTrigger_Up, VirtualInputMask::Any },
	{ VirtualInput::Stick_Down, kATInputTrigger_Down, VirtualInputMask::Any },
	{ VirtualInput::Button_0, kATInputTrigger_Button0, VirtualInputMask::Any },
	{ VirtualInput::Button_1, kATInputTrigger_Button0+1 | kATInputTriggerMode_Toggle, VirtualInputMask::Any },
	{ VirtualInput::Button_2, kATInputTrigger_Button0+2, VirtualInputMask::Any },
	{ VirtualInput::Button_3, kATInputTrigger_Button0+3, VirtualInputMask::Any },
};

// KoalaPad has different button order from Tablet (matches Windows uiinput.cpp)
static const TargetMapping kKoalaPadTargetMappings[] = {
	{ VirtualInput::Stick_AnalogMoveX, kATInputTrigger_Axis0, VirtualInputMask::Any },
	{ VirtualInput::Stick_AnalogMoveY, kATInputTrigger_Axis0+1, VirtualInputMask::Any },
	{ VirtualInput::Stick_AnalogBeamX, kATInputTrigger_Axis0, VirtualInputMask::Any },
	{ VirtualInput::Stick_AnalogBeamY, kATInputTrigger_Axis0+1, VirtualInputMask::Any },
	{ VirtualInput::Stick_AnalogPosX, kATInputTrigger_Axis0, VirtualInputMask::Any },
	{ VirtualInput::Stick_AnalogPosY, kATInputTrigger_Axis0+1, VirtualInputMask::Any },
	{ VirtualInput::Stick_AnalogX, kATInputTrigger_Axis0, VirtualInputMask::Any },
	{ VirtualInput::Stick_AnalogY, kATInputTrigger_Axis0+1, VirtualInputMask::Any },
	{ VirtualInput::Stick_Left, kATInputTrigger_Left, VirtualInputMask::Any },
	{ VirtualInput::Stick_Right, kATInputTrigger_Right, VirtualInputMask::Any },
	{ VirtualInput::Stick_Up, kATInputTrigger_Up, VirtualInputMask::Any },
	{ VirtualInput::Stick_Down, kATInputTrigger_Down, VirtualInputMask::Any },
	{ VirtualInput::Button_0, kATInputTrigger_Button0, VirtualInputMask::Any },
	{ VirtualInput::Button_1, kATInputTrigger_Button0+3 | kATInputTriggerMode_Toggle, VirtualInputMask::Any },
	{ VirtualInput::Button_2, kATInputTrigger_Button0+1, VirtualInputMask::Any },
	{ VirtualInput::Button_3, kATInputTrigger_Button0+2, VirtualInputMask::Any },
};

static const TargetMapping kKeypadTargetMappings[] = {
	{ VirtualInput::Key_1, kATInputTrigger_Button0, VirtualInputMask::Any },
	{ VirtualInput::Key_2, kATInputTrigger_Button0+1, VirtualInputMask::Any },
	{ VirtualInput::Key_3, kATInputTrigger_Button0+2, VirtualInputMask::Any },
	{ VirtualInput::Key_4, kATInputTrigger_Button0+3, VirtualInputMask::Any },
	{ VirtualInput::Key_5, kATInputTrigger_Button0+4, VirtualInputMask::Any },
	{ VirtualInput::Key_6, kATInputTrigger_Button0+5, VirtualInputMask::Any },
	{ VirtualInput::Key_7, kATInputTrigger_Button0+6, VirtualInputMask::Any },
	{ VirtualInput::Key_8, kATInputTrigger_Button0+7, VirtualInputMask::Any },
	{ VirtualInput::Key_9, kATInputTrigger_Button0+8, VirtualInputMask::Any },
	{ VirtualInput::Key_0, kATInputTrigger_Button0+9, VirtualInputMask::Any },
	{ VirtualInput::Key_Period, kATInputTrigger_Button0+10, VirtualInputMask::Any },
	{ VirtualInput::Key_PlusEnter, kATInputTrigger_Button0+11, VirtualInputMask::Any },
	{ VirtualInput::Key_Minus, kATInputTrigger_Button0+12, VirtualInputMask::Any },
	{ VirtualInput::Key_Y, kATInputTrigger_Button0+13, VirtualInputMask::Any },
	{ VirtualInput::Key_N, kATInputTrigger_Button0+14, VirtualInputMask::Any },
	{ VirtualInput::Key_Del, kATInputTrigger_Button0+15, VirtualInputMask::Any },
	{ VirtualInput::Key_Esc, kATInputTrigger_Button0+16, VirtualInputMask::Any },
};

static const TargetMapping kTrackballCX80TargetMappings[] = {
	{ VirtualInput::Stick_AnalogMoveX, kATInputTrigger_Axis0, VirtualInputMask::Any },
	{ VirtualInput::Stick_AnalogMoveY, kATInputTrigger_Axis0+1, VirtualInputMask::Any },
	{ VirtualInput::Stick_AnalogX, kATInputTrigger_Axis0, VirtualInputMask::Any },
	{ VirtualInput::Stick_AnalogY, kATInputTrigger_Axis0+1, VirtualInputMask::Any },
	{ VirtualInput::Stick_Left, kATInputTrigger_Left, VirtualInputMask::Any },
	{ VirtualInput::Stick_Right, kATInputTrigger_Right, VirtualInputMask::Any },
	{ VirtualInput::Stick_Up, kATInputTrigger_Up, VirtualInputMask::Any },
	{ VirtualInput::Stick_Down, kATInputTrigger_Down, VirtualInputMask::Any },
	{ VirtualInput::Button_0, kATInputTrigger_Button0, VirtualInputMask::Any },
};

static const TargetMapping kDrivingTargetMappings[] = {
	{ VirtualInput::Stick_AnalogMoveX, kATInputTrigger_Axis0, VirtualInputMask::Any },
	{ VirtualInput::Stick_Left, kATInputTrigger_Left, VirtualInputMask::Any },
	{ VirtualInput::Stick_Right, kATInputTrigger_Right, VirtualInputMask::Any },
	{ VirtualInput::Button_0, kATInputTrigger_Button0, VirtualInputMask::Any },
};

static const TargetMapping kKeyboardCtrlTargetMappings[] = {
	{ VirtualInput::Key_1, kATInputTrigger_Button0, VirtualInputMask::Any },
	{ VirtualInput::Key_2, kATInputTrigger_Button0+1, VirtualInputMask::Any },
	{ VirtualInput::Key_3, kATInputTrigger_Button0+2, VirtualInputMask::Any },
	{ VirtualInput::Key_4, kATInputTrigger_Button0+3, VirtualInputMask::Any },
	{ VirtualInput::Key_5, kATInputTrigger_Button0+4, VirtualInputMask::Any },
	{ VirtualInput::Key_6, kATInputTrigger_Button0+5, VirtualInputMask::Any },
	{ VirtualInput::Key_7, kATInputTrigger_Button0+6, VirtualInputMask::Any },
	{ VirtualInput::Key_8, kATInputTrigger_Button0+7, VirtualInputMask::Any },
	{ VirtualInput::Key_9, kATInputTrigger_Button0+8, VirtualInputMask::Any },
	{ VirtualInput::Key_Star, kATInputTrigger_Button0+9, VirtualInputMask::Any },
	{ VirtualInput::Key_0, kATInputTrigger_Button0+10, VirtualInputMask::Any },
	{ VirtualInput::Key_Pound, kATInputTrigger_Button0+11, VirtualInputMask::Any },
};

static const TargetMapping kPowerPadTargetMappings[] = {
	{ VirtualInput::Stick_AnalogPosX, kATInputTrigger_Axis0, VirtualInputMask::Any },
	{ VirtualInput::Stick_AnalogPosY, kATInputTrigger_Axis0+1, VirtualInputMask::Any },
	{ VirtualInput::Stick_AnalogX, kATInputTrigger_Axis0, VirtualInputMask::Any },
	{ VirtualInput::Stick_AnalogY, kATInputTrigger_Axis0+1, VirtualInputMask::Any },
	{ VirtualInput::Stick_Left, kATInputTrigger_Left, VirtualInputMask::Any },
	{ VirtualInput::Stick_Right, kATInputTrigger_Right, VirtualInputMask::Any },
	{ VirtualInput::Stick_Up, kATInputTrigger_Up, VirtualInputMask::Any },
	{ VirtualInput::Stick_Down, kATInputTrigger_Down, VirtualInputMask::Any },
	{ VirtualInput::Button_0, kATInputTrigger_Button0, VirtualInputMask::Any },
	{ VirtualInput::Button_1, kATInputTrigger_Button0+1, VirtualInputMask::Any },
	{ VirtualInput::Button_2, kATInputTrigger_Button0+1, VirtualInputMask::Mouse },
	{ VirtualInput::Button_2, kATInputTrigger_Button0+4, VirtualInputMask::Mouse },
	{ VirtualInput::Scroll_Vert, kATInputTrigger_Axis0+2 | kATInputTriggerMode_Relative | (1 << kATInputTriggerSpeed_Shift), VirtualInputMask::Mouse },
	{ VirtualInput::Button_2, kATInputTrigger_Button0+2, VirtualInputMask::Gamepad },
	{ VirtualInput::Button_3, kATInputTrigger_Button0+3, VirtualInputMask::Gamepad },
	{ VirtualInput::Shift, kATInputTrigger_Button0+4, VirtualInputMask::Gamepad },
	{ VirtualInput::Shift2, kATInputTrigger_Button0, VirtualInputMask::Gamepad },
	{ VirtualInput::Scroll_Vert, kATInputTrigger_ScrollUp, VirtualInputMask::Gamepad },
};

static const TargetMapping k5200ControllerTargetMappings[] = {
	{ VirtualInput::Stick_AnalogMoveX, kATInputTrigger_Axis0, VirtualInputMask::Any },
	{ VirtualInput::Stick_AnalogMoveY, kATInputTrigger_Axis0+1, VirtualInputMask::Any },
	{ VirtualInput::Stick_AnalogX, kATInputTrigger_Axis0, VirtualInputMask::Any },
	{ VirtualInput::Stick_AnalogY, kATInputTrigger_Axis0+1, VirtualInputMask::Any },
	{ VirtualInput::Stick_Left, kATInputTrigger_Left, VirtualInputMask::Any },
	{ VirtualInput::Stick_Right, kATInputTrigger_Right, VirtualInputMask::Any },
	{ VirtualInput::Stick_Up, kATInputTrigger_Up, VirtualInputMask::Any },
	{ VirtualInput::Stick_Down, kATInputTrigger_Down, VirtualInputMask::Any },
	{ VirtualInput::Button_0, kATInputTrigger_Button0, VirtualInputMask::Any },
	{ VirtualInput::Button_1, kATInputTrigger_Button0+1, VirtualInputMask::Any },
	{ VirtualInput::Key_0, kATInputTrigger_5200_0, VirtualInputMask::Any },
	{ VirtualInput::Key_1, kATInputTrigger_5200_1, VirtualInputMask::Any },
	{ VirtualInput::Key_2, kATInputTrigger_5200_2, VirtualInputMask::Any },
	{ VirtualInput::Key_3, kATInputTrigger_5200_3, VirtualInputMask::Any },
	{ VirtualInput::Key_4, kATInputTrigger_5200_4, VirtualInputMask::Any },
	{ VirtualInput::Key_5, kATInputTrigger_5200_5, VirtualInputMask::Any },
	{ VirtualInput::Key_6, kATInputTrigger_5200_6, VirtualInputMask::Any },
	{ VirtualInput::Key_7, kATInputTrigger_5200_7, VirtualInputMask::Any },
	{ VirtualInput::Key_8, kATInputTrigger_5200_8, VirtualInputMask::Any },
	{ VirtualInput::Key_9, kATInputTrigger_5200_9, VirtualInputMask::Any },
	{ VirtualInput::Key_Star, kATInputTrigger_5200_Star, VirtualInputMask::Any },
	{ VirtualInput::Key_Pound, kATInputTrigger_5200_Pound, VirtualInputMask::Any },
	{ VirtualInput::Key_Start, kATInputTrigger_5200_Start, VirtualInputMask::Any },
	{ VirtualInput::Key_Pause, kATInputTrigger_5200_Pause, VirtualInputMask::Any },
	{ VirtualInput::Key_Reset, kATInputTrigger_5200_Reset, VirtualInputMask::Any },
};

// The full controller type template info table
static const ControllerTypeTemplateInfo kControllerTypeTemplateInfo[] = {
	{ kATInputControllerType_Joystick, "Joystick (CX40)", "Joystick", 0,
	  false, false, 0, 0,
	  ARRAY_AND_COUNT(kJoystickTargetMappings), nullptr, 0 },
	{ kATInputControllerType_Paddle, "Paddle A (CX30)", "Paddle A", 0,
	  true, false,
	  kATInputTriggerMode_Relative | (4 << kATInputTriggerSpeed_Shift) | (6 << kATInputTriggerAccel_Shift),
	  kATInputTriggerMode_Relative | (6 << kATInputTriggerSpeed_Shift) | (10 << kATInputTriggerAccel_Shift),
	  ARRAY_AND_COUNT(kPaddleTargetMappings), nullptr, 0 },
	{ kATInputControllerType_Paddle, "Paddle B (CX30)", "Paddle B", 1,
	  true, false,
	  kATInputTriggerMode_Relative | (4 << kATInputTriggerSpeed_Shift) | (6 << kATInputTriggerAccel_Shift),
	  kATInputTriggerMode_Relative | (6 << kATInputTriggerSpeed_Shift) | (10 << kATInputTriggerAccel_Shift),
	  ARRAY_AND_COUNT(kPaddleTargetMappings), nullptr, 0 },
	{ kATInputControllerType_STMouse, "ST Mouse", nullptr, 0,
	  false, true,
	  kATInputTriggerMode_Relative | (5 << kATInputTriggerSpeed_Shift) | (7 << kATInputTriggerAccel_Shift),
	  kATInputTriggerMode_Relative | (4 << kATInputTriggerSpeed_Shift) | (10 << kATInputTriggerAccel_Shift),
	  ARRAY_AND_COUNT(kSTMouseTargetMappings), nullptr, 0 },
	{ kATInputControllerType_AmigaMouse, "Amiga Mouse", nullptr, 0,
	  false, true,
	  kATInputTriggerMode_Relative | (5 << kATInputTriggerSpeed_Shift) | (7 << kATInputTriggerAccel_Shift),
	  kATInputTriggerMode_Relative | (4 << kATInputTriggerSpeed_Shift) | (10 << kATInputTriggerAccel_Shift),
	  ARRAY_AND_COUNT(kSTMouseTargetMappings), nullptr, 0 },
	{ kATInputControllerType_LightPen, "Light Pen (CX70/CX75)", nullptr, 0,
	  true, false,
	  kATInputTriggerMode_Relative | (5 << kATInputTriggerSpeed_Shift) | (7 << kATInputTriggerAccel_Shift),
	  kATInputTriggerMode_Relative | (4 << kATInputTriggerSpeed_Shift) | (10 << kATInputTriggerAccel_Shift),
	  ARRAY_AND_COUNT(kLightPenTargetMappings), ARRAY_AND_COUNT(kLightPenExtraMappings) },
	{ kATInputControllerType_LightPenStack, "Light Pen (Stack)", nullptr, 0,
	  true, false,
	  kATInputTriggerMode_Relative | (5 << kATInputTriggerSpeed_Shift) | (7 << kATInputTriggerAccel_Shift),
	  kATInputTriggerMode_Relative | (4 << kATInputTriggerSpeed_Shift) | (10 << kATInputTriggerAccel_Shift),
	  ARRAY_AND_COUNT(kLightPenTargetMappings), ARRAY_AND_COUNT(kLightPenExtraMappings) },
	{ kATInputControllerType_LightGun, "Light Gun (XG-1)", nullptr, 1,
	  true, false,
	  kATInputTriggerMode_Relative | (5 << kATInputTriggerSpeed_Shift) | (7 << kATInputTriggerAccel_Shift),
	  kATInputTriggerMode_Relative | (4 << kATInputTriggerSpeed_Shift) | (10 << kATInputTriggerAccel_Shift),
	  ARRAY_AND_COUNT(kLightPenTargetMappings), ARRAY_AND_COUNT(kLightPenExtraMappings) },
	{ kATInputControllerType_Tablet, "Tablet (Atari touch tablet)", nullptr, 0,
	  true, false,
	  kATInputTriggerMode_Relative | (4 << kATInputTriggerSpeed_Shift) | (7 << kATInputTriggerAccel_Shift),
	  kATInputTriggerMode_Relative | (3 << kATInputTriggerSpeed_Shift) | (10 << kATInputTriggerAccel_Shift),
	  ARRAY_AND_COUNT(kTabletTargetMappings), nullptr, 0 },
	{ kATInputControllerType_KoalaPad, "Tablet (KoalaPad)", nullptr, 0,
	  true, false,
	  kATInputTriggerMode_Relative | (4 << kATInputTriggerSpeed_Shift) | (7 << kATInputTriggerAccel_Shift),
	  kATInputTriggerMode_Relative | (3 << kATInputTriggerSpeed_Shift) | (10 << kATInputTriggerAccel_Shift),
	  ARRAY_AND_COUNT(kKoalaPadTargetMappings), nullptr, 0 },
	{ kATInputControllerType_Keypad, "Numerical Keypad (CX85)", nullptr, 0,
	  false, false, 0, 0,
	  ARRAY_AND_COUNT(kKeypadTargetMappings), nullptr, 0 },
	{ kATInputControllerType_Trackball_CX80, "Trak-Ball (CX80)", nullptr, 0,
	  false, true,
	  kATInputTriggerMode_Relative | (4 << kATInputTriggerSpeed_Shift) | (6 << kATInputTriggerAccel_Shift),
	  kATInputTriggerMode_Relative | (10 << kATInputTriggerSpeed_Shift) | (10 << kATInputTriggerAccel_Shift),
	  ARRAY_AND_COUNT(kTrackballCX80TargetMappings), nullptr, 0 },
	{ kATInputControllerType_Driving, "Driving Controller (CX20)", "Driving Controller", 0,
	  false, true,
	  kATInputTriggerMode_Relative | (4 << kATInputTriggerSpeed_Shift) | (6 << kATInputTriggerAccel_Shift),
	  kATInputTriggerMode_Relative | (5 << kATInputTriggerSpeed_Shift),
	  ARRAY_AND_COUNT(kDrivingTargetMappings), nullptr, 0 },
	{ kATInputControllerType_Keyboard, "Keyboard Controller (CX21/23/50)", "Keyboard Controller", 0,
	  false, false, 0, 0,
	  ARRAY_AND_COUNT(kKeyboardCtrlTargetMappings), nullptr, 0 },
	{ kATInputControllerType_PowerPad, "Chalk Board PowerPad", "PowerPad", 0,
	  true, false,
	  kATInputTriggerMode_Relative | (4 << kATInputTriggerSpeed_Shift) | (6 << kATInputTriggerAccel_Shift),
	  kATInputTriggerMode_Relative | (5 << kATInputTriggerSpeed_Shift),
	  ARRAY_AND_COUNT(kPowerPadTargetMappings), nullptr, 0 },
	{ kATInputControllerType_5200Controller, "5200 Controller (CX52)", nullptr, 0,
	  true, false,
	  kATInputTriggerMode_Relative | (4 << kATInputTriggerSpeed_Shift) | (6 << kATInputTriggerAccel_Shift),
	  kATInputTriggerMode_Relative | (3 << kATInputTriggerSpeed_Shift) | (8 << kATInputTriggerAccel_Shift),
	  ARRAY_AND_COUNT(k5200ControllerTargetMappings), nullptr, 0 },
	{ kATInputControllerType_5200Trackball, "5200 Trak-Ball (CX53)", nullptr, 0,
	  false, true,
	  kATInputTriggerMode_Relative | (5 << kATInputTriggerSpeed_Shift) | (6 << kATInputTriggerAccel_Shift),
	  kATInputTriggerMode_Relative | (3 << kATInputTriggerSpeed_Shift) | (8 << kATInputTriggerAccel_Shift),
	  ARRAY_AND_COUNT(k5200ControllerTargetMappings), nullptr, 0 },
};

static constexpr uint32 kControllerTypeTemplateCount = sizeof(kControllerTypeTemplateInfo) / sizeof(kControllerTypeTemplateInfo[0]);

// Create input map from template — equivalent to Windows CreateInputMap()
static vdrefptr<ATInputMap> CreateInputMapFromTemplate(
	ATInputManager *pIM,
	int typeIdx, int portIdx, int sourceIdx, bool usingRelative)
{
	if (typeIdx < 0 || typeIdx >= (int)kControllerTypeTemplateCount)
		return nullptr;
	if (sourceIdx < 0 || sourceIdx >= (int)kInputSourceCount)
		return nullptr;
	if (portIdx < 0 || portIdx > 3)
		portIdx = 0;

	const ControllerTypeTemplateInfo& ctinfo = kControllerTypeTemplateInfo[typeIdx];
	const InputSourceInfo& isinfo = kInputSourceInfo[sourceIdx];

	vdrefptr<ATInputMap> imap(new ATInputMap);

	// Generate name
	VDStringW name;
	const char *srcShort = isinfo.mpShortName ? isinfo.mpShortName : isinfo.mpName;
	const char *ctShort = ctinfo.mpShortName ? ctinfo.mpShortName : ctinfo.mpName;
	const char *modeStr = ctinfo.mbHasAbsoluteRelative ? (usingRelative ? "relative; " : "absolute; ") : "";

	VDStringA nameA;
	nameA.sprintf("%s -> %s (%sport %u)", srcShort, ctShort, modeStr, portIdx + 1);
	name = VDTextU8ToW(nameA);
	imap->SetName(name.c_str());

	uint32 controllerIdx = (uint32)portIdx;
	if (ctinfo.mType == kATInputControllerType_Paddle)
		controllerIdx = controllerIdx * 2 + ctinfo.mIndex;

	imap->AddController(ctinfo.mType, controllerIdx);

	uint32 analogAxesBound = 0;

	for (uint32 ti = 0; ti < ctinfo.mMappingCount; ++ti) {
		const TargetMapping& targetMapping = ctinfo.mMappings[ti];

		for (uint32 si = 0; si < isinfo.mMappingCount; ++si) {
			const InputMapping& inputMapping = isinfo.mMappings[si];
			if (inputMapping.mVirtualInput != targetMapping.mVirtualInput)
				continue;

			if (targetMapping.mVirtualInputMask != VirtualInputMask::Any &&
				isinfo.mVirtualInputMask != targetMapping.mVirtualInputMask)
				continue;

			uint32 targetCode = targetMapping.mTargetCode;

			bool isDigitalAxisMapping = false;
			bool isAnalogAxisMapping = false;
			bool isAbsoluteMapping = false;

			switch (inputMapping.mVirtualInput) {
				case VirtualInput::Stick_Left:
				case VirtualInput::Stick_Right:
				case VirtualInput::Stick_Up:
				case VirtualInput::Stick_Down:
					isDigitalAxisMapping = true;
					break;
				case VirtualInput::Stick_AnalogMoveX:
				case VirtualInput::Stick_AnalogMoveY:
					isAnalogAxisMapping = true;
					if (!usingRelative && ctinfo.mbHasAbsoluteRelative)
						continue;
					break;
				case VirtualInput::Stick_AnalogX:
				case VirtualInput::Stick_AnalogY:
				case VirtualInput::Scroll_Vert:
					isAnalogAxisMapping = true;
					break;
				case VirtualInput::Stick_AnalogBeamX:
				case VirtualInput::Stick_AnalogBeamY:
				case VirtualInput::Stick_AnalogPosX:
				case VirtualInput::Stick_AnalogPosY:
					isAbsoluteMapping = true;
					break;
				default:
					break;
			}

			if (targetCode == kATInputTrigger_Axis0 || targetCode == kATInputTrigger_Axis0+1 ||
				targetCode == kATInputTrigger_Left || targetCode == kATInputTrigger_Right ||
				targetCode == kATInputTrigger_Up || targetCode == kATInputTrigger_Down)
			{
				uint32 axisBit = 0;
				switch (targetCode) {
					case kATInputTrigger_Axis0:
					case kATInputTrigger_Left:
					case kATInputTrigger_Right:
						axisBit = 1;
						break;
					case kATInputTrigger_Axis0+1:
					case kATInputTrigger_Up:
					case kATInputTrigger_Down:
						axisBit = 2;
						break;
				}

				const bool isAxisAnalogBound = (analogAxesBound & axisBit) != 0;
				if (isAxisAnalogBound) {
					if (!isDigitalAxisMapping || isAbsoluteMapping)
						continue;
					if (isDigitalAxisMapping && !isinfo.mbAllowDualADMapping)
						continue;
				} else {
					if (!isDigitalAxisMapping)
						analogAxesBound |= axisBit;
				}
			}

			if (usingRelative || ((isDigitalAxisMapping || isAnalogAxisMapping) && ctinfo.mbRequiresRelative)) {
				if (!(targetCode & kATInputTriggerMode_Mask)) {
					if (isAnalogAxisMapping)
						targetCode |= ctinfo.mAnalogRelativeModifier;
					else if (isDigitalAxisMapping)
						targetCode |= ctinfo.mDigitalRelativeModifier;
				}
			}

			imap->AddMapping(inputMapping.mInputCode, 0, targetCode);
		}
	}

	// Add extra mappings
	for (uint32 i = 0; i < ctinfo.mExtraMappingCount; ++i) {
		const ExtraMapping& em = ctinfo.mExtraMappings[i];
		imap->AddMapping(em.mInputCode, em.mControllerId, em.mCode);
	}

	pIM->AddInputMap(imap);
	return imap;
}

// =========================================================================
// Dialog state — persistent across frames
// =========================================================================

// --- Input Mappings list ---
static int g_editingMapIndex = -1;
static char g_editNameBuf[128] = {};
static int g_selectedMapIndex = -1;
static bool g_inputMappingsWasOpen = false;

// --- Create Input Map popup ---
static bool g_showCreateInputMap = false;
static int g_createCtrlType = 0;
static int g_createPort = 0;
static int g_createSource = 0;
static int g_createAnalogMode = 0; // 0=absolute, 1=relative

// --- Edit Input Map dialog ---
static bool g_showEditInputMap = false;
static vdrefptr<ATInputMap> g_editMap;
static bool g_editMapWasEnabled = false; // track whether map was enabled before editing
static int g_editMapSelectedCtrl = -1;
static int g_editMapSelectedMapping = -1;
static int g_editMapGamepadUnit = 0; // 0=Any, 1+=specific

// Working copy of controllers and mappings for the edit dialog
struct EditMappingInfo {
	uint32 mInputCode;
	uint32 mTargetCode;
};

struct EditControllerInfo {
	ATInputControllerType mType;
	int mUnit;
	uint32 mFlagCheckBits;
	std::vector<EditMappingInfo> mMappings;
};
static std::vector<EditControllerInfo> g_editControllers;

// --- Edit Controller popup ---
static bool g_showEditController = false;
static bool g_editControllerIsNew = false;
static bool g_editControllerAddDefaults = true;
static int g_editControllerTypeIdx = 0;
static int g_editControllerPortIdx = 0;
static int g_editControllerFlag1 = 1; // 0=off, 1=don't care, 2=on
static int g_editControllerFlag2 = 1;
static int g_editControllerTargetIdx = -1; // which controller in g_editControllers we're editing

// --- Edit Mapping popup ---
static bool g_showEditMapping = false;
static bool g_editMappingWasOpen = false; // tracks capture mode for gamepad polling
static bool g_editMappingIsNew = false;
static int g_editMappingCtrlIdx = -1;
static int g_editMappingIdx = -1;
static int g_editMappingSourceIdx = 0;
static int g_editMappingTargetIdx = 0;
static int g_editMappingModeIdx = 0;
static int g_editMappingSpeed = 5;
static int g_editMappingAccel = 0;

// --- Rebind state ---
static bool g_showRebind = false;
static bool g_rebindCaptureActive = false; // tracks whether we enabled capture mode
static int g_rebindCtrlIdx = -1;
static int g_rebindMappingIdx = -1; // current mapping being rebound
static int g_rebindStartIdx = -1;

// --- Input Setup dialog ---

// =========================================================================
// Helper: load edit state from ATInputMap
// =========================================================================

static void LoadEditState(ATInputMap *imap, ATInputManager *pIM) {
	g_editControllers.clear();
	g_editMapSelectedCtrl = -1;
	g_editMapSelectedMapping = -1;

	if (!imap)
		return;

	uint32 mappingCount = imap->GetMappingCount();
	for (uint32 i = 0; i < mappingCount; ++i) {
		const ATInputMap::Mapping& m = imap->GetMapping(i);
		const ATInputMap::Controller& c = imap->GetController(m.mControllerId);
		uint32 flagBits = m.mInputCode & kATInputCode_FlagMask;

		// Find or create controller group
		int ctrlIdx = -1;
		for (int ci = 0; ci < (int)g_editControllers.size(); ++ci) {
			auto& ec = g_editControllers[ci];
			if (ec.mType == c.mType && ec.mUnit == (int)c.mIndex && ec.mFlagCheckBits == flagBits) {
				ctrlIdx = ci;
				break;
			}
		}

		if (ctrlIdx < 0) {
			ctrlIdx = (int)g_editControllers.size();
			EditControllerInfo eci;
			eci.mType = c.mType;
			eci.mUnit = (int)c.mIndex;
			eci.mFlagCheckBits = flagBits;
			g_editControllers.push_back(eci);
		}

		EditMappingInfo mi;
		mi.mInputCode = m.mInputCode & kATInputCode_IdMask;
		mi.mTargetCode = m.mCode;
		g_editControllers[ctrlIdx].mMappings.push_back(mi);
	}

	g_editMapGamepadUnit = imap->GetSpecificInputUnit() + 1;
}

// Save edit state back to ATInputMap
static void SaveEditState(ATInputMap *imap) {
	imap->Clear();

	int nc = 0;
	for (auto& ec : g_editControllers) {
		// Find or create controller in map
		int cid = -1;
		for (int j = 0; j < nc; ++j) {
			const ATInputMap::Controller& existing = imap->GetController(j);
			if (existing.mType == ec.mType && existing.mIndex == (uint32)ec.mUnit) {
				cid = j;
				break;
			}
		}
		if (cid < 0) {
			cid = nc++;
			imap->AddController(ec.mType, ec.mUnit);
		}

		for (auto& mi : ec.mMappings) {
			uint32 inputCode = mi.mInputCode + ec.mFlagCheckBits;
			imap->AddMapping(inputCode, cid, mi.mTargetCode);
		}
	}

	imap->SetSpecificInputUnit(g_editMapGamepadUnit - 1);
}

// =========================================================================
// Render: Edit Mapping popup (source, target, mode, speed/accel)
// =========================================================================

static void RenderEditMappingPopup(ATInputManager *pIM) {
	if (!g_showEditMapping) {
		// Disable capture mode when dialog closes (unless Rebind still needs it)
		if (g_editMappingWasOpen) {
			if (!g_rebindCaptureActive) {
				IATJoystickManager *pJoyMan = g_sim.GetJoystickManager();
				if (pJoyMan)
					pJoyMan->SetCaptureMode(false);
			}
			g_editMappingWasOpen = false;
		}
		return;
	}

	// Enable capture mode when dialog first opens (matches Windows OnLoaded)
	if (!g_editMappingWasOpen) {
		IATJoystickManager *pJoyMan = g_sim.GetJoystickManager();
		if (pJoyMan)
			pJoyMan->SetCaptureMode(true);
		g_editMappingWasOpen = true;
	}

	if (g_editMappingCtrlIdx < 0 || g_editMappingCtrlIdx >= (int)g_editControllers.size()) {
		g_showEditMapping = false;
		return;
	}

	ATInputControllerType ctrlType = g_editControllers[g_editMappingCtrlIdx].mType;
	TargetCodeSet tcs = GetTargetCodes(ctrlType);

	// Clamp indices to valid ranges
	if (g_editMappingSourceIdx < 0 || g_editMappingSourceIdx >= (int)kInputCodeCount)
		g_editMappingSourceIdx = 0;
	if (g_editMappingTargetIdx < 0 || g_editMappingTargetIdx >= (int)tcs.count)
		g_editMappingTargetIdx = 0;
	if (g_editMappingModeIdx < 0 || g_editMappingModeIdx >= (int)kTargetModeCount)
		g_editMappingModeIdx = 0;

	ImGui::SetNextWindowSize(ImVec2(420, 300), ImGuiCond_Appearing);
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	if (!ImGui::Begin("Edit Mapping", &g_showEditMapping, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings)) {
		ImGui::End();
		return;
	}

	if (ATUICheckEscClose()) {
		g_showEditMapping = false;
		ImGui::End();
		return;
	}

	// Source input
	ImGui::Text("Source Input:");
	if (ImGui::BeginCombo("##source", [&]() -> const char * {
		VDStringW name;
		pIM->GetNameForInputCode(kInputCodes[g_editMappingSourceIdx], name);
		static char buf[128];
		VDStringA a = VDTextWToU8(name);
		strncpy(buf, a.c_str(), sizeof(buf) - 1);
		buf[sizeof(buf) - 1] = 0;
		return buf;
	}())) {
		for (uint32 i = 0; i < kInputCodeCount; ++i) {
			VDStringW name;
			pIM->GetNameForInputCode(kInputCodes[i], name);
			VDStringA a = VDTextWToU8(name);
			bool selected = ((int)i == g_editMappingSourceIdx);
			if (ImGui::Selectable(a.c_str(), selected))
				g_editMappingSourceIdx = (int)i;
			if (selected)
				ImGui::SetItemDefaultFocus();
		}
		ImGui::EndCombo();
	}

	// Target
	ImGui::Text("Target:");
	if (ImGui::BeginCombo("##target", [&]() -> const char * {
		if (g_editMappingTargetIdx >= 0 && g_editMappingTargetIdx < (int)tcs.count) {
			VDStringW name;
			pIM->GetNameForTargetCode(tcs.codes[g_editMappingTargetIdx], ctrlType, name);
			static char buf[128];
			VDStringA a = VDTextWToU8(name);
			strncpy(buf, a.c_str(), sizeof(buf) - 1);
			buf[sizeof(buf) - 1] = 0;
			return buf;
		}
		return "None";
	}())) {
		for (uint32 i = 0; i < tcs.count; ++i) {
			VDStringW name;
			pIM->GetNameForTargetCode(tcs.codes[i], ctrlType, name);
			VDStringA a = VDTextWToU8(name);
			bool selected = ((int)i == g_editMappingTargetIdx);
			if (ImGui::Selectable(a.c_str(), selected))
				g_editMappingTargetIdx = (int)i;
			if (selected)
				ImGui::SetItemDefaultFocus();
		}
		ImGui::EndCombo();
	}

	// Mode
	ImGui::Text("Mode:");
	if (ImGui::BeginCombo("##mode", kTargetModeNames[g_editMappingModeIdx])) {
		for (uint32 i = 0; i < kTargetModeCount; ++i) {
			bool selected = ((int)i == g_editMappingModeIdx);
			if (ImGui::Selectable(kTargetModeNames[i], selected))
				g_editMappingModeIdx = (int)i;
			if (selected)
				ImGui::SetItemDefaultFocus();
		}
		ImGui::EndCombo();
	}

	// Speed/Accel (only for modes that use them)
	bool hasSpeed = false;
	switch (kTargetModes[g_editMappingModeIdx]) {
		case kATInputTriggerMode_AutoFire:
		case kATInputTriggerMode_ToggleAF:
		case kATInputTriggerMode_Relative:
			hasSpeed = true;
			break;
	}

	if (hasSpeed) {
		ImGui::Text("Speed:");
		ImGui::SliderInt("##speed", &g_editMappingSpeed, 0, 10);
		ImGui::Text("Acceleration:");
		ImGui::SliderInt("##accel", &g_editMappingAccel, 0, 10);
	}

	// Poll gamepad for source auto-selection (matches Windows 20ms timer)
	IATJoystickManager *pJoyMan = g_sim.GetJoystickManager();
	if (pJoyMan) {
		int unit;
		uint32 digitalInputCode, analogInputCode;
		if (pJoyMan->PollForCapture(unit, digitalInputCode, analogInputCode)) {
			// Prefer analog if target accepts analog input
			int analogIndex = -1;
			int digitalIndex = -1;
			for (uint32 i = 0; i < kInputCodeCount; ++i) {
				if (kInputCodes[i] == digitalInputCode)
					digitalIndex = (int)i;
				if (kInputCodes[i] == analogInputCode)
					analogIndex = (int)i;
			}

			if (analogIndex >= 0 && pIM->IsAnalogTrigger(
					tcs.codes[g_editMappingTargetIdx], ctrlType))
				g_editMappingSourceIdx = analogIndex;
			else if (digitalIndex >= 0)
				g_editMappingSourceIdx = digitalIndex;
		}
	}

	ImGui::Separator();

	if (ImGui::Button("OK", ImVec2(80, 0))) {
		// Apply
		auto& ctrl = g_editControllers[g_editMappingCtrlIdx];
		uint32 inputCode = kInputCodes[g_editMappingSourceIdx];
		uint32 targetCode = tcs.codes[g_editMappingTargetIdx];
		targetCode |= kTargetModes[g_editMappingModeIdx];
		if (hasSpeed) {
			targetCode |= (uint32)g_editMappingSpeed << kATInputTriggerSpeed_Shift;
			targetCode |= (uint32)g_editMappingAccel << kATInputTriggerAccel_Shift;
		}

		if (g_editMappingIsNew) {
			EditMappingInfo mi;
			mi.mInputCode = inputCode;
			mi.mTargetCode = targetCode;
			ctrl.mMappings.push_back(mi);
		} else if (g_editMappingIdx >= 0 && g_editMappingIdx < (int)ctrl.mMappings.size()) {
			ctrl.mMappings[g_editMappingIdx].mInputCode = inputCode;
			ctrl.mMappings[g_editMappingIdx].mTargetCode = targetCode;
		}
		g_showEditMapping = false;
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(80, 0)))
		g_showEditMapping = false;

	ImGui::End();
}

// =========================================================================
// Render: Edit Controller popup
// =========================================================================

static void RenderEditControllerPopup(ATInputManager *pIM) {
	if (!g_showEditController)
		return;

	ImGui::SetNextWindowSize(ImVec2(380, 250), ImGuiCond_Appearing);
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	if (!ImGui::Begin(g_editControllerIsNew ? "Add Controller" : "Edit Controller",
			&g_showEditController, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings)) {
		ImGui::End();
		return;
	}

	if (ATUICheckEscClose()) {
		g_showEditController = false;
		ImGui::End();
		return;
	}

	// Controller type (sorted alphabetically, matches Windows)
	InitControllerTypeSortOrder();
	if (g_editControllerTypeIdx < 0 || g_editControllerTypeIdx >= (int)kControllerTypeEntryCount)
		g_editControllerTypeIdx = 0;
	ImGui::Text("Controller type:");
	if (ImGui::BeginCombo("##ctrltype", kControllerTypeEntries[g_editControllerTypeIdx].mpLabel)) {
		for (uint32 si = 0; si < kControllerTypeEntryCount; ++si) {
			int i = g_controllerTypeSortOrder[si];
			bool selected = (i == g_editControllerTypeIdx);
			if (ImGui::Selectable(kControllerTypeEntries[i].mpLabel, selected))
				g_editControllerTypeIdx = i;
			if (selected)
				ImGui::SetItemDefaultFocus();
		}
		ImGui::EndCombo();
	}

	// Port
	ImGui::Text("Port:");
	if (g_editControllerPortIdx < 0 || g_editControllerPortIdx >= (int)kPortLabelCount)
		g_editControllerPortIdx = 0;
	if (ImGui::BeginCombo("##port", kPortLabels[g_editControllerPortIdx])) {
		for (uint32 i = 0; i < kPortLabelCount; ++i) {
			bool selected = ((int)i == g_editControllerPortIdx);
			if (ImGui::Selectable(kPortLabels[i], selected))
				g_editControllerPortIdx = (int)i;
			if (selected)
				ImGui::SetItemDefaultFocus();
		}
		ImGui::EndCombo();
	}

	// Flag check bits (F1, F2) — tri-state: don't care / off / on
	const char *flagStates[] = { "Off", "Don't care", "On" };
	ImGui::Text("Flag F1:");
	ImGui::SameLine();
	if (ImGui::BeginCombo("##flag1", flagStates[g_editControllerFlag1])) {
		for (int i = 0; i < 3; ++i) {
			if (ImGui::Selectable(flagStates[i], g_editControllerFlag1 == i))
				g_editControllerFlag1 = i;
		}
		ImGui::EndCombo();
	}

	ImGui::Text("Flag F2:");
	ImGui::SameLine();
	if (ImGui::BeginCombo("##flag2", flagStates[g_editControllerFlag2])) {
		for (int i = 0; i < 3; ++i) {
			if (ImGui::Selectable(flagStates[i], g_editControllerFlag2 == i))
				g_editControllerFlag2 = i;
		}
		ImGui::EndCombo();
	}

	// Add default mappings checkbox (only for new controllers)
	if (g_editControllerIsNew)
		ImGui::Checkbox("Add default mappings", &g_editControllerAddDefaults);

	ImGui::Separator();

	if (ImGui::Button("OK", ImVec2(80, 0))) {
		const ControllerTypeEntry& cte = kControllerTypeEntries[g_editControllerTypeIdx];
		ATInputControllerType type = cte.mType;
		int unit = g_editControllerPortIdx * cte.mIndexScale + cte.mIndexOffset;

		uint32 flagBits = 0;
		if (g_editControllerFlag1 == 0)
			flagBits |= kATInputCode_FlagCheck0;
		else if (g_editControllerFlag1 == 2)
			flagBits |= kATInputCode_FlagCheck0 | kATInputCode_FlagValue0;
		if (g_editControllerFlag2 == 0)
			flagBits |= kATInputCode_FlagCheck1;
		else if (g_editControllerFlag2 == 2)
			flagBits |= kATInputCode_FlagCheck1 | kATInputCode_FlagValue1;

		if (g_editControllerIsNew) {
			EditControllerInfo eci;
			eci.mType = type;
			eci.mUnit = unit;
			eci.mFlagCheckBits = flagBits;

			// Add default mappings if requested
			if (g_editControllerAddDefaults) {
				TargetCodeSet tcs = GetTargetCodes(type);
				for (uint32 i = 0; i < tcs.count; ++i) {
					EditMappingInfo mi;
					mi.mInputCode = kATInputCode_None;
					mi.mTargetCode = tcs.codes[i];
					eci.mMappings.push_back(mi);
				}
			}

			g_editControllers.push_back(eci);
			g_editMapSelectedCtrl = (int)g_editControllers.size() - 1;
		} else if (g_editControllerTargetIdx >= 0 && g_editControllerTargetIdx < (int)g_editControllers.size()) {
			auto& ec = g_editControllers[g_editControllerTargetIdx];
			ec.mType = type;
			ec.mUnit = unit;
			ec.mFlagCheckBits = flagBits;
		}

		g_showEditController = false;
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(80, 0)))
		g_showEditController = false;

	ImGui::End();
}

// =========================================================================
// Render: Rebind dialog — captures next keyboard/gamepad input
// =========================================================================

static void RenderRebindPopup(ATInputManager *pIM) {
	if (!g_showRebind)
		return;

	if (g_rebindCtrlIdx < 0 || g_rebindCtrlIdx >= (int)g_editControllers.size()) {
		g_showRebind = false;
		return;
	}

	auto& ctrl = g_editControllers[g_rebindCtrlIdx];
	if (g_rebindMappingIdx < 0 || g_rebindMappingIdx >= (int)ctrl.mMappings.size()) {
		g_showRebind = false;
		return;
	}

	// Show current target being rebound
	VDStringW targetName;
	uint32 targetCode = ctrl.mMappings[g_rebindMappingIdx].mTargetCode & kATInputTrigger_Mask;
	pIM->GetNameForTargetCode(targetCode, ctrl.mType, targetName);
	VDStringA targetNameA = VDTextWToU8(targetName);

	ImGui::SetNextWindowSize(ImVec2(350, 0), ImGuiCond_Always);
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
	if (!ImGui::Begin("Rebind Input", &g_showRebind,
			ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
		ImGui::End();
		return;
	}

	ImGui::Text("Press a key or button for:");
	ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "%s", targetNameA.c_str());
	ImGui::Text("(Shift+Escape to cancel)");

	ImGui::Separator();

	// Check for keyboard input — we need to intercept ImGui's keyboard
	// to capture raw key presses for rebinding.
	//
	// Shift keys are special (matches Windows uiinputrebind.cpp):
	// - Shift is detected on key RELEASE, not press, so Shift+Escape
	//   cancel works without accidentally binding Shift first.
	// - All other keys are detected on press.
	bool captured = false;
	uint32 capturedInputCode = kATInputCode_None;

	// First check: Shift+Escape cancel (must be before any capture)
	if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
		if (ImGui::IsKeyDown(ImGuiKey_LeftShift) || ImGui::IsKeyDown(ImGuiKey_RightShift)) {
			g_showRebind = false;
			ImGui::End();
			return;
		}
		capturedInputCode = kATInputCode_KeyEscape;
		captured = true;
	}

	// Shift keys: detect on RELEASE (Windows defers Shift to WM_KEYUP)
	if (!captured) {
		if (ImGui::IsKeyReleased(ImGuiKey_LeftShift)) {
			capturedInputCode = kATInputCode_KeyLShift;
			captured = true;
		} else if (ImGui::IsKeyReleased(ImGuiKey_RightShift)) {
			capturedInputCode = kATInputCode_KeyRShift;
			captured = true;
		}
	}

	// All other keys: detect on press (excluding Shift/Escape handled above)
	if (!captured) {
		static const struct { ImGuiKey key; uint32 code; } keyMap[] = {
			{ ImGuiKey_LeftCtrl, kATInputCode_KeyLControl },
			{ ImGuiKey_RightCtrl, kATInputCode_KeyRControl },
			{ ImGuiKey_LeftArrow, kATInputCode_KeyLeft },
			{ ImGuiKey_UpArrow, kATInputCode_KeyUp },
			{ ImGuiKey_RightArrow, kATInputCode_KeyRight },
			{ ImGuiKey_DownArrow, kATInputCode_KeyDown },
			{ ImGuiKey_0, kATInputCode_Key0 }, { ImGuiKey_1, kATInputCode_Key1 },
			{ ImGuiKey_2, kATInputCode_Key2 }, { ImGuiKey_3, kATInputCode_Key3 },
			{ ImGuiKey_4, kATInputCode_Key4 }, { ImGuiKey_5, kATInputCode_Key5 },
			{ ImGuiKey_6, kATInputCode_Key6 }, { ImGuiKey_7, kATInputCode_Key7 },
			{ ImGuiKey_8, kATInputCode_Key8 }, { ImGuiKey_9, kATInputCode_Key9 },
			{ ImGuiKey_A, kATInputCode_KeyA+0 }, { ImGuiKey_B, kATInputCode_KeyA+1 },
			{ ImGuiKey_C, kATInputCode_KeyA+2 }, { ImGuiKey_D, kATInputCode_KeyA+3 },
			{ ImGuiKey_E, kATInputCode_KeyA+4 }, { ImGuiKey_F, kATInputCode_KeyA+5 },
			{ ImGuiKey_G, kATInputCode_KeyA+6 }, { ImGuiKey_H, kATInputCode_KeyA+7 },
			{ ImGuiKey_I, kATInputCode_KeyA+8 }, { ImGuiKey_J, kATInputCode_KeyA+9 },
			{ ImGuiKey_K, kATInputCode_KeyA+10 }, { ImGuiKey_L, kATInputCode_KeyA+11 },
			{ ImGuiKey_M, kATInputCode_KeyA+12 }, { ImGuiKey_N, kATInputCode_KeyA+13 },
			{ ImGuiKey_O, kATInputCode_KeyA+14 }, { ImGuiKey_P, kATInputCode_KeyA+15 },
			{ ImGuiKey_Q, kATInputCode_KeyA+16 }, { ImGuiKey_R, kATInputCode_KeyA+17 },
			{ ImGuiKey_S, kATInputCode_KeyA+18 }, { ImGuiKey_T, kATInputCode_KeyA+19 },
			{ ImGuiKey_U, kATInputCode_KeyA+20 }, { ImGuiKey_V, kATInputCode_KeyA+21 },
			{ ImGuiKey_W, kATInputCode_KeyA+22 }, { ImGuiKey_X, kATInputCode_KeyA+23 },
			{ ImGuiKey_Y, kATInputCode_KeyA+24 }, { ImGuiKey_Z, kATInputCode_KeyA+25 },
			{ ImGuiKey_Backspace, kATInputCode_KeyBack },
			{ ImGuiKey_Tab, kATInputCode_KeyTab },
			{ ImGuiKey_Enter, kATInputCode_KeyReturn },
			{ ImGuiKey_Space, kATInputCode_KeySpace },
			{ ImGuiKey_PageUp, kATInputCode_KeyPrior },
			{ ImGuiKey_PageDown, kATInputCode_KeyNext },
			{ ImGuiKey_End, kATInputCode_KeyEnd },
			{ ImGuiKey_Home, kATInputCode_KeyHome },
			{ ImGuiKey_Insert, kATInputCode_KeyInsert },
			{ ImGuiKey_Delete, kATInputCode_KeyDelete },
			{ ImGuiKey_Keypad0, kATInputCode_KeyNumpad0 },
			{ ImGuiKey_Keypad1, kATInputCode_KeyNumpad1 },
			{ ImGuiKey_Keypad2, kATInputCode_KeyNumpad2 },
			{ ImGuiKey_Keypad3, kATInputCode_KeyNumpad3 },
			{ ImGuiKey_Keypad4, kATInputCode_KeyNumpad4 },
			{ ImGuiKey_Keypad5, kATInputCode_KeyNumpad5 },
			{ ImGuiKey_Keypad6, kATInputCode_KeyNumpad6 },
			{ ImGuiKey_Keypad7, kATInputCode_KeyNumpad7 },
			{ ImGuiKey_Keypad8, kATInputCode_KeyNumpad8 },
			{ ImGuiKey_Keypad9, kATInputCode_KeyNumpad9 },
			{ ImGuiKey_KeypadEnter, kATInputCode_KeyNumpadEnter },
			{ ImGuiKey_KeypadMultiply, kATInputCode_KeyMultiply },
			{ ImGuiKey_KeypadAdd, kATInputCode_KeyAdd },
			{ ImGuiKey_KeypadSubtract, kATInputCode_KeySubtract },
			{ ImGuiKey_KeypadDecimal, kATInputCode_KeyDecimal },
			{ ImGuiKey_KeypadDivide, kATInputCode_KeyDivide },
			{ ImGuiKey_F1, kATInputCode_KeyF1 }, { ImGuiKey_F2, kATInputCode_KeyF2 },
			{ ImGuiKey_F3, kATInputCode_KeyF3 }, { ImGuiKey_F4, kATInputCode_KeyF4 },
			{ ImGuiKey_F5, kATInputCode_KeyF5 }, { ImGuiKey_F6, kATInputCode_KeyF6 },
			{ ImGuiKey_F7, kATInputCode_KeyF7 }, { ImGuiKey_F8, kATInputCode_KeyF8 },
			{ ImGuiKey_F9, kATInputCode_KeyF9 }, { ImGuiKey_F10, kATInputCode_KeyF10 },
			{ ImGuiKey_F11, kATInputCode_KeyF11 }, { ImGuiKey_F12, kATInputCode_KeyF12 },
			{ ImGuiKey_Semicolon, kATInputCode_KeyOem1 },
			{ ImGuiKey_Equal, kATInputCode_KeyOemPlus },
			{ ImGuiKey_Comma, kATInputCode_KeyOemComma },
			{ ImGuiKey_Minus, kATInputCode_KeyOemMinus },
			{ ImGuiKey_Period, kATInputCode_KeyOemPeriod },
			{ ImGuiKey_Slash, kATInputCode_KeyOem2 },
			{ ImGuiKey_GraveAccent, kATInputCode_KeyOem3 },
			{ ImGuiKey_LeftBracket, kATInputCode_KeyOem4 },
			{ ImGuiKey_Backslash, kATInputCode_KeyOem5 },
			{ ImGuiKey_RightBracket, kATInputCode_KeyOem6 },
			{ ImGuiKey_Apostrophe, kATInputCode_KeyOem7 },
		};

		for (const auto& km : keyMap) {
			if (ImGui::IsKeyPressed(km.key, false)) {
				capturedInputCode = km.code;
				captured = true;
				break;
			}
		}
	}

	// Also poll gamepad via IATJoystickManager
	IATJoystickManager *pJoyMan = g_sim.GetJoystickManager();
	if (!captured && pJoyMan) {
		int unit;
		uint32 digitalCode, analogCode;
		if (pJoyMan->PollForCapture(unit, digitalCode, analogCode)) {
			// Prefer analog if target is analog
			bool targetIsAnalog = pIM->IsAnalogTrigger(
				ctrl.mMappings[g_rebindMappingIdx].mTargetCode, ctrl.mType);
			if (targetIsAnalog && analogCode != kATInputCode_None)
				capturedInputCode = analogCode;
			else if (digitalCode != kATInputCode_None)
				capturedInputCode = digitalCode;
			captured = capturedInputCode != kATInputCode_None;
		}
	}

	if (captured) {
		ctrl.mMappings[g_rebindMappingIdx].mInputCode = capturedInputCode;

		// Move to next mapping
		g_rebindMappingIdx++;
		if (g_rebindMappingIdx >= (int)ctrl.mMappings.size())
			g_showRebind = false;
	}

	// Buttons — disable when a key was captured this frame to prevent
	// double-advance (captured Enter/Space would also activate the button)
	ImGui::BeginDisabled(captured);
	if (ImGui::Button("Next", ImVec2(80, 0))) {
		// Accept None, move to next
		g_rebindMappingIdx++;
		if (g_rebindMappingIdx >= (int)ctrl.mMappings.size())
			g_showRebind = false;
	}
	ImGui::SameLine();
	if (ImGui::Button("Stop", ImVec2(80, 0)))
		g_showRebind = false;
	ImGui::EndDisabled();

	// Status
	ImGui::SameLine();
	char statusBuf[32];
	snprintf(statusBuf, sizeof(statusBuf), "%d / %d",
		g_rebindMappingIdx + 1, (int)ctrl.mMappings.size());
	ImGui::Text("%s", statusBuf);

	ImGui::End();
}

// =========================================================================
// Render: Edit Input Map dialog
// =========================================================================

static void RenderEditInputMapDialog(ATInputManager *pIM) {
	if (!g_showEditInputMap)
		return;

	ImGui::SetNextWindowSize(ImVec2(600, 500), ImGuiCond_Appearing);
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	if (!ImGui::Begin("Edit Input Map", &g_showEditInputMap, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings)) {
		ImGui::End();
		return;
	}

	if (ATUICheckEscClose()) {
		g_showEditInputMap = false;
		ImGui::End();
		return;
	}

	// Gamepad unit selection
	{
		const char *unitLabels[8];
		char unitBufs[8][64];
		unitLabels[0] = "Any";
		if (g_editMapGamepadUnit < 0 || g_editMapGamepadUnit > 7)
			g_editMapGamepadUnit = 0;
		for (int i = 0; i < 7; ++i) {
			const wchar_t *name = pIM->GetInputUnitName(i);
			if (name) {
				VDStringA a = VDTextWToU8(VDStringW(name));
				snprintf(unitBufs[i], sizeof(unitBufs[i]), "Game controller %d [%s]", i+1, a.c_str());
			} else {
				snprintf(unitBufs[i], sizeof(unitBufs[i]), "Game controller %d", i+1);
			}
			unitLabels[i+1] = unitBufs[i];
		}

		ImGui::Text("Gamepad:");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(250);
		if (ImGui::BeginCombo("##gamepad", unitLabels[g_editMapGamepadUnit])) {
			for (int i = 0; i < 8; ++i) {
				if (ImGui::Selectable(unitLabels[i], g_editMapGamepadUnit == i))
					g_editMapGamepadUnit = i;
			}
			ImGui::EndCombo();
		}
	}

	ImGui::Separator();

	// Tree view of controllers and mappings
	float footerHeight = ImGui::GetFrameHeightWithSpacing() * 2 + ImGui::GetStyle().ItemSpacing.y + 4.0f;

	ImGui::BeginChild("##tree", ImVec2(0, -footerHeight), ImGuiChildFlags_Borders);

	for (int ci = 0; ci < (int)g_editControllers.size(); ++ci) {
		auto& ec = g_editControllers[ci];
		VDStringA ctrlText = FormatControllerText(ec.mType, ec.mUnit, ec.mFlagCheckBits);

		ImGuiTreeNodeFlags nodeFlags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_DefaultOpen;
		if (ci == g_editMapSelectedCtrl && g_editMapSelectedMapping < 0)
			nodeFlags |= ImGuiTreeNodeFlags_Selected;

		bool open = ImGui::TreeNodeEx((void *)(intptr_t)(ci + 1000), nodeFlags, "%s", ctrlText.c_str());

		if (ImGui::IsItemClicked()) {
			g_editMapSelectedCtrl = ci;
			g_editMapSelectedMapping = -1;
		}

		// Double-click to edit controller
		if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
			g_showEditController = true;
			g_editControllerIsNew = false;
			g_editControllerTargetIdx = ci;

			// Find matching controller type entry
			g_editControllerTypeIdx = 0;
			for (uint32 k = 0; k < kControllerTypeEntryCount; ++k) {
				const ControllerTypeEntry& cte = kControllerTypeEntries[k];
				if (cte.mType == ec.mType) {
					if (cte.mIndexScale == 0 || ec.mUnit % cte.mIndexScale == cte.mIndexOffset) {
						g_editControllerTypeIdx = (int)k;
						break;
					}
				}
			}
			const ControllerTypeEntry& cte = kControllerTypeEntries[g_editControllerTypeIdx];
			g_editControllerPortIdx = cte.mIndexScale ? ec.mUnit / cte.mIndexScale : 0;

			g_editControllerFlag1 = 1;
			if (ec.mFlagCheckBits & kATInputCode_FlagCheck0)
				g_editControllerFlag1 = (ec.mFlagCheckBits & kATInputCode_FlagValue0) ? 2 : 0;
			g_editControllerFlag2 = 1;
			if (ec.mFlagCheckBits & kATInputCode_FlagCheck1)
				g_editControllerFlag2 = (ec.mFlagCheckBits & kATInputCode_FlagValue1) ? 2 : 0;
		}

		if (open) {
			for (int mi = 0; mi < (int)ec.mMappings.size(); ++mi) {
				auto& mapping = ec.mMappings[mi];

				VDStringW targetName, inputName;
				pIM->GetNameForTargetCode(mapping.mTargetCode & kATInputTrigger_Mask, ec.mType, targetName);
				pIM->GetNameForInputCode(mapping.mInputCode, inputName);

				VDStringA label = VDTextWToU8(targetName);
				label += " -> ";
				label += VDTextWToU8(inputName);

				uint32 mode = mapping.mTargetCode & kATInputTriggerMode_Mask;
				if (mode) {
					label += " (";
					ATGetInputTriggerModeNameA(mapping.mTargetCode, true, label);
					label += ')';
				}

				ImGuiTreeNodeFlags leafFlags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
				if (ci == g_editMapSelectedCtrl && mi == g_editMapSelectedMapping)
					leafFlags |= ImGuiTreeNodeFlags_Selected;

				ImGui::PushID(ci);
				ImGui::TreeNodeEx((void *)(intptr_t)mi, leafFlags, "%s", label.c_str());

				if (ImGui::IsItemClicked()) {
					g_editMapSelectedCtrl = ci;
					g_editMapSelectedMapping = mi;
				}

				// Double-click to edit mapping
				if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
					g_showEditMapping = true;
					g_editMappingIsNew = false;
					g_editMappingCtrlIdx = ci;
					g_editMappingIdx = mi;

					// Find source index
					g_editMappingSourceIdx = 0;
					for (uint32 k = 0; k < kInputCodeCount; ++k) {
						if (kInputCodes[k] == (mapping.mInputCode & 0xFFFF)) {
							g_editMappingSourceIdx = (int)k;
							break;
						}
					}

					// Find target index
					TargetCodeSet tcs = GetTargetCodes(ec.mType);
					g_editMappingTargetIdx = 0;
					for (uint32 k = 0; k < tcs.count; ++k) {
						if (tcs.codes[k] == (mapping.mTargetCode & kATInputTrigger_Mask)) {
							g_editMappingTargetIdx = (int)k;
							break;
						}
					}

					// Find mode
					g_editMappingModeIdx = 0;
					for (uint32 k = 0; k < kTargetModeCount; ++k) {
						if (kTargetModes[k] == (mapping.mTargetCode & kATInputTriggerMode_Mask)) {
							g_editMappingModeIdx = (int)k;
							break;
						}
					}

					g_editMappingSpeed = (mapping.mTargetCode & kATInputTriggerSpeed_Mask) >> kATInputTriggerSpeed_Shift;
					g_editMappingAccel = (mapping.mTargetCode & kATInputTriggerAccel_Mask) >> kATInputTriggerAccel_Shift;
				}
				ImGui::PopID();
			}
			ImGui::TreePop();
		}
	}

	ImGui::EndChild();

	// Action buttons
	bool hasCtrlSelection = (g_editMapSelectedCtrl >= 0 && g_editMapSelectedCtrl < (int)g_editControllers.size());

	if (ImGui::Button("Add Controller")) {
		g_showEditController = true;
		g_editControllerIsNew = true;
		g_editControllerAddDefaults = true;
		g_editControllerTypeIdx = 0;
		g_editControllerPortIdx = 0;
		g_editControllerFlag1 = 1;
		g_editControllerFlag2 = 1;
	}
	ImGui::SameLine();

	if (ImGui::Button("Add Mapping") && hasCtrlSelection) {
		int ci = g_editMapSelectedCtrl;
		g_showEditMapping = true;
		g_editMappingIsNew = true;
		g_editMappingCtrlIdx = ci;
		g_editMappingIdx = -1;
		g_editMappingSourceIdx = 0;
		g_editMappingTargetIdx = 0;
		g_editMappingModeIdx = 0;
		g_editMappingSpeed = 5;
		g_editMappingAccel = 0;
	}
	ImGui::SameLine();

	if (ImGui::Button("Delete") && hasCtrlSelection) {
		if (g_editMapSelectedMapping >= 0) {
			auto& ctrl = g_editControllers[g_editMapSelectedCtrl];
			if (g_editMapSelectedMapping < (int)ctrl.mMappings.size()) {
				ctrl.mMappings.erase(ctrl.mMappings.begin() + g_editMapSelectedMapping);
				g_editMapSelectedMapping = -1;
			}
		} else {
			g_editControllers.erase(g_editControllers.begin() + g_editMapSelectedCtrl);
			g_editMapSelectedCtrl = -1;
			g_editMapSelectedMapping = -1;
		}
	}
	ImGui::SameLine();

	if (ImGui::Button("Rebind") && hasCtrlSelection) {
		int ci = g_editMapSelectedCtrl;
		int startIdx = 0;

		// If a mapping is selected, start from that mapping
		if (g_editMapSelectedMapping >= 0)
			startIdx = g_editMapSelectedMapping;

		if (!g_editControllers[ci].mMappings.empty()) {
			g_showRebind = true;
			g_rebindCtrlIdx = ci;
			g_rebindMappingIdx = startIdx;
			g_rebindStartIdx = startIdx;

			// Enable capture mode on joystick manager
			IATJoystickManager *pJoyMan = g_sim.GetJoystickManager();
			if (pJoyMan) {
				pJoyMan->SetCaptureMode(true);
				g_rebindCaptureActive = true;
			}
		}
	}

	// Disable capture mode when rebind closes (unless Edit Mapping still needs it)
	if (!g_showRebind && g_rebindCaptureActive) {
		if (!g_editMappingWasOpen) {
			IATJoystickManager *pJoyMan = g_sim.GetJoystickManager();
			if (pJoyMan)
				pJoyMan->SetCaptureMode(false);
		}
		g_rebindCaptureActive = false;
	}

	// OK / Cancel
	ImGui::Separator();
	float bw = 80.0f;
	float spacing = ImGui::GetStyle().ItemSpacing.x;
	ImGui::SetCursorPosX(ImGui::GetWindowWidth() - bw * 2 - spacing - ImGui::GetStyle().WindowPadding.x);

	if (ImGui::Button("OK", ImVec2(bw, 0))) {
		// Save back to map
		SaveEditState(g_editMap);
		g_showEditInputMap = false;
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(bw, 0))) {
		g_showEditInputMap = false;
	}

	// Close sub-dialogs when edit dialog closes (via X button or OK/Cancel)
	if (!g_showEditInputMap) {
		g_showEditController = false;
		g_showEditMapping = false;
		g_showRebind = false;
	}

	ImGui::End();

	// Render sub-dialogs
	RenderEditControllerPopup(pIM);
	RenderEditMappingPopup(pIM);
	RenderRebindPopup(pIM);
}

// =========================================================================
// Render: Create Input Map wizard
// =========================================================================

static void RenderCreateInputMapPopup(ATInputManager *pIM) {
	if (!g_showCreateInputMap)
		return;

	ImGui::SetNextWindowSize(ImVec2(420, 260), ImGuiCond_Appearing);
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	if (!ImGui::Begin("Create Input Map", &g_showCreateInputMap, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings)) {
		ImGui::End();
		return;
	}

	if (ATUICheckEscClose()) {
		g_showCreateInputMap = false;
		ImGui::End();
		return;
	}

	// Controller type
	ImGui::Text("Controller type:");
	if (ImGui::BeginCombo("##ctrltype", kControllerTypeTemplateInfo[g_createCtrlType].mpName)) {
		for (uint32 i = 0; i < kControllerTypeTemplateCount; ++i) {
			bool selected = ((int)i == g_createCtrlType);
			if (ImGui::Selectable(kControllerTypeTemplateInfo[i].mpName, selected))
				g_createCtrlType = (int)i;
			if (selected)
				ImGui::SetItemDefaultFocus();
		}
		ImGui::EndCombo();
	}

	// Port
	ImGui::Text("Controller port:");
	const char *portNames[] = { "Port 1", "Port 2", "Port 3", "Port 4" };
	if (ImGui::BeginCombo("##port", portNames[g_createPort])) {
		for (int i = 0; i < 4; ++i) {
			if (ImGui::Selectable(portNames[i], g_createPort == i))
				g_createPort = i;
		}
		ImGui::EndCombo();
	}

	// Analog mode (only if controller supports it)
	bool hasAnalog = kControllerTypeTemplateInfo[g_createCtrlType].mbHasAbsoluteRelative;
	if (hasAnalog) {
		ImGui::Text("Analog control mode:");
		const char *analogModes[] = { "Absolute - Map input position directly",
			"Relative - Gradually move in input direction" };
		if (ImGui::BeginCombo("##analogmode", analogModes[g_createAnalogMode])) {
			for (int i = 0; i < 2; ++i) {
				if (ImGui::Selectable(analogModes[i], g_createAnalogMode == i))
					g_createAnalogMode = i;
			}
			ImGui::EndCombo();
		}
	}

	// Input source
	ImGui::Text("Input source:");
	if (ImGui::BeginCombo("##source", kInputSourceInfo[g_createSource].mpName)) {
		for (uint32 i = 0; i < kInputSourceCount; ++i) {
			bool selected = ((int)i == g_createSource);
			if (ImGui::Selectable(kInputSourceInfo[i].mpName, selected))
				g_createSource = (int)i;
			if (selected)
				ImGui::SetItemDefaultFocus();
		}
		ImGui::EndCombo();
	}

	ImGui::Separator();

	if (ImGui::Button("OK", ImVec2(80, 0))) {
		vdrefptr<ATInputMap> newMap = CreateInputMapFromTemplate(
			pIM, g_createCtrlType, g_createPort, g_createSource, g_createAnalogMode > 0);
		if (newMap)
			pIM->ActivateInputMap(newMap, true);
		g_showCreateInputMap = false;
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(80, 0)))
		g_showCreateInputMap = false;

	ImGui::End();
}

// =========================================================================
// Input Mappings list dialog
// =========================================================================

void ATUIRenderInputMappings(ATSimulator &sim, ATUIState &state) {
	if (!state.showInputMappings) {
		// Clean up if dialog was closed while edit sub-dialog was open
		if (g_editMap) {
			ATInputManager *pIM = sim.GetInputManager();
			pIM->ActivateInputMap(g_editMap, g_editMapWasEnabled);
			g_editMap = nullptr;
			g_showEditInputMap = false;
			g_showEditController = false;
			g_showEditMapping = false;
			g_showRebind = false;
			g_showCreateInputMap = false;
		}
		g_inputMappingsWasOpen = false;
		return;
	}

	// Reset selection state when dialog first opens
	if (!g_inputMappingsWasOpen) {
		g_selectedMapIndex = -1;
		g_editingMapIndex = -1;
		g_inputMappingsWasOpen = true;
	}

	ImGui::SetNextWindowSize(ImVec2(580, 420), ImGuiCond_Appearing);
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	if (!ImGui::Begin("Input Mappings", &state.showInputMappings,
			ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings)) {
		ImGui::End();
		if (!state.showInputMappings)
			g_inputMappingsWasOpen = false;
		return;
	}

	if (ATUICheckEscClose()) {
		state.showInputMappings = false;
		g_inputMappingsWasOpen = false;
		ImGui::End();
		return;
	}

	ATInputManager *pIM = sim.GetInputManager();
	uint32 mapCount = pIM->GetInputMapCount();

	// Clamp selection to valid range
	if (g_selectedMapIndex >= (int)mapCount)
		g_selectedMapIndex = (int)mapCount - 1;

	// Reserve space for bottom buttons (2 rows + separator)
	float footerHeight = ImGui::GetFrameHeightWithSpacing() * 2
		+ ImGui::GetStyle().ItemSpacing.y + 4.0f;

	// Maps table
	if (ImGui::BeginTable("InputMapsList", 4,
			ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg |
			ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersInnerV |
			ImGuiTableFlags_Resizable,
			ImVec2(0, -footerHeight))) {

		ImGui::TableSetupScrollFreeze(0, 1);
		ImGui::TableSetupColumn("On", ImGuiTableColumnFlags_WidthFixed, 30.0f);
		ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableSetupColumn("Controllers", ImGuiTableColumnFlags_WidthStretch, 0.6f);
		ImGui::TableSetupColumn("Quick", ImGuiTableColumnFlags_WidthFixed, 42.0f);
		ImGui::TableHeadersRow();

		for (uint32 i = 0; i < mapCount; ++i) {
			vdrefptr<ATInputMap> imap;
			if (!pIM->GetInputMapByIndex(i, ~imap))
				continue;

			ImGui::TableNextRow();
			ImGui::PushID((int)i);

			// Column 0: Enable checkbox
			ImGui::TableNextColumn();
			bool enabled = pIM->IsInputMapEnabled(imap);
			if (ImGui::Checkbox("##en", &enabled))
				pIM->ActivateInputMap(imap, enabled);

			// Column 1: Name (editable on double-click)
			ImGui::TableNextColumn();
			if (g_editingMapIndex == (int)i) {
				ImGui::SetNextItemWidth(-1);
				if (ImGui::InputText("##name", g_editNameBuf, sizeof(g_editNameBuf),
						ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)) {
					imap->SetName(VDTextU8ToW(g_editNameBuf, -1).c_str());
					g_editingMapIndex = -1;
				}
				if (!ImGui::IsItemActive() && !ImGui::IsItemFocused() && g_editingMapIndex == (int)i) {
					imap->SetName(VDTextU8ToW(g_editNameBuf, -1).c_str());
					g_editingMapIndex = -1;
				}
			} else {
				VDStringA nameU8 = VDTextWToU8(VDStringW(imap->GetName()));
				bool selected = ((int)i == g_selectedMapIndex);
				if (ImGui::Selectable(nameU8.c_str(), selected,
						ImGuiSelectableFlags_AllowDoubleClick)) {
					g_selectedMapIndex = (int)i;
					if (ImGui::IsMouseDoubleClicked(0)) {
						// Double-click opens Edit dialog (matches Windows behavior)
						g_editMap = imap;
						g_showEditInputMap = true;
						g_editMapWasEnabled = pIM->IsInputMapEnabled(imap);

						// Temporarily deactivate map while editing
						if (g_editMapWasEnabled)
							pIM->ActivateInputMap(imap, false);

						LoadEditState(imap, pIM);
					}
				}
				// Right-click or F2 to start inline name editing
				if (selected && ImGui::IsItemHovered() && ImGui::IsKeyPressed(ImGuiKey_F2)) {
					g_editingMapIndex = (int)i;
					strncpy(g_editNameBuf, nameU8.c_str(), sizeof(g_editNameBuf) - 1);
					g_editNameBuf[sizeof(g_editNameBuf) - 1] = 0;
				}
			}

			// Column 2: Controller types summary
			ImGui::TableNextColumn();
			{
				VDStringA ctrlSummary;
				uint32 cc = imap->GetControllerCount();
				for (uint32 c = 0; c < cc; ++c) {
					const auto& ctrl = imap->GetController(c);
					if (c > 0) ctrlSummary += ", ";
					ctrlSummary += ATUIGetControllerTypeName(ctrl.mType);
					if (ctrl.mIndex > 0) {
						char buf[16];
						snprintf(buf, sizeof(buf), " P%u", ctrl.mIndex + 1);
						ctrlSummary += buf;
					}
				}
				ImGui::TextUnformatted(ctrlSummary.c_str());
			}

			// Column 3: Quick map checkbox
			ImGui::TableNextColumn();
			bool quick = imap->IsQuickMap();
			if (ImGui::Checkbox("##qm", &quick))
				imap->SetQuickMap(quick);

			ImGui::PopID();
		}

		ImGui::EndTable();
	}

	// Re-activate map when edit dialog closes (restore original enabled state)
	if (!g_showEditInputMap && g_editMap) {
		pIM->ActivateInputMap(g_editMap, g_editMapWasEnabled);
		g_editMap = nullptr;
	}

	// Action buttons
	bool hasSelection = (g_selectedMapIndex >= 0 && g_selectedMapIndex < (int)mapCount);

	if (ImGui::Button("Add")) {
		vdrefptr<ATInputMap> newMap(new ATInputMap);
		VDStringW name;
		name.sprintf(L"Input map %d", pIM->GetInputMapCount() + 1);
		newMap->SetName(name.c_str());
		pIM->AddInputMap(newMap);
		pIM->ActivateInputMap(newMap, true);
	}
	ImGui::SameLine();

	if (ImGui::Button("Clone") && hasSelection) {
		vdrefptr<ATInputMap> srcMap;
		if (pIM->GetInputMapByIndex(g_selectedMapIndex, ~srcMap)) {
			vdrefptr<ATInputMap> cloned(new ATInputMap);
			VDStringW cloneName(srcMap->GetName());
			cloneName += L" (copy)";
			cloned->SetName(cloneName.c_str());

			for (uint32 c = 0; c < srcMap->GetControllerCount(); ++c) {
				const auto& ctrl = srcMap->GetController(c);
				cloned->AddController(ctrl.mType, ctrl.mIndex);
			}
			for (uint32 m = 0; m < srcMap->GetMappingCount(); ++m) {
				const auto& mapping = srcMap->GetMapping(m);
				cloned->AddMapping(mapping.mInputCode, mapping.mControllerId, mapping.mCode);
			}
			cloned->SetQuickMap(srcMap->IsQuickMap());
			cloned->SetSpecificInputUnit(srcMap->GetSpecificInputUnit());

			pIM->AddInputMap(cloned);
			pIM->ActivateInputMap(cloned, true);
		}
	}
	ImGui::SameLine();

	if (ImGui::Button("Edit...") && hasSelection) {
		vdrefptr<ATInputMap> imap;
		if (pIM->GetInputMapByIndex(g_selectedMapIndex, ~imap)) {
			g_editMap = imap;
			g_showEditInputMap = true;
			g_editMapWasEnabled = pIM->IsInputMapEnabled(imap);

			if (g_editMapWasEnabled)
				pIM->ActivateInputMap(imap, false);

			LoadEditState(imap, pIM);
		}
	}
	ImGui::SameLine();

	if (ImGui::Button("Delete") && hasSelection) {
		g_editingMapIndex = -1;
		vdrefptr<ATInputMap> delMap;
		if (pIM->GetInputMapByIndex(g_selectedMapIndex, ~delMap)) {
			pIM->RemoveInputMap(delMap);
			if (g_selectedMapIndex >= (int)pIM->GetInputMapCount())
				g_selectedMapIndex = (int)pIM->GetInputMapCount() - 1;
		}
	}
	ImGui::SameLine();

	// Presets dropdown
	if (ImGui::Button("Presets..."))
		ImGui::OpenPopup("PresetMenu");

	if (ImGui::BeginPopup("PresetMenu")) {
		uint32 presetCount = pIM->GetPresetInputMapCount();
		for (uint32 i = 0; i < presetCount; ++i) {
			vdrefptr<ATInputMap> preset;
			if (pIM->GetPresetInputMapByIndex(i, ~preset)) {
				VDStringA name = VDTextWToU8(VDStringW(preset->GetName()));
				if (ImGui::MenuItem(name.c_str())) {
					pIM->AddInputMap(preset);
					pIM->ActivateInputMap(preset, true);
				}
			}
		}

		ImGui::Separator();
		if (ImGui::MenuItem("Create From Template...")) {
			g_showCreateInputMap = true;
			// Default to joystick or 5200 based on mode
			g_createCtrlType = 0;
			for (uint32 i = 0; i < kControllerTypeTemplateCount; ++i) {
				if (pIM->Is5200Mode()) {
					if (kControllerTypeTemplateInfo[i].mType == kATInputControllerType_5200Controller) {
						g_createCtrlType = (int)i;
						break;
					}
				} else {
					if (kControllerTypeTemplateInfo[i].mType == kATInputControllerType_Joystick) {
						g_createCtrlType = (int)i;
						break;
					}
				}
			}
			g_createPort = 0;
			g_createSource = 0;
			g_createAnalogMode = 0;
		}

		ImGui::EndPopup();
	}
	ImGui::SameLine();

	if (ImGui::Button("Reset All"))
		ImGui::OpenPopup("ConfirmReset");

	if (ImGui::BeginPopup("ConfirmReset")) {
		ImGui::Text("Remove all input maps and load defaults?");
		if (ImGui::Button("Yes")) {
			g_editingMapIndex = -1;
			g_selectedMapIndex = -1;
			pIM->ResetToDefaults();
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("No"))
			ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}

	// OK button
	ImGui::Separator();
	float buttonWidth = 80.0f;
	ImGui::SetCursorPosX(ImGui::GetWindowWidth() - buttonWidth - ImGui::GetStyle().WindowPadding.x);
	if (ImGui::Button("OK", ImVec2(buttonWidth, 0))) {
		state.showInputMappings = false;
		g_inputMappingsWasOpen = false;
		// Save input maps to registry
		VDRegistryAppKey key("InputMaps", true);
		pIM->SaveMaps(key);
		pIM->SaveSelections(key);
	}

	// Track dialog close via X button
	if (!state.showInputMappings)
		g_inputMappingsWasOpen = false;

	ImGui::End();

	// Render sub-dialogs
	RenderCreateInputMapPopup(pIM);
	RenderEditInputMapDialog(pIM);
}

