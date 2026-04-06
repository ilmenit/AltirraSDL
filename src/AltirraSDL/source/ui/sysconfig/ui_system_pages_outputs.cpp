//	AltirraSDL - System Configuration pages (split from ui_system_pages_a.cpp Phase 3f)

#include <stdafx.h>
#include <SDL3/SDL.h>
#include <imgui.h>
#include <vd2/system/vdtypes.h>
#include <vd2/system/VDString.h>
#include <vd2/system/text.h>
#include <vd2/system/filesys.h>
#include <vd2/system/unknown.h>
#include <vd2/system/file.h>

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
#include <algorithm>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>
#include <cstring>
#include "logging.h"

extern ATSimulator g_sim;
extern ATUIManager g_ATUIManager;
void ATUIUpdateSpeedTiming();
void ATUIResizeDisplay();
void ATSyncCPUHistoryState();

// =========================================================================
// Video (matches Windows IDD_CONFIGURE_VIDEO)
// =========================================================================

void RenderVideoCategory(ATSimulator &sim) {
	ATGTIAEmulator& gtia = sim.GetGTIA();

	ImGui::SeparatorText("Video effects");

	static const char *kArtifactLabels[] = {
		"None", "NTSC", "PAL", "NTSC High", "PAL High", "Auto", "Auto High"
	};
	int artifact = (int)gtia.GetArtifactingMode();
	if (artifact < 0 || artifact >= 7) artifact = 0;
	if (ImGui::Combo("Artifacting", &artifact, kArtifactLabels, 7))
		gtia.SetArtifactingMode((ATArtifactMode)artifact);
	ImGui::SetItemTooltip("Emulate false color effects derived from composite video encoding.");

	static const char *kMonitorLabels[] = {
		"Color", "Peritel", "Green Mono", "Amber Mono", "Blue-White Mono", "White Mono"
	};
	int monitor = (int)gtia.GetMonitorMode();
	if (monitor < 0 || monitor >= 6) monitor = 0;
	if (ImGui::Combo("Monitor Mode", &monitor, kMonitorLabels, 6))
		gtia.SetMonitorMode((ATMonitorMode)monitor);
	ImGui::SetItemTooltip("Selects the monitor (screen) type.");

	ImGui::Separator();

	bool blend = gtia.IsBlendModeEnabled();
	if (ImGui::Checkbox("Frame Blending", &blend))
		gtia.SetBlendModeEnabled(blend);
	ImGui::SetItemTooltip("Blend adjacent frames together to eliminate flickering from alternating frame techniques.");

	bool linearBlend = gtia.IsLinearBlendEnabled();
	if (ImGui::Checkbox("Linear Frame Blending", &linearBlend))
		gtia.SetLinearBlendEnabled(linearBlend);
	ImGui::SetItemTooltip("Use linear color blending for more accurate colors when frame blending.");

	bool monoPersist = gtia.IsBlendMonoPersistenceEnabled();
	if (ImGui::Checkbox("Mono Persistence", &monoPersist))
		gtia.SetBlendMonoPersistenceEnabled(monoPersist);
	ImGui::SetItemTooltip("Emulate phosphor persistence on monochrome monitors.");

	bool interlace = gtia.IsInterlaceEnabled();
	if (ImGui::Checkbox("Interlace", &interlace)) {
		gtia.SetInterlaceEnabled(interlace);
		ATUIResizeDisplay();
	}
	ImGui::SetItemTooltip("Enable support for displaying video as interlaced fields.");

	bool scanlines = gtia.AreScanlinesEnabled();
	if (ImGui::Checkbox("Scanlines", &scanlines)) {
		gtia.SetScanlinesEnabled(scanlines);
		ATUIResizeDisplay();
	}
	ImGui::SetItemTooltip("Darken video between scanlines to simulate CRT beam scanning.");

	ImGui::Separator();

	// PAL phase (matches Windows Video.PALPhase0/1 from cmds.cpp)
	int palPhase = gtia.GetPALPhase();
	static const char *kPALPhaseLabels[] = { "Phase 0 (standard)", "Phase 1 (alternate)" };
	if (palPhase < 0 || palPhase > 1) palPhase = 0;
	if (ImGui::Combo("PAL Phase", &palPhase, kPALPhaseLabels, 2))
		gtia.SetPALPhase(palPhase);
	ImGui::SetItemTooltip("Controls the V-phase of even and odd lines for PAL video output.");

	bool palExt = gtia.IsOverscanPALExtended();
	if (ImGui::Checkbox("Extended PAL Height", &palExt))
		gtia.SetOverscanPALExtended(palExt);
	ImGui::SetItemTooltip("Show additional scanlines visible in PAL mode.");
}

// =========================================================================
// Audio (matches Windows IDD_CONFIGURE_AUDIO)
// =========================================================================

void RenderAudioCategory(ATSimulator &sim, ATUIState &state) {
	ImGui::SeparatorText("Audio setup");

	IATAudioOutput *pAudio = sim.GetAudioOutput();
	if (pAudio) {
		bool muted = pAudio->GetMute();
		if (ImGui::Checkbox("Mute All", &muted))
			pAudio->SetMute(muted);
		ImGui::SetItemTooltip("Mute all audio output.");
	}

	bool dualPokey = sim.IsDualPokeysEnabled();
	if (ImGui::Checkbox("Stereo", &dualPokey))
		sim.SetDualPokeysEnabled(dualPokey);
	ImGui::SetItemTooltip("Enable emulation of two POKEYs, controlling the left and right channels.");

	ATPokeyEmulator& pokey = sim.GetPokey();

	bool stereoMono = pokey.IsStereoAsMonoEnabled();
	if (ImGui::Checkbox("Downmix stereo to mono", &stereoMono))
		pokey.SetStereoAsMonoEnabled(stereoMono);
	ImGui::SetItemTooltip("Downmix stereo audio from dual POKEYs to a single mono output.");

	bool nonlinear = pokey.IsNonlinearMixingEnabled();
	if (ImGui::Checkbox("Non-linear mixing", &nonlinear))
		pokey.SetNonlinearMixingEnabled(nonlinear);
	ImGui::SetItemTooltip("Emulate analog behavior where audio signal output is compressed at high levels.");

	bool serialNoise = pokey.IsSerialNoiseEnabled();
	if (ImGui::Checkbox("Serial noise", &serialNoise))
		pokey.SetSerialNoiseEnabled(serialNoise);
	ImGui::SetItemTooltip("Enable audio noise when serial transfers occur.");

	bool speaker = pokey.IsSpeakerFilterEnabled();
	if (ImGui::Checkbox("Simulate console speaker", &speaker))
		pokey.SetSpeakerFilterEnabled(speaker);
	ImGui::SetItemTooltip("Simulate the acoustics of the console speaker.");

	ImGui::SeparatorText("Enabled channels");

	// Primary POKEY channels 1-4
	for (int i = 0; i < 4; ++i) {
		char label[32];
		snprintf(label, sizeof(label), "%d", i + 1);
		bool ch = pokey.IsChannelEnabled(i);
		if (ImGui::Checkbox(label, &ch))
			pokey.SetChannelEnabled(i, ch);
		if (i < 3) ImGui::SameLine();
	}

	// Secondary POKEY channels (if stereo enabled)
	if (dualPokey) {
		for (int i = 0; i < 4; ++i) {
			char label[32];
			snprintf(label, sizeof(label), "%dR", i + 1);
			bool ch = pokey.IsSecondaryChannelEnabled(i);
			if (ImGui::Checkbox(label, &ch))
				pokey.SetSecondaryChannelEnabled(i, ch);
			if (i < 3) ImGui::SameLine();
		}
	}

	ImGui::Separator();

	bool driveSounds = ATUIGetDriveSoundsEnabled();
	if (ImGui::Checkbox("Drive Sounds", &driveSounds))
		ATUISetDriveSoundsEnabled(driveSounds);
	ImGui::SetItemTooltip("Simulate the sounds of a real disk drive.");

	bool audioMonitor = sim.IsAudioMonitorEnabled();
	if (ImGui::Checkbox("Audio monitor", &audioMonitor))
		sim.SetAudioMonitorEnabled(audioMonitor);
	ImGui::SetItemTooltip("Display real-time audio output monitor on screen.");

	bool audioScope = sim.IsAudioScopeEnabled();
	if (ImGui::Checkbox("Audio scope", &audioScope))
		sim.SetAudioScopeEnabled(audioScope);

	ImGui::Separator();
	if (ImGui::Button("Host audio options..."))
		state.showAudioOptions = true;
}

// =========================================================================
// Keyboard (matches Windows IDD_CONFIGURE_KEYBOARD)
// =========================================================================

// Declared in ui_keyboard_customize.cpp
void ATUIGetDefaultKeyMap(const ATUIKeyboardOptions& options, vdfastvector<uint32>& mappings);

void RenderKeyboardCategory(ATSimulator &, ATUIState &state) {
	// Matches Windows IDD_CONFIGURE_KEYBOARD
	extern ATUIKeyboardOptions g_kbdOpts;

	ImGui::SeparatorText("Keyboard mode");

	// Maps to Windows Input > Keyboard Mode (Cooked/Raw/Full Scan)
	// Cooked: mbRawKeys=false, mbFullRawKeys=false
	// Raw:    mbRawKeys=true,  mbFullRawKeys=false
	// Full:   mbRawKeys=true,  mbFullRawKeys=true
	static const char *kKeyboardModes[] = { "Cooked", "Raw", "Full Scan" };
	int kbdMode = 0;
	if (g_kbdOpts.mbFullRawKeys) kbdMode = 2;
	else if (g_kbdOpts.mbRawKeys) kbdMode = 1;
	if (ImGui::Combo("##KeyboardMode", &kbdMode, kKeyboardModes, 3)) {
		g_kbdOpts.mbRawKeys = (kbdMode >= 1);
		g_kbdOpts.mbFullRawKeys = (kbdMode == 2);
	}
	ImGui::SetItemTooltip("Control how keys are sent to the emulation.");

	ImGui::SeparatorText("Arrow key mode");

	static const char *kArrowModes[] = {
		"Invert Ctrl", "Auto Ctrl", "Default Ctrl"
	};
	int akm = (int)g_kbdOpts.mArrowKeyMode;
	if (akm < 0 || akm >= 3) akm = 0;
	if (ImGui::Combo("##ArrowKeyMode", &akm, kArrowModes, 3)) {
		g_kbdOpts.mArrowKeyMode = (ATUIKeyboardOptions::ArrowKeyMode)akm;
		ATUIInitVirtualKeyMap(g_kbdOpts);
	}
	ImGui::SetItemTooltip("Controls how arrow keys are mapped to the emulated keyboard.");

	ImGui::SeparatorText("Key press mode");

	static const char *kLayoutModes[] = {
		"Natural", "Raw", "Custom"
	};
	int lm = (int)g_kbdOpts.mLayoutMode;
	if (lm < 0 || lm >= 3) lm = 0;
	if (ImGui::Combo("##LayoutMode", &lm, kLayoutModes, 3)) {
		g_kbdOpts.mLayoutMode = (ATUIKeyboardOptions::LayoutMode)lm;
		ATUIInitVirtualKeyMap(g_kbdOpts);
	}
	ImGui::SetItemTooltip("Select mapping from host to emulated keyboard.");

	// "Copy Default Layout to Custom" — copies current default map as starting point
	if (g_kbdOpts.mLayoutMode != ATUIKeyboardOptions::kLM_Custom) {
		if (ImGui::Button("Copy Default Layout to Custom")) {
			vdfastvector<uint32> mappings;
			ATUIGetDefaultKeyMap(g_kbdOpts, mappings);
			ATUISetCustomKeyMap(mappings.data(), mappings.size());
			g_kbdOpts.mLayoutMode = ATUIKeyboardOptions::kLM_Custom;
			ATUIInitVirtualKeyMap(g_kbdOpts);
		}
		ImGui::SetItemTooltip("Copy the current default key layout to the custom map for editing.");
	}

	// "Customize..." — open the custom keyboard layout editor
	if (g_kbdOpts.mLayoutMode == ATUIKeyboardOptions::kLM_Custom) {
		if (ImGui::Button("Customize..."))
			state.showKeyboardCustomize = true;
		ImGui::SetItemTooltip("Open the custom keyboard layout editor.");
	}

	ImGui::Separator();

	if (ImGui::Checkbox("Allow SHIFT key to be detected on cold reset", &g_kbdOpts.mbAllowShiftOnColdReset))
		ATUIInitVirtualKeyMap(g_kbdOpts);
	ImGui::SetItemTooltip("Control whether the emulation detects the SHIFT key being held during cold reset.");

	if (ImGui::Checkbox("Enable F1-F4 as 1200XL function keys", &g_kbdOpts.mbEnableFunctionKeys))
		ATUIInitVirtualKeyMap(g_kbdOpts);
	ImGui::SetItemTooltip("Map F1-F4 in the default keyboard layouts to the four function keys on the 1200XL.");

	if (ImGui::Checkbox("Share modifier host keys between keyboard and input maps", &g_kbdOpts.mbAllowInputMapModifierOverlap))
		ATUIInitVirtualKeyMap(g_kbdOpts);
	ImGui::SetItemTooltip("Allow Ctrl/Shift keys to be shared between the keyboard handler and input maps.");

	if (ImGui::Checkbox("Share non-modifier host keys between keyboard and input maps", &g_kbdOpts.mbAllowInputMapOverlap))
		ATUIInitVirtualKeyMap(g_kbdOpts);
	ImGui::SetItemTooltip("Allow the same non-Ctrl/Shift key to be used by both the keyboard handler and input maps.");
}

// =========================================================================
// Disk (matches Windows IDD_CONFIGURE_DISK)
// =========================================================================

void RenderDiskCategory(ATSimulator &sim) {
	bool accurateTiming = sim.IsDiskAccurateTimingEnabled();
	if (ImGui::Checkbox("Accurate sector timing", &accurateTiming))
		sim.SetDiskAccurateTimingEnabled(accurateTiming);
	ImGui::SetItemTooltip("Emulate the seek times and rotational delays of a real disk drive.");

	bool driveSounds = ATUIGetDriveSoundsEnabled();
	if (ImGui::Checkbox("Play drive sounds", &driveSounds))
		ATUISetDriveSoundsEnabled(driveSounds);
	ImGui::SetItemTooltip("Simulate the sounds of a real disk drive.");

	bool sectorCounter = sim.IsDiskSectorCounterEnabled();
	if (ImGui::Checkbox("Show sector counter", &sectorCounter))
		sim.SetDiskSectorCounterEnabled(sectorCounter);
	ImGui::SetItemTooltip("During disk access, display the sector number being read or written.");
}

// =========================================================================
// Cassette (matches Windows IDD_CONFIGURE_CASSETTE)
// =========================================================================

void RenderCassetteCategory(ATSimulator &sim) {
	ATCassetteEmulator& cas = sim.GetCassette();

	ImGui::SeparatorText("Tape setup");

	bool autoBoot = sim.IsCassetteAutoBootEnabled();
	if (ImGui::Checkbox("Auto-boot on startup", &autoBoot))
		sim.SetCassetteAutoBootEnabled(autoBoot);
	ImGui::SetItemTooltip("Automatically hold down the Start button on power-up to boot from tape.");

	bool autoBasicBoot = sim.IsCassetteAutoBasicBootEnabled();
	if (ImGui::Checkbox("Auto-boot BASIC on startup", &autoBasicBoot))
		sim.SetCassetteAutoBasicBootEnabled(autoBasicBoot);
	ImGui::SetItemTooltip("Try to determine if the tape has a BASIC or binary program and auto-start accordingly.");

	bool autoRewind = sim.IsCassetteAutoRewindEnabled();
	if (ImGui::Checkbox("Auto-rewind on startup", &autoRewind))
		sim.SetCassetteAutoRewindEnabled(autoRewind);
	ImGui::SetItemTooltip("Automatically rewind the tape to the beginning on startup.");

	bool loadDataAsAudio = cas.IsLoadDataAsAudioEnabled();
	if (ImGui::Checkbox("Load data as audio", &loadDataAsAudio))
		cas.SetLoadDataAsAudioEnable(loadDataAsAudio);
	ImGui::SetItemTooltip("Play the data track as the audio track.");

	bool randomStart = sim.IsCassetteRandomizedStartEnabled();
	if (ImGui::Checkbox("Randomize starting position", &randomStart))
		sim.SetCassetteRandomizedStartEnabled(randomStart);
	ImGui::SetItemTooltip("Apply a slight jitter to the start position of the tape.");

	ImGui::SeparatorText("Turbo support");

	static const char *kTurboModes[] = {
		"None", "Command Control", "Proceed Sense",
		"Interrupt Sense", "KSO Turbo 2000", "Turbo D",
		"Data Control", "Always"
	};
	int turbo = (int)cas.GetTurboMode();
	if (turbo < 0 || turbo >= 8) turbo = 0;
	if (ImGui::Combo("Turbo mode", &turbo, kTurboModes, 8))
		cas.SetTurboMode((ATCassetteTurboMode)turbo);
	ImGui::SetItemTooltip("Select turbo tape hardware modification to support.");

	static const char *kTurboDecoders[] = {
		"Slope (No Filter)", "Slope (Filter)",
		"Peak (Filter)", "Peak (Balance Lo-Hi)", "Peak (Balance Hi-Lo)"
	};
	int decoder = (int)cas.GetTurboDecodeAlgorithm();
	if (decoder < 0 || decoder >= 5) decoder = 0;
	if (ImGui::Combo("Turbo decoder", &decoder, kTurboDecoders, 5))
		cas.SetTurboDecodeAlgorithm((ATCassetteTurboDecodeAlgorithm)decoder);
	ImGui::SetItemTooltip("Decoding algorithm to apply when decoding turbo data.");

	bool invertTurbo = cas.GetPolarityMode() == kATCassettePolarityMode_Inverted;
	if (ImGui::Checkbox("Invert turbo data", &invertTurbo))
		cas.SetPolarityMode(invertTurbo
			? kATCassettePolarityMode_Inverted
			: kATCassettePolarityMode_Normal);
	ImGui::SetItemTooltip("Invert the polarity of turbo data read by the computer.");

	ImGui::SeparatorText("Direct read filter");

	static const char *kDirectSenseModes[] = {
		"Normal", "Low Speed", "High Speed", "Max Speed"
	};
	int dsm = (int)cas.GetDirectSenseMode();
	if (dsm < 0 || dsm >= 4) dsm = 0;
	if (ImGui::Combo("##DirectReadFilter", &dsm, kDirectSenseModes, 4))
		cas.SetDirectSenseMode((ATCassetteDirectSenseMode)dsm);
	ImGui::SetItemTooltip("Selects the bandwidth of filter used for FSK direct read decoding.");

	ImGui::SeparatorText("Workarounds");

	bool vbiAvoid = cas.IsVBIAvoidanceEnabled();
	if (ImGui::Checkbox("Avoid OS C: random VBI-related errors", &vbiAvoid))
		cas.SetVBIAvoidanceEnabled(vbiAvoid);
	ImGui::SetItemTooltip("Latch cassette data across the start of vertical blank to avoid random read errors.");

	ImGui::SeparatorText("Pre-filtering");

	bool fskComp = cas.GetFSKSpeedCompensationEnabled();
	if (ImGui::Checkbox("Enable FSK speed compensation", &fskComp))
		cas.SetFSKSpeedCompensationEnabled(fskComp);
	ImGui::SetItemTooltip("Correct for speed variation on the tape.");

	bool crosstalk = cas.GetCrosstalkReductionEnabled();
	if (ImGui::Checkbox("Enable crosstalk reduction", &crosstalk))
		cas.SetCrosstalkReductionEnabled(crosstalk);
	ImGui::SetItemTooltip("Reduce crosstalk leakage from the data track into the audio track.");
}

// =========================================================================
// Display (matches Windows IDD_CONFIGURE_DISPLAY)
// =========================================================================

void RenderDisplayCategory(ATSimulator &) {
	// Matches Windows IDD_CONFIGURE_DISPLAY — pointer/indicator settings
	// Note: Filter mode, stretch mode, overscan are View menu items, not here.

	bool autoHide = ATUIGetPointerAutoHide();
	if (ImGui::Checkbox("Auto-hide mouse pointer after short delay", &autoHide))
		ATUISetPointerAutoHide(autoHide);
	ImGui::SetItemTooltip("Automatically hide the mouse pointer after a short delay.");

	bool constrainFS = ATUIGetConstrainMouseFullScreen();
	if (ImGui::Checkbox("Constrain mouse pointer in full-screen mode", &constrainFS))
		ATUISetConstrainMouseFullScreen(constrainFS);
	ImGui::SetItemTooltip("Restrict pointer movement to the emulator window in full-screen mode.");

	bool hideTarget = !ATUIGetTargetPointerVisible();
	if (ImGui::Checkbox("Hide target pointer for absolute mouse input (light pen/gun/tablet)", &hideTarget))
		ATUISetTargetPointerVisible(!hideTarget);
	ImGui::SetItemTooltip("Hide the target reticle pointer for absolute mouse input (light pen/gun/tablet).");

	ImGui::Separator();

	bool indicators = ATUIGetDisplayIndicators();
	if (ImGui::Checkbox("Show indicators", &indicators))
		ATUISetDisplayIndicators(indicators);
	ImGui::SetItemTooltip("Draw on-screen overlays for device status.");

	bool padIndicators = ATUIGetDisplayPadIndicators();
	if (ImGui::Checkbox("Pad bottom margin to reserve space for indicators", &padIndicators))
		ATUISetDisplayPadIndicators(padIndicators);
	ImGui::SetItemTooltip("Move the display up to reserve space for indicators at the bottom.");

	bool padBounds = ATUIGetDrawPadBoundsEnabled();
	if (ImGui::Checkbox("Show tablet/pad bounds", &padBounds))
		ATUISetDrawPadBoundsEnabled(padBounds);
	ImGui::SetItemTooltip("Show a rectangle for the on-screen input area.");

	bool padPointers = ATUIGetDrawPadPointersEnabled();
	if (ImGui::Checkbox("Show tablet/pad pointers", &padPointers))
		ATUISetDrawPadPointersEnabled(padPointers);
	ImGui::SetItemTooltip("Show the location and size of tablet/pad touch points.");
}

// =========================================================================
// Input (matches Windows IDD_CONFIGURE_INPUT)
// =========================================================================

void RenderInputCategory(ATSimulator &sim) {
	ATPokeyEmulator& pokey = sim.GetPokey();

	bool potNoise = sim.GetPotNoiseEnabled();
	if (ImGui::Checkbox("Enable paddle potentiometer noise", &potNoise))
		sim.SetPotNoiseEnabled(potNoise);
	ImGui::SetItemTooltip("Jitter paddle inputs to simulate a dirty paddle.");

	bool immPots = pokey.IsImmediatePotUpdateEnabled();
	if (ImGui::Checkbox("Use immediate analog updates", &immPots))
		pokey.SetImmediatePotUpdateEnabled(immPots);
	ImGui::SetItemTooltip("Allow paddle position registers to update immediately rather than waiting for scan.");

	bool immLightPen = sim.GetLightPenPort()->GetImmediateUpdateEnabled();
	if (ImGui::Checkbox("Use immediate light pen updates", &immLightPen))
		sim.GetLightPenPort()->SetImmediateUpdateEnabled(immLightPen);
	ImGui::SetItemTooltip("Allow light pen position registers to update immediately.");
}

// =========================================================================
// Ease of Use (matches Windows IDD_CONFIGURE_EASEOFUSE)
// =========================================================================

void RenderEaseOfUseCategory(ATSimulator &) {
	// Matches Windows IDD_CONFIGURE_EASEOFUSE
	uint32 flags = ATUIGetResetFlags();

	bool resetCart = (flags & kATUIResetFlag_CartridgeChange) != 0;
	if (ImGui::Checkbox("Reset when changing cartridges", &resetCart))
		ATUIModifyResetFlag(kATUIResetFlag_CartridgeChange, resetCart);
	ImGui::SetItemTooltip("Reset when adding or removing a cartridge.");

	bool resetVS = (flags & kATUIResetFlag_VideoStandardChange) != 0;
	if (ImGui::Checkbox("Reset when changing video standard", &resetVS))
		ATUIModifyResetFlag(kATUIResetFlag_VideoStandardChange, resetVS);
	ImGui::SetItemTooltip("Reset when changing between NTSC/PAL/SECAM.");

	bool resetBasic = (flags & kATUIResetFlag_BasicChange) != 0;
	if (ImGui::Checkbox("Reset when toggling internal BASIC", &resetBasic))
		ATUIModifyResetFlag(kATUIResetFlag_BasicChange, resetBasic);
	ImGui::SetItemTooltip("Reset when enabling or disabling internal BASIC.");
}
