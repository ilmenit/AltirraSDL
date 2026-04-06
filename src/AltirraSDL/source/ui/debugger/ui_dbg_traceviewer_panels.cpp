//	AltirraSDL - Dear ImGui Performance Analyzer (Trace Viewer)
//	Bottom panel dispatcher. Each tab lives in its own file:
//	  ui_dbg_traceviewer_history_panel.cpp  (CPU History)
//	  ui_dbg_traceviewer_profile_panel.cpp  (CPU Profile)
//	  ui_dbg_traceviewer_log_panel.cpp      (Log)

#include <stdafx.h>
#include <imgui.h>
#include "ui_dbg_traceviewer.h"
#include "traceviewer_panels_internal.h"

namespace at_traceviewer_panels {

namespace {
EnableCPUHistoryFn s_enableCPUHistoryFn = nullptr;
} // namespace

void SetEnableCPUHistoryCallback(EnableCPUHistoryFn fn) {
	s_enableCPUHistoryFn = fn;
}

EnableCPUHistoryFn GetEnableCPUHistoryCallback() {
	return s_enableCPUHistoryFn;
}

} // namespace at_traceviewer_panels

// =========================================================================
// Public API (declared in ui_dbg_traceviewer.h)
// =========================================================================

void ATImGuiTraceViewer_SetEnableCPUHistoryCallback(ATImGuiTraceViewerEnableCPUHistoryFn fn) {
	at_traceviewer_panels::SetEnableCPUHistoryCallback(
		reinterpret_cast<at_traceviewer_panels::EnableCPUHistoryFn>(fn));
}

void ATImGuiTraceViewer_ResetPanelState() {
	at_traceviewer_panels::ResetCPUHistoryState();
	at_traceviewer_panels::ResetCPUProfileState();
	at_traceviewer_panels::ResetLogState();
}

void ATImGuiTraceViewer_RenderPanels(ATImGuiTraceViewerContext& ctx) {
	if (ImGui::BeginTabBar("##TraceViewerTabs")) {
		if (ImGui::BeginTabItem("CPU History")) {
			at_traceviewer_panels::RenderCPUHistory(ctx);
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("CPU Profile")) {
			at_traceviewer_panels::RenderCPUProfile(ctx);
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Log")) {
			at_traceviewer_panels::RenderLog(ctx);
			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}
}
