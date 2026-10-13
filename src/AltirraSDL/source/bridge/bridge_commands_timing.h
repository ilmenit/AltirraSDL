// AltirraBridge - cycle stepping and POKEY bus-write capture.
#ifndef ALTIRRASDL_BRIDGE_COMMANDS_TIMING_H
#define ALTIRRASDL_BRIDGE_COMMANDS_TIMING_H
#include <string>
#include <vector>
class ATSimulator;
namespace ATBridge {
std::string CmdTiming(ATSimulator&, const std::vector<std::string>&);
bool IsTimingGateActive();
void UpdateTimingCommands(ATSimulator&);
void CleanupTimingCommands(ATSimulator&);
}
#endif
