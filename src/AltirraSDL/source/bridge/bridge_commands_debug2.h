// AltirraBridge - Phase 5b debugger commands (breakpoints, watchpoints,
// symbols, memory search, profiler, verifier).
//
// All wrappers around existing public Altirra APIs
// (IATDebugger / IATDebuggerSymbolLookup / ATSimulator /
// ATCPUProfiler / ATCPUVerifier). Zero chip semantics are
// reimplemented here — the bridge is strictly a marshalling layer.
//
// Scope deliberately omits:
//   - tracepoint format strings      (complex token quoting — Phase 5c)
//   - SIO_TRACE                       (SetSIOTracingEnabled is private
//                                      to ATDebugger impl; would need
//                                      a QueueCommand dispatch)
//   - VERIFIER_REPORT violation log   (no public log-sink API — the
//                                      verifier only writes to the
//                                      debugger console)
//   - SYM_FIND substring search       (needs multi-module symbol-store
//                                      enumeration; deferred)

#pragma once

#include <string>
#include <vector>

class ATSimulator;

namespace ATBridge {

// Breakpoints (PC + optional condition)
std::string CmdBpSet      (ATSimulator& sim, const std::vector<std::string>& tokens);
std::string CmdBpClear    (ATSimulator& sim, const std::vector<std::string>& tokens);
std::string CmdBpClearAll (ATSimulator& sim, const std::vector<std::string>& tokens);
std::string CmdBpList     (ATSimulator& sim, const std::vector<std::string>& tokens);

// Access breakpoints (read/write watchpoints that halt execution)
std::string CmdWatchSet   (ATSimulator& sim, const std::vector<std::string>& tokens);

// Symbols
std::string CmdSymLoad      (ATSimulator& sim, const std::vector<std::string>& tokens);
std::string CmdSymResolve   (ATSimulator& sim, const std::vector<std::string>& tokens);
std::string CmdSymLookup    (ATSimulator& sim, const std::vector<std::string>& tokens);
std::string CmdSymUnload    (ATSimulator& sim, const std::vector<std::string>& tokens);
std::string CmdSymClearAll  (ATSimulator& sim, const std::vector<std::string>& tokens);

// Cross-module hook for FRESH — unloads every module the bridge has
// recorded via SYM_LOAD and clears the tracking list. Idempotent and
// safe when no symbols are loaded.
void BridgeSymClearAllInternal();

// Cross-module hook for FRESH — stop the profiler if running and drop
// any cached session. Idempotent.
void BridgeProfileResetInternal(ATSimulator& sim);

// Cross-module hook for FRESH — disable the verifier. Idempotent.
void BridgeVerifierResetInternal(ATSimulator& sim);

// Memory search
std::string CmdMemSearch  (ATSimulator& sim, const std::vector<std::string>& tokens);

// Profiler
std::string CmdProfileStart   (ATSimulator& sim, const std::vector<std::string>& tokens);
std::string CmdProfileStop    (ATSimulator& sim, const std::vector<std::string>& tokens);
std::string CmdProfileStatus  (ATSimulator& sim, const std::vector<std::string>& tokens);
std::string CmdProfileDump    (ATSimulator& sim, const std::vector<std::string>& tokens);
std::string CmdProfileDumpTree(ATSimulator& sim, const std::vector<std::string>& tokens);

// Verifier
std::string CmdVerifierStatus (ATSimulator& sim, const std::vector<std::string>& tokens);
std::string CmdVerifierSet    (ATSimulator& sim, const std::vector<std::string>& tokens);

}  // namespace ATBridge
