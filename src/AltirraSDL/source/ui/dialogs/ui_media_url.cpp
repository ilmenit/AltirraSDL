// Fork extension explicitly requested for remote media. Windows Altirra's
// adjacent File > Open Image item remains in its canonical menu position.
#include <stdafx.h>
#include "ui_media_url.h"
#include "ui_main.h"
#include "ui_main_internal.h"
#include "../../media/media_url.h"
#include "../../media/http_client.h"
#include <vd2/system/VDString.h>
#include <imgui.h>
#include <atomic>
#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>

extern VDStringA ATGetConfigDir();

namespace {
struct DownloadJob {
	ATMediaURL::Progress progress;
	ATMediaURL::Result result;
	std::atomic<bool> done{false};
	bool boot = false;
	uint32 profile = kATProfileId_Invalid;
	std::thread worker;
	~DownloadJob() {
		progress.cancel.store(true, std::memory_order_relaxed);
		if (worker.joinable()) worker.join();
	}
};

std::unique_ptr<DownloadJob> s_job;
bool s_open = false;
char s_url[4096] = {};
std::string s_error;

void SameLineIfFits(const char* label) {
	const auto& style = ImGui::GetStyle();
	const float used = ImGui::GetItemRectMax().x - ImGui::GetCursorScreenPos().x;
	const float width = ImGui::CalcTextSize(label).x + style.FramePadding.x * 2;
	if (used + style.ItemSpacing.x + width <= ImGui::GetContentRegionAvail().x)
		ImGui::SameLine();
}

void Start(const std::string& url, bool boot, uint32 profile = kATProfileId_Invalid) {
	if (s_job)
		return;
	s_error.clear();
	if (!ATHttp::Available()) {
		s_error = "HTTP(S) downloads are unavailable in this build.";
		return;
	}
	if (!ATMediaURL::IsURL(url)) {
		s_error = "Enter a complete HTTP or HTTPS image URL (encode spaces as %20).";
		return;
	}
	const VDStringA config = ATGetConfigDir();
	if (config.empty()) {
		s_error = "The configuration directory is unavailable.";
		return;
	}
	const std::string cache = std::string(config.c_str()) + "/downloads";
	auto job = std::make_unique<DownloadJob>();
	job->boot = boot;
	job->profile = profile;
	DownloadJob* raw = job.get();
	try {
		raw->worker = std::thread([raw, url, cache] {
			raw->result = ATMediaURL::Download(url, cache, raw->progress);
			raw->done.store(true, std::memory_order_release);
		});
		s_job = std::move(job);
	} catch (const std::exception& error) {
		s_error = error.what();
	}
}
} // namespace

void ATUIShowMediaURLDialog() {
	s_open = true;
}

bool ATUIIsMediaURLDownloadPending() { return s_job != nullptr; }
const char* ATUIGetMediaURLDownloadError() { return s_error.c_str(); }

void ATUIStartMediaURLDownload(const char* url, bool boot, uint32 profile) {
	s_open = true;
	if (s_job) {
		s_error = "An image download is already in progress.";
		return;
	}
	snprintf(s_url, sizeof s_url, "%s", url ? url : "");
	Start(url ? url : "", boot, profile);
}

void ATUIPollMediaURLDownload() {
	if (!s_job || !s_job->done.load(std::memory_order_acquire))
		return;
	const bool cancelled = s_job->progress.cancel.load(std::memory_order_relaxed);
	const bool boot = s_job->boot;
	const uint32 profile = s_job->profile;
	ATMediaURL::Result result = std::move(s_job->result);
	s_job.reset();
	if (cancelled)
		return;
	if (!result.error.empty()) {
		s_error = std::move(result.error);
		s_open = true;
		return;
	}
	s_open = false;
	// Only the normal main-thread loader changes media/hardware/run state.
	// Its compatibility, cartridge selection, writable-disk state and
	// netplay checks apply equally to downloaded images.
	if (boot && profile != kATProfileId_Invalid)
		ATUIBootWithProfile(result.path.c_str(), profile);
	else
		ATUIPushDeferred(boot ? kATDeferred_BootImage : kATDeferred_OpenImage,
			result.path.c_str());
}

void ATUIRenderMediaURLDialog() {
	if (!s_open)
		return;
	ATUIConstrainDialogSize();
	ImGui::SetNextWindowSize(ImVec2(std::min(620.0f,
		ImGui::GetMainViewport()->WorkSize.x - 24.0f), 0), ImGuiCond_Appearing);
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
		ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	if (ImGui::Begin("Open Image from URL", &s_open,
		ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize)) {
		ATUIClampDialogPosition();
		ImGui::TextWrapped("Paste a direct HTTP(S) image, ZIP or gzip URL.");
		ImGui::BeginDisabled(s_job != nullptr || !ATHttp::Available());
		ImGui::SetNextItemWidth(-1);
		ImGui::InputText("##image-url", s_url, sizeof s_url);
		if (ImGui::Button("Open Image"))
			Start(s_url, false);
		SameLineIfFits("Boot Image");
		if (ImGui::Button("Boot Image"))
			Start(s_url, true);
		ImGui::EndDisabled();
		SameLineIfFits(s_job ? "Cancel Download" : "Close");
		if (ImGui::Button(s_job ? "Cancel Download" : "Close")) {
			if (s_job)
				s_job->progress.cancel.store(true, std::memory_order_relaxed);
			else
				s_open = false;
		}
		ImGui::TextWrapped("Downloads are limited to 64 MiB and retained in the downloads cache.");
		if (s_job) {
			const auto received = s_job->progress.received.load(std::memory_order_relaxed);
			const auto total = s_job->progress.total.load(std::memory_order_relaxed);
			if (total)
				ImGui::ProgressBar((float)((double)received / total));
			ImGui::Text("%.2f MiB received%s", (double)received / (1024 * 1024),
				s_job->progress.cancel.load(std::memory_order_relaxed) ? " (cancelling...)" : "");
		}
		if (!ATHttp::Available())
			ImGui::TextWrapped("HTTP(S) downloads are unavailable in this build.");
		if (ImGui::Button("URL help"))
			ATUIShowHelpTopic("sdl-remote-media.html", nullptr);
		if (!s_error.empty())
			ImGui::TextWrapped("%s", s_error.c_str());
	}
	ImGui::End();
	// Closing via the titlebar never leaves an invisible download that can
	// later replace the user's mounted image.
	if (!s_open && s_job)
		s_job->progress.cancel.store(true, std::memory_order_relaxed);
}

void ATUIShutdownMediaURLDownload() {
	s_job.reset();
	s_open = false;
}
