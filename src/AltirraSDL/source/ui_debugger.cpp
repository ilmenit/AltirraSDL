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
#include "display_backend.h"
#include "ui_textselection.h"
#include "logging.h"

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
	uint32 g_focusedPaneId = 0;

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
extern void ATUIDebuggerEnsureWatchPane(int index);
extern void ATUIDebuggerEnsureBreakpointsPane();
extern void ATUIDebuggerEnsureCallStackPane();
extern void ATUIDebuggerEnsureTargetsPane();
extern void ATUIDebuggerEnsureDebugDisplayPane();
extern void ATUIDebuggerEnsurePrinterOutputPane();
extern void ATUIDebuggerEnsureTraceViewerPane();

static void EnsurePaneExists(uint32 id) {
	switch (id) {
		case kATUIPaneId_Console:     ATUIDebuggerEnsureConsolePane(); break;
		case kATUIPaneId_Registers:   ATUIDebuggerEnsureRegistersPane(); break;
		case kATUIPaneId_Disassembly: ATUIDebuggerEnsureDisassemblyPane(); break;
		case kATUIPaneId_History:     ATUIDebuggerEnsureHistoryPane(); break;
		case kATUIPaneId_Breakpoints: ATUIDebuggerEnsureBreakpointsPane(); break;
		case kATUIPaneId_CallStack:   ATUIDebuggerEnsureCallStackPane(); break;
		case kATUIPaneId_Targets:     ATUIDebuggerEnsureTargetsPane(); break;
		case kATUIPaneId_DebugDisplay: ATUIDebuggerEnsureDebugDisplayPane(); break;
		case kATUIPaneId_PrinterOutput: ATUIDebuggerEnsurePrinterOutputPane(); break;
		case kATUIPaneId_Profiler:   ATUIDebuggerEnsureTraceViewerPane(); break;
		default:
			// Memory pane instances: kATUIPaneId_MemoryN + 0..3
			if (id >= kATUIPaneId_MemoryN && id <= kATUIPaneId_MemoryN + 3) {
				ATUIDebuggerEnsureMemoryPane(id - kATUIPaneId_MemoryN);
			} else if (id >= kATUIPaneId_WatchN && id <= kATUIPaneId_WatchN + 3) {
				ATUIDebuggerEnsureWatchPane(id - kATUIPaneId_WatchN);
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

extern void ATUIDebuggerClearSourceWindows();

void ATUIDebuggerShutdown() {
	IATDebugger *dbg = ATGetDebugger();
	for (auto& e : g_debugPanes) {
		if (dbg)
			dbg->RemoveClient(e.pane);
	}
	g_debugPanes.clear();
	g_paneCreators.clear();
	g_debuggerOpen = false;
	ATUIDebuggerClearSourceWindows();
}

// =========================================================================
// Display pane — renders emulation texture inside an ImGui dockable window
// =========================================================================

static void RenderDisplayPane() {
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
	bool open = true;
	if (ImGui::Begin("Display", &open)) {
		IDisplayBackend *backend = ATUIGetDisplayBackend();
		void *texID = backend && backend->HasTexture() ? backend->GetImGuiTextureID() : nullptr;
		if (texID) {
			ImVec2 avail = ImGui::GetContentRegionAvail();
			if (avail.x > 0 && avail.y > 0) {
				// Maintain aspect ratio (Atari ~1.2:1 pixel aspect)
				float texW = (float)backend->GetTextureWidth();
				float texH = (float)backend->GetTextureHeight();
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

					// Record image position before drawing
					ImVec2 imagePos = ImGui::GetCursorScreenPos();
					ImVec2 imageSize(drawW, drawH);

					ImGui::Image((ImTextureID)texID, imageSize);

					// Text selection: handle mouse drag and draw highlight overlay.
					// Always call when a drag is active (mouse may leave window);
					// otherwise only when the Display window is hovered.
					ATTextSelectionState& sel = ATUIGetTextSelection();
					if (sel.mbDragActive || ImGui::IsWindowHovered())
						ATUITextSelectionHandleMouse(imagePos, imageSize);
					ATUITextSelectionDrawOverlay(imagePos, imageSize);
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
// Extends Windows ATLoadDefaultPaneLayout (console.cpp:921) with additional
// panes (Memory, Watch, Call Stack) in a resolution-independent layout:
//   ┌───────────┬──────────┬────────┬──────────────┐
//   │           │          │ Regs   │              │
//   │  Display  │ Memory 1 ├────────┤ Disassembly  │
//   │           │          │ CStk   │              │
//   │           ├──────────┤────────┤              │
//   │           │ Watch 1  │ Hist   │              │
//   ├───────────┴──────────┴────────┴──────────────┤
//   │                    Console                    │
//   └──────────────────────────────────────────────┘
static void ApplyDefaultDockLayout() {
	if (g_dockLayoutApplied)
		return;
	g_dockLayoutApplied = true;

	ImGuiID dockspace_id = ImGui::GetID("DebuggerDockSpace");

	// Only apply if the dockspace has no saved layout yet.
	// DockSpace() auto-creates an empty node, so check for children
	// to distinguish "has real layout" from "just auto-created".
	ImGuiDockNode *node = ImGui::DockBuilderGetNode(dockspace_id);
	LOG_INFO("DockLayout", "dockspace_id=0x%08X node=%p children=%p",
		dockspace_id, (void*)node,
		node ? (void*)node->ChildNodes[0] : nullptr);
	if (node != nullptr && node->ChildNodes[0] != nullptr) {
		LOG_INFO("DockLayout", "Skipping — saved layout exists");
		return;
	}

	LOG_INFO("DockLayout", "Applying default layout");
	ImGui::DockBuilderRemoveNode(dockspace_id);
	ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);

	// Use the host window's actual size (viewport minus menu bar)
	ImVec2 hostSize = ImGui::GetWindowSize();
	ImGui::DockBuilderSetNodeSize(dockspace_id, hostSize);

	// Split: bottom for Console (~19% height), top for the rest
	ImGuiID dock_top, dock_bottom;
	ImGui::DockBuilderSplitNode(dockspace_id, ImGuiDir_Down, 0.19f, &dock_bottom, &dock_top);

	// Split top: Disassembly on the far right (~24% width)
	ImGuiID dock_top_left, dock_disasm;
	ImGui::DockBuilderSplitNode(dock_top, ImGuiDir_Right, 0.24f, &dock_disasm, &dock_top_left);

	// Split remaining: Display on the left (~39% of total width)
	// dock_top_left is ~76% of total; Display should be ~39/76 ≈ 51%
	ImGuiID dock_display, dock_middle;
	ImGui::DockBuilderSplitNode(dock_top_left, ImGuiDir_Left, 0.52f, &dock_display, &dock_middle);

	// Split middle into: left column (Memory/Watch) and right column (Regs/CallStack/History)
	// Memory+Watch ~26% of total, Regs column ~10.4% of total
	// In this remaining space: Regs = 10.4/(26+10.4) ≈ 29%
	ImGuiID dock_mem_watch, dock_regs_col;
	ImGui::DockBuilderSplitNode(dock_middle, ImGuiDir_Right, 0.29f, &dock_regs_col, &dock_mem_watch);

	// Split Memory/Watch column vertically: Memory ~71%, Watch ~29%
	ImGuiID dock_memory, dock_watch;
	ImGui::DockBuilderSplitNode(dock_mem_watch, ImGuiDir_Down, 0.29f, &dock_watch, &dock_memory);

	// Split Registers column vertically into 3: Registers ~27%, Call Stack ~23%, History ~50%
	ImGuiID dock_regs_top, dock_history;
	ImGui::DockBuilderSplitNode(dock_regs_col, ImGuiDir_Down, 0.50f, &dock_history, &dock_regs_top);
	ImGuiID dock_registers, dock_callstack;
	ImGui::DockBuilderSplitNode(dock_regs_top, ImGuiDir_Down, 0.46f, &dock_callstack, &dock_registers);

	LOG_INFO("DockLayout", "hostSize=%.0fx%.0f", hostSize.x, hostSize.y);
	LOG_INFO("DockLayout", "dock IDs: display=0x%08X memory=0x%08X watch=0x%08X regs=0x%08X callstack=0x%08X history=0x%08X disasm=0x%08X bottom=0x%08X",
		dock_display, dock_memory, dock_watch, dock_registers, dock_callstack, dock_history, dock_disasm, dock_bottom);

	// Dock panes into layout
	ImGui::DockBuilderDockWindow("Display", dock_display);
	ImGui::DockBuilderDockWindow("Console", dock_bottom);
	ImGui::DockBuilderDockWindow("Memory 1", dock_memory);
	ImGui::DockBuilderDockWindow("Watch 1", dock_watch);
	ImGui::DockBuilderDockWindow("Registers", dock_registers);
	ImGui::DockBuilderDockWindow("Call Stack", dock_callstack);
	ImGui::DockBuilderDockWindow("History", dock_history);
	ImGui::DockBuilderDockWindow("Disassembly", dock_disasm);

	ImGui::DockBuilderFinish(dockspace_id);
	LOG_INFO("DockLayout", "Finished applying default layout");
}

void ATUIDebuggerRenderPanes(ATSimulator &sim, ATUIState &state) {
	if (!g_debuggerOpen)
		return;

	// Create a full-window dockspace below the menu bar.
	// All debugger panes (including Display) dock into this space.
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

	// GetID must be called inside DebuggerDockHost's Begin/End so the
	// ID stack is consistent between ApplyDefaultDockLayout and DockSpace.
	ImGuiID dockspace_id = ImGui::GetID("DebuggerDockSpace");

	// Apply default layout BEFORE DockSpace() — DockBuilder must configure
	// the node tree before the dockspace is submitted for this frame.
	ApplyDefaultDockLayout();

	ImGui::DockSpace(dockspace_id, ImVec2(0, 0), ImGuiDockNodeFlags_None);

	ImGui::End();

	// Render the Display pane (emulation texture inside a dockable window)
	RenderDisplayPane();

	// Render all debugger panes and track which one has focus
	g_focusedPaneId = 0;
	for (auto& e : g_debugPanes) {
		if (!e.pane->IsVisible())
			continue;

		if (!e.pane->Render()) {
			// Pane closed via X button
			e.pane->SetVisible(false);
		}

		if (e.pane->HasFocus())
			g_focusedPaneId = e.id;
	}
}

// =========================================================================
// Tick
// =========================================================================

void ATUIDebuggerTick() {
	IATDebugger *dbg = ATGetDebugger();
	if (!dbg)
		return;

	// Process all pending commands, not just one.  Windows does this via
	// the idle loop (Tick returns true → loop again immediately).  We
	// replicate by looping here, with a safety limit to avoid blocking
	// the UI if a command generates infinite sub-commands.
	//
	// No IsEnabled() guard — Windows calls Tick() unconditionally, and
	// --debugcmd queues commands before the debugger UI is opened.
	// Tick() is a no-op when no commands are queued.
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

	// Create default panes — extends Windows ATLoadDefaultPaneLayout
	// with Memory 1, Watch 1, and Call Stack for a richer default.
	ATUIDebuggerEnsureConsolePane();
	ATUIDebuggerEnsureRegistersPane();
	ATUIDebuggerEnsureDisassemblyPane();
	ATUIDebuggerEnsureHistoryPane();
	ATUIDebuggerEnsureMemoryPane(0);
	ATUIDebuggerEnsureWatchPane(0);
	ATUIDebuggerEnsureCallStackPane();

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
// Focus management
// =========================================================================

uint32 ATUIDebuggerGetFocusedPaneId() {
	return g_focusedPaneId;
}

void ATUIDebuggerFocusConsole() {
	ATActivateUIPane(kATUIPaneId_Console, true, true);
}

// =========================================================================
// Source mode detection — matches Windows ATUIGetDebugSrcMode()
// =========================================================================

static ATDebugSrcMode GetDebugSrcMode() {
	uint32 focusId = g_focusedPaneId;

	if (focusId == kATUIPaneId_Disassembly)
		return kATDebugSrcMode_Disasm;

	if (focusId >= kATUIPaneId_Source)
		return kATDebugSrcMode_Source;

	return kATDebugSrcMode_Same;
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
		dbg->Run(GetDebugSrcMode());
	}
}

void ATUIDebuggerBreak() {
	IATDebugger *dbg = ATGetDebugger();
	if (dbg && dbg->IsEnabled())
		dbg->Break();
}

void ATUIDebuggerToggleBreakpoint() {
	IATDebugger *dbg = ATGetDebugger();
	if (!dbg || !dbg->IsEnabled())
		return;

	// Toggle breakpoint at the current frame PC — matches Windows
	// OnCommandDebugToggleBreakpoint which delegates to the active pane.
	uint32 pc = dbg->GetFramePC();
	dbg->ToggleBreakpoint(pc);
}

void ATUIDebuggerStepInto() {
	IATDebugger *dbg = ATGetDebugger();
	if (!dbg || !dbg->IsEnabled()) return;

	ATDebugSrcMode srcMode = GetDebugSrcMode();

	// If source pane is focused, delegate to source-level stepping
	if (srcMode == kATDebugSrcMode_Source) {
		if (ATUIDebuggerSourceStepInto())
			return;
	}

	try {
		dbg->StepInto(srcMode);
	} catch(const MyError& e) {
		ATConsolePrintf("%s\n", e.c_str());
	}
}

void ATUIDebuggerStepOver() {
	IATDebugger *dbg = ATGetDebugger();
	if (!dbg || !dbg->IsEnabled()) return;

	ATDebugSrcMode srcMode = GetDebugSrcMode();

	// If source pane is focused, delegate to source-level stepping
	if (srcMode == kATDebugSrcMode_Source) {
		if (ATUIDebuggerSourceStepOver())
			return;
	}

	try {
		dbg->StepOver(srcMode);
	} catch(const MyError& e) {
		ATConsolePrintf("%s\n", e.c_str());
	}
}

void ATUIDebuggerStepOut() {
	IATDebugger *dbg = ATGetDebugger();
	if (!dbg || !dbg->IsEnabled()) return;
	try {
		dbg->StepOut(GetDebugSrcMode());
	} catch(const MyError& e) {
		ATConsolePrintf("%s\n", e.c_str());
	}
}

void ATUIDebuggerFocusDisplay() {
	// Focus the Display pane in the debugger dockspace
	ImGui::SetWindowFocus("Display");
}

// =========================================================================
// Window management — Close / Undock / Cycle panes
// =========================================================================

void ATUIDebuggerClosePaneById(uint32 paneId) {
	for (auto& e : g_debugPanes) {
		if (e.id == paneId) {
			e.pane->SetVisible(false);
			return;
		}
	}
}

bool ATUIDebuggerHasVisiblePanes() {
	for (auto& e : g_debugPanes) {
		if (e.pane->IsVisible())
			return true;
	}
	return false;
}

void ATUIDebuggerCloseActivePane() {
	if (!g_debuggerOpen || g_focusedPaneId == 0)
		return;

	// Find and close the focused pane
	for (auto& e : g_debugPanes) {
		if (e.id == g_focusedPaneId) {
			e.pane->SetVisible(false);
			g_focusedPaneId = 0;
			return;
		}
	}
}

void ATUIDebuggerUndockActivePane() {
	if (!g_debuggerOpen || g_focusedPaneId == 0)
		return;

	// Find the focused pane and undock it from the docking layout
	for (auto& e : g_debugPanes) {
		if (e.id == g_focusedPaneId) {
			ImGuiWindow *window = ImGui::FindWindowByName(e.pane->GetTitle());
			if (window && window->DockId != 0) {
				// Setting DockId to 0 undocks the window on the next frame
				ImGui::SetWindowDock(window, 0, ImGuiCond_Always);
			}
			return;
		}
	}
}

void ATUIDebuggerCyclePane(int direction) {
	if (!g_debuggerOpen)
		return;

	// Build ordered list of visible pane indices
	std::vector<size_t> visibleIndices;
	size_t currentIdx = SIZE_MAX;

	for (size_t i = 0; i < g_debugPanes.size(); ++i) {
		if (g_debugPanes[i].pane->IsVisible()) {
			if (g_debugPanes[i].id == g_focusedPaneId)
				currentIdx = visibleIndices.size();
			visibleIndices.push_back(i);
		}
	}

	if (visibleIndices.empty())
		return;

	// If no pane is focused, start from the first one
	if (currentIdx == SIZE_MAX)
		currentIdx = 0;

	// Advance with wraparound
	int n = (int)visibleIndices.size();
	int next = ((int)currentIdx + direction + n) % n;

	auto& target = g_debugPanes[visibleIndices[next]];
	ImGui::SetWindowFocus(target.pane->GetTitle());
	g_focusedPaneId = target.id;
}
