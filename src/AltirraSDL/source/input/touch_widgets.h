//	AltirraSDL - Touch-friendly ImGui widgets
//
//	Raw ImGui widgets (Checkbox, Combo, SliderInt) are designed for
//	mouse use and feel cramped on a finger, even with
//	`style.ScaleAllSizes(contentScale)` applied.  This module provides
//	custom-drawn wrappers sized per Material Design touch-target
//	guidelines (≥48dp hit area), using the ImGui low-level draw APIs:
//
//	  - ATTouchToggle     — animated switch (replaces Checkbox)
//	  - ATTouchSegmented  — 2-6 choice tab bar (replaces Combo)
//	  - ATTouchSlider     — full-width slider with 24dp thumb
//	  - ATTouchSection    — styled section header
//
//	All sizes are expressed in dp and internally converted to pixels
//	using the same `dp()` helper as the rest of the mobile UI.  Each
//	widget returns true on the frame the value changes, so caller
//	code can persist settings immediately.

#pragma once

#include <vd2/system/vdtypes.h>

// Animated toggle switch.  Full-row (56dp tall) hit area, label on
// the left, pill track on the right.  Returns true on change.
bool ATTouchToggle(const char *label, bool *value);

// Horizontal segmented control.  Each segment has a ≥56dp wide /
// 48dp tall hit area; segments stretch to fill the available width.
// `items[count]` is an array of C-string labels.  `*current` is the
// zero-based selected index.  Returns true on change.
bool ATTouchSegmented(const char *label, int *current,
	const char * const *items, int count);

// Numeric slider.  Full available width, 48dp tall hit area with a
// 24dp visual thumb and a value label on the right.  `fmt` is a
// printf-style format string for the value label (e.g. "%d%%").
// Returns true on change.
bool ATTouchSlider(const char *label, int *value, int minv, int maxv,
	const char *fmt);

// Styled section header.  Uppercase accent-coloured text with the
// standard spacing above and below.  Drop-in replacement for
// `ImGui::SeparatorText` that matches the rest of the touch UI.
void ATTouchSection(const char *label);

// Touch-drag scroll for the current scrollable window / BeginChild.
// Call at the top of any child that the user should be able to
// scroll by swiping anywhere inside it.  Tracks a latched down-state
// per-window so the first pixel of drag starts scrolling (no drag
// threshold), uses a fractional accumulator to avoid stepping on
// high-DPI displays, and clamps to the window's scroll extents.
//
// Call AFTER `ImGui::Begin`/`ImGui::BeginChild` and BEFORE rendering
// the children whose touches should drive the scroll.
void ATTouchDragScroll();

// Pair with ATTouchDragScroll().  Call before ImGui::End() / EndChild()
// to pop any style state ATTouchDragScroll() pushed for the current
// frame (used to suppress hover/active highlight on Buttons and
// Selectables that the finger drags across — touch screens have no
// hover and Material list scrolling never lights up items in transit).
void ATTouchEndDragScroll();

// Returns true if the widget activation (e.g. Button click) at the
// current mouse position should be suppressed because the finger has
// drifted beyond the tap-slop threshold.  Use to let touch-drag
// scrolling take precedence over a Button that was pressed down
// before the drag started.
bool ATTouchIsDraggingBeyondSlop();
