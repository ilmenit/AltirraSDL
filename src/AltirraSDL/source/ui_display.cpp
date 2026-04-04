//	AltirraSDL - Display Settings dialog
//	Filter mode, stretch mode, overscan, FPS, indicators.
//	Adjust Colors dialog with palette preview, menu bar, palette solver.

#include <stdafx.h>
#include <SDL3/SDL.h>
#include <imgui.h>
#include <mutex>
#include <vd2/system/vdtypes.h>
#include <vd2/system/file.h>
#include <vd2/system/text.h>
#include <at/atcore/snapshotimpl.h>

#include "ui_main.h"
#include "simulator.h"
#include "gtia.h"
#include "uiaccessors.h"
#include "uitypes.h"
#include "savestateio.h"

extern ATSimulator g_sim;
extern SDL_Window *g_pWindow;

// =========================================================================
// Color settings serialization (duplicated from Windows uicolors.cpp since
// the classes are defined there and that file is excluded from the SDL3 build)
// =========================================================================

class ATSaveStateColorParameters final : public ATSnapExchangeObject<ATSaveStateColorParameters, "ATSaveStateColorParameters"> {
public:
	ATSaveStateColorParameters() {
		mParams.mPresetTag = ATGetColorPresetTagByIndex(0);
		static_cast<ATColorParams&>(mParams) = ATGetColorPresetByIndex(0);
	}
	ATSaveStateColorParameters(const ATNamedColorParams& params) : mParams(params) {}

	template<typename T>
	void Exchange(T& rw) {
		rw.Transfer("profile_name", &mParams.mPresetTag);
		rw.Transfer("hue_start", &mParams.mHueStart);
		rw.Transfer("hue_range", &mParams.mHueRange);
		rw.Transfer("brightness", &mParams.mBrightness);
		rw.Transfer("contrast", &mParams.mContrast);
		rw.Transfer("saturation", &mParams.mSaturation);
		rw.Transfer("gamma", &mParams.mGammaCorrect);
		rw.Transfer("intensity_scale", &mParams.mIntensityScale);
		rw.Transfer("artifacting_hue", &mParams.mArtifactHue);
		rw.Transfer("artifacting_saturation", &mParams.mArtifactSat);
		rw.Transfer("artifacting_sharpness", &mParams.mArtifactSharpness);
		rw.Transfer("matrix_red_shift", &mParams.mRedShift);
		rw.Transfer("matrix_red_scale", &mParams.mRedScale);
		rw.Transfer("matrix_green_shift", &mParams.mGrnShift);
		rw.Transfer("matrix_green_scale", &mParams.mGrnScale);
		rw.Transfer("matrix_blue_shift", &mParams.mBluShift);
		rw.Transfer("matrix_blue_scale", &mParams.mBluScale);
		rw.Transfer("use_pal_quirks", &mParams.mbUsePALQuirks);
		rw.TransferEnum("luma_ramp", &mParams.mLumaRampMode);
		rw.TransferEnum("color_correction", &mParams.mColorMatchingMode);
	}

	ATNamedColorParams mParams;
};

class ATSaveStateColorSettings final : public ATSnapExchangeObject<ATSaveStateColorSettings, "ATSaveStateColorSettings"> {
public:
	ATSaveStateColorSettings() = default;
	ATSaveStateColorSettings(const ATColorSettings& settings) {
		mpNTSCParams = new ATSaveStateColorParameters(settings.mNTSCParams);
		if (settings.mbUsePALParams)
			mpPALParams = new ATSaveStateColorParameters(settings.mPALParams);
	}

	template<typename T>
	void Exchange(T& rw) {
		rw.Transfer("ntsc_params", &mpNTSCParams);
		rw.Transfer("pal_params", &mpPALParams);
	}

	vdrefptr<ATSaveStateColorParameters> mpNTSCParams;
	vdrefptr<ATSaveStateColorParameters> mpPALParams;
};

static const ATDisplayFilterMode kFilterValues[] = {
	kATDisplayFilterMode_Point, kATDisplayFilterMode_Bilinear,
	kATDisplayFilterMode_SharpBilinear, kATDisplayFilterMode_Bicubic,
	kATDisplayFilterMode_AnySuitable,
};
static const char *kFilterLabels[] = {
	"Point (Nearest)", "Bilinear", "Sharp Bilinear",
	"Bicubic", "Default (Any Suitable)",
};

static const ATDisplayStretchMode kStretchValues[] = {
	kATDisplayStretchMode_Unconstrained,
	kATDisplayStretchMode_PreserveAspectRatio,
	kATDisplayStretchMode_SquarePixels,
	kATDisplayStretchMode_Integral,
	kATDisplayStretchMode_IntegralPreserveAspectRatio,
};
static const char *kStretchLabels[] = {
	"Fit to Window", "Preserve Aspect Ratio", "Square Pixels",
	"Integer Scale", "Integer + Aspect Ratio",
};

void ATUIRenderDisplaySettings(ATSimulator &sim, ATUIState &state) {
	ImGui::SetNextWindowSize(ImVec2(400, 340), ImGuiCond_Appearing);
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	if (!ImGui::Begin("Display Settings", &state.showDisplaySettings, ImGuiWindowFlags_NoSavedSettings)) {
		ImGui::End();
		return;
	}

	if (ATUICheckEscClose()) {
		state.showDisplaySettings = false;
		ImGui::End();
		return;
	}

	// Filter mode
	ATDisplayFilterMode curFM = ATUIGetDisplayFilterMode();
	int fmIdx = 0;
	for (int i = 0; i < 5; ++i)
		if (kFilterValues[i] == curFM) { fmIdx = i; break; }
	if (ImGui::Combo("Filter Mode", &fmIdx, kFilterLabels, 5))
		ATUISetDisplayFilterMode(kFilterValues[fmIdx]);

	// Stretch mode
	ATDisplayStretchMode curSM = ATUIGetDisplayStretchMode();
	int smIdx = 0;
	for (int i = 0; i < 5; ++i)
		if (kStretchValues[i] == curSM) { smIdx = i; break; }
	if (ImGui::Combo("Stretch Mode", &smIdx, kStretchLabels, 5))
		ATUISetDisplayStretchMode(kStretchValues[smIdx]);

	// Overscan
	ATGTIAEmulator& gtia = sim.GetGTIA();
	static const char *kOverscanLabels[] = {
		"Normal", "Extended", "Full", "OS Screen Only", "Widescreen"
	};
	// Enum order: Normal=0, Extended=1, Full=2, OSScreen=3, Widescreen=4
	int osIdx = (int)gtia.GetOverscanMode();
	if (osIdx < 0 || osIdx >= 5) osIdx = 0;
	if (ImGui::Combo("Overscan Mode", &osIdx, kOverscanLabels, 5))
		gtia.SetOverscanMode((ATGTIAEmulator::OverscanMode)osIdx);

	ImGui::Separator();

	bool showFPS = ATUIGetShowFPS();
	if (ImGui::Checkbox("Show FPS", &showFPS))
		ATUISetShowFPS(showFPS);

	bool indicators = ATUIGetDisplayIndicators();
	if (ImGui::Checkbox("Show Indicators", &indicators))
		ATUISetDisplayIndicators(indicators);

	bool pointerHide = ATUIGetPointerAutoHide();
	if (ImGui::Checkbox("Auto-Hide Mouse Pointer", &pointerHide))
		ATUISetPointerAutoHide(pointerHide);

	ImGui::End();
}

// =========================================================================
// Adjust Colors dialog (matches Windows IDD_ADJUST_COLORS)
// =========================================================================

// Helper: labeled slider with percent-style mapping
static bool SliderPercent(const char *label, float *v, float vmin, float vmax, const char *fmt = "%.0f%%") {
	float pct = *v * 100.0f;
	float pmin = vmin * 100.0f;
	float pmax = vmax * 100.0f;
	if (ImGui::SliderFloat(label, &pct, pmin, pmax, fmt)) {
		*v = pct / 100.0f;
		return true;
	}
	return false;
}

static bool SliderDegrees(const char *label, float *v, float vmin, float vmax) {
	return ImGui::SliderFloat(label, v, vmin, vmax, "%.0f\xc2\xb0");
}

// Persistent state for Adjust Colors dialog (mirrors Windows ATAdjustColorsDialog members)
static bool s_adjustColors_showRelativeOffsets = false;
static bool s_adjustColors_showPaletteSolver = false;

// Pending color settings from Load callback (thread-safe handoff)
static std::mutex s_pendingColorsMutex;
static bool s_pendingColorsLoaded = false;
static ATColorSettings s_pendingColorSettings;


// Export palette callback for SDL file dialog
static void AdjustColorsExportPaletteCallback(void *, const char * const *filelist, int) {
	if (!filelist || !filelist[0])
		return;

	ATGTIAEmulator& gtia = g_sim.GetGTIA();
	uint32 pal[256];
	gtia.GetPalette(pal);

	uint8 pal8[768];
	for (int i = 0; i < 256; ++i) {
		const uint32 c = pal[i];
		pal8[i * 3 + 0] = (uint8)(c >> 16);
		pal8[i * 3 + 1] = (uint8)(c >> 8);
		pal8[i * 3 + 2] = (uint8)(c >> 0);
	}

	try {
		VDFile f(VDTextU8ToW(VDStringA(filelist[0])).c_str(),
			nsVDFile::kWrite | nsVDFile::kDenyAll | nsVDFile::kCreateAlways);
		f.write(pal8, sizeof pal8);
	} catch (...) {
	}
}

// Save color settings callback for SDL file dialog
static void AdjustColorsSaveCallback(void *, const char * const *filelist, int) {
	if (!filelist || !filelist[0])
		return;

	try {
		ATGTIAEmulator& gtia = g_sim.GetGTIA();
		ATColorSettings cs = gtia.GetColorSettings();
		vdrefptr<ATSaveStateColorSettings> sscs(new ATSaveStateColorSettings(cs));

		vdautoptr<IATSaveStateSerializer> ser(ATCreateSaveStateSerializer());
		VDStringW wpath = VDTextU8ToW(VDStringA(filelist[0]));
		VDFileStream fs(wpath.c_str(), nsVDFile::kWrite | nsVDFile::kCreateAlways | nsVDFile::kSequential);
		VDBufferedWriteStream bs(&fs, 4096);
		ser->Serialize(bs, *sscs, L"ATColorSettings");
		bs.Flush();
		fs.close();
	} catch (...) {
	}
}

// Load color settings callback for SDL file dialog.
// May be called from a non-main thread, so we stage the result and apply it
// on the main thread during the next render frame.
static void AdjustColorsLoadCallback(void *, const char * const *filelist, int) {
	if (!filelist || !filelist[0])
		return;

	try {
		VDStringW wpath = VDTextU8ToW(VDStringA(filelist[0]));
		vdrefptr<IATSerializable> rawData;

		{
			vdautoptr<IATSaveStateDeserializer> deser{ATCreateSaveStateDeserializer()};
			VDFileStream fs(wpath.c_str());
			deser->Deserialize(fs, ~rawData);
		}

		ATSaveStateColorSettings *cs = atser_cast<ATSaveStateColorSettings *>(rawData);
		if (!cs || !cs->mpNTSCParams)
			return;

		ATColorSettings settings;
		settings.mNTSCParams = cs->mpNTSCParams->mParams;

		if (cs->mpPALParams) {
			settings.mPALParams = cs->mpPALParams->mParams;
			settings.mbUsePALParams = true;
		} else {
			settings.mPALParams = settings.mNTSCParams;
			settings.mbUsePALParams = false;
		}

		std::lock_guard<std::mutex> lock(s_pendingColorsMutex);
		s_pendingColorSettings = settings;
		s_pendingColorsLoaded = true;
	} catch (...) {
	}
}

// Draw the 16x16 palette preview (256 colors) plus NTSC artifact colors
static void DrawPalettePreview(ATGTIAEmulator& gtia) {
	uint32 pal[256];
	gtia.GetPalette(pal);

	// Compute cell size based on available width
	float availW = ImGui::GetContentRegionAvail().x;
	float cellSize = floorf(std::min(availW / 16.0f, 16.0f));
	if (cellSize < 4.0f)
		cellSize = 4.0f;

	float gridW = cellSize * 16.0f;
	float gridH = cellSize * 16.0f;

	// Extra rows: row 17 = text screen demo, row 18 = NTSC artifact colors
	float totalH = gridH + cellSize * 3.0f;

	ImVec2 pos = ImGui::GetCursorScreenPos();
	ImDrawList *dl = ImGui::GetWindowDrawList();

	// Draw 16x16 palette grid.  Windows uses a bottom-up BMP with a palette
	// flip, producing $00-$0F at the top and $F0-$FF at the bottom.
	for (int row = 0; row < 16; ++row) {
		for (int col = 0; col < 16; ++col) {
			int palIdx = row * 16 + col;
			uint32 c = pal[palIdx];
			ImU32 color = IM_COL32((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF, 255);

			ImVec2 p0(pos.x + col * cellSize, pos.y + row * cellSize);
			ImVec2 p1(p0.x + cellSize, p0.y + cellSize);
			dl->AddRectFilled(p0, p1, color);
		}
	}

	// Below the 16x16 palette: 3 extra rows matching Windows layout.
	// Windows bitmap (16x19, bottom-up) has the following below the palette:
	//   Row 2: empty (black)
	//   Row 1: text screen + artifact colors (border|bg...|textchar|bg...|border|art0|art0|art1|art1)
	//   Row 0: text screen no char (border|bg...|border)
	// Displayed top-to-bottom after palette: empty, text+artifacts, text plain.
	float extraY = pos.y + gridH;
	{
		ImU32 blackColor = IM_COL32(0, 0, 0, 255);
		ImU32 borderColor = IM_COL32((pal[0] >> 16) & 0xFF, (pal[0] >> 8) & 0xFF, pal[0] & 0xFF, 255);
		ImU32 scrColor = IM_COL32((pal[0x94] >> 16) & 0xFF, (pal[0x94] >> 8) & 0xFF, pal[0x94] & 0xFF, 255);
		ImU32 fgColor = IM_COL32((pal[0x9A] >> 16) & 0xFF, (pal[0x9A] >> 8) & 0xFF, pal[0x9A] & 0xFF, 255);

		// Fill all 3 extra rows with black first, then overdraw specific cells.
		// Windows bitmap: row 2 (separator) = all zero, rows 0-1 use pal[0] for
		// border cells and zero for cols 12-15 in the text-only row.
		dl->AddRectFilled(ImVec2(pos.x, extraY), ImVec2(pos.x + gridW, extraY + cellSize * 3.0f), blackColor);

		// Row 1: text screen with text char at col 2, plus NTSC artifact colors in cols 12-15.
		// Windows: col 0 = pal[0], cols 1-10 = pal[0x94], col 2 = pal[0x9A], col 11 = pal[0],
		//          cols 12-13 = ntscac[0], cols 14-15 = ntscac[1].
		float r1y = extraY + cellSize;
		dl->AddRectFilled(ImVec2(pos.x, r1y), ImVec2(pos.x + cellSize, r1y + cellSize), borderColor);            // col 0
		for (int c = 1; c <= 10; ++c)
			dl->AddRectFilled(ImVec2(pos.x + c * cellSize, r1y), ImVec2(pos.x + (c + 1) * cellSize, r1y + cellSize), scrColor);
		dl->AddRectFilled(ImVec2(pos.x + 2 * cellSize, r1y), ImVec2(pos.x + 3 * cellSize, r1y + cellSize), fgColor);  // text char
		dl->AddRectFilled(ImVec2(pos.x + 11 * cellSize, r1y), ImVec2(pos.x + 12 * cellSize, r1y + cellSize), borderColor); // col 11

		uint32 ntscac[2];
		gtia.GetNTSCArtifactColors(ntscac);
		for (int i = 0; i < 2; ++i) {
			uint32 ac = ntscac[i];
			ImU32 acColor = IM_COL32((ac >> 16) & 0xFF, (ac >> 8) & 0xFF, ac & 0xFF, 255);
			dl->AddRectFilled(
				ImVec2(pos.x + (12 + i * 2) * cellSize, r1y),
				ImVec2(pos.x + (14 + i * 2) * cellSize, r1y + cellSize),
				acColor);
		}

		// Row 2: text screen without text char.
		// Windows: col 0 = pal[0], cols 1-10 = pal[0x94], col 11 = pal[0], cols 12-15 = 0 (black).
		float r2y = extraY + 2.0f * cellSize;
		dl->AddRectFilled(ImVec2(pos.x, r2y), ImVec2(pos.x + cellSize, r2y + cellSize), borderColor);            // col 0
		for (int c = 1; c <= 10; ++c)
			dl->AddRectFilled(ImVec2(pos.x + c * cellSize, r2y), ImVec2(pos.x + (c + 1) * cellSize, r2y + cellSize), scrColor);
		dl->AddRectFilled(ImVec2(pos.x + 11 * cellSize, r2y), ImVec2(pos.x + 12 * cellSize, r2y + cellSize), borderColor); // col 11
	}

	// Reserve space in ImGui layout
	ImGui::Dummy(ImVec2(gridW, totalH));
}

void ATUIRenderAdjustColors(ATSimulator &sim, ATUIState &state) {
	ImGui::SetNextWindowSize(ImVec2(500, 720), ImGuiCond_Appearing);
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

	ImGuiWindowFlags flags = ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_MenuBar;
	if (!ImGui::Begin("Adjust Colors", &state.showAdjustColors, flags)) {
		ImGui::End();
		return;
	}

	if (ATUICheckEscClose()) {
		state.showAdjustColors = false;
		ImGui::End();
		return;
	}

	ATGTIAEmulator& gtia = sim.GetGTIA();

	// Apply pending load from file dialog callback (thread-safe handoff)
	{
		std::lock_guard<std::mutex> lock(s_pendingColorsMutex);
		if (s_pendingColorsLoaded) {
			gtia.SetColorSettings(s_pendingColorSettings);
			s_pendingColorsLoaded = false;
		}
	}

	ATColorSettings settings = gtia.GetColorSettings();

	// Select active params based on current video mode
	bool isPAL = gtia.IsPALMode();
	ATNamedColorParams *params = isPAL ? &settings.mPALParams : &settings.mNTSCParams;

	// paramsChanged: slider/combo changes that should clear the preset tag
	// settingsChanged: menu changes (shared/separate, PAL quirks) that preserve the preset
	bool paramsChanged = false;
	bool settingsChanged = false;
	bool openShareConfirm = false;

	// ---- Menu bar (matches Windows IDR_ADJUSTCOLORS_MENU) ----
	if (ImGui::BeginMenuBar()) {
		if (ImGui::BeginMenu("File")) {
			if (ImGui::MenuItem("Load...")) {
				static const SDL_DialogFileFilter kFilter[] = {
					{ "Altirra color settings (*.atcolors)", "atcolors" },
				};
				SDL_ShowOpenFileDialog(AdjustColorsLoadCallback,
					nullptr, g_pWindow, kFilter, 1, nullptr, false);
			}
			if (ImGui::MenuItem("Save...")) {
				static const SDL_DialogFileFilter kFilter[] = {
					{ "Altirra color settings (*.atcolors)", "atcolors" },
				};
				SDL_ShowSaveFileDialog(AdjustColorsSaveCallback,
					nullptr, g_pWindow, kFilter, 1, nullptr);
			}
			if (ImGui::MenuItem("Export Palette...")) {
				static const SDL_DialogFileFilter kPalFilter[] = {
					{ "Atari800 palette (*.pal)", "pal" },
				};
				SDL_ShowSaveFileDialog(AdjustColorsExportPaletteCallback,
					nullptr, g_pWindow, kPalFilter, 1, nullptr);
			}
			if (ImGui::MenuItem("Palette Solver...")) {
				s_adjustColors_showPaletteSolver = true;
			}
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("View")) {
			if (ImGui::MenuItem("Show Red/Green Shifts", nullptr, !s_adjustColors_showRelativeOffsets))
				s_adjustColors_showRelativeOffsets = false;
			if (ImGui::MenuItem("Show Red/Green Relative Offsets", nullptr, s_adjustColors_showRelativeOffsets))
				s_adjustColors_showRelativeOffsets = true;
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Options")) {
			if (ImGui::MenuItem("Shared NTSC/PAL Settings", nullptr, !settings.mbUsePALParams)) {
				if (settings.mbUsePALParams) {
					// Windows prompts: "Enabling palette sharing will overwrite
					// the other profile with the current colors. Proceed?"
					// Defer OpenPopup to main window context (can't open from menu)
					openShareConfirm = true;
				}
			}
			if (ImGui::MenuItem("Separate NTSC and PAL Settings", nullptr, settings.mbUsePALParams)) {
				if (!settings.mbUsePALParams) {
					settings.mbUsePALParams = true;
					settingsChanged = true;
				}
			}
			ImGui::Separator();
			bool palQuirks = params->mbUsePALQuirks;
			if (ImGui::MenuItem("Use PAL Quirks", nullptr, &palQuirks, isPAL)) {
				params->mbUsePALQuirks = palQuirks;
				settingsChanged = true;
			}
			ImGui::EndMenu();
		}
		ImGui::EndMenuBar();
	}

	// ---- Palette preview ----
	if (ImGui::CollapsingHeader("Palette Preview", ImGuiTreeNodeFlags_DefaultOpen)) {
		DrawPalettePreview(gtia);
	}

	// ---- Preset selection ----
	// Windows OnPresetChanged: selecting a named preset sets the tag + copies
	// params via OnDataExchange(true) without going through ForcePresetToCustom.
	// Selecting "(Custom)" clears the tag.  Neither path should trigger the
	// preset-clearing logic, so we use settingsChanged (not paramsChanged).
	{
		uint32 presetCount = ATGetColorPresetCount();
		sint32 curPresetIdx = -1;
		if (!params->mPresetTag.empty())
			curPresetIdx = ATGetColorPresetIndexByTag(params->mPresetTag.c_str());

		int comboIdx = (curPresetIdx >= 0) ? (curPresetIdx + 1) : 0;
		VDStringA curLabel = (comboIdx == 0) ? VDStringA("(Custom)")
			: VDTextWToU8(VDStringW(ATGetColorPresetNameByIndex(curPresetIdx)));
		if (ImGui::BeginCombo("Preset", curLabel.c_str())) {
			if (ImGui::Selectable("(Custom)", comboIdx == 0)) {
				params->mPresetTag.clear();
				settingsChanged = true;
			}
			for (uint32 i = 0; i < presetCount; ++i) {
				VDStringA name = VDTextWToU8(VDStringW(ATGetColorPresetNameByIndex(i)));
				bool selected = ((int)i + 1 == comboIdx);
				if (ImGui::Selectable(name.c_str(), selected)) {
					params->mPresetTag = ATGetColorPresetTagByIndex(i);
					(ATColorParams &)*params = ATGetColorPresetByIndex(i);
					settingsChanged = true;
				}
			}
			ImGui::EndCombo();
		}
	}

	// Shared NTSC/PAL toggle (also accessible from Options menu)
	{
		bool shared = !settings.mbUsePALParams;
		if (ImGui::Checkbox("Share NTSC/PAL settings", &shared)) {
			if (shared && settings.mbUsePALParams) {
				// Switching to shared — needs confirmation (same as menu)
				openShareConfirm = true;
			} else if (!shared) {
				settings.mbUsePALParams = true;
				settingsChanged = true;
			}
		}
		ImGui::SameLine();
		ImGui::TextDisabled("(%s active)", isPAL ? "PAL" : "NTSC");
	}

	// Confirmation popup for switching to shared palettes
	if (openShareConfirm)
		ImGui::OpenPopup("Confirm Share Palettes");
	if (ImGui::BeginPopupModal("Confirm Share Palettes", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
		ImGui::Text("Enabling palette sharing will overwrite the other\nprofile with the current colors. Proceed?");
		ImGui::Separator();
		if (ImGui::Button("OK", ImVec2(120, 0))) {
			settings.mbUsePALParams = false;
			settingsChanged = true;
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel", ImVec2(120, 0))) {
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}

	ImGui::Separator();

	// Luma ramp mode
	{
		static const char *kLumaLabels[] = { "Linear", "XL/XE" };
		int lumaIdx = (int)params->mLumaRampMode;
		if (lumaIdx < 0 || lumaIdx >= 2) lumaIdx = 0;
		if (ImGui::Combo("Luma Ramp", &lumaIdx, kLumaLabels, 2)) {
			params->mLumaRampMode = (ATLumaRampMode)lumaIdx;
			paramsChanged = true;
		}
	}

	// Color matching mode (matches Windows kColorModeNames order)
	{
		static const char *kMatchLabels[] = {
			"None",
			"NTSC/PAL to sRGB",
			"NTSC/PAL to gamma 2.2",
			"NTSC/PAL to gamma 2.4",
			"NTSC/PAL to Adobe RGB"
		};
		// Windows order: None, SRGB, Gamma22, Gamma24, AdobeRGB
		static const ATColorMatchingMode kMatchModes[] = {
			ATColorMatchingMode::None,
			ATColorMatchingMode::SRGB,
			ATColorMatchingMode::Gamma22,
			ATColorMatchingMode::Gamma24,
			ATColorMatchingMode::AdobeRGB,
		};
		int matchIdx = 0;
		for (int i = 0; i < 5; ++i) {
			if (kMatchModes[i] == params->mColorMatchingMode) {
				matchIdx = i;
				break;
			}
		}
		if (ImGui::Combo("Color Matching", &matchIdx, kMatchLabels, 5)) {
			params->mColorMatchingMode = kMatchModes[matchIdx];
			paramsChanged = true;
		}
	}

	ImGui::Separator();

	// Hue controls
	if (ImGui::CollapsingHeader("Hue", ImGuiTreeNodeFlags_DefaultOpen)) {
		if (SliderDegrees("Hue Start", &params->mHueStart, -120.0f, 360.0f))
			paramsChanged = true;

		// Hue Step: slider moves mHueRange (0-540 total).  Windows shows
		// the per-step angle (hueRange/15) as the value label beside the slider.
		if (ImGui::SliderFloat("Hue Step", &params->mHueRange, 0.0f, 540.0f, "%.0f\xc2\xb0"))
			paramsChanged = true;
		ImGui::SameLine();
		ImGui::Text("(%.1f\xc2\xb0/step)", params->mHueRange / 15.0f);
	}

	// Brightness / Contrast
	if (ImGui::CollapsingHeader("Brightness / Contrast", ImGuiTreeNodeFlags_DefaultOpen)) {
		if (SliderPercent("Brightness", &params->mBrightness, -0.50f, 0.50f, "%+.0f%%"))
			paramsChanged = true;
		if (SliderPercent("Contrast", &params->mContrast, 0.0f, 2.0f))
			paramsChanged = true;
		if (SliderPercent("Saturation", &params->mSaturation, 0.0f, 1.0f))
			paramsChanged = true;
		if (ImGui::SliderFloat("Gamma Correction", &params->mGammaCorrect, 0.50f, 2.60f, "%.2f"))
			paramsChanged = true;
		if (ImGui::SliderFloat("Intensity Scale", &params->mIntensityScale, 0.50f, 2.0f, "%.2f"))
			paramsChanged = true;
	}

	// Artifacting
	if (ImGui::CollapsingHeader("Artifacting")) {
		if (SliderDegrees("Phase", &params->mArtifactHue, -60.0f, 360.0f))
			paramsChanged = true;
		if (SliderPercent("Saturation##Art", &params->mArtifactSat, 0.0f, 4.0f))
			paramsChanged = true;
		if (ImGui::SliderFloat("Sharpness", &params->mArtifactSharpness, 0.0f, 1.0f, "%.2f"))
			paramsChanged = true;
	}

	// Color matrix adjustments
	if (ImGui::CollapsingHeader("Color Matrix")) {
		// R-Y controls with optional relative offset display
		if (s_adjustColors_showRelativeOffsets) {
			float relAngle = 90.0f - params->mRedShift;
			char buf[32];
			snprintf(buf, sizeof(buf), "B%+.1f\xc2\xb0", relAngle);
			if (ImGui::SliderFloat("R-Y Shift", &params->mRedShift, -22.5f, 22.5f, buf))
				paramsChanged = true;
		} else {
			if (ImGui::SliderFloat("R-Y Shift", &params->mRedShift, -22.5f, 22.5f, "%.1f\xc2\xb0"))
				paramsChanged = true;
		}

		if (s_adjustColors_showRelativeOffsets) {
			float relScale = params->mRedScale * 0.560949f;
			char buf[32];
			snprintf(buf, sizeof(buf), "B\xc3\x97%.2f", relScale);
			if (ImGui::SliderFloat("R-Y Scale", &params->mRedScale, 0.0f, 4.0f, buf))
				paramsChanged = true;
		} else {
			if (ImGui::SliderFloat("R-Y Scale", &params->mRedScale, 0.0f, 4.0f, "%.2f"))
				paramsChanged = true;
		}

		// G-Y controls
		if (s_adjustColors_showRelativeOffsets) {
			float relAngle = 235.80197f - params->mGrnShift;
			char buf[32];
			snprintf(buf, sizeof(buf), "B%+.1f\xc2\xb0", relAngle);
			if (ImGui::SliderFloat("G-Y Shift", &params->mGrnShift, -22.5f, 22.5f, buf))
				paramsChanged = true;
		} else {
			if (ImGui::SliderFloat("G-Y Shift", &params->mGrnShift, -22.5f, 22.5f, "%.1f\xc2\xb0"))
				paramsChanged = true;
		}

		if (s_adjustColors_showRelativeOffsets) {
			float relScale = params->mGrnScale * 0.3454831f;
			char buf[32];
			snprintf(buf, sizeof(buf), "B\xc3\x97%.2f", relScale);
			if (ImGui::SliderFloat("G-Y Scale", &params->mGrnScale, 0.0f, 4.0f, buf))
				paramsChanged = true;
		} else {
			if (ImGui::SliderFloat("G-Y Scale", &params->mGrnScale, 0.0f, 4.0f, "%.2f"))
				paramsChanged = true;
		}

		// B-Y controls (no relative offset mode)
		if (ImGui::SliderFloat("B-Y Shift", &params->mBluShift, -22.5f, 22.5f, "%.1f\xc2\xb0"))
			paramsChanged = true;
		if (ImGui::SliderFloat("B-Y Scale", &params->mBluScale, 0.0f, 4.0f, "%.2f"))
			paramsChanged = true;
	}

	// Apply changes live
	if (paramsChanged || settingsChanged) {
		// Only clear preset tag when actual color parameters changed,
		// not for menu-only changes (shared/separate toggle, PAL quirks).
		if (paramsChanged)
			params->mPresetTag.clear();

		if (!settings.mbUsePALParams) {
			if (isPAL)
				settings.mNTSCParams = *params;
			else
				settings.mPALParams = *params;
		}
		gtia.SetColorSettings(settings);
	}

	ImGui::Separator();

	if (ImGui::Button("Reset to Defaults")) {
		ATColorSettings defaults = gtia.GetDefaultColorSettings();
		gtia.SetColorSettings(defaults);
	}

	ImGui::End();

	// Render palette solver as a separate window if open
	if (s_adjustColors_showPaletteSolver)
		ATUIRenderPaletteSolver(sim, s_adjustColors_showPaletteSolver);
}
