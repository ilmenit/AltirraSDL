//	AltirraSDL - Trace viewer panel internal header
//	Shared between the three panel .cpp files and the panel dispatcher.
//	Each panel owns its own file-static state and exposes a Render*() +
//	Reset*() entry point so the dispatcher can coordinate them.

#ifndef f_AT_TRACEVIEWER_PANELS_INTERNAL_H
#define f_AT_TRACEVIEWER_PANELS_INTERNAL_H

struct ATImGuiTraceViewerContext;

namespace at_traceviewer_panels {

// CPU History panel (ui_dbg_traceviewer_history_panel.cpp)
void RenderCPUHistory(ATImGuiTraceViewerContext& ctx);
void ResetCPUHistoryState();

// CPU Profile panel (ui_dbg_traceviewer_profile_panel.cpp)
void RenderCPUProfile(ATImGuiTraceViewerContext& ctx);
void ResetCPUProfileState();

// Log panel (ui_dbg_traceviewer_log_panel.cpp)
void RenderLog(ATImGuiTraceViewerContext& ctx);
void ResetLogState();

// Set the "enable CPU history & start trace" callback. Shared with the
// history panel so the dispatcher can still register callbacks through
// the public ATImGuiTraceViewer_SetEnableCPUHistoryCallback entry point.
typedef void (*EnableCPUHistoryFn)();
void SetEnableCPUHistoryCallback(EnableCPUHistoryFn fn);
EnableCPUHistoryFn GetEnableCPUHistoryCallback();

} // namespace at_traceviewer_panels

#endif
