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

struct InputState;

// Select which client context's InputState the input commands (JOY,
// CONSOL, KEYRAW) and the lifecycle functions below operate on. Called
// by Execute() before every command; nullptr selects a private default.
void SelectInputState(InputState *state);

// Register the selected context and allocate its PIA input slot for
// joystick injection. Idempotent.
void InitWriteCommands(ATSimulator& sim);

// Release the selected context's input, free its PIA input slot and
// unregister it. Idempotent.
void ShutdownWriteCommands(ATSimulator& sim);

// Release every input the selected context has acquired: joystick directions
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
// TYPE text — push one keystroke per character via
// ATUIGetDefaultScanCodeForCharacter(). Response includes "typed"
// (count) and "skipped" (characters with no key mapping).
std::string CmdType(ATSimulator& sim, const std::vector<std::string>& tokens);
std::string CmdKeyRaw(ATSimulator& sim, const std::vector<std::string>& tokens);
std::string CmdConsol(ATSimulator& sim, const std::vector<std::string>& tokens);

std::string CmdBoot(ATSimulator& sim, const std::vector<std::string>& tokens);
// BOOT_BARE defers its reply until PollBareBoot returns a response.
std::string CmdBootBare(ATSimulator& sim, const std::vector<std::string>& tokens);
bool IsBareBootGateActive();
std::string PollBareBoot(ATSimulator& sim);
void CancelBareBoot();
std::string CmdMount(ATSimulator& sim, const std::vector<std::string>& tokens);
std::string CmdEject(ATSimulator& sim, const std::vector<std::string>& tokens);
std::string CmdCartEject(ATSimulator& sim, const std::vector<std::string>& tokens);

std::string CmdColdReset(ATSimulator& sim, const std::vector<std::string>& tokens);
std::string CmdWarmReset(ATSimulator& sim, const std::vector<std::string>& tokens);

// FRESH — single-shot "factory reset" of the simulator. Clears every
// piece of session-scope state the bridge knows about and cold-resets
// the machine. See implementation for the precise `keep=` taxonomy.
std::string CmdFresh(ATSimulator& sim, const std::vector<std::string>& tokens);

std::string CmdStateSave(ATSimulator& sim, const std::vector<std::string>& tokens);
std::string CmdStateLoad(ATSimulator& sim, const std::vector<std::string>& tokens);
std::string CmdStateList(ATSimulator& sim, const std::vector<std::string>& tokens);
std::string CmdStateDrop(ATSimulator& sim, const std::vector<std::string>& tokens);

std::string CmdConfig(ATSimulator& sim, const std::vector<std::string>& tokens);

std::string CmdDeviceList(ATSimulator& sim, const std::vector<std::string>& tokens);
std::string CmdDeviceGet(ATSimulator& sim, const std::vector<std::string>& tokens);
std::string CmdDeviceAdd(ATSimulator& sim, const std::vector<std::string>& tokens);
std::string CmdDeviceSet(ATSimulator& sim, const std::vector<std::string>& tokens);
std::string CmdDeviceRemove(ATSimulator& sim, const std::vector<std::string>& tokens);
std::string CmdDeviceClear(ATSimulator& sim, const std::vector<std::string>& tokens);

}  // namespace ATBridge
