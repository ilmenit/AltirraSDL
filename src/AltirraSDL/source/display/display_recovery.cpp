#include <stdafx.h>
#include "display_recovery.h"
#include "display_advisor.h"
#include "display_recovery_policy.h"
#include "logging.h"
#include "ui_confirm_dialog.h"
#include "input_capture.h"
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif
#include <SDL3/SDL.h>
#include <imgui.h>
#include <vd2/system/registry.h>
#include <vd2/system/VDString.h>
#include <string>
#include <cstdlib>
#include <utility>
#ifdef __APPLE__
#include <TargetConditionals.h>
#endif

extern void ATRegistryFlushToDisk();
extern bool g_testModeEnabled;

namespace ATDisplayRecovery {
namespace {
ATDisplayRecoveryPolicy s_policy;
ATDisplayBackendPreference s_requested;
DisplayBackendType s_actual;
bool s_tracking = false, s_armed = false, s_firstTexture = false;
bool s_persistent = false, s_failedSaved = false, s_explicit = false;
bool s_markInterruptions = false, s_uiPresented = false;
bool s_promptQueued = false, s_contextLost = false, s_rescueMode = false;
Uint64 s_started = 0;
std::string s_fingerprint;
std::string s_notice;
constexpr const char *kKey = "Graphics Recovery";
bool s_saved = false, s_savedPending = false;
int s_savedCount = -1;
ATDisplayBackendPreference s_savedBackend;
std::string s_savedFingerprint;

void Save(bool pending) {
	if (!s_persistent) return;
	pending = pending && s_markInterruptions;
	if (s_saved && s_savedPending == pending
		&& s_savedCount == s_policy.interruptions
		&& s_savedBackend == s_requested && s_savedFingerprint == s_fingerprint)
		return;
	VDRegistryAppKey key(kKey, true);
	key.setString("Fingerprint", s_fingerprint.c_str());
	key.setInt("Backend", (int)s_requested);
	key.setInt("Interruptions", s_policy.interruptions);
	key.setBool("Pending", pending);
	ATRegistryFlushToDisk();
	s_saved = true; s_savedPending = pending;
	s_savedCount = s_policy.interruptions; s_savedBackend = s_requested;
	s_savedFingerprint = s_fingerprint;
}

void Describe() {
	if (s_policy.failed)
		s_notice = "Graphics presentation failed repeatedly. You can select a "
			"different renderer for the next launch.";
	else if (s_notice.empty())
		s_notice = "Graphics startup took more than five seconds. A cold shader "
			"cache can cause this. Keeping the current renderer is safe to try.";
}
}

void Begin(ATDisplayBackendPreference& preference, bool explicitOverride, bool gaming) {
	s_requested = preference;
	s_explicit = explicitOverride;
#if defined(__EMSCRIPTEN__)
	// WebGL has no native SDL_GPU alternative; never poison native choices.
	s_tracking = true;
	return;
#else
	s_tracking = true;
	// Mobile OS termination is normal. Do not infer failures from pending
	// markers there. Known errors and slow first frames still get UI help.
#if !defined(__ANDROID__) && !(defined(__APPLE__) && defined(TARGET_OS_IOS) && TARGET_OS_IOS)
	s_markInterruptions = !explicitOverride;
#endif
	s_persistent = !explicitOverride;
	// Version the shader policy, and invalidate records when SDL/video
	// platform changes. A pending marker is evidence of interruption only.
	// v2 excludes window-system waits; old pending records included them.
	s_fingerprint = std::string(AT_GRAPHICS_RECOVERY_BUILD "/renderer-v2/") + SDL_GetRevision()
		+ "/" + (SDL_GetCurrentVideoDriver() ? SDL_GetCurrentVideoDriver() : "");
	if (s_persistent) {
		VDRegistryAppKey key(kKey, false);
		VDStringA fingerprint;
		if (key.getString("Fingerprint", fingerprint)
			&& s_fingerprint == fingerprint.c_str()
			&& key.getInt("Backend", -1) == (int)preference)
			s_policy.Load(key.getInt("Interruptions", 0), s_markInterruptions && key.getBool("Pending", false));
		if (s_policy.RecommendRecovery() && gaming) {
			// A native pre-start message box cannot be navigated by gamepad.
			// Bootstrap this session on the other API, then ask in the normal
			// touch/controller UI. Do not overwrite the user's preference.
			preference = preference == ATDisplayBackendPreference::OpenGL
				|| s_policy.interruptions >= 3
				? ATDisplayBackendPreference::SDLRenderer
				: ATDisplayBackendPreference::OpenGL;
			s_rescueMode = true;
			s_policy.notice = true;
			s_notice = "Graphics startup failed or was interrupted repeatedly. "
				"An alternative renderer is being used for this session so you "
				"can choose how to continue. Your saved preference is unchanged.";
		} else if (s_policy.RecommendRecovery()) {
			// Ask before graphics creation: this dialog does not depend on
			// the suspect renderer, unlike an ImGui prompt during a stall.
			const SDL_MessageBoxButtonData buttons[] = {
				{SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT | SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT,
					0, "Try current again"},
				{0, 1, "Use OpenGL"}, {0, 2, "Use compatibility renderer"}
			};
			SDL_MessageBoxData box {};
			box.flags = SDL_MESSAGEBOX_WARNING;
			box.title = "AltirraSDL graphics recovery";
			box.message = "Two graphics startup attempts were interrupted or failed. "
				"This can also happen after forced termination. Try again, or choose "
				"another renderer. OpenGL does not support HDR; compatibility graphics "
				"also omit screen effects. Your effect settings will be preserved.";
			box.numbuttons = 3; box.buttons = buttons;
			int choice = 0;
			// Test automation uses the in-app prompt rather than a native box.
			if (!g_testModeEnabled && SDL_ShowMessageBox(&box, &choice)) {
				if (choice == 1 || choice == 2) {
					preference = choice == 1 ? ATDisplayBackendPreference::OpenGL
						: ATDisplayBackendPreference::SDLRenderer;
					ATDisplayBackendPreferenceSave(preference);
					s_requested = preference;
					s_policy.interruptions = 0;
				}
			} else {
				s_policy.notice = true;
				s_notice = "Graphics startup was interrupted repeatedly. "
					"You can choose another renderer for the next launch.";
			}
		}
	}
	LOG_INFO("GraphicsRecovery", "Startup renderer=%s interruptions=%d override=%d",
		ATDisplayBackendPreferenceName(preference), s_policy.interruptions, explicitOverride);
	// Window mapping can wait for the desktop/window manager, independently
	// of graphics compilation. Arm only once a window is available.
	Save(false);
	s_started = SDL_GetTicksNS();
#endif
}

void BeforeWindow() {
	Save(false);
	s_started = SDL_GetTicksNS();
	LOG_INFO("GraphicsRecovery", "Creating window (SDL %s)",
		SDL_GetCurrentVideoDriver() ? SDL_GetCurrentVideoDriver() : "unknown");
	if (g_testModeEnabled) {
		const char *fault = SDL_getenv("ALTIRRA_TEST_GRAPHICS_WINDOW");
		if (fault && !SDL_strcmp(fault, "interrupt")) std::_Exit(87);
		if (fault && !SDL_strcmp(fault, "slow")) SDL_Delay(5100);
	}
}

void AfterWindow(bool created) {
	LOG_INFO("GraphicsRecovery", "Window %s in %.1f ms%s%s",
		created ? "ready" : "failed", (SDL_GetTicksNS() - s_started) / 1e6,
		created ? "" : ": ", created ? "" : SDL_GetError());
	// Exclude display-server waits from shader/renderer startup diagnosis.
	if (!created) return;
	Save(true);
	// Registry I/O is not renderer work either (notably on slow storage).
	s_started = SDL_GetTicksNS();
	// Fault injection is available only in explicit UI automation mode.
	if (g_testModeEnabled) {
		const char *fault = SDL_getenv("ALTIRRA_TEST_GRAPHICS_STARTUP");
		if (fault && !SDL_strcmp(fault, "interrupt")) std::_Exit(86);
		if (fault && !SDL_strcmp(fault, "slow")) SDL_Delay(5100);
	}
}

#ifdef __EMSCRIPTEN__
extern "C" EMSCRIPTEN_KEEPALIVE void ATGraphicsContextLost() {
	s_contextLost = true;
}
#endif

void Created(DisplayBackendType actual) {
#ifdef __EMSCRIPTEN__
	s_actual = actual;
	EM_ASM({
		var canvas = Module['canvas'];
		if (canvas) canvas.addEventListener('webglcontextlost', function() {
			Module['_ATGraphicsContextLost']();
			var banner = document.createElement('div');
			banner.setAttribute('role', 'alert');
			banner.style.cssText = 'position:fixed;inset:20px;z-index:2147483647;background:#222;color:white;padding:24px;font:20px sans-serif';
			banner.textContent = 'Graphics context was lost. Reload to restore graphics. Saved settings and files will be kept. Unsaved progress may be lost. ';
			var button = document.createElement('button');
			button.textContent = 'Reload';
			button.onclick = function() { window.location.reload(); };
			banner.appendChild(button); document.body.appendChild(banner);
		});
	});
	return;
#endif
	if (!s_tracking) return;
	s_actual = actual;
	const double ms = (SDL_GetTicksNS() - s_started) / 1e6;
	LOG_INFO("GraphicsRecovery", "Renderer ready: %s in %.1f ms",
		ATDisplayBackendTypeName(actual), ms);
	s_policy.Observe(ms, true);
	const bool fallback = (s_requested == ATDisplayBackendPreference::SDLGPU && actual != DisplayBackendType::SDLGPU)
		|| (s_requested == ATDisplayBackendPreference::OpenGL && actual != DisplayBackendType::OpenGL);
	if (fallback && !s_rescueMode) {
		s_policy.notice = true;
		s_notice = std::string("The requested graphics renderer was unavailable. Using ")
			+ ATDisplayBackendTypeName(actual) + ". Your renderer preference and effect settings are preserved.";
	}
	// Do not mark firmware, audio, file pickers or profile loading as GPU work.
	Save(false);
}

void BeforeFrame(bool hasTexture) {
	if (!s_tracking || s_policy.settled || s_armed
		|| (!hasTexture && s_uiPresented)) return;
	s_firstTexture = hasTexture;
	s_armed = true;
	Save(true);
	s_started = SDL_GetTicksNS();
}

void AfterFrame(bool healthy) {
	if (!s_tracking) return;
	const double ms = s_armed ? (SDL_GetTicksNS() - s_started) / 1e6 : 0;
	if (s_armed || !healthy)
		LOG_INFO("GraphicsRecovery", "Presentation: %.1f ms texture=%d success=%d %s",
			ms, s_firstTexture, healthy, healthy ? "" : SDL_GetError());
	s_policy.Observe(ms, healthy);
	if (s_armed && healthy) {
		s_uiPresented = true;
		if (s_firstTexture) {
			s_policy.settled = true;
			if (!s_rescueMode) s_policy.interruptions = 0;
		}
		Save(false);
	}
	s_armed = false;
	if (s_policy.failed && !s_failedSaved) {
		s_failedSaved = true;
		s_policy.interruptions = 2;
		Save(false);
	}
	if (s_policy.notice) Describe();
}

const char *ActualRenderer() { return ATDisplayBackendTypeName(s_actual); }

const char *Notice() { return s_policy.notice ? s_notice.c_str() : ""; }

void InjectTest(const char *event) {
	if (!g_testModeEnabled) return;
	if (!SDL_strcmp(event, "slow")) {
		const bool settled = s_policy.settled;
		s_policy.settled = false;
		s_policy.Observe(5000, true);
		s_policy.settled = settled;
		s_notice.clear(); Describe();
	}
	if (!SDL_strcmp(event, "failed")) {
		s_policy.Observe(0, false); s_policy.Observe(0, false); s_policy.Observe(0, false);
		s_policy.interruptions = 2; Save(false); Describe();
	}
}

bool Active() {
	return s_promptQueued || (s_policy.notice && !ATInputCapture::IsActive());
}
bool ContextLost() { return s_contextLost; }
void Suspend() {
	ATDisplayAdvisor::Cancel();
	s_armed = false;
	s_policy.errors = 0;
	// Resume is not startup; shader recompilation after surface loss must
	// not be recorded as a failed launch or a slow initial presentation.
	s_policy.settled = true;
	Save(false);
}

void RenderUI(bool gaming) {
	if (!s_policy.notice || s_promptQueued || ATInputCapture::IsActive()) return;
	Describe();
	ATDisplayAdvisor::Cancel();
	s_promptQueued = true;
	// Explicit fork extension; use the established modal/nav/release path.
	ATUIConfirmOptions options;
	options.title = "Graphics recovery";
	options.stackedButtons = true;
	options.buttonHeight = gaming ? std::max(48.0f, ImGui::GetFrameHeight() * 1.5f) : 0;
	std::string message = s_notice;
#ifdef __EMSCRIPTEN__
	message += " Reload the page if graphics stop working. Saved settings and files are kept, but unsaved progress may be lost.";
	options.destructive = true;
	options.confirmLabel = "Reload page";
	options.cancelLabel = "Keep current graphics";
	options.onConfirm = [] { s_promptQueued = false; emscripten_run_script("window.location.reload()"); };
#else
	message += std::string(" Active: ") + ATDisplayBackendTypeName(s_actual)
		+ ". Changes apply after closing and reopening AltirraSDL. "
		"The current session continues. OpenGL supports screen effects but not "
		"HDR. Compatibility graphics omit HDR and screen effects. Settings are kept.";
	if (s_explicit)
		message += " A launch override is active: remove --renderer or ALTIRRA_DISPLAY_BACKEND to use a saved selection.";
	options.destructive = true; // Enter/Escape keep the current renderer.
	options.confirmLabel = "Use OpenGL next launch";
	options.cancelLabel = s_rescueMode
		? "Try preferred renderer next launch" : "Keep current renderer";
	options.alternateLabel = "Use compatibility graphics next launch";
	options.onConfirm = [] {
		s_promptQueued = false;
		s_policy.interruptions = 0; Save(s_armed);
		ATDisplayBackendPreferenceSave(ATDisplayBackendPreference::OpenGL);
	};
	options.onAlternate = [] {
		s_promptQueued = false;
		s_policy.interruptions = 0; Save(s_armed);
		ATDisplayBackendPreferenceSave(ATDisplayBackendPreference::SDLRenderer);
	};
#endif
	options.onCancel = [] {
		s_promptQueued = false;
		s_policy.interruptions = 0;
		Save(s_armed);
	};
	options.message = message.c_str();
	// The confirmation queue copies all text and owns callback lifetimes.
	ATUIShowConfirm(std::move(options));
	s_policy.notice = false;
}
}
