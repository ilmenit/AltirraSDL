//	AltirraSDL - Dear ImGui UI layer
//	Top-level UI state and rendering interface.

#pragma once

#include <vd2/system/vdtypes.h>
#include <vd2/system/VDString.h>
#include <imgui.h>

struct SDL_Window;
struct SDL_Renderer;
union SDL_Event;
class ATSimulator;
class VDVideoDisplaySDL3;

struct ATUIState {
	bool requestExit = false;
	bool fileDialogPending = false;
	bool showExitConfirm = false;       // Exit confirmation popup pending
	bool exitConfirmed = false;          // User confirmed exit — proceed with quit

	// Dialog windows
	bool showSystemConfig = false;
	bool showDiskManager = false;
	bool showCassetteControl = false;
	bool showAboutDialog = false;
	bool showAdjustColors = false;
	bool showDisplaySettings = false;
	bool showCartridgeMapper = false;
	bool showAudioOptions = false;
	bool showInputMappings = false;
	bool showInputSetup = false;
	bool showProfiles = false;
	bool showCommandLineHelp = false;
	bool showChangeLog = false;
	bool showCompatWarning = false;

	// Tools menu dialogs
	bool showDiskExplorer = false;
	bool showSetupWizard = false;
	bool showKeyboardShortcuts = false;
	bool showCompatDB = false;
	bool showAdvancedConfig = false;

	// System config sidebar selection
	int systemConfigCategory = 0;
};

bool ATUIInit(SDL_Window *window, SDL_Renderer *renderer);
void ATUIShutdown();
bool ATUIProcessEvent(const SDL_Event *event);
bool ATUIWantCaptureKeyboard();
bool ATUIWantCaptureMouse();

// Frame timing telemetry — independent FPS measurement from the frame pacer.
float ATUIGetMeasuredFPS();

void ATUIRenderFrame(ATSimulator &sim, VDVideoDisplaySDL3 &display,
	SDL_Renderer *renderer, ATUIState &state);

// Process deferred file dialog results on main thread (call each frame)
void ATUIPollDeferredActions();

// MRU list (shared with main loop for file drop)
void ATAddMRU(const wchar_t *path);

// Quick save state (F7 load, F8 save)
void ATUIQuickSaveState();
void ATUIQuickLoadState();

// Mouse capture — SDL3 implementation (defined in uiaccessors_stubs.cpp)
void ATUISetMouseCaptureWindow(SDL_Window *window);
void ATUICaptureMouse();
void ATUIReleaseMouse();

// Deferred action types — shared between ui_main.cpp and ui_cartmapper.cpp
enum ATDeferredActionType {
	kATDeferred_BootImage,
	kATDeferred_OpenImage,
	kATDeferred_AttachCartridge,
	kATDeferred_AttachSecondaryCartridge,
	kATDeferred_AttachDisk,        // uses mInt for drive index
	kATDeferred_LoadState,
	kATDeferred_SaveState,
	kATDeferred_SaveCassette,
	kATDeferred_ExportCassetteAudio,
	kATDeferred_SaveCartridge,
	kATDeferred_SaveFirmware,      // uses mInt for firmware index
	kATDeferred_LoadCassette,
	kATDeferred_StartRecordRaw,
	kATDeferred_StartRecordWAV,
	kATDeferred_StartRecordSAP,
	kATDeferred_StartRecordVideo,  // uses mInt for ATVideoEncoding
	kATDeferred_SetCompatDBPath,
	kATDeferred_ConvertSAPToEXE,   // mPath = source SAP, mStr = dest XEX
	kATDeferred_ExportROMSet,      // mPath = target folder
	kATDeferred_AnalyzeTapeDecode, // mPath = source WAV, mStr = dest WAV
};

// Push a deferred action (thread-safe — may be called from file dialog callbacks)
void ATUIPushDeferred(ATDeferredActionType type, const char *utf8path, int extra = 0);

// Cartridge mapper dialog (ui_cartmapper.cpp)
extern bool g_cartMapperPending;
void ATUIOpenCartridgeMapperDialog(ATDeferredActionType origAction,
	const VDStringW &path, int slot, bool coldReset,
	const vdfastvector<uint8> &capturedData, uint32 cartSize);
void ATUIRenderCartridgeMapper(ATUIState &state);

// Firmware Manager — global visibility flag and drop handler (ui_system.cpp)
extern bool g_showFirmwareManager;
bool ATUIFirmwareManagerHandleDrop(const char *utf8path);

// Menu bar (ui_menus.cpp)
void ATUIRenderMainMenu(ATSimulator &sim, SDL_Window *window, SDL_Renderer *renderer, ATUIState &state);
void ATUIPushDeferred2(ATDeferredActionType type, const char *utf8path1, const char *utf8path2);
bool ATUIHasQuickSaveState();

// Recording (ui_recording.cpp)
bool ATUIIsRecording();
void ATUIStopRecording();
void ATUIRenderVideoRecordingDialog(SDL_Window *window);
void ATUIRenderAudioOptionsDialog(ATUIState &state);
bool ATUIIsRecordingPaused();
void ATUIToggleRecordingPause();
bool ATUIIsVideoRecording();
void ATUIShowVideoRecordingDialog();

// Frame capture (ui_main.cpp)
extern bool g_copyFrameRequested;
void ATUISaveFrameCallback(void *, const char * const *filelist, int);

// Dialog render functions (each in its own .cpp file)
void ATUIRenderSystemConfig(ATSimulator &sim, ATUIState &state);
void ATUIRenderDiskManager(ATSimulator &sim, ATUIState &state, SDL_Window *window);
void ATUIRenderCassetteControl(ATSimulator &sim, ATUIState &state, SDL_Window *window);
void ATUIRenderAdjustColors(ATSimulator &sim, ATUIState &state);
void ATUIRenderDisplaySettings(ATSimulator &sim, ATUIState &state);
void ATUIRenderInputMappings(ATSimulator &sim, ATUIState &state);
void ATUIRenderInputSetup(ATSimulator &sim, ATUIState &state);
void ATUIRenderProfiles(ATSimulator &sim, ATUIState &state);

// Tools menu dialogs
void ATUIRenderDiskExplorer(ATSimulator &sim, ATUIState &state, SDL_Window *window);
void ATUIRenderSetupWizard(ATSimulator &sim, ATUIState &state, SDL_Window *window);
void ATUIRenderKeyboardShortcuts(ATUIState &state);
void ATUIRenderCompatDB(ATSimulator &sim, ATUIState &state);
void ATUIRenderAdvancedConfig(ATUIState &state);

// Device configuration dialog (ui_devconfig.cpp)
class IATDevice;
class ATDeviceManager;
void ATUIOpenDeviceConfig(IATDevice *dev, ATDeviceManager *devMgr);
bool ATUIIsDeviceConfigOpen();
void ATUICloseDeviceConfigFor(IATDevice *dev); // close if open for this device
void ATUIRenderDeviceConfig(ATDeviceManager *devMgr);

// Compatibility warning — SDL3/ImGui replacement for Windows IDD_COMPATIBILITY
void ATUICheckCompatibility(ATSimulator &sim, ATUIState &state);
void ATUIRenderCompatWarning(ATSimulator &sim, ATUIState &state);

// ESC-to-close helper for ImGui dialog windows.
// Win32 dialogs close on ESC automatically (IDCANCEL). ImGui::Begin() windows
// do not.  Call this right after ImGui::Begin() succeeds; returns true if the
// dialog should close.  Only fires when the window (or a child within it) is
// focused and no popup is consuming input above it.
inline bool ATUICheckEscClose() {
	return ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)
		&& ImGui::IsKeyPressed(ImGuiKey_Escape);
}

// Exit confirmation — checks for dirty storage and shows discard dialog.
// Set state.showExitConfirm = true to trigger; the render function handles
// building the message, showing the popup, and pushing SDL_EVENT_QUIT on OK.
void ATUIRenderExitConfirm(ATSimulator &sim, ATUIState &state);
