//	AltirraSDL - Dear ImGui debugger pane manager
//	Provides the pane registry and rendering infrastructure that replaces
//	the Win32 ATUIPane / ATFrameWindow / ATContainerWindow docking system.

#pragma once

#include <vd2/system/vdtypes.h>
#include <vd2/system/refcount.h>
#include <vd2/system/VDString.h>
#include <vd2/system/function.h>
#include <imgui.h>
#include "console.h"
#include "debugger.h"

// Forward
class ATSimulator;
struct ATUIState;

// ---------------------------------------------------------------------------
// ATImGuiDebuggerPane — base class for all ImGui debugger panes.
// Each pane implements IATDebuggerClient to receive state updates.
// ---------------------------------------------------------------------------

class ATImGuiDebuggerPane : public vdrefcounted<IVDRefCount>, public IATDebuggerClient {
public:
	ATImGuiDebuggerPane(uint32 paneId, const char *title);
	virtual ~ATImGuiDebuggerPane();

	uint32 GetPaneId() const { return mPaneId; }
	const char *GetTitle() const { return mTitle.c_str(); }

	bool IsVisible() const { return mbVisible; }
	void SetVisible(bool vis) { mbVisible = vis; }

	// Focus request — set by ATActivateUIPane, consumed on next render
	void RequestFocus() { mbFocusRequested = true; }

	// Called each frame to render the pane.  Returns false if the pane
	// was closed via the X button.
	virtual bool Render() = 0;

	// IATDebuggerClient
	void OnDebuggerSystemStateUpdate(const ATDebuggerSystemState& state) override;
	void OnDebuggerEvent(ATDebugEvent eventId) override;

protected:
	uint32 mPaneId;
	VDStringA mTitle;
	bool mbVisible = true;
	bool mbFocusRequested = false;

	// Cached state from last OnDebuggerSystemStateUpdate
	ATDebuggerSystemState mLastState {};
	bool mbStateValid = false;
};

// ---------------------------------------------------------------------------
// Pane manager — singleton that tracks all open debugger panes.
// ---------------------------------------------------------------------------

void ATUIDebuggerInit();
void ATUIDebuggerShutdown();

// Render all open debugger panes.  Called from ATUIRenderFrame().
void ATUIDebuggerRenderPanes(ATSimulator &sim, ATUIState &state);

// Tick the debugger engine (call ATDebugger::Tick).
// Called from the main loop after ATUIPollDeferredActions().
void ATUIDebuggerTick();

// Open/close the debugger (matches Debug > Enable Debugger toggle)
void ATUIDebuggerOpen();
void ATUIDebuggerClose();
bool ATUIDebuggerIsOpen();

// Pane registration
void ATUIDebuggerRegisterPane(ATImGuiDebuggerPane *pane);
void ATUIDebuggerUnregisterPane(uint32 paneId);
ATImGuiDebuggerPane *ATUIDebuggerGetPane(uint32 paneId);

// Debug stepping commands (for menu/shortcut wiring)
void ATUIDebuggerRunStop();
void ATUIDebuggerBreak();
void ATUIDebuggerStepInto();
void ATUIDebuggerStepOver();
void ATUIDebuggerStepOut();
