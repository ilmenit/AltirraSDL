// AltirraSDL emulated display accessibility. Native callbacks only read snapshots.
#pragma once

#include <memory>
#include <string>
#include <vector>

struct SDL_Window;
class ATSimulator;

struct ATSDLAccessibilityLine {
	int offset = 0;
	int length = 0;
	float x = 0, y = 0, width = 0, height = 0;
	bool operator==(const ATSDLAccessibilityLine&) const = default;
};

struct ATSDLAccessibilitySnapshot {
	std::wstring text;
	std::vector<ATSDLAccessibilityLine> lines;
	bool focused = false;
	float x = 0, y = 0, width = 0, height = 0;
	int windowX = 0, windowY = 0;
	int windowWidth = 0, windowHeight = 0;
	bool operator==(const ATSDLAccessibilitySnapshot&) const = default;
};

enum class ATSDLAccessibilityState { Disabled, Connecting, Active, Unavailable };
struct ATSDLAccessibilityStatus {
	ATSDLAccessibilityState state = ATSDLAccessibilityState::Disabled;
	std::string message;
};
// Status crosses the Linux publisher worker boundary as plain copied data.
ATSDLAccessibilityStatus ATSDLAccessibilityGetStatus();
void ATSDLAccessibilityPublishStatus(ATSDLAccessibilityState, const char *message);

// Call only on the emulator thread. Rectangle coordinates are SDL logical
// client coordinates; platform adapters convert to native screen coordinates.
void ATSDLAccessibilityUpdate(SDL_Window *, ATSimulator &, bool enabled,
	bool focused, float x, float y, float width, float height);
void ATSDLAccessibilityShutdown();
// Main-thread notification that must run even when the next frame cannot be
// acquired (minimized window/device loss). It only republishes a plain snapshot.
void ATSDLAccessibilityFocusLost();
std::shared_ptr<const ATSDLAccessibilitySnapshot> ATSDLAccessibilityGetSnapshot();

// Platform entry points. They must never access the simulator or ImGui state.
void ATSDLAccessibilityPlatformUpdate(SDL_Window *,
	std::shared_ptr<const ATSDLAccessibilitySnapshot>);
void ATSDLAccessibilityPlatformShutdown();
