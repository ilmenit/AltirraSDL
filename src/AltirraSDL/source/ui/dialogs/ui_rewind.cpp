//	AltirraSDL - Rewind dialog and Quick Rewind
//	ImGui implementation of the Windows ATUIDialogSaveState rewind browser.
//	Shows a visual timeline of saved states with preview images.

#include <stdafx.h>
#include <SDL3/SDL.h>
#include <imgui.h>

#include <vd2/system/vdtypes.h>
#include <vd2/system/date.h>
#include <vd2/system/refcount.h>
#include <vd2/Kasumi/pixmap.h>
#include <vd2/Kasumi/pixmaputils.h>

#include "ui_main.h"
#include "display_backend.h"
#include "gl_helpers.h"
#include "simulator.h"
#include "autosavemanager.h"

extern ATSimulator g_sim;
extern SDL_Window *g_pWindow;

// Rewind dialog state
static vdvector<vdrefptr<IATAutoSaveView>> s_rewindSaves;
static int s_rewindIndex = 0;
static int s_previewLastIndex = -1;     // track which save was last uploaded
static SDL_Texture *s_previewSDLTexture = nullptr;
static uint32 s_previewGLTexture = 0;
static int s_previewTexW = 0;
static int s_previewTexH = 0;
static bool s_rewindJustOpened = false;
static bool s_rewindWasPaused = false;

static void CleanupPreviewTexture() {
	if (s_previewSDLTexture) {
		SDL_DestroyTexture(s_previewSDLTexture);
		s_previewSDLTexture = nullptr;
	}
	if (s_previewGLTexture) {
		glDeleteTextures(1, &s_previewGLTexture);
		s_previewGLTexture = 0;
	}
	s_previewTexW = 0;
	s_previewTexH = 0;
	s_previewLastIndex = -1;
}

static ImTextureID UpdatePreviewTexture(int saveIndex, const VDPixmap *px) {
	if (!px || !px->data || px->w <= 0 || px->h <= 0) {
		CleanupPreviewTexture();
		s_previewLastIndex = -1;
		return (ImTextureID)nullptr;
	}

	IDisplayBackend *backend = ATUIGetDisplayBackend();
	bool useGL = backend && backend->GetType() == DisplayBackendType::OpenGL;

	bool needsAlloc = (s_previewTexW != px->w || s_previewTexH != px->h);
	if (useGL)
		needsAlloc = needsAlloc || !s_previewGLTexture;
	else
		needsAlloc = needsAlloc || !s_previewSDLTexture;

	if (needsAlloc) {
		CleanupPreviewTexture();
		s_previewTexW = px->w;
		s_previewTexH = px->h;
		s_previewLastIndex = -1; // force upload

		if (useGL) {
			// Rewind preview pixels are XRGB8888 — same byte layout as
			// the live emulator frame, fed by the rewind ring buffer.
			s_previewGLTexture = GLCreateXRGB8888Texture(
				px->w, px->h, false, nullptr);
			if (!s_previewGLTexture)
				return (ImTextureID)nullptr;
		} else {
			SDL_Renderer *renderer = SDL_GetRenderer(g_pWindow);
			if (!renderer)
				return (ImTextureID)nullptr;
			s_previewSDLTexture = SDL_CreateTexture(renderer,
				SDL_PIXELFORMAT_XRGB8888,
				SDL_TEXTUREACCESS_STREAMING, px->w, px->h);
			if (!s_previewSDLTexture)
				return (ImTextureID)nullptr;
		}
	}

	// Only re-upload when the selected save changed
	if (s_previewLastIndex != saveIndex) {
		if (useGL && s_previewGLTexture) {
			// Convert to contiguous buffer for GL upload
			std::vector<uint32> buf(px->w * px->h);
			const uint8 *src = (const uint8 *)px->data;
			for (int y = 0; y < px->h; ++y) {
				memcpy(&buf[y * px->w], src + y * px->pitch, px->w * 4);
			}
			glBindTexture(GL_TEXTURE_2D, s_previewGLTexture);
			GLUploadXRGB8888(px->w, px->h, buf.data(), 0);
		} else if (s_previewSDLTexture) {
			void *pixels = nullptr;
			int pitch = 0;
			if (SDL_LockTexture(s_previewSDLTexture, nullptr, &pixels, &pitch)) {
				const uint8 *src = (const uint8 *)px->data;
				uint8 *dst = (uint8 *)pixels;
				int copyBytes = px->w * 4;

				for (int y = 0; y < px->h; ++y) {
					memcpy(dst, src, copyBytes);
					src += px->pitch;
					dst += pitch;
				}
				SDL_UnlockTexture(s_previewSDLTexture);
			}
		}
		s_previewLastIndex = saveIndex;
	}

	if (useGL)
		return (ImTextureID)(intptr_t)s_previewGLTexture;
	return (ImTextureID)s_previewSDLTexture;
}

static void FormatRunTime(char *buf, size_t bufSize, double seconds) {
	int totalSec = (int)seconds;
	int h = totalSec / 3600;
	int m = (totalSec / 60) % 60;
	int s = totalSec % 60;
	snprintf(buf, bufSize, "%d:%02d:%02d", h, m, s);
}

static void FormatTimeAgo(char *buf, size_t bufSize, const VDDate &saveDate, const VDDate &nowDate) {
	VDDateInterval diff = nowDate - saveDate;
	int64 diffTicks = diff.mDeltaTicks;

	if (diffTicks <= 0) {
		snprintf(buf, bufSize, "just now");
		return;
	}

	uint64 diffSec = (uint64)diffTicks / 10000000ULL;

	if (diffSec < 60)
		snprintf(buf, bufSize, "%u seconds ago", (unsigned)diffSec);
	else if (diffSec < 3600)
		snprintf(buf, bufSize, "%u minutes ago", (unsigned)(diffSec / 60));
	else if (diffSec < 86400)
		snprintf(buf, bufSize, "%u hours ago", (unsigned)(diffSec / 3600));
	else
		snprintf(buf, bufSize, "%u days ago", (unsigned)(diffSec / 86400));
}

bool ATUIOpenRewindDialog() {
	IATAutoSaveManager &mgr = g_sim.GetAutoSaveManager();
	s_rewindSaves.clear();
	mgr.GetRewindStates(s_rewindSaves);

	if (s_rewindSaves.empty())
		return false;

	s_rewindIndex = (int)s_rewindSaves.size() - 1; // start at most recent
	s_rewindJustOpened = true;
	s_rewindWasPaused = !g_sim.IsRunning();
	return true;
}

void ATUIQuickRewind() {
	IATAutoSaveManager &mgr = g_sim.GetAutoSaveManager();
	if (mgr.GetRewindEnabled()) {
		// Rewind restores state but does not change run/pause.
		// Match Windows: caller does not force resume.
		mgr.Rewind();
	}
}

void ATUIRenderRewindDialog(ATSimulator &sim, ATUIState &state) {
	if (s_rewindSaves.empty()) {
		state.showRewind = false;
		CleanupPreviewTexture();
		return;
	}

	// Pause emulation while rewind dialog is open
	if (s_rewindJustOpened) {
		if (!s_rewindWasPaused)
			sim.Pause();
		s_rewindJustOpened = false;
	}

	ImGuiIO &io = ImGui::GetIO();
	ImVec2 displaySize = io.DisplaySize;

	// Full-screen semi-transparent overlay
	ImGui::SetNextWindowPos(ImVec2(0, 0));
	ImGui::SetNextWindowSize(displaySize);
	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0.75f));

	if (!ImGui::Begin("##RewindOverlay", nullptr,
			ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings |
			ImGuiWindowFlags_NoMove)) {
		ImGui::PopStyleColor();
		ImGui::End();
		return;
	}

	IATAutoSaveManager &mgr = sim.GetAutoSaveManager();
	int count = (int)s_rewindSaves.size();
	bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

	// Helper lambda to close the dialog and clean up
	auto closeDialog = [&](bool applyState) {
		if (applyState && s_rewindIndex >= 0 && s_rewindIndex < count) {
			s_rewindSaves[s_rewindIndex]->ApplyState();
			if (!s_rewindWasPaused)
				sim.Resume();
		} else if (!applyState) {
			if (!s_rewindWasPaused)
				sim.Resume();
		}
		s_rewindSaves.clear();
		state.showRewind = false;
		CleanupPreviewTexture();
		ImGui::PopStyleColor();
		ImGui::End();
	};

	// Keyboard navigation — only when overlay has focus
	if (focused) {
		if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow) || ImGui::IsKeyPressed(ImGuiKey_GamepadDpadLeft)) {
			if (s_rewindIndex > 0)
				--s_rewindIndex;
		}
		if (ImGui::IsKeyPressed(ImGuiKey_RightArrow) || ImGui::IsKeyPressed(ImGuiKey_GamepadDpadRight)) {
			if (s_rewindIndex < count - 1)
				++s_rewindIndex;
		}

		// Accept: apply state
		if (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown)) {
			closeDialog(true);
			return;
		}

		// Cancel: close without applying
		if (ImGui::IsKeyPressed(ImGuiKey_Escape) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight)) {
			closeDialog(false);
			return;
		}
	}

	// Get current save info
	IATAutoSaveView *view = s_rewindSaves[s_rewindIndex];

	// --- Layout: centered content area ---
	float centerX = displaySize.x * 0.5f;
	float topY = displaySize.y * 0.08f;

	// Title line
	char titleBuf[64];
	snprintf(titleBuf, sizeof(titleBuf), "Rewind %d/%d", s_rewindIndex + 1, count);

	ImVec2 titleSize = ImGui::CalcTextSize(titleBuf);
	ImGui::SetCursorPos(ImVec2(centerX - titleSize.x * 0.5f, topY));
	ImGui::TextUnformatted(titleBuf);

	// Runtime line
	char runTimeBuf[64];
	FormatRunTime(runTimeBuf, sizeof(runTimeBuf), view->GetSimulatedTimeSeconds());

	char runtimeLine[128];
	bool pastColdReset = (view->GetColdStartId() != mgr.GetCurrentColdStartId());
	if (pastColdReset)
		snprintf(runtimeLine, sizeof(runtimeLine), "Runtime: %s [past cold reset]", runTimeBuf);
	else
		snprintf(runtimeLine, sizeof(runtimeLine), "Runtime: %s", runTimeBuf);

	ImVec2 rtSize = ImGui::CalcTextSize(runtimeLine);
	ImGui::SetCursorPos(ImVec2(centerX - rtSize.x * 0.5f, topY + 24));
	ImGui::TextUnformatted(runtimeLine);

	// Time ago line
	VDDate saveDate = view->GetTimestamp();
	VDDate nowDate = VDGetCurrentDate();
	char timeAgoBuf[128];
	FormatTimeAgo(timeAgoBuf, sizeof(timeAgoBuf), saveDate, nowDate);

	ImVec2 taSize = ImGui::CalcTextSize(timeAgoBuf);
	ImGui::SetCursorPos(ImVec2(centerX - taSize.x * 0.5f, topY + 48));
	ImGui::TextUnformatted(timeAgoBuf);

	// Preview image
	const VDPixmap *px = view->GetImage();
	if (px && px->data) {
		ImTextureID texID = UpdatePreviewTexture(s_rewindIndex, px);

		if (texID) {
			// Compute display size with aspect ratio correction
			float par = view->GetImagePAR();
			if (par <= 0.0f) par = 1.0f;

			float maxW = displaySize.x * 0.6f;
			float maxH = displaySize.y * 0.55f;
			float imgW = (float)px->w * par;
			float imgH = (float)px->h;

			float scale = fminf(maxW / imgW, maxH / imgH);
			float drawW = imgW * scale;
			float drawH = imgH * scale;

			float imgX = centerX - drawW * 0.5f;
			float imgY = topY + 80;

			ImGui::SetCursorPos(ImVec2(imgX, imgY));
			ImGui::Image(texID, ImVec2(drawW, drawH));
		}
	}

	// Navigation hint at bottom
	const char *hint = "Left/Right: navigate    Enter: apply    Escape: cancel";
	ImVec2 hintSize = ImGui::CalcTextSize(hint);
	ImVec2 contentMax = ImGui::GetWindowContentRegionMax();
	ImGui::SetCursorPos(ImVec2(centerX - hintSize.x * 0.5f, contentMax.y - hintSize.y - 8));
	ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
	ImGui::TextUnformatted(hint);
	ImGui::PopStyleColor();

	// Navigation arrows (clickable)
	float arrowY = contentMax.y * 0.5f;
	ImGui::SetCursorPos(ImVec2(20, arrowY));
	if (s_rewindIndex > 0) {
		if (ImGui::ArrowButton("##prev", ImGuiDir_Left))
			--s_rewindIndex;
	}

	ImGui::SetCursorPos(ImVec2(contentMax.x - 40, arrowY));
	if (s_rewindIndex < count - 1) {
		if (ImGui::ArrowButton("##next", ImGuiDir_Right))
			++s_rewindIndex;
	}

	ImGui::PopStyleColor(); // WindowBg
	ImGui::End();
}
