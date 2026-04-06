//	AltirraSDL - Device configuration browse helpers
//	Async SDL3 file/folder dialogs shared by all Render*Config functions.
//	Split out of ui_devconfig_devices.cpp (Phase 2h).

#include <stdafx.h>
#include <SDL3/SDL.h>
#include <imgui.h>
#include <mutex>
#include <string>
#include <vector>
#include <cstring>

#include "ui_devconfig_internal.h"

namespace {

struct DevBrowsePending {
	char *buf;
	int maxLen;
	std::string path;
};

std::mutex s_devBrowseMutex;
std::vector<DevBrowsePending> s_devBrowsePending;

struct DevBrowseTarget {
	char *buf;
	int maxLen;
};

void DevBrowseFileCallback(void *userdata, const char * const *filelist, int /*filter*/) {
	auto *tgt = (DevBrowseTarget *)userdata;
	if (filelist && filelist[0] && tgt && tgt->buf) {
		std::lock_guard<std::mutex> lock(s_devBrowseMutex);
		s_devBrowsePending.push_back({tgt->buf, tgt->maxLen, filelist[0]});
	}
	delete tgt;
}

void DevBrowseFolderCallback(void *userdata, const char * const *filelist, int /*filter*/) {
	auto *tgt = (DevBrowseTarget *)userdata;
	if (filelist && filelist[0] && tgt && tgt->buf) {
		std::lock_guard<std::mutex> lock(s_devBrowseMutex);
		s_devBrowsePending.push_back({tgt->buf, tgt->maxLen, filelist[0]});
	}
	delete tgt;
}

} // namespace

void DevBrowseApplyPending() {
	std::lock_guard<std::mutex> lock(s_devBrowseMutex);
	for (auto &p : s_devBrowsePending) {
		if (p.buf) {
			strncpy(p.buf, p.path.c_str(), p.maxLen - 1);
			p.buf[p.maxLen - 1] = 0;
		}
	}
	s_devBrowsePending.clear();
}

void DevBrowseForFile(char *buf, int maxLen,
	const SDL_DialogFileFilter *filters, int filterCount)
{
	auto *tgt = new DevBrowseTarget{buf, maxLen};
	SDL_ShowOpenFileDialog(DevBrowseFileCallback, tgt,
		SDL_GetKeyboardFocus(), filters, filterCount, nullptr, false);
}

void DevBrowseForFolder(char *buf, int maxLen) {
	auto *tgt = new DevBrowseTarget{buf, maxLen};
	SDL_ShowOpenFolderDialog(DevBrowseFolderCallback, tgt,
		SDL_GetKeyboardFocus(), nullptr, false);
}

void DevBrowseForSaveFile(char *buf, int maxLen,
	const SDL_DialogFileFilter *filters, int filterCount)
{
	auto *tgt = new DevBrowseTarget{buf, maxLen};
	SDL_ShowSaveFileDialog(DevBrowseFileCallback, tgt,
		SDL_GetKeyboardFocus(), filters, filterCount, nullptr);
}

bool InputTextWithBrowse(const char *label, char *buf, int bufSize, const char *browseId) {
	ImGui::InputText(label, buf, bufSize);
	ImGui::SameLine();
	ImGui::PushID(browseId);
	bool clicked = ImGui::SmallButton("...");
	ImGui::PopID();
	return clicked;
}
