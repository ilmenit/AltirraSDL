//	AltirraSDL - ui/core internal header
//	Shared between ui_main.cpp and the split-out help/about dialogs.

#ifndef f_AT_UI_MAIN_INTERNAL_H
#define f_AT_UI_MAIN_INTERNAL_H

struct ATUIState;

// Help-menu dialogs defined in ui_main_aboutdialogs.cpp.
void ATUIRenderCommandLineHelpDialog(ATUIState &state);
void ATUIRenderChangeLogDialog(ATUIState &state);
void ATUIRenderAboutDialog(ATUIState &state);

#endif
