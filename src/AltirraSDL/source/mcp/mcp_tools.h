//	AltirraSDL - embedded MCP server: tool catalogue
//
//	Each MCP tool is a typed wrapper that validates JSON arguments,
//	builds AltirraBridge command tokens and runs them through
//	ATBridge::Execute() on the MCP server's own client context, then
//	shapes the bridge's JSON reply into an MCP CallToolResult.

#pragma once

#include <cstdint>
#include <string>

class ATSimulator;
struct VDJSONValueRef;

namespace ATBridge {
	struct ClientContext;
}

namespace ATMCP {

struct ToolEnv {
	ATSimulator& sim;
	ATBridge::ClientContext& ctx;
};

struct ToolOutcome {
	// True when the call armed the frame gate and must wait for it to
	// release before completing (advance_frames, bridge_command FRAME).
	bool pending = false;
	// With pending: true waits for a CYCLES/SCANLINES step or BOOT_BARE
	// (ATBridge::PollAsync) instead of the frame gate.
	bool async = false;
	uint32_t pendingFrames = 0;
	uint32_t timeoutMs = 0;

	// CallToolResult JSON object, valid when !pending.
	std::string result;
};

// JSON array of every tool definition, in fixed order.
const std::string& GetToolsListJson();

// True if the named tool exists.
bool IsKnownTool(const std::string& name);

// True if the tool changes emulator state (blocked during Online Play).
bool IsWriteTool(const std::string& name);

// True if this particular call changes emulator state. Multi-action tools
// (symbols, profiler) only mutate for some actions.
bool IsWriteCall(const std::string& name, const VDJSONValueRef& args);

ToolOutcome CallTool(const std::string& name, const VDJSONValueRef& args, ToolEnv& env);

// CallToolResult for a completed frame-gated call.
std::string BuildFramesCompletedResult(ToolEnv& env, uint32_t frames);

// CallToolResult for a completed step / bare boot (reply from PollAsync).
std::string BuildAsyncCompletedResult(ToolEnv& env, const std::string& reply);

// Resources: JSON arrays for resources/list and resources/templates/list,
// and a reader that fills `contents` with one ResourceContents object.
enum class ResourceReadStatus { Ok, NotFound, Failed };
const std::string& GetResourcesListJson();
const std::string& GetResourceTemplatesListJson();
ResourceReadStatus ReadResource(const std::string& uri, ToolEnv& env,
	std::string& contents, std::string& error);

// Resource notifications (subscriptions/listen, resources/subscribe).
bool IsKnownResourceUri(const std::string& uri);

// What stopped emulation in the debugger, for altirra://debugger/last-stop
// and breakpoint notifications. Built on the main thread once stopped.
enum class StopAccess { None, Read, Write };
std::string BuildStopReport(ToolEnv& env, const char *reason, StopAccess access, uint32_t seq);
void SetLastStopJson(const std::string& json);

// CallToolResult with isError set and a text message.
std::string ToolErrorResult(const std::string& message);

// Natural-language usage notes returned by initialize / server/discover.
const char *GetServerInstructions();

}  // namespace ATMCP
