//	AltirraSDL - View menu
//	Split out of ui_menus.cpp (Phase 2c).

#include <stdafx.h>
#include <SDL3/SDL.h>
#include <imgui.h>
#include <vd2/system/vdtypes.h>
#include <vd2/system/VDString.h>

#include "display_backend.h"
#include "ui_main.h"
#include "ui_debugger.h"
#include "ui_textselection.h"
#include "ui_menus_internal.h"
#include "simulator.h"
#include "gtia.h"
#include "constants.h"
#include "uitypes.h"
#include "uiaccessors.h"
#include "uikeyboard.h"
#include "accel_sdl3.h"
#include "options.h"

extern ATSimulator g_sim;
extern SDL_Window *g_pWindow;
void ATUIPasteText();
void ATSetFullscreen(bool fs);

// Use the same ShortcutContextMenu short name as ui_menus.cpp so the
// extracted body compiles unchanged.
static inline void ShortcutContextMenu(const char *command) {
	ATUIMenuShortcutContextMenu(command);
}

// --- body extracted from ui_menus.cpp -------------------------------------
void ATUIRenderViewMenu(ATSimulator &sim, ATUIState &state, SDL_Window *window, IDisplayBackend *backend) {
	bool isFullscreen = (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0;
	if (ImGui::MenuItem("Full Screen", ATUIGetShortcutStringForCommand("View.ToggleFullScreen"), isFullscreen))
		ATSetFullscreen(!isFullscreen);

	ImGui::Separator();

	if (ImGui::BeginMenu("Filter Mode")) {
		ATDisplayFilterMode fm = ATUIGetDisplayFilterMode();
		if (ImGui::MenuItem("Next Mode")) {
			static const ATDisplayFilterMode kModes[] = {
				kATDisplayFilterMode_Point, kATDisplayFilterMode_Bilinear,
				kATDisplayFilterMode_SharpBilinear, kATDisplayFilterMode_Bicubic,
				kATDisplayFilterMode_AnySuitable,
			};
			int cur = 0;
			for (int i = 0; i < 5; ++i)
				if (kModes[i] == fm) { cur = i; break; }
			ATUISetDisplayFilterMode(kModes[(cur + 1) % 5]);
		}
		ImGui::Separator();
		if (ImGui::MenuItem("Point", nullptr, fm == kATDisplayFilterMode_Point))
			ATUISetDisplayFilterMode(kATDisplayFilterMode_Point);
		if (ImGui::MenuItem("Bilinear", nullptr, fm == kATDisplayFilterMode_Bilinear))
			ATUISetDisplayFilterMode(kATDisplayFilterMode_Bilinear);
		if (ImGui::MenuItem("Sharp Bilinear", nullptr, fm == kATDisplayFilterMode_SharpBilinear))
			ATUISetDisplayFilterMode(kATDisplayFilterMode_SharpBilinear);
		if (ImGui::MenuItem("Bicubic", nullptr, fm == kATDisplayFilterMode_Bicubic))
			ATUISetDisplayFilterMode(kATDisplayFilterMode_Bicubic);
		if (ImGui::MenuItem("Default (Any Suitable)", nullptr, fm == kATDisplayFilterMode_AnySuitable))
			ATUISetDisplayFilterMode(kATDisplayFilterMode_AnySuitable);
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("Filter Sharpness")) {
		int sharpness = ATUIGetViewFilterSharpness();
		if (ImGui::MenuItem("Softer", nullptr, sharpness == -2))
			ATUISetViewFilterSharpness(-2);
		if (ImGui::MenuItem("Soft", nullptr, sharpness == -1))
			ATUISetViewFilterSharpness(-1);
		if (ImGui::MenuItem("Normal", nullptr, sharpness == 0))
			ATUISetViewFilterSharpness(0);
		if (ImGui::MenuItem("Sharp", nullptr, sharpness == 1))
			ATUISetViewFilterSharpness(1);
		if (ImGui::MenuItem("Sharper", nullptr, sharpness == 2))
			ATUISetViewFilterSharpness(2);
		ImGui::EndMenu();
	}

	// Video Frame submenu (matches Windows "Video Frame" menu)
	if (ImGui::BeginMenu("Video Frame")) {
		ATDisplayStretchMode sm = ATUIGetDisplayStretchMode();
		if (ImGui::MenuItem("Fit to Window", nullptr, sm == kATDisplayStretchMode_Unconstrained))
			ATUISetDisplayStretchMode(kATDisplayStretchMode_Unconstrained);
		if (ImGui::MenuItem("Preserve Aspect Ratio", nullptr, sm == kATDisplayStretchMode_PreserveAspectRatio))
			ATUISetDisplayStretchMode(kATDisplayStretchMode_PreserveAspectRatio);
		if (ImGui::MenuItem("Preserve Aspect Ratio (fixed multiples only)", nullptr, sm == kATDisplayStretchMode_IntegralPreserveAspectRatio))
			ATUISetDisplayStretchMode(kATDisplayStretchMode_IntegralPreserveAspectRatio);
		if (ImGui::MenuItem("Square Pixels", nullptr, sm == kATDisplayStretchMode_SquarePixels))
			ATUISetDisplayStretchMode(kATDisplayStretchMode_SquarePixels);
		if (ImGui::MenuItem("Square Pixels (fixed multiples only)", nullptr, sm == kATDisplayStretchMode_Integral))
			ATUISetDisplayStretchMode(kATDisplayStretchMode_Integral);

		ImGui::Separator();

		{
			bool pzActive = ATUIIsPanZoomToolActive();
			if (ImGui::MenuItem("Pan/Zoom Tool", nullptr, pzActive))
				ATUISetPanZoomToolActive(!pzActive);
		}
		if (ImGui::MenuItem("Reset Pan and Zoom")) {
			ATUISetDisplayZoom(1.0f);
			ATUISetDisplayPanOffset({0, 0});
		}
		if (ImGui::MenuItem("Reset Panning"))
			ATUISetDisplayPanOffset({0, 0});
		if (ImGui::MenuItem("Reset Zoom"))
			ATUISetDisplayZoom(1.0f);

		ImGui::EndMenu();
	}

	// Overscan Mode submenu (matches Windows exactly, with sub-submenus)
	if (ImGui::BeginMenu("Overscan Mode")) {
		ATGTIAEmulator& gtia = sim.GetGTIA();
		auto om = gtia.GetOverscanMode();
		if (ImGui::MenuItem("OS Screen Only", nullptr, om == ATGTIAEmulator::kOverscanOSScreen))
			gtia.SetOverscanMode(ATGTIAEmulator::kOverscanOSScreen);
		if (ImGui::MenuItem("Normal", nullptr, om == ATGTIAEmulator::kOverscanNormal))
			gtia.SetOverscanMode(ATGTIAEmulator::kOverscanNormal);
		if (ImGui::MenuItem("Widescreen", nullptr, om == ATGTIAEmulator::kOverscanWidescreen))
			gtia.SetOverscanMode(ATGTIAEmulator::kOverscanWidescreen);
		if (ImGui::MenuItem("Extended", nullptr, om == ATGTIAEmulator::kOverscanExtended))
			gtia.SetOverscanMode(ATGTIAEmulator::kOverscanExtended);
		if (ImGui::MenuItem("Full (With Blanking)", nullptr, om == ATGTIAEmulator::kOverscanFull))
			gtia.SetOverscanMode(ATGTIAEmulator::kOverscanFull);

		ImGui::Separator();

		// Vertical Override sub-submenu
		if (ImGui::BeginMenu("Vertical Override")) {
			auto vom = gtia.GetVerticalOverscanMode();
			if (ImGui::MenuItem("Off", nullptr, vom == ATGTIAEmulator::kVerticalOverscan_Default))
				gtia.SetVerticalOverscanMode(ATGTIAEmulator::kVerticalOverscan_Default);
			if (ImGui::MenuItem("OS Screen Only", nullptr, vom == ATGTIAEmulator::kVerticalOverscan_OSScreen))
				gtia.SetVerticalOverscanMode(ATGTIAEmulator::kVerticalOverscan_OSScreen);
			if (ImGui::MenuItem("Normal", nullptr, vom == ATGTIAEmulator::kVerticalOverscan_Normal))
				gtia.SetVerticalOverscanMode(ATGTIAEmulator::kVerticalOverscan_Normal);
			if (ImGui::MenuItem("Extended", nullptr, vom == ATGTIAEmulator::kVerticalOverscan_Extended))
				gtia.SetVerticalOverscanMode(ATGTIAEmulator::kVerticalOverscan_Extended);
			if (ImGui::MenuItem("Full (With Blanking)", nullptr, vom == ATGTIAEmulator::kVerticalOverscan_Full))
				gtia.SetVerticalOverscanMode(ATGTIAEmulator::kVerticalOverscan_Full);
			ImGui::EndMenu();
		}

		bool palExt = gtia.IsOverscanPALExtended();
		if (ImGui::MenuItem("Extended PAL Height", nullptr, palExt))
			gtia.SetOverscanPALExtended(!palExt);

		bool indicatorMargin = ATUIGetDisplayPadIndicators();
		if (ImGui::MenuItem("Indicator Margin", nullptr, indicatorMargin))
			ATUISetDisplayPadIndicators(!indicatorMargin);

		ImGui::EndMenu();
	}

	ImGui::Separator();

	// VSync toggle — works with both GL and SDL_Renderer backends
	{
		bool vsyncOn = false;
		if (backend->GetType() == DisplayBackendType::OpenGL33) {
			int interval = 0;
			SDL_GL_GetSwapInterval(&interval);
			vsyncOn = (interval != 0);
			if (ImGui::MenuItem("Vertical Sync", nullptr, vsyncOn))
				SDL_GL_SetSwapInterval(vsyncOn ? 0 : 1);
		} else {
			int vsync = 0;
			SDL_GetRenderVSync(backend->GetSDLRenderer(), &vsync);
			vsyncOn = (vsync != 0);
			if (ImGui::MenuItem("Vertical Sync", nullptr, vsyncOn))
				SDL_SetRenderVSync(backend->GetSDLRenderer(), vsyncOn ? 0 : 1);
		}
	}

	bool showFPS = ATUIGetShowFPS();
	if (ImGui::MenuItem("Show FPS", nullptr, showFPS))
		ATUISetShowFPS(!showFPS);

	// Video Outputs submenu
	if (ImGui::BeginMenu("Video Outputs")) {
		bool altView = ATUIGetAltViewEnabled();
		if (ImGui::MenuItem("1 Computer Output", nullptr, !altView))
			ATUISetAltViewEnabled(false);

		if (ATUIIsAltOutputAvailable()) {
			if (ImGui::MenuItem("Next Output"))
				ATUISelectNextAltOutput();
		}

		bool autoSwitch = ATUIGetAltViewAutoswitchingEnabled();
		if (ImGui::MenuItem("Auto-Switch Video Output", nullptr, autoSwitch))
			ATUISetAltViewAutoswitchingEnabled(!autoSwitch);

		ImGui::EndMenu();
	}

	ImGui::Separator();

	if (ImGui::MenuItem("Adjust Colors..."))
		state.showAdjustColors = true;

	// Screen Effects submenu — combines built-in effects (Basic) and
	// librashader presets into a unified mode selector.
	if (ImGui::BeginMenu("Screen Effects")) {
		IDisplayBackend *be = ATUIGetDisplayBackend();
		bool shaderAvail = be && be->SupportsExternalShaders();
		bool hasPreset = be && be->HasShaderPreset();

		// Sync mode from actual backend state
		if (hasPreset)
			state.screenEffectsMode = ATUIState::kSFXMode_Preset;

		bool isNone = (state.screenEffectsMode == ATUIState::kSFXMode_None);
		bool isBasic = (state.screenEffectsMode == ATUIState::kSFXMode_Basic);
		bool isPreset = (state.screenEffectsMode == ATUIState::kSFXMode_Preset);

		// (None) — disable all effects
		if (ImGui::MenuItem("(None)", nullptr, isNone)) {
			ATUIShaderPresetsClear(be);
			state.screenEffectsMode = ATUIState::kSFXMode_None;
		}

		// Basic — built-in Altirra effects (scanlines, bloom, distortion, mask)
		if (ImGui::MenuItem("Basic", nullptr, isBasic)) {
			ATUIShaderPresetsClear(be);
			state.screenEffectsMode = ATUIState::kSFXMode_Basic;
		}

		ImGui::Separator();

		// Preset submenu — shader directory tree + Browse
		ATUIRenderShaderPresetMenu(backend);
		if (!shaderAvail && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled))
			ImGui::SetTooltip("librashader not found.\nUse Shader Setup or rebuild with --librashader.");

		ImGui::Separator();

		// Shader Parameters — context-sensitive
		{
			bool canShowParams = !isNone;
			if (ImGui::MenuItem("Shader Parameters...", nullptr, false, canShowParams)) {
				if (isPreset)
					state.showShaderParams = true;
				else
					state.showScreenEffects = true;
			}
			if (!canShowParams && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled))
				ImGui::SetTooltip("Select Basic or a Preset first.");
		}

		// Shader Setup — always accessible
		if (ImGui::MenuItem("Shader Setup..."))
			state.showShaderSetup = true;

		ImGui::EndMenu();
	}

	if (ImGui::MenuItem("Customize HUD..."))
		state.showCustomizeHud = true;
	if (ImGui::MenuItem("Calibrate..."))
		state.showCalibrate = true;

	ImGui::Separator();

	{
		bool dbgOpen = ATUIDebuggerIsOpen();
		if (ImGui::MenuItem("Display", nullptr, false, dbgOpen))
			ATUIDebuggerFocusDisplay();
		if (ImGui::MenuItem("Printer Output", nullptr, false, dbgOpen))
			ATActivateUIPane(kATUIPaneId_PrinterOutput, true, true);
	}

	ImGui::Separator();

	// Copy/Save Frame
	if (ImGui::MenuItem("Copy Frame to Clipboard", ATUIGetShortcutStringForCommand("Edit.CopyFrame")))
		g_copyFrameRequested = true;
	ImGui::MenuItem("Copy Frame to Clipboard (True Aspect)", nullptr, false, false);  // placeholder

	if (ImGui::MenuItem("Save Frame...", ATUIGetShortcutStringForCommand("Edit.SaveFrame")))
		ATUIShowSaveFrameDialog(window);
	ImGui::MenuItem("Save Frame (True Aspect)...", nullptr, false, false);  // placeholder

	// Text Selection submenu
	if (ImGui::BeginMenu("Text Selection")) {
		bool hasSelection = ATUIIsTextSelected();
		if (ImGui::MenuItem("Copy Text", ATUIGetShortcutStringForCommand("Edit.CopyText"), false, hasSelection))
			ATUITextCopy(ATTextCopyMode::ASCII);
		if (ImGui::MenuItem("Copy Escaped Text", nullptr, false, hasSelection))
			ATUITextCopy(ATTextCopyMode::Escaped);
		if (ImGui::MenuItem("Copy Hex", nullptr, false, hasSelection))
			ATUITextCopy(ATTextCopyMode::Hex);
		if (ImGui::MenuItem("Copy Unicode", nullptr, false, hasSelection))
			ATUITextCopy(ATTextCopyMode::Unicode);
		if (ImGui::MenuItem("Paste Text", ATUIGetShortcutStringForCommand("Edit.PasteText")))
			ATUIPasteText();
		ImGui::Separator();
		if (ImGui::MenuItem("Select All", ATUIGetShortcutStringForCommand("Edit.SelectAll")))
			ATUITextSelectAll();
		if (ImGui::MenuItem("Deselect", ATUIGetShortcutStringForCommand("Edit.Deselect"), false, hasSelection))
			ATUITextDeselect();
		ImGui::EndMenu();
	}
}
