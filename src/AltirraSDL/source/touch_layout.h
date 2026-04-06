//	AltirraSDL - Adaptive touch control layout engine
//	Calculates control zone positions for portrait/landscape orientations.
//	All coordinates are normalized (0.0-1.0) fractions of the screen.

#pragma once

#include <vd2/system/vdtypes.h>

struct ATTouchRect {
	float x0, y0, x1, y1;

	float Width() const { return x1 - x0; }
	float Height() const { return y1 - y0; }
	float CenterX() const { return (x0 + x1) * 0.5f; }
	float CenterY() const { return (y0 + y1) * 0.5f; }
	bool Contains(float px, float py) const {
		return px >= x0 && px <= x1 && py >= y0 && py <= y1;
	}
};

enum class ATTouchControlSize : int {
	Small = 0,
	Medium = 1,
	Large = 2
};

enum class ATTouchJoystickStyle : int {
	Analog = 0,   // Free-floating knob anchored at touch-down point (default).
	DPad8  = 1,   // 8-way pizza-sliced dpad anchored at a fixed point.
	DPad4  = 2,   // 4-way pizza-sliced dpad (cardinal snap) at a fixed point.
};

struct ATTouchLayoutConfig {
	ATTouchControlSize controlSize = ATTouchControlSize::Medium;
	ATTouchJoystickStyle joystickStyle = ATTouchJoystickStyle::Analog;
	float controlOpacity = 0.5f;
	bool hapticEnabled = true;
	float contentScale = 1.0f;		// DPI scale factor from SDL_GetDisplayContentScale()
};

struct ATTouchLayoutInsets {
	int top = 0;
	int bottom = 0;
	int left = 0;
	int right = 0;
};

struct ATTouchLayout {
	// Screen dimensions (pixels)
	int screenW = 0;
	int screenH = 0;
	bool landscape = true;

	// System UI safe-area insets (status bar, nav bar, display cutout)
	// applied to all zones.  Zero on desktop.
	ATTouchLayoutInsets insets;

	// Normalized zones
	ATTouchRect topBar;        // Console keys + hamburger
	ATTouchRect joystickZone;  // Left thumb area
	ATTouchRect fireZone;      // Right thumb area
	ATTouchRect displayArea;   // Emulator output

	// Button positions (pixel coordinates, calculated from zones + screen size)
	// Console keys
	ATTouchRect btnStart;
	ATTouchRect btnSelect;
	ATTouchRect btnOption;
	ATTouchRect btnMenu;       // Hamburger icon

	// Fire buttons
	ATTouchRect btnFireA;
	ATTouchRect btnFireB;

	// Joystick parameters
	float joyMaxRadius;        // Max joystick displacement in pixels
	float joyDeadZone;         // Dead zone in pixels

	// Last-applied config (for detecting when recalculation is needed)
	ATTouchControlSize lastControlSize = ATTouchControlSize::Medium;
	float lastContentScale = 0.0f;
	ATTouchLayoutInsets lastInsets;
};

// Recalculate layout for current screen dimensions
void ATTouchLayout_Update(ATTouchLayout &layout, int screenW, int screenH,
	const ATTouchLayoutConfig &config,
	const ATTouchLayoutInsets &insets = ATTouchLayoutInsets{});

// Convert normalized rect to pixel rect
ATTouchRect ATTouchLayout_ToPixels(const ATTouchRect &norm, int screenW, int screenH);
