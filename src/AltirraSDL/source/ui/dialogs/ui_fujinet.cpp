// Authorized fork extension: upstream Windows Altirra has no FujiNet panel.
// The native device is a UDP listener; FujiNet initiates the connection.
#include <stdafx.h>
#include "ui_fujinet.h"
#include "ui_main.h"
#include "ui_fonts.h"
#include "../../media/fujinet_pc.h"
#include "../../media/fujinet_compat.h"
#include "../../media/media_url.h"
#include "../../app/ui_file_dialog_sdl3.h"
#include <SDL3/SDL.h>
#include <imgui.h>
#ifdef IMGUI_ENABLE_TEST_ENGINE
#include <imgui_internal.h>
#endif
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
#include <vector>

extern ATSimulator g_sim;
extern SDL_Window* g_pWindow;
extern VDStringA ATGetConfigDir();
extern void ATRegistryFlushToDisk();

namespace {
bool DetailHeader(const char* label) {
	ImGui::PushStyleColor(ImGuiCol_Header, ImGui::GetStyleColorVec4(ImGuiCol_WindowBg));
	ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0);
	const bool open = ImGui::CollapsingHeader(label);
	ImGui::PopStyleVar();
	ImGui::PopStyleColor();
	return open;
}
ATFujiNetPC s_runtime;
bool s_open = false;
bool s_loaded = false;
bool s_lan = false;
bool s_external = false;
bool s_openConnection = false;
bool s_selectMode = true;
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
ATFujiNetCompat::LegacyDescriptor LegacyTransport(bool refresh = false) {
	static std::string observed;
	static ATFujiNetCompat::LegacyDescriptor cached;
	std::string signature = std::to_string(ATSettingsGetCurrentProfileId());
	std::vector<std::string> paths;
	for (auto* device : g_sim.GetDeviceManager()->GetDevices(false, false, false)) {
		ATDeviceInfo info;
		device->GetDeviceInfo(info);
		if (!info.mpDef || !info.mpDef->mpTag || strcmp(info.mpDef->mpTag, "custom")) continue;
		ATPropertySet properties;
		device->GetSettings(properties);
		paths.emplace_back(VDTextWToU8(VDStringSpanW(properties.GetString("path", L""))).c_str());
		signature += "|" + paths.back();
		std::error_code error;
		const auto modified = std::filesystem::last_write_time(
			ATFujiNetCompat::NativePath(paths.back()), error);
		if (!error) {
			// Keep native clock units: converting the Windows 1601 epoch
			// to nanoseconds would overflow a signed 64-bit duration.
			// The cast also handles libc++'s wider file-clock integer.
			signature += "@" + std::to_string(static_cast<long long>(
				modified.time_since_epoch().count()));
		}
	}
	if (refresh || signature != observed) {
		observed = signature;
		const auto previous = cached;
		cached = {};
		for (const auto& path : paths) {
			auto descriptor = ATFujiNetCompat::InspectLegacyDescriptor(path);
			// A loaded VM can keep running after its source is deleted or
			// unreadable. Retain known ownership until the device is removed
			// or a readable replacement actually changes the descriptor.
			if (!descriptor.sourceRead && previous.recognizedLegacy && previous.path == path)
				descriptor = previous;
			if (descriptor.recognizedLegacy) { cached = std::move(descriptor); break; }
			if (descriptor.suspectedLegacy) cached = std::move(descriptor);
		}
	}
	return cached;
}
bool CanUseNative() {
	if (LegacyTransport(true).recognizedLegacy) {
		s_error = "This profile uses the NetSIO custom-device bridge. Keep that manual setup, or remove its custom device and stop the hub before using native NetSIO.";
		return false;
	}
	unsigned count = 0;
	for (auto* device : g_sim.GetDeviceManager()->GetDevices(false, false, false)) {
		ATDeviceInfo info;
		device->GetDeviceInfo(info);
		if (info.mpDef && info.mpDef->mpTag && !strcmp(info.mpDef->mpTag, "netsio")) ++count;
	}
	if (count > 1) {
		s_error = "This profile has multiple NetSIO devices. Keep one listener in Configure System > Devices before using the FujiNet panel.";
		return false;
	}
	return true;
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
	s_external = key.getBool("ExternalMode", false) || !ATFujiNetPC::Available();
	s_loaded = true;
}
void Save() {
	if (!s_loaded) return;
	// A fresh XDG_CONFIG_HOME may not exist yet. Settings-only use must
	// persist even before launching a runtime creates its private SD tree.
	const auto directory = ATGetConfigDir();
	if (!directory.empty() && !SDL_CreateDirectory(directory.c_str()))
		s_error = std::string("Could not create the settings directory: ") + SDL_GetError();
	VDRegistryAppKey key("FujiNet", true);
	key.setString("RuntimeExecutable", VDTextU8ToW(VDStringSpanA(s_executable)).c_str());
	key.setString("WebURL", VDTextU8ToW(VDStringSpanA(s_webURL)).c_str());
	key.setInt("WebPort", s_webPort);
	key.setBool("ExternalMode", s_external);
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
	if (!s_runtime.GetStatus().running && Control() && Control()->GetNetSIOStatus().connected)
		s_external = true;
	s_selectMode = true;
	LegacyTransport(true);
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
	if (!CanChange() || !CanUseNative()) return false;
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
	if (!CanChange() || !CanUseNative()) return false;
	if (!s_runtime.GetStatus().running && Control() && Control()->GetNetSIOStatus().connected) {
		s_error = "A FujiNet peer is already connected outside this launcher. Use External to boot that connection; disconnect it before starting another runtime.";
		return false;
	}
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
	if (!CanUseNative()) return false;
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
	if (!CanChange()) return false;
	const bool legacy = LegacyTransport(true).recognizedLegacy;
	if (legacy && Control() && Control()->GetNetSIOStatus().listening) {
		s_error = "Both NetSIO routes are configured. Disconnect the native listener before booting through the custom-device bridge.";
		return false;
	}
	if (!legacy && !CanUseNative()) return false;
	auto* control = Control();
	if (!legacy && (!control || !control->GetNetSIOStatus().connected)) {
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
	const bool wasRunning = s_runtime.GetStatus().running;
	s_runtime.Poll();
	if (wasRunning && !s_runtime.GetStatus().running &&
		ListenerEndpoint() == s_runtimeEndpoint && Control() && Control()->GetNetSIOStatus().listening)
		Control()->Reconnect();
	SyncDeviceDraft();
	if (s_runtime.GetStatus().running && !s_runtime.GetStatus().stopping &&
		LegacyTransport().recognizedLegacy) {
		s_bootPending = false;
		s_runtime.Stop();
		s_error = "A NetSIO custom-device bridge was added to this profile. The managed runtime is stopping to prevent duplicate SIO traffic.";
	}
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

namespace {
void Heading(const char* text) {
	ImGui::PushFont(ATUIGetFontHeading(), ImGui::GetFontSize() * 1.2f);
	ImGui::TextWrapped("%s", text);
	ImGui::PopFont();
}
void Hint(const char* text) {
	ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
	ImGui::TextWrapped("%s", text);
	ImGui::PopStyleColor();
}
bool PrimaryAction(const char* label) {
	ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive));
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
		ImVec2(ImGui::GetStyle().FramePadding.x + 8, ImGui::GetStyle().FramePadding.y + 4));
	const bool pressed = ActionButton(label);
	ImGui::PopStyleVar();
	ImGui::PopStyleColor();
	return pressed;
}
void OpenWebInterface(const ATFujiNetUIStatus& status) {
	const std::string url = !status.running && s_webURL[0] ? s_webURL :
		"http://127.0.0.1:" + std::to_string(s_webPort);
	if (!ATMediaURL::IsURL(url)) s_error = "Enter a complete HTTP(S) web-interface URL.";
	else if (!SDL_OpenURL(url.c_str())) s_error = SDL_GetError();
}
void OpenDataFolder() {
	const auto directory = StateDirectory();
	if (directory.empty()) return;
	if (!SDL_CreateDirectory(directory.c_str())) {
		s_error = SDL_GetError();
		return;
	}
	std::string url = "file://";
	if (directory[0] != '/') url += '/';
	static constexpr char hex[] = "0123456789ABCDEF";
	for (unsigned char c : directory) {
		if (c == '\\') c = '/';
		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
			(c >= '0' && c <= '9') || c == '/' || c == ':' ||
			c == '-' || c == '_' || c == '.' || c == '~') url += (char)c;
		else { url += '%'; url += hex[c >> 4]; url += hex[c & 15]; }
	}
	if (!SDL_OpenURL(url.c_str())) s_error = SDL_GetError();
}
void BootNote() {
	Hint("Boot uses FujiNet for D1 and normal speed. Local disk images stay mounted.");
}
void ConnectionDetails(const ATFujiNetUIStatus& status) {
	if (s_openConnection) {
		ImGui::SetNextItemOpen(true);
		s_openConnection = false;
	}
	if (!DetailHeader("Connection settings")) return;
	ImGui::Indent();
	ImGui::BeginDisabled(Locked() || status.running);
	ImGui::Checkbox("Allow LAN", &s_lan);
	Hint(s_lan ? "Accept NetSIO from other computers on all IPv4 interfaces." :
		"Accept NetSIO on this computer only (127.0.0.1).");
	const int columns = ImGui::GetContentRegionAvail().x > ImGui::GetFontSize() * 32 ? 2 : 1;
	if (ImGui::BeginTable("##connection-fields", columns, ImGuiTableFlags_SizingStretchSame)) {
		ImGui::TableNextColumn();
		FieldLabel("UDP port");
		ImGui::InputInt("##UDP port", &s_port, 0, 0);
		ImGui::TableNextColumn();
		FieldLabel("Synchronization timeout (ms)");
		ImGui::InputInt("##Synchronization timeout", &s_timeout, 0, 0);
		ImGui::EndTable();
	}
	if (ActionButton("Apply connection settings"))
		ATUIFujiNetConfigureListener(s_lan, s_port, s_timeout);
	ImGui::EndDisabled();
	if (status.running) Hint("Stop FujiNet-PC before changing the listener.");
	ImGui::BeginDisabled(Locked());
	if (status.listening) {
		if (ActionButton("Disconnect")) ATUIFujiNetDisconnect();
	} else if (status.present) {
		if (ActionButton("Reconnect NetSIO")) ATUIFujiNetReconnect();
	}
	ImGui::EndDisabled();
	if (s_external) {
		ImGui::Spacing();
		FieldLabel("Web interface URL (optional)");
		ImGui::SetNextItemWidth(-1);
		ImGui::InputTextWithHint("##External web URL", "http://fujinet/", s_webURL, sizeof s_webURL);
		if (ImGui::IsItemDeactivatedAfterEdit()) Save();
		Hint("Enable SIO over Network / BOIP in FujiNet and send it to this computer's address and UDP port.");
	}
	ImGui::Unindent();
}
void LocalDiskDetails(const ATFujiNetUIStatus& status) {
	if (!DetailHeader("Local disks")) return;
	ImGui::Indent();
	ImGui::TextWrapped("Local D1: %s", status.localD1Enabled ? "enabled" : "disabled for FujiNet");
	ImGui::TextWrapped("Fast boot: %s", status.fastBoot ? "enabled" : "disabled");
	ImGui::BeginDisabled(Locked() || status.localD1Enabled);
	if (ActionButton("Enable local D1")) ATUIFujiNetEnableLocalD1();
	ImGui::EndDisabled();
	Hint("Boot disables local D1 and Fast boot, selects 100% speed, then resets and runs the Atari. "
		"The D1 image and unsaved changes stay mounted. D2-D15 and BASIC stay unchanged. "
		"Use different drive numbers for active local and FujiNet disks.");
	ImGui::Unindent();
}
void Diagnostics(const ATFujiNetUIStatus& status) {
	if (!DetailHeader("Diagnostics")) return;
	ImGui::Indent();
	ImGui::TextWrapped("Listener: %s", status.listening ? ListenerEndpoint().c_str() : "stopped");
	ImGui::TextWrapped("Peer: %s", status.connected ? status.peer.c_str() : "none");
	ImGui::TextWrapped("Packets: %llu received, %llu sent",
		(unsigned long long)status.rxPackets, (unsigned long long)status.txPackets);
	ImGui::TextWrapped("Dropped: %llu | Synchronization timeouts: %llu",
		(unsigned long long)status.droppedPackets, (unsigned long long)status.syncTimeouts);
	if (status.processAvailable) {
		ImGui::TextWrapped("Last runtime exit: %d | Restarts: %u", status.exitCode, status.restarts);
		if (ActionButton("Open Data Folder")) OpenDataFolder();
		Hint(status.stateDirectory.c_str());
		if (ImGui::TreeNode("Runtime log")) {
			ImGui::BeginChild("##FujiNetLog", ImVec2(0, ImGui::GetTextLineHeightWithSpacing() * 8),
				ImGuiChildFlags_Borders);
			ImGui::TextUnformatted(status.logTail.c_str());
			ImGui::EndChild();
			ImGui::TreePop();
		}
	}
	ImGui::Unindent();
}
void ManualConnection(const ATFujiNetUIStatus& status,
	const ATFujiNetCompat::LegacyDescriptor& legacy) {
	Heading("Manual FujiNet connection");
	Hint("This profile uses your NetSIO custom device and external hub.");
	if (!status.error.empty()) ImGui::TextWrapped("%s", status.error.c_str());
	ImGui::Spacing();
	ImGui::BeginDisabled(Locked());
	if (status.listening) {
		ImGui::TextWrapped("A native NetSIO listener is also active. Disconnect it to keep one SIO route.");
		if (ActionButton("Disconnect native NetSIO")) ATUIFujiNetDisconnect();
	} else {
		if (PrimaryAction("Boot Atari")) ATUIFujiNetBoot();
		SameLineIfFits(s_webURL[0] ? "Open Web Interface" : "Set web address");
		if (ActionButton(s_webURL[0] ? "Open Web Interface" : "Set web address")) {
			if (s_webURL[0]) OpenWebInterface(status);
			else s_openConnection = true;
		}
		BootNote();
	}
	ImGui::EndDisabled();
	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();
	if (s_openConnection) { ImGui::SetNextItemOpen(true); s_openConnection = false; }
	if (DetailHeader("Web interface")) {
		ImGui::Indent();
		FieldLabel("FujiNet web URL");
		ImGui::InputTextWithHint("##External web URL", "http://fujinet/", s_webURL, sizeof s_webURL);
		if (ImGui::IsItemDeactivatedAfterEdit()) Save();
		ImGui::Unindent();
	}
	LocalDiskDetails(status);
	if (DetailHeader("Manual setup")) {
		ImGui::Indent();
		ImGui::TextWrapped("Custom device: %s", legacy.path.c_str());
		Hint("Keep starting your hub and FujiNet-PC as before. Your descriptor, filters, "
			"host settings and external processes stay under your control.");
		ImGui::Spacing();
		Hint("To use native NetSIO instead, remove the NetSIO custom device in Configure System > Devices "
			"and stop its hub. Use a separate profile if you want to keep both setups.");
		ImGui::Unindent();
	}
}
} // namespace

void ATUIRenderFujiNetDialog() {
	if (!s_open) return;
	Load();
	const auto status = ATUIGetFujiNetStatus();
#ifdef IMGUI_ENABLE_TEST_ENGINE
	ImGuiContext& g = *ImGui::GetCurrentContext();
#endif
	const float scale = ImGui::GetFontSize() / 13.0f;
	const float spacingScale = std::min(scale, 1.4f);
	const auto* viewport = ImGui::GetMainViewport();
	const ImVec2 available = ATUIFitDialogSize(ImVec2(620 * scale, 100000));
	ImGui::SetNextWindowSize(ImVec2(available.x, 0), ImGuiCond_Appearing);
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
		ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSizeConstraints(ImVec2(available.x, 0), available);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18 * spacingScale, 14 * spacingScale));
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8 * spacingScale, 9 * spacingScale));
	if (ImGui::Begin("FujiNet", &s_open, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize)) {
		static ImVec2 lastSize, lastViewport;
		const ImVec2 size = ImGui::GetWindowSize();
		if (ImGui::IsWindowAppearing() || size.x != lastSize.x || size.y != lastSize.y ||
			viewport->WorkSize.x != lastViewport.x || viewport->WorkSize.y != lastViewport.y) {
			ImGui::SetWindowPos(ImVec2(viewport->WorkPos.x + (viewport->WorkSize.x - size.x) * 0.5f,
				viewport->WorkPos.y + (viewport->WorkSize.y - size.y) * 0.5f));
		} else ATUIClampDialogPosition();
		lastSize = size;
		lastViewport = viewport->WorkSize;
#ifdef __EMSCRIPTEN__
		Heading("FujiNet needs a native build");
		Hint("Browsers cannot listen for UDP NetSIO. Use a native AltirraSDL build to connect FujiNet.");
#else
		const auto legacy = LegacyTransport();
		if (legacy.recognizedLegacy) {
			ManualConnection(status, legacy);
		} else {
			const bool unmanagedPeer = status.connected && !status.running;
			if (unmanagedPeer && !s_external) { s_external = true; s_selectMode = true; }
			const bool selectingMode = ImGui::IsWindowAppearing() || s_selectMode;
			const bool requestedExternal = s_external;
			bool selectedExternal = s_external;
			if (status.processAvailable && ImGui::BeginTabBar("##FujiNetMode")) {
				ImGui::BeginDisabled(unmanagedPeer);
				const bool localTab = ImGui::BeginTabItem("Local PC", nullptr, selectingMode && !requestedExternal ? ImGuiTabItemFlags_SetSelected : 0);
#ifdef IMGUI_ENABLE_TEST_ENGINE
				IMGUI_TEST_ENGINE_ITEM_INFO(ImGui::GetItemID(), "Local PC", 0);
#endif
				if (localTab) {
					if (!selectingMode) selectedExternal = false;
					ImGui::EndTabItem();
				}
				ImGui::EndDisabled();
				if (unmanagedPeer && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
					ImGui::SetTooltip("Disconnect the external peer before starting a managed runtime.");
				ImGui::BeginDisabled(status.running);
				const bool externalTab = ImGui::BeginTabItem("External", nullptr, selectingMode && requestedExternal ? ImGuiTabItemFlags_SetSelected : 0);
#ifdef IMGUI_ENABLE_TEST_ENGINE
				IMGUI_TEST_ENGINE_ITEM_INFO(ImGui::GetItemID(), "External", 0);
#endif
				if (externalTab) {
					if (!selectingMode) selectedExternal = true;
					ImGui::EndTabItem();
				}
				ImGui::EndDisabled();
				ImGui::EndTabBar();
				s_selectMode = false;
				if (selectedExternal != s_external) {
					s_external = selectedExternal;
					s_openConnection = s_external && !status.present;
					Save();
				}
			}
			ImGui::Spacing();
			const bool needsRuntime = !s_external && !s_executable[0];
			const char* title = status.stopping ? "Stopping FujiNet-PC" :
				status.connected ? "Connected to FujiNet" : status.bootPending ? "Starting FujiNet" :
				needsRuntime ? "Set up FujiNet-PC" : status.running ? "Waiting for FujiNet" :
				status.listening && s_external ? "Waiting for FujiNet" : s_external ? "Connect an external FujiNet" : "Ready to start";
			Heading(title);
			if (legacy.suspectedLegacy)
				Hint("A custom device may already provide NetSIO. Check it before enabling a second SIO route.");
			if (!status.error.empty()) {
				const auto background = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
				const bool light = background.x + background.y + background.z > 1.5f;
				ImGui::PushStyleColor(ImGuiCol_Text, light ? ImVec4(0.60f, 0.18f, 0.12f, 1) : ImVec4(1, 0.63f, 0.57f, 1));
				ImGui::TextWrapped("%s", status.error.c_str());
				ImGui::PopStyleColor();
			}
			ImGui::Spacing();
			ImGui::BeginDisabled(Locked());
			if (!s_external) {
				if (needsRuntime) {
					if (PrimaryAction("Choose FujiNet-PC")) Browse();
					SameLineIfFits("Download FujiNet-PC");
					if (ActionButton("Download FujiNet-PC"))
						SDL_OpenURL("https://github.com/FujiNetWIFI/fujinet-firmware/releases");
					Hint("Use an official ATARI release for your operating system.");
				} else {
					const std::string executable(s_executable);
					const auto separator = executable.find_last_of("/\\");
					ImGui::TextWrapped("Runtime: %s", executable.substr(separator == std::string::npos ? 0 : separator + 1).c_str());
					if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", s_executable);
					if (status.running) {
						SameLineIfFits("Stop FujiNet-PC");
						ImGui::BeginDisabled(status.stopping);
						if (ActionButton("Stop FujiNet-PC")) ATUIFujiNetStopRuntime();
						ImGui::EndDisabled();
					} else {
						SameLineIfFits("Change...");
						if (ActionButton("Change...")) Browse();
					}
					ImGui::Spacing();
					ImGui::BeginDisabled(status.stopping || status.bootPending);
					if (status.connected) {
						if (PrimaryAction("Boot Atari")) ATUIFujiNetBoot();
					} else if (PrimaryAction(status.running ? "Boot when connected" : "Start and Boot FujiNet")) {
						ATUIFujiNetStartAndBoot();
					}
					ImGui::EndDisabled();
					if (status.bootPending) {
						SameLineIfFits("Cancel boot");
						if (ActionButton("Cancel boot")) s_bootPending = false;
					} else if (!status.running) {
						SameLineIfFits("Start without booting");
						if (ActionButton("Start without booting")) ATUIFujiNetStartRuntime();
					}
					if (status.running) {
						SameLineIfFits("Open Web Interface");
						if (ActionButton("Open Web Interface")) OpenWebInterface(status);
					}
					if (status.running && !status.listening && !status.stopping) {
						if (ActionButton("Reconnect NetSIO")) ATUIFujiNetReconnect();
					}
					BootNote();
					if (status.bootPending) Hint("The Atari will boot as soon as FujiNet connects.");
				}
			} else {
				if (status.connected) {
					if (PrimaryAction("Boot Atari")) ATUIFujiNetBoot();
					SameLineIfFits(s_webURL[0] ? "Open Web Interface" : "Set web address");
					if (ActionButton(s_webURL[0] ? "Open Web Interface" : "Set web address")) {
						if (s_webURL[0]) OpenWebInterface(status);
						else s_openConnection = true;
					}
					BootNote();
				} else if (!status.listening) {
					if (PrimaryAction(status.present ? "Reconnect NetSIO" : "Connect NetSIO"))
						ATUIFujiNetConfigureListener(s_lan, s_port, s_timeout);
				}
				if (status.listening) {
					ImGui::TextWrapped("Listening on %s", ListenerEndpoint().c_str());
					if (ActionButton("Disconnect")) ATUIFujiNetDisconnect();
				}
				if (!status.connected) Hint("Send NetSIO from your FujiNet or existing FujiNet-PC to this computer.");
			}
			ImGui::EndDisabled();
			if (Locked()) Hint("FujiNet changes are disabled during netplay.");
			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Spacing();
			if (!s_external && DetailHeader(needsRuntime ? "Installation help" : "Runtime settings")) {
				ImGui::Indent();
				Hint("Extract the official ATARI archive for your system. Select fujinet (fujinet.exe on Windows), "
					"with data/ and fnconfig.ini beside it. AltirraSDL keeps a private configuration and SD card.");
				ImGui::BeginDisabled(Locked() || status.running);
				FieldLabel("Executable");
				ImGui::InputText("##Executable", s_executable, sizeof s_executable);
				if (ImGui::IsItemDeactivatedAfterEdit()) Save();
				FieldLabel("Local web port");
				ImGui::InputInt("##Local web port", &s_webPort, 0, 0);
				if (ImGui::IsItemDeactivatedAfterEdit()) {
					s_webPort = std::clamp(s_webPort, 1, 65535);
					Save();
				}
				ImGui::EndDisabled();
				ImGui::Unindent();
			}
			ConnectionDetails(status);
			LocalDiskDetails(status);
			Diagnostics(status);
		}
#endif
	}
	ImGui::End();
	ImGui::PopStyleVar(2);
}
