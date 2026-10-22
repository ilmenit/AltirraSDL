//	AltirraSDL - Reusable modal confirmation dialog
//	Cross-platform (Windows/macOS/Linux) ImGui confirmation helper with
//	full keyboard navigation: Left/Right/Tab select, Enter/Space activate,
//	Escape cancels.  Default-focus button matches Windows behavior for
//	destructive actions (Cancel is default when `destructive` is true).

#pragma once

#include <functional>

using ATUIConfirmCallback = std::function<void()>;

struct ATUIConfirmOptions {
	// Dialog title.  Also used as the ImGui popup identifier, so pick a
	// unique per-site value when multiple distinct confirmations can be
	// stacked; the helper appends a disambiguator internally as well.
	const char *title = "Confirm";

	// Body text (UTF-8, may contain \n).  Wrapped automatically.
	const char *message = "";

	// Button labels.  Default is English OK/Cancel to match existing
	// dialogs in this build; pass Yes/No for question-style prompts.
	const char *confirmLabel = "OK";
	const char *cancelLabel  = "Cancel";
	// Optional third choice, displayed between confirm and cancel.
	const char *alternateLabel = nullptr;
	ATUIConfirmCallback onAlternate;

	// If true, the cancel button is the initially-focused / default
	// button.  Use for destructive actions (discard unsaved work, delete
	// files, etc.) to match Windows Altirra's safe-default convention.
	bool destructive = false;
	unsigned group = 0; // Allows an owner to cancel obsolete queued prompts.

	// Invoked on confirm / cancel respectively.  Cancel callback is
	// optional and is also fired when the user presses Escape, clicks
	// the close (X) button, or clicks outside the dialog.
	ATUIConfirmCallback onConfirm;
	ATUIConfirmCallback onCancel;
};

// Queue a confirmation dialog.  Safe to call from any ImGui rendering
// context; the dialog is displayed by the next call to
// ATUIRenderConfirmDialogs().  Multiple queued dialogs are shown
// one-at-a-time in submission order.
//
// Thread: main thread only (same thread that drives ImGui).
void ATUIShowConfirm(ATUIConfirmOptions opts);

// Convenience overload for the common "title + message + confirm cb" case.
void ATUIShowConfirm(const char *title,
	const char *message,
	ATUIConfirmCallback onConfirm,
	bool destructive = false);

// Render any pending confirmation dialogs.  Must be called once per
// frame, after all other UI rendering (so the modal draws above
// everything else).
void ATUIRenderConfirmDialogs();

// True if a confirmation dialog is currently on the queue (visible or
// about to be shown).  Used by main loop to suppress hotkey handling
// when a modal is active.
bool ATUIIsConfirmDialogActive();

void ATUICancelConfirmations(unsigned group);
