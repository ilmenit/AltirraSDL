//	AltirraSDL - Virtual touch controls for Android
//	Handles multi-touch finger tracking, joystick/dpad computation,
//	button hit testing, and ImGui overlay rendering.
//	Routes input through ATInputManager (joystick/fire) and GTIA
//	(console switches) — same paths as physical gamepads.

#include <stdafx.h>
#include <cmath>
#include <imgui.h>
#include <SDL3/SDL.h>
#include "touch_controls.h"
#include "inputmanager.h"
#include "inputdefs.h"
#include "gtia.h"
#include "simulator.h"
#include "android_platform.h"

extern ATSimulator g_sim;

static bool s_hapticEnabled = true;

void ATTouchControls_SetHapticEnabled(bool enabled) {
	s_hapticEnabled = enabled;
}

// Fire a short Android vibration pulse if haptic is enabled.  No-op
// on desktop (ATAndroid_Vibrate is a stub).
static void HapticPulse(int ms) {
	if (!s_hapticEnabled) return;
	ATAndroid_Vibrate(ms);
}

// True iff the current hardware is an Atari 5200, which has two
// joystick buttons.  All other hardware (400/800/XL/XE) has a single
// trigger, so we hide Fire B to declutter the UI.
static bool FireBEnabled() {
	return g_sim.GetHardwareMode() == kATHardwareMode_5200;
}

// -------------------------------------------------------------------------
// State
// -------------------------------------------------------------------------

static ATInputManager *s_pInputManager = nullptr;
static ATGTIAEmulator *s_pGTIA = nullptr;

// Joystick finger tracking
static SDL_FingerID s_joyFinger = 0;
static bool s_joyActive = false;
static float s_joyBaseX = 0, s_joyBaseY = 0;  // Touch-down position (pixels)
static float s_joyCurX = 0, s_joyCurY = 0;    // Current position (pixels)
static uint8 s_joyDirMask = 0;                // Active directions (bit 0=L,1=R,2=U,3=D)

// Fire button finger tracking
static SDL_FingerID s_fireFinger = 0;
static bool s_fireActive = false;
static bool s_fireAHeld = false;
static bool s_fireBHeld = false;

// Console button tracking (START/SELECT/OPTION each track independently)
static SDL_FingerID s_consoleFinger = 0;
static bool s_consoleActive = false;
static bool s_startHeld = false;
static bool s_selectHeld = false;
static bool s_optionHeld = false;

// Menu button tap detection — not static, accessed by ui_mobile.cpp
bool s_menuTapped = false;

// -------------------------------------------------------------------------
// Direction conversion
// -------------------------------------------------------------------------

// Convert joystick displacement to 4-bit direction mask.
// bit 0 = left, bit 1 = right, bit 2 = up, bit 3 = down
static uint8 ComputeDirectionMask(float dx, float dy, float deadZone) {
	float len = sqrtf(dx * dx + dy * dy);
	if (len < deadZone)
		return 0;

	// Normalize
	float nx = dx / len;
	float ny = dy / len;

	uint8 mask = 0;

	// Use 45-degree sectors for 8-way input
	// tan(22.5) ~= 0.4142
	static const float kThreshold = 0.4142f;

	if (nx < -kThreshold) mask |= 0x01;  // left
	if (nx >  kThreshold) mask |= 0x02;  // right
	if (ny < -kThreshold) mask |= 0x04;  // up
	if (ny >  kThreshold) mask |= 0x08;  // down

	return mask;
}

// Apply direction mask change to ATInputManager
static void ApplyDirectionMask(uint8 newMask, uint8 oldMask) {
	if (!s_pInputManager)
		return;

	// Use unit 0 with the kATInputCode_JoyStick1* codes — these are
	// the SAME codes that physical SDL gamepads emit in
	// joystick_sdl3.cpp, so the default input-map bindings (which
	// route JoyStick1Left/Right/Up/Down on unit 0 to port 0 of the
	// Atari joystick) pick them up automatically.  The kATInputCode_JoyPOV*
	// codes we used previously require an explicit POV-hat binding
	// that the default map doesn't create.
	struct DirEntry {
		uint8 bit;
		uint32 code;
	};

	static const DirEntry kDirs[] = {
		{ 0x01, kATInputCode_JoyStick1Left  },
		{ 0x02, kATInputCode_JoyStick1Right },
		{ 0x04, kATInputCode_JoyStick1Up    },
		{ 0x08, kATInputCode_JoyStick1Down  },
	};

	for (const auto &d : kDirs) {
		bool wasDown = (oldMask & d.bit) != 0;
		bool isDown  = (newMask & d.bit) != 0;
		if (isDown && !wasDown)
			s_pInputManager->OnButtonDown(0, d.code);
		else if (!isDown && wasDown)
			s_pInputManager->OnButtonUp(0, d.code);
	}
}

// -------------------------------------------------------------------------
// Console switch helpers
// -------------------------------------------------------------------------

static void SetConsoleSwitch(uint8 bit, bool down) {
	if (s_pGTIA)
		s_pGTIA->SetConsoleSwitch(bit, down);
}

// -------------------------------------------------------------------------
// Public API
// -------------------------------------------------------------------------

void ATTouchControls_Init(ATInputManager *inputMgr, ATGTIAEmulator *gtia) {
	s_pInputManager = inputMgr;
	s_pGTIA = gtia;
	ATTouchControls_ReleaseAll();
}

void ATTouchControls_Shutdown() {
	ATTouchControls_ReleaseAll();
	s_pInputManager = nullptr;
	s_pGTIA = nullptr;
}

void ATTouchControls_ReleaseAll() {
	// Release joystick
	if (s_joyActive) {
		ApplyDirectionMask(0, s_joyDirMask);
		s_joyDirMask = 0;
		s_joyActive = false;
	}

	// Release fire buttons
	if (s_fireAHeld && s_pInputManager) {
		s_pInputManager->OnButtonUp(0, kATInputCode_JoyButton0);
		s_fireAHeld = false;
	}
	if (s_fireBHeld && s_pInputManager) {
		s_pInputManager->OnButtonUp(0, kATInputCode_JoyButton0 + 1);
		s_fireBHeld = false;
	}
	s_fireActive = false;

	// Release console switches
	if (s_startHeld)  { SetConsoleSwitch(0x01, false); s_startHeld = false; }
	if (s_selectHeld) { SetConsoleSwitch(0x02, false); s_selectHeld = false; }
	if (s_optionHeld) { SetConsoleSwitch(0x04, false); s_optionHeld = false; }
	s_consoleActive = false;

	s_menuTapped = false;
}

bool ATTouchControls_IsActive() {
	return s_joyActive || s_fireActive || s_consoleActive;
}

// -------------------------------------------------------------------------
// Event handling
// -------------------------------------------------------------------------

// Compute the fixed anchor point for D-Pad styles.  Matches the
// inactive Analog hint position so users' thumb memory carries over.
static void GetDPadAnchor(const ATTouchLayout &layout,
	const ATTouchLayoutConfig &config, float &ax, float &ay)
{
	ATTouchRect joyPx = ATTouchLayout_ToPixels(layout.joystickZone, layout.screenW, layout.screenH);
	float cs = (config.contentScale < 1.0f) ? 1.0f : config.contentScale;
	float margin = 80.0f * cs;
	float r = layout.joyMaxRadius;
	ax = joyPx.x0 + margin;
	ay = joyPx.y1 - margin;
	if (ax < joyPx.x0 + r) ax = joyPx.x0 + r;
	if (ay > joyPx.y1 - r) ay = joyPx.y1 - r;
	if (ay < joyPx.y0 + r) ay = joyPx.y0 + r;
}

// 4-way direction mask — picks a single dominant cardinal.  |dx|>|dy|
// ⇒ L/R, else U/D.  Sub-deadzone returns 0.
static uint8 ComputeDirectionMask4(float dx, float dy, float deadZone) {
	float len = sqrtf(dx * dx + dy * dy);
	if (len < deadZone)
		return 0;
	if (fabsf(dx) > fabsf(dy))
		return (dx < 0.0f) ? 0x01 : 0x02;
	else
		return (dy < 0.0f) ? 0x04 : 0x08;
}

bool ATTouchControls_HandleEvent(const SDL_Event &ev, const ATTouchLayout &layout,
	const ATTouchLayoutConfig &config)
{
	const ATTouchJoystickStyle style = config.joystickStyle;
	const bool isDPad = (style != ATTouchJoystickStyle::Analog);
	if (ev.type != SDL_EVENT_FINGER_DOWN &&
		ev.type != SDL_EVENT_FINGER_MOTION &&
		ev.type != SDL_EVENT_FINGER_UP)
		return false;

	// Convert normalized touch coords to pixels
	float px = ev.tfinger.x * layout.screenW;
	float py = ev.tfinger.y * layout.screenH;
	SDL_FingerID fid = ev.tfinger.fingerID;

	// Build pixel-space zone rects for hit testing
	ATTouchRect joyZonePx = ATTouchLayout_ToPixels(layout.joystickZone, layout.screenW, layout.screenH);
	ATTouchRect fireZonePx = ATTouchLayout_ToPixels(layout.fireZone, layout.screenW, layout.screenH);
	ATTouchRect topBarPx = ATTouchLayout_ToPixels(layout.topBar, layout.screenW, layout.screenH);

	if (ev.type == SDL_EVENT_FINGER_DOWN) {
		// --- MENU BUTTON ---
		if (layout.btnMenu.Contains(px, py)) {
			s_menuTapped = true;
			HapticPulse(10);
			return true;
		}

		// --- CONSOLE KEYS (top bar) ---
		if (topBarPx.Contains(px, py) && !s_consoleActive) {
			// Only claim the finger if it actually hits a console button
			if (layout.btnStart.Contains(px, py)) {
				s_consoleFinger = fid;
				s_consoleActive = true;
				s_startHeld = true;
				SetConsoleSwitch(0x01, true);
				HapticPulse(10);
				return true;
			} else if (layout.btnSelect.Contains(px, py)) {
				s_consoleFinger = fid;
				s_consoleActive = true;
				s_selectHeld = true;
				SetConsoleSwitch(0x02, true);
				HapticPulse(10);
				return true;
			} else if (layout.btnOption.Contains(px, py)) {
				s_consoleFinger = fid;
				s_consoleActive = true;
				s_optionHeld = true;
				SetConsoleSwitch(0x04, true);
				HapticPulse(10);
				return true;
			}
			// Touch in top bar but not on any button — don't consume
		}

		// --- JOYSTICK ZONE (left side) ---
		if (joyZonePx.Contains(px, py) && !s_joyActive) {
			s_joyFinger = fid;
			s_joyActive = true;
			if (isDPad) {
				// Fixed anchor at the inactive hint position so
				// the wedge layout never moves under the finger.
				float ax, ay;
				GetDPadAnchor(layout, config, ax, ay);
				s_joyBaseX = ax;
				s_joyBaseY = ay;
			} else {
				s_joyBaseX = px;
				s_joyBaseY = py;
			}
			s_joyCurX = px;
			s_joyCurY = py;

			uint8 newMask = 0;
			float dx = px - s_joyBaseX;
			float dy = py - s_joyBaseY;
			if (style == ATTouchJoystickStyle::DPad4)
				newMask = ComputeDirectionMask4(dx, dy, layout.joyDeadZone);
			else if (style == ATTouchJoystickStyle::DPad8)
				newMask = ComputeDirectionMask(dx, dy, layout.joyDeadZone);
			s_joyDirMask = 0;
			if (newMask) {
				ApplyDirectionMask(newMask, 0);
				s_joyDirMask = newMask;
			}
			HapticPulse(5);
			return true;
		}

		// --- FIRE BUTTON ZONE (right side) ---
		if (fireZonePx.Contains(px, py) && !s_fireActive) {
			s_fireFinger = fid;
			s_fireActive = true;

			const bool fireBEnabled = FireBEnabled();

			if (layout.btnFireA.Contains(px, py)) {
				s_fireAHeld = true;
				if (s_pInputManager)
					s_pInputManager->OnButtonDown(0, kATInputCode_JoyButton0);
			}
			if (fireBEnabled && layout.btnFireB.Contains(px, py)) {
				s_fireBHeld = true;
				if (s_pInputManager)
					s_pInputManager->OnButtonDown(0, kATInputCode_JoyButton0 + 1);
			}
			// If finger lands between buttons (or Fire B is disabled),
			// default to Fire A for the whole zone.
			if (!s_fireAHeld && !s_fireBHeld) {
				s_fireAHeld = true;
				if (s_pInputManager)
					s_pInputManager->OnButtonDown(0, kATInputCode_JoyButton0);
			}
			HapticPulse(15);
			return true;
		}

		return false;
	}

	if (ev.type == SDL_EVENT_FINGER_MOTION) {
		// --- JOYSTICK MOTION ---
		if (s_joyActive && fid == s_joyFinger) {
			s_joyCurX = px;
			s_joyCurY = py;

			float dx = s_joyCurX - s_joyBaseX;
			float dy = s_joyCurY - s_joyBaseY;

			if (!isDPad) {
				// Analog: clamp to max radius and move knob with
				// the finger.
				float len = sqrtf(dx * dx + dy * dy);
				if (len > layout.joyMaxRadius) {
					dx = dx / len * layout.joyMaxRadius;
					dy = dy / len * layout.joyMaxRadius;
					s_joyCurX = s_joyBaseX + dx;
					s_joyCurY = s_joyBaseY + dy;
				}
			}

			uint8 newMask;
			if (style == ATTouchJoystickStyle::DPad4)
				newMask = ComputeDirectionMask4(dx, dy, layout.joyDeadZone);
			else
				newMask = ComputeDirectionMask(dx, dy, layout.joyDeadZone);

			if (newMask != s_joyDirMask) {
				ApplyDirectionMask(newMask, s_joyDirMask);
				s_joyDirMask = newMask;
				HapticPulse(3);
			}
			return true;
		}

		// Fire finger motion — allow sliding between fire A and fire B
		if (s_fireActive && fid == s_fireFinger) {
			const bool fireBEnabled = FireBEnabled();
			bool nowA = layout.btnFireA.Contains(px, py);
			bool nowB = fireBEnabled && layout.btnFireB.Contains(px, py);

			if (nowA && !s_fireAHeld) {
				s_fireAHeld = true;
				if (s_pInputManager) s_pInputManager->OnButtonDown(0, kATInputCode_JoyButton0);
			} else if (!nowA && s_fireAHeld) {
				s_fireAHeld = false;
				if (s_pInputManager) s_pInputManager->OnButtonUp(0, kATInputCode_JoyButton0);
			}

			if (nowB && !s_fireBHeld) {
				s_fireBHeld = true;
				if (s_pInputManager) s_pInputManager->OnButtonDown(0, kATInputCode_JoyButton0 + 1);
			} else if (!nowB && s_fireBHeld) {
				s_fireBHeld = false;
				if (s_pInputManager) s_pInputManager->OnButtonUp(0, kATInputCode_JoyButton0 + 1);
			}
			return true;
		}

		return false;
	}

	if (ev.type == SDL_EVENT_FINGER_UP) {
		// --- JOYSTICK RELEASE ---
		if (s_joyActive && fid == s_joyFinger) {
			ApplyDirectionMask(0, s_joyDirMask);
			s_joyDirMask = 0;
			s_joyActive = false;
			return true;
		}

		// --- FIRE RELEASE ---
		if (s_fireActive && fid == s_fireFinger) {
			if (s_fireAHeld && s_pInputManager) {
				s_pInputManager->OnButtonUp(0, kATInputCode_JoyButton0);
				s_fireAHeld = false;
			}
			if (s_fireBHeld && s_pInputManager) {
				s_pInputManager->OnButtonUp(0, kATInputCode_JoyButton0 + 1);
				s_fireBHeld = false;
			}
			s_fireActive = false;
			return true;
		}

		// --- CONSOLE KEY RELEASE ---
		if (s_consoleActive && fid == s_consoleFinger) {
			if (s_startHeld)  { SetConsoleSwitch(0x01, false); s_startHeld = false; }
			if (s_selectHeld) { SetConsoleSwitch(0x02, false); s_selectHeld = false; }
			if (s_optionHeld) { SetConsoleSwitch(0x04, false); s_optionHeld = false; }
			s_consoleActive = false;
			return true;
		}

		return false;
	}

	return false;
}

// -------------------------------------------------------------------------
// Rendering
// -------------------------------------------------------------------------

// Draw a rounded-rect button with label
static void DrawButton(ImDrawList *dl, const ATTouchRect &r, const char *label,
	ImU32 color, ImU32 textColor, bool pressed)
{
	ImU32 fillColor = pressed
		? IM_COL32(
			(color >> IM_COL32_R_SHIFT & 0xFF),
			(color >> IM_COL32_G_SHIFT & 0xFF),
			(color >> IM_COL32_B_SHIFT & 0xFF),
			200)
		: color;

	float rounding = (r.x1 - r.x0) * 0.15f;
	dl->AddRectFilled(ImVec2(r.x0, r.y0), ImVec2(r.x1, r.y1), fillColor, rounding);
	dl->AddRect(ImVec2(r.x0, r.y0), ImVec2(r.x1, r.y1),
		IM_COL32(255, 255, 255, 100), rounding, 0, 2.0f);

	// Center label text
	ImVec2 textSize = ImGui::CalcTextSize(label);
	float tx = r.CenterX() - textSize.x * 0.5f;
	float ty = r.CenterY() - textSize.y * 0.5f;
	dl->AddText(ImVec2(tx, ty), textColor, label);
}

// Wedge index → 4-bit direction mask (bit0=L,1=R,2=U,3=D).
// For 8 segments with startAngle=22.5° + k·45° the wedge centres
// fall at 45° + k·45°, i.e. NE, N, NW, W, SW, S, SE, E.
static uint8 SectorToMask8(int k) {
	static const uint8 kTbl[8] = {
		0x02 | 0x04,   // 0: NE = right+up
		0x04,          // 1: N  = up
		0x01 | 0x04,   // 2: NW = left+up
		0x01,          // 3: W  = left
		0x01 | 0x08,   // 4: SW = left+down
		0x08,          // 5: S  = down
		0x02 | 0x08,   // 6: SE = right+down
		0x02,          // 7: E  = right
	};
	return kTbl[k & 7];
}

// Draw a pizza-sliced dpad.  The centre vertex of every wedge is
// `(cx, cy)` so there is no dead square in the middle.
//   segments = 8 → wedge boundaries at 22.5° + k·45° (cardinals lie
//               at 0°/90°/... which is *inside* a wedge).
//   segments = 4 → wedge boundaries at 45° + k·90° (cardinals lie at
//               the centre of each wedge).
static void DrawDPad(ImDrawList *dl, float cx, float cy, float radius,
	uint8 mask, float opacity, int segments)
{
	ImU32 ringColor = IM_COL32(255, 255, 255, (int)(140 * opacity));
	ImU32 edgeColor = IM_COL32(255, 255, 255, (int)(110 * opacity));
	ImU32 idleFill  = IM_COL32(255, 255, 255, (int)( 35 * opacity));
	ImU32 hotFill   = IM_COL32(255, 255, 255, (int)(170 * opacity));
	ImU32 glyphColor= IM_COL32(255, 255, 255, (int)(220 * opacity));

	const float kPi = 3.14159265358979323846f;
	float step = (2.0f * kPi) / (float)segments;
	// SDL / ImGui y axis is screen-down.  Use math angles but negate y
	// when converting to screen coords so 0° points to the right (+x),
	// 90° points up (−screen y), etc.
	float startAngle = (segments == 8)
		? (kPi / 8.0f)        // 22.5°
		: (kPi / 4.0f);       // 45°

	for (int k = 0; k < segments; ++k) {
		float a0 = startAngle + step * (float)k;
		float a1 = a0 + step;

		ImVec2 p0(cx, cy);
		ImVec2 p1(cx + radius * cosf(a0), cy - radius * sinf(a0));
		ImVec2 p2(cx + radius * cosf(a1), cy - radius * sinf(a1));

		// Is this wedge lit?
		bool lit = false;
		if (segments == 8) {
			uint8 wantMask = SectorToMask8(k);
			// Light wedge if the *exact* cardinal or diagonal is
			// set.  Single-axis presses light the cardinal wedge;
			// two-axis presses light the diagonal wedge only.
			lit = (mask == wantMask);
		} else {
			// 4-way: wedge k spans [45° + k·90°, 135° + k·90°].
			// Centres lie at 90° + k·90° = N, W, S, E.
			static const uint8 k4[4] = {
				0x04, // 0: N (up)
				0x01, // 1: W (left)
				0x08, // 2: S (down)
				0x02, // 3: E (right)
			};
			lit = (mask == k4[k]);
		}

		dl->AddTriangleFilled(p0, p1, p2, lit ? hotFill : idleFill);
		dl->AddLine(p0, p1, edgeColor, 1.5f);
	}

	// Outer ring on top.
	dl->AddCircle(ImVec2(cx, cy), radius, ringColor, 48, 2.0f);

	// Cardinal glyphs using ASCII (default ImGui font does not
	// include Unicode arrows).
	float gr = radius * 0.68f;
	struct Glyph { const char *s; float dx, dy; };
	const Glyph glyphs[4] = {
		{ ">",  gr, 0 },
		{ "<", -gr, 0 },
		{ "^",  0, -gr },
		{ "v",  0,  gr },
	};
	for (int i = 0; i < 4; ++i) {
		ImVec2 ts = ImGui::CalcTextSize(glyphs[i].s);
		dl->AddText(ImVec2(cx + glyphs[i].dx - ts.x * 0.5f,
			cy + glyphs[i].dy - ts.y * 0.5f),
			glyphColor, glyphs[i].s);
	}
}

// Draw circular joystick base and knob
static void DrawJoystick(ImDrawList *dl, float baseX, float baseY,
	float curX, float curY, float maxRadius, float opacity)
{
	ImU32 baseColor = IM_COL32(255, 255, 255, (int)(50 * opacity));
	ImU32 knobColor = IM_COL32(255, 255, 255, (int)(120 * opacity));
	ImU32 ringColor = IM_COL32(255, 255, 255, (int)(80 * opacity));

	// Outer ring (max extent)
	dl->AddCircle(ImVec2(baseX, baseY), maxRadius, ringColor, 48, 2.0f);

	// Base circle
	dl->AddCircleFilled(ImVec2(baseX, baseY), maxRadius * 0.85f, baseColor, 48);

	// Knob (follows finger)
	float knobRadius = maxRadius * 0.35f;
	dl->AddCircleFilled(ImVec2(curX, curY), knobRadius, knobColor, 32);
}

void ATTouchControls_Render(const ATTouchLayout &layout, const ATTouchLayoutConfig &config) {
	float alpha = config.controlOpacity;
	ImU32 btnNormal    = IM_COL32(80, 80, 80, (int)(140 * alpha));
	ImU32 btnConsole   = IM_COL32(60, 60, 80, (int)(140 * alpha));
	ImU32 btnFireA     = IM_COL32(180, 40, 40, (int)(160 * alpha));
	ImU32 btnFireB     = IM_COL32(40, 80, 180, (int)(160 * alpha));
	ImU32 textColor    = IM_COL32(255, 255, 255, (int)(220 * alpha));
	ImU32 menuColor    = IM_COL32(200, 200, 200, (int)(180 * alpha));

	// Get foreground draw list (renders on top of everything)
	ImDrawList *dl = ImGui::GetForegroundDrawList();

	// --- Console keys ---
	DrawButton(dl, layout.btnStart, "START", btnConsole, textColor, s_startHeld);
	DrawButton(dl, layout.btnSelect, "SELECT", btnConsole, textColor, s_selectHeld);
	DrawButton(dl, layout.btnOption, "OPTION", btnConsole, textColor, s_optionHeld);

	// --- Hamburger menu icon ---
	{
		const ATTouchRect &m = layout.btnMenu;
		float cx = m.CenterX();
		float cy = m.CenterY();
		float hw = m.Width() * 0.3f;
		float lineSpacing = m.Height() * 0.18f;
		float thickness = 2.5f;

		for (int i = -1; i <= 1; i++) {
			float ly = cy + i * lineSpacing;
			dl->AddLine(ImVec2(cx - hw, ly), ImVec2(cx + hw, ly), menuColor, thickness);
		}
	}

	// --- Fire buttons ---
	// On a 5200 the single-label "A" is unused but we still only have
	// one primary button.  Label as "FIRE" in 8-bit mode (single button)
	// to make it unambiguous.
	const bool fireBEnabled = FireBEnabled();
	DrawButton(dl, layout.btnFireA,
		fireBEnabled ? "A" : "FIRE",
		btnFireA, textColor, s_fireAHeld);
	if (fireBEnabled)
		DrawButton(dl, layout.btnFireB, "B", btnFireB, textColor, s_fireBHeld);

	// --- Joystick ---
	const ATTouchJoystickStyle style = config.joystickStyle;
	if (style == ATTouchJoystickStyle::DPad8 || style == ATTouchJoystickStyle::DPad4) {
		// Pizza-sliced dpad at the fixed anchor (matches the
		// inactive Analog hint position).  Always drawn, regardless
		// of whether a finger is down.
		float ax, ay;
		GetDPadAnchor(layout, config, ax, ay);
		int segs = (style == ATTouchJoystickStyle::DPad8) ? 8 : 4;
		DrawDPad(dl, ax, ay, layout.joyMaxRadius,
			s_joyActive ? s_joyDirMask : 0, alpha, segs);
	} else if (s_joyActive) {
		DrawJoystick(dl, s_joyBaseX, s_joyBaseY,
			s_joyCurX, s_joyCurY,
			layout.joyMaxRadius, alpha);
	} else {
		// When inactive, anchor a clearly-visible hint at the bottom-
		// left of the joystick zone so the user sees where to put
		// their thumb.  Bumped to 22% opacity with a 2dp stroke plus
		// a faint fill and crosshair.
		ATTouchRect joyPx = ATTouchLayout_ToPixels(layout.joystickZone, layout.screenW, layout.screenH);
		float cs = (config.contentScale < 1.0f) ? 1.0f : config.contentScale;
		float margin = 80.0f * cs;  // 80dp inset from the zone edges
		float r = layout.joyMaxRadius;

		float hintX = joyPx.x0 + margin;
		float hintY = joyPx.y1 - margin;
		// Clamp inside zone
		if (hintX < joyPx.x0 + r) hintX = joyPx.x0 + r;
		if (hintY > joyPx.y1 - r) hintY = joyPx.y1 - r;
		if (hintY < joyPx.y0 + r) hintY = joyPx.y0 + r;

		ImU32 hintRing = IM_COL32(255, 255, 255, (int)(160 * alpha));
		ImU32 hintFill = IM_COL32(255, 255, 255, (int)(35  * alpha));
		ImU32 hintCross= IM_COL32(255, 255, 255, (int)(110 * alpha));
		dl->AddCircleFilled(ImVec2(hintX, hintY), r * 0.85f, hintFill, 48);
		dl->AddCircle(ImVec2(hintX, hintY), r, hintRing, 48, 2.0f * cs);
		// Small crosshair through the middle
		float cr = r * 0.18f;
		dl->AddLine(ImVec2(hintX - cr, hintY), ImVec2(hintX + cr, hintY), hintCross, 2.0f * cs);
		dl->AddLine(ImVec2(hintX, hintY - cr), ImVec2(hintX, hintY + cr), hintCross, 2.0f * cs);
	}
}
