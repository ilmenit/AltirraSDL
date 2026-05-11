//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2008-2014 Avery Lee
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

#ifndef f_AT_UIACCESSORS_H
#define f_AT_UIACCESSORS_H

#include <vd2/system/vdtypes.h>

// UI accessors are scattered all over the code base, so we at least collect
// the prototypes here.

enum ATHardwareMode : uint32;
enum ATMemoryMode : uint32;
enum ATVideoStandard : uint32;
enum ATDisplayFilterMode : uint32;
enum ATDisplayStretchMode : uint32;
enum ATFrameRateMode : uint32;
class ATUICommandManager;
struct ATUICommandOptions;
class ATUIManager;
class IATAsyncDispatcher;
class IATDisplayPane;

const char *ATUIGetCurrentAltOutputName();
void ATUISetCurrentAltOutputName(const char *name);
void ATUIToggleAltOutput(const char *name);
bool ATUIIsAltOutputAvailable();

bool ATUIIsXEPViewEnabled();
void ATUISetXEPViewEnabled(bool enabled);
bool ATUIGetAltViewEnabled();
void ATUISetAltViewEnabled(bool enabled);

sint32 ATUIGetCurrentAltViewIndex();
void ATUISetAltViewByIndex(sint32 idx);

void ATUISelectPrevAltOutput();
void ATUISelectNextAltOutput();

bool ATUIGetAltViewAutoswitchingEnabled();
void ATUISetAltViewAutoswitchingEnabled(bool enabled);

bool ATUIGetDrawPadBoundsEnabled();
void ATUISetDrawPadBoundsEnabled(bool enabled);
bool ATUIGetDrawPadPointersEnabled();
void ATUISetDrawPadPointersEnabled(bool enabled);

ATDisplayStretchMode ATUIGetDisplayStretchMode();
void ATUISetDisplayStretchMode(ATDisplayStretchMode mode);

float ATUIGetDisplayZoom();
void ATUISetDisplayZoom(float zoom);

vdfloat2 ATUIGetDisplayPanOffset();
void ATUISetDisplayPanOffset(const vdfloat2& pan);

IATDisplayPane *ATUIGetDisplayPane();

void ATSetVideoStandard(ATVideoStandard vs);
bool ATUISwitchHardwareMode(VDGUIHandle h, ATHardwareMode mode, bool switchProfile);
void ATUISwitchMemoryMode(VDGUIHandle h, ATMemoryMode mode);

bool ATUIGetDriveSoundsEnabled();
void ATUISetDriveSoundsEnabled(bool enabled);

void ATUIRecalibrateLightPen();
void ATUIActivatePanZoomTool();
void ATUIOpenOnScreenKeyboard();
void ATUIToggleHoldKeys();

uint32 ATUIGetBootUnloadStorageMask();
void ATUISetBootUnloadStorageMask(uint32 mask);

int ATUIGetViewFilterSharpness();
void ATUISetViewFilterSharpness(int sharpness);
int ATUIGetViewFilterSharpness();
void ATUISetViewFilterSharpness(int sharpness);
ATDisplayFilterMode ATUIGetDisplayFilterMode();
void ATUISetDisplayFilterMode(ATDisplayFilterMode mode);
bool ATUIGetShowFPS();
void ATUISetShowFPS(bool enabled);

// Autosuggest auto-show-on-edit toggle (test10).  Persisted via the
// "View: Auto-suggest enabled" registry key.
bool ATUIGetAutoSuggestEnabled();
void ATUISetAutoSuggestEnabled(bool enabled);
bool ATUIGetFullscreen();
bool ATUIGetDisplayFullscreen();
void ATSetFullscreen(bool);

bool ATUIGetConstrainMouseFullScreen();
void ATUISetConstrainMouseFullScreen(bool enabled);

bool ATUIGetDisplayPadIndicators();
void ATUISetDisplayPadIndicators(bool enabled);

bool ATUIGetDisplayIndicators();
void ATUISetDisplayIndicators(bool enabled);

bool ATUICanManipulateWindows();
bool ATUIIsModalActive();

bool ATUIIsMouseCaptured();

bool ATUIGetMouseAutoCapture();
void ATUISetMouseAutoCapture(bool enabled);

bool ATUIGetPointerAutoHide();
void ATUISetPointerAutoHide(bool enabled);

bool ATUIGetTargetPointerVisible();
void ATUISetTargetPointerVisible(bool enabled);

bool ATUIGetRawInputEnabled();
void ATUISetRawInputEnabled(bool enabled);

bool ATUIGetTurbo();
void ATUISetTurbo(bool turbo);

bool ATUIGetTurboPulse();
void ATUISetTurboPulse(bool turbo);

bool ATUIGetSlowMotion();
void ATUISetSlowMotion(bool slowmo);

bool ATUIGetPauseWhenInactive();
void ATUISetPauseWhenInactive(bool enabled);

ATFrameRateMode ATUIGetFrameRateMode();
void ATUISetFrameRateMode(ATFrameRateMode mode);

bool ATUIGetFrameRateVSyncAdaptive();
void ATUISetFrameRateVSyncAdaptive(bool enable);

float ATUIGetSpeedModifier();
void ATUISetSpeedModifier(float modifier);

enum ATUIRecordingStatus {
	kATUIRecordingStatus_None,
	kATUIRecordingStatus_Video,
	kATUIRecordingStatus_Audio,
	kATUIRecordingStatus_RawAudio,
	kATUIRecordingStatus_Sap,
	kATUIRecordingStatus_Vgm
};

ATUIRecordingStatus ATUIGetRecordingStatus();

enum ATUIEnhancedTextMode {
	kATUIEnhancedTextMode_None,
	kATUIEnhancedTextMode_Hardware,
	kATUIEnhancedTextMode_Software,
	kATUIEnhancedTextModeCount
};

ATUIEnhancedTextMode ATUIGetEnhancedTextMode();
void ATUISetEnhancedTextMode(ATUIEnhancedTextMode mode);

ATUIManager& ATUIGetManager();

VDGUIHandle ATUIGetMainWindow();
VDGUIHandle ATUIGetNewPopupOwner();
bool ATUIGetAppActive();
void ATUISetAppActive(bool active);

void ATUIExit(bool forceNoConfirm);

void ATUISetWindowCaptionTemplate(const char *s);
const char *ATUIGetWindowCaptionTemplate();

bool ATUIGetDeviceButtonSupported(uint32 idx);
bool ATUIGetDeviceButtonDepressed(uint32 idx);
void ATUIActivateDeviceButton(uint32 idx, bool state);

ATUICommandManager& ATUIGetCommandManager();
void ATUIExecuteCommandStringAndShowErrors(const char *cmd, const ATUICommandOptions *opts) noexcept;

void ATUIBootImage(const wchar_t *path);

IATAsyncDispatcher *ATUIGetDispatcher();
void ATUISetDispatcher(IATAsyncDispatcher *dispatcher);

#endif	// f_AT_UIACCESSORS_H
