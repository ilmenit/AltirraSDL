//	AltirraSDL - Font management public interface
//
//	Loaded by ui_main.cpp during ImGui initialization; the active UI and
//	monospace fonts are exposed as ImFont* pointers so other UI code can
//	push the mono font for debugger panes.

#pragma once

struct ImFont;
class ATSimulator;

// Discover fonts under <exe>/fonts, load registry settings, build the
// initial font atlas.  Call after ImGui::CreateContext() but BEFORE the
// renderer backend is initialised — the backend picks up the atlas on its
// first NewFrame.
void ATUIFontsInit(float contentScale, bool usingGLBackend);

// Called once per frame, BEFORE ImGui_Impl*_NewFrame().  If a font setting
// changed since the last call, tears down the backend texture, rebuilds the
// atlas from the new settings, and writes the choice back to the registry.
void ATUIFontsRebuildIfDirty();

// Active fonts (valid after ATUIFontsInit).  Mono may equal UI if no
// monospaced font is available.
ImFont *ATUIGetFontUI();
ImFont *ATUIGetFontMono();

// Configure System -> Emulator -> Fonts page.
void RenderFontsCategory(ATSimulator &sim);

// Direct accessors for the UI font point size.  Used by the First Time
// Setup wizard so the Appearance step can offer a font-size slider with
// live preview that shares state with the Configure System Fonts page.
int  ATUIFontsGetUISize();
void ATUIFontsSetUISize(int pt);
