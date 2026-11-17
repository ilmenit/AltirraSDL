//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2015 Avery Lee
//
//	This program is free software; you can redistribute it and/or modify
//	it under the terms of the GNU General Public License as published by
//	the Free Software Foundation; either version 2 of the License, or
//	(at your option) any later version.
//
//	This program is distributed in the hope that it will be useful,
//	but WITHOUT ANY WARRANTY; without even the implied warranty of
//	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//	GNU General Public License for more details.
//
//	You should have received a copy of the GNU General Public License
//	along with this program; if not, write to the Free Software
//	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

#include <stdafx.h>
#include <at/atcore/propertyset.h>
#include <at/atnativeui/uiframe.h>
#include <at/atnativeui/uipane.h>
#include "autosavemanager.h"
#include "cmdhelpers.h"
#include "console.h"
#include "devicemanager.h"
#include "firmwaremanager.h"
#include "simulator.h"
#include "uiaccessors.h"
#include "uiconfirm.h"
#include "uidisplay.h"
#include "uikeyboard.h"
#include "uimenu.h"
#include "uitypes.h"

void ATUIShowCPUOptionsDialog(VDGUIHandle h);
void ATUIShowDialogSpeedOptions(VDGUIHandle parent);
void ATUIShowDialogDevices(VDGUIHandle parent);
void ATUIShowDialogFirmware(VDGUIHandle hParent, ATFirmwareManager& fwm, bool *anyChanges);
void ATUIShowDialogProfiles(VDGUIHandle hParent);
void ATUIOpenAdjustColorsDialog(VDGUIHandle hParent);
void ATUIOpenAdjustScreenEffectsDialog(VDGUIHandle hParent);
void ATUIShowDialogConfigureSystem(VDGUIHandle hParent);

void ATSyncCPUHistoryState();
void ATUIUpdateSpeedTiming();
void ATUISwitchKernel(uint64 id);
void ATUISwitchBasic(uint64 basicId);
void ATUIResizeDisplay();

extern ATSimulator g_sim;
extern ATUIKeyboardOptions g_kbdOpts;

void OnCommandSystemTogglePause() {
	if (g_sim.IsRunning())
		g_sim.Pause();
	else
		g_sim.Resume();
}

void OnCommandSystemWarmReset() {
	g_sim.WarmReset();
	g_sim.Resume();
}

void OnCommandSystemColdReset() {
	g_sim.ColdReset();
	g_sim.Resume();

	if (!g_kbdOpts.mbAllowShiftOnColdReset)
		g_sim.GetPokey().SetShiftKeyState(false, true);
}

void OnCommandSystemColdResetComputerOnly() {
	g_sim.ColdResetComputerOnly();
	g_sim.Resume();

	if (!g_kbdOpts.mbAllowShiftOnColdReset)
		g_sim.GetPokey().SetShiftKeyState(false, true);
}

void OnCommandSystemTogglePauseWhenInactive() {
	ATUISetPauseWhenInactive(!ATUIGetPauseWhenInactive());
}

void OnCommandSystemToggleSlowMotion() {
	ATUISetSlowMotion(!ATUIGetSlowMotion());
}

void OnCommandSystemToggleWarpSpeed() {
	ATUISetTurbo(!ATUIGetTurbo());
}

void OnCommandSystemPulseWarpOn() {
	ATUISetTurboPulse(true);
}

void OnCommandSystemPulseWarpOff() {
	ATUISetTurboPulse(false);
}

void OnCommandSystemCPUOptionsDialog() {
	ATUIShowCPUOptionsDialog(ATUIGetNewPopupOwner());
	ATSyncCPUHistoryState();
}

void OnCommandSystemToggleFPPatch() {
	g_sim.SetFPPatchEnabled(!g_sim.IsFPPatchEnabled());
}

void OnCommandSystemSpeedOptionsDialog() {
	ATUIShowDialogSpeedOptions(ATUIGetNewPopupOwner());
}

void OnCommandSystemDevicesDialog() {
	ATUIShowDialogDevices(ATUIGetNewPopupOwner());
}

void OnCommandSystemToggleKeyboardPresent() {
	g_sim.SetKeyboardPresent(!g_sim.IsKeyboardPresent());
}

void OnCommandSystemToggleForcedSelfTest() {
	g_sim.SetForcedSelfTest(!g_sim.IsForcedSelfTest());
}

void OnCommandSystemHardwareMode(ATHardwareMode mode) {
	ATUISwitchHardwareMode(ATUIGetNewPopupOwner(), mode, false);
}

void OnCommandSystemKernel(ATFirmwareId id) {
	ATUISwitchKernel(id);
}

void OnCommandSystemBasic(ATFirmwareId id) {
	ATUISwitchBasic(id);
}

void OnCommandSystemMemoryMode(ATMemoryMode mode) {
	ATUISwitchMemoryMode(ATUIGetNewPopupOwner(), mode);
}

void OnCommandSystemAxlonMemoryMode(uint8 bankBits) {
	if (g_sim.GetHardwareMode() != kATHardwareMode_5200 && g_sim.GetAxlonMemoryMode() != bankBits) {
		if (!ATUIConfirmSystemChangeReset())
			return;

		g_sim.SetAxlonMemoryMode(bankBits);
		
		ATUIConfirmSystemChangeResetComplete();
	}
}

void OnCommandSystemHighMemBanks(sint32 banks) {
	if (g_sim.GetCPU().GetCPUMode() == kATCPUMode_65C816 && g_sim.GetHighMemoryBanks() != banks) {
		if (!ATUIConfirmSystemChangeReset())
			return;

		g_sim.SetHighMemoryBanks(banks);

		ATUIConfirmSystemChangeResetComplete();
	}
}

void OnCommandSystemToggleMapRAM() {
	g_sim.SetMapRAMEnabled(!g_sim.IsMapRAMEnabled());
}

void OnCommandSystemToggleUltimate1MB() {
	if (!ATUIConfirmSystemChangeReset())
		return;

	g_sim.SetUltimate1MBEnabled(!g_sim.IsUltimate1MBEnabled());

	ATUIConfirmSystemChangeResetComplete();
}

void OnCommandSystemToggleFloatingIoBus() {
	if (!ATUIConfirmSystemChangeReset())
		return;

	g_sim.SetFloatingIoBusEnabled(!g_sim.IsFloatingIoBusEnabled());

	ATUIConfirmSystemChangeResetComplete();
}

void OnCommandSystemTogglePreserveExtRAM() {
	g_sim.SetPreserveExtRAMEnabled(!g_sim.IsPreserveExtRAMEnabled());
}

void OnCommandSystemMemoryClearMode(ATMemoryClearMode mode) {
	g_sim.SetMemoryClearMode(mode);
}

void OnCommandSystemToggleMemoryRandomizationEXE() {
	g_sim.SetRandomFillEXEEnabled(!g_sim.IsRandomFillEXEEnabled());
}

void OnCommandSystemToggleBASIC() {
	if (!ATUIConfirmBasicChangeReset())
		return;

	g_sim.SetBASICEnabled(!g_sim.IsBASICEnabled());

	ATUIConfirmBasicChangeResetComplete();
}

void OnCommandSystemToggleFastBoot() {
	g_sim.SetFastBootEnabled(!g_sim.IsFastBootEnabled());
}

void OnCommandSystemROMImagesDialog() {
	bool anyChanges = false;
	ATUIShowDialogFirmware(ATUIGetNewPopupOwner(), *g_sim.GetFirmwareManager(), &anyChanges);

	if (anyChanges) {
		if (g_sim.LoadROMs())
			g_sim.ColdReset();
	}

	ATUIRebuildDynamicMenu(0);
	ATUIRebuildDynamicMenu(1);
}

void OnCommandSystemToggleRTime8() {
	auto *dm = g_sim.GetDeviceManager();

	dm->ToggleDevice("rtime8");
}

void OnCommandSystemSpeedMatchHardware() {
	ATUISetFrameRateMode(kATFrameRateMode_Hardware);
}

void OnCommandSystemSpeedMatchBroadcast() {
	ATUISetFrameRateMode(kATFrameRateMode_Broadcast);
}

void OnCommandSystemSpeedInteger() {
	ATUISetFrameRateMode(kATFrameRateMode_Integral);
}

void OnCommandSystemSpeedToggleVSyncAdaptive() {
	ATUISetFrameRateVSyncAdaptive(!ATUIGetFrameRateVSyncAdaptive());
}

void OnCommandVideoGTIADefectModeNone() {
	g_sim.GetGTIA().SetDefectMode(ATGTIADefectMode::None);
}

void OnCommandVideoGTIADefectModeType1() {
	g_sim.GetGTIA().SetDefectMode(ATGTIADefectMode::Type1);
}

void OnCommandVideoGTIADefectModeType2() {
	g_sim.GetGTIA().SetDefectMode(ATGTIADefectMode::Type2);
}

void OnCommandVideoToggleCTIA() {
	ATGTIAEmulator& gtia = g_sim.GetGTIA();

	gtia.SetCTIAMode(!gtia.IsCTIAMode());
}

void OnCommandVideoToggleFrameBlending() {
	ATGTIAEmulator& gtia = g_sim.GetGTIA();

	gtia.SetBlendModeEnabled(!gtia.IsBlendModeEnabled());
}

void OnCommandVideoToggleLinearFrameBlending() {
	ATGTIAEmulator& gtia = g_sim.GetGTIA();

	gtia.SetLinearBlendEnabled(!gtia.IsLinearBlendEnabled());
}

void OnCommandVideoToggleMonoPersistence() {
	ATGTIAEmulator& gtia = g_sim.GetGTIA();

	gtia.SetBlendMonoPersistenceEnabled(!gtia.IsBlendMonoPersistenceEnabled());
}

void OnCommandVideoToggleInterlace() {
	ATGTIAEmulator& gtia = g_sim.GetGTIA();

	gtia.SetInterlaceEnabled(!gtia.IsInterlaceEnabled());
	ATUIResizeDisplay();
}

void OnCommandVideoToggleScanlines() {
	ATGTIAEmulator& gtia = g_sim.GetGTIA();

	gtia.SetScanlinesEnabled(!gtia.AreScanlinesEnabled());
	ATUIResizeDisplay();
}

void OnCommandVideoToggleBloom() {
	ATGTIAEmulator& gtia = g_sim.GetGTIA();
	auto params = gtia.GetArtifactingParams();

	params.mbEnableBloom = !params.mbEnableBloom;

	gtia.SetArtifactingParams(params);
}

void OnCommandVideoMonitorMode(ATMonitorMode mode) {
	ATGTIAEmulator& gtia = g_sim.GetGTIA();

	gtia.SetMonitorMode(mode);
}

void OnCommandVideoToggleXEP80() {
	g_sim.GetDeviceManager()->ToggleDevice("xep80");
}

void OnCommandVideoAdjustColorsDialog() {
	ATUIOpenAdjustColorsDialog(ATUIGetNewPopupOwner());
}

void OnCommandVideoAdjustScreenEffectsDialog() {
	ATUIOpenAdjustScreenEffectsDialog(ATUIGetNewPopupOwner());
}

void OnCommandVideoArtifacting(ATArtifactMode mode) {
	ATGTIAEmulator& gtia = g_sim.GetGTIA();

	gtia.SetArtifactingMode(mode);

	// We have to adjust the display filter if it is in sharp bilinear
	// mode, since the horizontal crisping is disabled if high artifacting
	// is on.
	IATDisplayPane *pane = vdpoly_cast<IATDisplayPane *>(ATGetUIPane(kATUIPaneId_Display));
	if (pane)
		pane->UpdateFilterMode();
}

void OnCommandVideoArtifactingNext() {
	ATGTIAEmulator& gtia = g_sim.GetGTIA();

	auto mode = gtia.GetArtifactingMode();
	gtia.SetArtifactingMode((ATArtifactMode)(((int)mode + 1) % (int)ATArtifactMode::Count));

	// We have to adjust the display filter if it is in sharp bilinear
	// mode, since the horizontal crisping is disabled if high artifacting
	// is on.
	IATDisplayPane *pane = vdpoly_cast<IATDisplayPane *>(ATGetUIPane(kATUIPaneId_Display));
	if (pane)
		pane->UpdateFilterMode();
}

void OnCommandVideoStandard(ATVideoStandard mode) {
	if (g_sim.GetHardwareMode() == kATHardwareMode_5200)
		return;

	if (g_sim.GetVideoStandard() == mode)
		return;

	if (!ATUIConfirmVideoStandardChangeReset())
		return;

	ATSetVideoStandard(mode);

	ATUIConfirmVideoStandardChangeResetComplete();
}

void OnCommandVideoToggleStandardNTSCPAL() {
	if (g_sim.GetHardwareMode() == kATHardwareMode_5200)
		return;

	if (!ATUIConfirmVideoStandardChangeReset())
		return;

	if (g_sim.GetVideoStandard() == kATVideoStandard_NTSC)
		ATSetVideoStandard(kATVideoStandard_PAL);
	else
		ATSetVideoStandard(kATVideoStandard_NTSC);

	ATUIUpdateSpeedTiming();

	IATDisplayPane *pane = ATGetUIPaneAs<IATDisplayPane>(kATUIPaneId_Display);
	if (pane)
		pane->OnSize();

	ATUIConfirmVideoStandardChangeResetComplete();
}

void OnCommandVideoEnhancedModeNone() {
	ATUISetEnhancedTextMode(kATUIEnhancedTextMode_None);
}

void OnCommandVideoEnhancedModeHardware() {
	ATUISetEnhancedTextMode(kATUIEnhancedTextMode_Hardware);
}

void OnCommandVideoEnhancedModeCIO() {
	ATUISetEnhancedTextMode(kATUIEnhancedTextMode_Software);
}

void OnCommandSystemEditProfilesDialog() {
	ATUIShowDialogProfiles(ATUIGetNewPopupOwner());
	ATUIRebuildDynamicMenu(kATUIDynamicMenu_Profile);
}

void OnCommandConfigureSystem() {
	ATUIShowDialogConfigureSystem(ATUIGetNewPopupOwner());
}

void OnCommandRewind() {
	g_sim.GetAutoSaveManager().Rewind();
}

void OnCommandShowRewindDialog() {
	extern void ATUIShowDialogRewind(class IATAutoSaveManager&);

	ATUIShowDialogRewind(g_sim.GetAutoSaveManager());
}

void OnCommandToggleRewindRecording() {
	auto& asmgr = g_sim.GetAutoSaveManager();

	asmgr.SetRewindEnabled(!asmgr.GetRewindEnabled());
}

void ATUIInitCommandMappingsSystem(ATUICommandManager& cmdMgr) {
	using namespace ATCommands;

	auto IsRewindEnabled = []() -> bool {
		return g_sim.GetAutoSaveManager().GetRewindEnabled();
	};

	static constexpr struct ATUICommand kCommands[]={
		{ "System.TogglePause", OnCommandSystemTogglePause },
		{ "System.WarmReset", OnCommandSystemWarmReset },
		{ "System.ColdReset", OnCommandSystemColdReset },
		{ "System.ColdResetComputerOnly", OnCommandSystemColdResetComputerOnly },

		{ "System.ToggleVSyncAdaptiveSpeed", OnCommandSystemSpeedToggleVSyncAdaptive, nullptr, [] { return ToChecked(ATUIGetFrameRateVSyncAdaptive()); } },

		{ "System.Rewind", OnCommandRewind, IsRewindEnabled },
		{ "System.ShowRewindDialog", OnCommandShowRewindDialog, IsRewindEnabled },
		{ "System.ToggleRewindRecording", OnCommandToggleRewindRecording, nullptr, [] { return ToChecked(g_sim.GetAutoSaveManager().GetRewindEnabled()); } },
	};

	cmdMgr.RegisterCommands(kCommands, vdcountof(kCommands));
}
