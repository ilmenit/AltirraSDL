//	AltirraSDL - System Configuration dialog
//	Mirrors Windows Altirra's Configure System paged dialog
//	(IDD_CONFIGURE with tree sidebar IDC_PAGE_TREE).
//	Hierarchy: Computer, Outputs, Peripherals, Media, Emulator.

#include <stdafx.h>
#include <SDL3/SDL.h>
#include <imgui.h>
#include <vd2/system/vdtypes.h>
#include <vd2/system/VDString.h>
#include <vd2/system/text.h>
#include <vd2/system/filesys.h>
#include <vd2/system/unknown.h>

#include "ui_main.h"
#include "ui_system_internal.h"
#include "simulator.h"
#include "constants.h"
#include "cpu.h"
#include "firmwaremanager.h"
#include "devicemanager.h"
#include "diskinterface.h"
#include "cartridge.h"
#include "gtia.h"
#include "cassette.h"
#include "options.h"
#include "uiaccessors.h"
#include <at/atcore/media.h>
#include <at/atcore/device.h>
#include <at/atcore/deviceparent.h>
#include <at/atcore/propertyset.h>
#include "uiconfirm.h"
#include "uikeyboard.h"
#include "uitypes.h"
#include <at/ataudio/pokey.h>
#include <at/ataudio/audiooutput.h>
#include <at/atio/cassetteimage.h>
#include "inputcontroller.h"
#include "compatengine.h"
#include "firmwaredetect.h"
#include "autosavemanager.h"
#include "debugger.h"
#include "settings.h"
#include <at/atnativeui/genericdialog.h>
#include <at/atui/uimanager.h>
#include <vd2/system/file.h>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>
#include "logging.h"

extern ATSimulator g_sim;
extern ATUIManager g_ATUIManager;
void ATUIUpdateSpeedTiming();
void ATUIResizeDisplay();
void ATSyncCPUHistoryState();

// Firmware Manager window visibility (used by drag-and-drop handler in main loop)

// =========================================================================
// Category IDs (flat index used by systemConfigCategory in ATUIState)
// Matches Windows tree order from uiconfiguresystem.cpp OnPopulatePages()
// =========================================================================

enum {
	// Top-level (before categories)
	kCat_Overview,      // Overview (system config summary)
	kCat_Recommendations, // Recommendations (assessment)
	// Computer
	kCat_System,        // Computer > System
	kCat_CPU,           // Computer > CPU
	kCat_Firmware,      // Computer > Firmware
	kCat_Memory,        // Computer > Memory
	kCat_Acceleration,  // Computer > Acceleration
	kCat_Speed,         // Computer > Speed
	kCat_Boot,          // Computer > Boot
	// Outputs
	kCat_Video,         // Outputs > Video
	kCat_EnhancedText,  // Outputs > Enhanced Text
	kCat_Audio,         // Outputs > Audio
	// Peripherals
	kCat_Devices,       // Peripherals > Devices
	kCat_Keyboard,      // Peripherals > Keyboard
	// Media
	kCat_MediaDefaults, // Media > Defaults
	kCat_Disk,          // Media > Disk
	kCat_Cassette,      // Media > Cassette
	kCat_Flash,         // Media > Flash
	// Emulator
	kCat_CompatDB,      // Emulator > Compat DB
	kCat_Display,       // Emulator > Display
	kCat_EaseOfUse,     // Emulator > Ease of Use
	kCat_ErrorHandling, // Emulator > Error Handling
	kCat_Input,         // Emulator > Input
	kCat_Caption,       // Emulator > Window Caption
	kCat_Workarounds,   // Emulator > Workarounds
	kCat_Accessibility, // Emulator > Accessibility
	kCat_DebuggerCfg,   // Emulator > Debugger
	kCat_UI,            // Emulator > UI
	kCat_Fonts,         // Emulator > Fonts
	kCat_Display2,      // Emulator > Display Effects
	kCat_SettingsCfg,   // Emulator > Settings
	kCat_Count
};

// =========================================================================
// System Configuration window — paged dialog with hierarchical sidebar
// Matches Windows tree: Computer, Outputs, Peripherals, Media, Emulator
// =========================================================================

struct TreeEntry {
	const char *label;
	int catId;       // -1 = header only (non-selectable)
	int indent;      // 0 = top-level header, 1 = leaf
};

static const TreeEntry kTreeEntries[] = {
	// Top-level pages (before categories, matches Windows)
	{ "Overview",       kCat_Overview,  1 },
	{ "Recommendations", kCat_Recommendations, 1 },
	// Computer
	{ "Computer",       -1,             0 },
	{ "System",         kCat_System,    1 },
	{ "CPU",            kCat_CPU,       1 },
	{ "Firmware",       kCat_Firmware,  1 },
	{ "Memory",         kCat_Memory,    1 },
	{ "Acceleration",   kCat_Acceleration, 1 },
	{ "Speed",          kCat_Speed,     1 },
	{ "Boot",           kCat_Boot,      1 },
	// Outputs
	{ "Outputs",        -1,             0 },
	{ "Video",          kCat_Video,     1 },
	{ "Enhanced Text",  kCat_EnhancedText, 1 },
	{ "Audio",          kCat_Audio,     1 },
	// Peripherals
	{ "Peripherals",    -1,             0 },
	{ "Devices",        kCat_Devices,   1 },
	{ "Keyboard",       kCat_Keyboard,  1 },
	// Media
	{ "Media",          -1,             0 },
	{ "Defaults",       kCat_MediaDefaults, 1 },
	{ "Disk",           kCat_Disk,      1 },
	{ "Cassette",       kCat_Cassette,  1 },
	{ "Flash",          kCat_Flash,     1 },
	// Emulator
	{ "Emulator",       -1,             0 },
	{ "Compat DB",      kCat_CompatDB,  1 },
	{ "Display",        kCat_Display,   1 },
	{ "Ease of Use",    kCat_EaseOfUse, 1 },
	{ "Error Handling", kCat_ErrorHandling, 1 },
	{ "Input",          kCat_Input,     1 },
	{ "Window Caption", kCat_Caption,   1 },
	{ "Workarounds",    kCat_Workarounds, 1 },
	{ "Accessibility",  kCat_Accessibility, 1 },
	{ "Debugger",       kCat_DebuggerCfg, 1 },
	{ "UI",             kCat_UI,        1 },
	{ "Fonts",          kCat_Fonts,     1 },
	{ "Display Effects", kCat_Display2, 1 },
	{ "Settings",       kCat_SettingsCfg, 1 },
};
static const int kNumTreeEntries = sizeof(kTreeEntries) / sizeof(kTreeEntries[0]);

void ATUIRenderSystemConfig(ATSimulator &sim, ATUIState &state) {
	ImGui::SetNextWindowSize(ImVec2(640, 480), ImGuiCond_Appearing);
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	if (!ImGui::Begin("Configure System", &state.showSystemConfig, ImGuiWindowFlags_NoSavedSettings)) {
		ImGui::End();
		return;
	}

	if (ATUICheckEscClose()) {
		state.showSystemConfig = false;
		ImGui::End();
		return;
	}

	// Reserve space at bottom for OK button
	float footerHeight = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;

	// Left sidebar — tree hierarchy
	ImGui::BeginChild("##SysCfgTree", ImVec2(150, -footerHeight), ImGuiChildFlags_Borders);
	for (int i = 0; i < kNumTreeEntries; ++i) {
		const TreeEntry& te = kTreeEntries[i];
		if (te.indent == 0) {
			// Category header (non-selectable)
			ImGui::Spacing();
			ImGui::TextDisabled("%s", te.label);
		} else {
			// Leaf item (selectable)
			ImGui::Indent(8.0f);
			if (ImGui::Selectable(te.label, state.systemConfigCategory == te.catId))
				state.systemConfigCategory = te.catId;
			ImGui::Unindent(8.0f);
		}
	}
	ImGui::EndChild();

	ImGui::SameLine();

	// Right content
	ImGui::BeginChild("##SysCfgContent", ImVec2(0, -footerHeight));
	switch (state.systemConfigCategory) {
	case kCat_Overview:       RenderOverviewCategory(sim); break;
	case kCat_Recommendations: RenderRecommendationsCategory(sim); break;
	case kCat_System:         RenderSystemCategory(sim); break;
	case kCat_CPU:            RenderCPUCategory(sim); break;
	case kCat_Firmware:       RenderFirmwareCategory(sim); break;
	case kCat_Memory:         RenderMemoryCategory(sim); break;
	case kCat_Acceleration:   RenderAccelerationCategory(sim); break;
	case kCat_Speed:          RenderSpeedCategory(sim); break;
	case kCat_Boot:           RenderBootCategory(sim); break;
	case kCat_Video:          RenderVideoCategory(sim); break;
	case kCat_EnhancedText:   RenderEnhancedTextCategory(sim); break;
	case kCat_Audio:          RenderAudioCategory(sim, state); break;
	case kCat_Devices:        RenderDevicesCategory(sim); break;
	case kCat_Keyboard:       RenderKeyboardCategory(sim, state); break;
	case kCat_MediaDefaults:  RenderMediaDefaultsCategory(sim); break;
	case kCat_Disk:           RenderDiskCategory(sim); break;
	case kCat_Cassette:       RenderCassetteCategory(sim); break;
	case kCat_Flash:          RenderFlashCategory(sim); break;
	case kCat_CompatDB:       RenderCompatDBCategory(sim); break;
	case kCat_Display:        RenderDisplayCategory(sim); break;
	case kCat_EaseOfUse:      RenderEaseOfUseCategory(sim); break;
	case kCat_ErrorHandling:  RenderErrorHandlingCategory(sim); break;
	case kCat_Input:          RenderInputCategory(sim); break;
	case kCat_Caption:        RenderCaptionCategory(sim); break;
	case kCat_Workarounds:    RenderWorkaroundsCategory(sim); break;
	case kCat_Accessibility:  RenderAccessibilityCategory(sim); break;
	case kCat_DebuggerCfg:    RenderDebuggerCfgCategory(sim); break;
	case kCat_UI:             RenderUICategory(sim); break;
	case kCat_Fonts:          RenderFontsCategory(sim); break;
	case kCat_Display2:       RenderDisplay2Category(sim); break;
	case kCat_SettingsCfg:    RenderSettingsCfgCategory(sim); break;
	}
	ImGui::EndChild();

	// OK button — matches Windows DEFPUSHBUTTON "OK"
	ImGui::Separator();
	float buttonWidth = 80.0f;
	ImGui::SetCursorPosX(ImGui::GetWindowWidth() - buttonWidth - ImGui::GetStyle().WindowPadding.x);
	if (ImGui::Button("OK", ImVec2(buttonWidth, 0)))
		state.showSystemConfig = false;

	ImGui::End();
}
