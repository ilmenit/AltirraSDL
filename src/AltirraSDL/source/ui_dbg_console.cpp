//	AltirraSDL - Dear ImGui debugger console pane
//	Replaces Win32 ATConsoleWindow (uidbgconsole.cpp) with an ImGui window.
//	Provides command input, output text buffer, and command history.

#include <stdafx.h>
#include <mutex>
#include <string>
#include <vector>
#include <stdio.h>
#include <stdarg.h>
#include <imgui.h>

#include <vd2/system/vdtypes.h>
#include <vd2/system/VDString.h>
#include <vd2/system/function.h>
#include "ui_debugger.h"
#include "console.h"
#include "debugger.h"
#include "simulator.h"

extern ATSimulator g_sim;

// Forward declaration from debugger.cpp — commands must be queued,
// not executed directly, because Tick() only processes them when the
// simulator is stopped.
void ATConsoleQueueCommand(const char *s);

// =========================================================================
// Console text buffer — thread-safe append, ImGui rendering
// =========================================================================

// Console text buffer — extern so console_stubs.cpp can append before pane exists
std::mutex g_consoleMutex;
std::string g_consoleText;
bool g_consoleScrollToBottom = false;
bool g_consoleNeedsScroll = false;

// Command history
static std::vector<std::string> g_cmdHistory;
static int g_cmdHistoryPos = -1;  // -1 = new line, 0..N = browsing history

// Forward
class ATImGuiConsolePaneImpl;
static ATImGuiConsolePaneImpl *g_pConsolePane = nullptr;

// Console output is routed through console_stubs.cpp → g_pConsoleWindow → Write()
// or directly into the shared buffer (g_consoleText) before the pane exists.

// =========================================================================
// Console pane implementation
// =========================================================================

class ATImGuiConsolePaneImpl final : public ATImGuiDebuggerPane,
                                      public IATUIDebuggerConsoleWindow {
public:
	ATImGuiConsolePaneImpl();
	~ATImGuiConsolePaneImpl() override;

	// IATUIDebuggerConsoleWindow
	void Write(const char *s) override;
	void ShowEnd() override;

	// ATImGuiDebuggerPane
	bool Render() override;

	// IATDebuggerClient
	void OnDebuggerSystemStateUpdate(const ATDebuggerSystemState& state) override;

private:
	static int InputTextCallback(ImGuiInputTextCallbackData *data);
	int HandleInputCallback(ImGuiInputTextCallbackData *data);

	void OnPromptChanged(IATDebugger *target, const char *prompt);
	void OnRunStateChanged(IATDebugger *target, bool running);

	char mInputBuf[512] = {};
	bool mbFocusInput = true;
	bool mbFirstRender = true;
	bool mbRunning = false;  // tracks simulator run state for input disabling

	// Event delegates for prompt/run state changes
	VDDelegate mDelPromptChanged;
	VDDelegate mDelRunStateChanged;
};

// Global console window pointer (used by debugger engine via g_pConsoleWindow)
IATUIDebuggerConsoleWindow *g_pConsoleWindow = nullptr;

ATImGuiConsolePaneImpl::ATImGuiConsolePaneImpl()
	: ATImGuiDebuggerPane(kATUIPaneId_Console, "Console")
{
	g_pConsolePane = this;
	g_pConsoleWindow = this;

	// Subscribe to prompt/run state change events (like Windows uidbgconsole.cpp)
	IATDebugger *dbg = ATGetDebugger();
	if (dbg) {
		dbg->OnPromptChanged() += mDelPromptChanged.Bind(this, &ATImGuiConsolePaneImpl::OnPromptChanged);
		dbg->OnRunStateChanged() += mDelRunStateChanged.Bind(this, &ATImGuiConsolePaneImpl::OnRunStateChanged);
		mbRunning = dbg->IsRunning();
	}
}

ATImGuiConsolePaneImpl::~ATImGuiConsolePaneImpl() {
	// Unsubscribe from events
	IATDebugger *dbg = ATGetDebugger();
	if (dbg) {
		dbg->OnPromptChanged() -= mDelPromptChanged;
		dbg->OnRunStateChanged() -= mDelRunStateChanged;
	}

	if (g_pConsolePane == this)
		g_pConsolePane = nullptr;
	if (g_pConsoleWindow == this)
		g_pConsoleWindow = nullptr;
}

void ATImGuiConsolePaneImpl::Write(const char *s) {
	std::lock_guard<std::mutex> lock(g_consoleMutex);
	g_consoleText.append(s);
	g_consoleNeedsScroll = true;
}

void ATImGuiConsolePaneImpl::ShowEnd() {
	g_consoleScrollToBottom = true;
}

void ATImGuiConsolePaneImpl::OnDebuggerSystemStateUpdate(const ATDebuggerSystemState& state) {
	ATImGuiDebuggerPane::OnDebuggerSystemStateUpdate(state);
}

void ATImGuiConsolePaneImpl::OnPromptChanged(IATDebugger *target, const char *prompt) {
	// Prompt is read dynamically in Render(), nothing to cache
}

void ATImGuiConsolePaneImpl::OnRunStateChanged(IATDebugger *target, bool running) {
	mbRunning = running;
	if (!running) {
		// Debugger just broke — auto-focus the console input
		mbFocusInput = true;
	}
}

bool ATImGuiConsolePaneImpl::Render() {
	bool open = true;

	if (mbFocusRequested) {
		ImGui::SetNextWindowFocus();
		mbFocusRequested = false;
		mbFocusInput = true;
	}

	if (!ImGui::Begin(mTitle.c_str(), &open)) {
		ImGui::End();
		return open;
	}

	// Output area: scrollable child region
	const float footerHeight = ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeightWithSpacing();
	if (ImGui::BeginChild("ConsoleOutput", ImVec2(0, -footerHeight), ImGuiChildFlags_None,
			ImGuiWindowFlags_HorizontalScrollbar)) {
		// Render all text
		{
			std::lock_guard<std::mutex> lock(g_consoleMutex);
			if (!g_consoleText.empty())
				ImGui::TextUnformatted(g_consoleText.c_str(), g_consoleText.c_str() + g_consoleText.size());
		}

		// Auto-scroll
		if (g_consoleNeedsScroll || g_consoleScrollToBottom) {
			if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 20.0f || g_consoleScrollToBottom)
				ImGui::SetScrollHereY(1.0f);
			g_consoleNeedsScroll = false;
			g_consoleScrollToBottom = false;
		}
	}
	ImGui::EndChild();

	// Input line
	ImGui::Separator();

	// Show prompt with ">" suffix (matches Windows: "Altirra>")
	IATDebugger *dbg = ATGetDebugger();
	const char *prompt = dbg ? dbg->GetPrompt() : "Altirra";
	ImGui::Text("%s>", prompt);
	ImGui::SameLine();

	// Disable input when simulator is running (matches Windows behavior)
	if (mbRunning)
		ImGui::BeginDisabled();

	ImGuiInputTextFlags inputFlags = ImGuiInputTextFlags_EnterReturnsTrue
		| ImGuiInputTextFlags_CallbackHistory;

	ImGui::PushItemWidth(-1);
	if (ImGui::InputText("##input", mInputBuf, sizeof(mInputBuf), inputFlags,
			InputTextCallback, this)) {
		// Enter pressed — queue command for execution in Tick()
		if (mInputBuf[0]) {
			// Add to history
			g_cmdHistory.push_back(mInputBuf);
			g_cmdHistoryPos = -1;

			// Queue (Tick processes when simulator is stopped)
			ATConsoleQueueCommand(mInputBuf);
			mInputBuf[0] = '\0';
		} else {
			// Empty enter = repeat last command
			ATConsoleQueueCommand("");
		}
		mbFocusInput = true;
	}
	ImGui::PopItemWidth();

	if (mbRunning)
		ImGui::EndDisabled();

	// Auto-focus input on first render or after break
	if (mbFocusInput || mbFirstRender) {
		if (!mbRunning) {
			ImGui::SetKeyboardFocusHere(-1);
			mbFocusInput = false;
		}
		mbFirstRender = false;
	}

	ImGui::End();
	return open;
}

int ATImGuiConsolePaneImpl::InputTextCallback(ImGuiInputTextCallbackData *data) {
	ATImGuiConsolePaneImpl *self = (ATImGuiConsolePaneImpl *)data->UserData;
	return self->HandleInputCallback(data);
}

int ATImGuiConsolePaneImpl::HandleInputCallback(ImGuiInputTextCallbackData *data) {
	if (data->EventFlag == ImGuiInputTextFlags_CallbackHistory) {
		const int prevPos = g_cmdHistoryPos;
		if (data->EventKey == ImGuiKey_UpArrow) {
			if (g_cmdHistoryPos == -1)
				g_cmdHistoryPos = (int)g_cmdHistory.size() - 1;
			else if (g_cmdHistoryPos > 0)
				g_cmdHistoryPos--;
		} else if (data->EventKey == ImGuiKey_DownArrow) {
			if (g_cmdHistoryPos >= 0) {
				g_cmdHistoryPos++;
				if (g_cmdHistoryPos >= (int)g_cmdHistory.size())
					g_cmdHistoryPos = -1;
			}
		}

		if (prevPos != g_cmdHistoryPos) {
			if (g_cmdHistoryPos >= 0 && g_cmdHistoryPos < (int)g_cmdHistory.size()) {
				data->DeleteChars(0, data->BufTextLen);
				data->InsertChars(0, g_cmdHistory[g_cmdHistoryPos].c_str());
			} else {
				data->DeleteChars(0, data->BufTextLen);
			}
		}
	}
	return 0;
}

// =========================================================================
// Console creation / management
// =========================================================================

void ATUIDebuggerEnsureConsolePane() {
	if (!g_pConsolePane) {
		auto *pane = new ATImGuiConsolePaneImpl();
		ATUIDebuggerRegisterPane(pane);
		// vdrefcounted starts at refcount 0; the vdrefptr in the registry
		// AddRef'd to 1.  No Release() here — that would destroy the object.
	}
}

// Console output functions (ATConsoleWrite, ATConsolePrintfImpl, etc.) are
// implemented in console_stubs.cpp.  They route to g_pConsoleWindow->Write()
// when the console pane is open, or to the shared g_consoleText buffer.
