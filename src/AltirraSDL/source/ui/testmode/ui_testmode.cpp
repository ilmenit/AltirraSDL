//	AltirraSDL - UI Test Automation Framework
//	Implementation of cross-platform IPC, ImGui test engine hooks,
//	item registry, and command dispatcher.

#include <stdafx.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
#include <deque>
#include <mutex>
#include <algorithm>
#include <map>
#include <vd2/system/error.h>
#include <vd2/system/text.h>
#include <at/atui/uicommandmanager.h>

#include "testmode_ipc.h"
#include "ui_testmode.h"
#include "ui_main.h"
#include "uiaccessors.h"
#include "ui_frame_capture.h"
#include "ui_mode.h"
#include "ui/mobile/ui_mobile.h"
#include "simulator.h"
#include "gtia.h"
#include "oshelper.h"
#include "inputmanager.h"
#include "inputdefs.h"
#include "ui/debugger/ui_debugger.h"
#include "netplay/netplay_input.h"
#include "logging.h"

bool g_testModeEnabled = false;
extern ATMobileUIState g_mobileState;
extern ATUICommandManager& ATUIGetCommandManager();
extern SDL_Window *g_pWindow;
extern void ATConsoleExecuteCommand(const char *s, bool echo);

// =========================================================================
// IPC transport (cross-platform wrapper)
// =========================================================================

static TestModeIPC g_ipc;
static std::string g_ipcAddress;   // socket path or pipe name (for display)
static std::string g_recvBuf;      // accumulates partial reads
static std::string g_sendBuf;      // accumulates responses to flush
static std::vector<std::string> g_scriptLines;
static size_t g_scriptLineNext = 0;
static FILE *g_scriptOutput = nullptr;
static bool g_hasMouseOverride = false;
static ImVec2 g_mouseOverride {};
static uint32 g_lastMessageBoxSeq = 0;
static std::string g_lastMessageBoxKind;
static std::string g_lastMessageBoxTitle;
static std::string g_lastMessageBoxMessage;

bool ATTestModeRecordMessageBox(const char *kind,
	const wchar_t *title,
	const wchar_t *message)
{
	if (!g_testModeEnabled)
		return false;

	++g_lastMessageBoxSeq;
	g_lastMessageBoxKind = kind ? kind : "";
	g_lastMessageBoxTitle = VDTextWToU8(VDStringW(title ? title : L"")).c_str();
	g_lastMessageBoxMessage = VDTextWToU8(VDStringW(message ? message : L"")).c_str();
	return true;
}

static void SendResponse(const std::string &json) {
	if (g_scriptOutput) {
		fputs(json.c_str(), g_scriptOutput);
		fputc('\n', g_scriptOutput);
		fflush(g_scriptOutput);
	}

	if (g_ipc.HasClient()) {
		g_sendBuf += json;
		g_sendBuf += '\n';
	}
}

// Forward declarations for cleanup (defined further down)
static void ResetClientState();

static void FlushSendBuffer() {
	if (!g_ipc.HasClient() || g_sendBuf.empty())
		return;

	int sent = g_ipc.Send(g_sendBuf.data(), g_sendBuf.size());
	if (sent < 0) {
		LOG_ERROR("TestMode", "Client disconnected (send error)");
		g_ipc.DisconnectClient();
		ResetClientState();
		return;
	}
	if (sent == 0)
		return;  // would block, try again next frame
	g_sendBuf.erase(0, (size_t)sent);
}

// =========================================================================
// Item Registry — populated by ImGui test engine hooks each frame
// =========================================================================

struct TestItem {
	ImGuiID id;
	ImRect rect;
	std::string label;
	std::string windowName;
	ImGuiItemStatusFlags flags;
	ImGuiItemFlags itemFlags;
};

static std::vector<TestItem> g_items;

// Track the current window name for item context.
// During hooks, we read it from ImGui's CurrentWindow.
static const char* GetCurrentWindowName(ImGuiContext *ctx) {
	if (ctx->CurrentWindow)
		return ctx->CurrentWindow->Name;
	return "";
}

// =========================================================================
// Pending interactions — queued by commands, executed via synthetic input
// =========================================================================

enum class PendingActionType {
	None,
	Click,          // click at a position (mouse down + up)
	MouseMove,      // move pointer to a fixed position
	KeyPress,       // key down + up
	WaitFrames,     // wait N frames before responding
};

struct PendingAction {
	PendingActionType type = PendingActionType::None;
	ImGuiID targetId = 0;           // item to click (looked up from registry)
	std::string targetWindow;       // window name filter
	std::string targetLabel;        // label filter
	ImVec2 clickPos = {0, 0};       // resolved screen position
	int mouseButton = 0;            // 0=left, 1=right, etc.
	ImGuiKey key = ImGuiKey_None;
	bool ctrl = false;
	bool shift = false;
	bool alt = false;
	int framesRemaining = 0;        // for WaitFrames
	bool posResolved = false;       // true once we found the item and set clickPos
	int clickPhase = 0;             // 0=move, 1=press, 2=release+done
};

static std::vector<PendingAction> g_pendingActions;
static bool g_commandsBlocked = false;  // true while wait_frames is pending
static std::deque<std::string> g_syntheticTextEvents;

// Reset all per-client state (called on disconnect from any path)
static void ResetClientState() {
	g_recvBuf.clear();
	g_sendBuf.clear();
	g_pendingActions.clear();
	g_hasMouseOverride = false;
	g_commandsBlocked = false;
}

static bool LoadScriptFile(const char *path) {
	FILE *f = fopen(path, "rb");
	if (!f) {
		LOG_ERROR("TestMode", "failed to open script '%s'", path);
		return false;
	}

	char buf[4096];
	while (fgets(buf, sizeof buf, f)) {
		std::string line(buf);
		while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
			line.pop_back();
		g_scriptLines.push_back(std::move(line));
	}

	fclose(f);
	g_scriptLineNext = 0;
	return true;
}

// =========================================================================
// JSON helpers
// =========================================================================

static std::string JsonEscape(const std::string &s) {
	std::string out;
	out.reserve(s.size() + 8);
	for (char c : s) {
		switch (c) {
			case '"':  out += "\\\""; break;
			case '\\': out += "\\\\"; break;
			case '\n': out += "\\n"; break;
			case '\r': out += "\\r"; break;
			case '\t': out += "\\t"; break;
			default:   out += c; break;
		}
	}
	return out;
}

static std::string JsonOk() {
	return "{\"ok\":true}";
}

static std::string JsonError(const std::string &msg) {
	return "{\"ok\":false,\"error\":\"" + JsonEscape(msg) + "\"}";
}

static const char *CommandStateToString(ATUICmdState state) {
	switch(state) {
		case kATUICmdState_Checked:
			return "checked";
		case kATUICmdState_RadioChecked:
			return "radio";
		default:
			return "none";
	}
}

static std::string BuildCommandJson(const std::string& command) {
	const ATUICommand *cmd = ATUIGetCommandManager().GetCommand(command.c_str());

	if (!cmd)
		return "{\"ok\":true,\"exists\":false}";

	const bool enabled = !cmd->mpTestFn || cmd->mpTestFn();
	const ATUICmdState state = cmd->mpStateFn ? cmd->mpStateFn() : kATUICmdState_None;

	std::string json = "{\"ok\":true,\"exists\":true";
	json += ",\"enabled\":";
	json += enabled ? "true" : "false";
	json += ",\"state\":\"";
	json += CommandStateToString(state);
	json += "\"}";
	return json;
}

// =========================================================================
// Command Dispatcher
// =========================================================================

// Parse the first whitespace-delimited token from cmd, advance cmd past it.
static std::string NextToken(std::string &cmd) {
	// skip leading whitespace
	size_t start = cmd.find_first_not_of(" \t");
	if (start == std::string::npos) {
		cmd.clear();
		return {};
	}

	// Check for quoted string
	if (cmd[start] == '"') {
		size_t end = cmd.find('"', start + 1);
		if (end == std::string::npos) {
			std::string tok = cmd.substr(start + 1);
			cmd.clear();
			return tok;
		}
		std::string tok = cmd.substr(start + 1, end - start - 1);
		cmd.erase(0, end + 1);
		return tok;
	}

	size_t end = cmd.find_first_of(" \t", start);
	if (end == std::string::npos) {
		std::string tok = cmd.substr(start);
		cmd.clear();
		return tok;
	}
	std::string tok = cmd.substr(start, end - start);
	cmd.erase(0, end);
	return tok;
}

// Rest of string after stripping leading whitespace
static std::string RestOfLine(std::string &cmd) {
	size_t start = cmd.find_first_not_of(" \t");
	if (start == std::string::npos)
		return {};
	return cmd.substr(start);
}

static std::map<std::string, std::string> ParseKeyValueArgs(std::string cmd) {
	std::map<std::string, std::string> args;

	for (;;) {
		std::string token = NextToken(cmd);
		if (token.empty())
			break;

		size_t eq = token.find('=');
		if (eq == std::string::npos)
			args[token] = "1";
		else
			args[token.substr(0, eq)] = token.substr(eq + 1);
	}

	return args;
}

static bool GetBoolArg(const std::map<std::string, std::string>& args,
	const char *key,
	bool defaultValue)
{
	auto it = args.find(key);
	if (it == args.end())
		return defaultValue;

	const std::string& v = it->second;
	return v == "1" || v == "true" || v == "yes" || v == "on";
}

static int GetIntArg(const std::map<std::string, std::string>& args,
	const char *key,
	int defaultValue)
{
	auto it = args.find(key);
	if (it == args.end())
		return defaultValue;

	return atoi(it->second.c_str());
}

static const char *GetCStringArg(const std::map<std::string, std::string>& args,
	const char *key,
	const char *defaultValue)
{
	auto it = args.find(key);
	if (it == args.end())
		return defaultValue;

	return it->second.c_str();
}

// Dialog name -> ATUIState bool field mapping
struct DialogMapping {
	const char *name;
	bool ATUIState::*field;
};

static const DialogMapping kDialogMap[] = {
	{ "SystemConfig",      &ATUIState::showSystemConfig },
	{ "DiskManager",       &ATUIState::showDiskManager },
	{ "CassetteControl",   &ATUIState::showCassetteControl },
	{ "About",             &ATUIState::showAboutDialog },
	{ "AdjustColors",      &ATUIState::showAdjustColors },
	{ "DisplaySettings",   &ATUIState::showDisplaySettings },
	{ "CartridgeMapper",   &ATUIState::showCartridgeMapper },
	{ "AudioOptions",      &ATUIState::showAudioOptions },
	{ "InputMappings",     &ATUIState::showInputMappings },
	{ "InputSetup",        &ATUIState::showInputSetup },
	{ "Profiles",          &ATUIState::showProfiles },
	{ "CommandLineHelp",   &ATUIState::showCommandLineHelp },
	{ "ChangeLog",         &ATUIState::showChangeLog },
	{ "CompatWarning",     &ATUIState::showCompatWarning },
	{ "DiskExplorer",      &ATUIState::showDiskExplorer },
	{ "SetupWizard",       &ATUIState::showSetupWizard },
	{ "KeyboardShortcuts", &ATUIState::showKeyboardShortcuts },
	{ "KeyboardCustomize", &ATUIState::showKeyboardCustomize },
	{ "CompatDB",          &ATUIState::showCompatDB },
	{ "AdvancedConfig",    &ATUIState::showAdvancedConfig },
	{ "Cheater",           &ATUIState::showCheater },
	{ "LightPen",          &ATUIState::showLightPen },
	{ "Rewind",            &ATUIState::showRewind },
	{ "ScreenEffects",     &ATUIState::showScreenEffects },
};

static bool* FindDialogField(ATUIState &state, const std::string &name) {
	for (auto &m : kDialogMap) {
		if (name == m.name)
			return &(state.*(m.field));
	}
	return nullptr;
}

// Build the query_state JSON response
static std::string BuildStateJson(ATSimulator &sim, ATUIState &state) {
	std::string json = "{\"ok\":true,\"state\":{";

	// Dialog flags
	json += "\"dialogs\":{";
	bool first = true;
	for (auto &m : kDialogMap) {
		if (!first) json += ',';
		json += "\"";
		json += m.name;
		json += "\":";
		json += (state.*(m.field)) ? "true" : "false";
		first = false;
	}
	json += "}";

	// Simulator state
	json += ",\"sim\":{";
	json += "\"running\":";
	json += sim.IsRunning() ? "true" : "false";
	json += ",\"paused\":";
	json += sim.IsPaused() ? "true" : "false";
	json += ",\"turbo\":";
	json += sim.IsTurboModeEnabled() ? "true" : "false";

	// Hardware type
	auto hwMode = sim.GetHardwareMode();
	json += ",\"hardwareMode\":";
	json += std::to_string((int)hwMode);

	json += "}";

	// Visible windows
	json += ",\"windows\":[";
	ImGuiContext &g = *ImGui::GetCurrentContext();
	first = true;
	for (ImGuiWindow *win : g.Windows) {
		if (!win->Active || win->Hidden)
			continue;
		if (!first) json += ',';
		json += "{\"name\":\"";
		json += JsonEscape(win->Name);
		json += "\",\"x\":";
		json += std::to_string((int)win->Pos.x);
		json += ",\"y\":";
		json += std::to_string((int)win->Pos.y);
		json += ",\"w\":";
		json += std::to_string((int)win->Size.x);
		json += ",\"h\":";
		json += std::to_string((int)win->Size.y);
		json += ",\"focused\":";
		json += (g.NavWindow == win) ? "true" : "false";
		json += "}";
		first = false;
	}
	json += "]";

	json += "}}";
	return json;
}

// Build list_items JSON for items in a specific window
static std::string BuildItemListJson(const std::string &windowFilter) {
	std::string json = "{\"ok\":true,\"items\":[";
	bool first = true;

	for (auto &item : g_items) {
		// Skip items without labels (resize grips, borders, internal decorations)
		if (item.label.empty())
			continue;
		if (!windowFilter.empty() && item.windowName.find(windowFilter) == std::string::npos)
			continue;

		if (!first) json += ',';
		json += "{\"label\":\"";
		json += JsonEscape(item.label);
		json += "\",\"window\":\"";
		json += JsonEscape(item.windowName);
		json += "\",\"id\":";
		json += std::to_string(item.id);

		// Item type from status flags
		if (item.flags & ImGuiItemStatusFlags_Checkable) {
			json += ",\"type\":\"checkbox\"";
			json += ",\"checked\":";
			json += (item.flags & ImGuiItemStatusFlags_Checked) ? "true" : "false";
		} else if (item.flags & ImGuiItemStatusFlags_Openable) {
			json += ",\"type\":\"treenode\"";
			json += ",\"opened\":";
			json += (item.flags & ImGuiItemStatusFlags_Opened) ? "true" : "false";
		} else if (item.flags & ImGuiItemStatusFlags_Inputable) {
			json += ",\"type\":\"input\"";
		} else {
			json += ",\"type\":\"button\"";
		}

		json += ",\"disabled\":";
		json += (item.itemFlags & ImGuiItemFlags_Disabled) ? "true" : "false";

		json += ",\"x\":";
		json += std::to_string((int)item.rect.Min.x);
		json += ",\"y\":";
		json += std::to_string((int)item.rect.Min.y);
		json += ",\"w\":";
		json += std::to_string((int)(item.rect.Max.x - item.rect.Min.x));
		json += ",\"h\":";
		json += std::to_string((int)(item.rect.Max.y - item.rect.Min.y));

		json += "}";
		first = false;
	}

	json += "]}";
	return json;
}

// Find an item by window name substring and label
static const TestItem* FindItem(const std::string &windowFilter, const std::string &label) {
	if (label.empty())
		return nullptr;
	for (auto &item : g_items) {
		if (item.label.empty())
			continue;
		if (!windowFilter.empty() && item.windowName.find(windowFilter) == std::string::npos)
			continue;
		if (item.label == label)
			return &item;
	}
	return nullptr;
}

static ImGuiKey ParseTestKey(const std::string& keyName) {
	if (keyName == "delete" || keyName == "Delete" || keyName == "del")
		return ImGuiKey_Delete;
	if (keyName == "escape" || keyName == "Escape" || keyName == "esc")
		return ImGuiKey_Escape;
	if (keyName == "enter" || keyName == "Enter" || keyName == "return" || keyName == "Return")
		return ImGuiKey_Enter;
	if (keyName == "tab" || keyName == "Tab")
		return ImGuiKey_Tab;
	if (keyName == "f2" || keyName == "F2")
		return ImGuiKey_F2;
	if (keyName == "f9" || keyName == "F9")
		return ImGuiKey_F9;
	if (keyName == "pageup" || keyName == "PageUp" || keyName == "page_up")
		return ImGuiKey_PageUp;
	if (keyName == "pagedown" || keyName == "PageDown" || keyName == "page_down")
		return ImGuiKey_PageDown;
	if (keyName == "home" || keyName == "Home")
		return ImGuiKey_Home;
	if (keyName == "end" || keyName == "End")
		return ImGuiKey_End;
	if (keyName == "up" || keyName == "Up")
		return ImGuiKey_UpArrow;
	if (keyName == "down" || keyName == "Down")
		return ImGuiKey_DownArrow;
	if (keyName == "left" || keyName == "Left")
		return ImGuiKey_LeftArrow;
	if (keyName == "right" || keyName == "Right")
		return ImGuiKey_RightArrow;
	if (keyName == "f" || keyName == "F")
		return ImGuiKey_F;

	return ImGuiKey_None;
}

static bool ParseTestKeyChord(const std::string& text, PendingAction& action) {
	std::string keyName;
	size_t start = 0;

	for (;;) {
		const size_t plus = text.find('+', start);
		std::string part = text.substr(start,
			plus == std::string::npos ? std::string::npos : plus - start);

		if (part == "ctrl" || part == "Ctrl" || part == "control" || part == "Control") {
			action.ctrl = true;
		} else if (part == "shift" || part == "Shift") {
			action.shift = true;
		} else if (part == "alt" || part == "Alt") {
			action.alt = true;
		} else {
			keyName = part;
		}

		if (plus == std::string::npos)
			break;

		start = plus + 1;
	}

	action.key = ParseTestKey(keyName);
	return action.key != ImGuiKey_None;
}

static std::string DispatchCommand(std::string cmd, ATSimulator &sim, ATUIState &state) {
	std::string verb = NextToken(cmd);
	if (verb.empty())
		return JsonError("empty command");

	// --- Liveness ---
	if (verb == "ping") {
		return JsonOk();
	}

	// --- State queries ---
	if (verb == "query_state") {
		return BuildStateJson(sim, state);
	}

	if (verb == "list_items") {
		std::string windowFilter = NextToken(cmd);
		return BuildItemListJson(windowFilter);
	}

	if (verb == "query_window" || verb == "query_window_label") {
		std::string title = (verb == "query_window_label")
			? RestOfLine(cmd)
			: NextToken(cmd);
		if (title.empty())
			return JsonError(std::string("usage: ") + verb + " <title>");

		ImGuiWindow *win = ImGui::FindWindowByName(title.c_str());
		if (!win || !win->Active || win->Hidden)
			return "{\"ok\":true,\"visible\":false}";

		std::string json = "{\"ok\":true,\"visible\":true";
		json += ",\"x\":" + std::to_string((int)win->Pos.x);
		json += ",\"y\":" + std::to_string((int)win->Pos.y);
		json += ",\"w\":" + std::to_string((int)win->Size.x);
		json += ",\"h\":" + std::to_string((int)win->Size.y);
		json += "}";
		return json;
	}

	if (verb == "query_command") {
		std::string command = RestOfLine(cmd);
		if (command.empty())
			return JsonError("usage: query_command <command>");

		return BuildCommandJson(command);
	}

	if (verb == "query_console_input") {
		std::string json = "{\"ok\":true,\"text\":\"";
		json += JsonEscape(ATUIDebuggerGetConsoleInputTextForTest());
		json += "\"}";
		return json;
	}

	if (verb == "query_debugger_focus") {
		const uint32 paneId = ATUIDebuggerGetFocusedPaneId();
		std::string json = "{\"ok\":true,\"pane_id\":";
		json += std::to_string(paneId);
		json += "}";
		return json;
	}

	if (verb == "query_message_box") {
		std::string json = "{\"ok\":true,\"seq\":";
		json += std::to_string(g_lastMessageBoxSeq);
		json += ",\"kind\":\"";
		json += JsonEscape(g_lastMessageBoxKind);
		json += "\",\"title\":\"";
		json += JsonEscape(g_lastMessageBoxTitle);
		json += "\",\"message\":\"";
		json += JsonEscape(g_lastMessageBoxMessage);
		json += "\"}";
		return json;
	}

	if (verb == "clear_message_box") {
		g_lastMessageBoxSeq = 0;
		g_lastMessageBoxKind.clear();
		g_lastMessageBoxTitle.clear();
		g_lastMessageBoxMessage.clear();
		return JsonOk();
	}

	if (verb == "debugger_request_file") {
		std::string mode = NextToken(cmd);
		if (mode != "open" && mode != "save")
			return JsonError("usage: debugger_request_file <open|save> [utf8_path]");

		VDStringW pathW;
		ATUIDebuggerRequestFileForTest(mode == "save", RestOfLine(cmd).c_str(), pathW);

		std::string json = "{\"ok\":true,\"save\":";
		json += (mode == "save") ? "true" : "false";
		json += ",\"path\":\"";
		json += JsonEscape(VDTextWToU8(pathW).c_str());
		json += "\"}";
		return json;
	}

	if (verb == "breakpoint_format_trace") {
		std::string traceText = RestOfLine(cmd);

		VDStringA command;
		VDStringA error;
		const bool ok = ATUIDebuggerFormatBreakpointTraceForTest(
			traceText.c_str(),
			command,
			error);

		std::string json = "{\"ok\":true,\"valid\":";
		json += ok ? "true" : "false";
		json += ",\"command\":\"";
		json += JsonEscape(command.c_str());
		json += "\",\"error\":\"";
		json += JsonEscape(error.c_str());
		json += "\"}";
		return json;
	}

	if (verb == "breakpoint_submit") {
		auto args = ParseKeyValueArgs(RestOfLine(cmd));

		uint32 userIdx = (uint32)-1;
		VDStringA error;
		const bool ok = ATUIDebuggerSubmitBreakpointForTest(
			GetIntArg(args, "type", 0),
			GetCStringArg(args, "location", ""),
			GetBoolArg(args, "condition", false),
			GetCStringArg(args, "condition_text", ""),
			GetBoolArg(args, "stop", true),
			GetBoolArg(args, "command", false),
			GetCStringArg(args, "command_text", ""),
			GetBoolArg(args, "trace", false),
			GetCStringArg(args, "trace_text", ""),
			userIdx,
			error);

		std::string json = "{\"ok\":true,\"valid\":";
		json += ok ? "true" : "false";
		json += ",\"id\":";
		json += ok ? std::to_string(userIdx) : "-1";
		json += ",\"error\":\"";
		json += JsonEscape(error.c_str());
		json += "\"}";
		return json;
	}

	if (verb == "breakpoint_describe") {
		std::string idStr = NextToken(cmd);
		if (idStr.empty())
			return JsonError("usage: breakpoint_describe <id>");

		const uint32 id = (uint32)strtoul(idStr.c_str(), nullptr, 0);
		VDStringA desc;
		const bool found = ATUIDebuggerDescribeBreakpointForTest(id, desc);

		std::string json = "{\"ok\":true,\"found\":";
		json += found ? "true" : "false";
		json += ",\"description\":\"";
		json += JsonEscape(desc.c_str());
		json += "\"}";
		return json;
	}

	if (verb == "breakpoint_format_description") {
		auto args = ParseKeyValueArgs(RestOfLine(cmd));

		VDStringA desc;
		const bool ok = ATUIDebuggerFormatBreakpointDescriptionForTest(
			GetBoolArg(args, "oneshot", false),
			GetBoolArg(args, "clear", false),
			GetBoolArg(args, "trace", false),
			desc);

		std::string json = "{\"ok\":true,\"valid\":";
		json += ok ? "true" : "false";
		json += ",\"description\":\"";
		json += JsonEscape(desc.c_str());
		json += "\"}";
		return json;
	}

	if (verb == "breakpoint_delete") {
		std::string idStr = NextToken(cmd);
		if (idStr.empty())
			return JsonError("usage: breakpoint_delete <id>");

		const uint32 id = (uint32)strtoul(idStr.c_str(), nullptr, 0);
		const bool deleted = ATUIDebuggerDeleteBreakpointForTest(id);

		std::string json = "{\"ok\":true,\"deleted\":";
		json += deleted ? "true" : "false";
		json += "}";
		return json;
	}

	if (verb == "breakpoint_select") {
		std::string idStr = NextToken(cmd);
		if (idStr.empty())
			return JsonError("usage: breakpoint_select <id>");

		const uint32 id = (uint32)strtoul(idStr.c_str(), nullptr, 0);
		const bool selected = ATUIDebuggerSelectBreakpointForTest(id);

		std::string json = "{\"ok\":true,\"selected\":";
		json += selected ? "true" : "false";
		json += "}";
		return json;
	}

	if (verb == "breakpoint_delete_selected") {
		std::string idStr = NextToken(cmd);
		if (idStr.empty())
			return JsonError("usage: breakpoint_delete_selected <id>");

		const uint32 id = (uint32)strtoul(idStr.c_str(), nullptr, 0);
		int remainingCount = -1;
		const bool deleted = ATUIDebuggerDeleteBreakpointViaPaneForTest(id, remainingCount);

		std::string json = "{\"ok\":true,\"deleted\":";
		json += deleted ? "true" : "false";
		json += ",\"remaining\":";
		json += std::to_string(remainingCount);
		json += "}";
		return json;
	}

	if (verb == "breakpoint_pane_order") {
		VDStringA rowOrder;
		if (!ATUIDebuggerGetBreakpointPaneOrderForTest(rowOrder))
			return JsonError("breakpoint pane is not available");

		std::string json = "{\"ok\":true,\"order\":\"";
		json += JsonEscape(rowOrder.c_str());
		json += "\"}";
		return json;
	}

	if (verb == "breakpoint_count") {
		std::string json = "{\"ok\":true,\"count\":";
		json += std::to_string(ATUIDebuggerCountBreakpointsForTest());
		json += "}";
		return json;
	}

	if (verb == "watch_interface") {
		std::string expr = RestOfLine(cmd);

		ATUIDebuggerOpen();
		ATActivateUIPane(kATUIPaneId_WatchN, true, true);

		auto *watchPane = static_cast<IATUIDebuggerWatchPane *>(
			ATGetUIPaneAs(kATUIPaneId_WatchN, IATUIDebuggerWatchPane::kTypeID));

		const bool found = watchPane != nullptr;
		const bool added = found && !expr.empty();

		if (added)
			watchPane->AddWatch(expr.c_str());

		std::string json = "{\"ok\":true,\"found\":";
		json += found ? "true" : "false";
		json += ",\"added\":";
		json += added ? "true" : "false";
		json += "}";
		return json;
	}

	if (verb == "watch_edit") {
		std::string expr = RestOfLine(cmd);

		VDStringA state;
		const bool edited = ATUIDebuggerEditWatchForTest(expr.c_str(), state);

		std::string json = "{\"ok\":true,\"edited\":";
		json += edited ? "true" : "false";
		json += ",\"state\":\"";
		json += JsonEscape(state.c_str());
		json += "\"}";
		return json;
	}

	if (verb == "watch_printable_edit") {
		std::string first = NextToken(cmd);
		std::string suffix = RestOfLine(cmd);
		if (first.empty())
			return JsonError("usage: watch_printable_edit <char> [suffix]");

		VDStringA state;
		const bool edited = ATUIDebuggerPrintableEditWatchForTest(first[0],
			suffix.c_str(),
			state);

		std::string json = "{\"ok\":true,\"edited\":";
		json += edited ? "true" : "false";
		json += ",\"state\":\"";
		json += JsonEscape(state.c_str());
		json += "\"}";
		return json;
	}

	if (verb == "watch_delete_selected") {
		VDStringA state;
		const bool deleted = ATUIDebuggerDeleteSelectedWatchForTest(state);

		std::string json = "{\"ok\":true,\"deleted\":";
		json += deleted ? "true" : "false";
		json += ",\"state\":\"";
		json += JsonEscape(state.c_str());
		json += "\"}";
		return json;
	}

	if (verb == "watch_describe") {
		VDStringA state;
		if (!ATUIDebuggerDescribeWatchForTest(state))
			return JsonError("watch pane is not available");

		std::string json = "{\"ok\":true,\"state\":\"";
		json += JsonEscape(state.c_str());
		json += "\"}";
		return json;
	}

	if (verb == "watch_clear_all") {
		IATDebugger *dbg = ATGetDebugger();
		if (!dbg)
			return JsonError("debugger is not available");

		dbg->ClearAllWatches();
		return JsonOk();
	}

	if (verb == "watch_count") {
		IATDebugger *dbg = ATGetDebugger();
		if (!dbg)
			return JsonError("debugger is not available");

		int count = 0;
		for (int i = 0; i < 8; ++i) {
			ATDebuggerWatchInfo info {};
			if (dbg->GetWatchInfo(i, info))
				++count;
		}

		std::string json = "{\"ok\":true,\"count\":";
		json += std::to_string(count);
		json += "}";
		return json;
	}

	if (verb == "watch_fill") {
		IATDebugger *dbg = ATGetDebugger();
		if (!dbg)
			return JsonError("debugger is not available");

		dbg->ClearAllWatches();

		int added = 0;
		for (int i = 0; i < 8; ++i) {
			if (dbg->AddWatch(0x2000 + (uint32)i, 1) >= 0)
				++added;
		}

		std::string json = "{\"ok\":true,\"added\":";
		json += std::to_string(added);
		json += "}";
		return json;
	}

	if (verb == "memory_add_to_watch_expr") {
		std::string addrStr = NextToken(cmd);
		auto args = ParseKeyValueArgs(cmd);
		if (addrStr.empty())
			return JsonError("usage: memory_add_to_watch_expr <addr> [word=1]");

		const uint32 addr = (uint32)strtoul(addrStr.c_str(), nullptr, 0);
		const bool wordMode = GetBoolArg(args, "word", false);

		VDStringA expr;
		if (!ATUIDebuggerFormatMemoryAddToWatchForTest(addr, wordMode, expr))
			return JsonError("memory add-to-watch expression test failed");

		std::string json = "{\"ok\":true,\"expr\":\"";
		json += JsonEscape(expr.c_str());
		json += "\"}";
		return json;
	}

	if (verb == "memory_track_on_screen") {
		std::string addrStr = NextToken(cmd);
		auto args = ParseKeyValueArgs(cmd);
		if (addrStr.empty())
			return JsonError("usage: memory_track_on_screen <addr> [len=1]");

		const uint32 addr = (uint32)strtoul(addrStr.c_str(), nullptr, 0);
		const int len = GetIntArg(args, "len", 1);

		int watchIndex = -1;
		const bool added = ATUIDebuggerTrackMemoryOnScreenForTest(addr,
			len,
			watchIndex);

		std::string json = "{\"ok\":true,\"added\":";
		json += added ? "true" : "false";
		json += ",\"watch_index\":";
		json += std::to_string(watchIndex);
		json += ",\"overflow\":";
		json += added ? "false" : "true";
		json += "}";
		return json;
	}

	if (verb == "memory_ensure_visible") {
		auto args = ParseKeyValueArgs(RestOfLine(cmd));
		const uint32 viewStart = (uint32)strtoul(
			args["view"].empty() ? "0" : args["view"].c_str(), nullptr, 0);
		const uint32 highlight = (uint32)strtoul(
			args["highlight"].empty() ? "0" : args["highlight"].c_str(), nullptr, 0);
		const uint32 columns = (uint32)GetIntArg(args, "columns", 16);
		const uint32 rows = (uint32)GetIntArg(args, "rows", 16);

		uint32 outViewStart = 0;
		if (!ATUIDebuggerEnsureMemoryHighlightVisibleForTest(viewStart,
			columns,
			rows,
			highlight,
			outViewStart))
			return JsonError("memory pane is not available");

		char viewBuf[32];
		snprintf(viewBuf, sizeof(viewBuf), "$%08X", outViewStart);

		std::string json = "{\"ok\":true,\"view_start\":\"";
		json += viewBuf;
		json += "\"}";
		return json;
	}

	if (verb == "memory_navigation") {
		std::string op = NextToken(cmd);
		auto args = ParseKeyValueArgs(cmd);
		if (op.empty()) {
			return JsonError(
				"usage: memory_navigation <left|right|up|down|ctrl_up|ctrl_down|page_up|page_down|enter|tab> view=<addr> highlight=<addr> [columns=16] [rows=16] [data=0]");
		}

		const uint32 viewStart = (uint32)strtoul(
			args["view"].empty() ? "0" : args["view"].c_str(), nullptr, 0);
		const uint32 highlight = (uint32)strtoul(
			args["highlight"].empty() ? "0" : args["highlight"].c_str(), nullptr, 0);
		const uint32 columns = (uint32)GetIntArg(args, "columns", 16);
		const uint32 rows = (uint32)GetIntArg(args, "rows", 16);
		const bool dataColumn = GetIntArg(args, "data", 0) != 0;

		uint32 outViewStart = 0;
		uint32 outHighlight = 0;
		bool outDataColumn = false;
		if (!ATUIDebuggerMemoryNavigationForTest(viewStart,
				columns,
				rows,
				highlight,
				dataColumn,
				op.c_str(),
				outViewStart,
				outHighlight,
				outDataColumn)) {
			return JsonError("memory navigation test failed");
		}

		char viewBuf[32];
		char highlightBuf[32];
		snprintf(viewBuf, sizeof(viewBuf), "$%08X", outViewStart);
		snprintf(highlightBuf, sizeof(highlightBuf), "$%08X", outHighlight);

		std::string json = "{\"ok\":true,\"view_start\":\"";
		json += viewBuf;
		json += "\",\"highlight\":\"";
		json += highlightBuf;
		json += "\",\"data\":";
		json += outDataColumn ? "true" : "false";
		json += "}";
		return json;
	}

		if (verb == "memory_hex_edit" || verb == "memory_hex_cancel") {
		std::string addrStr = NextToken(cmd);
		std::string valueStr = NextToken(cmd);
		if (addrStr.empty() || valueStr.empty()) {
			return JsonError(verb == "memory_hex_edit"
				? "usage: memory_hex_edit <addr> <byte>"
				: "usage: memory_hex_cancel <addr> <byte>");
		}

		const uint32 addr = (uint32)strtoul(addrStr.c_str(), nullptr, 0);
		const uint8 value = (uint8)strtoul(valueStr.c_str(), nullptr, 0);
		uint8 before = 0;
		uint8 after = 0;
		uint32 selectedAddr = 0;
		bool selectionEnabled = false;

		bool ok = false;
		if (verb == "memory_hex_edit") {
			ok = ATUIDebuggerEditMemoryHexByteForTest(addr,
				value,
				before,
				after);
		} else {
			ok = ATUIDebuggerCancelMemoryHexByteEditForTest(addr,
				value,
				before,
				after,
				selectedAddr,
				selectionEnabled);
		}

		if (!ok)
			return JsonError("memory hex edit test failed");

		char beforeBuf[8];
		char afterBuf[8];
		char selectedBuf[16];
		snprintf(beforeBuf, sizeof(beforeBuf), "$%02X", before);
		snprintf(afterBuf, sizeof(afterBuf), "$%02X", after);
		snprintf(selectedBuf, sizeof(selectedBuf), "$%04X", selectedAddr & 0xffff);

		std::string json = "{\"ok\":true,\"before\":\"";
		json += beforeBuf;
		json += "\",\"after\":\"";
		json += afterBuf;
		json += "\"";

		if (verb == "memory_hex_cancel") {
			json += ",\"selected\":\"";
			json += selectedBuf;
			json += "\",\"selection_enabled\":";
			json += selectionEnabled ? "true" : "false";
		}

		json += "}";
			return json;
		}

		if (verb == "memory_hex_auto_advance") {
			std::string modeStr = NextToken(cmd);
			std::string digits = NextToken(cmd);
			auto args = ParseKeyValueArgs(cmd);
			if (modeStr.empty() || digits.empty()) {
				return JsonError(
					"usage: memory_hex_auto_advance <hex_byte|hex_word> <digits> view=<addr> highlight=<addr> [columns=16] [rows=16]");
			}

			int mode = -1;
			if (modeStr == "hex_byte")
				mode = 0;
			else if (modeStr == "hex_word")
				mode = 1;
			else
				return JsonError("bad hex mode: " + modeStr);

			const uint32 viewStart = (uint32)strtoul(
				args["view"].empty() ? "0" : args["view"].c_str(), nullptr, 0);
			const uint32 highlight = (uint32)strtoul(
				args["highlight"].empty() ? "0" : args["highlight"].c_str(), nullptr, 0);
			const uint32 columns = (uint32)GetIntArg(args, "columns", 16);
			const uint32 rows = (uint32)GetIntArg(args, "rows", 16);

			uint16 value = 0;
			uint32 selectedAddr = 0;
			uint32 outViewStart = 0;
			if (!ATUIDebuggerMemoryHexAutoAdvanceForTest(mode,
					viewStart,
					columns,
					rows,
					highlight,
					digits.c_str(),
					value,
					selectedAddr,
					outViewStart)) {
				return JsonError("memory hex auto-advance test failed");
			}

			char valueBuf[16];
			char selectedBuf[32];
			char viewBuf[32];
			if (mode == 0)
				snprintf(valueBuf, sizeof(valueBuf), "$%02X", value & 0xff);
			else
				snprintf(valueBuf, sizeof(valueBuf), "$%04X", value);
			snprintf(selectedBuf, sizeof(selectedBuf), "$%08X", selectedAddr);
			snprintf(viewBuf, sizeof(viewBuf), "$%08X", outViewStart);

			std::string json = "{\"ok\":true,\"mode\":\"";
			json += JsonEscape(modeStr);
			json += "\",\"value\":\"";
			json += valueBuf;
			json += "\",\"selected\":\"";
			json += selectedBuf;
			json += "\",\"view_start\":\"";
			json += viewBuf;
			json += "\"}";
			return json;
		}

		if (verb == "memory_value_edit") {
		std::string modeStr = NextToken(cmd);
		std::string addrStr = NextToken(cmd);
		std::string valueStr = NextToken(cmd);
		if (modeStr.empty() || addrStr.empty() || valueStr.empty()) {
			return JsonError(
				"usage: memory_value_edit <hex_byte|hex_word|dec_byte|dec_word> <addr> <value>");
		}

		int mode = -1;
		if (modeStr == "hex_byte")
			mode = 0;
		else if (modeStr == "hex_word")
			mode = 1;
		else if (modeStr == "dec_byte")
			mode = 2;
		else if (modeStr == "dec_word")
			mode = 3;
		else
			return JsonError("bad value mode: " + modeStr);

		char *end = nullptr;
		uint32 addr = (uint32)strtoul(addrStr.c_str(), &end, 0);
		if (end == addrStr.c_str() || *end)
			return JsonError("bad address: " + addrStr);

		const uint32 value = (uint32)strtoul(valueStr.c_str(), &end, 0);
		if (end == valueStr.c_str() || *end || value > 0xFFFF)
			return JsonError("bad value: " + valueStr);

		uint16 before = 0;
		uint16 after = 0;
		if (!ATUIDebuggerEditMemoryValueForTest(mode,
				addr,
				(uint16)value,
				before,
				after)) {
			return JsonError("memory value edit test failed");
		}

		const bool wordMode = mode == 1 || mode == 3;
		char beforeBuf[16];
		char afterBuf[16];
		if (wordMode) {
			snprintf(beforeBuf, sizeof(beforeBuf), "$%04X", before);
			snprintf(afterBuf, sizeof(afterBuf), "$%04X", after);
		} else {
			snprintf(beforeBuf, sizeof(beforeBuf), "$%02X", before & 0xff);
			snprintf(afterBuf, sizeof(afterBuf), "$%02X", after & 0xff);
		}

		std::string json = "{\"ok\":true,\"mode\":\"";
		json += JsonEscape(modeStr);
		json += "\",\"before\":\"";
		json += beforeBuf;
		json += "\",\"after\":\"";
		json += afterBuf;
		json += "\"}";
			return json;
		}

		if (verb == "memory_text_auto_advance") {
			std::string modeStr = NextToken(cmd);
			std::string chStr = NextToken(cmd);
			auto args = ParseKeyValueArgs(cmd);
			if (modeStr.empty() || chStr.empty())
				return JsonError("usage: memory_text_auto_advance <atascii|internal> <char_code> view=<addr> highlight=<addr> [columns=16] [rows=16]");

			int mode = -1;
			if (modeStr == "atascii")
				mode = 0;
			else if (modeStr == "internal")
				mode = 1;
			else
				return JsonError("bad text mode: " + modeStr);

			char *end = nullptr;
			const uint32 ch = (uint32)strtoul(chStr.c_str(), &end, 0);
			if (end == chStr.c_str() || *end || ch > 0x7e || ch < 0x20)
				return JsonError("bad character code: " + chStr);

			const uint32 viewStart = (uint32)strtoul(
				args["view"].empty() ? "0" : args["view"].c_str(), nullptr, 0);
			const uint32 highlight = (uint32)strtoul(
				args["highlight"].empty() ? "0" : args["highlight"].c_str(), nullptr, 0);
			const uint32 columns = (uint32)GetIntArg(args, "columns", 16);
			const uint32 rows = (uint32)GetIntArg(args, "rows", 16);

			uint8 written = 0;
			uint32 selectedAddr = 0;
			uint32 outViewStart = 0;
			if (!ATUIDebuggerMemoryTextAutoAdvanceForTest(mode,
					viewStart,
					columns,
					rows,
					highlight,
					(uint8)ch,
					written,
					selectedAddr,
					outViewStart)) {
				return JsonError("memory text auto-advance test failed");
			}

			char writtenBuf[8];
			char selectedBuf[32];
			char viewBuf[32];
			snprintf(writtenBuf, sizeof(writtenBuf), "$%02X", written);
			snprintf(selectedBuf, sizeof(selectedBuf), "$%08X", selectedAddr);
			snprintf(viewBuf, sizeof(viewBuf), "$%08X", outViewStart);

			std::string json = "{\"ok\":true,\"mode\":\"";
			json += JsonEscape(modeStr);
			json += "\",\"written\":\"";
			json += writtenBuf;
			json += "\",\"selected\":\"";
			json += selectedBuf;
			json += "\",\"view_start\":\"";
			json += viewBuf;
			json += "\"}";
			return json;
		}

		if (verb == "memory_text_edit") {
		std::string modeStr = NextToken(cmd);
		std::string addrStr = NextToken(cmd);
		std::string chStr = NextToken(cmd);
		if (modeStr.empty() || addrStr.empty() || chStr.empty())
			return JsonError("usage: memory_text_edit <atascii|internal> <addr> <char_code>");

		int mode = -1;
		if (modeStr == "atascii")
			mode = 0;
		else if (modeStr == "internal")
			mode = 1;
		else
			return JsonError("bad text mode: " + modeStr);

		char *end = nullptr;
		uint32 addr = (uint32)strtoul(addrStr.c_str(), &end, 0);
		if (end == addrStr.c_str() || *end)
			return JsonError("bad address: " + addrStr);

		const uint32 ch = (uint32)strtoul(chStr.c_str(), &end, 0);
		if (end == chStr.c_str() || *end || ch > 0x7e || ch < 0x20)
			return JsonError("bad character code: " + chStr);

		uint8 before = 0;
		uint8 after = 0;
		uint8 written = 0;
		if (!ATUIDebuggerEditMemoryTextForTest(mode,
				addr,
				(uint8)ch,
				before,
				after,
				written)) {
			return JsonError("memory text edit test failed");
		}

		char beforeBuf[8];
		char afterBuf[8];
		char writtenBuf[8];
		snprintf(beforeBuf, sizeof(beforeBuf), "$%02X", before);
		snprintf(afterBuf, sizeof(afterBuf), "$%02X", after);
		snprintf(writtenBuf, sizeof(writtenBuf), "$%02X", written);

		std::string json = "{\"ok\":true,\"mode\":\"";
		json += JsonEscape(modeStr);
		json += "\",\"before\":\"";
		json += beforeBuf;
		json += "\",\"written\":\"";
		json += writtenBuf;
		json += "\",\"after\":\"";
		json += afterBuf;
		json += "\"}";
		return json;
	}

	if (verb == "disasm_selected_breakpoint") {
		std::string mode = NextToken(cmd);
		if (mode.empty())
			mode = "query";
		if (mode != "query" && mode != "toggle")
			return JsonError("usage: disasm_selected_breakpoint [query|toggle]");

		ATUIDebuggerOpen();
		ATActivateUIPane(kATUIPaneId_Disassembly, true, true);

		int line = -1;
		uint32 addr = 0;
		bool hasBreakpoint = false;
		const bool valid = mode == "toggle"
			? ATUIDebuggerToggleDisassemblySelectedBreakpointForTest(line, addr, hasBreakpoint)
			: ATUIDebuggerQueryDisassemblySelectedBreakpointForTest(line, addr, hasBreakpoint);

		char addrBuf[32];
		snprintf(addrBuf, sizeof(addrBuf), "$%04X", addr & 0xFFFF);

		std::string json = "{\"ok\":true,\"valid\":";
		json += valid ? "true" : "false";
		json += ",\"line\":";
		json += std::to_string(line);
		json += ",\"address\":\"";
		json += JsonEscape(addrBuf);
		json += "\",\"has_breakpoint\":";
		json += hasBreakpoint ? "true" : "false";
		json += "}";
		return json;
	}

	if (verb == "disasm_context") {
		std::string action = NextToken(cmd);
		if (action != "go_source")
			return JsonError("usage: disasm_context <go_source>");

		int line = -1;
		uint32 addr = 0;
		bool applied = false;
		if (!ATUIDebuggerDisassemblyGoToSelectedSourceForTest(
				line,
				addr,
				applied)) {
			return JsonError("disassembly context action failed");
		}

		char addrBuf[32];
		snprintf(addrBuf, sizeof(addrBuf), "$%04X", addr & 0xFFFF);

		std::string json = "{\"ok\":true,\"applied\":";
		json += applied ? "true" : "false";
		json += ",\"line\":";
		json += std::to_string(line);
		json += ",\"address\":\"";
		json += JsonEscape(addrBuf);
		json += "\"}";
		return json;
	}

	if (verb == "disasm_target_nav") {
		VDStringA state;
		if (!ATUIDebuggerDisassemblyTargetNavigationForTest(state))
			return JsonError("disassembly target navigation test failed");

		std::string json = "{\"ok\":true,\"state\":\"";
		json += JsonEscape(state.c_str());
		json += "\"}";
		return json;
	}

	if (verb == "disasm_preview") {
		VDStringA state;
		if (!ATUIDebuggerDisassemblyCallPreviewForTest(state))
			return JsonError("disassembly call preview test failed");

		std::string json = "{\"ok\":true,\"state\":\"";
		json += JsonEscape(state.c_str());
		json += "\"}";
		return json;
	}

	if (verb == "disasm_breakpoint_runstop_regression") {
		ATUIDebuggerOpen();
		ATActivateUIPane(kATUIPaneId_Disassembly, true, true);

		int initialLine = -1;
		uint32 initialAddr = 0;
		bool initialHas = false;
		if (!ATUIDebuggerQueryDisassemblySelectedBreakpointForTest(
				initialLine,
				initialAddr,
				initialHas)) {
			return JsonError("disassembly selected-line query failed");
		}

		int line = initialLine;
		uint32 addr = initialAddr;
		bool hasBreakpoint = initialHas;

		if (initialHas
			&& !ATUIDebuggerToggleDisassemblySelectedBreakpointForTest(
				line,
				addr,
				hasBreakpoint)) {
			return JsonError("disassembly breakpoint baseline clear failed");
		}

		bool firstToggleHas = false;
		line = initialLine;
		addr = initialAddr;
		if (!ATUIDebuggerToggleDisassemblySelectedBreakpointForTest(
				line,
				addr,
				firstToggleHas)) {
			return JsonError("disassembly first breakpoint toggle failed");
		}

		int afterRunStopLine = -1;
		uint32 afterRunStopAddr = 0;
		bool afterRunStopHas = false;
		if (!ATUIDebuggerSimulateDisassemblyRunStopForTest(
				initialAddr,
				afterRunStopLine,
				afterRunStopAddr,
				afterRunStopHas)) {
			return JsonError("disassembly run/stop simulation failed");
		}

		int secondToggleLine = -1;
		uint32 secondToggleAddr = 0;
		bool secondToggleHas = true;
		if (!ATUIDebuggerToggleDisassemblySelectedBreakpointForTest(
				secondToggleLine,
				secondToggleAddr,
				secondToggleHas)) {
			return JsonError("disassembly second breakpoint toggle failed");
		}

		bool restoredHas = secondToggleHas;
		if (initialHas) {
			int restoreLine = -1;
			uint32 restoreAddr = 0;
			if (!ATUIDebuggerToggleDisassemblySelectedBreakpointForTest(
					restoreLine,
					restoreAddr,
					restoredHas)) {
				return JsonError("disassembly breakpoint restore failed");
			}
		}

		char initialAddrBuf[32];
		char afterAddrBuf[32];
		char secondAddrBuf[32];
		snprintf(initialAddrBuf, sizeof(initialAddrBuf), "$%04X", initialAddr & 0xFFFF);
		snprintf(afterAddrBuf, sizeof(afterAddrBuf), "$%04X", afterRunStopAddr & 0xFFFF);
		snprintf(secondAddrBuf, sizeof(secondAddrBuf), "$%04X", secondToggleAddr & 0xFFFF);

		const bool passed = firstToggleHas
			&& afterRunStopHas
			&& !secondToggleHas
			&& afterRunStopAddr == initialAddr
			&& secondToggleAddr == initialAddr
			&& restoredHas == initialHas;

		std::string json = "{\"ok\":true,\"passed\":";
		json += passed ? "true" : "false";
		json += ",\"initial_line\":";
		json += std::to_string(initialLine);
		json += ",\"initial_address\":\"";
		json += JsonEscape(initialAddrBuf);
		json += "\",\"initial_has_breakpoint\":";
		json += initialHas ? "true" : "false";
		json += ",\"after_first_toggle\":";
		json += firstToggleHas ? "true" : "false";
		json += ",\"after_runstop_line\":";
		json += std::to_string(afterRunStopLine);
		json += ",\"after_runstop_address\":\"";
		json += JsonEscape(afterAddrBuf);
		json += "\",\"after_runstop_has_breakpoint\":";
		json += afterRunStopHas ? "true" : "false";
		json += ",\"after_second_toggle_address\":\"";
		json += JsonEscape(secondAddrBuf);
		json += "\",\"after_second_toggle\":";
		json += secondToggleHas ? "true" : "false";
		json += ",\"restored_has_breakpoint\":";
		json += restoredHas ? "true" : "false";
		json += "}";
		return json;
	}

	if (verb == "targets_activate") {
		std::string rowStr = NextToken(cmd);
		if (rowStr.empty())
			return JsonError("usage: targets_activate <row>");

		const int row = atoi(rowStr.c_str());
		uint32 currentTarget = 0;
		uint32 targetCount = 0;
		VDStringA error;
		const bool activated = ATUIDebuggerActivateTargetForTest(row,
			currentTarget,
			targetCount,
			error);

		std::string json = "{\"ok\":true,\"activated\":";
		json += activated ? "true" : "false";
		json += ",\"current\":";
		json += std::to_string(currentTarget);
		json += ",\"count\":";
		json += std::to_string(targetCount);
		json += ",\"error\":\"";
		json += JsonEscape(error.c_str());
		json += "\"}";
		return json;
	}

	if (verb == "debug_display") {
		std::string subcmd = NextToken(cmd);
		VDStringA state;
		bool applied = true;

		if (subcmd == "describe") {
			if (!ATUIDebuggerDescribeDebugDisplayForTest(state))
				return JsonError("debug display pane is not available");
		} else if (subcmd == "dl") {
			applied = ATUIDebuggerApplyDebugDisplayDLForTest(
				RestOfLine(cmd).c_str(), state);
		} else if (subcmd == "pf") {
			applied = ATUIDebuggerApplyDebugDisplayPFForTest(
				RestOfLine(cmd).c_str(), state);
		} else if (subcmd == "filter") {
			std::string modeStr = NextToken(cmd);
			if (modeStr.empty())
				return JsonError("usage: debug_display filter <1|2|3>");
			applied = ATUIDebuggerSetDebugDisplayFilterForTest(
				atoi(modeStr.c_str()), state);
		} else if (subcmd == "palette") {
			std::string modeStr = NextToken(cmd);
			if (modeStr.empty())
				return JsonError("usage: debug_display palette <0|1>");
			applied = ATUIDebuggerSetDebugDisplayPaletteForTest(
				atoi(modeStr.c_str()), state);
		} else {
			return JsonError("usage: debug_display <describe|dl|pf|filter|palette> ...");
		}

		std::string json = "{\"ok\":true,\"applied\":";
		json += applied ? "true" : "false";
		json += ",\"state\":\"";
		json += JsonEscape(state.c_str());
		json += "\"}";
		return json;
	}

	if (verb == "history_interface") {
		ATUIDebuggerOpen();
		ATActivateUIPane(kATUIPaneId_History, true, true);

		auto *historyPane = static_cast<IATUIDebuggerHistoryPane *>(
			ATGetUIPaneAs(kATUIPaneId_History, IATUIDebuggerHistoryPane::kTypeID));

		std::string json = "{\"ok\":true,\"found\":";
		json += historyPane ? "true" : "false";
		json += "}";
		return json;
	}

	if (verb == "history_describe") {
		VDStringA state;
		if (!ATUIDebuggerDescribeHistoryForTest(state))
			return JsonError("history pane is not available");

		std::string json = "{\"ok\":true,\"state\":\"";
		json += JsonEscape(state.c_str());
		json += "\"}";
		return json;
	}

	if (verb == "history_context") {
		std::string action = NextToken(cmd);
		if (action.empty()) {
			return JsonError(
				"usage: history_context <go_source|set_origin|reset_origin|mode_beam|mode_us|mode_cycles|mode_unhalted|mode_tape_samples|mode_tape_seconds>");
		}

		VDStringA state;
		bool applied = false;
		if (!ATUIDebuggerHistoryContextActionForTest(action.c_str(),
				state,
				applied)) {
			return JsonError("history context action failed");
		}

		std::string json = "{\"ok\":true,\"applied\":";
		json += applied ? "true" : "false";
		json += ",\"state\":\"";
		json += JsonEscape(state.c_str());
		json += "\"}";
		return json;
	}

	if (verb == "history_select") {
		std::string which = NextToken(cmd);
		if (which != "first_insn" && which != "last_insn")
			return JsonError("usage: history_select <first_insn|last_insn>");

		VDStringA state;
		if (!ATUIDebuggerSelectHistoryInstructionForTest(
				which == "last_insn",
				state)) {
			return JsonError("history instruction selection failed");
		}

		std::string json = "{\"ok\":true,\"state\":\"";
		json += JsonEscape(state.c_str());
		json += "\"}";
		return json;
	}

	if (verb == "history_open_context") {
		VDStringA state;
		if (!ATUIDebuggerOpenHistoryContextMenuForTest(state))
			return JsonError("history context menu open failed");

		std::string json = "{\"ok\":true,\"state\":\"";
		json += JsonEscape(state.c_str());
		json += "\"}";
		return json;
	}

	if (verb == "history_hscroll") {
		std::string xText = NextToken(cmd);
		if (xText.empty())
			return JsonError("usage: history_hscroll <pixels>");

		VDStringA state;
		if (!ATUIDebuggerSetHistoryHorizontalScrollForTest(
				(float)atof(xText.c_str()),
				state)) {
			return JsonError("history horizontal scroll failed");
		}

		std::string json = "{\"ok\":true,\"state\":\"";
		json += JsonEscape(state.c_str());
		json += "\"}";
		return json;
	}

	if (verb == "source_missing_file") {
		std::string symbolPath = NextToken(cmd);
		if (symbolPath.empty())
			return JsonError("usage: source_missing_file <symbol_path> [resolved_path]");

		VDStringW openedPathW;
		VDStringW aliasW;
		if (!ATUIDebuggerOpenMissingSourceForTest(
				symbolPath.c_str(),
				RestOfLine(cmd).c_str(),
				openedPathW,
				aliasW)) {
			return JsonError("source missing-file test failed");
		}

		std::string json = "{\"ok\":true,\"opened\":";
		json += openedPathW.empty() ? "false" : "true";
		json += ",\"path\":\"";
		json += JsonEscape(VDTextWToU8(openedPathW).c_str());
		json += "\",\"alias\":\"";
		json += JsonEscape(VDTextWToU8(aliasW).c_str());
		json += "\"}";
		return json;
	}

	if (verb == "source_open_file") {
		std::string path = RestOfLine(cmd);
		if (path.empty())
			return JsonError("usage: source_open_file <utf8_path>");

		uint32 paneId = 0;
		int lineCount = 0;
		VDStringW openedPathW;
		if (!ATUIDebuggerOpenSourceForTest(path.c_str(), paneId, lineCount, openedPathW))
			return JsonError("source open-file test failed");

		std::string json = "{\"ok\":true,\"opened\":";
		json += paneId ? "true" : "false";
		json += ",\"pane_id\":";
		json += std::to_string(paneId);
		json += ",\"line_count\":";
		json += std::to_string(lineCount);
		json += ",\"path\":\"";
		json += JsonEscape(VDTextWToU8(openedPathW).c_str());
		json += "\"}";
		return json;
	}

	if (verb == "source_query_file") {
		std::string path = RestOfLine(cmd);
		if (path.empty())
			return JsonError("usage: source_query_file <utf8_path>");

		uint32 paneId = 0;
		int lineCount = 0;
		int selectedLine = -1;
		VDStringW openedPathW;
		VDStringA firstLine;
		VDStringA lastLine;
		if (!ATUIDebuggerQuerySourceForTest(path.c_str(),
				paneId,
				lineCount,
				selectedLine,
				openedPathW,
				firstLine,
				lastLine)) {
			return JsonError("source query-file test failed");
		}

		std::string json = "{\"ok\":true,\"opened\":";
		json += paneId ? "true" : "false";
		json += ",\"pane_id\":";
		json += std::to_string(paneId);
		json += ",\"line_count\":";
		json += std::to_string(lineCount);
		json += ",\"selected_line\":";
		json += std::to_string(selectedLine);
		json += ",\"path\":\"";
		json += JsonEscape(VDTextWToU8(openedPathW).c_str());
		json += "\",\"first_line\":\"";
		json += JsonEscape(firstLine.c_str());
		json += "\",\"last_line\":\"";
		json += JsonEscape(lastLine.c_str());
		json += "\"}";
		return json;
	}

	if (verb == "source_reload") {
		std::string lineStr = NextToken(cmd);
		std::string path = RestOfLine(cmd);
		if (lineStr.empty() || path.empty())
			return JsonError("usage: source_reload <selected_line|-1> <utf8_path>");

		char *end = nullptr;
		const long lineLong = strtol(lineStr.c_str(), &end, 10);
		if (end == lineStr.c_str() || *end || lineLong < -1)
			return JsonError("bad selected line: " + lineStr);

		uint32 paneId = 0;
		int beforeLineCount = 0;
		int afterLineCount = 0;
		int beforeSelectedLine = -1;
		int afterSelectedLine = -1;
		VDStringA beforeLastLine;
		VDStringA afterLastLine;
		if (!ATUIDebuggerReloadSourceForTest(path.c_str(),
				(int)lineLong,
				paneId,
				beforeLineCount,
				afterLineCount,
				beforeSelectedLine,
				afterSelectedLine,
				beforeLastLine,
				afterLastLine)) {
			return JsonError("source reload test failed");
		}

		std::string json = "{\"ok\":true,\"opened\":";
		json += paneId ? "true" : "false";
		json += ",\"pane_id\":";
		json += std::to_string(paneId);
		json += ",\"before_line_count\":";
		json += std::to_string(beforeLineCount);
		json += ",\"after_line_count\":";
		json += std::to_string(afterLineCount);
		json += ",\"before_selected_line\":";
		json += std::to_string(beforeSelectedLine);
		json += ",\"after_selected_line\":";
		json += std::to_string(afterSelectedLine);
		json += ",\"before_last_line\":\"";
		json += JsonEscape(beforeLastLine.c_str());
		json += "\",\"after_last_line\":\"";
		json += JsonEscape(afterLastLine.c_str());
		json += "\"}";
		return json;
	}

	if (verb == "source_query_mapping") {
		std::string addrStr = NextToken(cmd);
		std::string lineStr = NextToken(cmd);
		std::string path = RestOfLine(cmd);
		if (addrStr.empty() || lineStr.empty() || path.empty())
			return JsonError("usage: source_query_mapping <hex_addr> <line_index> <utf8_path>");

		char *end = nullptr;
		uint32 addr = (uint32)strtoul(addrStr.c_str(), &end, 16);
		if (end == addrStr.c_str() || *end)
			return JsonError("bad hex address: " + addrStr);

		const long lineLong = strtol(lineStr.c_str(), &end, 0);
		if (end == lineStr.c_str() || *end)
			return JsonError("bad line index: " + lineStr);

		int lineForAddress = -1;
		sint32 addressForLine = -1;
		if (!ATUIDebuggerQuerySourceMappingForTest(path.c_str(),
				addr,
				(int)lineLong,
				lineForAddress,
				addressForLine)) {
			return JsonError("source query-mapping test failed");
		}

		char addrBuf[32];
		if (addressForLine >= 0)
			snprintf(addrBuf, sizeof(addrBuf), "\"$%04X\"", (uint32)addressForLine & 0xFFFF);
		else
			snprintf(addrBuf, sizeof(addrBuf), "null");

		std::string json = "{\"ok\":true,\"line_for_address\":";
		json += std::to_string(lineForAddress);
		json += ",\"address_for_line\":";
		json += addrBuf;
		json += "}";
		return json;
	}

	if (verb == "source_query_step_range") {
		std::string pcStr = NextToken(cmd);
		std::string path = RestOfLine(cmd);
		if (pcStr.empty() || path.empty())
			return JsonError("usage: source_query_step_range <hex_pc> <utf8_path>");

		char *end = nullptr;
		uint32 pc = (uint32)strtoul(pcStr.c_str(), &end, 16);
		if (end == pcStr.c_str() || *end)
			return JsonError("bad hex pc: " + pcStr);

		bool hasRange = false;
		uint32 start = 0;
		uint32 length = 0;
		if (!ATUIDebuggerQuerySourceStepRangeForTest(path.c_str(),
				pc,
				hasRange,
				start,
				length)) {
			return JsonError("source query-step-range test failed");
		}

		std::string json = "{\"ok\":true,\"has_range\":";
		json += hasRange ? "true" : "false";
		json += ",\"start\":\"$";
		char buf[32];
		snprintf(buf, sizeof(buf), "%04X", start & 0xFFFF);
		json += buf;
		json += "\",\"length\":";
		json += std::to_string(length);
		json += "}";
		return json;
	}

		if (verb == "source_toggle_breakpoint") {
			std::string lineStr = NextToken(cmd);
			std::string path = RestOfLine(cmd);
		if (lineStr.empty() || path.empty())
			return JsonError("usage: source_toggle_breakpoint <line_index> <utf8_path>");

		char *end = nullptr;
		long lineLong = strtol(lineStr.c_str(), &end, 10);
		if (end == lineStr.c_str() || *end || lineLong < 0)
			return JsonError("bad line index: " + lineStr);

		int before = 0;
		int after = 0;
		sint32 address = -1;
		if (!ATUIDebuggerToggleSourceBreakpointForTest(path.c_str(),
				(int)lineLong,
				before,
				after,
				address)) {
			return JsonError("source toggle-breakpoint test failed");
		}

		char addrBuf[32];
		if (address >= 0)
			snprintf(addrBuf, sizeof(addrBuf), "\"$%04X\"", (uint32)address & 0xFFFF);
		else
			snprintf(addrBuf, sizeof(addrBuf), "null");

		std::string json = "{\"ok\":true,\"before\":";
		json += std::to_string(before);
		json += ",\"after\":";
		json += std::to_string(after);
		json += ",\"address\":";
		json += addrBuf;
			json += "}";
			return json;
		}

		if (verb == "source_command") {
			std::string command = NextToken(cmd);
			std::string pcToken = NextToken(cmd);
			std::string path = RestOfLine(cmd);
			if (command.empty() || pcToken.empty() || path.empty()) {
				return JsonError(
					"usage: source_command <run|step_into|step_over|step_out> <pc|-> <utf8_path>");
			}

			bool pcOverride = false;
			uint32 pcOverrideValue = 0;
			if (pcToken != "-") {
				char *end = nullptr;
				pcOverrideValue = (uint32)strtoul(pcToken.c_str(), &end, 0);
				if (end == pcToken.c_str() || *end)
					return JsonError("bad pc override: " + pcToken);
				pcOverride = true;
			}

			bool handled = false;
			uint32 pc = 0;
			bool hadRange = false;
			uint32 rangeStart = 0;
			uint32 rangeLength = 0;
			bool wasRunning = false;
			bool runningAfterCommand = false;
			bool runningAfterCleanup = false;
			if (!ATUIDebuggerExecuteSourceCommandForTest(path.c_str(),
					command.c_str(),
					pcOverride,
					pcOverrideValue,
					handled,
					pc,
					hadRange,
					rangeStart,
					rangeLength,
					wasRunning,
					runningAfterCommand,
					runningAfterCleanup)) {
				return JsonError("source command test failed");
			}

			char pcBuf[32];
			char rangeStartBuf[32];
			snprintf(pcBuf, sizeof(pcBuf), "$%04X", pc & 0xFFFF);
			snprintf(rangeStartBuf, sizeof(rangeStartBuf), "$%04X", rangeStart & 0xFFFF);

			std::string json = "{\"ok\":true,\"command\":\"";
			json += JsonEscape(command);
			json += "\",\"handled\":";
			json += handled ? "true" : "false";
			json += ",\"pc\":\"";
			json += pcBuf;
			json += "\",\"had_range\":";
			json += hadRange ? "true" : "false";
			json += ",\"range_start\":\"";
			json += rangeStartBuf;
			json += "\",\"range_length\":";
			json += std::to_string(rangeLength);
			json += ",\"was_running\":";
			json += wasRunning ? "true" : "false";
			json += ",\"running_after_command\":";
			json += runningAfterCommand ? "true" : "false";
			json += ",\"running_after_cleanup\":";
			json += runningAfterCleanup ? "true" : "false";
			json += "}";
			return json;
		}

		if (verb == "export_debugger_help") {
		std::string path = RestOfLine(cmd);

		VDStringW outPathW;
		if (!ATUIExportDebugHelpForTest(path.c_str(), outPathW))
			return JsonError("export debugger help failed");

		std::string json = "{\"ok\":true,\"exported\":";
		json += outPathW.empty() ? "false" : "true";
		json += ",\"path\":\"";
		json += JsonEscape(VDTextWToU8(outPathW).c_str());
		json += "\"}";
		return json;
	}

	// --- Dialog control ---
	if (verb == "open_dialog") {
		std::string name = NextToken(cmd);
		bool *field = FindDialogField(state, name);
		if (!field)
			return JsonError("unknown dialog: " + name);
		*field = true;
		return JsonOk();
	}

	if (verb == "close_dialog") {
		std::string name = NextToken(cmd);
		bool *field = FindDialogField(state, name);
		if (!field)
			return JsonError("unknown dialog: " + name);
		*field = false;
		return JsonOk();
	}

	if (verb == "system_config_category") {
		std::string categoryStr = NextToken(cmd);
		if (categoryStr.empty())
			return JsonError("usage: system_config_category <index>");

		state.showSystemConfig = true;
		state.systemConfigCategory = atoi(categoryStr.c_str());
		return JsonOk();
	}

	if (verb == "mobile_hamburger") {
		ATUISetMode(ATUIMode::Gaming);
		ATUIApplyModeStyle(1.0f);
		g_mobileState.gameLoaded = true;
		g_mobileState.currentScreen = ATMobileUIScreen::HamburgerMenu;
		return JsonOk();
	}

	if (verb == "mobile_exit_confirm") {
		ATUISetMode(ATUIMode::Gaming);
		ATUIApplyModeStyle(1.0f);
		ATMobileUI_ShowExitEmulatorConfirm(sim, state, g_mobileState);
		return JsonOk();
	}

	if (verb == "list_dialogs") {
		std::string json = "{\"ok\":true,\"dialogs\":[";
		bool first = true;
		for (auto &m : kDialogMap) {
			if (!first) json += ',';
			json += "\"";
			json += m.name;
			json += "\"";
			first = false;
		}
		json += "]}";
		return json;
	}

	// --- Interactions ---
	if (verb == "send_text") {
		std::string text = RestOfLine(cmd);
		g_syntheticTextEvents.push_back(text);

		SDL_Event ev {};
		ev.type = SDL_EVENT_TEXT_INPUT;
		ev.text.windowID = g_pWindow ? SDL_GetWindowID(g_pWindow) : 0;
		ev.text.text = g_syntheticTextEvents.back().c_str();
		SDL_PushEvent(&ev);
		return JsonOk();
	}

	// click/right_click are fire-and-forget: the action is queued and executes over the
	// next 3 rendered frames (move → press → release).  Use wait_frames
	// after click if you need to wait for the UI to settle before querying.
	if (verb == "click" || verb == "right_click"
		|| verb == "click_label" || verb == "right_click_label") {
		const bool restLabel = (verb == "click_label" || verb == "right_click_label");
		std::string windowFilter = NextToken(cmd);
		std::string label = restLabel ? RestOfLine(cmd) : NextToken(cmd);
		if (label.empty())
			return JsonError(std::string("usage: ") + verb + " <window> <label>");

		const TestItem *item = FindItem(windowFilter, label);
		if (!item)
			return JsonError("item not found: " + label + " in " + windowFilter);

		PendingAction action;
		action.type = PendingActionType::Click;
		action.mouseButton = (verb == "right_click" || verb == "right_click_label") ? 1 : 0;
		action.targetWindow = windowFilter;
		action.targetLabel = label;
		action.targetId = item->id;
		action.clickPos = ImVec2(
			(item->rect.Min.x + item->rect.Max.x) * 0.5f,
			(item->rect.Min.y + item->rect.Max.y) * 0.5f
		);
		action.posResolved = true;
		action.clickPhase = 0;
		g_pendingActions.push_back(action);
		return JsonOk();
	}

	if (verb == "key") {
		std::string keyName = NextToken(cmd);
		PendingAction action;
		action.type = PendingActionType::KeyPress;
		if (!ParseTestKeyChord(keyName, action))
			return JsonError("usage: key <delete|escape|enter|tab|f2|f9|pageup|pagedown|home|end|up|down|left|right|ctrl+f>");

		action.clickPhase = 0;
		g_pendingActions.push_back(action);
		return JsonOk();
	}

	if (verb == "mouse_move") {
		std::string xStr = NextToken(cmd);
		std::string yStr = NextToken(cmd);
		if (xStr.empty() || yStr.empty())
			return JsonError("usage: mouse_move <x> <y>");

		PendingAction action;
		action.type = PendingActionType::MouseMove;
		action.clickPos = ImVec2((float)atof(xStr.c_str()), (float)atof(yStr.c_str()));
		g_pendingActions.push_back(action);
		return JsonOk();
	}

	if (verb == "wait_frames") {
		std::string countStr = NextToken(cmd);
		int count = countStr.empty() ? 1 : std::max(1, atoi(countStr.c_str()));

		PendingAction action;
		action.type = PendingActionType::WaitFrames;
		action.framesRemaining = count;
		g_pendingActions.push_back(action);

		// Block processing of further commands until wait completes.
		// This ensures response ordering is preserved.
		g_commandsBlocked = true;
		return {};  // response sent when frames elapse
	}

	// --- Screenshot ---
	if (verb == "screenshot") {
		std::string path = NextToken(cmd);
		if (path.empty())
			return JsonError("usage: screenshot <path>");

		try {
			VDPixmapBuffer frame;
			if (!ATUICaptureEmulatorFrame(sim, ATUIFrameCaptureMode::Display, frame))
				return JsonError("no emulator frame is available");

			const VDStringW pathW = VDTextU8ToW(path.c_str(), -1);
			ATSaveFrame(frame, pathW.c_str());
		} catch (const MyError& e) {
			return JsonError(e.c_str());
		}

		return "{\"ok\":true,\"path\":\"" + JsonEscape(path) + "\"}";
	}

	// --- Memory inspection (read-only, side-effect-free) ---
	// Reads bytes via ATSimulator::DebugReadByte so I/O reads do not
	// trigger side effects (collision-clear, IRQ ack, etc).  Useful for
	// dynamic analysis of disassembled software: dump zero-page,
	// inspect object arrays, verify hypotheses about register usage.
	//
	// Format:  mem_read <hex_addr> [<count>]
	// Default count = 1.  Max count = 4096 (one response packet).
	// Response: {"ok":true,"addr":"$xxxx","bytes":[h0,h1,...]}
	if (verb == "mem_read") {
		std::string addrStr  = NextToken(cmd);
		std::string countStr = NextToken(cmd);
		if (addrStr.empty())
			return JsonError("usage: mem_read <hex_addr> [<count>]");

		// Reject unparseable input loudly.  A debugging verb that
		// silently falls back to reading $0000 on a typo would hand the
		// caller wrong-but-plausible data — worse than any error.
		char *end = nullptr;
		uint32 addr = (uint32)strtoul(addrStr.c_str(), &end, 16);
		if (end == addrStr.c_str() || *end)
			return JsonError("bad hex address: " + addrStr);

		uint32 count = 1;
		if (!countStr.empty()) {
			count = (uint32)strtoul(countStr.c_str(), &end, 0);
			if (end == countStr.c_str() || *end)
				return JsonError("bad count: " + countStr);
		}
		if (count == 0) count = 1;
		if (count > 4096) count = 4096;
		std::string body = "{\"ok\":true,\"addr\":\"$";
		char buf[32];
		snprintf(buf, sizeof(buf), "%04X", addr & 0xFFFF);
		body += buf;
		body += "\",\"bytes\":[";
		for (uint32 i = 0; i < count; ++i) {
			uint8 b = sim.DebugReadByte((uint16)((addr + i) & 0xFFFF));
			snprintf(buf, sizeof(buf), "%s%u", i ? "," : "", (unsigned)b);
			body += buf;
		}
		body += "]}";
		return body;
	}

	// --- Controller input ---
	//
	// Format:  input joy <unit> <action>
	//          input console <button> [up]
	//
	// joy actions:   left | right | up | down | fire | release_all
	// console btns:  start | select | option
	//                ("up" suffix releases the switch; default = press)
	//
	// <unit> is the input-map controller unit (0 = first/default
	// controller); which console port it drives is decided by the
	// active input map, exactly as for a physical game controller.
	//
	// Joystick state is sticky — a press is held until release_all.
	// Directions on one axis are exclusive: pressing left releases
	// right and vice versa (same for up/down), because a physical
	// stick cannot hold both and ATInputManager does no such
	// exclusion itself.  The caller (test script) owns press/release
	// scheduling so frame timing stays deterministic.
	if (verb == "input") {
		std::string sub = NextToken(cmd);

		if (sub == "joy") {
			std::string unitStr = NextToken(cmd);
			std::string action  = NextToken(cmd);
			if (unitStr.empty() || action.empty())
				return JsonError("usage: input joy <unit> <action>");
			int unit = atoi(unitStr.c_str());
			if (unit < 0 || unit > 3)
				return JsonError("unit must be 0..3");
			ATInputManager *im = sim.GetInputManager();
			if (!im)
				return JsonError("no input manager");

			if (action == "release_all") {
				im->OnButtonUp(unit, kATInputCode_JoyStick1Left);
				im->OnButtonUp(unit, kATInputCode_JoyStick1Right);
				im->OnButtonUp(unit, kATInputCode_JoyStick1Up);
				im->OnButtonUp(unit, kATInputCode_JoyStick1Down);
				im->OnButtonUp(unit, kATInputCode_JoyButton0);
				return JsonOk();
			}

			int code = -1;
			int opposite = -1;
			if (action == "left") {
				code = kATInputCode_JoyStick1Left;
				opposite = kATInputCode_JoyStick1Right;
			} else if (action == "right") {
				code = kATInputCode_JoyStick1Right;
				opposite = kATInputCode_JoyStick1Left;
			} else if (action == "up") {
				code = kATInputCode_JoyStick1Up;
				opposite = kATInputCode_JoyStick1Down;
			} else if (action == "down") {
				code = kATInputCode_JoyStick1Down;
				opposite = kATInputCode_JoyStick1Up;
			} else if (action == "fire") {
				code = kATInputCode_JoyButton0;
			} else
				return JsonError("unknown joy action: " + action);

			// Enforce per-axis exclusivity (see header comment): a
			// physical stick cannot hold left+right or up+down, and
			// some games misbehave on the impossible state.
			if (opposite >= 0)
				im->OnButtonUp(unit, opposite);
			im->OnButtonDown(unit, code);
			return JsonOk();
		}

		if (sub == "console") {
			std::string button = NextToken(cmd);
			std::string state  = NextToken(cmd);   // optional "up"
			if (button.empty())
				return JsonError("usage: input console <start|select|option> [up]");

			uint8 bit = 0;
			if      (button == "start")  bit = 0x01;
			else if (button == "select") bit = 0x02;
			else if (button == "option") bit = 0x04;
			else
				return JsonError("unknown console button: " + button);

			ATGTIAEmulator &gtia = sim.GetGTIA();
			ATNetplayInput::RouteConsoleSwitch(&gtia, bit, state != "up");
			return JsonOk();
		}

		return JsonError(
			"usage: input joy <unit> <left|right|up|down|fire|release_all>"
			" | input console <start|select|option> [up]"
		);
	}

	// --- Speed control ---
	//
	// Format:  set_speed <turbo|warp|normal|real>
	//          get_speed
	//
	// `turbo` / `warp` enable the sticky turbo mode (== Tab key in
	// the GUI) so the simulator runs as fast as the host CPU allows.
	// `normal` / `real` clear it so the simulator runs at its native
	// ~60 Hz NTSC (or 50 Hz PAL) rate.
	//
	// Test-mode defaults to whatever the user's settings.ini specified
	// (typically normal speed); use this verb at the start of a
	// capture session if you need to override.
	//
	// Interaction with wait_frames: wait_frames counts RENDERED frames
	// (decremented in ATTestModePostRender), and the turbo frame-skip
	// renders only 1 of every engine.turbo_fps_divisor (default 16)
	// emulated frames — so `wait_frames N` under turbo spans roughly
	// N*divisor emulated frames.  Scripts that count emulated frames
	// should stay at normal speed or divide accordingly.
	if (verb == "set_speed") {
		std::string mode = NextToken(cmd);
		if (mode.empty())
			return JsonError("usage: set_speed <turbo|warp|normal|real>");
		if (mode == "turbo" || mode == "warp") {
			ATUISetTurbo(true);
			return JsonOk();
		}
		if (mode == "normal" || mode == "real") {
			ATUISetTurbo(false);
			return JsonOk();
		}
		return JsonError("unknown speed mode: " + mode);
	}

	if (verb == "get_speed") {
		std::string body = "{\"ok\":true,\"turbo\":";
		body += ATUIGetTurbo() ? "true" : "false";
		body += "}";
		return body;
	}

	// --- Emulation control ---
	if (verb == "cold_reset") {
		sim.ColdReset();
		sim.Resume();
		return JsonOk();
	}

	if (verb == "warm_reset") {
		sim.WarmReset();
		sim.Resume();
		return JsonOk();
	}

	if (verb == "pause") {
		sim.Pause();
		return JsonOk();
	}

	if (verb == "resume") {
		sim.Resume();
		return JsonOk();
	}

	if (verb == "quit") {
		state.exitConfirmed = true;
		SDL_Event q {};
		q.type = SDL_EVENT_QUIT;
		SDL_PushEvent(&q);
		return JsonOk();
	}

	if (verb == "run_command") {
		std::string command = RestOfLine(cmd);
		if (command.empty())
			return JsonError("usage: run_command <command>");

		ATUIExecuteCommandStringAndShowErrors(command.c_str(), nullptr);
		return JsonOk();
	}

	if (verb == "debugger_console") {
		std::string command = RestOfLine(cmd);
		if (command.empty())
			return JsonError("usage: debugger_console <command>");

		ATConsoleExecuteCommand(command.c_str(), false);
		return JsonOk();
	}

	if (verb == "boot_image") {
		std::string path = RestOfLine(cmd);
		if (path.empty())
			return JsonError("usage: boot_image <path>");
		ATUIPushDeferred(kATDeferred_BootImage, path.c_str());
		return JsonOk();
	}

	if (verb == "attach_disk") {
		std::string driveStr = NextToken(cmd);
		std::string path = RestOfLine(cmd);
		if (path.empty())
			return JsonError("usage: attach_disk <drive_index> <path>");
		int drive = atoi(driveStr.c_str());
		ATUIPushDeferred(kATDeferred_AttachDisk, path.c_str(), drive);
		return JsonOk();
	}

	if (verb == "load_state") {
		std::string path = RestOfLine(cmd);
		if (path.empty())
			return JsonError("usage: load_state <path>");
		ATUIPushDeferred(kATDeferred_LoadState, path.c_str());
		return JsonOk();
	}

	if (verb == "save_state") {
		std::string path = RestOfLine(cmd);
		if (path.empty())
			return JsonError("usage: save_state <path>");
		ATUIPushDeferred(kATDeferred_SaveState, path.c_str());
		return JsonOk();
	}

	// --- Help ---
	if (verb == "help") {
		return "{\"ok\":true,\"commands\":["
			"\"ping\","
			"\"query_state\","
			"\"query_window <title>\","
			"\"query_window_label <title with spaces>\","
			"\"query_command <command>\","
			"\"query_console_input\","
			"\"query_debugger_focus\","
			"\"query_message_box\","
			"\"clear_message_box\","
			"\"debugger_request_file <open|save> [utf8_path]\","
			"\"breakpoint_format_trace <trace_text>\","
			"\"breakpoint_submit [type=0|1|2|3] [location=expr] [condition=1] [condition_text=expr] [stop=0|1] [command=1] [command_text=cmd] [trace=1] [trace_text=text]\","
			"\"breakpoint_describe <id>\","
			"\"breakpoint_format_description [oneshot=1] [clear=1] [trace=1]\","
			"\"breakpoint_delete <id>\","
			"\"breakpoint_select <id>\","
			"\"breakpoint_delete_selected <id>\","
			"\"breakpoint_pane_order\","
			"\"breakpoint_count\","
			"\"watch_interface [expr]\","
			"\"watch_edit <expr>\","
			"\"watch_printable_edit <char> [suffix]\","
			"\"watch_delete_selected\","
			"\"watch_describe\","
			"\"watch_clear_all\","
			"\"watch_count\","
			"\"watch_fill\","
			"\"memory_add_to_watch_expr <addr> [word=1]\","
				"\"memory_track_on_screen <addr> [len=1]\","
				"\"memory_ensure_visible view=<addr> highlight=<addr> [columns=16] [rows=16]\","
				"\"memory_navigation <op> view=<addr> highlight=<addr> [columns=16] [rows=16] [data=0]\","
				"\"memory_hex_edit <addr> <byte>\","
				"\"memory_hex_cancel <addr> <byte>\","
				"\"memory_hex_auto_advance <hex_byte|hex_word> <digits> view=<addr> highlight=<addr> [columns=16] [rows=16]\","
				"\"memory_value_edit <hex_byte|hex_word|dec_byte|dec_word> <addr> <value>\","
				"\"memory_text_edit <atascii|internal> <addr> <char_code>\","
				"\"memory_text_auto_advance <atascii|internal> <char_code> view=<addr> highlight=<addr> [columns=16] [rows=16]\","
				"\"disasm_selected_breakpoint [query|toggle]\","
				"\"disasm_context <go_source>\","
				"\"disasm_target_nav\","
				"\"disasm_preview\","
				"\"disasm_breakpoint_runstop_regression\","
				"\"targets_activate <row>\","
			"\"debug_display <describe|dl|pf|filter|palette> ...\","
			"\"history_interface\","
			"\"history_describe\","
			"\"history_context <go_source|set_origin|reset_origin|mode_beam|mode_us|mode_cycles|mode_unhalted|mode_tape_samples|mode_tape_seconds>\","
			"\"history_select <first_insn|last_insn>\","
			"\"history_open_context\","
			"\"history_hscroll <pixels>\","
			"\"source_missing_file <symbol_path> [resolved_path]\","
			"\"source_open_file <utf8_path>\","
			"\"source_query_file <utf8_path>\","
			"\"source_reload <selected_line|-1> <utf8_path>\","
			"\"source_query_mapping <hex_addr> <line_index> <utf8_path>\","
			"\"source_query_step_range <hex_pc> <utf8_path>\","
			"\"source_toggle_breakpoint <line_index> <utf8_path>\","
			"\"source_command <run|step_into|step_over|step_out> <pc|-> <utf8_path>\","
			"\"export_debugger_help [utf8_path]\","
			"\"list_items [window_filter]\","
			"\"list_dialogs\","
			"\"open_dialog <name>\","
			"\"close_dialog <name>\","
			"\"system_config_category <index>\","
			"\"mobile_hamburger\","
			"\"mobile_exit_confirm\","
			"\"send_text <utf8>\","
			"\"click <window> <label>\","
			"\"click_label <window> <label with spaces>\","
			"\"right_click <window> <label>\","
			"\"right_click_label <window> <label with spaces>\","
			"\"key <delete|escape|enter|tab|f2|f9|pageup|pagedown|home|end|up|down|left|right|ctrl+f>\","
			"\"mouse_move <x> <y>\","
			"\"wait_frames [n]\","
			"\"screenshot <path>\","
			"\"mem_read <hex_addr> [<count>]\","
			"\"input joy <unit> <left|right|up|down|fire|release_all>\","
			"\"input console <start|select|option> [up]\","
			"\"set_speed <turbo|warp|normal|real>\","
			"\"get_speed\","
			"\"cold_reset\","
			"\"warm_reset\","
			"\"pause\","
			"\"resume\","
			"\"quit\","
			"\"run_command <command>\","
			"\"debugger_console <command>\","
			"\"boot_image <path>\","
			"\"attach_disk <drive> <path>\","
			"\"load_state <path>\","
			"\"save_state <path>\","
			"\"help\""
			"]}";
	}

	return JsonError("unknown command: " + verb);
}

// =========================================================================
// Pending action processing — runs during PostRender
// =========================================================================

static void ProcessPendingActions() {
	ImGuiIO &io = ImGui::GetIO();
	bool mouseUsedThisFrame = false;

	for (size_t i = 0; i < g_pendingActions.size(); ) {
		PendingAction &action = g_pendingActions[i];

		if (action.type == PendingActionType::WaitFrames) {
			if (--action.framesRemaining <= 0) {
				SendResponse(JsonOk());
				g_pendingActions.erase(g_pendingActions.begin() + i);
				g_commandsBlocked = false;  // unblock command processing
				continue;
			}
			++i;
			continue;
		}

		if (action.type == PendingActionType::MouseMove) {
			if (mouseUsedThisFrame) {
				++i;
				continue;
			}

			mouseUsedThisFrame = true;
			g_hasMouseOverride = true;
			g_mouseOverride = action.clickPos;
			io.AddMousePosEvent(action.clickPos.x, action.clickPos.y);
			g_pendingActions.erase(g_pendingActions.begin() + i);
			continue;
		}

		if (action.type == PendingActionType::KeyPress) {
			switch (action.clickPhase) {
				case 0:
					if (action.ctrl) {
						io.AddKeyEvent(ImGuiMod_Ctrl, true);
						io.AddKeyEvent(ImGuiKey_LeftCtrl, true);
					}
					if (action.shift) {
						io.AddKeyEvent(ImGuiMod_Shift, true);
						io.AddKeyEvent(ImGuiKey_LeftShift, true);
					}
					if (action.alt) {
						io.AddKeyEvent(ImGuiMod_Alt, true);
						io.AddKeyEvent(ImGuiKey_LeftAlt, true);
					}
					if (action.ctrl || action.shift || action.alt) {
						action.clickPhase = 1;
						++i;
						break;
					}
					io.AddKeyEvent(action.key, true);
					action.clickPhase = 2;
					++i;
					break;
				case 1:
					io.AddKeyEvent(action.key, true);
					action.clickPhase = 2;
					++i;
					break;
				case 2:
					io.AddKeyEvent(action.key, false);
					if (action.alt) {
						io.AddKeyEvent(ImGuiKey_LeftAlt, false);
						io.AddKeyEvent(ImGuiMod_Alt, false);
					}
					if (action.shift) {
						io.AddKeyEvent(ImGuiKey_LeftShift, false);
						io.AddKeyEvent(ImGuiMod_Shift, false);
					}
					if (action.ctrl) {
						io.AddKeyEvent(ImGuiKey_LeftCtrl, false);
						io.AddKeyEvent(ImGuiMod_Ctrl, false);
					}
					g_pendingActions.erase(g_pendingActions.begin() + i);
					break;
				default:
					g_pendingActions.erase(g_pendingActions.begin() + i);
					break;
			}
			continue;
		}

		if (action.type == PendingActionType::Click) {
			// Only one click action may inject mouse events per frame to
			// avoid conflicting position/button state.
			if (mouseUsedThisFrame) {
				++i;
				continue;
			}

			if (!action.posResolved) {
				// Try to find item again (it may have appeared after a frame)
				const TestItem *item = FindItem(action.targetWindow, action.targetLabel);
				if (!item) {
					// Item still not found — give up silently (click was
					// already acknowledged with ok, so we can't error now)
					LOG_INFO("TestMode", "click: item '%s' in '%s' not found, dropping", action.targetLabel.c_str(), action.targetWindow.c_str());
					g_pendingActions.erase(g_pendingActions.begin() + i);
					continue;
				}
				action.clickPos = ImVec2(
					(item->rect.Min.x + item->rect.Max.x) * 0.5f,
					(item->rect.Min.y + item->rect.Max.y) * 0.5f
				);
				action.posResolved = true;
			}

			mouseUsedThisFrame = true;
			switch (action.clickPhase) {
				case 0:
					// Move mouse to target
					io.AddMousePosEvent(action.clickPos.x, action.clickPos.y);
					action.clickPhase = 1;
					++i;
					break;
				case 1:
					// Press
					io.AddMousePosEvent(action.clickPos.x, action.clickPos.y);
					io.AddMouseButtonEvent(action.mouseButton, true);
					action.clickPhase = 2;
					++i;
					break;
				case 2:
					// Release — click complete
					io.AddMousePosEvent(action.clickPos.x, action.clickPos.y);
					io.AddMouseButtonEvent(action.mouseButton, false);
					g_pendingActions.erase(g_pendingActions.begin() + i);
					break;
			}
			continue;
		}

		++i;
	}
}

// =========================================================================
// Frame boundary — clear item registry at start of each frame
// =========================================================================
// We hook into ImGui's NewFrame via a context hook to clear the registry.

static void TestModeNewFrameHook(ImGuiContext *ctx, ImGuiContextHook *hook) {
	if (hook->Type == ImGuiContextHookType_NewFramePre) {
		// clear() keeps allocated capacity — no reallocation on steady-state frames
		g_items.clear();
		return;
	}
}

static bool g_hookRegistered = false;

static void EnsureHookRegistered() {
	if (g_hookRegistered)
		return;

	ImGuiContextHook hook;
	hook.HookId = 0;
	hook.Type = ImGuiContextHookType_NewFramePre;
	hook.Callback = TestModeNewFrameHook;
	hook.UserData = nullptr;
	hook.Owner = 0;
	ImGui::AddContextHook(ImGui::GetCurrentContext(), &hook);
	g_hookRegistered = true;
}

// =========================================================================
// Public API
// =========================================================================

bool ATTestModeInit() {
	if (!g_testModeEnabled)
		return true;

	const char *scriptPath = SDL_getenv("ALTIRRA_TESTMODE_SCRIPT");
	const char *outputPath = SDL_getenv("ALTIRRA_TESTMODE_OUTPUT");
	if (scriptPath && *scriptPath) {
		if (!LoadScriptFile(scriptPath))
			return false;

		if (outputPath && *outputPath) {
			g_scriptOutput = fopen(outputPath, "wb");
			if (!g_scriptOutput) {
				LOG_ERROR("TestMode", "failed to open script output '%s'", outputPath);
				return false;
			}
		}
	}

	if (g_scriptLines.empty()) {
		g_ipcAddress = g_ipc.Init();
		if (g_ipcAddress.empty())
			return false;
	} else {
		g_ipcAddress.clear();
	}

	// Enable ImGui test engine hooks
	ImGuiContext &g = *ImGui::GetCurrentContext();
	g.TestEngineHookItems = true;

	// Register NewFrame hook to clear item registry each frame
	EnsureHookRegistered();

	if (!g_ipcAddress.empty())
		LOG_INFO("TestMode", "Initialized (PID %lu)", (unsigned long)SDL_GetCurrentThreadID());
	else
		LOG_INFO("TestMode", "Initialized with script transport only");
	return true;
}

void ATTestModeShutdown() {
	if (!g_testModeEnabled)
		return;

	// Disable hooks
	ImGuiContext *ctx = ImGui::GetCurrentContext();
	if (ctx)
		ctx->TestEngineHookItems = false;

	g_ipc.Shutdown();
	g_ipcAddress.clear();
	if (g_scriptOutput) {
		fclose(g_scriptOutput);
		g_scriptOutput = nullptr;
	}
	g_scriptLines.clear();
	g_scriptLineNext = 0;
	g_items.clear();
	ResetClientState();
	LOG_INFO("TestMode", "Shutdown");
}

void ATTestModePollCommands(ATSimulator &sim, ATUIState &state) {
	if (!g_testModeEnabled)
		return;

	if (!g_ipcAddress.empty())
		g_ipc.TryAccept();

	if (!g_ipc.HasClient() && g_scriptLineNext >= g_scriptLines.size())
		return;

	// Read available data
	if (g_ipc.HasClient()) {
		char buf[4096];
		int n = g_ipc.Recv(buf, sizeof(buf));
		if (n > 0) {
			g_recvBuf.append(buf, n);
		} else if (n < 0) {
			// Client disconnected or error
			LOG_INFO("TestMode", "Client disconnected");
			g_ipc.DisconnectClient();
			ResetClientState();
			return;
		}
		// n == 0: no data available (would block)
	}

	while (!g_commandsBlocked && g_scriptLineNext < g_scriptLines.size()) {
		std::string line = g_scriptLines[g_scriptLineNext++];
		if (line.empty() || line[0] == '#')
			continue;

		std::string response = DispatchCommand(line, sim, state);
		if (!response.empty())
			SendResponse(response);
	}

	// Process complete lines — stop if a blocking command (wait_frames) is active
	size_t pos;
	while (!g_commandsBlocked && (pos = g_recvBuf.find('\n')) != std::string::npos) {
		std::string line = g_recvBuf.substr(0, pos);
		g_recvBuf.erase(0, pos + 1);

		// Strip \r if present
		if (!line.empty() && line.back() == '\r')
			line.pop_back();

		if (line.empty())
			continue;

		std::string response = DispatchCommand(line, sim, state);
		if (!response.empty())
			SendResponse(response);
		// Empty response means it's a blocking command (wait_frames)
	}

	FlushSendBuffer();
}

void ATTestModePostRender(ATSimulator &sim, ATUIState &state) {
	if (!g_testModeEnabled)
		return;

	ProcessPendingActions();
	FlushSendBuffer();
}

bool ATTestModeGetMousePosOverride(ImVec2& pos) {
	if (!g_testModeEnabled || !g_hasMouseOverride)
		return false;

	pos = g_mouseOverride;
	return true;
}

// =========================================================================
// ImGui Test Engine Hook Implementations
// =========================================================================
// These are called by ImGui's internal widget code when IMGUI_ENABLE_TEST_ENGINE
// is defined and g.TestEngineHookItems is true.

void ImGuiTestEngineHook_ItemAdd(ImGuiContext *ctx, ImGuiID id, const ImRect &bb, const ImGuiLastItemData *item_data) {
	// ItemAdd is called for every widget — we just record the bounding box.
	// The label comes later via ItemInfo.  We store a placeholder entry.
	if (!g_testModeEnabled || id == 0)
		return;

	TestItem item;
	item.id = id;
	item.rect = bb;
	item.windowName = GetCurrentWindowName(ctx);
	item.flags = item_data ? item_data->StatusFlags : ImGuiItemStatusFlags_None;
	item.itemFlags = item_data ? item_data->ItemFlags : ImGuiItemFlags_None;
	g_items.push_back(std::move(item));
}

void ImGuiTestEngineHook_ItemInfo(ImGuiContext *ctx, ImGuiID id, const char *label, ImGuiItemStatusFlags flags) {
	// Called after ItemAdd with the widget's label and semantic flags.
	// Walk backwards to find the matching item and fill in label + flags.
	if (!g_testModeEnabled || !label || !label[0])
		return;

	for (auto it = g_items.rbegin(); it != g_items.rend(); ++it) {
		if (it->id == id) {
			it->label = label;
			it->flags = flags;
			return;
		}
	}
}

void ImGuiTestEngineHook_Log(ImGuiContext *ctx, const char *fmt, ...) {
	if (!g_testModeEnabled)
		return;

	char buf[1024];
	va_list args;
	va_start(args, fmt);
	vsnprintf(buf, sizeof buf, fmt, args);
	va_end(args);
	LOG_INFO("TestMode", "%s", buf);
}

const char* ImGuiTestEngine_FindItemDebugLabel(ImGuiContext *ctx, ImGuiID id) {
	if (!g_testModeEnabled)
		return nullptr;

	for (auto &item : g_items) {
		if (item.id == id && !item.label.empty())
			return item.label.c_str();
	}
	return nullptr;
}
