//	AltirraSDL - embedded MCP (Model Context Protocol) server
//	See mcp_server.h and docs/mcp-server-design.md.
//
//	Protocol: dual-era Streamable HTTP.
//	  - Modern (2026-07-28): stateless; each request carries its protocol
//	    version in params._meta and the MCP-Protocol-Version header, plus
//	    Mcp-Method / Mcp-Name headers that must match the body.
//	  - Legacy (2025-11-25, 2025-06-18, 2025-03-26): initialize handshake
//	    and an Mcp-Session-Id session header.

#include <stdafx.h>

#include "mcp_server.h"
#include "mcp_http.h"
#include "mcp_json.h"
#include "mcp_tools.h"

#include "bridge_context.h"
#include "logging.h"
#include "simulator.h"
#include "simeventmanager.h"
#include "versioninfo.h"

#ifdef ALTIRRA_NETPLAY_ENABLED
#include "netplay/netplay_glue.h"
#endif

#include <vd2/vdjson/jsonreader.h>
#include <vd2/vdjson/jsonvalue.h>

#include <chrono>
#include <cstdio>
#include <deque>
#include <map>
#include <random>
#include <set>
#include <string>
#include <vector>

namespace {

using namespace ATMCP;

constexpr const char *kModernVersion = "2026-07-28";
constexpr const char *kLegacyVersions[] = { "2025-11-25", "2025-06-18", "2025-03-26" };
constexpr const char *kMetaProtocolVersion = "io.modelcontextprotocol/protocolVersion";

// JSON-RPC / MCP error codes.
constexpr int kErrParse = -32700;
constexpr int kErrInvalidRequest = -32600;
constexpr int kErrMethodNotFound = -32601;
constexpr int kErrInvalidParams = -32602;
constexpr int kErrInternal = -32603;
constexpr int kErrResourceNotFound = -32002;
constexpr int kErrHeaderMismatch = -32020;
constexpr int kErrUnsupportedVersion = -32022;

constexpr size_t kMaxSessions = 32;
constexpr uint64_t kSessionIdleMs = 60ull * 60 * 1000;

enum class Era { Modern, Legacy };

struct Session {
	std::string version;
	uint64_t lastUsedMs = 0;
	std::set<std::string> subscriptions;    // resources/subscribe URIs
	int logLevel = 0;                       // logging/setLevel (0 = debug: everything)
};

// A long-lived notification stream: a 2026-07-28 subscriptions/listen
// request, or a legacy session's standalone GET stream.
struct Listener {
	uint32_t connId = 0;
	Era era = Era::Modern;
	std::string subscriptionIdJson;         // modern: the listen request's id
	std::set<std::string> uris;             // modern: acknowledged resourceSubscriptions
	std::string sessionId;                  // legacy: owning session
	bool started = false;                   // stream headers + acknowledgment sent
	uint64_t lastSentMs = 0;
};

// Syslog-style MCP logging levels, lowest first.
constexpr const char *kLogLevels[] = {
	"debug", "info", "notice", "warning", "error", "critical", "alert", "emergency"
};
constexpr int kLogLevelNotice = 2;
constexpr uint64_t kKeepAliveMs = 15000;

int LogLevelIndex(const std::string& name) {
	for (int i = 0; i < 8; ++i) {
		if (name == kLogLevels[i])
			return i;
	}
	return -1;
}

struct PendingCall {
	enum class Kind {
		Deferred,       // waiting for the frame gate before executing
		FrameWait,      // executed; waiting for its frames to run
		AsyncWait       // executed; waiting for a step or BOOT_BARE
	};

	Kind kind = Kind::Deferred;
	uint32_t connId = 0;
	Era era = Era::Modern;
	std::string idJson;
	std::string sessionId;      // legacy session the request came from
	std::string toolName;
	std::string argsJson;       // re-parsed when a Deferred call runs
	uint32_t frames = 0;
	uint64_t deadlineMs = 0;

	// Progress reporting: requested by a params._meta.progressToken on a
	// client that accepts text/event-stream.
	std::string progressTokenJson;
	bool acceptsEventStream = false;
	bool streaming = false;
	uint32_t lastProgress = 0;
	uint64_t lastProgressMs = 0;
};

// Identity and transport details of one tools/call request.
struct CallInfo {
	uint32_t connId = 0;
	Era era = Era::Modern;
	std::string idJson;
	std::string sessionId;
	std::string progressTokenJson;
	bool acceptsEventStream = false;
};

// Progress notifications are throttled to this interval (and only sent
// when the frame count moved), so a long run produces a steady trickle
// rather than one event per frame.
constexpr uint64_t kProgressIntervalMs = 250;

struct ServerState {
	HttpServer http;
	ATBridge::ClientContext ctx { "mcp" };

	bool applyPending = false;
	ATMCPServer::Config config;     // wanted configuration (SetConfig)
	bool requireToken = false;      // in effect
	std::string token;

	ATMCPServer::Status status;
	uint16_t boundPort = 0;
	bool loggedBindFailure = false;

	std::map<std::string, Session> sessions;
	std::deque<PendingCall> pending;
	std::vector<Listener> listeners;

	// Simulator events seen since the last poll (raised inside Advance()).
	ATSimulatorEvent stopEvent = kATSimEvent_None;
	bool statusChanged = false;
	uint32_t stopSeq = 0;
	bool simCallbackAttached = false;
};

ServerState g_mcp;

void RemoveListenersFor(uint32_t connId);

// Records debugger stops and state changes for the notification streams.
// Called on the main thread from inside ATSimulator::Advance(); the work
// happens later in Poll(), once the emulator has stopped.
class SimEventSink final : public IATSimulatorCallback {
public:
	void OnSimulatorEvent(ATSimulatorEvent ev) override {
		switch (ev) {
			case kATSimEvent_CPUPCBreakpoint:
			case kATSimEvent_CPUStackBreakpoint:
			case kATSimEvent_ReadBreakpoint:
			case kATSimEvent_WriteBreakpoint:
			case kATSimEvent_DiskSectorBreakpoint:
			case kATSimEvent_ScanlineBreakpoint:
			case kATSimEvent_CPUIllegalInsn:
			case kATSimEvent_CPUNewPath:
			case kATSimEvent_VerifierFailure:
				g_mcp.stopEvent = ev;
				g_mcp.statusChanged = true;
				break;

			case kATSimEvent_SimPause:
			case kATSimEvent_SimResume:
			case kATSimEvent_ColdReset:
			case kATSimEvent_WarmReset:
			case kATSimEvent_StateLoaded:
			case kATSimEvent_EXELoad:
				g_mcp.statusChanged = true;
				break;

			default:
				break;
		}
	}
} g_simEventSink;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

uint64_t NowMs() {
	return (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::steady_clock::now().time_since_epoch()).count();
}

bool IsLegacyVersion(const std::string& v) {
	for (const char *s : kLegacyVersions) {
		if (v == s)
			return true;
	}
	return false;
}

std::string SupportedVersionsJson() {
	std::string out = "[";
	out += JsonStr(kModernVersion);
	for (const char *s : kLegacyVersions) {
		out += ',';
		out += JsonStr(s);
	}
	return out + "]";
}

std::string ServerInfoJson() {
	return JsonObject()
		.Str("name", "AltirraSDL")
		.Str("title", "AltirraSDL Atari 8-bit emulator")
		.Str("version", AT_VERSION)
		.Build();
}

std::string RpcResult(const std::string& idJson, const std::string& result) {
	return JsonObject()
		.Str("jsonrpc", "2.0")
		.Raw("id", idJson)
		.Raw("result", result)
		.Build();
}

std::string RpcError(const std::string& idJson, int code, const std::string& message,
	const std::string& dataJson = std::string())
{
	JsonObject err;
	err.Int("code", code).Str("message", message);
	if (!dataJson.empty())
		err.Raw("data", dataJson);

	JsonObject o;
	o.Str("jsonrpc", "2.0");
	if (!idJson.empty())
		o.Raw("id", idJson);
	o.Raw("error", err.Build());
	return o.Build();
}

// Modern results carry resultType "complete" (input_required is never
// used: the server makes no requests of the client).
std::string ShapeResult(Era era, const std::string& result) {
	if (era != Era::Modern)
		return result;
	if (result == "{}")
		return "{\"resultType\":\"complete\"}";
	return "{\"resultType\":\"complete\"," + result.substr(1);
}

HttpResponse JsonResponse(int status, const std::string& body) {
	HttpResponse r;
	r.status = status;
	r.contentType = "application/json";
	r.body = body;
	return r;
}

HttpResponse TextResponse(int status, const char *text) {
	HttpResponse r;
	r.status = status;
	r.contentType = "text/plain; charset=utf-8";
	r.body = text;
	r.body += "\n";
	return r;
}

std::string NewSessionId() {
	std::random_device rd;
	char buf[33];
	std::snprintf(buf, sizeof buf, "%08x%08x%08x%08x",
		(unsigned)rd(), (unsigned)rd(), (unsigned)rd(), (unsigned)rd());
	return buf;
}

bool ConstantTimeEquals(const std::string& a, const std::string& b) {
	if (a.size() != b.size())
		return false;
	unsigned char diff = 0;
	for (size_t i = 0; i < a.size(); ++i)
		diff |= (unsigned char)(a[i] ^ b[i]);
	return diff == 0;
}

std::string ToLower(std::string s) {
	for (char& c : s) {
		if (c >= 'A' && c <= 'Z')
			c = (char)(c + 32);
	}
	return s;
}

// Host: 127.0.0.1:<port>, localhost:<port> or [::1]:<port> (a missing
// port is accepted only when it would be the default, i.e. never here).
bool IsAllowedHost(const std::string& hostHeader) {
	const std::string host = ToLower(hostHeader);
	const std::string port = ":" + std::to_string(g_mcp.boundPort);
	return host == "127.0.0.1" + port || host == "localhost" + port || host == "[::1]" + port;
}

bool IsAllowedOrigin(const std::string& originHeader) {
	const std::string origin = ToLower(originHeader);
	const std::string port = ":" + std::to_string(g_mcp.boundPort);
	return origin == "http://127.0.0.1" + port
		|| origin == "http://localhost" + port
		|| origin == "http://[::1]" + port;
}

const char *StateText(ATMCPServer::State s) {
	switch (s) {
		case ATMCPServer::State::Listening: return "listening";
		case ATMCPServer::State::PortInUse: return "port in use";
		case ATMCPServer::State::Error:     return "error";
		default:                            return "stopped";
	}
}

void ExpireSessions(uint64_t now) {
	for (auto it = g_mcp.sessions.begin(); it != g_mcp.sessions.end();) {
		if (now - it->second.lastUsedMs > kSessionIdleMs)
			it = g_mcp.sessions.erase(it);
		else
			++it;
	}
}

// ---------------------------------------------------------------------------
// Tool execution and the frame gate
// ---------------------------------------------------------------------------

ATSimulator *g_pSim = nullptr;   // valid only during Poll()

bool IsGateBusy() {
	return g_mcp.ctx.framesRemaining > 0;
}

void CancelFrameGate(ATSimulator& sim) {
	if (g_mcp.ctx.framesRemaining > 0) {
		g_mcp.ctx.framesRemaining = 0;
		sim.Pause();
	}
}

HttpResponse ToolResponse(Era era, const std::string& idJson, const std::string& result) {
	return JsonResponse(200, RpcResult(idJson, ShapeResult(era, result)));
}

// Run a tool call now. Returns true with the response filled in, or
// false when the call armed the frame gate and was queued as FrameWait.
bool ExecuteToolCall(const CallInfo& info, const std::string& name,
	const VDJSONValueRef& args, HttpResponse& out)
{
	ATSimulator& sim = *g_pSim;
	const Era era = info.era;
	const std::string& idJson = info.idJson;

#ifdef ALTIRRA_NETPLAY_ENABLED
	// Changing the machine mid-match would desync the peers.
	if (IsWriteCall(name, args) && ATNetplayGlue::IsSessionEngaged()) {
		out = ToolResponse(era, idJson, ToolErrorResult(
			"An Online Play session is in progress; '" + name
			+ "' is disabled until it ends."));
		return true;
	}
#endif

	ToolEnv env { sim, g_mcp.ctx };
	ToolOutcome outcome = CallTool(name, args, env);

	if (IsWriteCall(name, args))
		LOG_INFO("MCP", "tool call: %s", name.c_str());

	if (!outcome.pending) {
		out = ToolResponse(era, idJson, outcome.result);
		return true;
	}

	PendingCall call;
	call.kind = outcome.async ? PendingCall::Kind::AsyncWait : PendingCall::Kind::FrameWait;
	call.connId = info.connId;
	call.era = era;
	call.idJson = idJson;
	call.sessionId = info.sessionId;
	call.toolName = name;
	call.frames = outcome.pendingFrames;
	call.deadlineMs = NowMs() + outcome.timeoutMs;
	call.progressTokenJson = info.progressTokenJson;
	call.acceptsEventStream = info.acceptsEventStream;
	call.lastProgressMs = NowMs();
	g_mcp.pending.push_front(call);
	return false;
}

bool HandleToolCall(const CallInfo& info, const VDJSONValueRef& params, HttpResponse& out)
{
	const std::string& idJson = info.idJson;
	const VDJSONValueRef nameVal = Member(params, "name");
	if (!nameVal.IsString()) {
		out = JsonResponse(200, RpcError(idJson, kErrInvalidParams, "tools/call requires a string 'name'"));
		return true;
	}

	const std::string name = ValueToU8(nameVal);
	if (!IsKnownTool(name)) {
		out = JsonResponse(200, RpcError(idJson, kErrInvalidParams, "Unknown tool: " + name));
		return true;
	}

	const VDJSONValueRef args = Member(params, "arguments");

	// Calls run strictly in order behind an active frame gate, so a
	// read issued after advance_frames sees the machine after the frames
	// have run (the bridge's synchronisation contract).
	if (IsGateBusy() || !g_mcp.pending.empty()) {
		PendingCall call;
		call.kind = PendingCall::Kind::Deferred;
		call.connId = info.connId;
		call.era = info.era;
		call.idJson = idJson;
		call.sessionId = info.sessionId;
		call.toolName = name;
		call.argsJson = args.IsNull() ? std::string() : SerializeValue(args);
		call.progressTokenJson = info.progressTokenJson;
		call.acceptsEventStream = info.acceptsEventStream;
		g_mcp.pending.push_back(call);
		return false;
	}

	return ExecuteToolCall(info, name, args, out);
}

// Deliver the final JSON-RPC message for a waiting call: as the last
// event of its SSE stream if one was started, else as a plain response.
void CompleteCall(const PendingCall& call, const std::string& result) {
	const std::string body = RpcResult(call.idJson, ShapeResult(call.era, result));
	if (call.streaming)
		g_mcp.http.FinishEventStream(call.connId, body);
	else
		g_mcp.http.CompleteParked(call.connId, JsonResponse(200, body));
}

// Send a notifications/progress event for a frame run when it is due.
void ReportProgress(PendingCall& call, uint64_t now) {
	if (call.progressTokenJson.empty() || !call.acceptsEventStream)
		return;

	const uint32_t remaining = g_mcp.ctx.framesRemaining;
	const uint32_t done = remaining < call.frames ? call.frames - remaining : 0;
	if (done == call.lastProgress || now - call.lastProgressMs < kProgressIntervalMs)
		return;

	if (!call.streaming) {
		if (!g_mcp.http.StartEventStream(call.connId))
			return;
		call.streaming = true;
	}

	char message[64];
	std::snprintf(message, sizeof message, "Ran %u of %u frames",
		(unsigned)done, (unsigned)call.frames);

	const std::string params = JsonObject()
		.Raw("progressToken", call.progressTokenJson)
		.Int("progress", done)
		.Int("total", call.frames)
		.Str("message", message)
		.Build();

	g_mcp.http.SendEvent(call.connId, JsonObject()
		.Str("jsonrpc", "2.0")
		.Str("method", "notifications/progress")
		.Raw("params", params)
		.Build());

	call.lastProgress = done;
	call.lastProgressMs = now;
}

// Stop a waiting call the client cancelled: end its frame run (paused,
// injected input released) and drop the request without a response.
void CancelPendingCall(const PendingCall& call) {
	if (call.kind == PendingCall::Kind::FrameWait && g_pSim) {
		CancelFrameGate(*g_pSim);
		ATBridge::ReleaseContext(g_mcp.ctx, *g_pSim);
	} else if (call.kind == PendingCall::Kind::AsyncWait && g_pSim) {
		ATBridge::CancelAsync(g_mcp.ctx, *g_pSim);
		g_pSim->Pause();
		ATBridge::ReleaseContext(g_mcp.ctx, *g_pSim);
	}
	g_mcp.http.AbortParked(call.connId);
}

void ServicePending(uint64_t now) {
	ATSimulator& sim = *g_pSim;

	while (!g_mcp.pending.empty()) {
		PendingCall& front = g_mcp.pending.front();

		if (front.kind == PendingCall::Kind::AsyncWait) {
			std::string reply;
			if (ATBridge::PollAsync(g_mcp.ctx, sim, reply)) {
				ToolEnv env { sim, g_mcp.ctx };
				const PendingCall call = front;
				g_mcp.pending.pop_front();
				CompleteCall(call, BuildAsyncCompletedResult(env, reply));
				continue;
			}

			if (now >= front.deadlineMs) {
				ATBridge::CancelAsync(g_mcp.ctx, sim);
				sim.Pause();
				const PendingCall call = front;
				g_mcp.pending.pop_front();
				CompleteCall(call, ToolErrorResult("Timed out; the step or bare boot was cancelled "
					"and the emulator has been paused."));
				continue;
			}

			return;
		}

		if (front.kind == PendingCall::Kind::FrameWait) {
			if (g_mcp.ctx.framesRemaining == 0) {
				ToolEnv env { sim, g_mcp.ctx };
				const PendingCall call = front;
				g_mcp.pending.pop_front();
				CompleteCall(call, BuildFramesCompletedResult(env, call.frames));
				continue;
			}

			if (now >= front.deadlineMs) {
				const uint32_t remaining = g_mcp.ctx.framesRemaining;
				CancelFrameGate(sim);
				char msg[160];
				std::snprintf(msg, sizeof msg,
					"Timed out with %u of %u frames still to run; the emulator has been paused. "
					"Is the emulator running at normal speed?",
					(unsigned)remaining, (unsigned)front.frames);
				const PendingCall call = front;
				g_mcp.pending.pop_front();
				CompleteCall(call, ToolErrorResult(msg));
				continue;
			}

			ReportProgress(front, now);
			return;
		}

		// Deferred: run it now that the gate is clear.
		PendingCall call = front;
		g_mcp.pending.pop_front();

		VDJSONDocument doc;
		VDJSONReader reader;
		VDJSONValueRef args(nullptr, &VDJSONValue::null);
		if (!call.argsJson.empty() && reader.Parse(call.argsJson.data(), call.argsJson.size(), doc))
			args = doc.Root();

		CallInfo info;
		info.connId = call.connId;
		info.era = call.era;
		info.idJson = call.idJson;
		info.sessionId = call.sessionId;
		info.progressTokenJson = call.progressTokenJson;
		info.acceptsEventStream = call.acceptsEventStream;

		HttpResponse resp;
		if (ExecuteToolCall(info, call.toolName, args, resp))
			g_mcp.http.CompleteParked(call.connId, resp);
		// else: it became the FrameWait at the front; loop re-checks it.
	}
}

void OnParkedConnectionClosed(uint32_t connId) {
	// A notification stream's client went away (that is how a listen
	// subscription is cancelled over HTTP).
	RemoveListenersFor(connId);

	// The client went away: closing the response is the cancellation
	// signal (2026-07-28). Drop its queued calls, and stop a frame run it
	// was waiting on.
	for (auto it = g_mcp.pending.begin(); it != g_mcp.pending.end();) {
		if (it->connId != connId) {
			++it;
			continue;
		}

		if (it->kind == PendingCall::Kind::FrameWait && g_pSim) {
			CancelFrameGate(*g_pSim);
			ATBridge::ReleaseContext(g_mcp.ctx, *g_pSim);
		} else if (it->kind == PendingCall::Kind::AsyncWait && g_pSim) {
			ATBridge::CancelAsync(g_mcp.ctx, *g_pSim);
			g_pSim->Pause();
			ATBridge::ReleaseContext(g_mcp.ctx, *g_pSim);
		}
		it = g_mcp.pending.erase(it);
	}
}

void FailAllPending(const char *reason) {
	for (const PendingCall& call : g_mcp.pending)
		CompleteCall(call, ToolErrorResult(reason));
	g_mcp.pending.clear();
}

// ---------------------------------------------------------------------------
// Notification streams
// ---------------------------------------------------------------------------

void RemoveListenersFor(uint32_t connId) {
	for (auto it = g_mcp.listeners.begin(); it != g_mcp.listeners.end();) {
		if (it->connId == connId)
			it = g_mcp.listeners.erase(it);
		else
			++it;
	}
}

bool ListenerWantsUri(const Listener& l, const std::string& uri) {
	if (l.era == Era::Modern)
		return l.uris.count(uri) != 0;

	auto it = g_mcp.sessions.find(l.sessionId);
	return it != g_mcp.sessions.end() && it->second.subscriptions.count(uri) != 0;
}

std::string ModernMeta(const Listener& l) {
	return JsonObject().Raw("io.modelcontextprotocol/subscriptionId", l.subscriptionIdJson).Build();
}

void SendToListener(Listener& l, const std::string& message, uint64_t now) {
	if (!g_mcp.http.SendEvent(l.connId, message))
		l.connId = 0;   // gone; reaped by the caller
	l.lastSentMs = now;
}

void ReapDeadListeners() {
	for (auto it = g_mcp.listeners.begin(); it != g_mcp.listeners.end();) {
		if (!it->connId) {
			it = g_mcp.listeners.erase(it);
		} else {
			++it;
		}
	}
}

void NotifyResourceUpdated(const std::string& uri, uint64_t now) {
	for (Listener& l : g_mcp.listeners) {
		if (!l.started || !l.connId || !ListenerWantsUri(l, uri))
			continue;

		JsonObject params;
		if (l.era == Era::Modern)
			params.Raw("_meta", ModernMeta(l));
		params.Str("uri", uri);

		SendToListener(l, JsonObject()
			.Str("jsonrpc", "2.0")
			.Str("method", "notifications/resources/updated")
			.Raw("params", params.Build())
			.Build(), now);
	}
}

// notifications/message for legacy sessions (2026-07-28 deprecates logging
// and keeps it request-scoped, so it never goes on a modern listen stream).
void NotifyLog(int level, const char *logger, const std::string& dataJson, uint64_t now) {
	for (Listener& l : g_mcp.listeners) {
		if (!l.started || !l.connId || l.era != Era::Legacy)
			continue;

		auto it = g_mcp.sessions.find(l.sessionId);
		if (it == g_mcp.sessions.end() || level < it->second.logLevel)
			continue;

		SendToListener(l, JsonObject()
			.Str("jsonrpc", "2.0")
			.Str("method", "notifications/message")
			.Raw("params", JsonObject()
				.Str("level", kLogLevels[level])
				.Str("logger", logger)
				.Raw("data", dataJson)
				.Build())
			.Build(), now);
	}
}

const char *StopReason(ATSimulatorEvent ev, StopAccess& access) {
	access = StopAccess::None;
	switch (ev) {
		case kATSimEvent_CPUPCBreakpoint:      return "breakpoint";
		case kATSimEvent_ReadBreakpoint:       access = StopAccess::Read;  return "read breakpoint";
		case kATSimEvent_WriteBreakpoint:      access = StopAccess::Write; return "write breakpoint";
		case kATSimEvent_CPUStackBreakpoint:   return "stack breakpoint";
		case kATSimEvent_DiskSectorBreakpoint: return "disk sector breakpoint";
		case kATSimEvent_ScanlineBreakpoint:   return "scanline breakpoint";
		case kATSimEvent_CPUIllegalInsn:       return "illegal instruction";
		case kATSimEvent_CPUNewPath:           return "new code path";
		case kATSimEvent_VerifierFailure:      return "verifier failure";
		default:                               return "stopped";
	}
}

void ServiceListeners(uint64_t now) {
	ATSimulator& sim = *g_pSim;

	// Open streams whose request was parked during this poll.
	for (Listener& l : g_mcp.listeners) {
		if (l.started || !l.connId)
			continue;

		if (!g_mcp.http.StartEventStream(l.connId)) {
			l.connId = 0;
			continue;
		}
		l.started = true;
		l.lastSentMs = now;

		if (l.era == Era::Modern) {
			std::string uris = "[";
			for (const std::string& u : l.uris) {
				if (uris.size() > 1)
					uris += ',';
				uris += JsonStr(u);
			}
			uris += "]";

			SendToListener(l, JsonObject()
				.Str("jsonrpc", "2.0")
				.Str("method", "notifications/subscriptions/acknowledged")
				.Raw("params", JsonObject()
					.Raw("_meta", ModernMeta(l))
					.Raw("notifications", JsonObject().Raw("resourceSubscriptions", uris).Build())
					.Build())
				.Build(), now);
		} else {
			// Legacy streams carry no initial message; a comment flushes the
			// headers to the client right away.
			if (!g_mcp.http.SendKeepAlive(l.connId))
				l.connId = 0;
		}
	}

	// Debugger stop: refresh altirra://debugger/last-stop and tell everyone.
	if (g_mcp.stopEvent != kATSimEvent_None) {
		StopAccess access;
		const char *reason = StopReason(g_mcp.stopEvent, access);
		g_mcp.stopEvent = kATSimEvent_None;

		ToolEnv env { sim, g_mcp.ctx };
		const std::string report = BuildStopReport(env, reason, access, ++g_mcp.stopSeq);
		SetLastStopJson(report);
		LOG_INFO("MCP", "debugger stop #%u: %s", (unsigned)g_mcp.stopSeq, reason);

		NotifyResourceUpdated("altirra://debugger/last-stop", now);
		NotifyResourceUpdated("altirra://cpu", now);
		NotifyLog(kLogLevelNotice, "altirra.debugger", report, now);
	}

	if (g_mcp.statusChanged) {
		g_mcp.statusChanged = false;
		NotifyResourceUpdated("altirra://status", now);
	}

	// Keep idle streams alive through proxies and client idle timeouts.
	for (Listener& l : g_mcp.listeners) {
		if (l.started && l.connId && now - l.lastSentMs >= kKeepAliveMs) {
			if (!g_mcp.http.SendKeepAlive(l.connId))
				l.connId = 0;
			l.lastSentMs = now;
		}
	}

	ReapDeadListeners();
}

void CloseAllListeners() {
	for (Listener& l : g_mcp.listeners) {
		if (!l.connId)
			continue;

		if (l.started && l.era == Era::Modern) {
			// Graceful end of the subscription: the listen request's result.
			g_mcp.http.FinishEventStream(l.connId, RpcResult(l.subscriptionIdJson,
				JsonObject()
					.Str("resultType", "complete")
					.Raw("_meta", ModernMeta(l))
					.Build()));
		} else {
			g_mcp.http.AbortParked(l.connId);
		}
	}
	g_mcp.listeners.clear();
}

// ---------------------------------------------------------------------------
// Request handling
// ---------------------------------------------------------------------------

HttpResponse HeaderMismatch(const std::string& idJson, const std::string& message) {
	return JsonResponse(400, RpcError(idJson, kErrHeaderMismatch, "Header mismatch: " + message));
}

bool HandleRpc(uint32_t connId, const HttpRequest& req, HttpResponse& out) {
	VDJSONDocument doc;
	VDJSONReader reader;
	if (!reader.Parse(req.body.data(), req.body.size(), doc)) {
		out = JsonResponse(400, RpcError("null", kErrParse, "Parse error"));
		return true;
	}

	const VDJSONValueRef root = doc.Root();
	if (!root.IsObject()) {
		out = JsonResponse(400, RpcError("null", kErrInvalidRequest, "Batch requests are not supported"));
		return true;
	}

	const VDJSONValueRef idVal = Member(root, "id");
	const bool isRequest = !idVal.IsNull();
	const std::string idJson = isRequest ? SerializeValue(idVal) : std::string("null");
	const std::string method = ValueToU8(Member(root, "method"));
	const VDJSONValueRef params = Member(root, "params");

	if (ValueToU8(Member(root, "jsonrpc")) != "2.0") {
		out = JsonResponse(400, RpcError(idJson, kErrInvalidRequest, "jsonrpc must be \"2.0\""));
		return true;
	}

	if (method.empty()) {
		// A JSON-RPC response from a legacy client: we never send requests,
		// so there is nothing to match it against. Accept and ignore.
		if (!Member(root, "result").IsNull() || !Member(root, "error").IsNull()) {
			HttpResponse r;
			r.status = 202;
			out = r;
			return true;
		}
		out = JsonResponse(400, RpcError(idJson, kErrInvalidRequest, "Missing method"));
		return true;
	}

	const std::string *sessionHdr = req.FindHeader("mcp-session-id");
	const std::string *versionHdr = req.FindHeader("mcp-protocol-version");
	const uint64_t now = NowMs();

	Era era;
	std::string legacyVersion;

	if (method == "initialize") {
		// Legacy handshake: negotiate a version and start a session.
		if (!isRequest) {
			out = JsonResponse(400, RpcError("null", kErrInvalidRequest, "initialize must be a request"));
			return true;
		}

		std::string requested = ValueToU8(Member(params, "protocolVersion"));
		const std::string version = IsLegacyVersion(requested) ? requested : std::string(kLegacyVersions[0]);

		ExpireSessions(now);
		if (g_mcp.sessions.size() >= kMaxSessions) {
			// Drop the least recently used session.
			auto oldest = g_mcp.sessions.begin();
			for (auto it = g_mcp.sessions.begin(); it != g_mcp.sessions.end(); ++it) {
				if (it->second.lastUsedMs < oldest->second.lastUsedMs)
					oldest = it;
			}
			g_mcp.sessions.erase(oldest);
		}

		const std::string sessionId = NewSessionId();
		g_mcp.sessions[sessionId] = Session { version, now };

		const std::string clientName = ValueToU8(Member(Member(params, "clientInfo"), "name"));
		LOG_INFO("MCP", "initialize from '%s' (protocol %s)", clientName.c_str(), version.c_str());

		const std::string result = JsonObject()
			.Str("protocolVersion", version)
			.Raw("capabilities", "{\"tools\":{\"listChanged\":false},\"resources\":{\"subscribe\":true,\"listChanged\":false},\"logging\":{}}")
			.Raw("serverInfo", ServerInfoJson())
			.Str("instructions", GetServerInstructions())
			.Build();

		out = JsonResponse(200, RpcResult(idJson, result));
		out.headers.emplace_back("Mcp-Session-Id", sessionId);
		return true;
	}

	if (sessionHdr) {
		auto it = g_mcp.sessions.find(*sessionHdr);
		if (it == g_mcp.sessions.end()) {
			out = JsonResponse(404, RpcError(idJson, kErrInvalidRequest, "Session not found; send initialize"));
			return true;
		}

		if (versionHdr && !IsLegacyVersion(*versionHdr)) {
			out = JsonResponse(400, RpcError(idJson, kErrInvalidRequest,
				"Unsupported MCP-Protocol-Version for this session: " + *versionHdr));
			return true;
		}

		it->second.lastUsedMs = now;
		legacyVersion = it->second.version;
		era = Era::Legacy;
	} else {
		const VDJSONValueRef meta = Member(params, "_meta");
		const VDJSONValueRef metaVersionVal = Member(meta, kMetaProtocolVersion);
		const std::string metaVersion = ValueToU8(metaVersionVal);

		if (!versionHdr) {
			if (!metaVersion.empty()) {
				out = HeaderMismatch(idJson, "missing MCP-Protocol-Version header");
				return true;
			}
			out = JsonResponse(400, RpcError(idJson, kErrInvalidRequest,
				"Missing MCP-Protocol-Version header. Clients using protocol 2025-11-25 or "
				"earlier must send initialize first; supported versions: 2026-07-28, "
				"2025-11-25, 2025-06-18, 2025-03-26."));
			return true;
		}

		if (*versionHdr == kModernVersion) {
			if (metaVersion != *versionHdr) {
				out = HeaderMismatch(idJson, "MCP-Protocol-Version header '" + *versionHdr
					+ "' does not match params._meta protocolVersion '" + metaVersion + "'");
				return true;
			}

			const std::string *methodHdr = req.FindHeader("mcp-method");
			if (!methodHdr || *methodHdr != method) {
				out = HeaderMismatch(idJson, methodHdr
					? "Mcp-Method header '" + *methodHdr + "' does not match body method '" + method + "'"
					: std::string("missing Mcp-Method header"));
				return true;
			}

			const bool namedByName = method == "tools/call" || method == "prompts/get";
			const bool namedByUri = method == "resources/read";
			if (namedByName || namedByUri) {
				const std::string bodyName = ValueToU8(Member(params, namedByName ? "name" : "uri"));
				const std::string *nameHdr = req.FindHeader("mcp-name");
				std::string decoded;
				if (!nameHdr) {
					out = HeaderMismatch(idJson, "missing Mcp-Name header");
					return true;
				}
				if (!DecodeHeaderSentinel(*nameHdr, decoded) || decoded != bodyName) {
					out = HeaderMismatch(idJson, "Mcp-Name header does not match the body value '" + bodyName + "'");
					return true;
				}
			}

			era = Era::Modern;
		} else if (IsLegacyVersion(*versionHdr)) {
			out = JsonResponse(400, RpcError(idJson, kErrInvalidRequest,
				"No session for protocol " + *versionHdr + "; send initialize first"));
			return true;
		} else {
			const std::string data = JsonObject()
				.Raw("supported", SupportedVersionsJson())
				.Str("requested", *versionHdr)
				.Build();
			out = JsonResponse(400, RpcError(idJson, kErrUnsupportedVersion,
				"Unsupported protocol version", data));
			return true;
		}
	}

	// Notifications (notifications/initialized, notifications/cancelled,
	// ...) need no reply.
	if (!isRequest) {
		// notifications/cancelled names a request this client sent
		// earlier (2025-era clients; 2026-07-28 cancels by closing the
		// stream). Only a call from the same session can be cancelled.
		if (method == "notifications/cancelled") {
			const std::string target = SerializeValue(Member(params, "requestId"));
			const std::string session = sessionHdr ? *sessionHdr : std::string();
			for (auto it = g_mcp.pending.begin(); it != g_mcp.pending.end(); ++it) {
				if (it->idJson == target && it->sessionId == session) {
					LOG_INFO("MCP", "request %s cancelled by client", target.c_str());
					const PendingCall call = *it;
					g_mcp.pending.erase(it);
					CancelPendingCall(call);
					break;
				}
			}
		}

		HttpResponse r;
		r.status = 202;
		out = r;
		return true;
	}

	if (method == "ping") {
		out = JsonResponse(200, RpcResult(idJson, ShapeResult(era, "{}")));
		return true;
	}

	if (method == "server/discover" && era == Era::Modern) {
		const std::string meta = JsonObject()
			.Raw("io.modelcontextprotocol/serverInfo", ServerInfoJson())
			.Build();
		const std::string result = JsonObject()
			.Raw("supportedVersions", SupportedVersionsJson())
			.Raw("capabilities", "{\"tools\":{},\"resources\":{\"subscribe\":true}}")
			.Raw("_meta", meta)
			.Str("instructions", GetServerInstructions())
			.Build();
		out = JsonResponse(200, RpcResult(idJson, ShapeResult(era, result)));
		return true;
	}

	if (method == "tools/list") {
		const std::string result = JsonObject()
			.Raw("tools", GetToolsListJson())
			.Build();
		out = JsonResponse(200, RpcResult(idJson, ShapeResult(era, result)));
		return true;
	}

	if (method == "subscriptions/listen" && era == Era::Modern) {
		Listener l;
		l.connId = connId;
		l.era = Era::Modern;
		l.subscriptionIdJson = idJson;

		// Only resource subscriptions are honoured: the tool, prompt and
		// resource lists never change, so list_changed is not offered.
		const VDJSONValueRef subs = Member(Member(params, "notifications"), "resourceSubscriptions");
		for (const VDJSONValueRef& u : subs.AsArray()) {
			const std::string uri = ValueToU8(u);
			if (IsKnownResourceUri(uri))
				l.uris.insert(uri);
		}

		g_mcp.listeners.push_back(l);
		return false;   // parked; ServiceListeners() opens the stream
	}

	if (era == Era::Legacy && (method == "resources/subscribe" || method == "resources/unsubscribe")) {
		const std::string uri = ValueToU8(Member(params, "uri"));
		if (!IsKnownResourceUri(uri)) {
			out = JsonResponse(200, RpcError(idJson, kErrResourceNotFound, "Resource not found",
				JsonObject().Str("uri", uri).Build()));
			return true;
		}

		Session& session = g_mcp.sessions[*sessionHdr];
		if (method == "resources/subscribe")
			session.subscriptions.insert(uri);
		else
			session.subscriptions.erase(uri);
		out = JsonResponse(200, RpcResult(idJson, "{}"));
		return true;
	}

	if (era == Era::Legacy && method == "logging/setLevel") {
		const int level = LogLevelIndex(ValueToU8(Member(params, "level")));
		if (level < 0) {
			out = JsonResponse(200, RpcError(idJson, kErrInvalidParams, "Unknown logging level"));
			return true;
		}
		g_mcp.sessions[*sessionHdr].logLevel = level;
		out = JsonResponse(200, RpcResult(idJson, "{}"));
		return true;
	}

	if (method == "resources/list") {
		out = JsonResponse(200, RpcResult(idJson, ShapeResult(era,
			JsonObject().Raw("resources", GetResourcesListJson()).Build())));
		return true;
	}

	if (method == "resources/templates/list") {
		out = JsonResponse(200, RpcResult(idJson, ShapeResult(era,
			JsonObject().Raw("resourceTemplates", GetResourceTemplatesListJson()).Build())));
		return true;
	}

	if (method == "resources/read") {
		const VDJSONValueRef uriVal = Member(params, "uri");
		if (!uriVal.IsString()) {
			out = JsonResponse(200, RpcError(idJson, kErrInvalidParams, "resources/read requires a string 'uri'"));
			return true;
		}

		const std::string uri = ValueToU8(uriVal);
		ToolEnv env { *g_pSim, g_mcp.ctx };
		std::string contents, error;
		switch (ReadResource(uri, env, contents, error)) {
			case ResourceReadStatus::Ok:
				out = JsonResponse(200, RpcResult(idJson, ShapeResult(era,
					JsonObject().Raw("contents", "[" + contents + "]").Build())));
				break;

			case ResourceReadStatus::NotFound:
				out = JsonResponse(200, RpcError(idJson, kErrResourceNotFound, "Resource not found",
					JsonObject().Str("uri", uri).Build()));
				break;

			case ResourceReadStatus::Failed:
				out = JsonResponse(200, RpcError(idJson, kErrInternal, error));
				break;
		}
		return true;
	}

	if (method == "tools/call") {
		CallInfo info;
		info.connId = connId;
		info.era = era;
		info.idJson = idJson;
		info.sessionId = sessionHdr ? *sessionHdr : std::string();

		const VDJSONValueRef token = Member(Member(params, "_meta"), "progressToken");
		if (token.IsString() || token.IsInt())
			info.progressTokenJson = SerializeValue(token);

		const std::string *accept = req.FindHeader("accept");
		info.acceptsEventStream = accept && ToLower(*accept).find("text/event-stream") != std::string::npos;

		return HandleToolCall(info, params, out);
	}

	// Unknown method: modern servers answer 404 + -32601 so dual-era
	// clients can tell it apart from a legacy server's 404.
	out = JsonResponse(era == Era::Modern ? 404 : 200,
		RpcError(idJson, kErrMethodNotFound, "Method not found: " + method));
	(void)legacyVersion;
	return true;
}

bool HandleHttpRequest(uint32_t connId, const HttpRequest& req, HttpResponse& out) {
	// Single endpoint: /mcp (a query string is ignored).
	std::string path = req.target;
	const size_t q = path.find('?');
	if (q != std::string::npos)
		path.erase(q);
	if (path != "/mcp" && path != "/mcp/") {
		out = TextResponse(404, "Not found. The MCP endpoint is /mcp.");
		return true;
	}

	// DNS-rebinding protection (spec MUST): only loopback Host/Origin.
	const std::string *host = req.FindHeader("host");
	if (!host || req.CountHeader("host") != 1 || !IsAllowedHost(*host)) {
		out = TextResponse(403, "Forbidden: Host must be 127.0.0.1 or localhost with the server's port.");
		return true;
	}

	const std::string *origin = req.FindHeader("origin");
	if (origin && !IsAllowedOrigin(*origin)) {
		out = JsonResponse(403, RpcError(std::string(), kErrInvalidRequest, "Forbidden origin"));
		return true;
	}

	if (g_mcp.requireToken) {
		const std::string *auth = req.FindHeader("authorization");
		static const char kBearer[] = "bearer ";
		const size_t bl = sizeof kBearer - 1;
		const bool ok = auth && auth->size() > bl
			&& ToLower(auth->substr(0, bl)) == kBearer
			&& ConstantTimeEquals(auth->substr(bl), g_mcp.token);
		if (!ok) {
			out = TextResponse(401, "Unauthorized: this server requires an access token "
				"(Authorization: Bearer <token>). See Configure System > Emulator > MCP Server.");
			out.headers.emplace_back("WWW-Authenticate", "Bearer realm=\"AltirraSDL MCP\"");
			return true;
		}
	}

	if (req.method == "DELETE") {
		// Legacy session termination.
		const std::string *sessionHdr = req.FindHeader("mcp-session-id");
		if (sessionHdr && g_mcp.sessions.count(*sessionHdr)) {
			for (Listener& l : g_mcp.listeners) {
				if (l.era == Era::Legacy && l.sessionId == *sessionHdr && l.connId) {
					g_mcp.http.AbortParked(l.connId);
					l.connId = 0;
				}
			}
			ReapDeadListeners();
			g_mcp.sessions.erase(*sessionHdr);
			HttpResponse r;
			r.status = 204;
			out = r;
		} else if (sessionHdr) {
			out = TextResponse(404, "Session not found.");
		} else {
			out = TextResponse(405, "Method not allowed.");
			out.headers.emplace_back("Allow", "POST, DELETE");
		}
		return true;
	}

	if (req.method == "GET") {
		// Legacy (2025-era) standalone server-to-client stream for a
		// session: resources/updated and notifications/message arrive here.
		const std::string *sessionHdr = req.FindHeader("mcp-session-id");
		const std::string *accept = req.FindHeader("accept");
		if (sessionHdr && g_mcp.sessions.count(*sessionHdr)
			&& accept && ToLower(*accept).find("text/event-stream") != std::string::npos)
		{
			// One stream per session: a new GET replaces the old one.
			for (Listener& l : g_mcp.listeners) {
				if (l.era == Era::Legacy && l.sessionId == *sessionHdr && l.connId) {
					g_mcp.http.AbortParked(l.connId);
					l.connId = 0;
				}
			}
			ReapDeadListeners();

			Listener l;
			l.connId = connId;
			l.era = Era::Legacy;
			l.sessionId = *sessionHdr;
			g_mcp.listeners.push_back(l);
			g_mcp.sessions[*sessionHdr].lastUsedMs = NowMs();
			return false;   // parked; ServiceListeners() opens the stream
		}

		if (sessionHdr && !g_mcp.sessions.count(*sessionHdr)) {
			out = TextResponse(404, "Session not found.");
			return true;
		}
	}

	if (req.method != "POST") {
		// GET without a session (or without SSE in Accept) and everything else.
		out = TextResponse(405, "Method not allowed. Send JSON-RPC requests with POST.");
		out.headers.emplace_back("Allow", "POST, DELETE");
		return true;
	}

	const std::string *contentType = req.FindHeader("content-type");
	if (!contentType || ToLower(*contentType).compare(0, 16, "application/json") != 0) {
		out = TextResponse(415, "Content-Type must be application/json.");
		return true;
	}

	return HandleRpc(connId, req, out);
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void StopServer(ATSimulator& sim, const char *reason) {
	if (g_mcp.simCallbackAttached) {
		sim.GetEventManager()->RemoveCallback(&g_simEventSink);
		g_mcp.simCallbackAttached = false;
	}
	g_mcp.stopEvent = kATSimEvent_None;
	g_mcp.statusChanged = false;

	if (g_mcp.http.IsListening()) {
		FailAllPending(reason);
		CloseAllListeners();
		g_mcp.http.Close();
		LOG_INFO("MCP", "stopped (%s)", reason);
	}
	g_mcp.pending.clear();
	g_mcp.listeners.clear();
	g_mcp.sessions.clear();

	// Release the input MCP clients injected; stop a frame run in
	// flight so the emulator isn't left racing ahead unattended.
	CancelFrameGate(sim);
	ATBridge::CancelAsync(g_mcp.ctx, sim);
	ATBridge::ReleaseContext(g_mcp.ctx, sim);
	g_mcp.boundPort = 0;
}

void ApplySettings(ATSimulator& sim) {
	g_mcp.applyPending = false;
	const uint16_t wantPort = g_mcp.config.port;
	g_mcp.requireToken = g_mcp.config.requireToken;
	g_mcp.token = g_mcp.requireToken ? g_mcp.config.token : std::string();

	if (!g_mcp.config.enabled) {
		StopServer(sim, "disabled");
		g_mcp.status = ATMCPServer::Status();
		return;
	}

	if (g_mcp.http.IsListening() && g_mcp.boundPort == wantPort)
		return;

	StopServer(sim, "port changed");

	int osError = 0;
	const ListenResult lr = g_mcp.http.Listen(wantPort, osError);

	g_mcp.status.port = wantPort;
	g_mcp.status.osError = osError;

	switch (lr) {
		case ListenResult::Ok:
			g_mcp.boundPort = wantPort;
			g_mcp.status.state = ATMCPServer::State::Listening;
			ATBridge::AttachContext(g_mcp.ctx, sim);
			if (!g_mcp.simCallbackAttached) {
				sim.GetEventManager()->AddCallback(&g_simEventSink);
				g_mcp.simCallbackAttached = true;
			}
			LOG_INFO("MCP", "listening on http://127.0.0.1:%u/mcp%s", (unsigned)wantPort,
				g_mcp.requireToken ? " (access token required)" : "");
			break;

		case ListenResult::PortInUse:
			g_mcp.status.state = ATMCPServer::State::PortInUse;
			LOG_ERROR("MCP", "port %u is in use", (unsigned)wantPort);
			break;

		case ListenResult::Error:
			g_mcp.status.state = ATMCPServer::State::Error;
			LOG_ERROR("MCP", "could not listen on port %u (error %d)", (unsigned)wantPort, osError);
			break;
	}
}

}  // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

namespace ATMCPServer {

void SetConfig(const Config& config) {
	g_mcp.config = config;
	g_mcp.applyPending = true;
}

void Poll(ATSimulator& sim) {
	g_pSim = &sim;

	if (g_mcp.applyPending)
		ApplySettings(sim);

	if (g_mcp.http.IsListening()) {
		const uint64_t now = NowMs();
		g_mcp.http.Poll(now, HandleHttpRequest, OnParkedConnectionClosed);
		ServicePending(now);
		ServiceListeners(now);
	}

	g_pSim = nullptr;
}

void Shutdown(ATSimulator& sim) {
	g_pSim = &sim;
	StopServer(sim, "shutting down");
	ATBridge::DetachContext(g_mcp.ctx, sim);
	g_mcp.status = Status();
	g_pSim = nullptr;
}

Status GetStatus() {
	return g_mcp.status;
}

}  // namespace ATMCPServer
