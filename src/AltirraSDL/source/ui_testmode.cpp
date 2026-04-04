//	AltirraSDL - UI Test Automation Framework
//	Implementation of Unix domain socket IPC, ImGui test engine hooks,
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

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include "ui_testmode.h"
#include "ui_main.h"
#include "simulator.h"
#include "gtia.h"

bool g_testModeEnabled = false;

// =========================================================================
// Socket IPC
// =========================================================================

static int g_listenFd = -1;
static int g_clientFd = -1;
static std::string g_sockPath;
static std::string g_recvBuf;      // accumulates partial reads
static std::string g_sendBuf;      // accumulates responses to flush

static bool CreateListenSocket() {
	g_sockPath = "/tmp/altirra-test-" + std::to_string(getpid()) + ".sock";

	// Remove stale socket file if it exists
	unlink(g_sockPath.c_str());

	g_listenFd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (g_listenFd < 0) {
		fprintf(stderr, "[TestMode] socket() failed: %s\n", strerror(errno));
		return false;
	}

	struct sockaddr_un addr;
	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, g_sockPath.c_str(), sizeof(addr.sun_path) - 1);

	if (bind(g_listenFd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
		fprintf(stderr, "[TestMode] bind(%s) failed: %s\n", g_sockPath.c_str(), strerror(errno));
		close(g_listenFd);
		g_listenFd = -1;
		return false;
	}

	if (listen(g_listenFd, 1) < 0) {
		fprintf(stderr, "[TestMode] listen() failed: %s\n", strerror(errno));
		close(g_listenFd);
		g_listenFd = -1;
		unlink(g_sockPath.c_str());
		return false;
	}

	// Non-blocking accept
	fcntl(g_listenFd, F_SETFL, O_NONBLOCK);

	fprintf(stderr, "[TestMode] Listening on %s\n", g_sockPath.c_str());
	return true;
}

static void DestroySocket() {
	if (g_clientFd >= 0) { close(g_clientFd); g_clientFd = -1; }
	if (g_listenFd >= 0) { close(g_listenFd); g_listenFd = -1; }
	if (!g_sockPath.empty()) { unlink(g_sockPath.c_str()); g_sockPath.clear(); }
}

static void TryAcceptClient() {
	if (g_clientFd >= 0 || g_listenFd < 0)
		return;

	int fd = accept(g_listenFd, nullptr, nullptr);
	if (fd < 0)
		return;

	fcntl(fd, F_SETFL, O_NONBLOCK);
	g_clientFd = fd;
	g_recvBuf.clear();
	g_sendBuf.clear();
	fprintf(stderr, "[TestMode] Client connected\n");
}

static void SendResponse(const std::string &json) {
	if (g_clientFd < 0)
		return;
	g_sendBuf += json;
	g_sendBuf += '\n';
}

// Forward declarations for cleanup (defined further down)
static void ResetClientState();

static void FlushSendBuffer() {
	if (g_clientFd < 0 || g_sendBuf.empty())
		return;

	ssize_t sent = send(g_clientFd, g_sendBuf.data(), g_sendBuf.size(), MSG_NOSIGNAL);
	if (sent < 0) {
		if (errno == EAGAIN || errno == EWOULDBLOCK)
			return;  // try again next frame
		fprintf(stderr, "[TestMode] Client disconnected (send error)\n");
		close(g_clientFd);
		g_clientFd = -1;
		ResetClientState();
		return;
	}
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
	g_commandsBlocked = false;
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

		// Use existing save frame mechanism
		extern std::mutex g_saveFrameMutex;
		extern VDStringA g_saveFramePath;
		{
			std::lock_guard<std::mutex> lock(g_saveFrameMutex);
			g_saveFramePath = path.c_str();
		}
		return "{\"ok\":true,\"path\":\"" + JsonEscape(path) + "\"}";
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
			"\"click <window> <label>\","
			"\"wait_frames [n]\","
			"\"screenshot <path>\","
			"\"cold_reset\","
			"\"warm_reset\","
			"\"pause\","
			"\"resume\","
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
					fprintf(stderr, "[TestMode] click: item '%s' in '%s' not found, dropping\n",
						action.targetLabel.c_str(), action.targetWindow.c_str());
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

	if (!CreateListenSocket())
		return false;

	// Enable ImGui test engine hooks
	ImGuiContext &g = *ImGui::GetCurrentContext();
	g.TestEngineHookItems = true;

	// Register NewFrame hook to clear item registry each frame
	EnsureHookRegistered();

	fprintf(stderr, "[TestMode] Initialized (PID %d)\n", getpid());
	return true;
}

void ATTestModeShutdown() {
	if (!g_testModeEnabled)
		return;

	// Disable hooks
	ImGuiContext *ctx = ImGui::GetCurrentContext();
	if (ctx)
		ctx->TestEngineHookItems = false;

	DestroySocket();
	g_items.clear();
	ResetClientState();
	fprintf(stderr, "[TestMode] Shutdown\n");
}

void ATTestModePollCommands(ATSimulator &sim, ATUIState &state) {
	if (!g_testModeEnabled)
		return;

	TryAcceptClient();

	if (g_clientFd < 0)
		return;

	// Read available data
	char buf[4096];
	ssize_t n = recv(g_clientFd, buf, sizeof(buf), 0);
	if (n > 0) {
		g_recvBuf.append(buf, n);
	} else if (n == 0) {
		// Client disconnected
		fprintf(stderr, "[TestMode] Client disconnected\n");
		close(g_clientFd);
		g_clientFd = -1;
		ResetClientState();
		return;
	}
	// n < 0 with EAGAIN/EWOULDBLOCK is fine — no data available

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

	va_list args;
	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);
	fprintf(stderr, "\n");
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

