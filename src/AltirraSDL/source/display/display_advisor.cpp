#include <stdafx.h>
#include "display_advisor.h"
#include "display_advisor_policy.h"
#include "display_backend.h"
#include "display_recovery.h"
#include "ui_main.h"
#include "ui_mobile.h"
#include "ui_confirm_dialog.h"
#include "uiaccessors.h"
#include "input_capture.h"
#include "logging.h"
#include "settings.h"
#include <SDL3/SDL.h>
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <utility>

extern ATUIState g_uiState;
extern ATMobileUIState g_mobileState;
extern bool g_testModeEnabled;
extern void ATRegistryFlushToDisk();
namespace ATDisplayAdvisor {
namespace {
ATDisplayAdvisorPolicy s_policy;
Diagnostics s_diagnostics {};
enum class Trial { None, Effects, Filter };
Trial s_trial = Trial::None;
bool s_pending = false, s_queued = false;
bool s_canEffects = false, s_canFilter = false;
uint32 s_profile = 0;
bool s_temporaryProfile = false;
ATUIState::ScreenEffectsMode s_effects;
ATDisplayFilterMode s_filter;
Uint64 s_expires = 0, s_refreshAt = 0;
double s_waitAllowance = -1;
constexpr unsigned kConfirmGroup = 3;

bool SameSettings() {
	return s_profile == ATSettingsGetCurrentProfileId()
		&& s_temporaryProfile == ATSettingsGetTemporaryProfileMode()
		&& s_effects == g_uiState.screenEffectsMode
		&& s_filter == ATUIGetDisplayFilterMode();
}
void Snapshot() {
	s_profile = ATSettingsGetCurrentProfileId();
	s_temporaryProfile = ATSettingsGetTemporaryProfileMode();
	s_effects = g_uiState.screenEffectsMode;
	s_filter = ATUIGetDisplayFilterMode();
}
void StartTrial(Trial trial) {
	s_queued = false;
	if (!SameSettings()) return;
	s_trial = trial;
	s_expires = SDL_GetTicksNS() + 15000000000ULL;
}
void Keep() {
	if (!SameSettings()) { Cancel(); return; }
	if (s_trial == Trial::Effects)
		g_uiState.screenEffectsMode = ATUIState::kSFXMode_None;
	if (s_trial == Trial::Filter)
		ATUISetDisplayFilterMode(kATDisplayFilterMode_Bilinear);
	if (!ATSettingsGetTemporaryProfileMode()) {
		ATMobileUI_MarkGraphicsCustom(g_mobileState);
		// Preserve the live settings previously supplied by the Gaming
		// preset when marking it Custom. Match the existing mobile-edit
		// save mask: fullscreen must not be reasserted, and graphics edits
		// do not change mounted media. Trial overrides never reach getters.
		try {
			ATSaveSettings((ATSettingsCategory)(kATSettingsCategory_AllCategories
				& ~kATSettingsCategory_FullScreen & ~kATSettingsCategory_MountedImages));
			ATRegistryFlushToDisk();
		} catch (...) {
			// The normal exit/suspend path will retry persistence.
		}
	}
	s_trial = Trial::None;
	// Existing profile save/suspend lifecycle persists accepted choices.
	// Trial overrides are never exposed to settings getters or serializers.
}
}

bool NeedsObservation() {
	if (s_policy.offered) return false;
	if (g_uiState.screenEffectsMode == ATUIState::kSFXMode_None
		&& ATUIGetDisplayFilterMode() != kATDisplayFilterMode_Bicubic) {
		s_policy.ResetWindow(); return false;
	}
	return true;
}
bool TrialActive() { return s_trial != Trial::None; }
bool Active() { return s_queued || TrialActive()
	|| (s_pending && !ATInputCapture::IsActive()); }
bool EffectiveEffects(bool stored) {
	return stored && s_trial != Trial::Effects;
}
ATDisplayFilterMode EffectiveFilter(ATDisplayFilterMode stored) {
	return s_trial == Trial::Filter ? kATDisplayFilterMode_Bilinear : stored;
}
void Cancel() {
	s_trial = Trial::None; s_pending = false; s_queued = false;
	ATUICancelConfirmations(kConfirmGroup);
	s_policy.ResetWindow();
	s_refreshAt = 0;
}
void Update() {
	if ((Active() && !SameSettings())
		|| (TrialActive() && (SDL_GetTicksNS() >= s_expires
			|| ATInputCapture::IsActive() || ATUIIsMenuOpen()
			|| ATUIIsConfirmDialogActive()))) Cancel();
}
Diagnostics GetDiagnostics() {
	s_diagnostics.warmup = s_policy.warmup;
	s_diagnostics.slow = s_policy.slow;
	return s_diagnostics;
}
void Observe(SDL_Window *window, IDisplayBackend *backend,
	double workMs, double presentMs, double budgetMs, bool eligible)
{
	if (g_testModeEnabled) {
		s_diagnostics.work = workMs; s_diagnostics.present = presentMs;
		s_diagnostics.allowance = s_waitAllowance; s_diagnostics.eligible = eligible;
	}
	if (s_policy.offered || Active()) return;
	const bool effects = backend->SupportsScreenFX()
		&& g_uiState.screenEffectsMode != ATUIState::kSFXMode_None;
	const bool filter = backend->SupportsScreenFX()
		&& ATUIGetDisplayFilterMode() == kATDisplayFilterMode_Bicubic;
	if (eligible && (effects || filter)) {
#ifndef __EMSCRIPTEN__
		const Uint64 now = SDL_GetTicksNS();
		// Cache display/vsync queries; no per-frame allocations or GPU queries.
		if (now >= s_refreshAt) {
			s_refreshAt = now + 1000000000ULL;
			int interval = 0;
			const bool known = backend->GetSDLRenderer()
				? SDL_GetRenderVSync(backend->GetSDLRenderer(), &interval)
				: SDL_GL_GetSwapInterval(&interval);
			const auto *mode = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(window));
			s_waitAllowance = known && mode && mode->refresh_rate > 0
				? 1000.0 * std::max(1, std::abs(interval)) / mode->refresh_rate : -1;
		}
#else
		// rAF/compositor waiting is outside SwapWindow on browsers. Never
		// infer GPU load from browser frame intervals or present duration.
		(void)window;
		s_waitAllowance = -1;
#endif
	}
	if (s_policy.Observe(workMs, presentMs, budgetMs, s_waitAllowance,
		eligible, effects || filter)) {
		LOG_INFO("GraphicsAdvisor", "Sustained rendering cost: work=%.2f present=%.2f budget=%.2f wait=%.2f ms",
			workMs, presentMs, budgetMs, s_waitAllowance);
		Snapshot(); s_canEffects = effects; s_canFilter = filter;
		s_pending = true;
	}
}

void RenderUI(bool gaming) {
	Update();
	if (s_pending && !s_queued && !ATInputCapture::IsActive()
		&& !ATDisplayRecovery::Active() && !ATUIIsConfirmDialogActive()) {
		s_pending = false; s_queued = true;
		ATUIConfirmOptions options;
		options.title = "Smoother graphics";
		options.message = "Graphics are taking longer to draw than expected. "
			"Try simpler graphics for 15 seconds. Your current settings and "
			"CRT adjustments stay unchanged unless you choose Keep.";
		options.stackedButtons = true;
		options.buttonHeight = gaming ? std::max(48.0f, ImGui::GetFrameHeight() * 1.5f) : 0;
		options.destructive = true;
		options.group = kConfirmGroup;
		options.cancelLabel = "Not now";
		options.onCancel = [] { s_queued = false; };
		if (s_canEffects) {
			options.confirmLabel = "Try screen effects off";
			options.onConfirm = [] { StartTrial(Trial::Effects); };
		} else {
			options.confirmLabel = "Try bilinear filtering";
			options.onConfirm = [] { StartTrial(Trial::Filter); };
		}
		if (s_canEffects && s_canFilter) {
			options.alternateLabel = "Try bilinear filtering";
			options.onAlternate = [] { StartTrial(Trial::Filter); };
		}
		ATUIShowConfirm(std::move(options));
	}
	if (!TrialActive()) return;
	// Nonpersistent preview controls: centered on open, no saved position.
	// Gameplay keeps rendering so the user can judge the difference.
	const auto *viewport = ImGui::GetMainViewport();
	const float width = std::max(1.0f, std::min(440.0f, viewport->WorkSize.x - 16));
	static ImVec2 previousWorkSize {};
	const bool resized = previousWorkSize.x != viewport->WorkSize.x
		|| previousWorkSize.y != viewport->WorkSize.y;
	previousWorkSize = viewport->WorkSize;
	ImGui::SetNextWindowPos(viewport->GetCenter(),
		resized ? ImGuiCond_Always : ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSize(ImVec2(width, 0), ImGuiCond_Appearing);
	ImGui::SetNextWindowSizeConstraints(ImVec2(width, 0),
		ImVec2(width, std::max(1.0f, viewport->WorkSize.y - 16)));
	bool open = true;
	if (ImGui::Begin("Graphics trial", &open,
		ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize
		| ImGuiWindowFlags_NoCollapse)) {
		const Uint64 now = SDL_GetTicksNS();
		ImGui::TextWrapped("Trying %s. Restores in %d seconds.",
			s_trial == Trial::Effects ? "screen effects off" : "bilinear filtering",
			(int)((s_expires > now ? s_expires - now : 0) / 1000000000ULL) + 1);
		ImGui::TextWrapped("%s, or restore your previous graphics.",
			s_temporaryProfile ? "Keep for this session" : "Keep for this profile");
		static bool keepSelected = false;
		if (ImGui::IsWindowAppearing()) keepSelected = false;
		if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, false)
			|| ImGui::IsKeyPressed(ImGuiKey_DownArrow, false)
			|| ImGui::IsKeyPressed(ImGuiKey_Tab, false)
			|| ImGui::IsKeyPressed(ImGuiKey_GamepadDpadUp, false)
			|| ImGui::IsKeyPressed(ImGuiKey_GamepadDpadDown, false))
			keepSelected = !keepSelected;
		const bool activate = ImGui::IsKeyPressed(ImGuiKey_Enter, false)
			|| ImGui::IsKeyPressed(ImGuiKey_Space, false)
			|| ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown, false);
		bool keep = activate && keepSelected;
		bool restore = activate && !keepSelected;
		const ImVec2 size(-1, gaming ? std::max(48.0f, ImGui::GetFrameHeight() * 1.5f) : 0);
		ImGui::PushItemFlag(ImGuiItemFlags_NoNav, true);
		auto button = [&](const char *label, bool selected) {
			if (selected) {
				ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.0f);
				ImGui::PushStyleColor(ImGuiCol_Border,
					ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
			}
			const bool pressed = ImGui::Button(label, size);
			if (selected) { ImGui::PopStyleColor(); ImGui::PopStyleVar(); }
			return pressed;
		};
		keep |= button("Keep simpler graphics", keepSelected);
		restore |= button("Restore previous graphics", !keepSelected);
		ImGui::PopItemFlag();
		restore |= ImGui::IsKeyPressed(ImGuiKey_Escape, false)
			|| ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false);
		if (restore) Cancel();
		else if (keep) Keep();
	}
	ImGui::End();
	if (!open) Cancel();
}
void InjectTest(const char *scenario) {
	if (!g_testModeEnabled) return;
	const bool vsync = !SDL_strcmp(scenario, "vsync");
	const bool cpu = !SDL_strcmp(scenario, "cpu");
	const bool paused = !SDL_strcmp(scenario, "paused");
	for (int i = 0; i < 240; ++i) {
		if (s_policy.Observe(cpu || vsync ? 1 : 50, vsync ? 33 : 1,
			16.7, 33.4, !paused, true)) {
			Snapshot(); s_canEffects = g_uiState.screenEffectsMode != ATUIState::kSFXMode_None;
			s_canFilter = ATUIGetDisplayFilterMode() == kATDisplayFilterMode_Bicubic;
			s_pending = s_canEffects || s_canFilter;
		}
	}
}
}
