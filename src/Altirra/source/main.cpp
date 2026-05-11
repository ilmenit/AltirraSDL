//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2026 Avery Lee
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
//	You should have received a copy of the GNU General Public License along
//	with this program. If not, see <http://www.gnu.org/licenses/>.

#include <stdafx.h>
#include <windows.h>
#include <tchar.h>
#include <time.h>
#include <mmsystem.h>
#include <shlwapi.h>
#include <commdlg.h>
#include <commctrl.h>
#include <ole2.h>
#include <uxtheme.h>
#include <dwmapi.h>
#include <vd2/system/bitmath.h>
#include <vd2/system/cpuaccel.h>
#include <vd2/system/filesys.h>
#include <vd2/system/fraction.h>
#include <vd2/system/math.h>
#include <vd2/system/time.h>
#include <vd2/system/vdtypes.h>
#include <vd2/system/w32assist.h>
#include <vd2/system/error.h>
#include <vd2/system/registry.h>
#include <vd2/system/registrymemory.h>
#include <vd2/system/cmdline.h>
#include <vd2/system/thunk.h>
#include <vd2/system/binary.h>
#include <vd2/system/strutil.h>
#include <vd2/system/zip.h>
#include <vd2/Kasumi/pixmapops.h>
#include <vd2/Kasumi/pixmaputils.h>
#include <vd2/VDDisplay/display.h>
#include <vd2/VDDisplay/direct3d.h>
#include <vd2/VDDisplay/logging.h>
#include <vd2/Dita/accel.h>
#include <vd2/Dita/services.h>
#include <at/atappbase/crthooks.h>
#include <at/atappbase/exceptionfilter.h>
#include <at/atcore/asyncdispatcher.h>
#include <at/atcore/configvar.h>
#include <at/atcore/constants.h>
#include <at/atcore/checksum.h>
#include <at/atcore/devicevideo.h>
#include <at/atcore/enumparseimpl.h>
#include <at/atcore/media.h>
#include <at/atcore/profile.h>
#include <at/atcore/serializable.h>
#include <at/atdevices/devices.h>
#include <at/atio/atfs.h>
#include <at/atio/cassetteimage.h>
#include <at/atio/image.h>
#include <at/atui/accessibility.h>
#include <at/atui/constants.h>
#include <at/atnativeui/acceleditdialog.h>
#include <at/atnativeui/dialog.h>
#include <at/atnativeui/genericdialog.h>
#include <at/atnativeui/hotkeyexcontrol.h>
#include <at/atnativeui/messageloop.h>
#include <at/atnativeui/progress.h>
#include <at/atnativeui/theme.h>
#include <at/atnativeui/uiframe.h>
#include <at/atnativeui/uipane.h>
#include <at/atnetworksockets/nativesockets.h>
#include <at/atcore/device.h>
#include <at/atcore/propertyset.h>
#include <at/atdebugger/target.h>
#include "console.h"
#include "simulator.h"
#include "cassette.h"
#include "debugger.h"
#include "debuggerlog.h"
#include "disk.h"
#include "hostdevice.h"
#include "savestate.h"
#include "resource.h"
#include "oshelper.h"
#include "ostracing.h"
#include "audiowriter.h"
#include "sapwriter.h"
#include "inputcontroller.h"
#include "inputdefs.h"
#include "inputmanager.h"
#include "inputmap.h"
#include "cartridge.h"
#include "version.h"
#include "ide.h"
#include "ultimate1mb.h"
#include "options.h"
#include "cheatengine.h"
#include "joystick.h"
#include "uienhancedtext.h"
#include "uikeyboard.h"
#include "uicaptionupdater.h"
#include "uiportmenus.h"
#include "uimenu.h"
#include "uimrulist.h"
#include "uiprogress.h"
#include "uirender.h"
#include "uiinstance.h"
#include "uidisplay.h"
#include "uitypes.h"
#include <at/atui/uicommandmanager.h>
#include "uiprofiler.h"
#include "uiqueue.h"
#include "uicommondialogs.h"
#include "uiaccessors.h"
#include "uiclipboard.h"
#include "uifilefilters.h"
#include "uifrontend.h"
#include "cmdhelpers.h"
#include "settings.h"
#include "compatdb.h"
#include "compatengine.h"
#include "uicompat.h"
#include "trace.h"
#include "mediamanager.h"
#include "savestateio.h"
#include "versioninfo.h"
#include "directorywatcher.h"

#include "firmwaremanager.h"
#include "devicemanager.h"
#include "startuplogger.h"

#include <sdkddkver.h>

// Wine64 5.7 on macOS has a critical problem with its ABI emulation where it
// can't properly emulate TEB access through the GS: selector due to macOS
// already using it for pthread local storage. It tries to work around this
// by hijacking the slots as gs:[30h] and gs:[58h] for the self and thread-
// local-storage pointers. Unfortunately, this runs afoul of code in the UCRT
// that directly accesses gs:[60h] to get to the PEB for Windows Runtime
// support... so, despite us not caring about UWP, we're screwed by this
// compatibility issue. The result is a null pointer crash in
// _beginthreadex(), typically when we try to spin up the display manager.
//
// Ultimately, this is a really bad bug that Wine64 needs to fix. It is
// rumored to be fixed in Crossover 20 betas, but in the meantime, while we
// don't *offically* support Wine or macOS, we have some users with that
// configuration anyway.
//
// In the Windows 10.0.22621.0 SDK, this has been fixed by the UCRT now
// using NtCurrentTeb() instead of reading gs:[60h] directly. The check below
// is to ensure that we are using at least 10.0.22621.0.
//
// More info:
//	https://bugs.winehq.org/show_bug.cgi?id=49802
//	https://developercommunity.visualstudio.com/t/64-bit-EXEs-built-with-VS2019-crash-unde/1248753
//
#ifndef NTDDI_WIN10_NI
#error Windows 11 SDK 10.0.22621.0 or higher is required.
#endif

#pragma comment(lib, "comctl32")
#pragma comment(lib, "shlwapi")
#pragma comment(lib, "gdi32")
#pragma comment(lib, "shell32")
#pragma comment(lib, "ole32")
#pragma comment(lib, "comdlg32")
#pragma comment(lib, "advapi32")
#pragma comment(lib, "dwmapi")

void ATUIInitControlStylesW32();
void ATUIShutdownControlStylesW32();

void ATUITriggerButtonDown(uint32 vk);
void ATUITriggerButtonUp(uint32 vk);

///////////////////////////////////////////////////////////////////////////////

void ATUIShowDialogInputMappings(VDZHWND parent, ATInputManager& iman, IATJoystickManager *ijoy);
void ATUIShowDialogInputSetup(VDZHWND parent, ATInputManager& iman, IATJoystickManager *ijoy);
void ATUIShowDiskDriveDialog(VDGUIHandle hParent);
int ATUIShowDialogCartridgeMapper(VDGUIHandle h, uint32 cartSize, const void *data);
void ATUIShowDialogLightPen(VDGUIHandle h, ATLightPenPort *lpp);
void ATUIShowDialogCheater(VDGUIHandle hParent, ATCheatEngine *engine);
void ATUIShowDialogDiskExplorer(VDGUIHandle h);
void ATUIShowDialogAbout(VDGUIHandle h);
void ATUIShowDialogKeyboardOptions(VDGUIHandle hParent);
void ATUIShowDialogSetFileAssociations(VDGUIHandle parent, bool allowElevation, bool userOnly);
void ATRegisterFileAssociations(bool allowElevation, bool userOnly);
void ATRemoveAllFileAssociations(bool allowElevation);
void ATUIShowDialogRemoveFileAssociations(VDGUIHandle parent, bool allowElevation, bool userOnly);
void ATUIShowDialogSetupWizard(VDGUIHandle hParent);
void ATUIShowDialogCheckForUpdates(VDGUIHandle hParent);

void ATUILoadRegistry(const wchar_t *path);
void ATUISaveRegistry(const wchar_t *fnpath);
void ATUIMigrateSettings();

void ATRegisterDevices(ATDeviceManager& dm);
void ATRegisterDeviceXCmds(ATDeviceManager& dm);

void ATInitEmuErrorHandler(VDGUIHandle h, ATSimulator *sim);
void ATShutdownEmuErrorHandler();

void ATUIInitManager();
void ATUIShutdownManager();
void ATUIFlushDisplay();

void ATUIInitFirmwareMenuCallbacks(ATFirmwareManager *fwmgr);
void ATUIInitProfileMenuCallbacks();
void ATUIInitVideoOutputMenuCallback(IATDeviceVideoManager& devMgr);

void ATInitSaveStateDeserializer();

void ATLoadConfigVars();
void ATResetConfigVars();

void ATUIProcessCommandLine(VDCommandLine&& cmdLine);
void ATUICancelProcessCommandLine();

///////////////////////////////////////////////////////////////////////////////

void ATInitDebugger();
void ATShutdownDebugger();
void ATInitProfilerUI();
void ATInitUIPanes();
void ATShutdownUIPanes();
void ATShowChangeLog(VDGUIHandle hParent);
void ATUIShowDialogCmdLineHelp(VDGUIHandle hParent);
void ATUIInitCommandMappings(ATUICommandManager& cmdMgr);

void DoLoad(VDGUIHandle h, const wchar_t *path, const ATMediaWriteMode *writeMode, int cartmapper, ATImageType loadType = kATImageType_None, bool *suppressColdReset = NULL, int loadIndex = -1, bool autoProfile = false);
void DoBootWithConfirm(const wchar_t *path, const ATMediaWriteMode *writeMode, int cartmapper);

void LoadBaselineSettings();

void ATUICreateMainWindow(ATContainerWindow **);
void ATUILinkMainWindowToSimulator(ATContainerWindow& w);
void ATUIUnlinkMainWindowFromSimulator();
void ATUIResetDisplay();

///////////////////////////////////////////////////////////////////////////////

ATConfigVarBool g_ATCVEngineUseWaitableTimer("engine.use_waitable_timer", true);
ATConfigVarBool g_ATCVEngineUseWaitableTimerHighRes("engine.use_waitable_timer_high_res", true);
ATConfigVarFloat g_ATCVEngineSlowmoScale("engine.slowmo_scale", 0.5);
ATConfigVarInt32 g_ATCVEngineTurboFPSDivisor("engine.turbo_fps_divisor", 16);
ATConfigVarBool g_ATCVEngineAllowDisplayLibraryOverrides("engine.allow_display_library_overrides", false,
	[] {
		VDDSetLibraryOverridesEnabled(g_ATCVEngineAllowDisplayLibraryOverrides);
	}
);

ATConfigVarBool g_ATCVDisplayShowDebugInfo("display.show_debug_info", false,
	[] {
		VDVideoDisplaySetDebugInfoEnabled(g_ATCVDisplayShowDebugInfo);
	}
);

ATConfigVarBool g_ATCVDisplayDXFlipMode("display.dx.use_flip", true,
	[] {
		VDVideoDisplaySetDXFlipModeEnabled(g_ATCVDisplayDXFlipMode);
		ATUIResetDisplay();
	}
);

ATConfigVarBool g_ATCVDisplayDXFlipDiscard("display.dx.use_flip_discard", true,
	[] {
		VDVideoDisplaySetDXFlipDiscardEnabled(g_ATCVDisplayDXFlipDiscard);
		ATUIResetDisplay();
	}
);

ATConfigVarBool g_ATCVDisplayDXWaitableObject("display.dx.use_waitable_object", true,
	[] {
		VDVideoDisplaySetDXWaitableObjectEnabled(g_ATCVDisplayDXWaitableObject);
		ATUIResetDisplay();
	}
);

ATConfigVarBool g_ATCVDisplayDXDoNotWait("display.dx.use_do_not_wait", true,
	[] {
		VDVideoDisplaySetDXDoNotWaitEnabled(g_ATCVDisplayDXDoNotWait);
		ATUIResetDisplay();
	}
);

ATConfigVarBool g_ATCVDisplayD3D9LimitPS1_1("display.d3d9.limit_ps1_1", false,
	[] {
		VDVideoDisplaySetD3D9LimitPS1_1(g_ATCVDisplayD3D9LimitPS1_1);
		ATUIResetDisplay();
	}
);

ATConfigVarBool g_ATCVDisplayD3D9LimitPS2_0("display.d3d9.limit_ps2_0", false,
	[] {
		VDVideoDisplaySetD3D9LimitPS2_0(g_ATCVDisplayD3D9LimitPS2_0);
		ATUIResetDisplay();
	}
);

ATConfigVarBool g_ATCVDisplayD3D11Force9_1("display.d3d11.force_9_1", false,
	[] {
		VDVideoDisplaySetD3D11Force9_1(g_ATCVDisplayD3D11Force9_1);
		ATUIResetDisplay();
	}
);

ATConfigVarBool g_ATCVDisplayD3D11Force9_3("display.d3d11.force_9_3", false,
	[] {
		VDVideoDisplaySetD3D11Force9_3(g_ATCVDisplayD3D11Force9_3);
		ATUIResetDisplay();
	}
);

ATConfigVarBool g_ATCVDisplayD3D11Force10_0("display.d3d11.force_10_0", false,
	[] {
		VDVideoDisplaySetD3D11Force10_0(g_ATCVDisplayD3D11Force10_0);
		ATUIResetDisplay();
	}
);

ATConfigVarBool g_ATCVDisplayShowCustomShaderStats("display.customeffect.show_stats", false,
	[] {
		VDVideoDisplaySetShowCustomShaderStats(g_ATCVDisplayShowCustomShaderStats);
		ATUIResetDisplay();
	}
);

ATConfigVarFloat g_ATCVDisplayVSyncAdaptivePScale("display.vsync_adaptive.p_scale", 0.04f);
ATConfigVarFloat g_ATCVDisplayVSyncAdaptiveTargetOffset("display.vsync_adaptive.target_offset", 8.0f);
ATConfigVarFloat g_ATCVDisplayVSyncAdaptivePAdapt("display.vsync_adaptive.p_adapt", 0.15f);
ATConfigVarFloat g_ATCVDisplayVSyncAdaptivePClamp("display.vsync_adaptive.p_clamp", 2.0f);

ATConfigVarFloat g_ATCVAudioGTIAVolumeOverride("audio.speaker_vol_override", -1.0f,
	[] {
		g_sim.GetPokey().SetSpeakerVolumeOverride(g_ATCVAudioGTIAVolumeOverride);
	}
);

extern const wchar_t g_wszWarning[]=L"Altirra Warning";

ATLogChannel g_ATLCHostDisp(false, false, "HOSTDISP", "Host display debug output");

HINSTANCE g_hInst;
HWND g_hwnd;
ATContainerWindow *g_pMainWindow;
IVDVideoDisplay *g_pDisplay;
HMENU g_hMenu;

vdfunction<bool(bool)> g_pATIdle;

VDCommandLine g_ATCmdLine;
bool g_ATCmdLineRead;
bool g_ATCmdLineHadAnything;
bool g_ATRegistryHadAnything;
bool g_ATRegistryTemp;
VDRegistryProviderMemory *g_pATRegistryMemory;
VDStringW g_ATRegistryPathPortable;

ATSimulator g_sim;

bool g_ATAutoFrameFlipping = false;
bool g_fullscreen = false;
bool g_fullscreenDisplay = false;
WINDOWPLACEMENT g_ATWindowPreFSPlacement;
uint32 g_ATWindowPreFSDpi;
bool g_mouseCaptured = false;
bool g_mouseClipped = false;
bool g_mouseAutoCapture = true;
bool g_ATUIPointerAutoHide = true;
bool g_ATUITargetPointerVisible = true;
bool g_pauseInactive = true;
bool g_winActive = true;
bool g_showFps = false;
bool g_xepViewEnabled = false;
bool g_xepViewAutoswitchingEnabled = false;

ATUIKeyboardOptions g_kbdOpts = {
	.mbRawKeys = true,
	.mbFullRawKeys = false,
	.mbAllowInputMapModifierOverlap = true
};

ATDisplayFilterMode g_dispFilterMode = kATDisplayFilterMode_SharpBilinear;
float g_dispCustomRefreshRate;
int g_dispFilterSharpness = +1;
int g_enhancedText = 0;
LOGFONTW g_enhancedTextFont;

vdrefptr<IATSerializable> g_pATQuickState;

uint32 g_ATUIBootUnloadStorageMask = 0;

ATDisplayStretchMode g_displayStretchMode = kATDisplayStretchMode_PreserveAspectRatio;
float g_displayZoom = 1.0f;
vdfloat2 g_displayPan {};

IATUIWindowCaptionUpdater *g_winCaptionUpdater;
VDStringA g_winCaptionTemplate;

ATUICommandManager g_ATUICommandMgr;

///////////////////////////////////////////////////////////////////////////////

ATUICommandManager& ATUIGetCommandManager() {
	return g_ATUICommandMgr;
}

void ATUIExecuteCommandStringAndShowErrors(const char *cmd, const ATUICommandOptions *opts) noexcept {
	if (!cmd)
		return;

	ATUICommandOptions nullOpts;

	try {
		if (!g_ATUICommandMgr.ExecuteCommand(cmd, opts ? *opts : nullOpts))
			throw VDException(L"Unknown command: %hs", cmd);
	} catch(const VDException& ex) {
		ATUIShowError(ex);
	}
}

VDGUIHandle ATUIGetMainWindow() {
	return (VDGUIHandle)g_hwnd;
}

VDGUIHandle ATUIGetNewPopupOwner() {
	HWND hwnd = GetFocus();

	if (!hwnd)
		hwnd = GetActiveWindow();

	return (VDGUIHandle)GetAncestor(hwnd, GA_ROOT);
}

bool ATUIGetAppActive() {
	return g_winActive;
}

void ATUISetAppActive(bool active) {
	g_winActive = active;
}

void ATUISetWindowCaptionTemplate(const char *s) {
	g_winCaptionTemplate = s;

	if (g_winCaptionUpdater)
		g_winCaptionUpdater->SetTemplate(s);
}

const char *ATUIGetWindowCaptionTemplate() {
	return g_winCaptionTemplate.c_str();
}

void ATUIExit(bool forceNoConfirm) {
	if (!g_hwnd)
		return;

	if (forceNoConfirm)
		PostQuitMessage(0);
	else
		::SendMessage(g_hwnd, WM_CLOSE, 0, 0);
}

void ATUIResetDisplay() {
	IATDisplayPane *pane = vdpoly_cast<IATDisplayPane *>(ATGetUIPane(kATUIPaneId_Display));
	if (pane)
		pane->ResetDisplay();
}

///////////////////////////////////////////////////////////////////////////////

ATFrameRateMode g_frameRateMode = kATFrameRateMode_Hardware;
bool	g_frameRateVSyncAdaptive = false;
float	g_speedModifier;
uint8	g_speedFlags;
sint64	g_frameTicks;
uint32	g_frameSubTicks;
sint64	g_frameErrorBound;
sint64	g_frameTimeout;
float	g_frameRefreshPeriod;
float	g_frameCorrectionFactor;

void ATUIUpdateSpeedTiming() {
	// NTSC: 1.7897725MHz master clock, 262 scanlines of 114 clocks each
	// PAL: 1.773447MHz master clock, 312 scanlines of 114 clocks each
	// SECAM: 1.7815MHz master clock, 312 scanlines of 114 clocks each
	static constexpr double kMasterClocks[3]={
		kATMasterClock_NTSC,
		kATMasterClock_PAL,
		kATMasterClock_SECAM,
	};

	static constexpr double kPeriods[3][3]={
		{ 1.0 / kATFrameRate_NTSC, 1.0f / kATFrameRate_PAL, 1.0f / kATFrameRate_SECAM },
		{ 1.0 / 59.9400, 1.0 / 50.0000, 1.0 / 50.0 },
		{ 1.0 / 60.0000, 1.0 / 50.0000, 1.0 / 50.0 },
	};

	const auto vstd = g_sim.GetVideoStandard();
	const bool hz50 = vstd != kATVideoStandard_NTSC && vstd != kATVideoStandard_PAL60;
	const bool isSECAM = vstd == kATVideoStandard_SECAM;
	const int tableIndex = isSECAM ? 2 : hz50 ? 1 : 0;
	double rawSecondsPerFrame = kPeriods[g_frameRateMode][tableIndex];

	float customRefreshRate = g_ATOptions.mbDisplayCustomRefresh ? (float)(1.0 / rawSecondsPerFrame) : 0.0f;

	if (g_dispCustomRefreshRate != customRefreshRate) {
		g_dispCustomRefreshRate = customRefreshRate;

		IATDisplayPane *pane = vdpoly_cast<IATDisplayPane *>(ATGetUIPane(kATUIPaneId_Display));
		if (pane)
			pane->UpdateCustomRefreshRate();
	}

	const double cyclesPerSecond = kMasterClocks[tableIndex] * kPeriods[0][tableIndex] / rawSecondsPerFrame;

	double rate = 1.0;
	
	if (!g_sim.IsTurboModeEnabled()) {
		rate = std::max<double>(0, g_speedModifier + 1.0);
		if (g_speedFlags & (kATUISpeedFlags_Slow | kATUISpeedFlags_SlowPulse))
			rate *= g_ATCVEngineSlowmoScale;
	}

	rate = std::clamp<double>(rate, 0.01, 100.0);

	IATAudioOutput *audioOutput = g_sim.GetAudioOutput();
	if (audioOutput)
		audioOutput->SetCyclesPerSecond(cyclesPerSecond, 1.0 / rate);

	double secondsPerFrame = rawSecondsPerFrame / rate;

	double secondTime = VDGetPreciseTicksPerSecond();
	double frameTimeF;

	if (g_frameRateVSyncAdaptive && g_frameRefreshPeriod > 0 && g_frameRefreshPeriod > secondsPerFrame * 0.98f && g_frameRefreshPeriod < secondsPerFrame * 1.02f)
		frameTimeF = secondTime * ((g_frameRefreshPeriod ? g_frameRefreshPeriod : secondsPerFrame) + g_frameCorrectionFactor);
	else
		frameTimeF = secondTime * secondsPerFrame;

	g_frameTicks = VDFloorToInt64(frameTimeF);
	g_frameSubTicks = VDRoundToInt32((frameTimeF - g_frameTicks) * 65536.0);
	g_frameErrorBound = std::max<sint64>(2 * g_frameTicks, VDRoundToInt64(secondTime * 0.1f));
	g_frameTimeout = std::max<sint64>(5 * g_frameTicks, VDGetPreciseTicksPerSecondI());
}

void ATUIAdaptSpeedTiming(const VDDVSyncStatus& vss) {
	if (!g_frameRateVSyncAdaptive)
		return;

	g_frameRefreshPeriod = vss.mRefreshRate > 0 ? 1.0f / vss.mRefreshRate : 0;

	if (vss.mOffset >= 0) {
		float error = vss.mOffset - g_ATCVDisplayVSyncAdaptiveTargetOffset / 1000.0f;
		float clamp = g_ATCVDisplayVSyncAdaptivePClamp / 1000.0f;

		g_frameCorrectionFactor -= g_ATCVDisplayVSyncAdaptivePAdapt * (g_frameCorrectionFactor - std::clamp<float>(error * g_ATCVDisplayVSyncAdaptivePScale, -clamp, clamp));
	} else
		g_frameCorrectionFactor = 0;

	ATUIUpdateSpeedTiming();
}

ATFrameRateMode ATUIGetFrameRateMode() {
	return g_frameRateMode;
}

void ATUISetFrameRateMode(ATFrameRateMode mode) {
	if (g_frameRateMode != mode) {
		g_frameRateMode = mode;

		g_frameRefreshPeriod = 0;
		g_frameCorrectionFactor = 0;

		ATUIUpdateSpeedTiming();
	}
}

bool ATUIGetFrameRateVSyncAdaptive() {
	return g_frameRateVSyncAdaptive;
}

void ATUISetFrameRateVSyncAdaptive(bool enable) {
	if (g_frameRateVSyncAdaptive != enable) {
		g_frameRateVSyncAdaptive = enable;

		g_sim.GetGTIA().SetVsyncAdaptiveEnabled(enable);

		ATUIUpdateSpeedTiming();
	}
}

float ATUIGetSpeedModifier() {
	return g_speedModifier;
}

void ATUISetSpeedModifier(float modifier) {
	if (g_speedModifier != modifier) {
		g_speedModifier = modifier;

		ATUIUpdateSpeedTiming();
	}
}

void ATUIChangeSpeedFlags(uint8 mask, uint8 value) {
	uint8 delta = (g_speedFlags ^ value) & mask;

	if (!delta)
		return;

	uint8 flags = (g_speedFlags ^ delta);
	g_speedFlags = flags;

	g_sim.SetTurboModeEnabled((flags & (kATUISpeedFlags_Turbo | kATUISpeedFlags_TurboPulse)) != 0);

	ATUIUpdateSpeedTiming();
}

bool ATUIIsMouseCaptured() {
	return g_mouseCaptured || g_mouseClipped;
}

bool ATUIGetMouseAutoCapture() {
	return g_mouseAutoCapture;
}

void ATUISetMouseAutoCapture(bool enabled) {
	g_mouseAutoCapture = enabled;
}

bool ATUIGetPointerAutoHide() {
	return g_ATUIPointerAutoHide;
}

void ATUISetPointerAutoHide(bool enabled) {
	g_ATUIPointerAutoHide = enabled;
}

bool ATUIGetTargetPointerVisible() {
	return g_ATUITargetPointerVisible;
}

void ATUISetTargetPointerVisible(bool enabled) {
	g_ATUITargetPointerVisible = enabled;
}

bool ATUIGetTurbo() {
	return (g_speedFlags & kATUISpeedFlags_Turbo) != 0;
}

void ATUISetTurbo(bool turbo) {
	ATUIChangeSpeedFlags(kATUISpeedFlags_Turbo, turbo ? kATUISpeedFlags_Turbo : 0);
}

bool ATUIGetTurboPulse() {
	return (g_speedFlags & kATUISpeedFlags_TurboPulse) != 0;
}

void ATUISetTurboPulse(bool turbo) {
	ATUIChangeSpeedFlags(kATUISpeedFlags_TurboPulse, turbo ? kATUISpeedFlags_TurboPulse : 0);
}

bool ATUIGetSlowMotion() {
	return (g_speedFlags & kATUISpeedFlags_Slow) != 0;
}

void ATUISetSlowMotion(bool slowmo) {
	ATUIChangeSpeedFlags(kATUISpeedFlags_Slow, slowmo ? kATUISpeedFlags_Slow : 0);
}

bool ATUIGetPauseWhenInactive() {
	return g_pauseInactive;
}

void ATUISetPauseWhenInactive(bool enabled) {
	g_pauseInactive = enabled;
}

///////////////////////////////////////////////////////////////////////////////
// This is a bad hack which is needed for now since the CPU history state
// is the master state.
void ATSyncCPUHistoryState() {
	const bool historyEnabled = g_sim.GetCPU().IsHistoryEnabled();

	for(IATDeviceDebugTarget *devtarget : g_sim.GetDeviceManager()->GetInterfaces<IATDeviceDebugTarget>(false, false, false)) {
		uint32 index = 0;

		while(IATDebugTarget *target = devtarget->GetDebugTarget(index++)) {
			auto *thist = vdpoly_cast<IATDebugTargetHistory *>(target);

			if (thist)
				thist->SetHistoryEnabled(historyEnabled);
		}
	}
}

///////////////////////////////////////////////////////////////////////////////

class ATInputConsoleCallback : public IATInputConsoleCallback {
public:
	virtual void SetConsoleTrigger(uint32 id, bool state);
};

void ATInputConsoleCallback::SetConsoleTrigger(uint32 id, bool state) {
	switch(id) {
		case kATInputTrigger_UILeft:
		case kATInputTrigger_UIRight:
		case kATInputTrigger_UIUp:
		case kATInputTrigger_UIDown:
		case kATInputTrigger_UIAccept:
		case kATInputTrigger_UIReject:
		case kATInputTrigger_UIMenu:
		case kATInputTrigger_UIOption:
		case kATInputTrigger_UISwitchLeft:
		case kATInputTrigger_UISwitchRight:
		case kATInputTrigger_UILeftShift:
		case kATInputTrigger_UIRightShift:
			ATUISetNativeDialogMode(false);

			if (state)
				ATUITriggerButtonDown(id);
			else
				ATUITriggerButtonUp(id);
			break;

		default:
			break;
	}

	// block the below actions when a modal is up to prevent improper nesting
	if (state && ATUIIsModalActive())
		return;

	switch(id) {
		case kATInputTrigger_Start:
			g_sim.GetGTIA().SetConsoleSwitch(0x01, state);
			break;
		case kATInputTrigger_Select:
			g_sim.GetGTIA().SetConsoleSwitch(0x02, state);
			break;
		case kATInputTrigger_Option:
			g_sim.GetGTIA().SetConsoleSwitch(0x04, state);
			break;
		case kATInputTrigger_ColdReset:
			if (state)
				g_ATUICommandMgr.ExecuteCommandNT("System.ColdReset");
			break;
		case kATInputTrigger_WarmReset:
			if (state)
				g_ATUICommandMgr.ExecuteCommandNT("System.WarmReset");
			break;
		case kATInputTrigger_Turbo:
			g_ATUICommandMgr.ExecuteCommandNT(state ? "System.PulseWarpOn" : "System.PulseWarpOff");
			break;
		case kATInputTrigger_KeySpace:
			if (g_kbdOpts.mbRawKeys) {
				if (state)
					g_sim.GetPokey().PushRawKey(0x21, !g_kbdOpts.mbFullRawKeys);
				else
					g_sim.GetPokey().ReleaseRawKey(0x21, !g_kbdOpts.mbFullRawKeys);
			} else if (state)
				g_sim.GetPokey().PushKey(0x21, false);
			break;

		case kATInputTrigger_Rewind:
			if (state)
				g_ATUICommandMgr.ExecuteCommandNT("System.Rewind");
			break;

		case kATInputTrigger_RewindMenu:
			if (state)
				g_ATUICommandMgr.ExecuteCommandNT("System.ShowRewindDialog");
			break;

		default:
			break;
	};
}

ATInputConsoleCallback g_inputConsoleCallback;

///////////////////////////////////////////////////////////////////////////////

bool ATUIConfirmDiscardCartridge(VDGUIHandle h) {
	if (!g_sim.IsStorageDirty(kATStorageId_Cartridge))
		return true;

	return IDYES == MessageBoxW((HWND)h, L"Modified cartridge image has not been saved. Discard it anyway?", L"Altirra Warning", MB_ICONEXCLAMATION | MB_YESNO);
}

VDStringW ATUIConfirmDiscardAllStorageGetMessage(const wchar_t *prompt, bool includeUnmountables, uint32 storageTypeMask = ~(uint32)0, bool includeMemory = false) {
	typedef vdfastvector<ATStorageId> DirtyIds;
	DirtyIds dirtyIds;

	typedef vdfastvector<ATDebuggerStorageId> DbgDirtyIds;
	DbgDirtyIds dbgDirtyIds;

	g_sim.GetDirtyStorage(dirtyIds, storageTypeMask);

	if (includeUnmountables) {
		IATDebugger *dbg = ATGetDebugger();
		if (dbg)
			dbg->GetDirtyStorage(dbgDirtyIds);
	}

	if (dirtyIds.empty() && dbgDirtyIds.empty())
		return VDStringW();

	std::sort(dirtyIds.begin(), dirtyIds.end());
	std::sort(dbgDirtyIds.begin(), dbgDirtyIds.end());

	VDStringW msg;

	msg = L"The following modified items have not been saved:\n\n";
	
	if (includeMemory)
		msg += L"\tContents of emulation memory\n";

	for(DirtyIds::const_iterator it(dirtyIds.begin()), itEnd(dirtyIds.end()); it != itEnd; ++it) {
		ATStorageId id = *it;

		const uint32 type = id & kATStorageId_TypeMask;
		const uint32 unit = id & kATStorageId_UnitMask;

		switch(type) {
			case kATStorageId_Cartridge:
				msg += L"\tCartridge";

				if (unit)
					msg.append_sprintf(L" %u", unit + 1);
				break;

			case kATStorageId_Disk:
				msg.append_sprintf(L"\tDisk (D%u:)", unit + 1);
				break;

			case kATStorageId_Tape:
				msg += L"\tTape";
				break;

			case kATStorageId_Firmware:
				switch(unit) {
					case 0:
						msg += L"\tIDE main firmware";
						break;

					case 1:
						msg += L"\tIDE SDX firmware";
						break;

					case 2:
						msg += L"\tUltimate1MB firmware";
						break;

					case 3:
						msg += L"\tRapidus flash firmware";
						break;

					case 4:
						msg += L"\tRapidus PBI firmware";
						break;
				}
				break;
		}

		msg += '\n';
	}

	for(DbgDirtyIds::const_iterator it(dbgDirtyIds.begin()), itEnd(dbgDirtyIds.end()); it != itEnd; ++it) {
		ATDebuggerStorageId id = *it;

		switch(id) {
			case kATDebuggerStorageId_CustomSymbols:
				msg += L"\tDebugger: Custom Symbols\n";
				break;
		}
	}

	msg += L'\n';
	msg += prompt;

	return msg;
}

bool ATUIConfirmDiscardMemory(VDGUIHandle h, const wchar_t *title) {
	return ATUIConfirm(h, "DiscardMemory", L"Any unsaved work in emulation memory will be lost. Are you sure?", title);
}

bool ATUIConfirmReset(VDGUIHandle h, const char *key, const wchar_t *message, const wchar_t *title) {
	return g_sim.TicksSinceColdReset() == 0 || ATUIConfirm(h, key, message, title);
}

void ATUIConfirmResetComplete() {
	g_sim.ColdReset();
}

bool ATUIConfirmDiscardAll(VDGUIHandle h, const wchar_t *title, const wchar_t *prompt) {
	const VDStringW& msg = ATUIConfirmDiscardAllStorageGetMessage(prompt, true, ~(uint32)0, true);

	if (msg.empty())
		return ATUIConfirmDiscardMemory(h, title);

	return ATUIConfirm(h, "DiscardStorage", msg.c_str(), title);
}

bool ATUIConfirmDiscardAllStorage(VDGUIHandle h, const wchar_t *prompt, bool includeUnmountables, uint32 storageTypeMask) {
	const VDStringW& msg = ATUIConfirmDiscardAllStorageGetMessage(prompt, includeUnmountables, storageTypeMask);

	if (msg.empty())
		return true;

	return ATUIConfirm(h, "DiscardStorage", msg.c_str(), L"Unsaved Items");
}

vdrefptr<ATUIFutureWithResult<bool> > ATUIConfirmDiscardAllStorage(const wchar_t *prompt, bool includeUnmountables, uint32 storageTypeMask) {
	const VDStringW& msg = ATUIConfirmDiscardAllStorageGetMessage(prompt, includeUnmountables);

	if (msg.empty())
		return vdrefptr<ATUIFutureWithResult<bool> >(new ATUIFutureWithResult<bool>(true));

	if (ATUIGetNativeDialogMode())
		return vdmakerefptr(new ATUIFutureWithResult<bool>(ATUIConfirm(ATUIGetNewPopupOwner(), "DiscardStorage", msg.c_str(), L"Unsaved Items")));

	return ATUIShowAlertWarningConfirm(msg.c_str(), L"Discarding modified storage");
}

bool ATUISwitchHardwareMode(VDGUIHandle h, ATHardwareMode mode, bool switchProfiles) {
	ATHardwareMode prevMode = g_sim.GetHardwareMode();
	if (prevMode == mode)
		return true;

	ATDefaultProfile defaultProfile;

	switch(mode) {
		case kATHardwareMode_800:
			defaultProfile = kATDefaultProfile_800;
			break;

		case kATHardwareMode_800XL:
		case kATHardwareMode_130XE:
		default:
			defaultProfile = kATDefaultProfile_XL;
			break;

		case kATHardwareMode_5200:
			defaultProfile = kATDefaultProfile_5200;
			break;

		case kATHardwareMode_XEGS:
			defaultProfile = kATDefaultProfile_XEGS;
			break;

		case kATHardwareMode_1200XL:
			defaultProfile = kATDefaultProfile_1200XL;
			break;
	}

	const uint32 oldProfileId = ATSettingsGetCurrentProfileId();
	const uint32 newProfileId = ATGetDefaultProfileId(defaultProfile);
	const bool switchingProfile = switchProfiles && (newProfileId != kATProfileId_Invalid && newProfileId != oldProfileId);

	// check if we are switching to or from 5200 mode
	const bool switching5200 = (mode == kATHardwareMode_5200 || prevMode == kATHardwareMode_5200);
	if (switching5200 || switchingProfile) {
		// check if it's OK to unload everything
		if (!ATSettingsGetBootstrapProfileMode() && h && !ATUIConfirmDiscardAll(h, L"Changing hardware type", L"OK to switch hardware mode and discard everything?"))
			return false;
	}

	// switch profile if necessary
	if (switchingProfile) {
		ATSettingsSwitchProfile(newProfileId);
	}

	if (switching5200) {
		g_sim.UnloadAll();

		// 5200 mode needs the default cart and 16K memory
		if (mode == kATHardwareMode_5200) {
			g_sim.LoadCartridge5200Default();
			g_sim.SetMemoryMode(kATMemoryMode_16K);
		}
	}

	g_sim.SetHardwareMode(mode);

	// Check for incompatible kernel.
	switch(g_sim.GetKernelMode()) {
		case kATKernelMode_Default:
			break;

		case kATKernelMode_XL:
			if (!kATHardwareModeTraits[mode].mbRunsXLOS)
				g_sim.SetKernel(0);
			break;

		case kATKernelMode_5200:
			if (mode != kATHardwareMode_5200)
				g_sim.SetKernel(0);
			break;

		default:
			if (mode == kATHardwareMode_5200)
				g_sim.SetKernel(0);
			break;
	}

	// If we are in 5200 mode, we must be in NTSC
	if (mode == kATHardwareMode_5200 && g_sim.GetVideoStandard() != kATVideoStandard_NTSC)
	{
		g_sim.SetVideoStandard(kATVideoStandard_NTSC);
		ATUIUpdateSpeedTiming();
	}

	g_sim.ColdReset();
	return true;
}

bool ATUISwitchHardwareModeComputer(VDGUIHandle h) {
	if (g_sim.GetHardwareMode() != kATHardwareMode_5200)
		return true;

	return ATUISwitchHardwareMode(h, kATHardwareMode_800XL, true);
}

bool ATUISwitchHardwareMode5200(VDGUIHandle h) {
	if (g_sim.GetHardwareMode() == kATHardwareMode_5200)
		return true;

	return ATUISwitchHardwareMode(h, kATHardwareMode_5200, true);
}

bool ATUISwitchKernel(VDGUIHandle h, uint64 kernelId) {
	if (g_sim.GetKernelId() == kernelId)
		return true;

	// If the kernel mode is incompatible, check if we can switch the computer
	// mode.
	ATFirmwareManager& fwm = *g_sim.GetFirmwareManager();

	if (kernelId) {
		ATFirmwareInfo fwinfo;
		if (!fwm.GetFirmwareInfo(kernelId, fwinfo))
			return false;

		const auto hwmode = g_sim.GetHardwareMode();
		const bool canUseXLOS = kATHardwareModeTraits[hwmode].mbRunsXLOS;

		switch(fwinfo.mType) {
			case kATFirmwareType_Kernel1200XL:
				if (!canUseXLOS) {
					if (!ATUISwitchHardwareMode(h, kATHardwareMode_1200XL, true))
						return false;
				}
				break;

			case kATFirmwareType_KernelXL:
				if (!canUseXLOS) {
					if (!ATUISwitchHardwareMode(h, kATHardwareMode_800XL, true))
						return false;
				}
				break;

			case kATFirmwareType_KernelXEGS:
				if (!canUseXLOS) {
					if (!ATUISwitchHardwareMode(h, kATHardwareMode_XEGS, true))
						return false;
				}
				break;

			case kATFirmwareType_Kernel800_OSA:
			case kATFirmwareType_Kernel800_OSB:
				if (hwmode == kATHardwareMode_5200) {
					if (!ATUISwitchHardwareMode(h, kATHardwareMode_800, true))
						return false;
				}
				break;

			case kATFirmwareType_Kernel5200:
				if (!ATUISwitchHardwareMode5200(h))
					return false;
				break;
		}

		// Check if we need to adjust the memory size. XL and Other kernels can't
		// run with 48K or 56K. 16K is OK (600XL configuration). We don't need to
		// check 5200 here as it was already changed in the hardware mode switch.
		switch(fwinfo.mType) {
			case kATFirmwareType_KernelXL:
			case kATFirmwareType_Kernel1200XL:
				switch(g_sim.GetMemoryMode()) {
					case kATMemoryMode_8K:
					case kATMemoryMode_24K:
					case kATMemoryMode_32K:
					case kATMemoryMode_40K:
					case kATMemoryMode_48K:
					case kATMemoryMode_52K:
						g_sim.SetMemoryMode(kATMemoryMode_64K);
						break;
				}
				break;
		}
	}

	g_sim.SetKernel(kernelId);
	g_sim.ColdReset();
	return true;
}

void ATUISwitchKernel(uint64 id) {
	ATUISwitchKernel((VDGUIHandle)g_hwnd, id);
}

void ATUISwitchBasic(uint64 basicId) {
	if (g_sim.GetBasicId() == basicId)
		return;

	g_sim.SetBasic(basicId);
	g_sim.ColdReset();
	return;
}

void ATUISwitchMemoryMode(VDGUIHandle h, ATMemoryMode mode) {
	if (g_sim.GetMemoryMode() == mode)
		return;

	switch(g_sim.GetHardwareMode()) {
		case kATHardwareMode_5200:
			if (mode != kATMemoryMode_16K)
				return;
			break;

		case kATHardwareMode_800XL:
			if (mode == kATMemoryMode_48K ||
				mode == kATMemoryMode_52K ||
				mode == kATMemoryMode_8K ||
				mode == kATMemoryMode_24K ||
				mode == kATMemoryMode_32K ||
				mode == kATMemoryMode_40K)
				return;
			break;

		case kATHardwareMode_1200XL:
		case kATHardwareMode_XEGS:
		case kATHardwareMode_130XE:
		case kATHardwareMode_1400XL:
			if (mode == kATMemoryMode_48K ||
				mode == kATMemoryMode_52K ||
				mode == kATMemoryMode_8K ||
				mode == kATMemoryMode_16K ||
				mode == kATMemoryMode_24K ||
				mode == kATMemoryMode_32K ||
				mode == kATMemoryMode_40K)
				return;
			break;
	}

	g_sim.SetMemoryMode(mode);
	g_sim.ColdReset();
}

void DoCompatibilityCheck() {
	vdfastvector<ATCompatKnownTag> tags;
	auto *title = ATCompatCheck(tags);

	if (title) {
		auto result = ATUIShowDialogCompatWarning((VDGUIHandle)g_hwnd, title, tags.data(), tags.size());

		VDTextU8ToW(VDStringSpanA(title->mName.c_str())).c_str();

		if (result == kATUICompatAction_Pause)
			g_sim.Pause();
		else if (result == kATUICompatAction_AutoAdjust)
			ATCompatAdjust((VDGUIHandle)g_hwnd, tags.data(), tags.size());
	}
}

void DoLoadStream(VDGUIHandle h, const wchar_t *origPath, const wchar_t *imageName, IVDRandomAccessStream *stream, const ATMediaWriteMode *writeMode, int cartmapper, ATImageType loadType, bool *suppressColdReset, int loadIndex, bool autoProfile) {
	vdfastvector<uint8> captureBuffer;

	ATCartLoadContext cartctx = {};
	cartctx.mbReturnOnUnknownMapper = true;

	if (cartmapper > 0) {
		cartctx.mbReturnOnUnknownMapper = false;
		cartctx.mCartMapper = cartmapper;
	} else {
		cartctx.mpCaptureBuffer = &captureBuffer;

		if (cartmapper < 0)
			cartctx.mbIgnoreChecksum = true;
	}

	ATStateLoadContext statectx = {};

	ATImageLoadContext ctx;
	ctx.mLoadType = loadType;
	ctx.mLoadIndex = loadIndex;
	ctx.mpCartLoadContext = &cartctx;
	ctx.mpStateLoadContext = &statectx;

	ATMediaLoadContext mctx;
	mctx.mOriginalPath = origPath ? origPath : L"";
	mctx.mImageName = imageName ? imageName : L"";
	mctx.mpStream = stream;
	mctx.mWriteMode = writeMode ? *writeMode : g_ATOptions.mDefaultWriteMode;
	mctx.mbStopOnModeIncompatibility = true;
	mctx.mbStopAfterImageLoaded = true;
	mctx.mbStopOnMemoryConflictBasic = true;
	mctx.mbStopOnIncompatibleDiskFormat = true;
	mctx.mpImageLoadContext = &ctx;

	int safetyCounter = 10;
	for(;;) {
		if (g_sim.Load(mctx))
			break;

		if (!--safetyCounter)
			return;

		if (mctx.mbStopAfterImageLoaded)
			mctx.mbStopAfterImageLoaded = false;

		if (mctx.mbMode5200Required) {
			if (!ATUISwitchHardwareMode5200(h)) {
				if (suppressColdReset)
					*suppressColdReset = true;

				return;
			}

			continue;
		} else if (mctx.mbModeComputerRequired) {
			if (autoProfile) {
				if (!ATUISwitchHardwareMode(h, kATHardwareMode_800XL, true)) {
					if (suppressColdReset)
						*suppressColdReset = true;

					return;
				}
			} else {
				if (!ATUISwitchHardwareModeComputer(h)) {
					if (suppressColdReset)
						*suppressColdReset = true;

					return;
				}
			}

			continue;
		} else if (mctx.mbMemoryConflictBasic) {
			mctx.mbStopOnMemoryConflictBasic = false;

			ATUIGenericDialogOptions opts {};
			opts.mhParent = h;
			opts.mpTitle = L"Memory Conflict";
			opts.mpMessage = L"The program being loaded overlaps internal BASIC and may not work with it enabled. Disable internal BASIC?";
			opts.mpIgnoreTag = "MemoryConflictBasic";
			opts.mIconType = kATUIGenericIconType_Warning;
			opts.mResultMask = kATUIGenericResultMask_YesNoCancel;
			opts.mValidIgnoreMask = kATUIGenericResultMask_Yes | kATUIGenericResultMask_No;
			const auto result = ATUIShowGenericDialogAutoCenter(opts);

			if (result == kATUIGenericResult_Cancel) {
				if (suppressColdReset)
					*suppressColdReset = true;
				return;
			}

			if (result == kATUIGenericResult_Yes)
				g_sim.SetBASICEnabled(false);

			continue;
		} else if (mctx.mbIncompatibleDiskFormat) {
			mctx.mbIncompatibleDiskFormat = false;

			ATUIGenericDialogOptions opts {};
			opts.mhParent = h;
			opts.mpTitle = L"Incompatible Disk Format";
			opts.mpMessage = L"The current disk drive model can't read the type of disk being inserted into it. Mount the disk anyway?";
			opts.mpIgnoreTag = "IncompatibleDiskFormat";
			opts.mIconType = kATUIGenericIconType_Warning;
			opts.mResultMask = kATUIGenericResultMask_OKCancel;
			opts.mValidIgnoreMask = kATUIGenericResultMask_OKCancel;
			const auto result = ATUIShowGenericDialogAutoCenter(opts);

			if (result == kATUIGenericResult_Cancel) {
				if (suppressColdReset)
					*suppressColdReset = true;
				return;
			}

			mctx.mbStopOnIncompatibleDiskFormat = false;
			continue;
		}

		if (ctx.mLoadType == kATImageType_Cartridge) {
			vdfastvector<ATCompatKnownTag> tags;
			ATCompatMarker markers[1];
			size_t numMarkers = 0;

			if (cartctx.mFileSHA256.has_value())
				markers[numMarkers++] = ATCompatMarker::FromSHA256(kATCompatRuleType_CartFileSHA256, cartctx.mFileSHA256.value());

			ATCompatFindTitle(vdvector_view<ATCompatMarker>(markers, numMarkers), tags, false);

			int mapper = -1;
			for(auto&& tag : tags) {
				switch(tag) {
					case kATCompatKnownTag_Cart52008K:
						mapper = kATCartridgeMode_5200_8K;
						break;

					case kATCompatKnownTag_Cart520016KOneChip:
						mapper = kATCartridgeMode_5200_16K_OneChip;
						break;

					case kATCompatKnownTag_Cart520016KTwoChip:
						mapper = kATCartridgeMode_5200_16K_TwoChip;
						break;

					case kATCompatKnownTag_Cart520032K:
						mapper = kATCartridgeMode_5200_32K;
						break;
				}
			}

			if (mapper < 0) {
				mapper = ATUIShowDialogCartridgeMapper(h, cartctx.mCartSize, captureBuffer.data());
				if (mapper < 0)
					return;
			}

			cartctx.mbReturnOnUnknownMapper = false;
			cartctx.mCartMapper = mapper;
		} else if (ctx.mLoadType == kATImageType_SaveState || ctx.mLoadType == kATImageType_SaveState2) {
			if (statectx.mbKernelMismatchDetected) {
				if (IDOK != MessageBoxW((HWND)h,
					L"The currently loaded kernel ROM image doesn't match the one referenced by the saved state. This may cause the simulated program to fail when resumed. Proceed anyway?",
					L"Altirra Warning", MB_ICONWARNING | MB_OKCANCEL))
					return;

				statectx.mbAllowKernelMismatch = true;
			}
		}
	}

	if (ctx.mLoadType == kATImageType_SaveState) {
		if (!statectx.mbPrivateStateLoaded) {
			MessageBoxW((HWND)h, L"The save state loaded successfully, but detailed emulation state could not be loaded as it was produced by a different program version. Some glitches may appear in the simulation.", L"Altirra Warning", MB_ICONWARNING | MB_OK);
		}

		if (suppressColdReset)
			*suppressColdReset = true;
	} else if (ctx.mLoadType == kATImageType_SaveState2) {
		if (suppressColdReset)
			*suppressColdReset = true;
	}

	DoCompatibilityCheck();
}

void DoLoadStream(VDGUIHandle h, const wchar_t *origPath, const wchar_t *imageName, IVDRandomAccessStream& stream, const ATMediaWriteMode *writeMode, int cartmapper, ATImageType loadType, bool *suppressColdReset, int loadIndex) {
	DoLoadStream(h, origPath, imageName, &stream, writeMode, cartmapper, loadType, suppressColdReset, loadIndex, false);
}

void DoLoad(VDGUIHandle h, const wchar_t *path, const ATMediaWriteMode *writeMode, int cartmapper, ATImageType loadType, bool *suppressColdReset, int loadIndex, bool autoProfile) {
	DoLoadStream(h, path, path, nullptr, writeMode, cartmapper, loadType, suppressColdReset, loadIndex, autoProfile);
}

uint32 ATUIGetBootUnloadStorageMask() {
	return g_ATUIBootUnloadStorageMask;
}

void ATUISetBootUnloadStorageMask(uint32 mask) {
	g_ATUIBootUnloadStorageMask = mask;
}

void ATUIUnloadStorageForBoot() {
	g_sim.UnloadAll(g_ATUIBootUnloadStorageMask);
}

void ATUIBootImage(const wchar_t *path) {
	bool suppressColdReset = false;
	ATUIUnloadStorageForBoot();

	DoLoad((VDGUIHandle)g_hwnd, path, nullptr, 0);

	if (!suppressColdReset)
		g_sim.ColdReset();
}

void DoBootWithConfirm(const wchar_t *path, const ATMediaWriteMode *writeMode, int cartmapper) {
	if (!ATUIConfirmDiscardAllStorage((VDGUIHandle)g_hwnd, L"OK to discard?", false, g_ATUIBootUnloadStorageMask))
		return;

	bool suppressColdReset = false;
	try {
		ATUIUnloadStorageForBoot();

		DoLoad((VDGUIHandle)g_hwnd, path, writeMode, cartmapper, kATImageType_None, &suppressColdReset);

		ATAddMRUListItem(path);
	} catch(const MyError& e) {
		e.post(g_hwnd, "Altirra Error");
	}

	if (!suppressColdReset)
		g_sim.ColdReset();
}

void DoBootStreamWithConfirm(const wchar_t *origPath, const wchar_t *imageName, IVDRandomAccessStream& stream, const ATMediaWriteMode *writeMode, int cartmapper) {
	if (!ATUIConfirmDiscardAllStorage((VDGUIHandle)g_hwnd, L"OK to discard?", false, g_ATUIBootUnloadStorageMask))
		return;

	bool suppressColdReset = false;
	try {
		ATUIUnloadStorageForBoot();

		DoLoadStream((VDGUIHandle)g_hwnd, origPath, imageName, stream, writeMode, cartmapper, kATImageType_None, &suppressColdReset, -1);
	} catch(const MyError& e) {
		e.post(g_hwnd, "Altirra Error");
	}

	if (!suppressColdReset)
		g_sim.ColdReset();
}

class ATUIFutureOpenBootImage : public ATUIFuture {
public:
	ATUIFutureOpenBootImage(bool coldBoot, ATUICommandContext *ctx)
		: mbColdBoot(coldBoot)
		, mpCommandContext(ctx)
	{
	}

	virtual void RunInner() {
		switch(mStage) {
			case 0:
				if (mbColdBoot && !mpCommandContext->mbQuiet) {
					mpConfirmResult = ATUIConfirmDiscardAllStorage(L"OK to discard?", false, g_ATUIBootUnloadStorageMask);
					Wait(mpConfirmResult);
				}
				++mStage;
				break;

			case 1:
				if (mpConfirmResult && !mpConfirmResult->GetResult()) {
					mpCommandContext->MarkCompleted(false);

					MarkCompleted();
					break;
				}

				mpConfirmResult.clear();

				if (VDStringW path; mpCommandContext->GetArg(0, path) && !path.empty()) {
					mpFileDialogResult = new ATUIFileDialogResult;
					mpFileDialogResult->mbAccepted = true;
					mpFileDialogResult->mPath = path;
				} else {
					mpFileDialogResult = ATUIShowOpenFileDialog("load"_vdfcctypeid, L"Load disk, cassette, cartridge, or program image",
						L"All supported types\0*.atr;*.xfd;*.dcm;*.pro;*.atx;*.xex;*.obx;*.com;*.car;*.rom;*.a52;*.bin;*.cas;*.wav;*.flac;*.ogg;*.zip;*.atz;*.gz;*.bas;*.arc;*.sap;*.vgm;*.vgz\0"
						L"Atari program (*.xex,*.obx,*.com)\0*.xex;*.obx;*.com\0"
						L"BASIC program (*.bas)\0*.bas\0"
						L"Atari disk image (*.atr,*.xfd,*.dcm)\0*.atr;*.xfd;*.dcm;*.arc\0"
						L"Protected disk image (*.pro)\0*.pro\0"
						L"VAPI disk image (*.atx)\0*.atx\0"
						L"Cartridge (*.rom,*.bin,*.a52,*.car)\0*.rom;*.bin;*.a52;*.car\0"
						L"Cassette tape (*.cas,*.wav,*.flac,*.ogg)\0*.cas;*.wav;*.flac;*.ogg\0"
						L"Zip archive (*.zip)\0*.zip\0"
						L"gzip archive (*.gz;*.atz)\0*.gz;*.atz\0"
						L".ARC archive (*.arc)\0*.arc\0"
						L"SAP file (*.sap)\0*.sap\0"
						L"VGM POKEY file (*.vgm,*.vgz)\0*.vgm;*.vgz\0"
						L"All files\0*.*\0");

					Wait(mpFileDialogResult);
				}
				++mStage;
				break;

			case 2:
				if (mpFileDialogResult->mbAccepted) {
					if (mbColdBoot)
						ATUIUnloadStorageForBoot();

					bool suppressColdReset = false;
					DoLoad((VDGUIHandle)g_hwnd, mpFileDialogResult->mPath.c_str(), nullptr, 0, kATImageType_None, &suppressColdReset);

					if (mbColdBoot && !suppressColdReset)
						g_sim.ColdReset();

					ATAddMRUListItem(mpFileDialogResult->mPath.c_str());

					mpCommandContext->SetArg(0, mpFileDialogResult->mPath.c_str());
				}

				mpCommandContext->MarkCompleted(mpFileDialogResult->mbAccepted);

				mpFileDialogResult.clear();

				MarkCompleted();
				break;
		}
	}

	const bool mbColdBoot;
	vdrefptr<ATUIFileDialogResult> mpFileDialogResult;
	vdrefptr<ATUIFutureWithResult<bool> > mpConfirmResult;
	vdrefptr<ATUICommandContext> mpCommandContext;
};

void OnCommandOpen(bool forceColdBoot, ATUICommandContext& ctx) {
	vdrefptr<ATUIFutureOpenBootImage> stage(new ATUIFutureOpenBootImage(forceColdBoot, &ctx));

	ATUIPushStep(stage->GetStep());
}

bool ATUIGetFullscreen() { 
	return g_fullscreen;
}

bool ATUIGetDisplayFullscreen() {
	return g_fullscreenDisplay;
}

void ATUISaveMainWindowPlacement() {
	if (g_fullscreen) {
		ATUISaveWindowPlacement("Main window",
			vdrect32 {
				g_ATWindowPreFSPlacement.rcNormalPosition.left,
				g_ATWindowPreFSPlacement.rcNormalPosition.top,
				g_ATWindowPreFSPlacement.rcNormalPosition.right,
				g_ATWindowPreFSPlacement.rcNormalPosition.bottom
			},
			g_ATWindowPreFSPlacement.showCmd == SW_MAXIMIZE,
			g_ATWindowPreFSDpi);
	} else {
		ATUISaveWindowPlacement(g_hwnd, "Main window");
	}
}

void ATSetFullscreen(bool fs) {
	ATUIPane *dispPane = ATGetUIPane(kATUIPaneId_Display);
	ATFrameWindow *frame = NULL;

	if (dispPane) {
		HWND parent = ::GetParent(dispPane->AsNativeWindow().GetHandleW32());

		if (parent) {
			frame = ATFrameWindow::GetFrameWindow(parent);
		}
	}

	if (!frame || !frame->GetPane())
		fs = false;

	if (fs == g_fullscreen)
		return;

	g_pMainWindow->SuspendLayout();

	bool displayFS = fs && !g_ATOptions.mbFullScreenBorderless;

	ATUISetNativeDialogMode(!displayFS);
	ATUIShowModelessWindows(!displayFS, g_hwnd);

	if (frame)
		frame->SetFullScreen(fs);

	g_fullscreenDisplay = displayFS;

	// Disable animations during the transition. This particularly avoids an ugly bounce due to the
	// Win7 restore workaround.
	const BOOL forceDisabledTrue = TRUE;
	DwmSetWindowAttribute(g_hwnd, DWMWA_TRANSITIONS_FORCEDISABLED, &forceDisabledTrue, sizeof forceDisabledTrue);

	DWORD style = GetWindowLong(g_hwnd, GWL_STYLE);
	if (fs) {
		memset(&g_ATWindowPreFSPlacement, 0, sizeof g_ATWindowPreFSPlacement);
		g_ATWindowPreFSPlacement.length = sizeof(g_ATWindowPreFSPlacement);
		GetWindowPlacement(g_hwnd, &g_ATWindowPreFSPlacement);

		switch(g_ATWindowPreFSPlacement.showCmd) {
			case SW_SHOWMINIMIZED:
				g_ATWindowPreFSPlacement.showCmd = SW_SHOWMINNOACTIVE;
				break;

			case SW_SHOW:
				g_ATWindowPreFSPlacement.showCmd = SW_SHOWNA;
				break;

			case SW_SHOWNORMAL:
				g_ATWindowPreFSPlacement.showCmd = SW_SHOWNOACTIVATE;
				break;
		}
		g_ATWindowPreFSPlacement.flags = 0;

		g_ATWindowPreFSDpi = ATUIGetWindowDpiW32(g_hwnd);

		ATUISetMenuFullScreenHidden(true);

		// We must clear WS_CLIPCHILDREN for D3D9 exclusive fullscreen to work on Windows 10 build 1709 since
		// the top-level window has to be used as the device window and we need the child windows to not be
		// excluded from the window region.
		SetWindowLong(g_hwnd, GWL_STYLE, (style & ~(WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN)) | WS_POPUP);
		
		ShowWindow(g_hwnd, SW_RESTORE);

		if (displayFS) {
			SetWindowPos(g_hwnd, NULL, 0, 0, 0, 0, SWP_NOMOVE|SWP_NOSIZE|SWP_FRAMECHANGED|SWP_NOZORDER|SWP_NOACTIVATE);
			SetWindowPos(g_hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE|SWP_NOSIZE);
			BringWindowToTop(g_hwnd);
		} else {
			vdrect32 r { 0, 0, 0, 0 };

			if (g_pDisplay)
				r = g_pDisplay->GetMonitorRect();

			if (!r.empty())
				SetWindowPos(g_hwnd, NULL, r.left, r.top, r.width(), r.height(), SWP_FRAMECHANGED|SWP_NOZORDER|SWP_NOACTIVATE);
			else
				SetWindowPos(g_hwnd, NULL, 0, 0, 0, 0, SWP_NOMOVE|SWP_NOSIZE|SWP_FRAMECHANGED|SWP_NOZORDER|SWP_NOACTIVATE);
		}

		g_fullscreen = true;

		if (displayFS) {
			if (g_pDisplay)
				g_pDisplay->SetFullScreen(true, g_ATOptions.mFullScreenWidth, g_ATOptions.mFullScreenHeight, g_ATOptions.mFullScreenRefreshRate);
			g_sim.SetFrameSkipEnabled(true);
		}
	} else {
		if (g_pDisplay)
			g_pDisplay->SetFullScreen(false);

		SetWindowLong(g_hwnd, GWL_STYLE, (style | WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN) & ~WS_POPUP);
		SetWindowPos(g_hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE|SWP_NOSIZE|SWP_FRAMECHANGED|SWP_NOACTIVATE);

		// We _should_ be able to just use SetWindowPlacement() here. However, there's a weird
		// problem on Windows 7 where the window manager picks up the restored size as the maximized
		// size when the frame is changed, so we force the maximization manually here.
		bool wasMaxed = false;
		if (g_ATWindowPreFSPlacement.showCmd == SW_MAXIMIZE) {
			wasMaxed = true;
			g_ATWindowPreFSPlacement.showCmd = SW_RESTORE;
		}

		SetWindowPlacement(g_hwnd, &g_ATWindowPreFSPlacement);
		if (wasMaxed)
			ShowWindow(g_hwnd, SW_MAXIMIZE);

		g_fullscreen = false;
		ATUISetMenuFullScreenHidden(false);
		g_sim.SetFrameSkipEnabled(true);
	}

	g_winCaptionUpdater->SetFullScreen(g_fullscreen);

	const BOOL forceDisabledFalse = FALSE;
	DwmSetWindowAttribute(g_hwnd, DWMWA_TRANSITIONS_FORCEDISABLED, &forceDisabledFalse, sizeof forceDisabledFalse);

	g_pMainWindow->ResumeLayout();
}

bool ATUICanManipulateWindows() {
	return g_pMainWindow && g_pMainWindow->IsEnabled() && g_pMainWindow->GetActiveFrame() && !g_pMainWindow->GetModalFrame() && !g_pMainWindow->GetFullScreenFrame();
}

bool ATUIIsModalActive() {
	if (!g_pMainWindow)
		return false;

	if (!g_pMainWindow->IsEnabled() || g_pMainWindow->GetModalFrame())
		return true;

	GUITHREADINFO gti { sizeof(GUITHREADINFO) };
	if (GetGUIThreadInfo(GetCurrentThreadId(), &gti)) {
		if (gti.flags & (GUI_INMENUMODE | GUI_INMOVESIZE | GUI_POPUPMENUMODE | GUI_SYSTEMMENUMODE))
			return true;
	}

	return false;
}

void OnCommandAnticVisualizationNext() {
	ATAnticEmulator& antic = g_sim.GetAntic();

	ATAnticEmulator::AnalysisMode mode = (ATAnticEmulator::AnalysisMode)(((int)antic.GetAnalysisMode() + 1) % ATAnticEmulator::kAnalyzeModeCount);
	antic.SetAnalysisMode(mode);

	IATUIRenderer *uir = g_sim.GetUIRenderer();

	if (uir) {
		switch(mode) {
			case ATAnticEmulator::kAnalyzeOff:
				uir->SetStatusMessage(L"DMA analysis disabled");
				break;

			case ATAnticEmulator::kAnalyzeDMATiming:
				uir->SetStatusMessage(L"DMA analysis enabled");
				break;
		}
	}
}

void OnCommandGTIAVisualizationNext() {
	ATGTIAEmulator& gtia = g_sim.GetGTIA();

	ATGTIAEmulator::AnalysisMode mode = (ATGTIAEmulator::AnalysisMode)(((int)gtia.GetAnalysisMode() + 1) % ATGTIAEmulator::kAnalyzeCount);
	gtia.SetAnalysisMode(mode);

	IATUIRenderer *uir = g_sim.GetUIRenderer();

	if (uir) {
		switch(mode) {
			case ATGTIAEmulator::kAnalyzeNone:
				uir->SetStatusMessage(L"Display analysis disabled");
				break;

			case ATGTIAEmulator::kAnalyzeColors:
				uir->SetStatusMessage(L"Color analysis");
				break;

			case ATGTIAEmulator::kAnalyzeLayers:
				uir->SetStatusMessage(L"Layer analysis");
				break;

			case ATGTIAEmulator::kAnalyzeDList:
				uir->SetStatusMessage(L"Display list analysis");
				break;
		}
	}
}

bool ATUIGetAltViewAutoswitchingEnabled() {
	return g_xepViewAutoswitchingEnabled;
}

void ATUISetAltViewAutoswitchingEnabled(bool enabled) {
	g_xepViewAutoswitchingEnabled = enabled;
}

void OnCommandVideoToggleXEP80Output() {
	ATUIToggleAltOutput("xep80");
}

void OnCommandVideoToggleOutputAutoswitching() {
	g_xepViewAutoswitchingEnabled = !g_xepViewAutoswitchingEnabled;
}

void OnCommandVideoEnhancedTextFontDialog() {
	CHOOSEFONTW cf = {sizeof(CHOOSEFONTW)};

	cf.hwndOwner	= (VDZHWND)ATUIGetNewPopupOwner();
	cf.hDC			= NULL;
	cf.lpLogFont	= &g_enhancedTextFont;
	cf.iPointSize	= 0;
	cf.Flags		= CF_FIXEDPITCHONLY | CF_SCREENFONTS | CF_INITTOLOGFONTSTRUCT;

	if (ChooseFontW(&cf)) {
		g_enhancedTextFont.lfWidth			= 0;
		g_enhancedTextFont.lfEscapement		= 0;
		g_enhancedTextFont.lfOrientation	= 0;
		g_enhancedTextFont.lfWeight			= 0;
		g_enhancedTextFont.lfItalic			= FALSE;
		g_enhancedTextFont.lfUnderline		= FALSE;
		g_enhancedTextFont.lfStrikeOut		= FALSE;
		g_enhancedTextFont.lfCharSet		= DEFAULT_CHARSET;
		g_enhancedTextFont.lfOutPrecision	= OUT_DEFAULT_PRECIS;
		g_enhancedTextFont.lfClipPrecision	= CLIP_DEFAULT_PRECIS;
		g_enhancedTextFont.lfQuality		= DEFAULT_QUALITY;
		g_enhancedTextFont.lfPitchAndFamily	= FF_DONTCARE | DEFAULT_PITCH;

		VDRegistryAppKey key("Settings");
		key.setString("Enhanced video: Font family", g_enhancedTextFont.lfFaceName);
		key.setInt("Enhanced video: Font size", g_enhancedTextFont.lfHeight);

		IATDisplayPane *pane = vdpoly_cast<IATDisplayPane *>(ATGetUIPane(kATUIPaneId_Display));
		if (pane)
			pane->UpdateTextModeFont();
	}
}

namespace {
	constexpr ATEnumEntry<uint8> g_scancodeNameTable[] = {
		{ 0x2C, "tab" },
		{ 0x34, "back" },
		{ 0x34, "backspace" },
		{ 0x34, "bksp" },
		{ 0x0C, "enter" },
		{ 0x0C, "return" },
		{ 0x1C, "esc" },
		{ 0x1C, "escape" },
		{ 0x27, "fuji" },
		{ 0x27, "inv" },
		{ 0x27, "invert" },
		{ 0x11, "help" },
		{ 0x76, "clear" },
		{ 0xB4, "del" },
		{ 0xB4, "delete" },
		{ 0xB7, "ins" },
		{ 0xB7, "insert" },
		{ 0x3C, "caps" },
		{ 0x86, "left" },
		{ 0x87, "right" },
		{ 0x8E, "up" },
		{ 0x8F, "down" },
	};

	constexpr auto g_scancodeNameMap = ATCreateEnumLookupTable(g_scancodeNameTable);
}

void Paste(const wchar_t *s, size_t len, bool useCooldown) {
	vdfastvector<wchar_t> pasteChars;

	while(len--) {
		wchar_t c = *s++;

		if (!c)
			continue;

		// fix annoying characters
		int repeat = 1;

		switch(c) {
			case L'\u200B':	// zero width space
			case L'\u200C':	// zero width non-joiner
			case L'\u200D':	// zero width joiner
			case L'\u200E':	// left to right mark
			case L'\u200F':	// right to left mark
				continue;

			case L'\u2010':	// hyphen
			case L'\u2011':	// non-breaking hyphen
			case L'\u2012':	// figure dash
			case L'\u2013':	// en dash
			case L'\u2014':	// em dash
			case L'\u2015':	// horizontal bar
				c = L'-';
				break;

			case L'\u2018':	// left single quotation mark
			case L'\u2019':	// right single quotation mark
				c = L'\'';
				break;

			case L'\u201C':	// left double quotation mark
			case L'\u201D':	// right double quotation mark
				c = L'"';
				break;

			case L'\u2026':	// ellipsis
				c = L'.';
				repeat = 3;
				break;

			case L'\uFEFF':	// byte order mark
				continue;
		}

		while(repeat--)
			pasteChars.push_back(c);
	}

	if (g_enhancedText == 2) {
		IATDisplayPane *pane = vdpoly_cast<IATDisplayPane *>(ATGetUIPane(kATUIPaneId_Display));
		if (pane)
			pane->Paste(pasteChars.data(), pasteChars.size());

		return;
	}

	pasteChars.push_back(0);

	auto& pokey = g_sim.GetPokey();
	wchar_t skipLT = 0;
	uint8 scancodeModifier = 0;

	const wchar_t *t = pasteChars.data();

	while(wchar_t c = *t++) {
		if (c == skipLT) {
			skipLT = 0;
			continue;
		}

		skipLT = 0;

		if (c == '{') {
			const wchar_t *start = t;

			while(*t && *t != '}')
				++t;

			if (*t != '}')
				break;

			VDStringSpanW name(start, t);
			++t;

			while(!name.empty()) {
				if (name[0] == L'+') {
					scancodeModifier |= 0x40;
					name = name.subspan(1);
					continue;
				}

				if (name.subspan(0, 6).comparei(L"shift-") == 0 ||
					name.subspan(0, 6).comparei(L"shift+") == 0) {
					scancodeModifier |= 0x40;
					name = name.subspan(6);
					continue;
				}

				if (name[0] == L'^') {
					scancodeModifier |= 0x80;
					name = name.subspan(1);
					continue;
				}

				if (name.subspan(0, 5).comparei(L"ctrl-") == 0 ||
					name.subspan(0, 5).comparei(L"ctrl+") == 0) {
					scancodeModifier |= 0x80;
					name = name.subspan(5);
					continue;
				}

				auto parseResult = ATParseEnum(ATEnumLookupTable { g_scancodeNameMap.mHashEntries, vdcountof(g_scancodeNameMap.mHashEntries), 0}, name);
				if (parseResult.mValid) {
					uint8 scancode = parseResult.mValue;

					if (scancodeModifier)
						scancode = (scancode & 0x3F) | scancodeModifier;

					pokey.PushKey(scancode, false, true, false, useCooldown);
				} else {
					VDStringW err;
					err.sprintf(L"Paste failed: unrecognized token \"%.*ls\"", (int)name.size(), name.data());
					g_sim.GetUIRenderer()->SetStatusMessage(err.c_str());
				}

				scancodeModifier = 0;
				break;
			}

			continue;
		}

		const uint8 kInvalidScancode = 0xFF;
		uint8 scancode = kInvalidScancode;

		switch(c) {
			case L'\r':
			case L'\n':
				skipLT = c ^ (L'\r' ^ L'\n');
				scancode = 0x0C;
				break;

			case L'\t':			// Tab
				scancode = 0x2C;
				break;

			case L'\x001B':		// Esc
				scancode = 0x1C;
				break;

			default:
				if (ATUIGetDefaultScanCodeForCharacter(c, scancode)) {
					// For control characters $1B-1F and $7B-7F, we need to inject an
					// ESC as these characters are intended to be visible. They've
					// already been converted to scancode, however, which we must
					// match by here.
					switch(scancode) {
						case 0x1C:	// escape
						case 0x8E:	// up arrow
						case 0x8F:	// down arrow
						case 0x86:	// left arrow
						case 0x87:	// right arrow
						case 0x82:	// spade
						case 0x76:	// curved arrow up-left
						case 0x34:	// tall left arrow
						case 0x2C:	// tall right arrow
							pokey.PushKey(0x1C /*esc*/, false, true, false, useCooldown);
							break;
					}
				} else
					scancode = kInvalidScancode;

				break;
		}

		if (scancode != kInvalidScancode)
			pokey.PushKey(scancode | scancodeModifier, false, true, false, useCooldown);

		scancodeModifier = 0;
	}
}

void ATUIPaste(VDStringSpanW text) {
	Paste(text.data(), text.size(), true);
}

void ATUIPaste(VDStringSpanA text) {
	const auto& wtext = VDTextU8ToW(text);

	Paste(wtext.data(), wtext.size(), true);
}

IATDisplayPane *ATUIGetDisplayPane() {
	return vdpoly_cast<IATDisplayPane *>(ATGetUIPane(kATUIPaneId_Display));
}

void ATUIResizeDisplay() {
	IATDisplayPane *pane = ATUIGetDisplayPane();
	if (pane)
		pane->OnSize();
}

///////////////////////////////////////////////////////////////////////////

void OnCommandOpenImage(ATUICommandContext& ctx) {
	OnCommandOpen(false, ctx);
}

void OnCommandBootImage(ATUICommandContext& ctx) {
	OnCommandOpen(true, ctx);
}

bool OnTestCommandQuickLoadState() {
	return g_pATQuickState != nullptr;
}

void OnCommandQuickLoadState() {
	try {
		if (g_pATQuickState)
			g_sim.ApplySnapshot(*g_pATQuickState, nullptr);
	} catch(const MyError& e) {
		e.post(g_hwnd, "Altirra Error");
	}
}

bool ATUIConfirmPartiallyAccurateSnapshot() {
	const ATSnapshotStatus status = g_sim.GetSnapshotStatus();

	if (!status.mbPartialAccuracy)
		return true;

	return ATUIConfirm(
		ATUIGetNewPopupOwner(),
		"snapshotPartialAccuracy",
		L"A disk I/O operation is in progress that may not save correctly. Capture save state anyway?",
		L"Unstable save state"
	);
}

void OnCommandQuickSaveState() {
	if (!ATUIConfirmPartiallyAccurateSnapshot())
		return;

	try {
		vdrefptr<IATSerializable> quickStateInfo;
		g_sim.CreateSnapshot(~g_pATQuickState, ~quickStateInfo);
	} catch(const MyError& e) {
		e.post(g_hwnd, "Altirra Error");
	}
}

void OnCommandLoadState() {
	const VDStringW fn(VDGetLoadFileName("save"_vdfcctypeid, (VDGUIHandle)g_hwnd, L"Load save state",
		g_ATUIFileFilter_LoadState,
		L"atstate2"
		));

	if (!fn.empty()) {
		DoLoad((VDGUIHandle)g_hwnd, fn.c_str(), nullptr, 0, kATImageType_SaveState);
	}
}

void OnCommandSaveState() {
	if (!ATUIConfirmPartiallyAccurateSnapshot())
		return;

	const VDStringW fn(VDGetSaveFileName("save"_vdfcctypeid, (VDGUIHandle)g_hwnd, L"Save save state",
		g_ATUIFileFilter_SaveState,
		L"atstate2"
		));

	if (!fn.empty()) {
		try {
			g_sim.SaveState(fn.c_str());
		} catch(const MyError& e) {
			ATUIShowError((VDGUIHandle)g_hwnd, e);
		}
	}
}

void OnCommandSaveFirmware(int idx) {
	auto storageId = (ATStorageId)(kATStorageId_Firmware + idx);

	if (!g_sim.IsStoragePresent(storageId))
		throw MyError("The selected type of firmware is not present.");

	VDStringW fn(VDGetSaveFileName("rom "_vdfcctypeid, (VDGUIHandle)g_hwnd, L"Save firmware image",
		L"Raw firmware image\0*.rom\0",
		L"rom"));

	if (!fn.empty()) {
		g_sim.SaveStorage(storageId, fn.c_str());
	}
}

void OnCommandSaveFirmwareIDEMain() {
	OnCommandSaveFirmware(0);
}

void OnCommandSaveFirmwareIDESDX() {
	OnCommandSaveFirmware(1);
}

void OnCommandSaveFirmwareU1MB() {
	OnCommandSaveFirmware(2);
}

void OnCommandSaveFirmwareRapidusFlash() {
	OnCommandSaveFirmware(3);
}

void OnCommandExit(ATUICommandContext& ctx) {
	ATUIExit(ctx.mbQuiet);
}

ATDisplayFilterMode ATUIGetDisplayFilterMode() {
	return g_dispFilterMode;
}

void ATUISetDisplayFilterMode(ATDisplayFilterMode mode) {
	g_dispFilterMode = mode;

	IATDisplayPane *pane = vdpoly_cast<IATDisplayPane *>(ATGetUIPane(kATUIPaneId_Display));
	if (pane)
		pane->UpdateFilterMode();
}

int ATUIGetViewFilterSharpness() {
	return g_dispFilterSharpness;
}

void ATUISetViewFilterSharpness(int sharpness) {
	g_dispFilterSharpness = sharpness;

	IATDisplayPane *pane = vdpoly_cast<IATDisplayPane *>(ATGetUIPane(kATUIPaneId_Display));
	if (pane)
		pane->UpdateFilterMode();
}

ATDisplayStretchMode ATUIGetDisplayStretchMode() {
	return g_displayStretchMode;
}

void ATUISetDisplayStretchMode(ATDisplayStretchMode mode) {
	if (g_displayStretchMode != mode) {
		g_displayStretchMode = mode;

		ATUIResizeDisplay();
	}
}

float ATUIGetDisplayZoom() {
	return g_displayZoom;
}

void ATUISetDisplayZoom(float zoom) {
	zoom = std::clamp(zoom, 0.01f, 100.0f);

	if (g_displayZoom != zoom) {
		g_displayZoom = zoom;

		ATUIResizeDisplay();
	}
}

vdfloat2 ATUIGetDisplayPanOffset() {
	return g_displayPan;
}

void ATUISetDisplayPanOffset(const vdfloat2& pan) {
	vdfloat2 pan2;
	pan2.x = std::clamp(pan.x, -10.0f, 10.0f);
	pan2.y = std::clamp(pan.y, -10.0f, 10.0f);

	if (g_displayPan != pan2) {
		g_displayPan = pan2;
	
		ATUIResizeDisplay();
	}
}

void ATUISetOverscanMode(ATGTIAEmulator::OverscanMode mode) {
	g_sim.GetGTIA().SetOverscanMode(mode);
	ATUIResizeDisplay();
}

void OnCommandViewVerticalOverscan(ATGTIAEmulator::VerticalOverscanMode mode) {
	g_sim.GetGTIA().SetVerticalOverscanMode(mode);
	ATUIResizeDisplay();
}

void OnCommandViewTogglePALExtended() {
	g_sim.GetGTIA().SetOverscanPALExtended(!g_sim.GetGTIA().IsOverscanPALExtended());
	ATUIResizeDisplay();
}

void OnCommandViewToggleVSync() {
	ATGTIAEmulator& gtia = g_sim.GetGTIA();

	gtia.SetVsyncEnabled(!gtia.IsVsyncEnabled());
}

bool ATUIGetShowFPS() {
	return g_showFps;
}

void ATUISetShowFPS(bool enabled) {
	g_showFps = enabled;
	g_winCaptionUpdater->SetShowFps(enabled);

	if (enabled)
		g_sim.GetUIRenderer()->SetFpsIndicator(-1.0f);
}

// Autosuggest auto-show-on-edit (test10).  On Windows native, route to the
// pointer-owned ATUIFrontEnd; on SDL3, this same name is provided by
// AltirraSDL/stubs/uiaccessors_stubs.cpp.  Either way settings.cpp can
// exchange the "View: Auto-suggest enabled" key via the same accessor pair.
bool ATUIGetAutoSuggestEnabled() {
	return ATUIGetFrontEnd().IsAutoSuggestEnabled();
}

void ATUISetAutoSuggestEnabled(bool enabled) {
	ATUIGetFrontEnd().SetAutoSuggestEnabled(enabled);
}

void OnCommandViewAdjustWindowSize() {
	if (g_pMainWindow)
		g_pMainWindow->AutoSize();
}

void OnCommandViewResetWindowLayout() {
	ATLoadDefaultPaneLayout();
}


void OnCommandPane(uint32 paneId) {
	ATActivateUIPane(paneId, true);
}

void OnCommandEditCopyFrame() {
	IATDisplayPane *pane = vdpoly_cast<IATDisplayPane *>(ATGetUIPane(kATUIPaneId_Display));
	if (pane)
		pane->CopyFrame(false);
}

void OnCommandEditCopyFrameTrueAspect() {
	IATDisplayPane *pane = vdpoly_cast<IATDisplayPane *>(ATGetUIPane(kATUIPaneId_Display));
	if (pane)
		pane->CopyFrame(true);
}

void OnCommandEditSaveFrame() {
	IATDisplayPane *pane = vdpoly_cast<IATDisplayPane *>(ATGetUIPane(kATUIPaneId_Display));
	if (pane)
		pane->SaveFrame(false);
}

bool ATUISaveFrame(const wchar_t *path) {
	if (!g_sim.GetGTIA().IsLastFrameBufferAvailable())
		return false;

	IATDisplayPane *pane = vdpoly_cast<IATDisplayPane *>(ATGetUIPane(kATUIPaneId_Display));
	if (!pane)
		return false;

	pane->SaveFrame(false, path);
	return true;
}

void OnCommandEditSaveFrameTrueAspect() {
	IATDisplayPane *pane = vdpoly_cast<IATDisplayPane *>(ATGetUIPane(kATUIPaneId_Display));
	if (pane)
		pane->SaveFrame(true);
}

void OnCommandEditDeselect() {
	IATDisplayPane *pane = vdpoly_cast<IATDisplayPane *>(ATGetUIPane(kATUIPaneId_Display));
	if (pane)
		pane->Deselect();
}

void OnCommandEditSelectAll() {
	IATDisplayPane *pane = vdpoly_cast<IATDisplayPane *>(ATGetUIPane(kATUIPaneId_Display));
	if (pane)
		pane->SelectAll();
}

void OnCommandEditCopyText() {
	IATDisplayPane *pane = vdpoly_cast<IATDisplayPane *>(ATGetUIPane(kATUIPaneId_Display));
	if (pane)
		pane->Copy(ATTextCopyMode::ASCII);
}

void OnCommandEditCopyEscapedText() {
	IATDisplayPane *pane = vdpoly_cast<IATDisplayPane *>(ATGetUIPane(kATUIPaneId_Display));
	if (pane)
		pane->Copy(ATTextCopyMode::Escaped);
}

void OnCommandEditCopyHex() {
	IATDisplayPane *pane = vdpoly_cast<IATDisplayPane *>(ATGetUIPane(kATUIPaneId_Display));
	if (pane)
		pane->Copy(ATTextCopyMode::Hex);
}

void OnCommandEditCopyUnicode() {
	IATDisplayPane *pane = vdpoly_cast<IATDisplayPane *>(ATGetUIPane(kATUIPaneId_Display));
	if (pane)
		pane->Copy(ATTextCopyMode::Unicode);
}

void OnCommandEditPasteText() {
	VDStringA s8;
	VDStringW s16;
	bool use16;

	if (ATUIClipGetText(s8, s16, use16)) {
		if (use16)
			Paste(s16.data(), s16.size(), true);
		else {
			const VDStringW& s16c = VDTextAToW(s8);
			Paste(s16c.data(), s16c.size(), true);
		}
	}
}

///////////////////////////////////////////////////////////////////////////

void OnCommandConsoleHoldKeys() {
	ATUIToggleHoldKeys();
}

void OnCommandConsoleBlackBoxDumpScreen() {
	for(IATDeviceButtons *p : g_sim.GetDeviceManager()->GetInterfaces<IATDeviceButtons>(false, false, false))
		p->ActivateButton(kATDeviceButton_BlackBoxDumpScreen, true);
}

void OnCommandConsoleBlackBoxMenu() {
	for(IATDeviceButtons *p : g_sim.GetDeviceManager()->GetInterfaces<IATDeviceButtons>(false, false, false))
		p->ActivateButton(kATDeviceButton_BlackBoxMenu, true);
}

void OnCommandConsoleIDEPlus2SwitchDisks() {
	for(IATDeviceButtons *p : g_sim.GetDeviceManager()->GetInterfaces<IATDeviceButtons>(false, false, false))
		p->ActivateButton(kATDeviceButton_IDEPlus2SwitchDisks, true);
}

void OnCommandConsoleIDEPlus2WriteProtect() {
	for(IATDeviceButtons *p : g_sim.GetDeviceManager()->GetInterfaces<IATDeviceButtons>(false, false, false))
		p->ActivateButton(kATDeviceButton_IDEPlus2WriteProtect, true);
}

void OnCommandConsoleIDEPlus2SDX() {
	for(IATDeviceButtons *p : g_sim.GetDeviceManager()->GetInterfaces<IATDeviceButtons>(false, false, false))
		p->ActivateButton(kATDeviceButton_IDEPlus2SDX, true);
}

void OnCommandConsoleIndusGTId() {
	for(IATDeviceButtons *p : g_sim.GetDeviceManager()->GetInterfaces<IATDeviceButtons>(false, false, false)) {
		p->ActivateButton(kATDeviceButton_IndusGTId, true);
		p->ActivateButton(kATDeviceButton_IndusGTId, false);
	}
}

void OnCommandConsoleIndusGTError() {
	for(IATDeviceButtons *p : g_sim.GetDeviceManager()->GetInterfaces<IATDeviceButtons>(false, false, false)) {
		p->ActivateButton(kATDeviceButton_IndusGTError, true);
		p->ActivateButton(kATDeviceButton_IndusGTError, false);
	}
}

void OnCommandConsoleIndusGTTrack() {
	for(IATDeviceButtons *p : g_sim.GetDeviceManager()->GetInterfaces<IATDeviceButtons>(false, false, false)) {
		p->ActivateButton(kATDeviceButton_IndusGTTrack, true);
		p->ActivateButton(kATDeviceButton_IndusGTTrack, false);
	}
}

void OnCommandConsoleIndusGTBootCPM() {
	for(IATDeviceButtons *p : g_sim.GetDeviceManager()->GetInterfaces<IATDeviceButtons>(false, false, false))
		p->ActivateButton(kATDeviceButton_IndusGTBootCPM, true);
}

void OnCommandConsoleIndusGTChangeDensity() {
	for(IATDeviceButtons *p : g_sim.GetDeviceManager()->GetInterfaces<IATDeviceButtons>(false, false, false))
		p->ActivateButton(kATDeviceButton_IndusGTChangeDensity, true);
}

void OnCommandConsoleHappyToggleFastSlow() {
	for(IATDeviceButtons *p : g_sim.GetDeviceManager()->GetInterfaces<IATDeviceButtons>(false, false, false))
		p->ActivateButton(kATDeviceButton_HappySlow, !p->IsButtonDepressed(kATDeviceButton_HappySlow));
}

void OnCommandConsoleHappyToggleWriteProtect() {
	for(IATDeviceButtons *p : g_sim.GetDeviceManager()->GetInterfaces<IATDeviceButtons>(false, false, false))
		p->ActivateButton(kATDeviceButton_HappyWPEnable, !p->IsButtonDepressed(kATDeviceButton_HappyWPEnable));
}

void OnCommandConsoleHappyToggleWriteEnable() {
	for(IATDeviceButtons *p : g_sim.GetDeviceManager()->GetInterfaces<IATDeviceButtons>(false, false, false))
		p->ActivateButton(kATDeviceButton_HappyWPDisable, !p->IsButtonDepressed(kATDeviceButton_HappyWPDisable));
}

void OnCommandConsoleATR8000Reset() {
	for(IATDeviceButtons *p : g_sim.GetDeviceManager()->GetInterfaces<IATDeviceButtons>(false, false, false)) {
		p->ActivateButton(kATDeviceButton_ATR8000Reset, true);
		p->ActivateButton(kATDeviceButton_ATR8000Reset, false);
	}
}

void OnCommandConsoleXELCFSwap() {
	for(IATDeviceButtons *p : g_sim.GetDeviceManager()->GetInterfaces<IATDeviceButtons>(false, false, false)) {
		p->ActivateButton(kATDeviceButton_XELCFSwap, true);
		p->ActivateButton(kATDeviceButton_XELCFSwap, false);
	}
}

///////////////////////////////////////////////////////////////////////////

void OnCommandDiskDrivesDialog() {
	ATUIShowDiskDriveDialog((VDGUIHandle)g_hwnd);
}

void OnCommandDiskToggleSIOPatch() {
	g_sim.SetDiskSIOPatchEnabled(!g_sim.IsDiskSIOPatchEnabled());
}

void OnCommandDiskToggleSIOOverrideDetection() {
	g_sim.SetDiskSIOOverrideDetectEnabled(!g_sim.IsDiskSIOOverrideDetectEnabled());
}

void OnCommandDiskToggleAccurateSectorTiming() {
	g_sim.SetDiskAccurateTimingEnabled(!g_sim.IsDiskAccurateTimingEnabled());
}

bool ATUIGetDriveSoundsEnabled() {
	const ATDiskInterface& diskIf = g_sim.GetDiskInterface(0);
	return diskIf.AreDriveSoundsEnabled();
}

void ATUISetDriveSoundsEnabled(bool enabled) {
	for(int i=0; i<15; ++i) {
		ATDiskInterface& diskIf = g_sim.GetDiskInterface(i);
		diskIf.SetDriveSoundsEnabled(enabled);
	}
}

void OnCommandDiskToggleDriveSounds() {
	ATUISetDriveSoundsEnabled(!ATUIGetDriveSoundsEnabled());
}

void OnCommandDiskToggleSectorCounter() {
	g_sim.SetDiskSectorCounterEnabled(!g_sim.IsDiskSectorCounterEnabled());
}

class ATUIFutureAttachDisk : public ATUIFuture {
public:
	ATUIFutureAttachDisk(int index)
		: mIndex(index)
	{
	}

	virtual void RunInner() {
		switch(mStage) {
			case 0:
				if (g_sim.IsStorageDirty(ATStorageId(kATStorageId_Disk + mIndex))) {
					VDStringW msg;

					msg.sprintf(L"Modified disk image in D%u: has not been saved. OK to discard it?", mIndex + 1);

					mpConfirmResult = ATUIShowAlertWarningConfirm(msg.c_str(), L"Discarding modified storage");
					Wait(mpConfirmResult);
				}
				++mStage;
				break;

			case 1:
				if (mpConfirmResult && !mpConfirmResult->GetResult()) {
					MarkCompleted();
					break;
				}

				mpConfirmResult.clear();
				mpFileDialogResult = ATUIShowOpenFileDialog("disk"_vdfcctypeid, L"Attach disk image",
					L"All supported types\0*.atr;*.pro;*.atx;*.xfd;*.dcm;*.zip;*.gz;*.atz;*.arc\0"
					L"Atari disk image (*.atr, *.xfd)\0*.atr;*.xfd;*.dcm\0"
					L"Protected disk image (*.pro)\0*.pro\0"
					L"VAPI disk image (*.atx)\0*.atx\0"
					L"Zip archive (*.zip)\0*.zip\0"
					L"Gzip archive (*.gz;*.atz)\0*.gz;*.atz\0"
					L".ARC archive (*.arc)\0*.arc\0"
					L"All files\0*.*\0");

				Wait(mpFileDialogResult);
				++mStage;
				break;

			case 2:
				if (mpFileDialogResult->mbAccepted) {
					DoLoad((VDGUIHandle)g_hwnd, mpFileDialogResult->mPath.c_str(), nullptr, 0, kATImageType_Disk, NULL, mIndex);

					ATAddMRUListItem(mpFileDialogResult->mPath.c_str());
				}
				mpFileDialogResult.clear();

				MarkCompleted();
				break;
		}
	}

	const int mIndex;
	vdrefptr<ATUIFileDialogResult> mpFileDialogResult;
	vdrefptr<ATUIFutureWithResult<bool> > mpConfirmResult;
};

void OnCommandDiskAttach(int index) {
	vdrefptr<ATUIFutureAttachDisk> stage(new ATUIFutureAttachDisk(index));

	ATUIPushStep(stage->GetStep());
}

class ATUIFutureDetachDisk : public ATUIFuture {
public:
	ATUIFutureDetachDisk(int index)
		: mIndex(index)
	{
	}

	virtual void RunInner() {
		switch(mStage) {
			case 0:
				if (mIndex < 0) {
					bool dirtyDisks = false;

					for(int i=0; i<15; ++i) {
						if (g_sim.IsStorageDirty(ATStorageId(kATStorageId_Disk + i))) {
							dirtyDisks = true;
							break;
						}
					}

					if (dirtyDisks) {
						mpConfirmResult = ATUIConfirmDiscardAllStorage(L"OK to discard?", false, kATStorageTypeMask_Disk);
						Wait(mpConfirmResult);
					}
				} else {
					if (g_sim.IsStorageDirty(ATStorageId(kATStorageId_Disk + mIndex))) {
						VDStringW msg;

						msg.sprintf(L"Modified disk image in D%u: has not been saved. Discard it to mount a new image?", mIndex + 1);

						mpConfirmResult = ATUIShowAlertWarningConfirm(msg.c_str(), L"Discarding modified storage");
						Wait(mpConfirmResult);
					}
				}
				++mStage;
				break;

			case 1:
				if (mpConfirmResult && !mpConfirmResult->GetResult()) {
					MarkCompleted();
					break;
				}

				if (mIndex < 0) {
					for(int i=0; i<15; ++i) {
						ATDiskInterface& diskIf = g_sim.GetDiskInterface(i);
						diskIf.UnloadDisk();

						ATDiskEmulator& drive = g_sim.GetDiskDrive(i);
						drive.SetEnabled(false);
					}
				} else {
					ATDiskInterface& diskIf = g_sim.GetDiskInterface(mIndex);
					diskIf.UnloadDisk();

					ATDiskEmulator& drive = g_sim.GetDiskDrive(mIndex);
					drive.SetEnabled(false);
				}

				MarkCompleted();
				break;
		}
	}

	const int mIndex;
	vdrefptr<ATUIFileDialogResult> mpFileDialogResult;
	vdrefptr<ATUIFutureWithResult<bool> > mpConfirmResult;
};

void OnCommandDiskDetach(int index) {
	vdrefptr<ATUIFutureDetachDisk> stage(new ATUIFutureDetachDisk(index));

	ATUIPushStep(stage->GetStep());
}

void OnCommandDiskDetachAll() {
	vdrefptr<ATUIFutureDetachDisk> stage(new ATUIFutureDetachDisk(-1));

	ATUIPushStep(stage->GetStep());
}

void OnCommandDiskRotate(int delta) {
	// find highest drive in use
	int activeDrives = 0;
	for(int i=14; i>=0; --i) {
		if (g_sim.GetDiskDrive(i).IsEnabled() || g_sim.GetDiskInterface(i).GetClientCount() > 1) {
			activeDrives = i+1;
			break;
		}
	}

	IATUIRenderer *uir = g_sim.GetUIRenderer();
	if (!activeDrives) {
		if (uir)
			uir->SetStatusMessage(L"No disk drives are active.");

		return;
	}

	g_sim.RotateDrives(activeDrives, delta);

	if (uir)
		uir->SetStatusMessage(VDStringW().sprintf(L"Rotated to D1: %ls", g_sim.GetDiskInterface(0).GetMountedImageLabel().c_str()).c_str());
}

///////////////////////////////////////////////////////////////////////////

void ATSetVideoStandard(ATVideoStandard mode) {
	// Don't allow switching to PAL or SECAM in 5200 mode!
	if (g_sim.GetHardwareMode() == kATHardwareMode_5200)
		return;

	g_sim.SetVideoStandard(mode);

	ATUIUpdateSpeedTiming();

	IATDisplayPane *pane = vdpoly_cast<IATDisplayPane *>(ATGetUIPane(kATUIPaneId_Display));
	if (pane)
		pane->OnSize();
}

ATUIEnhancedTextMode ATUIGetEnhancedTextMode() {
	return (ATUIEnhancedTextMode)g_enhancedText;
}

void ATUISetEnhancedTextMode(ATUIEnhancedTextMode mode) {
	if (g_enhancedText == mode)
		return;

	g_enhancedText = mode;

	switch(mode) {
		case kATUIEnhancedTextMode_None:
		case kATUIEnhancedTextMode_Hardware:
			g_sim.SetVirtualScreenEnabled(false);
			break;

		case kATUIEnhancedTextMode_Software:
			g_sim.SetVirtualScreenEnabled(true);

			// push a break to attempt to kick out of the OS get byte routine
			g_sim.GetPokey().PushBreak();
			break;
	}
}

///////////////////////////////////////////////////////////////////////////

void OnCommandInputCaptureMouse() {
	IATDisplayPane *pane = vdpoly_cast<IATDisplayPane *>(ATGetUIPane(kATUIPaneId_Display));
	if (pane)
		pane->ToggleCaptureMouse();
}

void OnCommandInputToggleAutoCaptureMouse() {
	g_mouseAutoCapture = !g_mouseAutoCapture;
}

void OnCommandInputInputMappingsDialog() {
	ATUIShowDialogInputMappings(g_hwnd, *g_sim.GetInputManager(), g_sim.GetJoystickManager());
	ATReloadPortMenus();
}

void OnCommandInputInputSetupDialog() {
	ATUIShowDialogInputSetup(g_hwnd, *g_sim.GetInputManager(), g_sim.GetJoystickManager());
}

void OnCommandInputKeyboardDialog() {
	ATUIShowDialogKeyboardOptions((VDGUIHandle)g_hwnd);
}

void OnCommandInputLightPenDialog() {
	ATUIShowDialogLightPen((VDGUIHandle)g_hwnd, g_sim.GetLightPenPort());
}

void OnCommandInputCycleQuickMaps() {
	auto *pIM = g_sim.GetInputManager();

	if (pIM) {
		ATInputMap *pMap = pIM->CycleQuickMaps();

		auto *pUIR = g_sim.GetUIRenderer();

		if (pUIR) {
			if (pMap)
				pUIR->SetStatusMessage((VDStringW(L"Quick map: ") + pMap->GetName()).c_str());
			else
				pUIR->SetStatusMessage(L"Quick maps disabled");
		}
	}
}

///////////////////////////////////////////////////////////////////////////

void OnCommandCheatTogglePMCollisions() {
	g_sim.GetGTIA().SetPMCollisionsEnabled(!g_sim.GetGTIA().ArePMCollisionsEnabled());
}

void OnCommandCheatTogglePFCollisions() {
	g_sim.GetGTIA().SetPFCollisionsEnabled(!g_sim.GetGTIA().ArePFCollisionsEnabled());
}

void OnCommandCheatCheatDialog() {
	g_sim.SetCheatEngineEnabled(true);
	ATUIShowDialogCheater((VDGUIHandle)g_hwnd, g_sim.GetCheatEngine());
}

///////////////////////////////////////////////////////////////////////////

void OnCommandToolsDiskExplorer() {
	ATUIShowDialogDiskExplorer((VDGUIHandle)g_hwnd);
}

void ATUIShowDialogEditAccelerators(const char *preSelectedCommand) {
	vdfastvector<VDAccelToCommandEntry> commands;

	g_ATUICommandMgr.ListCommands(commands);

	static const wchar_t *const kContextNames[]={
		L"Global",
		L"Display",
		L"Debugger",
	};

	VDASSERTCT(vdcountof(kContextNames) == kATUIAccelContextCount);

	if (ATUIShowDialogEditAccelerators((VDGUIHandle)g_hwnd,
		commands.data(),
		(uint32)commands.size(),
		ATUIGetAccelTables(), ATUIGetDefaultAccelTables(), kATUIAccelContextCount, kContextNames, preSelectedCommand))
	{
		ATUISaveAccelTables();
		ATUILoadMenu();
	}
}

void OnCommandToolsKeyboardShortcutsDialog() {
	ATUIShowDialogEditAccelerators(nullptr);
}

void OnCommandToolsSetupWizard() {
	void ATUIShowDialogSetupWizard(VDGUIHandle hParent);

	ATUIShowDialogSetupWizard((VDGUIHandle)g_hwnd);
}

void OnCommandHelpContents() {
	ATShowHelp(g_hwnd, NULL);
}

void OnCommandHelpAbout() {
	ATUIShowDialogAbout((VDGUIHandle)g_hwnd);
}

void OnCommandHelpChangeLog() {
	ATShowChangeLog((VDGUIHandle)g_hwnd);
}

void OnCommandHelpCmdLine() {
	ATUIShowDialogCmdLineHelp((VDGUIHandle)g_hwnd);
}

void OnCommandHelpOnline() {
	ATLaunchURL(AT_PRIMARY_URL);
}

void OnCommandHelpCheckForUpdates() {
	ATUIShowDialogCheckForUpdates((VDGUIHandle)g_hwnd);
}

///////////////////////////////////////////////////////////////////////////

uint32 g_ATDeviceButtonChangeCount = 0;
uint32 g_ATDeviceButtonMask = 0;

bool ATUIGetDeviceButtonSupported(uint32 idx) {
	ATDeviceManager& dm = *g_sim.GetDeviceManager();
	const uint32 cc = dm.GetChangeCounter();

	if (g_ATDeviceButtonChangeCount != cc) {
		g_ATDeviceButtonChangeCount = cc;

		uint32 mask = 0;

		for(IATDeviceButtons *devbtns : dm.GetInterfaces<IATDeviceButtons>(false, false, false))
			mask |= devbtns->GetSupportedButtons();

		g_ATDeviceButtonMask = mask;
	}

	return (g_ATDeviceButtonMask & (1 << idx)) != 0;
}

bool ATUIGetDeviceButtonDepressed(uint32 idx) {
	if (!(g_ATDeviceButtonMask & (1 << idx)))
		return false;

	ATDeviceManager& dm = *g_sim.GetDeviceManager();
	bool depressed = false;

	for(IATDeviceButtons *devbtns : dm.GetInterfaces<IATDeviceButtons>(false, false, false)) {
		if (devbtns->IsButtonDepressed((ATDeviceButton)idx))
			depressed = true;
	}

	return depressed;
}

void ATUIActivateDeviceButton(uint32 idx, bool state) {
	if (!(g_ATDeviceButtonMask & (1 << idx)))
		return;

	ATDeviceManager& dm = *g_sim.GetDeviceManager();
	for(IATDeviceButtons *devbtns : dm.GetInterfaces<IATDeviceButtons>(false, false, false))
		devbtns->ActivateButton((ATDeviceButton)idx, state);
}

///////////////////////////////////////////////////////////////////////////

class D3D9Lock : public VDD3D9Client {
public:
	D3D9Lock() : mpMgr(NULL) {}

	void Lock() {
		if (!mpMgr)
			mpMgr = VDInitDirect3D9(this, NULL, false);
	}

	void Unlock() {
		if (mpMgr) {
			VDDeinitDirect3D9(mpMgr, this);
			mpMgr = NULL;
		}
	}

	void OnPreDeviceReset() {}
	void OnPostDeviceReset() {}
private:
	VDD3D9Manager *mpMgr;
} g_d3d9Lock;

void LoadSettingsEarly() {
	g_enhancedTextFont.lfHeight			= 16;
    g_enhancedTextFont.lfWidth			= 0;
    g_enhancedTextFont.lfEscapement		= 0;
    g_enhancedTextFont.lfOrientation	= 0;
    g_enhancedTextFont.lfWeight			= 0;
    g_enhancedTextFont.lfItalic			= FALSE;
    g_enhancedTextFont.lfUnderline		= FALSE;
    g_enhancedTextFont.lfStrikeOut		= FALSE;
	g_enhancedTextFont.lfCharSet		= DEFAULT_CHARSET;
	g_enhancedTextFont.lfOutPrecision	= OUT_DEFAULT_PRECIS;
	g_enhancedTextFont.lfClipPrecision	= CLIP_DEFAULT_PRECIS;
    g_enhancedTextFont.lfQuality		= DEFAULT_QUALITY;
	g_enhancedTextFont.lfPitchAndFamily	= FF_DONTCARE | DEFAULT_PITCH;
    wcscpy(g_enhancedTextFont.lfFaceName, L"Lucida Console");

	VDRegistryAppKey key("Settings");
	VDStringW family;
	int fontSize;
	if (key.getString("Enhanced video: Font family", family)
		&& (fontSize = key.getInt("Enhanced video: Font size", 0))) {

		g_enhancedTextFont.lfHeight = fontSize;
		vdwcslcpy(g_enhancedTextFont.lfFaceName, family.c_str(), sizeof(g_enhancedTextFont.lfFaceName)/sizeof(g_enhancedTextFont.lfFaceName[0]));
	}
}

uint64 ATGetCumulativeCPUTime() {
	FILETIME ct;
	FILETIME et;
	FILETIME kt;
	FILETIME ut;
	if (!::GetProcessTimes(GetCurrentProcess(), &ct, &et, &kt, &ut))
		return 0;

	return ((uint64)kt.dwHighDateTime << 32) + kt.dwLowDateTime
		+ ((uint64)ut.dwHighDateTime << 32) + ut.dwLowDateTime;
}

// The joystick poll is normally done once a frame. However, when we aren't
// running, we need to do an idle poll. We poll only a 100ms timer normally
// and speed that up to 10ms for five seconds when activity is detected.
VDLazyTimer g_joystickPollTimer;
uint32 g_joystickPollLastFast;
bool g_joystickPollActive;
bool g_joystickPollFast;
uint32 g_joystickLastActivity;

void ATPollJoysticks() {
	auto *jm = g_sim.GetJoystickManager();
	if (!jm)
		return;

	switch(jm->Poll()) {
		case IATJoystickManager::kPollResult_NoControllers:
			g_joystickPollTimer.Stop();
			break;

		case IATJoystickManager::kPollResult_NoActivity:
			if (g_joystickPollFast && VDGetCurrentTick() - g_joystickPollLastFast > 5000) {
				g_joystickPollFast = false;
				g_joystickPollTimer.SetPeriodicFn(ATPollJoysticks, 100);
			}
			break;

		case IATJoystickManager::kPollResult_OK:
			g_joystickPollLastFast = VDGetCurrentTick();
			if (!g_joystickPollFast) {
				g_joystickPollFast = true;
				g_joystickPollTimer.SetPeriodicFn(ATPollJoysticks, 10);
			}
			break;
	}
}

void ATBeginIdleJoystickPoll() {
	if (g_joystickPollActive)
		return;
	g_joystickPollActive = true;

	g_joystickPollLastFast = VDGetCurrentTick();
	g_joystickPollFast = true;
	g_joystickPollTimer.SetPeriodicFn(ATPollJoysticks, 10);
}

void ATEndIdleJoystickPoll() {
	if (g_joystickPollActive) {
		g_joystickPollActive = false;
		g_joystickPollTimer.Stop();
	}
}

void ATOnJoystickActivity() {
	uint32 t = VDGetCurrentTick();

	if (t - g_joystickLastActivity >= 2000) {
		g_joystickLastActivity = t;

		// send dummy mouse input to reset the screensaver timer
		INPUT input = {};
		input.type = INPUT_MOUSE;
		input.mi.dx = 0;
		input.mi.dy = 0;
		input.mi.mouseData = 0;
		input.mi.dwFlags = MOUSEEVENTF_MOVE;
		input.mi.time = 0;
		input.mi.dwExtraInfo = 0;

		SendInput(1, &input, sizeof input);
	}
}

void ATInitJoysticks() {
	vdautoptr<IATJoystickManager> jm { ATCreateJoystickManager() };

	if (!jm->Init(g_hwnd, g_sim.GetInputManager()))
		return;

	jm->SetOnActivity(ATOnJoystickActivity);

	g_sim.SetJoystickManager(jm.release());
}

void ATShutdownJoysticks() {
	IATJoystickManager *jm = g_sim.GetJoystickManager();

	if (jm) {
		g_sim.SetJoystickManager(nullptr);

		delete jm;
	}
}

int RunMainLoop2(HWND hwnd) {
	uint32 ticks = 0;

	ATAnticEmulator& antic = g_sim.GetAntic();
	uint32 lastFrame = antic.GetPresentedFrameCounter();
	uint32 frameTimeErrorAccum = 0;
	sint64 error = 0;
	uint64 secondTime = VDGetPreciseTicksPerSecondI();
	float invSecondTime = 1.0f / (float)secondTime;

	uint64 lastTime = VDGetPreciseTick();
	uint64 lastStatusTime = lastTime;
	uint32 lastStatusTick = 0;

	const sint64 minStatusTime = secondTime / 10;

	sint64 nextFrameTime;
	uint32 nextTickUpdate = 60;
	bool nextFrameTimeValid = false;
	bool updateScreenPending = false;
	uint64 lastCPUTime = 0;

	g_winCaptionUpdater->Update(true, 0, 0, 0);

	int rcode = 0;
	bool lastIsRunning = false;
	bool waitingForFrameReady = false;

	HANDLE hTimer = nullptr;

	if (g_ATCVEngineUseWaitableTimer) {
		if (g_ATCVEngineUseWaitableTimerHighRes) {
			// This requires at least Windows 10 1803 (RS4) or later.
			if (VDIsAtLeast10_1803W32())
				hTimer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
		}

		if (!hTimer)
			hTimer = CreateWaitableTimer(nullptr, FALSE, nullptr);
	}

	g_sim.SetOnAdvanceUnblocked(
		[&] {
			if (waitingForFrameReady) {
				waitingForFrameReady = false;

				PostThreadMessage(GetCurrentThreadId(), WM_NULL, 0, 0);
			}
		}
	);

	g_pATIdle = [&](bool modalLoop) -> bool {
		if (modalLoop && g_ATOptions.mbPauseDuringMenu)
			return false;

		bool isRunning = g_sim.IsRunning();
		if (!isRunning)
			ATUIFlushDisplay();

		if (ATUIGetQueue().Run()) {
			ATBeginIdleJoystickPoll();
			return true;
		}

		if (isRunning)
			ATEndIdleJoystickPoll();

		if (isRunning != lastIsRunning) {
			if (isRunning) {
				timeBeginPeriod(1);
			} else {
				timeEndPeriod(1);
				ATBeginIdleJoystickPoll();
			}

			lastIsRunning = isRunning;
		}

		const bool turbo = g_sim.IsTurboModeEnabled();
		if (isRunning && (g_winActive || !g_pauseInactive)) {
			uint64 curTime;
			bool curTimeValid = false;

			g_ATAutoFrameFlipping = true;

			uint32 frame = antic.GetPresentedFrameCounter();
			if (frame != lastFrame) {
				ATUIGetFrontEnd().CheckRecordingExceptions();

				if (g_pDisplay)
					ATUIAdaptSpeedTiming(g_pDisplay->GetVSyncStatus());

				IATDisplayPane *pane = vdpoly_cast<IATDisplayPane *>(ATGetUIPane(kATUIPaneId_Display));
				if (pane)
					pane->UpdateTextDisplay(g_enhancedText != 0);

				lastFrame = frame;

				curTime = VDGetPreciseTick();
				curTimeValid = true;

				++ticks;

				if ((sint32)(ticks - nextTickUpdate) >= 0) {
					sint64 deltaStatusTime = curTime - lastStatusTime;

					if (deltaStatusTime >= minStatusTime) {
						float fps = 0;
						float cpu = 0;

						if (g_showFps) {
							float deltaPreciseTicks = (float)deltaStatusTime;
							fps = (float)secondTime / deltaPreciseTicks * (ticks - lastStatusTick);

							uint64 newCPUTime = ATGetCumulativeCPUTime();

							sint64 deltaCPUTime = newCPUTime - lastCPUTime;
							lastCPUTime = newCPUTime;

							cpu = ((float)deltaCPUTime / 10000000.0f) / (deltaPreciseTicks * invSecondTime) * 100.0f;
						}

						g_sim.GetUIRenderer()->SetFpsIndicator(g_fullscreen && g_showFps ? fps : -1.0f);

						if (!g_fullscreen)
							g_winCaptionUpdater->Update(true, ticks, fps, cpu);

						lastStatusTime = curTime;
						lastStatusTick = ticks;
					}

					nextTickUpdate = ticks - ticks % 60 + 60;
				}

				sint64 lastFrameDuration = curTime - lastTime;
				lastTime = curTime;

				error += lastFrameDuration - g_frameTicks;
				frameTimeErrorAccum += g_frameSubTicks;

				if (frameTimeErrorAccum >= 0x10000) {
					frameTimeErrorAccum &= 0xFFFF;

					--error;
				}

				if (error > g_frameErrorBound || error < -g_frameErrorBound)
					error = -g_frameTicks;

				nextFrameTimeValid = false;
				if (turbo)
					error = 0;
				else if (error < 0) {
					nextFrameTimeValid = true;
					nextFrameTime = curTime - error;

					if (hTimer) {
						int timerTicks = -(sint64)((-error * 10000000 + secondTime / 2) / secondTime);

						if (timerTicks >= 0) {
							nextFrameTimeValid = false;
						} else {
							LARGE_INTEGER deadline { .QuadPart = timerTicks };
							SetWaitableTimerEx(hTimer, &deadline, 0, nullptr, nullptr, nullptr, 0);
						}
					}
				}
			}

			bool dropFrame = g_sim.IsTurboModeEnabled() && (lastFrame % std::clamp<uint32>(g_ATCVEngineTurboFPSDivisor, 1, 100)) != 0;
			if (!turbo && nextFrameTimeValid) {
				if (hTimer) {
					ATProfileBeginRegion(kATProfileRegion_Idle);
					bool timerElapsed = ::MsgWaitForMultipleObjects(1, &hTimer, FALSE, INFINITE, QS_ALLINPUT) == WAIT_OBJECT_0;
					ATProfileEndRegion(kATProfileRegion_Idle);

					if (!timerElapsed)
						return true;

					nextFrameTimeValid = false;
				} else {
					if (!curTimeValid) {
						curTime = VDGetPreciseTick();
						curTimeValid = true;
					}

					sint64 ticksToNextFrame = nextFrameTime - curTime;

					if (ticksToNextFrame <= 0 || ticksToNextFrame > g_frameTimeout) {
						// we're already at or past next frame, or it's way off (>1 second);
						// just render
						nextFrameTimeValid = false;
					} else {
						int msToDelay = (int)(ticksToNextFrame * 1000 / secondTime);

						if (msToDelay <= 0) {
							// sub-1ms delay not possible, so just render frame
							nextFrameTimeValid = false;
						} else {
							// delay to next frame
							ATProfileBeginRegion(kATProfileRegion_Idle);
							::MsgWaitForMultipleObjects(0, NULL, FALSE, msToDelay, QS_ALLINPUT);
							ATProfileEndRegion(kATProfileRegion_Idle);
							return true;
						}
					}
				}
			}

			if (g_sim.GetAntic().GetBeamY() == 248)
				ATProfileMarkEvent(kATProfileEvent_BeginFrame);

			ATProfileBeginRegion(kATProfileRegion_Simulation);
			ATOSTraceSimulateBegin();
			ATSimulator::AdvanceResult ar = g_sim.Advance(dropFrame);
			ATOSTraceSimulateEnd();
			ATProfileEndRegion(kATProfileRegion_Simulation);

			if (ar == ATSimulator::kAdvanceResult_Stopped) {
				updateScreenPending = !g_sim.IsPaused();
			} else {
				updateScreenPending = false;

				if (ar == ATSimulator::kAdvanceResult_WaitingForFrame) {
					waitingForFrameReady = true;

					ATProfileBeginRegion(kATProfileRegion_IdleFrameDelay);
					::MsgWaitForMultipleObjects(0, NULL, FALSE, INFINITE, QS_ALLINPUT);
					ATProfileEndRegion(kATProfileRegion_IdleFrameDelay);
				}
			}

			return true;
		} else {
			if (ATGetDebugger()->Tick())
				return true;

			g_ATAutoFrameFlipping = false;
			ATUIFlushDisplay();

			if (ATIsDebugConsoleActive()) {
				if (g_fullscreen)
					ATSetFullscreen(false);

				IATDisplayPane *pane = vdpoly_cast<IATDisplayPane *>(ATGetUIPane(kATUIPaneId_Display));
				if (pane)
					pane->UpdateTextDisplay(g_enhancedText != 0);
			}

			if (updateScreenPending) {
				updateScreenPending = false;
				g_sim.GetGTIA().UpdateScreen(true, false);
			}

			g_winCaptionUpdater->Update(false, 0, 0, 0);
			nextTickUpdate = ticks - ticks % 60;

			g_sim.FlushDeferredEvents();
		}

		return false;
	};

	for(;;) {
		if (!ATUIProcessMessages(false, rcode))
			break;

		if (!g_ATCmdLineRead) {
			g_ATCmdLineRead = true;

			ATUIProcessCommandLine(std::move(g_ATCmdLine));
		}

		if (!g_pATIdle(false))
			WaitMessage();

	}

	ATUICancelProcessCommandLine();

	g_sim.SetOnAdvanceUnblocked(nullptr);

	g_pATIdle = nullptr;

	if (hTimer)
		CloseHandle(hTimer);

	if (lastIsRunning) {
		ATEndIdleJoystickPoll();
		timeEndPeriod(1);
	}

	return 0;
}

int RunMainLoop(HWND hwnd) {
	int rc;

	AT_EXCEPTIONFILTER_TRY {
		rc = RunMainLoop2(hwnd);
	} AT_EXCEPTIONFILTER_EXCEPT {
	}

	return rc;
}

int RunInstance(int nCmdShow, ATSettingsCategory categoriesToIgnore);

void ATUISetCommandLine(const wchar_t *s) {
	g_ATCmdLine.Init(s);
	g_ATCmdLineRead = false;
}

bool ATInitRegistry(ATSettingsCategory& categoriesToIgnore) {
	// setup registry
	VDRegistryAppKey::setDefaultKey("Software\\virtualdub.org\\Altirra\\");

	bool resetAll = g_ATCmdLine.FindAndRemoveSwitch(L"resetall");

	const wchar_t *portableAltFile = nullptr;
	g_ATCmdLine.FindAndRemoveSwitch(L"portablealt", portableAltFile);

	g_ATRegistryTemp = g_ATCmdLine.FindAndRemoveSwitch(L"portabletemp");
	const bool portable = g_ATRegistryTemp || g_ATCmdLine.FindAndRemoveSwitch(L"portable");

	// Check for any switches other than /resetall, /portable, and /portablealt
	g_ATCmdLineHadAnything = g_ATCmdLine.GetCount() > 1;

	if (portableAltFile)
		g_ATRegistryPathPortable = VDGetFullPath(portableAltFile);
	else
		g_ATRegistryPathPortable = ATSettingsGetDefaultPortablePath();

	if (portableAltFile || portable || VDDoesPathExist(g_ATRegistryPathPortable.c_str())) {
		g_pATRegistryMemory = new VDRegistryProviderMemory;
		VDSetRegistryProvider(g_pATRegistryMemory);

		ATSettingsSetInPortableMode(true);

		if (!resetAll && !g_ATRegistryTemp && VDDoesPathExist(g_ATRegistryPathPortable.c_str())) {
			try {
				ATUILoadRegistry(g_ATRegistryPathPortable.c_str());
			} catch(const MyError& err) {
				VDStringW message;

				message.sprintf(L"There was an error loading the settings file:\n\n%ls\n\nDo you want to continue? If so, settings will be reset to defaults and may not be saved.", err.wc_str());
				if (IDYES != MessageBox(NULL, message.c_str(), _T("Altirra Warning"), MB_YESNO | MB_ICONWARNING))
					return false;
			}
		}

		ATSetFirmwarePathPortabilityMode(true);
	} else {
		if (!resetAll)
			resetAll = ATSettingsIsResetPending();

		if (resetAll)
			SHDeleteKey(HKEY_CURRENT_USER, _T("Software\\virtualdub.org\\Altirra"));
	}

	g_ATRegistryHadAnything = VDRegistryAppKey("", false).isReady();

	const wchar_t *resetTags = nullptr;
	if (g_ATCmdLine.FindAndRemoveSwitch(L"reset", resetTags)) {
		VDStringRefW resetTagsRef(resetTags);
		VDStringRefW resetToken;

		for(;;) {
			bool didSplit = resetTagsRef.split(L',', resetToken);

			if (!didSplit)
				resetToken = resetTagsRef;

			if (resetToken == L"windowlayouts") {
				VDRegistryAppKey appKey;
				appKey.removeKeyRecursive("Pane layouts 2");
			} else if (resetToken == L"display") {
				VDRegistryAppKey appKey("Settings");
				vdvector<VDStringA> valuesToDelete;

				VDRegistryValueIterator it(appKey);

				while(const char *valueName = it.Next()) {
					if (!vdstricmp(valueName, "Display:", 8))
						valuesToDelete.emplace_back(valueName);
				}

				for(const VDStringA& valueName : valuesToDelete)
					appKey.removeValue(valueName.c_str());
			} else if (resetToken == L"advconfig") {
				ATResetConfigVars();
			} else if (resetToken == L"devices") {
				categoriesToIgnore = ATSettingsCategory(categoriesToIgnore | kATSettingsCategory_Devices);
			} else {
				VDStringW msg;
				msg.sprintf(L"Unknown settings type to reset: '%*ls'", (int)resetToken.size(), resetToken.data());

				MessageBoxW(nullptr, msg.c_str(), L"Altirra Warning", MB_OK | MB_ICONWARNING);
			}

			if (!didSplit)
				break;
		}
	}

	return true;
}

void ATShutdownRegistry() {
	bool migrated = false;

	if (ATSettingsIsMigrationScheduled()) {
		ATUIMigrateSettings();
		migrated = true;
	}

	const bool shouldReset = ATSettingsIsResetPending();

	if (g_pATRegistryMemory) {
		if (migrated) {
			// if we migrated, we should delete
			VDRemoveFile(g_ATRegistryPathPortable.c_str());
		} else if (!g_ATRegistryTemp) {
			try {
				if (shouldReset) {
					// reset pending -- truncate the file instead of saving
					VDFile f(g_ATRegistryPathPortable.c_str(), nsVDFile::kWrite | nsVDFile::kDenyAll | nsVDFile::kTruncateExisting);
				} else {
					// no reset or migration -- save registry
					ATUISaveRegistry(g_ATRegistryPathPortable.c_str());
				}
			} catch(const MyError&) {
			}
		}

		VDSetRegistryProvider(nullptr);
		delete g_pATRegistryMemory;
		g_pATRegistryMemory = nullptr;
	} else {
		if (shouldReset || migrated)
			SHDeleteKey(HKEY_CURRENT_USER, _T("Software\\virtualdub.org\\Altirra"));
	}
}

void ATInitCPUOptions() {
	uint32 cpuext = CPUCheckForExtensions();

	const wchar_t *token;
	if (g_ATCmdLine.FindAndRemoveSwitch(L"hostcpu", token)) {
#if VD_CPU_X86 || VD_CPU_X64
		if (!vdwcsicmp(token, L"none"))
			cpuext = 0;
		else if (!vdwcsicmp(token, L"mmx"))
			cpuext &= CPUF_SUPPORTS_MMX;
		else if (!vdwcsicmp(token, L"isse"))
			cpuext &= CPUF_SUPPORTS_MMX | CPUF_SUPPORTS_INTEGER_SSE;
		else if (!vdwcsicmp(token, L"sse"))
			cpuext &= CPUF_SUPPORTS_MMX | CPUF_SUPPORTS_INTEGER_SSE | CPUF_SUPPORTS_SSE
					| CPUF_SUPPORTS_SSE2;
		else if (!vdwcsicmp(token, L"sse2"))
			cpuext &= CPUF_SUPPORTS_MMX | CPUF_SUPPORTS_INTEGER_SSE | CPUF_SUPPORTS_SSE
					| CPUF_SUPPORTS_SSE2;
		else if (!vdwcsicmp(token, L"sse3"))
			cpuext &= CPUF_SUPPORTS_MMX | CPUF_SUPPORTS_INTEGER_SSE | CPUF_SUPPORTS_SSE
					| CPUF_SUPPORTS_SSE2 | CPUF_SUPPORTS_SSE3;
		else if (!vdwcsicmp(token, L"ssse3"))
			cpuext &= CPUF_SUPPORTS_MMX | CPUF_SUPPORTS_INTEGER_SSE | CPUF_SUPPORTS_SSE
					| CPUF_SUPPORTS_SSE2 | CPUF_SUPPORTS_SSE3 | CPUF_SUPPORTS_SSSE3;
		else if (!vdwcsicmp(token, L"sse41"))
			cpuext &= CPUF_SUPPORTS_MMX | CPUF_SUPPORTS_INTEGER_SSE | CPUF_SUPPORTS_SSE
					| CPUF_SUPPORTS_SSE2 | CPUF_SUPPORTS_SSE3 | CPUF_SUPPORTS_SSSE3 | CPUF_SUPPORTS_SSE41;
		else if (!vdwcsicmp(token, L"sse42"))
			cpuext &= CPUF_SUPPORTS_MMX | CPUF_SUPPORTS_INTEGER_SSE | CPUF_SUPPORTS_SSE
					| CPUF_SUPPORTS_SSE2 | CPUF_SUPPORTS_SSE3 | CPUF_SUPPORTS_SSSE3 | CPUF_SUPPORTS_SSE41
					| CPUF_SUPPORTS_CLMUL;
		else if (!vdwcsicmp(token, L"avx"))
			cpuext &= CPUF_SUPPORTS_MMX | CPUF_SUPPORTS_INTEGER_SSE | CPUF_SUPPORTS_SSE
					| CPUF_SUPPORTS_SSE2 | CPUF_SUPPORTS_SSE3 | CPUF_SUPPORTS_SSSE3 | CPUF_SUPPORTS_SSE41
					| CPUF_SUPPORTS_CLMUL | CPUF_SUPPORTS_AVX;
		else if (!vdwcsicmp(token, L"avx2"))
			cpuext &= CPUF_SUPPORTS_MMX | CPUF_SUPPORTS_INTEGER_SSE | CPUF_SUPPORTS_SSE
					| CPUF_SUPPORTS_SSE2 | CPUF_SUPPORTS_SSE3 | CPUF_SUPPORTS_SSSE3 | CPUF_SUPPORTS_SSE41
					| CPUF_SUPPORTS_CLMUL | CPUF_SUPPORTS_LZCNT | CPUF_SUPPORTS_AVX | CPUF_SUPPORTS_AVX2;
#endif
	}

	CPUEnableExtensions(cpuext);
	VDFastMemcpyAutodetect();
}

void ATInitDisplayOptions() {
	if (g_ATCmdLine.FindAndRemoveSwitch(L"gdi")) {
		g_ATOptions.mbDirty = true;
		g_ATOptions.mbDisplayD3D9 = false;
		g_ATOptions.mbDisplay3D = false;
	} else if (g_ATCmdLine.FindAndRemoveSwitch(L"d3d9")) {
		g_ATOptions.mbDirty = true;
		g_ATOptions.mbDisplayD3D9 = true;
		g_ATOptions.mbDisplay3D = false;
	} else if (g_ATCmdLine.FindAndRemoveSwitch(L"d3d11")) {
		g_ATOptions.mbDirty = true;
		g_ATOptions.mbDisplayD3D9 = false;
		g_ATOptions.mbDisplay3D = true;
	}
}

void ATInitRand() {
	// Visual C++'s rand() is a 48-bit LCG with a factor of 214013 and returning
	// the high 16-bits, which unfortunately means that the output of the first
	// call to rand() highly tracks the argument to srand() x3. This makes
	// srand(time()) unacceptable, even for our minimal requirements. To fix
	// this, we mix in the system uptime, thread ID, and process ID.

	const struct ChecksumData {
		time_t t;
		uint32 pid;
		uint32 tid;
	} data = {
		time(nullptr),
		VDGetCurrentProcessId(),
		VDGetCurrentThreadID()
	};

	const uint32 hash32 = VDCRCTable::CRC32.CRC(&data, sizeof data);

	srand((unsigned)hash32);
}

///////////////////////////////////////////////////////////////////////////

#ifdef ATNRELEASE
extern int ATTestMain();
#endif

int CALLBACK WinMain(HINSTANCE, HINSTANCE, LPSTR, int nCmdShow) {
#ifdef ATNRELEASE
	// In Debug and Profile configurations, the main Altirra executable now also
	// doubles as the unit test driver so that code within the main project can
	// be tested. However, we want unit tests to run in console mode for easier
	// logging and batching. To deal with this, we check the PE header to see if
	// the executable has been built or changed to the console subsystem. If so,
	// we invoke the test driver instead of the main executable.
	const IMAGE_NT_HEADERS *imageHeaders = (const IMAGE_NT_HEADERS *)((char *)&__ImageBase + ((const IMAGE_DOS_HEADER *)&__ImageBase)->e_lfanew);

	if (imageHeaders->OptionalHeader.Subsystem == IMAGE_SUBSYSTEM_WINDOWS_CUI)
		return ATTestMain();
#endif

	ATInitSaveStateDeserializer();

	ATInitCRTHooks();

	extern void ATSetOutputDebugStringFilter();
	ATSetOutputDebugStringFilter();

	ATUISetCommandLine(GetCommandLineW());

	BOOL suppressTimerExceptions = FALSE;
	::SetUserObjectInformation(GetCurrentProcess(), UOI_TIMERPROC_EXCEPTION_SUPPRESSION, &suppressTimerExceptions, sizeof suppressTimerExceptions);
	::SetUnhandledExceptionFilter(ATUnhandledExceptionFilter);

	const wchar_t *token = nullptr;
	if (g_ATCmdLine.FindAndRemoveSwitch(L"startuplog", token)) {
		ATStartupLogInit(token);
		ATStartupLog("Startup logging enabled.");
	}

	if (g_ATCmdLine.FindAndRemoveSwitch(L"fullheapdump"))
		ATExceptionFilterSetFullHeapDump(true);

	ATInitCPUOptions();

	VDInitThunkAllocator();

	ATStartupLog("Initializing OLE.");
	OleInitialize(NULL);

	ATInitRand();
	ATVFSInstallAtfsHandler();

	InitCommonControls();
	BufferedPaintInit();

	ATStartupLog("Initializing themes");
	ATUIInitThemes();
	ATUIInitControlStylesW32();

	extern void ATUITemporarilyMountVHDImageImplW32(VDZHWND hwnd, const wchar_t *path, bool readOnly);

	int rval = 0;

	const bool mountRO = g_ATCmdLine.FindAndRemoveSwitch(L"mountvhdro", token);
	const bool mountRW = !mountRO && g_ATCmdLine.FindAndRemoveSwitch(L"mountvhdrw", token);
	const bool allowElevation = !g_ATCmdLine.FindAndRemoveSwitch(L"noelevation");

	if (mountRO || mountRW) {
		const wchar_t *parentWinToken = nullptr;
		g_ATCmdLine.FindAndRemoveSwitch(L"parentwindow", parentWinToken);

		VDGUIHandle h = nullptr;
		if (parentWinToken)
			h = (VDGUIHandle)wcstoull(parentWinToken, nullptr, 0);

		ATUITemporarilyMountVHDImageImplW32((VDZHWND)h, token, mountRO);
	} else if (g_ATCmdLine.FindAndRemoveSwitch(L"showfileassocdlg", token)) {
		// We don't _really_ need to do this as Win64 kernel handles are documented as
		// being 32-bit significant, but it's cheap.
		unsigned long long hwndParent;
		wchar_t dummy;

		if (1 == swscanf(token, L"%llx%c", &hwndParent, &dummy))
			ATUIShowDialogSetFileAssociations((VDGUIHandle)(UINT_PTR)hwndParent, false, false);
	} else if (g_ATCmdLine.FindAndRemoveSwitch(L"registersysfileassocs")) {
		ATRegisterFileAssociations(allowElevation, false);
	} else if (g_ATCmdLine.FindAndRemoveSwitch(L"registeruserfileassocs")) {
		ATRegisterFileAssociations(allowElevation, true);
	} else if (g_ATCmdLine.FindAndRemoveSwitch(L"unregisterfileassocs")) {
		ATRemoveAllFileAssociations(allowElevation);
	} else if (g_ATCmdLine.FindAndRemoveSwitch(L"removefileassocs", token)) {
		unsigned long long hwndParent;
		wchar_t dummy;

		if (1 == swscanf(token, L"%llx%c", &hwndParent, &dummy))
			ATUIShowDialogRemoveFileAssociations((VDGUIHandle)(UINT_PTR)hwndParent, false, false);
	} else if (g_ATCmdLine.FindAndRemoveSwitch(L"?")) {
		// This is normally done by ATInitUIPanes(), but we are skipping that here.
		HMODULE hm = VDLoadSystemLibraryW32("msftedit");

		OnCommandHelpCmdLine();

		if (hm)
			FreeLibrary(hm);
	} else {
		ATSettingsCategory categoriesToIgnore {};

		if (!ATInitRegistry(categoriesToIgnore)) {
			rval = 5;
		} else {
			if (g_ATCmdLine.FindAndRemoveSwitch(L"advconfig")) {
				extern void ATUIShowDialogAdvancedConfiguration(VDGUIHandle h);
				ATUIShowDialogAdvancedConfiguration(nullptr);
			} else {
				ATStartupLog("Loading config var overrides");
				ATLoadConfigVars();

				ATStartupLog("Loading options");
				ATOptionsLoad();

				ATStartupLog("Loading settings");
				LoadSettingsEarly();

				ATInitDisplayOptions();

				bool singleInstance = g_ATOptions.mbSingleInstance;
				if (g_ATCmdLine.FindAndRemoveSwitch(L"singleinstance") ||
					g_ATCmdLine.FindAndRemoveSwitch(L"si"))
				{
					singleInstance = true;
				}
				else if (g_ATCmdLine.FindAndRemoveSwitch(L"nosingleinstance") ||
					g_ATCmdLine.FindAndRemoveSwitch(L"nosi"))
				{
					singleInstance = false;
				}

				ATNotifyOtherInstanceResult notifyResult = ATNotifyOtherInstanceResult::NoOtherInstance;
				if (singleInstance) {
					notifyResult = ATNotifyOtherInstance(g_ATCmdLine);

					if (notifyResult == ATNotifyOtherInstanceResult::Failed) {
						ATUIShowError2(nullptr,
							L"The single instance option is enabled, but the running instance of the program is not responding.",
							L"Unable to launch"
						);
						rval = 20;
					} else if (notifyResult == ATNotifyOtherInstanceResult::OtherInstanceElevated) {
						ATUIShowError2(nullptr,
							L"The single instance option is enabled, but the running instance was elevated with admin privileges. "
								L"For security reasons, non-elevated programs can't trigger a launch on an elevated program.",
							L"Unable to launch"
						);
						rval = 20;
					}
				}

				if (notifyResult == ATNotifyOtherInstanceResult::NoOtherInstance) {
					if (g_ATCmdLine.FindAndRemoveSwitch(L"lockd3d"))
						g_d3d9Lock.Lock();

					ATStartupLog("Running instance");
					rval = RunInstance(nCmdShow, categoriesToIgnore);
				}
			}

			ATStartupLog("Shutting down registry");
			ATShutdownRegistry();
		}
	}

	ATStartupLog("Shutting down UI");
	BufferedPaintUnInit();
	ATUIShutdownControlStylesW32();

	ATStartupLog("Shutting down OLE");
	OleUninitialize();

	ATStartupLog("Shutting down thunk allocator");
	VDShutdownThunkAllocator();

	ATStartupLog("Exiting (end of log).");
	ATStartupLogShutdown();

	return rval;
}

class ATSystemLibraryLockW32 {
public:
	ATSystemLibraryLockW32(const char *name)
		: mhmod(VDLoadSystemLibraryW32(name))
	{
	}

	~ATSystemLibraryLockW32() {
		if (mhmod)
			FreeLibrary(mhmod);
	}

private:
	HMODULE mhmod = nullptr;
};

int RunInstance(int nCmdShow, ATSettingsCategory categoriesToIgnore) {
	ATStartupLog("Preloading DLLs");
	ATSystemLibraryLockW32 lockd3d9("d3d9");
	ATSystemLibraryLockW32 lockd3d11("d3d11");
	ATSystemLibraryLockW32 lockdxgi("dxgi");
	ATSystemLibraryLockW32 lockdwmapi("dwmapi");

	g_hInst = VDGetLocalModuleHandleW32();

	ATStartupLog("Registering controls");
	VDRegisterVideoDisplayControl();
	VDUIRegisterHotKeyExControl();
	ATUINativeWindow::Register();
	
	ATStartupLog("Initializing frame system");
	ATInitUIFrameSystem();

	ATStartupLog("Initializing commands and accelerators");
	ATUIInitCommandMappings(g_ATUICommandMgr);
	ATUIInitDefaultAccelTables();
	
	VDDialogFrameW32::SetDefaultCaption(L"Altirra");
	ATUISetDefaultGenericDialogCaption(L"Altirra");

	ATStartupLog("Applying options");

	struct local {
		static void DisplayUpdateFn(ATOptions& opts, const ATOptions *prevOpts, void *) {
			if (prevOpts) {
				if (prevOpts->mbDisplayD3D9 == opts.mbDisplayD3D9
					&& prevOpts->mbDisplay3D == opts.mbDisplay3D
					&& prevOpts->mbDisplay16Bit == opts.mbDisplay16Bit
					)
					return;
			}

			VDVideoDisplaySetFeatures(true, false, false, opts.mbDisplayD3D9, false, false);
			VDVideoDisplaySet3DEnabled(opts.mbDisplay3D);

			ATUIResetDisplay();
		}
	};

	ATOptionsAddUpdateCallback(true, local::DisplayUpdateFn);
	ATOptionsAddUpdateCallback(true,
		[](ATOptions& opts, const ATOptions *prevOpts, void *) {
			g_sim.GetGTIA().SetAccelScreenFXEnabled(opts.mbDisplayAccelScreenFX);
		}
	);

	ATOptionsAddUpdateCallback(true,
		[](ATOptions& opts, const ATOptions *prevOpts, void *) {
			if (!prevOpts || opts.mbDisplayCustomRefresh != prevOpts->mbDisplayCustomRefresh)
				ATUIUpdateSpeedTiming();
		}
	);

	ATOptionsAddUpdateCallback(true,
		[](ATOptions& opts, const ATOptions *prevOpts, void *) {
			ATUISetDarkThemeActive(opts.mbDarkTheme);
		}
	);

	ATOptionsAddUpdateCallback(true,
		[](ATOptions& opts, const ATOptions *prevOpts, void *) {
			ATDirectoryWatcher::SetShouldUsePolling(opts.mbPollDirectories);
		}
	);

	ATOptionsAddUpdateCallback(true,
		[](ATOptions& opts, const ATOptions *prevOpts, void *) {
			ATUIAccSetEnabled(opts.mbAccEnabled);
		}
	);

	ATOptionsAddUpdateCallback(true,
		[](ATOptions& opts, const ATOptions *prevOpts, void *) {
			if (!prevOpts || opts.mEfficiencyMode != prevOpts->mEfficiencyMode)
				ATSetProcessEfficiencyMode(opts.mEfficiencyMode);
		}
	);

	ATOptionsAddUpdateCallback(true,
		[](ATOptions& opts, const ATOptions *prevOpts, void *) {
			if (!prevOpts || opts.mbTextOutputLFOnly != prevOpts->mbTextOutputLFOnly)
				VDTextOutputStream::SetDefaultLFOnly(opts.mbTextOutputLFOnly);
		}
	);

	VDVideoDisplaySetMonitorSwitchingDXEnabled(true);
	VDVideoDisplaySetSecondaryDXEnabled(true);
	VDVideoDisplaySetD3D9ExEnabled(false);
	VDVideoDisplaySetTermServ3DEnabled(true);

	VDDispSetLogHook([](const char *s) { g_ATLCHostDisp("%s\n", s); });

	ATStartupLog("Initializing filespec system");
	VDInitFilespecSystem();
	VDLoadFilespecSystemData();

	ATStartupLog("Initializing UI panes");
	ATInitUIPanes();

	ATStartupLog("Initializing logging");
	ATInitDebuggerLogging(
		ATStartupLogIsInited()
			? vdfunction<void(const char *)>([](const char *msg) {
					VDStringRefA msg2(msg);

					ATStartupLog(msg2.trim_end("\r\n"));
				})
			: nullptr
	);

#ifdef ATNRELEASE
	if (g_ATCmdLine.FindAndRemoveSwitch(L"etw"))
		ATInitOSTracing();
#endif

	ATStartupLog("Initializing native UI");
	ATInitProfilerUI();
	ATUIRegisterDisplayPane();
	ATUIInitVirtualKeyMap(g_kbdOpts);
	ATUILoadAccelTables();

	ATStartupLog("Creating main window");
	WNDCLASS wc = {};
	wc.lpszClassName = _T("AltirraMainWindow");
	ATOM atom = ATContainerWindow::RegisterCustom(wc);
	ATUICreateMainWindow(&g_pMainWindow);

	// Windows 11 22631 taskbar uses the initial window caption to size the taskbar label and doesn't
	// resize it, so it's important that we not create this window with a blank caption.
	HWND hwnd = (HWND)g_pMainWindow->Create(atom, AT_PROGRAM_NAME_STR, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, NULL, false);
	g_hwnd = hwnd;

	ATUIInitProgressDialog();
	ATUIPushProgressParent((VDGUIHandle)hwnd);

	ATStartupLog("Restoring main window");
	ATUIRestoreWindowPlacement(hwnd, "Main window", nCmdShow);

	if (!(GetWindowLong(hwnd, GWL_STYLE) & WS_VISIBLE))
		ShowWindow(hwnd, nCmdShow);

	ATUICreateWindowCaptionUpdater(&g_winCaptionUpdater);
	g_winCaptionUpdater->Init([hwnd](const wchar_t *s) { SetWindowTextW(hwnd, s); } );

	// Init Winsock. We want to delay this as much as possible, but it must be done before we
	// attempt to bring up a modem (which LoadSettings or a command prompt can do).

	ATStartupLog("Initializing WinSock");
	ATSocketInit();

	// bring up simulator
	ATStartupLog("Initializing simulator");
	g_sim.Init();
	g_sim.SetFrontEnd(ATUIGetFrontEndSnapshot());
	g_sim.SetRandomSeed(rand() ^ (rand() << 15));

	ATUISetDispatcher(g_sim.GetDeviceManager()->GetService<IATAsyncDispatcher>());
	ATUILinkMainWindowToSimulator(*g_pMainWindow);

	ATStartupLog("Initializing front end");
	ATUIInitFrontEnd();

	ATStartupLog("Initializing game controllers");
	ATInitJoysticks();

	g_sim.GetInputManager()->SetConsoleCallback(&g_inputConsoleCallback);
	g_winCaptionUpdater->InitMonitoring(&g_sim);
	g_winCaptionUpdater->SetTemplate(g_winCaptionTemplate.c_str());
	ATRegisterDevices(*g_sim.GetDeviceManager());
	ATRegisterDeviceXCmds(*g_sim.GetDeviceManager());

	// initialize menus (requires simulator)
	ATStartupLog("Initializing firmware menus");
	ATUIInitFirmwareMenuCallbacks(g_sim.GetFirmwareManager());

	ATStartupLog("Initializing profile menus");
	ATUIInitProfileMenuCallbacks();

	ATStartupLog("Initializing video output menu");
	ATUIInitVideoOutputMenuCallback(*g_sim.GetDeviceManager()->GetService<IATDeviceVideoManager>());

	ATStartupLog("Loading menu");
	try {
		ATUILoadMenu();
	} catch(const MyError& e) {
		e.post(hwnd, "Altirra Error");
	}

	ATStartupLog("Initializing port menus");
	ATInitPortMenus(g_sim.GetInputManager());

	// bring up debugger
	ATStartupLog("Initializing debugger");
	ATInitDebugger();

	// init compatibility database
	ATStartupLog("Initializing compatibility system");
	ATCompatInit();

	// initialize runtime UI (requires simulator)
	ATStartupLog("Initializing display UI");
	ATUIInitManager();

	// load available profile set
	ATStartupLog("Loading profiles");
	if (ATLoadDefaultProfiles())
		ATUIRebuildDynamicMenu(kATUIDynamicMenu_Profile);

	// load initial profile
	const bool useHardwareBaseline = g_ATCmdLine.FindAndRemoveSwitch(L"baseline");

	ATSettingsCategory settingsToLoad = useHardwareBaseline
			? ATSettingsCategory(kATSettingsCategory_All & ~(kATSettingsCategory_FullScreen | kATSettingsCategory_Baseline))
			: ATSettingsCategory(kATSettingsCategory_All & ~kATSettingsCategory_FullScreen);

	settingsToLoad = ATSettingsCategory(settingsToLoad & ~categoriesToIgnore);

	const wchar_t *profileName;
	uint32 profileToLoad = kATProfileId_Invalid;
	if (g_ATCmdLine.FindAndRemoveSwitch(L"profile", profileName)) {
		profileToLoad = ATSettingsFindProfileByName(profileName);
	} else if (g_ATCmdLine.FindAndRemoveSwitch(L"defprofile", profileName)) {
		VDStringSpanW profileNameSpan(profileName);

		if (profileNameSpan == L"xl") {
			profileToLoad = ATGetDefaultProfileId(kATDefaultProfile_XL);
		} else if (profileNameSpan == L"xegs") {
			profileToLoad = ATGetDefaultProfileId(kATDefaultProfile_XEGS);
		} else if (profileNameSpan == L"800") {
			profileToLoad = ATGetDefaultProfileId(kATDefaultProfile_800);
		} else if (profileNameSpan == L"1200xl") {
			profileToLoad = ATGetDefaultProfileId(kATDefaultProfile_1200XL);
		} else if (profileNameSpan == L"5200") {
			profileToLoad = ATGetDefaultProfileId(kATDefaultProfile_5200);
		} else {
			VDException(L"Unknown default profile: %ls", profileName).post(hwnd, "Altirra error");
		}
	}

	ATStartupLog("Loading current profile");
	if (profileToLoad != kATProfileId_Invalid && ATSettingsIsValidProfile(profileToLoad))
		ATSettingsLoadProfile(profileToLoad, settingsToLoad);
	else
		ATSettingsLoadLastProfile(settingsToLoad);

	if (g_ATCmdLine.FindAndRemoveSwitch(L"tempprofile"))
		ATSettingsSetTemporaryProfileMode(true);

	if (g_ATCmdLine.FindSwitch(L"autoprofile"))
		ATSettingsSetBootstrapProfileMode(true);

	if (useHardwareBaseline)
		LoadBaselineSettings();
	
	ATStartupLog("Initializing native audio");

	g_sim.GetAudioOutput()->InitNativeAudio();
	
	ATStartupLog("Loading ROMs");

	try {
		g_sim.LoadROMs();
	} catch(MyError& e) {
		e.post(hwnd, "Altirra Error");
	}

	ATStartupLog("Saving options");
	ATOptionsSave();

	ATStartupLog("Starting emulation");
	g_sim.Resume();
	ATInitEmuErrorHandler((VDGUIHandle)g_hwnd, &g_sim);

	ATStartupLog("Restoring pane layout");

	// try to restore the pane layout; if it succeeds, give the Display pane
	// focus if it exists, but if it fails, force the Display pane to exist
	if (!ATRestorePaneLayout(NULL) || ATGetUIPane(kATUIPaneId_Display))
		ATActivateUIPane(kATUIPaneId_Display, true);

	ATStartupLog("Initiating cold reset");
	g_sim.ColdReset();

	// we can't go full screen until the display panes have been created
	if (!g_ATCmdLine.FindAndRemoveSwitch(L"w")) {
		ATStartupLog("Initializing full screen mode");
		ATLoadSettings(kATSettingsCategory_FullScreen);
	}

	ATStartupLog("Running main loop");
	int returnCode = RunMainLoop(hwnd);

	ATStartupLog("Beginning exit");

	ATStartupLog("Destroying pane layout");

	// hide the main window, as we're going to start taking it down and no need
	// to repaint stuff; we need to clear the panes as they refer to some high
	// level systems
	if (g_pMainWindow) {
		g_pMainWindow->Hide();
		g_pMainWindow->Clear();
	}

	ATStartupLog("Destroying remaining UI");
	ATUIDestroyModelessWindows(nullptr);

	ATStartupLog("Saving settings");
	ATSaveSettings(ATSettingsCategory(kATSettingsCategory_All & ~kATSettingsCategory_FullScreen));

	ATStartupLog("Exiting fullscreen mode");
	ATSetFullscreen(false);

	ATStartupLog("Stopping front end");
	ATUIShutdownFrontEnd();

	ATStartupLog("Stopping sockets");
	ATSocketPreShutdown();

	ATStartupLog("Shutting down filespec system");
	VDSaveFilespecSystemData();

	ATStartupLog("Shutting down compat system");
	ATCompatShutdown();

	ATStartupLog("Shutting down UI");
	ATUIShutdownManager();

	ATStartupLog("Shutting down debugger");
	ATShutdownEmuErrorHandler();
	ATShutdownDebugger();
	ATConsoleCloseLogFileNT();

	ATStartupLog("Shutting down game controllers");
	ATShutdownJoysticks();

	ATStartupLog("Shutting down simulator");
	ATUISetDispatcher(nullptr);
	ATUIUnlinkMainWindowFromSimulator();
	g_sim.Shutdown();

	ATStartupLog("Shutting down native UI");
	g_d3d9Lock.Unlock();

	ATUIShutdownProgressDialog();
	ATUIPopProgressParent((VDGUIHandle)g_hwnd, nullptr);

	g_pMainWindow->Destroy();
	g_pMainWindow->Release();
	g_pMainWindow = NULL;
	g_hwnd = NULL;

	g_winCaptionUpdater->Release();
	g_winCaptionUpdater = nullptr;

	ATShutdownPortMenus();

	ATShutdownUIPanes();
	ATShutdownUIFrameSystem();
	ATUIShutdownThemes();

	ATStartupLog("Shutting down UI accessibility");
	ATUIAccShutdown();

#ifdef ATNRELEASE
	ATStartupLog("Shutting down logging");
	ATShutdownOSTracing();
#endif

	ATStartupLog("Shutting down WinSock");
	ATSocketShutdown();

	return returnCode;
}
