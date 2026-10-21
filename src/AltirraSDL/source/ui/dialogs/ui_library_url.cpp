// Fork extension requested for importing games and multi-GB game packs.
// Downloads only touch private files; library mutations run on the UI thread.
#include <stdafx.h>
#include "ui_library_url.h"
#include "ui_main.h"
#include "ui_main_internal.h"
#include "ui/gamelibrary/game_library.h"
#include "media/library_url.h"
#include "media/media_url.h"
#include "media/http_client.h"
#include "ui_file_dialog_sdl3.h"
#include <SDL3/SDL.h>
#include <imgui.h>
#include <vd2/system/registry.h>
#include <vd2/system/text.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <memory>
#include <mutex>
#include <thread>

extern SDL_Window* g_pWindow;
extern VDStringA ATGetConfigDir();
extern void ATRegistryFlushToDisk();
extern void GameBrowser_Init();
extern void GameBrowser_Invalidate();
extern ATGameLibrary* GetGameLibrary();

namespace {
struct Job {
	ATLibraryURL::Progress progress;
	ATLibraryURL::Result result;
	std::atomic<bool> done{false};
	std::thread worker;
	~Job() {
		progress.cancel.store(true, std::memory_order_relaxed);
		if (worker.joinable()) worker.join();
	}
};
std::unique_ptr<Job> s_job;
ATUILibraryURLStatus s_status;
bool s_loaded = false;
bool s_awaitScan = false;
char s_url[4096] = {};
std::mutex s_pickerMutex;
std::string s_pickedDirectory;

void Load() {
	if (s_loaded) return;
	s_loaded = true;
	VDStringW directory;
	VDRegistryAppKey key("Game Library Downloads", false);
	key.getString("Directory", directory);
	s_status.destination = VDTextWToU8(directory).c_str();
	if (s_status.destination.empty()) {
		const auto config = ATGetConfigDir();
		if (!config.empty())
			s_status.destination = std::string(config.c_str()) + "/library-downloads";
	}
}
void PickDirectory(void*, const char* const* files, int) {
	if (files && files[0]) {
		std::lock_guard lock(s_pickerMutex);
		s_pickedDirectory = files[0];
	}
}
void SameLineIfFits(const char* label) {
	const auto& style = ImGui::GetStyle();
	const float used = ImGui::GetItemRectMax().x - ImGui::GetCursorScreenPos().x;
	const float width = ImGui::CalcTextSize(label).x + style.FramePadding.x * 2;
	if (used + style.ItemSpacing.x + width <= ImGui::GetContentRegionAvail().x)
		ImGui::SameLine();
}
} // namespace

void ATUIShowLibraryURLDialog() {
	Load();
	s_status.open = true;
}
bool ATUISetLibraryURLDestination(const char* path) {
	Load();
	if (s_job || !path || !*path) return false;
	const auto config = ATGetConfigDir();
	if (config.empty() || !SDL_CreateDirectory(config.c_str())) {
		s_status.error = "Could not create the emulator settings directory.";
		return false;
	}
	s_status.destination = path;
	VDRegistryAppKey key("Game Library Downloads", true);
	key.setString("Directory", VDTextU8ToW(VDStringSpanA(path)).c_str());
	// Source settings are flushed after a successful import. On Android,
	// a folder choice must also survive OS process termination beforehand.
#ifdef __ANDROID__
	ATRegistryFlushToDisk();
#endif
	return true;
}
bool ATUIStartLibraryURLImport(const char* url) {
	ATUIShowLibraryURLDialog();
	if (s_job) return false;
	s_status.error.clear();
	s_status.path.clear();
	s_status.files = 0;
	s_status.added = false;
	s_status.received = s_status.total = 0;
	if (!ATHttp::Available()) {
		s_status.error = "URL imports are unavailable in this build.";
		return false;
	}
	if (!url || !ATMediaURL::IsURL(url)) {
		s_status.error = "Enter a direct HTTP or HTTPS download URL.";
		return false;
	}
	if (s_status.destination.empty()) {
		s_status.error = "Choose a download folder first.";
		return false;
	}
	const std::string address = url;
	snprintf(s_url, sizeof s_url, "%s", address.c_str());
	auto job = std::make_unique<Job>();
	auto* raw = job.get();
	const std::string destination = s_status.destination;
	try {
		raw->worker = std::thread([raw, address, destination] {
			raw->result = ATLibraryURL::Download(address, destination, raw->progress);
			raw->done.store(true, std::memory_order_release);
		});
		s_job = std::move(job);
		return true;
	} catch (const std::exception& e) {
		s_status.error = e.what();
		return false;
	}
}
void ATUICancelLibraryURLImport() {
	if (s_job) s_job->progress.cancel.store(true, std::memory_order_relaxed);
}
ATUILibraryURLStatus ATUIGetLibraryURLStatus() {
	Load();
	auto status = s_status;
	status.scanning = s_awaitScan;
	status.pending = s_job != nullptr;
	if (s_job) {
		status.cancelling = s_job->progress.cancel.load(std::memory_order_relaxed);
		status.received = s_job->progress.received.load(std::memory_order_relaxed);
		status.total = s_job->progress.total.load(std::memory_order_relaxed);
	}
	return status;
}
void ATUIPollLibraryURLImport() {
	if (s_awaitScan) {
		if (auto* lib = GetGameLibrary()) {
			if (lib->IsScanComplete()) {
				lib->ConsumeScanResults();
				GameBrowser_Invalidate();
			}
			if (!lib->IsScanning()) s_awaitScan = false;
		}
	}
	std::string directory;
	{
		std::lock_guard lock(s_pickerMutex);
		directory.swap(s_pickedDirectory);
	}
	if (!directory.empty()) ATUISetLibraryURLDestination(directory.c_str());
	if (!s_job || !s_job->done.load(std::memory_order_acquire)) return;
	const auto status = ATUIGetLibraryURLStatus();
	auto result = std::move(s_job->result);
	s_job.reset();
	s_status.received = status.received;
	s_status.total = status.total;
	if (status.cancelling) {
		s_status.error = "Import cancelled. Your library was not changed.";
		return;
	}
	if (!result.error.empty()) {
		s_status.error = std::move(result.error);
		s_status.open = true;
		return;
	}
	GameBrowser_Init();
	auto* lib = GetGameLibrary();
	if (!lib) {
		s_status.error = "The Game Library is unavailable. The downloaded file was kept.";
		s_status.open = true;
		return;
	}
	// A running scan reads the source list without a mutex. Join it before
	// changing sources, and consume any completed history/metadata results.
	if (lib->IsScanComplete()) lib->ConsumeScanResults();
	lib->CancelScan();
	auto sources = lib->GetSources();
	const bool duplicate = std::any_of(sources.begin(), sources.end(),
		[&](const GameSource& source) { return source.mPath == result.path; });
	if (!duplicate) {
		GameSource source;
		source.mPath = result.path;
		source.mbIsArchive = result.archive;
		source.mbIsFile = !result.archive;
		sources.push_back(std::move(source));
		lib->SetSources(std::move(sources));
		lib->SaveSettingsToRegistry();
		ATRegistryFlushToDisk();
	}
	lib->StartScan();
	s_awaitScan = true;
	GameBrowser_Invalidate();
	s_status.added = !duplicate;
	s_status.files = result.supportedFileCount;
	s_status.path = VDTextWToU8(result.path).c_str();
}
void ATUIRenderLibraryURLDialog() {
	if (!s_status.open) return;
	ATUIConstrainDialogSize();
	ImGui::SetNextWindowSize(ImVec2(std::min(620.0f,
		ImGui::GetMainViewport()->WorkSize.x - 24.0f), 0), ImGuiCond_Appearing);
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
		ImGuiCond_Appearing, ImVec2(.5f, .5f));
	if (ImGui::Begin("Add to Game Library from URL", &s_status.open,
		ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize)) {
		ATUIClampDialogPosition();
		const auto status = ATUIGetLibraryURLStatus();
		ImGui::TextWrapped("Add a game file or a ZIP game pack. Packs stay as archives; the library lists the games inside.");
		ImGui::BeginDisabled(status.pending);
		ImGui::SetNextItemWidth(-1);
		ImGui::InputTextWithHint("##Library URL", "https://...", s_url, sizeof s_url);
		ImGui::EndDisabled();
		if (status.pending) {
			if (status.total)
				ImGui::ProgressBar((float)((double)status.received / status.total));
			ImGui::Text("%.2f GiB downloaded%s", (double)status.received / (1024*1024*1024),
				status.cancelling ? " (cancelling...)" : "");
			ImGui::BeginDisabled(status.cancelling);
			if (ImGui::Button("Cancel Import")) ATUICancelLibraryURLImport();
			ImGui::EndDisabled();
			SameLineIfFits("Hide");
			if (ImGui::Button("Hide")) s_status.open = false;
			ImGui::TextWrapped("You can close this window and keep using the emulator. Open Add from URL again to see progress.");
		} else {
			ImGui::BeginDisabled(!ATHttp::Available());
			if (ImGui::Button("Import")) ATUIStartLibraryURLImport(s_url);
			ImGui::EndDisabled();
			SameLineIfFits("Close");
			if (ImGui::Button("Close")) s_status.open = false;
			if (!status.path.empty()) {
				ImGui::TextWrapped(status.added ? "Added %llu game files to your library."
					: "This download is already in your library (%llu game files).",
					(unsigned long long)status.files);
				if (status.scanning) ImGui::TextUnformatted("Scanning the download...");
			}
		}
		if (ImGui::CollapsingHeader("Download location")) {
			ImGui::TextWrapped("%s", status.destination.c_str());
			ImGui::TextWrapped("Choose a drive with enough free space for the pack. Removing a library source keeps the downloaded file.");
			ImGui::BeginDisabled(status.pending);
			if (ImGui::Button("Choose Folder..."))
				ATUIShowOpenFolderDialog('lurl', PickDirectory, nullptr, g_pWindow);
			ImGui::EndDisabled();
		}
		if (!status.error.empty()) ImGui::TextWrapped("%s", status.error.c_str());
		if (!ATHttp::Available())
			ImGui::TextWrapped("Native URL imports need a supported HTTP backend. Browser builds use their existing game-pack installer.");
		if (ImGui::Button("Import help")) ATUIShowHelpTopic("sdl-remote-media.html", nullptr);
	}
	ImGui::End();
}
void ATUIShutdownLibraryURLImport() { s_job.reset(); }
