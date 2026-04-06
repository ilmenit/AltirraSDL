//	AltirraSDL - Debug menu
//	Split out of ui_menus.cpp (Phase 2c).

#include <stdafx.h>
#include <imgui.h>
#include <vd2/system/vdtypes.h>

#include <SDL3/SDL.h>

#include "ui_main.h"
#include "ui_debugger.h"
#include "ui_menus_internal.h"
#include "simulator.h"
#include "debugger.h"
#include "console.h"
#include "uitypes.h"
#include "uiaccessors.h"
#include "accel_sdl3.h"

extern ATSimulator g_sim;
extern SDL_Window *g_pWindow;

// --- body extracted from ui_menus.cpp -------------------------------------
void ATUIRenderDebugMenu(ATSimulator &sim) {
	IATDebugger *dbg = ATGetDebugger();
	bool dbgEnabled = ATUIDebuggerIsOpen();
	bool dbgRunning = dbg && dbg->IsRunning();

	if (ImGui::MenuItem("Enable Debugger", nullptr, dbgEnabled)) {
		if (dbgEnabled)
			ATUIDebuggerClose();
		else
			ATUIDebuggerOpen();
	}
	if (ImGui::MenuItem("Open Source File...", ATUIGetShortcutStringForCommand("Debug.OpenSourceFile"), false, dbgEnabled))
		ATUIShowOpenSourceFileDialog(g_pWindow);
	if (ImGui::MenuItem("Source File List...", nullptr, false, dbgEnabled))
		ATUIDebuggerShowSourceListDialog();

	if (ImGui::BeginMenu("Window", dbgEnabled)) {
		if (ImGui::MenuItem("Console"))
			ATActivateUIPane(kATUIPaneId_Console, true);
		if (ImGui::MenuItem("Registers"))
			ATActivateUIPane(kATUIPaneId_Registers, true);
		if (ImGui::MenuItem("Disassembly"))
			ATActivateUIPane(kATUIPaneId_Disassembly, true);
		if (ImGui::MenuItem("Call Stack"))
			ATActivateUIPane(kATUIPaneId_CallStack, true);
		if (ImGui::MenuItem("History"))
			ATActivateUIPane(kATUIPaneId_History, true);
		if (ImGui::BeginMenu("Memory")) {
			for (int i = 0; i < 4; ++i) {
				char label[16];
				snprintf(label, sizeof(label), "Memory %d", i + 1);
				if (ImGui::MenuItem(label))
					ATActivateUIPane(kATUIPaneId_MemoryN + i, true);
			}
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Watch")) {
			for (int i = 0; i < 4; ++i) {
				char label[16];
				snprintf(label, sizeof(label), "Watch %d", i + 1);
				if (ImGui::MenuItem(label))
					ATActivateUIPane(kATUIPaneId_WatchN + i, true);
			}
			ImGui::EndMenu();
		}
		if (ImGui::MenuItem("Breakpoints"))
			ATActivateUIPane(kATUIPaneId_Breakpoints, true);
		ImGui::MenuItem("Targets", nullptr, false, false);
		ImGui::MenuItem("Debug Display", nullptr, false, false);
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("Visualization")) {
		ATGTIAEmulator& gtia = sim.GetGTIA();
		auto am = gtia.GetAnalysisMode();

		if (ImGui::MenuItem("Cycle GTIA Visualization")) {
			int next = ((int)am + 1) % ATGTIAEmulator::kAnalyzeCount;
			gtia.SetAnalysisMode((ATGTIAEmulator::AnalysisMode)next);
		}
		if (ImGui::MenuItem("Cycle ANTIC Visualization")) {
			ATAnticEmulator& antic = sim.GetAntic();
			int next = ((int)antic.GetAnalysisMode() + 1) % ATAnticEmulator::kAnalyzeModeCount;
			antic.SetAnalysisMode((ATAnticEmulator::AnalysisMode)next);
		}

		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("Options")) {
		if (dbg) {
			bool breakAtExe = dbg->IsBreakOnEXERunAddrEnabled();
			if (ImGui::MenuItem("Break at EXE Run Address", nullptr, breakAtExe))
				dbg->SetBreakOnEXERunAddrEnabled(!breakAtExe);
		}
		{
			bool autoReload = sim.IsROMAutoReloadEnabled();
			if (ImGui::MenuItem("Auto-Reload ROMs on Cold Reset", nullptr, autoReload))
				sim.SetROMAutoReloadEnabled(!autoReload);
		}
		{
			bool randomEXE = sim.IsRandomFillEXEEnabled();
			if (ImGui::MenuItem("Randomize Memory On EXE Load", nullptr, randomEXE))
				sim.SetRandomFillEXEEnabled(!randomEXE);
		}
		ImGui::Separator();
		ImGui::MenuItem("Change Font...", nullptr, false, false);                   // TODO
		ImGui::EndMenu();
	}

	ImGui::Separator();

	{
		// Run/Break shows combined hint from Debug.Run (F5) and Debug.RunStop (F8)
		const char *runKey = ATUIGetShortcutStringForCommand("Debug.Run");
		const char *stopKey = ATUIGetShortcutStringForCommand("Debug.RunStop");
		static char runBreakHint[64];
		if (runKey[0] && stopKey[0])
			snprintf(runBreakHint, sizeof(runBreakHint), "%s/%s", runKey, stopKey);
		else if (runKey[0])
			snprintf(runBreakHint, sizeof(runBreakHint), "%s", runKey);
		else
			snprintf(runBreakHint, sizeof(runBreakHint), "%s", stopKey);
		if (ImGui::MenuItem("Run/Break", runBreakHint, false, dbgEnabled))
			ATUIDebuggerRunStop();
	}
	if (ImGui::MenuItem("Break", nullptr, false, dbgEnabled && dbgRunning))
		ATUIDebuggerBreak();

	ImGui::Separator();

	if (ImGui::MenuItem("Step Into", ATUIGetShortcutStringForCommand("Debug.StepInto"), false, dbgEnabled && !dbgRunning))
		ATUIDebuggerStepInto();
	if (ImGui::MenuItem("Step Over", ATUIGetShortcutStringForCommand("Debug.StepOver"), false, dbgEnabled && !dbgRunning))
		ATUIDebuggerStepOver();
	if (ImGui::MenuItem("Step Out", ATUIGetShortcutStringForCommand("Debug.StepOut"), false, dbgEnabled && !dbgRunning))
		ATUIDebuggerStepOut();
	if (ImGui::MenuItem("Toggle Breakpoint", ATUIGetShortcutStringForCommand("Debug.ToggleBreakpoint"), false, dbgEnabled && !dbgRunning))
		ATUIDebuggerToggleBreakpoint();
	if (ImGui::MenuItem("New Breakpoint...", ATUIGetShortcutStringForCommand("Debug.NewBreakpoint"), false, dbgEnabled)) {
		ATActivateUIPane(kATUIPaneId_Breakpoints, true, true);
		ATUIDebuggerShowBreakpointDialog(-1);
	}

	ImGui::Separator();

	if (ImGui::BeginMenu("Profile")) {
		if (ImGui::MenuItem("Profile View"))
			ATActivateUIPane(kATUIPaneId_Profiler, true);
		ImGui::EndMenu();
	}
	if (ImGui::MenuItem("Verifier...", nullptr, g_sim.IsVerifierEnabled()))
		ATUIShowDialogVerifier();
	if (ImGui::MenuItem("Performance Analyzer..."))
		ATActivateUIPane(kATUIPaneId_Profiler, true);
}
