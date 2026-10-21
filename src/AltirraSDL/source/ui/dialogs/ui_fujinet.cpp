// Authorized fork extension: upstream Windows Altirra has no FujiNet panel.
// The native device is a UDP listener; FujiNet initiates the connection.
#include <stdafx.h>
#include "ui_fujinet.h"
#include "ui_main.h"
#include "ui_fonts.h"
#include "../../media/fujinet_pc.h"
#include "../../media/fujinet_mount.h"
#include "../../media/fujinet_compat.h"
#include "../../media/fujinet_install.h"
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
#include "customdevice.h"
#include "ui_main_internal.h"
#ifdef ALTIRRA_NETPLAY_ENABLED
#include "netplay/netplay_glue.h"
#endif
#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <memory>
#include <thread>
#include <stdexcept>
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
struct FileJob {
	ATFujiNetMount::Progress progress;
	ATFujiNetMount::Result result;
	std::atomic<bool> done{false};
	std::thread worker;
	uint32 profile = 0;
	std::string endpoint;
	~FileJob() {
		progress.cancel.store(true, std::memory_order_relaxed);
		if (worker.joinable()) worker.join();
	}
};
std::unique_ptr<FileJob> s_fileJob;
std::string s_pickedGame;
std::string s_fileMessage;
bool s_runFileOpen = false;
char s_gameURL[4096] = {};
ATFujiNetPC s_runtime;
// Transient guided workflow, not a saved emulation preference.
bool s_guided = false;
int s_guidedStep = 0;
// Full controls chosen this session: stop opening on the welcome page.
bool s_guideDismissed = false;
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
std::string s_runtimeVersion;
struct InstallJob {
	ATFujiNetInstall::Progress progress;
	ATFujiNetInstall::Result result;
	std::atomic<bool> done{false};
	std::thread worker;
	~InstallJob() {
		progress.cancel.store(true, std::memory_order_relaxed);
		if (worker.joinable()) worker.join();
	}
};
std::unique_ptr<InstallJob> s_installJob;
// One-click setup continues to Start and Boot once FujiNet-PC is ready,
// including after a later file pick or Homebrew library re-check.
bool s_autoContinue = false;
std::string s_missingPath;
std::vector<ATFujiNetInstall::MissingLibrary> s_missing;
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
	if (s_fileJob) {
		s_error = "Wait for the FujiNet file operation, or cancel it first.";
		return false;
	}
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
	struct Loaded {
		std::string path;
		IATDeviceCustomDescriptor* descriptor;
	};
	std::vector<Loaded> loaded;
	for (auto* device : g_sim.GetDeviceManager()->GetDevices(false, false, false)) {
		auto* descriptor = vdpoly_cast<IATDeviceCustomDescriptor*>(device);
		if (!descriptor) continue;
		ATPropertySet properties;
		device->GetSettings(properties);
		loaded.push_back({VDTextWToU8(VDStringSpanW(properties.GetString("path", L""))).c_str(), descriptor});
		signature += "|" + loaded.back().path + "@" +
			std::to_string(descriptor->GetLoadedDescriptorRevision());
	}
	if (refresh || signature != observed) {
		observed = signature;
		cached = {};
		for (const auto& item : loaded) {
			VDStringSpanA source;
			const auto state = item.descriptor->GetLoadedDescriptorSource(source);
			// Disk edits/deletion do not change a VM with hot reload disabled.
			// Revision/source change only when the actual loader tears it down.
			auto descriptor = ATFujiNetCompat::InspectLegacyDescriptorSource(item.path,
				std::string_view(source.data(), source.size()),
				state == IATDeviceCustomDescriptor::SourceStatus::Available);
			if (state == IATDeviceCustomDescriptor::SourceStatus::TooLarge) {
				descriptor.suspectedLegacy = true;
				descriptor.label = "Loaded custom descriptor exceeds the identification limit";
			}
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
	VDStringW version;
	key.getString("RuntimeVersion", version);
	s_runtimeVersion = VDTextWToU8(version).c_str();
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
	key.setString("RuntimeVersion", VDTextU8ToW(VDStringSpanA(s_runtimeVersion.c_str())).c_str());
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
// The native device reports socket failures technically; the usual cause is
// a second emulator window or a NetSIO hub already holding the port.
std::string ListenerError(const VDStringW& lastError, unsigned port) {
	const std::string text = VDTextWToU8(lastError).c_str();
	if (text.find("bind NetSIO UDP port") == std::string::npos &&
		text.find("UDP socket failed") == std::string::npos)
		return text;
	return "Could not open port " + std::to_string(port) + " for FujiNet. Another program "
		"probably uses it, for example a second AltirraSDL window or a NetSIO hub. Close "
		"that program, or choose another port in Connection settings (FujiNet must then "
		"use the same port).";
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
const ATFujiNetPCPackage& Package() {
	static const ATFujiNetPCPackage package = ATFujiNetPCSelectPackage(ATFujiNetPCDetectHost());
	return package;
}
// Private to AltirraSDL, beside (not inside) the FujiNet-PC state folder.
std::string InstallRoot() {
	const auto directory = ATGetConfigDir();
	return directory.empty() ? std::string() : std::string(directory.c_str()) + "/FujiNet-PC";
}
// Homebrew libraries the selected program links but are not installed.
// Reading the program is cheap but not per-frame cheap, so cache by path.
// Only macOS builds link Homebrew paths, so other systems skip the scan.
const std::vector<ATFujiNetInstall::MissingLibrary>& MissingLibraries(bool refresh = false) {
#ifdef __APPLE__
	if (refresh || s_missingPath != s_executable) {
		s_missingPath = s_executable;
		s_missing = s_executable[0] ? ATFujiNetInstall::FindMissingLibraries(s_executable)
			: std::vector<ATFujiNetInstall::MissingLibrary>();
	}
#else
	(void)refresh;
#endif
	return s_missing;
}
bool RuntimeReady() {
	return s_executable[0] && ATFujiNetPCCheckExecutable(s_executable).empty();
}
} // namespace

void ATUIShowFujiNetDialog() {
	Load();
	ReadDeviceSettings();
	if (!s_runtime.GetStatus().running && Control() && Control()->GetNetSIOStatus().connected)
		s_external = true;
	s_selectMode = true;
	// Nothing set up yet: open on the welcome page, whose single button
	// does everything. Existing setups open on the full controls.
	if (!LegacyTransport(true).recognizedLegacy && !s_guideDismissed && !s_guided &&
		!s_external && !s_executable[0] && !Device()) {
		s_guided = true;
		s_guidedStep = 0;
	}
	s_open = true;
}
bool ATUIFujiNetSetRuntime(const char* executable, unsigned webPort) {
	Load();
	if (!CanChange()) return false;
	if (s_runtime.GetStatus().running) {
		s_error = "Stop the managed runtime before changing its executable or web port.";
		return false;
	}
	if (s_installJob) {
		s_error = "Wait for the FujiNet-PC installation to finish, or cancel it.";
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
				s_error = ListenerError(status.lastError, port);
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
	// The installer replaces older versions in its folder when it finishes.
	if (s_installJob) {
		s_error = "Wait for the FujiNet-PC installation to finish, or cancel it.";
		return false;
	}
	// Attach/reconnect before starting FujiNet so its initial HELLO is heard.
	// The managed runtime is told its port, so when another program (often
	// a second AltirraSDL) holds it, the next free port is used instead.
	bool listening = false;
	for (unsigned port = (unsigned)s_port, tries = 0; !listening && port <= 65535 && tries < 20; ++port, ++tries) {
		listening = ATUIFujiNetConfigureListener(s_lan, port, (unsigned)s_timeout);
		if (!listening && s_error.compare(0, 20, "Could not open port ")) return false;
	}
	if (!listening) return false;
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
void ATUIFujiNetCancelFile() {
	if (s_fileJob) s_fileJob->progress.cancel.store(true, std::memory_order_relaxed);
}
void ATUIFujiNetStopRuntime() {
	// Keep the runtime and its data lease alive until its worker stops.
	s_fileJob.reset();
	s_bootPending = false;
	s_runtime.Stop();
}
bool ATUIFujiNetRunFile(const char* pathOrURL) {
	if (!CanChange() || !CanUseNative()) return false;
	const auto& process = s_runtime.GetStatus();
	if (!process.running || process.stopping || s_bootPending ||
		ListenerEndpoint() != s_runtimeEndpoint || !Control()
		|| !Control()->GetNetSIOStatus().connected) {
		s_error = "Start and connect the managed FujiNet-PC runtime before running a file through it.";
		return false;
	}
	if (!pathOrURL || !*pathOrURL) {
		s_error = "Choose a local file or enter a direct game URL.";
		return false;
	}
	if (g_sim.GetDiskInterface(0).GetClientCount() > 1) {
		s_error = "D1 has a full-drive emulator attached. Disable that drive before using FujiNet D1.";
		return false;
	}
	s_error.clear();
	s_fileMessage.clear();
	auto job = std::make_unique<FileJob>();
	job->profile = ATSettingsGetCurrentProfileId();
	job->endpoint = ListenerEndpoint();
	auto* raw = job.get();
	const std::string source = pathOrURL, directory = StateDirectory();
	const unsigned webPort = s_webPort;
	try {
		raw->worker = std::thread([raw, source, directory, webPort] {
			raw->result = ATFujiNetMount::Run(source, directory, webPort, raw->progress);
			raw->done.store(true, std::memory_order_release);
		});
		s_fileJob = std::move(job);
		return true;
	} catch (const std::exception& e) { s_error = e.what(); return false; }
}
bool ATUIFujiNetReconnect() {
	if (!CanUseNative()) return false;
	IATDeviceNetSIOControl* control;
	if (!RequireControl(control)) return false;
	control->Reconnect();
	const auto status = ListenerReady(*control);
	s_error = status.listening ? std::string() : ListenerError(status.lastError, status.port);
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
bool ATUIFujiNetInstallRuntime(bool thenStart) {
	Load();
	if (!CanChange()) return false;
	if (s_installJob) {
		s_error = "FujiNet-PC is already being installed.";
		return false;
	}
	if (!ATFujiNetPC::Available()) {
		s_error = "This platform connects to an external FujiNet; FujiNet-PC cannot run here.";
		return false;
	}
	if (s_runtime.GetStatus().running) {
		s_error = "Stop FujiNet-PC before updating it.";
		return false;
	}
	const auto root = InstallRoot();
	if (root.empty()) {
		s_error = "The AltirraSDL settings folder is unavailable.";
		return false;
	}
	// Tests point the installer at a local mock of GitHub's release list.
	const char* custom = SDL_getenv("ALTIRRA_FUJINET_RELEASES_URL");
	const std::string releases = custom && *custom ? custom : ATFujiNetInstall::kReleasesURL;
	auto job = std::make_unique<InstallJob>();
	auto* raw = job.get();
	const auto package = Package();
	try {
		raw->worker = std::thread([raw, package, root, releases] {
			raw->result = ATFujiNetInstall::Install(package, root, raw->progress, releases);
			raw->done.store(true, std::memory_order_release);
		});
	} catch (const std::exception& e) {
		s_error = e.what();
		return false;
	}
	s_installJob = std::move(job);
	s_autoContinue = thenStart;
	s_error.clear();
	return true;
}
void ATUIFujiNetCancelInstall() {
	s_autoContinue = false;
	if (s_installJob) s_installJob->progress.cancel.store(true, std::memory_order_relaxed);
}
namespace {
// Final automatic step: start and boot unless a Homebrew library is still
// missing, in which case the panel explains it and keeps the intent.
bool ContinueAutoSetup() {
	if (!MissingLibraries(true).empty()) return true;
	s_autoContinue = false;
	return ATUIFujiNetStartAndBoot();
}
} // namespace
bool ATUIFujiNetRecheckLibraries() {
	if (!MissingLibraries(true).empty()) {
		s_error = "Still missing: " + s_missing.front().path + ".";
		return false;
	}
	s_error.clear();
	return s_autoContinue ? ContinueAutoSetup() : true;
}
bool ATUIFujiNetAutoSetup() {
	Load();
	if (!CanChange() || !CanUseNative()) return false;
	if (!ATFujiNetPC::Available()) {
		s_error = "This platform connects to an external FujiNet; FujiNet-PC cannot run here.";
		return false;
	}
	const auto& process = s_runtime.GetStatus();
	const bool connected = Control() && Control()->GetNetSIOStatus().connected;
	if (!process.running && connected) {
		s_error = "Another FujiNet is already connected. Use Other FujiNet, or close its connection first.";
		return false;
	}
	if (s_installJob) return true;
	s_external = false;
	Save();
	if (process.running) {
		s_autoContinue = false;
		return connected ? ATUIFujiNetBoot() : ATUIFujiNetStartAndBoot();
	}
	if (!RuntimeReady()) return ATUIFujiNetInstallRuntime(true);
	s_autoContinue = true;
	return ContinueAutoSetup();
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
	result.filePending = s_fileJob != nullptr;
	result.installing = s_installJob != nullptr;
	if (s_installJob) {
		result.installReceived = s_installJob->progress.received.load(std::memory_order_relaxed);
		result.installTotal = s_installJob->progress.total.load(std::memory_order_relaxed);
	}
	result.autoSetupPending = s_autoContinue;
	result.runtimeVersion = s_runtimeVersion;
	for (const auto& library : MissingLibraries()) result.missingLibraries.push_back(library.path);
	if (s_fileJob) {
		result.fileReceived = s_fileJob->progress.received.load(std::memory_order_relaxed);
		result.fileTotal = s_fileJob->progress.total.load(std::memory_order_relaxed);
	}
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
		if (result.error.empty()) result.error = ListenerError(status.lastError, status.port);
	}
	return result;
}
void ATUIPollFujiNet() {
	bool bootPreparedFile = false;
	std::string picked, game;
	{
		std::lock_guard<std::mutex> lock(s_pickerMutex);
		picked.swap(s_pickedPath);
		game.swap(s_pickedGame);
	}
	if (!picked.empty() && ATUIFujiNetSetRuntime(picked.c_str(), (unsigned)s_webPort)) {
		// A manual pick is not the version the installer recorded.
		s_runtimeVersion.clear();
		Save();
		if (s_autoContinue && RuntimeReady()) ContinueAutoSetup();
	}
	if (s_installJob && s_installJob->done.load(std::memory_order_acquire)) {
		auto result = std::move(s_installJob->result);
		s_installJob.reset();
		if (!result.error.empty()) {
			s_error = std::move(result.error);
			s_autoContinue = false;
		} else if (ATUIFujiNetSetRuntime(result.executable.c_str(), (unsigned)s_webPort)) {
			s_runtimeVersion = result.version;
			Save();
			// Only now that the new version is selected may older ones go.
			ATFujiNetInstall::RemoveOtherVersions(InstallRoot(), s_executable);
#ifdef __APPLE__
			s_missingPath = s_executable;
			s_missing = std::move(result.missing);
#endif
			if (s_autoContinue) ContinueAutoSetup();
		} else {
			s_autoContinue = false;
		}
	}
	if (!game.empty()) ATUIFujiNetRunFile(game.c_str());
	if (s_fileJob) {
		const bool valid = !Locked() && s_runtime.GetStatus().running &&
			!s_runtime.GetStatus().stopping && Control() &&
			Control()->GetNetSIOStatus().connected &&
			s_fileJob->profile == ATSettingsGetCurrentProfileId() &&
			s_fileJob->endpoint == ListenerEndpoint() &&
			ListenerEndpoint() == s_runtimeEndpoint && !LegacyTransport().recognizedLegacy;
		if (!valid) {
			s_fileJob.reset();
			s_error = "FujiNet file operation cancelled because the connection or profile changed. No automatic boot was performed.";
		} else if (s_fileJob->done.load(std::memory_order_acquire)) {
			const bool cancelled = s_fileJob->progress.cancel.load(std::memory_order_relaxed);
			auto result = std::move(s_fileJob->result);
			s_fileJob.reset();
			if (cancelled) s_error = "FujiNet file operation cancelled. Check the web interface if mounting had already begun.";
			else if (!result.error.empty()) s_error = std::move(result.error);
			else if (result.mounted) bootPreparedFile = true;
		}
	}
	const bool wasRunning = s_runtime.GetStatus().running;
	const auto restartsBefore = s_runtime.GetStatus().restarts;
	// A file worker uses private SD/config. Drain output while retaining
	// the runtime lease; reaping/restarting resumes when that worker ends.
	if (s_fileJob) s_runtime.PollOutput();
	else s_runtime.Poll([] {
		// Clear the previous process's UDP identity before its successor
		// sends C1, even if the old process's LEFT datagram was lost.
		if (ListenerEndpoint() == s_runtimeEndpoint && Control() &&
			Control()->GetNetSIOStatus().listening) {
			Control()->Reconnect();
			if (!ListenerReady(*Control()).listening)
				throw std::runtime_error("Could not reconnect NetSIO before restarting FujiNet-PC.");
		}
	});
	if (bootPreparedFile) {
		if (!s_runtime.GetStatus().running ||
			s_runtime.GetStatus().restarts != restartsBefore)
			s_error = "FujiNet-PC restarted or exited while preparing the file. Run the file again after reconnecting; no automatic boot was performed.";
		else if (ATUIFujiNetBoot()) {
			s_fileMessage = "Mounted read-only in FujiNet D1 and booted through SIO.";
			s_runFileOpen = false;
			s_open = false;
		}
	}
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
			// The runtime's own error explains missing libraries in plain words.
			s_error = !s_runtime.GetStatus().error.empty() ? s_runtime.GetStatus().error :
				"FujiNet-PC stopped before connecting. Open Diagnostics > Runtime log for details.";
		} else if (auto* control = Control(); control && control->GetNetSIOStatus().connected) {
			ATUIFujiNetBoot();
		} else if (SDL_GetTicks() >= s_bootDeadline) {
			s_bootPending = false;
			s_error = "FujiNet-PC started but did not connect within 10 seconds. Open Diagnostics > "
				"Runtime log for details, check Connection settings, then try again.";
		}
	}
}
void ATUIShutdownFujiNet() {
	s_installJob.reset();
	s_fileJob.reset();
	s_bootPending = false;
	s_runtime.Shutdown();
	Save();
	s_open = false;
}

namespace {
// The nightly FujiNet-PC tag is replaced on each version bump, so link the
// release search, which lists the current FujiNet-PC Nightly first.
constexpr char kDownloadURL[] =
	"https://github.com/FujiNetWIFI/fujinet-firmware/releases?q=FujiNet-PC+Nightly&expanded=true";

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
void ErrorText(const char* text) {
	const auto background = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
	const bool light = background.x + background.y + background.z > 1.5f;
	ImGui::PushStyleColor(ImGuiCol_Text, light ? ImVec4(0.60f, 0.18f, 0.12f, 1) : ImVec4(1, 0.63f, 0.57f, 1));
	ImGui::TextWrapped("%s", text);
	ImGui::PopStyleColor();
}
// Numbered instruction whose wrapped lines align after the number.
void Step(const char* number, const char* text) {
	ImGui::TextUnformatted(number);
	ImGui::SameLine();
	ImGui::TextWrapped("%s", text);
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
void Tooltip(const char* text) {
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayNormal))
		ImGui::SetTooltip("%s", text);
}
void OpenWebInterface(const ATFujiNetUIStatus& status) {
	const std::string url = !status.running && s_webURL[0] ? s_webURL :
		"http://127.0.0.1:" + std::to_string(s_webPort);
	if (!ATMediaURL::IsURL(url)) s_error = "Enter a complete HTTP(S) web-interface URL.";
	else if (!SDL_OpenURL(url.c_str())) s_error = SDL_GetError();
}
void WebInterfaceButton(const ATFujiNetUIStatus& status) {
	if (ActionButton("Open Web Interface")) OpenWebInterface(status);
	Tooltip("Opens FujiNet's own settings page in your web browser. There you can "
		"mount disks, edit the server list and change FujiNet options.");
}
void OpenFile(const std::string& path) {
	std::string url = "file://";
	if (path[0] != '/') url += '/';
	static constexpr char hex[] = "0123456789ABCDEF";
	for (unsigned char c : path) {
		if (c == '\\') c = '/';
		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
			(c >= '0' && c <= '9') || c == '/' || c == ':' ||
			c == '-' || c == '_' || c == '.' || c == '~') url += (char)c;
		else { url += '%'; url += hex[c >> 4]; url += hex[c & 15]; }
	}
	if (!SDL_OpenURL(url.c_str())) s_error = SDL_GetError();
}
void OpenFolder(const std::string& directory) {
	if (directory.empty()) return;
	if (!SDL_CreateDirectory(directory.c_str())) {
		s_error = SDL_GetError();
		return;
	}
	OpenFile(directory);
}
void OpenDataFolder() { OpenFolder(StateDirectory()); }
void BootNote() {
	Hint("Booting makes FujiNet the Atari's drive 1 and runs at normal speed. "
		"Your own disk images stay inserted; see Emulator drives.");
}
// Plain-language download instructions for this computer. FujiNet-PC
// releases mix 40+ archives for other machines and the physical device's
// firmware; naming the exact file avoids the common wrong download.
// Guide version: numbered steps, each followed by its own button.
void RuntimeSetupSteps() {
	const auto& package = Package();
	ImGui::TextWrapped("FujiNet-PC is a free program that works like a FujiNet device inside "
		"your computer. AltirraSDL starts and stops it for you; you only need to download it once.");
	ImGui::Spacing();
	const float indent = ImGui::GetFontSize() * 1.5f;
	if (package.assetSuffix.empty()) {
		Step("1.", ("Download: there is no official FujiNet-PC build for " + package.system + ".").c_str());
		ImGui::Indent(indent);
		if (!package.requirement.empty()) ErrorText(package.requirement.c_str());
		Hint("A FujiNet-PC you built yourself still works: choose its fujinet program in step 3. "
			"You can also connect a real FujiNet device under Other FujiNet.");
	} else {
		Step("1.", ("Download. Your computer is " + package.system + ". On the download page, "
			"find the newest \"FujiNet-PC Nightly\" and download the file ending in:").c_str());
		ImGui::Indent(indent);
		ImGui::PushFont(ATUIGetFontMono(), 0.0f);
		ImGui::TextWrapped("fujinet-pc-ATARI_..._%s", package.assetSuffix.c_str());
		ImGui::PopFont();
		if (!package.requirement.empty()) ImGui::TextWrapped("%s", package.requirement.c_str());
	}
	if (ActionButton("Download FujiNet-PC")) SDL_OpenURL(kDownloadURL);
	Tooltip("Opens the official FujiNet download page in your web browser.");
	if (!package.assetSuffix.empty()) {
		SameLineIfFits("Copy file ending");
		if (ActionButton("Copy file ending")) ImGui::SetClipboardText(package.assetSuffix.c_str());
		Tooltip("Paste it into your browser's Find (Ctrl+F) on the download page.");
	}
	Hint("Not the fujinet-ATARI-v1.x.zip files from the regular releases: those are firmware "
		"for the physical FujiNet device and contain no PC program.");
	ImGui::Unindent(indent);
	Step("2.", "Extract (unzip) the downloaded file into a folder you will keep, "
		"for example Documents/FujiNet-PC.");
	Step("3.", "Choose the program named fujinet (fujinet.exe on Windows) inside the "
		"extracted fujinet-pc-ATARI folder.");
	ImGui::Indent(indent);
	if (PrimaryAction("Choose FujiNet-PC")) {
		// In the guide, picking the program finishes setup automatically.
		if (s_guided) s_autoContinue = true;
		Browse();
	}
	ImGui::Unindent(indent);
}
enum class Mark { Pending, Working, Done, Failed };
// Checklist row: a drawn status mark (fonts may lack check glyphs) and text.
void StatusLine(Mark mark, const char* text) {
	const float size = ImGui::GetFontSize();
	const ImVec2 pos = ImGui::GetCursorScreenPos();
	auto* draw = ImGui::GetWindowDrawList();
	const ImVec2 c(pos.x + size * 0.5f, pos.y + size * 0.5f + 1);
	const float r = size * 0.42f, line = std::max(1.5f, size * 0.12f);
	const ImU32 accent = ImGui::GetColorU32(ImGuiCol_HeaderActive);
	const ImU32 white = IM_COL32(255, 255, 255, 255);
	switch (mark) {
	case Mark::Done:
		draw->AddCircleFilled(c, r, accent);
		draw->AddLine(ImVec2(c.x - r * 0.5f, c.y), ImVec2(c.x - r * 0.1f, c.y + r * 0.4f), white, line);
		draw->AddLine(ImVec2(c.x - r * 0.1f, c.y + r * 0.4f), ImVec2(c.x + r * 0.5f, c.y - r * 0.4f), white, line);
		break;
	case Mark::Working: {
		const float t = (float)ImGui::GetTime() * 6.0f;
		draw->PathArcTo(c, r * 0.85f, t, t + 4.2f, 16);
		draw->PathStroke(accent, 0, line);
		break;
	}
	case Mark::Failed:
		draw->AddCircleFilled(c, r, IM_COL32(200, 60, 50, 255));
		draw->AddLine(ImVec2(c.x - r * 0.4f, c.y - r * 0.4f), ImVec2(c.x + r * 0.4f, c.y + r * 0.4f), white, line);
		draw->AddLine(ImVec2(c.x - r * 0.4f, c.y + r * 0.4f), ImVec2(c.x + r * 0.4f, c.y - r * 0.4f), white, line);
		break;
	case Mark::Pending:
		draw->AddCircle(c, r * 0.85f, ImGui::GetColorU32(ImGuiCol_TextDisabled), 0, line * 0.8f);
		break;
	}
	ImGui::Dummy(ImVec2(size, size));
	ImGui::SameLine();
	if (mark == Mark::Pending) Hint(text);
	else ImGui::TextWrapped("%s", text);
}
void InstallProgress() {
	if (!s_installJob) return;
	const auto& progress = s_installJob->progress;
	const auto phase = progress.phase.load(std::memory_order_relaxed);
	const uint64_t received = progress.received.load(std::memory_order_relaxed);
	const uint64_t total = progress.total.load(std::memory_order_relaxed);
	ImGui::TextUnformatted(phase == ATFujiNetInstall::Phase::Finding ? "Finding the newest FujiNet-PC..." :
		phase == ATFujiNetInstall::Phase::Downloading ? "Downloading FujiNet-PC..." :
		phase == ATFujiNetInstall::Phase::Verifying ? "Checking the download..." : "Installing...");
	if (phase == ATFujiNetInstall::Phase::Downloading && total) {
		char text[64];
		snprintf(text, sizeof text, "%.1f of %.1f MB", received / 1048576.0, total / 1048576.0);
		ImGui::ProgressBar((float)((double)received / total), ImVec2(-1, 0), text);
	}
	if (ActionButton("Cancel installation")) ATUIFujiNetCancelInstall();
}
// A .command file opens in Terminal, which shows Homebrew's own prompts and
// asks for the password; AltirraSDL never handles it. Used on macOS only,
// but compiled everywhere so every CI build checks it.
[[maybe_unused]] void OpenInTerminal(const std::string& command) {
	const auto directory = StateDirectory();
	const auto path = directory + "/install-fujinet-pc-libraries.command";
	SDL_CreateDirectory(directory.c_str());
	{
		std::ofstream file(path, std::ios::binary | std::ios::trunc);
		file << "#!/bin/bash\necho 'Installing the libraries FujiNet-PC needs...'\n" << command
			<< "\necho\necho 'Finished. Return to AltirraSDL and select Check again.'\n";
		if (!file) {
			s_error = "Could not prepare the Terminal command. Copy it instead.";
			return;
		}
	}
	std::error_code error;
	std::filesystem::permissions(path, std::filesystem::perms::owner_all |
		std::filesystem::perms::group_read | std::filesystem::perms::group_exec, error);
	OpenFile(path);
}
// macOS builds link Homebrew's OpenSSL. It cannot be installed without the
// user's password, so explain it and make the command one click away.
void MissingLibrariesBlock() {
	const auto& missing = MissingLibraries();
	if (missing.empty()) return;
	std::string formulas;
	for (const auto& library : missing)
		if (formulas.find(library.formula) == std::string::npos)
			formulas += (formulas.empty() ? "" : ", ") + library.formula;
	const auto command = ATFujiNetInstall::HomebrewCommand(missing);
	ImGui::TextWrapped("One more step: FujiNet-PC needs %s from Homebrew.", formulas.c_str());
	Hint("Homebrew is the usual free way to add libraries like this to a Mac. Run the command "
		"below in Terminal; it may ask for your Mac password and take a few minutes. Then select "
		"Check again.");
	ImGui::PushFont(ATUIGetFontMono(), 0.0f);
	ImGui::TextWrapped("%s", command.c_str());
	ImGui::PopFont();
#ifdef __APPLE__
	if (PrimaryAction("Install in Terminal")) OpenInTerminal(command);
	Tooltip("Opens Terminal with the command ready to run.");
	SameLineIfFits("Copy command");
#endif
	if (ActionButton("Copy command")) ImGui::SetClipboardText(command.c_str());
	SameLineIfFits("Check again");
	if (ActionButton("Check again")) ATUIFujiNetRecheckLibraries();
	SameLineIfFits("Start anyway");
	if (ActionButton("Start anyway")) {
		s_autoContinue = false;
		ATUIFujiNetStartAndBoot();
	}
	Tooltip("Try to start FujiNet-PC even though the library was not found.");
}
// Full-controls version: actions first so they stay reachable with large
// fonts. Install is the one-click route; Choose covers a manual download.
void RuntimeSetupCompact(const ATFujiNetUIStatus& status) {
	const auto& package = Package();
	if (status.installing) {
		InstallProgress();
		return;
	}
	const bool installable = !package.assetSuffixes.empty();
	if (installable) {
		if (PrimaryAction("Install FujiNet-PC")) ATUIFujiNetInstallRuntime(false);
		Tooltip("Downloads, checks and installs the official FujiNet-PC for this computer.");
		SameLineIfFits("Choose FujiNet-PC");
	}
	if (installable ? ActionButton("Choose FujiNet-PC") : PrimaryAction("Choose FujiNet-PC")) Browse();
	Tooltip("Select the fujinet program from a FujiNet-PC you downloaded and extracted yourself.");
	SameLineIfFits("Download page");
	if (ActionButton("Download page")) SDL_OpenURL(kDownloadURL);
	Tooltip("Opens the official FujiNet download page in your web browser.");
	if (!installable) {
		ImGui::TextWrapped("There is no official FujiNet-PC build for %s.", package.system.c_str());
		if (!package.requirement.empty()) ErrorText(package.requirement.c_str());
		return;
	}
	Hint(("Install downloads the official FujiNet-PC for " + package.system + " from FujiNet's "
		"GitHub page (a few megabytes), checks it and sets it up for AltirraSDL only. To do it "
		"yourself, download the file ending in " + package.assetSuffix + " from \"FujiNet-PC "
		"Nightly\", extract it, and choose the fujinet program inside.").c_str());
	if (!package.requirement.empty()) Hint(package.requirement.c_str());
}
// Why the selection is not the extracted ATARI FujiNet-PC program (device
// firmware, archive, script or another machine), or empty. Filesystem probes
// are refreshed once a second rather than every frame.
const std::string& RuntimeProblem() {
	static std::string path, problem;
	static uint64_t checked = 0;
	if (path != s_executable || SDL_GetTicks() - checked >= 1000) {
		path = s_executable;
		problem = ATFujiNetPCCheckExecutable(path);
		checked = SDL_GetTicks();
	}
	return problem;
}
void RuntimeCheck() {
	if (!RuntimeProblem().empty()) ErrorText(RuntimeProblem().c_str());
}
void ConnectionDetails(const ATFujiNetUIStatus& status) {
	if (s_openConnection) {
		ImGui::SetNextItemOpen(true);
		s_openConnection = false;
	}
	if (!DetailHeader("Connection settings")) return;
	ImGui::PushID("connection");
	ImGui::Indent();
	ImGui::BeginDisabled(Locked() || status.running);
	ImGui::Checkbox("Allow other devices on my network to connect###Allow LAN", &s_lan);
	Hint(s_lan ? "Any computer or FujiNet device on your network may connect. Needed for a "
		"real FujiNet device or a FujiNet-PC on another computer. Allow the port in your firewall." :
		"Only programs on this computer can connect (address 127.0.0.1). Turn this on for a "
		"real FujiNet device or a FujiNet-PC on another computer.");
	const int columns = ImGui::GetContentRegionAvail().x > ImGui::GetFontSize() * 32 ? 2 : 1;
	if (ImGui::BeginTable("##connection-fields", columns, ImGuiTableFlags_SizingStretchSame)) {
		ImGui::TableNextColumn();
		FieldLabel("Port (UDP)");
		ImGui::InputInt("##UDP port", &s_port, 0, 0);
		ImGui::TableNextColumn();
		FieldLabel("Reply timeout (ms)");
		ImGui::InputInt("##Synchronization timeout", &s_timeout, 0, 0);
		ImGui::EndTable();
	}
	Hint("The port must match the one set in FujiNet (normally 9997). The reply timeout is "
		"how long the Atari waits for FujiNet's answer; keep 100 unless told otherwise.");
	if (ActionButton("Apply connection settings"))
		ATUIFujiNetConfigureListener(s_lan, s_port, s_timeout);
	ImGui::EndDisabled();
	if (status.running) Hint("Stop FujiNet-PC before changing these settings.");
	// External mode keeps its open/close buttons in the main view, and a
	// running Local PC shows Reopen there; never draw the same action twice.
	if (!s_external) {
		ImGui::BeginDisabled(Locked());
		if (status.listening) {
			if (ActionButton("Close connection")) ATUIFujiNetDisconnect();
			Tooltip("Stops accepting FujiNet traffic. FujiNet-PC keeps running.");
		} else if (status.present && !status.running) {
			if (ActionButton("Reopen connection")) ATUIFujiNetReconnect();
		}
		ImGui::EndDisabled();
	} else {
		ImGui::Spacing();
		FieldLabel("FujiNet web page address (optional)");
		ImGui::SetNextItemWidth(-1);
		ImGui::InputTextWithHint("##External web URL", "http://fujinet/", s_webURL, sizeof s_webURL);
		if (ImGui::IsItemDeactivatedAfterEdit()) Save();
		Hint("The address of your FujiNet's settings page, for example http://192.168.1.50. "
			"Lets Open Web Interface open it.");
	}
	ImGui::Unindent();
	ImGui::PopID();
}
void LocalDiskDetails(const ATFujiNetUIStatus& status) {
	if (!DetailHeader("Emulator drives")) return;
	ImGui::PushID("drives");
	ImGui::Indent();
	ImGui::TextWrapped("While FujiNet is in use it acts as the Atari's drive 1 (D1:). Booting "
		"through FujiNet therefore switches off the emulator's own D1, turns off Fast boot and "
		"selects normal speed. Your D1 disk image and its unsaved changes stay inserted.");
	ImGui::TextWrapped("Emulator's D1: %s", status.localD1Enabled ? "on" : "off (FujiNet is drive 1)");
	ImGui::TextWrapped("Fast boot: %s", status.fastBoot ? "on" : "off");
	ImGui::BeginDisabled(Locked() || status.localD1Enabled);
	if (ActionButton("Use emulator's D1 again")) ATUIFujiNetEnableLocalD1();
	ImGui::EndDisabled();
	Hint("Drives D2 and up are unchanged, so you can keep local disk images there. Do not give "
		"a local disk and a FujiNet disk the same drive number. BASIC is not changed.");
	ImGui::Unindent();
	ImGui::PopID();
}
void Diagnostics(const ATFujiNetUIStatus& status) {
	if (!DetailHeader("Diagnostics")) return;
	ImGui::PushID("diagnostics");
	ImGui::Indent();
	ImGui::TextWrapped("Connection: %s", status.listening ?
		("open on " + ListenerEndpoint()).c_str() : "closed");
	ImGui::TextWrapped("FujiNet: %s", status.connected ?
		("connected from " + status.peer).c_str() : "not connected");
	ImGui::TextWrapped("Packets: %llu received, %llu sent",
		(unsigned long long)status.rxPackets, (unsigned long long)status.txPackets);
	ImGui::TextWrapped("Dropped: %llu | Reply timeouts: %llu",
		(unsigned long long)status.droppedPackets, (unsigned long long)status.syncTimeouts);
	if (status.processAvailable) {
		ImGui::TextWrapped("Last FujiNet-PC exit code: %d | Restarts: %u", status.exitCode, status.restarts);
		if (ActionButton("Open Data Folder")) OpenDataFolder();
		Tooltip("FujiNet-PC's private settings, log and virtual SD card. Copy your own "
			"games into its SD folder to see them under the SD server in FujiNet's menu.");
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
	ImGui::PopID();
}
void ManualConnection(const ATFujiNetUIStatus& status,
	const ATFujiNetCompat::LegacyDescriptor& legacy) {
	Heading("Manual FujiNet connection");
	Hint("This profile uses your NetSIO custom device and external hub.");
	if (!status.error.empty()) ErrorText(status.error.c_str());
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
		FieldLabel("FujiNet web page address");
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
void PickGame(void*, const char* const* files, int) {
	if (files && files[0]) {
		std::lock_guard lock(s_pickerMutex);
		s_pickedGame = files[0];
	}
}
void RunFileButton(const ATFujiNetUIStatus& status) {
	if (status.running && status.connected && !status.stopping) {
		if (ActionButton("Run a file through FujiNet...")) s_runFileOpen = true;
		Tooltip("Run a game file from this computer, or from a direct web link, through FujiNet.");
		if (!s_fileMessage.empty()) Hint(s_fileMessage.c_str());
	}
}
void RenderFileDialog() {
	if (!s_runFileOpen) return;
	ATUIConstrainDialogSize();
	ImGui::SetNextWindowSize(ImVec2(std::min(620.0f,
		ImGui::GetMainViewport()->WorkSize.x - 24.0f), 0), ImGuiCond_Appearing);
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
		ImGuiCond_Appearing, ImVec2(.5f, .5f));
	if (ImGui::Begin("Run a file through FujiNet", &s_runFileOpen,
		ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize)) {
		ATUIClampDialogPosition();
		Hint("Choose an Atari program (XEX) or disk image (ATR/ATX), or paste a direct download link. "
			"AltirraSDL copies it to FujiNet-PC's virtual SD card, puts it in drive 1 (read-only) "
			"and boots the Atari from it through FujiNet.");
		ImGui::BeginDisabled(s_fileJob != nullptr || Locked());
		if (ActionButton("Choose local game...")) {
			static const SDL_DialogFileFilter filters[] = {
				{"FujiNet games", "xex;com;exe;obx;atr;atx"},
			};
			ATUIShowOpenFileDialog('fngm', PickGame, nullptr, g_pWindow, filters, 1, false);
		}
		FieldLabel("Or use a direct URL");
		ImGui::SetNextItemWidth(-1);
		ImGui::InputTextWithHint("##FujiNet game URL", "https://.../game.xex", s_gameURL, sizeof s_gameURL);
		bool basic = g_sim.IsBASICEnabled();
		if (ImGui::Checkbox("Internal BASIC", &basic)) g_sim.SetBASICEnabled(basic);
		Hint("Many games need BASIC off; turn it off if the game's instructions say to hold OPTION "
			"while booting. Leave it on for BASIC programs. This setting belongs to the current "
			"emulator profile.");
		ImGui::BeginDisabled(!ATMediaURL::IsURL(s_gameURL));
		if (PrimaryAction("Download, mount and boot")) ATUIFujiNetRunFile(s_gameURL);
		ImGui::EndDisabled();
		ImGui::EndDisabled();
		if (s_fileJob) {
			const auto phase = s_fileJob->progress.phase.load(std::memory_order_relaxed);
			const char* phaseText = phase == ATFujiNetMount::Phase::Downloading ? "Downloading game..."
				: phase == ATFujiNetMount::Phase::Copying ? "Copying to virtual SD..."
				: phase == ATFujiNetMount::Phase::CheckingHost ? "Finding the FujiNet SD host..."
				: phase == ATFujiNetMount::Phase::Mounting ? "Mounting read-only in D1..."
				: phase == ATFujiNetMount::Phase::Verifying ? "Checking the mounted file..." : "Preparing game...";
			ImGui::TextUnformatted(phaseText);
			const auto received = s_fileJob->progress.received.load(std::memory_order_relaxed);
			const auto total = s_fileJob->progress.total.load(std::memory_order_relaxed);
			if (total) ImGui::ProgressBar((float)((double)received / total));
			if (ActionButton("Cancel file operation")) ATUIFujiNetCancelFile();
		} else if (ActionButton("Close")) s_runFileOpen = false;
		if (!s_error.empty()) ErrorText(s_error.c_str());
		Hint("FujiNet-PC on this computer must be running and connected. Your own disk images stay "
			"inserted. Use Game Library for ZIP collections; this shortcut accepts single games up to 64 MiB.");
	}
	ImGui::End();
}
// How a connected Atari loads software from FujiNet's CONFIG menu. Keys
// follow fujinet-config's Atari input.c; OPTION's default binding is F4.
void ConfigInstructions() {
	ImGui::TextWrapped("The Atari screen now shows FujiNet's menu (CONFIG) with a list of "
		"servers called HOST LIST. To load a game:");
	Step("1.", "Use the arrow keys to pick a server, for example fujinet.online or "
		"apps.irata.online, and press Return.");
	Step("2.", "Browse folders with the arrow keys and Return. Pick a game ending in .XEX "
		"or .ATR and press Return.");
	Step("3.", "When asked for a drive slot, choose 1 and press Return, then Return again "
		"for read-only.");
	Step("4.", "Press OPTION (F4 on your keyboard) to start the game.");
	Hint("Escape goes back to the server list. If a game does not start, it may need BASIC "
		"turned off: System > Internal BASIC, then Boot Atari. Server contents change over time. "
		"Drag this window aside by its title bar if it covers the Atari screen.");
}
void GuidedWelcome(const ATFujiNetUIStatus& status) {
	const auto& package = Package();
	const bool ready = RuntimeReady();
	Heading("Welcome to FujiNet");
	// Short enough that the button stays visible with large fonts; the
	// details follow it.
	ImGui::TextWrapped("Load Atari games and programs from the internet. No hardware needed.");
	ImGui::Spacing();
	if (status.processAvailable && (ready || !package.assetSuffixes.empty())) {
		ImGui::BeginDisabled(status.connected && !status.running);
		if (PrimaryAction("Set up FujiNet automatically") && ATUIFujiNetAutoSetup())
			s_guidedStep = 2;
		ImGui::EndDisabled();
		Hint(ready ? "FujiNet-PC is already installed. This starts it and restarts the Atari "
			"into FujiNet's menu, where you choose a game." :
			("FujiNet is an add-on that connects the Atari to servers on the internet. This "
			"button downloads the free FujiNet-PC program, which does the same job on this "
			"computer, for " + package.system + " (a few megabytes) from FujiNet's official "
			"GitHub page, checks it, and installs it for AltirraSDL only. Then it starts "
			"FujiNet-PC and restarts the Atari into FujiNet's menu, where you choose a game. "
			"Your disks and settings are kept.").c_str());
		if (!ready && !package.requirement.empty()) Hint(package.requirement.c_str());
	} else if (status.processAvailable) {
		ErrorText(package.requirement.empty() ? "There is no official FujiNet-PC build for "
			"this computer." : package.requirement.c_str());
	}
	ImGui::Spacing();
	Hint("Other ways to connect:");
	if (status.processAvailable) {
		ImGui::BeginDisabled(status.connected && !status.running);
		if (ActionButton("I already downloaded FujiNet-PC")) {
			s_external = false;
			s_autoContinue = true;
			Save();
			Browse();
			s_guidedStep = 2;
		}
		ImGui::EndDisabled();
		SameLineIfFits("I have a FujiNet device or run FujiNet-PC myself");
	}
	ImGui::BeginDisabled(status.running);
	if (ActionButton("I have a FujiNet device or run FujiNet-PC myself")) {
		s_external = true;
		s_selectMode = true;
		s_openConnection = false;
		s_guidedStep = 1;
		Save();
	}
	ImGui::EndDisabled();
}
void GuidedExternal(const ATFujiNetUIStatus& status) {
	Heading("Connect your FujiNet");
	ImGui::TextWrapped("AltirraSDL opens a connection point, and your FujiNet connects "
		"to it. Your FujiNet must have \"SIO over network\" (also called NetSIO or BOIP) "
		"turned on and pointed at this computer.");
	if (!status.listening && PrimaryAction("Open connection"))
		ATUIFujiNetConfigureListener(s_lan, s_port, s_timeout);
	if (status.listening) {
		ImGui::TextWrapped("%s", status.connected ? "FujiNet is connected." :
			("Waiting for FujiNet. In FujiNet's settings use host " +
			std::string(s_lan ? "this computer's IP address" : "127.0.0.1") +
			" and port " + std::to_string(s_port) + ".").c_str());
	}
	Hint("A real FujiNet device or another computer needs \"Allow other devices on my "
		"network\" in Connection settings below.");
	ConnectionDetails(status);
	ImGui::BeginDisabled(!status.connected);
	if (PrimaryAction("Boot and continue") && ATUIFujiNetBoot()) s_guidedStep = 3;
	ImGui::EndDisabled();
	BootNote();
}
// One page for the whole automatic setup: each row shows its own state, so
// the user never has to decide what comes next.
void GuidedProgress(const ATFujiNetUIStatus& status) {
	const bool ready = RuntimeReady();
	const bool booted = status.running && status.connected && !status.bootPending;
	if (booted && !status.installing && !s_autoContinue) {
		s_guidedStep = 3;
		return;
	}
	const bool missing = ready && !status.running && !MissingLibraries().empty();
	const bool failed = !status.error.empty() && !status.installing && !status.bootPending;
	Heading("Setting up FujiNet");
	StatusLine(status.installing ? Mark::Working : ready ? Mark::Done :
		failed ? Mark::Failed : Mark::Pending, "Get FujiNet-PC");
	if (status.installing) {
		ImGui::Indent(ImGui::GetFontSize() * 1.5f);
		InstallProgress();
		ImGui::Unindent(ImGui::GetFontSize() * 1.5f);
	}
	StatusLine(status.running && status.connected ? Mark::Done :
		status.running || status.bootPending ? Mark::Working :
		(failed || missing) && ready ? Mark::Failed : Mark::Pending, "Start FujiNet-PC");
	StatusLine(booted ? Mark::Done : status.bootPending ? Mark::Working : Mark::Pending,
		"Restart the Atari into FujiNet's menu");
	ImGui::Spacing();
	if (missing) {
		MissingLibrariesBlock();
	} else if (failed) {
		ErrorText(status.error.c_str());
		if (PrimaryAction("Try again")) ATUIFujiNetAutoSetup();
		if (!ready && DetailHeader("Download FujiNet-PC yourself instead")) {
			ImGui::Indent();
			RuntimeSetupSteps();
			ImGui::Unindent();
		}
	} else if (!ready && !status.installing) {
		// "I already downloaded FujiNet-PC" and the file dialog is open or
		// was closed without a choice.
		if (s_executable[0]) RuntimeCheck();
		ImGui::TextWrapped("Choose the program named fujinet (fujinet.exe on Windows) inside "
			"the fujinet-pc-ATARI folder you extracted.");
		if (PrimaryAction("Choose FujiNet-PC")) {
			s_autoContinue = true;
			Browse();
		}
		if (!Package().assetSuffixes.empty()) {
			SameLineIfFits("Install automatically instead");
			if (ActionButton("Install automatically instead")) ATUIFujiNetAutoSetup();
		}
	} else if (status.bootPending) {
		Hint("This usually takes a few seconds.");
		if (ActionButton("Cancel boot")) s_bootPending = false;
	}
}
void GuidedReady(const ATFujiNetUIStatus& status) {
	Heading("FujiNet is ready: pick a game");
	if (status.bootPending) {
		Hint("Waiting for FujiNet. The Atari will boot as soon as it connects.");
		if (ActionButton("Cancel boot")) s_bootPending = false;
	} else if (!status.connected) {
		ErrorText("FujiNet is not connected. Go back and set it up again.");
	} else {
		ConfigInstructions();
		ImGui::Spacing();
		if (PrimaryAction("Boot Atari")) ATUIFujiNetBoot();
		Tooltip("Restarts the Atari into FujiNet's menu again.");
		if (status.running || s_webURL[0]) {
			SameLineIfFits("Open Web Interface");
			WebInterfaceButton(status);
		}
		if (status.running) {
			ImGui::Spacing();
			ImGui::TextWrapped("Have a game file on this computer, or a link to one?");
			RunFileButton(status);
			SameLineIfFits("Open virtual SD folder");
			if (ActionButton("Open virtual SD folder")) OpenFolder(StateDirectory() + "/SD");
			Tooltip("Files copied here appear under the SD server in FujiNet's menu.");
		}
	}
	if (ActionButton("Read the CONFIG and XEX guide")) ATUIShowHelpTopic("sdl-fujinet.html", nullptr);
}
// Optional guided path reuses the exact same controller actions as full
// controls. Entering it does not change transport, media or running state.
// Steps: 0 welcome, 1 connect another FujiNet, 2 automatic setup, 3 ready.
void GuidedSetup(const ATFujiNetUIStatus& status,
	const ATFujiNetCompat::LegacyDescriptor& legacy) {
	if (legacy.recognizedLegacy) {
		ManualConnection(status, legacy);
		Hint("Keep your existing hub and FujiNet process running. Boot Atari, then browse a host in CONFIG and mount a game in D1.");
	} else {
		// The automatic-setup page reports errors next to the failed step.
		if (!status.error.empty() && s_guidedStep != 2) ErrorText(status.error.c_str());
		ImGui::BeginDisabled(Locked());
		if (s_guidedStep == 0) GuidedWelcome(status);
		else if (s_guidedStep == 1) GuidedExternal(status);
		else if (s_guidedStep == 2) GuidedProgress(status);
		else GuidedReady(status);
		ImGui::EndDisabled();
		if (s_guidedStep > 0) {
			if (ActionButton("Back")) {
				s_bootPending = false;
				ATUIFujiNetCancelInstall();
				s_guidedStep = s_guidedStep == 3 && s_external ? 1 : 0;
			}
			SameLineIfFits("Full controls");
		}
	}
	if (ActionButton("Full controls")) {
		s_guided = false;
		s_guideDismissed = true;
	}
	SameLineIfFits("Help");
}
void LocalView(const ATFujiNetUIStatus& status, bool needsRuntime) {
	if (needsRuntime) {
		if (s_executable[0]) {
			ImGui::TextWrapped("Selected: %s", s_executable);
			RuntimeCheck();
			ImGui::Spacing();
		}
		RuntimeSetupCompact(status);
		return;
	}
	const std::string executable(s_executable);
	const auto separator = executable.find_last_of("/\\");
	ImGui::TextWrapped("FujiNet-PC: %s", executable.substr(separator == std::string::npos ? 0 : separator + 1).c_str());
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", s_executable);
	if (status.running) {
		SameLineIfFits("Stop FujiNet-PC");
		ImGui::BeginDisabled(status.stopping);
		if (ActionButton("Stop FujiNet-PC")) ATUIFujiNetStopRuntime();
		ImGui::EndDisabled();
	} else {
		SameLineIfFits("Change...");
		if (ActionButton("Change...")) Browse();
		Tooltip("Choose a different fujinet program.");
	}
	if (!status.running) MissingLibrariesBlock();
	ImGui::Spacing();
	ImGui::BeginDisabled(status.stopping || status.bootPending || status.installing);
	if (status.connected) {
		if (PrimaryAction("Boot Atari")) ATUIFujiNetBoot();
		Tooltip("Restarts the Atari into FujiNet's menu (CONFIG). FujiNet-PC keeps running.");
	} else if (PrimaryAction(status.running ? "Boot when connected" : "Start and Boot FujiNet")) {
		ATUIFujiNetStartAndBoot();
	}
	ImGui::EndDisabled();
	if (status.bootPending) {
		SameLineIfFits("Cancel boot");
		if (ActionButton("Cancel boot")) s_bootPending = false;
	} else if (!status.running) {
		SameLineIfFits("Start without booting");
		ImGui::BeginDisabled(status.installing);
		if (ActionButton("Start without booting")) ATUIFujiNetStartRuntime();
		ImGui::EndDisabled();
		Tooltip("Starts FujiNet-PC but leaves the Atari as it is. Use Boot Atari later.");
	}
	if (status.running) {
		SameLineIfFits("Open Web Interface");
		WebInterfaceButton(status);
	}
	if (status.running && !status.listening && !status.stopping) {
		if (ActionButton("Reopen connection")) ATUIFujiNetReconnect();
		Tooltip("FujiNet-PC is running but the emulator stopped listening to it. "
			"This reconnects without restarting the Atari.");
	}
	if (status.bootPending)
		Hint("Starting FujiNet-PC. The Atari will boot as soon as it connects.");
	else if (status.connected)
		Hint("On the Atari, FujiNet's menu lists servers: pick one with the arrow keys and Return, "
			"choose a game, put it in drive 1, then press OPTION (F4). The step-by-step guide "
			"explains it in detail.");
	else if (!status.running)
		Hint("Start and Boot starts FujiNet-PC and restarts the Atari into FujiNet's menu, where "
			"you choose a game from the internet.");
	BootNote();
	RunFileButton(status);
}
void ExternalView(const ATFujiNetUIStatus& status) {
	if (status.connected) {
		ImGui::TextWrapped("FujiNet is connected from %s.", status.peer.c_str());
		if (PrimaryAction("Boot Atari")) ATUIFujiNetBoot();
		Tooltip("Restarts the Atari into FujiNet's menu (CONFIG).");
		SameLineIfFits(s_webURL[0] ? "Open Web Interface" : "Set web address");
		if (s_webURL[0]) WebInterfaceButton(status);
		else if (ActionButton("Set web address")) s_openConnection = true;
		BootNote();
	} else if (!status.listening) {
		ImGui::TextWrapped("AltirraSDL opens a connection point, then your FujiNet connects to "
			"it. Your FujiNet needs \"SIO over network\" (NetSIO, also called BOIP) turned on "
			"in its settings.");
		if (PrimaryAction(status.present ? "Reopen connection" : "Open connection"))
			ATUIFujiNetConfigureListener(s_lan, s_port, s_timeout);
	} else {
		ImGui::TextWrapped("Turn on SIO over network (NetSIO/BOIP) in your FujiNet's settings "
			"and enter:");
		ImGui::Indent(ImGui::GetFontSize() * 1.5f);
		ImGui::TextWrapped("Host: %s", s_lan ? "this computer's IP address" : "127.0.0.1");
		ImGui::TextWrapped("Port: %d", s_port);
		ImGui::Unindent(ImGui::GetFontSize() * 1.5f);
		if (!s_lan)
			Hint("127.0.0.1 only accepts a FujiNet-PC on this computer. For a real FujiNet "
				"device, turn on \"Allow other devices\" in Connection settings.");
	}
	if (status.listening) {
		if (ActionButton("Close connection")) ATUIFujiNetDisconnect();
		Tooltip("Stops accepting FujiNet traffic. The Atari is not reset.");
	}
}
} // namespace

void ATUIRenderFujiNetDialog() {
	if (!s_open) { RenderFileDialog(); return; }
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
		ImGui::BeginDisabled(status.filePending);
#ifdef __EMSCRIPTEN__
		Heading("FujiNet needs a native build");
		Hint("Browsers cannot listen for UDP NetSIO. Use a native AltirraSDL build to connect FujiNet.");
#else
		const auto legacy = LegacyTransport();
		if (s_guided) {
			GuidedSetup(status, legacy);
		} else if (legacy.recognizedLegacy) {
			ManualConnection(status, legacy);
		} else {
			const bool unmanagedPeer = status.connected && !status.running;
			if (unmanagedPeer && !s_external) { s_external = true; s_selectMode = true; }
			const bool selectingMode = ImGui::IsWindowAppearing() || s_selectMode;
			const bool requestedExternal = s_external;
			bool selectedExternal = s_external;
			if (status.processAvailable && ImGui::BeginTabBar("##FujiNetMode")) {
				ImGui::BeginDisabled(unmanagedPeer);
				const bool localTab = ImGui::BeginTabItem("This computer", nullptr, selectingMode && !requestedExternal ? ImGuiTabItemFlags_SetSelected : 0);
#ifdef IMGUI_ENABLE_TEST_ENGINE
				IMGUI_TEST_ENGINE_ITEM_INFO(ImGui::GetItemID(), "This computer", 0);
#endif
				if (localTab) {
					if (!selectingMode) selectedExternal = false;
					ImGui::EndTabItem();
				}
				ImGui::EndDisabled();
				if (unmanagedPeer && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
					ImGui::SetTooltip("Close the other FujiNet's connection before starting FujiNet-PC here.");
				else
					Tooltip("AltirraSDL starts the free FujiNet-PC program on this computer.");
				ImGui::BeginDisabled(status.running);
				const bool externalTab = ImGui::BeginTabItem("Other FujiNet", nullptr, selectingMode && requestedExternal ? ImGuiTabItemFlags_SetSelected : 0);
#ifdef IMGUI_ENABLE_TEST_ENGINE
				IMGUI_TEST_ENGINE_ITEM_INFO(ImGui::GetItemID(), "Other FujiNet", 0);
#endif
				if (externalTab) {
					if (!selectingMode) selectedExternal = true;
					ImGui::EndTabItem();
				}
				ImGui::EndDisabled();
				if (status.running && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
					ImGui::SetTooltip("Stop FujiNet-PC on this computer first.");
				else
					Tooltip("A real FujiNet device, or a FujiNet-PC you start yourself.");
				ImGui::EndTabBar();
				s_selectMode = false;
				if (selectedExternal != s_external) {
					s_external = selectedExternal;
					s_openConnection = s_external && !status.present;
					Save();
				}
				Hint(s_external ?
					"A real FujiNet device, or a FujiNet-PC you start yourself." :
					"Recommended: AltirraSDL runs FujiNet-PC for you. No hardware needed.");
			}
			ImGui::Spacing();
			// A wrong selection (often the device firmware) returns to the
			// download steps; a running process is never second-guessed.
			const bool needsRuntime = !s_external && !status.running &&
				(!s_executable[0] || !RuntimeProblem().empty());
			const char* title = status.stopping ? "Stopping FujiNet-PC" :
				status.connected ? "Connected to FujiNet" : status.bootPending ? "Starting FujiNet" :
				needsRuntime && s_executable[0] ? "Choose the FujiNet-PC program" :
				needsRuntime ? "Get FujiNet-PC" : status.running ? "Waiting for FujiNet" :
				status.listening && s_external ? "Waiting for FujiNet" : s_external ? "Connect your FujiNet" : "Ready to start";
			Heading(title);
			if (legacy.suspectedLegacy) {
				Hint(legacy.label.c_str());
				Hint("A custom device may already provide NetSIO. Check it before enabling a second SIO route.");
			}
			if (!status.error.empty()) ErrorText(status.error.c_str());
			ImGui::Spacing();
			ImGui::BeginDisabled(Locked());
			if (!s_external) LocalView(status, needsRuntime);
			else ExternalView(status);
			ImGui::EndDisabled();
			if (Locked()) Hint("FujiNet changes are disabled during netplay.");
			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Spacing();
			if (!s_external && DetailHeader("FujiNet-PC program")) {
				ImGui::PushID("runtime");
				ImGui::Indent();
				Hint("The fujinet program inside the extracted fujinet-pc-ATARI folder. AltirraSDL "
					"keeps FujiNet-PC's settings and virtual SD card in its own data folder.");
				ImGui::BeginDisabled(Locked() || status.running);
				FieldLabel("Program location");
				ImGui::InputText("##Executable", s_executable, sizeof s_executable);
				if (ImGui::IsItemDeactivatedAfterEdit()) Save();
				FieldLabel("Web page port");
				ImGui::InputInt("##Local web port", &s_webPort, 0, 0);
				if (ImGui::IsItemDeactivatedAfterEdit()) {
					s_webPort = std::clamp(s_webPort, 1, 65535);
					Save();
				}
				Hint("Open Web Interface opens http://127.0.0.1 at this port. Change it only if "
					"another program already uses it (default 8000).");
				ImGui::EndDisabled();
				if (!s_runtimeVersion.empty())
					ImGui::TextWrapped("Installed by AltirraSDL: %s", s_runtimeVersion.c_str());
				if (!needsRuntime) {
					if (status.installing) {
						InstallProgress();
					} else {
						ImGui::BeginDisabled(Locked() || status.running || Package().assetSuffixes.empty());
						if (ActionButton("Install latest FujiNet-PC")) ATUIFujiNetInstallRuntime(false);
						ImGui::EndDisabled();
						Tooltip(status.running ? "Stop FujiNet-PC first." :
							"Downloads the newest official FujiNet-PC and switches to it. Your FujiNet "
							"settings, server list and SD card are kept.");
					}
				}
				ImGui::Unindent();
				ImGui::PopID();
			}
			ConnectionDetails(status);
			LocalDiskDetails(status);
			Diagnostics(status);
		}
#endif
		ImGui::EndDisabled();
		if (status.filePending) {
			Hint("Preparing a game for FujiNet. The runtime stays connected until the operation finishes.");
			if (ActionButton("Cancel file operation")) ATUIFujiNetCancelFile();
		}
#ifndef __EMSCRIPTEN__
		if (!s_guided) {
			if (ActionButton("Step-by-step guide")) { s_guided = true; s_guidedStep = 0; }
			Tooltip("Walks you through setting up FujiNet and loading your first game.");
			SameLineIfFits("Help");
		}
#endif
		if (ImGui::Button("Help"))
			ATUIShowHelpTopic("sdl-fujinet.html", nullptr);
	}
	ImGui::End();
	ImGui::PopStyleVar(2);
	RenderFileDialog();
}
