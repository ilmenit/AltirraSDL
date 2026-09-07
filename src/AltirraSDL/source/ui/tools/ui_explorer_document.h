// Shared document behavior for the SDL Explorer tools.
// User-requested extension of the Windows Disk Explorer: independent documents,
// file-first opening, and consistent contents/preview interactions.
#ifndef f_ATUI_EXPLORER_DOCUMENT_H
#define f_ATUI_EXPLORER_DOCUMENT_H

#include <algorithm>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <imgui.h>
#include <imgui_internal.h>
#include <vd2/system/filesys.h>
#include <vd2/system/file.h>
#include <vd2/system/error.h>
#include <vd2/system/text.h>
#include "ui_file_dialog_sdl3.h"

struct ATUIExplorerMailbox {
	struct Result {
		SDL_DialogFileCallback callback;
		void *userdata;
		std::vector<std::string> paths;
		int filter;
	};
	std::mutex mutex;
	std::vector<Result> results;
};

struct ATUIExplorerDocument {
	inline static unsigned nextId = 0;
	unsigned id = ++nextId;
	bool open = true;
	bool requestClose = false;
	bool focus = true;
	bool dialogPending = false;
	float split = 0.42f;
	std::string source;
	ImGuiID windowId = 0;
	std::shared_ptr<ATUIExplorerMailbox> mailbox = std::make_shared<ATUIExplorerMailbox>();

	std::string Title(const char *type) const {
		const auto slash = source.find_last_of("/\\");
		return source.substr(slash == std::string::npos ? 0 : slash + 1)
			+ " - " + type + "###Explorer" + std::to_string(id);
	}
	std::string ToolTitle(const char *label) const {
		return std::string(label) + " - " + source + "###Explorer"
			+ std::to_string(id) + label;
	}
	void Prepare() {
		ImGui::SetNextWindowSize(ImVec2(900, 680), ImGuiCond_FirstUseEver);
		const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
		const float offset = ((id - 1) % 6) * 22.0f;
		ImGui::SetNextWindowPos(ImVec2(center.x + offset, center.y + offset),
			ImGuiCond_FirstUseEver, ImVec2(.5f, .5f));
		if (focus) { ImGui::SetNextWindowFocus(); focus = false; }
	}
	void Track() { windowId = ImGui::GetCurrentWindow()->ID; }
	bool Hit(float x, float y, ImVec2 *pos = nullptr, ImVec2 *size = nullptr) const {
		if (!open || !windowId) return false;
		ImGuiContext& ctx = *ImGui::GetCurrentContext();
		if (ImGui::GetTopMostPopupModal()) return false;
		for (int i = ctx.Windows.Size - 1; i >= 0; --i) {
			ImGuiWindow *w = ctx.Windows[i];
			if (!w->WasActive || w->Hidden || (w->Flags & ImGuiWindowFlags_NoMouseInputs)) continue;
			if (!w->OuterRectClipped.Contains(ImVec2(x, y))) continue;
			if (w->RootWindow->ID != windowId) return false;
			if (pos) *pos = w->RootWindow->Pos;
			if (size) *size = w->RootWindow->Size;
			return true;
		}
		return false;
	}
	void Drain() {
		std::vector<ATUIExplorerMailbox::Result> results;
		{ std::lock_guard<std::mutex> lock(mailbox->mutex); results.swap(mailbox->results); }
		for (auto& r : results) {
			dialogPending = false;
			std::vector<const char *> paths;
			for (const auto& p : r.paths) paths.push_back(p.c_str());
			paths.push_back(nullptr);
			r.callback(r.userdata, paths.data(), r.filter);
		}
	}
};

// Only this trampoline runs on the dialog thread. No UI/emulator objects are
// accessed there; closing a document safely expires its outstanding request.
struct ATUIExplorerDialogRequest {
	std::weak_ptr<ATUIExplorerMailbox> mailbox;
	SDL_DialogFileCallback callback;
	void *userdata;
};
inline void ATUIExplorerDialogCallback(void *userdata, const char * const *paths, int filter) {
	std::unique_ptr<ATUIExplorerDialogRequest> request(static_cast<ATUIExplorerDialogRequest *>(userdata));
	if (auto box = request->mailbox.lock()) {
		ATUIExplorerMailbox::Result r{request->callback, request->userdata, {}, filter};
		if (paths) for (; *paths; ++paths) r.paths.emplace_back(*paths);
		std::lock_guard<std::mutex> lock(box->mutex);
		box->results.push_back(std::move(r));
	}
}
inline void ATUIExplorerFileDialog(ATUIExplorerDocument& doc, bool save, long key,
	SDL_DialogFileCallback callback, void *userdata, SDL_Window *window,
	const SDL_DialogFileFilter *filters, int count, bool many = false) {
	if (doc.dialogPending) return;
	doc.dialogPending = true;
	auto *request = new ATUIExplorerDialogRequest{doc.mailbox, callback, userdata};
	if (save) ATUIShowSaveFileDialog(key, ATUIExplorerDialogCallback, request, window, filters, count, nullptr, true);
	else ATUIShowOpenFileDialog(key, ATUIExplorerDialogCallback, request, window, filters, count, many, nullptr, true);
}
inline void ATUIExplorerFolderDialog(ATUIExplorerDocument& doc, long key,
	SDL_DialogFileCallback callback, void *userdata, SDL_Window *window) {
	if (doc.dialogPending) return;
	doc.dialogPending = true;
	auto *request = new ATUIExplorerDialogRequest{doc.mailbox, callback, userdata};
	ATUIShowOpenFolderDialog(key, ATUIExplorerDialogCallback, request, window, nullptr, false, true);
}

template<class T> struct ATUIExplorerScope {
	T *&slot;
	T *previous;
	ATUIExplorerScope(T *&s, T *value) : slot(s), previous(s) { slot = value; }
	~ATUIExplorerScope() { slot = previous; }
};

inline std::string ATUIExplorerFullPath(const char *path) {
	return VDTextWToU8(VDGetFullPath(VDTextU8ToW(path, -1).c_str())).c_str();
}
inline float ATUIExplorerListHeight(ATUIExplorerDocument& doc) {
	return std::max(70.0f, ImGui::GetContentRegionAvail().y * doc.split);
}
inline void ATUIExplorerSplitter(ATUIExplorerDocument& doc, float total) {
	ImGui::InvisibleButton("##PreviewSplitter", ImVec2(-1, 6));
	if (ImGui::IsItemHovered() || ImGui::IsItemActive())
		ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
	if (ImGui::IsItemActive() && total > 0)
		doc.split = std::clamp(doc.split + ImGui::GetIO().MouseDelta.y / total, .2f, .7f);
	ImGui::GetWindowDrawList()->AddLine(ImGui::GetItemRectMin(),
		ImVec2(ImGui::GetItemRectMax().x, ImGui::GetItemRectMin().y), ImGui::GetColorU32(ImGuiCol_Separator));
}
// Numeric columns align on the least significant digit; addresses retain
// the emulator's familiar hexadecimal notation and monospace typography.
inline void ATUIExplorerNumber(const char *format, unsigned value) {
	char text[40]; snprintf(text, sizeof text, format, value);
	const float width = ImGui::CalcTextSize(text).x;
	ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, ImGui::GetContentRegionAvail().x - width - 4));
	ImGui::TextUnformatted(text);
}
inline void ATUIExplorerSource(const std::string& source) {
	ImGui::TextDisabled("%s", source.c_str());
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", source.c_str());
}
struct ATUIExplorerSelection {
	std::vector<bool> items;
	int anchor = 0;
	void Resize(int n) { if ((int)items.size() != n) { items.assign(n, false); if (n) items[0] = true; anchor = 0; } }
	void Select(int i, bool context = false) {
		if (context && items[i]) return;
		const auto& io = ImGui::GetIO();
		if (!context && io.KeyShift) {
			std::fill(items.begin(), items.end(), false);
			for (int j = std::min(anchor, i); j <= std::max(anchor, i); ++j) items[j] = true;
		} else if (!context && (io.KeyCtrl || io.KeySuper)) { items[i] = !items[i]; anchor = i; }
		else { std::fill(items.begin(), items.end(), false); items[i] = true; anchor = i; }
	}
	int Count() const { return (int)std::count(items.begin(), items.end(), true); }
};

// Export snapshots belong to the initiating document and cannot change when
// the user selects another row. Batch export never overwrites existing files.
struct ATUIExplorerExport {
	struct File { std::string name; std::vector<uint8> bytes; };
	std::vector<File> files;
	std::string status;
	static void Complete(void *userdata, const char * const *paths, int) {
		auto& job = *static_cast<ATUIExplorerExport *>(userdata);
		if (!paths || !paths[0]) { job.files.clear(); return; }
		unsigned done = 0;
		try {
			for (const auto& item : job.files) {
				std::string path = paths[0];
				if (job.files.size() > 1) path += "/" + item.name;
				VDFile file(VDTextU8ToW(path.c_str(), -1).c_str(), nsVDFile::kWrite |
					(job.files.size() > 1 ? nsVDFile::kCreateNew : nsVDFile::kCreateAlways));
				if (!item.bytes.empty()) file.write(item.bytes.data(), (long)item.bytes.size());
				file.close();
				++done;
			}
			job.status = "Exported " + std::to_string(done) + " file(s).";
		} catch (const MyError& e) {
			job.status = "Export stopped after " + std::to_string(done) + " file(s): " + e.c_str();
		}
		job.files.clear();
	}
	void Start(ATUIExplorerDocument& doc, SDL_Window *window) {
		if (files.empty()) return;
		if (files.size() > 1) ATUIExplorerFolderDialog(doc, 'expt', Complete, this, window);
		else {
			static const SDL_DialogFileFilter filters[] = {{"All files", "*"}};
			ATUIExplorerFileDialog(doc, true, 'expt', Complete, this, window, filters, 1);
		}
	}
};
#endif
