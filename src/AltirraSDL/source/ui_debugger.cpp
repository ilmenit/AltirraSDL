//	AltirraSDL - Dear ImGui debugger pane manager implementation
//	Replaces Win32 ATUIPane / ATContainerWindow docking with ImGui docking.

#include <stdafx.h>
#include <vector>
#include <vd2/system/vdtypes.h>
#include <vd2/system/error.h>
#include <vd2/system/vdstl.h>
#include <at/atnativeui/uiframe.h>
#include <imgui.h>
#include "ui_debugger.h"
#include "ui_main.h"
#include "console.h"
#include "debugger.h"
#include "simulator.h"

extern ATSimulator g_sim;

// =========================================================================
// ATImGuiDebuggerPane base class
// =========================================================================

ATImGuiDebuggerPane::ATImGuiDebuggerPane(uint32 paneId, const char *title)
	: mPaneId(paneId)
	, mTitle(title)
{
}

ATImGuiDebuggerPane::~ATImGuiDebuggerPane() {
}

void ATImGuiDebuggerPane::OnDebuggerSystemStateUpdate(const ATDebuggerSystemState& state) {
	mLastState = state;
	mbStateValid = true;
}

void ATImGuiDebuggerPane::OnDebuggerEvent(ATDebugEvent eventId) {
	// Subclasses override as needed
}

// =========================================================================
// Pane manager globals
// =========================================================================

namespace {
	struct PaneEntry {
		uint32 id;
		vdrefptr<ATImGuiDebuggerPane> pane;
	};

	std::vector<PaneEntry> g_debugPanes;
	bool g_debuggerOpen = false;

	// Pane creator registry (populated by ATRegisterUIPaneType/Class)
	struct PaneCreatorEntry {
		uint32 id;
		ATPaneCreator creator;
		ATPaneClassCreator classCreator;
	};
	std::vector<PaneCreatorEntry> g_paneCreators;
}

// =========================================================================
// Pane registration — called from debugger.cpp's InitDebugger
// =========================================================================

void ATRegisterUIPaneType(uint32 id, ATPaneCreator creator) {
	for (auto& e : g_paneCreators) {
		if (e.id == id) {
			e.creator = creator;
			e.classCreator = nullptr;
			return;
		}
	}
	g_paneCreators.push_back({id, creator, nullptr});
}

void ATRegisterUIPaneClass(uint32 id, ATPaneClassCreator creator) {
	for (auto& e : g_paneCreators) {
		if (e.id == id) {
			e.creator = nullptr;
			e.classCreator = creator;
			return;
		}
	}
	g_paneCreators.push_back({id, nullptr, creator});
}

// Forward declarations for pane creation functions
extern void ATUIDebuggerEnsureConsolePane();
extern void ATUIDebuggerEnsureRegistersPane();

static void EnsurePaneExists(uint32 id) {
	switch (id) {
		case kATUIPaneId_Console:    ATUIDebuggerEnsureConsolePane(); break;
		case kATUIPaneId_Registers:  ATUIDebuggerEnsureRegistersPane(); break;
		// Future panes will be added here as they're implemented
		default:
			fprintf(stderr, "[Debugger] ATActivateUIPane(0x%x) — no ImGui pane implemented yet\n", id);
			break;
	}
}

void ATActivateUIPane(uint32 id, bool giveFocus, bool visible, uint32 relid, int reldock) {
	// Check if pane already exists
	for (auto& e : g_debugPanes) {
		if (e.id == id) {
			e.pane->SetVisible(visible);
			if (giveFocus)
				e.pane->RequestFocus();
			return;
		}
	}

	// Pane doesn't exist — create it on demand
	EnsurePaneExists(id);

	// Try again after creation
	for (auto& e : g_debugPanes) {
		if (e.id == id) {
			e.pane->SetVisible(visible);
			if (giveFocus)
				e.pane->RequestFocus();
			return;
		}
	}
}

// =========================================================================
// Pane manager API
// =========================================================================

void ATUIDebuggerRegisterPane(ATImGuiDebuggerPane *pane) {
	if (!pane) return;

	// Remove existing pane with same ID
	for (auto it = g_debugPanes.begin(); it != g_debugPanes.end(); ++it) {
		if (it->id == pane->GetPaneId()) {
			IATDebugger *dbg = ATGetDebugger();
			if (dbg)
				dbg->RemoveClient(it->pane);
			g_debugPanes.erase(it);
			break;
		}
	}

	PaneEntry entry;
	entry.id = pane->GetPaneId();
	entry.pane = pane;
	g_debugPanes.push_back(entry);

	// Register as debugger client for state updates
	IATDebugger *dbg = ATGetDebugger();
	if (dbg && dbg->IsEnabled())
		dbg->AddClient(pane, true);
}

void ATUIDebuggerUnregisterPane(uint32 paneId) {
	for (auto it = g_debugPanes.begin(); it != g_debugPanes.end(); ++it) {
		if (it->id == paneId) {
			IATDebugger *dbg = ATGetDebugger();
			if (dbg)
				dbg->RemoveClient(it->pane);
			g_debugPanes.erase(it);
			return;
		}
	}
}

ATImGuiDebuggerPane *ATUIDebuggerGetPane(uint32 paneId) {
	for (auto& e : g_debugPanes) {
		if (e.id == paneId)
			return e.pane;
	}
	return nullptr;
}

// =========================================================================
// Init / Shutdown
// =========================================================================

void ATUIDebuggerInit() {
}

void ATUIDebuggerShutdown() {
	IATDebugger *dbg = ATGetDebugger();
	for (auto& e : g_debugPanes) {
		if (dbg)
			dbg->RemoveClient(e.pane);
	}
	g_debugPanes.clear();
	g_paneCreators.clear();
	g_debuggerOpen = false;
}

// =========================================================================
// Render all panes
// =========================================================================

void ATUIDebuggerRenderPanes(ATSimulator &sim, ATUIState &state) {
	if (!g_debuggerOpen)
		return;

	for (auto& e : g_debugPanes) {
		if (!e.pane->IsVisible())
			continue;

		if (!e.pane->Render()) {
			// Pane closed via X button
			e.pane->SetVisible(false);
		}
	}
}

// =========================================================================
// Tick
// =========================================================================

void ATUIDebuggerTick() {
	IATDebugger *dbg = ATGetDebugger();
	if (!dbg || !dbg->IsEnabled())
		return;

	// Process all pending commands, not just one.  Windows does this via
	// the idle loop (Tick returns true → loop again immediately).  We
	// replicate by looping here, with a safety limit to avoid blocking
	// the UI if a command generates infinite sub-commands.
	for (int safety = 0; safety < 1000; ++safety) {
		if (!dbg->Tick())
			break;
	}
}

// =========================================================================
// Open / Close
// =========================================================================

void ATUIDebuggerOpen() {
	if (g_debuggerOpen)
		return;

	IATDebugger *dbg = ATGetDebugger();
	if (!dbg)
		return;

	g_debuggerOpen = true;
	dbg->SetEnabled(true);

	// Register all existing panes as debugger clients before showing
	// the banner (which generates console output).
	for (auto& e : g_debugPanes) {
		dbg->AddClient(e.pane, true);
	}

	// Create default panes (matches Windows ATLoadDefaultPaneLayout:
	// Console + Registers + Disassembly + History).
	ATUIDebuggerEnsureConsolePane();
	ATUIDebuggerEnsureRegistersPane();

	dbg->ShowBannerOnce();
}

void ATUIDebuggerClose() {
	if (!g_debuggerOpen)
		return;

	IATDebugger *dbg = ATGetDebugger();
	if (dbg) {
		for (auto& e : g_debugPanes) {
			dbg->RemoveClient(e.pane);
		}
		dbg->SetEnabled(false);
	}

	g_debuggerOpen = false;
}

bool ATUIDebuggerIsOpen() {
	return g_debuggerOpen;
}

// =========================================================================
// Debug commands (for menu/shortcut wiring)
// =========================================================================

void ATUIDebuggerRunStop() {
	IATDebugger *dbg = ATGetDebugger();
	if (!dbg) return;

	if (!dbg->IsEnabled()) {
		ATUIDebuggerOpen();
		return;
	}

	// Match Windows cmddebug.cpp OnCommandDebugRunStop:
	// break if running OR if commands are still queued
	if (dbg->IsRunning() || dbg->AreCommandsQueued()) {
		ATOpenConsole();  // no-op if already open, but ensures console exists
		dbg->Break();
	} else {
		dbg->Run(kATDebugSrcMode_Same);
	}
}

void ATUIDebuggerBreak() {
	IATDebugger *dbg = ATGetDebugger();
	if (dbg && dbg->IsEnabled())
		dbg->Break();
}

void ATUIDebuggerStepInto() {
	IATDebugger *dbg = ATGetDebugger();
	if (!dbg || !dbg->IsEnabled()) return;
	try {
		dbg->StepInto(kATDebugSrcMode_Same);
	} catch(const MyError& e) {
		ATConsolePrintf("%s\n", e.c_str());
	}
}

void ATUIDebuggerStepOver() {
	IATDebugger *dbg = ATGetDebugger();
	if (!dbg || !dbg->IsEnabled()) return;
	try {
		dbg->StepOver(kATDebugSrcMode_Same);
	} catch(const MyError& e) {
		ATConsolePrintf("%s\n", e.c_str());
	}
}

void ATUIDebuggerStepOut() {
	IATDebugger *dbg = ATGetDebugger();
	if (!dbg || !dbg->IsEnabled()) return;
	try {
		dbg->StepOut(kATDebugSrcMode_Same);
	} catch(const MyError& e) {
		ATConsolePrintf("%s\n", e.c_str());
	}
}
