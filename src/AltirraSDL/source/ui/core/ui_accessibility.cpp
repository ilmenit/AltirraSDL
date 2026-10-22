#include <stdafx.h>
#include "ui_accessibility.h"
#include <algorithm>
#include <atomic>
#include <mutex>
#include <SDL3/SDL.h>
#include <at/atcore/atascii.h>
#include <at/atcore/devicevideo.h>
#include "simulator.h"
#include "antic.h"
#include "gtia.h"
#include "video_output.h"
#include "ui_textselection.h"

namespace {
	std::shared_ptr<const ATSDLAccessibilitySnapshot> g_snapshot;
	Uint64 g_lastCapture = 0;
	SDL_Window *g_window = nullptr;
	std::mutex g_statusMutex;
	ATSDLAccessibilityStatus g_status;

	void AppendLine(ATSDLAccessibilitySnapshot& snapshot, const uint8 *data,
		int count, bool intl, float x, float y, float cellWidth, float height) {
		while (count && (data[count - 1] & 0x7f) == 0x20)
			--count;
		if (!snapshot.lines.empty())
			snapshot.text += L'\n';
		ATSDLAccessibilityLine line;
		line.offset = (int)snapshot.text.size();
		line.x = x;
		line.y = y;
		line.width = cellWidth * count;
		line.height = height;
		const auto& table = kATATASCIITables.mATASCIIToUnicode[intl ? 1 : 0];
		// The high bit is reverse video, not a different spoken character.
		for (int i = 0; i < count; ++i)
			snapshot.text += table[data[i] & 0x7f];
		line.length = (int)snapshot.text.size() - line.offset;
		snapshot.lines.push_back(line);
	}
}

ATSDLAccessibilityStatus ATSDLAccessibilityGetStatus() {
	std::lock_guard lock(g_statusMutex);
	return g_status;
}

void ATSDLAccessibilityPublishStatus(ATSDLAccessibilityState state, const char *message) {
	std::lock_guard lock(g_statusMutex);
	g_status.state = state;
	g_status.message = message;
}

std::shared_ptr<const ATSDLAccessibilitySnapshot> ATSDLAccessibilityGetSnapshot() {
	return std::atomic_load(&g_snapshot);
}

void ATSDLAccessibilityUpdate(SDL_Window *window, ATSimulator& sim,
	bool enabled, bool focused, float x, float y, float width, float height) {
	if (!enabled || !window) {
		if (g_window)
			ATSDLAccessibilityShutdown();
		return;
	}
	if (g_window && g_window != window)
		ATSDLAccessibilityShutdown();
	g_window = window;
	const auto old = ATSDLAccessibilityGetSnapshot();
	const auto now = SDL_GetTicks();
	if (old && old->focused == focused && now - g_lastCapture < 100)
		return;
	g_lastCapture = now;
	auto next = std::make_shared<ATSDLAccessibilitySnapshot>();
	next->focused = focused;
	next->x = x;
	next->y = y;
	next->width = std::max(0.0f, width);
	next->height = std::max(0.0f, height);
	SDL_GetWindowPosition(window, &next->windowX, &next->windowY);
	SDL_GetWindowSize(window, &next->windowWidth, &next->windowHeight);
	if (width > 0 && height > 0) {
		if (auto *output = ATSDLGetActiveVideoOutput()) {
			const auto& info = output->GetVideoInfo();
			const auto& area = info.mDisplayArea;
			std::vector<uint8> data(std::clamp(info.mTextColumns, 0, 4096));
			if (!area.empty() && !data.empty()) {
				for (int row = 0; row < std::clamp(info.mTextRows, 0, 1024); ++row) {
					int count = output->ReadRawText(data.data(), 0, row, (int)data.size());
					count = std::clamp(count, 0, (int)data.size());
					const auto rect = output->CharToPixelRect(vdrect32(0, row, 1, row + 1));
					AppendLine(*next, data.data(), count, false,
						x + (rect.left - area.left) * width / area.width(),
						y + (rect.top - area.top) * height / area.height(),
						rect.width() * width / area.width(),
						rect.height() * height / area.height());
				}
			}
		} else {
			const auto area = sim.GetGTIA().GetFrameScanArea();
			const auto *history = sim.GetAntic().GetDLHistory();
			if (!area.empty()) {
				for (int row = 8; row < 248; ++row) {
					uint8 data[48];
					bool intl;
					int count = ATUITextReadAnticLine(data, row, 0, 48, intl);
					if (!count)
						continue;
					const auto& entry = history[row];
					int pfwidth = entry.mDMACTL & 3;
					if (pfwidth < 3 && (entry.mControl & 0x10))
						++pfwidth;
					int lineHeight = 1;
					while (lineHeight < 16 && row + lineHeight < 248
						&& !history[row + lineHeight].mbValid)
						++lineHeight;
					const int cellWidth = entry.mControl & 4 ? 8 : 4;
					AppendLine(*next, data, count, intl,
						x + (0x50 - 0x10 * pfwidth - area.left) * width / area.width(),
						y + (row - area.top) * height / area.height(),
						cellWidth * width / area.width(), lineHeight * height / area.height());
				}
			}
		}
	}
	if (!old || *old != *next) {
		if (!old)
			ATSDLAccessibilityPublishStatus(ATSDLAccessibilityState::Connecting,
				"Connecting to the desktop accessibility service...");
		std::shared_ptr<const ATSDLAccessibilitySnapshot> immutable = next;
		std::atomic_store(&g_snapshot, immutable);
		ATSDLAccessibilityPlatformUpdate(window, std::move(immutable));
	}
}

void ATSDLAccessibilityShutdown() {
	ATSDLAccessibilityPlatformShutdown();
	ATSDLAccessibilityPublishStatus(ATSDLAccessibilityState::Disabled, "Screen reader support is disabled.");
	std::atomic_store(&g_snapshot, std::shared_ptr<const ATSDLAccessibilitySnapshot>());
	g_window = nullptr;
	g_lastCapture = 0;
}

void ATSDLAccessibilityFocusLost() {
	const auto previous = ATSDLAccessibilityGetSnapshot();
	if (!g_window || !previous || !previous->focused)
		return;
	auto next = std::make_shared<ATSDLAccessibilitySnapshot>(*previous);
	next->focused = false;
	std::shared_ptr<const ATSDLAccessibilitySnapshot> immutable = next;
	std::atomic_store(&g_snapshot, immutable);
	ATSDLAccessibilityPlatformUpdate(g_window, std::move(immutable));
}

#if defined(__ANDROID__) || defined(__EMSCRIPTEN__) || (!defined(_WIN32) && !defined(__APPLE__) && !defined(__linux__))
void ATSDLAccessibilityPlatformUpdate(SDL_Window *,
	std::shared_ptr<const ATSDLAccessibilitySnapshot>) {
	ATSDLAccessibilityPublishStatus(ATSDLAccessibilityState::Unavailable,
		"Emulated-screen accessibility requires the Windows, macOS, or Linux desktop application.");
}
void ATSDLAccessibilityPlatformShutdown() {}
#endif
