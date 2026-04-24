//	AltirraSDL - System Config dialog internal header
//	Declares every Render*Category entry point so the paged dialog
//	dispatcher in ui_system.cpp can dispatch to pages defined in
//	ui_system_pages_*.cpp files.

#ifndef f_AT_UI_SYSTEM_INTERNAL_H
#define f_AT_UI_SYSTEM_INTERNAL_H

class ATSimulator;
struct ATUIState;

// Computer group (sysconfig_pages_computer.cpp)
void RenderOverviewCategory(ATSimulator &sim);
void RenderRecommendationsCategory(ATSimulator &sim);
void RenderSystemCategory(ATSimulator &sim);
void RenderCPUCategory(ATSimulator &sim);
void RenderMemoryCategory(ATSimulator &sim);
void RenderAccelerationCategory(ATSimulator &sim);
void RenderSpeedCategory(ATSimulator &sim);
void RenderBootCategory(ATSimulator &sim);

// Outputs + small media/emulator pages (sysconfig_pages_outputs.cpp)
void RenderVideoCategory(ATSimulator &sim);
void RenderAudioCategory(ATSimulator &sim, ATUIState &state);
void RenderEnhancedTextCategory(ATSimulator &sim);
void RenderKeyboardCategory(ATSimulator &sim, ATUIState &state);
void RenderDiskCategory(ATSimulator &sim);
void RenderCassetteCategory(ATSimulator &sim);
void RenderDisplayCategory(ATSimulator &sim);
void RenderInputCategory(ATSimulator &sim);
void RenderEaseOfUseCategory(ATSimulator &sim);

// Devices + rest (sysconfig_pages_emulator.cpp)
void RenderDevicesCategory(ATSimulator &sim);
void RenderMediaDefaultsCategory(ATSimulator &sim);
void RenderCaptionCategory(ATSimulator &sim);
void RenderWorkaroundsCategory(ATSimulator &sim);
void RenderCompatDBCategory(ATSimulator &sim);
void RenderErrorHandlingCategory(ATSimulator &sim);
void RenderFlashCategory(ATSimulator &sim);
void RenderAccessibilityCategory(ATSimulator &sim);
void RenderDebuggerCfgCategory(ATSimulator &sim);
void RenderUICategory(ATSimulator &sim);
void RenderDisplay2Category(ATSimulator &sim);
void RenderSettingsCfgCategory(ATSimulator &sim);
#ifdef ALTIRRA_NETPLAY_ENABLED
// Defined in ui_system_pages_netplay.cpp, which is only compiled when
// the netplay module is enabled.  The sysconfig sidebar's kCat_OnlinePlay
// branch is guarded by the same macro.
void RenderOnlinePlayCategory(ATSimulator &sim);
#endif

// Firmware page lives in ui_firmware_category.cpp (ui/firmware/).
void RenderFirmwareCategory(ATSimulator &sim);

// Fonts page lives in ui_fonts.cpp (ui/core/).
void RenderFontsCategory(ATSimulator &sim);

#endif
