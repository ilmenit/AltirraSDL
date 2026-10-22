// AltirraSDL cursor policy and emulated tablet/light-pen pointer overlays.
#include <stdafx.h>
#include <algorithm>
#include <SDL3/SDL.h>
#include <imgui.h>
#include "input_pointer_sdl3.h"
#include "inputmanager.h"
#include "simulator.h"
#include "gtia.h"
#include "uiaccessors.h"
#include "ui_main.h"
#include "ui_debugger.h"
#include "ui_confirm_dialog.h"

extern ATSimulator g_sim;

namespace {
bool HasMousePopup() {
	return ATUIIsConfirmDialogActive() || ImGui::IsPopupOpen(nullptr,
		ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
}
}

bool ATInputSDL3_GetDisplayRect(float& x, float& y, float& w, float& h) {
	if (ATUIDebuggerIsOpen()) {
		bool focused = false;
		return ATUIDebuggerGetDisplayRect(x, y, w, h, focused);
	}
	return ATGetMainDisplayRect(x, y, w, h);
}

bool ATInputSDL3_CanRouteMouseToDisplay() {
	if (HasMousePopup())
		return false;
	if (ATUIDebuggerIsOpen()) {
		if (!ATUIDebuggerIsDisplayImageHovered()) return false;
		// Event routing uses last frame's ImGui hover ownership. Also test
		// SDL's current position so the first move/click into another pane
		// cannot still drive the emulator through a stale hover flag.
		float x = 0, y = 0, w = 0, h = 0, mx = 0, my = 0;
		if (!ATInputSDL3_GetDisplayRect(x, y, w, h)) return false;
		SDL_GetMouseState(&mx, &my);
		return mx >= x && my >= y && mx < x + w && my < y + h;
	}
	return !ATUIWantCaptureMouse();
}

void ATInputSDL3_UpdatePointer(SDL_Window *window) {
	if (!window)
		return;

	float dx = 0, dy = 0, dw = 0, dh = 0;
	const bool hasDisplay = ATInputSDL3_GetDisplayRect(dx, dy, dw, dh)
		&& dw > 0 && dh > 0;
	const bool allowInput = ATInputSDL3_CanRouteMouseToDisplay();
	const SDL_WindowFlags flags = SDL_GetWindowFlags(window);
	const bool focused = (flags & SDL_WINDOW_INPUT_FOCUS) != 0;
	const bool usable = focused && allowInput && hasDisplay;
	auto *im = g_sim.GetInputManager();
	const bool captured = ATUIIsMouseCaptured();
	const bool grab = usable && ((captured && im && im->IsMouseAbsoluteMode())
		|| ((flags & SDL_WINDOW_FULLSCREEN) && ATUIGetConstrainMouseFullScreen()));
	if (SDL_GetWindowMouseGrab(window) != grab)
		SDL_SetWindowMouseGrab(window, grab);

	float mx = 0, my = 0;
	const SDL_MouseButtonFlags buttons = SDL_GetMouseState(&mx, &my);
	const bool overDisplay = usable && SDL_GetMouseFocus() == window
		&& mx >= dx && my >= dy && mx < dx + dw && my < dy + dh;
	const uint64 now = SDL_GetTicks();
	static float lastX = 0, lastY = 0;
	static SDL_MouseButtonFlags lastButtons = 0;
	static uint64 lastMotion = 0;
	if (!overDisplay || mx != lastX || my != lastY || buttons != lastButtons)
		lastMotion = now;
	lastX = mx;
	lastY = my;
	lastButtons = buttons;

	const bool absoluteTarget = overDisplay && im && im->IsMouseMapped()
		&& im->IsMouseAbsoluteMode() && (captured || !ATUIGetMouseAutoCapture());
	const bool relativeTarget = usable && captured && im
		&& !im->IsMouseAbsoluteMode();
	const bool autoHidden = overDisplay && !captured && ATUIGetPointerAutoHide()
		&& !(SDL_GetModState() & SDL_KMOD_ALT) && now - lastMotion >= 1500;
	// ImGui updates the platform cursor at NewFrame; apply our display-only
	// policy after UI capture has been resolved so menus retain their cursor.
	const bool hidden = absoluteTarget || relativeTarget || autoHidden;
	if (SDL_CursorVisible() == hidden) {
		if (hidden)
			SDL_HideCursor();
		else
			SDL_ShowCursor();
	}

	bool allowOverlay = allowInput;
	if (ATUIDebuggerIsOpen() && hasDisplay && !HasMousePopup()) {
		bool displayFocused = false;
		float x, y, w, h;
		ATUIDebuggerGetDisplayRect(x, y, w, h, displayFocused);
		allowOverlay = displayFocused || ATUIDebuggerIsDisplayImageHovered();
	}
	if (!im || !hasDisplay || !allowOverlay)
		return;
	ImDrawList *draw = ImGui::GetForegroundDrawList();
	if (!draw)
		return;
	const ImVec2 origin = ImGui::GetMainViewport()->Pos;
	const ImVec2 lo(origin.x + dx, origin.y + dy);
	const ImVec2 hi(lo.x + dw, lo.y + dh);
	draw->PushClipRect(lo, hi, true);
	const ImU32 light = IM_COL32(224, 228, 255, 255);
	const ImU32 dark = IM_COL32(16, 18, 48, 255);
	auto cross = [&](ImVec2 p, bool offTarget) {
		draw->AddLine(ImVec2(p.x - 20, p.y), ImVec2(p.x + 20, p.y), light);
		draw->AddLine(ImVec2(p.x, p.y - 20), ImVec2(p.x, p.y + 20), light);
		draw->AddLine(ImVec2(p.x - 10, p.y), ImVec2(p.x + 10, p.y), dark);
		draw->AddLine(ImVec2(p.x, p.y - 10), ImVec2(p.x, p.y + 10), dark);
		if (offTarget)
			draw->AddCircle(p, 7.0f, light, 16);
	};
	if (absoluteTarget
		&& (ATUIGetTargetPointerVisible() || !im->IsMouseActiveTarget()))
		cross(ImVec2(origin.x + mx, origin.y + my), !im->IsMouseActiveTarget());

	if (ATUIGetDrawPadBoundsEnabled() && im->HasNonBeamPointer())
		draw->AddRect(lo, ImVec2(hi.x - 1, hi.y - 1), light);
	if (ATUIGetDrawPadPointersEnabled()) {
		vdfastvector<ATInputPointerInfo> pointers;
		im->GetPointers(pointers);
		const vdrect32 scan(g_sim.GetGTIA().GetFrameScanArea());
		for (const auto& pointer : pointers) {
			// The primary mouse pointer already has the target cursor above.
			if (pointer.mbPrimary && im->HasAbsMousePointer()
				&& im->IsMouseActiveTarget() && absoluteTarget
				&& ATUIGetTargetPointerVisible())
				continue;
			float px = 0, py = 0;
			if (pointer.mCoordSpace == ATInputPointerCoordinateSpace::Normalized) {
				px = (pointer.mPos.x * 0.5f + 0.5f) * (dw - 1);
				py = (pointer.mPos.y * 0.5f + 0.5f) * (dh - 1);
			} else if (pointer.mCoordSpace == ATInputPointerCoordinateSpace::Beam
				&& scan.width() > 0 && scan.height() > 0) {
				px = (pointer.mPos.x - scan.left) * dw / scan.width();
				py = (pointer.mPos.y - scan.top) * dh / scan.height();
			} else
				continue;
			const ImVec2 p(lo.x + px, lo.y + py);
			if (pointer.mRadius < 0)
				cross(p, false);
			else {
				const float rx = pointer.mRadius * dw * 0.5f;
				const float ry = pointer.mRadius * dh * 0.5f;
				if (rx > 2 && ry > 2)
					draw->AddEllipse(p, ImVec2(rx, ry), light, 0, 32);
				else
					draw->AddRect(ImVec2(p.x - rx, p.y - ry),
						ImVec2(p.x + rx, p.y + ry), light);
			}
		}
	}
	draw->PopClipRect();
}
