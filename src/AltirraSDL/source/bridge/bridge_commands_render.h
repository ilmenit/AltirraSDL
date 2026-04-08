// AltirraBridge - Phase 4 rendering commands
//
// SCREENSHOT  — PNG snapshot of the last GTIA frame (path or inline)
// RAWSCREEN   — raw XRGB8888 (little-endian) snapshot of the last frame
// RENDER_FRAME — alias for SCREENSHOT inline + dimensions (no state
//                override yet; reserved for a later phase)
//
// All commands read from ATGTIAEmulator::GetLastFrameBuffer, which
// returns the same pixmap the display pane reads. The headless
// AltirraBridgeServer target installs a null IVDVideoDisplay (see
// ui_stubs.cpp) because GTIA::BeginFrame bails out early when
// mpDisplay is null and never populates mpLastFrame. With a null
// display attached, every VBI stores a fresh frame we can capture.

#pragma once

#include <string>
#include <vector>

class ATSimulator;

namespace ATBridge {

std::string CmdScreenshot (ATSimulator& sim, const std::vector<std::string>& tokens);
std::string CmdRawScreen  (ATSimulator& sim, const std::vector<std::string>& tokens);
std::string CmdRenderFrame(ATSimulator& sim, const std::vector<std::string>& tokens);

}  // namespace ATBridge
