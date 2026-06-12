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
#include <mutex>
#include <algorithm>
#include <vd2/system/error.h>
#include <vd2/system/text.h>

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
#include "logging.h"

bool g_testModeEnabled = false;
extern ATMobileUIState g_mobileState;

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
	WaitFrames,     // wait N frames before responding
};

struct PendingAction {
	PendingActionType type = PendingActionType::None;
	ImGuiID targetId = 0;           // item to click (looked up from registry)
	std::string targetWindow;       // window name filter
	std::string targetLabel;        // label filter
	ImVec2 clickPos = {0, 0};       // resolved screen position
	int framesRemaining = 0;        // for WaitFrames
	bool posResolved = false;       // true once we found the item and set clickPos
	int clickPhase = 0;             // 0=move, 1=press, 2=release+done
};

static std::vector<PendingAction> g_pendingActions;
static bool g_commandsBlocked = false;  // true while wait_frames is pending

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

	if (verb == "query_window") {
		std::string title = NextToken(cmd);
		if (title.empty())
			return JsonError("usage: query_window <title>");

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
	// click is fire-and-forget: the action is queued and executes over the
	// next 3 rendered frames (move → press → release).  Use wait_frames
	// after click if you need to wait for the UI to settle before querying.
	if (verb == "click") {
		std::string windowFilter = NextToken(cmd);
		std::string label = NextToken(cmd);
		if (label.empty())
			return JsonError("usage: click <window> <label>");

		const TestItem *item = FindItem(windowFilter, label);
		if (!item)
			return JsonError("item not found: " + label + " in " + windowFilter);

		PendingAction action;
		action.type = PendingActionType::Click;
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
			"\"list_items [window_filter]\","
			"\"list_dialogs\","
			"\"open_dialog <name>\","
			"\"close_dialog <name>\","
			"\"system_config_category <index>\","
			"\"mobile_hamburger\","
			"\"mobile_exit_confirm\","
			"\"click <window> <label>\","
			"\"mouse_move <x> <y>\","
			"\"wait_frames [n]\","
			"\"screenshot <path>\","
			"\"mem_read <hex_addr> [<count>]\","
			"\"cold_reset\","
			"\"warm_reset\","
			"\"pause\","
			"\"resume\","
			"\"quit\","
			"\"run_command <command>\","
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
					io.AddMouseButtonEvent(0, true);
					action.clickPhase = 2;
					++i;
					break;
				case 2:
					// Release — click complete
					io.AddMousePosEvent(action.clickPos.x, action.clickPos.y);
					io.AddMouseButtonEvent(0, false);
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
	// clear() keeps allocated capacity — no reallocation on steady-state frames
	g_items.clear();
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
