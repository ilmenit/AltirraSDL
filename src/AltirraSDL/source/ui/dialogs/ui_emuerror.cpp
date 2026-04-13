//	AltirraSDL - ImGui emulation error dialog
//	Replaces Windows IDD_PROGRAM_ERROR / ATUIDialogEmuError / ATEmuErrorHandler.
//
//	When the emulated CPU encounters an illegal state, the debugger fires
//	OnDebuggerOpen.  This handler intercepts the event, pauses the simulator,
//	and sets a flag so the ImGui dialog renders on the next frame.

#include <stdafx.h>
#include <imgui.h>
#include <vd2/system/vdtypes.h>
#include <vd2/system/VDString.h>
#include <vd2/system/event.h>

#include "ui_emuerror.h"
#include "ui_main.h"
#include "ui_mode.h"
#include "touch_widgets.h"
#include "simulator.h"
#include "constants.h"
#include "cpu.h"
#include "options.h"
#include "debugger.h"

// Debugger open/close from ui_debugger.cpp
extern void ATUIDebuggerOpen();

///////////////////////////////////////////////////////////////////////////
// Dialog state
///////////////////////////////////////////////////////////////////////////

static bool g_showEmuError = false;
static bool g_emuErrorNeedOpen = false;
static bool g_emuErrorRequestDebugger = false;

// Checkbox states — set when dialog opens based on hardware config
static bool g_chkHardware = false;
static bool g_chkFirmware = false;
static bool g_chkMemory = false;
static bool g_chkVideo = false;
static bool g_chkBasic = false;
static bool g_chkCPU = false;
static bool g_chkDebugging = false;
static bool g_chkDiskIO = false;

// Whether each checkbox is enabled (relevant given current config)
static bool g_enHardware = false;
static bool g_enFirmware = false;
static bool g_enMemory = false;
static bool g_enVideo = false;
static bool g_enBasic = false;
static bool g_enCPU = false;
static bool g_enDebugging = false;
static bool g_enDiskIO = false;

// Computed target values for changes
static bool g_newPALMode = false;

///////////////////////////////////////////////////////////////////////////
// Populate checkbox enabled states from current hardware config
// (mirrors ATUIDialogEmuError::OnDataExchange in uiemuerror.cpp)
///////////////////////////////////////////////////////////////////////////

static void PopulateCheckboxStates(ATSimulator &sim) {
	// Reset all checkboxes to unchecked
	g_chkHardware = g_chkFirmware = g_chkMemory = g_chkVideo = false;
	g_chkBasic = g_chkCPU = g_chkDebugging = g_chkDiskIO = false;

	ATHardwareMode hw = sim.GetHardwareMode();

	// Hardware mode — enable if not already 800XL or 5200
	switch (hw) {
		case kATHardwareMode_800:
		case kATHardwareMode_1200XL:
		case kATHardwareMode_XEGS:
		case kATHardwareMode_130XE:
		case kATHardwareMode_1400XL:
			g_enHardware = true;
			break;
		default:
			g_enHardware = false;
			break;
	}

	// Firmware — enable if not 5200 and kernel not Default/XL
	if (hw != kATHardwareMode_5200) {
		switch (sim.GetKernelMode()) {
			case kATKernelMode_Default:
			case kATKernelMode_XL:
				g_enFirmware = false;
				break;
			default:
				g_enFirmware = true;
				break;
		}
	} else {
		g_enFirmware = false;
	}

	// Memory — enable if not 5200 and not already 320K
	g_enMemory = (hw != kATHardwareMode_5200) && (sim.GetMemoryMode() != kATMemoryMode_320K);

	// Video — enable if not 5200
	g_enVideo = (hw != kATHardwareMode_5200);
	g_newPALMode = (sim.GetVideoStandard() == kATVideoStandard_NTSC);

	// BASIC
	g_enBasic = sim.IsBASICEnabled();

	// CPU
	ATCPUEmulator &cpu = sim.GetCPU();
	g_enCPU = (cpu.GetCPUMode() != kATCPUMode_6502);

	// Debugging
	g_enDebugging = cpu.IsPathBreakEnabled() || !cpu.AreIllegalInsnsEnabled() || cpu.GetStopOnBRK();

	// Disk I/O
	g_enDiskIO = !sim.IsDiskAccurateTimingEnabled() || sim.IsDiskSIOPatchEnabled();
}

///////////////////////////////////////////////////////////////////////////
// Apply checked changes (mirrors ATUIDialogEmuError::OnOK)
///////////////////////////////////////////////////////////////////////////

static void ApplyChanges(ATSimulator &sim) {
	if (g_chkHardware && g_enHardware) {
		sim.SetHardwareMode(kATHardwareMode_800XL);

		if (kATHardwareModeTraits[kATHardwareMode_800XL].mbRunsXLOS) {
			switch (sim.GetMemoryMode()) {
				case kATMemoryMode_8K:
				case kATMemoryMode_24K:
				case kATMemoryMode_32K:
				case kATMemoryMode_40K:
				case kATMemoryMode_48K:
				case kATMemoryMode_52K:
					sim.SetMemoryMode(kATMemoryMode_64K);
					break;
				default:
					break;
			}
		}
	}

	if (g_chkMemory && g_enMemory)
		sim.SetMemoryMode(kATMemoryMode_320K);

	if (g_chkFirmware && g_enFirmware)
		sim.SetKernel(0);

	if (g_chkVideo && g_enVideo)
		sim.SetVideoStandard(g_newPALMode ? kATVideoStandard_PAL : kATVideoStandard_NTSC);

	if (g_chkBasic && g_enBasic)
		sim.SetBASICEnabled(false);

	ATCPUEmulator &cpu = sim.GetCPU();
	if (g_chkCPU && g_enCPU)
		sim.SetCPUMode(kATCPUMode_6502, 1);

	if (g_chkDebugging && g_enDebugging) {
		cpu.SetIllegalInsnsEnabled(true);
		cpu.SetPathBreakEnabled(false);
		cpu.SetStopOnBRK(false);
	}

	if (g_chkDiskIO && g_enDiskIO) {
		sim.SetDiskSIOPatchEnabled(false);
		sim.SetDiskAccurateTimingEnabled(true);
	}
}

///////////////////////////////////////////////////////////////////////////
// Error handler — hooks into debugger OnDebuggerOpen event
///////////////////////////////////////////////////////////////////////////

class ATEmuErrorHandlerSDL3 {
public:
	void Init(ATSimulator *sim);
	void Shutdown();

private:
	void OnDebuggerOpen(IATDebugger *dbg, ATDebuggerOpenEvent *event);

	ATSimulator *mpSim = nullptr;
	VDDelegate mDelDebuggerOpen;
};

void ATEmuErrorHandlerSDL3::Init(ATSimulator *sim) {
	mpSim = sim;
	ATGetDebugger()->OnDebuggerOpen() += mDelDebuggerOpen.Bind(this, &ATEmuErrorHandlerSDL3::OnDebuggerOpen);
}

void ATEmuErrorHandlerSDL3::Shutdown() {
	ATGetDebugger()->OnDebuggerOpen() -= mDelDebuggerOpen;
	mpSim = nullptr;
}

void ATEmuErrorHandlerSDL3::OnDebuggerOpen(IATDebugger *, ATDebuggerOpenEvent *event) {
	if (!mpSim)
		return;

	extern ATOptions g_ATOptions;

	switch (g_ATOptions.mErrorMode) {
		case kATErrorMode_Dialog:
			break;

		case kATErrorMode_Debug:
			// Allow debugger to open
			return;

		case kATErrorMode_Pause:
			mpSim->Pause();
			event->mbAllowOpen = false;
			return;

		case kATErrorMode_ColdReset:
			mpSim->ColdReset();
			mpSim->Resume();
			event->mbAllowOpen = false;
			return;
	}

	// Show the ImGui error dialog.  Since we're inside sim.Advance(),
	// we can't render ImGui here — just set state and return.
	// The dialog will be drawn on the next frame.
	mpSim->Pause();
	PopulateCheckboxStates(*mpSim);
	g_showEmuError = true;
	g_emuErrorNeedOpen = true;
	event->mbAllowOpen = false;
}

static ATEmuErrorHandlerSDL3 g_emuErrorHandler;

void ATInitEmuErrorHandlerSDL3(ATSimulator *sim) {
	g_emuErrorHandler.Init(sim);
}

void ATShutdownEmuErrorHandlerSDL3() {
	g_emuErrorHandler.Shutdown();
}

///////////////////////////////////////////////////////////////////////////
// Desktop-mode ImGui dialog render
///////////////////////////////////////////////////////////////////////////

static void RenderEmuErrorDesktop(ATSimulator &sim) {
	if (g_emuErrorNeedOpen) {
		ImGui::OpenPopup("Program Error");
		g_emuErrorNeedOpen = false;
	}

	ImGui::SetNextWindowSize(ImVec2(480, 0), ImGuiCond_Appearing);
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
		ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

	if (ImGui::BeginPopupModal("Program Error", nullptr,
			ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
		ImGui::TextWrapped(
			"The emulated computer has stopped due to a program error.\n"
			"You can try changing one of these options and restarting:");
		ImGui::Spacing();

		ImGui::BeginDisabled(!g_enHardware);
		ImGui::Checkbox("Change hardware mode to XL/XE", &g_chkHardware);
		ImGui::EndDisabled();

		ImGui::BeginDisabled(!g_enFirmware);
		ImGui::Checkbox("Change firmware to Default", &g_chkFirmware);
		ImGui::EndDisabled();

		ImGui::BeginDisabled(!g_enMemory);
		ImGui::Checkbox("Change memory to 320K", &g_chkMemory);
		ImGui::EndDisabled();

		ImGui::BeginDisabled(!g_enVideo);
		if (g_newPALMode)
			ImGui::Checkbox("Change video standard to PAL", &g_chkVideo);
		else
			ImGui::Checkbox("Change video standard to NTSC", &g_chkVideo);
		ImGui::EndDisabled();

		ImGui::BeginDisabled(!g_enBasic);
		ImGui::Checkbox("Disable BASIC", &g_chkBasic);
		ImGui::EndDisabled();

		ImGui::BeginDisabled(!g_enCPU);
		ImGui::Checkbox("Disable CPU diagnostics (use 6502)", &g_chkCPU);
		ImGui::EndDisabled();

		ImGui::BeginDisabled(!g_enDebugging);
		ImGui::Checkbox("Disable debugging options", &g_chkDebugging);
		ImGui::EndDisabled();

		ImGui::BeginDisabled(!g_enDiskIO);
		ImGui::Checkbox("Enable accurate floppy disk timing", &g_chkDiskIO);
		ImGui::EndDisabled();

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		if (ImGui::Button("Debug", ImVec2(100, 0))) {
			g_showEmuError = false;
			g_emuErrorRequestDebugger = true;
			ImGui::CloseCurrentPopup();
		}

		ImGui::SameLine();
		if (ImGui::Button("Warm Reset", ImVec2(100, 0))) {
			g_showEmuError = false;
			sim.WarmReset();
			sim.Resume();
			ImGui::CloseCurrentPopup();
		}

		ImGui::SameLine();
		if (ImGui::Button("Restart", ImVec2(100, 0))) {
			g_showEmuError = false;
			ApplyChanges(sim);
			sim.ColdReset();
			sim.Resume();
			ImGui::CloseCurrentPopup();
		}

		ImGui::SameLine();
		if (ImGui::Button("Pause", ImVec2(100, 0))) {
			g_showEmuError = false;
			ImGui::CloseCurrentPopup();
		}

		if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
			g_showEmuError = false;
			ImGui::CloseCurrentPopup();
		}

		ImGui::EndPopup();
	}
}

///////////////////////////////////////////////////////////////////////////
// Gaming-mode card-style dialog render
///////////////////////////////////////////////////////////////////////////

static float GamingDP(float v) {
	ImGuiIO &io = ImGui::GetIO();
	float fs = io.Fonts->Fonts.Size > 0
		? io.Fonts->Fonts[0]->FontSize : 13.0f;
	float scale = (fs / 16.0f) * io.FontGlobalScale;
	if (scale < 0.5f) scale = 0.5f;
	if (scale > 5.0f) scale = 5.0f;
	return v * scale;
}

static void RenderEmuErrorGaming(ATSimulator &sim) {
	g_emuErrorNeedOpen = false;

	ImGuiIO &io = ImGui::GetIO();

	// Dim backdrop
	ImGui::GetBackgroundDrawList()->AddRectFilled(
		ImVec2(0, 0), io.DisplaySize,
		IM_COL32(0, 0, 0, 160));

	float sheetW = GamingDP(520.0f);
	if (sheetW > io.DisplaySize.x - GamingDP(32.0f))
		sheetW = io.DisplaySize.x - GamingDP(32.0f);
	if (sheetW < GamingDP(300.0f))
		sheetW = GamingDP(300.0f);

	// Window height: 8 toggle rows (56dp each) + header + footer,
	// capped to fit the screen.  Content is static so no per-frame
	// measurement is needed.
	float toggleAreaH  = GamingDP(56.0f) * 8.0f;
	float headerAreaH  = GamingDP(130.0f);
	float footerAreaH  = GamingDP(170.0f);
	float desiredH = headerAreaH + toggleAreaH + footerAreaH;
	float maxH = io.DisplaySize.y - GamingDP(48.0f);
	float windowH = desiredH < maxH ? desiredH : maxH;
	if (windowH < GamingDP(300.0f)) windowH = GamingDP(300.0f);

	ImGui::SetNextWindowPos(
		ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f),
		ImGuiCond_Always, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSize(ImVec2(sheetW, windowH));
	ImGui::SetNextWindowFocus();

	ImGuiStyle &style = ImGui::GetStyle();
	float prevR = style.WindowRounding;
	float prevB = style.WindowBorderSize;
	style.WindowRounding = GamingDP(14.0f);
	style.WindowBorderSize = GamingDP(1.0f);
	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.10f, 0.12f, 0.18f, 0.98f));
	ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.27f, 0.51f, 0.82f, 1.0f));

	ImGui::Begin("##GamingEmuError", nullptr,
		ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
		| ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse
		| ImGuiWindowFlags_NoSavedSettings
		| ImGuiWindowFlags_NoScrollbar
		| ImGuiWindowFlags_NoScrollWithMouse);

	// Title (non-scrolling header)
	ImGui::Dummy(ImVec2(0, GamingDP(8.0f)));
	ImGui::SetWindowFontScale(1.25f);
	ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.40f, 0.70f, 1.00f, 1.0f));
	ImGui::TextUnformatted("Program Error");
	ImGui::PopStyleColor();
	ImGui::SetWindowFontScale(1.0f);
	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();

	ImGui::PushTextWrapPos(sheetW - GamingDP(24.0f));
	ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.90f, 0.92f, 0.96f, 1));
	ImGui::TextUnformatted(
		"The emulated computer has stopped due to a program error.\n"
		"You can try changing these options and restarting:");
	ImGui::PopStyleColor();
	ImGui::PopTextWrapPos();
	ImGui::Dummy(ImVec2(0, GamingDP(8.0f)));

	// Buttons are 2 rows × (56dp + 12dp gap) + 8dp pad = ~140dp.
	// Reserve that space so buttons always stay visible at the bottom,
	// and the toggle list gets the remaining height as a scroll area.
	float btnAreaH = GamingDP(56.0f) * 2.0f + GamingDP(12.0f) * 2.0f
		+ GamingDP(8.0f);
	float separatorH = GamingDP(28.0f);
	float childH = ImGui::GetContentRegionAvail().y
		- btnAreaH - separatorH;
	if (childH < GamingDP(100.0f))
		childH = GamingDP(100.0f);

	// Scrollable child for the 8 compatibility toggles
	ImGui::BeginChild("##EmuErrorToggles", ImVec2(0, childH),
		ImGuiChildFlags_NavFlattened);
	ATTouchDragScroll();

	if (!g_enHardware) ImGui::BeginDisabled();
	ATTouchToggle("Change hardware to XL/XE", &g_chkHardware);
	if (!g_enHardware) ImGui::EndDisabled();

	if (!g_enFirmware) ImGui::BeginDisabled();
	ATTouchToggle("Change firmware to Default", &g_chkFirmware);
	if (!g_enFirmware) ImGui::EndDisabled();

	if (!g_enMemory) ImGui::BeginDisabled();
	ATTouchToggle("Change memory to 320K", &g_chkMemory);
	if (!g_enMemory) ImGui::EndDisabled();

	if (!g_enVideo) ImGui::BeginDisabled();
	ATTouchToggle(g_newPALMode
		? "Change video to PAL" : "Change video to NTSC",
		&g_chkVideo);
	if (!g_enVideo) ImGui::EndDisabled();

	if (!g_enBasic) ImGui::BeginDisabled();
	ATTouchToggle("Disable BASIC", &g_chkBasic);
	if (!g_enBasic) ImGui::EndDisabled();

	if (!g_enCPU) ImGui::BeginDisabled();
	ATTouchToggle("Disable CPU diagnostics", &g_chkCPU);
	if (!g_enCPU) ImGui::EndDisabled();

	if (!g_enDebugging) ImGui::BeginDisabled();
	ATTouchToggle("Disable debugging options", &g_chkDebugging);
	if (!g_enDebugging) ImGui::EndDisabled();

	if (!g_enDiskIO) ImGui::BeginDisabled();
	ATTouchToggle("Use accurate disk timing", &g_chkDiskIO);
	if (!g_enDiskIO) ImGui::EndDisabled();

	ATTouchEndDragScroll();
	ImGui::EndChild();

	// Separator + action buttons (non-scrolling footer)
	ImGui::Dummy(ImVec2(0, GamingDP(4.0f)));
	ImGui::Separator();
	ImGui::Dummy(ImVec2(0, GamingDP(8.0f)));

	float btnH = GamingDP(56.0f);
	float rowW = ImGui::GetContentRegionAvail().x;
	float gap = GamingDP(12.0f);
	float halfW = (rowW - gap) * 0.5f;

	// Row 1: Restart (primary) | Warm Reset
	ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.25f, 0.55f, 0.90f, 1));
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered,  ImVec4(0.30f, 0.62f, 0.95f, 1));
	ImGui::PushStyleColor(ImGuiCol_ButtonActive,   ImVec4(0.20f, 0.48f, 0.85f, 1));
	if (ImGui::Button("Restart", ImVec2(halfW, btnH))) {
		g_showEmuError = false;
		ApplyChanges(sim);
		sim.ColdReset();
		sim.Resume();
	}
	ImGui::PopStyleColor(3);

	ImGui::SameLine(0.0f, gap);

	ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.30f, 0.32f, 0.38f, 1));
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered,  ImVec4(0.38f, 0.40f, 0.48f, 1));
	ImGui::PushStyleColor(ImGuiCol_ButtonActive,   ImVec4(0.22f, 0.24f, 0.30f, 1));
	if (ImGui::Button("Warm Reset", ImVec2(halfW, btnH))) {
		g_showEmuError = false;
		sim.WarmReset();
		sim.Resume();
	}
	ImGui::PopStyleColor(3);

	ImGui::Dummy(ImVec2(0, gap));

	// Row 2: Debug | Pause
	ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.30f, 0.32f, 0.38f, 1));
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered,  ImVec4(0.38f, 0.40f, 0.48f, 1));
	ImGui::PushStyleColor(ImGuiCol_ButtonActive,   ImVec4(0.22f, 0.24f, 0.30f, 1));
	if (ImGui::Button("Debug", ImVec2(halfW, btnH))) {
		g_showEmuError = false;
		g_emuErrorRequestDebugger = true;
	}
	ImGui::PopStyleColor(3);

	ImGui::SameLine(0.0f, gap);

	ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.30f, 0.32f, 0.38f, 1));
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered,  ImVec4(0.38f, 0.40f, 0.48f, 1));
	ImGui::PushStyleColor(ImGuiCol_ButtonActive,   ImVec4(0.22f, 0.24f, 0.30f, 1));
	if (ImGui::Button("Pause", ImVec2(halfW, btnH))) {
		g_showEmuError = false;
	}
	ImGui::PopStyleColor(3);

	ImGui::Dummy(ImVec2(0, GamingDP(8.0f)));

	// ESC / B-button closes as Pause
	{
		bool dismiss = ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false);
		if (!ImGui::IsAnyItemActive()) {
			dismiss = dismiss
				|| ImGui::IsKeyPressed(ImGuiKey_Escape, false)
				|| ImGui::IsKeyPressed(ImGuiKey_Backspace, false);
		}
		if (dismiss)
			g_showEmuError = false;
	}

	ImGui::End();
	ImGui::PopStyleColor(2);
	style.WindowRounding = prevR;
	style.WindowBorderSize = prevB;
}

///////////////////////////////////////////////////////////////////////////
// Public entry point (called each frame from ATUIRenderFrame)
///////////////////////////////////////////////////////////////////////////

void ATUIRenderEmuErrorDialog(ATSimulator &sim) {
	if (g_emuErrorRequestDebugger) {
		g_emuErrorRequestDebugger = false;
		ATUIDebuggerOpen();
	}

	if (!g_showEmuError)
		return;

	if (ATUIIsGamingMode())
		RenderEmuErrorGaming(sim);
	else
		RenderEmuErrorDesktop(sim);
}
