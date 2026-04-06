//	AltirraSDL - ui/core internal header
//	Shared between ui_main.cpp and the split-out help/about dialogs.

#ifndef f_AT_UI_MAIN_INTERNAL_H
#define f_AT_UI_MAIN_INTERNAL_H

#include <vd2/system/vdstl.h>
#include "compatdb.h"

struct ATUIState;
class ATSimulator;

// Help-menu dialogs defined in ui_main_aboutdialogs.cpp.
void ATUIRenderCommandLineHelpDialog(ATUIState &state);
void ATUIRenderChangeLogDialog(ATUIState &state);
void ATUIRenderAboutDialog(ATUIState &state);

// -------------------------------------------------------------------------
// Phase 3g: Compatibility warning state — definition lives in
// ui_main_compat.cpp; ui_main.cpp's deferred-action queue and frame
// renderer push/consume the pending flag.
// -------------------------------------------------------------------------

struct ATUICompatWarningState {
	const ATCompatDBTitle *pTitle = nullptr;
	vdfastvector<ATCompatKnownTag> tags;
	bool ignoreThistitle = false;
	bool ignoreAll = false;
};

extern bool g_compatCheckPending;
extern ATUICompatWarningState g_compatWarningState;

// Phase 3g: peeled-off render entry points (defined in
// ui_main_compat.cpp / ui_main_overlays.cpp).
void ATUIRenderCompatWarning(ATSimulator &sim, ATUIState &state);
void ATUIRenderExitConfirm(ATSimulator &sim, ATUIState &state);
void ATUIRenderDragDropOverlay();

#endif
