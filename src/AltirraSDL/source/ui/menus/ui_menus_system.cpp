//	AltirraSDL - System menu
//	Split out of ui_menus.cpp (Phase 2c).

#include <stdafx.h>
#include <imgui.h>
#include <vd2/system/vdtypes.h>

#include "ui_main.h"
#include "ui_menus_internal.h"
#include "simulator.h"
#include "autosavemanager.h"
#include "uiaccessors.h"
#include "uiconfirm.h"
#include "uikeyboard.h"
#include "accel_sdl3.h"
#include "settings.h"

#ifdef ALTIRRA_NETPLAY_ENABLED
#include "netplay/netplay_glue.h"
#include "ui/netplay/ui_netplay.h"
#endif

extern ATSimulator g_sim;
extern ATUIKeyboardOptions g_kbdOpts;

// The original file had a file-local helper ShortcutContextMenu; we use
// the public one declared in ui_menus_internal.h.
static inline void ShortcutContextMenu(const char *command) {
	ATUIMenuShortcutContextMenu(command);
}

// --- body extracted from ui_menus.cpp -------------------------------------
void ATUIRenderSystemMenu(ATSimulator &sim, ATUIState &state) {
#ifdef ALTIRRA_NETPLAY_ENABLED
	// Gate menu disables on "peer is actually engaged", NOT on "any
	// coordinator exists".  Merely hosting (WaitingForJoiner) leaves
	// the user free to reset, configure, switch profiles, etc. — the
	// constraints only kick in once a peer is past Handshaking and
	// actions like Cold Reset would diverge the lockstep.
	const bool netplayActiveTop = ATNetplayGlue::IsSessionEngaged();
#else
	const bool netplayActiveTop = false;
#endif

	// Profiles submenu (matches Windows System > Profiles).  Disabled
	// while Online Play is active — the canonical Online Play profile
	// must not be mutated mid-session, and switching out of it would
	// abandon the active session in an inconsistent state.
	if (netplayActiveTop) {
		ImGui::BeginDisabled();
		ImGui::MenuItem("Profiles (disabled: Playing Online)");
		ImGui::EndDisabled();
	} else if (ImGui::BeginMenu("Profiles")) {
		if (ImGui::MenuItem("Edit Profiles..."))
			state.showProfiles = true;

		bool temporary = ATSettingsGetTemporaryProfileMode();
		if (ImGui::MenuItem("Temporary Profile", nullptr, temporary))
			ATSettingsSetTemporaryProfileMode(!temporary);

		ImGui::Separator();

		// Quick profile switching — list visible profiles
		uint32 currentId = ATSettingsGetCurrentProfileId();

		// Global profile
		{
			VDStringW name = ATSettingsProfileGetName(0);
			VDStringA nameU8 = VDTextWToU8(name);
			if (ImGui::MenuItem(nameU8.c_str(), nullptr, currentId == 0)) {
				if (currentId != 0) {
					ATSettingsSwitchProfile(0);
					sim.Resume();
				}
			}
		}

		// Enumerated profiles (visible only)
		vdfastvector<uint32> profileIds;
		ATSettingsProfileEnum(profileIds);
		for (uint32 id : profileIds) {
			if (!ATSettingsProfileGetVisible(id))
				continue;
			VDStringW name = ATSettingsProfileGetName(id);
			VDStringA nameU8 = VDTextWToU8(name);
			if (ImGui::MenuItem(nameU8.c_str(), nullptr, currentId == id)) {
				if (currentId != id) {
					ATSettingsSwitchProfile(id);
					sim.Resume();
				}
			}
		}

		ImGui::EndMenu();
	}

	// Configure System dialog likewise edits settings the canonical
	// Online Play profile pins — disable to prevent mid-session
	// configuration drift.
	if (netplayActiveTop) {
		ImGui::BeginDisabled();
		ImGui::MenuItem("Configure System... (disabled: Playing Online)");
		ImGui::EndDisabled();
	} else {
		if (ImGui::MenuItem("Configure System...", ATUIGetShortcutStringForCommand("System.Configure")))
			state.showSystemConfig = true;
		ShortcutContextMenu("System.Configure");
	}

	ImGui::Separator();

	// Warm/Cold Reset are sim-mutating actions that would diverge a
	// netplay session.  Items stay enabled and clickable; if a session
	// is active, ATNetplayUI_TryConfirmResetEndsSession queues a
	// confirmation that ends the session before resetting.  When no
	// session is live the click runs the reset directly.  Same wiring
	// for the F5 / Shift+F5 accelerators (commands_sdl3.cpp).
	if (ImGui::MenuItem("Warm Reset", ATUIGetShortcutStringForCommand("System.WarmReset"))) {
		auto doReset = [&sim]{
			sim.WarmReset();
			sim.Resume();
		};
#ifdef ALTIRRA_NETPLAY_ENABLED
		if (!ATNetplayUI_TryConfirmResetEndsSession("Warm Reset", doReset))
#endif
			doReset();
	}
	ShortcutContextMenu("System.WarmReset");
	if (ImGui::MenuItem("Cold Reset", ATUIGetShortcutStringForCommand("System.ColdReset"))) {
		auto doReset = [&sim]{
			sim.ColdReset();
			sim.Resume();
			if (!g_kbdOpts.mbAllowShiftOnColdReset)
				sim.GetPokey().SetShiftKeyState(false, true);
		};
#ifdef ALTIRRA_NETPLAY_ENABLED
		if (!ATNetplayUI_TryConfirmResetEndsSession("Cold Reset", doReset))
#endif
			doReset();
	}
	if (ImGui::MenuItem("Cold Reset (Computer Only)")) {
		auto doReset = [&sim]{
			sim.ColdResetComputerOnly();
			sim.Resume();
			if (!g_kbdOpts.mbAllowShiftOnColdReset)
				sim.GetPokey().SetShiftKeyState(false, true);
		};
#ifdef ALTIRRA_NETPLAY_ENABLED
		if (!ATNetplayUI_TryConfirmResetEndsSession(
				"Cold Reset (Computer Only)", doReset))
#endif
			doReset();
	}
	ShortcutContextMenu("System.ColdReset");

	bool paused = sim.IsPaused();
	if (netplayActiveTop) {
		ImGui::BeginDisabled();
		ImGui::MenuItem("Pause (disabled: Playing Online)", nullptr, paused);
		ImGui::EndDisabled();
	} else {
		if (ImGui::MenuItem("Pause", ATUIGetShortcutStringForCommand("System.TogglePause"), paused)) {
			if (paused) sim.Resume(); else sim.Pause();
		}
		ShortcutContextMenu("System.TogglePause");
	}

	ImGui::Separator();

	bool turbo = ATUIGetTurbo();
#ifdef ALTIRRA_NETPLAY_ENABLED
	// Same engagement gate as netplayActiveTop above — Warp Speed and
	// Pause-When-Inactive only need to be locked down once a peer is
	// engaged in lockstep, not while the host is still waiting for a
	// joiner.
	const bool netplayActive = ATNetplayGlue::IsSessionEngaged();
#else
	const bool netplayActive = false;
#endif
	if (netplayActive) {
		// Warp is forced off during online play (would run this peer
		// faster than the other and blow up lockstep).  Show disabled.
		ImGui::BeginDisabled();
		bool off = false;
		ImGui::MenuItem("Warp Speed (disabled: Playing Online)",
			nullptr, &off);
		ImGui::EndDisabled();
	} else if (ImGui::MenuItem("Warp Speed", nullptr, turbo)) {
		ATUISetTurbo(!turbo);
	}

	bool pauseInactive = ATUIGetPauseWhenInactive();
	if (netplayActive) {
		// Force-disabled during netplay: stalling the sim would stall
		// the lockstep pipeline on the other peer.  Grey the checkbox
		// and show the effective state ("off"), but keep the user's
		// saved preference untouched so it restores when the session
		// ends.
		ImGui::BeginDisabled();
		bool off = false;
		ImGui::MenuItem("Pause When Inactive (disabled: Playing Online)",
			nullptr, &off);
		ImGui::EndDisabled();
	} else if (ImGui::MenuItem("Pause When Inactive", nullptr, pauseInactive)) {
		ATUISetPauseWhenInactive(!pauseInactive);
	}

	// Rewind submenu — disabled during netplay because rewinding
	// applies a previous savestate to the running sim, which would
	// jump our sim to a different frame than the peer's and instantly
	// desync.  The pre-session restore handled by
	// ATNetplayProfile::EndSession is a separate (cleaner) mechanism.
	if (netplayActiveTop) {
		ImGui::BeginDisabled();
		ImGui::MenuItem("Rewind (disabled: Playing Online)");
		ImGui::EndDisabled();
	} else if (ImGui::BeginMenu("Rewind")) {
		IATAutoSaveManager &mgr = sim.GetAutoSaveManager();
		bool rewindEnabled = mgr.GetRewindEnabled();

		if (ImGui::MenuItem("Quick Rewind", nullptr, false, rewindEnabled))
			ATUIQuickRewind();
		if (ImGui::MenuItem("Rewind...", nullptr, false, rewindEnabled)) {
			if (ATUIOpenRewindDialog())
				state.showRewind = true;
		}
		ImGui::Separator();
		if (ImGui::MenuItem("Enable Rewind Recording", nullptr, rewindEnabled))
			mgr.SetRewindEnabled(!rewindEnabled);
		ImGui::EndMenu();
	}

	ImGui::Separator();

	// Power-On Delay, Internal BASIC, and Auto-Boot Tape mutate
	// canonical-pinned simulator state — disable while online.
	if (netplayActiveTop) {
		ImGui::BeginDisabled();
		ImGui::MenuItem("Power-On Delay (disabled: Playing Online)");
		ImGui::EndDisabled();
	} else if (ImGui::BeginMenu("Power-On Delay")) {
		int delay = sim.GetPowerOnDelay();
		if (ImGui::MenuItem("Auto", nullptr, delay < 0))
			sim.SetPowerOnDelay(-1);
		if (ImGui::MenuItem("None", nullptr, delay == 0))
			sim.SetPowerOnDelay(0);
		if (ImGui::MenuItem("1 Second", nullptr, delay == 10))
			sim.SetPowerOnDelay(10);
		if (ImGui::MenuItem("2 Seconds", nullptr, delay == 20))
			sim.SetPowerOnDelay(20);
		if (ImGui::MenuItem("3 Seconds", nullptr, delay == 30))
			sim.SetPowerOnDelay(30);
		ImGui::EndMenu();
	}

	if (ImGui::MenuItem("Hold Keys For Reset"))
		ATUIToggleHoldKeys();

	bool basic = sim.IsBASICEnabled();
	if (netplayActiveTop) {
		ImGui::BeginDisabled();
		ImGui::MenuItem("Internal BASIC (disabled: Playing Online)",
			nullptr, basic);
		ImGui::EndDisabled();
	} else if (ImGui::MenuItem("Internal BASIC (Boot Without Option Key)", nullptr, basic)) {
		sim.SetBASICEnabled(!basic);
		if (ATUIIsResetNeeded(kATUIResetFlag_BasicChange))
			sim.ColdReset();
	}

	bool casAutoBoot = sim.IsCassetteAutoBootEnabled();
	if (netplayActiveTop) {
		ImGui::BeginDisabled();
		ImGui::MenuItem("Auto-Boot Tape (disabled: Playing Online)",
			nullptr, casAutoBoot);
		ImGui::EndDisabled();
	} else if (ImGui::MenuItem("Auto-Boot Tape (Hold Start)", nullptr, casAutoBoot))
		sim.SetCassetteAutoBootEnabled(!casAutoBoot);

	ImGui::Separator();

	// Console Switches submenu (matches Windows menu_default.txt exactly).
	// Disabled during netplay — Keyboard Present / Force Self-Test /
	// Cart Switch all mutate hashed simulator state.  Device buttons
	// (BlackBox, IDE Plus 2, Indus GT etc.) often press hardware
	// buttons on attached devices that aren't present in the
	// canonical netplay profile anyway, so the items are also
	// suppressed.
	if (netplayActiveTop) {
		ImGui::BeginDisabled();
		ImGui::MenuItem("Console Switches (disabled: Playing Online)");
		ImGui::EndDisabled();
	} else if (ImGui::BeginMenu("Console Switches")) {
		bool kbdPresent = sim.IsKeyboardPresent();
		if (ImGui::MenuItem("Keyboard Present (XEGS)", nullptr, kbdPresent))
			sim.SetKeyboardPresent(!kbdPresent);

		bool selfTest = sim.IsForcedSelfTest();
		if (ImGui::MenuItem("Force Self-Test", nullptr, selfTest))
			sim.SetForcedSelfTest(!selfTest);

		// "Activate Cart Menu Button" — momentary action (matches Windows Cart.ActivateMenuButton)
		if (ImGui::MenuItem("Activate Cart Menu Button"))
			ATUIActivateDeviceButton(kATDeviceButton_CartridgeResetBank, true);

		// "Enable Cart Switch" — toggle (matches Windows Cart.ToggleSwitch)
		bool cartSwitch = sim.GetCartridgeSwitch();
		if (ImGui::MenuItem("Enable Cart Switch", nullptr, cartSwitch))
			sim.SetCartridgeSwitch(!cartSwitch);

		// Device buttons (shown only when the device is present)
		static const struct { ATDeviceButton btn; const char *label; } kDevButtons[] = {
			{ kATDeviceButton_BlackBoxDumpScreen, "BlackBox: Dump Screen" },
			{ kATDeviceButton_BlackBoxMenu, "BlackBox: Menu" },
			{ kATDeviceButton_IDEPlus2SwitchDisks, "IDE Plus 2.0: Switch Disks" },
			{ kATDeviceButton_IDEPlus2WriteProtect, "IDE Plus 2.0: Write Protect" },
			{ kATDeviceButton_IDEPlus2SDX, "IDE Plus 2.0: SDX Enable" },
			{ kATDeviceButton_IndusGTError, "Indus GT: Error Button" },
			{ kATDeviceButton_IndusGTTrack, "Indus GT: Track Button" },
			{ kATDeviceButton_IndusGTId, "Indus GT: Drive Type Button" },
			{ kATDeviceButton_IndusGTBootCPM, "Indus GT: Boot CP/M" },
			{ kATDeviceButton_IndusGTChangeDensity, "Indus GT: Change Density" },
			{ kATDeviceButton_HappySlow, "Happy: Slow Switch" },
			{ kATDeviceButton_HappyWPEnable, "Happy 1050: Write protect disk" },
			{ kATDeviceButton_HappyWPDisable, "Happy 1050: Write enable disk" },
			{ kATDeviceButton_ATR8000Reset, "ATR8000: Reset" },
			{ kATDeviceButton_XELCFSwap, "XEL-CF3: Swap" },
		};

		bool anyDevBtn = false;
		for (auto& db : kDevButtons) {
			if (ATUIGetDeviceButtonSupported((uint32)db.btn)) {
				if (!anyDevBtn) {
					ImGui::Separator();
					anyDevBtn = true;
				}
				bool dep = ATUIGetDeviceButtonDepressed((uint32)db.btn);
				if (ImGui::MenuItem(db.label, nullptr, dep))
					ATUIActivateDeviceButton((uint32)db.btn, !dep);
			}
		}
		ImGui::EndMenu();
	}

}
