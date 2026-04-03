//	AltirraSDL - Dear ImGui debugger pane manager implementation
//	Replaces Win32 ATUIPane / ATContainerWindow docking with ImGui docking.

#include <stdafx.h>
#include <vector>
#include <vd2/system/vdtypes.h>
#include <vd2/system/error.h>
#include <vd2/system/vdstl.h>
#include <at/atnativeui/uiframe.h>
#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_internal.h>
#include "ui_debugger.h"
#include "ui_main.h"
#include "console.h"
#include "debugger.h"
#include "simulator.h"
#include "display_sdl3_impl.h"

extern ATSimulator g_sim;
extern VDVideoDisplaySDL3 *g_pDisplay;

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
	bool g_dockLayoutApplied = false;

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
extern void ATUIDebuggerEnsureDisassemblyPane();
extern void ATUIDebuggerEnsureHistoryPane();
extern void ATUIDebuggerEnsureMemoryPane(int index);
extern void ATUIDebuggerEnsureBreakpointsPane();
extern void ATUIDebuggerEnsureCallStackPane();

static void EnsurePaneExists(uint32 id) {
	switch (id) {
		case kATUIPaneId_Console:     ATUIDebuggerEnsureConsolePane(); break;
		case kATUIPaneId_Registers:   ATUIDebuggerEnsureRegistersPane(); break;
		case kATUIPaneId_Disassembly: ATUIDebuggerEnsureDisassemblyPane(); break;
		case kATUIPaneId_History:     ATUIDebuggerEnsureHistoryPane(); break;
		case kATUIPaneId_Breakpoints: ATUIDebuggerEnsureBreakpointsPane(); break;
		case kATUIPaneId_CallStack:   ATUIDebuggerEnsureCallStackPane(); break;
		default:
			// Memory pane instances: kATUIPaneId_MemoryN + 0..3
			if (id >= kATUIPaneId_MemoryN && id <= kATUIPaneId_MemoryN + 3) {
				ATUIDebuggerEnsureMemoryPane(id - kATUIPaneId_MemoryN);
			} else {
				fprintf(stderr, "[Debugger] ATActivateUIPane(0x%x) — no ImGui pane implemented yet\n", id);
			}
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
// Display pane — renders emulation texture inside an ImGui dockable window
// =========================================================================

static void RenderDisplayPane() {
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
	bool open = true;
	if (ImGui::Begin("Display", &open)) {
		SDL_Texture *emuTex = g_pDisplay ? g_pDisplay->GetTexture() : nullptr;
		if (emuTex) {
			ImVec2 avail = ImGui::GetContentRegionAvail();
			if (avail.x > 0 && avail.y > 0) {
				// Maintain aspect ratio (Atari ~1.2:1 pixel aspect)
				float texW = (float)g_pDisplay->GetTextureWidth();
				float texH = (float)g_pDisplay->GetTextureHeight();
				if (texW > 0 && texH > 0) {
					float aspectRatio = texW / texH;
					float drawW = avail.x;
					float drawH = drawW / aspectRatio;
					if (drawH > avail.y) {
						drawH = avail.y;
						drawW = drawH * aspectRatio;
					}
					// Center in available space
					float offsetX = (avail.x - drawW) * 0.5f;
					float offsetY = (avail.y - drawH) * 0.5f;
					if (offsetX > 0) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offsetX);
					if (offsetY > 0) ImGui::SetCursorPosY(ImGui::GetCursorPosY() + offsetY);
					ImGui::Image((ImTextureID)(intptr_t)emuTex, ImVec2(drawW, drawH));
				}
			}
		} else {
			ImGui::TextDisabled("(no display)");
		}
	}
	ImGui::End();
	ImGui::PopStyleVar();
}

// =========================================================================
// Render all panes
// =========================================================================

// Apply default docking layout using ImGui DockBuilder.
// Replicates Windows ATLoadDefaultPaneLayout (console.cpp:921):
//   ┌──────────────────────┬────────────────────┐
//   │                      │  Registers         │
//   │    Display           │  Disassembly (tab) │
//   │                      │  History (tab)     │
//   ├──────────────────────┴────────────────────┤
//   │              Console (focused)             │
//   └───────────────────────────────────────────┘
static void ApplyDefaultDockLayout() {
	if (g_dockLayoutApplied)
		return;
	g_dockLayoutApplied = true;

	ImGuiID dockspace_id = ImGui::GetID("DebuggerDockSpace");

	// Only apply if the dockspace has no saved layout yet
	if (ImGui::DockBuilderGetNode(dockspace_id) != nullptr)
		return;

	ImGui::DockBuilderRemoveNode(dockspace_id);
	ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);

	ImGuiViewport *viewport = ImGui::GetMainViewport();
	ImGui::DockBuilderSetNodeSize(dockspace_id, viewport->WorkSize);

	// Split: bottom for Console (35% height), top for the rest
	ImGuiID dock_top, dock_bottom;
	ImGui::DockBuilderSplitNode(dockspace_id, ImGuiDir_Down, 0.35f, &dock_bottom, &dock_top);

	// Split top: left for Display, right for Registers/Disassembly/History
	ImGuiID dock_left, dock_right;
	ImGui::DockBuilderSplitNode(dock_top, ImGuiDir_Right, 0.45f, &dock_right, &dock_left);

	// Dock panes into layout
	ImGui::DockBuilderDockWindow("Display", dock_left);
	ImGui::DockBuilderDockWindow("Console", dock_bottom);
	ImGui::DockBuilderDockWindow("Registers", dock_right);
	ImGui::DockBuilderDockWindow("Disassembly", dock_right);
	ImGui::DockBuilderDockWindow("History", dock_right);

	ImGui::DockBuilderFinish(dockspace_id);
}

void ATUIDebuggerRenderPanes(ATSimulator &sim, ATUIState &state) {
	if (!g_debuggerOpen)
		return;

	// Create a full-window dockspace below the menu bar.
	// All debugger panes (including Display) dock into this space.
	ImGuiID dockspace_id = ImGui::GetID("DebuggerDockSpace");
	ImGuiViewport *viewport = ImGui::GetMainViewport();

	float menuBarHeight = ImGui::GetFrameHeight();
	ImVec2 dockPos(viewport->WorkPos.x, viewport->WorkPos.y + menuBarHeight);
	ImVec2 dockSize(viewport->WorkSize.x, viewport->WorkSize.y - menuBarHeight);

	ImGui::SetNextWindowPos(dockPos);
	ImGui::SetNextWindowSize(dockSize);
	ImGui::SetNextWindowViewport(viewport->ID);

	ImGuiWindowFlags hostFlags =
		ImGuiWindowFlags_NoTitleBar |
		ImGuiWindowFlags_NoCollapse |
		ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoBringToFrontOnFocus |
		ImGuiWindowFlags_NoNavFocus |
		ImGuiWindowFlags_NoBackground;

	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
	ImGui::Begin("DebuggerDockHost", nullptr, hostFlags);
	ImGui::PopStyleVar(3);

	ImGui::DockSpace(dockspace_id, ImVec2(0, 0), ImGuiDockNodeFlags_None);

	ImGui::End();

	// Apply default layout on first open
	ApplyDefaultDockLayout();

	// Render the Display pane (emulation texture inside a dockable window)
	RenderDisplayPane();

	// Render all debugger panes
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
	g_dockLayoutApplied = false;	// reset so layout is applied on next render
	dbg->SetEnabled(true);

	// Match Windows: do NOT break on open.  The console input is greyed
	// out while the simulator is running.  The user can break manually
	// with F5 (Run/Break) or the menu.

	// Register all existing panes as debugger clients before showing
	// the banner (which generates console output).
	for (auto& e : g_debugPanes) {
		dbg->AddClient(e.pane, true);
	}

	// Create default panes (matches Windows ATLoadDefaultPaneLayout:
	// Console + Registers + Disassembly + History).
	ATUIDebuggerEnsureConsolePane();
	ATUIDebuggerEnsureRegistersPane();
	ATUIDebuggerEnsureDisassemblyPane();
	ATUIDebuggerEnsureHistoryPane();

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
