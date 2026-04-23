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
#include <imgui.h>  // for ImVec2 / ImVec4 used by inline helpers below

// -------------------------------------------------------------------------
// Mobile / Gaming-Mode palette
//
// The Gaming-Mode UI paints most of its widgets by hand with ImDrawList
// so colour choices are not driven by the default ImGui Dark/Light
// themes.  This palette is the single source of truth that every
// mobile_*.cpp file consults so a change to `g_ATOptions.mThemeMode`
// (System / Light / Dark) re-colours the whole UI uniformly.
//
// Each colour is an ImU32 (premultiplied RGBA packed little-endian —
// the value format returned by IM_COL32) and is resolved once per call
// to ATMobileGetPalette() against the currently resolved theme.  The
// struct is cheap to copy and safe to capture by value.
// -------------------------------------------------------------------------

struct ATMobilePalette {
	bool dark;

	// Page / panel surfaces
	uint32 windowBg;            // overall settings / menu background
	uint32 windowBgTop;         // top of subtle window gradient
	uint32 cardBg;              // resting card / category row background
	uint32 cardBgTop;            // top of card gradient (lighter in dark, darker in light)
	uint32 cardBgHover;         // hovered / focused card background
	uint32 cardBgHoverTop;      // top of hovered card gradient
	uint32 cardBorder;          // subtle 1px border around cards
	uint32 backdropDim;         // full-screen modal dim backdrop

	// Primary accent (blue on both themes, calibrated for contrast)
	uint32 accent;
	uint32 accentHover;
	uint32 accentPressed;
	uint32 accentSoft;          // low-alpha accent for focus rings
	uint32 accentTop;           // gradient top for accent fills

	// Text
	uint32 text;                // primary body text
	uint32 textMuted;           // secondary / subtitle text
	uint32 textOnAccent;        // text drawn on an accent background
	uint32 textSection;         // section header text (slightly tinted)
	uint32 textTitle;           // title text in modal sheets

	// Toggle / slider track
	uint32 trackOff;
	uint32 trackOn;
	uint32 thumb;

	// Segmented control
	uint32 segBgInactive;
	uint32 segBgInactiveTop;
	uint32 segBgHover;
	uint32 segBgHoverTop;
	uint32 segBgActive;
	uint32 segBgActiveTop;
	uint32 segBorder;
	uint32 segFocus;

	// Row hover / focus overlay (drawn on top of card bg)
	uint32 rowHover;
	uint32 rowFocus;

	// Modal sheet
	uint32 modalBg;
	uint32 modalBgTop;
	uint32 modalBorder;

	// Generic button (e.g. header back-arrow) — tints only
	uint32 buttonHover;         // low-alpha foreground tint on hover
	uint32 buttonActive;        // same, pressed

	// Semantic state colours (calibrated per-theme for readability)
	uint32 warning;             // amber — e.g. "disk modified" tag
	uint32 danger;              // red   — e.g. destructive state warnings
	uint32 success;             // green — e.g. "saved" confirmation tag
};

// Fetch the palette for the currently active theme.  Cheap; call per
// frame rather than caching across frames so a live theme switch takes
// effect without an explicit invalidation pass.
const ATMobilePalette &ATMobileGetPalette();

// ImU32 → ImVec4 conversion for ImGui PushStyleColor / TextColored
// sites that need a palette colour.  Inline so there is no link-time
// duplication — each TU gets its own stamp.
inline ImVec4 ATMobileCol(uint32 c) {
	return ImVec4(
		((c >> IM_COL32_R_SHIFT) & 0xFF) / 255.0f,
		((c >> IM_COL32_G_SHIFT) & 0xFF) / 255.0f,
		((c >> IM_COL32_B_SHIFT) & 0xFF) / 255.0f,
		((c >> IM_COL32_A_SHIFT) & 0xFF) / 255.0f);
}

// Convenience: wrap TextWrapped in a PushStyleColor(textMuted).  Saves
// the six-line Push/TextWrapped/Pop boilerplate that appears on every
// hint caption in the mobile settings pages.
void ATTouchMutedText(const char *text);

// Draw a subtle vertical gradient inside [p1,p2] going from `topCol`
// at the top to `bottomCol` at the bottom, with rounded corners.
// Thin wrapper around ImDrawList::AddRectFilledMultiColor that also
// handles the rounded-corner fallback (ImGui's multi-colour rect does
// not natively support rounding — we stamp a matching rounded rect on
// top at `bottomCol` alpha 0 and rely on the gradient showing through).
// Using the simpler non-rounded multicolor rect where rounding==0.
void ATMobileDrawGradientRect(const ImVec2 &p1, const ImVec2 &p2,
	uint32 topCol, uint32 bottomCol, float rounding);

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

// Styled button.  Card-surface gradient background, palette-aware
// hover/focus/active states, 56dp minimum touch height, full-width
// or custom-sized.  Replaces raw ImGui::Button inside Gaming Mode so
// every interactive surface shares the same lifted-card aesthetic as
// the Settings category rows.
//
// Variants:
//   Neutral — card-gradient background, body text colour.  Use for
//             list items, menu entries, secondary actions.
//   Accent  — primary-blue gradient, white text.  Use for the hero
//             action on a screen (Boot Game, Confirm, etc.).
//   Danger  — warning-red gradient, white text.  Destructive actions
//             (Delete, Exit without saving, etc.).
//   Subtle  — fully transparent background with a low-alpha hover
//             tint.  Use for header icon buttons (back arrow, close)
//             where a card would crowd the layout.
//
// `size` follows the ImGui::Button convention: pass 0 in either axis
// to auto-size, -FLT_MIN (or -1) to fill the available axis extent.
// Returns true on the frame the button is clicked.
enum class ATTouchButtonStyle {
	Neutral,
	Accent,
	Danger,
	Subtle,
};

bool ATTouchButton(const char *label, const ImVec2 &size,
	ATTouchButtonStyle style = ATTouchButtonStyle::Neutral);

// List-item row.  Full-width, two-line (title + subtitle) card with
// an optional chevron on the right.  Used by the Settings home screen
// for category rows, and by the file browser / hamburger menu for
// list entries.  Returns true on click.  The `selected` flag paints
// the accent colour so the row stands out from its peers.
bool ATTouchListItem(const char *title, const char *subtitle,
	bool selected = false, bool chevron = true);

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

// Call from the top-level SDL event pump whenever a SDL_EVENT_FINGER_*
// event is seen — including events that subsequently get consumed by
// the touch-controls / hamburger / file-browser dispatchers.  This
// stamps a "recent touch" timestamp that ATTouchDragScroll() uses as a
// reliable touch indicator in addition to ImGui's MouseSource flag.
// On Android the synthetic mouse events do not always carry
// SDL_TOUCH_MOUSEID in a form that ImGui's SDL3 backend tags as
// TouchScreen on the first frame, so relying solely on MouseSource
// causes the momentum path to silently no-op.  The timestamp path is
// version-independent.
void ATTouchNotifyFingerEvent();

// -------------------------------------------------------------------------
// Shared Gaming-Mode primitives (promoted from ui_netplay_widgets so they
// are reachable from the hamburger/settings/library screens too).
// -------------------------------------------------------------------------

namespace ATTouch {
constexpr float kButtonHeightLarge   = 56.0f; // hero / menu items
constexpr float kButtonHeightNormal  = 48.0f; // primary footer actions
constexpr float kButtonHeightSmall   = 40.0f; // inline / tertiary
constexpr float kFooterReserveSingle = 72.0f;
constexpr float kFooterReserveDouble = 144.0f;
}

// DPI-scaled pixel-per-dp helper.  Returns `dp` multiplied by the
// current content scale (same logic used by the older ATNetplayUI::Dp).
float ATTouchDp(float dp);

// Safe-area insets control.  The mobile main-loop calls SetSafeAreaInsets
// each frame with the platform-reported notch/gesture margins; Desktop
// leaves zeros.  Origin/Size return the content rect inside the main
// viewport minus those insets.
void  ATTouchSetSafeAreaInsets(int top, int bottom, int left, int right);
ImVec2 ATTouchSafeOrigin();
ImVec2 ATTouchSafeSize();

// On-screen keyboard (IME) height hint.  Android reports the soft-
// keyboard height via SDL events; the touch UI uses this to shrink the
// effective safe-area bottom so pinned footers stay visible and
// ATTouchInputTextScrollAware can park focused inputs above the IME.
// Desktop leaves this zero.
void  ATTouchSetImeHeight(int px);
int   ATTouchGetImeHeight();

// Shared screen header: 48×52dp "<" back button + centred title.  No-op
// on Desktop (ImGui window chrome handles the title).  Returns true on
// the frame the user activates the back affordance (mouse, touch, Enter,
// Gamepad A) — and intercepts Escape / Gamepad B as hardware back keys.
bool ATTouchScreenHeader(const char *title);

// Transient toast API.  See the older PushToast/RenderToasts/ClearToasts
// contract: the ToastSeverity value maps to palette colours, toasts
// stack with a per-entry fade and auto-dismiss, and RenderToasts should
// be called once per frame *outside* any modal / overlay window so the
// toast stays visible when the overlay is Closed.
enum class ATTouchToastSeverity {
	Info,     // neutral surface, textMuted on card
	Success,  // green pill
	Warning,  // amber pill
	Danger,   // red pill
};

void ATTouchPushToast(const char *text,
	ATTouchToastSeverity severity = ATTouchToastSeverity::Info,
	uint64_t durationMs = 4000);
void ATTouchRenderToasts();
void ATTouchClearToasts();

// Unified feedback API — routes either to a transient toast
// (blocking=false) or the ShowInfoModal dialog (blocking=true).  Intended
// replacement for the scattered ShowInfoModal success-path calls in the
// mobile shell; errors / confirmations that require acknowledgement stay
// on ShowInfoModal directly.
void ATTouchPushFeedback(const char *title, const char *body,
	ATTouchToastSeverity severity = ATTouchToastSeverity::Info,
	bool blocking = false, uint64_t durationMs = 4000);

// InputText wrapper that, on the frame the widget first becomes active,
// scrolls the focused input into view (roughly the upper third of the
// scrollable host) so the on-screen keyboard does not cover it.  Drop-in
// replacement for ImGui::InputText for Gaming-Mode single-line inputs.
bool ATTouchInputTextScrollAware(const char *label, char *buf, size_t bufSize,
	ImGuiInputTextFlags flags = 0);

// Same as ATTouchInputTextScrollAware but renders a placeholder hint when
// the buffer is empty — drop-in replacement for ImGui::InputTextWithHint.
bool ATTouchInputTextWithHintScrollAware(const char *label, const char *hint,
	char *buf, size_t bufSize, ImGuiInputTextFlags flags = 0);

// Render a vertically-centred empty state inside the current child
// window: large muted title, wrapped body text, and an optional CTA
// button (ATTouchButton Accent) that calls `onCta` when pressed.  The
// container's current content region drives the vertical centering so
// this can slot into BeginScreenBody() directly.
void ATTouchEmptyState(const char *title, const char *body,
	const char *ctaLabel, void (*onCta)(void *userData),
	void *userData = nullptr);
