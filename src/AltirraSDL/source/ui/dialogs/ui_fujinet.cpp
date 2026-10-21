// Authorized fork extension: upstream Windows Altirra has no FujiNet panel.
// The native device is a UDP listener; FujiNet initiates the connection.
#include <stdafx.h>
#include "ui_fujinet.h"
#include "ui_main.h"
#include "../../media/fujinet_pc.h"
#include "../../media/media_url.h"
#include "../../app/ui_file_dialog_sdl3.h"
#include <SDL3/SDL.h>
#include <imgui.h>
#include <vd2/system/registry.h>
#include <vd2/system/error.h>
#include <vd2/system/refcount.h>
#include <vd2/system/text.h>
#include <at/atcore/propertyset.h>
#include "simulator.h"
#include "devicemanager.h"
#include "disk.h"
#include "diskinterface.h"
#include "settings.h"
#include "uiaccessors.h"
#include "netsio.h"
#ifdef ALTIRRA_NETPLAY_ENABLED
#include "netplay/netplay_glue.h"
#endif
#include <algorithm>
#include <cstdio>
#include <mutex>
#include <string>

extern ATSimulator g_sim;
extern SDL_Window* g_pWindow;
extern VDStringA ATGetConfigDir();
extern void ATRegistryFlushToDisk();

namespace {
ATFujiNetPC s_runtime;
bool s_open = false;
bool s_loaded = false;
bool s_lan = false;
int s_port = 9997;
int s_timeout = 100;
int s_webPort = 8000;
char s_executable[4096] = {};
char s_webURL[1024] = {};
std::string s_error;
bool s_bootPending = false;
uint64_t s_bootDeadline = 0;
std::string s_observedSettings;
std::string s_runtimeEndpoint;
std::mutex s_pickerMutex;
std::string s_pickedPath;

bool Locked() {
#ifdef ALTIRRA_NETPLAY_ENABLED
	return ATNetplayGlue::IsSessionEngaged();
#else
	return false;
#endif
}
bool CanChange() {
	if (!Locked()) return true;
	s_error = "FujiNet configuration and boot are unavailable during an active netplay session.";
	return false;
}
std::string StateDirectory() {
	const auto directory = ATGetConfigDir();
	return directory.empty() ? std::string() : std::string(directory.c_str()) + "/FujiNet";
}
IATDevice* Device() {
	return g_sim.GetDeviceManager()->GetDeviceByTag("netsio");
}
IATDeviceNetSIOControl* Control() {
	auto* device = Device();
	return vdpoly_cast<IATDeviceNetSIOControl*>(device);
}
void Load() {
	if (s_loaded) return;
	VDRegistryAppKey key("FujiNet", false);
	VDStringW executable, web;
	key.getString("RuntimeExecutable", executable);
	key.getString("WebURL", web);
	snprintf(s_executable, sizeof s_executable, "%s", VDTextWToU8(executable).c_str());
	snprintf(s_webURL, sizeof s_webURL, "%s", VDTextWToU8(web).c_str());
	s_webPort = std::clamp(key.getInt("WebPort", 8000), 1, 65535);
	s_loaded = true;
}
void Save() {
	if (!s_loaded) return;
	VDRegistryAppKey key("FujiNet", true);
	key.setString("RuntimeExecutable", VDTextU8ToW(VDStringSpanA(s_executable)).c_str());
	key.setString("WebURL", VDTextU8ToW(VDStringSpanA(s_webURL)).c_str());
	key.setInt("WebPort", s_webPort);
#ifdef __ANDROID__
	ATRegistryFlushToDisk();
#endif
}
void ReadDeviceSettings() {
	if (auto* device = Device()) {
		ATPropertySet properties;
		device->GetSettings(properties);
		s_lan = !wcscmp(properties.GetString("bind_address", L"127.0.0.1"), L"0.0.0.0");
		s_port = (int)properties.GetUint32("port", 9997);
		s_timeout = (int)properties.GetUint32("sync_timeout_ms", 100);
	} else {
		s_lan = false;
		s_port = 9997;
		s_timeout = 100;
	}
}
std::string ListenerEndpoint() {
	if (auto* device = Device()) {
		ATPropertySet properties;
		device->GetSettings(properties);
		return std::string(VDTextWToU8(VDStringSpanW(
			properties.GetString("bind_address", L"127.0.0.1"))).c_str()) + ":" +
			std::to_string(properties.GetUint32("port", 9997));
	}
	return "none";
}
void SyncDeviceDraft() {
	const auto endpoint = ListenerEndpoint();
	ATPropertySet properties;
	if (auto* device = Device()) device->GetSettings(properties);
	const std::string settings = std::to_string(ATSettingsGetCurrentProfileId()) +
		"|" + endpoint + "|" + std::to_string(properties.GetUint32("sync_timeout_ms", 100));
	if (settings != s_observedSettings) {
		s_observedSettings = settings;
		ReadDeviceSettings();
	}
}
void PickCallback(void*, const char* const* files, int) {
	if (files && files[0]) {
		std::lock_guard<std::mutex> lock(s_pickerMutex);
		s_pickedPath = files[0];
	}
}
void Browse() {
	static const SDL_DialogFileFilter filters[] = {
#ifdef _WIN32
		{ "FujiNet-PC executable", "exe" },
#else
		{ "FujiNet-PC executable", "*" },
#endif
	};
	ATUIShowOpenFileDialog('fnpc', PickCallback, nullptr, g_pWindow,
		filters, 1, false);
}
bool RequireControl(IATDeviceNetSIOControl*& control) {
	if (!CanChange()) return false;
	control = Control();
	if (control) return true;
	s_error = "Connect the NetSIO listener first.";
	return false;
}
IATDeviceNetSIOControl::Status ListenerReady(IATDeviceNetSIOControl& control) {
	// Socket binding completes on the native worker, independently of the
	// emulation dispatcher. Bound this short configuration wait; a peer
	// connection is a separate asynchronous state handled by normal Poll.
	const auto deadline = SDL_GetTicks() + 100;
	auto status = control.GetNetSIOStatus();
	while (!status.listening && status.lastError.empty() && SDL_GetTicks() < deadline) {
		SDL_Delay(1);
		status = control.GetNetSIOStatus();
	}
	return status;
}
void FieldLabel(const char* label) {
	ImGui::TextWrapped("%s", label);
	ImGui::SetNextItemWidth(-1);
}
void SameLineIfFits(const char* label) {
	const auto& style = ImGui::GetStyle();
	const float used = ImGui::GetItemRectMax().x - ImGui::GetCursorScreenPos().x;
	const float width = ImGui::CalcTextSize(label).x + style.FramePadding.x * 2;
	if (used + style.ItemSpacing.x + width <= ImGui::GetContentRegionAvail().x)
		ImGui::SameLine();
}
bool ActionButton(const char* label) {
	const float width = ImGui::GetContentRegionAvail().x
		- ImGui::GetStyle().FramePadding.x * 2;
	if (ImGui::CalcTextSize(label).x <= width)
		return ImGui::Button(label);
	// Large fonts and narrow canvases wrap button captions at word
	// boundaries instead of clipping them.
	std::string wrapped, line;
	for (const char* word = label; *word;) {
		const char* end = strchr(word, ' ');
		if (!end) end = word + strlen(word);
		std::string next(word, end);
		const std::string candidate = line.empty() ? next : line + " " + next;
		if (!line.empty() && ImGui::CalcTextSize(candidate.c_str()).x > width) {
			wrapped += line + '\n';
			line = std::move(next);
		} else {
			line = candidate;
		}
		word = *end ? end + 1 : end;
	}
	wrapped += line + "###" + label;
	return ImGui::Button(wrapped.c_str());
}
} // namespace

void ATUIShowFujiNetDialog() {
	Load();
	ReadDeviceSettings();
	s_open = true;
}
bool ATUIFujiNetSetRuntime(const char* executable, unsigned webPort) {
	Load();
	if (!CanChange()) return false;
	if (s_runtime.GetStatus().running) {
		s_error = "Stop the managed runtime before changing its executable or web port.";
		return false;
	}
	if (!executable || strlen(executable) >= sizeof s_executable || !webPort || webPort > 65535) {
		s_error = "Choose a valid runtime path and web port (1–65535).";
		return false;
	}
	snprintf(s_executable, sizeof s_executable, "%s", executable);
	s_webPort = (int)webPort;
	s_error.clear();
	Save();
	return true;
}
bool ATUIFujiNetConfigureListener(bool allowLAN, unsigned port, unsigned timeout) {
	Load();
	if (!CanChange()) return false;
	if (s_runtime.GetStatus().running) {
		s_error = "Stop the managed runtime before changing the NetSIO listener.";
		return false;
	}
#ifdef __EMSCRIPTEN__
	s_error = "The browser cannot listen for native UDP NetSIO packets. Use a native build.";
	return false;
#else
	if (!port || port > 65535 || timeout < 10 || timeout > 1000) {
		s_error = "UDP port must be 1–65535 and synchronization timeout 10–1000 ms.";
		return false;
	}
	try {
		ATPropertySet properties;
		ATPropertySet previous;
		auto* device = Device();
		auto* previousControl = Control();
		const bool wasListening = previousControl &&
			previousControl->GetNetSIOStatus().listening;
		if (device) {
			device->GetSettings(previous);
			device->GetSettings(properties);
		}
		properties.SetString("bind_address", allowLAN ? L"0.0.0.0" : L"127.0.0.1");
		properties.SetUint32("port", port);
		properties.SetUint32("sync_timeout_ms", timeout);
		if (device) {
			const bool endpointChanged = port != previous.GetUint32("port", 9997) ||
				wcscmp(previous.GetString("bind_address", L"127.0.0.1"),
					allowLAN ? L"0.0.0.0" : L"127.0.0.1");
			if (previousControl && endpointChanged) previousControl->Disconnect();
			if (!device->SetSettings(properties)) {
				device->SetSettings(previous);
				if (wasListening) {
					previousControl->Reconnect();
					ListenerReady(*previousControl);
				}
				ReadDeviceSettings();
				s_error = "The NetSIO device could not apply these settings.";
				return false;
			}
		} else {
			g_sim.GetDeviceManager()->AddDevice("netsio", properties, false);
		}
		if (auto* control = Control()) {
			control->Reconnect();
			const auto status = ListenerReady(*control);
			if (!status.listening) {
				s_error = VDTextWToU8(status.lastError).c_str();
				if (s_error.empty()) s_error = "The NetSIO UDP listener could not start.";
				if (device) {
					control->Disconnect();
					device->SetSettings(previous);
					if (wasListening) {
						control->Reconnect();
						ListenerReady(*control);
					}
					ReadDeviceSettings();
				}
				return false;
			}
		} else {
			s_error = "The native NetSIO device is unavailable in this build.";
			return false;
		}
		s_lan = allowLAN;
		s_port = (int)port;
		s_timeout = (int)timeout;
		s_error.clear();
		return true;
	} catch (const MyError& error) {
		s_error = error.c_str();
		return false;
	}
#endif
}
bool ATUIFujiNetStartRuntime() {
	Load();
	if (!CanChange()) return false;
	SyncDeviceDraft();
	if (!ATFujiNetPC::Available()) {
		s_error = "This platform connects to an external FujiNet; local process management is unavailable.";
		return false;
	}
	if (auto* device = Device()) {
		ATPropertySet properties;
		device->GetSettings(properties);
		const auto* bind = properties.GetString("bind_address", L"127.0.0.1");
		if (wcscmp(bind, L"127.0.0.1") && wcscmp(bind, L"0.0.0.0")) {
			s_error = "A local managed runtime needs a loopback-accessible listener. Apply 127.0.0.1 or all IPv4 interfaces first.";
			return false;
		}
	}
	if (s_runtime.GetStatus().running) {
		s_error = "FujiNet-PC is already running.";
		return false;
	}
	// Attach/reconnect before starting FujiNet so its initial HELLO is heard.
	if (!ATUIFujiNetConfigureListener(s_lan, (unsigned)s_port, (unsigned)s_timeout))
		return false;
	ATFujiNetPCOptions options;
	options.executable = s_executable;
	options.stateDirectory = StateDirectory();
	options.netsioPort = (uint16_t)s_port;
	options.webPort = (uint16_t)s_webPort;
	const bool started = s_runtime.Start(options);
	if (started) s_runtimeEndpoint = ListenerEndpoint();
	s_error = started ? std::string() : s_runtime.GetStatus().error;
	return started;
}
bool ATUIFujiNetStartAndBoot() {
	if (!CanChange()) return false;
	if (s_runtime.GetStatus().stopping) {
		s_error = "Wait for FujiNet-PC to stop before starting another boot.";
		return false;
	}
	if (g_sim.GetDiskInterface(0).GetClientCount() > 1) {
		s_error = "D1 has a full-drive emulator attached. Disable that drive in Configure System before booting FujiNet.";
		return false;
	}
	if (!s_runtime.GetStatus().running) {
		if (!ATUIFujiNetStartRuntime()) return false;
	} else if (!Control() || !Control()->GetNetSIOStatus().listening) {
		if (!ATUIFujiNetReconnect()) return false;
	}
	s_bootPending = true;
	s_bootDeadline = SDL_GetTicks() + 10000;
	s_error.clear();
	return true;
}
void ATUIFujiNetStopRuntime() {
	s_bootPending = false;
	s_runtime.Stop();
}
bool ATUIFujiNetReconnect() {
	IATDeviceNetSIOControl* control;
	if (!RequireControl(control)) return false;
	control->Reconnect();
	const auto status = ListenerReady(*control);
	s_error = status.listening ? std::string() : VDTextWToU8(status.lastError).c_str();
	return status.listening;
}
bool ATUIFujiNetDisconnect() {
	s_bootPending = false;
	IATDeviceNetSIOControl* control;
	if (!RequireControl(control)) return false;
	control->Disconnect();
	s_error.clear();
	return true;
}
bool ATUIFujiNetBoot() {
	s_bootPending = false;
	IATDeviceNetSIOControl* control;
	if (!RequireControl(control)) return false;
	if (!control->GetNetSIOStatus().connected) {
		s_error = "No FujiNet peer is connected. Start FujiNet-PC or point an external FujiNet at this listener, then retry.";
		return false;
	}
	// A full-drive emulator can still answer D1 even with the built-in
	// drive disabled. Do not silently remove hardware or discard media.
	if (g_sim.GetDiskInterface(0).GetClientCount() > 1) {
		s_error = "D1 has a full-drive emulator attached. Disable that drive in Configure System before booting FujiNet.";
		return false;
	}
	g_sim.GetDiskDrive(0).SetEnabled(false);
	g_sim.SetFastBootEnabled(false);
	// FujiNet performs real network I/O. Match wall-clock speed so a
	// previously selected Warp/accelerated mode cannot outrun remote SIO.
	ATUISetTurbo(false);
	ATUISetTurboPulse(false);
	ATUISetSlowMotion(false);
	ATUISetSpeedModifier(0.0f);
	// Like resetting a physical computer while FujiNet remains powered:
	// do not send a peripheral cold reset that restarts FujiNet-PC.
	g_sim.ColdResetComputerOnly();
	g_sim.Resume();
	s_error.clear();
	return true;
}
bool ATUIFujiNetEnableLocalD1() {
	if (!CanChange()) return false;
	g_sim.GetDiskDrive(0).SetEnabled(true);
	s_error.clear();
	return true;
}
ATFujiNetUIStatus ATUIGetFujiNetStatus() {
	Load();
	ATFujiNetUIStatus result;
	const auto& runtime = s_runtime.GetStatus();
	result.open = s_open;
	result.processAvailable = ATFujiNetPC::Available();
	result.running = runtime.running;
	result.stopping = runtime.stopping;
	result.exitCode = runtime.exitCode;
	result.restarts = runtime.restarts;
	result.logTail = runtime.logTail;
	result.error = !s_error.empty() ? s_error : runtime.error;
	result.executable = s_executable;
	result.stateDirectory = StateDirectory();
	result.webPort = s_webPort;
	result.port = s_port;
	result.localD1Enabled = g_sim.GetDiskDrive(0).IsEnabled();
	result.fastBoot = g_sim.IsFastBootEnabled();
	result.bootPending = s_bootPending;
	if (auto* control = Control()) {
		const auto status = control->GetNetSIOStatus();
		result.present = true;
		result.listening = status.listening;
		result.connected = status.connected;
		result.port = status.port;
		result.peer = status.peer.c_str();
		result.rxPackets = status.rxPackets;
		result.txPackets = status.txPackets;
		result.droppedPackets = status.droppedPackets;
		result.syncTimeouts = status.syncTimeouts;
		if (result.error.empty()) result.error = VDTextWToU8(status.lastError).c_str();
	}
	return result;
}
void ATUIPollFujiNet() {
	std::string picked;
	{
		std::lock_guard<std::mutex> lock(s_pickerMutex);
		picked.swap(s_pickedPath);
	}
	if (!picked.empty()) ATUIFujiNetSetRuntime(picked.c_str(), (unsigned)s_webPort);
	s_runtime.Poll();
	SyncDeviceDraft();
	if (s_runtime.GetStatus().running && !s_runtime.GetStatus().stopping &&
		ListenerEndpoint() != s_runtimeEndpoint) {
		s_bootPending = false;
		s_runtime.Stop();
		s_error = "The active profile's NetSIO listener changed. The managed runtime is stopping; restart it to use the new configuration.";
	}
	if (s_bootPending) {
		if (Locked()) {
			s_bootPending = false;
			s_error = "Pending FujiNet boot was cancelled because a netplay session started.";
		} else if (!s_runtime.GetStatus().running) {
			s_bootPending = false;
			s_error = "FujiNet-PC exited before connecting. See its runtime log.";
		} else if (auto* control = Control(); control && control->GetNetSIOStatus().connected) {
			ATUIFujiNetBoot();
		} else if (SDL_GetTicks() >= s_bootDeadline) {
			s_bootPending = false;
			s_error = "FujiNet-PC did not connect within 10 seconds. Check its log, BOIP settings and UDP listener port, then retry Boot FujiNet.";
		}
	}
}
void ATUIShutdownFujiNet() {
	s_bootPending = false;
	s_runtime.Shutdown();
	Save();
	s_open = false;
}

void ATUIRenderFujiNetDialog() {
	if (!s_open) return;
	Load();
	const auto status = ATUIGetFujiNetStatus();
	ImGui::SetNextWindowSize(ATUIFitDialogSize(ImVec2(760, 700)), ImGuiCond_Appearing);
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
		ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	ATUIConstrainDialogSize();
	if (ImGui::Begin("FujiNet", &s_open, ImGuiWindowFlags_NoSavedSettings)) {
		ATUIClampDialogPosition();
#ifdef __EMSCRIPTEN__
		ImGui::TextWrapped("The browser cannot listen for native UDP NetSIO packets. "
			"Use a native AltirraSDL build to connect FujiNet.");
#else
		ImGui::BeginDisabled(Locked());
		if (status.processAvailable) {
			ImGui::BeginDisabled(status.bootPending || status.stopping);
			if (ActionButton("Start and Boot FujiNet")) ATUIFujiNetStartAndBoot();
			ImGui::EndDisabled();
			if (status.bootPending) {
				SameLineIfFits("Cancel Pending Boot");
				if (ActionButton("Cancel Pending Boot")) s_bootPending = false;
				ImGui::TextWrapped("Waiting for FujiNet to connect before booting...");
			}
		}
		ImGui::EndDisabled();
		ImGui::TextWrapped("Listener: %s | Peer: %s", status.listening ? "listening" : "stopped",
			status.connected ? status.peer.c_str() : "waiting for FujiNet");
		if (!status.error.empty()) ImGui::TextWrapped("%s", status.error.c_str());
		ImGui::TextWrapped("Connect the Atari SIO bus to FujiNet-PC or an external FujiNet.");
		ImGui::BeginDisabled(Locked());
		ImGui::SeparatorText("Local FujiNet-PC");
		if (status.processAvailable) {
			ImGui::BeginDisabled(status.running);
			FieldLabel("Executable");
			ImGui::InputText("##Executable", s_executable, sizeof s_executable);
			if (ImGui::IsItemDeactivatedAfterEdit()) Save();
			if (ActionButton("Browse...")) Browse();
			SameLineIfFits("Download FujiNet-PC");
			if (ActionButton("Download FujiNet-PC"))
				SDL_OpenURL("https://github.com/FujiNetWIFI/fujinet-firmware/releases");
			FieldLabel("Local web port");
			ImGui::InputInt("##Local web port", &s_webPort);
			if (ImGui::IsItemDeactivatedAfterEdit()) {
				s_webPort = std::clamp(s_webPort, 1, 65535);
				Save();
			}
			ImGui::TextWrapped("Download the ATARI archive for your operating system. "
				"Select the executable inside the fully extracted official FujiNet-PC release, "
				"with its data folder and fnconfig.ini beside it. "
				"A private configuration and virtual SD card are kept under the data folder below.");
			if (ActionButton("Start FujiNet-PC")) ATUIFujiNetStartRuntime();
			ImGui::EndDisabled();
			SameLineIfFits("Stop FujiNet-PC");
			ImGui::BeginDisabled(!status.running || status.stopping);
			if (ActionButton("Stop FujiNet-PC")) ATUIFujiNetStopRuntime();
			ImGui::EndDisabled();
			ImGui::TextWrapped("Runtime: %s | Last exit: %d | Restarts: %u",
				status.stopping ? "stopping" : status.running ? "running" : "stopped",
				status.exitCode, status.restarts);
		} else {
			ImGui::TextWrapped("This platform uses an external FujiNet or FujiNet-PC. "
				"Local process management is unavailable.");
		}
		ImGui::EndDisabled();

		ImGui::SeparatorText("NetSIO connection");
		ImGui::BeginDisabled(Locked());
		ImGui::BeginDisabled(status.running);
		ImGui::Checkbox("Allow LAN", &s_lan);
		FieldLabel("UDP port");
		ImGui::InputInt("##UDP port", &s_port);
		FieldLabel("Synchronization timeout (ms)");
		ImGui::InputInt("##Synchronization timeout", &s_timeout);
		if (ActionButton(status.present ? "Apply / Reconnect" : "Connect NetSIO"))
			ATUIFujiNetConfigureListener(s_lan, s_port, s_timeout);
		ImGui::EndDisabled();
		SameLineIfFits("Disconnect");
		ImGui::BeginDisabled(!status.listening);
		if (ActionButton("Disconnect")) ATUIFujiNetDisconnect();
		ImGui::EndDisabled();
		if (status.present && !status.listening) {
			SameLineIfFits("Reconnect NetSIO");
			if (ActionButton("Reconnect NetSIO")) ATUIFujiNetReconnect();
		}
		ImGui::TextWrapped("External FujiNet: enable SIO over Network / BOIP in its web "
			"settings and point it at this computer's address and UDP port. "
			"Use 127.0.0.1 for a local FujiNet-PC runtime.");
		ImGui::TextWrapped("Bind endpoint: %s", ListenerEndpoint().c_str());
		ImGui::TextWrapped("Packets: %llu received, %llu sent | Dropped: %llu | Sync timeouts: %llu",
			(unsigned long long)status.rxPackets, (unsigned long long)status.txPackets,
			(unsigned long long)status.droppedPackets, (unsigned long long)status.syncTimeouts);

		ImGui::SeparatorText("Boot from FujiNet");
		ImGui::TextWrapped("Boot disables the built-in local D1 drive and Fast boot, sets "
			"100%% speed, then cold-resets the computer and runs. The D1 image and unsaved changes stay mounted; D2–D15 remain unchanged. "
			"External FujiNet disk slots must use different numbers from active local drives.");
		ImGui::BeginDisabled(!status.connected || status.bootPending || status.stopping);
		if (ActionButton("Boot FujiNet (disable local D1)")) ATUIFujiNetBoot();
		ImGui::EndDisabled();
		SameLineIfFits("Enable local D1");
		ImGui::BeginDisabled(status.localD1Enabled);
		if (ActionButton("Enable local D1")) ATUIFujiNetEnableLocalD1();
		ImGui::EndDisabled();
		ImGui::TextWrapped("Local D1: %s | Fast boot: %s", status.localD1Enabled ? "enabled" : "disabled",
			status.fastBoot ? "enabled" : "disabled");
		ImGui::EndDisabled();
		if (Locked()) ImGui::TextWrapped("FujiNet changes are disabled during an active netplay session.");

		ImGui::SeparatorText("Web interface and data");
		FieldLabel("External web URL (used while local runtime is stopped)");
		ImGui::BeginDisabled(status.running);
		ImGui::InputText("##External web URL", s_webURL, sizeof s_webURL);
		if (ImGui::IsItemDeactivatedAfterEdit()) Save();
		ImGui::EndDisabled();
		if (ActionButton("Open Web Interface")) {
			const std::string url = !status.running && s_webURL[0] ? s_webURL :
				"http://127.0.0.1:" + std::to_string(s_webPort);
			if (ATMediaURL::IsURL(url)) SDL_OpenURL(url.c_str());
			else s_error = "Enter a complete HTTP(S) web-interface URL.";
		}
		SameLineIfFits("Open Data Folder");
		if (ActionButton("Open Data Folder")) {
			const auto directory = StateDirectory();
			if (!directory.empty()) {
				SDL_CreateDirectory(directory.c_str());
				std::string url = "file://";
				if (directory[0] != '/') url += '/';
				static constexpr char hex[] = "0123456789ABCDEF";
				for (unsigned char c : directory) {
					if (c == '\\') c = '/';
					if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
						(c >= '0' && c <= '9') || c == '/' || c == ':' ||
						c == '-' || c == '_' || c == '.' || c == '~') {
						url += (char)c;
					} else {
						url += '%'; url += hex[c >> 4]; url += hex[c & 15];
					}
				}
				if (!SDL_OpenURL(url.c_str())) s_error = SDL_GetError();
			}
		}
		ImGui::TextWrapped("Data and logs: %s", status.stateDirectory.c_str());
		if (ImGui::CollapsingHeader("Runtime log")) {
			ImGui::BeginChild("##FujiNetLog", ImVec2(0, 150), ImGuiChildFlags_Borders);
			ImGui::TextUnformatted(status.logTail.c_str());
			ImGui::EndChild();
		}
#endif
	}
	ImGui::End();
}
