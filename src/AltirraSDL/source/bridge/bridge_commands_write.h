// AltirraBridge - Phase 3 state-write and input-injection commands
//
// Mirrors bridge_commands_state.h. The difference: these commands
// modify simulator state. Most are still safe to call without
// pausing — the bridge's frame gate gives the client deterministic
// timing if they want it.
//
// Several commands acquire input state (joystick directions, fire,
// console switches, key queue). The bridge owns the global "input
// release" path and must call CleanupInjectedInput() on disconnect.

#pragma once

#include <string>
#include <vector>
#include <cstdint>

class ATSimulator;

namespace ATBridge {

// One-time init: allocate the PIA input slot for joystick injection.
// Called from bridge_server.cpp Init() after the simulator exists.
// Idempotent.
void InitWriteCommands(ATSimulator& sim);

// Tear down PIA input slot. Called from bridge_server.cpp Shutdown().
// Idempotent.
void ShutdownWriteCommands(ATSimulator& sim);

// Release every input the bridge has acquired: joystick directions
// for all 4 ports → centred, fire buttons → released, console
// switches → released, POKEY key matrix → cleared. Called from
// bridge_server.cpp OnClientDisconnected().
void CleanupInjectedInput(ATSimulator& sim);

// --- Phase 3 commands ---

std::string CmdPoke(ATSimulator& sim, const std::vector<std::string>& tokens);
std::string CmdPoke16(ATSimulator& sim, const std::vector<std::string>& tokens);
std::string CmdHwPoke(ATSimulator& sim, const std::vector<std::string>& tokens);
std::string CmdMemDump(ATSimulator& sim, const std::vector<std::string>& tokens);
std::string CmdMemLoad(ATSimulator& sim, const std::vector<std::string>& tokens);

std::string CmdJoy(ATSimulator& sim, const std::vector<std::string>& tokens);
std::string CmdKey(ATSimulator& sim, const std::vector<std::string>& tokens);
std::string CmdConsol(ATSimulator& sim, const std::vector<std::string>& tokens);

std::string CmdBoot(ATSimulator& sim, const std::vector<std::string>& tokens);
std::string CmdBootBare(ATSimulator& sim, const std::vector<std::string>& tokens);
std::string CmdMount(ATSimulator& sim, const std::vector<std::string>& tokens);

std::string CmdColdReset(ATSimulator& sim, const std::vector<std::string>& tokens);
std::string CmdWarmReset(ATSimulator& sim, const std::vector<std::string>& tokens);

std::string CmdStateSave(ATSimulator& sim, const std::vector<std::string>& tokens);
std::string CmdStateLoad(ATSimulator& sim, const std::vector<std::string>& tokens);

}  // namespace ATBridge
