//	AltirraSDL - Virtual on-screen keyboard
//	Displays a photographic Atari XL/XE keyboard image with clickable keys.
//	Key press/release logic faithfully replicates uionscreenkeyboard.cpp.

#include <stdafx.h>
#include <SDL3/SDL.h>
#include <imgui.h>

#include "ui_virtual_keyboard.h"
#include "display_backend.h"
#include "gl_helpers.h"
#include "simulator.h"
#include "gtia.h"
#include <at/ataudio/pokey.h>
#include "uikeyboard.h"
#include "keyboard_data.h"

#ifdef __ANDROID__
#include "android_platform.h"
#endif

extern SDL_Window *g_pWindow;
extern float g_menuBarHeight;
extern ATUIKeyboardOptions g_kbdOpts;
extern IDisplayBackend *ATUIGetDisplayBackend();

// ---------------------------------------------------------------------------
// Texture state
// ---------------------------------------------------------------------------
static SDL_Texture *s_sdlTexture = nullptr;
static uint32_t s_glTexture = 0;
static int s_texW = 0;
static int s_texH = 0;
static bool s_textureInited = false;
static bool s_useGL = false;

// ---------------------------------------------------------------------------
// Keyboard interaction state
// ---------------------------------------------------------------------------
static int s_focusedKey = -1;     // gamepad cursor index (-1 = none, kOSKKeyCount = close btn)
static int s_pressedKey = -1;     // currently pressed key (mouse/touch)
static int s_hoverKey = -1;       // key under mouse cursor
static bool s_closeRequested = false; // set by close button press

// Modifier sticky/held state (matches uionscreenkeyboard.cpp pattern)
static bool s_shiftHeld = false;
static bool s_shiftSticky = false;
static bool s_controlHeld = false;
static bool s_controlSticky = false;

// Track which console switches are held by the virtual keyboard
static bool s_consoleStartHeld = false;
static bool s_consoleSelectHeld = false;
static bool s_consoleOptionHeld = false;

// Previous-frame insets for display rect computation
static float s_lastBottomInset = 0;
static float s_lastRightInset = 0;

// Touch finger tracking (for mobile)
static SDL_FingerID s_touchFinger = 0;
static bool s_touchActive = false;

// ---------------------------------------------------------------------------
// Texture management
// ---------------------------------------------------------------------------
static ImTextureID EnsureTexture() {
	if (s_textureInited) {
		if (s_useGL)
			return (ImTextureID)(intptr_t)s_glTexture;
		return (ImTextureID)s_sdlTexture;
	}
	s_textureInited = true;

	// Load BMP from baked data
	SDL_IOStream *io = SDL_IOFromConstMem(kKeyboardBMPData, (size_t)kKeyboardBMPSize);
	if (!io)
		return (ImTextureID)0;

	SDL_Surface *bmpSurf = SDL_LoadBMP_IO(io, true);
	if (!bmpSurf)
		return (ImTextureID)0;

	// Convert to RGBA32 for GPU upload
	SDL_Surface *rgba = SDL_ConvertSurface(bmpSurf, SDL_PIXELFORMAT_RGBA32);
	SDL_DestroySurface(bmpSurf);
	if (!rgba)
		return (ImTextureID)0;

	s_texW = rgba->w;
	s_texH = rgba->h;

	IDisplayBackend *backend = ATUIGetDisplayBackend();
	s_useGL = backend && backend->GetType() == DisplayBackendType::OpenGL33;

	if (s_useGL) {
		s_glTexture = GLCreateTexture2D(
			rgba->w, rgba->h, GL_RGBA8, GL_RGBA,
			GL_UNSIGNED_BYTE, rgba->pixels, true);
	} else {
		SDL_Renderer *renderer = SDL_GetRenderer(g_pWindow);
		if (renderer)
			s_sdlTexture = SDL_CreateTextureFromSurface(renderer, rgba);
	}

	SDL_DestroySurface(rgba);

	if (s_useGL)
		return (ImTextureID)(intptr_t)s_glTexture;
	return (ImTextureID)s_sdlTexture;
}

void ATUIVirtualKeyboard_Shutdown() {
	if (s_sdlTexture) {
		SDL_DestroyTexture(s_sdlTexture);
		s_sdlTexture = nullptr;
	}
	if (s_glTexture) {
		glDeleteTextures(1, &s_glTexture);
		s_glTexture = 0;
	}
	s_textureInited = false;
	s_texW = 0;
	s_texH = 0;
}

// ---------------------------------------------------------------------------
// Console switch helpers
// ---------------------------------------------------------------------------
static uint8_t GetConsoleBit(uint8_t scanCode) {
	switch (scanCode) {
		case 0x48: return 0x01;  // START
		case 0x49: return 0x02;  // SELECT
		case 0x4A: return 0x04;  // OPTION
		default:   return 0;
	}
}

static bool* GetConsoleHeldFlag(uint8_t scanCode) {
	switch (scanCode) {
		case 0x48: return &s_consoleStartHeld;
		case 0x49: return &s_consoleSelectHeld;
		case 0x4A: return &s_consoleOptionHeld;
		default:   return nullptr;
	}
}

// ---------------------------------------------------------------------------
// Modifier logic (replicates uionscreenkeyboard.cpp lines 355-495)
// ---------------------------------------------------------------------------
static bool IsShiftActive() {
	return s_shiftHeld || s_shiftSticky;
}

static bool IsControlActive() {
	return s_controlHeld || s_controlSticky;
}

static void HandleModifierPress(ATSimulator &sim, int index) {
	const ATOSKKeyDef &key = kOSKKeys[index];
	ATPokeyEmulator &pokey = sim.GetPokey();

	if (key.scanCode == 0x42) {
		// Shift — toggle sticky
		s_shiftSticky = !s_shiftSticky;
		s_shiftHeld = true;
		pokey.SetShiftKeyState(true, !g_kbdOpts.mbFullRawKeys);
	} else if (key.scanCode == 0x41) {
		// Control — toggle sticky
		s_controlSticky = !s_controlSticky;
		s_controlHeld = true;
		pokey.SetControlKeyState(true);
	}
}

static void HandleModifierRelease(ATSimulator &sim, int index) {
	const ATOSKKeyDef &key = kOSKKeys[index];
	ATPokeyEmulator &pokey = sim.GetPokey();

	if (key.scanCode == 0x42) {
		s_shiftHeld = false;
		if (!s_shiftSticky)
			pokey.SetShiftKeyState(false, !g_kbdOpts.mbFullRawKeys);
	} else if (key.scanCode == 0x41) {
		s_controlHeld = false;
		if (!s_controlSticky)
			pokey.SetControlKeyState(false);
	}
}

static void ReleaseStickyModifiers(ATSimulator &sim) {
	ATPokeyEmulator &pokey = sim.GetPokey();

	if (s_shiftSticky) {
		s_shiftSticky = false;
		if (!s_shiftHeld)
			pokey.SetShiftKeyState(false, !g_kbdOpts.mbFullRawKeys);
	}
	if (s_controlSticky) {
		s_controlSticky = false;
		if (!s_controlHeld)
			pokey.SetControlKeyState(false);
	}
}

// ---------------------------------------------------------------------------
// Key press/release (replicates uionscreenkeyboard.cpp lines 355-459)
// ---------------------------------------------------------------------------
static void PressKey(ATSimulator &sim, int index) {
	if (index < 0 || index >= kOSKKeyCount)
		return;

	const ATOSKKeyDef &key = kOSKKeys[index];
	ATPokeyEmulator &pokey = sim.GetPokey();
	ATGTIAEmulator &gtia = sim.GetGTIA();

	if (key.flags & kOSKFlag_Console) {
		uint8_t bit = GetConsoleBit(key.scanCode);
		gtia.SetConsoleSwitch(bit, true);
		bool *flag = GetConsoleHeldFlag(key.scanCode);
		if (flag) *flag = true;
	} else if (key.flags & kOSKFlag_Break) {
		pokey.PushBreak();
	} else if (key.flags & kOSKFlag_Reset) {
		sim.WarmReset();
	} else if (key.flags & kOSKFlag_Toggle) {
		HandleModifierPress(sim, index);
	} else {
		uint8_t sc = key.scanCode;
		if (pokey.GetShiftKeyState())
			sc += 0x40;
		if (pokey.GetControlKeyState())
			sc += 0x80;
		pokey.PushRawKey(sc, !g_kbdOpts.mbFullRawKeys);
	}

	s_pressedKey = index;

#ifdef __ANDROID__
	ATAndroid_Vibrate(10);
#endif
}

static void ReleaseKey(ATSimulator &sim, int index) {
	if (index < 0 || index >= kOSKKeyCount)
		return;

	const ATOSKKeyDef &key = kOSKKeys[index];
	ATPokeyEmulator &pokey = sim.GetPokey();
	ATGTIAEmulator &gtia = sim.GetGTIA();

	if (key.flags & kOSKFlag_Console) {
		uint8_t bit = GetConsoleBit(key.scanCode);
		gtia.SetConsoleSwitch(bit, false);
		bool *flag = GetConsoleHeldFlag(key.scanCode);
		if (flag) *flag = false;
	} else if (key.flags & kOSKFlag_Break) {
		// No release action
	} else if (key.flags & kOSKFlag_Reset) {
		// No release action
	} else if (key.flags & kOSKFlag_Toggle) {
		HandleModifierRelease(sim, index);
	} else {
		pokey.ReleaseRawKey(key.scanCode, !g_kbdOpts.mbFullRawKeys);
		// Auto-release sticky modifiers after a normal key press
		ReleaseStickyModifiers(sim);
	}

	if (s_pressedKey == index)
		s_pressedKey = -1;
}

void ATUIVirtualKeyboard_ReleaseAll(ATSimulator &sim) {
	if (s_pressedKey >= 0) {
		ReleaseKey(sim, s_pressedKey);
		s_pressedKey = -1;
	}

	ATPokeyEmulator &pokey = sim.GetPokey();
	ATGTIAEmulator &gtia = sim.GetGTIA();

	if (s_shiftHeld || s_shiftSticky) {
		s_shiftHeld = false;
		s_shiftSticky = false;
		pokey.SetShiftKeyState(false, !g_kbdOpts.mbFullRawKeys);
	}
	if (s_controlHeld || s_controlSticky) {
		s_controlHeld = false;
		s_controlSticky = false;
		pokey.SetControlKeyState(false);
	}
	if (s_consoleStartHeld)  { gtia.SetConsoleSwitch(0x01, false); s_consoleStartHeld = false; }
	if (s_consoleSelectHeld) { gtia.SetConsoleSwitch(0x02, false); s_consoleSelectHeld = false; }
	if (s_consoleOptionHeld) { gtia.SetConsoleSwitch(0x04, false); s_consoleOptionHeld = false; }

	s_touchActive = false;
	s_focusedKey = -1;
}

// ---------------------------------------------------------------------------
// Placement / layout computation
// ---------------------------------------------------------------------------
static const float kKeyboardAspect = 1024.0f / 448.0f;  // reference aspect ratio

static int ResolveAutoPlacement(int placement) {
	if (placement != kOSKPlacement_Auto)
		return placement;

	int winW, winH;
	SDL_GetWindowSize(g_pWindow, &winW, &winH);
	float aspect = (winH > 0) ? (float)winW / (float)winH : 1.6f;

	// Wide windows: keyboard on the right; tall/square: bottom
	return (aspect > 1.6f) ? kOSKPlacement_Right : kOSKPlacement_Bottom;
}

static void ComputeKeyboardRect(int placement, ImVec2 *outPos, ImVec2 *outSize) {
	int winW, winH;
	SDL_GetWindowSize(g_pWindow, &winW, &winH);
	float menuH = g_menuBarHeight;

	int resolved = ResolveAutoPlacement(placement);

	if (resolved == kOSKPlacement_Right) {
		float availH = (float)winH - menuH;
		float kbdW = availH * kKeyboardAspect;
		float maxW = (float)winW * 0.4f;
		if (kbdW > maxW) kbdW = maxW;
		float kbdH = kbdW / kKeyboardAspect;

		outPos->x = (float)winW - kbdW;
		outPos->y = menuH + (availH - kbdH) * 0.5f;
		outSize->x = kbdW;
		outSize->y = kbdH;
	} else {
		float kbdH = (float)winW / kKeyboardAspect;
		float maxH = ((float)winH - menuH) * 0.4f;
		if (kbdH > maxH) kbdH = maxH;
		float kbdW = kbdH * kKeyboardAspect;

		outPos->x = ((float)winW - kbdW) * 0.5f;
		outPos->y = (float)winH - kbdH;
		outSize->x = kbdW;
		outSize->y = kbdH;
	}
}

void ATUIVirtualKeyboard_GetDisplayInset(bool visible, int placement,
	float *outBottom, float *outRight)
{
	if (!visible) {
		s_lastBottomInset = 0;
		s_lastRightInset = 0;
		*outBottom = 0;
		*outRight = 0;
		return;
	}

	ImVec2 pos, size;
	ComputeKeyboardRect(placement, &pos, &size);

	int resolved = ResolveAutoPlacement(placement);
	if (resolved == kOSKPlacement_Right) {
		s_lastBottomInset = 0;
		s_lastRightInset = size.x;
	} else {
		s_lastBottomInset = size.y;
		s_lastRightInset = 0;
	}

	*outBottom = s_lastBottomInset;
	*outRight = s_lastRightInset;
}

// ---------------------------------------------------------------------------
// Hit testing
// ---------------------------------------------------------------------------
static int HitTestKey(ImVec2 imgPos, ImVec2 imgSize, ImVec2 point) {
	for (int i = 0; i < kOSKKeyCount; i++) {
		float x0 = imgPos.x + kOSKKeys[i].u0 * imgSize.x;
		float y0 = imgPos.y + kOSKKeys[i].v0 * imgSize.y;
		float x1 = imgPos.x + kOSKKeys[i].u1 * imgSize.x;
		float y1 = imgPos.y + kOSKKeys[i].v1 * imgSize.y;

		if (point.x >= x0 && point.x < x1 && point.y >= y0 && point.y < y1)
			return i;
	}
	return -1;
}

// ---------------------------------------------------------------------------
// Check if a key is a modifier and currently active
// ---------------------------------------------------------------------------
static bool IsModifierActive(int index) {
	if (index < 0 || index >= kOSKKeyCount)
		return false;
	const ATOSKKeyDef &key = kOSKKeys[index];
	if (key.flags & kOSKFlag_Toggle) {
		if (key.scanCode == 0x42)
			return IsShiftActive();
		if (key.scanCode == 0x41)
			return IsControlActive();
	}
	// Console keys show as held while their switch is active
	if (key.flags & kOSKFlag_Console) {
		bool *flag = GetConsoleHeldFlag(key.scanCode);
		return flag && *flag;
	}
	return false;
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

// Stored each frame so HandleEvent can use the same geometry for touch hit testing
static ImVec2 s_lastImgPos = {0, 0};
static ImVec2 s_lastImgSize = {0, 0};
static bool s_lastVisible = false;

// Close button index — one past the last key
static const int kCloseButtonIndex = 62;  // == kOSKKeyCount

// Close button position in UV space (top-left empty area of keyboard)
static const float kCloseBtnU0 = 0.005f, kCloseBtnV0 = 0.01f;
static const float kCloseBtnU1 = 0.07f,  kCloseBtnV1 = 0.10f;

static ImVec2 s_closeBtnMin = {0, 0};
static ImVec2 s_closeBtnMax = {0, 0};

static bool HitTestCloseButton(ImVec2 point) {
	return point.x >= s_closeBtnMin.x && point.x < s_closeBtnMax.x
	    && point.y >= s_closeBtnMin.y && point.y < s_closeBtnMax.y;
}

static bool s_wasVisible = false;

bool ATUIRenderVirtualKeyboard(ATSimulator &sim, bool visible, int placement) {
	// Release all keys when keyboard is hidden (e.g. via menu toggle)
	if (s_wasVisible && !visible)
		ATUIVirtualKeyboard_ReleaseAll(sim);
	s_wasVisible = visible;
	s_lastVisible = visible;

	// Check if close was requested by HandleEvent (gamepad A on close button)
	// before this frame's render.  Reset the flag after reading.
	bool closeFromEvent = s_closeRequested;
	s_closeRequested = false;

	if (!visible) return false;
	if (closeFromEvent) return true;

	ImTextureID texID = EnsureTexture();
	if (!texID) return false;

	ImVec2 panelPos, panelSize;
	ComputeKeyboardRect(placement, &panelPos, &panelSize);

	// Borderless window for the keyboard panel
	ImGui::SetNextWindowPos(panelPos);
	ImGui::SetNextWindowSize(panelSize);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.12f, 0.12f, 0.12f, 1.0f));

	ImGuiWindowFlags flags =
		ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
		ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse |
		ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;

	if (!ImGui::Begin("##VirtualKeyboard", nullptr, flags)) {
		ImGui::End();
		ImGui::PopStyleColor();
		ImGui::PopStyleVar();
		return false;
	}

	// Compute image size maintaining aspect ratio, centered in panel
	float scaleX = panelSize.x / (float)s_texW;
	float scaleY = panelSize.y / (float)s_texH;
	float scale = (scaleX < scaleY) ? scaleX : scaleY;

	ImVec2 imgSize((float)s_texW * scale, (float)s_texH * scale);
	ImVec2 imgPos = ImGui::GetCursorScreenPos();
	imgPos.x += (panelSize.x - imgSize.x) * 0.5f;
	imgPos.y += (panelSize.y - imgSize.y) * 0.5f;

	// Store for touch hit testing in HandleEvent
	s_lastImgPos = imgPos;
	s_lastImgSize = imgSize;

	ImGui::SetCursorScreenPos(imgPos);
	ImGui::Image(texID, imgSize);

	ImDrawList *dl = ImGui::GetWindowDrawList();
	ImVec2 mousePos = ImGui::GetMousePos();
	bool mouseInWindow = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);

	// --- Close button (top-left empty area) ---
	s_closeBtnMin = ImVec2(imgPos.x + kCloseBtnU0 * imgSize.x,
	                        imgPos.y + kCloseBtnV0 * imgSize.y);
	s_closeBtnMax = ImVec2(imgPos.x + kCloseBtnU1 * imgSize.x,
	                        imgPos.y + kCloseBtnV1 * imgSize.y);

	{
		bool closeFocused = (s_focusedKey == kCloseButtonIndex);
		bool closeHover = mouseInWindow && HitTestCloseButton(mousePos);

		// Background
		ImU32 closeBg = IM_COL32(80, 80, 80, 160);
		if (closeFocused)
			closeBg = IM_COL32(60, 60, 80, 200);
		else if (closeHover)
			closeBg = IM_COL32(100, 100, 100, 180);
		dl->AddRectFilled(s_closeBtnMin, s_closeBtnMax, closeBg, 4.0f);

		// "X" lines
		float pad = (s_closeBtnMax.x - s_closeBtnMin.x) * 0.28f;
		ImVec2 a0(s_closeBtnMin.x + pad, s_closeBtnMin.y + pad);
		ImVec2 a1(s_closeBtnMax.x - pad, s_closeBtnMax.y - pad);
		ImVec2 b0(s_closeBtnMax.x - pad, s_closeBtnMin.y + pad);
		ImVec2 b1(s_closeBtnMin.x + pad, s_closeBtnMax.y - pad);
		ImU32 xColor = IM_COL32(220, 220, 220, 240);
		float thick = (s_closeBtnMax.x - s_closeBtnMin.x) * 0.08f;
		if (thick < 1.5f) thick = 1.5f;
		dl->AddLine(a0, a1, xColor, thick);
		dl->AddLine(b0, b1, xColor, thick);

		// Focus ring
		if (closeFocused)
			dl->AddRect(s_closeBtnMin, s_closeBtnMax, IM_COL32(0, 200, 255, 220), 4.0f, 0, 2.0f);

		// Mouse click on close
		if (mouseInWindow && closeHover && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
			s_closeRequested = true;
	}

	// --- Key overlays ---
	s_hoverKey = -1;
	if (mouseInWindow)
		s_hoverKey = HitTestKey(imgPos, imgSize, mousePos);

	for (int i = 0; i < kOSKKeyCount; i++) {
		ImVec2 keyMin(imgPos.x + kOSKKeys[i].u0 * imgSize.x,
		              imgPos.y + kOSKKeys[i].v0 * imgSize.y);
		ImVec2 keyMax(imgPos.x + kOSKKeys[i].u1 * imgSize.x,
		              imgPos.y + kOSKKeys[i].v1 * imgSize.y);

		bool isPressed = (s_pressedKey == i);
		bool isFocused = (s_focusedKey == i);
		bool isHover = (s_hoverKey == i);
		bool isModActive = IsModifierActive(i);

		if (isPressed || isModActive)
			dl->AddRectFilled(keyMin, keyMax, IM_COL32(255, 255, 255, 140), 3.0f);
		else if (isHover || isFocused)
			dl->AddRectFilled(keyMin, keyMax, IM_COL32(255, 255, 255, 80), 3.0f);

		// Gamepad focus ring
		if (isFocused)
			dl->AddRect(keyMin, keyMax, IM_COL32(0, 200, 255, 220), 3.0f, 0, 2.0f);
	}

	// Handle mouse click on keys (desktop)
	if (mouseInWindow) {
		if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && s_hoverKey >= 0)
			PressKey(sim, s_hoverKey);
	}
	if (ImGui::IsMouseReleased(ImGuiMouseButton_Left) && s_pressedKey >= 0) {
		const ATOSKKeyDef &key = kOSKKeys[s_pressedKey];
		if (key.flags & kOSKFlag_Toggle) {
			HandleModifierRelease(sim, s_pressedKey);
			s_pressedKey = -1;
		} else {
			ReleaseKey(sim, s_pressedKey);
		}
	}

	ImGui::End();
	ImGui::PopStyleColor();
	ImGui::PopStyleVar();

	return s_closeRequested;
}

// ---------------------------------------------------------------------------
// Event handling (gamepad + touch)
// ---------------------------------------------------------------------------
bool ATUIVirtualKeyboard_HandleEvent(const SDL_Event &ev, ATSimulator &sim, bool visible) {
	if (!visible)
		return false;

	// --- Gamepad navigation ---
	if (ev.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
		switch (ev.gbutton.button) {
			case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
			case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
			case SDL_GAMEPAD_BUTTON_DPAD_UP:
			case SDL_GAMEPAD_BUTTON_DPAD_DOWN: {
				// Initialize focus on first D-pad press
				if (s_focusedKey < 0) {
					for (int i = 0; i < kOSKKeyCount; i++) {
						if (kOSKKeys[i].label[0] == 'A' && kOSKKeys[i].label[1] == '\0') {
							s_focusedKey = i;
							break;
						}
					}
					if (s_focusedKey < 0)
						s_focusedKey = 0;
					return true;
				}

				int dir;
				switch (ev.gbutton.button) {
					case SDL_GAMEPAD_BUTTON_DPAD_LEFT:  dir = 0; break;
					case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: dir = 1; break;
					case SDL_GAMEPAD_BUTTON_DPAD_UP:    dir = 2; break;
					case SDL_GAMEPAD_BUTTON_DPAD_DOWN:  dir = 3; break;
					default: dir = 0; break;
				}

				if (s_focusedKey == kCloseButtonIndex) {
					// From close button: down → ESC (index 5), right → HELP (index 0)
					if (dir == 3)       s_focusedKey = 5;   // down → ESC
					else if (dir == 1)  s_focusedKey = 0;   // right → HELP
					else if (dir == 0)  s_focusedKey = 5;   // left → ESC
				} else {
					int next = kOSKKeys[s_focusedKey].nav[dir];
					if (dir == 2 && next < 0) {
						// Up from top row (nav[up]==-1) → close button
						s_focusedKey = kCloseButtonIndex;
					} else if (next >= 0) {
						s_focusedKey = next;
					}
				}
				return true;
			}

			case SDL_GAMEPAD_BUTTON_SOUTH:  // A — press focused key
				if (s_focusedKey == kCloseButtonIndex) {
					s_closeRequested = true;
					return true;
				}
				if (s_focusedKey >= 0 && s_focusedKey < kOSKKeyCount)
					PressKey(sim, s_focusedKey);
				return true;

			case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:  // LB — hold Shift
				s_shiftHeld = true;
				sim.GetPokey().SetShiftKeyState(true, !g_kbdOpts.mbFullRawKeys);
				return true;

			case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER:  // RB — hold Control
				s_controlHeld = true;
				sim.GetPokey().SetControlKeyState(true);
				return true;

			default:
				break;
		}
	}

	if (ev.type == SDL_EVENT_GAMEPAD_BUTTON_UP) {
		switch (ev.gbutton.button) {
			case SDL_GAMEPAD_BUTTON_SOUTH:
				if (s_pressedKey >= 0)
					ReleaseKey(sim, s_pressedKey);
				return true;

			case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:
				s_shiftHeld = false;
				if (!s_shiftSticky)
					sim.GetPokey().SetShiftKeyState(false, !g_kbdOpts.mbFullRawKeys);
				return true;

			case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER:
				s_controlHeld = false;
				if (!s_controlSticky)
					sim.GetPokey().SetControlKeyState(false);
				return true;

			default:
				break;
		}
	}

	// --- Touch events (mobile) ---
	if (ev.type == SDL_EVENT_FINGER_DOWN) {
		if (!s_lastVisible)
			return false;

		int winW, winH;
		SDL_GetWindowSize(g_pWindow, &winW, &winH);
		float fx = ev.tfinger.x * (float)winW;
		float fy = ev.tfinger.y * (float)winH;
		ImVec2 touchPt(fx, fy);

		// Check close button first
		if (HitTestCloseButton(touchPt)) {
			s_closeRequested = true;
			return true;
		}

		int hit = HitTestKey(s_lastImgPos, s_lastImgSize, touchPt);
		if (hit >= 0) {
			s_touchFinger = ev.tfinger.fingerID;
			s_touchActive = true;
			PressKey(sim, hit);
			return true;
		}
	}

	if (ev.type == SDL_EVENT_FINGER_UP) {
		if (s_touchActive && ev.tfinger.fingerID == s_touchFinger) {
			if (s_pressedKey >= 0) {
				const ATOSKKeyDef &key = kOSKKeys[s_pressedKey];
				if (key.flags & kOSKFlag_Toggle)
					HandleModifierRelease(sim, s_pressedKey);
				else
					ReleaseKey(sim, s_pressedKey);
				s_pressedKey = -1;
			}
			s_touchActive = false;
			return true;
		}
	}

	return false;
}
