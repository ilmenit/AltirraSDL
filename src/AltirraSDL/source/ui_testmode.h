//	AltirraSDL - UI Test Automation Framework
//	Lightweight test harness using ImGui's IMGUI_ENABLE_TEST_ENGINE hooks.
//	Activated by --test-mode flag. Provides a Unix domain socket interface
//	for external agents (LLM, scripts) to interact with the UI.
//
//	Protocol: newline-delimited text commands in, single-line JSON out.
//	Socket path: /tmp/altirra-test-<pid>.sock

#pragma once

#include <imgui.h>

struct ATUIState;
class ATSimulator;

// Global enable flag — set by --test-mode command line argument.
// When false, all test mode code is skipped (zero overhead).
extern bool g_testModeEnabled;

// Lifecycle
bool ATTestModeInit();       // creates socket, enables hooks — call after ATUIInit()
void ATTestModeShutdown();   // destroys socket, cleans up — call before ATUIShutdown()

// Per-frame processing — call in main loop after HandleEvents(), before ATUIPollDeferredActions()
void ATTestModePollCommands(ATSimulator &sim, ATUIState &state);

// Call after ImGui::Render() to collect window/item data and process pending interactions
void ATTestModePostRender(ATSimulator &sim, ATUIState &state);

// ImGui Test Engine hook implementations.
// These are called automatically by ImGui when IMGUI_ENABLE_TEST_ENGINE is defined
// and g.TestEngineHookItems is true. They build the per-frame item registry.
//
// Declared extern in imgui_internal.h — we provide the definitions.
// Do NOT call these directly.
