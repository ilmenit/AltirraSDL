//	AltirraSDL - First Time Setup Wizard - shared state and helpers
//
//	The wizard has two renderers — a Desktop ImGui window
//	(ATUIRenderSetupWizard in ui_tools_setup_wizard.cpp) and a Gaming
//	Mode full-screen panel (RenderMobileSetupWizard in
//	mobile_setup_wizard.cpp).  Both renderers operate on the same
//	state (page index, async results, etc.) declared here so a user
//	who toggles modes on page 1 keeps their progress.

#pragma once

#include <mutex>
#include <string>
#include <vector>
#include <vd2/system/vdtypes.h>
#include <vd2/system/VDString.h>

class ATSimulator;
struct ATUIState;
struct SDL_Window;
enum class ATUIMode : uint8;

struct SetupWizardState {
	int page = 0;
	bool wentPastFirst = false;
	int scanFound = 0;
	int scanExisting = 0;
	VDStringA scanMessage;

	// True if the user touched a setting that requires LoadROMs() +
	// ColdReset() to take effect: firmware scan, hardware mode, video
	// standard, or experience profile (which adjusts SIO patches and
	// disk timing).  Wiz_Finish only resets the simulator when this is
	// set, so a wizard run that only touches Theme / Game Library /
	// Joystick mappings doesn't pointlessly throw away the user's game.
	bool needsHardwareReset = false;

	// True once the joystick page (35) has auto-activated the
	// hardware-appropriate default for port 1 in this session.  Cleared
	// by Reset() so a fresh wizard session re-seeds.
	bool joystickPageSeeded = false;

	// True once the Hardware Add-ons page (32) has applied its
	// experience-driven seed.  Without this guard the seed would re-run
	// every frame the page is displayed, undoing user toggles.
	bool addonsPageSeeded = false;

	// Thread-safe: paths stored by SDL file-dialog callbacks (which fire
	// on background threads), processed on the main thread by Wiz_PumpAsync.
	std::mutex scanMutex;
	std::string pendingScanPath;
	std::string pendingLibFolderPath;

	void Reset();
};

extern SetupWizardState g_setupWiz;

// Page navigation.  Both helpers consult the live ATUIIsGamingMode() so
// page 6 (desktop "Screen Effects") is automatically skipped in Gaming
// Mode, where the merged Appearance page already covers it through the
// performance preset.
int  Wiz_GetPrevPage(int page);
int  Wiz_GetNextPage(int page);

// Drain async results from SDL file dialog callbacks.  Must run on the
// main thread before page rendering — both renderers call this at the
// top of their entry point.
void Wiz_PumpAsync();

// Trigger the OS folder pickers used by pages 2 (Game Library) and 10
// (Firmware).  On WASM the SDL folder picker is unavailable, so these
// helpers seed the pending path slot directly with the well-known
// uploads directory; on Linux/macOS/Windows they call SDL_ShowOpenFolderDialog.
void Wiz_TriggerFirmwareScan(SDL_Window *window);
void Wiz_TriggerLibFolderPicker(SDL_Window *window);

// Open the wizard from a UI trigger (Tools menu or first-launch).
// Sets state.showSetupWizard and, if currently in Gaming Mode, also
// sets the mobile screen so the Gaming Mode renderer takes over.
void Wiz_Open(ATUIState &state);

// Live mode swap on page 1.  Applies the new UI mode immediately so the
// wizard re-renders in the new style next frame.  The mode is NOT
// persisted to disk here — Wiz_Finish writes ATUISaveMode() on close so
// a wizard cancelled mid-toggle still leaves the user in their final
// chosen mode (matching the behaviour where Close, X, ESC and Finish
// are all "commit" exits — there is no Cancel path).
void Wiz_ApplyMode(ATUIMode newMode, SDL_Window *window);

// Close path shared by both renderers.  Performs LoadROMs+ColdReset
// (only if the user moved past page 0, matching the existing
// wentPastFirst guard), persists the current UI mode, switches Gaming
// Mode to the Game Library home screen, resets wizard state, and clears
// state.showSetupWizard.
void Wiz_Finish(ATSimulator &sim, ATUIState &state, SDL_Window *window);

// Utility shared by both renderers — runs the firmware scan on the
// main thread.  Defined in ui_tools_setup_wizard.cpp.
void ATUIDoFirmwareScan(const char *utf8path);

// Joystick page (35) helpers.  Both renderers list the input maps that
// touch physical port 1 and let the user pick one as a radio-style
// selection (matching Input > Port 1).  When the user reaches the page
// and no port-1 map is currently active, Wiz_SeedDefaultPort1Map
// activates the canonical "Arrow Keys -> Joystick (port 1)" map so
// pressing Next is a one-click confirmation of a sensible default.
class ATInputManager;
class ATInputMap;

struct WizPortMapEntry {
	ATInputMap *map;
	VDStringA  name;   // UTF-8
	bool       active;
};

// Collect all input maps that target physical port `portIdx` (0-based),
// alphabetically sorted, with `active` set per current activation.
void Wiz_GatherPortMaps(ATInputManager &im, int portIdx,
	std::vector<WizPortMapEntry> &outEntries);

// Activate `chosen` for `portIdx`, deactivating all other maps that
// touch the same port (radio behaviour, matches Input > Port submenu).
void Wiz_ActivatePortMap(ATInputManager &im,
	const std::vector<WizPortMapEntry> &entries, ATInputMap *chosen);

// If no map is currently active for `portIdx`, pick the canonical
// default for the active hardware mode and activate it: "Arrow Keys ->
// Joystick (port 1)" for computer hardware, "Keyboard -> 5200
// Controller (absolute; port 1)" for the 5200.  Falls back to any
// "Arrow"-named map otherwise.  Called once when the user first lands
// on the joystick page; safe no-op when a map is already active or no
// suitable default exists.
void Wiz_SeedDefaultPort1Map(ATInputManager &im,
	std::vector<WizPortMapEntry> &entries);

// =========================================================================
// Hardware Add-ons page (32) — recommended expansions
// =========================================================================
//
// The four toggles ("Stereo POKEY", "Covox", "VBXE", "1088 KB RAM")
// each map to a single helper.  The helpers are shared by the Desktop
// and Gaming Mode renderers, by the shortened mobile FirstRunWizard
// in mobile_about_wizard.cpp, and by the WASM/Android startup-time
// silent first-run apply in main_sdl3.cpp — all routes converge on
// Wiz_ApplyHardwareAddons below.
//
// Each setter sets g_setupWiz.needsHardwareReset = true, since adding
// or removing devices, banking RAM, or installing a second POKEY all
// require LoadROMs+ColdReset to take effect cleanly.

bool Wiz_HasDualPokey(ATSimulator &sim);
void Wiz_SetDualPokey(ATSimulator &sim, bool enable);

bool Wiz_HasMemory1088K(ATSimulator &sim);
void Wiz_SetMemory1088K(ATSimulator &sim, bool enable);

bool Wiz_HasVBXE(ATSimulator &sim);
void Wiz_SetVBXE(ATSimulator &sim, bool enable);

bool Wiz_HasCovox(ATSimulator &sim);
void Wiz_SetCovox(ATSimulator &sim, bool enable);

// Single-axis helpers — apply ONLY the emulation experience preset
// (artifacting, SIO patches, drive sounds, accurate-disk timing,
// display filter, sharpness).  Mirrors the wizard's Experience radio
// at page 30.  Independent of hardware add-ons — composable with any
// hardware configuration.  Skipped silently for 5200 hardware.
//
// `deferReset=true` skips the trailing LoadROMs()+ColdReset() so a
// caller (e.g. main_sdl3.cpp's startup-time silent apply) can batch
// the reset with its own one.  Default false matches the wizard's
// in-app behaviour where each radio click expects an immediate reset.
void Wiz_ApplyConvenientExperience(ATSimulator &sim, bool deferReset = false);
void Wiz_ApplyAuthenticExperience (ATSimulator &sim, bool deferReset = false);

// Single-axis helper — apply ONLY the four hardware add-ons (VBXE,
// Covox, Stereo POKEY, 1088 KB RAM) as one batch.  Mirrors the
// wizard's "Hardware add-ons" page 32.  Skipped silently for 5200
// (none of the four apply there).
//
// enable=true:  add VBXE 1.26 + Covox $D600/4 ch + Stereo POKEY,
//               set memory mode to 1088 K.
// enable=false: actively REMOVE VBXE/Covox/Stereo POKEY if present
//               (so a `?addons=off` URL param yields a predictable
//               "stock" hardware config regardless of what the loaded
//               profile shipped with).  Memory mode is LEFT ALONE so
//               the user's --memsize / profile choice survives.
//
// Same `deferReset=true` semantics as the experience helpers.
void Wiz_ApplyHardwareAddons(ATSimulator &sim, bool enable, bool deferReset = false);

// Seed the wizard page 32 toggles when the user first lands on it.
// If Convenient mode is currently active, enable any add-ons that are
// off (matches the spirit of "convenient = compatibility-first").  If
// Authentic, leave everything as-is.  Never reduces existing config.
// Called once per wizard session, gated by addonsPageSeeded.
void Wiz_SeedHardwareAddonsPage(ATSimulator &sim);
