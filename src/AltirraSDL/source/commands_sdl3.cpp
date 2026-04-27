//	AltirraSDL - SDL3 command handler registration
//	Registers ATUICommand entries for all accelerator-table-driven commands.
//	This replaces the Windows cmd*.cpp files which depend on Win32 UI.

#include <stdafx.h>
#include <SDL3/SDL.h>
#include <at/atui/uicommandmanager.h>
#include <at/ataudio/pokey.h>
#include <at/ataudio/audiooutput.h>
#include <at/atcore/constants.h>

#include "simulator.h"
#include "uiaccessors.h"
#include "inputmanager.h"
#include "inputdefs.h"
#include "inputcontroller.h"
#include "antic.h"
#include "gtia.h"
#include "uikeyboard.h"
#include "uitypes.h"
#include "cassette.h"
#include "options.h"
#include "uirender.h"
#include "devicemanager.h"
#include "ui_main.h"
#include "ui_debugger.h"
#include "ui_textselection.h"
#include "uiclipboard.h"
#include "ui/emotes/emote_picker.h"
#include "netplay/netplay_glue.h"

extern ATSimulator g_sim;
extern SDL_Window *g_pWindow;
extern ATUIKeyboardOptions g_kbdOpts;
extern bool g_copyFrameRequested;

ATUICommandManager g_ATUICommandMgr;

ATUICommandManager& ATUIGetCommandManager() {
	return g_ATUICommandMgr;
}

// =========================================================================
// Display context commands
// =========================================================================

static void CmdPulseWarpOn() {
	// During a netplay lockstep session, warp is force-blocked by
	// ATUINetplayBlocksWarp() (see uiaccessors_stubs.cpp).  Repurpose
	// the bare F1 press to open the communication-icon picker instead,
	// so the key isn't a silent no-op while online.
	if (ATNetplayGlue::IsLockstepping()) {
		ATEmotePicker::Open();
		return;
	}
	ATUISetTurboPulse(true);
}

static void CmdPulseWarpOff() {
	// Symmetric to CmdPulseWarpOn: during lockstep the key-up never
	// needs to clear a warp state because warp never engaged.  The
	// picker closes via its own cancel paths (Esc, click-outside,
	// gamepad B), so nothing to do here.
	if (ATNetplayGlue::IsLockstepping())
		return;
	ATUISetTurboPulse(false);
}

static void CmdCycleQuickMaps() {
	ATInputManager *pIM = g_sim.GetInputManager();
	if (pIM) pIM->CycleQuickMaps();
}

static void CmdHoldKeys() {
	ATUIToggleHoldKeys();
}

// All three of these mutate hashed simulator state (RAM via reset,
// video standard) and would silently desync a netplay session.
// F5 (Warm Reset) and Shift+F5 (Cold Reset) are common-enough
// keypresses that the user could trigger them by accident; Ctrl+F7
// (Toggle NTSC/PAL) is rarer but equally fatal.  All ATNetplay
// session reset paths run via the canonical-profile boot sequence
// in kATDeferred_NetplayHostBoot / NetplayJoinerApply, so the
// keyboard shortcut is purely a user action that would break sync.
static void CmdWarmReset() {
	if (ATNetplayGlue::IsActive()) return;
	g_sim.WarmReset();
	g_sim.Resume();
}

static void CmdColdReset() {
	if (ATNetplayGlue::IsActive()) return;
	g_sim.ColdReset();
	g_sim.Resume();
	if (!g_kbdOpts.mbAllowShiftOnColdReset)
		g_sim.GetPokey().SetShiftKeyState(false, true);
}

static void CmdToggleNTSCPAL() {
	if (ATNetplayGlue::IsActive()) return;
	if (g_sim.GetVideoStandard() == kATVideoStandard_NTSC)
		g_sim.SetVideoStandard(kATVideoStandard_PAL);
	else
		g_sim.SetVideoStandard(kATVideoStandard_NTSC);
}

static void CmdNextANTICVisMode() {
	ATAnticEmulator& antic = g_sim.GetAntic();
	antic.SetAnalysisMode((ATAnticEmulator::AnalysisMode)
		(((int)antic.GetAnalysisMode() + 1) % ATAnticEmulator::kAnalyzeModeCount));
}

static void CmdNextGTIAVisMode() {
	ATGTIAEmulator& gtia = g_sim.GetGTIA();
	gtia.SetAnalysisMode((ATGTIAEmulator::AnalysisMode)
		(((int)gtia.GetAnalysisMode() + 1) % ATGTIAEmulator::kAnalyzeCount));
}

static void CmdTogglePause() {
	// Pausing during a netplay lockstep session desyncs the peers —
	// our simulator stops advancing while the remote peer continues,
	// and the lockstep frame counter diverges.  Suppress the toggle
	// entirely while online; the user can Disconnect first if they
	// actually want to stop.
	if (ATNetplayGlue::IsLockstepping())
		return;
	if (g_sim.IsPaused()) g_sim.Resume();
	else g_sim.Pause();
}

static void CmdCaptureMouse() {
	ATUICaptureMouse();
}

static void CmdToggleFullScreen() {
	extern void ATSetFullscreen(bool fs);
	bool fs = (SDL_GetWindowFlags(g_pWindow) & SDL_WINDOW_FULLSCREEN) != 0;
	ATSetFullscreen(!fs);
}

static void CmdToggleSlowMotion() {
	ATUISetSlowMotion(!ATUIGetSlowMotion());
}

// POKEY channel toggles affect POKEY internal state which is
// netplay-hashed (see netplay_simhash.cpp:pokeyRegs).  Toggling
// during a session would diverge the next frame's hash.
static void CmdToggleChannel1() {
	if (ATNetplayGlue::IsActive()) return;
	ATPokeyEmulator& pokey = g_sim.GetPokey();
	pokey.SetChannelEnabled(0, !pokey.IsChannelEnabled(0));
}

static void CmdToggleChannel2() {
	if (ATNetplayGlue::IsActive()) return;
	ATPokeyEmulator& pokey = g_sim.GetPokey();
	pokey.SetChannelEnabled(1, !pokey.IsChannelEnabled(1));
}

static void CmdToggleChannel3() {
	if (ATNetplayGlue::IsActive()) return;
	ATPokeyEmulator& pokey = g_sim.GetPokey();
	pokey.SetChannelEnabled(2, !pokey.IsChannelEnabled(2));
}

static void CmdToggleChannel4() {
	if (ATNetplayGlue::IsActive()) return;
	ATPokeyEmulator& pokey = g_sim.GetPokey();
	pokey.SetChannelEnabled(3, !pokey.IsChannelEnabled(3));
}

static void CmdPasteText() {
	ATUIPasteText();
}

static void CmdSaveFrame() {
	ATUIShowSaveFrameDialog(g_pWindow);
}

static void CmdCopyText() {
	ATUITextCopy(ATTextCopyMode::ASCII);
}

static void CmdCopyFrame() {
	g_copyFrameRequested = true;
}

static void CmdSelectAll() {
	ATUITextSelectAll();
}

static void CmdDeselect() {
	ATUITextDeselect();
}

// =========================================================================
// Global context commands
// =========================================================================

static void CmdBootImage() {
	// Loading a new game during a netplay session would UnloadAll +
	// Load + ColdReset on this peer only, instantly desyncing.
	// Block the dialog opener; the deferred-action handler
	// (ui_main.cpp kATDeferred_BootImage) also has a backstop check
	// for paths that bypass this command (drag-and-drop, mobile
	// file browser).
	if (ATNetplayGlue::IsActive()) return;
	ATUIShowBootImageDialog(g_pWindow);
}

static void CmdOpenImage() {
	if (ATNetplayGlue::IsActive()) return;
	ATUIShowOpenImageDialog(g_pWindow);
}

static void CmdOpenSourceFile() {
	ATUIShowOpenSourceFileDialog(g_pWindow);
}

// These access g_uiState which is in main_sdl3.cpp — use extern accessors
extern void ATUISetShowDiskManager(bool v);
extern void ATUISetShowSystemConfig(bool v);
extern void ATUISetShowCheater(bool v);

static void CmdDrivesDialog() {
	// Same rationale as CmdConfigure: mounting/unmounting disks
	// during a netplay session would mutate MountedImages on this
	// peer only and instantly desync.
	if (ATNetplayGlue::IsActive()) return;
	ATUISetShowDiskManager(true);
}

static void CmdConfigure() {
	// Configure System edits settings the canonical Online Play
	// profile pins.  Suppress the shortcut while a session is active
	// so the user can't open the dialog and silently desync.
	if (ATNetplayGlue::IsActive()) return;
	ATUISetShowSystemConfig(true);
}

static void CmdCheatDialog() {
	ATUISetShowCheater(true);
}

// The debugger halts the simulator on a breakpoint / RunStop / Step and
// expects to single-step at user pace.  That's incompatible with
// lockstep netplay: the remote peer keeps advancing while we're halted
// and the session desyncs within a frame.  Every debug-entry keypress
// is suppressed while lockstepping; the user must Disconnect first.
static void CmdDebugRunStop() {
	if (ATNetplayGlue::IsLockstepping()) return;
	ATUIDebuggerRunStop();
}

static void CmdDebugStepInto() {
	if (ATNetplayGlue::IsLockstepping()) return;
	ATUIDebuggerStepInto();
}

static void CmdDebugStepOver() {
	if (ATNetplayGlue::IsLockstepping()) return;
	ATUIDebuggerStepOver();
}

static void CmdDebugStepOut() {
	if (ATNetplayGlue::IsLockstepping()) return;
	ATUIDebuggerStepOut();
}

static void CmdDebugBreak() {
	if (ATNetplayGlue::IsLockstepping()) return;
	ATUIDebuggerBreak();
}

// Pane commands — only active when debugger is open.
// mpTestFn returns false when debugger is closed, so the key is NOT consumed
// and falls through to the emulator (matching old if-chain behavior).
static bool TestDebuggerOpen() {
	return ATUIDebuggerIsOpen();
}

static void CmdPaneDisplay() {
	ATUIDebuggerFocusDisplay();
}

static void CmdPaneConsole() {
	ATActivateUIPane(kATUIPaneId_Console, true, true);
}

static void CmdPaneRegisters() {
	ATActivateUIPane(kATUIPaneId_Registers, true, true);
}

static void CmdPaneDisassembly() {
	ATActivateUIPane(kATUIPaneId_Disassembly, true, true);
}

static void CmdPaneCallStack() {
	ATActivateUIPane(kATUIPaneId_CallStack, true, true);
}

static void CmdPaneHistory() {
	ATActivateUIPane(kATUIPaneId_History, true, true);
}

static void CmdPaneMemory1() {
	ATActivateUIPane(kATUIPaneId_MemoryN, true, true);
}

static void CmdPanePrinterOutput() {
	ATActivateUIPane(kATUIPaneId_PrinterOutput, true, true);
}

static void CmdPaneProfileView() {
	ATActivateUIPane(kATUIPaneId_Profiler, true, true);
}

// =========================================================================
// Debugger context commands
// =========================================================================

static void CmdDebugRun() {
	ATUIDebuggerRunStop();
}

static void CmdDebugToggleBreakpoint() {
	ATUIDebuggerToggleBreakpoint();
}

static void CmdDebugNewBreakpoint() {
	ATActivateUIPane(kATUIPaneId_Breakpoints, true, true);
	ATUIDebuggerShowBreakpointDialog(-1);
}

static void CmdOpenEmotePicker() {
	ATEmotePicker::Open();
}

// =========================================================================
// Bulk command port from Windows cmd*.cpp
// =========================================================================
//
// These commands map 1:1 onto Windows ATUICommand entries from
// src/Altirra/source/cmd*.cpp. They are needed so accelerator-table
// bindings, command-palette dispatch, and Custom Device VM
// `run_command "..."` calls have the same vocabulary as the Windows
// build. State/Test functions (used by Win32 menu checkmark
// rendering) are intentionally omitted — the SDL3 ImGui menu queries
// engine state directly each frame.
//
// Test/State callbacks are included only where they prevent invalid
// dispatch (e.g., Cart.Detach when no cart is mounted).

namespace {
	bool IsCart0Attached() { return g_sim.IsCartridgeAttached(0); }
	bool IsCart1Attached() { return g_sim.IsCartridgeAttached(1); }
	bool IsNot5200() { return g_sim.GetHardwareMode() != kATHardwareMode_5200; }
}

static const ATUICommand kSDL3CommandsExtra[] = {
	// =====================================================================
	// Audio (cmdaudio.cpp)
	// =====================================================================
	{ "Audio.ToggleStereo",
		[] { g_sim.SetDualPokeysEnabled(!g_sim.IsDualPokeysEnabled()); } },
	{ "Audio.ToggleStereoAsMono",
		[] {
			ATPokeyEmulator& p = g_sim.GetPokey();
			p.SetStereoAsMonoEnabled(!p.IsStereoAsMonoEnabled());
		} },
	{ "Audio.ToggleMonitor",
		[] { g_sim.SetAudioMonitorEnabled(!g_sim.IsAudioMonitorEnabled()); } },
	{ "Audio.ToggleScope",
		[] { g_sim.SetAudioScopeEnabled(!g_sim.IsAudioScopeEnabled()); } },
	{ "Audio.ToggleMute",
		[] { if (auto *out = g_sim.GetAudioOutput()) out->SetMute(!out->GetMute()); } },
	{ "Audio.ToggleNonlinearMixing",
		[] {
			ATPokeyEmulator& p = g_sim.GetPokey();
			p.SetNonlinearMixingEnabled(!p.IsNonlinearMixingEnabled());
		} },
	{ "Audio.ToggleSpeakerFilter",
		[] {
			ATPokeyEmulator& p = g_sim.GetPokey();
			p.SetSpeakerFilterEnabled(!p.IsSpeakerFilterEnabled());
		} },
	{ "Audio.ToggleSerialNoise",
		[] {
			ATPokeyEmulator& p = g_sim.GetPokey();
			p.SetSerialNoiseEnabled(!p.IsSerialNoiseEnabled());
		} },
	{ "Audio.ToggleSecondaryChannel1",
		[] {
			ATPokeyEmulator& p = g_sim.GetPokey();
			p.SetSecondaryChannelEnabled(0, !p.IsSecondaryChannelEnabled(0));
		} },
	{ "Audio.ToggleSecondaryChannel2",
		[] {
			ATPokeyEmulator& p = g_sim.GetPokey();
			p.SetSecondaryChannelEnabled(1, !p.IsSecondaryChannelEnabled(1));
		} },
	{ "Audio.ToggleSecondaryChannel3",
		[] {
			ATPokeyEmulator& p = g_sim.GetPokey();
			p.SetSecondaryChannelEnabled(2, !p.IsSecondaryChannelEnabled(2));
		} },
	{ "Audio.ToggleSecondaryChannel4",
		[] {
			ATPokeyEmulator& p = g_sim.GetPokey();
			p.SetSecondaryChannelEnabled(3, !p.IsSecondaryChannelEnabled(3));
		} },
	{ "Audio.ToggleSlightSid",
		[] { g_sim.GetDeviceManager()->ToggleDevice("slightsid"); } },
	{ "Audio.ToggleCovox",
		[] { g_sim.GetDeviceManager()->ToggleDevice("covox"); } },

	// =====================================================================
	// Cassette (cmdcassette.cpp) — settings half. Load/Unload/Save
	// dialogs remain UI-driven and live in their respective ImGui
	// panels.
	// =====================================================================
	{ "Cassette.Unload",
		[] { g_sim.GetCassette().Unload(); } },
	{ "Cassette.ToggleSIOPatch",
		[] { g_sim.SetCassetteSIOPatchEnabled(!g_sim.IsCassetteSIOPatchEnabled()); } },
	{ "Cassette.ToggleAutoBoot",
		[] { g_sim.SetCassetteAutoBootEnabled(!g_sim.IsCassetteAutoBootEnabled()); } },
	{ "Cassette.ToggleAutoBasicBoot",
		[] { g_sim.SetCassetteAutoBasicBootEnabled(!g_sim.IsCassetteAutoBasicBootEnabled()); } },
	{ "Cassette.ToggleAutoRewind",
		[] { g_sim.SetCassetteAutoRewindEnabled(!g_sim.IsCassetteAutoRewindEnabled()); } },
	{ "Cassette.ToggleLoadDataAsAudio",
		[] {
			ATCassetteEmulator& cas = g_sim.GetCassette();
			cas.SetLoadDataAsAudioEnable(!cas.IsLoadDataAsAudioEnabled());
		} },
	{ "Cassette.ToggleRandomizeStartPosition",
		[] { g_sim.SetCassetteRandomizedStartEnabled(!g_sim.IsCassetteRandomizedStartEnabled()); } },
	{ "Cassette.TurboModeNone",
		[] { g_sim.GetCassette().SetTurboMode(kATCassetteTurboMode_None); } },
	{ "Cassette.TurboModeCommandControl",
		[] { g_sim.GetCassette().SetTurboMode(kATCassetteTurboMode_CommandControl); } },
	{ "Cassette.TurboModeDataControl",
		[] { g_sim.GetCassette().SetTurboMode(kATCassetteTurboMode_DataControl); } },
	{ "Cassette.TurboModeProceedSense",
		[] { g_sim.GetCassette().SetTurboMode(kATCassetteTurboMode_ProceedSense); } },
	{ "Cassette.TurboModeInterruptSense",
		[] { g_sim.GetCassette().SetTurboMode(kATCassetteTurboMode_InterruptSense); } },
	{ "Cassette.TurboModeKSOTurbo2000",
		[] { g_sim.GetCassette().SetTurboMode(kATCassetteTurboMode_KSOTurbo2000); } },
	{ "Cassette.TurboModeTurboD",
		[] { g_sim.GetCassette().SetTurboMode(kATCassetteTurboMode_TurboD); } },
	{ "Cassette.TurboModeAlways",
		[] { g_sim.GetCassette().SetTurboMode(kATCassetteTurboMode_Always); } },
	{ "Cassette.TogglePolarity",
		[] {
			auto& cas = g_sim.GetCassette();
			cas.SetPolarityMode(cas.GetPolarityMode() == kATCassettePolarityMode_Normal
				? kATCassettePolarityMode_Inverted
				: kATCassettePolarityMode_Normal);
		} },
	{ "Cassette.PolarityModeNormal",
		[] { g_sim.GetCassette().SetPolarityMode(kATCassettePolarityMode_Normal); } },
	{ "Cassette.PolarityModeInverted",
		[] { g_sim.GetCassette().SetPolarityMode(kATCassettePolarityMode_Inverted); } },
	{ "Cassette.DirectSenseNormal",
		[] { g_sim.GetCassette().SetDirectSenseMode(ATCassetteDirectSenseMode::Normal); } },
	{ "Cassette.DirectSenseLowSpeed",
		[] { g_sim.GetCassette().SetDirectSenseMode(ATCassetteDirectSenseMode::LowSpeed); } },
	{ "Cassette.DirectSenseHighSpeed",
		[] { g_sim.GetCassette().SetDirectSenseMode(ATCassetteDirectSenseMode::HighSpeed); } },
	{ "Cassette.DirectSenseMaxSpeed",
		[] { g_sim.GetCassette().SetDirectSenseMode(ATCassetteDirectSenseMode::MaxSpeed); } },
	{ "Cassette.ToggleTurboPrefilter",
		[] {
			auto& cas = g_sim.GetCassette();
			cas.SetTurboDecodeAlgorithm(
				cas.GetTurboDecodeAlgorithm() == ATCassetteTurboDecodeAlgorithm::SlopeNoFilter
					? ATCassetteTurboDecodeAlgorithm::SlopeFilter
					: ATCassetteTurboDecodeAlgorithm::SlopeNoFilter);
		} },
	{ "Cassette.TurboDecoderSlopeNoFilter",
		[] { g_sim.GetCassette().SetTurboDecodeAlgorithm(ATCassetteTurboDecodeAlgorithm::SlopeNoFilter); } },
	{ "Cassette.TurboDecoderSlopeFilter",
		[] { g_sim.GetCassette().SetTurboDecodeAlgorithm(ATCassetteTurboDecodeAlgorithm::SlopeFilter); } },
	{ "Cassette.TurboDecoderPeakFilter",
		[] { g_sim.GetCassette().SetTurboDecodeAlgorithm(ATCassetteTurboDecodeAlgorithm::PeakFilter); } },

	// =====================================================================
	// View (cmdview.cpp) — display/filter/stretch/overscan toggles.
	// =====================================================================
	{ "View.NextFilterMode",
		[] {
			switch (ATUIGetDisplayFilterMode()) {
			case kATDisplayFilterMode_Point:        ATUISetDisplayFilterMode(kATDisplayFilterMode_Bilinear); break;
			case kATDisplayFilterMode_Bilinear:     ATUISetDisplayFilterMode(kATDisplayFilterMode_SharpBilinear); break;
			case kATDisplayFilterMode_SharpBilinear:ATUISetDisplayFilterMode(kATDisplayFilterMode_Bicubic); break;
			case kATDisplayFilterMode_Bicubic:      ATUISetDisplayFilterMode(kATDisplayFilterMode_AnySuitable); break;
			case kATDisplayFilterMode_AnySuitable:  ATUISetDisplayFilterMode(kATDisplayFilterMode_Point); break;
			}
		} },
	{ "View.FilterModePoint",         [] { ATUISetDisplayFilterMode(kATDisplayFilterMode_Point); } },
	{ "View.FilterModeBilinear",      [] { ATUISetDisplayFilterMode(kATDisplayFilterMode_Bilinear); } },
	{ "View.FilterModeSharpBilinear", [] { ATUISetDisplayFilterMode(kATDisplayFilterMode_SharpBilinear); } },
	{ "View.FilterModeBicubic",       [] { ATUISetDisplayFilterMode(kATDisplayFilterMode_Bicubic); } },
	{ "View.FilterModeDefault",       [] { ATUISetDisplayFilterMode(kATDisplayFilterMode_AnySuitable); } },
	{ "View.FilterSharpnessSofter",   [] { ATUISetViewFilterSharpness(-2); } },
	{ "View.FilterSharpnessSoft",     [] { ATUISetViewFilterSharpness(-1); } },
	{ "View.FilterSharpnessNormal",   [] { ATUISetViewFilterSharpness(0); } },
	{ "View.FilterSharpnessSharp",    [] { ATUISetViewFilterSharpness(+1); } },
	{ "View.FilterSharpnessSharper",  [] { ATUISetViewFilterSharpness(+2); } },
	{ "View.StretchFitToWindow",
		[] { ATUISetDisplayStretchMode(kATDisplayStretchMode_Unconstrained); } },
	{ "View.StretchPreserveAspectRatio",
		[] { ATUISetDisplayStretchMode(kATDisplayStretchMode_PreserveAspectRatio); } },
	{ "View.StretchSquarePixels",
		[] { ATUISetDisplayStretchMode(kATDisplayStretchMode_SquarePixels); } },
	{ "View.StretchSquarePixelsInt",
		[] { ATUISetDisplayStretchMode(kATDisplayStretchMode_Integral); } },
	{ "View.StretchPreserveAspectRatioInt",
		[] { ATUISetDisplayStretchMode(kATDisplayStretchMode_IntegralPreserveAspectRatio); } },
	{ "View.ToggleFPS",
		[] { ATUISetShowFPS(!ATUIGetShowFPS()); } },
	{ "View.ToggleIndicators",
		[] { ATUISetDisplayIndicators(!ATUIGetDisplayIndicators()); } },
	{ "View.ToggleIndicatorMargin",
		[] { ATUISetDisplayPadIndicators(!ATUIGetDisplayPadIndicators()); } },
	{ "View.ToggleAutoHidePointer",
		[] { ATUISetPointerAutoHide(!ATUIGetPointerAutoHide()); } },
	{ "View.ToggleConstrainPointerFullScreen",
		[] { ATUISetConstrainMouseFullScreen(!ATUIGetConstrainMouseFullScreen()); } },
	{ "View.ToggleTargetPointer",
		[] { ATUISetTargetPointerVisible(!ATUIGetTargetPointerVisible()); } },
	{ "View.TogglePadBounds",
		[] { ATUISetDrawPadBoundsEnabled(!ATUIGetDrawPadBoundsEnabled()); } },
	{ "View.TogglePadPointers",
		[] { ATUISetDrawPadPointersEnabled(!ATUIGetDrawPadPointersEnabled()); } },
	{ "View.ResetPan",
		[] { ATUISetDisplayPanOffset(vdfloat2{0.0f, 0.0f}); } },
	{ "View.ResetZoom",
		[] { ATUISetDisplayZoom(1.0f); } },
	{ "View.ResetViewFrame",
		[] { ATUISetDisplayPanOffset(vdfloat2{0.0f, 0.0f}); ATUISetDisplayZoom(1.0f); } },
	{ "View.PanZoomTool",
		[] { ATUIActivatePanZoomTool(); } },
	{ "View.VideoOutputNormal",
		[] { ATUISetAltViewEnabled(false); } },

	// =====================================================================
	// Input (cmdinput.cpp) — keyboard layout/mode commands.
	// =====================================================================
	{ "Input.KeyboardLayoutNatural",
		[] {
			if (g_kbdOpts.mLayoutMode != ATUIKeyboardOptions::kLM_Natural) {
				g_kbdOpts.mLayoutMode = ATUIKeyboardOptions::kLM_Natural;
				ATUIInitVirtualKeyMap(g_kbdOpts);
			}
		} },
	{ "Input.KeyboardLayoutDirect",
		[] {
			if (g_kbdOpts.mLayoutMode != ATUIKeyboardOptions::kLM_Raw) {
				g_kbdOpts.mLayoutMode = ATUIKeyboardOptions::kLM_Raw;
				ATUIInitVirtualKeyMap(g_kbdOpts);
			}
		} },
	{ "Input.KeyboardLayoutCustom",
		[] {
			if (g_kbdOpts.mLayoutMode != ATUIKeyboardOptions::kLM_Custom) {
				g_kbdOpts.mLayoutMode = ATUIKeyboardOptions::kLM_Custom;
				ATUIInitVirtualKeyMap(g_kbdOpts);
			}
		} },
	{ "Input.KeyboardModeCooked",
		[] { g_kbdOpts.mbRawKeys = false; g_kbdOpts.mbFullRawKeys = false; } },
	{ "Input.KeyboardModeRaw",
		[] { g_kbdOpts.mbRawKeys = true; g_kbdOpts.mbFullRawKeys = false; } },
	{ "Input.KeyboardModeFullScan",
		[] { g_kbdOpts.mbRawKeys = true; g_kbdOpts.mbFullRawKeys = true; } },
	{ "Input.KeyboardArrowModeDefault",
		[] {
			g_kbdOpts.mArrowKeyMode = ATUIKeyboardOptions::kAKM_InvertCtrl;
			ATUIInitVirtualKeyMap(g_kbdOpts);
		} },
	{ "Input.KeyboardArrowModeAutoCtrl",
		[] {
			g_kbdOpts.mArrowKeyMode = ATUIKeyboardOptions::kAKM_AutoCtrl;
			ATUIInitVirtualKeyMap(g_kbdOpts);
		} },
	{ "Input.KeyboardArrowModeRaw",
		[] {
			g_kbdOpts.mArrowKeyMode = ATUIKeyboardOptions::kAKM_DefaultCtrl;
			ATUIInitVirtualKeyMap(g_kbdOpts);
		} },
	{ "Input.ToggleAllowShiftOnReset",
		[] { g_kbdOpts.mbAllowShiftOnColdReset = !g_kbdOpts.mbAllowShiftOnColdReset; } },
	{ "Input.Toggle1200XLFunctionKeys",
		[] {
			g_kbdOpts.mbEnableFunctionKeys = !g_kbdOpts.mbEnableFunctionKeys;
			ATUIInitVirtualKeyMap(g_kbdOpts);
		} },
	{ "Input.ToggleAllowInputMapKeyboardOverlap",
		[] { g_kbdOpts.mbAllowInputMapOverlap = !g_kbdOpts.mbAllowInputMapOverlap; } },
	{ "Input.ToggleAllowInputMapKeyboardModifierOverlap",
		[] { g_kbdOpts.mbAllowInputMapModifierOverlap = !g_kbdOpts.mbAllowInputMapModifierOverlap; } },
	{ "Input.ToggleRawInputEnabled",
		[] { ATUISetRawInputEnabled(!ATUIGetRawInputEnabled()); } },
	{ "Input.ToggleImmediatePotUpdate",
		[] {
			ATPokeyEmulator& p = g_sim.GetPokey();
			p.SetImmediatePotUpdateEnabled(!p.IsImmediatePotUpdateEnabled());
		} },
	{ "Input.ToggleImmediateLightPenUpdate",
		[] {
			auto *lpp = g_sim.GetLightPenPort();
			lpp->SetImmediateUpdateEnabled(!lpp->GetImmediateUpdateEnabled());
		} },
	{ "Input.RecalibrateLightPen", ATUIRecalibrateLightPen },
	{ "Input.TogglePotNoise",
		[] { g_sim.SetPotNoiseEnabled(!g_sim.GetPotNoiseEnabled()); } },

	// =====================================================================
	// System (cmdsystem.cpp) — high-traffic toggles. Hardware/memory
	// mode switchers go through ATUISwitchHardwareMode/MemoryMode/
	// Kernel/Basic which already exist as real implementations in
	// stubs/uiaccessors_stubs.cpp.
	// =====================================================================
	{ "System.ColdResetComputerOnly",
		[] {
			g_sim.ColdResetComputerOnly();
			g_sim.Resume();
			if (!g_kbdOpts.mbAllowShiftOnColdReset)
				g_sim.GetPokey().SetShiftKeyState(false, true);
		} },
	{ "System.TogglePauseWhenInactive",
		[] { ATUISetPauseWhenInactive(!ATUIGetPauseWhenInactive()); } },
	{ "System.ToggleWarpSpeed",
		[] { ATUISetTurbo(!ATUIGetTurbo()); } },
	{ "System.ToggleFPPatch",
		[] { g_sim.SetFPPatchEnabled(!g_sim.IsFPPatchEnabled()); } },
	{ "System.ToggleKeyboardPresent",
		[] { g_sim.SetKeyboardPresent(!g_sim.IsKeyboardPresent()); } },
	{ "System.ToggleForcedSelfTest",
		[] { g_sim.SetForcedSelfTest(!g_sim.IsForcedSelfTest()); } },
	{ "System.ToggleMapRAM",
		[] { g_sim.SetMapRAMEnabled(!g_sim.IsMapRAMEnabled()); } },
	{ "System.ToggleUltimate1MB",
		[] { g_sim.SetUltimate1MBEnabled(!g_sim.IsUltimate1MBEnabled()); } },
	{ "System.ToggleFloatingIoBus",
		[] { g_sim.SetFloatingIoBusEnabled(!g_sim.IsFloatingIoBusEnabled()); } },
	{ "System.TogglePreserveExtRAM",
		[] { g_sim.SetPreserveExtRAMEnabled(!g_sim.IsPreserveExtRAMEnabled()); } },
	{ "System.ToggleMemoryRandomizationEXE",
		[] { g_sim.SetRandomFillEXEEnabled(!g_sim.IsRandomFillEXEEnabled()); } },
	{ "System.ToggleBASIC",
		[] { g_sim.SetBASICEnabled(!g_sim.IsBASICEnabled()); } },
	{ "System.ToggleFastBoot",
		[] { g_sim.SetFastBootEnabled(!g_sim.IsFastBootEnabled()); } },
	{ "System.ToggleRTime8",
		[] { g_sim.GetDeviceManager()->ToggleDevice("rtime8"); } },
	{ "System.SpeedMatchHardware",
		[] { ATUISetFrameRateMode(kATFrameRateMode_Hardware); } },
	{ "System.SpeedMatchBroadcast",
		[] { ATUISetFrameRateMode(kATFrameRateMode_Broadcast); } },
	{ "System.SpeedInteger",
		[] { ATUISetFrameRateMode(kATFrameRateMode_Integral); } },
	{ "System.SpeedToggleVSyncAdaptive",
		[] { ATUISetFrameRateVSyncAdaptive(!ATUIGetFrameRateVSyncAdaptive()); } },

	// Memory clear modes
	{ "System.MemoryClearModeZero",
		[] { g_sim.SetMemoryClearMode(kATMemoryClearMode_Zero); } },
	{ "System.MemoryClearModeRandom",
		[] { g_sim.SetMemoryClearMode(kATMemoryClearMode_Random); } },
	{ "System.MemoryClearModeDRAM1",
		[] { g_sim.SetMemoryClearMode(kATMemoryClearMode_DRAM1); } },
	{ "System.MemoryClearModeDRAM2",
		[] { g_sim.SetMemoryClearMode(kATMemoryClearMode_DRAM2); } },
	{ "System.MemoryClearModeDRAM3",
		[] { g_sim.SetMemoryClearMode(kATMemoryClearMode_DRAM3); } },

	// Hardware mode shortcuts (the ATUISwitchHardwareMode in
	// uiaccessors_stubs.cpp does the heavy lifting, including
	// auto-profile switching).
	{ "System.HardwareMode800",
		[] { ATUISwitchHardwareMode(nullptr, kATHardwareMode_800, false); } },
	{ "System.HardwareMode800XL",
		[] { ATUISwitchHardwareMode(nullptr, kATHardwareMode_800XL, false); } },
	{ "System.HardwareMode1200XL",
		[] { ATUISwitchHardwareMode(nullptr, kATHardwareMode_1200XL, false); } },
	{ "System.HardwareModeXEGS",
		[] { ATUISwitchHardwareMode(nullptr, kATHardwareMode_XEGS, false); } },
	{ "System.HardwareMode130XE",
		[] { ATUISwitchHardwareMode(nullptr, kATHardwareMode_130XE, false); } },
	{ "System.HardwareMode5200",
		[] { ATUISwitchHardwareMode(nullptr, kATHardwareMode_5200, false); } },

	// Memory mode shortcuts
	{ "System.MemoryMode8K",
		[] { ATUISwitchMemoryMode(nullptr, kATMemoryMode_8K); } },
	{ "System.MemoryMode16K",
		[] { ATUISwitchMemoryMode(nullptr, kATMemoryMode_16K); } },
	{ "System.MemoryMode24K",
		[] { ATUISwitchMemoryMode(nullptr, kATMemoryMode_24K); } },
	{ "System.MemoryMode32K",
		[] { ATUISwitchMemoryMode(nullptr, kATMemoryMode_32K); } },
	{ "System.MemoryMode40K",
		[] { ATUISwitchMemoryMode(nullptr, kATMemoryMode_40K); } },
	{ "System.MemoryMode48K",
		[] { ATUISwitchMemoryMode(nullptr, kATMemoryMode_48K); } },
	{ "System.MemoryMode52K",
		[] { ATUISwitchMemoryMode(nullptr, kATMemoryMode_52K); } },
	{ "System.MemoryMode64K",
		[] { ATUISwitchMemoryMode(nullptr, kATMemoryMode_64K); } },
	{ "System.MemoryMode128K",
		[] { ATUISwitchMemoryMode(nullptr, kATMemoryMode_128K); } },
	{ "System.MemoryMode320K",
		[] { ATUISwitchMemoryMode(nullptr, kATMemoryMode_320K); } },
	{ "System.MemoryMode576K",
		[] { ATUISwitchMemoryMode(nullptr, kATMemoryMode_576K); } },
	{ "System.MemoryMode1088K",
		[] { ATUISwitchMemoryMode(nullptr, kATMemoryMode_1088K); } },

	// Video standard shortcuts
	{ "System.VideoStandardNTSC",
		[] { g_sim.SetVideoStandard(kATVideoStandard_NTSC); } },
	{ "System.VideoStandardPAL",
		[] { g_sim.SetVideoStandard(kATVideoStandard_PAL); } },
	{ "System.VideoStandardSECAM",
		[] { g_sim.SetVideoStandard(kATVideoStandard_SECAM); } },
	{ "System.VideoStandardNTSC50",
		[] { g_sim.SetVideoStandard(kATVideoStandard_NTSC50); } },
	{ "System.VideoStandardPAL60",
		[] { g_sim.SetVideoStandard(kATVideoStandard_PAL60); } },

	// =====================================================================
	// Cart (cmdcart.cpp) — detach. Attach commands route to the ImGui
	// cart picker which is opened from the File menu; this only needs
	// to expose the detach so VM scripts and accelerators can drop the
	// current cart without UI interaction.
	// =====================================================================
	{ "Cart.Detach",
		[] { g_sim.LoadCartridge(0, nullptr, (ATCartLoadContext *)nullptr); },
		IsCart0Attached },
	{ "Cart.DetachSecond",
		[] { g_sim.LoadCartridge(1, nullptr, (ATCartLoadContext *)nullptr); },
		IsCart1Attached },

	// =====================================================================
	// Help (cmds.cpp) — URL launchers via SDL_OpenURL.
	// =====================================================================
	{ "Help.Online",
		[] { SDL_OpenURL("http://www.virtualdub.org/altirra.html"); } },
};

// =========================================================================
// Original SDL3 command table (display/global/debugger context)
// =========================================================================

static const ATUICommand kSDL3Commands[] = {
	// Display context
	{ "System.PulseWarpOn",            CmdPulseWarpOn,          nullptr, nullptr, nullptr },
	{ "System.PulseWarpOff",           CmdPulseWarpOff,         nullptr, nullptr, nullptr },
	{ "Input.CycleQuickMaps",          CmdCycleQuickMaps,       nullptr, nullptr, nullptr },
	{ "Console.HoldKeys",              CmdHoldKeys,             nullptr, nullptr, nullptr },
	{ "System.WarmReset",              CmdWarmReset,             nullptr, nullptr, nullptr },
	{ "System.ColdReset",              CmdColdReset,             nullptr, nullptr, nullptr },
	{ "Video.ToggleStandardNTSCPAL",   CmdToggleNTSCPAL,       nullptr, nullptr, nullptr },
	{ "View.NextANTICVisMode",         CmdNextANTICVisMode,     nullptr, nullptr, nullptr },
	{ "View.NextGTIAVisMode",          CmdNextGTIAVisMode,      nullptr, nullptr, nullptr },
	{ "System.TogglePause",            CmdTogglePause,          nullptr, nullptr, nullptr },
	{ "Netplay.OpenEmotePicker",       CmdOpenEmotePicker,      nullptr, nullptr, nullptr },
	{ "Input.CaptureMouse",            CmdCaptureMouse,         nullptr, nullptr, nullptr },
	{ "View.ToggleFullScreen",         CmdToggleFullScreen,     nullptr, nullptr, nullptr },
	{ "System.ToggleSlowMotion",       CmdToggleSlowMotion,     nullptr, nullptr, nullptr },
	{ "Audio.ToggleChannel1",          CmdToggleChannel1,       nullptr, nullptr, nullptr },
	{ "Audio.ToggleChannel2",          CmdToggleChannel2,       nullptr, nullptr, nullptr },
	{ "Audio.ToggleChannel3",          CmdToggleChannel3,       nullptr, nullptr, nullptr },
	{ "Audio.ToggleChannel4",          CmdToggleChannel4,       nullptr, nullptr, nullptr },
	{ "Edit.PasteText",                CmdPasteText,            ATUIClipIsTextAvailable, nullptr, nullptr },
	{ "Edit.SaveFrame",                CmdSaveFrame,            nullptr, nullptr, nullptr },
	{ "Edit.CopyText",                 CmdCopyText,             nullptr, nullptr, nullptr },
	{ "Edit.CopyFrame",                CmdCopyFrame,            nullptr, nullptr, nullptr },
	{ "Edit.SelectAll",                CmdSelectAll,            nullptr, nullptr, nullptr },
	{ "Edit.Deselect",                 CmdDeselect,             nullptr, nullptr, nullptr },

	// Global context
	{ "File.BootImage",                CmdBootImage,            nullptr, nullptr, nullptr },
	{ "File.OpenImage",                CmdOpenImage,            nullptr, nullptr, nullptr },
	{ "Debug.OpenSourceFile",          CmdOpenSourceFile,       nullptr, nullptr, nullptr },
	{ "Disk.DrivesDialog",             CmdDrivesDialog,         nullptr, nullptr, nullptr },
	{ "System.Configure",              CmdConfigure,            nullptr, nullptr, nullptr },
	{ "Cheat.CheatDialog",             CmdCheatDialog,          nullptr, nullptr, nullptr },
	{ "Debug.RunStop",                 CmdDebugRunStop,         nullptr, nullptr, nullptr },
	{ "Debug.StepInto",                CmdDebugStepInto,        nullptr, nullptr, nullptr },
	{ "Debug.StepOver",                CmdDebugStepOver,        nullptr, nullptr, nullptr },
	{ "Debug.StepOut",                 CmdDebugStepOut,         nullptr, nullptr, nullptr },
	{ "Debug.Break",                   CmdDebugBreak,           nullptr, nullptr, nullptr },
	{ "Pane.Display",                  CmdPaneDisplay,          TestDebuggerOpen, nullptr, nullptr },
	{ "Pane.Console",                  CmdPaneConsole,          TestDebuggerOpen, nullptr, nullptr },
	{ "Pane.Registers",                CmdPaneRegisters,        TestDebuggerOpen, nullptr, nullptr },
	{ "Pane.Disassembly",              CmdPaneDisassembly,      TestDebuggerOpen, nullptr, nullptr },
	{ "Pane.CallStack",                CmdPaneCallStack,        TestDebuggerOpen, nullptr, nullptr },
	{ "Pane.History",                  CmdPaneHistory,          TestDebuggerOpen, nullptr, nullptr },
	{ "Pane.Memory1",                  CmdPaneMemory1,          TestDebuggerOpen, nullptr, nullptr },
	{ "Pane.PrinterOutput",            CmdPanePrinterOutput,    TestDebuggerOpen, nullptr, nullptr },
	{ "Pane.ProfileView",              CmdPaneProfileView,      TestDebuggerOpen, nullptr, nullptr },

	// Debugger context
	{ "Debug.Run",                     CmdDebugRun,             nullptr, nullptr, nullptr },
	{ "Debug.ToggleBreakpoint",        CmdDebugToggleBreakpoint, nullptr, nullptr, nullptr },
	{ "Debug.NewBreakpoint",           CmdDebugNewBreakpoint,   nullptr, nullptr, nullptr },
};

void ATUIInitSDL3Commands() {
	g_ATUICommandMgr.RegisterCommands(kSDL3Commands, vdcountof(kSDL3Commands));
	g_ATUICommandMgr.RegisterCommands(kSDL3CommandsExtra, vdcountof(kSDL3CommandsExtra));
}
