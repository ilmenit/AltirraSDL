//	AltirraSDL - Reusable modal confirmation dialog implementation
//
//	See ui_confirm_dialog.h for the interface.  Implementation notes:
//
//	- The desktop build does not enable ImGuiConfigFlags_NavEnableKeyboard
//	  globally (only the mobile frontend does), so ImGui's built-in
//	  gamepad/keyboard nav is unavailable here.  We therefore drive
//	  button focus and the visual highlight ourselves, which keeps the
//	  behavior predictable on Windows, macOS, and Linux regardless of
//	  any future changes to the global nav flag.
//
//	- Dialogs are queued rather than drawn in place by the caller.
//	  ATUIShowConfirm() may be invoked from inside another ImGui window
//	  (e.g. a per-drive popup menu), and ImGui forbids OpenPopup() from
//	  inside a BeginPopup/EndPopup pair for a *different* popup ID.
//	  Queueing decouples construction from presentation.
//
//	- Callbacks are dispatched after CloseCurrentPopup() + EndPopup() so
//	  the dialog is already torn down when user code runs.  A callback
//	  that opens another confirmation works correctly: it just pushes a
//	  new entry that appears on the next frame.

#include <stdafx.h>
#include <imgui.h>
#include <deque>
#include <string>
#include <utility>
#include "ui_confirm_dialog.h"

namespace {

// All const char* fields from ATUIConfirmOptions are copied into owned
// std::strings here.  Call sites freely pass stack buffers
// (e.g. snprintf) without worrying about lifetime — the copy outlives
// the caller for the entire duration of the modal.
struct PendingConfirm {
	std::string          title;
	std::string          message;
	std::string          confirmLabel;
	std::string          cancelLabel;
	std::string          alternateLabel;
	ATUIConfirmCallback  onAlternate;
	bool                 destructive = false;
	unsigned group = 0;
	bool cancelled = false;
	ATUIConfirmCallback  onConfirm;
	ATUIConfirmCallback  onCancel;

	bool opened    = false;  // have we called OpenPopup() yet?
	int  selected  = 0;      // 0 = confirm, 1 = cancel
	bool focusInit = false;  // need to (re)apply SetKeyboardFocusHere
};

std::deque<PendingConfirm> g_pending;

}  // namespace

void ATUIShowConfirm(ATUIConfirmOptions opts) {
	PendingConfirm pc;
	pc.title        = opts.title        ? opts.title        : "Confirm";
	pc.message      = opts.message      ? opts.message      : "";
	pc.confirmLabel = opts.confirmLabel ? opts.confirmLabel : "OK";
	pc.cancelLabel  = opts.cancelLabel  ? opts.cancelLabel  : "Cancel";
	pc.alternateLabel = opts.alternateLabel ? opts.alternateLabel : "";
	pc.onAlternate = std::move(opts.onAlternate);
	pc.destructive  = opts.destructive;
	pc.group = opts.group;
	pc.onConfirm    = std::move(opts.onConfirm);
	pc.onCancel     = std::move(opts.onCancel);
	pc.selected     = pc.destructive ? 1 : 0;
	g_pending.push_back(std::move(pc));
}

void ATUIShowConfirm(const char *title, const char *message,
	ATUIConfirmCallback onConfirm, bool destructive)
{
	ATUIConfirmOptions opts;
	opts.title       = title;
	opts.message     = message;
	opts.onConfirm   = std::move(onConfirm);
	opts.destructive = destructive;
	ATUIShowConfirm(std::move(opts));
}

void ATUICancelConfirmations(unsigned group) {
	for (auto& prompt : g_pending)
		if (prompt.group == group) prompt.cancelled = true;
}

bool ATUIIsConfirmDialogActive() {
	return !g_pending.empty();
}

void ATUIRenderConfirmDialogs() {
	if (g_pending.empty())
		return;

	PendingConfirm &pc = g_pending.front();

	// Build a unique popup id so distinct simultaneous dialogs with the
	// same title don't collide.  The address of the queue entry is
	// stable across other pushes because std::deque preserves element
	// references under push_back / pop_front (unless the element itself
	// is erased — which only happens after the dialog closes).
	char popupId[160];
	snprintf(popupId, sizeof(popupId), "%s##ATConfirm_%p",
		pc.title.c_str(), (const void *)&pc);

	if (!pc.opened) {
		ImGui::OpenPopup(popupId);
		pc.opened    = true;
		pc.focusInit = false;
	}

	// Center on the main viewport every time the dialog appears.  Do
	// not persist its position — this mirrors the config/modal dialog
	// pattern documented in CLAUDE.md.
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
		ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSizeConstraints(ImVec2(320, 0),
		ImVec2(600, FLT_MAX));

	bool decisionConfirm = false;
	bool decisionCancel  = pc.cancelled;
	bool decisionAlternate = false;
	const int count = pc.alternateLabel.empty() ? 2 : 3;

	// `&open` lets the user close via the title-bar X (or via ImGui's
	// built-in ESC-to-close on modals, which sets *p_open = false).
	// An ImGui-side true->false transition translates to "cancel" via
	// the post-block check below.
	bool open = true;
	if (ImGui::BeginPopupModal(popupId, &open,
			ImGuiWindowFlags_AlwaysAutoResize
			| ImGuiWindowFlags_NoSavedSettings
			| ImGuiWindowFlags_NoMove))
	{
		// --- Message body ---
		// Wrap at an explicit window-local x so the auto-resize pass
		// converges on the first frame.  Using PushTextWrapPos(0) here
		// would create a feedback loop with AlwaysAutoResize (wrap
		// width depends on window width depends on wrap width).
		// The chosen width matches the upper SizeConstraint minus
		// approximate window padding.
		const float kWrapX = 560.0f;
		ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + kWrapX);
		ImGui::TextUnformatted(pc.message.c_str());
		ImGui::PopTextWrapPos();

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		// --- Keyboard handling ---
		// Only intercept keys when no text input field is focused; the
		// helper is button-only today but this keeps it safe for future
		// extension (e.g. an embedded "don't ask again" checkbox).
		ImGuiIO &io = ImGui::GetIO();
		if (!io.WantTextInput && !pc.cancelled) {
			if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, false)
				|| ImGui::IsKeyPressed(ImGuiKey_RightArrow, false)
				|| ImGui::IsKeyPressed(ImGuiKey_Tab, false))
			{
				const bool backwards = ImGui::IsKeyPressed(ImGuiKey_LeftArrow, false)
					|| (ImGui::IsKeyPressed(ImGuiKey_Tab, false) && io.KeyShift);
				int visual = pc.selected == 1 ? count - 1 : pc.selected == 2 ? 1 : 0;
				visual = (visual + (backwards ? count - 1 : 1)) % count;
				pc.selected = visual == count - 1 ? 1 : visual == 1 ? 2 : 0;
				pc.focusInit = false;
			}

			if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
				decisionCancel = true;
			}
			if (ImGui::IsKeyPressed(ImGuiKey_Enter, false)
				|| ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false)
				|| ImGui::IsKeyPressed(ImGuiKey_Space, false))
			{
				if (pc.selected == 0) decisionConfirm = true;
				else if (pc.selected == 1) decisionCancel = true;
				else decisionAlternate = true;
			}
		}

		// --- Buttons ---
		const float buttonWidth = 120.0f;
		const float spacing     = ImGui::GetStyle().ItemSpacing.x;
		const float total = buttonWidth * count + spacing * (count - 1);
		const float avail       = ImGui::GetContentRegionAvail().x;
		if (avail > total)
			ImGui::SetCursorPosX(ImGui::GetCursorPosX()
				+ (avail - total) * 0.5f);

		auto drawHighlight = [](ImVec2 rmin, ImVec2 rmax) {
			// Draw a 2px colored border around the item just rendered to
			// indicate keyboard focus.  The color is derived from the
			// current style so it adapts to light/dark themes.
			const ImVec4 &base = ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive);
			ImU32 col = ImGui::GetColorU32(ImVec4(base.x, base.y, base.z, 1.0f));
			const float pad = 2.0f;
			ImGui::GetWindowDrawList()->AddRect(
				ImVec2(rmin.x - pad, rmin.y - pad),
				ImVec2(rmax.x + pad, rmax.y + pad),
				col, 3.0f, 0, 2.0f);
		};

		// Confirm button
		if (!pc.focusInit && pc.selected == 0) {
			ImGui::SetKeyboardFocusHere();
			pc.focusInit = true;
		}
		if (ImGui::Button(pc.confirmLabel.c_str(), ImVec2(buttonWidth, 0)))
			decisionConfirm = true;
		ImVec2 confirmMin = ImGui::GetItemRectMin();
		ImVec2 confirmMax = ImGui::GetItemRectMax();

		ImVec2 alternateMin {}, alternateMax {};
		if (count == 3) {
			ImGui::SameLine();
			if (!pc.focusInit && pc.selected == 2) {
				ImGui::SetKeyboardFocusHere();
				pc.focusInit = true;
			}
			if (ImGui::Button(pc.alternateLabel.c_str(), ImVec2(buttonWidth, 0)))
				decisionAlternate = true;
			alternateMin = ImGui::GetItemRectMin();
			alternateMax = ImGui::GetItemRectMax();
		}
		ImGui::SameLine();

		// Cancel button
		if (!pc.focusInit && pc.selected == 1) {
			ImGui::SetKeyboardFocusHere();
			pc.focusInit = true;
		}
		if (ImGui::Button(pc.cancelLabel.c_str(), ImVec2(buttonWidth, 0)))
			decisionCancel = true;
		ImVec2 cancelMin = ImGui::GetItemRectMin();
		ImVec2 cancelMax = ImGui::GetItemRectMax();

		// Draw the highlight on whichever button is currently selected.
		if (pc.selected == 0)
			drawHighlight(confirmMin, confirmMax);
		else if (pc.selected == 1)
			drawHighlight(cancelMin, cancelMax);
		else
			drawHighlight(alternateMin, alternateMax);

		if (decisionConfirm || decisionCancel || decisionAlternate)
			ImGui::CloseCurrentPopup();

		ImGui::EndPopup();
	}

	if (pc.cancelled) {
		decisionConfirm = decisionAlternate = false;
		decisionCancel = true;
	}

	// User closed via title-bar X or click-outside while the popup was
	// still returning false from BeginPopupModal.  Treat as cancel.
	if (!open && !decisionConfirm && !decisionAlternate)
		decisionCancel = true;

	if (decisionConfirm || decisionCancel || decisionAlternate) {
		// Move the callbacks out of the entry before popping, so the
		// front() reference is no longer needed when we mutate the
		// deque, and so a callback that queues another confirmation
		// doesn't observe a still-present "previous" entry.
		ATUIConfirmCallback onConfirm = std::move(pc.onConfirm);
		ATUIConfirmCallback onCancel  = std::move(pc.onCancel);
		ATUIConfirmCallback onAlternate = std::move(pc.onAlternate);
		g_pending.pop_front();

		if (decisionConfirm) {
			if (onConfirm) onConfirm();
		} else if (decisionAlternate) {
			if (onAlternate) onAlternate();
		} else {
			if (onCancel)  onCancel();
		}
	}
}
