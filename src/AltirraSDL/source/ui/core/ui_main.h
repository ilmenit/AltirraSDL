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
class IDisplayBackend;
class IATBlockDevice;

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
	bool showKeyboardCustomize = false;
	bool showCompatDB = false;
	bool showAdvancedConfig = false;
	bool showCheater = false;
	bool showLightPen = false;
	bool showRewind = false;
	bool showTapeEditor = false;
	bool showScreenEffects = false;
	bool showShaderParams = false;
	bool showShaderSetup = false;

	// Screen effects mode
	enum ScreenEffectsMode { kSFXMode_None, kSFXMode_Basic, kSFXMode_Preset };
	ScreenEffectsMode screenEffectsMode = kSFXMode_Basic;

	// View menu dialogs
	bool showCalibrate = false;
	bool showCustomizeHud = false;

	// Virtual on-screen keyboard
	bool showVirtualKeyboard = false;
	int oskPlacement = 0;  // ATOSKPlacement: 0=Auto, 1=Bottom, 2=Right

	// System config sidebar selection
	int systemConfigCategory = 0;
};

bool ATUIInit(SDL_Window *window, IDisplayBackend *backend);
void ATUIShutdown();
bool ATUIProcessEvent(const SDL_Event *event);

// Theme management
void ATUIApplyTheme();
void ATUIUpdateSystemTheme();
bool ATUIWantCaptureKeyboard();
bool ATUIWantCaptureMouse();

// Frame timing telemetry — independent FPS measurement from the frame pacer.
float ATUIGetMeasuredFPS();

void ATUIRenderFrame(ATSimulator &sim, VDVideoDisplaySDL3 &display,
	IDisplayBackend *backend, ATUIState &state);

// Process deferred file dialog results on main thread (call each frame)
void ATUIPollDeferredActions();

// MRU list (shared with main loop for file drop)
void ATAddMRU(const wchar_t *path);

// Quick save state (menu only — no default keyboard shortcut, matches Windows)
void ATUIQuickSaveState();
void ATUIQuickLoadState();

// File dialog openers (for keyboard shortcut wiring)
void ATUIShowBootImageDialog(SDL_Window *window);     // Alt+B
void ATUIShowOpenImageDialog(SDL_Window *window);     // Alt+O
void ATUIShowOpenSourceFileDialog(SDL_Window *window); // Alt+Shift+O
void ATUIShowSaveFrameDialog(SDL_Window *window);     // Alt+F10

// Capture the current display framebuffer as an SDL surface (caller frees).
struct SDL_Surface;
SDL_Surface *ATUIReadFramebuffer();

// Paste text from clipboard into emulator (Alt+Shift+V)
void ATUIPasteText();

// Paste text directly (not from clipboard) — used by --type command-line switch
void ATUIPasteTextDirect(const wchar_t *text, size_t len);

// Command-line processing (commandline_sdl3.cpp) — returns true if any
// boot image was loaded (caller should skip default ColdReset+Resume)
bool ATProcessCommandLineSDL3(int argc, char** argv);

// Mouse capture — SDL3 implementation (defined in uiaccessors_stubs.cpp)
void ATUISetMouseCaptureWindow(SDL_Window *window);
void ATUICaptureMouse();
void ATUIReleaseMouse();

// Main display rectangle in screen-space pixels (defined in main_sdl3.cpp).
// Returns false when the display is not currently drawn directly to the SDL
// framebuffer (e.g. debugger is open, or no frame has been produced yet).
bool ATGetMainDisplayRect(float& x, float& y, float& w, float& h);

// Process mouse text selection and draw the highlight overlay for the main
// Atari display.  Called once per frame from ATUIRenderFrame() when the
// debugger is closed.  Also renders the right-click context menu that
// matches Windows IDR_DISPLAY_CONTEXT_MENU.
void ATUIRenderMainDisplayTextSelection();

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
	kATDeferred_StartRecordVGM,
	kATDeferred_SetCompatDBPath,
	kATDeferred_ConvertSAPToEXE,   // mPath = source SAP, mStr = dest XEX
	kATDeferred_ExportROMSet,      // mPath = target folder
	kATDeferred_AnalyzeTapeDecode, // mPath = source WAV, mStr = dest WAV
};

// Push a deferred action (thread-safe — may be called from file dialog callbacks)
void ATUIPushDeferred(ATDeferredActionType type, const char *utf8path, int extra = 0);

// Tape editor request from trace viewer (ui_dbg_traceviewer_timeline.cpp)
struct ATTapeEditorRequest {
	bool pending = false;
	bool hasLocation = false;
	uint32 sample = 0;
	float pixelsPerSample = 0;
};
extern ATTapeEditorRequest g_tapeEditorRequest;

// Cartridge mapper dialog (ui_cartmapper.cpp)
extern bool g_cartMapperPending;
void ATUIOpenCartridgeMapperDialog(ATDeferredActionType origAction,
	const VDStringW &path, int slot, bool coldReset,
	const vdfastvector<uint8> &capturedData, uint32 cartSize);
void ATUIRenderCartridgeMapper(ATUIState &state);

// Firmware Manager — global visibility flag and drop handler (ui_system.cpp)
extern bool g_showFirmwareManager;
bool ATUIFirmwareManagerHandleDrop(const char *utf8path, float dropX, float dropY);
bool ATUIFirmwareManagerGetDropRect(ImVec2 &pos, ImVec2 &size); // returns true if open

// Menu bar (ui_menus.cpp)
void ATUIRenderMainMenu(ATSimulator &sim, SDL_Window *window, IDisplayBackend *backend, ATUIState &state);
void ATUIPushDeferred2(ATDeferredActionType type, const char *utf8path1, const char *utf8path2);
bool ATUIHasQuickSaveState();

// Recording (ui_recording.cpp)
bool ATUIIsRecording();
void ATUIStopRecording();
// Polls CheckExceptions() on any active video/audio/SAP/VGM writer and, if
// a background I/O error has surfaced, tears down that writer and shows a
// modal error. Safe to call every frame; no-op when nothing is recording.
// Parity with Windows ATUIFrontEnd::CheckRecordingExceptions().
void ATUICheckRecordingExceptions();
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

// Palette Solver sub-dialog (ui_palette_solver.cpp)
void ATUIRenderPaletteSolver(ATSimulator &sim, bool &open);
void ATUIShutdownPaletteSolver();
void ATUIRenderInputMappings(ATSimulator &sim, ATUIState &state);
void ATUIRenderInputSetup(ATSimulator &sim, ATUIState &state);
void ATUIRenderProfiles(ATSimulator &sim, ATUIState &state);

// Tools menu dialogs
void ATUIRenderDiskExplorer(ATSimulator &sim, ATUIState &state, SDL_Window *window);
void ATUIOpenDiskExplorerForDrive(int driveIdx, bool writable, bool autoFlush);
void ATUIOpenDiskExplorerForBlockDevice(IATBlockDevice *dev);
bool ATUIDiskExplorerHandleDrop(const char *utf8path, float dropX, float dropY);
bool ATUIDiskExplorerGetDropRect(ImVec2 &pos, ImVec2 &size); // returns true if open+writable
void ATUIRenderSetupWizard(ATSimulator &sim, ATUIState &state, SDL_Window *window);
void ATUIRenderCheater(ATSimulator &sim, ATUIState &state);
bool ATUIOpenRewindDialog();
void ATUIQuickRewind();
void ATUIRenderRewindDialog(ATSimulator &sim, ATUIState &state);
void ATUIRenderLightPenDialog(ATSimulator &sim, ATUIState &state);
void ATUIRenderKeyboardShortcuts(ATUIState &state);
void ATUIRenderKeyboardCustomize(ATUIState &state);
void ATUIRenderCompatDB(ATSimulator &sim, ATUIState &state);
void ATUIRenderAdvancedConfig(ATUIState &state);
void ATUIRenderTapeEditor(ATSimulator &sim, ATUIState &state, SDL_Window *window);
void ATUIRenderScreenEffects(ATSimulator &sim, ATUIState &state);

// Calibration dialog (ui_calibrate.cpp)
void ATUIRenderCalibrationDialog(ATUIState &state);

// HUD customization dialog (ui_hud_customize.cpp)
void ATUIRenderCustomizeHudDialog(ATUIState &state);

// HUD element visibility settings — used by ui_indicators.cpp for rendering,
// ui_hud_customize.cpp for the dialog, persisted to registry under "HUD" key.
struct ATHudSettings {
	bool showDiskLEDs      = true;
	bool showHActivity     = true;
	bool showCassette      = true;
	bool showRecording     = true;
	bool showFPS           = true;
	bool showWatches       = true;
	bool showStatusMessage = true;
	bool showErrors        = true;
	bool showPauseOverlay  = true;
	bool showHeldButtons   = true;
	bool showAudioScope    = false;
};
const ATHudSettings& ATUIGetHudSettings();

// Pan/Zoom tool state (managed in main_sdl3.cpp event loop)
bool ATUIIsPanZoomToolActive();
void ATUISetPanZoomToolActive(bool active);

// Shader presets (ui_shader_presets.cpp)
void ATUIShaderPresetsAutoLoad(IDisplayBackend *backend);
void ATUIShaderPresetsPoll(IDisplayBackend *backend);
void ATUIShaderPresetsClear(IDisplayBackend *backend);
void ATUIRenderShaderPresetMenu(IDisplayBackend *backend);
void ATUIRenderShaderParameters(ATUIState &state);
void ATUIRenderShaderSetupHelp(ATUIState &state);

// HUD overlay — drive LEDs, status messages, FPS, pause, errors
void ATUIRenderHUDOverlay();

// Fullscreen notification — shows a fading "Press Alt+Enter to exit" hint
void ATUIShowFullscreenNotification();
void ATUIRenderFullscreenNotification();

// Device configuration dialog (ui_devconfig.cpp)
class IATDevice;
class ATDeviceManager;
void ATUIOpenDeviceConfig(IATDevice *dev, ATDeviceManager *devMgr);
bool ATUIIsDeviceConfigOpen();
void ATUICloseDeviceConfigFor(IATDevice *dev); // close if open for this device
void ATUIRenderDeviceConfig(ATDeviceManager *devMgr);
// Begin adding a new device: opens the configure dialog with empty
// properties (so path/geometry/etc. can be provided) and, on apply,
// creates the device and attaches it to a compatible parent bus if
// one exists.  Falls back to direct creation for simple devices with
// no configure dialog.
void ATUIBeginAddDevice(ATDeviceManager *devMgr, const char *tag);

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

// Drag-and-drop visual feedback state (updated by event loop, drawn by UI)
struct ATUIDragDropState {
	bool active = false;      // true between DROP_BEGIN and DROP_COMPLETE/DROP_FILE
	float x = 0, y = 0;      // current cursor position during drag
};
extern ATUIDragDropState g_dragDropState;

// Render drag-and-drop overlay (call at end of frame, after all windows)
void ATUIRenderDragDropOverlay();

// Exit confirmation — checks for dirty storage and shows discard dialog.
// Set state.showExitConfirm = true to trigger; the render function handles
// building the message, showing the popup, and pushing SDL_EVENT_QUIT on OK.
void ATUIRenderExitConfirm(ATSimulator &sim, ATUIState &state);
